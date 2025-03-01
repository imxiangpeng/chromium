// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIFontStyle.h"

#include "core/css/properties/CSSPropertyFontUtils.h"

namespace blink {

const CSSValue* CSSPropertyAPIFontStyle::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext&,
    const CSSParserLocalContext&) {
  return CSSPropertyFontUtils::ConsumeFontStyle(range);
}

}  // namespace blink
