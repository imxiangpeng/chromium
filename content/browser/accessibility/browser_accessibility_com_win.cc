// Copyright (c) 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/accessibility/browser_accessibility_com_win.h"

#include <UIAutomationClient.h>
#include <UIAutomationCoreApi.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/win/enum_variant.h"
#include "base/win/scoped_comptr.h"
#include "base/win/windows_version.h"
#include "content/browser/accessibility/browser_accessibility_event_win.h"
#include "content/browser/accessibility/browser_accessibility_manager_win.h"
#include "content/browser/accessibility/browser_accessibility_state_impl.h"
#include "content/browser/accessibility/browser_accessibility_win.h"
#include "content/common/accessibility_messages.h"
#include "content/public/common/content_client.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/accessibility/ax_modes.h"
#include "ui/accessibility/ax_role_properties.h"
#include "ui/accessibility/ax_text_utils.h"
#include "ui/base/win/accessibility_ids_win.h"
#include "ui/base/win/accessibility_misc_utils.h"
#include "ui/base/win/atl_module.h"

// There is no easy way to decouple |kScreenReader| and |kHTML| accessibility
// modes when Windows screen readers are used. For example, certain roles use
// the HTML tag name. Input fields require their type attribute to be exposed.
const uint32_t kScreenReaderAndHTMLAccessibilityModes =
    ui::AXMode::kScreenReader | ui::AXMode::kHTML;

namespace content {

using AXPlatformPositionInstance = AXPlatformPosition::AXPositionInstance;
using AXPlatformRange = ui::AXRange<AXPlatformPositionInstance::element_type>;

// These nonstandard GUIDs are taken directly from the Mozilla sources
// (accessible/src/msaa/nsAccessNodeWrap.cpp); some documentation is here:
// http://developer.mozilla.org/en/Accessibility/AT-APIs/ImplementationFeatures/MSAA
const GUID GUID_ISimpleDOM = {0x0c539790,
                              0x12e4,
                              0x11cf,
                              {0xb6, 0x61, 0x00, 0xaa, 0x00, 0x4c, 0xd6, 0xd8}};
const GUID GUID_IAccessibleContentDocument = {
    0xa5d8e1f3,
    0x3571,
    0x4d8f,
    {0x95, 0x21, 0x07, 0xed, 0x28, 0xfb, 0x07, 0x2e}};

const base::char16 BrowserAccessibilityComWin::kEmbeddedCharacter = L'\xfffc';

void AddAccessibilityModeFlags(ui::AXMode mode_flags) {
  BrowserAccessibilityStateImpl::GetInstance()->AddAccessibilityModeFlags(
      mode_flags);
}

//
// BrowserAccessibilityComWin::WinAttributes
//

BrowserAccessibilityComWin::WinAttributes::WinAttributes()
    : ia_role(0), ia_state(0), ia2_role(0), ia2_state(0) {}

BrowserAccessibilityComWin::WinAttributes::~WinAttributes() {}

//
// BrowserAccessibilityComWin
//
BrowserAccessibilityComWin::BrowserAccessibilityComWin()
    : owner_(nullptr),
      win_attributes_(new WinAttributes()),
      previous_scroll_x_(0),
      previous_scroll_y_(0) {}

BrowserAccessibilityComWin::~BrowserAccessibilityComWin() {
}

//
// IAccessible overrides:
//

STDMETHODIMP BrowserAccessibilityComWin::get_accDefaultAction(
    VARIANT var_id,
    BSTR* def_action) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_accDefaultAction(var_id, def_action);
}

//
// IAccessible2 overrides:
//

STDMETHODIMP BrowserAccessibilityComWin::get_attributes(BSTR* attributes) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IA2_GET_ATTRIBUTES);
  if (!owner())
    return E_FAIL;
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!attributes)
    return E_INVALIDARG;
  *attributes = nullptr;

  if (!owner())
    return E_FAIL;

  base::string16 str;
  for (const base::string16& attribute : ia2_attributes())
    str += attribute + L';';

  if (str.empty())
    return S_FALSE;

  *attributes = SysAllocString(str.c_str());
  DCHECK(*attributes);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_states(AccessibleStates* states) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_states(states);
}

STDMETHODIMP BrowserAccessibilityComWin::get_uniqueID(LONG* unique_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_UNIQUE_ID);
  if (!owner())
    return E_FAIL;

  if (!unique_id)
    return E_INVALIDARG;

  *unique_id = -AXPlatformNodeWin::unique_id();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_windowHandle(HWND* window_handle) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_WINDOW_HANDLE);
  if (!owner())
    return E_FAIL;

  if (!window_handle)
    return E_INVALIDARG;

  *window_handle =
      Manager()->ToBrowserAccessibilityManagerWin()->GetParentHWND();
  if (!*window_handle)
    return E_FAIL;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_indexInParent(
    LONG* index_in_parent) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_INDEX_IN_PARENT);
  if (!owner())
    return E_FAIL;

  if (!index_in_parent)
    return E_INVALIDARG;

  *index_in_parent = GetIndexInParent();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nRelations(LONG* n_relations) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_RELATIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nRelations(n_relations);
}

STDMETHODIMP BrowserAccessibilityComWin::get_relation(
    LONG relation_index,
    IAccessibleRelation** relation) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_RELATION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_relation(relation_index, relation);
}

STDMETHODIMP BrowserAccessibilityComWin::get_relations(
    LONG max_relations,
    IAccessibleRelation** relations,
    LONG* n_relations) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_RELATIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_relations(max_relations, relations,
                                          n_relations);
}

STDMETHODIMP BrowserAccessibilityComWin::scrollTo(IA2ScrollType scroll_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IA2_SCROLL_TO);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();

  if (!manager)
    return E_FAIL;

  gfx::Rect r = owner()->GetFrameBoundsRect();
  switch (scroll_type) {
    case IA2_SCROLL_TYPE_TOP_LEFT:
      manager->ScrollToMakeVisible(*owner(), gfx::Rect(r.x(), r.y(), 0, 0));
      break;
    case IA2_SCROLL_TYPE_BOTTOM_RIGHT:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.right(), r.bottom(), 0, 0));
      break;
    case IA2_SCROLL_TYPE_TOP_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.x(), r.y(), r.width(), 0));
      break;
    case IA2_SCROLL_TYPE_BOTTOM_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.x(), r.bottom(), r.width(), 0));
      break;
    case IA2_SCROLL_TYPE_LEFT_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.x(), r.y(), 0, r.height()));
      break;
    case IA2_SCROLL_TYPE_RIGHT_EDGE:
      manager->ScrollToMakeVisible(*owner(),
                                   gfx::Rect(r.right(), r.y(), 0, r.height()));
      break;
    case IA2_SCROLL_TYPE_ANYWHERE:
    default:
      manager->ScrollToMakeVisible(*owner(), r);
      break;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollToPoint(
    IA2CoordinateType coordinate_type,
    LONG x,
    LONG y) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_TO_POINT);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  gfx::Point scroll_to(x, y);

  if (coordinate_type == IA2_COORDTYPE_SCREEN_RELATIVE) {
    scroll_to -= manager->GetViewBounds().OffsetFromOrigin();
  } else if (coordinate_type == IA2_COORDTYPE_PARENT_RELATIVE) {
    if (owner()->PlatformGetParent()) {
      scroll_to +=
          owner()->PlatformGetParent()->GetFrameBoundsRect().OffsetFromOrigin();
    }
  } else {
    return E_INVALIDARG;
  }

  manager->ScrollToPoint(*owner(), scroll_to);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_groupPosition(
    LONG* group_level,
    LONG* similar_items_in_group,
    LONG* position_in_group) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_GROUP_POSITION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!group_level || !similar_items_in_group || !position_in_group)
    return E_INVALIDARG;

  *group_level = owner()->GetIntAttribute(ui::AX_ATTR_HIERARCHICAL_LEVEL);
  *similar_items_in_group = owner()->GetIntAttribute(ui::AX_ATTR_SET_SIZE);
  *position_in_group = owner()->GetIntAttribute(ui::AX_ATTR_POS_IN_SET);

  if (*group_level == *similar_items_in_group == *position_in_group == 0)
    return S_FALSE;
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_localizedExtendedRole(
    BSTR* localized_extended_role) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCALIZED_EXTENDED_ROLE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!localized_extended_role)
    return E_INVALIDARG;

  return GetStringAttributeAsBstr(ui::AX_ATTR_ROLE_DESCRIPTION,
                                  localized_extended_role);
}

//
// IAccessibleApplication methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_appName(BSTR* app_name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_APP_NAME);

  if (!app_name)
    return E_INVALIDARG;

  // GetProduct() returns a string like "Chrome/aa.bb.cc.dd", split out
  // the part before the "/".
  std::vector<std::string> product_components =
      base::SplitString(GetContentClient()->GetProduct(), "/",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  DCHECK_EQ(2U, product_components.size());
  if (product_components.size() != 2)
    return E_FAIL;
  *app_name = SysAllocString(base::UTF8ToUTF16(product_components[0]).c_str());
  DCHECK(*app_name);
  return *app_name ? S_OK : E_FAIL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_appVersion(BSTR* app_version) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_APP_VERSION);

  if (!app_version)
    return E_INVALIDARG;

  // GetProduct() returns a string like "Chrome/aa.bb.cc.dd", split out
  // the part after the "/".
  std::vector<std::string> product_components =
      base::SplitString(GetContentClient()->GetProduct(), "/",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);
  DCHECK_EQ(2U, product_components.size());
  if (product_components.size() != 2)
    return E_FAIL;
  *app_version =
      SysAllocString(base::UTF8ToUTF16(product_components[1]).c_str());
  DCHECK(*app_version);
  return *app_version ? S_OK : E_FAIL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_toolkitName(BSTR* toolkit_name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TOOLKIT_NAME);
  if (!toolkit_name)
    return E_INVALIDARG;

  // This is hard-coded; all products based on the Chromium engine
  // will have the same toolkit name, so that assistive technology can
  // detect any Chrome-based product.
  *toolkit_name = SysAllocString(L"Chrome");
  DCHECK(*toolkit_name);
  return *toolkit_name ? S_OK : E_FAIL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_toolkitVersion(
    BSTR* toolkit_version) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TOOLKIT_VERSION);
  if (!toolkit_version)
    return E_INVALIDARG;

  std::string user_agent = GetContentClient()->GetUserAgent();
  *toolkit_version = SysAllocString(base::UTF8ToUTF16(user_agent).c_str());
  DCHECK(*toolkit_version);
  return *toolkit_version ? S_OK : E_FAIL;
}

//
// IAccessibleImage methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_description(BSTR* desc) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_DESCRIPTION);
  if (!owner())
    return E_FAIL;

  if (!desc)
    return E_INVALIDARG;

  if (description().empty())
    return S_FALSE;

  *desc = SysAllocString(description().c_str());

  DCHECK(*desc);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_imagePosition(
    IA2CoordinateType coordinate_type,
    LONG* x,
    LONG* y) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IMAGE_POSITION);
  if (!owner())
    return E_FAIL;

  if (!x || !y)
    return E_INVALIDARG;

  if (coordinate_type == IA2_COORDTYPE_SCREEN_RELATIVE) {
    gfx::Rect bounds = owner()->GetScreenBoundsRect();
    *x = bounds.x();
    *y = bounds.y();
  } else if (coordinate_type == IA2_COORDTYPE_PARENT_RELATIVE) {
    gfx::Rect bounds = owner()->GetPageBoundsRect();
    gfx::Rect parent_bounds =
        owner()->PlatformGetParent()
            ? owner()->PlatformGetParent()->GetPageBoundsRect()
            : gfx::Rect();
    *x = bounds.x() - parent_bounds.x();
    *y = bounds.y() - parent_bounds.y();
  } else {
    return E_INVALIDARG;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_imageSize(LONG* height,
                                                       LONG* width) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IMAGE_SIZE);
  if (!owner())
    return E_FAIL;

  if (!height || !width)
    return E_INVALIDARG;

  *height = owner()->GetPageBoundsRect().height();
  *width = owner()->GetPageBoundsRect().width();
  return S_OK;
}

//
// IAccessibleTable methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_accessibleAt(
    long row,
    long column,
    IUnknown** accessible) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_accessibleAt(row, column, accessible);
}

STDMETHODIMP BrowserAccessibilityComWin::get_caption(IUnknown** accessible) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_caption(accessible);
}

STDMETHODIMP BrowserAccessibilityComWin::get_childIndex(long row,
                                                        long column,
                                                        long* cell_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_childIndex(row, column, cell_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnDescription(
    long column,
    BSTR* description) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnDescription(column, description);
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnExtentAt(
    long row,
    long column,
    long* n_columns_spanned) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnExtentAt(row, column, n_columns_spanned);
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnHeader(
    IAccessibleTable** accessible_table,
    long* starting_row_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnHeader(accessible_table,
                                             starting_row_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnIndex(long cell_index,
                                                         long* column_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnIndex(cell_index, column_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_nColumns(long* column_count) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nColumns(column_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_nRows(long* row_count) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nRows(row_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedChildren(
    long* cell_count) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nSelectedChildren(cell_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedColumns(
    long* column_count) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nSelectedColumns(column_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedRows(long* row_count) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nSelectedRows(row_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowDescription(long row,
                                                            BSTR* description) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowDescription(row, description);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowExtentAt(long row,
                                                         long column,
                                                         long* n_rows_spanned) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowExtentAt(row, column, n_rows_spanned);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowHeader(
    IAccessibleTable** accessible_table,
    long* starting_column_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowHeader(accessible_table,
                                          starting_column_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowIndex(long cell_index,
                                                      long* row_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowIndex(cell_index, row_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedChildren(
    long max_children,
    long** children,
    long* n_children) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_selectedChildren(max_children, children,
                                                 n_children);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedColumns(long max_columns,
                                                             long** columns,
                                                             long* n_columns) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_selectedColumns(max_columns, columns,
                                                n_columns);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedRows(long max_rows,
                                                          long** rows,
                                                          long* n_rows) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_selectedRows(max_rows, rows, n_rows);
}

STDMETHODIMP BrowserAccessibilityComWin::get_summary(IUnknown** accessible) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_summary(accessible);
}

STDMETHODIMP BrowserAccessibilityComWin::get_isColumnSelected(
    long column,
    boolean* is_selected) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_isColumnSelected(column, is_selected);
}

STDMETHODIMP BrowserAccessibilityComWin::get_isRowSelected(
    long row,
    boolean* is_selected) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_isRowSelected(row, is_selected);
}

STDMETHODIMP BrowserAccessibilityComWin::get_isSelected(long row,
                                                        long column,
                                                        boolean* is_selected) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_isSelected(row, column, is_selected);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowColumnExtentsAtIndex(
    long index,
    long* row,
    long* column,
    long* row_extents,
    long* column_extents,
    boolean* is_selected) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowColumnExtentsAtIndex(
      index, row, column, row_extents, column_extents, is_selected);
}

STDMETHODIMP BrowserAccessibilityComWin::selectRow(long row) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::selectRow(row);
}

STDMETHODIMP BrowserAccessibilityComWin::selectColumn(long column) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::selectColumn(column);
}

STDMETHODIMP BrowserAccessibilityComWin::unselectRow(long row) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::unselectRow(row);
}

STDMETHODIMP BrowserAccessibilityComWin::unselectColumn(long column) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::unselectColumn(column);
}

STDMETHODIMP
BrowserAccessibilityComWin::get_modelChange(IA2TableModelChange* model_change) {
  return AXPlatformNodeWin::get_modelChange(model_change);
}

//
// IAccessibleTable2 methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_cellAt(long row,
                                                    long column,
                                                    IUnknown** cell) {
  AddAccessibilityModeFlags(ui::AXMode::kScreenReader);
  return AXPlatformNodeWin::get_cellAt(row, column, cell);
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelectedCells(long* cell_count) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_nSelectedCells(cell_count);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedCells(
    IUnknown*** cells,
    long* n_selected_cells) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_selectedCells(cells, n_selected_cells);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedColumns(long** columns,
                                                             long* n_columns) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_selectedColumns(columns, n_columns);
}

STDMETHODIMP BrowserAccessibilityComWin::get_selectedRows(long** rows,
                                                          long* n_rows) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_selectedRows(rows, n_rows);
}

//
// IAccessibleTableCell methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_columnExtent(
    long* n_columns_spanned) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnExtent(n_columns_spanned);
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnHeaderCells(
    IUnknown*** cell_accessibles,
    long* n_column_header_cells) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnHeaderCells(cell_accessibles,
                                                  n_column_header_cells);
}

STDMETHODIMP BrowserAccessibilityComWin::get_columnIndex(long* column_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_columnIndex(column_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowExtent(long* n_rows_spanned) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowExtent(n_rows_spanned);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowHeaderCells(
    IUnknown*** cell_accessibles,
    long* n_row_header_cells) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowHeaderCells(cell_accessibles,
                                               n_row_header_cells);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowIndex(long* row_index) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowIndex(row_index);
}

STDMETHODIMP BrowserAccessibilityComWin::get_isSelected(boolean* is_selected) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_isSelected(is_selected);
}

STDMETHODIMP BrowserAccessibilityComWin::get_rowColumnExtents(
    long* row_index,
    long* column_index,
    long* row_extents,
    long* column_extents,
    boolean* is_selected) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_rowColumnExtents(
      row_index, column_index, row_extents, column_extents, is_selected);
}

STDMETHODIMP BrowserAccessibilityComWin::get_table(IUnknown** table) {
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return AXPlatformNodeWin::get_table(table);
}

//
// IAccessibleText methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_nCharacters(LONG* n_characters) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_CHARACTERS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!n_characters)
    return E_INVALIDARG;

  *n_characters = static_cast<LONG>(owner()->GetText().size());
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_caretOffset(LONG* offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CARET_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!offset)
    return E_INVALIDARG;

  if (!owner()->HasCaret())
    return S_FALSE;

  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  // The caret is always at the end of the selection.
  *offset = selection_end;
  if (*offset < 0)
    return S_FALSE;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_characterExtents(
    LONG offset,
    IA2CoordinateType coordinate_type,
    LONG* out_x,
    LONG* out_y,
    LONG* out_width,
    LONG* out_height) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CHARACTER_EXTENTS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!out_x || !out_y || !out_width || !out_height)
    return E_INVALIDARG;

  const base::string16& text_str = owner()->GetText();
  HandleSpecialTextOffset(&offset);
  if (offset < 0 || offset > static_cast<LONG>(text_str.size()))
    return E_INVALIDARG;

  gfx::Rect character_bounds;
  if (coordinate_type == IA2_COORDTYPE_SCREEN_RELATIVE) {
    character_bounds = owner()->GetScreenBoundsForRange(offset, 1);
  } else if (coordinate_type == IA2_COORDTYPE_PARENT_RELATIVE) {
    character_bounds = owner()->GetPageBoundsForRange(offset, 1);
    if (owner()->PlatformGetParent()) {
      character_bounds -=
          owner()->PlatformGetParent()->GetPageBoundsRect().OffsetFromOrigin();
    }
  } else {
    return E_INVALIDARG;
  }

  *out_x = character_bounds.x();
  *out_y = character_bounds.y();
  *out_width = character_bounds.width();
  *out_height = character_bounds.height();

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nSelections(LONG* n_selections) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_SELECTIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_selections)
    return E_INVALIDARG;

  *n_selections = 0;
  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  if (selection_start >= 0 && selection_end >= 0 &&
      selection_start != selection_end) {
    *n_selections = 1;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_selection(LONG selection_index,
                                                       LONG* start_offset,
                                                       LONG* end_offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || selection_index != 0)
    return E_INVALIDARG;

  *start_offset = 0;
  *end_offset = 0;
  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  if (selection_start >= 0 && selection_end >= 0 &&
      selection_start != selection_end) {
    // We should ignore the direction of the selection when exposing start and
    // end offsets. According to the IA2 Spec the end offset is always increased
    // by one past the end of the selection. This wouldn't make sense if
    // end < start.
    if (selection_end < selection_start)
      std::swap(selection_start, selection_end);

    *start_offset = selection_start;
    *end_offset = selection_end;
    return S_OK;
  }

  return E_INVALIDARG;
}

STDMETHODIMP BrowserAccessibilityComWin::get_text(LONG start_offset,
                                                  LONG end_offset,
                                                  BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!text)
    return E_INVALIDARG;

  const base::string16& text_str = owner()->GetText();
  HandleSpecialTextOffset(&start_offset);
  HandleSpecialTextOffset(&end_offset);

  // The spec allows the arguments to be reversed.
  if (start_offset > end_offset) {
    LONG tmp = start_offset;
    start_offset = end_offset;
    end_offset = tmp;
  }

  // The spec does not allow the start or end offsets to be out or range;
  // we must return an error if so.
  LONG len = text_str.length();
  if (start_offset < 0)
    return E_INVALIDARG;
  if (end_offset > len)
    return E_INVALIDARG;

  base::string16 substr =
      text_str.substr(start_offset, end_offset - start_offset);

  if (substr.empty())
    return S_FALSE;

  *text = SysAllocString(substr.c_str());
  DCHECK(*text);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_textAtOffset(
    LONG offset,
    IA2TextBoundaryType boundary_type,
    LONG* start_offset,
    LONG* end_offset,
    BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT_AT_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || !text)
    return E_INVALIDARG;

  *start_offset = 0;
  *end_offset = 0;
  *text = nullptr;

  HandleSpecialTextOffset(&offset);
  if (offset < 0)
    return E_INVALIDARG;

  const base::string16& text_str = owner()->GetText();
  LONG text_len = text_str.length();
  if (offset > text_len)
    return E_INVALIDARG;

  // The IAccessible2 spec says we don't have to implement the "sentence"
  // boundary type, we can just let the screenreader handle it.
  if (boundary_type == IA2_TEXT_BOUNDARY_SENTENCE)
    return S_FALSE;

  // According to the IA2 Spec, only line boundaries should succeed when
  // the offset is one past the end of the text.
  if (offset == text_len && boundary_type != IA2_TEXT_BOUNDARY_LINE)
    return S_FALSE;

  LONG start = FindBoundary(boundary_type, offset, ui::BACKWARDS_DIRECTION);
  LONG end = FindBoundary(boundary_type, start, ui::FORWARDS_DIRECTION);
  if (end < offset)
    return S_FALSE;

  *start_offset = start;
  *end_offset = end;
  return get_text(start, end, text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_textBeforeOffset(
    LONG offset,
    IA2TextBoundaryType boundary_type,
    LONG* start_offset,
    LONG* end_offset,
    BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT_BEFORE_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || !text)
    return E_INVALIDARG;

  *start_offset = 0;
  *end_offset = 0;
  *text = NULL;

  const base::string16& text_str = owner()->GetText();
  LONG text_len = text_str.length();
  if (offset > text_len)
    return E_INVALIDARG;

  // The IAccessible2 spec says we don't have to implement the "sentence"
  // boundary type, we can just let the screenreader handle it.
  if (boundary_type == IA2_TEXT_BOUNDARY_SENTENCE)
    return S_FALSE;

  *start_offset = FindBoundary(boundary_type, offset, ui::BACKWARDS_DIRECTION);
  *end_offset = offset;
  return get_text(*start_offset, *end_offset, text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_textAfterOffset(
    LONG offset,
    IA2TextBoundaryType boundary_type,
    LONG* start_offset,
    LONG* end_offset,
    BSTR* text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TEXT_AFTER_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!start_offset || !end_offset || !text)
    return E_INVALIDARG;

  *start_offset = 0;
  *end_offset = 0;
  *text = NULL;

  const base::string16& text_str = owner()->GetText();
  LONG text_len = text_str.length();
  if (offset > text_len)
    return E_INVALIDARG;

  // The IAccessible2 spec says we don't have to implement the "sentence"
  // boundary type, we can just let the screenreader handle it.
  if (boundary_type == IA2_TEXT_BOUNDARY_SENTENCE)
    return S_FALSE;

  *start_offset = offset;
  *end_offset = FindBoundary(boundary_type, offset, ui::FORWARDS_DIRECTION);
  return get_text(*start_offset, *end_offset, text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_newText(IA2TextSegment* new_text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NEW_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!new_text)
    return E_INVALIDARG;

  if (!old_win_attributes_)
    return E_FAIL;

  int start, old_len, new_len;
  ComputeHypertextRemovedAndInserted(&start, &old_len, &new_len);
  if (new_len == 0)
    return E_FAIL;

  base::string16 substr = owner()->GetText().substr(start, new_len);
  new_text->text = SysAllocString(substr.c_str());
  new_text->start = static_cast<long>(start);
  new_text->end = static_cast<long>(start + new_len);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_oldText(IA2TextSegment* old_text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_OLD_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!old_text)
    return E_INVALIDARG;

  if (!old_win_attributes_)
    return E_FAIL;

  int start, old_len, new_len;
  ComputeHypertextRemovedAndInserted(&start, &old_len, &new_len);
  if (old_len == 0)
    return E_FAIL;

  base::string16 old_hypertext = old_win_attributes_->hypertext;
  base::string16 substr = old_hypertext.substr(start, old_len);
  old_text->text = SysAllocString(substr.c_str());
  old_text->start = static_cast<long>(start);
  old_text->end = static_cast<long>(start + old_len);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_offsetAtPoint(
    LONG x,
    LONG y,
    IA2CoordinateType coord_type,
    LONG* offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_OFFSET_AT_POINT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!offset)
    return E_INVALIDARG;

  // TODO(dmazzoni): implement this. We're returning S_OK for now so that
  // screen readers still return partially accurate results rather than
  // completely failing.
  *offset = 0;
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollSubstringTo(
    LONG start_index,
    LONG end_index,
    IA2ScrollType scroll_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_SUBSTRING_TO);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  // TODO(dmazzoni): adjust this for the start and end index, too.
  return scrollTo(scroll_type);
}

STDMETHODIMP BrowserAccessibilityComWin::scrollSubstringToPoint(
    LONG start_index,
    LONG end_index,
    IA2CoordinateType coordinate_type,
    LONG x,
    LONG y) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_SUBSTRING_TO_POINT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (start_index > end_index)
    std::swap(start_index, end_index);
  LONG length = end_index - start_index + 1;
  DCHECK_GE(length, 0);

  gfx::Rect string_bounds = owner()->GetPageBoundsForRange(start_index, length);
  string_bounds -= owner()->GetPageBoundsRect().OffsetFromOrigin();
  x -= string_bounds.x();
  y -= string_bounds.y();

  return scrollToPoint(coordinate_type, x, y);
}

STDMETHODIMP BrowserAccessibilityComWin::addSelection(LONG start_offset,
                                                      LONG end_offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ADD_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  // We only support one selection.
  SetIA2HypertextSelection(start_offset, end_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::removeSelection(LONG selection_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_REMOVE_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (selection_index != 0)
    return E_INVALIDARG;

  // Simply collapse the selection to the position of the caret if a caret is
  // visible, otherwise set the selection to 0.
  LONG caret_offset = 0;
  int selection_start, selection_end;
  GetSelectionOffsets(&selection_start, &selection_end);
  if (owner()->HasCaret() && selection_end >= 0)
    caret_offset = selection_end;
  SetIA2HypertextSelection(caret_offset, caret_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::setCaretOffset(LONG offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SET_CARET_OFFSET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;
  SetIA2HypertextSelection(offset, offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::setSelection(LONG selection_index,
                                                      LONG start_offset,
                                                      LONG end_offset) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SET_SELECTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;
  if (selection_index != 0)
    return E_INVALIDARG;
  SetIA2HypertextSelection(start_offset, end_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributes(LONG offset,
                                                        LONG* start_offset,
                                                        LONG* end_offset,
                                                        BSTR* text_attributes) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IATEXT_GET_ATTRIBUTES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!start_offset || !end_offset || !text_attributes)
    return E_INVALIDARG;

  *start_offset = *end_offset = 0;
  *text_attributes = nullptr;
  if (!owner())
    return E_FAIL;

  const base::string16 text = owner()->GetText();
  HandleSpecialTextOffset(&offset);
  if (offset < 0 || offset > static_cast<LONG>(text.size()))
    return E_INVALIDARG;

  ComputeStylesIfNeeded();
  *start_offset = FindStartOfStyle(offset, ui::BACKWARDS_DIRECTION);
  *end_offset = FindStartOfStyle(offset, ui::FORWARDS_DIRECTION);

  base::string16 attributes_str;
  const std::vector<base::string16>& attributes =
      offset_to_text_attributes().find(*start_offset)->second;
  for (const base::string16& attribute : attributes) {
    attributes_str += attribute + L';';
  }

  if (attributes.empty())
    return S_FALSE;

  *text_attributes = SysAllocString(attributes_str.c_str());
  DCHECK(*text_attributes);
  return S_OK;
}

//
// IAccessibleHypertext methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_nHyperlinks(
    long* hyperlink_count) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_N_HYPERLINKS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!hyperlink_count)
    return E_INVALIDARG;

  *hyperlink_count = hyperlink_offset_to_index().size();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_hyperlink(
    long index,
    IAccessibleHyperlink** hyperlink) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HYPERLINK);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!hyperlink || index < 0 ||
      index >= static_cast<long>(hyperlinks().size())) {
    return E_INVALIDARG;
  }

  int32_t id = hyperlinks()[index];
  auto* link = static_cast<BrowserAccessibilityComWin*>(
      AXPlatformNodeWin::GetFromUniqueId(id));
  if (!link)
    return E_FAIL;

  *hyperlink = static_cast<IAccessibleHyperlink*>(link->NewReference());
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_hyperlinkIndex(
    long char_index,
    long* hyperlink_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HYPERLINK_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!hyperlink_index)
    return E_INVALIDARG;

  if (char_index < 0 ||
      char_index >= static_cast<long>(owner()->GetText().size())) {
    return E_INVALIDARG;
  }

  std::map<int32_t, int32_t>::iterator it =
      hyperlink_offset_to_index().find(char_index);
  if (it == hyperlink_offset_to_index().end()) {
    *hyperlink_index = -1;
    return S_FALSE;
  }

  *hyperlink_index = it->second;
  return S_OK;
}

//
// IAccessibleHyperlink methods.
//

// Currently, only text links are supported.
STDMETHODIMP BrowserAccessibilityComWin::get_anchor(long index,
                                                    VARIANT* anchor) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ANCHOR);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner() || !IsHyperlink())
    return E_FAIL;

  // IA2 text links can have only one anchor, that is the text inside them.
  if (index != 0 || !anchor)
    return E_INVALIDARG;

  BSTR ia2_hypertext = SysAllocString(owner()->GetText().c_str());
  DCHECK(ia2_hypertext);
  anchor->vt = VT_BSTR;
  anchor->bstrVal = ia2_hypertext;

  // Returning S_FALSE is not mentioned in the IA2 Spec, but it might have been
  // an oversight.
  if (!SysStringLen(ia2_hypertext))
    return S_FALSE;

  return S_OK;
}

// Currently, only text links are supported.
STDMETHODIMP BrowserAccessibilityComWin::get_anchorTarget(
    long index,
    VARIANT* anchor_target) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ANCHOR_TARGET);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner() || !IsHyperlink())
    return E_FAIL;

  // IA2 text links can have at most one target, that is when they represent an
  // HTML hyperlink, i.e. an <a> element with a "href" attribute.
  if (index != 0 || !anchor_target)
    return E_INVALIDARG;

  BSTR target;
  if (!(MSAAState() & STATE_SYSTEM_LINKED) ||
      FAILED(GetStringAttributeAsBstr(ui::AX_ATTR_URL, &target))) {
    target = SysAllocString(L"");
  }
  DCHECK(target);
  anchor_target->vt = VT_BSTR;
  anchor_target->bstrVal = target;

  // Returning S_FALSE is not mentioned in the IA2 Spec, but it might have been
  // an oversight.
  if (!SysStringLen(target))
    return S_FALSE;

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_startIndex(long* index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_START_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner() || !IsHyperlink())
    return E_FAIL;

  if (!index)
    return E_INVALIDARG;

  int32_t hypertext_offset = 0;
  auto* parent = owner()->PlatformGetParent();
  if (parent) {
    hypertext_offset =
        ToBrowserAccessibilityComWin(parent)->GetHypertextOffsetFromChild(
            *this);
  }
  *index = static_cast<LONG>(hypertext_offset);
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_endIndex(long* index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_END_INDEX);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  LONG start_index;
  HRESULT hr = get_startIndex(&start_index);
  if (hr == S_OK)
    *index = start_index + 1;
  return hr;
}

// This method is deprecated in the IA2 Spec.
STDMETHODIMP BrowserAccessibilityComWin::get_valid(boolean* valid) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_VALID);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

//
// IAccessibleAction partly implemented.
//

STDMETHODIMP BrowserAccessibilityComWin::nActions(long* n_actions) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_N_ACTIONS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!n_actions)
    return E_INVALIDARG;

  // |IsHyperlink| is required for |IAccessibleHyperlink::anchor/anchorTarget|
  // to work properly because the |IAccessibleHyperlink| interface inherits from
  // |IAccessibleAction|.
  if (IsHyperlink() ||
      owner()->HasIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB)) {
    *n_actions = 1;
  } else {
    *n_actions = 0;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::doAction(long action_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_DO_ACTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!owner()->HasIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB) ||
      action_index != 0)
    return E_INVALIDARG;

  Manager()->DoDefaultAction(*owner());
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_description(long action_index,
                                            BSTR* description) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_IAACTION_GET_DESCRIPTION);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_keyBinding(long action_index,
                                                        long n_max_bindings,
                                                        BSTR** key_bindings,
                                                        long* n_bindings) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_KEY_BINDING);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_name(long action_index,
                                                  BSTR* name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NAME);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!name)
    return E_INVALIDARG;

  int action;
  if (!owner()->GetIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB, &action) ||
      action_index != 0) {
    *name = nullptr;
    return E_INVALIDARG;
  }

  base::string16 action_verb = ui::ActionVerbToUnlocalizedString(
      static_cast<ui::AXDefaultActionVerb>(action));
  if (action_verb.empty() || action_verb == L"none") {
    *name = nullptr;
    return S_FALSE;
  }

  *name = SysAllocString(action_verb.c_str());
  DCHECK(name);
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_localizedName(long action_index,
                                              BSTR* localized_name) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCALIZED_NAME);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!localized_name)
    return E_INVALIDARG;

  int action;
  if (!owner()->GetIntAttribute(ui::AX_ATTR_DEFAULT_ACTION_VERB, &action) ||
      action_index != 0) {
    *localized_name = nullptr;
    return E_INVALIDARG;
  }

  base::string16 action_verb = ui::ActionVerbToLocalizedString(
      static_cast<ui::AXDefaultActionVerb>(action));
  if (action_verb.empty()) {
    *localized_name = nullptr;
    return S_FALSE;
  }

  *localized_name = SysAllocString(action_verb.c_str());
  DCHECK(localized_name);
  return S_OK;
}

//
// IAccessibleValue methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_currentValue(VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CURRENT_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  float float_val;
  if (GetFloatAttribute(ui::AX_ATTR_VALUE_FOR_RANGE, &float_val)) {
    value->vt = VT_R8;
    value->dblVal = float_val;
    return S_OK;
  }

  value->vt = VT_EMPTY;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_minimumValue(VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_MINIMUM_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  float float_val;
  if (GetFloatAttribute(ui::AX_ATTR_MIN_VALUE_FOR_RANGE, &float_val)) {
    value->vt = VT_R8;
    value->dblVal = float_val;
    return S_OK;
  }

  value->vt = VT_EMPTY;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::get_maximumValue(VARIANT* value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_MAXIMUM_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!value)
    return E_INVALIDARG;

  float float_val;
  if (GetFloatAttribute(ui::AX_ATTR_MAX_VALUE_FOR_RANGE, &float_val)) {
    value->vt = VT_R8;
    value->dblVal = float_val;
    return S_OK;
  }

  value->vt = VT_EMPTY;
  return S_FALSE;
}

STDMETHODIMP BrowserAccessibilityComWin::setCurrentValue(VARIANT new_value) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SET_CURRENT_VALUE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  // TODO(dmazzoni): Implement this.
  return E_NOTIMPL;
}

//
// ISimpleDOMDocument methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_URL(BSTR* url) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_URL);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!url)
    return E_INVALIDARG;

  if (owner() != manager->GetRoot())
    return E_FAIL;

  std::string str = manager->GetTreeData().url;
  if (str.empty())
    return S_FALSE;

  *url = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*url);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_title(BSTR* title) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_TITLE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!title)
    return E_INVALIDARG;

  std::string str = manager->GetTreeData().title;
  if (str.empty())
    return S_FALSE;

  *title = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*title);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_mimeType(BSTR* mime_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_MIME_TYPE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!mime_type)
    return E_INVALIDARG;

  std::string str = manager->GetTreeData().mimetype;
  if (str.empty())
    return S_FALSE;

  *mime_type = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*mime_type);

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_docType(BSTR* doc_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_DOC_TYPE);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  if (!doc_type)
    return E_INVALIDARG;

  std::string str = manager->GetTreeData().doctype;
  if (str.empty())
    return S_FALSE;

  *doc_type = SysAllocString(base::UTF8ToUTF16(str).c_str());
  DCHECK(*doc_type);

  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_nameSpaceURIForID(short name_space_id,
                                                  BSTR* name_space_uri) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NAMESPACE_URI_FOR_ID);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::put_alternateViewMediaTypes(
    BSTR* comma_separated_media_types) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_PUT_ALTERNATE_VIEW_MEDIA_TYPES);
  return E_NOTIMPL;
}

//
// ISimpleDOMNode methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_nodeInfo(
    BSTR* node_name,
    short* name_space_id,
    BSTR* node_value,
    unsigned int* num_children,
    unsigned int* unique_id,
    unsigned short* node_type) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NODE_INFO);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!node_name || !name_space_id || !node_value || !num_children ||
      !unique_id || !node_type) {
    return E_INVALIDARG;
  }

  base::string16 tag;
  if (owner()->GetString16Attribute(ui::AX_ATTR_HTML_TAG, &tag))
    *node_name = SysAllocString(tag.c_str());
  else
    *node_name = nullptr;

  *name_space_id = 0;
  *node_value = SysAllocString(value().c_str());
  *num_children = owner()->PlatformChildCount();
  *unique_id = -AXPlatformNodeWin::unique_id();

  if (owner()->IsDocument()) {
    *node_type = NODETYPE_DOCUMENT;
  } else if (owner()->IsTextOnlyObject()) {
    *node_type = NODETYPE_TEXT;
  } else {
    *node_type = NODETYPE_ELEMENT;
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributes(
    unsigned short max_attribs,
    BSTR* attrib_names,
    short* name_space_id,
    BSTR* attrib_values,
    unsigned short* num_attribs) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ISIMPLEDOMNODE_GET_ATTRIBUTES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!attrib_names || !name_space_id || !attrib_values || !num_attribs)
    return E_INVALIDARG;

  *num_attribs = max_attribs;
  if (*num_attribs > owner()->GetHtmlAttributes().size())
    *num_attribs = owner()->GetHtmlAttributes().size();

  for (unsigned short i = 0; i < *num_attribs; ++i) {
    attrib_names[i] = SysAllocString(
        base::UTF8ToUTF16(owner()->GetHtmlAttributes()[i].first).c_str());
    name_space_id[i] = 0;
    attrib_values[i] = SysAllocString(
        base::UTF8ToUTF16(owner()->GetHtmlAttributes()[i].second).c_str());
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_attributesForNames(
    unsigned short num_attribs,
    BSTR* attrib_names,
    short* name_space_id,
    BSTR* attrib_values) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_ATTRIBUTES_FOR_NAMES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!attrib_names || !name_space_id || !attrib_values)
    return E_INVALIDARG;

  for (unsigned short i = 0; i < num_attribs; ++i) {
    name_space_id[i] = 0;
    bool found = false;
    std::string name = base::UTF16ToUTF8((LPCWSTR)attrib_names[i]);
    for (unsigned int j = 0; j < owner()->GetHtmlAttributes().size(); ++j) {
      if (owner()->GetHtmlAttributes()[j].first == name) {
        attrib_values[i] = SysAllocString(
            base::UTF8ToUTF16(owner()->GetHtmlAttributes()[j].second).c_str());
        found = true;
        break;
      }
    }
    if (!found) {
      attrib_values[i] = NULL;
    }
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_computedStyle(
    unsigned short max_style_properties,
    boolean use_alternate_view,
    BSTR* style_properties,
    BSTR* style_values,
    unsigned short* num_style_properties) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COMPUTED_STYLE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!style_properties || !style_values)
    return E_INVALIDARG;

  // We only cache a single style property for now: DISPLAY

  base::string16 display;
  if (max_style_properties == 0 ||
      !owner()->GetString16Attribute(ui::AX_ATTR_DISPLAY, &display)) {
    *num_style_properties = 0;
    return S_OK;
  }

  *num_style_properties = 1;
  style_properties[0] = SysAllocString(L"display");
  style_values[0] = SysAllocString(display.c_str());

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_computedStyleForProperties(
    unsigned short num_style_properties,
    boolean use_alternate_view,
    BSTR* style_properties,
    BSTR* style_values) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_COMPUTED_STYLE_FOR_PROPERTIES);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!style_properties || !style_values)
    return E_INVALIDARG;

  // We only cache a single style property for now: DISPLAY

  for (unsigned short i = 0; i < num_style_properties; ++i) {
    base::string16 name = base::ToLowerASCII(
        reinterpret_cast<const base::char16*>(style_properties[i]));
    if (name == L"display") {
      base::string16 display =
          owner()->GetString16Attribute(ui::AX_ATTR_DISPLAY);
      style_values[i] = SysAllocString(display.c_str());
    } else {
      style_values[i] = NULL;
    }
  }

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollTo(boolean placeTopLeft) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_ISIMPLEDOMNODE_SCROLL_TO);
  return scrollTo(placeTopLeft ? IA2_SCROLL_TYPE_TOP_LEFT
                               : IA2_SCROLL_TYPE_ANYWHERE);
}

STDMETHODIMP BrowserAccessibilityComWin::get_parentNode(ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PARENT_NODE);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  *node = ToBrowserAccessibilityComWin(owner()->PlatformGetParent())
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_firstChild(ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_FIRST_CHILD);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (owner()->PlatformChildCount() == 0) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(owner()->PlatformGetChild(0))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_lastChild(ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LAST_CHILD);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (owner()->PlatformChildCount() == 0) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(
              owner()->PlatformGetChild(owner()->PlatformChildCount() - 1))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_previousSibling(
    ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PREVIOUS_SIBLING);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (!owner()->PlatformGetParent() || GetIndexInParent() <= 0) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(
              owner()->PlatformGetParent()->InternalGetChild(
                  GetIndexInParent() - 1))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_nextSibling(
    ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_NEXT_SIBLING);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (!owner()->PlatformGetParent() || GetIndexInParent() < 0 ||
      GetIndexInParent() >=
          static_cast<int>(owner()->PlatformGetParent()->InternalChildCount()) -
              1) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(
              owner()->PlatformGetParent()->InternalGetChild(
                  GetIndexInParent() + 1))
              ->NewReference();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_childAt(unsigned int child_index,
                                                     ISimpleDOMNode** node) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CHILD_AT);
  if (!owner())
    return E_FAIL;

  if (!node)
    return E_INVALIDARG;

  if (child_index >= owner()->PlatformChildCount())
    return E_INVALIDARG;

  BrowserAccessibility* child = owner()->PlatformGetChild(child_index);
  if (!child) {
    *node = NULL;
    return S_FALSE;
  }

  *node = ToBrowserAccessibilityComWin(child)->NewReference();
  return S_OK;
}

// We only support this method for retrieving MathML content.
STDMETHODIMP BrowserAccessibilityComWin::get_innerHTML(BSTR* innerHTML) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_INNER_HTML);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;
  if (owner()->GetRole() != ui::AX_ROLE_MATH)
    return E_NOTIMPL;

  base::string16 inner_html =
      owner()->GetString16Attribute(ui::AX_ATTR_INNER_HTML);
  *innerHTML = SysAllocString(inner_html.c_str());
  DCHECK(*innerHTML);
  return S_OK;
}

STDMETHODIMP
BrowserAccessibilityComWin::get_localInterface(void** local_interface) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LOCAL_INTERFACE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_language(BSTR* language) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_LANGUAGE);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!language)
    return E_INVALIDARG;
  *language = nullptr;

  if (!owner())
    return E_FAIL;

  base::string16 lang =
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_LANGUAGE);
  if (lang.empty())
    lang = L"en-US";

  *language = SysAllocString(lang.c_str());
  DCHECK(*language);
  return S_OK;
}

//
// ISimpleDOMText methods.
//

STDMETHODIMP BrowserAccessibilityComWin::get_domText(BSTR* dom_text) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_DOM_TEXT);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!owner())
    return E_FAIL;

  if (!dom_text)
    return E_INVALIDARG;

  return GetStringAttributeAsBstr(ui::AX_ATTR_NAME, dom_text);
}

STDMETHODIMP BrowserAccessibilityComWin::get_clippedSubstringBounds(
    unsigned int start_index,
    unsigned int end_index,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_CLIPPED_SUBSTRING_BOUNDS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  // TODO(dmazzoni): fully support this API by intersecting the
  // rect with the container's rect.
  return get_unclippedSubstringBounds(start_index, end_index, out_x, out_y,
                                      out_width, out_height);
}

STDMETHODIMP BrowserAccessibilityComWin::get_unclippedSubstringBounds(
    unsigned int start_index,
    unsigned int end_index,
    int* out_x,
    int* out_y,
    int* out_width,
    int* out_height) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_UNCLIPPED_SUBSTRING_BOUNDS);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  if (!out_x || !out_y || !out_width || !out_height)
    return E_INVALIDARG;

  unsigned int text_length =
      static_cast<unsigned int>(owner()->GetText().size());
  if (start_index > text_length || end_index > text_length ||
      start_index > end_index) {
    return E_INVALIDARG;
  }

  gfx::Rect bounds =
      owner()->GetScreenBoundsForRange(start_index, end_index - start_index);
  *out_x = bounds.x();
  *out_y = bounds.y();
  *out_width = bounds.width();
  *out_height = bounds.height();
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::scrollToSubstring(
    unsigned int start_index,
    unsigned int end_index) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_SCROLL_TO_SUBSTRING);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes |
                            ui::AXMode::kInlineTextBoxes);
  if (!owner())
    return E_FAIL;

  auto* manager = Manager();
  if (!manager)
    return E_FAIL;

  unsigned int text_length =
      static_cast<unsigned int>(owner()->GetText().size());
  if (start_index > text_length || end_index > text_length ||
      start_index > end_index) {
    return E_INVALIDARG;
  }

  manager->ScrollToMakeVisible(
      *owner(),
      owner()->GetPageBoundsForRange(start_index, end_index - start_index));

  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_fontFamily(BSTR* font_family) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_FONT_FAMILY);
  AddAccessibilityModeFlags(kScreenReaderAndHTMLAccessibilityModes);
  if (!font_family)
    return E_INVALIDARG;
  *font_family = nullptr;

  if (!owner())
    return E_FAIL;

  base::string16 family =
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_FONT_FAMILY);
  if (family.empty())
    return S_FALSE;

  *font_family = SysAllocString(family.c_str());
  DCHECK(*font_family);
  return S_OK;
}

//
// IServiceProvider methods.
//

STDMETHODIMP BrowserAccessibilityComWin::QueryService(REFGUID guid_service,
                                                      REFIID riid,
                                                      void** object) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_QUERY_SERVICE);
  if (!owner())
    return E_FAIL;

  if (guid_service == GUID_IAccessibleContentDocument) {
    // Special Mozilla extension: return the accessible for the root document.
    // Screen readers use this to distinguish between a document loaded event
    // on the root document vs on an iframe.
    BrowserAccessibility* node = owner();
    while (node->PlatformGetParent())
      node = node->PlatformGetParent()->manager()->GetRoot();
    return ToBrowserAccessibilityComWin(node)->QueryInterface(IID_IAccessible2,
                                                              object);
  }

  if (guid_service == IID_IAccessible || guid_service == IID_IAccessible2 ||
      guid_service == IID_IAccessibleAction ||
      guid_service == IID_IAccessibleApplication ||
      guid_service == IID_IAccessibleHyperlink ||
      guid_service == IID_IAccessibleHypertext ||
      guid_service == IID_IAccessibleImage ||
      guid_service == IID_IAccessibleTable ||
      guid_service == IID_IAccessibleTable2 ||
      guid_service == IID_IAccessibleTableCell ||
      guid_service == IID_IAccessibleText ||
      guid_service == IID_IAccessibleValue ||
      guid_service == IID_ISimpleDOMDocument ||
      guid_service == IID_ISimpleDOMNode ||
      guid_service == IID_ISimpleDOMText || guid_service == GUID_ISimpleDOM) {
    return QueryInterface(riid, object);
  }

  // We only support the IAccessibleEx interface on Windows 8 and above. This
  // is needed for the on-screen Keyboard to show up in metro mode, when the
  // user taps an editable portion on the page.
  // All methods in the IAccessibleEx interface are unimplemented.
  if (riid == IID_IAccessibleEx &&
      base::win::GetVersion() >= base::win::VERSION_WIN8) {
    return QueryInterface(riid, object);
  }

  *object = NULL;
  return E_FAIL;
}

STDMETHODIMP
BrowserAccessibilityComWin::GetObjectForChild(long child_id,
                                              IAccessibleEx** ret) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_OBJECT_FOR_CHILD);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::GetIAccessiblePair(IAccessible** acc,
                                               long* child_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_IACCESSIBLE_PAIR);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::GetRuntimeId(SAFEARRAY** runtime_id) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_RUNTIME_ID);
  return E_NOTIMPL;
}

STDMETHODIMP
BrowserAccessibilityComWin::ConvertReturnedElement(
    IRawElementProviderSimple* element,
    IAccessibleEx** acc) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_CONVERT_RETURNED_ELEMENT);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::GetPatternProvider(
    PATTERNID id,
    IUnknown** provider) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PATTERN_PROVIDER);
  DVLOG(1) << "In Function: " << __func__ << " for pattern id: " << id;
  if (!owner())
    return E_FAIL;

  if (id == UIA_ValuePatternId || id == UIA_TextPatternId) {
    if (owner()->HasState(ui::AX_STATE_EDITABLE)) {
      DVLOG(1) << "Returning UIA text provider";
      base::win::UIATextProvider::CreateTextProvider(GetRangeValueText(), true,
                                                     provider);
      return S_OK;
    }
  }
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::GetPropertyValue(PROPERTYID id,
                                                          VARIANT* ret) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PROPERTY_VALUE);
  DVLOG(1) << "In Function: " << __func__ << " for property id: " << id;
  if (!owner())
    return E_FAIL;

  V_VT(ret) = VT_EMPTY;
  if (id == UIA_ControlTypePropertyId) {
    if (owner()->HasState(ui::AX_STATE_EDITABLE)) {
      V_VT(ret) = VT_I4;
      ret->lVal = UIA_EditControlTypeId;
      DVLOG(1) << "Returning Edit control type";
    } else {
      DVLOG(1) << "Returning empty control type";
    }
  }
  return S_OK;
}

STDMETHODIMP BrowserAccessibilityComWin::get_ProviderOptions(
    ProviderOptions* ret) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_PROVIDER_OPTIONS);
  return E_NOTIMPL;
}

STDMETHODIMP BrowserAccessibilityComWin::get_HostRawElementProvider(
    IRawElementProviderSimple** provider) {
  WIN_ACCESSIBILITY_API_HISTOGRAM(UMA_API_GET_HOST_RAW_ELEMENT_PROVIDER);
  return E_NOTIMPL;
}

//
// CComObjectRootEx methods.
//

// static
HRESULT WINAPI BrowserAccessibilityComWin::InternalQueryInterface(
    void* this_ptr,
    const _ATL_INTMAP_ENTRY* entries,
    REFIID iid,
    void** object) {
  BrowserAccessibilityComWin* accessibility =
      reinterpret_cast<BrowserAccessibilityComWin*>(this_ptr);

  if (!accessibility->owner()) {
    *object = nullptr;
    return E_NOINTERFACE;
  }

  int32_t ia_role = accessibility->MSAARole();
  if (iid == IID_IAccessibleImage) {
    if (ia_role != ROLE_SYSTEM_GRAPHIC) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleTable || iid == IID_IAccessibleTable2) {
    if (ia_role != ROLE_SYSTEM_TABLE) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleTableCell) {
    if (!ui::IsCellOrTableHeaderRole(accessibility->owner()->GetRole())) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleValue) {
    if (!accessibility->IsRangeValueSupported()) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_ISimpleDOMDocument) {
    if (ia_role != ROLE_SYSTEM_DOCUMENT) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  } else if (iid == IID_IAccessibleHyperlink) {
    auto* ax_object =
        reinterpret_cast<const BrowserAccessibilityComWin*>(this_ptr);
    if (!ax_object || !ax_object->IsHyperlink()) {
      *object = nullptr;
      return E_NOINTERFACE;
    }
  }

  return CComObjectRootBase::InternalQueryInterface(this_ptr, entries, iid,
                                                    object);
}

void BrowserAccessibilityComWin::ComputeStylesIfNeeded() {
  if (!offset_to_text_attributes().empty())
    return;

  std::map<int, std::vector<base::string16>> attributes_map;
  if (owner()->PlatformIsLeaf() || owner()->IsSimpleTextControl()) {
    attributes_map[0] = ComputeTextAttributes();
    std::map<int, std::vector<base::string16>> spelling_attributes =
        GetSpellingAttributes();
    for (auto& spelling_attribute : spelling_attributes) {
      auto attributes_iterator = attributes_map.find(spelling_attribute.first);
      if (attributes_iterator == attributes_map.end()) {
        attributes_map[spelling_attribute.first] =
            std::move(spelling_attribute.second);
      } else {
        std::vector<base::string16>& existing_attributes =
            attributes_iterator->second;

        // There might be a spelling attribute already in the list of text
        // attributes, originating from "aria-invalid".
        auto existing_spelling_attribute =
            std::find(existing_attributes.begin(), existing_attributes.end(),
                      L"invalid:false");
        if (existing_spelling_attribute != existing_attributes.end())
          existing_attributes.erase(existing_spelling_attribute);

        existing_attributes.insert(existing_attributes.end(),
                                   spelling_attribute.second.begin(),
                                   spelling_attribute.second.end());
      }
    }
    win_attributes_->offset_to_text_attributes.swap(attributes_map);
    return;
  }

  int start_offset = 0;
  for (size_t i = 0; i < owner()->PlatformChildCount(); ++i) {
    auto* child = ToBrowserAccessibilityComWin(owner()->PlatformGetChild(i));
    DCHECK(child);
    std::vector<base::string16> attributes(child->ComputeTextAttributes());

    if (attributes_map.empty()) {
      attributes_map[start_offset] = attributes;
    } else {
      // Only add the attributes for this child if we are at the start of a new
      // style span.
      std::vector<base::string16> previous_attributes =
          attributes_map.rbegin()->second;
      if (!std::equal(attributes.begin(), attributes.end(),
                      previous_attributes.begin())) {
        attributes_map[start_offset] = attributes;
      }
    }

    if (child->owner()->IsTextOnlyObject())
      start_offset += child->owner()->GetText().length();
    else
      start_offset += 1;
  }

  win_attributes_->offset_to_text_attributes.swap(attributes_map);
}

// |offset| could either be a text character or a child index in case of
// non-text objects.
// Currently, to be safe, we convert to text leaf equivalents and we don't use
// tree positions.
// TODO(nektar): Remove this function once selection fixes in Blink are
// thoroughly tested and convert to tree positions.
AXPlatformPosition::AXPositionInstance
BrowserAccessibilityComWin::CreatePositionForSelectionAt(int offset) const {
  AXPlatformPositionInstance position =
      owner()->CreatePositionAt(offset)->AsLeafTextPosition();
  if (position->GetAnchor() &&
      position->GetAnchor()->GetRole() == ui::AX_ROLE_INLINE_TEXT_BOX) {
    return position->CreateParentPosition();
    }
    return position;
}

//
// Private methods.
//

void BrowserAccessibilityComWin::UpdateStep1ComputeWinAttributes() {
  // Swap win_attributes_ to old_win_attributes_, allowing us to see
  // exactly what changed and fire appropriate events. Note that
  // old_win_attributes_ is cleared at the end of UpdateStep3FireEvents.
  old_win_attributes_.swap(win_attributes_);
  win_attributes_.reset(new WinAttributes());

  win_attributes_->ia_role = MSAARole();
  win_attributes_->ia_state = MSAAState();
  win_attributes_->role_name = base::UTF8ToUTF16(StringOverrideForMSAARole());

  win_attributes_->ia2_role = ComputeIA2Role();
  // If we didn't explicitly set the IAccessible2 role, make it the same
  // as the MSAA role.
  if (!win_attributes_->ia2_role)
    win_attributes_->ia2_role = win_attributes_->ia_role;

  win_attributes_->ia2_state = ComputeIA2State();
  win_attributes_->ia2_attributes = ComputeIA2Attributes();

  win_attributes_->name = owner()->GetString16Attribute(ui::AX_ATTR_NAME);

  win_attributes_->description =
      owner()->GetString16Attribute(ui::AX_ATTR_DESCRIPTION);

  base::string16 value = owner()->GetValue();

  // Expose slider value.
  if (IsRangeValueSupported()) {
    value = GetRangeValueText();
  } else if (owner()->IsDocument()) {
    // On Windows, the value of a document should be its url.
    value = base::UTF8ToUTF16(Manager()->GetTreeData().url);
  }
  // If this doesn't have a value and is linked then set its value to the url
  // attribute. This allows screen readers to read an empty link's
  // destination.
  if (value.empty() && (MSAAState() & STATE_SYSTEM_LINKED))
    value = owner()->GetString16Attribute(ui::AX_ATTR_URL);

  win_attributes_->value = value;

  CalculateRelationships();
}

void BrowserAccessibilityComWin::UpdateStep2ComputeHypertext() {
  if (owner()->IsSimpleTextControl()) {
    win_attributes_->hypertext = value();
    return;
  }

  if (!owner()->PlatformChildCount()) {
    if (owner()->IsRichTextControl()) {
      // We don't want to expose any associated label in IA2 Hypertext.
      return;
    }
    win_attributes_->hypertext = name();
    return;
  }

  // Construct the hypertext for this node, which contains the concatenation
  // of all of the static text and widespace of this node's children and an
  // embedded object character for all the other children. Build up a map from
  // the character index of each embedded object character to the id of the
  // child object it points to.
  for (unsigned int i = 0; i < owner()->PlatformChildCount(); ++i) {
    auto* child = ToBrowserAccessibilityComWin(owner()->PlatformGetChild(i));
    DCHECK(child);
    // Similar to Firefox, we don't expose text-only objects in IA2 hypertext.
    if (child->owner()->IsTextOnlyObject()) {
      win_attributes_->hypertext += child->name();
    } else {
      int32_t char_offset = static_cast<int32_t>(owner()->GetText().size());
      int32_t child_unique_id = child->unique_id();
      int32_t index = hyperlinks().size();
      win_attributes_->hyperlink_offset_to_index[char_offset] = index;
      win_attributes_->hyperlinks.push_back(child_unique_id);
      win_attributes_->hypertext += kEmbeddedCharacter;
    }
  }
}

void BrowserAccessibilityComWin::UpdateStep3FireEvents(
    bool is_subtree_creation) {
  // Fire an event when a new subtree is created.
  if (is_subtree_creation)
    FireNativeEvent(EVENT_OBJECT_SHOW);

  // The rest of the events only fire on changes, not on new objects.
  if (old_win_attributes_->ia_role != 0 ||
      !old_win_attributes_->role_name.empty()) {
    // Fire an event if the name, description, help, or value changes.
    if (name() != old_win_attributes_->name)
      FireNativeEvent(EVENT_OBJECT_NAMECHANGE);
    if (description() != old_win_attributes_->description)
      FireNativeEvent(EVENT_OBJECT_DESCRIPTIONCHANGE);
    if (value() != old_win_attributes_->value)
      FireNativeEvent(EVENT_OBJECT_VALUECHANGE);

    // Do not fire EVENT_OBJECT_STATECHANGE if the change was due to a focus
    // change.
    if ((MSAAState() & ~STATE_SYSTEM_FOCUSED) !=
        (old_win_attributes_->ia_state & ~STATE_SYSTEM_FOCUSED))
      FireNativeEvent(EVENT_OBJECT_STATECHANGE);

    // Handle selection being added or removed.
    bool is_selected_now = (MSAAState() & STATE_SYSTEM_SELECTED) != 0;
    bool was_selected_before =
        (old_win_attributes_->ia_state & STATE_SYSTEM_SELECTED) != 0;
    if (is_selected_now || was_selected_before) {
      bool multiselect = false;
      if (owner()->PlatformGetParent() &&
          owner()->PlatformGetParent()->HasState(ui::AX_STATE_MULTISELECTABLE))
        multiselect = true;

      if (multiselect) {
        // In a multi-select box, fire SELECTIONADD and SELECTIONREMOVE events.
        if (is_selected_now && !was_selected_before) {
          FireNativeEvent(EVENT_OBJECT_SELECTIONADD);
        } else if (!is_selected_now && was_selected_before) {
          FireNativeEvent(EVENT_OBJECT_SELECTIONREMOVE);
        }
      } else if (is_selected_now && !was_selected_before) {
        // In a single-select box, only fire SELECTION events.
        FireNativeEvent(EVENT_OBJECT_SELECTION);
      }
    }

    // Fire an event if this container object has scrolled.
    int sx = 0;
    int sy = 0;
    if (owner()->GetIntAttribute(ui::AX_ATTR_SCROLL_X, &sx) &&
        owner()->GetIntAttribute(ui::AX_ATTR_SCROLL_Y, &sy)) {
      if (sx != previous_scroll_x_ || sy != previous_scroll_y_)
        FireNativeEvent(EVENT_SYSTEM_SCROLLINGEND);
      previous_scroll_x_ = sx;
      previous_scroll_y_ = sy;
    }

    // Fire hypertext-related events.
    int start, old_len, new_len;
    ComputeHypertextRemovedAndInserted(&start, &old_len, &new_len);
    if (old_len > 0) {
      // In-process screen readers may call IAccessibleText::get_oldText
      // in reaction to this event to retrieve the text that was removed.
      FireNativeEvent(IA2_EVENT_TEXT_REMOVED);
    }
    if (new_len > 0) {
      // In-process screen readers may call IAccessibleText::get_newText
      // in reaction to this event to retrieve the text that was inserted.
      FireNativeEvent(IA2_EVENT_TEXT_INSERTED);
    }

    // Changing a static text node can affect the IAccessibleText hypertext
    // of the parent node, so force an update on the parent.
    BrowserAccessibilityComWin* parent =
        ToBrowserAccessibilityComWin(owner()->PlatformGetParent());
    if (parent && owner()->IsTextOnlyObject() &&
        name() != old_win_attributes_->name) {
      parent->owner()->UpdatePlatformAttributes();
    }
  }

  old_win_attributes_.reset(nullptr);
}

BrowserAccessibilityManager* BrowserAccessibilityComWin::Manager() const {
  DCHECK(owner());

  auto* manager = owner()->manager();
  DCHECK(manager);
  return manager;
}

//
// AXPlatformNode overrides
//
void BrowserAccessibilityComWin::Destroy() {
  // Detach BrowserAccessibilityWin from us.
  owner_ = nullptr;
  AXPlatformNodeWin::Destroy();
}

void BrowserAccessibilityComWin::Init(ui::AXPlatformNodeDelegate* delegate) {
  owner_ = static_cast<BrowserAccessibilityWin*>(delegate);
  AXPlatformNodeBase::Init(delegate);
}

std::vector<base::string16> BrowserAccessibilityComWin::ComputeTextAttributes()
    const {
  std::vector<base::string16> attributes;

  // We include list markers for now, but there might be other objects that are
  // auto generated.
  // TODO(nektar): Compute what objects are auto-generated in Blink.
  if (owner()->GetRole() == ui::AX_ROLE_LIST_MARKER)
    attributes.push_back(L"auto-generated:true");
  else
    attributes.push_back(L"auto-generated:false");

  int color;
  base::string16 color_value(L"transparent");
  if (owner()->GetIntAttribute(ui::AX_ATTR_BACKGROUND_COLOR, &color)) {
    unsigned int alpha = SkColorGetA(color);
    unsigned int red = SkColorGetR(color);
    unsigned int green = SkColorGetG(color);
    unsigned int blue = SkColorGetB(color);
    if (alpha) {
      color_value = L"rgb(" + base::UintToString16(red) + L',' +
                    base::UintToString16(green) + L',' +
                    base::UintToString16(blue) + L')';
    }
  }
  SanitizeStringAttributeForIA2(color_value, &color_value);
  attributes.push_back(L"background-color:" + color_value);

  if (owner()->GetIntAttribute(ui::AX_ATTR_COLOR, &color)) {
    unsigned int red = SkColorGetR(color);
    unsigned int green = SkColorGetG(color);
    unsigned int blue = SkColorGetB(color);
    color_value = L"rgb(" + base::UintToString16(red) + L',' +
                  base::UintToString16(green) + L',' +
                  base::UintToString16(blue) + L')';
  } else {
    color_value = L"rgb(0,0,0)";
  }
  SanitizeStringAttributeForIA2(color_value, &color_value);
  attributes.push_back(L"color:" + color_value);

  base::string16 font_family(
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_FONT_FAMILY));
  // Attribute has no default value.
  if (!font_family.empty()) {
    SanitizeStringAttributeForIA2(font_family, &font_family);
    attributes.push_back(L"font-family:" + font_family);
  }

  float font_size;
  // Attribute has no default value.
  if (GetFloatAttribute(ui::AX_ATTR_FONT_SIZE, &font_size)) {
    // The IA2 Spec requires the value to be in pt, not in pixels.
    // There are 72 points per inch.
    // We assume that there are 96 pixels per inch on a standard display.
    // TODO(nektar): Figure out the current value of pixels per inch.
    float points = font_size * 72.0 / 96.0;
    attributes.push_back(L"font-size:" +
                         base::UTF8ToUTF16(base::DoubleToString(points)) +
                         L"pt");
  }

  auto text_style = static_cast<ui::AXTextStyle>(
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_STYLE));
  if (text_style == ui::AX_TEXT_STYLE_NONE) {
    attributes.push_back(L"font-style:normal");
    attributes.push_back(L"font-weight:normal");
  } else {
    if (text_style & ui::AX_TEXT_STYLE_ITALIC) {
      attributes.push_back(L"font-style:italic");
    } else {
      attributes.push_back(L"font-style:normal");
    }

    if (text_style & ui::AX_TEXT_STYLE_BOLD) {
      attributes.push_back(L"font-weight:bold");
    } else {
      attributes.push_back(L"font-weight:normal");
    }
  }

  auto invalid_state = static_cast<ui::AXInvalidState>(
      owner()->GetIntAttribute(ui::AX_ATTR_INVALID_STATE));
  switch (invalid_state) {
    case ui::AX_INVALID_STATE_NONE:
    case ui::AX_INVALID_STATE_FALSE:
      attributes.push_back(L"invalid:false");
      break;
    case ui::AX_INVALID_STATE_TRUE:
      attributes.push_back(L"invalid:true");
      break;
    case ui::AX_INVALID_STATE_SPELLING:
    case ui::AX_INVALID_STATE_GRAMMAR: {
      base::string16 spelling_grammar_value;
      if (invalid_state & ui::AX_INVALID_STATE_SPELLING)
        spelling_grammar_value = L"spelling";
      else if (invalid_state & ui::AX_INVALID_STATE_GRAMMAR)
        spelling_grammar_value = L"grammar";
      else
        spelling_grammar_value = L"spelling,grammar";
      attributes.push_back(L"invalid:" + spelling_grammar_value);
      break;
    }
    case ui::AX_INVALID_STATE_OTHER: {
      base::string16 aria_invalid_value;
      if (owner()->GetString16Attribute(ui::AX_ATTR_ARIA_INVALID_VALUE,
                                        &aria_invalid_value)) {
        SanitizeStringAttributeForIA2(aria_invalid_value, &aria_invalid_value);
        attributes.push_back(L"invalid:" + aria_invalid_value);
      } else {
        // Set the attribute to L"true", since we cannot be more specific.
        attributes.push_back(L"invalid:true");
      }
      break;
    }
  }

  base::string16 language(
      owner()->GetInheritedString16Attribute(ui::AX_ATTR_LANGUAGE));
  // Default value should be L"en-US".
  if (language.empty()) {
    attributes.push_back(L"language:en-US");
  } else {
    SanitizeStringAttributeForIA2(language, &language);
    attributes.push_back(L"language:" + language);
  }

  // TODO(nektar): Add Blink support for the following attributes.
  // Currently set to their default values as dictated by the IA2 Spec.
  attributes.push_back(L"text-line-through-mode:continuous");
  if (text_style & ui::AX_TEXT_STYLE_LINE_THROUGH) {
    // TODO(nektar): Figure out a more specific value.
    attributes.push_back(L"text-line-through-style:solid");
  } else {
    attributes.push_back(L"text-line-through-style:none");
  }
  // Default value must be the empty string.
  attributes.push_back(L"text-line-through-text:");
  if (text_style & ui::AX_TEXT_STYLE_LINE_THROUGH) {
    // TODO(nektar): Figure out a more specific value.
    attributes.push_back(L"text-line-through-type:single");
  } else {
    attributes.push_back(L"text-line-through-type:none");
  }
  attributes.push_back(L"text-line-through-width:auto");
  attributes.push_back(L"text-outline:false");
  attributes.push_back(L"text-position:baseline");
  attributes.push_back(L"text-shadow:none");
  attributes.push_back(L"text-underline-mode:continuous");
  if (text_style & ui::AX_TEXT_STYLE_UNDERLINE) {
    // TODO(nektar): Figure out a more specific value.
    attributes.push_back(L"text-underline-style:solid");
    attributes.push_back(L"text-underline-type:single");
  } else {
    attributes.push_back(L"text-underline-style:none");
    attributes.push_back(L"text-underline-type:none");
  }
  attributes.push_back(L"text-underline-width:auto");

  auto text_direction = static_cast<ui::AXTextDirection>(
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_DIRECTION));
  switch (text_direction) {
    case ui::AX_TEXT_DIRECTION_NONE:
    case ui::AX_TEXT_DIRECTION_LTR:
      attributes.push_back(L"writing-mode:lr");
      break;
    case ui::AX_TEXT_DIRECTION_RTL:
      attributes.push_back(L"writing-mode:rl");
      break;
    case ui::AX_TEXT_DIRECTION_TTB:
      attributes.push_back(L"writing-mode:tb");
      break;
    case ui::AX_TEXT_DIRECTION_BTT:
      // Not listed in the IA2 Spec.
      attributes.push_back(L"writing-mode:bt");
      break;
  }

  return attributes;
}

BrowserAccessibilityComWin* BrowserAccessibilityComWin::NewReference() {
  AddRef();
  return this;
}

std::map<int, std::vector<base::string16>>
BrowserAccessibilityComWin::GetSpellingAttributes() {
  std::map<int, std::vector<base::string16>> spelling_attributes;
  if (owner()->IsTextOnlyObject()) {
    const std::vector<int32_t>& marker_types =
        owner()->GetIntListAttribute(ui::AX_ATTR_MARKER_TYPES);
    const std::vector<int>& marker_starts =
        owner()->GetIntListAttribute(ui::AX_ATTR_MARKER_STARTS);
    const std::vector<int>& marker_ends =
        owner()->GetIntListAttribute(ui::AX_ATTR_MARKER_ENDS);
    for (size_t i = 0; i < marker_types.size(); ++i) {
      if (!(static_cast<ui::AXMarkerType>(marker_types[i]) &
            ui::AX_MARKER_TYPE_SPELLING))
        continue;
      int start_offset = marker_starts[i];
      int end_offset = marker_ends[i];
      std::vector<base::string16> start_attributes;
      start_attributes.push_back(L"invalid:spelling");
      std::vector<base::string16> end_attributes;
      end_attributes.push_back(L"invalid:false");
      spelling_attributes[start_offset] = start_attributes;
      spelling_attributes[end_offset] = end_attributes;
    }
  }
  if (owner()->IsSimpleTextControl()) {
    int start_offset = 0;
    for (BrowserAccessibility* static_text =
             BrowserAccessibilityManager::NextTextOnlyObject(
                 owner()->InternalGetChild(0));
         static_text; static_text = static_text->GetNextSibling()) {
      auto* text_win = ToBrowserAccessibilityComWin(static_text);
      if (text_win) {
        std::map<int, std::vector<base::string16>> text_spelling_attributes =
            text_win->GetSpellingAttributes();
        for (auto& attribute : text_spelling_attributes) {
          spelling_attributes[start_offset + attribute.first] =
              std::move(attribute.second);
        }
        start_offset += static_cast<int>(text_win->owner()->GetText().length());
      }
    }
  }
  return spelling_attributes;
}

BrowserAccessibilityComWin* BrowserAccessibilityComWin::GetTargetFromChildID(
    const VARIANT& var_id) {
  if (!owner())
    return nullptr;

  if (var_id.vt != VT_I4)
    return nullptr;

  LONG child_id = var_id.lVal;
  if (child_id == CHILDID_SELF)
    return this;

  if (child_id >= 1 &&
      child_id <= static_cast<LONG>(owner()->PlatformChildCount()))
    return ToBrowserAccessibilityComWin(
        owner()->PlatformGetChild(child_id - 1));

  auto* child = static_cast<BrowserAccessibilityComWin*>(
      AXPlatformNodeWin::GetFromUniqueId(-child_id));
  if (child && child->owner()->IsDescendantOf(owner()))
    return child;

  return nullptr;
}

HRESULT BrowserAccessibilityComWin::GetStringAttributeAsBstr(
    ui::AXStringAttribute attribute,
    BSTR* value_bstr) {
  base::string16 str;
  if (!owner())
    return E_FAIL;

  if (!owner()->GetString16Attribute(attribute, &str))
    return S_FALSE;

  *value_bstr = SysAllocString(str.c_str());
  DCHECK(*value_bstr);

  return S_OK;
}

// Static
void BrowserAccessibilityComWin::SanitizeStringAttributeForIA2(
    const base::string16& input,
    base::string16* output) {
  DCHECK(output);
  // According to the IA2 Spec, these characters need to be escaped with a
  // backslash: backslash, colon, comma, equals and semicolon.
  // Note that backslash must be replaced first.
  base::ReplaceChars(input, L"\\", L"\\\\", output);
  base::ReplaceChars(*output, L":", L"\\:", output);
  base::ReplaceChars(*output, L",", L"\\,", output);
  base::ReplaceChars(*output, L"=", L"\\=", output);
  base::ReplaceChars(*output, L";", L"\\;", output);
}

void BrowserAccessibilityComWin::SetIA2HypertextSelection(LONG start_offset,
                                                          LONG end_offset) {
  HandleSpecialTextOffset(&start_offset);
  HandleSpecialTextOffset(&end_offset);
  AXPlatformPositionInstance start_position =
      CreatePositionForSelectionAt(static_cast<int>(start_offset));
  AXPlatformPositionInstance end_position =
      CreatePositionForSelectionAt(static_cast<int>(end_offset));
  Manager()->SetSelection(
      AXPlatformRange(std::move(start_position), std::move(end_position)));
}

bool BrowserAccessibilityComWin::IsHyperlink() const {
  int32_t hyperlink_index = -1;
  auto* parent = owner()->PlatformGetParent();
  if (parent) {
    hyperlink_index =
        ToBrowserAccessibilityComWin(parent)->GetHyperlinkIndexFromChild(*this);
  }

  if (hyperlink_index >= 0)
    return true;
  return false;
}

BrowserAccessibilityComWin*
BrowserAccessibilityComWin::GetHyperlinkFromHypertextOffset(int offset) const {
  std::map<int32_t, int32_t>::iterator iterator =
      hyperlink_offset_to_index().find(offset);
  if (iterator == hyperlink_offset_to_index().end())
    return nullptr;

  int32_t index = iterator->second;
  DCHECK_GE(index, 0);
  DCHECK_LT(index, static_cast<int32_t>(hyperlinks().size()));
  int32_t id = hyperlinks()[index];
  auto* hyperlink = static_cast<BrowserAccessibilityComWin*>(
      AXPlatformNodeWin::GetFromUniqueId(id));
  if (!hyperlink)
    return nullptr;
  return hyperlink;
}

int32_t BrowserAccessibilityComWin::GetHyperlinkIndexFromChild(
    const BrowserAccessibilityComWin& child) const {
  if (hyperlinks().empty())
    return -1;

  auto iterator =
      std::find(hyperlinks().begin(), hyperlinks().end(), child.unique_id());
  if (iterator == hyperlinks().end())
    return -1;

  return static_cast<int32_t>(iterator - hyperlinks().begin());
}

int32_t BrowserAccessibilityComWin::GetHypertextOffsetFromHyperlinkIndex(
    int32_t hyperlink_index) const {
  for (auto& offset_index : hyperlink_offset_to_index()) {
    if (offset_index.second == hyperlink_index)
      return offset_index.first;
  }

  return -1;
}

int32_t BrowserAccessibilityComWin::GetHypertextOffsetFromChild(
    BrowserAccessibilityComWin& child) {
  DCHECK(child.owner()->PlatformGetParent() == owner());

  // Handle the case when we are dealing with a direct text-only child.
  // (Note that this object might be a platform leaf, e.g. an ARIA searchbox,
  // and so |owner()->InternalChild...| functions need to be used. Also,
  // direct text-only children should not be present at tree roots and so no
  // cross-tree traversal is necessary.)
  if (child.owner()->IsTextOnlyObject()) {
    int32_t hypertextOffset = 0;
    int32_t index_in_parent = child.GetIndexInParent();
    DCHECK_GE(index_in_parent, 0);
    DCHECK_LT(index_in_parent,
              static_cast<int32_t>(owner()->InternalChildCount()));
    for (uint32_t i = 0; i < static_cast<uint32_t>(index_in_parent); ++i) {
      const BrowserAccessibilityComWin* sibling =
          ToBrowserAccessibilityComWin(owner()->InternalGetChild(i));
      DCHECK(sibling);
      if (sibling->owner()->IsTextOnlyObject())
        hypertextOffset += sibling->owner()->GetText().size();
      else
        ++hypertextOffset;
    }
    return hypertextOffset;
  }

  int32_t hyperlink_index = GetHyperlinkIndexFromChild(child);
  if (hyperlink_index < 0)
    return -1;

  return GetHypertextOffsetFromHyperlinkIndex(hyperlink_index);
}

int32_t BrowserAccessibilityComWin::GetHypertextOffsetFromDescendant(
    const BrowserAccessibilityComWin& descendant) const {
  auto* parent_object =
      ToBrowserAccessibilityComWin(descendant.owner()->PlatformGetParent());
  auto* current_object = const_cast<BrowserAccessibilityComWin*>(&descendant);
  while (parent_object && parent_object != this) {
    current_object = parent_object;
    parent_object = ToBrowserAccessibilityComWin(
        current_object->owner()->PlatformGetParent());
  }
  if (!parent_object)
    return -1;

  return parent_object->GetHypertextOffsetFromChild(*current_object);
}

int BrowserAccessibilityComWin::GetHypertextOffsetFromEndpoint(
    const BrowserAccessibilityComWin& endpoint_object,
    int endpoint_offset) const {
  // There are three cases:
  // 1. Either the selection endpoint is inside this object or is an ancestor of
  // of this object. endpoint_offset should be returned.
  // 2. The selection endpoint is a pure descendant of this object. The offset
  // of the character corresponding to the subtree in which the endpoint is
  // located should be returned.
  // 3. The selection endpoint is in a completely different part of the tree.
  // Either 0 or text_length should be returned depending on the direction that
  // one needs to travel to find the endpoint.

  // Case 1.
  //
  // IsDescendantOf includes the case when endpoint_object == this.
  if (owner()->IsDescendantOf(endpoint_object.owner()))
    return endpoint_offset;

  const BrowserAccessibility* common_parent = owner();
  int32_t index_in_common_parent = owner()->GetIndexInParent();
  while (common_parent &&
         !endpoint_object.owner()->IsDescendantOf(common_parent)) {
    index_in_common_parent = common_parent->GetIndexInParent();
    common_parent = common_parent->PlatformGetParent();
  }
  if (!common_parent)
    return -1;

  DCHECK_GE(index_in_common_parent, 0);
  DCHECK(!(common_parent->IsTextOnlyObject()));

  // Case 2.
  //
  // We already checked in case 1 if our endpoint is inside this object.
  // We can safely assume that it is a descendant or in a completely different
  // part of the tree.
  if (common_parent == owner()) {
    int32_t hypertext_offset =
        GetHypertextOffsetFromDescendant(endpoint_object);
    if (endpoint_object.owner()->PlatformGetParent() == owner() &&
        endpoint_object.owner()->IsTextOnlyObject()) {
      hypertext_offset += endpoint_offset;
    }

    return hypertext_offset;
  }

  // Case 3.
  //
  // We can safely assume that the endpoint is in another part of the tree or
  // at common parent, and that this object is a descendant of common parent.
  int32_t endpoint_index_in_common_parent = -1;
  for (uint32_t i = 0; i < common_parent->InternalChildCount(); ++i) {
    const BrowserAccessibility* child = common_parent->InternalGetChild(i);
    DCHECK(child);
    if (endpoint_object.owner()->IsDescendantOf(child)) {
      endpoint_index_in_common_parent = child->GetIndexInParent();
      break;
    }
  }
  DCHECK_GE(endpoint_index_in_common_parent, 0);

  if (endpoint_index_in_common_parent < index_in_common_parent)
    return 0;
  if (endpoint_index_in_common_parent > index_in_common_parent)
    return owner()->GetText().size();

  NOTREACHED();
  return -1;
}

int BrowserAccessibilityComWin::GetSelectionAnchor() const {
  int32_t anchor_id = Manager()->GetTreeData().sel_anchor_object_id;
  const BrowserAccessibilityComWin* anchor_object = GetFromID(anchor_id);
  if (!anchor_object)
    return -1;

  int anchor_offset = Manager()->GetTreeData().sel_anchor_offset;
  return GetHypertextOffsetFromEndpoint(*anchor_object, anchor_offset);
}

int BrowserAccessibilityComWin::GetSelectionFocus() const {
  int32_t focus_id = Manager()->GetTreeData().sel_focus_object_id;
  const BrowserAccessibilityComWin* focus_object = GetFromID(focus_id);
  if (!focus_object)
    return -1;

  int focus_offset = Manager()->GetTreeData().sel_focus_offset;
  return GetHypertextOffsetFromEndpoint(*focus_object, focus_offset);
}

void BrowserAccessibilityComWin::GetSelectionOffsets(int* selection_start,
                                                     int* selection_end) const {
  DCHECK(selection_start && selection_end);

  if (owner()->IsSimpleTextControl() &&
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_START, selection_start) &&
      owner()->GetIntAttribute(ui::AX_ATTR_TEXT_SEL_END, selection_end)) {
    return;
  }

  *selection_start = GetSelectionAnchor();
  *selection_end = GetSelectionFocus();
  if (*selection_start < 0 || *selection_end < 0)
    return;

  // There are three cases when a selection would start and end on the same
  // character:
  // 1. Anchor and focus are both in a subtree that is to the right of this
  // object.
  // 2. Anchor and focus are both in a subtree that is to the left of this
  // object.
  // 3. Anchor and focus are in a subtree represented by a single embedded
  // object character.
  // Only case 3 refers to a valid selection because cases 1 and 2 fall
  // outside this object in their entirety.
  // Selections that span more than one character are by definition inside this
  // object, so checking them is not necessary.
  if (*selection_start == *selection_end && !owner()->HasCaret()) {
    *selection_start = -1;
    *selection_end = -1;
    return;
  }

  // The IA2 Spec says that if the largest of the two offsets falls on an
  // embedded object character and if there is a selection in that embedded
  // object, it should be incremented by one so that it points after the
  // embedded object character.
  // This is a signal to AT software that the embedded object is also part of
  // the selection.
  int* largest_offset =
      (*selection_start <= *selection_end) ? selection_end : selection_start;
  BrowserAccessibilityComWin* hyperlink =
      GetHyperlinkFromHypertextOffset(*largest_offset);
  if (!hyperlink)
    return;

  LONG n_selections = 0;
  HRESULT hr = hyperlink->get_nSelections(&n_selections);
  DCHECK(SUCCEEDED(hr));
  if (n_selections > 0)
    ++(*largest_offset);
}

bool BrowserAccessibilityComWin::IsSameHypertextCharacter(
    size_t old_char_index,
    size_t new_char_index) {
  CHECK(old_win_attributes_);

  // For anything other than the "embedded character", we just compare the
  // characters directly.
  base::char16 old_ch = old_win_attributes_->hypertext[old_char_index];
  base::char16 new_ch = win_attributes_->hypertext[new_char_index];
  if (old_ch != new_ch)
    return false;
  if (old_ch == new_ch && new_ch != kEmbeddedCharacter)
    return true;

  // If it's an embedded character, they're only identical if the child id
  // the hyperlink points to is the same.
  std::map<int32_t, int32_t>& old_offset_to_index =
      old_win_attributes_->hyperlink_offset_to_index;
  std::vector<int32_t>& old_hyperlinks = old_win_attributes_->hyperlinks;
  int32_t old_hyperlinks_count = static_cast<int32_t>(old_hyperlinks.size());
  std::map<int32_t, int32_t>::iterator iter;
  iter = old_offset_to_index.find(old_char_index);
  int old_index = (iter != old_offset_to_index.end()) ? iter->second : -1;
  int old_child_id = (old_index >= 0 && old_index < old_hyperlinks_count)
                         ? old_hyperlinks[old_index]
                         : -1;

  std::map<int32_t, int32_t>& new_offset_to_index =
      win_attributes_->hyperlink_offset_to_index;
  std::vector<int32_t>& new_hyperlinks = win_attributes_->hyperlinks;
  int32_t new_hyperlinks_count = static_cast<int32_t>(new_hyperlinks.size());
  iter = new_offset_to_index.find(new_char_index);
  int new_index = (iter != new_offset_to_index.end()) ? iter->second : -1;
  int new_child_id = (new_index >= 0 && new_index < new_hyperlinks_count)
                         ? new_hyperlinks[new_index]
                         : -1;

  return old_child_id == new_child_id;
}

void BrowserAccessibilityComWin::ComputeHypertextRemovedAndInserted(
    int* start,
    int* old_len,
    int* new_len) {
  CHECK(old_win_attributes_);

  *start = 0;
  *old_len = 0;
  *new_len = 0;

  const base::string16& old_text = old_win_attributes_->hypertext;
  const base::string16& new_text = owner()->GetText();

  size_t common_prefix = 0;
  while (common_prefix < old_text.size() && common_prefix < new_text.size() &&
         IsSameHypertextCharacter(common_prefix, common_prefix)) {
    ++common_prefix;
  }

  size_t common_suffix = 0;
  while (common_prefix + common_suffix < old_text.size() &&
         common_prefix + common_suffix < new_text.size() &&
         IsSameHypertextCharacter(old_text.size() - common_suffix - 1,
                                  new_text.size() - common_suffix - 1)) {
    ++common_suffix;
  }

  *start = common_prefix;
  *old_len = old_text.size() - common_prefix - common_suffix;
  *new_len = new_text.size() - common_prefix - common_suffix;
}

void BrowserAccessibilityComWin::HandleSpecialTextOffset(LONG* offset) {
  if (*offset == IA2_TEXT_OFFSET_LENGTH) {
    *offset = static_cast<LONG>(owner()->GetText().length());
  } else if (*offset == IA2_TEXT_OFFSET_CARET) {
    // We shouldn't call |get_caretOffset| here as it affects UMA counts.
    int selection_start, selection_end;
    GetSelectionOffsets(&selection_start, &selection_end);
    *offset = selection_end;
  }
}

ui::TextBoundaryType BrowserAccessibilityComWin::IA2TextBoundaryToTextBoundary(
    IA2TextBoundaryType ia2_boundary) {
  switch (ia2_boundary) {
    case IA2_TEXT_BOUNDARY_CHAR:
      return ui::CHAR_BOUNDARY;
    case IA2_TEXT_BOUNDARY_WORD:
      return ui::WORD_BOUNDARY;
    case IA2_TEXT_BOUNDARY_LINE:
      return ui::LINE_BOUNDARY;
    case IA2_TEXT_BOUNDARY_SENTENCE:
      return ui::SENTENCE_BOUNDARY;
    case IA2_TEXT_BOUNDARY_PARAGRAPH:
      return ui::PARAGRAPH_BOUNDARY;
    case IA2_TEXT_BOUNDARY_ALL:
      return ui::ALL_BOUNDARY;
  }
  NOTREACHED();
  return ui::CHAR_BOUNDARY;
}

LONG BrowserAccessibilityComWin::FindBoundary(
    IA2TextBoundaryType ia2_boundary,
    LONG start_offset,
    ui::TextBoundaryDirection direction) {
  // If the boundary is relative to the caret, use the selection
  // affinity, otherwise default to downstream affinity.
  ui::AXTextAffinity affinity =
      start_offset == IA2_TEXT_OFFSET_CARET
          ? Manager()->GetTreeData().sel_focus_affinity
          : ui::AX_TEXT_AFFINITY_DOWNSTREAM;

  HandleSpecialTextOffset(&start_offset);
  if (ia2_boundary == IA2_TEXT_BOUNDARY_WORD) {
    switch (direction) {
      case ui::FORWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance next_word =
            position->CreateNextWordStartPosition();
        if (next_word->anchor_id() != owner()->GetId())
          next_word = position->CreatePositionAtEndOfAnchor();
        return next_word->text_offset();
      }
      case ui::BACKWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance previous_word;
        if (!position->AtStartOfWord()) {
          previous_word = position->CreatePreviousWordStartPosition();
          if (previous_word->anchor_id() != owner()->GetId())
            previous_word = position->CreatePositionAtStartOfAnchor();
        } else {
          previous_word = std::move(position);
        }
        return previous_word->text_offset();
      }
    }
  }

  if (ia2_boundary == IA2_TEXT_BOUNDARY_LINE) {
    switch (direction) {
      case ui::FORWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance next_line =
            position->CreateNextLineStartPosition();
        if (next_line->anchor_id() != owner()->GetId())
          next_line = position->CreatePositionAtEndOfAnchor();
        return next_line->text_offset();
      }
      case ui::BACKWARDS_DIRECTION: {
        AXPlatformPositionInstance position =
            owner()->CreatePositionAt(static_cast<int>(start_offset), affinity);
        AXPlatformPositionInstance previous_line;
        if (!position->AtStartOfLine()) {
          previous_line = position->CreatePreviousLineStartPosition();
          if (previous_line->anchor_id() != owner()->GetId())
            previous_line = position->CreatePositionAtStartOfAnchor();
        } else {
          previous_line = std::move(position);
        }
        return previous_line->text_offset();
      }
    }
  }

  // TODO(nektar): |AXPosition| can handle other types of boundaries as well.
  ui::TextBoundaryType boundary = IA2TextBoundaryToTextBoundary(ia2_boundary);
  return ui::FindAccessibleTextBoundary(
      owner()->GetText(), owner()->GetLineStartOffsets(), boundary,
      start_offset, direction, affinity);
}

LONG BrowserAccessibilityComWin::FindStartOfStyle(
    LONG start_offset,
    ui::TextBoundaryDirection direction) const {
  LONG text_length = static_cast<LONG>(owner()->GetText().length());
  DCHECK_GE(start_offset, 0);
  DCHECK_LE(start_offset, text_length);

  switch (direction) {
    case ui::BACKWARDS_DIRECTION: {
      if (offset_to_text_attributes().empty())
        return 0;

      auto iterator = offset_to_text_attributes().upper_bound(start_offset);
      --iterator;
      return static_cast<LONG>(iterator->first);
    }
    case ui::FORWARDS_DIRECTION: {
      const auto iterator =
          offset_to_text_attributes().upper_bound(start_offset);
      if (iterator == offset_to_text_attributes().end())
        return text_length;
      return static_cast<LONG>(iterator->first);
    }
  }

  NOTREACHED();
  return start_offset;
}

BrowserAccessibilityComWin* BrowserAccessibilityComWin::GetFromID(
    int32_t id) const {
  if (!owner())
    return nullptr;
  return ToBrowserAccessibilityComWin(Manager()->GetFromID(id));
}

bool BrowserAccessibilityComWin::IsListBoxOptionOrMenuListOption() {
  if (!owner()->PlatformGetParent())
    return false;

  int32_t role = owner()->GetRole();
  int32_t parent_role = owner()->PlatformGetParent()->GetRole();

  if (role == ui::AX_ROLE_LIST_BOX_OPTION &&
      parent_role == ui::AX_ROLE_LIST_BOX) {
    return true;
  }

  if (role == ui::AX_ROLE_MENU_LIST_OPTION &&
      parent_role == ui::AX_ROLE_MENU_LIST_POPUP) {
    return true;
  }

  return false;
}

void BrowserAccessibilityComWin::FireNativeEvent(LONG win_event_type) const {
  (new BrowserAccessibilityEventWin(BrowserAccessibilityEvent::FromTreeChange,
                                    ui::AX_EVENT_NONE, win_event_type, owner()))
      ->Fire();
}

BrowserAccessibilityComWin* ToBrowserAccessibilityComWin(
    BrowserAccessibility* obj) {
  if (!obj || !obj->IsNative())
    return nullptr;
  auto* result = static_cast<BrowserAccessibilityWin*>(obj)->GetCOM();
  return result;
}

}  // namespace content
