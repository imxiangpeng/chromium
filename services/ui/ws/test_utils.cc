// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/test_utils.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/strings/string_number_conversions.h"
#include "components/viz/common/quads/copy_output_request.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "services/service_manager/public/interfaces/connector.mojom.h"
#include "services/ui/common/image_cursors_set.h"
#include "services/ui/public/interfaces/cursor/cursor.mojom.h"
#include "services/ui/ws/display_binding.h"
#include "services/ui/ws/display_creation_config.h"
#include "services/ui/ws/display_manager.h"
#include "services/ui/ws/frame_sink_manager_client_binding.h"
#include "services/ui/ws/gpu_host.h"
#include "services/ui/ws/threaded_image_cursors.h"
#include "services/ui/ws/threaded_image_cursors_factory.h"
#include "services/ui/ws/window_manager_access_policy.h"
#include "services/ui/ws/window_manager_window_tree_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/cursor/cursor.h"
#include "ui/gfx/geometry/dip_util.h"

namespace ui {
namespace ws {
namespace test {
namespace {

ClientWindowId NextUnusedClientWindowId(WindowTree* tree) {
  ClientWindowId client_id;
  for (ClientSpecificId id = 1;; ++id) {
    // Used the id of the client in the upper bits to simplify things.
    const ClientWindowId client_id =
        ClientWindowId(WindowIdToTransportId(WindowId(tree->id(), id)));
    if (!tree->GetWindowByClientId(client_id))
      return client_id;
  }
}

display::ViewportMetrics MakeViewportMetrics(const display::Display& display) {
  gfx::Size pixel_size = gfx::ConvertSizeToPixel(display.device_scale_factor(),
                                                 display.bounds().size());

  display::ViewportMetrics metrics;
  metrics.bounds_in_pixels.set_size(pixel_size);
  metrics.device_scale_factor = display.device_scale_factor();
  return metrics;
}

class TestThreadedImageCursorsFactory : public ThreadedImageCursorsFactory {
 public:
  TestThreadedImageCursorsFactory() {}
  ~TestThreadedImageCursorsFactory() override {}

  // ThreadedImageCursorsFactory:
  std::unique_ptr<ThreadedImageCursors> CreateCursors() override {
    if (!resource_runner_) {
      resource_runner_ = base::ThreadTaskRunnerHandle::Get();
      image_cursors_set_ = base::MakeUnique<ui::ImageCursorsSet>();
    }
    return base::MakeUnique<ws::ThreadedImageCursors>(
        resource_runner_, image_cursors_set_->GetWeakPtr());
  }

 private:
  scoped_refptr<base::SingleThreadTaskRunner> resource_runner_;
  std::unique_ptr<ui::ImageCursorsSet> image_cursors_set_;

  DISALLOW_COPY_AND_ASSIGN(TestThreadedImageCursorsFactory);
};

}  // namespace

// TestScreenManager  -------------------------------------------------

TestScreenManager::TestScreenManager() {}

TestScreenManager::~TestScreenManager() {
  display::Screen::SetScreenInstance(nullptr);
}

int64_t TestScreenManager::AddDisplay() {
  return AddDisplay(
      display::Display(display::kInvalidDisplayId, gfx::Rect(100, 100)));
}

int64_t TestScreenManager::AddDisplay(const display::Display& input_display) {
  // Generate a unique display id.
  int64_t display_id = display_ids_.empty() ? 1 : *display_ids_.rbegin() + 1;
  display_ids_.insert(display_id);

  display::Display display = input_display;
  display.set_id(display_id);

  // First display added will be the primary display.
  display::DisplayList::Type type = display::DisplayList::Type::NOT_PRIMARY;
  if (display_ids_.size() == 1)
    type = display::DisplayList::Type::PRIMARY;

  screen_->display_list().AddDisplay(display, type);
  delegate_->OnDisplayAdded(display, MakeViewportMetrics(display));

  if (type == display::DisplayList::Type::PRIMARY)
    delegate_->OnPrimaryDisplayChanged(display_id);

  return display_id;
}

void TestScreenManager::ModifyDisplay(const display::Display& display) {
  DCHECK(display_ids_.count(display.id()) == 1);
  screen_->display_list().UpdateDisplay(display);
  delegate_->OnDisplayModified(display, MakeViewportMetrics(display));
}

void TestScreenManager::RemoveDisplay(int64_t display_id) {
  DCHECK(display_ids_.count(display_id) == 1);
  screen_->display_list().RemoveDisplay(display_id);
  delegate_->OnDisplayRemoved(display_id);
  display_ids_.erase(display_id);
}

void TestScreenManager::Init(display::ScreenManagerDelegate* delegate) {
  delegate_ = delegate;

  // Reset everything.
  display_ids_.clear();
  display::Screen::SetScreenInstance(nullptr);
  screen_ = base::MakeUnique<display::ScreenBase>();
  display::Screen::SetScreenInstance(screen_.get());
}

display::ScreenBase* TestScreenManager::GetScreen() {
  return screen_.get();
}

// TestPlatformDisplayFactory  -------------------------------------------------

TestPlatformDisplayFactory::TestPlatformDisplayFactory(
    ui::CursorData* cursor_storage)
    : cursor_storage_(cursor_storage) {}

TestPlatformDisplayFactory::~TestPlatformDisplayFactory() {}

std::unique_ptr<PlatformDisplay>
TestPlatformDisplayFactory::CreatePlatformDisplay(
    ServerWindow* root_window,
    const display::ViewportMetrics& metrics) {
  return base::MakeUnique<TestPlatformDisplay>(metrics, cursor_storage_);
}

// WindowTreeTestApi  ---------------------------------------------------------

WindowTreeTestApi::WindowTreeTestApi(WindowTree* tree) : tree_(tree) {}
WindowTreeTestApi::~WindowTreeTestApi() {}

void WindowTreeTestApi::StartPointerWatcher(bool want_moves) {
  tree_->StartPointerWatcher(want_moves);
}

void WindowTreeTestApi::StopPointerWatcher() {
  tree_->StopPointerWatcher();
}

// DisplayTestApi  ------------------------------------------------------------

DisplayTestApi::DisplayTestApi(Display* display) : display_(display) {}
DisplayTestApi::~DisplayTestApi() {}

// EventDispatcherTestApi  ----------------------------------------------------

bool EventDispatcherTestApi::IsWindowPointerTarget(
    const ServerWindow* window) const {
  for (const auto& pair : ed_->pointer_targets_) {
    if (pair.second.window == window)
      return true;
  }
  return false;
}

int EventDispatcherTestApi::NumberPointerTargetsForWindow(
    ServerWindow* window) {
  int count = 0;
  for (const auto& pair : ed_->pointer_targets_)
    if (pair.second.window == window)
      count++;
  return count;
}

// TestDisplayBinding ---------------------------------------------------------

WindowTree* TestDisplayBinding::CreateWindowTree(ServerWindow* root) {
  const uint32_t embed_flags = 0;
  WindowTree* tree = window_server_->EmbedAtWindow(
      root, service_manager::mojom::kRootUserID,
      ui::mojom::WindowTreeClientPtr(), embed_flags,
      base::WrapUnique(new WindowManagerAccessPolicy));
  tree->ConfigureWindowManager(automatically_create_display_roots_);
  return tree;
}

// TestWindowManager ----------------------------------------------------------

TestWindowManager::TestWindowManager() {}

TestWindowManager::~TestWindowManager() {}

void TestWindowManager::OnConnect(uint16_t client_id) {
  connect_count_++;
}

void TestWindowManager::WmNewDisplayAdded(
    const display::Display& display,
    ui::mojom::WindowDataPtr root,
    bool drawn,
    const base::Optional<viz::LocalSurfaceId>& local_surface_id) {
  display_added_count_++;
}

void TestWindowManager::WmDisplayRemoved(int64_t display_id) {
  got_display_removed_ = true;
  display_removed_id_ = display_id;
}

void TestWindowManager::WmSetModalType(uint32_t window_id, ui::ModalType type) {
  on_set_modal_type_called_ = true;
}

void TestWindowManager::WmCreateTopLevelWindow(
    uint32_t change_id,
    ClientSpecificId requesting_client_id,
    const std::unordered_map<std::string, std::vector<uint8_t>>& properties) {
  got_create_top_level_window_ = true;
  change_id_ = change_id;
}

void TestWindowManager::WmClientJankinessChanged(ClientSpecificId client_id,
                                                 bool janky) {}

void TestWindowManager::WmBuildDragImage(const gfx::Point& screen_location,
                                         const SkBitmap& drag_image,
                                         const gfx::Vector2d& drag_image_offset,
                                         ui::mojom::PointerKind source) {}

void TestWindowManager::WmMoveDragImage(
    const gfx::Point& screen_location,
    const WmMoveDragImageCallback& callback) {
  callback.Run();
}

void TestWindowManager::WmDestroyDragImage() {}

void TestWindowManager::WmPerformMoveLoop(uint32_t change_id,
                                          uint32_t window_id,
                                          mojom::MoveLoopSource source,
                                          const gfx::Point& cursor_location) {
  on_perform_move_loop_called_ = true;
}

void TestWindowManager::WmCancelMoveLoop(uint32_t window_id) {}

void TestWindowManager::WmDeactivateWindow(uint32_t window_id) {}

void TestWindowManager::WmStackAbove(uint32_t change_id,
                                     uint32_t above_id,
                                     uint32_t below_id) {}

void TestWindowManager::WmStackAtTop(uint32_t change_id, uint32_t window_id) {}

void TestWindowManager::OnAccelerator(uint32_t ack_id,
                                      uint32_t accelerator_id,
                                      std::unique_ptr<ui::Event> event) {
  on_accelerator_called_ = true;
  on_accelerator_id_ = accelerator_id;
}

void TestWindowManager::OnCursorTouchVisibleChanged(bool enabled) {}

// TestWindowTreeClient -------------------------------------------------------

TestWindowTreeClient::TestWindowTreeClient()
    : binding_(this), record_on_change_completed_(false) {}
TestWindowTreeClient::~TestWindowTreeClient() {}

void TestWindowTreeClient::Bind(
    mojo::InterfaceRequest<mojom::WindowTreeClient> request) {
  binding_.Bind(std::move(request));
}

void TestWindowTreeClient::OnEmbed(
    uint16_t client_id,
    mojom::WindowDataPtr root,
    ui::mojom::WindowTreePtr tree,
    int64_t display_id,
    Id focused_window_id,
    bool drawn,
    const base::Optional<viz::LocalSurfaceId>& local_surface_id) {
  // TODO(sky): add test coverage of |focused_window_id|.
  tracker_.OnEmbed(client_id, std::move(root), drawn);
}

void TestWindowTreeClient::OnEmbeddedAppDisconnected(uint32_t window) {
  tracker_.OnEmbeddedAppDisconnected(window);
}

void TestWindowTreeClient::OnUnembed(Id window_id) {
  tracker_.OnUnembed(window_id);
}

void TestWindowTreeClient::OnCaptureChanged(Id new_capture_window_id,
                                            Id old_capture_window_id) {
  tracker_.OnCaptureChanged(new_capture_window_id, old_capture_window_id);
}

void TestWindowTreeClient::OnFrameSinkIdAllocated(
    Id window_id,
    const viz::FrameSinkId& frame_sink_id) {
  tracker_.OnFrameSinkIdAllocated(window_id, frame_sink_id);
}

void TestWindowTreeClient::OnTopLevelCreated(
    uint32_t change_id,
    mojom::WindowDataPtr data,
    int64_t display_id,
    bool drawn,
    const base::Optional<viz::LocalSurfaceId>& local_surface_id) {
  tracker_.OnTopLevelCreated(change_id, std::move(data), drawn);
}

void TestWindowTreeClient::OnWindowBoundsChanged(
    uint32_t window,
    const gfx::Rect& old_bounds,
    const gfx::Rect& new_bounds,
    const base::Optional<viz::LocalSurfaceId>& local_surface_id) {
  tracker_.OnWindowBoundsChanged(window, std::move(old_bounds),
                                 std::move(new_bounds), local_surface_id);
}

void TestWindowTreeClient::OnWindowTransformChanged(
    uint32_t window,
    const gfx::Transform& old_transform,
    const gfx::Transform& new_transform) {}

void TestWindowTreeClient::OnClientAreaChanged(
    uint32_t window_id,
    const gfx::Insets& new_client_area,
    const std::vector<gfx::Rect>& new_additional_client_areas) {}

void TestWindowTreeClient::OnTransientWindowAdded(
    uint32_t window_id,
    uint32_t transient_window_id) {}

void TestWindowTreeClient::OnTransientWindowRemoved(
    uint32_t window_id,
    uint32_t transient_window_id) {}

void TestWindowTreeClient::OnWindowHierarchyChanged(
    uint32_t window,
    uint32_t old_parent,
    uint32_t new_parent,
    std::vector<mojom::WindowDataPtr> windows) {
  tracker_.OnWindowHierarchyChanged(window, old_parent, new_parent,
                                    std::move(windows));
}

void TestWindowTreeClient::OnWindowReordered(uint32_t window_id,
                                             uint32_t relative_window_id,
                                             mojom::OrderDirection direction) {
  tracker_.OnWindowReordered(window_id, relative_window_id, direction);
}

void TestWindowTreeClient::OnWindowDeleted(uint32_t window) {
  tracker_.OnWindowDeleted(window);
}

void TestWindowTreeClient::OnWindowVisibilityChanged(uint32_t window,
                                                     bool visible) {
  tracker_.OnWindowVisibilityChanged(window, visible);
}

void TestWindowTreeClient::OnWindowOpacityChanged(uint32_t window,
                                                  float old_opacity,
                                                  float new_opacity) {
  tracker_.OnWindowOpacityChanged(window, new_opacity);
}

void TestWindowTreeClient::OnWindowParentDrawnStateChanged(uint32_t window,
                                                           bool drawn) {
  tracker_.OnWindowParentDrawnStateChanged(window, drawn);
}

void TestWindowTreeClient::OnWindowSharedPropertyChanged(
    uint32_t window,
    const std::string& name,
    const base::Optional<std::vector<uint8_t>>& new_data) {
  tracker_.OnWindowSharedPropertyChanged(window, name, new_data);
}

void TestWindowTreeClient::OnWindowInputEvent(uint32_t event_id,
                                              uint32_t window,
                                              int64_t display_id,
                                              std::unique_ptr<ui::Event> event,
                                              bool matches_pointer_watcher) {
  tracker_.OnWindowInputEvent(window, *event.get(), matches_pointer_watcher);
}

void TestWindowTreeClient::OnPointerEventObserved(
    std::unique_ptr<ui::Event> event,
    uint32_t window_id,
    int64_t display_id) {
  tracker_.OnPointerEventObserved(*event.get(), window_id);
}

void TestWindowTreeClient::OnWindowFocused(uint32_t focused_window_id) {
  tracker_.OnWindowFocused(focused_window_id);
}

void TestWindowTreeClient::OnWindowCursorChanged(uint32_t window_id,
                                                 ui::CursorData cursor) {
  tracker_.OnWindowCursorChanged(window_id, cursor);
}

void TestWindowTreeClient::OnWindowSurfaceChanged(
    Id window_id,
    const viz::SurfaceInfo& surface_info) {}

void TestWindowTreeClient::OnDragDropStart(
    const std::unordered_map<std::string, std::vector<uint8_t>>& mime_data) {}

void TestWindowTreeClient::OnDragEnter(uint32_t window,
                                       uint32_t key_state,
                                       const gfx::Point& position,
                                       uint32_t effect_bitmask,
                                       const OnDragEnterCallback& callback) {}

void TestWindowTreeClient::OnDragOver(uint32_t window,
                                      uint32_t key_state,
                                      const gfx::Point& position,
                                      uint32_t effect_bitmask,
                                      const OnDragOverCallback& callback) {}

void TestWindowTreeClient::OnDragLeave(uint32_t window) {}

void TestWindowTreeClient::OnCompleteDrop(
    uint32_t window,
    uint32_t key_state,
    const gfx::Point& position,
    uint32_t effect_bitmask,
    const OnCompleteDropCallback& callback) {}

void TestWindowTreeClient::OnPerformDragDropCompleted(uint32_t window,
                                                      bool success,
                                                      uint32_t action_taken) {}

void TestWindowTreeClient::OnDragDropDone() {}

void TestWindowTreeClient::OnChangeCompleted(uint32_t change_id, bool success) {
  if (record_on_change_completed_)
    tracker_.OnChangeCompleted(change_id, success);
}

void TestWindowTreeClient::RequestClose(uint32_t window_id) {}

void TestWindowTreeClient::GetWindowManager(
    mojo::AssociatedInterfaceRequest<mojom::WindowManager> internal) {}

// TestWindowTreeBinding ------------------------------------------------------

TestWindowTreeBinding::TestWindowTreeBinding(
    WindowTree* tree,
    std::unique_ptr<TestWindowTreeClient> client)
    : WindowTreeBinding(client.get()),
      tree_(tree),
      client_(std::move(client)) {}

TestWindowTreeBinding::~TestWindowTreeBinding() {}

mojom::WindowManager* TestWindowTreeBinding::GetWindowManager() {
  if (!window_manager_.get())
    window_manager_ = base::MakeUnique<TestWindowManager>();
  return window_manager_.get();
}
void TestWindowTreeBinding::SetIncomingMethodCallProcessingPaused(bool paused) {
  is_paused_ = paused;
}

mojom::WindowTreeClient* TestWindowTreeBinding::CreateClientForShutdown() {
  DCHECK(!client_after_reset_);
  client_after_reset_ = base::MakeUnique<TestWindowTreeClient>();
  return client_after_reset_.get();
}

// TestWindowServerDelegate ----------------------------------------------

TestWindowServerDelegate::TestWindowServerDelegate()
    : threaded_image_cursors_factory_(
          base::MakeUnique<TestThreadedImageCursorsFactory>()) {}
TestWindowServerDelegate::~TestWindowServerDelegate() {}

void TestWindowServerDelegate::StartDisplayInit() {}

void TestWindowServerDelegate::OnNoMoreDisplays() {
  got_on_no_more_displays_ = true;
}

std::unique_ptr<WindowTreeBinding>
TestWindowServerDelegate::CreateWindowTreeBinding(
    BindingType type,
    ws::WindowServer* window_server,
    ws::WindowTree* tree,
    mojom::WindowTreeRequest* tree_request,
    mojom::WindowTreeClientPtr* client) {
  std::unique_ptr<TestWindowTreeBinding> binding =
      base::MakeUnique<TestWindowTreeBinding>(tree);
  bindings_.push_back(binding.get());
  return std::move(binding);
}

bool TestWindowServerDelegate::IsTestConfig() const {
  return true;
}

void TestWindowServerDelegate::OnWillCreateTreeForWindowManager(
    bool automatically_create_display_roots) {
  if (window_server_->display_creation_config() !=
      DisplayCreationConfig::UNKNOWN) {
    return;
  }
  window_server_->SetDisplayCreationConfig(
      automatically_create_display_roots ? DisplayCreationConfig::AUTOMATIC
                                         : DisplayCreationConfig::MANUAL);
}

ThreadedImageCursorsFactory*
TestWindowServerDelegate::GetThreadedImageCursorsFactory() {
  return threaded_image_cursors_factory_.get();
}

// WindowServerTestHelper  ---------------------------------------------------

WindowServerTestHelper::WindowServerTestHelper()
    : cursor_(ui::CursorType::kNull), platform_display_factory_(&cursor_) {
  // Some tests create their own message loop, for example to add a task runner.
  if (!base::MessageLoop::current())
    message_loop_ = base::MakeUnique<base::MessageLoop>();
  PlatformDisplay::set_factory_for_testing(&platform_display_factory_);
  window_server_ = base::MakeUnique<WindowServer>(&window_server_delegate_);
  // TODO(staraz): Replace DefaultGpuHost and FrameSinkManagerClientBinding with
  // test implementations.
  std::unique_ptr<GpuHost> gpu_host =
      base::MakeUnique<DefaultGpuHost>(window_server_.get());
  window_server_->SetGpuHost(std::move(gpu_host));
  std::unique_ptr<FrameSinkManagerClientBinding> frame_sink_manager =
      base::MakeUnique<FrameSinkManagerClientBinding>(
          window_server_.get(), window_server_->gpu_host());
  window_server_->SetFrameSinkManager(std::move(frame_sink_manager));
  window_server_delegate_.set_window_server(window_server_.get());
}

WindowServerTestHelper::~WindowServerTestHelper() {
  // Destroy |window_server_| while the message-loop is still alive.
  window_server_.reset();
}

// WindowEventTargetingHelper ------------------------------------------------

WindowEventTargetingHelper::WindowEventTargetingHelper(
    bool automatically_create_display_roots) {
  display_ = new Display(window_server());
  display_binding_ = new TestDisplayBinding(window_server(),
                                            automatically_create_display_roots);
  display_->Init(display::ViewportMetrics(),
                 base::WrapUnique(display_binding_));
  wm_client_ = ws_test_helper_.window_server_delegate()->last_client();
  wm_client_->tracker()->changes()->clear();
}

WindowEventTargetingHelper::~WindowEventTargetingHelper() {}

ServerWindow* WindowEventTargetingHelper::CreatePrimaryTree(
    const gfx::Rect& root_window_bounds,
    const gfx::Rect& window_bounds) {
  WindowTree* wm_tree = window_server()->GetTreeWithId(1);
  const ClientWindowId embed_window_id(WindowIdToTransportId(
      WindowId(wm_tree->id(), next_primary_tree_window_id_++)));
  EXPECT_TRUE(wm_tree->NewWindow(embed_window_id, ServerWindow::Properties()));
  EXPECT_TRUE(wm_tree->SetWindowVisibility(embed_window_id, true));
  EXPECT_TRUE(wm_tree->AddWindow(FirstRootId(wm_tree), embed_window_id));
  display_->root_window()->SetBounds(root_window_bounds, base::nullopt);
  mojom::WindowTreeClientPtr client;
  ws_test_helper_.window_server_delegate()->last_client()->Bind(
      mojo::MakeRequest(&client));
  const uint32_t embed_flags = 0;
  wm_tree->Embed(embed_window_id, std::move(client), embed_flags);
  ServerWindow* embed_window = wm_tree->GetWindowByClientId(embed_window_id);
  embed_window->set_event_targeting_policy(
      mojom::EventTargetingPolicy::DESCENDANTS_ONLY);
  WindowTree* tree1 = window_server()->GetTreeWithRoot(embed_window);
  EXPECT_NE(nullptr, tree1);
  EXPECT_NE(tree1, wm_tree);
  WindowTreeTestApi(tree1).set_user_id(wm_tree->user_id());

  embed_window->SetBounds(window_bounds, base::nullopt);

  return embed_window;
}

void WindowEventTargetingHelper::CreateSecondaryTree(
    ServerWindow* embed_window,
    const gfx::Rect& window_bounds,
    TestWindowTreeClient** out_client,
    WindowTree** window_tree,
    ServerWindow** window) {
  WindowTree* tree1 = window_server()->GetTreeWithRoot(embed_window);
  ASSERT_TRUE(tree1 != nullptr);
  const ClientWindowId child1_id(
      WindowIdToTransportId(WindowId(tree1->id(), 1)));
  ASSERT_TRUE(tree1->NewWindow(child1_id, ServerWindow::Properties()));
  ServerWindow* child1 = tree1->GetWindowByClientId(child1_id);
  ASSERT_TRUE(child1);
  EXPECT_TRUE(tree1->AddWindow(ClientWindowIdForWindow(tree1, embed_window),
                               child1_id));
  tree1->GetDisplay(embed_window)->AddActivationParent(embed_window);

  child1->SetVisible(true);
  child1->SetBounds(window_bounds, base::nullopt);

  TestWindowTreeClient* embed_client =
      ws_test_helper_.window_server_delegate()->last_client();
  embed_client->tracker()->changes()->clear();
  wm_client_->tracker()->changes()->clear();

  *out_client = embed_client;
  *window_tree = tree1;
  *window = child1;
}

void WindowEventTargetingHelper::SetTaskRunner(
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  base::MessageLoop::current()->SetTaskRunner(task_runner);
}

// ----------------------------------------------------------------------------

TestDisplayManagerObserver::TestDisplayManagerObserver() : binding_(this) {}

TestDisplayManagerObserver::~TestDisplayManagerObserver() = default;

mojom::DisplayManagerObserverPtr TestDisplayManagerObserver::GetPtr() {
  mojom::DisplayManagerObserverPtr ptr;
  binding_.Bind(mojo::MakeRequest(&ptr));
  return ptr;
}

std::string TestDisplayManagerObserver::GetAndClearObserverCalls() {
  std::string result;
  std::swap(observer_calls_, result);
  return result;
}

std::string TestDisplayManagerObserver::DisplayIdsToString(
    const std::vector<mojom::WsDisplayPtr>& wm_displays) {
  std::string display_ids;
  for (const auto& wm_display : wm_displays) {
    if (!display_ids.empty())
      display_ids += " ";
    display_ids += base::Int64ToString(wm_display->display.id());
  }
  return display_ids;
}

void TestDisplayManagerObserver::OnDisplaysChanged(
    std::vector<mojom::WsDisplayPtr> displays,
    int64_t primary_display_id,
    int64_t internal_display_id) {
  if (!observer_calls_.empty())
    observer_calls_ += "\n";
  observer_calls_ += "OnDisplaysChanged " + DisplayIdsToString(displays);
  observer_calls_ += " " + base::Int64ToString(internal_display_id);
}

// -----------------------------------------------------------------------------
TestPlatformDisplay::TestPlatformDisplay(
    const display::ViewportMetrics& metrics,
    ui::CursorData* cursor_storage)
    : metrics_(metrics), cursor_storage_(cursor_storage) {}

TestPlatformDisplay::~TestPlatformDisplay() = default;

// PlatformDisplay:
void TestPlatformDisplay::Init(PlatformDisplayDelegate* delegate) {
  delegate->OnAcceleratedWidgetAvailable();
}
void TestPlatformDisplay::SetViewportSize(const gfx::Size& size) {}
void TestPlatformDisplay::SetTitle(const base::string16& title) {}
void TestPlatformDisplay::SetCapture() {}
void TestPlatformDisplay::ReleaseCapture() {}
void TestPlatformDisplay::SetCursor(const ui::CursorData& cursor) {
  *cursor_storage_ = cursor;
}
void TestPlatformDisplay::SetCursorSize(const ui::CursorSize& cursor_size) {}
void TestPlatformDisplay::MoveCursorTo(
    const gfx::Point& window_pixel_location) {}
void TestPlatformDisplay::UpdateTextInputState(
    const ui::TextInputState& state) {}
void TestPlatformDisplay::SetImeVisibility(bool visible) {}
void TestPlatformDisplay::UpdateViewportMetrics(
    const display::ViewportMetrics& metrics) {
  metrics_ = metrics;
}
gfx::AcceleratedWidget TestPlatformDisplay::GetAcceleratedWidget() const {
  return gfx::kNullAcceleratedWidget;
}
FrameGenerator* TestPlatformDisplay::GetFrameGenerator() {
  return nullptr;
}
EventSink* TestPlatformDisplay::GetEventSink() {
  return nullptr;
}
void TestPlatformDisplay::SetCursorConfig(display::Display::Rotation rotation,
                                          float scale) {
  cursor_scale_ = scale;
}

// -----------------------------------------------------------------------------

void AddWindowManager(WindowServer* window_server,
                      const UserId& user_id,
                      bool automatically_create_display_roots) {
  window_server->window_manager_window_tree_factory_set()
      ->Add(user_id, nullptr)
      ->CreateWindowTree(nullptr, nullptr, automatically_create_display_roots);
}

display::Display MakeDisplay(int origin_x,
                             int origin_y,
                             int width_pixels,
                             int height_pixels,
                             float scale_factor) {
  gfx::Size scaled_size = gfx::ConvertSizeToDIP(
      scale_factor, gfx::Size(width_pixels, height_pixels));
  gfx::Rect bounds(gfx::Point(origin_x, origin_y), scaled_size);

  display::Display display;
  display.set_bounds(bounds);
  display.set_work_area(bounds);
  display.set_device_scale_factor(scale_factor);
  return display;
}

ServerWindow* FirstRoot(WindowTree* tree) {
  return tree->roots().size() == 1u
             ? tree->GetWindow((*tree->roots().begin())->id())
             : nullptr;
}

ClientWindowId FirstRootId(WindowTree* tree) {
  ServerWindow* first_root = FirstRoot(tree);
  return first_root ? ClientWindowIdForWindow(tree, first_root)
                    : ClientWindowId();
}

ClientWindowId ClientWindowIdForWindow(WindowTree* tree,
                                       const ServerWindow* window) {
  ClientWindowId client_window_id;
  // If window isn't known we'll return 0, which should then error out.
  tree->IsWindowKnown(window, &client_window_id);
  return client_window_id;
}

ServerWindow* NewWindowInTree(WindowTree* tree, ClientWindowId* client_id) {
  return NewWindowInTreeWithParent(tree, FirstRoot(tree), client_id);
}

ServerWindow* NewWindowInTreeWithParent(WindowTree* tree,
                                        ServerWindow* parent,
                                        ClientWindowId* client_id) {
  if (!parent)
    return nullptr;
  ClientWindowId parent_client_id;
  if (!tree->IsWindowKnown(parent, &parent_client_id))
    return nullptr;
  ClientWindowId client_window_id = NextUnusedClientWindowId(tree);
  if (!tree->NewWindow(client_window_id, ServerWindow::Properties()))
    return nullptr;
  if (!tree->SetWindowVisibility(client_window_id, true))
    return nullptr;
  if (!tree->AddWindow(parent_client_id, client_window_id))
    return nullptr;
  if (client_id)
    *client_id = client_window_id;
  return tree->GetWindowByClientId(client_window_id);
}

}  // namespace test
}  // namespace ws
}  // namespace ui
