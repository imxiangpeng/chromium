// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/web_contents/web_contents_view_aura.h"

#include <stddef.h>
#include <stdint.h>

#include "base/auto_reset.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/browser/browser_plugin/browser_plugin_guest.h"
#include "content/browser/download/drag_download_util.h"
#include "content/browser/frame_host/interstitial_page_impl.h"
#include "content/browser/frame_host/navigation_entry_impl.h"
#include "content/browser/renderer_host/dip_util.h"
#include "content/browser/renderer_host/input/touch_selection_controller_client_aura.h"
#include "content/browser/renderer_host/overscroll_controller.h"
#include "content/browser/renderer_host/render_view_host_factory.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/browser/renderer_host/render_widget_host_input_event_router.h"
#include "content/browser/renderer_host/render_widget_host_view_aura.h"
#include "content/browser/web_contents/aura/gesture_nav_simple.h"
#include "content/browser/web_contents/aura/overscroll_navigation_overlay.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/notification_observer.h"
#include "content/public/browser/notification_registrar.h"
#include "content/public/browser/notification_source.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/overscroll_configuration.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_view_delegate.h"
#include "content/public/browser/web_drag_dest_delegate.h"
#include "content/public/common/child_process_host.h"
#include "content/public/common/content_client.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/drop_data.h"
#include "net/base/filename_util.h"
#include "third_party/WebKit/public/platform/WebInputEvent.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/aura/client/drag_drop_client.h"
#include "ui/aura/client/drag_drop_delegate.h"
#include "ui/aura/client/screen_position_client.h"
#include "ui/aura/client/window_parenting_client.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/aura/window_tree_host.h"
#include "ui/aura/window_tree_host_observer.h"
#include "ui/base/clipboard/clipboard.h"
#include "ui/base/clipboard/custom_data_helper.h"
#include "ui/base/dragdrop/drag_drop_types.h"
#include "ui/base/dragdrop/drop_target_event.h"
#include "ui/base/dragdrop/os_exchange_data.h"
#include "ui/base/dragdrop/os_exchange_data_provider_factory.h"
#include "ui/base/hit_test.h"
#include "ui/compositor/layer.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/blink/web_input_event.h"
#include "ui/events/event.h"
#include "ui/events/event_utils.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_png_rep.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/touch_selection/touch_selection_controller.h"

namespace content {

WebContentsView* CreateWebContentsView(
    WebContentsImpl* web_contents,
    WebContentsViewDelegate* delegate,
    RenderViewHostDelegateView** render_view_host_delegate_view) {
  WebContentsViewAura* rv = new WebContentsViewAura(web_contents, delegate);
  *render_view_host_delegate_view = rv;
  return rv;
}

namespace {

WebContentsViewAura::RenderWidgetHostViewCreateFunction
    g_create_render_widget_host_view = nullptr;

RenderWidgetHostViewAura* ToRenderWidgetHostViewAura(
    RenderWidgetHostView* view) {
  if (!view || (RenderViewHostFactory::has_factory() &&
      !RenderViewHostFactory::is_real_render_view_host())) {
    return NULL;  // Can't cast to RenderWidgetHostViewAura in unit tests.
  }

  RenderViewHost* rvh = RenderViewHost::From(view->GetRenderWidgetHost());
  WebContentsImpl* web_contents = static_cast<WebContentsImpl*>(
      rvh ? WebContents::FromRenderViewHost(rvh) : NULL);
  if (BrowserPluginGuest::IsGuest(web_contents))
    return NULL;
  return static_cast<RenderWidgetHostViewAura*>(view);
}

// Listens to all mouse drag events during a drag and drop and sends them to
// the renderer.
class WebDragSourceAura : public NotificationObserver {
 public:
  WebDragSourceAura(aura::Window* window, WebContentsImpl* contents)
      : window_(window),
        contents_(contents) {
    registrar_.Add(this,
                   NOTIFICATION_WEB_CONTENTS_DISCONNECTED,
                   Source<WebContents>(contents));
  }

  ~WebDragSourceAura() override {}

  // NotificationObserver:
  void Observe(int type,
               const NotificationSource& source,
               const NotificationDetails& details) override {
    if (type != NOTIFICATION_WEB_CONTENTS_DISCONNECTED)
      return;

    // Cancel the drag if it is still in progress.
    aura::client::DragDropClient* dnd_client =
        aura::client::GetDragDropClient(window_->GetRootWindow());
    if (dnd_client && dnd_client->IsDragDropInProgress())
      dnd_client->DragCancel();

    window_ = NULL;
    contents_ = NULL;
  }

  aura::Window* window() const { return window_; }

 private:
  aura::Window* window_;
  WebContentsImpl* contents_;
  NotificationRegistrar registrar_;

  DISALLOW_COPY_AND_ASSIGN(WebDragSourceAura);
};

#if (!defined(OS_CHROMEOS) && defined(USE_X11)) || defined(OS_WIN)
// Fill out the OSExchangeData with a file contents, synthesizing a name if
// necessary.
void PrepareDragForFileContents(const DropData& drop_data,
                                ui::OSExchangeData::Provider* provider) {
  base::Optional<base::FilePath> filename =
      drop_data.GetSafeFilenameForImageFileContents();
  if (filename)
    provider->SetFileContents(*filename, drop_data.file_contents);
}
#endif

#if defined(OS_WIN)
void PrepareDragForDownload(
    const DropData& drop_data,
    ui::OSExchangeData::Provider* provider,
    WebContentsImpl* web_contents) {
  const GURL& page_url = web_contents->GetLastCommittedURL();
  const std::string& page_encoding = web_contents->GetEncoding();

  // Parse the download metadata.
  base::string16 mime_type;
  base::FilePath file_name;
  GURL download_url;
  if (!ParseDownloadMetadata(drop_data.download_metadata,
                             &mime_type,
                             &file_name,
                             &download_url))
    return;

  // Generate the file name based on both mime type and proposed file name.
  std::string default_name =
      GetContentClient()->browser()->GetDefaultDownloadName();
  base::FilePath generated_download_file_name =
      net::GenerateFileName(download_url,
                            std::string(),
                            std::string(),
                            base::UTF16ToUTF8(file_name.value()),
                            base::UTF16ToUTF8(mime_type),
                            default_name);

  // http://crbug.com/332579
  base::ThreadRestrictions::ScopedAllowIO allow_file_operations;

  base::FilePath temp_dir_path;
  if (!base::CreateNewTempDirectory(FILE_PATH_LITERAL("chrome_drag"),
                                    &temp_dir_path))
    return;

  base::FilePath download_path =
      temp_dir_path.Append(generated_download_file_name);

  // We cannot know when the target application will be done using the temporary
  // file, so schedule it to be deleted after rebooting.
  base::DeleteFileAfterReboot(download_path);
  base::DeleteFileAfterReboot(temp_dir_path);

  // Provide the data as file (CF_HDROP). A temporary download file with the
  // Zone.Identifier ADS (Alternate Data Stream) attached will be created.
  scoped_refptr<DragDownloadFile> download_file =
      new DragDownloadFile(
          download_path,
          base::File(),
          download_url,
          Referrer(page_url, drop_data.referrer_policy),
          page_encoding,
          web_contents);
  ui::OSExchangeData::DownloadFileInfo file_download(base::FilePath(),
                                                     download_file.get());
  provider->SetDownloadFileInfo(file_download);
}
#endif  // defined(OS_WIN)

// Returns the FormatType to store file system files.
const ui::Clipboard::FormatType& GetFileSystemFileFormatType() {
  static const char kFormatString[] = "chromium/x-file-system-files";
  CR_DEFINE_STATIC_LOCAL(ui::Clipboard::FormatType,
                         format,
                         (ui::Clipboard::GetFormatType(kFormatString)));
  return format;
}

// Writes file system files to the pickle.
void WriteFileSystemFilesToPickle(
    const std::vector<DropData::FileSystemFileInfo>& file_system_files,
    base::Pickle* pickle) {
  pickle->WriteUInt32(file_system_files.size());
  for (size_t i = 0; i < file_system_files.size(); ++i) {
    pickle->WriteString(file_system_files[i].url.spec());
    pickle->WriteInt64(file_system_files[i].size);
    pickle->WriteString(file_system_files[i].filesystem_id);
  }
}

// Reads file system files from the pickle.
bool ReadFileSystemFilesFromPickle(
    const base::Pickle& pickle,
    std::vector<DropData::FileSystemFileInfo>* file_system_files) {
  base::PickleIterator iter(pickle);

  uint32_t num_files = 0;
  if (!iter.ReadUInt32(&num_files))
    return false;
  file_system_files->resize(num_files);

  for (uint32_t i = 0; i < num_files; ++i) {
    std::string url_string;
    int64_t size = 0;
    std::string filesystem_id;
    if (!iter.ReadString(&url_string) || !iter.ReadInt64(&size) ||
        !iter.ReadString(&filesystem_id)) {
      return false;
    }

    GURL url(url_string);
    if (!url.is_valid())
      return false;

    (*file_system_files)[i].url = url;
    (*file_system_files)[i].size = size;
    (*file_system_files)[i].filesystem_id = filesystem_id;
  }
  return true;
}

// Utility to fill a ui::OSExchangeDataProvider object from DropData.
void PrepareDragData(const DropData& drop_data,
                     ui::OSExchangeData::Provider* provider,
                     WebContentsImpl* web_contents) {
  provider->MarkOriginatedFromRenderer();
#if defined(OS_WIN)
  // Put download before file contents to prefer the download of a image over
  // its thumbnail link.
  if (!drop_data.download_metadata.empty())
    PrepareDragForDownload(drop_data, provider, web_contents);
#endif
#if (!defined(OS_CHROMEOS) && defined(USE_X11)) || defined(OS_WIN)
  // We set the file contents before the URL because the URL also sets file
  // contents (to a .URL shortcut).  We want to prefer file content data over
  // a shortcut so we add it first.
  if (!drop_data.file_contents.empty())
    PrepareDragForFileContents(drop_data, provider);
#endif
  // Call SetString() before SetURL() when we actually have a custom string.
  // SetURL() will itself do SetString() when a string hasn't been set yet,
  // but we want to prefer drop_data.text.string() over the URL string if it
  // exists.
  if (!drop_data.text.string().empty())
    provider->SetString(drop_data.text.string());
  if (drop_data.url.is_valid())
    provider->SetURL(drop_data.url, drop_data.url_title);
  if (!drop_data.html.string().empty())
    provider->SetHtml(drop_data.html.string(), drop_data.html_base_url);
  if (!drop_data.filenames.empty())
    provider->SetFilenames(drop_data.filenames);
  if (!drop_data.file_system_files.empty()) {
    base::Pickle pickle;
    WriteFileSystemFilesToPickle(drop_data.file_system_files, &pickle);
    provider->SetPickledData(GetFileSystemFileFormatType(), pickle);
  }
  if (!drop_data.custom_data.empty()) {
    base::Pickle pickle;
    ui::WriteCustomDataToPickle(drop_data.custom_data, &pickle);
    provider->SetPickledData(ui::Clipboard::GetWebCustomDataFormatType(),
                             pickle);
  }
}

// Utility to fill a DropData object from ui::OSExchangeData.
void PrepareDropData(DropData* drop_data, const ui::OSExchangeData& data) {
  drop_data->did_originate_from_renderer = data.DidOriginateFromRenderer();

  base::string16 plain_text;
  data.GetString(&plain_text);
  if (!plain_text.empty())
    drop_data->text = base::NullableString16(plain_text, false);

  GURL url;
  base::string16 url_title;
  data.GetURLAndTitle(
      ui::OSExchangeData::DO_NOT_CONVERT_FILENAMES, &url, &url_title);
  if (url.is_valid()) {
    drop_data->url = url;
    drop_data->url_title = url_title;
  }

  base::string16 html;
  GURL html_base_url;
  data.GetHtml(&html, &html_base_url);
  if (!html.empty())
    drop_data->html = base::NullableString16(html, false);
  if (html_base_url.is_valid())
    drop_data->html_base_url = html_base_url;

  data.GetFilenames(&drop_data->filenames);

  base::Pickle pickle;
  std::vector<DropData::FileSystemFileInfo> file_system_files;
  if (data.GetPickledData(GetFileSystemFileFormatType(), &pickle) &&
      ReadFileSystemFilesFromPickle(pickle, &file_system_files))
    drop_data->file_system_files = file_system_files;

  if (data.GetPickledData(ui::Clipboard::GetWebCustomDataFormatType(), &pickle))
    ui::ReadCustomDataIntoMap(
        pickle.data(), pickle.size(), &drop_data->custom_data);
}

// Utilities to convert between blink::WebDragOperationsMask and
// ui::DragDropTypes.
int ConvertFromWeb(blink::WebDragOperationsMask ops) {
  int drag_op = ui::DragDropTypes::DRAG_NONE;
  if (ops & blink::kWebDragOperationCopy)
    drag_op |= ui::DragDropTypes::DRAG_COPY;
  if (ops & blink::kWebDragOperationMove)
    drag_op |= ui::DragDropTypes::DRAG_MOVE;
  if (ops & blink::kWebDragOperationLink)
    drag_op |= ui::DragDropTypes::DRAG_LINK;
  return drag_op;
}

blink::WebDragOperationsMask ConvertToWeb(int drag_op) {
  int web_drag_op = blink::kWebDragOperationNone;
  if (drag_op & ui::DragDropTypes::DRAG_COPY)
    web_drag_op |= blink::kWebDragOperationCopy;
  if (drag_op & ui::DragDropTypes::DRAG_MOVE)
    web_drag_op |= blink::kWebDragOperationMove;
  if (drag_op & ui::DragDropTypes::DRAG_LINK)
    web_drag_op |= blink::kWebDragOperationLink;
  return (blink::WebDragOperationsMask) web_drag_op;
}

int ConvertAuraEventFlagsToWebInputEventModifiers(int aura_event_flags) {
  int web_input_event_modifiers = 0;
  if (aura_event_flags & ui::EF_SHIFT_DOWN)
    web_input_event_modifiers |= blink::WebInputEvent::kShiftKey;
  if (aura_event_flags & ui::EF_CONTROL_DOWN)
    web_input_event_modifiers |= blink::WebInputEvent::kControlKey;
  if (aura_event_flags & ui::EF_ALT_DOWN)
    web_input_event_modifiers |= blink::WebInputEvent::kAltKey;
  if (aura_event_flags & ui::EF_COMMAND_DOWN)
    web_input_event_modifiers |= blink::WebInputEvent::kMetaKey;
  if (aura_event_flags & ui::EF_LEFT_MOUSE_BUTTON)
    web_input_event_modifiers |= blink::WebInputEvent::kLeftButtonDown;
  if (aura_event_flags & ui::EF_MIDDLE_MOUSE_BUTTON)
    web_input_event_modifiers |= blink::WebInputEvent::kMiddleButtonDown;
  if (aura_event_flags & ui::EF_RIGHT_MOUSE_BUTTON)
    web_input_event_modifiers |= blink::WebInputEvent::kRightButtonDown;
  if (aura_event_flags & ui::EF_BACK_MOUSE_BUTTON)
    web_input_event_modifiers |= blink::WebInputEvent::kBackButtonDown;
  if (aura_event_flags & ui::EF_FORWARD_MOUSE_BUTTON)
    web_input_event_modifiers |= blink::WebInputEvent::kForwardButtonDown;
  return web_input_event_modifiers;
}

GlobalRoutingID GetRenderViewHostID(RenderViewHost* rvh) {
  return GlobalRoutingID(rvh->GetProcess()->GetID(), rvh->GetRoutingID());
}

}  // namespace

class WebContentsViewAura::WindowObserver
    : public aura::WindowObserver, public aura::WindowTreeHostObserver {
 public:
  explicit WindowObserver(WebContentsViewAura* view)
      : view_(view),
        host_window_(NULL) {
    view_->window_->AddObserver(this);
  }

  ~WindowObserver() override {
    view_->window_->RemoveObserver(this);
    if (view_->window_->GetHost())
      view_->window_->GetHost()->RemoveObserver(this);
    if (host_window_)
      host_window_->RemoveObserver(this);
  }

  void OnWindowParentChanged(aura::Window* window,
                             aura::Window* parent) override {
    if (window != view_->window_.get())
      return;

    aura::Window* host_window =
        window->GetProperty(aura::client::kHostWindowKey);
    if (!host_window)
      host_window = parent;

    if (host_window_)
      host_window_->RemoveObserver(this);

    host_window_ = host_window;
    if (host_window)
      host_window->AddObserver(this);
  }

  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override {
    if (window == host_window_ || window == view_->window_.get()) {
      SendScreenRects();
      if (old_bounds.origin() != new_bounds.origin()) {
        TouchSelectionControllerClientAura* selection_controller_client =
            view_->GetSelectionControllerClient();
        if (selection_controller_client)
          selection_controller_client->OnWindowMoved();
      }
    }
  }

  void OnWindowDestroying(aura::Window* window) override {
    if (window == host_window_) {
      host_window_->RemoveObserver(this);
      host_window_ = NULL;
    }
  }

  void OnWindowAddedToRootWindow(aura::Window* window) override {
    if (window == view_->window_.get())
      window->GetHost()->AddObserver(this);
  }

  void OnWindowRemovingFromRootWindow(aura::Window* window,
                                      aura::Window* new_root) override {
    if (window == view_->window_.get())
      window->GetHost()->RemoveObserver(this);
  }

  void OnWindowPropertyChanged(aura::Window* window,
                               const void* key,
                               intptr_t old) override {
    if (key != aura::client::kMirroringEnabledKey)
      return;
    if (window->GetProperty(aura::client::kMirroringEnabledKey)) {
      view_->web_contents_->IncrementCapturerCount(gfx::Size());
      view_->web_contents_->UpdateWebContentsVisibility(true);
    } else {
      view_->web_contents_->DecrementCapturerCount();
      view_->web_contents_->UpdateWebContentsVisibility(window->IsVisible());
    }
  }

  // Overridden WindowTreeHostObserver:
  void OnHostMovedInPixels(const aura::WindowTreeHost* host,
                           const gfx::Point& new_origin_in_pixels) override {
    TRACE_EVENT1("ui",
                 "WebContentsViewAura::WindowObserver::OnHostMovedInPixels",
                 "new_origin_in_pixels", new_origin_in_pixels.ToString());

    // This is for the desktop case (i.e. Aura desktop).
    SendScreenRects();
  }

 private:
  void SendScreenRects() { view_->web_contents_->SendScreenRects(); }

  WebContentsViewAura* view_;

  // The parent window that hosts the constrained windows. We cache the old host
  // view so that we can unregister when it's not the parent anymore.
  aura::Window* host_window_;

  DISALLOW_COPY_AND_ASSIGN(WindowObserver);
};

// static
void WebContentsViewAura::InstallCreateHookForTests(
    RenderWidgetHostViewCreateFunction create_render_widget_host_view) {
  CHECK_EQ(nullptr, g_create_render_widget_host_view);
  g_create_render_widget_host_view = create_render_widget_host_view;
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, public:

WebContentsViewAura::WebContentsViewAura(WebContentsImpl* web_contents,
                                         WebContentsViewDelegate* delegate)
    : web_contents_(web_contents),
      delegate_(delegate),
      current_drag_op_(blink::kWebDragOperationNone),
      drag_dest_delegate_(nullptr),
      current_rvh_for_drag_(ChildProcessHost::kInvalidUniqueID,
                            MSG_ROUTING_NONE),
      drag_start_process_id_(ChildProcessHost::kInvalidUniqueID),
      drag_start_view_id_(ChildProcessHost::kInvalidUniqueID, MSG_ROUTING_NONE),
      current_overscroll_gesture_(OVERSCROLL_NONE),
      completed_overscroll_gesture_(OVERSCROLL_NONE),
      navigation_overlay_(nullptr),
      init_rwhv_with_null_parent_for_testing_(false) {}

void WebContentsViewAura::SetDelegateForTesting(
    WebContentsViewDelegate* delegate) {
  delegate_.reset(delegate);
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, private:

WebContentsViewAura::~WebContentsViewAura() {
  if (!window_)
    return;

  window_observer_.reset();
  window_->RemoveObserver(this);

  // Window needs a valid delegate during its destructor, so we explicitly
  // delete it here.
  window_.reset();
}

void WebContentsViewAura::SizeChangedCommon(const gfx::Size& size) {
  if (web_contents_->GetInterstitialPage())
    web_contents_->GetInterstitialPage()->SetSize(size);
  RenderWidgetHostView* rwhv =
      web_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->SetSize(size);
}

void WebContentsViewAura::EndDrag(RenderWidgetHost* source_rwh,
                                  blink::WebDragOperationsMask ops) {
  drag_start_process_id_ = ChildProcessHost::kInvalidUniqueID;
  drag_start_view_id_ = GlobalRoutingID(ChildProcessHost::kInvalidUniqueID,
                                        MSG_ROUTING_NONE);

  if (!web_contents_)
    return;

  aura::Window* window = GetContentNativeView();
  gfx::Point screen_loc = display::Screen::GetScreen()->GetCursorScreenPoint();
  gfx::Point client_loc = screen_loc;
  aura::client::ScreenPositionClient* screen_position_client =
      aura::client::GetScreenPositionClient(window->GetRootWindow());
  if (screen_position_client)
    screen_position_client->ConvertPointFromScreen(window, &client_loc);

  // |client_loc| and |screen_loc| are in the root coordinate space, for
  // non-root RenderWidgetHosts they need to be transformed.
  gfx::Point transformed_point = client_loc;
  gfx::Point transformed_screen_point = screen_loc;
  if (source_rwh && web_contents_->GetRenderWidgetHostView()) {
    static_cast<RenderWidgetHostViewBase*>(
        web_contents_->GetRenderWidgetHostView())
        ->TransformPointToCoordSpaceForView(
            client_loc,
            static_cast<RenderWidgetHostViewBase*>(source_rwh->GetView()),
            &transformed_point);
    static_cast<RenderWidgetHostViewBase*>(
        web_contents_->GetRenderWidgetHostView())
        ->TransformPointToCoordSpaceForView(
            screen_loc,
            static_cast<RenderWidgetHostViewBase*>(source_rwh->GetView()),
            &transformed_screen_point);
  }

  web_contents_->DragSourceEndedAt(transformed_point.x(), transformed_point.y(),
                                   transformed_screen_point.x(),
                                   transformed_screen_point.y(), ops,
                                   source_rwh);

  web_contents_->SystemDragEnded(source_rwh);
}

void WebContentsViewAura::InstallOverscrollControllerDelegate(
    RenderWidgetHostViewAura* view) {
  const std::string value = base::CommandLine::ForCurrentProcess()->
      GetSwitchValueASCII(switches::kOverscrollHistoryNavigation);
  if (value == "0") {
    navigation_overlay_.reset();
    return;
  }
  if (value == "2") {
    navigation_overlay_.reset();
    if (!gesture_nav_simple_)
      gesture_nav_simple_.reset(new GestureNavSimple(web_contents_));
    view->overscroll_controller()->set_delegate(gesture_nav_simple_.get());
    return;
  }
  view->overscroll_controller()->set_delegate(this);
  if (!navigation_overlay_) {
    navigation_overlay_.reset(
        new OverscrollNavigationOverlay(web_contents_, window_.get()));
  }
}

void WebContentsViewAura::CompleteOverscrollNavigation(OverscrollMode mode) {
  if (!web_contents_->GetRenderWidgetHostView())
    return;
  navigation_overlay_->relay_delegate()->OnOverscrollComplete(mode);
  ui::TouchSelectionController* selection_controller = GetSelectionController();
  if (selection_controller)
    selection_controller->HideAndDisallowShowingAutomatically();
}

ui::TouchSelectionController* WebContentsViewAura::GetSelectionController()
    const {
  RenderWidgetHostViewAura* view =
      ToRenderWidgetHostViewAura(web_contents_->GetRenderWidgetHostView());
  return view ? view->selection_controller() : nullptr;
}

TouchSelectionControllerClientAura*
WebContentsViewAura::GetSelectionControllerClient() const {
  RenderWidgetHostViewAura* view =
      ToRenderWidgetHostViewAura(web_contents_->GetRenderWidgetHostView());
  return view ? view->selection_controller_client() : nullptr;
}

gfx::NativeView WebContentsViewAura::GetRenderWidgetHostViewParent() const {
  if (init_rwhv_with_null_parent_for_testing_)
    return nullptr;
  return window_.get();
}

bool WebContentsViewAura::IsValidDragTarget(
    RenderWidgetHostImpl* target_rwh) const {
  return target_rwh->GetProcess()->GetID() == drag_start_process_id_ ||
      GetRenderViewHostID(web_contents_->GetRenderViewHost()) !=
      drag_start_view_id_;
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, WebContentsView implementation:

gfx::NativeView WebContentsViewAura::GetNativeView() const {
  return window_.get();
}

gfx::NativeView WebContentsViewAura::GetContentNativeView() const {
  RenderWidgetHostView* rwhv = web_contents_->GetRenderWidgetHostView();
  return rwhv ? rwhv->GetNativeView() : NULL;
}

gfx::NativeWindow WebContentsViewAura::GetTopLevelNativeWindow() const {
  gfx::NativeWindow window = window_->GetToplevelWindow();
  return window ? window : delegate_->GetNativeWindow();
}

namespace {

void GetScreenInfoForWindow(ScreenInfo* results,
                            aura::Window* window) {
  display::Screen* screen = display::Screen::GetScreen();
  const display::Display display = window
                                       ? screen->GetDisplayNearestWindow(window)
                                       : screen->GetPrimaryDisplay();
  results->rect = display.bounds();
  results->available_rect = display.work_area();
  results->depth = display.color_depth();
  results->depth_per_component = display.depth_per_component();
  results->is_monochrome = display.is_monochrome();
  results->device_scale_factor = display.device_scale_factor();
  results->color_space = display.color_space();

  // The Display rotation and the ScreenInfo orientation are not the same
  // angle. The former is the physical display rotation while the later is the
  // rotation required by the content to be shown properly on the screen, in
  // other words, relative to the physical display.
  results->orientation_angle = display.RotationAsDegree();
  if (results->orientation_angle == 90)
    results->orientation_angle = 270;
  else if (results->orientation_angle == 270)
    results->orientation_angle = 90;

  results->orientation_type =
      RenderWidgetHostViewBase::GetOrientationTypeForDesktop(display);
}

}  // namespace

// Static.
void WebContentsView::GetDefaultScreenInfo(ScreenInfo* results) {
  GetScreenInfoForWindow(results, NULL);
}

void WebContentsViewAura::GetScreenInfo(ScreenInfo* screen_info) const {
  GetScreenInfoForWindow(screen_info, window_.get());
}

void WebContentsViewAura::GetContainerBounds(gfx::Rect *out) const {
  *out = window_->GetBoundsInScreen();
}

void WebContentsViewAura::SizeContents(const gfx::Size& size) {
  gfx::Rect bounds = window_->bounds();
  if (bounds.size() != size) {
    bounds.set_size(size);
    window_->SetBounds(bounds);
  } else {
    // Our size matches what we want but the renderers size may not match.
    // Pretend we were resized so that the renderers size is updated too.
    SizeChangedCommon(size);
  }
}

void WebContentsViewAura::Focus() {
  if (web_contents_->GetInterstitialPage()) {
    web_contents_->GetInterstitialPage()->Focus();
    return;
  }

  if (delegate_.get() && delegate_->Focus())
    return;

  RenderWidgetHostView* rwhv =
      web_contents_->GetFullscreenRenderWidgetHostView();
  if (!rwhv)
    rwhv = web_contents_->GetRenderWidgetHostView();
  if (rwhv)
    rwhv->Focus();
}

void WebContentsViewAura::SetInitialFocus() {
  if (web_contents_->FocusLocationBarByDefault())
    web_contents_->SetFocusToLocationBar(false);
  else
    Focus();
}

void WebContentsViewAura::StoreFocus() {
  if (delegate_)
    delegate_->StoreFocus();
}

void WebContentsViewAura::RestoreFocus() {
  if (delegate_)
    delegate_->RestoreFocus();
}

DropData* WebContentsViewAura::GetDropData() const {
  return current_drop_data_.get();
}

gfx::Rect WebContentsViewAura::GetViewBounds() const {
  return window_->GetBoundsInScreen();
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, WebContentsView implementation:

void WebContentsViewAura::CreateView(
    const gfx::Size& initial_size, gfx::NativeView context) {
  // NOTE: we ignore |initial_size| since in some cases it's wrong (such as
  // if the bookmark bar is not shown and you create a new tab). The right
  // value is set shortly after this, so its safe to ignore.

  DCHECK(aura::Env::GetInstanceDontCreate());
  window_ = base::MakeUnique<aura::Window>(this);
  window_->set_owned_by_parent(false);
  window_->SetType(aura::client::WINDOW_TYPE_CONTROL);
  window_->SetName("WebContentsViewAura");
  window_->Init(ui::LAYER_NOT_DRAWN);
  window_->AddObserver(this);
  aura::Window* root_window = context ? context->GetRootWindow() : nullptr;
  if (root_window) {
    // There are places where there is no context currently because object
    // hierarchies are built before they're attached to a Widget. (See
    // views::WebView as an example; GetWidget() returns NULL at the point
    // where we are created.)
    //
    // It should be OK to not set a default parent since such users will
    // explicitly add this WebContentsViewAura to their tree after they create
    // us.
    aura::client::ParentWindowWithContext(window_.get(), root_window,
                                          root_window->GetBoundsInScreen());
  }
  window_->layer()->SetMasksToBounds(true);

  // WindowObserver is not interesting and is problematic for Browser Plugin
  // guests.
  // The use cases for WindowObserver do not apply to Browser Plugins:
  // 1) guests do not support NPAPI plugins.
  // 2) guests' window bounds are supposed to come from its embedder.
  if (!BrowserPluginGuest::IsGuest(web_contents_))
    window_observer_.reset(new WindowObserver(this));

  // delegate_->GetDragDestDelegate() creates a new delegate on every call.
  // Hence, we save a reference to it locally. Similar model is used on other
  // platforms as well.
  if (delegate_)
    drag_dest_delegate_ = delegate_->GetDragDestDelegate();
}

RenderWidgetHostViewBase* WebContentsViewAura::CreateViewForWidget(
    RenderWidgetHost* render_widget_host, bool is_guest_view_hack) {
  if (render_widget_host->GetView()) {
    // During testing, the view will already be set up in most cases to the
    // test view, so we don't want to clobber it with a real one. To verify that
    // this actually is happening (and somebody isn't accidentally creating the
    // view twice), we check for the RVH Factory, which will be set when we're
    // making special ones (which go along with the special views).
    DCHECK(RenderViewHostFactory::has_factory());
    return static_cast<RenderWidgetHostViewBase*>(
        render_widget_host->GetView());
  }

  RenderWidgetHostViewAura* view =
      g_create_render_widget_host_view
          ? g_create_render_widget_host_view(render_widget_host,
                                             is_guest_view_hack)
          : new RenderWidgetHostViewAura(render_widget_host,
                                         is_guest_view_hack);
  view->InitAsChild(GetRenderWidgetHostViewParent());

  RenderWidgetHostImpl* host_impl =
      RenderWidgetHostImpl::From(render_widget_host);

  if (!host_impl->is_hidden())
    view->Show();

  // We listen to drag drop events in the newly created view's window.
  aura::client::SetDragDropDelegate(view->GetNativeView(), this);

  if (view->overscroll_controller() &&
      (!web_contents_->GetDelegate() ||
       web_contents_->GetDelegate()->CanOverscrollContent())) {
    InstallOverscrollControllerDelegate(view);
  }

  return view;
}

RenderWidgetHostViewBase* WebContentsViewAura::CreateViewForPopupWidget(
    RenderWidgetHost* render_widget_host) {
  return new RenderWidgetHostViewAura(render_widget_host, false);
}

void WebContentsViewAura::SetPageTitle(const base::string16& title) {
  window_->SetTitle(title);
}

void WebContentsViewAura::RenderViewCreated(RenderViewHost* host) {
}

void WebContentsViewAura::RenderViewSwappedIn(RenderViewHost* host) {
}

void WebContentsViewAura::SetOverscrollControllerEnabled(bool enabled) {
  RenderWidgetHostViewAura* view =
      ToRenderWidgetHostViewAura(web_contents_->GetRenderWidgetHostView());
  if (view) {
    view->SetOverscrollControllerEnabled(enabled);
    if (enabled)
      InstallOverscrollControllerDelegate(view);
  }

  if (!enabled) {
    navigation_overlay_.reset();
  } else if (!navigation_overlay_) {
    navigation_overlay_.reset(
        new OverscrollNavigationOverlay(web_contents_, window_.get()));
  }
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, RenderViewHostDelegateView implementation:

void WebContentsViewAura::ShowContextMenu(RenderFrameHost* render_frame_host,
                                          const ContextMenuParams& params) {
  TouchSelectionControllerClientAura* selection_controller_client =
      GetSelectionControllerClient();
  if (selection_controller_client &&
      selection_controller_client->HandleContextMenu(params)) {
    return;
  }

  if (delegate_) {
    RenderWidgetHostViewAura* view = ToRenderWidgetHostViewAura(
        web_contents_->GetRenderWidgetHostView());
    if (view && !view->OnShowContextMenu(params))
      return;

    delegate_->ShowContextMenu(render_frame_host, params);
    // WARNING: we may have been deleted during the call to ShowContextMenu().
  }
}

void WebContentsViewAura::StartDragging(
    const DropData& drop_data,
    blink::WebDragOperationsMask operations,
    const gfx::ImageSkia& image,
    const gfx::Vector2d& image_offset,
    const DragEventSourceInfo& event_info,
    RenderWidgetHostImpl* source_rwh) {
  aura::Window* root_window = GetNativeView()->GetRootWindow();
  if (!aura::client::GetDragDropClient(root_window)) {
    web_contents_->SystemDragEnded(source_rwh);
    return;
  }

  // Grab a weak pointer to the RenderWidgetHost, since it can be destroyed
  // during the drag and drop nested run loop in StartDragAndDrop.
  // For example, the RenderWidgetHost can be deleted if a cross-process
  // transfer happens while dragging, since the RenderWidgetHost is deleted in
  // that case.
  base::WeakPtr<RenderWidgetHostImpl> source_rwh_weak_ptr =
      source_rwh->GetWeakPtr();

  drag_start_process_id_ = source_rwh->GetProcess()->GetID();
  drag_start_view_id_ = GetRenderViewHostID(web_contents_->GetRenderViewHost());

  ui::TouchSelectionController* selection_controller = GetSelectionController();
  if (selection_controller)
    selection_controller->HideAndDisallowShowingAutomatically();
  std::unique_ptr<ui::OSExchangeData::Provider> provider =
      ui::OSExchangeDataProviderFactory::CreateProvider();
  PrepareDragData(drop_data, provider.get(), web_contents_);

  ui::OSExchangeData data(
      std::move(provider));  // takes ownership of |provider|.

  if (!image.isNull())
    data.provider().SetDragImage(image, image_offset);

  std::unique_ptr<WebDragSourceAura> drag_source(
      new WebDragSourceAura(GetNativeView(), web_contents_));

  // We need to enable recursive tasks on the message loop so we can get
  // updates while in the system DoDragDrop loop.
  int result_op = 0;
  {
    gfx::NativeView content_native_view = GetContentNativeView();
    base::MessageLoop::ScopedNestableTaskAllower allow(
        base::MessageLoop::current());
    result_op = aura::client::GetDragDropClient(root_window)
        ->StartDragAndDrop(data,
                           root_window,
                           content_native_view,
                           event_info.event_location,
                           ConvertFromWeb(operations),
                           event_info.event_source);
  }

  // Bail out immediately if the contents view window is gone. Note that it is
  // not safe to access any class members in this case since |this| may already
  // be destroyed. The local variable |drag_source| will still be valid though,
  // so we can use it to determine if the window is gone.
  if (!drag_source->window()) {
    // Note that in this case, we don't need to call SystemDragEnded() since the
    // renderer is going away.
    return;
  }

  EndDrag(source_rwh_weak_ptr.get(), ConvertToWeb(result_op));
}

void WebContentsViewAura::UpdateDragCursor(blink::WebDragOperation operation) {
  current_drag_op_ = operation;
}

void WebContentsViewAura::GotFocus(RenderWidgetHostImpl* render_widget_host) {
  web_contents_->NotifyWebContentsFocused(render_widget_host);
}

void WebContentsViewAura::LostFocus(RenderWidgetHostImpl* render_widget_host) {
  web_contents_->NotifyWebContentsLostFocus(render_widget_host);
}

void WebContentsViewAura::TakeFocus(bool reverse) {
  if (web_contents_->GetDelegate() &&
      !web_contents_->GetDelegate()->TakeFocus(web_contents_, reverse) &&
      delegate_.get()) {
    delegate_->TakeFocus(reverse);
  }
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, OverscrollControllerDelegate implementation:

gfx::Size WebContentsViewAura::GetDisplaySize() const {
  RenderWidgetHostView* rwhv = web_contents_->GetRenderWidgetHostView();
  if (!rwhv)
    return gfx::Size();

  return display::Screen::GetScreen()
      ->GetDisplayNearestView(rwhv->GetNativeView())
      .size();
}

bool WebContentsViewAura::OnOverscrollUpdate(float delta_x, float delta_y) {
  if (current_overscroll_gesture_ != OVERSCROLL_EAST &&
      current_overscroll_gesture_ != OVERSCROLL_WEST) {
    return false;
  }

  return navigation_overlay_->relay_delegate()->OnOverscrollUpdate(delta_x,
                                                                   delta_y);
}

void WebContentsViewAura::OnOverscrollComplete(OverscrollMode mode) {
  CompleteOverscrollNavigation(mode);
}

void WebContentsViewAura::OnOverscrollModeChange(OverscrollMode old_mode,
                                                 OverscrollMode new_mode,
                                                 OverscrollSource source) {
  current_overscroll_gesture_ = new_mode;
  navigation_overlay_->relay_delegate()->OnOverscrollModeChange(
      old_mode, new_mode, source);
  completed_overscroll_gesture_ = OVERSCROLL_NONE;
}

base::Optional<float> WebContentsViewAura::GetMaxOverscrollDelta() const {
  return navigation_overlay_->relay_delegate()->GetMaxOverscrollDelta();
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, aura::WindowDelegate implementation:

gfx::Size WebContentsViewAura::GetMinimumSize() const {
  return gfx::Size();
}

gfx::Size WebContentsViewAura::GetMaximumSize() const {
  return gfx::Size();
}

void WebContentsViewAura::OnBoundsChanged(const gfx::Rect& old_bounds,
                                          const gfx::Rect& new_bounds) {
  SizeChangedCommon(new_bounds.size());
  if (delegate_)
    delegate_->SizeChanged(new_bounds.size());

  // Constrained web dialogs, need to be kept centered over our content area.
  for (size_t i = 0; i < window_->children().size(); i++) {
    if (window_->children()[i]->GetProperty(
            aura::client::kConstrainedWindowKey)) {
      gfx::Rect bounds = window_->children()[i]->bounds();
      bounds.set_origin(
          gfx::Point((new_bounds.width() - bounds.width()) / 2,
                     (new_bounds.height() - bounds.height()) / 2));
      window_->children()[i]->SetBounds(bounds);
    }
  }
}

gfx::NativeCursor WebContentsViewAura::GetCursor(const gfx::Point& point) {
  return gfx::kNullCursor;
}

int WebContentsViewAura::GetNonClientComponent(const gfx::Point& point) const {
  return HTCLIENT;
}

bool WebContentsViewAura::ShouldDescendIntoChildForEventHandling(
    aura::Window* child,
    const gfx::Point& location) {
  return true;
}

bool WebContentsViewAura::CanFocus() {
  // Do not take the focus if the render widget host view aura is gone or
  // is in the process of shutting down because neither the view window nor
  // this window can handle key events.
  RenderWidgetHostViewAura* view = ToRenderWidgetHostViewAura(
      web_contents_->GetRenderWidgetHostView());
  if (view != NULL && !view->IsClosing())
    return true;

  return false;
}

void WebContentsViewAura::OnCaptureLost() {
}

void WebContentsViewAura::OnPaint(const ui::PaintContext& context) {
}

void WebContentsViewAura::OnDeviceScaleFactorChanged(
    float device_scale_factor) {
}

void WebContentsViewAura::OnWindowDestroying(aura::Window* window) {
  // This means the destructor is going to be called soon. If there is an
  // overscroll gesture in progress (i.e. |overscroll_window_| is not NULL),
  // then destroying it in the WebContentsViewAura destructor can trigger other
  // virtual functions to be called (e.g. OnImplicitAnimationsCompleted()). So
  // destroy the overscroll window here.
  navigation_overlay_.reset();
}

void WebContentsViewAura::OnWindowDestroyed(aura::Window* window) {
}

void WebContentsViewAura::OnWindowTargetVisibilityChanged(bool visible) {
}

bool WebContentsViewAura::HasHitTestMask() const {
  return false;
}

void WebContentsViewAura::GetHitTestMask(gfx::Path* mask) const {
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, ui::EventHandler implementation:

void WebContentsViewAura::OnKeyEvent(ui::KeyEvent* event) {
}

void WebContentsViewAura::OnMouseEvent(ui::MouseEvent* event) {
  if (!web_contents_->GetDelegate())
    return;

  ui::EventType type = event->type();
  if (type == ui::ET_MOUSE_PRESSED) {
    // Linux window managers like to handle raise-on-click themselves.  If we
    // raise-on-click manually, this may override user settings that prevent
    // focus-stealing.
#if !defined(USE_X11) || defined (OS_CHROMEOS)
    web_contents_->GetDelegate()->ActivateContents(web_contents_);
#endif
  }

  web_contents_->GetDelegate()->ContentsMouseEvent(
      web_contents_, display::Screen::GetScreen()->GetCursorScreenPoint(),
      type == ui::ET_MOUSE_MOVED, type == ui::ET_MOUSE_EXITED);
}

////////////////////////////////////////////////////////////////////////////////
// WebContentsViewAura, aura::client::DragDropDelegate implementation:

void WebContentsViewAura::OnDragEntered(const ui::DropTargetEvent& event) {
  gfx::Point transformed_pt;
  RenderWidgetHostImpl* target_rwh =
      web_contents_->GetInputEventRouter()->GetRenderWidgetHostAtPoint(
          web_contents_->GetRenderViewHost()->GetWidget()->GetView(),
          event.location(), &transformed_pt);

  if (!IsValidDragTarget(target_rwh))
    return;

  current_rwh_for_drag_ = target_rwh->GetWeakPtr();
  current_rvh_for_drag_ =
      GetRenderViewHostID(web_contents_->GetRenderViewHost());
  current_drop_data_.reset(new DropData());
  PrepareDropData(current_drop_data_.get(), event.data());
  current_rwh_for_drag_->FilterDropData(current_drop_data_.get());

  blink::WebDragOperationsMask op = ConvertToWeb(event.source_operations());

  // Give the delegate an opportunity to cancel the drag.
  if (web_contents_->GetDelegate() &&
      !web_contents_->GetDelegate()->CanDragEnter(
          web_contents_, *current_drop_data_.get(), op)) {
    current_drop_data_.reset(NULL);
    return;
  }

  if (drag_dest_delegate_)
    drag_dest_delegate_->DragInitialize(web_contents_);

  gfx::Point screen_pt = display::Screen::GetScreen()->GetCursorScreenPoint();
  current_rwh_for_drag_->DragTargetDragEnter(
      *current_drop_data_, transformed_pt, screen_pt, op,
      ConvertAuraEventFlagsToWebInputEventModifiers(event.flags()));

  if (drag_dest_delegate_) {
    drag_dest_delegate_->OnReceiveDragData(event.data());
    drag_dest_delegate_->OnDragEnter();
  }
}

int WebContentsViewAura::OnDragUpdated(const ui::DropTargetEvent& event) {
  gfx::Point transformed_pt;
  RenderWidgetHostImpl* target_rwh =
      web_contents_->GetInputEventRouter()->GetRenderWidgetHostAtPoint(
          web_contents_->GetRenderViewHost()->GetWidget()->GetView(),
          event.location(), &transformed_pt);

  if (!IsValidDragTarget(target_rwh))
    return ui::DragDropTypes::DRAG_NONE;

  gfx::Point screen_pt = event.root_location();
  if (target_rwh != current_rwh_for_drag_.get()) {
    if (current_rwh_for_drag_) {
      gfx::Point transformed_leave_point = event.location();
      gfx::Point transformed_screen_point = screen_pt;
      static_cast<RenderWidgetHostViewBase*>(
          web_contents_->GetRenderWidgetHostView())
          ->TransformPointToCoordSpaceForView(
              event.location(), static_cast<RenderWidgetHostViewBase*>(
                                    current_rwh_for_drag_->GetView()),
              &transformed_leave_point);
      static_cast<RenderWidgetHostViewBase*>(
          web_contents_->GetRenderWidgetHostView())
          ->TransformPointToCoordSpaceForView(
              screen_pt, static_cast<RenderWidgetHostViewBase*>(
                             current_rwh_for_drag_->GetView()),
              &transformed_screen_point);
      current_rwh_for_drag_->DragTargetDragLeave(transformed_leave_point,
                                                 transformed_screen_point);
    }
    OnDragEntered(event);
  }

  if (!current_drop_data_)
    return ui::DragDropTypes::DRAG_NONE;

  blink::WebDragOperationsMask op = ConvertToWeb(event.source_operations());
  target_rwh->DragTargetDragOver(
      transformed_pt, screen_pt, op,
      ConvertAuraEventFlagsToWebInputEventModifiers(event.flags()));

  if (drag_dest_delegate_)
    drag_dest_delegate_->OnDragOver();

  return ConvertFromWeb(current_drag_op_);
}

void WebContentsViewAura::OnDragExited() {
  if (current_rvh_for_drag_ !=
      GetRenderViewHostID(web_contents_->GetRenderViewHost()) ||
      !current_drop_data_) {
    return;
  }

  if (current_rwh_for_drag_) {
    current_rwh_for_drag_->DragTargetDragLeave(gfx::Point(), gfx::Point());
    current_rwh_for_drag_.reset();
  }

  if (drag_dest_delegate_)
    drag_dest_delegate_->OnDragLeave();

  current_drop_data_.reset();
}

int WebContentsViewAura::OnPerformDrop(const ui::DropTargetEvent& event) {
  gfx::Point transformed_pt;
  RenderWidgetHostImpl* target_rwh =
      web_contents_->GetInputEventRouter()->GetRenderWidgetHostAtPoint(
          web_contents_->GetRenderViewHost()->GetWidget()->GetView(),
          event.location(), &transformed_pt);

  if (!IsValidDragTarget(target_rwh))
    return ui::DragDropTypes::DRAG_NONE;

  gfx::Point screen_pt = display::Screen::GetScreen()->GetCursorScreenPoint();
  if (target_rwh != current_rwh_for_drag_.get()) {
    if (current_rwh_for_drag_)
      current_rwh_for_drag_->DragTargetDragLeave(transformed_pt, screen_pt);
    OnDragEntered(event);
  }

  if (!current_drop_data_)
    return ui::DragDropTypes::DRAG_NONE;

  target_rwh->DragTargetDrop(
      *current_drop_data_, transformed_pt,
      display::Screen::GetScreen()->GetCursorScreenPoint(),
      ConvertAuraEventFlagsToWebInputEventModifiers(event.flags()));
  if (drag_dest_delegate_)
    drag_dest_delegate_->OnDrop();
  current_drop_data_.reset();
  return ConvertFromWeb(current_drag_op_);
}

void WebContentsViewAura::OnWindowVisibilityChanged(aura::Window* window,
                                                    bool visible) {
  // Ignore any visibility changes in the hierarchy below.
  if (window != window_.get() && window_->Contains(window))
    return;

  web_contents_->UpdateWebContentsVisibility(visible);
}

#if BUILDFLAG(USE_EXTERNAL_POPUP_MENU)
void WebContentsViewAura::ShowPopupMenu(RenderFrameHost* render_frame_host,
                                        const gfx::Rect& bounds,
                                        int item_height,
                                        double item_font_size,
                                        int selected_item,
                                        const std::vector<MenuItem>& items,
                                        bool right_aligned,
                                        bool allow_multiple_selection) {
  NOTIMPLEMENTED() << " show " << items.size() << " menu items";
}

void WebContentsViewAura::HidePopupMenu() {
  NOTIMPLEMENTED();
}
#endif

}  // namespace content
