/**
 * This file is part of the theme implementation for form controls in WebCore.
 *
 * Copyright (C) 2005, 2006, 2007, 2008, 2009, 2010, 2012 Apple Computer, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/layout/LayoutTheme.h"

#include "core/CSSValueKeywords.h"
#include "core/HTMLNames.h"
#include "core/InputTypeNames.h"
#include "core/dom/Document.h"
#include "core/dom/ElementShadow.h"
#include "core/editing/FrameSelection.h"
#include "core/fileapi/FileList.h"
#include "core/frame/LocalFrame.h"
#include "core/frame/Settings.h"
#include "core/html/HTMLCollection.h"
#include "core/html/HTMLDataListElement.h"
#include "core/html/HTMLDataListOptionsCollection.h"
#include "core/html/HTMLFormControlElement.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLOptionElement.h"
#include "core/html/forms/SpinButtonElement.h"
#include "core/html/forms/TextControlInnerElements.h"
#include "core/html/parser/HTMLParserIdioms.h"
#include "core/html/shadow/ShadowElementNames.h"
#include "core/layout/LayoutBox.h"
#include "core/layout/LayoutThemeMobile.h"
#include "core/page/FocusController.h"
#include "core/page/Page.h"
#include "core/style/ComputedStyle.h"
#include "platform/FileMetadata.h"
#include "platform/LayoutTestSupport.h"
#include "platform/RuntimeEnabledFeatures.h"
#include "platform/Theme.h"
#include "platform/fonts/FontSelector.h"
#include "platform/graphics/TouchAction.h"
#include "platform/text/PlatformLocale.h"
#include "platform/text/StringTruncator.h"
#include "platform/wtf/text/StringBuilder.h"
#include "public/platform/Platform.h"
#include "public/platform/WebFallbackThemeEngine.h"
#include "public/platform/WebRect.h"

// The methods in this file are shared by all themes on every platform.

namespace blink {

using namespace HTMLNames;

LayoutTheme& LayoutTheme::GetTheme() {
  if (RuntimeEnabledFeatures::MobileLayoutThemeEnabled()) {
    DEFINE_STATIC_REF(LayoutTheme, layout_theme_mobile,
                      (LayoutThemeMobile::Create()));
    return *layout_theme_mobile;
  }
  return NativeTheme();
}

LayoutTheme::LayoutTheme(Theme* platform_theme)
    : has_custom_focus_ring_color_(false), platform_theme_(platform_theme) {}

void LayoutTheme::AdjustStyle(ComputedStyle& style, Element* e) {
  DCHECK(style.HasAppearance());

  // Force inline and table display styles to be inline-block (except for table-
  // which is block)
  ControlPart part = style.Appearance();
  if (style.Display() == EDisplay::kInline ||
      style.Display() == EDisplay::kInlineTable ||
      style.Display() == EDisplay::kTableRowGroup ||
      style.Display() == EDisplay::kTableHeaderGroup ||
      style.Display() == EDisplay::kTableFooterGroup ||
      style.Display() == EDisplay::kTableRow ||
      style.Display() == EDisplay::kTableColumnGroup ||
      style.Display() == EDisplay::kTableColumn ||
      style.Display() == EDisplay::kTableCell ||
      style.Display() == EDisplay::kTableCaption)
    style.SetDisplay(EDisplay::kInlineBlock);
  else if (style.Display() == EDisplay::kListItem ||
           style.Display() == EDisplay::kTable)
    style.SetDisplay(EDisplay::kBlock);

  if (IsControlStyled(style)) {
    if (part == kMenulistPart) {
      style.SetAppearance(kMenulistButtonPart);
      part = kMenulistButtonPart;
    } else {
      style.SetAppearance(kNoControlPart);
      return;
    }
  }

  if (ShouldUseFallbackTheme(style)) {
    AdjustStyleUsingFallbackTheme(style);
    return;
  }

  if (platform_theme_) {
    switch (part) {
      case kCheckboxPart:
      case kInnerSpinButtonPart:
      case kRadioPart:
      case kPushButtonPart:
      case kSquareButtonPart:
      case kButtonPart: {
        // Border
        LengthBox border_box(style.BorderTopWidth(), style.BorderRightWidth(),
                             style.BorderBottomWidth(),
                             style.BorderLeftWidth());
        border_box = platform_theme_->ControlBorder(
            part, style.GetFont().GetFontDescription(), border_box,
            style.EffectiveZoom());
        if (border_box.Top().Value() !=
            static_cast<int>(style.BorderTopWidth())) {
          if (border_box.Top().Value())
            style.SetBorderTopWidth(border_box.Top().Value());
          else
            style.ResetBorderTop();
        }
        if (border_box.Right().Value() !=
            static_cast<int>(style.BorderRightWidth())) {
          if (border_box.Right().Value())
            style.SetBorderRightWidth(border_box.Right().Value());
          else
            style.ResetBorderRight();
        }
        if (border_box.Bottom().Value() !=
            static_cast<int>(style.BorderBottomWidth())) {
          style.SetBorderBottomWidth(border_box.Bottom().Value());
          if (border_box.Bottom().Value())
            style.SetBorderBottomWidth(border_box.Bottom().Value());
          else
            style.ResetBorderBottom();
        }
        if (border_box.Left().Value() !=
            static_cast<int>(style.BorderLeftWidth())) {
          style.SetBorderLeftWidth(border_box.Left().Value());
          if (border_box.Left().Value())
            style.SetBorderLeftWidth(border_box.Left().Value());
          else
            style.ResetBorderLeft();
        }

        // Padding
        LengthBox padding_box = platform_theme_->ControlPadding(
            part, style.GetFont().GetFontDescription(), style.PaddingTop(),
            style.PaddingRight(), style.PaddingBottom(), style.PaddingLeft(),
            style.EffectiveZoom());
        if (!style.PaddingEqual(padding_box))
          style.SetPadding(padding_box);

        // Whitespace
        if (platform_theme_->ControlRequiresPreWhiteSpace(part))
          style.SetWhiteSpace(EWhiteSpace::kPre);

        // Width / Height
        // The width and height here are affected by the zoom.
        // FIXME: Check is flawed, since it doesn't take min-width/max-width
        // into account.
        LengthSize control_size = platform_theme_->GetControlSize(
            part, style.GetFont().GetFontDescription(),
            LengthSize(style.Width(), style.Height()), style.EffectiveZoom());
        if (control_size.Width() != style.Width())
          style.SetWidth(control_size.Width());
        if (control_size.Height() != style.Height())
          style.SetHeight(control_size.Height());

        // Min-Width / Min-Height
        LengthSize min_control_size = platform_theme_->MinimumControlSize(
            part, style.GetFont().GetFontDescription(), style.EffectiveZoom());
        if (min_control_size.Width() != style.MinWidth())
          style.SetMinWidth(min_control_size.Width());
        if (min_control_size.Height() != style.MinHeight())
          style.SetMinHeight(min_control_size.Height());

        // Font
        FontDescription control_font = platform_theme_->ControlFont(
            part, style.GetFont().GetFontDescription(), style.EffectiveZoom());
        if (control_font != style.GetFont().GetFontDescription()) {
          // Reset our line-height
          style.SetLineHeight(ComputedStyle::InitialLineHeight());

          // Now update our font.
          if (style.SetFontDescription(control_font))
            style.GetFont().Update(nullptr);
        }
        break;
      }
      case kProgressBarPart:
        AdjustProgressBarBounds(style);
        break;
      default:
        break;
    }
  }

  if (!platform_theme_) {
    // Call the appropriate style adjustment method based off the appearance
    // value.
    switch (style.Appearance()) {
      case kCheckboxPart:
        return AdjustCheckboxStyle(style);
      case kRadioPart:
        return AdjustRadioStyle(style);
      case kPushButtonPart:
      case kSquareButtonPart:
      case kButtonPart:
        return AdjustButtonStyle(style);
      case kInnerSpinButtonPart:
        return AdjustInnerSpinButtonStyle(style);
      default:
        break;
    }
  }

  // Call the appropriate style adjustment method based off the appearance
  // value.
  switch (style.Appearance()) {
    case kMenulistPart:
      return AdjustMenuListStyle(style, e);
    case kMenulistButtonPart:
      return AdjustMenuListButtonStyle(style, e);
    case kSliderHorizontalPart:
    case kSliderVerticalPart:
    case kMediaSliderPart:
    case kMediaVolumeSliderPart:
      return AdjustSliderContainerStyle(style, e);
    case kSliderThumbHorizontalPart:
    case kSliderThumbVerticalPart:
      return AdjustSliderThumbStyle(style);
    case kSearchFieldPart:
      return AdjustSearchFieldStyle(style);
    case kSearchFieldCancelButtonPart:
      return AdjustSearchFieldCancelButtonStyle(style);
    default:
      break;
  }
}

String LayoutTheme::ExtraDefaultStyleSheet() {
  return g_empty_string;
}

String LayoutTheme::ExtraQuirksStyleSheet() {
  return String();
}

String LayoutTheme::ExtraMediaControlsStyleSheet() {
  return String();
}

String LayoutTheme::ExtraFullscreenStyleSheet() {
  return String();
}

static String FormatChromiumMediaControlsTime(float time,
                                              float duration,
                                              bool include_separator) {
  if (!std::isfinite(time))
    time = 0;
  if (!std::isfinite(duration))
    duration = 0;
  int seconds = static_cast<int>(fabsf(time));
  int minutes = seconds / 60;

  seconds %= 60;

  // duration defines the format of how the time is rendered
  int duration_secs = static_cast<int>(fabsf(duration));
  int duration_mins = duration_secs / 60;

  // New UI includes a leading "/ " before duration.
  const char* separator = include_separator ? "/ " : "";

  // 0-9 minutes duration is 0:00
  // 10-99 minutes duration is 00:00
  // >99 minutes duration is 000:00
  if (duration_mins > 99 || minutes > 99)
    return String::Format("%s%s%03d:%02d", separator, (time < 0 ? "-" : ""),
                          minutes, seconds);
  if (duration_mins > 10)
    return String::Format("%s%s%02d:%02d", separator, (time < 0 ? "-" : ""),
                          minutes, seconds);

  return String::Format("%s%s%01d:%02d", separator, (time < 0 ? "-" : ""),
                        minutes, seconds);
}

String LayoutTheme::FormatMediaControlsTime(float time) const {
  return FormatChromiumMediaControlsTime(time, time, true);
}

String LayoutTheme::FormatMediaControlsCurrentTime(float current_time,
                                                   float duration) const {
  return FormatChromiumMediaControlsTime(current_time, duration, false);
}

Color LayoutTheme::ActiveSelectionBackgroundColor() const {
  return PlatformActiveSelectionBackgroundColor().BlendWithWhite();
}

Color LayoutTheme::InactiveSelectionBackgroundColor() const {
  return PlatformInactiveSelectionBackgroundColor().BlendWithWhite();
}

Color LayoutTheme::ActiveSelectionForegroundColor() const {
  return PlatformActiveSelectionForegroundColor();
}

Color LayoutTheme::InactiveSelectionForegroundColor() const {
  return PlatformInactiveSelectionForegroundColor();
}

Color LayoutTheme::ActiveListBoxSelectionBackgroundColor() const {
  return PlatformActiveListBoxSelectionBackgroundColor();
}

Color LayoutTheme::InactiveListBoxSelectionBackgroundColor() const {
  return PlatformInactiveListBoxSelectionBackgroundColor();
}

Color LayoutTheme::ActiveListBoxSelectionForegroundColor() const {
  return PlatformActiveListBoxSelectionForegroundColor();
}

Color LayoutTheme::InactiveListBoxSelectionForegroundColor() const {
  return PlatformInactiveListBoxSelectionForegroundColor();
}

Color LayoutTheme::PlatformSpellingMarkerUnderlineColor() const {
  return Color(255, 0, 0);
}

Color LayoutTheme::PlatformGrammarMarkerUnderlineColor() const {
  return Color(192, 192, 192);
}

Color LayoutTheme::PlatformActiveSpellingMarkerHighlightColor() const {
  return Color(255, 0, 0, 102);
}

Color LayoutTheme::PlatformActiveSelectionBackgroundColor() const {
  // Use a blue color by default if the platform theme doesn't define anything.
  return Color(0, 0, 255);
}

Color LayoutTheme::PlatformActiveSelectionForegroundColor() const {
  // Use a white color by default if the platform theme doesn't define anything.
  return Color::kWhite;
}

Color LayoutTheme::PlatformInactiveSelectionBackgroundColor() const {
  // Use a grey color by default if the platform theme doesn't define anything.
  // This color matches Firefox's inactive color.
  return Color(176, 176, 176);
}

Color LayoutTheme::PlatformInactiveSelectionForegroundColor() const {
  // Use a black color by default.
  return Color::kBlack;
}

Color LayoutTheme::PlatformActiveListBoxSelectionBackgroundColor() const {
  return PlatformActiveSelectionBackgroundColor();
}

Color LayoutTheme::PlatformActiveListBoxSelectionForegroundColor() const {
  return PlatformActiveSelectionForegroundColor();
}

Color LayoutTheme::PlatformInactiveListBoxSelectionBackgroundColor() const {
  return PlatformInactiveSelectionBackgroundColor();
}

Color LayoutTheme::PlatformInactiveListBoxSelectionForegroundColor() const {
  return PlatformInactiveSelectionForegroundColor();
}

LayoutUnit LayoutTheme::BaselinePosition(const LayoutObject* o) const {
  if (!o->IsBox())
    return LayoutUnit();

  const LayoutBox* box = ToLayoutBox(o);

  if (platform_theme_) {
    return box->Size().Height() + box->MarginTop() +
           LayoutUnit(platform_theme_->BaselinePositionAdjustment(
                          o->Style()->Appearance()) *
                      o->Style()->EffectiveZoom());
  }
  return box->Size().Height() + box->MarginTop();
}

bool LayoutTheme::IsControlContainer(ControlPart appearance) const {
  // There are more leaves than this, but we'll patch this function as we add
  // support for more controls.
  return appearance != kCheckboxPart && appearance != kRadioPart;
}

bool LayoutTheme::IsControlStyled(const ComputedStyle& style) const {
  switch (style.Appearance()) {
    case kPushButtonPart:
    case kSquareButtonPart:
    case kButtonPart:
    case kProgressBarPart:
      return style.HasAuthorBackground() || style.HasAuthorBorder();

    case kMenulistPart:
    case kSearchFieldPart:
    case kTextAreaPart:
    case kTextFieldPart:
      return style.HasAuthorBackground() || style.HasAuthorBorder() ||
             style.BoxShadow();

    default:
      return false;
  }
}

void LayoutTheme::AddVisualOverflow(const LayoutObject& object,
                                    IntRect& border_box) {
  if (platform_theme_)
    platform_theme_->AddVisualOverflow(
        object.Style()->Appearance(), ControlStatesForLayoutObject(object),
        object.Style()->EffectiveZoom(), border_box);
}

bool LayoutTheme::ShouldDrawDefaultFocusRing(
    const LayoutObject& layout_object) const {
  if (ThemeDrawsFocusRing(layout_object.StyleRef()))
    return false;
  Node* node = layout_object.GetNode();
  if (!node)
    return true;
  if (!layout_object.StyleRef().HasAppearance() && !node->IsLink())
    return true;
  // We can't use LayoutTheme::isFocused because outline:auto might be
  // specified to non-:focus rulesets.
  if (node->IsFocused() && !node->ShouldHaveFocusAppearance())
    return false;
  return true;
}

bool LayoutTheme::ControlStateChanged(LayoutObject& o,
                                      ControlState state) const {
  if (!o.StyleRef().HasAppearance())
    return false;

  // Default implementation assumes the controls don't respond to changes in
  // :hover state
  if (state == kHoverControlState && !SupportsHover(o.StyleRef()))
    return false;

  // Assume pressed state is only responded to if the control is enabled.
  if (state == kPressedControlState && !IsEnabled(o))
    return false;

  o.SetShouldDoFullPaintInvalidationIncludingNonCompositingDescendants();
  return true;
}

ControlStates LayoutTheme::ControlStatesForLayoutObject(const LayoutObject& o) {
  ControlStates result = 0;
  if (IsHovered(o)) {
    result |= kHoverControlState;
    if (IsSpinUpButtonPartHovered(o))
      result |= kSpinUpControlState;
  }
  if (IsPressed(o)) {
    result |= kPressedControlState;
    if (IsSpinUpButtonPartPressed(o))
      result |= kSpinUpControlState;
  }
  if (IsFocused(o) && o.Style()->OutlineStyleIsAuto())
    result |= kFocusControlState;
  if (IsEnabled(o))
    result |= kEnabledControlState;
  if (IsChecked(o))
    result |= kCheckedControlState;
  if (IsReadOnlyControl(o))
    result |= kReadOnlyControlState;
  if (!IsActive(o))
    result |= kWindowInactiveControlState;
  if (IsIndeterminate(o))
    result |= kIndeterminateControlState;
  return result;
}

bool LayoutTheme::IsActive(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node)
    return false;

  Page* page = node->GetDocument().GetPage();
  if (!page)
    return false;

  return page->GetFocusController().IsActive();
}

bool LayoutTheme::IsChecked(const LayoutObject& o) {
  if (!isHTMLInputElement(o.GetNode()))
    return false;
  return toHTMLInputElement(o.GetNode())->ShouldAppearChecked();
}

bool LayoutTheme::IsIndeterminate(const LayoutObject& o) {
  if (!isHTMLInputElement(o.GetNode()))
    return false;
  return toHTMLInputElement(o.GetNode())->ShouldAppearIndeterminate();
}

bool LayoutTheme::IsEnabled(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node || !node->IsElementNode())
    return true;
  return !ToElement(node)->IsDisabledFormControl();
}

bool LayoutTheme::IsFocused(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node)
    return false;

  node = node->FocusDelegate();
  Document& document = node->GetDocument();
  LocalFrame* frame = document.GetFrame();
  return node == document.FocusedElement() && node->IsFocused() &&
         node->ShouldHaveFocusAppearance() && frame &&
         frame->Selection().FrameIsFocusedAndActive();
}

bool LayoutTheme::IsPressed(const LayoutObject& o) {
  if (!o.GetNode())
    return false;
  return o.GetNode()->IsActive();
}

bool LayoutTheme::IsSpinUpButtonPartPressed(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node || !node->IsActive() || !node->IsElementNode() ||
      !ToElement(node)->IsSpinButtonElement())
    return false;
  SpinButtonElement* element = ToSpinButtonElement(node);
  return element->GetUpDownState() == SpinButtonElement::kUp;
}

bool LayoutTheme::IsReadOnlyControl(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node || !node->IsElementNode() ||
      !ToElement(node)->IsFormControlElement())
    return false;
  HTMLFormControlElement* element = ToHTMLFormControlElement(node);
  return element->IsReadOnly();
}

bool LayoutTheme::IsHovered(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node)
    return false;
  if (!node->IsElementNode() || !ToElement(node)->IsSpinButtonElement())
    return node->IsHovered();
  SpinButtonElement* element = ToSpinButtonElement(node);
  return element->IsHovered() &&
         element->GetUpDownState() != SpinButtonElement::kIndeterminate;
}

bool LayoutTheme::IsSpinUpButtonPartHovered(const LayoutObject& o) {
  Node* node = o.GetNode();
  if (!node || !node->IsElementNode() ||
      !ToElement(node)->IsSpinButtonElement())
    return false;
  SpinButtonElement* element = ToSpinButtonElement(node);
  return element->GetUpDownState() == SpinButtonElement::kUp;
}

void LayoutTheme::AdjustCheckboxStyle(ComputedStyle& style) const {
  // A summary of the rules for checkbox designed to match WinIE:
  // width/height - honored (WinIE actually scales its control for small widths,
  // but lets it overflow for small heights.)
  // font-size - not honored (control has no text), but we use it to decide
  // which control size to use.
  SetCheckboxSize(style);

  // padding - not honored by WinIE, needs to be removed.
  style.ResetPadding();

  // border - honored by WinIE, but looks terrible (just paints in the control
  // box and turns off the Windows XP theme) for now, we will not honor it.
  style.ResetBorder();
}

void LayoutTheme::AdjustRadioStyle(ComputedStyle& style) const {
  // A summary of the rules for checkbox designed to match WinIE:
  // width/height - honored (WinIE actually scales its control for small widths,
  // but lets it overflow for small heights.)
  // font-size - not honored (control has no text), but we use it to decide
  // which control size to use.
  SetRadioSize(style);

  // padding - not honored by WinIE, needs to be removed.
  style.ResetPadding();

  // border - honored by WinIE, but looks terrible (just paints in the control
  // box and turns off the Windows XP theme) for now, we will not honor it.
  style.ResetBorder();
}

void LayoutTheme::AdjustButtonStyle(ComputedStyle& style) const {}

void LayoutTheme::AdjustInnerSpinButtonStyle(ComputedStyle&) const {}

void LayoutTheme::AdjustMenuListStyle(ComputedStyle&, Element*) const {}

double LayoutTheme::AnimationRepeatIntervalForProgressBar() const {
  return 0;
}

double LayoutTheme::AnimationDurationForProgressBar() const {
  return 0;
}

bool LayoutTheme::ShouldHaveSpinButton(HTMLInputElement* input_element) const {
  return input_element->IsSteppable() &&
         input_element->type() != InputTypeNames::range;
}

void LayoutTheme::AdjustMenuListButtonStyle(ComputedStyle&, Element*) const {}

void LayoutTheme::AdjustSliderContainerStyle(ComputedStyle& style,
                                             Element* e) const {
  if (e && (e->ShadowPseudoId() == "-webkit-media-slider-container" ||
            e->ShadowPseudoId() == "-webkit-slider-container")) {
    if (style.Appearance() == kSliderVerticalPart) {
      style.SetTouchAction(TouchAction::kTouchActionPanX);
      style.SetAppearance(kNoControlPart);
    } else {
      style.SetTouchAction(TouchAction::kTouchActionPanY);
      style.SetAppearance(kNoControlPart);
    }
  }
}

void LayoutTheme::AdjustSliderThumbStyle(ComputedStyle& style) const {
  AdjustSliderThumbSize(style);
}

void LayoutTheme::AdjustSliderThumbSize(ComputedStyle&) const {}

void LayoutTheme::AdjustSearchFieldStyle(ComputedStyle&) const {}

void LayoutTheme::AdjustSearchFieldCancelButtonStyle(ComputedStyle&) const {}

void LayoutTheme::PlatformColorsDidChange() {
  Page::PlatformColorsChanged();
}

void LayoutTheme::SetCaretBlinkInterval(double interval) {
  caret_blink_interval_ = interval;
}

double LayoutTheme::CaretBlinkInterval() const {
  // Disable the blinking caret in layout test mode, as it introduces
  // a race condition for the pixel tests. http://b/1198440
  return LayoutTestSupport::IsRunningLayoutTest() ? 0 : caret_blink_interval_;
}

static FontDescription& GetCachedFontDescription(CSSValueID system_font_id) {
  DEFINE_STATIC_LOCAL(FontDescription, caption, ());
  DEFINE_STATIC_LOCAL(FontDescription, icon, ());
  DEFINE_STATIC_LOCAL(FontDescription, menu, ());
  DEFINE_STATIC_LOCAL(FontDescription, message_box, ());
  DEFINE_STATIC_LOCAL(FontDescription, small_caption, ());
  DEFINE_STATIC_LOCAL(FontDescription, status_bar, ());
  DEFINE_STATIC_LOCAL(FontDescription, webkit_mini_control, ());
  DEFINE_STATIC_LOCAL(FontDescription, webkit_small_control, ());
  DEFINE_STATIC_LOCAL(FontDescription, webkit_control, ());
  DEFINE_STATIC_LOCAL(FontDescription, default_description, ());
  switch (system_font_id) {
    case CSSValueCaption:
      return caption;
    case CSSValueIcon:
      return icon;
    case CSSValueMenu:
      return menu;
    case CSSValueMessageBox:
      return message_box;
    case CSSValueSmallCaption:
      return small_caption;
    case CSSValueStatusBar:
      return status_bar;
    case CSSValueWebkitMiniControl:
      return webkit_mini_control;
    case CSSValueWebkitSmallControl:
      return webkit_small_control;
    case CSSValueWebkitControl:
      return webkit_control;
    case CSSValueNone:
      return default_description;
    default:
      NOTREACHED();
      return default_description;
  }
}

void LayoutTheme::SystemFont(CSSValueID system_font_id,
                             FontDescription& font_description) {
  font_description = GetCachedFontDescription(system_font_id);
  if (font_description.IsAbsoluteSize())
    return;

  FontSelectionValue font_slope = NormalSlopeValue();
  FontSelectionValue font_weight = NormalWeightValue();
  float font_size = 0;
  AtomicString font_family;
  SystemFont(system_font_id, font_slope, font_weight, font_size, font_family);
  font_description.SetStyle(font_slope);
  font_description.SetWeight(font_weight);
  font_description.SetSpecifiedSize(font_size);
  font_description.SetIsAbsoluteSize(true);
  font_description.FirstFamily().SetFamily(font_family);
  font_description.SetGenericFamily(FontDescription::kNoFamily);
}

Color LayoutTheme::SystemColor(CSSValueID css_value_id) const {
  switch (css_value_id) {
    case CSSValueActiveborder:
      return 0xFFFFFFFF;
    case CSSValueActivecaption:
      return 0xFFCCCCCC;
    case CSSValueAppworkspace:
      return 0xFFFFFFFF;
    case CSSValueBackground:
      return 0xFF6363CE;
    case CSSValueButtonface:
      return 0xFFC0C0C0;
    case CSSValueButtonhighlight:
      return 0xFFDDDDDD;
    case CSSValueButtonshadow:
      return 0xFF888888;
    case CSSValueButtontext:
      return 0xFF000000;
    case CSSValueCaptiontext:
      return 0xFF000000;
    case CSSValueGraytext:
      return 0xFF808080;
    case CSSValueHighlight:
      return 0xFFB5D5FF;
    case CSSValueHighlighttext:
      return 0xFF000000;
    case CSSValueInactiveborder:
      return 0xFFFFFFFF;
    case CSSValueInactivecaption:
      return 0xFFFFFFFF;
    case CSSValueInactivecaptiontext:
      return 0xFF7F7F7F;
    case CSSValueInfobackground:
      return 0xFFFBFCC5;
    case CSSValueInfotext:
      return 0xFF000000;
    case CSSValueMenu:
      return 0xFFC0C0C0;
    case CSSValueMenutext:
      return 0xFF000000;
    case CSSValueScrollbar:
      return 0xFFFFFFFF;
    case CSSValueText:
      return 0xFF000000;
    case CSSValueThreeddarkshadow:
      return 0xFF666666;
    case CSSValueThreedface:
      return 0xFFC0C0C0;
    case CSSValueThreedhighlight:
      return 0xFFDDDDDD;
    case CSSValueThreedlightshadow:
      return 0xFFC0C0C0;
    case CSSValueThreedshadow:
      return 0xFF888888;
    case CSSValueWindow:
      return 0xFFFFFFFF;
    case CSSValueWindowframe:
      return 0xFFCCCCCC;
    case CSSValueWindowtext:
      return 0xFF000000;
    case CSSValueInternalActiveListBoxSelection:
      return ActiveListBoxSelectionBackgroundColor();
    case CSSValueInternalActiveListBoxSelectionText:
      return ActiveListBoxSelectionForegroundColor();
    case CSSValueInternalInactiveListBoxSelection:
      return InactiveListBoxSelectionBackgroundColor();
    case CSSValueInternalInactiveListBoxSelectionText:
      return InactiveListBoxSelectionForegroundColor();
    default:
      break;
  }
  NOTREACHED();
  return Color();
}

Color LayoutTheme::PlatformTextSearchHighlightColor(bool active_match) const {
  if (active_match)
    return Color(255, 150, 50);  // Orange.
  return Color(255, 255, 0);     // Yellow.
}

Color LayoutTheme::PlatformTextSearchColor(bool active_match) const {
  return Color::kBlack;
}

Color LayoutTheme::TapHighlightColor() {
  return GetTheme().PlatformTapHighlightColor();
}

void LayoutTheme::SetCustomFocusRingColor(const Color& c) {
  custom_focus_ring_color_ = c;
  has_custom_focus_ring_color_ = true;
}

Color LayoutTheme::FocusRingColor() const {
  return has_custom_focus_ring_color_ ? custom_focus_ring_color_
                                      : GetTheme().PlatformFocusRingColor();
}

String LayoutTheme::FileListNameForWidth(Locale& locale,
                                         const FileList* file_list,
                                         const Font& font,
                                         int width) const {
  if (width <= 0)
    return String();

  String string;
  if (file_list->IsEmpty()) {
    string =
        locale.QueryString(WebLocalizedString::kFileButtonNoFileSelectedLabel);
  } else if (file_list->length() == 1) {
    string = file_list->item(0)->name();
  } else {
    return StringTruncator::RightTruncate(
        locale.QueryString(WebLocalizedString::kMultipleFileUploadText,
                           locale.ConvertToLocalizedNumber(
                               String::Number(file_list->length()))),
        width, font);
  }

  return StringTruncator::CenterTruncate(string, width, font);
}

bool LayoutTheme::ShouldOpenPickerWithF4Key() const {
  return false;
}

bool LayoutTheme::SupportsCalendarPicker(const AtomicString& type) const {
  DCHECK(RuntimeEnabledFeatures::InputMultipleFieldsUIEnabled());
  return type == InputTypeNames::date || type == InputTypeNames::datetime ||
         type == InputTypeNames::datetime_local ||
         type == InputTypeNames::month || type == InputTypeNames::week;
}

bool LayoutTheme::ShouldUseFallbackTheme(const ComputedStyle&) const {
  return false;
}

void LayoutTheme::AdjustStyleUsingFallbackTheme(ComputedStyle& style) {
  ControlPart part = style.Appearance();
  switch (part) {
    case kCheckboxPart:
      return AdjustCheckboxStyleUsingFallbackTheme(style);
    case kRadioPart:
      return AdjustRadioStyleUsingFallbackTheme(style);
    default:
      break;
  }
}

// static
void LayoutTheme::SetSizeIfAuto(ComputedStyle& style, const IntSize& size) {
  if (style.Width().IsIntrinsicOrAuto())
    style.SetWidth(Length(size.Width(), kFixed));
  if (style.Height().IsIntrinsicOrAuto())
    style.SetHeight(Length(size.Height(), kFixed));
}

// static
void LayoutTheme::SetMinimumSizeIfAuto(ComputedStyle& style,
                                       const IntSize& size) {
  // We only want to set a minimum size if no explicit size is specified, to
  // avoid overriding author intentions.
  if (style.MinWidth().IsIntrinsicOrAuto() && style.Width().IsIntrinsicOrAuto())
    style.SetMinWidth(Length(size.Width(), kFixed));
  if (style.MinHeight().IsIntrinsicOrAuto() &&
      style.Height().IsIntrinsicOrAuto())
    style.SetMinHeight(Length(size.Height(), kFixed));
}

void LayoutTheme::AdjustCheckboxStyleUsingFallbackTheme(
    ComputedStyle& style) const {
  // If the width and height are both specified, then we have nothing to do.
  if (!style.Width().IsIntrinsicOrAuto() && !style.Height().IsAuto())
    return;

  IntSize size = Platform::Current()->FallbackThemeEngine()->GetSize(
      WebFallbackThemeEngine::kPartCheckbox);
  float zoom_level = style.EffectiveZoom();
  size.SetWidth(size.Width() * zoom_level);
  size.SetHeight(size.Height() * zoom_level);
  SetMinimumSizeIfAuto(style, size);
  SetSizeIfAuto(style, size);

  // padding - not honored by WinIE, needs to be removed.
  style.ResetPadding();

  // border - honored by WinIE, but looks terrible (just paints in the control
  // box and turns off the Windows XP theme)
  // for now, we will not honor it.
  style.ResetBorder();
}

void LayoutTheme::AdjustRadioStyleUsingFallbackTheme(
    ComputedStyle& style) const {
  // If the width and height are both specified, then we have nothing to do.
  if (!style.Width().IsIntrinsicOrAuto() && !style.Height().IsAuto())
    return;

  IntSize size = Platform::Current()->FallbackThemeEngine()->GetSize(
      WebFallbackThemeEngine::kPartRadio);
  float zoom_level = style.EffectiveZoom();
  size.SetWidth(size.Width() * zoom_level);
  size.SetHeight(size.Height() * zoom_level);
  SetMinimumSizeIfAuto(style, size);
  SetSizeIfAuto(style, size);

  // padding - not honored by WinIE, needs to be removed.
  style.ResetPadding();

  // border - honored by WinIE, but looks terrible (just paints in the control
  // box and turns off the Windows XP theme)
  // for now, we will not honor it.
  style.ResetBorder();
}

}  // namespace blink
