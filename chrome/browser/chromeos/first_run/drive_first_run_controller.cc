// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/first_run/drive_first_run_controller.h"

#include <stdint.h>
#include <utility>

#include "ash/shell.h"
#include "ash/system/tray/system_tray_delegate.h"
#include "base/callback.h"
#include "base/location.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/metrics/histogram_macros.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/background/background_contents.h"
#include "chrome/browser/background/background_contents_service.h"
#include "chrome/browser/background/background_contents_service_factory.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/extensions/chrome_extension_web_contents_observer.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/singleton_tabs.h"
#include "chrome/grit/generated_resources.h"
#include "chrome/grit/theme_resources.h"
#include "components/user_manager/user_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/notification_details.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_set.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/message_center/message_center.h"
#include "ui/message_center/notification.h"
#include "ui/message_center/notification_delegate.h"
#include "url/gurl.h"

namespace chromeos {

namespace {

// The initial time to wait in seconds before enabling offline mode.
int kInitialDelaySeconds = 180;

// Time to wait for Drive app background page to come up before giving up.
int kWebContentsTimeoutSeconds = 15;

// Google Drive enable offline endpoint.
const char kDriveOfflineEndpointUrl[] =
    "https://docs.google.com/offline/autoenable";

// Google Drive app id.
const char kDriveHostedAppId[] = "apdfllckaahabafndbhieahigkjlhalf";

// Id of the notification shown when offline mode is enabled.
const char kDriveOfflineNotificationId[] = "chrome://drive/enable-offline";

// The URL of the support page opened when the notification button is clicked.
const char kDriveOfflineSupportUrl[] =
    "https://support.google.com/drive/answer/1628467";

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// DriveOfflineNotificationDelegate

// NotificationDelegate for the notification that is displayed when Drive
// offline mode is enabled automatically. Clicking on the notification button
// will open the Drive settings page.
class DriveOfflineNotificationDelegate
    : public message_center::NotificationDelegate {
 public:
  explicit DriveOfflineNotificationDelegate(Profile* profile)
      : profile_(profile) {}

  // message_center::NotificationDelegate overrides:
  void ButtonClick(int button_index) override;

 protected:
  ~DriveOfflineNotificationDelegate() override {}

 private:
  Profile* profile_;

  DISALLOW_COPY_AND_ASSIGN(DriveOfflineNotificationDelegate);
};

void DriveOfflineNotificationDelegate::ButtonClick(int button_index) {
  DCHECK_EQ(0, button_index);

  // The support page will be localized based on the user's GAIA account.
  const GURL url = GURL(kDriveOfflineSupportUrl);

  chrome::ScopedTabbedBrowserDisplayer displayer(profile_);
  chrome::ShowSingletonTabOverwritingNTP(
      displayer.browser(),
      chrome::GetSingletonTabNavigateParams(displayer.browser(), url));
}

////////////////////////////////////////////////////////////////////////////////
// DriveWebContentsManager

// Manages web contents that initializes Google Drive offline mode. We create
// a background WebContents that loads a Drive endpoint to initialize offline
// mode. If successful, a background page will be opened to sync the user's
// files for offline use.
class DriveWebContentsManager : public content::WebContentsObserver,
                                public content::WebContentsDelegate,
                                public content::NotificationObserver {
 public:
  typedef base::Callback<
      void(bool, DriveFirstRunController::UMAOutcome)> CompletionCallback;

  DriveWebContentsManager(Profile* profile,
                          const std::string& app_id,
                          const std::string& endpoint_url,
                          const CompletionCallback& completion_callback);
  ~DriveWebContentsManager() override;

  // Start loading the WebContents for the endpoint in the context of the Drive
  // hosted app that will initialize offline mode and open a background page.
  void StartLoad();

  // Stop loading the endpoint. The |completion_callback| will not be called.
  void StopLoad();

 private:
  // Called when when offline initialization succeeds or fails and schedules
  // |RunCompletionCallback|.
  void OnOfflineInit(bool success,
                     DriveFirstRunController::UMAOutcome outcome);

  // Runs |completion_callback|.
  void RunCompletionCallback(bool success,
                             DriveFirstRunController::UMAOutcome outcome);

  // content::WebContentsObserver overrides:
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;

  void DidFailLoad(content::RenderFrameHost* render_frame_host,
                   const GURL& validated_url,
                   int error_code,
                   const base::string16& error_description) override;

  // content::WebContentsDelegate overrides:
  bool ShouldCreateWebContents(
      content::WebContents* web_contents,
      content::RenderFrameHost* opener,
      content::SiteInstance* source_site_instance,
      int32_t route_id,
      int32_t main_frame_route_id,
      int32_t main_frame_widget_route_id,
      content::mojom::WindowContainerType window_container_type,
      const GURL& opener_url,
      const std::string& frame_name,
      const GURL& target_url,
      const std::string& partition_id,
      content::SessionStorageNamespace* session_storage_namespace) override;

  // content::NotificationObserver overrides:
  void Observe(int type,
               const content::NotificationSource& source,
               const content::NotificationDetails& details) override;

  Profile* profile_;
  const std::string app_id_;
  const std::string endpoint_url_;
  std::unique_ptr<content::WebContents> web_contents_;
  content::NotificationRegistrar registrar_;
  bool started_;
  CompletionCallback completion_callback_;
  base::WeakPtrFactory<DriveWebContentsManager> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DriveWebContentsManager);
};

DriveWebContentsManager::DriveWebContentsManager(
    Profile* profile,
    const std::string& app_id,
    const std::string& endpoint_url,
    const CompletionCallback& completion_callback)
    : profile_(profile),
      app_id_(app_id),
      endpoint_url_(endpoint_url),
      started_(false),
      completion_callback_(completion_callback),
      weak_ptr_factory_(this) {
  DCHECK(!completion_callback_.is_null());
  registrar_.Add(this, chrome::NOTIFICATION_BACKGROUND_CONTENTS_OPENED,
                 content::Source<Profile>(profile_));
}

DriveWebContentsManager::~DriveWebContentsManager() {
}

void DriveWebContentsManager::StartLoad() {
  started_ = true;
  const GURL url(endpoint_url_);
  content::WebContents::CreateParams create_params(
        profile_, content::SiteInstance::CreateForURL(profile_, url));

  web_contents_.reset(content::WebContents::Create(create_params));
  web_contents_->SetDelegate(this);
  extensions::ChromeExtensionWebContentsObserver::CreateForWebContents(
      web_contents_.get());

  content::NavigationController::LoadURLParams load_params(url);
  load_params.transition_type = ui::PAGE_TRANSITION_GENERATED;
  web_contents_->GetController().LoadURLWithParams(load_params);

  content::WebContentsObserver::Observe(web_contents_.get());
}

void DriveWebContentsManager::StopLoad() {
  started_ = false;
}

void DriveWebContentsManager::OnOfflineInit(
    bool success,
    DriveFirstRunController::UMAOutcome outcome) {
  if (started_) {
    // We postpone notifying the controller as we may be in the middle
    // of a call stack for some routine of the contained WebContents.
    base::ThreadTaskRunnerHandle::Get()->PostTask(
        FROM_HERE,
        base::Bind(&DriveWebContentsManager::RunCompletionCallback,
                   weak_ptr_factory_.GetWeakPtr(), success, outcome));
    StopLoad();
  }
}

void DriveWebContentsManager::RunCompletionCallback(
    bool success,
    DriveFirstRunController::UMAOutcome outcome) {
  completion_callback_.Run(success, outcome);
}

void DriveWebContentsManager::DidFinishNavigation(
    content::NavigationHandle* navigation_handle) {
  if (navigation_handle->IsInMainFrame() && navigation_handle->IsErrorPage()) {
    LOG(WARNING) << "Failed to load WebContents to enable offline mode.";
    OnOfflineInit(false,
                  DriveFirstRunController::OUTCOME_WEB_CONTENTS_LOAD_FAILED);
  }
}

void DriveWebContentsManager::DidFailLoad(
    content::RenderFrameHost* render_frame_host,
    const GURL& validated_url,
    int error_code,
    const base::string16& error_description) {
  if (!render_frame_host->GetParent()) {
    LOG(WARNING) << "Failed to load WebContents to enable offline mode.";
    OnOfflineInit(false,
                  DriveFirstRunController::OUTCOME_WEB_CONTENTS_LOAD_FAILED);
  }
}

bool DriveWebContentsManager::ShouldCreateWebContents(
    content::WebContents* web_contents,
    content::RenderFrameHost* opener,
    content::SiteInstance* source_site_instance,
    int32_t route_id,
    int32_t main_frame_route_id,
    int32_t main_frame_widget_route_id,
    content::mojom::WindowContainerType window_container_type,
    const GURL& opener_url,
    const std::string& frame_name,
    const GURL& target_url,
    const std::string& partition_id,
    content::SessionStorageNamespace* session_storage_namespace) {
  if (window_container_type == content::mojom::WindowContainerType::NORMAL)
    return true;

  // Check that the target URL is for the Drive app.
  const extensions::Extension* extension =
      extensions::ExtensionRegistry::Get(profile_)
          ->enabled_extensions().GetAppByURL(target_url);
  if (!extension || extension->id() != app_id_)
    return true;

  // The background contents creation is normally done in Browser, but
  // because we're using a detached WebContents, we need to do it ourselves.
  BackgroundContentsService* background_contents_service =
      BackgroundContentsServiceFactory::GetForProfile(profile_);

  // Prevent redirection if background contents already exists.
  if (background_contents_service->GetAppBackgroundContents(
      base::UTF8ToUTF16(app_id_))) {
    return false;
  }
  // drive_first_run/app/manifest.json sets allow_js_access to false and
  // therefore we are creating a new SiteInstance (and thus a new renderer
  // process) here, so we must use MSG_ROUTING_NONE and we cannot pass the
  // opener (similarily to how allow_js_access:false is handled in
  // Browser::MaybeCreateBackgroundContents).
  BackgroundContents* contents =
      background_contents_service->CreateBackgroundContents(
          content::SiteInstance::Create(profile_), nullptr, MSG_ROUTING_NONE,
          MSG_ROUTING_NONE, MSG_ROUTING_NONE, profile_, frame_name,
          base::ASCIIToUTF16(app_id_), partition_id, session_storage_namespace);

  contents->web_contents()->GetController().LoadURL(
      target_url,
      content::Referrer(),
      ui::PAGE_TRANSITION_LINK,
      std::string());

  // Return false as we already created the WebContents here.
  return false;
}

void DriveWebContentsManager::Observe(
    int type,
    const content::NotificationSource& source,
    const content::NotificationDetails& details) {
  DCHECK_EQ(chrome::NOTIFICATION_BACKGROUND_CONTENTS_OPENED, type);
  const std::string app_id = base::UTF16ToUTF8(
      content::Details<BackgroundContentsOpenedDetails>(details)
          ->application_id);
  if (app_id == app_id_)
    OnOfflineInit(true, DriveFirstRunController::OUTCOME_OFFLINE_ENABLED);
}

////////////////////////////////////////////////////////////////////////////////
// DriveFirstRunController

DriveFirstRunController::DriveFirstRunController(Profile* profile)
    : profile_(profile),
      started_(false),
      initial_delay_secs_(kInitialDelaySeconds),
      web_contents_timeout_secs_(kWebContentsTimeoutSeconds),
      drive_offline_endpoint_url_(kDriveOfflineEndpointUrl),
      drive_hosted_app_id_(kDriveHostedAppId) {
}

DriveFirstRunController::~DriveFirstRunController() {
}

void DriveFirstRunController::EnableOfflineMode() {
  if (!started_) {
    started_ = true;
    initial_delay_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromSeconds(initial_delay_secs_),
      this,
      &DriveFirstRunController::EnableOfflineMode);
    return;
  }

  if (!user_manager::UserManager::Get()->IsLoggedInAsUserWithGaiaAccount()) {
    LOG(ERROR) << "Attempting to enable offline access "
                  "but not logged in a regular user.";
    OnOfflineInit(false, OUTCOME_WRONG_USER_TYPE);
    return;
  }

  ExtensionService* extension_service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  if (!extension_service->GetExtensionById(drive_hosted_app_id_, false)) {
    LOG(WARNING) << "Drive app is not installed.";
    OnOfflineInit(false, OUTCOME_APP_NOT_INSTALLED);
    return;
  }

  BackgroundContentsService* background_contents_service =
      BackgroundContentsServiceFactory::GetForProfile(profile_);
  if (background_contents_service->GetAppBackgroundContents(
      base::UTF8ToUTF16(drive_hosted_app_id_))) {
    LOG(WARNING) << "Background page for Drive app already exists";
    OnOfflineInit(false, OUTCOME_BACKGROUND_PAGE_EXISTS);
    return;
  }

  web_contents_manager_.reset(new DriveWebContentsManager(
      profile_,
      drive_hosted_app_id_,
      drive_offline_endpoint_url_,
      base::Bind(&DriveFirstRunController::OnOfflineInit,
                 base::Unretained(this))));
  web_contents_manager_->StartLoad();
  web_contents_timer_.Start(
      FROM_HERE,
      base::TimeDelta::FromSeconds(web_contents_timeout_secs_),
      this,
      &DriveFirstRunController::OnWebContentsTimedOut);
}

void DriveFirstRunController::AddObserver(Observer* observer) {
  observer_list_.AddObserver(observer);
}

void DriveFirstRunController::RemoveObserver(Observer* observer) {
  observer_list_.RemoveObserver(observer);
}

void DriveFirstRunController::SetDelaysForTest(int initial_delay_secs,
                                               int timeout_secs) {
  DCHECK(!started_);
  initial_delay_secs_ = initial_delay_secs;
  web_contents_timeout_secs_ = timeout_secs;
}

void DriveFirstRunController::SetAppInfoForTest(
    const std::string& app_id,
    const std::string& endpoint_url) {
  DCHECK(!started_);
  drive_hosted_app_id_ = app_id;
  drive_offline_endpoint_url_ = endpoint_url;
}

void DriveFirstRunController::OnWebContentsTimedOut() {
  LOG(WARNING) << "Timed out waiting for web contents.";
  for (auto& observer : observer_list_)
    observer.OnTimedOut();
  OnOfflineInit(false, OUTCOME_WEB_CONTENTS_TIMED_OUT);
}

void DriveFirstRunController::CleanUp() {
  if (web_contents_manager_)
    web_contents_manager_->StopLoad();
  web_contents_timer_.Stop();
  base::ThreadTaskRunnerHandle::Get()->DeleteSoon(FROM_HERE, this);
}

void DriveFirstRunController::OnOfflineInit(bool success, UMAOutcome outcome) {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  if (success)
    ShowNotification();
  UMA_HISTOGRAM_ENUMERATION("DriveOffline.CrosAutoEnableOutcome",
                            outcome, OUTCOME_MAX);
  for (auto& observer : observer_list_)
    observer.OnCompletion(success);
  CleanUp();
}

void DriveFirstRunController::ShowNotification() {
  ExtensionService* service =
      extensions::ExtensionSystem::Get(profile_)->extension_service();
  DCHECK(service);
  const extensions::Extension* extension =
      service->GetExtensionById(drive_hosted_app_id_, false);
  DCHECK(extension);

  message_center::RichNotificationData data;
  data.buttons.push_back(message_center::ButtonInfo(
      l10n_util::GetStringUTF16(IDS_DRIVE_OFFLINE_NOTIFICATION_BUTTON)));
  ui::ResourceBundle& resource_bundle = ui::ResourceBundle::GetSharedInstance();
  std::unique_ptr<message_center::Notification> notification(
      new message_center::Notification(
          message_center::NOTIFICATION_TYPE_SIMPLE, kDriveOfflineNotificationId,
          base::string16(),  // title
          l10n_util::GetStringUTF16(IDS_DRIVE_OFFLINE_NOTIFICATION_MESSAGE),
          resource_bundle.GetImageNamed(IDR_NOTIFICATION_DRIVE),
          base::UTF8ToUTF16(extension->name()), GURL(),
          message_center::NotifierId(message_center::NotifierId::APPLICATION,
                                     kDriveHostedAppId),
          data, new DriveOfflineNotificationDelegate(profile_)));
  notification->set_priority(message_center::LOW_PRIORITY);
  message_center::MessageCenter::Get()->AddNotification(
      std::move(notification));
}

}  // namespace chromeos
