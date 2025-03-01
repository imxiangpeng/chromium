/*
 * Copyright (C) 2017 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "public/web/WebFrameSerializer.h"

#include "core/exported/WebViewBase.h"
#include "core/frame/FrameTestHelpers.h"
#include "core/frame/WebLocalFrameImpl.h"
#include "platform/testing/URLTestHelpers.h"
#include "platform/testing/UnitTestHelpers.h"
#include "platform/weborigin/KURL.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/Platform.h"
#include "public/platform/WebString.h"
#include "public/platform/WebURL.h"
#include "public/platform/WebURLLoaderMockFactory.h"
#include "public/platform/WebVector.h"
#include "public/web/WebFrameSerializerClient.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

namespace {

class SimpleWebFrameSerializerClient final : public WebFrameSerializerClient {
 public:
  String ToString() { return builder_.ToString(); }

 private:
  void DidSerializeDataForFrame(const WebVector<char>& data,
                                FrameSerializationStatus) final {
    builder_.Append(data.Data(), data.size());
  }

  StringBuilder builder_;
};

}  // namespace

class WebFrameSerializerTest : public ::testing::Test {
 protected:
  WebFrameSerializerTest() { helper_.Initialize(); }

  ~WebFrameSerializerTest() override {
    Platform::Current()
        ->GetURLLoaderMockFactory()
        ->UnregisterAllURLsAndClearMemoryCache();
  }

  void RegisterMockedImageURLLoad(const String& url) {
    // Image resources need to be mocked, but irrelevant here what image they
    // map to.
    RegisterMockedFileURLLoad(URLTestHelpers::ToKURL(url.Utf8().data()),
                              "frameserialization/awesome.png");
  }

  void RegisterMockedFileURLLoad(const KURL& url,
                                 const String& file_path,
                                 const String& mime_type = "image/png") {
    URLTestHelpers::RegisterMockedURLLoad(
        url, testing::CoreTestDataPath(file_path.Utf8().data()), mime_type);
  }

  class SingleLinkRewritingDelegate
      : public WebFrameSerializer::LinkRewritingDelegate {
   public:
    SingleLinkRewritingDelegate(const WebURL& url, const WebString& local_path)
        : url_(url), local_path_(local_path) {}

    bool RewriteFrameSource(WebFrame* frame,
                            WebString* rewritten_link) override {
      return false;
    }

    bool RewriteLink(const WebURL& url, WebString* rewritten_link) override {
      if (url != url_)
        return false;

      *rewritten_link = local_path_;
      return true;
    }

   private:
    const WebURL url_;
    const WebString local_path_;
  };

  String SerializeFile(const String& url, const String& file_name) {
    KURL parsed_url(kParsedURLString, url);
    String file_path("frameserialization/" + file_name);
    RegisterMockedFileURLLoad(parsed_url, file_path, "text/html");
    FrameTestHelpers::LoadFrame(MainFrameImpl(), url.Utf8().data());
    SingleLinkRewritingDelegate delegate(parsed_url, WebString("local"));
    SimpleWebFrameSerializerClient serializer_client;
    WebFrameSerializer::Serialize(MainFrameImpl(), &serializer_client,
                                  &delegate);
    return serializer_client.ToString();
  }

  WebLocalFrameImpl* MainFrameImpl() { return helper_.LocalMainFrame(); }

 private:
  FrameTestHelpers::WebViewHelper helper_;
};

TEST_F(WebFrameSerializerTest, URLAttributeValues) {
  RegisterMockedImageURLLoad("javascript:\"");

  const char* expected_html =
      "\n<!-- saved from url=(0020)http://www.test.com/ -->\n"
      "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; "
      "charset=UTF-8\">\n"
      "</head><body><img src=\"javascript:&quot;\">\n"
      "<a href=\"http://www.test.com/local#&quot;\">local</a>\n"
      "<a "
      "href=\"http://www.example.com/#&quot;&gt;&lt;script&gt;alert(0)&lt;/"
      "script&gt;\">external</a>\n"
      "</body></html>";
  String actual_html =
      SerializeFile("http://www.test.com", "url_attribute_values.html");
  EXPECT_EQ(expected_html, actual_html);
}

TEST_F(WebFrameSerializerTest, EncodingAndNormalization) {
  const char* expected_html =
      "<!DOCTYPE html>\n"
      "<!-- saved from url=(0020)http://www.test.com/ -->\n"
      "<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; "
      "charset=EUC-KR\">\n"
      "<title>Ensure NFC normalization is not performed by frame "
      "serializer</title>\n"
      "</head><body>\n"
      "\xe4\xc5\xd1\xe2\n"
      "\n</body></html>";
  String actual_html =
      SerializeFile("http://www.test.com", "encoding_normalization.html");
  EXPECT_EQ(expected_html, actual_html);
}

TEST_F(WebFrameSerializerTest, FromUrlWithMinusMinus) {
  String actual_html =
      SerializeFile("http://www.test.com?--x--", "text_only_page.html");
  EXPECT_EQ("<!-- saved from url=(0030)http://www.test.com/?-%2Dx-%2D -->",
            actual_html.Substring(1, 60));
}

}  // namespace blink
