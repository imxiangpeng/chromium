// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/paint/discardable_image_map.h"

#include <stddef.h>

#include <memory>

#include "base/memory/ref_counted.h"
#include "base/values.h"
#include "cc/base/region.h"
#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_op_buffer.h"
#include "cc/paint/paint_recorder.h"
#include "cc/test/fake_content_layer_client.h"
#include "cc/test/fake_recording_source.h"
#include "cc/test/skia_common.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkGraphics.h"
#include "third_party/skia/include/core/SkImageGenerator.h"
#include "third_party/skia/include/core/SkRefCnt.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/skia_util.h"

namespace cc {
namespace {

PaintImage CreateDiscardablePaintImageWithColorSpace(
    const gfx::Size& size,
    const gfx::ColorSpace& color_space) {
  sk_sp<SkImage> sk_image = CreateDiscardableImage(size);
  if (color_space.IsValid()) {
    sk_image = sk_image->makeColorSpace(color_space.ToSkColorSpace(),
                                        SkTransferFunctionBehavior::kIgnore);
  }
  return PaintImage(PaintImage::GetNextId(), sk_image);
}

PaintImage CreateDiscardablePaintImage(const gfx::Size& size) {
  return CreateDiscardablePaintImageWithColorSpace(size, gfx::ColorSpace());
}

struct PositionScaleDrawImage {
  PositionScaleDrawImage(const PaintImage& image,
                         const gfx::Rect& image_rect,
                         const SkSize& scale)
      : image(image), image_rect(image_rect), scale(scale) {}
  PaintImage image;
  gfx::Rect image_rect;
  SkSize scale;
};

sk_sp<PaintOpBuffer> CreateRecording(const PaintImage& discardable_image,
                                     const gfx::Rect& visible_rect) {
  auto buffer = sk_make_sp<PaintOpBuffer>();
  buffer->push<DrawImageOp>(discardable_image, 0.f, 0.f, nullptr);
  return buffer;
}

}  // namespace

class DiscardableImageMapTest : public testing::Test {
 public:
  std::vector<PositionScaleDrawImage> GetDiscardableImagesInRect(
      const DiscardableImageMap& image_map,
      const gfx::Rect& rect) {
    std::vector<DrawImage> draw_images;
    // Choose a not-SRGB-and-not-invalid target color space to verify that it
    // is passed correctly to the resulting DrawImages.
    gfx::ColorSpace target_color_space = gfx::ColorSpace::CreateXYZD50();
    image_map.GetDiscardableImagesInRect(rect, 1.f, target_color_space,
                                         &draw_images);

    std::vector<PositionScaleDrawImage> position_draw_images;
    for (DrawImage& image : image_map.images_rtree_.Search(rect)) {
      auto image_id = image.paint_image().stable_id();
      position_draw_images.push_back(PositionScaleDrawImage(
          image.paint_image(), image_map.GetRectForImage(image_id),
          image.scale()));
    }

    EXPECT_EQ(draw_images.size(), position_draw_images.size());
    for (size_t i = 0; i < draw_images.size(); ++i) {
      EXPECT_TRUE(draw_images[i].paint_image() ==
                  position_draw_images[i].image);
      EXPECT_EQ(draw_images[i].target_color_space(), target_color_space);
    }
    return position_draw_images;
  }

  // Note that the image rtree outsets the images by 1, see the comment in
  // DiscardableImagesMetadataCanvas::AddImage.
  std::vector<gfx::Rect> InsetImageRects(
      const std::vector<PositionScaleDrawImage>& images) {
    std::vector<gfx::Rect> result;
    for (auto& image : images) {
      result.push_back(image.image_rect);
      result.back().Inset(1, 1, 1, 1);
    }
    return result;
  }
};

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectTest) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  PaintImage discardable_image[4][4];
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] =
            CreateDiscardablePaintImage(gfx::Size(500, 500));
        PaintFlags flags;
        content_layer_client.add_draw_image(
            discardable_image[y][x], gfx::Point(x * 512 + 6, y * 512 + 6),
            flags);
      }
    }
  }

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<PositionScaleDrawImage> images = GetDiscardableImagesInRect(
          image_map, gfx::Rect(x * 512, y * 512, 500, 500));
      std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
      if ((x + y) & 1) {
        EXPECT_EQ(1u, images.size()) << x << " " << y;
        EXPECT_TRUE(images[0].image == discardable_image[y][x])
            << x << " " << y;
        EXPECT_EQ(gfx::Rect(x * 512 + 6, y * 512 + 6, 500, 500),
                  inset_rects[0]);
        EXPECT_EQ(images[0].image_rect,
                  image_map.GetRectForImage(images[0].image.stable_id()));
      } else {
        EXPECT_EQ(0u, images.size()) << x << " " << y;
      }
    }
  }

  // Capture 4 pixel refs.
  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(512, 512, 2048, 2048));
  std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
  EXPECT_EQ(4u, images.size());

  EXPECT_TRUE(images[0].image == discardable_image[1][2]);
  EXPECT_EQ(gfx::Rect(2 * 512 + 6, 512 + 6, 500, 500), inset_rects[0]);
  EXPECT_EQ(images[0].image_rect,
            image_map.GetRectForImage(images[0].image.stable_id()));

  EXPECT_TRUE(images[1].image == discardable_image[2][1]);
  EXPECT_EQ(gfx::Rect(512 + 6, 2 * 512 + 6, 500, 500), inset_rects[1]);
  EXPECT_EQ(images[1].image_rect,
            image_map.GetRectForImage(images[1].image.stable_id()));

  EXPECT_TRUE(images[2].image == discardable_image[2][3]);
  EXPECT_EQ(gfx::Rect(3 * 512 + 6, 2 * 512 + 6, 500, 500), inset_rects[2]);
  EXPECT_EQ(images[2].image_rect,
            image_map.GetRectForImage(images[2].image.stable_id()));

  EXPECT_TRUE(images[3].image == discardable_image[3][2]);
  EXPECT_EQ(gfx::Rect(2 * 512 + 6, 3 * 512 + 6, 500, 500), inset_rects[3]);
  EXPECT_EQ(images[3].image_rect,
            image_map.GetRectForImage(images[3].image.stable_id()));
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectNonZeroLayer) {
  gfx::Rect visible_rect(1024, 0, 2048, 2048);
  // Make sure visible rect fits into the layer size.
  gfx::Size layer_size(visible_rect.right(), visible_rect.bottom());
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(layer_size);

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  PaintImage discardable_image[4][4];
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] =
            CreateDiscardablePaintImage(gfx::Size(500, 500));
        PaintFlags flags;
        content_layer_client.add_draw_image(
            discardable_image[y][x],
            gfx::Point(1024 + x * 512 + 6, y * 512 + 6), flags);
      }
    }
  }

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<PositionScaleDrawImage> images = GetDiscardableImagesInRect(
          image_map, gfx::Rect(1024 + x * 512, y * 512, 500, 500));
      std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
      if ((x + y) & 1) {
        EXPECT_EQ(1u, images.size()) << x << " " << y;
        EXPECT_TRUE(images[0].image == discardable_image[y][x])
            << x << " " << y;
        EXPECT_EQ(gfx::Rect(1024 + x * 512 + 6, y * 512 + 6, 500, 500),
                  inset_rects[0]);
        EXPECT_EQ(images[0].image_rect,
                  image_map.GetRectForImage(images[0].image.stable_id()));
      } else {
        EXPECT_EQ(0u, images.size()) << x << " " << y;
      }
    }
  }
  // Capture 4 pixel refs.
  {
    std::vector<PositionScaleDrawImage> images = GetDiscardableImagesInRect(
        image_map, gfx::Rect(1024 + 512, 512, 2048, 2048));
    std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
    EXPECT_EQ(4u, images.size());

    EXPECT_TRUE(images[0].image == discardable_image[1][2]);
    EXPECT_EQ(gfx::Rect(1024 + 2 * 512 + 6, 512 + 6, 500, 500), inset_rects[0]);
    EXPECT_EQ(images[0].image_rect,
              image_map.GetRectForImage(images[0].image.stable_id()));

    EXPECT_TRUE(images[1].image == discardable_image[2][1]);
    EXPECT_EQ(gfx::Rect(1024 + 512 + 6, 2 * 512 + 6, 500, 500), inset_rects[1]);
    EXPECT_EQ(images[1].image_rect,
              image_map.GetRectForImage(images[1].image.stable_id()));

    EXPECT_TRUE(images[2].image == discardable_image[2][3]);
    EXPECT_EQ(gfx::Rect(1024 + 3 * 512 + 6, 2 * 512 + 6, 500, 500),
              inset_rects[2]);
    EXPECT_EQ(images[2].image_rect,
              image_map.GetRectForImage(images[2].image.stable_id()));

    EXPECT_TRUE(images[3].image == discardable_image[3][2]);
    EXPECT_EQ(gfx::Rect(1024 + 2 * 512 + 6, 3 * 512 + 6, 500, 500),
              inset_rects[3]);
    EXPECT_EQ(images[3].image_rect,
              image_map.GetRectForImage(images[3].image.stable_id()));
  }

  // Non intersecting rects
  {
    std::vector<PositionScaleDrawImage> images =
        GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
  {
    std::vector<PositionScaleDrawImage> images =
        GetDiscardableImagesInRect(image_map, gfx::Rect(3500, 0, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
  {
    std::vector<PositionScaleDrawImage> images =
        GetDiscardableImagesInRect(image_map, gfx::Rect(0, 1100, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }
  {
    std::vector<PositionScaleDrawImage> images = GetDiscardableImagesInRect(
        image_map, gfx::Rect(3500, 1100, 1000, 1000));
    EXPECT_EQ(0u, images.size());
  }

  // Image not present in the list.
  {
    PaintImage image = CreateDiscardablePaintImage(gfx::Size(500, 500));
    EXPECT_EQ(gfx::Rect(), image_map.GetRectForImage(image.stable_id()));
  }
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectOnePixelQuery) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  PaintImage discardable_image[4][4];
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] =
            CreateDiscardablePaintImage(gfx::Size(500, 500));
        PaintFlags flags;
        content_layer_client.add_draw_image(
            discardable_image[y][x], gfx::Point(x * 512 + 6, y * 512 + 6),
            flags);
      }
    }
  }

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<PositionScaleDrawImage> images = GetDiscardableImagesInRect(
          image_map, gfx::Rect(x * 512 + 256, y * 512 + 256, 1, 1));
      std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
      if ((x + y) & 1) {
        EXPECT_EQ(1u, images.size()) << x << " " << y;
        EXPECT_TRUE(images[0].image == discardable_image[y][x])
            << x << " " << y;
        EXPECT_EQ(gfx::Rect(x * 512 + 6, y * 512 + 6, 500, 500),
                  inset_rects[0]);
        EXPECT_EQ(images[0].image_rect,
                  image_map.GetRectForImage(images[0].image.stable_id()));
      } else {
        EXPECT_EQ(0u, images.size()) << x << " " << y;
      }
    }
  }
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectMassiveImage) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  PaintImage discardable_image =
      CreateDiscardablePaintImage(gfx::Size(1 << 25, 1 << 25));
  PaintFlags flags;
  content_layer_client.add_draw_image(discardable_image, gfx::Point(0, 0),
                                      flags);

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1, 1));
  std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image);
  EXPECT_EQ(gfx::Rect(0, 0, 2048, 2048), inset_rects[0]);
  EXPECT_EQ(images[0].image_rect,
            image_map.GetRectForImage(images[0].image.stable_id()));
}

TEST_F(DiscardableImageMapTest, PaintDestroyedWhileImageIsDrawn) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  PaintImage discardable_image = CreateDiscardablePaintImage(gfx::Size(10, 10));
  sk_sp<PaintRecord> record = CreateRecording(discardable_image, visible_rect);

  scoped_refptr<DisplayItemList> display_list = new DisplayItemList;
  PaintFlags paint;
  display_list->StartPaint();
  SkRect visible_sk_rect(gfx::RectToSkRect(visible_rect));
  display_list->push<SaveLayerOp>(&visible_sk_rect, &paint);
  display_list->push<DrawRecordOp>(std::move(record));
  display_list->push<RestoreOp>();
  display_list->EndPaintOfUnpaired(visible_rect);
  display_list->Finalize();

  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();
  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1, 1));
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image);
}

TEST_F(DiscardableImageMapTest, NullPaintOnSaveLayer) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  PaintImage discardable_image = CreateDiscardablePaintImage(gfx::Size(10, 10));
  sk_sp<PaintRecord> record = CreateRecording(discardable_image, visible_rect);

  scoped_refptr<DisplayItemList> display_list = new DisplayItemList;
  display_list->StartPaint();
  SkRect visible_sk_rect(gfx::RectToSkRect(visible_rect));
  display_list->push<SaveLayerOp>(&visible_sk_rect, nullptr);
  display_list->push<DrawRecordOp>(std::move(record));
  display_list->push<RestoreOp>();
  display_list->EndPaintOfUnpaired(visible_rect);
  display_list->Finalize();

  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();
  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1, 1));
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image);
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectMaxImage) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  int dimension = std::numeric_limits<int>::max();
  PaintImage discardable_image =
      CreateDiscardablePaintImage(gfx::Size(dimension, dimension));
  PaintFlags flags;
  content_layer_client.add_draw_image(discardable_image, gfx::Point(42, 42),
                                      flags);

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(42, 42, 1, 1));
  std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image);
  EXPECT_EQ(gfx::Rect(42, 42, 2006, 2006), inset_rects[0]);
  EXPECT_EQ(images[0].image_rect,
            image_map.GetRectForImage(images[0].image.stable_id()));
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInRectMaxImageMaxLayer) {
  // At large values of integer x, x != static_cast<int>(static_cast<float>(x)).
  // So, make sure the dimension can be converted back and forth for the
  // purposes of the unittest. Also, at near max int values, Skia seems to skip
  // some draw calls, so we subtract 64 since we only care about "really large"
  // values, not necessarily max int values.
  int dimension = static_cast<int>(
      static_cast<float>(std::numeric_limits<int>::max() - 64));
  gfx::Rect visible_rect(dimension, dimension);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  PaintImage discardable_image1 =
      CreateDiscardablePaintImage(gfx::Size(dimension, dimension));
  PaintImage discardable_image2 =
      CreateDiscardablePaintImage(gfx::Size(dimension, dimension));
  PaintImage discardable_image3 =
      CreateDiscardablePaintImage(gfx::Size(dimension, dimension));

  PaintFlags flags;
  content_layer_client.add_draw_image(discardable_image1, gfx::Point(0, 0),
                                      flags);
  content_layer_client.add_draw_image(discardable_image2, gfx::Point(10000, 0),
                                      flags);
  content_layer_client.add_draw_image(discardable_image3,
                                      gfx::Point(-10000, 500), flags);

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1, 1));
  std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
  EXPECT_EQ(1u, images.size());
  EXPECT_EQ(gfx::Rect(0, 0, dimension, dimension), inset_rects[0]);

  images = GetDiscardableImagesInRect(image_map, gfx::Rect(10000, 0, 1, 1));
  inset_rects = InsetImageRects(images);
  EXPECT_EQ(2u, images.size());
  EXPECT_EQ(gfx::Rect(10000, 0, dimension - 10000, dimension), inset_rects[1]);
  EXPECT_EQ(gfx::Rect(0, 0, dimension, dimension), inset_rects[0]);

  // Since we adjust negative offsets before using ToEnclosingRect, the expected
  // width will be converted to float, which means that we lose some precision.
  // The expected value is whatever the value is converted to float and then
  // back to int.
  int expected10k = static_cast<int>(static_cast<float>(dimension - 10000));
  images = GetDiscardableImagesInRect(image_map, gfx::Rect(0, 500, 1, 1));
  inset_rects = InsetImageRects(images);
  EXPECT_EQ(2u, images.size());
  EXPECT_EQ(gfx::Rect(0, 500, expected10k, dimension - 500), inset_rects[1]);
  EXPECT_EQ(gfx::Rect(0, 0, dimension, dimension), inset_rects[0]);
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesRectInBounds) {
  gfx::Rect visible_rect(1000, 1000);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  PaintImage discardable_image1 =
      CreateDiscardablePaintImage(gfx::Size(100, 100));
  PaintImage discardable_image2 =
      CreateDiscardablePaintImage(gfx::Size(100, 100));
  PaintImage long_discardable_image =
      CreateDiscardablePaintImage(gfx::Size(10000, 100));

  PaintFlags flags;
  content_layer_client.add_draw_image(discardable_image1, gfx::Point(-10, -11),
                                      flags);
  content_layer_client.add_draw_image(discardable_image2, gfx::Point(950, 951),
                                      flags);
  content_layer_client.add_draw_image(long_discardable_image,
                                      gfx::Point(-100, 500), flags);

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, gfx::Rect(0, 0, 1, 1));
  std::vector<gfx::Rect> inset_rects = InsetImageRects(images);

  ASSERT_EQ(1u, images.size());
  EXPECT_EQ(gfx::Rect(0, 0, 90, 89), inset_rects[0]);

  images = GetDiscardableImagesInRect(image_map, gfx::Rect(999, 999, 1, 1));
  inset_rects = InsetImageRects(images);
  ASSERT_EQ(1u, images.size());
  EXPECT_EQ(gfx::Rect(950, 951, 50, 49), inset_rects[0]);

  images = GetDiscardableImagesInRect(image_map, gfx::Rect(0, 500, 1, 1));
  inset_rects = InsetImageRects(images);
  ASSERT_EQ(1u, images.size());
  EXPECT_EQ(gfx::Rect(0, 500, 1000, 100), inset_rects[0]);
}

TEST_F(DiscardableImageMapTest, GetDiscardableImagesInShader) {
  gfx::Rect visible_rect(2048, 2048);
  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());

  // Discardable pixel refs are found in the following grids:
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  // |   | x |   | x |
  // |---|---|---|---|
  // | x |   | x |   |
  // |---|---|---|---|
  sk_sp<SkImage> discardable_image[4][4];

  // Skia doesn't allow shader instantiation with non-invertible local
  // transforms, so we can't let the scale drop all the way to 0.
  static constexpr float kMinScale = 0.1f;

  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      if ((x + y) & 1) {
        discardable_image[y][x] = CreateDiscardableImage(gfx::Size(500, 500));
        SkMatrix scale = SkMatrix::MakeScale(std::max(x * 0.5f, kMinScale),
                                             std::max(y * 0.5f, kMinScale));
        PaintFlags flags;
        flags.setShader(PaintShader::MakeImage(
            PaintImage(y * 4 + x, discardable_image[y][x]),
            SkShader::kClamp_TileMode, SkShader::kClamp_TileMode, &scale));
        content_layer_client.add_draw_rect(
            gfx::Rect(x * 512 + 6, y * 512 + 6, 500, 500), flags);
      }
    }
  }

  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  gfx::ColorSpace target_color_space = gfx::ColorSpace::CreateXYZD50();
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      std::vector<DrawImage> draw_images;
      image_map.GetDiscardableImagesInRect(
          gfx::Rect(x * 512, y * 512, 500, 500), 1.f, target_color_space,
          &draw_images);
      if ((x + y) & 1) {
        EXPECT_EQ(1u, draw_images.size()) << x << " " << y;
        EXPECT_TRUE(draw_images[0].image() == discardable_image[y][x])
            << x << " " << y;
        EXPECT_EQ(std::max(x * 0.5f, kMinScale), draw_images[0].scale().fWidth);
        EXPECT_EQ(std::max(y * 0.5f, kMinScale),
                  draw_images[0].scale().fHeight);
      } else {
        EXPECT_EQ(0u, draw_images.size()) << x << " " << y;
      }
    }
  }

  // Capture 4 pixel refs.
  std::vector<DrawImage> draw_images;
  image_map.GetDiscardableImagesInRect(gfx::Rect(512, 512, 2048, 2048), 1.f,
                                       target_color_space, &draw_images);
  EXPECT_EQ(4u, draw_images.size());
  EXPECT_TRUE(draw_images[0].image() == discardable_image[1][2]);
  EXPECT_TRUE(draw_images[1].image() == discardable_image[2][1]);
  EXPECT_TRUE(draw_images[2].image() == discardable_image[2][3]);
  EXPECT_TRUE(draw_images[3].image() == discardable_image[3][2]);
}

TEST_F(DiscardableImageMapTest, ClipsImageRects) {
  gfx::Rect visible_rect(500, 500);

  PaintImage discardable_image =
      CreateDiscardablePaintImage(gfx::Size(500, 500));
  sk_sp<PaintRecord> record = CreateRecording(discardable_image, visible_rect);

  scoped_refptr<DisplayItemList> display_list = new DisplayItemList;

  display_list->StartPaint();
  display_list->push<ClipRectOp>(gfx::RectToSkRect(gfx::Rect(250, 250)),
                                 SkClipOp::kIntersect, false);
  display_list->push<DrawRecordOp>(std::move(record));
  display_list->EndPaintOfUnpaired(gfx::Rect(250, 250));

  display_list->Finalize();

  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();
  std::vector<PositionScaleDrawImage> images =
      GetDiscardableImagesInRect(image_map, visible_rect);
  std::vector<gfx::Rect> inset_rects = InsetImageRects(images);
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(images[0].image == discardable_image);
  EXPECT_EQ(gfx::Rect(250, 250), inset_rects[0]);
}

TEST_F(DiscardableImageMapTest, GathersDiscardableImagesFromNestedOps) {
  // This |discardable_image| is in a PaintOpBuffer that gets added to
  // the root buffer.
  auto internal_record = sk_make_sp<PaintOpBuffer>();
  PaintImage discardable_image =
      CreateDiscardablePaintImage(gfx::Size(100, 100));
  internal_record->push<DrawImageOp>(discardable_image, 0.f, 0.f, nullptr);

  // This |discardable_image2| is in a DisplayItemList that gets added
  // to the root buffer.
  PaintImage discardable_image2 =
      CreateDiscardablePaintImage(gfx::Size(100, 100));

  scoped_refptr<DisplayItemList> display_list =
      new DisplayItemList(DisplayItemList::kToBeReleasedAsPaintOpBuffer);
  display_list->StartPaint();
  display_list->push<DrawImageOp>(discardable_image2, 100.f, 100.f, nullptr);
  display_list->EndPaintOfUnpaired(gfx::Rect(100, 100, 100, 100));
  display_list->Finalize();

  sk_sp<PaintRecord> record2 = display_list->ReleaseAsRecord();

  PaintOpBuffer root_buffer;
  root_buffer.push<DrawRecordOp>(internal_record);
  root_buffer.push<DrawRecordOp>(record2);
  DiscardableImageMap image_map;
  image_map.Generate(&root_buffer, gfx::Rect(200, 200));

  gfx::ColorSpace target_color_space;
  std::vector<DrawImage> images;
  image_map.GetDiscardableImagesInRect(gfx::Rect(0, 0, 5, 95), 1.f,
                                       target_color_space, &images);
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(discardable_image == images[0].paint_image());

  images.clear();
  image_map.GetDiscardableImagesInRect(gfx::Rect(105, 105, 5, 95), 1.f,
                                       target_color_space, &images);
  EXPECT_EQ(1u, images.size());
  EXPECT_TRUE(discardable_image2 == images[0].paint_image());
}

class DiscardableImageMapColorSpaceTest
    : public DiscardableImageMapTest,
      public testing::WithParamInterface<gfx::ColorSpace> {};

TEST_P(DiscardableImageMapColorSpaceTest, ColorSpace) {
  const gfx::ColorSpace image_color_space = GetParam();
  gfx::Rect visible_rect(500, 500);
  PaintImage discardable_image = CreateDiscardablePaintImageWithColorSpace(
      gfx::Size(500, 500), image_color_space);

  FakeContentLayerClient content_layer_client;
  content_layer_client.set_bounds(visible_rect.size());
  content_layer_client.add_draw_image(discardable_image, gfx::Point(0, 0),
                                      PaintFlags());
  scoped_refptr<DisplayItemList> display_list =
      content_layer_client.PaintContentsToDisplayList(
          ContentLayerClient::PAINTING_BEHAVIOR_NORMAL);
  display_list->GenerateDiscardableImagesMetadata();
  const DiscardableImageMap& image_map = display_list->discardable_image_map();

  if (!image_color_space.IsValid())
    EXPECT_TRUE(image_map.all_images_are_srgb());
  else if (image_color_space == gfx::ColorSpace::CreateSRGB())
    EXPECT_TRUE(image_map.all_images_are_srgb());
  else
    EXPECT_FALSE(image_map.all_images_are_srgb());
}

gfx::ColorSpace test_color_spaces[] = {
    gfx::ColorSpace(), gfx::ColorSpace::CreateSRGB(),
    gfx::ColorSpace::CreateDisplayP3D65(),
};

INSTANTIATE_TEST_CASE_P(ColorSpace,
                        DiscardableImageMapColorSpaceTest,
                        testing::ValuesIn(test_color_spaces));

}  // namespace cc
