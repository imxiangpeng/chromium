// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <utility>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/memory/ptr_util.h"
#include "base/test/scoped_feature_list.h"
#include "cc/base/filter_operation.h"
#include "cc/base/region.h"
#include "cc/output/ca_layer_overlay.h"
#include "cc/output/output_surface.h"
#include "cc/output/output_surface_client.h"
#include "cc/output/output_surface_frame.h"
#include "cc/output/overlay_candidate_validator.h"
#include "cc/output/overlay_processor.h"
#include "cc/output/overlay_strategy_fullscreen.h"
#include "cc/output/overlay_strategy_single_on_top.h"
#include "cc/output/overlay_strategy_underlay.h"
#include "cc/output/overlay_strategy_underlay_cast.h"
#include "cc/quads/render_pass.h"
#include "cc/quads/render_pass_draw_quad.h"
#include "cc/quads/solid_color_draw_quad.h"
#include "cc/quads/stream_video_draw_quad.h"
#include "cc/quads/texture_draw_quad.h"
#include "cc/resources/resource_provider.h"
#include "cc/test/fake_output_surface_client.h"
#include "cc/test/fake_resource_provider.h"
#include "cc/test/geometry_test_utils.h"
#include "cc/test/test_context_provider.h"
#include "cc/test/test_shared_bitmap_manager.h"
#include "cc/test/test_web_graphics_context_3d.h"
#include "components/viz/common/quads/texture_mailbox.h"
#include "components/viz/service/display/gl_renderer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gl/gl_switches.h"
#include "ui/latency/latency_info.h"

using testing::_;
using testing::Mock;

namespace cc {
namespace {

const gfx::Size kDisplaySize(256, 256);
const gfx::Rect kOverlayRect(0, 0, 256, 256);
const gfx::Rect kOverlayTopLeftRect(0, 0, 128, 128);
const gfx::Rect kOverlayBottomRightRect(128, 128, 128, 128);
const gfx::Rect kOverlayClipRect(0, 0, 128, 128);
const gfx::PointF kUVTopLeft(0.1f, 0.2f);
const gfx::PointF kUVBottomRight(1.0f, 1.0f);
const gfx::Transform kNormalTransform =
    gfx::Transform(0.9f, 0, 0, 0.8f, 0.1f, 0.2f);  // x,y -> x,y.
const gfx::Transform kXMirrorTransform =
    gfx::Transform(-0.9f, 0, 0, 0.8f, 1.0f, 0.2f);  // x,y -> 1-x,y.
const gfx::Transform kYMirrorTransform =
    gfx::Transform(0.9f, 0, 0, -0.8f, 0.1f, 1.0f);  // x,y -> x,1-y.
const gfx::Transform kBothMirrorTransform =
    gfx::Transform(-0.9f, 0, 0, -0.8f, 1.0f, 1.0f);  // x,y -> 1-x,1-y.
const gfx::Transform kSwapTransform =
    gfx::Transform(0, 1, 1, 0, 0, 0);  // x,y -> y,x.

void MailboxReleased(const gpu::SyncToken& sync_token,
                     bool lost_resource,
                     BlockingTaskRunner* main_thread_task_runner) {}

class FullscreenOverlayValidator : public OverlayCandidateValidator {
 public:
  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {
    strategies->push_back(base::MakeUnique<OverlayStrategyFullscreen>(this));
  }
  bool AllowCALayerOverlays() override { return false; }
  bool AllowDCLayerOverlays() override { return false; }
  void CheckOverlaySupport(OverlayCandidateList* surfaces) override {}
};

class SingleOverlayValidator : public OverlayCandidateValidator {
 public:
  SingleOverlayValidator() : expected_rects_(1, gfx::RectF(kOverlayRect)) {}

  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {
    strategies->push_back(base::MakeUnique<OverlayStrategySingleOnTop>(this));
    strategies->push_back(base::MakeUnique<OverlayStrategyUnderlay>(this));
  }

  bool AllowCALayerOverlays() override { return false; }
  bool AllowDCLayerOverlays() override { return false; }
  void CheckOverlaySupport(OverlayCandidateList* surfaces) override {
    // We may have 1 or 2 surfaces depending on whether this ran through the
    // full renderer and picked up the output surface, or not.
    ASSERT_LE(1U, surfaces->size());
    ASSERT_GE(2U, surfaces->size());

    OverlayCandidate& candidate = surfaces->back();
    EXPECT_TRUE(!candidate.use_output_surface_for_resource);
    for (const auto& r : expected_rects_) {
      const float kAbsoluteError = 0.01f;
      if (std::abs(r.x() - candidate.display_rect.x()) <= kAbsoluteError &&
          std::abs(r.y() - candidate.display_rect.y()) <= kAbsoluteError &&
          std::abs(r.width() - candidate.display_rect.width()) <=
              kAbsoluteError &&
          std::abs(r.height() - candidate.display_rect.height()) <=
              kAbsoluteError) {
        EXPECT_FLOAT_RECT_EQ(BoundingRect(kUVTopLeft, kUVBottomRight),
                             candidate.uv_rect);
        if (!candidate.clip_rect.IsEmpty()) {
          EXPECT_EQ(true, candidate.is_clipped);
          EXPECT_EQ(kOverlayClipRect, candidate.clip_rect);
        }
        candidate.overlay_handled = true;
        return;
      }
    }
    // We should find one rect in expected_rects_that matches candidate.
    EXPECT_TRUE(false);
  }

  void AddExpectedRect(const gfx::RectF& rect) {
    expected_rects_.push_back(rect);
  }

 private:
  std::vector<gfx::RectF> expected_rects_;
};

class CALayerValidator : public OverlayCandidateValidator {
 public:
  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {}
  bool AllowCALayerOverlays() override { return true; }
  bool AllowDCLayerOverlays() override { return false; }
  void CheckOverlaySupport(OverlayCandidateList* surfaces) override {}
};

class DCLayerValidator : public OverlayCandidateValidator {
 public:
  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {}
  bool AllowCALayerOverlays() override { return false; }
  bool AllowDCLayerOverlays() override { return true; }
  void CheckOverlaySupport(OverlayCandidateList* surfaces) override {}
};

class SingleOnTopOverlayValidator : public SingleOverlayValidator {
 public:
  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {
    strategies->push_back(base::MakeUnique<OverlayStrategySingleOnTop>(this));
  }
};

class UnderlayOverlayValidator : public SingleOverlayValidator {
 public:
  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {
    strategies->push_back(base::MakeUnique<OverlayStrategyUnderlay>(this));
  }
};

class UnderlayCastOverlayValidator : public SingleOverlayValidator {
 public:
  void GetStrategies(OverlayProcessor::StrategyList* strategies) override {
    strategies->push_back(base::MakeUnique<OverlayStrategyUnderlayCast>(this));
  }
};

class DefaultOverlayProcessor : public OverlayProcessor {
 public:
  explicit DefaultOverlayProcessor(OutputSurface* surface);
  size_t GetStrategyCount();
};

DefaultOverlayProcessor::DefaultOverlayProcessor(OutputSurface* surface)
    : OverlayProcessor(surface) {
}

size_t DefaultOverlayProcessor::GetStrategyCount() {
  return strategies_.size();
}

template <typename OverlayCandidateValidatorType>
class OverlayOutputSurface : public OutputSurface {
 public:
  explicit OverlayOutputSurface(
      scoped_refptr<TestContextProvider> context_provider)
      : OutputSurface(std::move(context_provider)) {
    is_displayed_as_overlay_plane_ = true;
  }

  // OutputSurface implementation.
  void BindToClient(OutputSurfaceClient* client) override {}
  void EnsureBackbuffer() override {}
  void DiscardBackbuffer() override {}
  void BindFramebuffer() override {
    bind_framebuffer_count_ += 1;
  }
  void SetDrawRectangle(const gfx::Rect& rect) override {}
  void Reshape(const gfx::Size& size,
               float device_scale_factor,
               const gfx::ColorSpace& color_space,
               bool has_alpha,
               bool use_stencil) override {}
  void SwapBuffers(OutputSurfaceFrame frame) override {}
  uint32_t GetFramebufferCopyTextureFormat() override {
    // TestContextProvider has no real framebuffer, just use RGB.
    return GL_RGB;
  }
  bool HasExternalStencilTest() const override { return false; }
  void ApplyExternalStencil() override {}
  OverlayCandidateValidatorType* GetOverlayCandidateValidator() const override {
    return overlay_candidate_validator_.get();
  }
  bool IsDisplayedAsOverlayPlane() const override {
    return is_displayed_as_overlay_plane_;
  }
  unsigned GetOverlayTextureId() const override { return 10000; }
  bool SurfaceIsSuspendForRecycle() const override { return false; }

  void set_is_displayed_as_overlay_plane(bool value) {
    is_displayed_as_overlay_plane_ = value;
  }

  void SetOverlayCandidateValidator(OverlayCandidateValidatorType* validator) {
    overlay_candidate_validator_.reset(validator);
  }

  unsigned bind_framebuffer_count() const { return bind_framebuffer_count_; }

 private:
  std::unique_ptr<OverlayCandidateValidatorType> overlay_candidate_validator_;
  bool is_displayed_as_overlay_plane_;
  unsigned bind_framebuffer_count_ = 0;
};

std::unique_ptr<RenderPass> CreateRenderPass() {
  int render_pass_id = 1;
  gfx::Rect output_rect(0, 0, 256, 256);

  std::unique_ptr<RenderPass> pass = RenderPass::Create();
  pass->SetNew(render_pass_id, output_rect, output_rect, gfx::Transform());

  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  return pass;
}

std::unique_ptr<RenderPass> CreateRenderPassWithTransform(
    const gfx::Transform& transform) {
  int render_pass_id = 1;
  gfx::Rect output_rect(0, 0, 256, 256);

  std::unique_ptr<RenderPass> pass = RenderPass::Create();
  pass->SetNew(render_pass_id, output_rect, output_rect, gfx::Transform());

  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  shared_state->quad_to_target_transform = transform;
  return pass;
}

viz::ResourceId CreateResource(ResourceProvider* resource_provider,
                               const gfx::Size& size,
                               bool is_overlay_candidate) {
  viz::TextureMailbox mailbox =
      viz::TextureMailbox(gpu::Mailbox::Generate(), gpu::SyncToken(),
                          GL_TEXTURE_2D, size, is_overlay_candidate, false);
  std::unique_ptr<SingleReleaseCallbackImpl> release_callback =
      SingleReleaseCallbackImpl::Create(base::Bind(&MailboxReleased));

  return resource_provider->CreateResourceFromTextureMailbox(
      mailbox, std::move(release_callback));
}

SolidColorDrawQuad* CreateSolidColorQuadAt(
    const SharedQuadState* shared_quad_state,
    SkColor color,
    RenderPass* render_pass,
    const gfx::Rect& rect) {
  SolidColorDrawQuad* quad =
      render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
  quad->SetNew(shared_quad_state, rect, rect, color, false);
  return quad;
}

TextureDrawQuad* CreateCandidateQuadAt(ResourceProvider* resource_provider,
                                       const SharedQuadState* shared_quad_state,
                                       RenderPass* render_pass,
                                       const gfx::Rect& rect) {
  bool premultiplied_alpha = false;
  bool flipped = false;
  bool nearest_neighbor = false;
  float vertex_opacity[4] = {1.0f, 1.0f, 1.0f, 1.0f};
  gfx::Size resource_size_in_pixels = rect.size();
  bool is_overlay_candidate = true;
  viz::ResourceId resource_id = CreateResource(
      resource_provider, resource_size_in_pixels, is_overlay_candidate);

  TextureDrawQuad* overlay_quad =
      render_pass->CreateAndAppendDrawQuad<TextureDrawQuad>();
  overlay_quad->SetNew(shared_quad_state, rect, rect, rect, resource_id,
                       premultiplied_alpha, kUVTopLeft, kUVBottomRight,
                       SK_ColorTRANSPARENT, vertex_opacity, flipped,
                       nearest_neighbor, false);
  overlay_quad->set_resource_size_in_pixels(resource_size_in_pixels);

  return overlay_quad;
}

StreamVideoDrawQuad* CreateCandidateVideoQuadAt(
    ResourceProvider* resource_provider,
    const SharedQuadState* shared_quad_state,
    RenderPass* render_pass,
    const gfx::Rect& rect,
    const gfx::Transform& transform) {
  gfx::Size resource_size_in_pixels = rect.size();
  bool is_overlay_candidate = true;
  viz::ResourceId resource_id = CreateResource(
      resource_provider, resource_size_in_pixels, is_overlay_candidate);

  StreamVideoDrawQuad* overlay_quad =
      render_pass->CreateAndAppendDrawQuad<StreamVideoDrawQuad>();
  overlay_quad->SetNew(shared_quad_state, rect, rect, rect, resource_id,
                       resource_size_in_pixels, transform);

  return overlay_quad;
}

TextureDrawQuad* CreateFullscreenCandidateQuad(
    ResourceProvider* resource_provider,
    const SharedQuadState* shared_quad_state,
    RenderPass* render_pass) {
  return CreateCandidateQuadAt(resource_provider, shared_quad_state,
                               render_pass, render_pass->output_rect);
}

StreamVideoDrawQuad* CreateFullscreenCandidateVideoQuad(
    ResourceProvider* resource_provider,
    const SharedQuadState* shared_quad_state,
    RenderPass* render_pass,
    const gfx::Transform& transform) {
  return CreateCandidateVideoQuadAt(resource_provider, shared_quad_state,
                                    render_pass, render_pass->output_rect,
                                    transform);
}

YUVVideoDrawQuad* CreateFullscreenCandidateYUVVideoQuad(
    ResourceProvider* resource_provider,
    const SharedQuadState* shared_quad_state,
    RenderPass* render_pass) {
  gfx::RectF tex_coord_rect(0, 0, 1, 1);
  gfx::Rect rect = render_pass->output_rect;
  gfx::Size resource_size_in_pixels = rect.size();
  bool is_overlay_candidate = true;
  viz::ResourceId resource_id = CreateResource(
      resource_provider, resource_size_in_pixels, is_overlay_candidate);

  YUVVideoDrawQuad* overlay_quad =
      render_pass->CreateAndAppendDrawQuad<YUVVideoDrawQuad>();
  overlay_quad->SetNew(shared_quad_state, rect, rect, rect, tex_coord_rect,
                       tex_coord_rect, resource_size_in_pixels,
                       resource_size_in_pixels, resource_id, resource_id,
                       resource_id, resource_id, YUVVideoDrawQuad::REC_601,
                       gfx::ColorSpace(), 0, 1.0, 8);

  return overlay_quad;
}

void CreateOpaqueQuadAt(ResourceProvider* resource_provider,
                        const SharedQuadState* shared_quad_state,
                        RenderPass* render_pass,
                        const gfx::Rect& rect) {
  SolidColorDrawQuad* color_quad =
      render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
  color_quad->SetNew(shared_quad_state, rect, rect, SK_ColorBLACK, false);
}

void CreateOpaqueQuadAt(ResourceProvider* resource_provider,
                        const SharedQuadState* shared_quad_state,
                        RenderPass* render_pass,
                        const gfx::Rect& rect,
                        SkColor color) {
  DCHECK_EQ(255u, SkColorGetA(color));
  SolidColorDrawQuad* color_quad =
      render_pass->CreateAndAppendDrawQuad<SolidColorDrawQuad>();
  color_quad->SetNew(shared_quad_state, rect, rect, color, false);
}

void CreateFullscreenOpaqueQuad(ResourceProvider* resource_provider,
                                const SharedQuadState* shared_quad_state,
                                RenderPass* render_pass) {
  CreateOpaqueQuadAt(resource_provider, shared_quad_state, render_pass,
                     render_pass->output_rect);
}

static void CompareRenderPassLists(const RenderPassList& expected_list,
                                   const RenderPassList& actual_list) {
  EXPECT_EQ(expected_list.size(), actual_list.size());
  for (size_t i = 0; i < actual_list.size(); ++i) {
    RenderPass* expected = expected_list[i].get();
    RenderPass* actual = actual_list[i].get();

    EXPECT_EQ(expected->id, actual->id);
    EXPECT_EQ(expected->output_rect, actual->output_rect);
    EXPECT_EQ(expected->transform_to_root_target,
              actual->transform_to_root_target);
    EXPECT_EQ(expected->damage_rect, actual->damage_rect);
    EXPECT_EQ(expected->has_transparent_background,
              actual->has_transparent_background);

    EXPECT_EQ(expected->shared_quad_state_list.size(),
              actual->shared_quad_state_list.size());
    EXPECT_EQ(expected->quad_list.size(), actual->quad_list.size());

    for (auto exp_iter = expected->quad_list.cbegin(),
              act_iter = actual->quad_list.cbegin();
         exp_iter != expected->quad_list.cend();
         ++exp_iter, ++act_iter) {
      EXPECT_EQ(exp_iter->rect.ToString(), act_iter->rect.ToString());
      EXPECT_EQ(exp_iter->shared_quad_state->quad_layer_rect.ToString(),
                act_iter->shared_quad_state->quad_layer_rect.ToString());
    }
  }
}

template <typename OverlayCandidateValidatorType>
class OverlayTest : public testing::Test {
  using OutputSurfaceType = OverlayOutputSurface<OverlayCandidateValidatorType>;

 protected:
  void SetUp() override {
    provider_ = TestContextProvider::Create();
    provider_->BindToCurrentThread();
    output_surface_ = base::MakeUnique<OutputSurfaceType>(provider_);
    output_surface_->BindToClient(&client_);
    output_surface_->SetOverlayCandidateValidator(
        new OverlayCandidateValidatorType);

    shared_bitmap_manager_.reset(new TestSharedBitmapManager());
    resource_provider_ = FakeResourceProvider::Create(
        provider_.get(), shared_bitmap_manager_.get());

    overlay_processor_.reset(new OverlayProcessor(output_surface_.get()));
    overlay_processor_->Initialize();
  }

  scoped_refptr<TestContextProvider> provider_;
  std::unique_ptr<OutputSurfaceType> output_surface_;
  FakeOutputSurfaceClient client_;
  std::unique_ptr<viz::SharedBitmapManager> shared_bitmap_manager_;
  std::unique_ptr<ResourceProvider> resource_provider_;
  std::unique_ptr<OverlayProcessor> overlay_processor_;
  gfx::Rect damage_rect_;
  std::vector<gfx::Rect> content_bounds_;
};

using FullscreenOverlayTest = OverlayTest<FullscreenOverlayValidator>;
using SingleOverlayOnTopTest = OverlayTest<SingleOnTopOverlayValidator>;
using UnderlayTest = OverlayTest<UnderlayOverlayValidator>;
using UnderlayCastTest = OverlayTest<UnderlayCastOverlayValidator>;
using CALayerOverlayTest = OverlayTest<CALayerValidator>;
using DCLayerOverlayTest = OverlayTest<DCLayerValidator>;

TEST(OverlayTest, NoOverlaysByDefault) {
  scoped_refptr<TestContextProvider> provider = TestContextProvider::Create();
  OverlayOutputSurface<OverlayCandidateValidator> output_surface(provider);
  EXPECT_EQ(NULL, output_surface.GetOverlayCandidateValidator());

  output_surface.SetOverlayCandidateValidator(new SingleOverlayValidator);
  EXPECT_TRUE(output_surface.GetOverlayCandidateValidator() != NULL);
}

TEST(OverlayTest, OverlaysProcessorHasStrategy) {
  scoped_refptr<TestContextProvider> provider = TestContextProvider::Create();
  provider->BindToCurrentThread();
  OverlayOutputSurface<OverlayCandidateValidator> output_surface(provider);
  FakeOutputSurfaceClient client;
  output_surface.BindToClient(&client);
  output_surface.SetOverlayCandidateValidator(new SingleOverlayValidator);

  std::unique_ptr<viz::SharedBitmapManager> shared_bitmap_manager(
      new TestSharedBitmapManager());
  std::unique_ptr<ResourceProvider> resource_provider =
      FakeResourceProvider::Create(provider.get(), shared_bitmap_manager.get());

  std::unique_ptr<DefaultOverlayProcessor> overlay_processor(
      new DefaultOverlayProcessor(&output_surface));
  overlay_processor->Initialize();
  EXPECT_GE(2U, overlay_processor->GetStrategyCount());
}

TEST_F(FullscreenOverlayTest, SuccessfulOverlay) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  gfx::Rect output_rect = pass->output_rect;
  TextureDrawQuad* original_quad = CreateFullscreenCandidateQuad(
      resource_provider_.get(), pass->shared_quad_state_list.back(),
      pass.get());
  unsigned original_resource_id = original_quad->resource_id();

  // Add something behind it.
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());

  // Check that all the quads are gone.
  EXPECT_EQ(0U, main_pass->quad_list.size());
  // Check that we have only one overlay.
  EXPECT_EQ(1U, candidate_list.size());
  // Check that the right resource id got extracted.
  EXPECT_EQ(original_resource_id, candidate_list.front().resource_id);
  gfx::Rect overlay_damage_rect =
      overlay_processor_->GetAndResetOverlayDamage();
  EXPECT_EQ(output_rect, overlay_damage_rect);
}

TEST_F(FullscreenOverlayTest, AlphaFail) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  TextureDrawQuad* original_quad = CreateFullscreenCandidateQuad(
      resource_provider_.get(), pass->shared_quad_state_list.back(),
      pass.get());
  original_quad->opaque_rect = gfx::Rect(0, 0, 0, 0);

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  // Check that all the quads are gone.
  EXPECT_EQ(1U, main_pass->quad_list.size());
  // Check that we have only one overlay.
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(FullscreenOverlayTest, ResourceSizeInPixelsFail) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  TextureDrawQuad* original_quad = CreateFullscreenCandidateQuad(
      resource_provider_.get(), pass->shared_quad_state_list.back(),
      pass.get());
  original_quad->set_resource_size_in_pixels(gfx::Size(64, 64));

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(0U, candidate_list.size());

  // Check that the quad is not gone.
  EXPECT_EQ(1U, main_pass->quad_list.size());
}

TEST_F(FullscreenOverlayTest, OnTopFail) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  // Add something in front of it.
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayTopLeftRect);

  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));

  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(0U, candidate_list.size());

  // Check that the 2 quads are not gone.
  EXPECT_EQ(2U, main_pass->quad_list.size());
}

TEST_F(FullscreenOverlayTest, NotCoveringFullscreenFail) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  gfx::Rect inset_rect = pass->output_rect;
  inset_rect.Inset(0, 1, 0, 1);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        inset_rect);

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(0U, candidate_list.size());

  // Check that the quad is not gone.
  EXPECT_EQ(1U, main_pass->quad_list.size());
}

TEST_F(FullscreenOverlayTest, RemoveFullscreenQuadFromQuadList) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  // Add something in front of it that is fully transparent.
  pass->shared_quad_state_list.back()->opacity = 0.0f;
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayTopLeftRect);

  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());

  // Check that the fullscreen quad is gone.
  for (const DrawQuad* quad : main_pass->quad_list) {
    EXPECT_NE(main_pass->output_rect, quad->rect);
  }
}

TEST_F(SingleOverlayOnTopTest, SuccessfulOverlay) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  TextureDrawQuad* original_quad =
      CreateFullscreenCandidateQuad(resource_provider_.get(),
                                    pass->shared_quad_state_list.back(),
                                    pass.get());
  unsigned original_resource_id = original_quad->resource_id();

  // Add something behind it.
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());

  // Check that the quad is gone.
  EXPECT_EQ(2U, main_pass->quad_list.size());
  const QuadList& quad_list = main_pass->quad_list;
  for (QuadList::ConstBackToFrontIterator it = quad_list.BackToFrontBegin();
       it != quad_list.BackToFrontEnd();
       ++it) {
    EXPECT_NE(DrawQuad::TEXTURE_CONTENT, it->material);
  }

  // Check that the right resource id got extracted.
  EXPECT_EQ(original_resource_id, candidate_list.back().resource_id);
}

TEST_F(SingleOverlayOnTopTest, PrioritizeBiggerOne) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  // Add a small quad.
  const auto kSmallCandidateRect = gfx::Rect(0, 0, 16, 16);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kSmallCandidateRect);
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kSmallCandidateRect));

  // Add a bigger quad below the previous one, but not occluded.
  const auto kBigCandidateRect = gfx::Rect(20, 20, 32, 32);
  TextureDrawQuad* quad_big = CreateCandidateQuadAt(
      resource_provider_.get(), pass->shared_quad_state_list.back(), pass.get(),
      kBigCandidateRect);
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kBigCandidateRect));

  unsigned resource_big = quad_big->resource_id();

  // Add something behind it.
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());

  // Check that one quad is gone.
  EXPECT_EQ(2U, main_pass->quad_list.size());
  // Check that we have only one overlay.
  EXPECT_EQ(1U, candidate_list.size());
  // Check that the right resource id (bigger quad) got extracted.
  EXPECT_EQ(resource_big, candidate_list.front().resource_id);
}

TEST_F(SingleOverlayOnTopTest, DamageRect) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  damage_rect_ = kOverlayRect;

  // Add something behind it.
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;

  // Primary plane.
  OverlayCandidate output_surface_plane;
  output_surface_plane.display_rect = gfx::RectF(kOverlayRect);
  output_surface_plane.quad_rect_in_target_space = kOverlayRect;
  output_surface_plane.use_output_surface_for_resource = true;
  output_surface_plane.overlay_handled = true;
  candidate_list.push_back(output_surface_plane);

  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_TRUE(damage_rect_.IsEmpty());
}

TEST_F(SingleOverlayOnTopTest, NoCandidates) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  RenderPassList original_pass_list;
  RenderPass::CopyAll(pass_list, &original_pass_list);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
  // There should be nothing new here.
  CompareRenderPassLists(pass_list, original_pass_list);
}

TEST_F(SingleOverlayOnTopTest, OccludedCandidates) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  RenderPassList original_pass_list;
  RenderPass::CopyAll(pass_list, &original_pass_list);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
  // There should be nothing new here.
  CompareRenderPassLists(pass_list, original_pass_list);
}

// Test with multiple render passes.
TEST_F(SingleOverlayOnTopTest, MultipleRenderPasses) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  // Add something behind it.
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  // Check for potential candidates.
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, AcceptBlending) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  TextureDrawQuad* quad =
      CreateFullscreenCandidateQuad(resource_provider_.get(),
                                    pass->shared_quad_state_list.back(),
                                    pass.get());
  quad->needs_blending = true;
  quad->opaque_rect = gfx::Rect(0, 0, 0, 0);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  damage_rect_ = quad->rect;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
  EXPECT_FALSE(damage_rect_.IsEmpty());
  gfx::Rect overlay_damage_rect =
      overlay_processor_->GetAndResetOverlayDamage();
  EXPECT_FALSE(overlay_damage_rect.IsEmpty());
}

TEST_F(SingleOverlayOnTopTest, RejectBackgroundColor) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  TextureDrawQuad* quad =
      CreateFullscreenCandidateQuad(resource_provider_.get(),
                                    pass->shared_quad_state_list.back(),
                                    pass.get());
  quad->background_color = SK_ColorBLACK;

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, RejectBlendMode) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()->blend_mode = SkBlendMode::kScreen;

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, RejectOpacity) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()->opacity = 0.5f;

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, RejectNonAxisAlignedTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutXAxis(45.f);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, AllowClipped) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()->is_clipped = true;
  pass->shared_quad_state_list.back()->clip_rect = kOverlayClipRect;

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(UnderlayTest, AllowVerticalFlip) {
  gfx::Rect rect = kOverlayRect;
  rect.set_width(rect.width() / 2);
  rect.Offset(0, -rect.height());
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()->quad_to_target_transform.Scale(2.0f,
                                                                      -1.0f);
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(gfx::OVERLAY_TRANSFORM_FLIP_VERTICAL,
            candidate_list.back().transform);
}

TEST_F(UnderlayTest, AllowHorizontalFlip) {
  gfx::Rect rect = kOverlayRect;
  rect.set_height(rect.height() / 2);
  rect.Offset(-rect.width(), 0);
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()->quad_to_target_transform.Scale(-1.0f,
                                                                      2.0f);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(gfx::OVERLAY_TRANSFORM_FLIP_HORIZONTAL,
            candidate_list.back().transform);
}

TEST_F(SingleOverlayOnTopTest, AllowPositiveScaleTransform) {
  gfx::Rect rect = kOverlayRect;
  rect.set_width(rect.width() / 2);
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()->quad_to_target_transform.Scale(2.0f,
                                                                      1.0f);
  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, AcceptMirrorYTransform) {
  gfx::Rect rect = kOverlayRect;
  rect.Offset(0, -rect.height());
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()->quad_to_target_transform.Scale(1.f,
                                                                      -1.f);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
}

TEST_F(UnderlayTest, Allow90DegreeRotation) {
  gfx::Rect rect = kOverlayRect;
  rect.Offset(0, -rect.height());
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutZAxis(90.f);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(gfx::OVERLAY_TRANSFORM_ROTATE_90, candidate_list.back().transform);
}

TEST_F(UnderlayTest, Allow180DegreeRotation) {
  gfx::Rect rect = kOverlayRect;
  rect.Offset(-rect.width(), -rect.height());
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutZAxis(180.f);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(gfx::OVERLAY_TRANSFORM_ROTATE_180, candidate_list.back().transform);
}

TEST_F(UnderlayTest, Allow270DegreeRotation) {
  gfx::Rect rect = kOverlayRect;
  rect.Offset(-rect.width(), 0);
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(), rect);
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutZAxis(270.f);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(gfx::OVERLAY_TRANSFORM_ROTATE_270, candidate_list.back().transform);
}

TEST_F(SingleOverlayOnTopTest, AllowNotTopIfNotOccluded) {
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayTopLeftRect);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(),
                        pass.get(),
                        kOverlayBottomRightRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, AllowTransparentOnTop) {
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 0.f;
  CreateSolidColorQuadAt(shared_state, SK_ColorBLACK, pass.get(),
                         kOverlayBottomRightRect);
  shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  CreateCandidateQuadAt(resource_provider_.get(), shared_state, pass.get(),
                        kOverlayBottomRightRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, AllowTransparentColorOnTop) {
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateSolidColorQuadAt(pass->shared_quad_state_list.back(),
                         SK_ColorTRANSPARENT, pass.get(),
                         kOverlayBottomRightRect);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayBottomRightRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, RejectOpaqueColorOnTop) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 0.5f;
  CreateSolidColorQuadAt(shared_state, SK_ColorBLACK, pass.get(),
                         kOverlayBottomRightRect);
  shared_state = pass->CreateAndAppendSharedQuadState();
  shared_state->opacity = 1.f;
  CreateCandidateQuadAt(resource_provider_.get(), shared_state, pass.get(),
                        kOverlayBottomRightRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, RejectTransparentColorOnTopWithoutBlending) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
  CreateSolidColorQuadAt(shared_state, SK_ColorTRANSPARENT, pass.get(),
                         kOverlayBottomRightRect)->opaque_rect =
      kOverlayBottomRightRect;
  CreateCandidateQuadAt(resource_provider_.get(), shared_state, pass.get(),
                        kOverlayBottomRightRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, RejectVideoSwapTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateVideoQuad(resource_provider_.get(),
                                     pass->shared_quad_state_list.back(),
                                     pass.get(), kSwapTransform);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, candidate_list.size());
}

TEST_F(UnderlayTest, AllowVideoXMirrorTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateVideoQuad(resource_provider_.get(),
                                     pass->shared_quad_state_list.back(),
                                     pass.get(), kXMirrorTransform);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(UnderlayTest, AllowVideoBothMirrorTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateVideoQuad(resource_provider_.get(),
                                     pass->shared_quad_state_list.back(),
                                     pass.get(), kBothMirrorTransform);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(UnderlayTest, AllowVideoNormalTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateVideoQuad(resource_provider_.get(),
                                     pass->shared_quad_state_list.back(),
                                     pass.get(), kNormalTransform);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(SingleOverlayOnTopTest, AllowVideoYMirrorTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateVideoQuad(resource_provider_.get(),
                                     pass->shared_quad_state_list.back(),
                                     pass.get(), kYMirrorTransform);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(1U, candidate_list.size());
}

TEST_F(UnderlayTest, OverlayLayerUnderMainLayer) {
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayBottomRightRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(-1, candidate_list[0].plane_z_order);
  EXPECT_EQ(2U, main_pass->quad_list.size());
  // The overlay quad should have changed to a SOLID_COLOR quad.
  EXPECT_EQ(main_pass->quad_list.back()->material, DrawQuad::SOLID_COLOR);
}

TEST_F(UnderlayTest, AllowOnTop) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->CreateAndAppendSharedQuadState()->opacity = 0.5f;
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  RenderPass* main_pass = pass.get();
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  ASSERT_EQ(1U, candidate_list.size());
  EXPECT_EQ(-1, candidate_list[0].plane_z_order);
  // The overlay quad should have changed to a SOLID_COLOR quad.
  EXPECT_EQ(main_pass->quad_list.front()->material, DrawQuad::SOLID_COLOR);
}

// The first time an underlay is scheduled its damage must not be subtracted.
TEST_F(UnderlayTest, InitialUnderlayDamageNotSubtracted) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  damage_rect_ = kOverlayRect;

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(kOverlayRect, damage_rect_);
}

// An identical underlay for two frames in a row means the damage can be
// subtracted the second time.
TEST_F(UnderlayTest, DamageSubtractedForConsecutiveIdenticalUnderlays) {
  for (int i = 0; i < 2; ++i) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    CreateFullscreenCandidateQuad(resource_provider_.get(),
                                  pass->shared_quad_state_list.back(),
                                  pass.get());

    damage_rect_ = kOverlayRect;

    // Add something behind it.
    CreateFullscreenOpaqueQuad(resource_provider_.get(),
                               pass->shared_quad_state_list.back(), pass.get());

    OverlayCandidateList candidate_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &candidate_list, nullptr, nullptr,
        &damage_rect_, &content_bounds_);
  }

  // The second time the same overlay rect is scheduled it will be subtracted
  // from the damage rect.
  EXPECT_TRUE(damage_rect_.IsEmpty());
}

// Underlay damage can only be subtracted if the previous frame's underlay
// was the same rect.
TEST_F(UnderlayTest, DamageNotSubtractedForNonIdenticalConsecutiveUnderlays) {
  gfx::Rect overlay_rects[] = {kOverlayBottomRightRect, kOverlayRect};
  for (int i = 0; i < 2; ++i) {
    output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
        gfx::RectF(overlay_rects[i]));

    std::unique_ptr<RenderPass> pass = CreateRenderPass();

    CreateCandidateQuadAt(resource_provider_.get(),
                          pass->shared_quad_state_list.back(), pass.get(),
                          overlay_rects[i]);

    damage_rect_ = overlay_rects[i];

    OverlayCandidateList candidate_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &candidate_list, nullptr, nullptr,
        &damage_rect_, &content_bounds_);

    EXPECT_EQ(overlay_rects[i], damage_rect_);
  }
}

// Underlay damage can only be subtracted if the previous frame's underlay
// exists.
TEST_F(UnderlayTest, DamageNotSubtractedForNonConsecutiveIdenticalUnderlays) {
  bool has_fullscreen_candidate[] = {true, false, true};

  for (int i = 0; i < 3; ++i) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();

    if (has_fullscreen_candidate[i]) {
      CreateFullscreenCandidateQuad(resource_provider_.get(),
                                    pass->shared_quad_state_list.back(),
                                    pass.get());
    }

    damage_rect_ = kOverlayRect;

    // Add something behind it.
    CreateFullscreenOpaqueQuad(resource_provider_.get(),
                               pass->shared_quad_state_list.back(), pass.get());

    OverlayCandidateList candidate_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &candidate_list, nullptr, nullptr,
        &damage_rect_, &content_bounds_);
  }

  EXPECT_EQ(kOverlayRect, damage_rect_);
}

TEST_F(UnderlayTest, DamageNotSubtractedWhenQuadsAboveOverlap) {
  for (int i = 0; i < 2; ++i) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    // Add an overlapping quad above the candidate.
    CreateFullscreenOpaqueQuad(resource_provider_.get(),
                               pass->shared_quad_state_list.back(), pass.get());
    CreateFullscreenCandidateQuad(resource_provider_.get(),
                                  pass->shared_quad_state_list.back(),
                                  pass.get());

    damage_rect_ = kOverlayRect;

    OverlayCandidateList candidate_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &candidate_list, nullptr, nullptr,
        &damage_rect_, &content_bounds_);
  }

  EXPECT_EQ(kOverlayRect, damage_rect_);
}

TEST_F(UnderlayTest, DamageSubtractedWhenQuadsAboveDontOverlap) {
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  for (int i = 0; i < 2; ++i) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    // Add a non-overlapping quad above the candidate.
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       kOverlayTopLeftRect);
    CreateCandidateQuadAt(resource_provider_.get(),
                          pass->shared_quad_state_list.back(), pass.get(),
                          kOverlayBottomRightRect);

    damage_rect_ = kOverlayBottomRightRect;

    OverlayCandidateList candidate_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &candidate_list, nullptr, nullptr,
        &damage_rect_, &content_bounds_);
  }

  EXPECT_TRUE(damage_rect_.IsEmpty());
}

TEST_F(UnderlayCastTest, NoOverlayContentBounds) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayTopLeftRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, content_bounds_.size());
}

TEST_F(UnderlayCastTest, FullScreenOverlayContentBounds) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, content_bounds_.size());
  EXPECT_TRUE(content_bounds_[0].IsEmpty());
}

TEST_F(UnderlayCastTest, BlackOutsideOverlayContentBounds) {
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  const gfx::Rect kLeftSide(0, 0, 128, 256);
  const gfx::Rect kTopRight(128, 0, 128, 128);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayBottomRightRect);
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(), kLeftSide,
                     SK_ColorBLACK);
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(), kTopRight,
                     SK_ColorBLACK);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, content_bounds_.size());
  EXPECT_TRUE(content_bounds_[0].IsEmpty());
}

TEST_F(UnderlayCastTest, OverlayOccludedContentBounds) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayTopLeftRect);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, content_bounds_.size());
  EXPECT_EQ(kOverlayTopLeftRect, content_bounds_[0]);
}

TEST_F(UnderlayCastTest, OverlayOccludedUnionContentBounds) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayTopLeftRect);
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     kOverlayBottomRightRect);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayRect);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, content_bounds_.size());
  EXPECT_EQ(kOverlayRect, content_bounds_[0]);
}

TEST_F(UnderlayCastTest, RoundOverlayContentBounds) {
  // Check rounding behaviour on overlay quads.  Be conservative (content
  // potentially visible on boundary).
  const gfx::Rect overlay_rect(1, 1, 8, 8);
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(1.5f, 1.5f, 8, 8));

  gfx::Transform transform;
  transform.Translate(0.5f, 0.5f);

  std::unique_ptr<RenderPass> pass = CreateRenderPassWithTransform(transform);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        overlay_rect);
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     gfx::Rect(0, 0, 10, 10), SK_ColorWHITE);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, content_bounds_.size());
  EXPECT_EQ(gfx::Rect(0, 0, 11, 11), content_bounds_[0]);
}

TEST_F(UnderlayCastTest, RoundContentBounds) {
  // Check rounding behaviour on content quads (bounds should be enclosing
  // rect).
  gfx::Rect overlay_rect = kOverlayRect;
  overlay_rect.Inset(0, 0, 1, 1);
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(0.5f, 0.5f, 255, 255));

  gfx::Transform transform;
  transform.Translate(0.5f, 0.5f);

  std::unique_ptr<RenderPass> pass = CreateRenderPassWithTransform(transform);
  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        overlay_rect);
  CreateOpaqueQuadAt(resource_provider_.get(),
                     pass->shared_quad_state_list.back(), pass.get(),
                     gfx::Rect(0, 0, 255, 255), SK_ColorWHITE);

  OverlayCandidateList candidate_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &candidate_list, nullptr, nullptr,
      &damage_rect_, &content_bounds_);

  EXPECT_EQ(1U, content_bounds_.size());
  EXPECT_EQ(kOverlayRect, content_bounds_[0]);
}

OverlayCandidateList BackbufferOverlayList(const RenderPass* root_render_pass) {
  OverlayCandidateList list;
  OverlayCandidate output_surface_plane;
  output_surface_plane.display_rect = gfx::RectF(root_render_pass->output_rect);
  output_surface_plane.quad_rect_in_target_space =
      root_render_pass->output_rect;
  output_surface_plane.use_output_surface_for_resource = true;
  output_surface_plane.overlay_handled = true;
  list.push_back(output_surface_plane);
  return list;
}

TEST_F(CALayerOverlayTest, AllowNonAxisAlignedTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutZAxis(45.f);

  gfx::Rect damage_rect;
  CALayerOverlayList ca_layer_list;
  OverlayCandidateList overlay_list(BackbufferOverlayList(pass.get()));
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &overlay_list, &ca_layer_list, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(gfx::Rect(), damage_rect);
  EXPECT_EQ(0U, overlay_list.size());
  EXPECT_EQ(1U, ca_layer_list.size());
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
}

TEST_F(CALayerOverlayTest, ThreeDTransform) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutXAxis(45.f);

  gfx::Rect damage_rect;
  CALayerOverlayList ca_layer_list;
  OverlayCandidateList overlay_list(BackbufferOverlayList(pass.get()));
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &overlay_list, &ca_layer_list, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(0U, overlay_list.size());
  EXPECT_EQ(1U, ca_layer_list.size());
  gfx::Transform expected_transform;
  expected_transform.RotateAboutXAxis(45.f);
  gfx::Transform actual_transform(ca_layer_list.back().shared_state->transform);
  EXPECT_EQ(expected_transform.ToString(), actual_transform.ToString());
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
}

TEST_F(CALayerOverlayTest, AllowContainingClip) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()->is_clipped = true;
  pass->shared_quad_state_list.back()->clip_rect = kOverlayRect;

  gfx::Rect damage_rect;
  CALayerOverlayList ca_layer_list;
  OverlayCandidateList overlay_list(BackbufferOverlayList(pass.get()));
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &overlay_list, &ca_layer_list, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(gfx::Rect(), damage_rect);
  EXPECT_EQ(0U, overlay_list.size());
  EXPECT_EQ(1U, ca_layer_list.size());
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
}

TEST_F(CALayerOverlayTest, NontrivialClip) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()->is_clipped = true;
  pass->shared_quad_state_list.back()->clip_rect = gfx::Rect(64, 64, 128, 128);

  gfx::Rect damage_rect;
  CALayerOverlayList ca_layer_list;
  OverlayCandidateList overlay_list(BackbufferOverlayList(pass.get()));
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &overlay_list, &ca_layer_list, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(gfx::Rect(), damage_rect);
  EXPECT_EQ(0U, overlay_list.size());
  EXPECT_EQ(1U, ca_layer_list.size());
  EXPECT_TRUE(ca_layer_list.back().shared_state->is_clipped);
  EXPECT_EQ(gfx::RectF(64, 64, 128, 128),
            ca_layer_list.back().shared_state->clip_rect);
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
}

TEST_F(CALayerOverlayTest, SkipTransparent) {
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  pass->shared_quad_state_list.back()->opacity = 0;

  gfx::Rect damage_rect;
  CALayerOverlayList ca_layer_list;
  OverlayCandidateList overlay_list(BackbufferOverlayList(pass.get()));
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &overlay_list, &ca_layer_list, nullptr,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(gfx::Rect(), damage_rect);
  EXPECT_EQ(0U, overlay_list.size());
  EXPECT_EQ(0U, ca_layer_list.size());
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
}

TEST_F(DCLayerOverlayTest, AllowNonAxisAlignedTransform) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kDirectCompositionComplexOverlays);
  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  CreateFullscreenCandidateYUVVideoQuad(resource_provider_.get(),
                                        pass->shared_quad_state_list.back(),
                                        pass.get());
  pass->shared_quad_state_list.back()
      ->quad_to_target_transform.RotateAboutZAxis(45.f);

  gfx::Rect damage_rect;
  DCLayerOverlayList dc_layer_list;
  OverlayCandidateList overlay_list;
  OverlayProcessor::FilterOperationsMap render_pass_filters;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters;
  damage_rect_ = gfx::Rect(1, 1, 10, 10);
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));
  overlay_processor_->ProcessForOverlays(
      resource_provider_.get(), &pass_list, render_pass_filters,
      render_pass_background_filters, &overlay_list, nullptr, &dc_layer_list,
      &damage_rect_, &content_bounds_);
  EXPECT_EQ(gfx::Rect(), damage_rect);
  EXPECT_EQ(0U, overlay_list.size());
  EXPECT_EQ(1U, dc_layer_list.size());
  EXPECT_EQ(1, dc_layer_list.back().shared_state->z_order);
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
  EXPECT_EQ(gfx::Rect(1, 1, 10, 10), damage_rect_);
}

TEST_F(DCLayerOverlayTest, Occluded) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(features::kDirectCompositionUnderlays);
  {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 2, 100, 100), SK_ColorWHITE);
    CreateFullscreenCandidateYUVVideoQuad(resource_provider_.get(),
                                          pass->shared_quad_state_list.back(),
                                          pass.get());

    gfx::Rect damage_rect;
    DCLayerOverlayList dc_layer_list;
    OverlayCandidateList overlay_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &overlay_list, nullptr, &dc_layer_list,
        &damage_rect_, &content_bounds_);
    EXPECT_EQ(gfx::Rect(), damage_rect);
    EXPECT_EQ(0U, overlay_list.size());
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
    EXPECT_EQ(-1, dc_layer_list.back().shared_state->z_order);
    // Entire underlay rect must be redrawn.
    EXPECT_EQ(gfx::Rect(0, 0, 256, 256), damage_rect_);
  }
  {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(2, 2, 100, 100), SK_ColorWHITE);
    CreateFullscreenCandidateYUVVideoQuad(resource_provider_.get(),
                                          pass->shared_quad_state_list.back(),
                                          pass.get());

    gfx::Rect damage_rect;
    DCLayerOverlayList dc_layer_list;
    OverlayCandidateList overlay_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &overlay_list, nullptr, &dc_layer_list,
        &damage_rect_, &content_bounds_);
    EXPECT_EQ(gfx::Rect(), damage_rect);
    EXPECT_EQ(0U, overlay_list.size());
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
    EXPECT_EQ(-1, dc_layer_list.back().shared_state->z_order);
    // The underlay rectangle is the same, so the damage is contained within
    // the combined occluding rects for this and the last frame.
    EXPECT_EQ(gfx::Rect(1, 2, 10, 9), damage_rect_);
  }
}

TEST_F(DCLayerOverlayTest, DamageRect) {
  for (int i = 0; i < 2; i++) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    CreateFullscreenCandidateYUVVideoQuad(resource_provider_.get(),
                                          pass->shared_quad_state_list.back(),
                                          pass.get());

    gfx::Rect damage_rect;
    DCLayerOverlayList dc_layer_list;
    OverlayCandidateList overlay_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &overlay_list, nullptr, &dc_layer_list,
        &damage_rect_, &content_bounds_);
    EXPECT_EQ(gfx::Rect(), damage_rect);
    EXPECT_EQ(0U, overlay_list.size());
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
    EXPECT_EQ(1, dc_layer_list.back().shared_state->z_order);
    // Damage rect should be unchanged on initial frame because of resize, but
    // should be empty on the second frame because everything was put in a
    // layer.
    if (i == 1)
      EXPECT_TRUE(damage_rect_.IsEmpty());
    else
      EXPECT_EQ(gfx::Rect(1, 1, 10, 10), damage_rect_);
  }
}

TEST_F(DCLayerOverlayTest, ClipRect) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(features::kDirectCompositionUnderlays);

  // Process twice. The second time through the overlay list shouldn't change,
  // which will allow the damage rect to reflect just the changes in that
  // frame.
  for (size_t i = 0; i < 2; ++i) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    CreateOpaqueQuadAt(resource_provider_.get(),
                       pass->shared_quad_state_list.back(), pass.get(),
                       gfx::Rect(0, 2, 100, 100), SK_ColorWHITE);
    pass->shared_quad_state_list.back()->is_clipped = true;
    pass->shared_quad_state_list.back()->clip_rect = gfx::Rect(0, 3, 100, 100);
    SharedQuadState* shared_state = pass->CreateAndAppendSharedQuadState();
    shared_state->opacity = 1.f;
    CreateFullscreenCandidateYUVVideoQuad(resource_provider_.get(),
                                          shared_state, pass.get());
    shared_state->is_clipped = true;
    // Clipped rect shouldn't be overlapped by clipped opaque quad rect.
    shared_state->clip_rect = gfx::Rect(0, 0, 100, 3);

    DCLayerOverlayList dc_layer_list;
    OverlayCandidateList overlay_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &overlay_list, nullptr, &dc_layer_list,
        &damage_rect_, &content_bounds_);
    EXPECT_EQ(0U, overlay_list.size());
    EXPECT_EQ(1U, dc_layer_list.size());
    // Because of clip rects the overlay isn't occluded and shouldn't be an
    // underlay.
    EXPECT_EQ(1, dc_layer_list.back().shared_state->z_order);
    if (i == 1) {
      // The damage rect should only contain contents that aren't in the
      // clipped overlay rect.
      EXPECT_EQ(gfx::Rect(1, 3, 10, 8), damage_rect_);
    }
  }
}

TEST_F(DCLayerOverlayTest, TransparentOnTop) {
  base::test::ScopedFeatureList feature_list;

  // Process twice. The second time through the overlay list shouldn't change,
  // which will allow the damage rect to reflect just the changes in that
  // frame.
  for (size_t i = 0; i < 2; ++i) {
    std::unique_ptr<RenderPass> pass = CreateRenderPass();
    CreateFullscreenCandidateYUVVideoQuad(resource_provider_.get(),
                                          pass->shared_quad_state_list.back(),
                                          pass.get());
    pass->shared_quad_state_list.back()->opacity = 0.5f;

    DCLayerOverlayList dc_layer_list;
    OverlayCandidateList overlay_list;
    OverlayProcessor::FilterOperationsMap render_pass_filters;
    OverlayProcessor::FilterOperationsMap render_pass_background_filters;
    damage_rect_ = gfx::Rect(1, 1, 10, 10);
    RenderPassList pass_list;
    pass_list.push_back(std::move(pass));
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list, render_pass_filters,
        render_pass_background_filters, &overlay_list, nullptr, &dc_layer_list,
        &damage_rect_, &content_bounds_);
    EXPECT_EQ(0U, overlay_list.size());
    EXPECT_EQ(1U, dc_layer_list.size());
    EXPECT_EQ(1, dc_layer_list.back().shared_state->z_order);
    // Quad isn't opaque, so underlying damage must remain the same.
    EXPECT_EQ(gfx::Rect(1, 1, 10, 10), damage_rect_);
  }
}

class OverlayInfoRendererGL : public viz::GLRenderer {
 public:
  OverlayInfoRendererGL(const viz::RendererSettings* settings,
                        OutputSurface* output_surface,
                        ResourceProvider* resource_provider)
      : viz::GLRenderer(settings, output_surface, resource_provider, NULL),
        expect_overlays_(false) {}

  MOCK_METHOD2(DoDrawQuad,
               void(const DrawQuad* quad, const gfx::QuadF* draw_region));

  void SetCurrentFrame(const DrawingFrame& frame) {
    SetCurrentFrameForTesting(frame);
  }

  using viz::GLRenderer::BeginDrawingFrame;

  void FinishDrawingFrame() override {
    viz::GLRenderer::FinishDrawingFrame();

    if (!expect_overlays_) {
      EXPECT_EQ(0U, current_frame()->overlay_list.size());
      return;
    }

    ASSERT_EQ(2U, current_frame()->overlay_list.size());
    EXPECT_GE(current_frame()->overlay_list.back().resource_id, 0U);
  }

  void set_expect_overlays(bool expect_overlays) {
    expect_overlays_ = expect_overlays;
  }

 private:
  bool expect_overlays_;
};

class MockOverlayScheduler {
 public:
  MOCK_METHOD5(Schedule,
               void(int plane_z_order,
                    gfx::OverlayTransform plane_transform,
                    unsigned overlay_texture_id,
                    const gfx::Rect& display_bounds,
                    const gfx::RectF& uv_rect));
};

class GLRendererWithOverlaysTest : public testing::Test {
  using OutputSurfaceType = OverlayOutputSurface<SingleOverlayValidator>;

 protected:
  GLRendererWithOverlaysTest() {
    provider_ = TestContextProvider::Create();
    provider_->BindToCurrentThread();
    output_surface_.reset(new OutputSurfaceType(provider_));
    output_surface_->BindToClient(&output_surface_client_);
    resource_provider_ = FakeResourceProvider::Create(provider_.get(), nullptr);

    provider_->support()->SetScheduleOverlayPlaneCallback(base::Bind(
        &MockOverlayScheduler::Schedule, base::Unretained(&scheduler_)));
  }

  void Init(bool use_validator) {
    if (use_validator)
      output_surface_->SetOverlayCandidateValidator(new SingleOverlayValidator);

    renderer_ = base::MakeUnique<OverlayInfoRendererGL>(
        &settings_, output_surface_.get(), resource_provider_.get());
    renderer_->Initialize();
    renderer_->SetVisible(true);
  }

  void DrawFrame(RenderPassList* pass_list, const gfx::Size& viewport_size) {
    renderer_->DrawFrame(pass_list, 1.f, viewport_size);
  }
  void SwapBuffers() {
    renderer_->SwapBuffers(std::vector<ui::LatencyInfo>());
    renderer_->SwapBuffersComplete();
  }
  void SwapBuffersWithoutComplete() {
    renderer_->SwapBuffers(std::vector<ui::LatencyInfo>());
  }
  void SwapBuffersComplete() {
    renderer_->SwapBuffersComplete();
  }
  void ReturnResourceInUseQuery(viz::ResourceId id) {
    ResourceProvider::ScopedReadLockGL lock(resource_provider_.get(), id);
    gpu::TextureInUseResponse response;
    response.texture = lock.texture_id();
    response.in_use = false;
    gpu::TextureInUseResponses responses;
    responses.push_back(response);
    renderer_->DidReceiveTextureInUseResponses(responses);
  }

  viz::RendererSettings settings_;
  FakeOutputSurfaceClient output_surface_client_;
  std::unique_ptr<OutputSurfaceType> output_surface_;
  std::unique_ptr<ResourceProvider> resource_provider_;
  std::unique_ptr<OverlayInfoRendererGL> renderer_;
  scoped_refptr<TestContextProvider> provider_;
  MockOverlayScheduler scheduler_;
};

TEST_F(GLRendererWithOverlaysTest, OverlayQuadNotDrawn) {
  bool use_validator = true;
  Init(use_validator);
  renderer_->set_expect_overlays(true);
  output_surface_->GetOverlayCandidateValidator()->AddExpectedRect(
      gfx::RectF(kOverlayBottomRightRect));

  gfx::Size viewport_size(16, 16);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  CreateCandidateQuadAt(resource_provider_.get(),
                        pass->shared_quad_state_list.back(), pass.get(),
                        kOverlayBottomRightRect);
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  // Candidate pass was taken out and extra skipped pass added,
  // so only draw 2 quads.
  EXPECT_CALL(*renderer_, DoDrawQuad(_, _)).Times(2);
  EXPECT_CALL(scheduler_,
              Schedule(0, gfx::OVERLAY_TRANSFORM_NONE, _,
                       gfx::Rect(kDisplaySize), gfx::RectF(0, 0, 1, 1)))
      .Times(1);
  EXPECT_CALL(scheduler_, Schedule(1, gfx::OVERLAY_TRANSFORM_NONE, _,
                                   kOverlayBottomRightRect,
                                   BoundingRect(kUVTopLeft, kUVBottomRight)))
      .Times(1);
  DrawFrame(&pass_list, viewport_size);
  EXPECT_EQ(1U, output_surface_->bind_framebuffer_count());

  SwapBuffers();

  Mock::VerifyAndClearExpectations(renderer_.get());
  Mock::VerifyAndClearExpectations(&scheduler_);
}

TEST_F(GLRendererWithOverlaysTest, OccludedQuadInUnderlay) {
  bool use_validator = true;
  Init(use_validator);
  renderer_->set_expect_overlays(true);
  gfx::Size viewport_size(16, 16);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  // Candidate quad should fail to be overlaid on top because of occlusion.
  // Expect to be replaced with transparent hole quad and placed in underlay.
  EXPECT_CALL(*renderer_, DoDrawQuad(_, _)).Times(3);
  EXPECT_CALL(scheduler_,
              Schedule(0, gfx::OVERLAY_TRANSFORM_NONE, _,
                       gfx::Rect(kDisplaySize), gfx::RectF(0, 0, 1, 1)))
      .Times(1);
  EXPECT_CALL(scheduler_,
              Schedule(-1, gfx::OVERLAY_TRANSFORM_NONE, _, kOverlayRect,
                       BoundingRect(kUVTopLeft, kUVBottomRight)))
      .Times(1);
  DrawFrame(&pass_list, viewport_size);
  EXPECT_EQ(1U, output_surface_->bind_framebuffer_count());

  SwapBuffers();

  Mock::VerifyAndClearExpectations(renderer_.get());
  Mock::VerifyAndClearExpectations(&scheduler_);
}

TEST_F(GLRendererWithOverlaysTest, NoValidatorNoOverlay) {
  bool use_validator = false;
  Init(use_validator);
  renderer_->set_expect_overlays(false);
  gfx::Size viewport_size(16, 16);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  // Should not see the primary surface's overlay.
  output_surface_->set_is_displayed_as_overlay_plane(false);
  EXPECT_CALL(*renderer_, DoDrawQuad(_, _)).Times(3);
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(0);
  DrawFrame(&pass_list, viewport_size);
  EXPECT_EQ(1U, output_surface_->bind_framebuffer_count());
  SwapBuffers();
  Mock::VerifyAndClearExpectations(renderer_.get());
  Mock::VerifyAndClearExpectations(&scheduler_);
}

// viz::GLRenderer skips drawing occluded quads when partial swap is enabled.
TEST_F(GLRendererWithOverlaysTest, OccludedQuadNotDrawnWhenPartialSwapEnabled) {
  provider_->TestContext3d()->set_have_post_sub_buffer(true);
  settings_.partial_swap_enabled = true;
  bool use_validator = true;
  Init(use_validator);
  renderer_->set_expect_overlays(true);
  gfx::Size viewport_size(16, 16);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  output_surface_->set_is_displayed_as_overlay_plane(true);
  EXPECT_CALL(*renderer_, DoDrawQuad(_, _)).Times(0);
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  DrawFrame(&pass_list, viewport_size);
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
  SwapBuffers();
  Mock::VerifyAndClearExpectations(renderer_.get());
  Mock::VerifyAndClearExpectations(&scheduler_);
}

// viz::GLRenderer skips drawing occluded quads when empty swap is enabled.
TEST_F(GLRendererWithOverlaysTest, OccludedQuadNotDrawnWhenEmptySwapAllowed) {
  provider_->TestContext3d()->set_have_commit_overlay_planes(true);
  bool use_validator = true;
  Init(use_validator);
  renderer_->set_expect_overlays(true);
  gfx::Size viewport_size(16, 16);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();

  CreateFullscreenCandidateQuad(resource_provider_.get(),
                                pass->shared_quad_state_list.back(),
                                pass.get());

  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());
  CreateFullscreenOpaqueQuad(resource_provider_.get(),
                             pass->shared_quad_state_list.back(), pass.get());

  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  output_surface_->set_is_displayed_as_overlay_plane(true);
  EXPECT_CALL(*renderer_, DoDrawQuad(_, _)).Times(0);
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  DrawFrame(&pass_list, viewport_size);
  EXPECT_EQ(0U, output_surface_->bind_framebuffer_count());
  SwapBuffers();
  Mock::VerifyAndClearExpectations(renderer_.get());
  Mock::VerifyAndClearExpectations(&scheduler_);
}

TEST_F(GLRendererWithOverlaysTest, ResourcesExportedAndReturnedWithDelay) {
  bool use_validator = true;
  Init(use_validator);
  renderer_->set_expect_overlays(true);

  viz::ResourceId resource1 =
      CreateResource(resource_provider_.get(), gfx::Size(32, 32), true);
  viz::ResourceId resource2 =
      CreateResource(resource_provider_.get(), gfx::Size(32, 32), true);
  viz::ResourceId resource3 =
      CreateResource(resource_provider_.get(), gfx::Size(32, 32), true);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  DirectRenderer::DrawingFrame frame1;
  frame1.render_passes_in_draw_order = &pass_list;
  frame1.overlay_list.resize(2);
  frame1.overlay_list.front().use_output_surface_for_resource = true;
  OverlayCandidate& overlay1 = frame1.overlay_list.back();
  overlay1.resource_id = resource1;
  overlay1.plane_z_order = 1;

  DirectRenderer::DrawingFrame frame2;
  frame2.render_passes_in_draw_order = &pass_list;
  frame2.overlay_list.resize(2);
  frame2.overlay_list.front().use_output_surface_for_resource = true;
  OverlayCandidate& overlay2 = frame2.overlay_list.back();
  overlay2.resource_id = resource2;
  overlay2.plane_z_order = 1;

  DirectRenderer::DrawingFrame frame3;
  frame3.render_passes_in_draw_order = &pass_list;
  frame3.overlay_list.resize(2);
  frame3.overlay_list.front().use_output_surface_for_resource = true;
  OverlayCandidate& overlay3 = frame3.overlay_list.back();
  overlay3.resource_id = resource3;
  overlay3.plane_z_order = 1;

  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame1);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource2));
  SwapBuffersWithoutComplete();
  Mock::VerifyAndClearExpectations(&scheduler_);

  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame2);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  Mock::VerifyAndClearExpectations(&scheduler_);

  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame3);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));
  Mock::VerifyAndClearExpectations(&scheduler_);

  // No overlays, release the resource.
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(0);
  DirectRenderer::DrawingFrame frame_no_overlays;
  frame_no_overlays.render_passes_in_draw_order = &pass_list;
  renderer_->set_expect_overlays(false);
  renderer_->SetCurrentFrame(frame_no_overlays);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));
  Mock::VerifyAndClearExpectations(&scheduler_);

  // Use the same buffer twice.
  renderer_->set_expect_overlays(true);
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame1);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  Mock::VerifyAndClearExpectations(&scheduler_);

  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame1);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  Mock::VerifyAndClearExpectations(&scheduler_);

  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(0);
  renderer_->set_expect_overlays(false);
  renderer_->SetCurrentFrame(frame_no_overlays);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  Mock::VerifyAndClearExpectations(&scheduler_);

  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(0);
  renderer_->set_expect_overlays(false);
  renderer_->SetCurrentFrame(frame_no_overlays);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  SwapBuffersComplete();
  SwapBuffersWithoutComplete();
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  Mock::VerifyAndClearExpectations(&scheduler_);
}

TEST_F(GLRendererWithOverlaysTest, ResourcesExportedAndReturnedAfterGpuQuery) {
  bool use_validator = true;
  settings_.release_overlay_resources_after_gpu_query = true;
  Init(use_validator);
  renderer_->set_expect_overlays(true);

  viz::ResourceId resource1 =
      CreateResource(resource_provider_.get(), gfx::Size(32, 32), true);
  viz::ResourceId resource2 =
      CreateResource(resource_provider_.get(), gfx::Size(32, 32), true);
  viz::ResourceId resource3 =
      CreateResource(resource_provider_.get(), gfx::Size(32, 32), true);

  std::unique_ptr<RenderPass> pass = CreateRenderPass();
  RenderPassList pass_list;
  pass_list.push_back(std::move(pass));

  DirectRenderer::DrawingFrame frame1;
  frame1.render_passes_in_draw_order = &pass_list;
  frame1.overlay_list.resize(2);
  frame1.overlay_list.front().use_output_surface_for_resource = true;
  OverlayCandidate& overlay1 = frame1.overlay_list.back();
  overlay1.resource_id = resource1;
  overlay1.plane_z_order = 1;

  DirectRenderer::DrawingFrame frame2;
  frame2.render_passes_in_draw_order = &pass_list;
  frame2.overlay_list.resize(2);
  frame2.overlay_list.front().use_output_surface_for_resource = true;
  OverlayCandidate& overlay2 = frame2.overlay_list.back();
  overlay2.resource_id = resource2;
  overlay2.plane_z_order = 1;

  DirectRenderer::DrawingFrame frame3;
  frame3.render_passes_in_draw_order = &pass_list;
  frame3.overlay_list.resize(2);
  frame3.overlay_list.front().use_output_surface_for_resource = true;
  OverlayCandidate& overlay3 = frame3.overlay_list.back();
  overlay3.resource_id = resource3;
  overlay3.plane_z_order = 1;

  // First frame, with no swap completion.
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame1);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  SwapBuffersWithoutComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  Mock::VerifyAndClearExpectations(&scheduler_);

  // Second frame, with no swap completion.
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame2);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  SwapBuffersWithoutComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  Mock::VerifyAndClearExpectations(&scheduler_);

  // Third frame, still with no swap completion (where the resources would
  // otherwise have been released).
  EXPECT_CALL(scheduler_, Schedule(_, _, _, _, _)).Times(2);
  renderer_->SetCurrentFrame(frame3);
  renderer_->BeginDrawingFrame();
  renderer_->FinishDrawingFrame();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));
  SwapBuffersWithoutComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));
  Mock::VerifyAndClearExpectations(&scheduler_);

  // This completion corresponds to the first frame.
  SwapBuffersComplete();
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));

  // This completion corresponds to the second frame. The first resource is no
  // longer in use.
  ReturnResourceInUseQuery(resource1);
  SwapBuffersComplete();
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));

  // This completion corresponds to the third frame.
  SwapBuffersComplete();
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_TRUE(resource_provider_->InUseByConsumer(resource3));

  ReturnResourceInUseQuery(resource2);
  ReturnResourceInUseQuery(resource3);
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource1));
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource2));
  EXPECT_FALSE(resource_provider_->InUseByConsumer(resource3));
}

class CALayerOverlayRPDQTest : public CALayerOverlayTest {
 protected:
  void SetUp() override {
    CALayerOverlayTest::SetUp();
    pass_list_.push_back(CreateRenderPass());
    pass_ = pass_list_.back().get();
    quad_ = pass_->CreateAndAppendDrawQuad<RenderPassDrawQuad>();
    render_pass_id_ = 3;
  }

  void ProcessForOverlays() {
    gfx::Rect damage_rect;
    overlay_list_ = BackbufferOverlayList(pass_);
    overlay_processor_->ProcessForOverlays(
        resource_provider_.get(), &pass_list_, render_pass_filters_,
        render_pass_background_filters_, &overlay_list_, &ca_layer_list_,
        nullptr, &damage_rect_, &content_bounds_);
  }
  RenderPassList pass_list_;
  RenderPass* pass_;
  RenderPassDrawQuad* quad_;
  int render_pass_id_;
  FilterOperations filters_;
  FilterOperations background_filters_;
  OverlayProcessor::FilterOperationsMap render_pass_filters_;
  OverlayProcessor::FilterOperationsMap render_pass_background_filters_;
  CALayerOverlayList ca_layer_list_;
  OverlayCandidateList overlay_list_;
};

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadNoFilters) {
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 1), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();

  EXPECT_EQ(1U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadAllValidFilters) {
  filters_.Append(FilterOperation::CreateGrayscaleFilter(0.1f));
  filters_.Append(FilterOperation::CreateSepiaFilter(0.2f));
  filters_.Append(FilterOperation::CreateSaturateFilter(0.3f));
  filters_.Append(FilterOperation::CreateHueRotateFilter(0.4f));
  filters_.Append(FilterOperation::CreateInvertFilter(0.5f));
  filters_.Append(FilterOperation::CreateBrightnessFilter(0.6f));
  filters_.Append(FilterOperation::CreateContrastFilter(0.7f));
  filters_.Append(FilterOperation::CreateOpacityFilter(0.8f));
  filters_.Append(FilterOperation::CreateBlurFilter(0.9f));
  filters_.Append(FilterOperation::CreateDropShadowFilter(gfx::Point(10, 20),
                                                          1.0f, SK_ColorGREEN));
  render_pass_filters_[render_pass_id_] = &filters_;
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 1), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();

  EXPECT_EQ(1U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadOpacityFilterScale) {
  filters_.Append(FilterOperation::CreateOpacityFilter(0.8f));
  render_pass_filters_[render_pass_id_] = &filters_;
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 2), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();
  EXPECT_EQ(1U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadBlurFilterScale) {
  filters_.Append(FilterOperation::CreateBlurFilter(0.8f));
  render_pass_filters_[render_pass_id_] = &filters_;
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 2), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();
  EXPECT_EQ(1U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadDropShadowFilterScale) {
  filters_.Append(FilterOperation::CreateDropShadowFilter(gfx::Point(10, 20),
                                                          1.0f, SK_ColorGREEN));
  render_pass_filters_[render_pass_id_] = &filters_;
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 2), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();
  EXPECT_EQ(1U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadBackgroundFilter) {
  background_filters_.Append(FilterOperation::CreateGrayscaleFilter(0.1f));
  render_pass_background_filters_[render_pass_id_] = &background_filters_;
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 1), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();
  EXPECT_EQ(0U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadMask) {
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 2, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 1), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();
  EXPECT_EQ(1U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, RenderPassDrawQuadUnsupportedFilter) {
  filters_.Append(FilterOperation::CreateZoomFilter(0.9f, 1));
  render_pass_filters_[render_pass_id_] = &filters_;
  quad_->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                kOverlayRect, render_pass_id_, 0, gfx::RectF(), gfx::Size(),
                gfx::Vector2dF(1, 1), gfx::PointF(), gfx::RectF());
  ProcessForOverlays();
  EXPECT_EQ(0U, ca_layer_list_.size());
}

TEST_F(CALayerOverlayRPDQTest, TooManyRenderPassDrawQuads) {
  filters_.Append(FilterOperation::CreateBlurFilter(0.8f));
  int count = 35;

  for (int i = 0; i < count; ++i) {
    RenderPassDrawQuad* quad =
        pass_->CreateAndAppendDrawQuad<RenderPassDrawQuad>();
    quad->SetNew(pass_->shared_quad_state_list.back(), kOverlayRect,
                 kOverlayRect, render_pass_id_, 2, gfx::RectF(), gfx::Size(),
                 gfx::Vector2dF(1, 1), gfx::PointF(), gfx::RectF());
  }

  ProcessForOverlays();
  EXPECT_EQ(0U, ca_layer_list_.size());
}

}  // namespace
}  // namespace cc
