// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/css/properties/CSSPropertyAPIMaskSourceType.h"

#include "core/css/properties/CSSPropertyBackgroundUtils.h"

namespace blink {

const CSSValue* CSSPropertyAPIMaskSourceType::parseSingleValue(
    CSSParserTokenRange& range,
    const CSSParserContext&,
    const CSSParserLocalContext&) {
  return CSSPropertyParserHelpers::ConsumeCommaSeparatedList(
      CSSPropertyBackgroundUtils::ConsumeMaskSourceType, range);
}

}  // namespace blink
