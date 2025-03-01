// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIStrokeOrLength.h"

#include "core/css/parser/CSSPropertyParserHelpers.h"

namespace blink {
class CSSParserLocalContext;

const CSSValue* CSSPropertyAPIStrokeOrLength::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext&,
    const CSSParserLocalContext&) {
  return CSSPropertyParserHelpers::ConsumeLengthOrPercent(
      range, kSVGAttributeMode, kValueRangeAll,
      CSSPropertyParserHelpers::UnitlessQuirk::kForbid);
}

}  // namespace blink
