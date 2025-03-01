// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_WIN_H_
#define UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_WIN_H_

#include <atlbase.h>
#include <atlcom.h>
#include <oleacc.h>
#include <vector>

#include "base/compiler_specific.h"
#include "base/metrics/histogram_macros.h"
#include "base/observer_list.h"
#include "base/win/scoped_comptr.h"
#include "third_party/iaccessible2/ia2_api_all.h"
#include "ui/accessibility/ax_export.h"
#include "ui/accessibility/ax_text_utils.h"
#include "ui/accessibility/platform/ax_platform_node_base.h"

// IMPORTANT!
// These values are written to logs.  Do not renumber or delete
// existing items; add new entries to the end of the list.
enum {
  UMA_API_ACC_DO_DEFAULT_ACTION = 0,
  UMA_API_ACC_HIT_TEST = 1,
  UMA_API_ACC_LOCATION = 2,
  UMA_API_ACC_NAVIGATE = 3,
  UMA_API_ACC_SELECT = 4,
  UMA_API_ADD_SELECTION = 5,
  UMA_API_CONVERT_RETURNED_ELEMENT = 6,
  UMA_API_DO_ACTION = 7,
  UMA_API_GET_ACCESSIBLE_AT = 8,
  UMA_API_GET_ACC_CHILD = 9,
  UMA_API_GET_ACC_CHILD_COUNT = 10,
  UMA_API_GET_ACC_DEFAULT_ACTION = 11,
  UMA_API_GET_ACC_DESCRIPTION = 12,
  UMA_API_GET_ACC_FOCUS = 13,
  UMA_API_GET_ACC_HELP = 14,
  UMA_API_GET_ACC_HELP_TOPIC = 15,
  UMA_API_GET_ACC_KEYBOARD_SHORTCUT = 16,
  UMA_API_GET_ACC_NAME = 17,
  UMA_API_GET_ACC_PARENT = 18,
  UMA_API_GET_ACC_ROLE = 19,
  UMA_API_GET_ACC_SELECTION = 20,
  UMA_API_GET_ACC_STATE = 21,
  UMA_API_GET_ACC_VALUE = 22,
  UMA_API_GET_ANCHOR = 23,
  UMA_API_GET_ANCHOR_TARGET = 24,
  UMA_API_GET_APP_NAME = 25,
  UMA_API_GET_APP_VERSION = 26,
  UMA_API_GET_ATTRIBUTES_FOR_NAMES = 27,
  UMA_API_GET_CAPTION = 28,
  UMA_API_GET_CARET_OFFSET = 29,
  UMA_API_GET_CELL_AT = 30,
  UMA_API_GET_CHARACTER_EXTENTS = 31,
  UMA_API_GET_CHILD_AT = 32,
  UMA_API_GET_CHILD_INDEX = 33,
  UMA_API_GET_CLIPPED_SUBSTRING_BOUNDS = 34,
  UMA_API_GET_COLUMN_DESCRIPTION = 35,
  UMA_API_GET_COLUMN_EXTENT = 36,
  UMA_API_GET_COLUMN_EXTENT_AT = 37,
  UMA_API_GET_COLUMN_HEADER = 38,
  UMA_API_GET_COLUMN_HEADER_CELLS = 39,
  UMA_API_GET_COLUMN_INDEX = 40,
  UMA_API_GET_COMPUTED_STYLE = 41,
  UMA_API_GET_COMPUTED_STYLE_FOR_PROPERTIES = 42,
  UMA_API_GET_CURRENT_VALUE = 43,
  UMA_API_GET_DESCRIPTION = 44,
  UMA_API_GET_DOC_TYPE = 45,
  UMA_API_GET_DOM_TEXT = 46,
  UMA_API_GET_END_INDEX = 47,
  UMA_API_GET_EXTENDED_ROLE = 48,
  UMA_API_GET_EXTENDED_STATES = 49,
  UMA_API_GET_FIRST_CHILD = 50,
  UMA_API_GET_FONT_FAMILY = 51,
  UMA_API_GET_GROUP_POSITION = 52,
  UMA_API_GET_HOST_RAW_ELEMENT_PROVIDER = 53,
  UMA_API_GET_HYPERLINK = 54,
  UMA_API_GET_HYPERLINK_INDEX = 55,
  UMA_API_GET_IACCESSIBLE_PAIR = 56,
  UMA_API_GET_IMAGE_POSITION = 57,
  UMA_API_GET_IMAGE_SIZE = 58,
  UMA_API_GET_INDEX_IN_PARENT = 59,
  UMA_API_GET_INNER_HTML = 60,
  UMA_API_GET_IS_COLUMN_SELECTED = 61,
  UMA_API_GET_IS_ROW_SELECTED = 62,
  UMA_API_GET_IS_SELECTED = 63,
  UMA_API_GET_KEY_BINDING = 64,
  UMA_API_GET_LANGUAGE = 65,
  UMA_API_GET_LAST_CHILD = 66,
  UMA_API_GET_LOCALE = 67,
  UMA_API_GET_LOCALIZED_EXTENDED_ROLE = 68,
  UMA_API_GET_LOCALIZED_EXTENDED_STATES = 69,
  UMA_API_GET_LOCALIZED_NAME = 70,
  UMA_API_GET_LOCAL_INTERFACE = 71,
  UMA_API_GET_MAXIMUM_VALUE = 72,
  UMA_API_GET_MIME_TYPE = 73,
  UMA_API_GET_MINIMUM_VALUE = 74,
  UMA_API_GET_NAME = 75,
  UMA_API_GET_NAMESPACE_URI_FOR_ID = 76,
  UMA_API_GET_NEW_TEXT = 77,
  UMA_API_GET_NEXT_SIBLING = 78,
  UMA_API_GET_NODE_INFO = 79,
  UMA_API_GET_N_CHARACTERS = 80,
  UMA_API_GET_N_COLUMNS = 81,
  UMA_API_GET_N_EXTENDED_STATES = 82,
  UMA_API_GET_N_HYPERLINKS = 83,
  UMA_API_GET_N_RELATIONS = 84,
  UMA_API_GET_N_ROWS = 85,
  UMA_API_GET_N_SELECTED_CELLS = 86,
  UMA_API_GET_N_SELECTED_CHILDREN = 87,
  UMA_API_GET_N_SELECTED_COLUMNS = 88,
  UMA_API_GET_N_SELECTED_ROWS = 89,
  UMA_API_GET_N_SELECTIONS = 90,
  UMA_API_GET_OBJECT_FOR_CHILD = 91,
  UMA_API_GET_OFFSET_AT_POINT = 92,
  UMA_API_GET_OLD_TEXT = 93,
  UMA_API_GET_PARENT_NODE = 94,
  UMA_API_GET_PATTERN_PROVIDER = 95,
  UMA_API_GET_PREVIOUS_SIBLING = 96,
  UMA_API_GET_PROPERTY_VALUE = 97,
  UMA_API_GET_PROVIDER_OPTIONS = 98,
  UMA_API_GET_RELATION = 99,
  UMA_API_GET_RELATIONS = 100,
  UMA_API_GET_ROW_COLUMN_EXTENTS = 101,
  UMA_API_GET_ROW_COLUMN_EXTENTS_AT_INDEX = 102,
  UMA_API_GET_ROW_DESCRIPTION = 103,
  UMA_API_GET_ROW_EXTENT = 104,
  UMA_API_GET_ROW_EXTENT_AT = 105,
  UMA_API_GET_ROW_HEADER = 106,
  UMA_API_GET_ROW_HEADER_CELLS = 107,
  UMA_API_GET_ROW_INDEX = 108,
  UMA_API_GET_RUNTIME_ID = 109,
  UMA_API_GET_SELECTED_CELLS = 110,
  UMA_API_GET_SELECTED_CHILDREN = 111,
  UMA_API_GET_SELECTED_COLUMNS = 112,
  UMA_API_GET_SELECTED_ROWS = 113,
  UMA_API_GET_SELECTION = 114,
  UMA_API_GET_START_INDEX = 115,
  UMA_API_GET_STATES = 116,
  UMA_API_GET_SUMMARY = 117,
  UMA_API_GET_TABLE = 118,
  UMA_API_GET_TEXT = 119,
  UMA_API_GET_TEXT_AFTER_OFFSET = 120,
  UMA_API_GET_TEXT_AT_OFFSET = 121,
  UMA_API_GET_TEXT_BEFORE_OFFSET = 122,
  UMA_API_GET_TITLE = 123,
  UMA_API_GET_TOOLKIT_NAME = 124,
  UMA_API_GET_TOOLKIT_VERSION = 125,
  UMA_API_GET_UNCLIPPED_SUBSTRING_BOUNDS = 126,
  UMA_API_GET_UNIQUE_ID = 127,
  UMA_API_GET_URL = 128,
  UMA_API_GET_VALID = 129,
  UMA_API_GET_WINDOW_HANDLE = 130,
  UMA_API_IA2_GET_ATTRIBUTES = 131,
  UMA_API_IA2_SCROLL_TO = 132,
  UMA_API_IAACTION_GET_DESCRIPTION = 133,
  UMA_API_IATEXT_GET_ATTRIBUTES = 134,
  UMA_API_ISIMPLEDOMNODE_GET_ATTRIBUTES = 135,
  UMA_API_ISIMPLEDOMNODE_SCROLL_TO = 136,
  UMA_API_N_ACTIONS = 137,
  UMA_API_PUT_ALTERNATE_VIEW_MEDIA_TYPES = 138,
  UMA_API_QUERY_SERVICE = 139,
  UMA_API_REMOVE_SELECTION = 140,
  UMA_API_ROLE = 141,
  UMA_API_SCROLL_SUBSTRING_TO = 142,
  UMA_API_SCROLL_SUBSTRING_TO_POINT = 143,
  UMA_API_SCROLL_TO_POINT = 144,
  UMA_API_SCROLL_TO_SUBSTRING = 145,
  UMA_API_SELECT_COLUMN = 146,
  UMA_API_SELECT_ROW = 147,
  UMA_API_SET_CARET_OFFSET = 148,
  UMA_API_SET_CURRENT_VALUE = 149,
  UMA_API_SET_SELECTION = 150,
  UMA_API_TABLE2_GET_SELECTED_COLUMNS = 151,
  UMA_API_TABLE2_GET_SELECTED_ROWS = 152,
  UMA_API_TABLECELL_GET_COLUMN_INDEX = 153,
  UMA_API_TABLECELL_GET_IS_SELECTED = 154,
  UMA_API_TABLECELL_GET_ROW_INDEX = 155,
  UMA_API_UNSELECT_COLUMN = 156,
  UMA_API_UNSELECT_ROW = 157,

  // This must always be the last enum. It's okay for its value to
  // increase, but none of the other enum values may change.
  UMA_API_MAX
};

#define WIN_ACCESSIBILITY_API_HISTOGRAM(enum_value) \
  UMA_HISTOGRAM_ENUMERATION("Accessibility.WinAPIs", enum_value, UMA_API_MAX)

namespace ui {
class AXPlatformNodeWin;

// A simple interface for a class that wants to be notified when IAccessible2
// is used by a client, a strong indication that full accessibility support
// should be enabled.
class AX_EXPORT IAccessible2UsageObserver {
 public:
  IAccessible2UsageObserver();
  virtual ~IAccessible2UsageObserver();
  virtual void OnIAccessible2Used() = 0;
};

// Get an observer list that allows modules across the codebase to
// listen to when usage of IAccessible2 is detected.
extern AX_EXPORT base::ObserverList<IAccessible2UsageObserver>&
    GetIAccessible2UsageObserverList();

//
// AXPlatformNodeRelationWin
//
// A simple implementation of IAccessibleRelation, used to represent
// a relationship between two accessible nodes in the tree.
//
class AXPlatformNodeRelationWin : public CComObjectRootEx<CComMultiThreadModel>,
                                  public IAccessibleRelation {
 public:
  BEGIN_COM_MAP(AXPlatformNodeRelationWin)
  COM_INTERFACE_ENTRY(IAccessibleRelation)
  END_COM_MAP()

  AXPlatformNodeRelationWin();
  virtual ~AXPlatformNodeRelationWin();

  void Initialize(ui::AXPlatformNodeWin* owner, const base::string16& type);
  void AddTarget(int target_id);
  void RemoveTarget(int target_id);

  // IAccessibleRelation methods.
  STDMETHODIMP get_relationType(BSTR* relation_type) override;
  STDMETHODIMP get_nTargets(long* n_targets) override;
  STDMETHODIMP get_target(long target_index, IUnknown** target) override;
  STDMETHODIMP get_targets(long max_targets,
                           IUnknown** targets,
                           long* n_targets) override;
  STDMETHODIMP get_localizedRelationType(BSTR* relation_type) override;

  // Accessors.
  const base::string16& get_type() const { return type_; }
  const std::vector<int>& get_target_ids() const { return target_ids_; }

 private:
  base::string16 type_;
  base::win::ScopedComPtr<ui::AXPlatformNodeWin> owner_;
  std::vector<int> target_ids_;
};

class AX_EXPORT __declspec(uuid("26f5641a-246d-457b-a96d-07f3fae6acf2"))
    AXPlatformNodeWin
    : public NON_EXPORTED_BASE(CComObjectRootEx<CComMultiThreadModel>),
      public IDispatchImpl<IAccessible2_2,
                           &IID_IAccessible2,
                           &LIBID_IAccessible2Lib>,
      public IAccessibleText,
      public IAccessibleTable,
      public IAccessibleTable2,
      public IAccessibleTableCell,
      public IServiceProvider,
      public AXPlatformNodeBase {
 public:
  BEGIN_COM_MAP(AXPlatformNodeWin)
    COM_INTERFACE_ENTRY2(IDispatch, IAccessible2_2)
    COM_INTERFACE_ENTRY(AXPlatformNodeWin)
    COM_INTERFACE_ENTRY(IAccessible)
    COM_INTERFACE_ENTRY(IAccessible2)
    COM_INTERFACE_ENTRY(IAccessible2_2)
    COM_INTERFACE_ENTRY(IAccessibleText)
    COM_INTERFACE_ENTRY(IAccessibleTable)
    COM_INTERFACE_ENTRY(IAccessibleTable2)
    COM_INTERFACE_ENTRY(IAccessibleTableCell)
    COM_INTERFACE_ENTRY(IServiceProvider)
  END_COM_MAP()

  ~AXPlatformNodeWin() override;

  // Clear node's current relationships and set them to the default values.
  void CalculateRelationships();
  static AXPlatformNode* GetFromUniqueId(int32_t unique_id);
  int32_t unique_id() const { return unique_id_; }

  // AXPlatformNode overrides.
  gfx::NativeViewAccessible GetNativeViewAccessible() override;
  void NotifyAccessibilityEvent(ui::AXEvent event_type) override;

  // AXPlatformNodeBase overrides.
  void Destroy() override;
  int GetIndexInParent() override;

  //
  // IAccessible methods.
  //

  // Retrieves the child element or child object at a given point on the screen.
  STDMETHODIMP accHitTest(LONG x_left, LONG y_top, VARIANT* child) override;

  // Performs the object's default action.
  STDMETHODIMP accDoDefaultAction(VARIANT var_id) override;

  // Retrieves the specified object's current screen location.
  STDMETHODIMP accLocation(LONG* x_left,
                           LONG* y_top,
                           LONG* width,
                           LONG* height,
                           VARIANT var_id) override;

  // Traverses to another UI element and retrieves the object.
  STDMETHODIMP accNavigate(LONG nav_dir, VARIANT start, VARIANT* end) override;

  // Retrieves an IDispatch interface pointer for the specified child.
  STDMETHODIMP get_accChild(VARIANT var_child, IDispatch** disp_child) override;

  // Retrieves the number of accessible children.
  STDMETHODIMP get_accChildCount(LONG* child_count) override;

  // Retrieves a string that describes the object's default action.
  STDMETHODIMP get_accDefaultAction(VARIANT var_id,
                                    BSTR* default_action) override;

  // Retrieves the tooltip description.
  STDMETHODIMP get_accDescription(VARIANT var_id, BSTR* desc) override;

  // Retrieves the object that has the keyboard focus.
  STDMETHODIMP get_accFocus(VARIANT* focus_child) override;

  // Retrieves the specified object's shortcut.
  STDMETHODIMP get_accKeyboardShortcut(VARIANT var_id,
                                       BSTR* access_key) override;

  // Retrieves the name of the specified object.
  STDMETHODIMP get_accName(VARIANT var_id, BSTR* name) override;

  // Retrieves the IDispatch interface of the object's parent.
  STDMETHODIMP get_accParent(IDispatch** disp_parent) override;

  // Retrieves information describing the role of the specified object.
  STDMETHODIMP get_accRole(VARIANT var_id, VARIANT* role) override;

  // Retrieves the current state of the specified object.
  STDMETHODIMP get_accState(VARIANT var_id, VARIANT* state) override;

  // Gets the help string for the specified object.
  STDMETHODIMP get_accHelp(VARIANT var_id, BSTR* help) override;

  // Retrieve or set the string value associated with the specified object.
  // Setting the value is not typically used by screen readers, but it's
  // used frequently by automation software.
  STDMETHODIMP get_accValue(VARIANT var_id, BSTR* value) override;
  STDMETHODIMP put_accValue(VARIANT var_id, BSTR new_value) override;

  // IAccessible methods not implemented.
  STDMETHODIMP get_accSelection(VARIANT* selected) override;
  STDMETHODIMP accSelect(LONG flags_sel, VARIANT var_id) override;
  STDMETHODIMP get_accHelpTopic(BSTR* help_file,
                                VARIANT var_id,
                                LONG* topic_id) override;
  STDMETHODIMP put_accName(VARIANT var_id, BSTR put_name) override;

  //
  // IAccessible2 methods.
  //

  STDMETHODIMP role(LONG* role) override;

  STDMETHODIMP get_states(AccessibleStates* states) override;

  STDMETHODIMP get_uniqueID(LONG* unique_id) override;

  STDMETHODIMP get_windowHandle(HWND* window_handle) override;

  STDMETHODIMP get_relationTargetsOfType(BSTR type,
                                         long max_targets,
                                         IUnknown*** targets,
                                         long* n_targets) override;

  STDMETHODIMP get_attributes(BSTR* attributes) override;

  STDMETHODIMP get_indexInParent(LONG* index_in_parent) override;

  STDMETHODIMP get_nRelations(LONG* n_relations) override;

  STDMETHODIMP get_relation(LONG relation_index,
                            IAccessibleRelation** relation) override;

  STDMETHODIMP get_relations(LONG max_relations,
                             IAccessibleRelation** relations,
                             LONG* n_relations) override;
  //
  // IAccessible2 methods not implemented.
  //

  STDMETHODIMP get_attribute(BSTR name, VARIANT* attribute) override;
  STDMETHODIMP get_extendedRole(BSTR* extended_role) override;
  STDMETHODIMP scrollTo(enum IA2ScrollType scroll_type) override;
  STDMETHODIMP scrollToPoint(enum IA2CoordinateType coordinate_type,
                             LONG x,
                             LONG y) override;
  STDMETHODIMP get_groupPosition(LONG* group_level,
                                 LONG* similar_items_in_group,
                                 LONG* position_in_group) override;
  STDMETHODIMP get_localizedExtendedRole(
      BSTR* localized_extended_role) override;
  STDMETHODIMP get_nExtendedStates(LONG* n_extended_states) override;
  STDMETHODIMP get_extendedStates(LONG max_extended_states,
                                  BSTR** extended_states,
                                  LONG* n_extended_states) override;
  STDMETHODIMP get_localizedExtendedStates(
      LONG max_localized_extended_states,
      BSTR** localized_extended_states,
      LONG* n_localized_extended_states) override;
  STDMETHODIMP get_locale(IA2Locale* locale) override;
  STDMETHODIMP get_accessibleWithCaret(IUnknown** accessible,
                                       long* caret_offset) override;

  //
  // IAccessibleText methods.
  //

  STDMETHODIMP get_nCharacters(LONG* n_characters) override;

  STDMETHODIMP get_caretOffset(LONG* offset) override;

  STDMETHODIMP get_nSelections(LONG* n_selections) override;

  STDMETHODIMP get_selection(LONG selection_index,
                             LONG* start_offset,
                             LONG* end_offset) override;

  STDMETHODIMP get_text(LONG start_offset,
                        LONG end_offset,
                        BSTR* text) override;

  STDMETHODIMP get_textAtOffset(LONG offset,
                                enum IA2TextBoundaryType boundary_type,
                                LONG* start_offset,
                                LONG* end_offset,
                                BSTR* text) override;

  STDMETHODIMP get_textBeforeOffset(LONG offset,
                                    enum IA2TextBoundaryType boundary_type,
                                    LONG* start_offset,
                                    LONG* end_offset,
                                    BSTR* text) override;

  STDMETHODIMP get_textAfterOffset(LONG offset,
                                   enum IA2TextBoundaryType boundary_type,
                                   LONG* start_offset,
                                   LONG* end_offset,
                                   BSTR* text) override;

  STDMETHODIMP get_offsetAtPoint(LONG x,
                                 LONG y,
                                 enum IA2CoordinateType coord_type,
                                 LONG* offset) override;

  //
  // IAccessibleTable methods.
  //

  // get_description - also used by IAccessibleImage

  STDMETHODIMP get_accessibleAt(long row,
                                long column,
                                IUnknown** accessible) override;

  STDMETHODIMP get_caption(IUnknown** accessible) override;

  STDMETHODIMP get_childIndex(long row_index,
                              long column_index,
                              long* cell_index) override;

  STDMETHODIMP get_columnDescription(long column, BSTR* description) override;

  STDMETHODIMP
  get_columnExtentAt(long row, long column, long* n_columns_spanned) override;

  STDMETHODIMP
  get_columnHeader(IAccessibleTable** accessible_table,
                   long* starting_row_index) override;

  STDMETHODIMP get_columnIndex(long cell_index, long* column_index) override;

  STDMETHODIMP get_nColumns(long* column_count) override;

  STDMETHODIMP get_nRows(long* row_count) override;

  STDMETHODIMP get_nSelectedChildren(long* cell_count) override;

  STDMETHODIMP get_nSelectedColumns(long* column_count) override;

  STDMETHODIMP get_nSelectedRows(long* row_count) override;

  STDMETHODIMP get_rowDescription(long row, BSTR* description) override;

  STDMETHODIMP get_rowExtentAt(long row,
                               long column,
                               long* n_rows_spanned) override;

  STDMETHODIMP
  get_rowHeader(IAccessibleTable** accessible_table,
                long* starting_column_index) override;

  STDMETHODIMP get_rowIndex(long cell_index, long* row_index) override;

  STDMETHODIMP get_selectedChildren(long max_children,
                                    long** children,
                                    long* n_children) override;

  STDMETHODIMP get_selectedColumns(long max_columns,
                                   long** columns,
                                   long* n_columns) override;

  STDMETHODIMP get_selectedRows(long max_rows,
                                long** rows,
                                long* n_rows) override;

  STDMETHODIMP get_summary(IUnknown** accessible) override;

  STDMETHODIMP
  get_isColumnSelected(long column, boolean* is_selected) override;

  STDMETHODIMP get_isRowSelected(long row, boolean* is_selected) override;

  STDMETHODIMP get_isSelected(long row,
                              long column,
                              boolean* is_selected) override;

  STDMETHODIMP
  get_rowColumnExtentsAtIndex(long index,
                              long* row,
                              long* column,
                              long* row_extents,
                              long* column_extents,
                              boolean* is_selected) override;

  STDMETHODIMP selectRow(long row) override;

  STDMETHODIMP selectColumn(long column) override;

  STDMETHODIMP unselectRow(long row) override;

  STDMETHODIMP unselectColumn(long column) override;

  STDMETHODIMP
  get_modelChange(IA2TableModelChange* model_change) override;

  //
  // IAccessibleTable2 methods.
  //
  // (Most of these are duplicates of IAccessibleTable methods, only the
  // unique ones are included here.)
  //

  STDMETHODIMP get_cellAt(long row, long column, IUnknown** cell) override;

  STDMETHODIMP get_nSelectedCells(long* cell_count) override;

  STDMETHODIMP
  get_selectedCells(IUnknown*** cells, long* n_selected_cells) override;

  STDMETHODIMP get_selectedColumns(long** columns, long* n_columns) override;

  STDMETHODIMP get_selectedRows(long** rows, long* n_rows) override;

  //
  // IAccessibleTableCell methods.
  //

  STDMETHODIMP
  get_columnExtent(long* n_columns_spanned) override;

  STDMETHODIMP
  get_columnHeaderCells(IUnknown*** cell_accessibles,
                        long* n_column_header_cells) override;

  STDMETHODIMP get_columnIndex(long* column_index) override;

  STDMETHODIMP get_rowExtent(long* n_rows_spanned) override;

  STDMETHODIMP
  get_rowHeaderCells(IUnknown*** cell_accessibles,
                     long* n_row_header_cells) override;

  STDMETHODIMP get_rowIndex(long* row_index) override;

  STDMETHODIMP get_isSelected(boolean* is_selected) override;

  STDMETHODIMP
  get_rowColumnExtents(long* row,
                       long* column,
                       long* row_extents,
                       long* column_extents,
                       boolean* is_selected) override;

  STDMETHODIMP get_table(IUnknown** table) override;

  //
  // IAccessibleText methods not implemented.
  //

  STDMETHODIMP get_newText(IA2TextSegment* new_text) override;
  STDMETHODIMP get_oldText(IA2TextSegment* old_text) override;
  STDMETHODIMP addSelection(LONG start_offset, LONG end_offset) override;
  STDMETHODIMP get_attributes(LONG offset,
                              LONG* start_offset,
                              LONG* end_offset,
                              BSTR* text_attributes) override;
  STDMETHODIMP get_characterExtents(LONG offset,
                                    enum IA2CoordinateType coord_type,
                                    LONG* x,
                                    LONG* y,
                                    LONG* width,
                                    LONG* height) override;
  STDMETHODIMP removeSelection(LONG selection_index) override;
  STDMETHODIMP setCaretOffset(LONG offset) override;
  STDMETHODIMP setSelection(LONG selection_index,
                            LONG start_offset,
                            LONG end_offset) override;
  STDMETHODIMP scrollSubstringTo(LONG start_index,
                                 LONG end_index,
                                 enum IA2ScrollType scroll_type) override;
  STDMETHODIMP scrollSubstringToPoint(LONG start_index,
                                      LONG end_index,
                                      enum IA2CoordinateType coordinate_type,
                                      LONG x,
                                      LONG y) override;

  //
  // IServiceProvider methods.
  //

  STDMETHODIMP QueryService(REFGUID guidService,
                            REFIID riid,
                            void** object) override;

 protected:
  AXPlatformNodeWin();

  int MSAAState();

  int MSAARole();
  std::string StringOverrideForMSAARole();

  int32_t ComputeIA2State();

  int32_t ComputeIA2Role();

  std::vector<base::string16> ComputeIA2Attributes();

  // AXPlatformNodeBase overrides.
  void Dispose() override;

 private:
  int32_t unique_id_;

  int MSAAEvent(ui::AXEvent event);
  bool IsWebAreaForPresentationalIframe();
  bool ShouldNodeHaveReadonlyStateByDefault(const AXNodeData& data) const;
  bool ShouldNodeHaveFocusableState(const AXNodeData& data) const;

  HRESULT GetStringAttributeAsBstr(
      ui::AXStringAttribute attribute,
      BSTR* value_bstr) const;

  // Escapes characters in string attributes as required by the IA2 Spec.
  // It's okay for input to be the same as output.
  static void SanitizeStringAttributeForIA2(const base::string16& input,
                                            base::string16* output);

  // Sets the selection given a start and end offset in IA2 Hypertext.
  void SetIA2HypertextSelection(LONG start_offset, LONG end_offset);

  // If the string attribute |attribute| is present, add its value as an
  // IAccessible2 attribute with the name |ia2_attr|.
  void StringAttributeToIA2(std::vector<base::string16>& attributes,
                            ui::AXStringAttribute attribute,
                            const char* ia2_attr);

  // If the bool attribute |attribute| is present, add its value as an
  // IAccessible2 attribute with the name |ia2_attr|.
  void BoolAttributeToIA2(std::vector<base::string16>& attributes,
                          ui::AXBoolAttribute attribute,
                          const char* ia2_attr);

  // If the int attribute |attribute| is present, add its value as an
  // IAccessible2 attribute with the name |ia2_attr|.
  void IntAttributeToIA2(std::vector<base::string16>& attributes,
                         ui::AXIntAttribute attribute,
                         const char* ia2_attr);

  void AddAlertTarget();
  void RemoveAlertTarget();

  // Return the text to use for IAccessibleText.
  base::string16 TextForIAccessibleText();

  // If offset is a member of IA2TextSpecialOffsets this function updates the
  // value of offset and returns, otherwise offset remains unchanged.
  void HandleSpecialTextOffset(LONG* offset);

  // Convert from a IA2TextBoundaryType to a ui::TextBoundaryType.
  ui::TextBoundaryType IA2TextBoundaryToTextBoundary(IA2TextBoundaryType type);

  // Search forwards (direction == 1) or backwards (direction == -1)
  // from the given offset until the given boundary is found, and
  // return the offset of that boundary.
  LONG FindBoundary(const base::string16& text,
                    IA2TextBoundaryType ia2_boundary,
                    LONG start_offset,
                    ui::TextBoundaryDirection direction);

  // Many MSAA methods take a var_id parameter indicating that the operation
  // should be performed on a particular child ID, rather than this object.
  // This method tries to figure out the target object from |var_id| and
  // returns a pointer to the target object if it exists, otherwise nullptr.
  // Does not return a new reference.
  AXPlatformNodeWin* GetTargetFromChildID(const VARIANT& var_id);

  // Returns true if this node is in a treegrid.
  bool IsInTreeGrid();

  //
  // For adding / removing IA2 relations.
  //
  void AddRelation(const base::string16& relation_type, int target_id);
  void AddBidirectionalRelations(const base::string16& relation_type,
                                 const base::string16& reverse_relation_type,
                                 ui::AXIntListAttribute attribute);
  void AddBidirectionalRelations(const base::string16& relation_type,
                                 const base::string16& reverse_relation_type,
                                 const std::vector<int32_t>& target_ids);
  // Clears all the forward relations from this object to any other object and
  // the associated  reverse relations on the other objects, but leaves any
  // reverse relations on this object alone.
  void ClearOwnRelations();
  void RemoveBidirectionalRelationsOfType(
      const base::string16& relation_type,
      const base::string16& reverse_relation_type);
  void RemoveTargetFromRelation(const base::string16& relation_type,
                                int target_id);

  // Helper method for returning selected indicies. It is expected that the
  // caller ensures that the input has been validated.
  HRESULT AllocateComArrayFromVector(std::vector<long>& results,
                                     long max,
                                     long** selected,
                                     long* n_selected);

  // Relationships between this node and other nodes.
  std::vector<ui::AXPlatformNodeRelationWin*> relations_;
};

}  // namespace ui

#endif  // UI_ACCESSIBILITY_PLATFORM_AX_PLATFORM_NODE_WIN_H_
