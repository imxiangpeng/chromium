// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/paint/TextPainterBase.h"

#include "core/dom/Document.h"
#include "core/paint/AppliedDecorationPainter.h"
#include "core/paint/BoxPainterBase.h"
#include "core/paint/PaintInfo.h"
#include "core/style/ComputedStyle.h"
#include "core/style/ShadowList.h"
#include "platform/fonts/Font.h"
#include "platform/graphics/GraphicsContext.h"
#include "platform/graphics/GraphicsContextStateSaver.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/text/CharacterNames.h"

namespace blink {

TextPainterBase::TextPainterBase(GraphicsContext& context,
                                 const Font& font,
                                 const LayoutPoint& text_origin,
                                 const LayoutRect& text_bounds,
                                 bool horizontal)
    : graphics_context_(context),
      font_(font),
      text_origin_(text_origin),
      text_bounds_(text_bounds),
      horizontal_(horizontal),
      emphasis_mark_offset_(0),
      ellipsis_offset_(0) {}

TextPainterBase::~TextPainterBase() {}

void TextPainterBase::SetEmphasisMark(const AtomicString& emphasis_mark,
                                      TextEmphasisPosition position) {
  emphasis_mark_ = emphasis_mark;
  const SimpleFontData* font_data = font_.PrimaryFont();
  DCHECK(font_data);

  if (!font_data || emphasis_mark.IsNull()) {
    emphasis_mark_offset_ = 0;
  } else if (position == TextEmphasisPosition::kOver) {
    emphasis_mark_offset_ = -font_data->GetFontMetrics().Ascent() -
                            font_.EmphasisMarkDescent(emphasis_mark);
  } else {
    DCHECK(position == TextEmphasisPosition::kUnder);
    emphasis_mark_offset_ = font_data->GetFontMetrics().Descent() +
                            font_.EmphasisMarkAscent(emphasis_mark);
  }
}

// static
void TextPainterBase::UpdateGraphicsContext(
    GraphicsContext& context,
    const Style& text_style,
    bool horizontal,
    GraphicsContextStateSaver& state_saver) {
  TextDrawingModeFlags mode = context.TextDrawingMode();
  if (text_style.stroke_width > 0) {
    TextDrawingModeFlags new_mode = mode | kTextModeStroke;
    if (mode != new_mode) {
      if (!state_saver.Saved())
        state_saver.Save();
      context.SetTextDrawingMode(new_mode);
      mode = new_mode;
    }
  }

  if (mode & kTextModeFill && text_style.fill_color != context.FillColor())
    context.SetFillColor(text_style.fill_color);

  if (mode & kTextModeStroke) {
    if (text_style.stroke_color != context.StrokeColor())
      context.SetStrokeColor(text_style.stroke_color);
    if (text_style.stroke_width != context.StrokeThickness())
      context.SetStrokeThickness(text_style.stroke_width);
  }

  if (text_style.shadow) {
    if (!state_saver.Saved())
      state_saver.Save();
    context.SetDrawLooper(text_style.shadow->CreateDrawLooper(
        DrawLooperBuilder::kShadowIgnoresAlpha, text_style.current_color,
        horizontal));
  }
}

Color TextPainterBase::TextColorForWhiteBackground(Color text_color) {
  int distance_from_white = DifferenceSquared(text_color, Color::kWhite);
  // semi-arbitrarily chose 65025 (255^2) value here after a few tests;
  return distance_from_white > 65025 ? text_color : text_color.Dark();
}

// static
TextPainterBase::Style TextPainterBase::TextPaintingStyle(
    const Document& document,
    const ComputedStyle& style,
    const PaintInfo& paint_info) {
  TextPainterBase::Style text_style;
  bool is_printing = paint_info.IsPrinting();

  if (paint_info.phase == kPaintPhaseTextClip) {
    // When we use the text as a clip, we only care about the alpha, thus we
    // make all the colors black.
    text_style.current_color = Color::kBlack;
    text_style.fill_color = Color::kBlack;
    text_style.stroke_color = Color::kBlack;
    text_style.emphasis_mark_color = Color::kBlack;
    text_style.stroke_width = style.TextStrokeWidth();
    text_style.shadow = 0;
  } else {
    text_style.current_color = style.VisitedDependentColor(CSSPropertyColor);
    text_style.fill_color =
        style.VisitedDependentColor(CSSPropertyWebkitTextFillColor);
    text_style.stroke_color =
        style.VisitedDependentColor(CSSPropertyWebkitTextStrokeColor);
    text_style.emphasis_mark_color =
        style.VisitedDependentColor(CSSPropertyWebkitTextEmphasisColor);
    text_style.stroke_width = style.TextStrokeWidth();
    text_style.shadow = style.TextShadow();

    // Adjust text color when printing with a white background.
    DCHECK(document.Printing() == is_printing ||
           RuntimeEnabledFeatures::PrintBrowserEnabled());
    bool force_background_to_white =
        BoxPainterBase::ShouldForceWhiteBackgroundForPrintEconomy(document,
                                                                  style);
    if (force_background_to_white) {
      text_style.fill_color =
          TextColorForWhiteBackground(text_style.fill_color);
      text_style.stroke_color =
          TextColorForWhiteBackground(text_style.stroke_color);
      text_style.emphasis_mark_color =
          TextColorForWhiteBackground(text_style.emphasis_mark_color);
    }

    // Text shadows are disabled when printing. http://crbug.com/258321
    if (is_printing)
      text_style.shadow = 0;
  }

  return text_style;
}

void TextPainterBase::DecorationsStripeIntercepts(
    float upper,
    float stripe_width,
    float dilation,
    const Vector<Font::TextIntercept>& text_intercepts) {
  for (auto intercept : text_intercepts) {
    FloatPoint clip_origin(text_origin_);
    FloatRect clip_rect(
        clip_origin + FloatPoint(intercept.begin_, upper),
        FloatSize(intercept.end_ - intercept.begin_, stripe_width));
    clip_rect.InflateX(dilation);
    // We need to ensure the clip rectangle is covering the full underline
    // extent. For horizontal drawing, using enclosingIntRect would be
    // sufficient, since we can clamp to full device pixels that way. However,
    // for vertical drawing, we have a transformation applied, which breaks the
    // integers-equal-device pixels assumption, so vertically inflating by 1
    // pixel makes sure we're always covering. This should only be done on the
    // clipping rectangle, not when computing the glyph intersects.
    clip_rect.InflateY(1.0);
    graphics_context_.ClipOut(clip_rect);
  }
}

void TextPainterBase::PaintDecorationsOnlyLineThrough(
    const DecorationInfo& decoration_info,
    const PaintInfo& paint_info,
    const Vector<AppliedTextDecoration>& decorations) {
  GraphicsContext& context = paint_info.context;
  GraphicsContextStateSaver state_saver(context);
  context.SetStrokeThickness(decoration_info.thickness);
  for (const AppliedTextDecoration& decoration : decorations) {
    TextDecoration lines = decoration.Lines();
    if (EnumHasFlags(lines, TextDecoration::kLineThrough)) {
      const float line_through_offset = 2 * decoration_info.baseline / 3;
      AppliedDecorationPainter decoration_painter(
          context, decoration_info, line_through_offset, decoration,
          decoration_info.double_offset, 0);
      // No skip: ink for line-through,
      // compare https://github.com/w3c/csswg-drafts/issues/711
      decoration_painter.Paint();
    }
  }
}

namespace {

static ResolvedUnderlinePosition ResolveUnderlinePosition(
    const ComputedStyle& style,
    FontBaseline baseline_type) {
  // |auto| should resolve to |under| to avoid drawing through glyphs in
  // scripts where it would not be appropriate (e.g., ideographs.)
  // However, this has performance implications. For now, we only work with
  // vertical text.
  switch (baseline_type) {
    case kAlphabeticBaseline:
      switch (style.GetTextUnderlinePosition()) {
        case TextUnderlinePosition::kAuto:
          return ResolvedUnderlinePosition::kRoman;
        case TextUnderlinePosition::kUnder:
          return ResolvedUnderlinePosition::kUnder;
      }
      break;
    case kIdeographicBaseline:
      // Compute language-appropriate default underline position.
      // https://drafts.csswg.org/css-text-decor-3/#default-stylesheet
      UScriptCode script = style.GetFontDescription().GetScript();
      if (script == USCRIPT_KATAKANA_OR_HIRAGANA || script == USCRIPT_HANGUL)
        return ResolvedUnderlinePosition::kOver;
      return ResolvedUnderlinePosition::kUnder;
  }
  NOTREACHED();
  return ResolvedUnderlinePosition::kRoman;
}

static bool ShouldSetDecorationAntialias(const ComputedStyle& style) {
  for (const auto& decoration : style.AppliedTextDecorations()) {
    ETextDecorationStyle decoration_style = decoration.Style();
    if (decoration_style == ETextDecorationStyle::kDotted ||
        decoration_style == ETextDecorationStyle::kDashed)
      return true;
  }
  return false;
}

float ComputeDecorationThickness(const ComputedStyle* style,
                                 const SimpleFontData* font_data) {
  // Set the thick of the line to be 10% (or something else ?)of the computed
  // font size and not less than 1px.  Using computedFontSize should take care
  // of zoom as well.

  // Update Underline thickness, in case we have Faulty Font Metrics calculating
  // underline thickness by old method.
  float text_decoration_thickness = 0.0;
  int font_height_int = 0;
  if (font_data) {
    text_decoration_thickness =
        font_data->GetFontMetrics().UnderlineThickness();
    font_height_int = font_data->GetFontMetrics().Height();
  }
  if ((text_decoration_thickness == 0.f) ||
      (text_decoration_thickness >= (font_height_int >> 1))) {
    text_decoration_thickness = std::max(1.f, style->ComputedFontSize() / 10.f);
  }
  return text_decoration_thickness;
}

}  // anonymous namespace

void TextPainterBase::ComputeDecorationInfo(
    DecorationInfo& decoration_info,
    const LayoutPoint& box_origin,
    LayoutPoint local_origin,
    LayoutUnit width,
    FontBaseline baseline_type,
    const ComputedStyle& style,
    const ComputedStyle* decorating_box_style) {
  decoration_info.width = width;
  decoration_info.local_origin = FloatPoint(local_origin);
  decoration_info.antialias = ShouldSetDecorationAntialias(style);
  decoration_info.style = &style;
  decoration_info.baseline_type = baseline_type;
  decoration_info.underline_position = ResolveUnderlinePosition(
      *decoration_info.style, decoration_info.baseline_type);

  decoration_info.font_data = decoration_info.style->GetFont().PrimaryFont();
  DCHECK(decoration_info.font_data);
  decoration_info.baseline =
      decoration_info.font_data
          ? decoration_info.font_data->GetFontMetrics().FloatAscent()
          : 0;

  if (decoration_info.underline_position == ResolvedUnderlinePosition::kRoman) {
    decoration_info.thickness = ComputeDecorationThickness(
        decoration_info.style, decoration_info.font_data);
  } else {
    // Compute decorating box. Position and thickness are computed from the
    // decorating box.
    // Only for non-Roman for now for the performance implications.
    // https:// drafts.csswg.org/css-text-decor-3/#decorating-box
    if (decorating_box_style) {
      decoration_info.thickness = ComputeDecorationThickness(
          decorating_box_style, decorating_box_style->GetFont().PrimaryFont());
    } else {
      decoration_info.thickness = ComputeDecorationThickness(
          decoration_info.style, decoration_info.font_data);
    }
  }

  // Offset between lines - always non-zero, so lines never cross each other.
  decoration_info.double_offset = decoration_info.thickness + 1.f;
}

}  // namespace blink
