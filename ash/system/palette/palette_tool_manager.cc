// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/system/palette/palette_tool_manager.h"

#include <algorithm>

#include "ash/resources/vector_icons/vector_icons.h"
#include "ash/system/palette/palette_tool.h"
#include "base/bind.h"
#include "base/metrics/histogram_macros.h"

namespace ash {

PaletteToolManager::PaletteToolManager(Delegate* delegate)
    : delegate_(delegate) {
  DCHECK(delegate_);
}

PaletteToolManager::~PaletteToolManager() {}

void PaletteToolManager::AddTool(std::unique_ptr<PaletteTool> tool) {
  // The same PaletteToolId cannot be registered twice.
  DCHECK_EQ(0, std::count_if(tools_.begin(), tools_.end(),
                             [&tool](const std::unique_ptr<PaletteTool>& t) {
                               return t->GetToolId() == tool->GetToolId();
                             }));

  tools_.emplace_back(std::move(tool));
}

void PaletteToolManager::ActivateTool(PaletteToolId tool_id) {
  PaletteTool* new_tool = FindToolById(tool_id);
  DCHECK(new_tool);

  PaletteTool* previous_tool = active_tools_[new_tool->GetGroup()];

  if (new_tool == previous_tool)
    return;

  if (previous_tool) {
    previous_tool->OnDisable();
    RecordPaletteModeCancellation(PaletteToolIdToPaletteModeCancelType(
        previous_tool->GetToolId(), true /*is_switched*/));
  }

  active_tools_[new_tool->GetGroup()] = new_tool;
  new_tool->OnEnable();

  delegate_->OnActiveToolChanged();
}

void PaletteToolManager::DeactivateTool(PaletteToolId tool_id) {
  PaletteTool* tool = FindToolById(tool_id);
  DCHECK(tool);

  active_tools_[tool->GetGroup()] = nullptr;
  tool->OnDisable();

  delegate_->OnActiveToolChanged();
}

bool PaletteToolManager::IsToolActive(PaletteToolId tool_id) {
  PaletteTool* tool = FindToolById(tool_id);
  DCHECK(tool);

  return active_tools_[tool->GetGroup()] == tool;
}

PaletteToolId PaletteToolManager::GetActiveTool(PaletteGroup group) {
  PaletteTool* active_tool = active_tools_[group];
  return active_tool ? active_tool->GetToolId() : PaletteToolId::NONE;
}

const gfx::VectorIcon& PaletteToolManager::GetActiveTrayIcon(
    PaletteToolId tool_id) const {
  PaletteTool* tool = FindToolById(tool_id);
  if (!tool)
    return kPaletteTrayIconDefaultIcon;

  return tool->GetActiveTrayIcon();
}

std::vector<PaletteToolView> PaletteToolManager::CreateViews() {
  std::vector<PaletteToolView> views;
  views.reserve(tools_.size());

  for (const auto& tool : tools_) {
    views::View* tool_view = tool->CreateView();
    if (!tool_view)
      continue;

    PaletteToolView view;
    view.group = tool->GetGroup();
    view.tool_id = tool->GetToolId();
    view.view = tool_view;
    views.push_back(view);
  }

  return views;
}

void PaletteToolManager::NotifyViewsDestroyed() {
  for (std::unique_ptr<PaletteTool>& tool : tools_)
    tool->OnViewDestroyed();
}

void PaletteToolManager::DisableActiveTool(PaletteGroup group) {
  PaletteToolId tool_id = GetActiveTool(group);
  if (tool_id != PaletteToolId::NONE)
    DeactivateTool(tool_id);
}

void PaletteToolManager::EnableTool(PaletteToolId tool_id) {
  ActivateTool(tool_id);
}

void PaletteToolManager::DisableTool(PaletteToolId tool_id) {
  DeactivateTool(tool_id);
}

void PaletteToolManager::HidePalette() {
  delegate_->HidePalette();
}

void PaletteToolManager::HidePaletteImmediately() {
  delegate_->HidePaletteImmediately();
}

aura::Window* PaletteToolManager::GetWindow() {
  return delegate_->GetWindow();
}

void PaletteToolManager::RecordPaletteOptionsUsage(PaletteTrayOptions option) {
  return delegate_->RecordPaletteOptionsUsage(option);
}

void PaletteToolManager::RecordPaletteModeCancellation(
    PaletteModeCancelType type) {
  return delegate_->RecordPaletteModeCancellation(type);
}

PaletteTool* PaletteToolManager::FindToolById(PaletteToolId tool_id) const {
  for (const std::unique_ptr<PaletteTool>& tool : tools_) {
    if (tool->GetToolId() == tool_id)
      return tool.get();
  }

  return nullptr;
}

}  // namespace ash
