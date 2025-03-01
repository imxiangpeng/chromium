// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/hardware_display_controller.h"

#include <drm.h>
#include <string.h>
#include <xf86drm.h>
#include <utility>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/trace_event/trace_event.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_pixmap.h"
#include "ui/gfx/swap_result.h"
#include "ui/ozone/platform/drm/gpu/crtc_controller.h"
#include "ui/ozone/platform/drm/gpu/drm_buffer.h"
#include "ui/ozone/platform/drm/gpu/drm_device.h"
#include "ui/ozone/platform/drm/gpu/page_flip_request.h"

namespace ui {

namespace {

void EmptyFlipCallback(gfx::SwapResult) {}

}  // namespace

HardwareDisplayController::HardwareDisplayController(
    std::unique_ptr<CrtcController> controller,
    const gfx::Point& origin)
    : origin_(origin), is_disabled_(controller->is_disabled()) {
  AddCrtc(std::move(controller));
}

HardwareDisplayController::~HardwareDisplayController() {
  // Reset the cursor.
  UnsetCursor();
}

bool HardwareDisplayController::Modeset(const OverlayPlane& primary,
                                        drmModeModeInfo mode) {
  TRACE_EVENT0("drm", "HDC::Modeset");
  DCHECK(primary.buffer.get());
  bool status = true;
  for (const auto& controller : crtc_controllers_)
    status &= controller->Modeset(primary, mode);

  is_disabled_ = false;

  return status;
}

bool HardwareDisplayController::Enable(const OverlayPlane& primary) {
  TRACE_EVENT0("drm", "HDC::Enable");
  DCHECK(primary.buffer.get());
  bool status = true;
  for (const auto& controller : crtc_controllers_)
    status &= controller->Modeset(primary, controller->mode());

  is_disabled_ = false;

  return status;
}

void HardwareDisplayController::Disable() {
  TRACE_EVENT0("drm", "HDC::Disable");
  for (const auto& controller : crtc_controllers_)
    controller->Disable();

  is_disabled_ = true;
}

void HardwareDisplayController::SchedulePageFlip(
    const OverlayPlaneList& plane_list,
    SwapCompletionOnceCallback callback) {
  ActualSchedulePageFlip(plane_list, false /* test_only */,
                         std::move(callback));
}

bool HardwareDisplayController::TestPageFlip(
    const OverlayPlaneList& plane_list) {
  return ActualSchedulePageFlip(plane_list, true /* test_only */,
                                base::BindOnce(&EmptyFlipCallback));
}

bool HardwareDisplayController::ActualSchedulePageFlip(
    const OverlayPlaneList& plane_list,
    bool test_only,
    SwapCompletionOnceCallback callback) {
  TRACE_EVENT0("drm", "HDC::SchedulePageFlip");

  DCHECK(!is_disabled_);

  // Ignore requests with no planes to schedule.
  if (plane_list.empty()) {
    std::move(callback).Run(gfx::SwapResult::SWAP_ACK);
    return true;
  }

  OverlayPlaneList pending_planes = plane_list;
  std::sort(pending_planes.begin(), pending_planes.end(),
            [](const OverlayPlane& l, const OverlayPlane& r) {
              return l.z_order < r.z_order;
            });
  if (pending_planes.front().z_order != 0) {
    std::move(callback).Run(gfx::SwapResult::SWAP_FAILED);
    return false;
  }
  scoped_refptr<PageFlipRequest> page_flip_request =
      new PageFlipRequest(crtc_controllers_.size(), std::move(callback));

  for (const auto& planes : owned_hardware_planes_)
    planes.first->plane_manager()->BeginFrame(planes.second.get());

  bool status = true;
  for (const auto& controller : crtc_controllers_) {
    status &= controller->SchedulePageFlip(
        owned_hardware_planes_[controller->drm().get()].get(), pending_planes,
        test_only, page_flip_request);
  }

  for (const auto& planes : owned_hardware_planes_) {
    if (!planes.first->plane_manager()->Commit(planes.second.get(),
                                               test_only)) {
      status = false;
    }
  }

  return status;
}

bool HardwareDisplayController::IsFormatSupported(uint32_t fourcc_format,
                                                  uint32_t z_order) const {
  for (size_t i = 0; i < crtc_controllers_.size(); ++i) {
    // Make sure all displays have overlay to support this format.
    if (!crtc_controllers_[i]->IsFormatSupported(fourcc_format, z_order))
      return false;
  }

  return true;
}

std::vector<uint64_t> HardwareDisplayController::GetFormatModifiers(
    uint32_t format) {
  std::vector<uint64_t> modifiers;

  if (crtc_controllers_.empty())
    return modifiers;

  modifiers = crtc_controllers_[0]->GetFormatModifiers(format);

  for (size_t i = 1; i < crtc_controllers_.size(); ++i) {
    std::vector<uint64_t> other =
        crtc_controllers_[i]->GetFormatModifiers(format);
    std::vector<uint64_t> intersection;

    std::set_intersection(modifiers.begin(), modifiers.end(), other.begin(),
                          other.end(), std::back_inserter(intersection));
    modifiers = std::move(intersection);
  }

  return modifiers;
}

bool HardwareDisplayController::SetCursor(
    const scoped_refptr<ScanoutBuffer>& buffer) {
  bool status = true;

  if (is_disabled_)
    return true;

  for (const auto& controller : crtc_controllers_)
    status &= controller->SetCursor(buffer);

  return status;
}

bool HardwareDisplayController::UnsetCursor() {
  bool status = true;
  for (const auto& controller : crtc_controllers_)
    status &= controller->SetCursor(nullptr);

  return status;
}

bool HardwareDisplayController::MoveCursor(const gfx::Point& location) {
  if (is_disabled_)
    return true;

  bool status = true;
  for (const auto& controller : crtc_controllers_)
    status &= controller->MoveCursor(location);

  return status;
}

void HardwareDisplayController::AddCrtc(
    std::unique_ptr<CrtcController> controller) {
  scoped_refptr<DrmDevice> drm = controller->drm();

  std::unique_ptr<HardwareDisplayPlaneList>& owned_planes =
      owned_hardware_planes_[drm.get()];
  if (!owned_planes)
    owned_planes.reset(new HardwareDisplayPlaneList());

  // Check if this controller owns any planes and ensure we keep track of them.
  const std::vector<std::unique_ptr<HardwareDisplayPlane>>& all_planes =
      drm->plane_manager()->planes();
  HardwareDisplayPlaneList* crtc_plane_list = owned_planes.get();
  uint32_t crtc = controller->crtc();
  for (const auto& plane : all_planes) {
    if (plane->in_use() && (plane->owning_crtc() == crtc))
      crtc_plane_list->old_plane_list.push_back(plane.get());
  }

  crtc_controllers_.push_back(std::move(controller));
}

std::unique_ptr<CrtcController> HardwareDisplayController::RemoveCrtc(
    const scoped_refptr<DrmDevice>& drm,
    uint32_t crtc) {
  for (auto it = crtc_controllers_.begin(); it != crtc_controllers_.end();
       ++it) {
    if ((*it)->drm() == drm && (*it)->crtc() == crtc) {
      std::unique_ptr<CrtcController> controller(std::move(*it));
      crtc_controllers_.erase(it);

      // Remove entry from |owned_hardware_planes_| iff no other crtcs share it.
      bool found = false;
      for (auto it = crtc_controllers_.begin(); it != crtc_controllers_.end();
           ++it) {
        if ((*it)->drm() == controller->drm()) {
          found = true;
          break;
        }
      }
      if (found) {
        std::vector<HardwareDisplayPlane*> all_planes;
        HardwareDisplayPlaneList* plane_list =
            owned_hardware_planes_[drm.get()].get();
        all_planes.swap(plane_list->old_plane_list);
        for (auto* plane : all_planes) {
          if (plane->owning_crtc() != crtc)
            plane_list->old_plane_list.push_back(plane);
        }
      } else {
        owned_hardware_planes_.erase(controller->drm().get());
      }

      return controller;
    }
  }

  return nullptr;
}

bool HardwareDisplayController::HasCrtc(const scoped_refptr<DrmDevice>& drm,
                                        uint32_t crtc) const {
  for (const auto& controller : crtc_controllers_) {
    if (controller->drm() == drm && controller->crtc() == crtc)
      return true;
  }

  return false;
}

bool HardwareDisplayController::IsMirrored() const {
  return crtc_controllers_.size() > 1;
}

bool HardwareDisplayController::IsDisabled() const {
  return is_disabled_;
}

gfx::Size HardwareDisplayController::GetModeSize() const {
  // If there are multiple CRTCs they should all have the same size.
  return gfx::Size(crtc_controllers_[0]->mode().hdisplay,
                   crtc_controllers_[0]->mode().vdisplay);
}

base::TimeTicks HardwareDisplayController::GetTimeOfLastFlip() const {
  base::TimeTicks time;
  for (const auto& controller : crtc_controllers_) {
    if (time < controller->time_of_last_flip())
      time = controller->time_of_last_flip();
  }

  return time;
}

scoped_refptr<DrmDevice> HardwareDisplayController::GetAllocationDrmDevice()
    const {
  DCHECK(!crtc_controllers_.empty());
  // TODO(dnicoara) When we support mirroring across DRM devices, figure out
  // which device should be used for allocations.
  return crtc_controllers_[0]->drm();
}

}  // namespace ui
