// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/benchmarks/rasterize_and_record_benchmark_impl.h"

#include <stddef.h>

#include <algorithm>
#include <limits>

#include "base/values.h"
#include "cc/base/lap_timer.h"
#include "cc/layers/layer_impl.h"
#include "cc/layers/picture_layer_impl.h"
#include "cc/raster/playback_image_provider.h"
#include "cc/raster/raster_buffer_provider.h"
#include "cc/trees/layer_tree_host_common.h"
#include "cc/trees/layer_tree_host_impl.h"
#include "cc/trees/layer_tree_impl.h"
#include "ui/gfx/geometry/axis_transform2d.h"
#include "ui/gfx/geometry/rect.h"

namespace cc {

namespace {

const int kDefaultRasterizeRepeatCount = 100;

void RunBenchmark(RasterSource* raster_source,
                  ImageDecodeCache* image_decode_cache,
                  const gfx::Rect& content_rect,
                  float contents_scale,
                  size_t repeat_count,
                  base::TimeDelta* min_time,
                  bool* is_solid_color) {
  // Parameters for LapTimer.
  const int kTimeLimitMillis = 1;
  const int kWarmupRuns = 0;
  const int kTimeCheckInterval = 1;

  *min_time = base::TimeDelta::Max();
  for (size_t i = 0; i < repeat_count; ++i) {
    // Run for a minimum amount of time to avoid problems with timer
    // quantization when the layer is very small.
    LapTimer timer(kWarmupRuns,
                   base::TimeDelta::FromMilliseconds(kTimeLimitMillis),
                   kTimeCheckInterval);
    SkColor color = SK_ColorTRANSPARENT;
    gfx::Rect layer_rect =
        gfx::ScaleToEnclosingRect(content_rect, 1.f / contents_scale);
    *is_solid_color =
        raster_source->PerformSolidColorAnalysis(layer_rect, &color);

    do {
      SkBitmap bitmap;
      bitmap.allocPixels(SkImageInfo::MakeN32Premul(content_rect.width(),
                                                    content_rect.height()));
      SkCanvas canvas(bitmap);

      PlaybackImageProvider image_provider(
          false, PaintImageIdFlatSet(), image_decode_cache, gfx::ColorSpace());
      RasterSource::PlaybackSettings settings;
      settings.image_provider = &image_provider;

      raster_source->PlaybackToCanvas(
          &canvas, gfx::ColorSpace(), content_rect, content_rect,
          gfx::AxisTransform2d(contents_scale, gfx::Vector2dF()), settings);

      timer.NextLap();
    } while (!timer.HasTimeLimitExpired());
    base::TimeDelta duration =
        base::TimeDelta::FromMillisecondsD(timer.MsPerLap());
    if (duration < *min_time)
      *min_time = duration;
  }
}

class FixedInvalidationPictureLayerTilingClient
    : public PictureLayerTilingClient {
 public:
  FixedInvalidationPictureLayerTilingClient(
      PictureLayerTilingClient* base_client,
      const Region& invalidation)
      : base_client_(base_client), invalidation_(invalidation) {}

  std::unique_ptr<Tile> CreateTile(const Tile::CreateInfo& info) override {
    return base_client_->CreateTile(info);
  }

  gfx::Size CalculateTileSize(const gfx::Size& content_bounds) const override {
    return base_client_->CalculateTileSize(content_bounds);
  }

  // This is the only function that returns something different from the base
  // client. Avoids sharing tiles in this area.
  const Region* GetPendingInvalidation() override { return &invalidation_; }

  const PictureLayerTiling* GetPendingOrActiveTwinTiling(
      const PictureLayerTiling* tiling) const override {
    return base_client_->GetPendingOrActiveTwinTiling(tiling);
  }

  bool HasValidTilePriorities() const override {
    return base_client_->HasValidTilePriorities();
  }

  bool RequiresHighResToDraw() const override {
    return base_client_->RequiresHighResToDraw();
  }

 private:
  PictureLayerTilingClient* base_client_;
  Region invalidation_;
};

}  // namespace

RasterizeAndRecordBenchmarkImpl::RasterizeAndRecordBenchmarkImpl(
    scoped_refptr<base::SingleThreadTaskRunner> origin_task_runner,
    base::Value* value,
    const MicroBenchmarkImpl::DoneCallback& callback)
    : MicroBenchmarkImpl(callback, origin_task_runner),
      rasterize_repeat_count_(kDefaultRasterizeRepeatCount) {
  base::DictionaryValue* settings = nullptr;
  value->GetAsDictionary(&settings);
  if (!settings)
    return;

  if (settings->HasKey("rasterize_repeat_count"))
    settings->GetInteger("rasterize_repeat_count", &rasterize_repeat_count_);
}

RasterizeAndRecordBenchmarkImpl::~RasterizeAndRecordBenchmarkImpl() {}

void RasterizeAndRecordBenchmarkImpl::DidCompleteCommit(
    LayerTreeHostImpl* host) {
  LayerTreeHostCommon::CallFunctionForEveryLayer(
      host->active_tree(), [this](LayerImpl* layer) {
        rasterize_results_.total_layers++;
        layer->RunMicroBenchmark(this);
      });

  std::unique_ptr<base::DictionaryValue> result(new base::DictionaryValue());
  result->SetDouble("rasterize_time_ms",
                    rasterize_results_.total_best_time.InMillisecondsF());
  result->SetDouble("total_pictures_in_pile_size",
                    static_cast<int>(rasterize_results_.total_memory_usage));
  result->SetInteger("pixels_rasterized", rasterize_results_.pixels_rasterized);
  result->SetInteger("pixels_rasterized_with_non_solid_color",
                     rasterize_results_.pixels_rasterized_with_non_solid_color);
  result->SetInteger("pixels_rasterized_as_opaque",
                     rasterize_results_.pixels_rasterized_as_opaque);
  result->SetInteger("total_layers", rasterize_results_.total_layers);
  result->SetInteger("total_picture_layers",
                     rasterize_results_.total_picture_layers);
  result->SetInteger("total_picture_layers_with_no_content",
                     rasterize_results_.total_picture_layers_with_no_content);
  result->SetInteger("total_picture_layers_off_screen",
                     rasterize_results_.total_picture_layers_off_screen);

  NotifyDone(std::move(result));
}

void RasterizeAndRecordBenchmarkImpl::RunOnLayer(PictureLayerImpl* layer) {
  rasterize_results_.total_picture_layers++;
  if (!layer->CanHaveTilings()) {
    rasterize_results_.total_picture_layers_with_no_content++;
    return;
  }
  if (layer->visible_layer_rect().IsEmpty()) {
    rasterize_results_.total_picture_layers_off_screen++;
    return;
  }

  FixedInvalidationPictureLayerTilingClient client(layer,
                                                   gfx::Rect(layer->bounds()));

  // In this benchmark, we will create a local tiling set and measure how long
  // it takes to rasterize content. As such, the actual settings used here don't
  // really matter.
  const LayerTreeSettings& settings = layer->layer_tree_impl()->settings();
  std::unique_ptr<PictureLayerTilingSet> tiling_set =
      PictureLayerTilingSet::Create(
          layer->GetTree(), &client, settings.tiling_interest_area_padding,
          settings.skewport_target_time_in_seconds,
          settings.skewport_extrapolation_limit_in_screen_pixels,
          settings.max_preraster_distance_in_screen_pixels);

  PictureLayerTiling* tiling =
      tiling_set->AddTiling(gfx::AxisTransform2d(), layer->GetRasterSource());
  tiling->set_resolution(HIGH_RESOLUTION);
  tiling->CreateAllTilesForTesting();
  RasterSource* raster_source = tiling->raster_source().get();
  for (PictureLayerTiling::CoverageIterator it(tiling, 1.f,
                                               layer->visible_layer_rect());
       it; ++it) {
    DCHECK(*it);

    gfx::Rect content_rect = (*it)->content_rect();
    float contents_scale = (*it)->raster_transform().scale();

    base::TimeDelta min_time;
    bool is_solid_color = false;
    RunBenchmark(raster_source, layer->layer_tree_impl()->image_decode_cache(),
                 content_rect, contents_scale, rasterize_repeat_count_,
                 &min_time, &is_solid_color);

    int tile_size = content_rect.width() * content_rect.height();
    if (layer->contents_opaque())
      rasterize_results_.pixels_rasterized_as_opaque += tile_size;

    if (!is_solid_color)
      rasterize_results_.pixels_rasterized_with_non_solid_color += tile_size;

    rasterize_results_.pixels_rasterized += tile_size;
    rasterize_results_.total_best_time += min_time;
  }

  const RasterSource* layer_raster_source = layer->GetRasterSource();
  rasterize_results_.total_memory_usage +=
      layer_raster_source->GetMemoryUsage();
}

RasterizeAndRecordBenchmarkImpl::RasterizeResults::RasterizeResults()
    : pixels_rasterized(0),
      pixels_rasterized_with_non_solid_color(0),
      pixels_rasterized_as_opaque(0),
      total_memory_usage(0),
      total_layers(0),
      total_picture_layers(0),
      total_picture_layers_with_no_content(0),
      total_picture_layers_off_screen(0) {}

RasterizeAndRecordBenchmarkImpl::RasterizeResults::~RasterizeResults() {}

}  // namespace cc
