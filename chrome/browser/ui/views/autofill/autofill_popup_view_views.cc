// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/autofill/autofill_popup_view_views.h"

#include "base/optional.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller.h"
#include "chrome/browser/ui/autofill/autofill_popup_layout_model.h"
#include "chrome/grit/generated_resources.h"
#include "components/autofill/core/browser/popup_item_ids.h"
#include "components/autofill/core/browser/suggestion.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/border.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

namespace autofill {

namespace {

// Child view only for triggering accessibility events. Rendering is handled
// by |AutofillPopupViewViews|.
class AutofillPopupChildView : public views::View {
 public:
  explicit AutofillPopupChildView(const Suggestion& suggestion)
      : suggestion_(suggestion) {
    SetFocusBehavior(FocusBehavior::ALWAYS);
  }

 private:
  ~AutofillPopupChildView() override {}

  // views::Views implementation
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override {
    node_data->role = ui::AX_ROLE_MENU_ITEM;
    node_data->SetName(suggestion_.value);
  }

  const Suggestion& suggestion_;

  DISALLOW_COPY_AND_ASSIGN(AutofillPopupChildView);
};

}  // namespace

AutofillPopupViewViews::AutofillPopupViewViews(
    AutofillPopupController* controller,
    views::Widget* parent_widget)
    : AutofillPopupBaseView(controller, parent_widget),
      controller_(controller) {
  CreateChildViews();
  SetFocusBehavior(FocusBehavior::ALWAYS);
}

AutofillPopupViewViews::~AutofillPopupViewViews() {}

void AutofillPopupViewViews::Show() {
  DoShow();
  NotifyAccessibilityEvent(ui::AX_EVENT_MENU_START, true);
}

void AutofillPopupViewViews::Hide() {
  // The controller is no longer valid after it hides us.
  controller_ = NULL;
  DoHide();
  NotifyAccessibilityEvent(ui::AX_EVENT_MENU_END, true);
}

void AutofillPopupViewViews::OnSuggestionsChanged() {
  // We recreate the child views so we can be sure the |controller_|'s
  // |GetLineCount()| will match the number of child views. Otherwise,
  // the number of suggestions i.e. |GetLineCount()| may not match 1x1 with the
  // child views. See crbug.com/697466.
  CreateChildViews();
  DoUpdateBoundsAndRedrawPopup();
}

void AutofillPopupViewViews::OnPaint(gfx::Canvas* canvas) {
  if (!controller_)
    return;

  canvas->DrawColor(GetNativeTheme()->GetSystemColor(
      ui::NativeTheme::kColorId_ResultsTableNormalBackground));
  OnPaintBorder(canvas);

  DCHECK_EQ(controller_->GetLineCount(), child_count());
  for (int i = 0; i < controller_->GetLineCount(); ++i) {
    gfx::Rect line_rect = controller_->layout_model().GetRowBounds(i);

    if (controller_->GetSuggestionAt(i).frontend_id ==
        POPUP_ITEM_ID_SEPARATOR) {
      canvas->FillRect(
          line_rect,
          GetNativeTheme()->GetSystemColor(
              ui::NativeTheme::kColorId_ResultsTableNormalDimmedText));
    } else {
      DrawAutofillEntry(canvas, i, line_rect);
    }
  }
}

void AutofillPopupViewViews::OnSelectedRowChanged(
    base::Optional<int> previous_row_selection,
    base::Optional<int> current_row_selection) {
  SchedulePaint();

  if (current_row_selection) {
    DCHECK_LT(*current_row_selection, child_count());
    child_at(*current_row_selection)
        ->NotifyAccessibilityEvent(ui::AX_EVENT_SELECTION, true);
  }
}

/**
* Autofill entries in ltr.
*
* ............................................................................
* . ICON | HTTP WARNING MESSAGE VALUE                                | LABEL .
* ............................................................................
* . OTHER AUTOFILL ENTRY VALUE |                                LABEL | ICON .
* ............................................................................
*
* Autofill entries in rtl.
*
* ............................................................................
* . LABEL |                                HTTP WARNING MESSAGE VALUE | ICON .
* ............................................................................
* . ICON | LABEL                                | OTHER AUTOFILL ENTRY VALUE .
* ............................................................................
*
* Anyone who wants to modify the code below, remember to make sure that HTTP
* warning entry displays right. To trigger the warning message entry, enable
* #mark-non-secure-as flag as "display form warning", go to goo.gl/CEIjc6 with
* stored autofill info and check for credit card or password forms.
*/
void AutofillPopupViewViews::DrawAutofillEntry(gfx::Canvas* canvas,
                                               int index,
                                               const gfx::Rect& entry_rect) {
  canvas->FillRect(
      entry_rect,
      GetNativeTheme()->GetSystemColor(
          controller_->GetBackgroundColorIDForRow(index)));

  const int frontend_id = controller_->GetSuggestionAt(index).frontend_id;
  const bool is_http_warning =
      (frontend_id == POPUP_ITEM_ID_HTTP_NOT_SECURE_WARNING_MESSAGE);
  const bool icon_in_front_of_text =
      (is_http_warning ||
       frontend_id == POPUP_ITEM_ID_ALL_SAVED_PASSWORDS_ENTRY);
  const bool is_rtl = controller_->IsRTL();
  const int text_align =
      is_rtl ? gfx::Canvas::TEXT_ALIGN_RIGHT : gfx::Canvas::TEXT_ALIGN_LEFT;
  gfx::Rect value_rect = entry_rect;
  value_rect.Inset(AutofillPopupLayoutModel::kEndPadding, 0);

  // If the icon is on the right of the rect, no matter in RTL or LTR mode.
  bool icon_on_the_right = icon_in_front_of_text == is_rtl;
  int x_align_left = icon_on_the_right ? value_rect.right() : value_rect.x();

  // Draw the Autofill icon, if one exists
  int row_height = controller_->layout_model().GetRowBounds(index).height();
  if (!controller_->GetSuggestionAt(index).icon.empty()) {
    const gfx::ImageSkia image =
        controller_->layout_model().GetIconImage(index);
    int icon_y = entry_rect.y() + (row_height - image.height()) / 2;

    int icon_x_align_left =
        icon_on_the_right ? x_align_left - image.width() : x_align_left;

    canvas->DrawImageInt(image, icon_x_align_left, icon_y);

    // An icon was drawn; adjust the |x_align_left| value for the next element.
    if (icon_in_front_of_text) {
      x_align_left =
          icon_x_align_left +
          (is_rtl ? -AutofillPopupLayoutModel::kPaddingAfterLeadingIcon
                  : image.width() +
                        AutofillPopupLayoutModel::kPaddingAfterLeadingIcon);
    } else {
      x_align_left =
          icon_x_align_left +
          (is_rtl ? image.width() + AutofillPopupLayoutModel::kIconPadding
                  : -AutofillPopupLayoutModel::kIconPadding);
    }
  }

  // Draw the value text
  const int value_width = gfx::GetStringWidth(
      controller_->GetElidedValueAt(index),
      controller_->layout_model().GetValueFontListForRow(index));
  int value_x_align_left = x_align_left;

  if (icon_in_front_of_text) {
    value_x_align_left += is_rtl ? -value_width : 0;
  } else {
    value_x_align_left =
        is_rtl ? value_rect.right() - value_width : value_rect.x();
  }

  canvas->DrawStringRectWithFlags(
      controller_->GetElidedValueAt(index),
      controller_->layout_model().GetValueFontListForRow(index),
      GetNativeTheme()->GetSystemColor(
          controller_->layout_model().GetValueFontColorIDForRow(index)),
      gfx::Rect(value_x_align_left, value_rect.y(), value_width,
                value_rect.height()),
      text_align);

  // Draw the label text, if one exists.
  if (!controller_->GetSuggestionAt(index).label.empty()) {
    const int label_width = gfx::GetStringWidth(
        controller_->GetElidedLabelAt(index),
        controller_->layout_model().GetLabelFontListForRow(index));
    int label_x_align_left = x_align_left;

    if (is_http_warning) {
      label_x_align_left =
          is_rtl ? value_rect.x() : value_rect.right() - label_width;
    } else {
      label_x_align_left += is_rtl ? 0 : -label_width;
    }

    // TODO(crbug.com/678033):Add a GetLabelFontColorForRow function similar to
    // GetValueFontColorForRow so that the cocoa impl could use it too
    canvas->DrawStringRectWithFlags(
        controller_->GetElidedLabelAt(index),
        controller_->layout_model().GetLabelFontListForRow(index),
        GetNativeTheme()->GetSystemColor(
            ui::NativeTheme::kColorId_ResultsTableNormalDimmedText),
        gfx::Rect(label_x_align_left, entry_rect.y(), label_width,
                  entry_rect.height()),
        text_align);
  }
}

void AutofillPopupViewViews::GetAccessibleNodeData(ui::AXNodeData* node_data) {
  node_data->role = ui::AX_ROLE_MENU;
  node_data->SetName(
      l10n_util::GetStringUTF16(IDS_AUTOFILL_POPUP_ACCESSIBLE_NODE_DATA));
}

void AutofillPopupViewViews::CreateChildViews() {
  RemoveAllChildViews(true /* delete_children */);

  for (int i = 0; i < controller_->GetLineCount(); ++i) {
    AddChildView(new AutofillPopupChildView(controller_->GetSuggestionAt(i)));
  }
}

AutofillPopupView* AutofillPopupView::Create(
    AutofillPopupController* controller) {
  views::Widget* observing_widget =
      views::Widget::GetTopLevelWidgetForNativeView(
          controller->container_view());

  // If the top level widget can't be found, cancel the popup since we can't
  // fully set it up.
  if (!observing_widget)
    return NULL;

  return new AutofillPopupViewViews(controller, observing_widget);
}

}  // namespace autofill
