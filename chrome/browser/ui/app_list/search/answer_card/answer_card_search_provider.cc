// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/app_list/search/answer_card/answer_card_search_provider.h"

#include <utility>

#include "base/command_line.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/user_metrics.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/app_list/search/answer_card/answer_card_result.h"
#include "chrome/browser/ui/browser_navigator.h"
#include "chrome/browser/ui/browser_navigator_params.h"
#include "components/omnibox/browser/autocomplete_input.h"
#include "components/omnibox/browser/autocomplete_match.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_status_code.h"
#include "ui/app_list/app_list_features.h"
#include "ui/app_list/app_list_model.h"
#include "ui/app_list/search_box_model.h"

namespace app_list {

namespace {

enum class SearchAnswerRequestResult {
  REQUEST_RESULT_ANOTHER_REQUEST_STARTED = 0,
  REQUEST_RESULT_REQUEST_FAILED = 1,
  REQUEST_RESULT_NO_ANSWER = 2,
  REQUEST_RESULT_RECEIVED_ANSWER = 3,
  REQUEST_RESULT_RECEIVED_ANSWER_TOO_LARGE = 4,
  REQUEST_RESULT_MAX = 5
};

constexpr char kSearchAnswerHasResult[] = "SearchAnswer-HasResult";
constexpr char kSearchAnswerIssuedQuery[] = "SearchAnswer-IssuedQuery";
constexpr char kSearchAnswerTitle[] = "SearchAnswer-Title";

void RecordRequestResult(SearchAnswerRequestResult request_result) {
  UMA_HISTOGRAM_ENUMERATION("SearchAnswer.RequestResult", request_result,
                            SearchAnswerRequestResult::REQUEST_RESULT_MAX);
}

}  // namespace

AnswerCardSearchProvider::AnswerCardSearchProvider(
    Profile* profile,
    app_list::AppListModel* model,
    AppListControllerDelegate* list_controller,
    std::unique_ptr<AnswerCardContents> contents)
    : profile_(profile),
      model_(model),
      list_controller_(list_controller),
      contents_(std::move(contents)),
      answer_server_url_(features::AnswerServerUrl()),
      template_url_service_(TemplateURLServiceFactory::GetForProfile(profile)) {
  contents_->SetDelegate(this);
}

AnswerCardSearchProvider::~AnswerCardSearchProvider() {
  RecordReceivedAnswerFinalResult();
}

void AnswerCardSearchProvider::Start(bool is_voice_query,
                                     const base::string16& query) {
  RecordReceivedAnswerFinalResult();
  // Reset the state.
  received_answer_ = false;
  OnResultAvailable(false);
  current_request_url_ = GURL();
  result_url_.empty();
  result_title_.empty();
  server_request_start_time_ = answer_loaded_time_ = base::TimeTicks();

  if (is_voice_query) {
    // No need to send a server request and show a card because launcher
    // automatically closes upon voice queries.
    return;
  }

  if (!model_->search_engine_is_google())
    return;

  if (query.empty())
    return;

  // Start a request to the answer server.
  result_url_ = GetResultUrl(query);

  // Lifetime of |prefixed_query| should be longer than the one of
  // |replacements|.
  base::string16 prefixed_query(
      base::UTF8ToUTF16("q=") + query +
      base::UTF8ToUTF16(features::AnswerServerQuerySuffix()));
  GURL::ReplacementsW replacements;
  replacements.SetQueryStr(prefixed_query);
  current_request_url_ = answer_server_url_.ReplaceComponents(replacements);
  contents_->LoadURL(current_request_url_);

  server_request_start_time_ = base::TimeTicks::Now();
}

void AnswerCardSearchProvider::UpdatePreferredSize(const gfx::Size& pref_size) {
  preferred_size_ = pref_size;
  OnResultAvailable(received_answer_ && IsCardSizeOk() &&
                    !contents_->IsLoading());
  if (!answer_loaded_time_.is_null()) {
    UMA_HISTOGRAM_TIMES("SearchAnswer.ResizeAfterLoadTime",
                        base::TimeTicks::Now() - answer_loaded_time_);
  }
}

content::WebContents* AnswerCardSearchProvider::OpenURLFromTab(
    const content::OpenURLParams& params) {
  // Open the user-clicked link in the browser taking into account the requested
  // disposition.
  chrome::NavigateParams new_tab_params(profile_, params.url,
                                        params.transition);

  new_tab_params.disposition = params.disposition;

  if (params.disposition == WindowOpenDisposition::NEW_BACKGROUND_TAB) {
    // When the user asks to open a link as a background tab, we show an
    // activated window with the new activated tab after the user closes the
    // launcher. So it's "background" relative to the launcher itself.
    new_tab_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    new_tab_params.window_action = chrome::NavigateParams::SHOW_WINDOW_INACTIVE;
  }

  chrome::Navigate(&new_tab_params);

  base::RecordAction(base::UserMetricsAction("SearchAnswer_OpenedUrl"));

  return new_tab_params.target_contents;
}

void AnswerCardSearchProvider::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->GetURL() != current_request_url_) {
    // TODO(vadimt): Remove this and similar logging once testing is complete if
    // we think this is not useful after release or happens too frequently.
    VLOG(1) << "DidFinishNavigation: Another request started";
    RecordRequestResult(
        SearchAnswerRequestResult::REQUEST_RESULT_ANOTHER_REQUEST_STARTED);
    return;
  }

  VLOG(1) << "DidFinishNavigation: Latest request completed";
  if (!navigation_handle->HasCommitted() || navigation_handle->IsErrorPage() ||
      !navigation_handle->IsInMainFrame()) {
    LOG(ERROR) << "Failed to navigate: HasCommitted="
               << navigation_handle->HasCommitted()
               << ", IsErrorPage=" << navigation_handle->IsErrorPage()
               << ", IsInMainFrame=" << navigation_handle->IsInMainFrame();
    RecordRequestResult(
        SearchAnswerRequestResult::REQUEST_RESULT_REQUEST_FAILED);
    return;
  }

  if (!features::IsAnswerCardDarkRunEnabled()) {
    if (!ParseResponseHeaders(navigation_handle->GetResponseHeaders())) {
      RecordRequestResult(SearchAnswerRequestResult::REQUEST_RESULT_NO_ANSWER);
      return;
    }
  } else {
    // In the dark run mode, every other "server response" contains a card.
    dark_run_received_answer_ = !dark_run_received_answer_;
    if (!dark_run_received_answer_)
      return;
    // SearchResult requires a non-empty id. This "url" will never be opened.
    result_url_ = "some string";
  }

  received_answer_ = true;
  UMA_HISTOGRAM_TIMES("SearchAnswer.NavigationTime",
                      base::TimeTicks::Now() - server_request_start_time_);
}

void AnswerCardSearchProvider::DidStopLoading() {
  if (!received_answer_)
    return;

  if (IsCardSizeOk())
    OnResultAvailable(true);
  answer_loaded_time_ = base::TimeTicks::Now();
  UMA_HISTOGRAM_TIMES("SearchAnswer.LoadingTime",
                      answer_loaded_time_ - server_request_start_time_);
  base::RecordAction(base::UserMetricsAction("SearchAnswer_StoppedLoading"));
}

bool AnswerCardSearchProvider::IsCardSizeOk() const {
  if (features::IsAnswerCardDarkRunEnabled())
    return true;

  if (preferred_size_.width() <= features::AnswerCardMaxWidth() &&
      preferred_size_.height() <= features::AnswerCardMaxHeight()) {
    return true;
  }

  LOG(ERROR) << "Card is too large: width=" << preferred_size_.width()
             << ", height=" << preferred_size_.height();
  return false;
}

void AnswerCardSearchProvider::RecordReceivedAnswerFinalResult() {
  // Recording whether a server response with an answer contains a card of a
  // fitting size, or a too large one. Cannot do this in DidStopLoading() or
  // UpdatePreferredSize() because this may be followed by a resizing with
  // different dimensions, so this method gets called when card's life ends.
  if (!received_answer_)
    return;

  RecordRequestResult(
      IsCardSizeOk() ? SearchAnswerRequestResult::REQUEST_RESULT_RECEIVED_ANSWER
                     : SearchAnswerRequestResult::
                           REQUEST_RESULT_RECEIVED_ANSWER_TOO_LARGE);
}

void AnswerCardSearchProvider::OnResultAvailable(bool is_available) {
  SearchProvider::Results results;
  if (is_available) {
    results.reserve(1);

    const GURL stripped_result_url = AutocompleteMatch::GURLToStrippedGURL(
        GURL(result_url_), AutocompleteInput(), template_url_service_,
        base::string16() /* keyword */);

    results.emplace_back(base::MakeUnique<AnswerCardResult>(
        profile_, list_controller_, result_url_, stripped_result_url.spec(),
        base::UTF8ToUTF16(result_title_), contents_.get()));
  }
  SwapResults(&results);
}

bool AnswerCardSearchProvider::ParseResponseHeaders(
    const net::HttpResponseHeaders* headers) {
  if (!headers) {
    LOG(ERROR) << "Failed to parse response headers: no headers";
    return false;
  }
  if (headers->response_code() != net::HTTP_OK) {
    LOG(ERROR) << "Failed to parse response headers: response code="
               << headers->response_code();
    return false;
  }
  if (!headers->HasHeaderValue(kSearchAnswerHasResult, "true")) {
    LOG(ERROR) << "Failed to parse response headers: " << kSearchAnswerHasResult
               << " header != true";
    return false;
  }
  if (!headers->GetNormalizedHeader(kSearchAnswerTitle, &result_title_)) {
    LOG(ERROR) << "Failed to parse response headers: " << kSearchAnswerTitle
               << " header is not present";
    return false;
  }

  std::string issued_query;
  // TODO(749320): Make the header mandatory once the production server starts
  // serving it.
  if (headers->GetNormalizedHeader(kSearchAnswerIssuedQuery, &issued_query)) {
    // The header contains the autocompleted query that corresponds to the card
    // contents. Use it for the open-URL, and for deduplication with the omnibox
    // search results.
    result_url_ = GetResultUrl(base::UTF8ToUTF16(issued_query));
  } else {
    VLOG(1) << "Warning: " << kSearchAnswerIssuedQuery
            << " header is not present";
  }

  return true;
}

std::string AnswerCardSearchProvider::GetResultUrl(
    const base::string16& query) const {
  return template_url_service_->GetDefaultSearchProvider()
      ->url_ref()
      .ReplaceSearchTerms(TemplateURLRef::SearchTermsArgs(query),
                          template_url_service_->search_terms_data());
}

}  // namespace app_list
