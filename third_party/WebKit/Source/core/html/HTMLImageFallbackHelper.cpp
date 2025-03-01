// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/html/HTMLImageFallbackHelper.h"

#include "core/HTMLNames.h"
#include "core/InputTypeNames.h"
#include "core/dom/ElementRareData.h"
#include "core/dom/ShadowRoot.h"
#include "core/dom/Text.h"
#include "core/html/HTMLElement.h"
#include "core/html/HTMLImageElement.h"
#include "core/html/HTMLImageLoader.h"
#include "core/html/HTMLInputElement.h"
#include "core/html/HTMLSpanElement.h"
#include "core/html/HTMLStyleElement.h"
#include "platform/wtf/text/StringBuilder.h"

namespace blink {

using namespace HTMLNames;

static bool ImageSmallerThanAltImage(int pixels_for_alt_image,
                                     const Length width,
                                     const Length height) {
  // We don't have a layout tree so can't compute the size of an image
  // relative dimensions - so we just assume we should display the alt image.
  if (!width.IsFixed() && !height.IsFixed())
    return false;
  if (height.IsFixed() && height.Value() < pixels_for_alt_image)
    return true;
  return width.IsFixed() && width.Value() < pixels_for_alt_image;
}

void HTMLImageFallbackHelper::CreateAltTextShadowTree(Element& element) {
  ShadowRoot& root = element.EnsureUserAgentShadowRoot();

  HTMLSpanElement* container = HTMLSpanElement::Create(element.GetDocument());
  root.AppendChild(container);
  container->setAttribute(idAttr, AtomicString("alttext-container"));

  HTMLImageElement* broken_image =
      HTMLImageElement::Create(element.GetDocument());
  container->AppendChild(broken_image);
  broken_image->SetIsFallbackImage();
  broken_image->setAttribute(idAttr, AtomicString("alttext-image"));
  broken_image->setAttribute(widthAttr, AtomicString("16"));
  broken_image->setAttribute(heightAttr, AtomicString("16"));
  broken_image->setAttribute(alignAttr, AtomicString("left"));
  broken_image->SetInlineStyleProperty(CSSPropertyMargin, 0,
                                       CSSPrimitiveValue::UnitType::kPixels);

  HTMLSpanElement* alt_text = HTMLSpanElement::Create(element.GetDocument());
  container->AppendChild(alt_text);
  alt_text->setAttribute(idAttr, AtomicString("alttext"));

  Text* text =
      Text::Create(element.GetDocument(), ToHTMLElement(element).AltText());
  alt_text->AppendChild(text);
}

RefPtr<ComputedStyle> HTMLImageFallbackHelper::CustomStyleForAltText(
    Element& element,
    RefPtr<ComputedStyle> new_style) {
  // If we have an author shadow root or have not created the UA shadow root
  // yet, bail early. We can't use ensureUserAgentShadowRoot() here because that
  // would alter the DOM tree during style recalc.
  if (element.AuthorShadowRoot() || !element.UserAgentShadowRoot())
    return new_style;

  Element* place_holder =
      element.UserAgentShadowRoot()->getElementById("alttext-container");
  Element* broken_image =
      element.UserAgentShadowRoot()->getElementById("alttext-image");
  // Input elements have a UA shadow root of their own. We may not have replaced
  // it with fallback content yet.
  if (!place_holder || !broken_image)
    return new_style;

  if (element.GetDocument().InQuirksMode()) {
    // Mimic the behaviour of the image host by setting symmetric dimensions if
    // only one dimension is specified.
    if (new_style->Width().IsSpecifiedOrIntrinsic() &&
        new_style->Height().IsAuto())
      new_style->SetHeight(new_style->Width());
    else if (new_style->Height().IsSpecifiedOrIntrinsic() &&
             new_style->Width().IsAuto())
      new_style->SetWidth(new_style->Height());
    if (new_style->Width().IsSpecifiedOrIntrinsic() &&
        new_style->Height().IsSpecifiedOrIntrinsic()) {
      place_holder->SetInlineStyleProperty(CSSPropertyVerticalAlign,
                                           CSSValueBaseline);
    }
  }

  bool image_has_intrinsic_dimensions =
      new_style->Width().IsSpecifiedOrIntrinsic() &&
      new_style->Height().IsSpecifiedOrIntrinsic();
  bool image_has_no_alt_attribute = ToHTMLElement(element).AltText().IsNull();
  bool treat_as_replaced =
      image_has_intrinsic_dimensions &&
      (element.GetDocument().InQuirksMode() || image_has_no_alt_attribute);
  if (treat_as_replaced) {
    // https://html.spec.whatwg.org/multipage/rendering.html#images-3:
    // "If the element does not represent an image, but the element already has
    // intrinsic dimensions (e.g. from the dimension attributes or CSS rules),
    // and either: the user agent has reason to believe that the image will
    // become available and be rendered in due course, or the element has no alt
    // attribute, or the Document is in quirks mode The user agent is expected
    // to treat the element as a replaced element whose content is the text that
    // the element represents, if any."
    place_holder->SetInlineStyleProperty(CSSPropertyOverflow, CSSValueHidden);
    place_holder->SetInlineStyleProperty(CSSPropertyDisplay,
                                         CSSValueInlineBlock);
    CSSPrimitiveValue::UnitType unit =
        new_style->Height().IsPercent()
            ? CSSPrimitiveValue::UnitType::kPercentage
            : CSSPrimitiveValue::UnitType::kPixels;
    place_holder->SetInlineStyleProperty(CSSPropertyHeight,
                                         new_style->Height().Value(), unit);
    place_holder->SetInlineStyleProperty(CSSPropertyWidth,
                                         new_style->Width().Value(), unit);

    // 16px for the image and 2px for its top/left border/padding offset.
    int pixels_for_alt_image = 18;
    if (ImageSmallerThanAltImage(pixels_for_alt_image, new_style->Width(),
                                 new_style->Height())) {
      broken_image->SetInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
    } else {
      place_holder->SetInlineStyleProperty(
          CSSPropertyBorderWidth, 1, CSSPrimitiveValue::UnitType::kPixels);
      place_holder->SetInlineStyleProperty(CSSPropertyBorderStyle,
                                           CSSValueSolid);
      place_holder->SetInlineStyleProperty(CSSPropertyBorderColor,
                                           CSSValueSilver);
      place_holder->SetInlineStyleProperty(
          CSSPropertyPadding, 1, CSSPrimitiveValue::UnitType::kPixels);
      place_holder->SetInlineStyleProperty(CSSPropertyBoxSizing,
                                           CSSValueBorderBox);
      broken_image->SetInlineStyleProperty(CSSPropertyDisplay, CSSValueInline);
      // Make sure the broken image icon appears on the appropriate side of the
      // image for the element's writing direction.
      broken_image->SetInlineStyleProperty(
          CSSPropertyFloat,
          AtomicString(new_style->Direction() == TextDirection::kLtr
                           ? "left"
                           : "right"));
    }
  } else {
    // "If the element is an img element that represents nothing and the user
    // agent does not expect this to change the user agent is expected to treat
    // the element as an empty inline element."
    //  - We achieve this by hiding the broken image so that the span is empty.
    // "If the element is an img element that represents some text and the user
    // agent does not expect this to change the user agent is expected to treat
    // the element as a non-replaced phrasing element whose content is the text,
    // optionally with an icon indicating that an image is missing, so that the
    // user can request the image be displayed or investigate why it is not
    // rendering."
    //  - We opt not to display an icon, like Firefox.
    if (new_style->Display() == EDisplay::kInline) {
      new_style->SetWidth(Length());
      new_style->SetHeight(Length());
    }
    broken_image->SetInlineStyleProperty(CSSPropertyDisplay, CSSValueNone);
  }

  return new_style;
}

}  // namespace blink
