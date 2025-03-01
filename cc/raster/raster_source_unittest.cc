// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/raster/raster_source.h"

#include <stddef.h>

#include <memory>

#include "cc/raster/playback_image_provider.h"
#include "cc/test/fake_recording_source.h"
#include "cc/test/skia_common.h"
#include "cc/tiles/software_image_decode_cache.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkPixelRef.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "third_party/skia/include/core/SkShader.h"
#include "ui/gfx/geometry/axis_transform2d.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size_conversions.h"

namespace cc {
namespace {

gfx::ColorSpace ColorSpaceForTesting() {
  return gfx::ColorSpace();
}

TEST(RasterSourceTest, AnalyzeIsSolidUnscaled) {
  gfx::Size layer_bounds(400, 400);

  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);

  PaintFlags solid_flags;
  SkColor solid_color = SkColorSetARGB(255, 12, 23, 34);
  solid_flags.setColor(solid_color);

  SkColor non_solid_color = SkColorSetARGB(128, 45, 56, 67);
  SkColor color = SK_ColorTRANSPARENT;
  PaintFlags non_solid_flags;
  bool is_solid_color = false;
  non_solid_flags.setColor(non_solid_color);

  recording_source->add_draw_rect_with_flags(gfx::Rect(layer_bounds),
                                             solid_flags);
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();

  // Ensure everything is solid.
  for (int y = 0; y <= 300; y += 100) {
    for (int x = 0; x <= 300; x += 100) {
      gfx::Rect rect(x, y, 100, 100);
      is_solid_color = raster->PerformSolidColorAnalysis(rect, &color);
      EXPECT_TRUE(is_solid_color) << rect.ToString();
      EXPECT_EQ(solid_color, color) << rect.ToString();
    }
  }

  // Add one non-solid pixel and recreate the raster source.
  recording_source->add_draw_rect_with_flags(gfx::Rect(50, 50, 1, 1),
                                             non_solid_flags);
  recording_source->Rerecord();
  raster = recording_source->CreateRasterSource();

  color = SK_ColorTRANSPARENT;
  is_solid_color =
      raster->PerformSolidColorAnalysis(gfx::Rect(0, 0, 100, 100), &color);
  EXPECT_FALSE(is_solid_color);

  color = SK_ColorTRANSPARENT;
  is_solid_color =
      raster->PerformSolidColorAnalysis(gfx::Rect(100, 0, 100, 100), &color);
  EXPECT_TRUE(is_solid_color);
  EXPECT_EQ(solid_color, color);

  // Boundaries should be clipped.
  color = SK_ColorTRANSPARENT;
  is_solid_color =
      raster->PerformSolidColorAnalysis(gfx::Rect(350, 0, 100, 100), &color);
  EXPECT_TRUE(is_solid_color);
  EXPECT_EQ(solid_color, color);

  color = SK_ColorTRANSPARENT;
  is_solid_color =
      raster->PerformSolidColorAnalysis(gfx::Rect(0, 350, 100, 100), &color);
  EXPECT_TRUE(is_solid_color);
  EXPECT_EQ(solid_color, color);

  color = SK_ColorTRANSPARENT;
  is_solid_color =
      raster->PerformSolidColorAnalysis(gfx::Rect(350, 350, 100, 100), &color);
  EXPECT_TRUE(is_solid_color);
  EXPECT_EQ(solid_color, color);
}

TEST(RasterSourceTest, PixelRefIteratorDiscardableRefsOneTile) {
  gfx::Size layer_bounds(512, 512);

  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);

  sk_sp<SkImage> discardable_image[2][2];
  discardable_image[0][0] = CreateDiscardableImage(gfx::Size(32, 32));
  discardable_image[0][1] = CreateDiscardableImage(gfx::Size(32, 32));
  discardable_image[1][1] = CreateDiscardableImage(gfx::Size(32, 32));

  // Discardable pixel refs are found in the following cells:
  // |---|---|
  // | x | x |
  // |---|---|
  // |   | x |
  // |---|---|
  recording_source->add_draw_image(discardable_image[0][0], gfx::Point(0, 0));
  recording_source->add_draw_image(discardable_image[0][1], gfx::Point(260, 0));
  recording_source->add_draw_image(discardable_image[1][1],
                                   gfx::Point(260, 260));
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();

  // Tile sized iterators. These should find only one pixel ref.
  {
    gfx::ColorSpace target_color_space = gfx::ColorSpace::CreateSRGB();
    std::vector<DrawImage> images;
    raster->GetDiscardableImagesInRect(gfx::Rect(0, 0, 256, 256), 1.f,
                                       target_color_space, &images);
    EXPECT_EQ(1u, images.size());
    EXPECT_EQ(discardable_image[0][0], images[0].image());
    EXPECT_EQ(target_color_space, images[0].target_color_space());
  }
  // Shifted tile sized iterators. These should find only one pixel ref.
  {
    gfx::ColorSpace target_color_space = gfx::ColorSpace::CreateXYZD50();
    std::vector<DrawImage> images;
    raster->GetDiscardableImagesInRect(gfx::Rect(260, 260, 256, 256), 1.f,
                                       target_color_space, &images);
    EXPECT_EQ(1u, images.size());
    EXPECT_EQ(discardable_image[1][1], images[0].image());
    EXPECT_EQ(target_color_space, images[0].target_color_space());
  }
  // Ensure there's no discardable pixel refs in the empty cell
  {
    gfx::ColorSpace target_color_space = gfx::ColorSpace::CreateSRGB();
    std::vector<DrawImage> images;
    raster->GetDiscardableImagesInRect(gfx::Rect(0, 256, 256, 256), 1.f,
                                       target_color_space, &images);
    EXPECT_EQ(0u, images.size());
  }
  // Layer sized iterators. These should find three pixel ref.
  {
    gfx::ColorSpace target_color_space;
    std::vector<DrawImage> images;
    raster->GetDiscardableImagesInRect(gfx::Rect(0, 0, 512, 512), 1.f,
                                       target_color_space, &images);
    EXPECT_EQ(3u, images.size());
    EXPECT_EQ(discardable_image[0][0], images[0].image());
    EXPECT_EQ(discardable_image[0][1], images[1].image());
    EXPECT_EQ(discardable_image[1][1], images[2].image());
    EXPECT_EQ(target_color_space, images[0].target_color_space());
    EXPECT_EQ(target_color_space, images[1].target_color_space());
    EXPECT_EQ(target_color_space, images[2].target_color_space());
  }
}

TEST(RasterSourceTest, RasterFullContents) {
  gfx::Size layer_bounds(3, 5);
  float contents_scale = 1.5f;
  float raster_divisions = 2.f;

  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);
  recording_source->SetBackgroundColor(SK_ColorBLACK);
  recording_source->SetClearCanvasWithDebugColor(false);

  // Because the caller sets content opaque, it also promises that it
  // has at least filled in layer_bounds opaquely.
  PaintFlags white_flags;
  white_flags.setColor(SK_ColorWHITE);
  recording_source->add_draw_rect_with_flags(gfx::Rect(layer_bounds),
                                             white_flags);
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();

  gfx::Size content_bounds(
      gfx::ScaleToCeiledSize(layer_bounds, contents_scale));

  // Simulate drawing into different tiles at different offsets.
  int step_x = std::ceil(content_bounds.width() / raster_divisions);
  int step_y = std::ceil(content_bounds.height() / raster_divisions);
  for (int offset_x = 0; offset_x < content_bounds.width();
       offset_x += step_x) {
    for (int offset_y = 0; offset_y < content_bounds.height();
         offset_y += step_y) {
      gfx::Rect content_rect(offset_x, offset_y, step_x, step_y);
      content_rect.Intersect(gfx::Rect(content_bounds));

      // Simulate a canvas rect larger than the content rect.  Every pixel
      // up to one pixel outside the content rect is guaranteed to be opaque.
      // Outside of that is undefined.
      gfx::Rect canvas_rect(content_rect);
      canvas_rect.Inset(0, 0, -1, -1);

      SkBitmap bitmap;
      bitmap.allocN32Pixels(canvas_rect.width(), canvas_rect.height());
      SkCanvas canvas(bitmap);
      canvas.clear(SK_ColorTRANSPARENT);

      raster->PlaybackToCanvas(
          &canvas, ColorSpaceForTesting(), canvas_rect, canvas_rect,
          gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()),
          RasterSource::PlaybackSettings());

      SkColor* pixels = reinterpret_cast<SkColor*>(bitmap.getPixels());
      int num_pixels = bitmap.width() * bitmap.height();
      bool all_white = true;
      for (int i = 0; i < num_pixels; ++i) {
        EXPECT_EQ(SkColorGetA(pixels[i]), 255u);
        all_white &= (SkColorGetR(pixels[i]) == 255);
        all_white &= (SkColorGetG(pixels[i]) == 255);
        all_white &= (SkColorGetB(pixels[i]) == 255);
      }

      // If the canvas doesn't extend past the edge of the content,
      // it should be entirely white. Otherwise, the edge of the content
      // will be non-white.
      EXPECT_EQ(all_white, gfx::Rect(content_bounds).Contains(canvas_rect));
    }
  }
}

TEST(RasterSourceTest, RasterPartialContents) {
  gfx::Size layer_bounds(3, 5);
  float contents_scale = 1.5f;

  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);
  recording_source->SetBackgroundColor(SK_ColorGREEN);
  recording_source->SetClearCanvasWithDebugColor(false);

  // First record everything as white.
  PaintFlags white_flags;
  white_flags.setColor(SK_ColorWHITE);
  recording_source->add_draw_rect_with_flags(gfx::Rect(layer_bounds),
                                             white_flags);
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();

  gfx::Size content_bounds(
      gfx::ScaleToCeiledSize(layer_bounds, contents_scale));

  SkBitmap bitmap;
  bitmap.allocN32Pixels(content_bounds.width(), content_bounds.height());
  SkCanvas canvas(bitmap);
  canvas.clear(SK_ColorTRANSPARENT);

  // Playback the full rect which should make everything white.
  gfx::Rect raster_full_rect(content_bounds);
  gfx::Rect playback_rect(content_bounds);
  raster->PlaybackToCanvas(
      &canvas, ColorSpaceForTesting(), raster_full_rect, playback_rect,
      gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()),
      RasterSource::PlaybackSettings());

  {
    SkColor* pixels = reinterpret_cast<SkColor*>(bitmap.getPixels());
    for (int i = 0; i < bitmap.width(); ++i) {
      for (int j = 0; j < bitmap.height(); ++j) {
        SCOPED_TRACE(i);
        SCOPED_TRACE(j);
        EXPECT_EQ(255u, SkColorGetA(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(255u, SkColorGetR(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(255u, SkColorGetG(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(255u, SkColorGetB(pixels[i + j * bitmap.width()]));
      }
    }
  }

  // Re-record everything as black.
  PaintFlags black_flags;
  black_flags.setColor(SK_ColorBLACK);
  recording_source->add_draw_rect_with_flags(gfx::Rect(layer_bounds),
                                             black_flags);
  recording_source->Rerecord();

  // Make a new RasterSource from the new recording.
  raster = recording_source->CreateRasterSource();

  // We're going to playback from "everything is black" into a smaller area,
  // that touches the edge pixels of the recording.
  playback_rect.Inset(1, 2, 0, 1);
  raster->PlaybackToCanvas(
      &canvas, ColorSpaceForTesting(), raster_full_rect, playback_rect,
      gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()),
      RasterSource::PlaybackSettings());

  SkColor* pixels = reinterpret_cast<SkColor*>(bitmap.getPixels());
  int num_black = 0;
  int num_white = 0;
  for (int i = 0; i < bitmap.width(); ++i) {
    for (int j = 0; j < bitmap.height(); ++j) {
      SCOPED_TRACE(j);
      SCOPED_TRACE(i);
      bool expect_black = playback_rect.Contains(i, j);
      if (expect_black) {
        EXPECT_EQ(255u, SkColorGetA(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(0u, SkColorGetR(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(0u, SkColorGetG(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(0u, SkColorGetB(pixels[i + j * bitmap.width()]));
        ++num_black;
      } else {
        EXPECT_EQ(255u, SkColorGetA(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(255u, SkColorGetR(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(255u, SkColorGetG(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(255u, SkColorGetB(pixels[i + j * bitmap.width()]));
        ++num_white;
      }
    }
  }
  EXPECT_GT(num_black, 0);
  EXPECT_GT(num_white, 0);
}

TEST(RasterSourceTest, RasterPartialClear) {
  gfx::Size layer_bounds(3, 5);
  gfx::Size partial_bounds(2, 4);
  float contents_scale = 1.5f;

  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);
  recording_source->SetBackgroundColor(SK_ColorGREEN);
  recording_source->SetRequiresClear(true);
  recording_source->SetClearCanvasWithDebugColor(false);

  // First record everything as white.
  const unsigned alpha_dark = 10u;
  PaintFlags white_flags;
  white_flags.setColor(SK_ColorWHITE);
  white_flags.setAlpha(alpha_dark);
  recording_source->add_draw_rect_with_flags(gfx::Rect(layer_bounds),
                                             white_flags);
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();

  gfx::Size content_bounds(
      gfx::ScaleToCeiledSize(layer_bounds, contents_scale));

  SkBitmap bitmap;
  bitmap.allocN32Pixels(content_bounds.width(), content_bounds.height());
  SkCanvas canvas(bitmap);
  canvas.clear(SK_ColorTRANSPARENT);

  // Playback the full rect which should make everything light gray (alpha=10).
  gfx::Rect raster_full_rect(content_bounds);
  gfx::Rect playback_rect(content_bounds);
  raster->PlaybackToCanvas(
      &canvas, ColorSpaceForTesting(), raster_full_rect, playback_rect,
      gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()),
      RasterSource::PlaybackSettings());

  {
    SkColor* pixels = reinterpret_cast<SkColor*>(bitmap.getPixels());
    for (int i = 0; i < bitmap.width(); ++i) {
      for (int j = 0; j < bitmap.height(); ++j) {
        SCOPED_TRACE(i);
        SCOPED_TRACE(j);
        EXPECT_EQ(alpha_dark, SkColorGetA(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(alpha_dark, SkColorGetR(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(alpha_dark, SkColorGetG(pixels[i + j * bitmap.width()]));
        EXPECT_EQ(alpha_dark, SkColorGetB(pixels[i + j * bitmap.width()]));
      }
    }
  }

  std::unique_ptr<FakeRecordingSource> recording_source_light =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);
  recording_source_light->SetBackgroundColor(SK_ColorGREEN);
  recording_source_light->SetRequiresClear(true);
  recording_source_light->SetClearCanvasWithDebugColor(false);

  // Record everything as a slightly lighter white.
  const unsigned alpha_light = 18u;
  white_flags.setAlpha(alpha_light);
  recording_source_light->add_draw_rect_with_flags(gfx::Rect(layer_bounds),
                                                   white_flags);
  recording_source_light->Rerecord();

  // Make a new RasterSource from the new recording.
  raster = recording_source_light->CreateRasterSource();

  // We're going to playback from alpha(18) white rectangle into a smaller area
  // of the recording resulting in a smaller lighter white rectangle over a
  // darker white background rectangle.
  playback_rect =
      gfx::Rect(gfx::ScaleToCeiledSize(partial_bounds, contents_scale));
  raster->PlaybackToCanvas(
      &canvas, ColorSpaceForTesting(), raster_full_rect, playback_rect,
      gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()),
      RasterSource::PlaybackSettings());

  // Test that the whole playback_rect was cleared and repainted with new alpha.
  SkColor* pixels = reinterpret_cast<SkColor*>(bitmap.getPixels());
  for (int i = 0; i < playback_rect.width(); ++i) {
    for (int j = 0; j < playback_rect.height(); ++j) {
      SCOPED_TRACE(j);
      SCOPED_TRACE(i);
      EXPECT_EQ(alpha_light, SkColorGetA(pixels[i + j * bitmap.width()]));
      EXPECT_EQ(alpha_light, SkColorGetR(pixels[i + j * bitmap.width()]));
      EXPECT_EQ(alpha_light, SkColorGetG(pixels[i + j * bitmap.width()]));
      EXPECT_EQ(alpha_light, SkColorGetB(pixels[i + j * bitmap.width()]));
    }
  }
}

TEST(RasterSourceTest, RasterContentsTransparent) {
  gfx::Size layer_bounds(5, 3);
  float contents_scale = 0.5f;

  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);
  recording_source->SetBackgroundColor(SK_ColorTRANSPARENT);
  recording_source->SetRequiresClear(true);
  recording_source->SetClearCanvasWithDebugColor(false);
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();
  gfx::Size content_bounds(
      gfx::ScaleToCeiledSize(layer_bounds, contents_scale));

  gfx::Rect canvas_rect(content_bounds);
  canvas_rect.Inset(0, 0, -1, -1);

  SkBitmap bitmap;
  bitmap.allocN32Pixels(canvas_rect.width(), canvas_rect.height());
  SkCanvas canvas(bitmap);

  raster->PlaybackToCanvas(
      &canvas, ColorSpaceForTesting(), canvas_rect, canvas_rect,
      gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()),
      RasterSource::PlaybackSettings());

  SkColor* pixels = reinterpret_cast<SkColor*>(bitmap.getPixels());
  int num_pixels = bitmap.width() * bitmap.height();
  for (int i = 0; i < num_pixels; ++i) {
    EXPECT_EQ(SkColorGetA(pixels[i]), 0u);
  }
}

TEST(RasterSourceTest, GetPictureMemoryUsageIncludesClientReportedMemory) {
  const size_t kReportedMemoryUsageInBytes = 100 * 1024 * 1024;
  gfx::Size layer_bounds(5, 3);
  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(layer_bounds);
  recording_source->set_reported_memory_usage(kReportedMemoryUsageInBytes);
  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster = recording_source->CreateRasterSource();
  size_t total_memory_usage = raster->GetMemoryUsage();
  EXPECT_GE(total_memory_usage, kReportedMemoryUsageInBytes);
  EXPECT_LT(total_memory_usage, 2 * kReportedMemoryUsageInBytes);
}

TEST(RasterSourceTest, ImageHijackCanvasRespectsSharedCanvasTransform) {
  gfx::Size size(100, 100);

  // Create a recording source that is filled with red and every corner is
  // green (4x4 rects in the corner are green to account for blending when
  // scaling). Note that we paint an image first, so that we can force image
  // hijack canvas to be used.
  std::unique_ptr<FakeRecordingSource> recording_source =
      FakeRecordingSource::CreateFilledRecordingSource(size);

  // 1. Paint the image.
  recording_source->add_draw_image(CreateDiscardableImage(gfx::Size(5, 5)),
                                   gfx::Point(0, 0));

  // 2. Cover everything in red.
  PaintFlags flags;
  flags.setColor(SK_ColorRED);
  recording_source->add_draw_rect_with_flags(gfx::Rect(size), flags);

  // 3. Draw 4x4 green rects into every corner.
  flags.setColor(SK_ColorGREEN);
  recording_source->add_draw_rect_with_flags(gfx::Rect(0, 0, 4, 4), flags);
  recording_source->add_draw_rect_with_flags(
      gfx::Rect(size.width() - 4, 0, 4, 4), flags);
  recording_source->add_draw_rect_with_flags(
      gfx::Rect(0, size.height() - 4, 4, 4), flags);
  recording_source->add_draw_rect_with_flags(
      gfx::Rect(size.width() - 4, size.height() - 4, 4, 4), flags);

  recording_source->Rerecord();

  scoped_refptr<RasterSource> raster_source =
      recording_source->CreateRasterSource();
  SoftwareImageDecodeCache controller(
      viz::ResourceFormat::RGBA_8888,
      LayerTreeSettings().decoded_image_working_set_budget_bytes);
  PlaybackImageProvider image_provider(false, PaintImageIdFlatSet(),
                                       &controller, gfx::ColorSpace());

  SkBitmap bitmap;
  bitmap.allocN32Pixels(size.width() * 0.5f, size.height() * 0.25f);
  SkCanvas canvas(bitmap);
  canvas.scale(0.5f, 0.25f);

  RasterSource::PlaybackSettings settings;
  settings.playback_to_shared_canvas = true;
  settings.image_provider = &image_provider;
  raster_source->PlaybackToCanvas(&canvas, ColorSpaceForTesting(),
                                  gfx::Rect(size), gfx::Rect(size),
                                  gfx::AxisTransform2d(), settings);

  EXPECT_EQ(SK_ColorGREEN, bitmap.getColor(0, 0));
  EXPECT_EQ(SK_ColorGREEN, bitmap.getColor(49, 0));
  EXPECT_EQ(SK_ColorGREEN, bitmap.getColor(0, 24));
  EXPECT_EQ(SK_ColorGREEN, bitmap.getColor(49, 24));
  for (int x = 0; x < 49; ++x)
    EXPECT_EQ(SK_ColorRED, bitmap.getColor(x, 12));
  for (int y = 0; y < 24; ++y)
    EXPECT_EQ(SK_ColorRED, bitmap.getColor(24, y));
}

}  // namespace
}  // namespace cc
