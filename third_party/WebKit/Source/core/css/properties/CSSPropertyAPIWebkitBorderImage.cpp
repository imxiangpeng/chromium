// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIWebkitBorderImage.h"

#include "core/css/properties/CSSPropertyBorderImageUtils.h"

namespace blink {

const CSSValue* CSSPropertyAPIWebkitBorderImage::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext& context,
    const CSSParserLocalContext&) {
  return CSSPropertyBorderImageUtils::ConsumeWebkitBorderImage(range, context);
}

}  // namespace blink
