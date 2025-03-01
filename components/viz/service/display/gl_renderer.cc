// Copyright 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/viz/service/display/gl_renderer.h"

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <string>
#include <vector>

#include "base/feature_list.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "cc/base/container_util.h"
#include "cc/base/math_util.h"
#include "cc/base/render_surface_filters.h"
#include "cc/debug/debug_colors.h"
#include "cc/output/compositor_frame.h"
#include "cc/output/compositor_frame_metadata.h"
#include "cc/output/layer_quad.h"
#include "cc/output/output_surface.h"
#include "cc/output/output_surface_frame.h"
#include "cc/output/texture_mailbox_deleter.h"
#include "cc/quads/draw_polygon.h"
#include "cc/quads/picture_draw_quad.h"
#include "cc/quads/render_pass.h"
#include "cc/quads/stream_video_draw_quad.h"
#include "cc/quads/texture_draw_quad.h"
#include "cc/raster/scoped_gpu_raster.h"
#include "cc/resources/resource_pool.h"
#include "cc/resources/scoped_resource.h"
#include "components/viz/common/display/renderer_settings.h"
#include "components/viz/common/gpu/context_provider.h"
#include "components/viz/common/quads/copy_output_request.h"
#include "components/viz/service/display/dynamic_geometry_binding.h"
#include "components/viz/service/display/static_geometry_binding.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/client/context_support.h"
#include "gpu/command_buffer/client/gles2_interface.h"
#include "gpu/command_buffer/common/gpu_memory_allocation.h"
#include "media/base/media_switches.h"
#include "skia/ext/texture_handle.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkColorFilter.h"
#include "third_party/skia/include/core/SkImage.h"
#include "third_party/skia/include/core/SkSurface.h"
#include "third_party/skia/include/gpu/GrBackendSurface.h"
#include "third_party/skia/include/gpu/GrContext.h"
#include "third_party/skia/include/gpu/gl/GrGLInterface.h"
#include "third_party/skia/include/gpu/gl/GrGLTypes.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/color_transform.h"
#include "ui/gfx/geometry/quad_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/skia_util.h"

using gpu::gles2::GLES2Interface;

namespace viz {
namespace {

Float4 UVTransform(const cc::TextureDrawQuad* quad) {
  gfx::PointF uv0 = quad->uv_top_left;
  gfx::PointF uv1 = quad->uv_bottom_right;
  Float4 xform = {{uv0.x(), uv0.y(), uv1.x() - uv0.x(), uv1.y() - uv0.y()}};
  if (quad->y_flipped) {
    xform.data[1] = 1.0f - xform.data[1];
    xform.data[3] = -xform.data[3];
  }
  return xform;
}

// To prevent sampling outside the visible rect.
Float4 UVClampRect(gfx::RectF uv_visible_rect,
                   const gfx::Size& texture_size,
                   SamplerType sampler) {
  gfx::SizeF half_texel(0.5f, 0.5f);
  if (sampler != SAMPLER_TYPE_2D_RECT) {
    half_texel.Scale(1.f / texture_size.width(), 1.f / texture_size.height());
  } else {
    uv_visible_rect.Scale(texture_size.width(), texture_size.height());
  }
  uv_visible_rect.Inset(half_texel.width(), half_texel.height());
  return {{uv_visible_rect.x(), uv_visible_rect.y(), uv_visible_rect.right(),
           uv_visible_rect.bottom()}};
}

Float4 PremultipliedColor(SkColor color, float opacity) {
  const float factor = 1.0f / 255.0f;
  const float alpha = opacity * SkColorGetA(color) * factor;

  Float4 result = {{SkColorGetR(color) * factor * alpha,
                    SkColorGetG(color) * factor * alpha,
                    SkColorGetB(color) * factor * alpha, alpha}};
  return result;
}

SamplerType SamplerTypeFromTextureTarget(GLenum target) {
  switch (target) {
    case GL_TEXTURE_2D:
      return SAMPLER_TYPE_2D;
    case GL_TEXTURE_RECTANGLE_ARB:
      return SAMPLER_TYPE_2D_RECT;
    case GL_TEXTURE_EXTERNAL_OES:
      return SAMPLER_TYPE_EXTERNAL_OES;
    default:
      NOTREACHED();
      return SAMPLER_TYPE_2D;
  }
}

BlendMode BlendModeFromSkXfermode(SkBlendMode mode) {
  switch (mode) {
    case SkBlendMode::kSrcOver:
      return BLEND_MODE_NORMAL;
    case SkBlendMode::kDstIn:
      return BLEND_MODE_DESTINATION_IN;
    case SkBlendMode::kScreen:
      return BLEND_MODE_SCREEN;
    case SkBlendMode::kOverlay:
      return BLEND_MODE_OVERLAY;
    case SkBlendMode::kDarken:
      return BLEND_MODE_DARKEN;
    case SkBlendMode::kLighten:
      return BLEND_MODE_LIGHTEN;
    case SkBlendMode::kColorDodge:
      return BLEND_MODE_COLOR_DODGE;
    case SkBlendMode::kColorBurn:
      return BLEND_MODE_COLOR_BURN;
    case SkBlendMode::kHardLight:
      return BLEND_MODE_HARD_LIGHT;
    case SkBlendMode::kSoftLight:
      return BLEND_MODE_SOFT_LIGHT;
    case SkBlendMode::kDifference:
      return BLEND_MODE_DIFFERENCE;
    case SkBlendMode::kExclusion:
      return BLEND_MODE_EXCLUSION;
    case SkBlendMode::kMultiply:
      return BLEND_MODE_MULTIPLY;
    case SkBlendMode::kHue:
      return BLEND_MODE_HUE;
    case SkBlendMode::kSaturation:
      return BLEND_MODE_SATURATION;
    case SkBlendMode::kColor:
      return BLEND_MODE_COLOR;
    case SkBlendMode::kLuminosity:
      return BLEND_MODE_LUMINOSITY;
    default:
      NOTREACHED();
      return BLEND_MODE_NONE;
  }
}

// Smallest unit that impact anti-aliasing output. We use this to
// determine when anti-aliasing is unnecessary.
const float kAntiAliasingEpsilon = 1.0f / 1024.0f;

// Block or crash if the number of pending sync queries reach this high as
// something is seriously wrong on the service side if this happens.
const size_t kMaxPendingSyncQueries = 16;
}  // anonymous namespace

// Parameters needed to draw a cc::RenderPassDrawQuad.
struct DrawRenderPassDrawQuadParams {
  DrawRenderPassDrawQuadParams() {}
  ~DrawRenderPassDrawQuadParams() {}

  // Required Inputs.
  const cc::RenderPassDrawQuad* quad = nullptr;
  const cc::Resource* contents_texture = nullptr;
  const gfx::QuadF* clip_region = nullptr;
  bool flip_texture = false;
  gfx::Transform window_matrix;
  gfx::Transform projection_matrix;
  gfx::Transform quad_to_target_transform;
  const cc::FilterOperations* filters = nullptr;
  const cc::FilterOperations* background_filters = nullptr;

  // Whether the texture to be sampled from needs to be flipped.
  bool source_needs_flip = false;

  float edge[24];
  SkScalar color_matrix[20];

  // Blending refers to modifications to the backdrop.
  bool use_shaders_for_blending = false;

  bool use_aa = false;

  // Some filters affect pixels outside the original contents bounds. This
  // requires translation of the source when texturing, as well as a change in
  // the bounds of the destination.
  gfx::Point src_offset;
  gfx::RectF dst_rect;

  // A Skia image that should be sampled from instead of the original
  // contents.
  sk_sp<SkImage> filter_image;

  // The original contents, bound for sampling.
  std::unique_ptr<cc::ResourceProvider::ScopedSamplerGL> contents_resource_lock;

  // A mask to be applied when drawing the RPDQ.
  std::unique_ptr<cc::ResourceProvider::ScopedSamplerGL> mask_resource_lock;

  // Original background texture.
  std::unique_ptr<cc::ScopedResource> background_texture;
  std::unique_ptr<cc::ResourceProvider::ScopedSamplerGL>
      shader_background_sampler_lock;

  // Backdrop bounding box.
  gfx::Rect background_rect;

  // Filtered background texture.
  sk_sp<SkImage> background_image;
  GLuint background_image_id = 0;

  // Whether the original background texture is needed for the mask.
  bool mask_for_background = false;

  // Whether a color matrix needs to be applied by the shaders when drawing
  // the RPDQ.
  bool use_color_matrix = false;

  gfx::QuadF surface_quad;

  gfx::Transform contents_device_transform;

  gfx::RectF tex_coord_rect;

  // The color space of the texture bound for sampling (from filter_image or
  // contents_resource_lock, depending on the path taken).
  gfx::ColorSpace contents_color_space;
};

static GLint GetActiveTextureUnit(GLES2Interface* gl) {
  GLint active_unit = 0;
  gl->GetIntegerv(GL_ACTIVE_TEXTURE, &active_unit);
  return active_unit;
}

class GLRenderer::ScopedUseGrContext {
 public:
  static std::unique_ptr<ScopedUseGrContext> Create(GLRenderer* renderer) {
    // GrContext for filters is created lazily, and may fail if the context
    // is lost.
    // TODO(vmiura,bsalomon): crbug.com/487850 Ensure that
    // ContextProvider::GrContext() does not return NULL.
    if (renderer->output_surface_->context_provider()->GrContext())
      return base::WrapUnique(new ScopedUseGrContext(renderer));
    return nullptr;
  }

  ~ScopedUseGrContext() {
    // Pass context control back to GLrenderer.
    scoped_gpu_raster_ = nullptr;
    renderer_->RestoreGLState();
  }

  GrContext* context() const {
    return renderer_->output_surface_->context_provider()->GrContext();
  }

 private:
  explicit ScopedUseGrContext(GLRenderer* renderer)
      : scoped_gpu_raster_(new cc::ScopedGpuRaster(
            renderer->output_surface_->context_provider())),
        renderer_(renderer) {
    // scoped_gpu_raster_ passes context control to Skia.
  }

  std::unique_ptr<cc::ScopedGpuRaster> scoped_gpu_raster_;
  GLRenderer* renderer_;

  DISALLOW_COPY_AND_ASSIGN(ScopedUseGrContext);
};

struct GLRenderer::PendingAsyncReadPixels {
  PendingAsyncReadPixels() : buffer(0) {}

  std::unique_ptr<CopyOutputRequest> copy_request;
  unsigned buffer;

 private:
  DISALLOW_COPY_AND_ASSIGN(PendingAsyncReadPixels);
};

class GLRenderer::SyncQuery {
 public:
  explicit SyncQuery(gpu::gles2::GLES2Interface* gl)
      : gl_(gl), query_id_(0u), is_pending_(false), weak_ptr_factory_(this) {
    gl_->GenQueriesEXT(1, &query_id_);
  }
  virtual ~SyncQuery() { gl_->DeleteQueriesEXT(1, &query_id_); }

  scoped_refptr<cc::ResourceProvider::Fence> Begin() {
    DCHECK(!IsPending());
    // Invalidate weak pointer held by old fence.
    weak_ptr_factory_.InvalidateWeakPtrs();
    // Note: In case the set of drawing commands issued before End() do not
    // depend on the query, defer BeginQueryEXT call until Set() is called and
    // query is required.
    return make_scoped_refptr<cc::ResourceProvider::Fence>(
        new Fence(weak_ptr_factory_.GetWeakPtr()));
  }

  void Set() {
    if (is_pending_)
      return;

    // Note: BeginQueryEXT on GL_COMMANDS_COMPLETED_CHROMIUM is effectively a
    // noop relative to GL, so it doesn't matter where it happens but we still
    // make sure to issue this command when Set() is called (prior to issuing
    // any drawing commands that depend on query), in case some future extension
    // can take advantage of this.
    gl_->BeginQueryEXT(GL_COMMANDS_COMPLETED_CHROMIUM, query_id_);
    is_pending_ = true;
  }

  void End() {
    if (!is_pending_)
      return;

    gl_->EndQueryEXT(GL_COMMANDS_COMPLETED_CHROMIUM);
  }

  bool IsPending() {
    if (!is_pending_)
      return false;

    unsigned result_available = 1;
    gl_->GetQueryObjectuivEXT(query_id_, GL_QUERY_RESULT_AVAILABLE_EXT,
                              &result_available);
    is_pending_ = !result_available;
    return is_pending_;
  }

  void Wait() {
    if (!is_pending_)
      return;

    unsigned result = 0;
    gl_->GetQueryObjectuivEXT(query_id_, GL_QUERY_RESULT_EXT, &result);
    is_pending_ = false;
  }

 private:
  class Fence : public cc::ResourceProvider::Fence {
   public:
    explicit Fence(base::WeakPtr<GLRenderer::SyncQuery> query)
        : query_(query) {}

    // Overridden from cc::ResourceProvider::Fence:
    void Set() override {
      DCHECK(query_);
      query_->Set();
    }
    bool HasPassed() override { return !query_ || !query_->IsPending(); }
    void Wait() override {
      if (query_)
        query_->Wait();
    }

   private:
    ~Fence() override {}

    base::WeakPtr<SyncQuery> query_;

    DISALLOW_COPY_AND_ASSIGN(Fence);
  };

  gpu::gles2::GLES2Interface* gl_;
  unsigned query_id_;
  bool is_pending_;
  base::WeakPtrFactory<SyncQuery> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(SyncQuery);
};

GLRenderer::GLRenderer(const RendererSettings* settings,
                       cc::OutputSurface* output_surface,
                       cc::ResourceProvider* resource_provider,
                       cc::TextureMailboxDeleter* texture_mailbox_deleter)
    : cc::DirectRenderer(settings, output_surface, resource_provider),
      shared_geometry_quad_(QuadVertexRect()),
      gl_(output_surface->context_provider()->ContextGL()),
      context_support_(output_surface->context_provider()->ContextSupport()),
      texture_mailbox_deleter_(texture_mailbox_deleter),
      gl_composited_overlay_candidate_quad_border_(
          settings->gl_composited_overlay_candidate_quad_border),
      bound_geometry_(NO_BINDING),
      color_lut_cache_(gl_,
                       output_surface_->context_provider()
                           ->ContextCapabilities()
                           .texture_half_float_linear),
      weak_ptr_factory_(this) {
  DCHECK(gl_);
  DCHECK(context_support_);

  const auto& context_caps =
      output_surface_->context_provider()->ContextCapabilities();
  DCHECK(!context_caps.iosurface || context_caps.texture_rectangle);

  use_discard_framebuffer_ = context_caps.discard_framebuffer;
  use_sync_query_ = context_caps.sync_query;
  use_blend_equation_advanced_ = context_caps.blend_equation_advanced;
  use_blend_equation_advanced_coherent_ =
      context_caps.blend_equation_advanced_coherent;
  use_occlusion_query_ = context_caps.occlusion_query;
  use_swap_with_bounds_ = context_caps.swap_buffers_with_bounds;

  InitializeSharedObjects();
}

GLRenderer::~GLRenderer() {
  CleanupSharedObjects();

  if (context_visibility_) {
    auto* context_provider = output_surface_->context_provider();
    auto* cache_controller = context_provider->CacheController();
    cache_controller->ClientBecameNotVisible(std::move(context_visibility_));
  }
}

bool GLRenderer::CanPartialSwap() {
  if (use_swap_with_bounds_)
    return false;
  auto* context_provider = output_surface_->context_provider();
  return context_provider->ContextCapabilities().post_sub_buffer;
}

ResourceFormat GLRenderer::BackbufferFormat() const {
  if (current_frame()->current_render_pass->color_space.IsHDR() &&
      resource_provider_->IsRenderBufferFormatSupported(RGBA_F16)) {
    return RGBA_F16;
  }
  return resource_provider_->best_texture_format();
}

void GLRenderer::DidChangeVisibility() {
  if (visible_) {
    output_surface_->EnsureBackbuffer();
  } else {
    TRACE_EVENT0("cc", "GLRenderer::DidChangeVisibility dropping resources");
    ReleaseRenderPassTextures();
    output_surface_->DiscardBackbuffer();
    gl_->ReleaseShaderCompiler();
  }

  PrepareGeometry(NO_BINDING);

  auto* context_provider = output_surface_->context_provider();
  auto* cache_controller = context_provider->CacheController();
  if (visible_) {
    DCHECK(!context_visibility_);
    context_visibility_ = cache_controller->ClientBecameVisible();
  } else {
    DCHECK(context_visibility_);
    cache_controller->ClientBecameNotVisible(std::move(context_visibility_));
  }
}

void GLRenderer::ReleaseRenderPassTextures() {
  render_pass_textures_.clear();
}

void GLRenderer::DiscardPixels() {
  if (!use_discard_framebuffer_)
    return;
  bool using_default_framebuffer =
      !current_framebuffer_lock_ &&
      output_surface_->capabilities().uses_default_gl_framebuffer;
  GLenum attachments[] = {static_cast<GLenum>(
      using_default_framebuffer ? GL_COLOR_EXT : GL_COLOR_ATTACHMENT0_EXT)};
  gl_->DiscardFramebufferEXT(GL_FRAMEBUFFER, arraysize(attachments),
                             attachments);
}

void GLRenderer::PrepareSurfaceForPass(
    SurfaceInitializationMode initialization_mode,
    const gfx::Rect& render_pass_scissor) {
  SetViewport();

  switch (initialization_mode) {
    case SURFACE_INITIALIZATION_MODE_PRESERVE:
      EnsureScissorTestDisabled();
      return;
    case SURFACE_INITIALIZATION_MODE_FULL_SURFACE_CLEAR:
      EnsureScissorTestDisabled();
      DiscardPixels();
      ClearFramebuffer();
      break;
    case SURFACE_INITIALIZATION_MODE_SCISSORED_CLEAR:
      SetScissorTestRect(render_pass_scissor);
      ClearFramebuffer();
      break;
  }
}

void GLRenderer::ClearFramebuffer() {
  // On DEBUG builds, opaque render passes are cleared to blue to easily see
  // regions that were not drawn on the screen.
  if (current_frame()->current_render_pass->has_transparent_background)
    gl_->ClearColor(0, 0, 0, 0);
  else
    gl_->ClearColor(0, 0, 1, 1);

  gl_->ClearStencil(0);

  bool always_clear = overdraw_feedback_;
#ifndef NDEBUG
  always_clear = true;
#endif
  if (always_clear ||
      current_frame()->current_render_pass->has_transparent_background) {
    GLbitfield clear_bits = GL_COLOR_BUFFER_BIT;
    if (always_clear)
      clear_bits |= GL_STENCIL_BUFFER_BIT;
    gl_->Clear(clear_bits);
  }
}

void GLRenderer::BeginDrawingFrame() {
  TRACE_EVENT0("cc", "GLRenderer::BeginDrawingFrame");

  scoped_refptr<cc::ResourceProvider::Fence> read_lock_fence;
  if (use_sync_query_) {
    // Block until oldest sync query has passed if the number of pending queries
    // ever reach kMaxPendingSyncQueries.
    if (pending_sync_queries_.size() >= kMaxPendingSyncQueries) {
      LOG(ERROR) << "Reached limit of pending sync queries.";

      pending_sync_queries_.front()->Wait();
      DCHECK(!pending_sync_queries_.front()->IsPending());
    }

    while (!pending_sync_queries_.empty()) {
      if (pending_sync_queries_.front()->IsPending())
        break;

      available_sync_queries_.push_back(cc::PopFront(&pending_sync_queries_));
    }

    current_sync_query_ = available_sync_queries_.empty()
                              ? base::MakeUnique<SyncQuery>(gl_)
                              : cc::PopFront(&available_sync_queries_);

    read_lock_fence = current_sync_query_->Begin();
  } else {
    read_lock_fence =
        make_scoped_refptr(new cc::ResourceProvider::SynchronousFence(gl_));
  }
  resource_provider_->SetReadLockFence(read_lock_fence.get());

  // Insert WaitSyncTokenCHROMIUM on quad resources prior to drawing the frame,
  // so that drawing can proceed without GL context switching interruptions.
  cc::ResourceProvider* resource_provider = resource_provider_;
  for (const auto& pass : *current_frame()->render_passes_in_draw_order) {
    for (auto* quad : pass->quad_list) {
      for (ResourceId resource_id : quad->resources)
        resource_provider->WaitSyncTokenIfNeeded(resource_id);
    }
  }

  // TODO(enne): Do we need to reinitialize all of this state per frame?
  ReinitializeGLState();

  num_triangles_drawn_ = 0;
}

void GLRenderer::DoDrawQuad(const cc::DrawQuad* quad,
                            const gfx::QuadF* clip_region) {
  DCHECK(quad->rect.Contains(quad->visible_rect));
  if (quad->material != cc::DrawQuad::TEXTURE_CONTENT) {
    FlushTextureQuadCache(SHARED_BINDING);
  }

  switch (quad->material) {
    case cc::DrawQuad::INVALID:
      NOTREACHED();
      break;
    case cc::DrawQuad::DEBUG_BORDER:
      DrawDebugBorderQuad(cc::DebugBorderDrawQuad::MaterialCast(quad));
      break;
    case cc::DrawQuad::PICTURE_CONTENT:
      // PictureDrawQuad should only be used for resourceless software draws.
      NOTREACHED();
      break;
    case cc::DrawQuad::RENDER_PASS:
      DrawRenderPassQuad(cc::RenderPassDrawQuad::MaterialCast(quad),
                         clip_region);
      break;
    case cc::DrawQuad::SOLID_COLOR:
      DrawSolidColorQuad(cc::SolidColorDrawQuad::MaterialCast(quad),
                         clip_region);
      break;
    case cc::DrawQuad::STREAM_VIDEO_CONTENT:
      DrawStreamVideoQuad(cc::StreamVideoDrawQuad::MaterialCast(quad),
                          clip_region);
      break;
    case cc::DrawQuad::SURFACE_CONTENT:
      // Surface content should be fully resolved to other quad types before
      // reaching a direct renderer.
      NOTREACHED();
      break;
    case cc::DrawQuad::TEXTURE_CONTENT:
      EnqueueTextureQuad(cc::TextureDrawQuad::MaterialCast(quad), clip_region);
      break;
    case cc::DrawQuad::TILED_CONTENT:
      DrawTileQuad(cc::TileDrawQuad::MaterialCast(quad), clip_region);
      break;
    case cc::DrawQuad::YUV_VIDEO_CONTENT:
      DrawYUVVideoQuad(cc::YUVVideoDrawQuad::MaterialCast(quad), clip_region);
      break;
  }
}

// This function does not handle 3D sorting right now, since the debug border
// quads are just drawn as their original quads and not in split pieces. This
// results in some debug border quads drawing over foreground quads.
void GLRenderer::DrawDebugBorderQuad(const cc::DebugBorderDrawQuad* quad) {
  SetBlendEnabled(quad->ShouldDrawWithBlending());

  SetUseProgram(ProgramKey::DebugBorder(), gfx::ColorSpace::CreateSRGB());

  // Use the full quad_rect for debug quads to not move the edges based on
  // partial swaps.
  gfx::Rect layer_rect = quad->rect;
  gfx::Transform render_matrix;
  QuadRectTransform(&render_matrix,
                    quad->shared_quad_state->quad_to_target_transform,
                    gfx::RectF(layer_rect));
  SetShaderMatrix(current_frame()->projection_matrix * render_matrix);
  SetShaderColor(quad->color, 1.f);

  gl_->LineWidth(quad->width);

  // The indices for the line are stored in the same array as the triangle
  // indices.
  gl_->DrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_SHORT, 0);
}

static sk_sp<SkImage> WrapTexture(
    const cc::ResourceProvider::ScopedReadLockGL& lock,
    GrContext* context,
    bool flip_texture) {
  // Wrap a given texture in a Ganesh backend texture.
  GrGLTextureInfo texture_info;
  texture_info.fTarget = lock.target();
  texture_info.fID = lock.texture_id();
  GrBackendTexture backend_texture(lock.size().width(), lock.size().height(),
                                   kSkia8888_GrPixelConfig, texture_info);
  GrSurfaceOrigin origin =
      flip_texture ? kBottomLeft_GrSurfaceOrigin : kTopLeft_GrSurfaceOrigin;

  return SkImage::MakeFromTexture(context, backend_texture, origin,
                                  kPremul_SkAlphaType, nullptr);
}

static sk_sp<SkImage> ApplyImageFilter(
    std::unique_ptr<GLRenderer::ScopedUseGrContext> use_gr_context,
    const gfx::RectF& src_rect,
    const gfx::RectF& dst_rect,
    const gfx::Vector2dF& scale,
    sk_sp<SkImageFilter> filter,
    const cc::ResourceProvider::ScopedReadLockGL& source_texture_lock,
    SkIPoint* offset,
    SkIRect* subset,
    bool flip_texture,
    const gfx::PointF& origin) {
  if (!filter || !use_gr_context)
    return nullptr;

  sk_sp<SkImage> src_image =
      WrapTexture(source_texture_lock, use_gr_context->context(), flip_texture);

  if (!src_image) {
    TRACE_EVENT_INSTANT0("cc",
                         "ApplyImageFilter wrap background texture failed",
                         TRACE_EVENT_SCOPE_THREAD);
    return nullptr;
  }

  // Big filters can sometimes fallback to CPU. Therefore, we need
  // to disable subnormal floats for performance and security reasons.
  cc::ScopedSubnormalFloatDisabler disabler;
  SkMatrix local_matrix;
  local_matrix.setTranslate(origin.x(), origin.y());
  local_matrix.postScale(scale.x(), scale.y());
  local_matrix.postTranslate(-src_rect.x(), -src_rect.y());

  SkIRect clip_bounds = gfx::RectFToSkRect(dst_rect).roundOut();
  clip_bounds.offset(-src_rect.x(), -src_rect.y());
  filter = filter->makeWithLocalMatrix(local_matrix);
  SkIRect in_subset = SkIRect::MakeWH(src_rect.width(), src_rect.height());
  sk_sp<SkImage> image = src_image->makeWithFilter(filter.get(), in_subset,
                                                   clip_bounds, subset, offset);

  if (!image || !image->isTextureBacked()) {
    return nullptr;
  }

  // Force a flush of the Skia pipeline before we switch back to the compositor
  // context.
  image->getTextureHandle(true);
  CHECK(image->isTextureBacked());
  return image;
}

bool GLRenderer::CanApplyBlendModeUsingBlendFunc(SkBlendMode blend_mode) {
  return use_blend_equation_advanced_ || blend_mode == SkBlendMode::kSrcOver ||
         blend_mode == SkBlendMode::kDstIn ||
         blend_mode == SkBlendMode::kScreen;
}

void GLRenderer::ApplyBlendModeUsingBlendFunc(SkBlendMode blend_mode) {
  // Any modes set here must be reset in RestoreBlendFuncToDefault
  if (blend_mode == SkBlendMode::kSrcOver) {
    // Left no-op intentionally.
  } else if (blend_mode == SkBlendMode::kDstIn) {
    gl_->BlendFunc(GL_ZERO, GL_SRC_ALPHA);
  } else if (blend_mode == SkBlendMode::kDstOut) {
    gl_->BlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
  } else if (blend_mode == SkBlendMode::kScreen) {
    gl_->BlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE);
  } else {
    DCHECK(use_blend_equation_advanced_);
    GLenum equation = GL_FUNC_ADD;
    switch (blend_mode) {
      case SkBlendMode::kScreen:
        equation = GL_SCREEN_KHR;
        break;
      case SkBlendMode::kOverlay:
        equation = GL_OVERLAY_KHR;
        break;
      case SkBlendMode::kDarken:
        equation = GL_DARKEN_KHR;
        break;
      case SkBlendMode::kLighten:
        equation = GL_LIGHTEN_KHR;
        break;
      case SkBlendMode::kColorDodge:
        equation = GL_COLORDODGE_KHR;
        break;
      case SkBlendMode::kColorBurn:
        equation = GL_COLORBURN_KHR;
        break;
      case SkBlendMode::kHardLight:
        equation = GL_HARDLIGHT_KHR;
        break;
      case SkBlendMode::kSoftLight:
        equation = GL_SOFTLIGHT_KHR;
        break;
      case SkBlendMode::kDifference:
        equation = GL_DIFFERENCE_KHR;
        break;
      case SkBlendMode::kExclusion:
        equation = GL_EXCLUSION_KHR;
        break;
      case SkBlendMode::kMultiply:
        equation = GL_MULTIPLY_KHR;
        break;
      case SkBlendMode::kHue:
        equation = GL_HSL_HUE_KHR;
        break;
      case SkBlendMode::kSaturation:
        equation = GL_HSL_SATURATION_KHR;
        break;
      case SkBlendMode::kColor:
        equation = GL_HSL_COLOR_KHR;
        break;
      case SkBlendMode::kLuminosity:
        equation = GL_HSL_LUMINOSITY_KHR;
        break;
      default:
        NOTREACHED() << "Unexpected blend mode: SkBlendMode::k"
                     << SkBlendMode_Name(blend_mode);
        return;
    }
    gl_->BlendEquation(equation);
  }
}

void GLRenderer::RestoreBlendFuncToDefault(SkBlendMode blend_mode) {
  switch (blend_mode) {
    case SkBlendMode::kSrcOver:
      break;
    case SkBlendMode::kDstIn:
    case SkBlendMode::kDstOut:
    case SkBlendMode::kScreen:
      gl_->BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      break;
    default:
      DCHECK(use_blend_equation_advanced_);
      gl_->BlendEquation(GL_FUNC_ADD);
  }
}

bool GLRenderer::ShouldApplyBackgroundFilters(
    const cc::RenderPassDrawQuad* quad,
    const cc::FilterOperations* background_filters) {
  if (!background_filters)
    return false;
  DCHECK(!background_filters->IsEmpty());

  // TODO(hendrikw): Look into allowing background filters to see pixels from
  // other render targets.  See crbug.com/314867.

  return true;
}

// This takes a gfx::Rect and a clip region quad in the same space,
// and returns a quad with the same proportions in the space -0.5->0.5.
bool GetScaledRegion(const gfx::Rect& rect,
                     const gfx::QuadF* clip,
                     gfx::QuadF* scaled_region) {
  if (!clip)
    return false;

  gfx::PointF p1(((clip->p1().x() - rect.x()) / rect.width()) - 0.5f,
                 ((clip->p1().y() - rect.y()) / rect.height()) - 0.5f);
  gfx::PointF p2(((clip->p2().x() - rect.x()) / rect.width()) - 0.5f,
                 ((clip->p2().y() - rect.y()) / rect.height()) - 0.5f);
  gfx::PointF p3(((clip->p3().x() - rect.x()) / rect.width()) - 0.5f,
                 ((clip->p3().y() - rect.y()) / rect.height()) - 0.5f);
  gfx::PointF p4(((clip->p4().x() - rect.x()) / rect.width()) - 0.5f,
                 ((clip->p4().y() - rect.y()) / rect.height()) - 0.5f);
  *scaled_region = gfx::QuadF(p1, p2, p3, p4);
  return true;
}

// This takes a gfx::Rect and a clip region quad in the same space,
// and returns the proportional uv's in the space 0->1.
bool GetScaledUVs(const gfx::Rect& rect, const gfx::QuadF* clip, float uvs[8]) {
  if (!clip)
    return false;

  uvs[0] = ((clip->p1().x() - rect.x()) / rect.width());
  uvs[1] = ((clip->p1().y() - rect.y()) / rect.height());
  uvs[2] = ((clip->p2().x() - rect.x()) / rect.width());
  uvs[3] = ((clip->p2().y() - rect.y()) / rect.height());
  uvs[4] = ((clip->p3().x() - rect.x()) / rect.width());
  uvs[5] = ((clip->p3().y() - rect.y()) / rect.height());
  uvs[6] = ((clip->p4().x() - rect.x()) / rect.width());
  uvs[7] = ((clip->p4().y() - rect.y()) / rect.height());
  return true;
}

gfx::Rect GLRenderer::GetBackdropBoundingBoxForRenderPassQuad(
    const cc::RenderPassDrawQuad* quad,
    const gfx::Transform& contents_device_transform,
    const cc::FilterOperations* filters,
    const cc::FilterOperations* background_filters,
    const gfx::QuadF* clip_region,
    bool use_aa,
    gfx::Rect* unclipped_rect) {
  gfx::QuadF scaled_region;
  if (!GetScaledRegion(quad->rect, clip_region, &scaled_region)) {
    scaled_region = SharedGeometryQuad().BoundingBox();
  }

  gfx::Rect backdrop_rect = gfx::ToEnclosingRect(cc::MathUtil::MapClippedRect(
      contents_device_transform, scaled_region.BoundingBox()));

  if (ShouldApplyBackgroundFilters(quad, background_filters)) {
    SkMatrix matrix;
    matrix.setScale(quad->filters_scale.x(), quad->filters_scale.y());
    if (FlippedFramebuffer()) {
      // TODO(jbroman): This probably isn't the right way to account for this.
      // Probably some combination of current_frame()->projection_matrix,
      // current_frame()->window_matrix and contents_device_transform?
      matrix.postScale(1, -1);
    }
    backdrop_rect = background_filters->MapRectReverse(backdrop_rect, matrix);
  }

  if (!backdrop_rect.IsEmpty() && use_aa) {
    const int kOutsetForAntialiasing = 1;
    backdrop_rect.Inset(-kOutsetForAntialiasing, -kOutsetForAntialiasing);
  }

  if (filters) {
    DCHECK(!filters->IsEmpty());
    // If we have filters, grab an extra one-pixel border around the
    // background, so texture edge clamping gives us a transparent border
    // in case the filter expands the result.
    backdrop_rect.Inset(-1, -1, -1, -1);
  }

  *unclipped_rect = backdrop_rect;
  backdrop_rect.Intersect(MoveFromDrawToWindowSpace(
      current_frame()->current_render_pass->output_rect));
  return backdrop_rect;
}

std::unique_ptr<cc::ScopedResource> GLRenderer::GetBackdropTexture(
    const gfx::Rect& bounding_rect) {
  auto device_background_texture =
      base::MakeUnique<cc::ScopedResource>(resource_provider_);
  // CopyTexImage2D fails when called on a texture having immutable storage.
  device_background_texture->Allocate(
      bounding_rect.size(), cc::ResourceProvider::TEXTURE_HINT_DEFAULT,
      BackbufferFormat(), current_frame()->current_render_pass->color_space);
  {
    cc::ResourceProvider::ScopedWriteLockGL lock(
        resource_provider_, device_background_texture->id(), false);
    GetFramebufferTexture(lock.texture_id(), bounding_rect);
  }
  return device_background_texture;
}

sk_sp<SkImage> GLRenderer::ApplyBackgroundFilters(
    const cc::RenderPassDrawQuad* quad,
    const cc::FilterOperations& background_filters,
    cc::ScopedResource* background_texture,
    const gfx::RectF& rect,
    const gfx::RectF& unclipped_rect) {
  DCHECK(ShouldApplyBackgroundFilters(quad, &background_filters));
  auto use_gr_context = ScopedUseGrContext::Create(this);

  gfx::Vector2dF clipping_offset =
      (rect.top_right() - unclipped_rect.top_right()) +
      (rect.bottom_left() - unclipped_rect.bottom_left());
  sk_sp<SkImageFilter> filter = cc::RenderSurfaceFilters::BuildImageFilter(
      background_filters, gfx::SizeF(background_texture->size()),
      clipping_offset);

  // TODO(senorblanco): background filters should be moved to the
  // makeWithFilter fast-path, and go back to calling ApplyImageFilter().
  // See http://crbug.com/613233.
  if (!filter || !use_gr_context)
    return nullptr;

  cc::ResourceProvider::ScopedReadLockGL lock(resource_provider_,
                                              background_texture->id());

  bool flip_texture = true;
  sk_sp<SkImage> src_image =
      WrapTexture(lock, use_gr_context->context(), flip_texture);
  if (!src_image) {
    TRACE_EVENT_INSTANT0(
        "cc", "ApplyBackgroundFilters wrap background texture failed",
        TRACE_EVENT_SCOPE_THREAD);
    return nullptr;
  }

  // Create surface to draw into.
  SkImageInfo dst_info =
      SkImageInfo::MakeN32Premul(rect.width(), rect.height());
  sk_sp<SkSurface> surface = SkSurface::MakeRenderTarget(
      use_gr_context->context(), SkBudgeted::kYes, dst_info);
  if (!surface) {
    TRACE_EVENT_INSTANT0("cc",
                         "ApplyBackgroundFilters surface allocation failed",
                         TRACE_EVENT_SCOPE_THREAD);
    return nullptr;
  }

  // Big filters can sometimes fallback to CPU. Therefore, we need
  // to disable subnormal floats for performance and security reasons.
  cc::ScopedSubnormalFloatDisabler disabler;
  SkMatrix local_matrix;
  local_matrix.setScale(quad->filters_scale.x(), quad->filters_scale.y());

  SkPaint paint;
  paint.setImageFilter(filter->makeWithLocalMatrix(local_matrix));
  surface->getCanvas()->translate(-rect.x(), -rect.y());
  surface->getCanvas()->drawImage(src_image, rect.x(), rect.y(), &paint);
  // Flush the drawing before source texture read lock goes out of scope.
  // Skia API does not guarantee that when the SkImage goes out of scope,
  // its externally referenced resources would force the rendering to be
  // flushed.
  surface->getCanvas()->flush();
  sk_sp<SkImage> image = surface->makeImageSnapshot();
  if (!image || !image->isTextureBacked()) {
    return nullptr;
  }

  return image;
}

// Map device space quad to local space. Device_transform has no 3d
// component since it was flattened, so we don't need to project.  We should
// have already checked that the transform was uninvertible before this call.
gfx::QuadF MapQuadToLocalSpace(const gfx::Transform& device_transform,
                               const gfx::QuadF& device_quad) {
  gfx::Transform inverse_device_transform(gfx::Transform::kSkipInitialization);
  DCHECK(device_transform.IsInvertible());
  bool did_invert = device_transform.GetInverse(&inverse_device_transform);
  DCHECK(did_invert);
  bool clipped = false;
  gfx::QuadF local_quad =
      cc::MathUtil::MapQuad(inverse_device_transform, device_quad, &clipped);
  // We should not DCHECK(!clipped) here, because anti-aliasing inflation may
  // cause device_quad to become clipped. To our knowledge this scenario does
  // not need to be handled differently than the unclipped case.
  return local_quad;
}

const cc::TileDrawQuad* GLRenderer::CanPassBeDrawnDirectly(
    const cc::RenderPass* pass) {
  // Can only collapse a single tile quad.
  if (pass->quad_list.size() != 1)
    return nullptr;
  // If we need copy requests, then render pass has to exist.
  if (!pass->copy_requests.empty())
    return nullptr;

  const cc::DrawQuad* quad = *pass->quad_list.BackToFrontBegin();
  // Hack: this could be supported by concatenating transforms, but
  // in practice if there is one quad, it is at the origin of the render pass
  // and has the same size as the pass.
  if (!quad->shared_quad_state->quad_to_target_transform.IsIdentity() ||
      quad->rect != pass->output_rect)
    return nullptr;
  // The quad is expected to be the entire layer so that AA edges are correct.
  if (quad->shared_quad_state->quad_layer_rect != quad->rect)
    return nullptr;
  if (quad->material != cc::DrawQuad::TILED_CONTENT)
    return nullptr;

  // TODO(chrishtr): support could be added for opacity, but care needs
  // to be taken to make sure it is correct w.r.t. non-commutative filters etc.
  if (quad->shared_quad_state->opacity != 1.0f)
    return nullptr;

  const cc::TileDrawQuad* tile_quad = cc::TileDrawQuad::MaterialCast(quad);
  // Hack: this could be supported by passing in a subrectangle to draw
  // render pass, although in practice if there is only one quad there
  // will be no border texels on the input.
  if (tile_quad->tex_coord_rect != gfx::RectF(tile_quad->rect))
    return nullptr;
  // Tile quad features not supported in render pass shaders.
  if (tile_quad->swizzle_contents || tile_quad->nearest_neighbor)
    return nullptr;
  // BUG=skia:3868, Skia currently doesn't support texture rectangle inputs.
  // See also the DCHECKs about GL_TEXTURE_2D in DrawRenderPassQuad.
  GLenum target =
      resource_provider_->GetResourceTextureTarget(tile_quad->resource_id());
  if (target != GL_TEXTURE_2D)
    return nullptr;
#if defined(OS_MACOSX)
  // On Macs, this path can sometimes lead to all black output.
  // TODO(enne): investigate this and remove this hack.
  return nullptr;
#endif

  return tile_quad;
}

void GLRenderer::DrawRenderPassQuad(const cc::RenderPassDrawQuad* quad,
                                    const gfx::QuadF* clip_region) {
  auto bypass = render_pass_bypass_quads_.find(quad->render_pass_id);
  DrawRenderPassDrawQuadParams params;
  params.quad = quad;
  params.clip_region = clip_region;
  params.window_matrix = current_frame()->window_matrix;
  params.projection_matrix = current_frame()->projection_matrix;
  params.tex_coord_rect = quad->tex_coord_rect;
  if (bypass != render_pass_bypass_quads_.end()) {
    cc::TileDrawQuad* tile_quad = &bypass->second;
    // RGBA_8888 and the gfx::ColorSpace() here are arbitrary and unused.
    cc::Resource tile_resource(tile_quad->resource_id(),
                               tile_quad->texture_size,
                               ResourceFormat::RGBA_8888, gfx::ColorSpace());
    // The projection matrix used by GLRenderer has a flip.  As tile texture
    // inputs are oriented opposite to framebuffer outputs, don't flip via
    // texture coords and let the projection matrix naturallyd o it.
    params.flip_texture = false;
    params.contents_texture = &tile_resource;
    DrawRenderPassQuadInternal(&params);
  } else {
    cc::ScopedResource* contents_texture =
        render_pass_textures_[quad->render_pass_id].get();
    DCHECK(contents_texture);
    DCHECK(contents_texture->id());
    // See above comments about texture flipping.  When the input is a
    // render pass, it needs to an extra flip to be oriented correctly.
    params.flip_texture = true;
    params.contents_texture = contents_texture;
    DrawRenderPassQuadInternal(&params);
  }
}

void GLRenderer::DrawRenderPassQuadInternal(
    DrawRenderPassDrawQuadParams* params) {
  params->quad_to_target_transform =
      params->quad->shared_quad_state->quad_to_target_transform;
  if (!InitializeRPDQParameters(params))
    return;
  UpdateRPDQShadersForBlending(params);
  if (!UpdateRPDQWithSkiaFilters(params))
    return;
  UseRenderPass(current_frame()->current_render_pass);
  SetViewport();
  UpdateRPDQTexturesForSampling(params);
  UpdateRPDQBlendMode(params);
  ChooseRPDQProgram(params);
  UpdateRPDQUniforms(params);
  DrawRPDQ(*params);
}

bool GLRenderer::InitializeRPDQParameters(
    DrawRenderPassDrawQuadParams* params) {
  const cc::RenderPassDrawQuad* quad = params->quad;
  SkMatrix local_matrix;
  local_matrix.setTranslate(quad->filters_origin.x(), quad->filters_origin.y());
  local_matrix.postScale(quad->filters_scale.x(), quad->filters_scale.y());
  params->filters = FiltersForPass(quad->render_pass_id);
  params->background_filters = BackgroundFiltersForPass(quad->render_pass_id);
  gfx::Rect dst_rect = params->filters
                           ? params->filters->MapRect(quad->rect, local_matrix)
                           : quad->rect;
  params->dst_rect.SetRect(static_cast<float>(dst_rect.x()),
                           static_cast<float>(dst_rect.y()),
                           static_cast<float>(dst_rect.width()),
                           static_cast<float>(dst_rect.height()));
  gfx::Transform quad_rect_matrix;
  gfx::Rect quad_layer_rect(quad->shared_quad_state->quad_layer_rect);
  if (params->filters)
    quad_layer_rect = params->filters->MapRect(quad_layer_rect, local_matrix);
  QuadRectTransform(&quad_rect_matrix, params->quad_to_target_transform,
                    gfx::RectF(quad_layer_rect));
  params->contents_device_transform =
      params->window_matrix * params->projection_matrix * quad_rect_matrix;
  params->contents_device_transform.FlattenTo2d();

  // Can only draw surface if device matrix is invertible.
  if (!params->contents_device_transform.IsInvertible())
    return false;

  // TODO(sunxd): unify the anti-aliasing logic of RPDQ and cc::TileDrawQuad.
  params->surface_quad = SharedGeometryQuad();
  gfx::QuadF device_layer_quad;
  if (settings_->allow_antialiasing && quad->IsEdge()) {
    bool clipped = false;
    device_layer_quad = cc::MathUtil::MapQuad(params->contents_device_transform,
                                              params->surface_quad, &clipped);
    params->use_aa = ShouldAntialiasQuad(device_layer_quad, clipped,
                                         settings_->force_antialiasing);
  }

  const gfx::QuadF* aa_quad = params->use_aa ? &device_layer_quad : nullptr;
  SetupRenderPassQuadForClippingAndAntialiasing(
      params->contents_device_transform, quad, aa_quad, params->clip_region,
      &params->surface_quad, params->edge);

  return true;
}

void GLRenderer::UpdateRPDQShadersForBlending(
    DrawRenderPassDrawQuadParams* params) {
  const cc::RenderPassDrawQuad* quad = params->quad;
  SkBlendMode blend_mode = quad->shared_quad_state->blend_mode;
  params->use_shaders_for_blending =
      !CanApplyBlendModeUsingBlendFunc(blend_mode) ||
      ShouldApplyBackgroundFilters(quad, params->background_filters) ||
      settings_->force_blending_with_shaders;

  if (params->use_shaders_for_blending) {
    // Compute a bounding box around the pixels that will be visible through
    // the quad.
    gfx::Rect unclipped_rect;
    params->background_rect = GetBackdropBoundingBoxForRenderPassQuad(
        quad, params->contents_device_transform, params->filters,
        params->background_filters, params->clip_region, params->use_aa,
        &unclipped_rect);

    if (!params->background_rect.IsEmpty()) {
      // The pixels from the filtered background should completely replace the
      // current pixel values.
      if (blend_enabled())
        SetBlendEnabled(false);

      // Read the pixels in the bounding box into a buffer R.
      // This function allocates a texture, which should contribute to the
      // amount of memory used by render surfaces:
      // LayerTreeHost::CalculateMemoryForRenderSurfaces.
      params->background_texture = GetBackdropTexture(params->background_rect);

      if (ShouldApplyBackgroundFilters(quad, params->background_filters) &&
          params->background_texture) {
        // Apply the background filters to R, so that it is applied in the
        // pixels' coordinate space.
        params->background_image = ApplyBackgroundFilters(
            quad, *params->background_filters, params->background_texture.get(),
            gfx::RectF(params->background_rect), gfx::RectF(unclipped_rect));
        if (params->background_image) {
          params->background_image_id =
              skia::GrBackendObjectToGrGLTextureInfo(
                  params->background_image->getTextureHandle(true))
                  ->fID;
          DCHECK(params->background_image_id);
        }
      }
    }

    if (!params->background_texture) {
      // Something went wrong with reading the backdrop.
      DCHECK(!params->background_image_id);
      params->use_shaders_for_blending = false;
    } else if (params->background_image_id) {
      // Reset original background texture if there is not any mask
      if (!quad->mask_resource_id())
        params->background_texture.reset();
    } else if (CanApplyBlendModeUsingBlendFunc(blend_mode) &&
               ShouldApplyBackgroundFilters(quad, params->background_filters)) {
      // Something went wrong with applying background filters to the backdrop.
      params->use_shaders_for_blending = false;
      params->background_texture.reset();
    }
  }
  // Need original background texture for mask?
  params->mask_for_background =
      params->background_texture &&   // Have original background texture
      params->background_image_id &&  // Have filtered background texture
      quad->mask_resource_id();       // Have mask texture
  DCHECK_EQ(params->background_texture || params->background_image_id,
            params->use_shaders_for_blending);
}

bool GLRenderer::UpdateRPDQWithSkiaFilters(
    DrawRenderPassDrawQuadParams* params) {
  const cc::RenderPassDrawQuad* quad = params->quad;
  // Apply filters to the contents texture.
  if (params->filters) {
    DCHECK(!params->filters->IsEmpty());
    sk_sp<SkImageFilter> filter = cc::RenderSurfaceFilters::BuildImageFilter(
        *params->filters, gfx::SizeF(params->contents_texture->size()));
    if (filter) {
      SkColorFilter* colorfilter_rawptr = NULL;
      filter->asColorFilter(&colorfilter_rawptr);
      sk_sp<SkColorFilter> cf(colorfilter_rawptr);

      if (cf && cf->asColorMatrix(params->color_matrix)) {
        // We have a color matrix at the root of the filter DAG; apply it
        // locally in the compositor and process the rest of the DAG (if any)
        // in Skia.
        params->use_color_matrix = true;
        filter = sk_ref_sp(filter->getInput(0));
      }
      if (filter) {
        gfx::Rect clip_rect = quad->shared_quad_state->clip_rect;
        if (clip_rect.IsEmpty()) {
          clip_rect = current_draw_rect_;
        }
        gfx::Transform transform = params->quad_to_target_transform;
        gfx::QuadF clip_quad = gfx::QuadF(gfx::RectF(clip_rect));
        gfx::QuadF local_clip = MapQuadToLocalSpace(transform, clip_quad);
        params->dst_rect.Intersect(local_clip.BoundingBox());
        // If we've been fully clipped out (by crop rect or clipping), there's
        // nothing to draw.
        if (params->dst_rect.IsEmpty()) {
          return false;
        }
        SkIPoint offset;
        SkIRect subset;
        gfx::RectF src_rect(quad->rect);

        cc::ResourceProvider::ScopedReadLockGL prefilter_contents_texture_lock(
            resource_provider_, params->contents_texture->id());
        params->contents_color_space =
            prefilter_contents_texture_lock.color_space();
        params->filter_image = ApplyImageFilter(
            ScopedUseGrContext::Create(this), src_rect, params->dst_rect,
            quad->filters_scale, std::move(filter),
            prefilter_contents_texture_lock, &offset, &subset,
            params->flip_texture, quad->filters_origin);
        if (!params->filter_image)
          return false;
        params->dst_rect =
            gfx::RectF(src_rect.x() + offset.fX, src_rect.y() + offset.fY,
                       subset.width(), subset.height());
        params->src_offset.SetPoint(subset.x(), subset.y());
        gfx::RectF tex_rect = gfx::RectF(gfx::PointF(params->src_offset),
                                         params->dst_rect.size());
        params->tex_coord_rect = tex_rect;
      }
    }
  }
  return true;
}

void GLRenderer::UpdateRPDQTexturesForSampling(
    DrawRenderPassDrawQuadParams* params) {
  if (params->quad->mask_resource_id()) {
    params->mask_resource_lock.reset(new cc::ResourceProvider::ScopedSamplerGL(
        resource_provider_, params->quad->mask_resource_id(), GL_TEXTURE1,
        GL_LINEAR));
  }

  if (params->filter_image) {
    GrSurfaceOrigin origin;
    GLuint filter_image_id =
        skia::GrBackendObjectToGrGLTextureInfo(
            params->filter_image->getTextureHandle(true, &origin))
            ->fID;
    DCHECK(filter_image_id);
    DCHECK_EQ(GL_TEXTURE0, GetActiveTextureUnit(gl_));
    gl_->BindTexture(GL_TEXTURE_2D, filter_image_id);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // |params->contents_color_space| was populated when |params->filter_image|
    // was populated.
    params->source_needs_flip = kBottomLeft_GrSurfaceOrigin == origin;
  } else {
    params->contents_resource_lock =
        base::MakeUnique<cc::ResourceProvider::ScopedSamplerGL>(
            resource_provider_, params->contents_texture->id(), GL_LINEAR);
    DCHECK_EQ(static_cast<GLenum>(GL_TEXTURE_2D),
              params->contents_resource_lock->target());
    params->contents_color_space =
        params->contents_resource_lock->color_space();
    params->source_needs_flip = params->flip_texture;
  }
}

void GLRenderer::UpdateRPDQBlendMode(DrawRenderPassDrawQuadParams* params) {
  SkBlendMode blend_mode = params->quad->shared_quad_state->blend_mode;
  SetBlendEnabled(!params->use_shaders_for_blending &&
                  (params->quad->ShouldDrawWithBlending() ||
                   !IsDefaultBlendMode(blend_mode)));
  if (!params->use_shaders_for_blending) {
    if (!use_blend_equation_advanced_coherent_ && use_blend_equation_advanced_)
      gl_->BlendBarrierKHR();

    ApplyBlendModeUsingBlendFunc(blend_mode);
  }
}

void GLRenderer::ChooseRPDQProgram(DrawRenderPassDrawQuadParams* params) {
  TexCoordPrecision tex_coord_precision = TexCoordPrecisionRequired(
      gl_, &highp_threshold_cache_, settings_->highp_threshold_min,
      params->quad->shared_quad_state->visible_quad_layer_rect.bottom_right());

  BlendMode shader_blend_mode =
      params->use_shaders_for_blending
          ? BlendModeFromSkXfermode(params->quad->shared_quad_state->blend_mode)
          : BLEND_MODE_NONE;

  SamplerType sampler_type = SAMPLER_TYPE_2D;
  MaskMode mask_mode = NO_MASK;
  bool mask_for_background = params->mask_for_background;
  if (params->mask_resource_lock) {
    mask_mode = HAS_MASK;
    sampler_type =
        SamplerTypeFromTextureTarget(params->mask_resource_lock->target());
  }
  SetUseProgram(ProgramKey::RenderPass(
                    tex_coord_precision, sampler_type, shader_blend_mode,
                    params->use_aa ? USE_AA : NO_AA, mask_mode,
                    mask_for_background, params->use_color_matrix),
                params->contents_color_space);
}

void GLRenderer::UpdateRPDQUniforms(DrawRenderPassDrawQuadParams* params) {
  gfx::RectF tex_rect = params->tex_coord_rect;

  gfx::Size texture_size;
  if (params->filter_image) {
    texture_size.set_width(params->filter_image->width());
    texture_size.set_height(params->filter_image->height());
  } else {
    texture_size = params->contents_texture->size();
  }
  tex_rect.Scale(1.0f / texture_size.width(), 1.0f / texture_size.height());

  DCHECK(current_program_->vertex_tex_transform_location() != -1 ||
         IsContextLost());
  if (params->source_needs_flip) {
    // Flip the content vertically in the shader, as the cc::RenderPass input
    // texture is already oriented the same way as the framebuffer, but the
    // projection transform does a flip.
    gl_->Uniform4f(current_program_->vertex_tex_transform_location(),
                   tex_rect.x(), 1.0f - tex_rect.y(), tex_rect.width(),
                   -tex_rect.height());
  } else {
    // Tile textures are oriented opposite the framebuffer, so can use
    // the projection transform to do the flip.
    gl_->Uniform4f(current_program_->vertex_tex_transform_location(),
                   tex_rect.x(), tex_rect.y(), tex_rect.width(),
                   tex_rect.height());
  }

  GLint last_texture_unit = 0;
  if (current_program_->mask_sampler_location() != -1) {
    DCHECK(params->mask_resource_lock);
    DCHECK_NE(current_program_->mask_tex_coord_scale_location(), 1);
    DCHECK_NE(current_program_->mask_tex_coord_offset_location(), 1);
    gl_->Uniform1i(current_program_->mask_sampler_location(), 1);

    gfx::RectF mask_uv_rect = params->quad->mask_uv_rect;
    if (SamplerTypeFromTextureTarget(params->mask_resource_lock->target()) !=
        SAMPLER_TYPE_2D) {
      mask_uv_rect.Scale(params->quad->mask_texture_size.width(),
                         params->quad->mask_texture_size.height());
    }

    SkMatrix tex_to_mask = SkMatrix::MakeRectToRect(RectFToSkRect(tex_rect),
                                                    RectFToSkRect(mask_uv_rect),
                                                    SkMatrix::kFill_ScaleToFit);

    if (params->source_needs_flip) {
      // Mask textures are oriented vertically flipped relative to the
      // framebuffer and the cc::RenderPass contents texture, so we flip the tex
      // coords from the cc::RenderPass texture to find the mask texture coords.
      tex_to_mask.preTranslate(0, 1);
      tex_to_mask.preScale(1, -1);
    }

    gl_->Uniform2f(current_program_->mask_tex_coord_offset_location(),
                   tex_to_mask.getTranslateX(), tex_to_mask.getTranslateY());
    gl_->Uniform2f(current_program_->mask_tex_coord_scale_location(),
                   tex_to_mask.getScaleX(), tex_to_mask.getScaleY());
    last_texture_unit = 1;
  }

  if (current_program_->edge_location() != -1)
    gl_->Uniform3fv(current_program_->edge_location(), 8, params->edge);

  if (current_program_->color_matrix_location() != -1) {
    float matrix[16];
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j)
        matrix[i * 4 + j] = SkScalarToFloat(params->color_matrix[j * 5 + i]);
    }
    gl_->UniformMatrix4fv(current_program_->color_matrix_location(), 1, false,
                          matrix);
  }
  static const float kScale = 1.0f / 255.0f;
  if (current_program_->color_offset_location() != -1) {
    float offset[4];
    for (int i = 0; i < 4; ++i)
      offset[i] = SkScalarToFloat(params->color_matrix[i * 5 + 4]) * kScale;

    gl_->Uniform4fv(current_program_->color_offset_location(), 1, offset);
  }

  if (current_program_->backdrop_location() != -1) {
    DCHECK(params->background_texture || params->background_image_id);
    DCHECK_NE(current_program_->backdrop_location(), 0);
    DCHECK_NE(current_program_->backdrop_rect_location(), 0);

    gl_->Uniform1i(current_program_->backdrop_location(), ++last_texture_unit);

    gl_->Uniform4f(current_program_->backdrop_rect_location(),
                   params->background_rect.x(), params->background_rect.y(),
                   params->background_rect.width(),
                   params->background_rect.height());

    if (params->background_image_id) {
      gl_->ActiveTexture(GL_TEXTURE0 + last_texture_unit);
      gl_->BindTexture(GL_TEXTURE_2D, params->background_image_id);
      gl_->ActiveTexture(GL_TEXTURE0);
      if (params->mask_for_background)
        gl_->Uniform1i(current_program_->original_backdrop_location(),
                       ++last_texture_unit);
    }
    if (params->background_texture) {
      params->shader_background_sampler_lock =
          base::MakeUnique<cc::ResourceProvider::ScopedSamplerGL>(
              resource_provider_, params->background_texture->id(),
              GL_TEXTURE0 + last_texture_unit, GL_LINEAR);
      DCHECK_EQ(static_cast<GLenum>(GL_TEXTURE_2D),
                params->shader_background_sampler_lock->target());
    }
  }

  SetShaderOpacity(params->quad);
  SetShaderQuadF(params->surface_quad);
}

void GLRenderer::DrawRPDQ(const DrawRenderPassDrawQuadParams& params) {
  DrawQuadGeometry(params.projection_matrix, params.quad_to_target_transform,
                   params.dst_rect);

  // Flush the compositor context before the filter bitmap goes out of
  // scope, so the draw gets processed before the filter texture gets deleted.
  if (params.filter_image)
    gl_->Flush();

  if (!params.use_shaders_for_blending)
    RestoreBlendFuncToDefault(params.quad->shared_quad_state->blend_mode);
}

namespace {
// These functions determine if a quad, clipped by a clip_region contains
// the entire {top|bottom|left|right} edge.
bool is_top(const gfx::QuadF* clip_region, const cc::DrawQuad* quad) {
  if (!quad->IsTopEdge())
    return false;
  if (!clip_region)
    return true;

  return std::abs(clip_region->p1().y()) < kAntiAliasingEpsilon &&
         std::abs(clip_region->p2().y()) < kAntiAliasingEpsilon;
}

bool is_bottom(const gfx::QuadF* clip_region, const cc::DrawQuad* quad) {
  if (!quad->IsBottomEdge())
    return false;
  if (!clip_region)
    return true;

  return std::abs(clip_region->p3().y() -
                  quad->shared_quad_state->quad_layer_rect.height()) <
             kAntiAliasingEpsilon &&
         std::abs(clip_region->p4().y() -
                  quad->shared_quad_state->quad_layer_rect.height()) <
             kAntiAliasingEpsilon;
}

bool is_left(const gfx::QuadF* clip_region, const cc::DrawQuad* quad) {
  if (!quad->IsLeftEdge())
    return false;
  if (!clip_region)
    return true;

  return std::abs(clip_region->p1().x()) < kAntiAliasingEpsilon &&
         std::abs(clip_region->p4().x()) < kAntiAliasingEpsilon;
}

bool is_right(const gfx::QuadF* clip_region, const cc::DrawQuad* quad) {
  if (!quad->IsRightEdge())
    return false;
  if (!clip_region)
    return true;

  return std::abs(clip_region->p2().x() -
                  quad->shared_quad_state->quad_layer_rect.width()) <
             kAntiAliasingEpsilon &&
         std::abs(clip_region->p3().x() -
                  quad->shared_quad_state->quad_layer_rect.width()) <
             kAntiAliasingEpsilon;
}
}  // anonymous namespace

static gfx::QuadF GetDeviceQuadWithAntialiasingOnExteriorEdges(
    const cc::LayerQuad& device_layer_edges,
    const gfx::Transform& device_transform,
    const gfx::QuadF& tile_quad,
    const gfx::QuadF* clip_region,
    const cc::DrawQuad* quad) {
  auto tile_rect = gfx::RectF(quad->visible_rect);

  gfx::PointF bottom_right = tile_quad.p3();
  gfx::PointF bottom_left = tile_quad.p4();
  gfx::PointF top_left = tile_quad.p1();
  gfx::PointF top_right = tile_quad.p2();
  bool clipped = false;

  // Map points to device space. We ignore |clipped|, since the result of
  // |MapPoint()| still produces a valid point to draw the quad with. When
  // clipped, the point will be outside of the viewport. See crbug.com/416367.
  bottom_right =
      cc::MathUtil::MapPoint(device_transform, bottom_right, &clipped);
  bottom_left = cc::MathUtil::MapPoint(device_transform, bottom_left, &clipped);
  top_left = cc::MathUtil::MapPoint(device_transform, top_left, &clipped);
  top_right = cc::MathUtil::MapPoint(device_transform, top_right, &clipped);

  cc::LayerQuad::Edge bottom_edge(bottom_right, bottom_left);
  cc::LayerQuad::Edge left_edge(bottom_left, top_left);
  cc::LayerQuad::Edge top_edge(top_left, top_right);
  cc::LayerQuad::Edge right_edge(top_right, bottom_right);

  // Only apply anti-aliasing to edges not clipped by culling or scissoring.
  // If an edge is degenerate we do not want to replace it with a "proper" edge
  // as that will cause the quad to possibly expand in strange ways.
  if (!top_edge.degenerate() && is_top(clip_region, quad) &&
      tile_rect.y() == quad->rect.y()) {
    top_edge = device_layer_edges.top();
  }
  if (!left_edge.degenerate() && is_left(clip_region, quad) &&
      tile_rect.x() == quad->rect.x()) {
    left_edge = device_layer_edges.left();
  }
  if (!right_edge.degenerate() && is_right(clip_region, quad) &&
      tile_rect.right() == quad->rect.right()) {
    right_edge = device_layer_edges.right();
  }
  if (!bottom_edge.degenerate() && is_bottom(clip_region, quad) &&
      tile_rect.bottom() == quad->rect.bottom()) {
    bottom_edge = device_layer_edges.bottom();
  }

  float sign = tile_quad.IsCounterClockwise() ? -1 : 1;
  bottom_edge.scale(sign);
  left_edge.scale(sign);
  top_edge.scale(sign);
  right_edge.scale(sign);

  // Create device space quad.
  return cc::LayerQuad(left_edge, top_edge, right_edge, bottom_edge).ToQuadF();
}

float GetTotalQuadError(const gfx::QuadF* clipped_quad,
                        const gfx::QuadF* ideal_rect) {
  return (clipped_quad->p1() - ideal_rect->p1()).LengthSquared() +
         (clipped_quad->p2() - ideal_rect->p2()).LengthSquared() +
         (clipped_quad->p3() - ideal_rect->p3()).LengthSquared() +
         (clipped_quad->p4() - ideal_rect->p4()).LengthSquared();
}

// Attempt to rotate the clipped quad until it lines up the most
// correctly. This is necessary because we check the edges of this
// quad against the expected left/right/top/bottom for anti-aliasing.
void AlignQuadToBoundingBox(gfx::QuadF* clipped_quad) {
  auto bounding_quad = gfx::QuadF(clipped_quad->BoundingBox());
  gfx::QuadF best_rotation = *clipped_quad;
  float least_error_amount = GetTotalQuadError(clipped_quad, &bounding_quad);
  for (size_t i = 1; i < 4; ++i) {
    clipped_quad->Realign(1);
    float new_error = GetTotalQuadError(clipped_quad, &bounding_quad);
    if (new_error < least_error_amount) {
      least_error_amount = new_error;
      best_rotation = *clipped_quad;
    }
  }
  *clipped_quad = best_rotation;
}

void InflateAntiAliasingDistances(const gfx::QuadF& quad,
                                  cc::LayerQuad* device_layer_edges,
                                  float edge[24]) {
  DCHECK(!quad.BoundingBox().IsEmpty());
  cc::LayerQuad device_layer_bounds(gfx::QuadF(quad.BoundingBox()));

  device_layer_edges->InflateAntiAliasingDistance();
  device_layer_edges->ToFloatArray(edge);

  device_layer_bounds.InflateAntiAliasingDistance();
  device_layer_bounds.ToFloatArray(&edge[12]);
}

// static
bool GLRenderer::ShouldAntialiasQuad(const gfx::QuadF& device_layer_quad,
                                     bool clipped,
                                     bool force_aa) {
  // AAing clipped quads is not supported by the code yet.
  if (clipped)
    return false;
  if (device_layer_quad.BoundingBox().IsEmpty())
    return false;
  if (force_aa)
    return true;

  bool is_axis_aligned_in_target = device_layer_quad.IsRectilinear();
  bool is_nearest_rect_within_epsilon =
      is_axis_aligned_in_target &&
      gfx::IsNearestRectWithinDistance(device_layer_quad.BoundingBox(),
                                       kAntiAliasingEpsilon);
  return !is_nearest_rect_within_epsilon;
}

// static
void GLRenderer::SetupQuadForClippingAndAntialiasing(
    const gfx::Transform& device_transform,
    const cc::DrawQuad* quad,
    const gfx::QuadF* aa_quad,
    const gfx::QuadF* clip_region,
    gfx::QuadF* local_quad,
    float edge[24]) {
  gfx::QuadF rotated_clip;
  const gfx::QuadF* local_clip_region = clip_region;
  if (local_clip_region) {
    rotated_clip = *clip_region;
    AlignQuadToBoundingBox(&rotated_clip);
    local_clip_region = &rotated_clip;
  }

  if (!aa_quad) {
    if (local_clip_region)
      *local_quad = *local_clip_region;
    return;
  }

  cc::LayerQuad device_layer_edges(*aa_quad);
  InflateAntiAliasingDistances(*aa_quad, &device_layer_edges, edge);

  // If we have a clip region then we are split, and therefore
  // by necessity, at least one of our edges is not an external
  // one.
  bool is_full_rect = quad->visible_rect == quad->rect;

  bool region_contains_all_outside_edges =
      is_full_rect &&
      (is_top(local_clip_region, quad) && is_left(local_clip_region, quad) &&
       is_bottom(local_clip_region, quad) && is_right(local_clip_region, quad));

  bool use_aa_on_all_four_edges =
      !local_clip_region && region_contains_all_outside_edges;

  gfx::QuadF device_quad;
  if (use_aa_on_all_four_edges) {
    device_quad = device_layer_edges.ToQuadF();
  } else {
    gfx::QuadF tile_quad(local_clip_region
                             ? *local_clip_region
                             : gfx::QuadF(gfx::RectF(quad->visible_rect)));
    device_quad = GetDeviceQuadWithAntialiasingOnExteriorEdges(
        device_layer_edges, device_transform, tile_quad, local_clip_region,
        quad);
  }

  *local_quad = MapQuadToLocalSpace(device_transform, device_quad);
}

// static
void GLRenderer::SetupRenderPassQuadForClippingAndAntialiasing(
    const gfx::Transform& device_transform,
    const cc::RenderPassDrawQuad* quad,
    const gfx::QuadF* aa_quad,
    const gfx::QuadF* clip_region,
    gfx::QuadF* local_quad,
    float edge[24]) {
  gfx::QuadF rotated_clip;
  const gfx::QuadF* local_clip_region = clip_region;
  if (local_clip_region) {
    rotated_clip = *clip_region;
    AlignQuadToBoundingBox(&rotated_clip);
    local_clip_region = &rotated_clip;
  }

  if (!aa_quad) {
    GetScaledRegion(quad->rect, local_clip_region, local_quad);
    return;
  }

  cc::LayerQuad device_layer_edges(*aa_quad);
  InflateAntiAliasingDistances(*aa_quad, &device_layer_edges, edge);

  gfx::QuadF device_quad;

  // Apply anti-aliasing only to the edges that are not being clipped
  if (local_clip_region) {
    gfx::QuadF tile_quad(gfx::RectF(quad->visible_rect));
    GetScaledRegion(quad->rect, local_clip_region, &tile_quad);
    device_quad = GetDeviceQuadWithAntialiasingOnExteriorEdges(
        device_layer_edges, device_transform, tile_quad, local_clip_region,
        quad);
  } else {
    device_quad = device_layer_edges.ToQuadF();
  }

  *local_quad = MapQuadToLocalSpace(device_transform, device_quad);
}

void GLRenderer::DrawSolidColorQuad(const cc::SolidColorDrawQuad* quad,
                                    const gfx::QuadF* clip_region) {
  gfx::Rect tile_rect = quad->visible_rect;

  SkColor color = quad->color;
  float opacity = quad->shared_quad_state->opacity;
  float alpha = (SkColorGetA(color) * (1.0f / 255.0f)) * opacity;

  // Early out if alpha is small enough that quad doesn't contribute to output.
  if (alpha < std::numeric_limits<float>::epsilon() &&
      quad->ShouldDrawWithBlending() &&
      quad->shared_quad_state->blend_mode == SkBlendMode::kSrcOver)
    return;

  gfx::Transform device_transform =
      current_frame()->window_matrix * current_frame()->projection_matrix *
      quad->shared_quad_state->quad_to_target_transform;
  device_transform.FlattenTo2d();
  if (!device_transform.IsInvertible())
    return;

  auto local_quad = gfx::QuadF(gfx::RectF(tile_rect));

  gfx::QuadF device_layer_quad;
  bool use_aa = false;
  bool allow_aa = settings_->allow_antialiasing &&
                  !quad->force_anti_aliasing_off && quad->IsEdge();

  if (allow_aa) {
    bool clipped = false;
    bool force_aa = false;
    device_layer_quad = cc::MathUtil::MapQuad(
        device_transform,
        gfx::QuadF(
            gfx::RectF(quad->shared_quad_state->visible_quad_layer_rect)),
        &clipped);
    use_aa = ShouldAntialiasQuad(device_layer_quad, clipped, force_aa);
  }

  float edge[24];
  const gfx::QuadF* aa_quad = use_aa ? &device_layer_quad : nullptr;
  SetupQuadForClippingAndAntialiasing(device_transform, quad, aa_quad,
                                      clip_region, &local_quad, edge);

  // TODO(ccameron): Solid color draw quads need to specify their implied
  // color space. Assume SRGB (which is wrong) for now.
  gfx::ColorSpace quad_color_space = gfx::ColorSpace::CreateSRGB();
  SetUseProgram(ProgramKey::SolidColor(use_aa ? USE_AA : NO_AA),
                quad_color_space);
  SetShaderColor(color, opacity);

  if (use_aa) {
    gl_->Uniform3fv(current_program_->edge_location(), 8, edge);
  }

  // Enable blending when the quad properties require it or if we decided
  // to use antialiasing.
  SetBlendEnabled(quad->ShouldDrawWithBlending() || use_aa);
  ApplyBlendModeUsingBlendFunc(quad->shared_quad_state->blend_mode);

  // Antialising requires a normalized quad, but this could lead to floating
  // point precision errors, so only normalize when antialising is on.
  if (use_aa) {
    // Normalize to tile_rect.
    local_quad.Scale(1.0f / tile_rect.width(), 1.0f / tile_rect.height());

    SetShaderQuadF(local_quad);

    // The transform and vertex data are used to figure out the extents that the
    // un-antialiased quad should have and which vertex this is and the float
    // quad passed in via uniform is the actual geometry that gets used to draw
    // it. This is why this centered rect is used and not the original
    // quad_rect.
    gfx::RectF centered_rect(
        gfx::PointF(-0.5f * tile_rect.width(), -0.5f * tile_rect.height()),
        gfx::SizeF(tile_rect.size()));
    DrawQuadGeometry(current_frame()->projection_matrix,
                     quad->shared_quad_state->quad_to_target_transform,
                     centered_rect);
  } else {
    PrepareGeometry(SHARED_BINDING);
    SetShaderQuadF(local_quad);
    SetShaderMatrix(current_frame()->projection_matrix *
                    quad->shared_quad_state->quad_to_target_transform);
    gl_->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    num_triangles_drawn_ += 2;
  }
  RestoreBlendFuncToDefault(quad->shared_quad_state->blend_mode);
}

void GLRenderer::DrawTileQuad(const cc::TileDrawQuad* quad,
                              const gfx::QuadF* clip_region) {
  DrawContentQuad(quad, quad->resource_id(), clip_region);
  // Draw the border if requested.
  if (gl_composited_overlay_candidate_quad_border_) {
    float gl_matrix[16];
    // Generate the transform matrix
    gfx::Transform quad_rect_matrix;
    QuadRectTransform(&quad_rect_matrix,
                      quad->shared_quad_state->quad_to_target_transform,
                      gfx::RectF(quad->rect));
    quad_rect_matrix = current_frame()->projection_matrix * quad_rect_matrix;
    ToGLMatrix(gl_matrix, quad_rect_matrix);

    DrawOverlayCandidateQuadBorder(gl_matrix);
  }
}

void GLRenderer::DrawContentQuad(const cc::ContentDrawQuadBase* quad,
                                 ResourceId resource_id,
                                 const gfx::QuadF* clip_region) {
  gfx::Transform device_transform =
      current_frame()->window_matrix * current_frame()->projection_matrix *
      quad->shared_quad_state->quad_to_target_transform;
  device_transform.FlattenTo2d();

  gfx::QuadF device_layer_quad;
  bool use_aa = false;
  bool allow_aa = settings_->allow_antialiasing && quad->IsEdge();
  if (allow_aa) {
    bool clipped = false;
    bool force_aa = false;
    device_layer_quad = cc::MathUtil::MapQuad(
        device_transform,
        gfx::QuadF(
            gfx::RectF(quad->shared_quad_state->visible_quad_layer_rect)),
        &clipped);
    use_aa = ShouldAntialiasQuad(device_layer_quad, clipped, force_aa);
  }

  // TODO(timav): simplify coordinate transformations in DrawContentQuadAA
  // similar to the way DrawContentQuadNoAA works and then consider
  // combining DrawContentQuadAA and DrawContentQuadNoAA into one method.
  if (use_aa)
    DrawContentQuadAA(quad, resource_id, device_transform, device_layer_quad,
                      clip_region);
  else
    DrawContentQuadNoAA(quad, resource_id, clip_region);
}

void GLRenderer::DrawContentQuadAA(const cc::ContentDrawQuadBase* quad,
                                   ResourceId resource_id,
                                   const gfx::Transform& device_transform,
                                   const gfx::QuadF& aa_quad,
                                   const gfx::QuadF* clip_region) {
  if (!device_transform.IsInvertible())
    return;

  gfx::Rect tile_rect = quad->visible_rect;

  gfx::RectF tex_coord_rect = cc::MathUtil::ScaleRectProportional(
      quad->tex_coord_rect, gfx::RectF(quad->rect), gfx::RectF(tile_rect));
  float tex_to_geom_scale_x = quad->rect.width() / quad->tex_coord_rect.width();
  float tex_to_geom_scale_y =
      quad->rect.height() / quad->tex_coord_rect.height();

  gfx::RectF clamp_geom_rect(tile_rect);
  gfx::RectF clamp_tex_rect(tex_coord_rect);
  // Clamp texture coordinates to avoid sampling outside the layer
  // by deflating the tile region half a texel or half a texel
  // minus epsilon for one pixel layers. The resulting clamp region
  // is mapped to the unit square by the vertex shader and mapped
  // back to normalized texture coordinates by the fragment shader
  // after being clamped to 0-1 range.
  float tex_clamp_x =
      std::min(0.5f, 0.5f * clamp_tex_rect.width() - kAntiAliasingEpsilon);
  float tex_clamp_y =
      std::min(0.5f, 0.5f * clamp_tex_rect.height() - kAntiAliasingEpsilon);
  float geom_clamp_x =
      std::min(tex_clamp_x * tex_to_geom_scale_x,
               0.5f * clamp_geom_rect.width() - kAntiAliasingEpsilon);
  float geom_clamp_y =
      std::min(tex_clamp_y * tex_to_geom_scale_y,
               0.5f * clamp_geom_rect.height() - kAntiAliasingEpsilon);
  clamp_geom_rect.Inset(geom_clamp_x, geom_clamp_y, geom_clamp_x, geom_clamp_y);
  clamp_tex_rect.Inset(tex_clamp_x, tex_clamp_y, tex_clamp_x, tex_clamp_y);

  // Map clamping rectangle to unit square.
  float vertex_tex_translate_x = -clamp_geom_rect.x() / clamp_geom_rect.width();
  float vertex_tex_translate_y =
      -clamp_geom_rect.y() / clamp_geom_rect.height();
  float vertex_tex_scale_x = tile_rect.width() / clamp_geom_rect.width();
  float vertex_tex_scale_y = tile_rect.height() / clamp_geom_rect.height();

  TexCoordPrecision tex_coord_precision = TexCoordPrecisionRequired(
      gl_, &highp_threshold_cache_, settings_->highp_threshold_min,
      quad->texture_size);

  auto local_quad = gfx::QuadF(gfx::RectF(tile_rect));
  float edge[24];
  SetupQuadForClippingAndAntialiasing(device_transform, quad, &aa_quad,
                                      clip_region, &local_quad, edge);
  cc::ResourceProvider::ScopedSamplerGL quad_resource_lock(
      resource_provider_, resource_id,
      quad->nearest_neighbor ? GL_NEAREST : GL_LINEAR);
  SamplerType sampler =
      SamplerTypeFromTextureTarget(quad_resource_lock.target());

  float fragment_tex_translate_x = clamp_tex_rect.x();
  float fragment_tex_translate_y = clamp_tex_rect.y();
  float fragment_tex_scale_x = clamp_tex_rect.width();
  float fragment_tex_scale_y = clamp_tex_rect.height();

  // Map to normalized texture coordinates.
  if (sampler != SAMPLER_TYPE_2D_RECT) {
    gfx::Size texture_size = quad->texture_size;
    DCHECK(!texture_size.IsEmpty());
    fragment_tex_translate_x /= texture_size.width();
    fragment_tex_translate_y /= texture_size.height();
    fragment_tex_scale_x /= texture_size.width();
    fragment_tex_scale_y /= texture_size.height();
  }

  SetUseProgram(
      ProgramKey::Tile(tex_coord_precision, sampler, USE_AA,
                       quad->swizzle_contents ? DO_SWIZZLE : NO_SWIZZLE, false),
      quad_resource_lock.color_space());

  gl_->Uniform3fv(current_program_->edge_location(), 8, edge);

  gl_->Uniform4f(current_program_->vertex_tex_transform_location(),
                 vertex_tex_translate_x, vertex_tex_translate_y,
                 vertex_tex_scale_x, vertex_tex_scale_y);
  gl_->Uniform4f(current_program_->fragment_tex_transform_location(),
                 fragment_tex_translate_x, fragment_tex_translate_y,
                 fragment_tex_scale_x, fragment_tex_scale_y);

  // Blending is required for antialiasing.
  SetBlendEnabled(true);

  // Normalize to tile_rect.
  local_quad.Scale(1.0f / tile_rect.width(), 1.0f / tile_rect.height());

  SetShaderOpacity(quad);
  SetShaderQuadF(local_quad);

  // The transform and vertex data are used to figure out the extents that the
  // un-antialiased quad should have and which vertex this is and the float
  // quad passed in via uniform is the actual geometry that gets used to draw
  // it. This is why this centered rect is used and not the original quad_rect.
  gfx::RectF centered_rect(
      gfx::PointF(-0.5f * tile_rect.width(), -0.5f * tile_rect.height()),
      gfx::SizeF(tile_rect.size()));
  DrawQuadGeometry(current_frame()->projection_matrix,
                   quad->shared_quad_state->quad_to_target_transform,
                   centered_rect);
}

void GLRenderer::DrawContentQuadNoAA(const cc::ContentDrawQuadBase* quad,
                                     ResourceId resource_id,
                                     const gfx::QuadF* clip_region) {
  gfx::RectF tex_coord_rect = cc::MathUtil::ScaleRectProportional(
      quad->tex_coord_rect, gfx::RectF(quad->rect),
      gfx::RectF(quad->visible_rect));
  float tex_to_geom_scale_x = quad->rect.width() / quad->tex_coord_rect.width();
  float tex_to_geom_scale_y =
      quad->rect.height() / quad->tex_coord_rect.height();

  bool scaled = (tex_to_geom_scale_x != 1.f || tex_to_geom_scale_y != 1.f);
  GLenum filter = (scaled || !quad->shared_quad_state->quad_to_target_transform
                                  .IsIdentityOrIntegerTranslation()) &&
                          !quad->nearest_neighbor
                      ? GL_LINEAR
                      : GL_NEAREST;

  cc::ResourceProvider::ScopedSamplerGL quad_resource_lock(resource_provider_,
                                                           resource_id, filter);
  SamplerType sampler =
      SamplerTypeFromTextureTarget(quad_resource_lock.target());

  float vertex_tex_translate_x = tex_coord_rect.x();
  float vertex_tex_translate_y = tex_coord_rect.y();
  float vertex_tex_scale_x = tex_coord_rect.width();
  float vertex_tex_scale_y = tex_coord_rect.height();

  // Map to normalized texture coordinates.
  if (sampler != SAMPLER_TYPE_2D_RECT) {
    gfx::Size texture_size = quad->texture_size;
    DCHECK(!texture_size.IsEmpty());
    vertex_tex_translate_x /= texture_size.width();
    vertex_tex_translate_y /= texture_size.height();
    vertex_tex_scale_x /= texture_size.width();
    vertex_tex_scale_y /= texture_size.height();
  }

  TexCoordPrecision tex_coord_precision = TexCoordPrecisionRequired(
      gl_, &highp_threshold_cache_, settings_->highp_threshold_min,
      quad->texture_size);

  SetUseProgram(
      ProgramKey::Tile(tex_coord_precision, sampler, NO_AA,
                       quad->swizzle_contents ? DO_SWIZZLE : NO_SWIZZLE,
                       !quad->ShouldDrawWithBlending()),
      quad_resource_lock.color_space());

  gl_->Uniform4f(current_program_->vertex_tex_transform_location(),
                 vertex_tex_translate_x, vertex_tex_translate_y,
                 vertex_tex_scale_x, vertex_tex_scale_y);

  SetBlendEnabled(quad->ShouldDrawWithBlending());

  SetShaderOpacity(quad);

  // Pass quad coordinates to the uniform in the same order as GeometryBinding
  // does, then vertices will match the texture mapping in the vertex buffer.
  // The method SetShaderQuadF() changes the order of vertices and so it's
  // not used here.
  auto tile_quad = gfx::QuadF(gfx::RectF(quad->visible_rect));
  float width = quad->visible_rect.width();
  float height = quad->visible_rect.height();
  auto top_left = gfx::PointF(quad->visible_rect.origin());
  if (clip_region) {
    tile_quad = *clip_region;
    float gl_uv[8] = {
        (tile_quad.p4().x() - top_left.x()) / width,
        (tile_quad.p4().y() - top_left.y()) / height,
        (tile_quad.p1().x() - top_left.x()) / width,
        (tile_quad.p1().y() - top_left.y()) / height,
        (tile_quad.p2().x() - top_left.x()) / width,
        (tile_quad.p2().y() - top_left.y()) / height,
        (tile_quad.p3().x() - top_left.x()) / width,
        (tile_quad.p3().y() - top_left.y()) / height,
    };
    PrepareGeometry(CLIPPED_BINDING);
    clipped_geometry_->InitializeCustomQuadWithUVs(
        gfx::QuadF(gfx::RectF(quad->visible_rect)), gl_uv);
  } else {
    PrepareGeometry(SHARED_BINDING);
  }
  float gl_quad[8] = {
      tile_quad.p4().x(), tile_quad.p4().y(), tile_quad.p1().x(),
      tile_quad.p1().y(), tile_quad.p2().x(), tile_quad.p2().y(),
      tile_quad.p3().x(), tile_quad.p3().y(),
  };
  gl_->Uniform2fv(current_program_->quad_location(), 4, gl_quad);

  SetShaderMatrix(current_frame()->projection_matrix *
                  quad->shared_quad_state->quad_to_target_transform);

  gl_->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
  num_triangles_drawn_ += 2;
}

void GLRenderer::DrawYUVVideoQuad(const cc::YUVVideoDrawQuad* quad,
                                  const gfx::QuadF* clip_region) {
  SetBlendEnabled(quad->ShouldDrawWithBlending());

  TexCoordPrecision tex_coord_precision = TexCoordPrecisionRequired(
      gl_, &highp_threshold_cache_, settings_->highp_threshold_min,
      quad->shared_quad_state->visible_quad_layer_rect.bottom_right());
  YUVAlphaTextureMode alpha_texture_mode = quad->a_plane_resource_id()
                                               ? YUV_HAS_ALPHA_TEXTURE
                                               : YUV_NO_ALPHA_TEXTURE;
  UVTextureMode uv_texture_mode =
      quad->v_plane_resource_id() == quad->u_plane_resource_id()
          ? UV_TEXTURE_MODE_UV
          : UV_TEXTURE_MODE_U_V;

  // TODO(ccameron): There are currently three sources of the color space: the
  // resource, quad->color_space, and quad->video_color_space. Remove two of
  // them.
  gfx::ColorSpace src_color_space = quad->video_color_space;
  gfx::ColorSpace dst_color_space =
      current_frame()->current_render_pass->color_space;
  if (!base::FeatureList::IsEnabled(media::kVideoColorManagement) &&
      !settings_->enable_color_correct_rendering) {
    dst_color_space = gfx::ColorSpace();
    switch (quad->color_space) {
      case cc::YUVVideoDrawQuad::REC_601:
        src_color_space = gfx::ColorSpace::CreateREC601();
        break;
      case cc::YUVVideoDrawQuad::REC_709:
        src_color_space = gfx::ColorSpace::CreateREC709();
        break;
      case cc::YUVVideoDrawQuad::JPEG:
        src_color_space = gfx::ColorSpace::CreateJpeg();
        break;
    }
  }
  // Invalid or unspecified color spaces should be treated as REC709.
  if (!src_color_space.IsValid())
    src_color_space = gfx::ColorSpace::CreateREC709();

  // The source color space should never be RGB.
  DCHECK_NE(src_color_space, src_color_space.GetAsFullRangeRGB());

  cc::ResourceProvider::ScopedSamplerGL y_plane_lock(
      resource_provider_, quad->y_plane_resource_id(), GL_TEXTURE1, GL_LINEAR);
  if (base::FeatureList::IsEnabled(media::kVideoColorManagement))
    DCHECK_EQ(src_color_space, y_plane_lock.color_space());
  cc::ResourceProvider::ScopedSamplerGL u_plane_lock(
      resource_provider_, quad->u_plane_resource_id(), GL_TEXTURE2, GL_LINEAR);
  DCHECK_EQ(y_plane_lock.target(), u_plane_lock.target());
  DCHECK_EQ(y_plane_lock.color_space(), u_plane_lock.color_space());
  // TODO(jbauman): Use base::Optional when available.
  std::unique_ptr<cc::ResourceProvider::ScopedSamplerGL> v_plane_lock;

  if (uv_texture_mode == UV_TEXTURE_MODE_U_V) {
    v_plane_lock.reset(new cc::ResourceProvider::ScopedSamplerGL(
        resource_provider_, quad->v_plane_resource_id(), GL_TEXTURE3,
        GL_LINEAR));
    DCHECK_EQ(y_plane_lock.target(), v_plane_lock->target());
    DCHECK_EQ(y_plane_lock.color_space(), v_plane_lock->color_space());
  }
  std::unique_ptr<cc::ResourceProvider::ScopedSamplerGL> a_plane_lock;
  if (alpha_texture_mode == YUV_HAS_ALPHA_TEXTURE) {
    a_plane_lock.reset(new cc::ResourceProvider::ScopedSamplerGL(
        resource_provider_, quad->a_plane_resource_id(), GL_TEXTURE4,
        GL_LINEAR));
    DCHECK_EQ(y_plane_lock.target(), a_plane_lock->target());
  }

  // All planes must have the same sampler type.
  SamplerType sampler = SamplerTypeFromTextureTarget(y_plane_lock.target());

  SetUseProgram(ProgramKey::YUVVideo(tex_coord_precision, sampler,
                                     alpha_texture_mode, uv_texture_mode),
                src_color_space, dst_color_space);

  gfx::SizeF ya_tex_scale(1.0f, 1.0f);
  gfx::SizeF uv_tex_scale(1.0f, 1.0f);
  if (sampler != SAMPLER_TYPE_2D_RECT) {
    DCHECK(!quad->ya_tex_size.IsEmpty());
    DCHECK(!quad->uv_tex_size.IsEmpty());
    ya_tex_scale = gfx::SizeF(1.0f / quad->ya_tex_size.width(),
                              1.0f / quad->ya_tex_size.height());
    uv_tex_scale = gfx::SizeF(1.0f / quad->uv_tex_size.width(),
                              1.0f / quad->uv_tex_size.height());
  }

  float ya_vertex_tex_translate_x =
      quad->ya_tex_coord_rect.x() * ya_tex_scale.width();
  float ya_vertex_tex_translate_y =
      quad->ya_tex_coord_rect.y() * ya_tex_scale.height();
  float ya_vertex_tex_scale_x =
      quad->ya_tex_coord_rect.width() * ya_tex_scale.width();
  float ya_vertex_tex_scale_y =
      quad->ya_tex_coord_rect.height() * ya_tex_scale.height();

  float uv_vertex_tex_translate_x =
      quad->uv_tex_coord_rect.x() * uv_tex_scale.width();
  float uv_vertex_tex_translate_y =
      quad->uv_tex_coord_rect.y() * uv_tex_scale.height();
  float uv_vertex_tex_scale_x =
      quad->uv_tex_coord_rect.width() * uv_tex_scale.width();
  float uv_vertex_tex_scale_y =
      quad->uv_tex_coord_rect.height() * uv_tex_scale.height();

  gl_->Uniform2f(current_program_->ya_tex_scale_location(),
                 ya_vertex_tex_scale_x, ya_vertex_tex_scale_y);
  gl_->Uniform2f(current_program_->ya_tex_offset_location(),
                 ya_vertex_tex_translate_x, ya_vertex_tex_translate_y);
  gl_->Uniform2f(current_program_->uv_tex_scale_location(),
                 uv_vertex_tex_scale_x, uv_vertex_tex_scale_y);
  gl_->Uniform2f(current_program_->uv_tex_offset_location(),
                 uv_vertex_tex_translate_x, uv_vertex_tex_translate_y);

  gfx::RectF ya_clamp_rect(ya_vertex_tex_translate_x, ya_vertex_tex_translate_y,
                           ya_vertex_tex_scale_x, ya_vertex_tex_scale_y);
  ya_clamp_rect.Inset(0.5f * ya_tex_scale.width(),
                      0.5f * ya_tex_scale.height());
  gfx::RectF uv_clamp_rect(uv_vertex_tex_translate_x, uv_vertex_tex_translate_y,
                           uv_vertex_tex_scale_x, uv_vertex_tex_scale_y);
  uv_clamp_rect.Inset(0.5f * uv_tex_scale.width(),
                      0.5f * uv_tex_scale.height());
  gl_->Uniform4f(current_program_->ya_clamp_rect_location(), ya_clamp_rect.x(),
                 ya_clamp_rect.y(), ya_clamp_rect.right(),
                 ya_clamp_rect.bottom());
  gl_->Uniform4f(current_program_->uv_clamp_rect_location(), uv_clamp_rect.x(),
                 uv_clamp_rect.y(), uv_clamp_rect.right(),
                 uv_clamp_rect.bottom());

  gl_->Uniform1i(current_program_->y_texture_location(), 1);
  if (uv_texture_mode == UV_TEXTURE_MODE_UV) {
    gl_->Uniform1i(current_program_->uv_texture_location(), 2);
  } else {
    gl_->Uniform1i(current_program_->u_texture_location(), 2);
    gl_->Uniform1i(current_program_->v_texture_location(), 3);
  }
  if (alpha_texture_mode == YUV_HAS_ALPHA_TEXTURE)
    gl_->Uniform1i(current_program_->a_texture_location(), 4);

  gl_->Uniform1f(current_program_->resource_multiplier_location(),
                 quad->resource_multiplier);
  gl_->Uniform1f(current_program_->resource_offset_location(),
                 quad->resource_offset);

  // The transform and vertex data are used to figure out the extents that the
  // un-antialiased quad should have and which vertex this is and the float
  // quad passed in via uniform is the actual geometry that gets used to draw
  // it. This is why this centered rect is used and not the original quad_rect.
  auto tile_rect = gfx::RectF(quad->rect);

  SetShaderOpacity(quad);
  if (!clip_region) {
    DrawQuadGeometry(current_frame()->projection_matrix,
                     quad->shared_quad_state->quad_to_target_transform,
                     tile_rect);
  } else {
    float uvs[8] = {0};
    GetScaledUVs(quad->visible_rect, clip_region, uvs);
    gfx::QuadF region_quad = *clip_region;
    region_quad.Scale(1.0f / tile_rect.width(), 1.0f / tile_rect.height());
    region_quad -= gfx::Vector2dF(0.5f, 0.5f);
    DrawQuadGeometryClippedByQuadF(
        quad->shared_quad_state->quad_to_target_transform, tile_rect,
        region_quad, uvs);
  }
}

void GLRenderer::DrawStreamVideoQuad(const cc::StreamVideoDrawQuad* quad,
                                     const gfx::QuadF* clip_region) {
  SetBlendEnabled(quad->ShouldDrawWithBlending());

  DCHECK(output_surface_->context_provider()
             ->ContextCapabilities()
             .egl_image_external);

  TexCoordPrecision tex_coord_precision = TexCoordPrecisionRequired(
      gl_, &highp_threshold_cache_, settings_->highp_threshold_min,
      quad->shared_quad_state->visible_quad_layer_rect.bottom_right());

  cc::ResourceProvider::ScopedReadLockGL lock(resource_provider_,
                                              quad->resource_id());

  SetUseProgram(ProgramKey::VideoStream(tex_coord_precision),
                lock.color_space());

  DCHECK_EQ(GL_TEXTURE0, GetActiveTextureUnit(gl_));
  gl_->BindTexture(GL_TEXTURE_EXTERNAL_OES, lock.texture_id());

  static float gl_matrix[16];
  ToGLMatrix(&gl_matrix[0], quad->matrix);
  gl_->UniformMatrix4fvStreamTextureMatrixCHROMIUM(
      current_program_->tex_matrix_location(), false, gl_matrix);

  SetShaderOpacity(quad);
  gfx::Size texture_size = lock.size();
  gfx::Vector2dF uv = quad->matrix.Scale2d();
  gfx::RectF uv_visible_rect(0, 0, uv.x(), uv.y());
  const SamplerType sampler = SamplerTypeFromTextureTarget(lock.target());
  Float4 tex_clamp_rect = UVClampRect(uv_visible_rect, texture_size, sampler);
  gl_->Uniform4f(current_program_->tex_clamp_rect_location(),
                 tex_clamp_rect.data[0], tex_clamp_rect.data[1],
                 tex_clamp_rect.data[2], tex_clamp_rect.data[3]);

  if (!clip_region) {
    DrawQuadGeometry(current_frame()->projection_matrix,
                     quad->shared_quad_state->quad_to_target_transform,
                     gfx::RectF(quad->rect));
  } else {
    gfx::QuadF region_quad(*clip_region);
    region_quad.Scale(1.0f / quad->rect.width(), 1.0f / quad->rect.height());
    region_quad -= gfx::Vector2dF(0.5f, 0.5f);
    float uvs[8] = {0};
    GetScaledUVs(quad->visible_rect, clip_region, uvs);
    DrawQuadGeometryClippedByQuadF(
        quad->shared_quad_state->quad_to_target_transform,
        gfx::RectF(quad->rect), region_quad, uvs);
  }
}

void GLRenderer::DrawOverlayCandidateQuadBorder(float* gl_matrix) {
  SetBlendEnabled(false);
  SetUseProgram(ProgramKey::DebugBorder(), gfx::ColorSpace::CreateSRGB());

  gl_->UniformMatrix4fv(current_program_->matrix_location(), 1, false,
                        gl_matrix);

  // Pick a random color based on the scale on X and Y.
  int colorIndex = static_cast<int>(gl_matrix[0] * gl_matrix[5]);
  SkColor color =
      cc::DebugColors::GLCompositedTextureQuadBorderColor(colorIndex);
  SetShaderColor(color, 1.f);

  gl_->LineWidth(cc::DebugColors::GLCompositedTextureQuadBoderWidth());
  // The indices for the line are stored in the same array as the triangle
  // indices.
  gl_->DrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_SHORT, 0);
}

void GLRenderer::FlushTextureQuadCache(BoundGeometry flush_binding) {
  // Check to see if we have anything to draw.
  if (draw_cache_.is_empty)
    return;

  PrepareGeometry(flush_binding);

  // Set the correct blending mode.
  SetBlendEnabled(draw_cache_.needs_blending);

  // Assume the current active textures is 0.
  cc::ResourceProvider::ScopedSamplerGL locked_quad(
      resource_provider_, draw_cache_.resource_id,
      draw_cache_.nearest_neighbor ? GL_NEAREST : GL_LINEAR);

  // Bind the program to the GL state.
  SetUseProgram(draw_cache_.program_key, locked_quad.color_space());

  DCHECK_EQ(GL_TEXTURE0, GetActiveTextureUnit(gl_));
  gl_->BindTexture(locked_quad.target(), locked_quad.texture_id());

  static_assert(sizeof(Float4) == 4 * sizeof(float),
                "Float4 struct should be densely packed");
  static_assert(sizeof(Float16) == 16 * sizeof(float),
                "Float16 struct should be densely packed");

  // Upload the tranforms for both points and uvs.
  gl_->UniformMatrix4fv(
      current_program_->matrix_location(),
      static_cast<int>(draw_cache_.matrix_data.size()), false,
      reinterpret_cast<float*>(&draw_cache_.matrix_data.front()));
  gl_->Uniform4fv(current_program_->vertex_tex_transform_location(),
                  static_cast<int>(draw_cache_.uv_xform_data.size()),
                  reinterpret_cast<float*>(&draw_cache_.uv_xform_data.front()));

  if (current_program_->tex_clamp_rect_location() != -1) {
    // Draw batching is not allowed with texture clamping.
    DCHECK_EQ(1u, draw_cache_.matrix_data.size());
    gl_->Uniform4f(current_program_->tex_clamp_rect_location(),
                   draw_cache_.tex_clamp_rect_data.data[0],
                   draw_cache_.tex_clamp_rect_data.data[1],
                   draw_cache_.tex_clamp_rect_data.data[2],
                   draw_cache_.tex_clamp_rect_data.data[3]);
  }

  if (draw_cache_.background_color != SK_ColorTRANSPARENT) {
    Float4 background_color =
        PremultipliedColor(draw_cache_.background_color, 1.f);
    gl_->Uniform4fv(current_program_->background_color_location(), 1,
                    background_color.data);
  }

  gl_->Uniform1fv(
      current_program_->vertex_opacity_location(),
      static_cast<int>(draw_cache_.vertex_opacity_data.size()),
      static_cast<float*>(&draw_cache_.vertex_opacity_data.front()));

  DCHECK_LE(draw_cache_.matrix_data.size(),
            static_cast<size_t>(std::numeric_limits<int>::max()) / 6u);
  // Draw the quads!
  gl_->DrawElements(GL_TRIANGLES,
                    6 * static_cast<int>(draw_cache_.matrix_data.size()),
                    GL_UNSIGNED_SHORT, 0);
  num_triangles_drawn_ += 2 * static_cast<int>(draw_cache_.matrix_data.size());

  // Draw the border if requested.
  if (gl_composited_overlay_candidate_quad_border_) {
    // When we draw the composited borders we have one flush per quad.
    DCHECK_EQ(1u, draw_cache_.matrix_data.size());
    DrawOverlayCandidateQuadBorder(
        reinterpret_cast<float*>(&draw_cache_.matrix_data.front()));
  }

  // Clear the cache.
  draw_cache_.is_empty = true;
  draw_cache_.resource_id = -1;
  draw_cache_.uv_xform_data.resize(0);
  draw_cache_.vertex_opacity_data.resize(0);
  draw_cache_.matrix_data.resize(0);
  draw_cache_.tex_clamp_rect_data = Float4();

  // If we had a clipped binding, prepare the shared binding for the
  // next inserts.
  if (flush_binding == CLIPPED_BINDING) {
    PrepareGeometry(SHARED_BINDING);
  }
}

void GLRenderer::EnqueueTextureQuad(const cc::TextureDrawQuad* quad,
                                    const gfx::QuadF* clip_region) {
  // If we have a clip_region then we have to render the next quad
  // with dynamic geometry, therefore we must flush all pending
  // texture quads.
  if (clip_region) {
    // We send in false here because we want to flush what's currently in the
    // queue using the shared_geometry and not clipped_geometry
    FlushTextureQuadCache(SHARED_BINDING);
  }

  TexCoordPrecision tex_coord_precision = TexCoordPrecisionRequired(
      gl_, &highp_threshold_cache_, settings_->highp_threshold_min,
      quad->shared_quad_state->visible_quad_layer_rect.bottom_right());

  cc::ResourceProvider::ScopedReadLockGL lock(resource_provider_,
                                              quad->resource_id());
  const SamplerType sampler = SamplerTypeFromTextureTarget(lock.target());

  bool need_tex_clamp_rect = !quad->resource_size_in_pixels().IsEmpty() &&
                             (quad->uv_top_left != gfx::PointF(0, 0) ||
                              quad->uv_bottom_right != gfx::PointF(1, 1));
  ProgramKey program_key = ProgramKey::Texture(
      tex_coord_precision, sampler,
      quad->premultiplied_alpha ? PREMULTIPLIED_ALPHA : NON_PREMULTIPLIED_ALPHA,
      quad->background_color != SK_ColorTRANSPARENT, need_tex_clamp_rect);
  int resource_id = quad->resource_id();

  size_t max_quads = StaticGeometryBinding::NUM_QUADS;
  if (draw_cache_.is_empty || draw_cache_.program_key != program_key ||
      draw_cache_.resource_id != resource_id ||
      draw_cache_.needs_blending != quad->ShouldDrawWithBlending() ||
      draw_cache_.nearest_neighbor != quad->nearest_neighbor ||
      draw_cache_.background_color != quad->background_color ||
      draw_cache_.matrix_data.size() >= max_quads) {
    FlushTextureQuadCache(SHARED_BINDING);
    draw_cache_.is_empty = false;
    draw_cache_.program_key = program_key;
    draw_cache_.resource_id = resource_id;
    draw_cache_.needs_blending = quad->ShouldDrawWithBlending();
    draw_cache_.nearest_neighbor = quad->nearest_neighbor;
    draw_cache_.background_color = quad->background_color;
  }

  // Generate the uv-transform
  Float4 uv_transform = {{0.0f, 0.0f, 1.0f, 1.0f}};
  if (!clip_region)
    uv_transform = UVTransform(quad);
  if (sampler == SAMPLER_TYPE_2D_RECT) {
    // Un-normalize the texture coordiantes for rectangle targets.
    gfx::Size texture_size = lock.size();
    uv_transform.data[0] *= texture_size.width();
    uv_transform.data[2] *= texture_size.width();
    uv_transform.data[1] *= texture_size.height();
    uv_transform.data[3] *= texture_size.height();
  }
  draw_cache_.uv_xform_data.push_back(uv_transform);

  if (need_tex_clamp_rect) {
    DCHECK_EQ(1u, draw_cache_.uv_xform_data.size());
    gfx::Size texture_size = quad->resource_size_in_pixels();
    DCHECK(!texture_size.IsEmpty());
    gfx::RectF uv_visible_rect(
        quad->uv_top_left.x(), quad->uv_top_left.y(),
        quad->uv_bottom_right.x() - quad->uv_top_left.x(),
        quad->uv_bottom_right.y() - quad->uv_top_left.y());
    Float4 tex_clamp_rect = UVClampRect(uv_visible_rect, texture_size, sampler);
    draw_cache_.tex_clamp_rect_data = tex_clamp_rect;
  }

  // Generate the vertex opacity
  const float opacity = quad->shared_quad_state->opacity;
  draw_cache_.vertex_opacity_data.push_back(quad->vertex_opacity[0] * opacity);
  draw_cache_.vertex_opacity_data.push_back(quad->vertex_opacity[1] * opacity);
  draw_cache_.vertex_opacity_data.push_back(quad->vertex_opacity[2] * opacity);
  draw_cache_.vertex_opacity_data.push_back(quad->vertex_opacity[3] * opacity);

  // Generate the transform matrix
  gfx::Transform quad_rect_matrix;
  QuadRectTransform(&quad_rect_matrix,
                    quad->shared_quad_state->quad_to_target_transform,
                    gfx::RectF(quad->rect));
  quad_rect_matrix = current_frame()->projection_matrix * quad_rect_matrix;

  Float16 m;
  quad_rect_matrix.matrix().asColMajorf(m.data);
  draw_cache_.matrix_data.push_back(m);

  if (clip_region) {
    gfx::QuadF scaled_region;
    if (!GetScaledRegion(quad->rect, clip_region, &scaled_region)) {
      scaled_region = SharedGeometryQuad().BoundingBox();
    }
    // Both the scaled region and the SharedGeomtryQuad are in the space
    // -0.5->0.5. We need to move that to the space 0->1.
    float uv[8];
    uv[0] = scaled_region.p1().x() + 0.5f;
    uv[1] = scaled_region.p1().y() + 0.5f;
    uv[2] = scaled_region.p2().x() + 0.5f;
    uv[3] = scaled_region.p2().y() + 0.5f;
    uv[4] = scaled_region.p3().x() + 0.5f;
    uv[5] = scaled_region.p3().y() + 0.5f;
    uv[6] = scaled_region.p4().x() + 0.5f;
    uv[7] = scaled_region.p4().y() + 0.5f;
    PrepareGeometry(CLIPPED_BINDING);
    clipped_geometry_->InitializeCustomQuadWithUVs(scaled_region, uv);
    FlushTextureQuadCache(CLIPPED_BINDING);
  } else if (gl_composited_overlay_candidate_quad_border_ ||
             need_tex_clamp_rect) {
    FlushTextureQuadCache(SHARED_BINDING);
  }
}

void GLRenderer::FinishDrawingFrame() {
  if (use_sync_query_) {
    DCHECK(current_sync_query_);
    current_sync_query_->End();
    pending_sync_queries_.push_back(std::move(current_sync_query_));
  }

  swap_buffer_rect_.Union(current_frame()->root_damage_rect);
  if (overdraw_feedback_)
    FlushOverdrawFeedback(swap_buffer_rect_);

  if (use_swap_with_bounds_)
    swap_content_bounds_ = current_frame()->root_content_bounds;

  current_framebuffer_lock_ = nullptr;

  gl_->Disable(GL_BLEND);
  blend_shadow_ = false;

  ScheduleCALayers();
  ScheduleDCLayers();
  ScheduleOverlays();

  TRACE_COUNTER1(TRACE_DISABLED_BY_DEFAULT("cc.debug.triangles"),
                 "Triangles Drawn", num_triangles_drawn_);
}

void GLRenderer::FinishDrawingQuadList() {
  FlushTextureQuadCache(SHARED_BINDING);
}

void GLRenderer::SetEnableDCLayers(bool enable) {
  gl_->SetEnableDCLayersCHROMIUM(enable);
}

bool GLRenderer::FlippedFramebuffer() const {
  if (force_drawing_frame_framebuffer_unflipped_)
    return false;
  if (current_frame()->current_render_pass != current_frame()->root_render_pass)
    return true;
  return FlippedRootFramebuffer();
}

bool GLRenderer::FlippedRootFramebuffer() const {
  // GL is normally flipped, so a flipped output results in an unflipping.
  return !output_surface_->capabilities().flipped_output_surface;
}

void GLRenderer::EnsureScissorTestEnabled() {
  if (is_scissor_enabled_)
    return;

  FlushTextureQuadCache(SHARED_BINDING);
  gl_->Enable(GL_SCISSOR_TEST);
  is_scissor_enabled_ = true;
}

void GLRenderer::EnsureScissorTestDisabled() {
  if (!is_scissor_enabled_)
    return;

  FlushTextureQuadCache(SHARED_BINDING);
  gl_->Disable(GL_SCISSOR_TEST);
  is_scissor_enabled_ = false;
}

void GLRenderer::CopyCurrentRenderPassToBitmap(
    std::unique_ptr<CopyOutputRequest> request) {
  TRACE_EVENT0("cc", "GLRenderer::CopyCurrentRenderPassToBitmap");
  gfx::Rect copy_rect = current_frame()->current_render_pass->output_rect;
  if (request->has_area())
    copy_rect.Intersect(request->area());
  GetFramebufferPixelsAsync(copy_rect, std::move(request));
}

void GLRenderer::ToGLMatrix(float* gl_matrix, const gfx::Transform& transform) {
  transform.matrix().asColMajorf(gl_matrix);
}

void GLRenderer::SetShaderQuadF(const gfx::QuadF& quad) {
  if (!current_program_ || current_program_->quad_location() == -1)
    return;
  float gl_quad[8];
  gl_quad[0] = quad.p1().x();
  gl_quad[1] = quad.p1().y();
  gl_quad[2] = quad.p2().x();
  gl_quad[3] = quad.p2().y();
  gl_quad[4] = quad.p3().x();
  gl_quad[5] = quad.p3().y();
  gl_quad[6] = quad.p4().x();
  gl_quad[7] = quad.p4().y();
  gl_->Uniform2fv(current_program_->quad_location(), 4, gl_quad);
}

void GLRenderer::SetShaderOpacity(const cc::DrawQuad* quad) {
  if (!current_program_ || current_program_->alpha_location() == -1)
    return;
  gl_->Uniform1f(current_program_->alpha_location(),
                 quad->shared_quad_state->opacity);
}

void GLRenderer::SetShaderMatrix(const gfx::Transform& transform) {
  if (!current_program_ || current_program_->matrix_location() == -1)
    return;
  float gl_matrix[16];
  ToGLMatrix(gl_matrix, transform);
  gl_->UniformMatrix4fv(current_program_->matrix_location(), 1, false,
                        gl_matrix);
}

void GLRenderer::SetShaderColor(SkColor color, float opacity) {
  if (!current_program_ || current_program_->color_location() == -1)
    return;
  Float4 float_color = PremultipliedColor(color, opacity);
  gl_->Uniform4fv(current_program_->color_location(), 1, float_color.data);
}

void GLRenderer::SetStencilEnabled(bool enabled) {
  if (enabled == stencil_shadow_)
    return;

  if (enabled)
    gl_->Enable(GL_STENCIL_TEST);
  else
    gl_->Disable(GL_STENCIL_TEST);
  stencil_shadow_ = enabled;
}

void GLRenderer::SetBlendEnabled(bool enabled) {
  if (enabled == blend_shadow_)
    return;

  if (enabled)
    gl_->Enable(GL_BLEND);
  else
    gl_->Disable(GL_BLEND);
  blend_shadow_ = enabled;
}

void GLRenderer::DrawQuadGeometryClippedByQuadF(
    const gfx::Transform& draw_transform,
    const gfx::RectF& quad_rect,
    const gfx::QuadF& clipping_region_quad,
    const float* uvs) {
  PrepareGeometry(CLIPPED_BINDING);
  if (uvs) {
    clipped_geometry_->InitializeCustomQuadWithUVs(clipping_region_quad, uvs);
  } else {
    clipped_geometry_->InitializeCustomQuad(clipping_region_quad);
  }
  gfx::Transform quad_rect_matrix;
  QuadRectTransform(&quad_rect_matrix, draw_transform, quad_rect);
  SetShaderMatrix(current_frame()->projection_matrix * quad_rect_matrix);

  gl_->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT,
                    reinterpret_cast<const void*>(0));
  num_triangles_drawn_ += 2;
}

void GLRenderer::DrawQuadGeometry(const gfx::Transform& projection_matrix,
                                  const gfx::Transform& draw_transform,
                                  const gfx::RectF& quad_rect) {
  PrepareGeometry(SHARED_BINDING);
  gfx::Transform quad_rect_matrix;
  QuadRectTransform(&quad_rect_matrix, draw_transform, quad_rect);
  SetShaderMatrix(projection_matrix * quad_rect_matrix);

  gl_->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
  num_triangles_drawn_ += 2;
}

void GLRenderer::SwapBuffers(std::vector<ui::LatencyInfo> latency_info) {
  DCHECK(visible_);

  TRACE_EVENT0("cc", "GLRenderer::SwapBuffers");
  // We're done! Time to swapbuffers!

  gfx::Size surface_size = surface_size_for_swap_buffers();

  cc::OutputSurfaceFrame output_frame;
  output_frame.latency_info = std::move(latency_info);
  output_frame.size = surface_size;
  if (use_swap_with_bounds_) {
    output_frame.content_bounds = std::move(swap_content_bounds_);
  } else if (use_partial_swap_) {
    // If supported, we can save significant bandwidth by only swapping the
    // damaged/scissored region (clamped to the viewport).
    swap_buffer_rect_.Intersect(gfx::Rect(surface_size));
    int flipped_y_pos_of_rect_bottom = surface_size.height() -
                                       swap_buffer_rect_.y() -
                                       swap_buffer_rect_.height();
    output_frame.sub_buffer_rect =
        gfx::Rect(swap_buffer_rect_.x(),
                  FlippedRootFramebuffer() ? flipped_y_pos_of_rect_bottom
                                           : swap_buffer_rect_.y(),
                  swap_buffer_rect_.width(), swap_buffer_rect_.height());
  } else if (swap_buffer_rect_.IsEmpty() && allow_empty_swap_) {
    output_frame.sub_buffer_rect = swap_buffer_rect_;
  }

  swapping_overlay_resources_.push_back(std::move(pending_overlay_resources_));
  pending_overlay_resources_.clear();

  output_surface_->SwapBuffers(std::move(output_frame));

  swap_buffer_rect_ = gfx::Rect();
}

void GLRenderer::SwapBuffersComplete() {
  if (settings_->release_overlay_resources_after_gpu_query) {
    // Once a resource has been swap-ACKed, send a query to the GPU process to
    // ask if the resource is no longer being consumed by the system compositor.
    // The response will come with the next swap-ACK.
    if (!swapping_overlay_resources_.empty()) {
      for (OverlayResourceLock& lock : swapping_overlay_resources_.front()) {
        unsigned texture = lock->texture_id();
        if (swapped_and_acked_overlay_resources_.find(texture) ==
            swapped_and_acked_overlay_resources_.end()) {
          swapped_and_acked_overlay_resources_[texture] = std::move(lock);
        }
      }
      swapping_overlay_resources_.pop_front();
    }

    if (!swapped_and_acked_overlay_resources_.empty()) {
      std::vector<unsigned> textures;
      textures.reserve(swapped_and_acked_overlay_resources_.size());
      for (auto& pair : swapped_and_acked_overlay_resources_) {
        textures.push_back(pair.first);
      }
      gl_->ScheduleCALayerInUseQueryCHROMIUM(textures.size(), textures.data());
    }
  } else if (swapping_overlay_resources_.size() > 1) {
    cc::ResourceProvider::ScopedBatchReturnResources returner(
        resource_provider_);

    // If a query is not needed to release the overlay buffers, we can assume
    // that once a swap buffer has completed we can remove the oldest buffers
    // from the queue.
    swapping_overlay_resources_.pop_front();
  }
}

void GLRenderer::DidReceiveTextureInUseResponses(
    const gpu::TextureInUseResponses& responses) {
  DCHECK(settings_->release_overlay_resources_after_gpu_query);
  cc::ResourceProvider::ScopedBatchReturnResources returner(resource_provider_);
  for (const gpu::TextureInUseResponse& response : responses) {
    if (!response.in_use) {
      swapped_and_acked_overlay_resources_.erase(response.texture);
    }
  }
  color_lut_cache_.Swap();
}

void GLRenderer::GetFramebufferPixelsAsync(
    const gfx::Rect& rect,
    std::unique_ptr<CopyOutputRequest> request) {
  DCHECK(!request->IsEmpty());
  if (request->IsEmpty())
    return;
  if (rect.IsEmpty())
    return;

  if (overdraw_feedback_)
    FlushOverdrawFeedback(rect);

  gfx::Rect window_rect = MoveFromDrawToWindowSpace(rect);
  DCHECK_GE(window_rect.x(), 0);
  DCHECK_GE(window_rect.y(), 0);
  DCHECK_LE(window_rect.right(), current_surface_size_.width());
  DCHECK_LE(window_rect.bottom(), current_surface_size_.height());

  if (!request->force_bitmap_result()) {
    bool own_mailbox = !request->has_texture_mailbox();

    GLuint texture_id = 0;
    gpu::Mailbox mailbox;
    if (own_mailbox) {
      gl_->GenMailboxCHROMIUM(mailbox.name);
      gl_->GenTextures(1, &texture_id);
      gl_->BindTexture(GL_TEXTURE_2D, texture_id);

      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      gl_->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      gl_->ProduceTextureCHROMIUM(GL_TEXTURE_2D, mailbox.name);
    } else {
      mailbox = request->texture_mailbox().mailbox();
      DCHECK_EQ(static_cast<unsigned>(GL_TEXTURE_2D),
                request->texture_mailbox().target());
      DCHECK(!mailbox.IsZero());
      const gpu::SyncToken& incoming_sync_token =
          request->texture_mailbox().sync_token();
      if (incoming_sync_token.HasData())
        gl_->WaitSyncTokenCHROMIUM(incoming_sync_token.GetConstData());

      texture_id =
          gl_->CreateAndConsumeTextureCHROMIUM(GL_TEXTURE_2D, mailbox.name);
    }
    GetFramebufferTexture(texture_id, window_rect);

    const GLuint64 fence_sync = gl_->InsertFenceSyncCHROMIUM();
    gl_->ShallowFlushCHROMIUM();

    gpu::SyncToken sync_token;
    gl_->GenSyncTokenCHROMIUM(fence_sync, sync_token.GetData());

    TextureMailbox texture_mailbox(mailbox, sync_token, GL_TEXTURE_2D);

    std::unique_ptr<SingleReleaseCallback> release_callback;
    if (own_mailbox) {
      gl_->BindTexture(GL_TEXTURE_2D, 0);
      release_callback = texture_mailbox_deleter_->GetReleaseCallback(
          output_surface_->context_provider(), texture_id);
    } else {
      gl_->DeleteTextures(1, &texture_id);
    }

    request->SendTextureResult(window_rect.size(), texture_mailbox,
                               std::move(release_callback));
    return;
  }

  DCHECK(request->force_bitmap_result());

  std::unique_ptr<PendingAsyncReadPixels> pending_read(
      new PendingAsyncReadPixels);
  pending_read->copy_request = std::move(request);
  pending_async_read_pixels_.insert(pending_async_read_pixels_.begin(),
                                    std::move(pending_read));

  GLuint buffer = 0;
  gl_->GenBuffers(1, &buffer);
  gl_->BindBuffer(GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM, buffer);
  gl_->BufferData(GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM,
                  4 * window_rect.size().GetArea(), NULL, GL_STREAM_READ);

  GLuint query = 0;
  gl_->GenQueriesEXT(1, &query);
  gl_->BeginQueryEXT(GL_ASYNC_PIXEL_PACK_COMPLETED_CHROMIUM, query);

  gl_->ReadPixels(window_rect.x(), window_rect.y(), window_rect.width(),
                  window_rect.height(), GL_RGBA, GL_UNSIGNED_BYTE, NULL);

  gl_->BindBuffer(GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM, 0);

  // Save the buffer to verify the callbacks happen in the expected order.
  pending_async_read_pixels_.front()->buffer = buffer;

  gl_->EndQueryEXT(GL_ASYNC_PIXEL_PACK_COMPLETED_CHROMIUM);
  context_support_->SignalQuery(
      query,
      base::Bind(&GLRenderer::FinishedReadback, weak_ptr_factory_.GetWeakPtr(),
                 buffer, query, window_rect.size()));
}

void GLRenderer::FinishedReadback(unsigned source_buffer,
                                  unsigned query,
                                  const gfx::Size& size) {
  DCHECK(!pending_async_read_pixels_.empty());

  if (query != 0) {
    gl_->DeleteQueriesEXT(1, &query);
  }

  // Make sure we are servicing the right readback. There is no guarantee that
  // callbacks to this function are in the same order as we post the copy
  // requests.
  // Nevertheless, it is very likely that the order is preserved, and thus
  // start searching from back to the front.
  auto iter = pending_async_read_pixels_.rbegin();
  const auto& reverse_end = pending_async_read_pixels_.rend();
  while (iter != reverse_end && (*iter)->buffer != source_buffer)
    ++iter;

  DCHECK(iter != reverse_end);
  PendingAsyncReadPixels* current_read = iter->get();

  uint8_t* src_pixels = NULL;
  std::unique_ptr<SkBitmap> bitmap;

  if (source_buffer != 0) {
    gl_->BindBuffer(GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM, source_buffer);
    src_pixels = static_cast<uint8_t*>(gl_->MapBufferCHROMIUM(
        GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM, GL_READ_ONLY));

    if (src_pixels) {
      bitmap.reset(new SkBitmap);
      bitmap->allocN32Pixels(size.width(), size.height());
      uint8_t* dest_pixels = static_cast<uint8_t*>(bitmap->getPixels());

      size_t row_bytes = size.width() * 4;
      int num_rows = size.height();
      size_t total_bytes = num_rows * row_bytes;
      for (size_t dest_y = 0; dest_y < total_bytes; dest_y += row_bytes) {
        // Flip Y axis.
        size_t src_y = total_bytes - dest_y - row_bytes;
        // Swizzle OpenGL -> Skia byte order.
        for (size_t x = 0; x < row_bytes; x += 4) {
          dest_pixels[dest_y + x + SK_R32_SHIFT / 8] =
              src_pixels[src_y + x + 0];
          dest_pixels[dest_y + x + SK_G32_SHIFT / 8] =
              src_pixels[src_y + x + 1];
          dest_pixels[dest_y + x + SK_B32_SHIFT / 8] =
              src_pixels[src_y + x + 2];
          dest_pixels[dest_y + x + SK_A32_SHIFT / 8] =
              src_pixels[src_y + x + 3];
        }
      }

      gl_->UnmapBufferCHROMIUM(GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM);
    }
    gl_->BindBuffer(GL_PIXEL_PACK_TRANSFER_BUFFER_CHROMIUM, 0);
    gl_->DeleteBuffers(1, &source_buffer);
  }

  if (bitmap)
    current_read->copy_request->SendBitmapResult(std::move(bitmap));

  // Conversion from reverse iterator to iterator:
  // Iterator |iter.base() - 1| points to the same element with reverse iterator
  // |iter|. The difference |-1| is due to the fact of correspondence of end()
  // with rbegin().
  pending_async_read_pixels_.erase(iter.base() - 1);
}

void GLRenderer::GetFramebufferTexture(unsigned texture_id,
                                       const gfx::Rect& window_rect) {
  DCHECK(texture_id);
  DCHECK_GE(window_rect.x(), 0);
  DCHECK_GE(window_rect.y(), 0);
  DCHECK_LE(window_rect.right(), current_surface_size_.width());
  DCHECK_LE(window_rect.bottom(), current_surface_size_.height());

  // If copying a non-root renderpass then use the format of the bound
  // texture. Otherwise, we use the format of the default framebuffer.
  GLenum format = current_framebuffer_lock_
                      ? GLCopyTextureInternalFormat(current_framebuffer_format_)
                      : output_surface_->GetFramebufferCopyTextureFormat();
  // Verify the format is valid for GLES2's glCopyTexImage2D.
  DCHECK(format == GL_ALPHA || format == GL_LUMINANCE ||
         format == GL_LUMINANCE_ALPHA || format == GL_RGB || format == GL_RGBA)
      << format;

  gl_->BindTexture(GL_TEXTURE_2D, texture_id);
  gl_->CopyTexImage2D(GL_TEXTURE_2D, 0, format, window_rect.x(),
                      window_rect.y(), window_rect.width(),
                      window_rect.height(), 0);
  gl_->BindTexture(GL_TEXTURE_2D, 0);
}

void GLRenderer::BindFramebufferToOutputSurface() {
  current_framebuffer_lock_ = nullptr;
  output_surface_->BindFramebuffer();

  if (overdraw_feedback_) {
    // Output surfaces that require an external stencil test should not allow
    // overdraw feedback by setting |supports_stencil| to false.
    DCHECK(!output_surface_->HasExternalStencilTest());
    SetupOverdrawFeedback();
    SetStencilEnabled(true);
  } else if (output_surface_->HasExternalStencilTest()) {
    output_surface_->ApplyExternalStencil();
    SetStencilEnabled(true);
  } else {
    SetStencilEnabled(false);
  }
}

bool GLRenderer::BindFramebufferToTexture(const cc::ScopedResource* texture) {
  DCHECK(texture->id());

  // Explicitly release lock, otherwise we can crash when try to lock
  // same texture again.
  current_framebuffer_lock_ = nullptr;

  gl_->BindFramebuffer(GL_FRAMEBUFFER, offscreen_framebuffer_id_);
  current_framebuffer_lock_ =
      base::MakeUnique<cc::ResourceProvider::ScopedWriteLockGL>(
          resource_provider_, texture->id(), false);
  current_framebuffer_format_ = texture->format();
  unsigned texture_id = current_framebuffer_lock_->texture_id();
  gl_->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                            texture_id, 0);
  if (overdraw_feedback_) {
    if (!offscreen_stencil_renderbuffer_id_)
      gl_->GenRenderbuffers(1, &offscreen_stencil_renderbuffer_id_);
    if (texture->size() != offscreen_stencil_renderbuffer_size_) {
      gl_->BindRenderbuffer(GL_RENDERBUFFER,
                            offscreen_stencil_renderbuffer_id_);
      gl_->RenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8,
                               texture->size().width(),
                               texture->size().height());
      gl_->BindRenderbuffer(GL_RENDERBUFFER, 0);
      offscreen_stencil_renderbuffer_size_ = texture->size();
    }
    gl_->FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT,
                                 GL_RENDERBUFFER,
                                 offscreen_stencil_renderbuffer_id_);
  }

  DCHECK(gl_->CheckFramebufferStatus(GL_FRAMEBUFFER) ==
             GL_FRAMEBUFFER_COMPLETE ||
         IsContextLost());

  if (overdraw_feedback_) {
    SetupOverdrawFeedback();
    SetStencilEnabled(true);
  } else {
    SetStencilEnabled(false);
  }
  return true;
}

void GLRenderer::SetScissorTestRect(const gfx::Rect& scissor_rect) {
  EnsureScissorTestEnabled();

  // Don't unnecessarily ask the context to change the scissor, because it
  // may cause undesired GPU pipeline flushes.
  if (scissor_rect == scissor_rect_)
    return;

  scissor_rect_ = scissor_rect;
  FlushTextureQuadCache(SHARED_BINDING);
  gl_->Scissor(scissor_rect.x(), scissor_rect.y(), scissor_rect.width(),
               scissor_rect.height());
}

void GLRenderer::SetViewport() {
  gl_->Viewport(current_window_space_viewport_.x(),
                current_window_space_viewport_.y(),
                current_window_space_viewport_.width(),
                current_window_space_viewport_.height());
}

void GLRenderer::InitializeSharedObjects() {
  TRACE_EVENT0("cc", "GLRenderer::InitializeSharedObjects");

  // Create an FBO for doing offscreen rendering.
  gl_->GenFramebuffers(1, &offscreen_framebuffer_id_);

  shared_geometry_ =
      base::MakeUnique<StaticGeometryBinding>(gl_, QuadVertexRect());
  clipped_geometry_ = base::MakeUnique<DynamicGeometryBinding>(gl_);
}

void GLRenderer::PrepareGeometry(BoundGeometry binding) {
  if (binding == bound_geometry_) {
    return;
  }

  switch (binding) {
    case SHARED_BINDING:
      shared_geometry_->PrepareForDraw();
      break;
    case CLIPPED_BINDING:
      clipped_geometry_->PrepareForDraw();
      break;
    case NO_BINDING:
      break;
  }
  bound_geometry_ = binding;
}

void GLRenderer::SetUseProgram(const ProgramKey& program_key,
                               const gfx::ColorSpace& src_color_space) {
  // The source color space for non-YUV draw quads should always be full-range
  // RGB.
  if (!disable_color_checks_for_testing_)
    DCHECK_EQ(src_color_space, src_color_space.GetAsFullRangeRGB());

  // Ensure that we do not apply any color conversion unless the color correct
  // rendering flag has been specified. This is because media mailboxes will
  // provide YUV color spaces despite YUV to RGB conversion already having been
  // performed.
  if (settings_->enable_color_correct_rendering) {
    SetUseProgram(program_key, src_color_space,
                  current_frame()->current_render_pass->color_space);
  } else {
    SetUseProgram(program_key, gfx::ColorSpace(), gfx::ColorSpace());
  }
}

void GLRenderer::SetUseProgram(const ProgramKey& program_key_no_color,
                               const gfx::ColorSpace& src_color_space,
                               const gfx::ColorSpace& dst_color_space) {
  ProgramKey program_key = program_key_no_color;
  const gfx::ColorTransform* color_transform =
      GetColorTransform(src_color_space, dst_color_space);
  program_key.SetColorTransform(color_transform);

  // Create and set the program if needed.
  std::unique_ptr<Program>& program = program_cache_[program_key];
  if (!program) {
    program.reset(new Program);
    program->Initialize(output_surface_->context_provider(), program_key);
  }
  DCHECK(program);
  if (current_program_ != program.get()) {
    current_program_ = program.get();
    gl_->UseProgram(current_program_->program());
  }
  if (!current_program_->initialized()) {
    DCHECK(IsContextLost());
    return;
  }

  // Set uniforms that are common to all programs.
  if (current_program_->sampler_location() != -1)
    gl_->Uniform1i(current_program_->sampler_location(), 0);
  if (current_program_->viewport_location() != -1) {
    float viewport[4] = {
        static_cast<float>(current_window_space_viewport_.x()),
        static_cast<float>(current_window_space_viewport_.y()),
        static_cast<float>(current_window_space_viewport_.width()),
        static_cast<float>(current_window_space_viewport_.height()),
    };
    gl_->Uniform4fv(current_program_->viewport_location(), 1, viewport);
  }
  if (current_program_->lut_texture_location() != -1) {
    ColorLUTCache::LUT lut = color_lut_cache_.GetLUT(color_transform);
    gl_->ActiveTexture(GL_TEXTURE5);
    gl_->BindTexture(GL_TEXTURE_2D, lut.texture);
    gl_->Uniform1i(current_program_->lut_texture_location(), 5);
    gl_->Uniform1f(current_program_->lut_size_location(), lut.size);
    gl_->ActiveTexture(GL_TEXTURE0);
  }
}

const Program* GLRenderer::GetProgramIfInitialized(
    const ProgramKey& desc) const {
  const auto found = program_cache_.find(desc);
  if (found == program_cache_.end())
    return nullptr;
  return found->second.get();
}

const gfx::ColorTransform* GLRenderer::GetColorTransform(
    const gfx::ColorSpace& src,
    const gfx::ColorSpace& dst) {
  std::unique_ptr<gfx::ColorTransform>& transform =
      color_transform_cache_[dst][src];
  if (!transform) {
    transform = gfx::ColorTransform::NewColorTransform(
        src, dst, gfx::ColorTransform::Intent::INTENT_PERCEPTUAL);
  }
  return transform.get();
}

void GLRenderer::CleanupSharedObjects() {
  shared_geometry_ = nullptr;

  gl_->ReleaseShaderCompiler();
  for (auto& iter : program_cache_)
    iter.second->Cleanup(gl_);
  program_cache_.clear();
  color_transform_cache_.clear();

  if (offscreen_framebuffer_id_)
    gl_->DeleteFramebuffers(1, &offscreen_framebuffer_id_);

  if (offscreen_stencil_renderbuffer_id_)
    gl_->DeleteRenderbuffers(1, &offscreen_stencil_renderbuffer_id_);

  ReleaseRenderPassTextures();
}

void GLRenderer::ReinitializeGLState() {
  is_scissor_enabled_ = false;
  scissor_rect_ = gfx::Rect();
  stencil_shadow_ = false;
  blend_shadow_ = true;
  current_program_ = nullptr;

  RestoreGLState();
}

void GLRenderer::RestoreGLState() {
  // This restores the current GLRenderer state to the GL context.
  bound_geometry_ = NO_BINDING;
  PrepareGeometry(SHARED_BINDING);

  gl_->Disable(GL_DEPTH_TEST);
  gl_->Disable(GL_CULL_FACE);
  gl_->ColorMask(true, true, true, true);
  gl_->BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  gl_->ActiveTexture(GL_TEXTURE0);

  if (current_program_)
    gl_->UseProgram(current_program_->program());

  if (stencil_shadow_)
    gl_->Enable(GL_STENCIL_TEST);
  else
    gl_->Disable(GL_STENCIL_TEST);

  if (blend_shadow_)
    gl_->Enable(GL_BLEND);
  else
    gl_->Disable(GL_BLEND);

  if (is_scissor_enabled_)
    gl_->Enable(GL_SCISSOR_TEST);
  else
    gl_->Disable(GL_SCISSOR_TEST);

  gl_->Scissor(scissor_rect_.x(), scissor_rect_.y(), scissor_rect_.width(),
               scissor_rect_.height());
}

bool GLRenderer::IsContextLost() {
  return gl_->GetGraphicsResetStatusKHR() != GL_NO_ERROR;
}

void GLRenderer::ScheduleCALayers() {
  if (overlay_resource_pool_) {
    overlay_resource_pool_->CheckBusyResources();
  }

  scoped_refptr<cc::CALayerOverlaySharedState> shared_state;
  size_t copied_render_pass_count = 0;
  for (const cc::CALayerOverlay& ca_layer_overlay :
       current_frame()->ca_layer_overlay_list) {
    if (ca_layer_overlay.rpdq) {
      ScheduleRenderPassDrawQuad(&ca_layer_overlay);
      shared_state = nullptr;
      ++copied_render_pass_count;
      continue;
    }

    ResourceId contents_resource_id = ca_layer_overlay.contents_resource_id;
    unsigned texture_id = 0;
    if (contents_resource_id) {
      pending_overlay_resources_.push_back(
          base::MakeUnique<cc::ResourceProvider::ScopedReadLockGL>(
              resource_provider_, contents_resource_id));
      texture_id = pending_overlay_resources_.back()->texture_id();
    }
    GLfloat contents_rect[4] = {
        ca_layer_overlay.contents_rect.x(), ca_layer_overlay.contents_rect.y(),
        ca_layer_overlay.contents_rect.width(),
        ca_layer_overlay.contents_rect.height(),
    };
    GLfloat bounds_rect[4] = {
        ca_layer_overlay.bounds_rect.x(), ca_layer_overlay.bounds_rect.y(),
        ca_layer_overlay.bounds_rect.width(),
        ca_layer_overlay.bounds_rect.height(),
    };
    GLboolean is_clipped = ca_layer_overlay.shared_state->is_clipped;
    GLfloat clip_rect[4] = {ca_layer_overlay.shared_state->clip_rect.x(),
                            ca_layer_overlay.shared_state->clip_rect.y(),
                            ca_layer_overlay.shared_state->clip_rect.width(),
                            ca_layer_overlay.shared_state->clip_rect.height()};
    GLint sorting_context_id =
        ca_layer_overlay.shared_state->sorting_context_id;
    GLfloat transform[16];
    ca_layer_overlay.shared_state->transform.asColMajorf(transform);
    unsigned filter = ca_layer_overlay.filter;

    if (ca_layer_overlay.shared_state != shared_state) {
      shared_state = ca_layer_overlay.shared_state;
      gl_->ScheduleCALayerSharedStateCHROMIUM(
          ca_layer_overlay.shared_state->opacity, is_clipped, clip_rect,
          sorting_context_id, transform);
    }
    gl_->ScheduleCALayerCHROMIUM(
        texture_id, contents_rect, ca_layer_overlay.background_color,
        ca_layer_overlay.edge_aa_mask, bounds_rect, filter);
  }

  // Take the number of copied render passes in this frame, and use 3 times that
  // amount as the cache limit.
  if (overlay_resource_pool_) {
    overlay_resource_pool_->SetResourceUsageLimits(
        std::numeric_limits<std::size_t>::max(), copied_render_pass_count * 5);
  }
}

void GLRenderer::ScheduleDCLayers() {
  if (overlay_resource_pool_) {
    overlay_resource_pool_->CheckBusyResources();
  }

  scoped_refptr<cc::DCLayerOverlaySharedState> shared_state;
  size_t copied_render_pass_count = 0;
  for (cc::DCLayerOverlay& dc_layer_overlay :
       current_frame()->dc_layer_overlay_list) {
    DCHECK(!dc_layer_overlay.rpdq);

    int i = 0;
    unsigned texture_ids[cc::DrawQuad::Resources::kMaxResourceIdCount] = {};
    int ids_to_send = 0;

    for (const auto& contents_resource_id : dc_layer_overlay.resources) {
      if (contents_resource_id) {
        pending_overlay_resources_.push_back(
            base::MakeUnique<cc::ResourceProvider::ScopedReadLockGL>(
                resource_provider_, contents_resource_id));
        texture_ids[i] = pending_overlay_resources_.back()->texture_id();
        ids_to_send = i + 1;
      }
      i++;
    }
    GLfloat contents_rect[4] = {
        dc_layer_overlay.contents_rect.x(), dc_layer_overlay.contents_rect.y(),
        dc_layer_overlay.contents_rect.width(),
        dc_layer_overlay.contents_rect.height(),
    };
    GLfloat bounds_rect[4] = {
        dc_layer_overlay.bounds_rect.x(), dc_layer_overlay.bounds_rect.y(),
        dc_layer_overlay.bounds_rect.width(),
        dc_layer_overlay.bounds_rect.height(),
    };
    GLboolean is_clipped = dc_layer_overlay.shared_state->is_clipped;
    GLfloat clip_rect[4] = {dc_layer_overlay.shared_state->clip_rect.x(),
                            dc_layer_overlay.shared_state->clip_rect.y(),
                            dc_layer_overlay.shared_state->clip_rect.width(),
                            dc_layer_overlay.shared_state->clip_rect.height()};
    GLint z_order = dc_layer_overlay.shared_state->z_order;
    GLfloat transform[16];
    dc_layer_overlay.shared_state->transform.asColMajorf(transform);
    unsigned filter = dc_layer_overlay.filter;

    if (dc_layer_overlay.shared_state != shared_state) {
      shared_state = dc_layer_overlay.shared_state;
      gl_->ScheduleDCLayerSharedStateCHROMIUM(
          dc_layer_overlay.shared_state->opacity, is_clipped, clip_rect,
          z_order, transform);
    }
    if (ids_to_send > 0) {
      gl_->SetColorSpaceForScanoutCHROMIUM(
          texture_ids[0],
          reinterpret_cast<GLColorSpace>(&dc_layer_overlay.color_space));
    }
    gl_->ScheduleDCLayerCHROMIUM(ids_to_send, texture_ids, contents_rect,
                                 dc_layer_overlay.background_color,
                                 dc_layer_overlay.edge_aa_mask, bounds_rect,
                                 filter);
  }

  // Take the number of copied render passes in this frame, and use 3 times that
  // amount as the cache limit.
  if (overlay_resource_pool_) {
    overlay_resource_pool_->SetResourceUsageLimits(
        std::numeric_limits<std::size_t>::max(), copied_render_pass_count * 5);
  }
}

void GLRenderer::ScheduleOverlays() {
  if (current_frame()->overlay_list.empty())
    return;

  cc::OverlayCandidateList& overlays = current_frame()->overlay_list;
  for (const cc::OverlayCandidate& overlay : overlays) {
    unsigned texture_id = 0;
    if (overlay.use_output_surface_for_resource) {
      texture_id = output_surface_->GetOverlayTextureId();
      DCHECK(texture_id || IsContextLost());
    } else {
      pending_overlay_resources_.push_back(
          base::MakeUnique<cc::ResourceProvider::ScopedReadLockGL>(
              resource_provider_, overlay.resource_id));
      texture_id = pending_overlay_resources_.back()->texture_id();
    }

    context_support_->ScheduleOverlayPlane(
        overlay.plane_z_order, overlay.transform, texture_id,
        ToNearestRect(overlay.display_rect), overlay.uv_rect);
  }
}

// This function draws the cc::RenderPassDrawQuad into a temporary
// texture/framebuffer, and then copies the result into an IOSurface. The
// inefficient (but simple) way to do this would be to:
//   1. Allocate a framebuffer the size of the screen.
//   2. Draw using all the normal RPDQ draw logic.
//
// Instead, this method does the following:
//   1. Configure parameters as if drawing to a framebuffer the size of the
//   screen. This reuses most of the RPDQ draw logic.
//   2. Update parameters to draw into a framebuffer only as large as needed.
//   3. Fix shader uniforms that were broken by (2).
//
// Then:
//   4. Allocate an IOSurface as the drawing destination.
//   5. Draw the RPDQ.
void GLRenderer::CopyRenderPassDrawQuadToOverlayResource(
    const cc::CALayerOverlay* ca_layer_overlay,
    cc::Resource** resource,
    gfx::RectF* new_bounds) {
  // Don't carry over any GL state from previous cc::RenderPass draw operations.
  ReinitializeGLState();

  cc::ScopedResource* contents_texture =
      render_pass_textures_[ca_layer_overlay->rpdq->render_pass_id].get();
  DCHECK(contents_texture);

  // Configure parameters as if drawing to a framebuffer the size of the
  // screen.
  DrawRenderPassDrawQuadParams params;
  params.quad = ca_layer_overlay->rpdq;
  params.flip_texture = true;
  params.contents_texture = contents_texture;
  params.quad_to_target_transform =
      params.quad->shared_quad_state->quad_to_target_transform;
  params.tex_coord_rect = params.quad->tex_coord_rect;

  // Calculate projection and window matrices using InitializeViewport(). This
  // requires creating a dummy DrawingFrame.
  {
    DrawingFrame dummy_frame;
    gfx::Rect frame_rect(current_frame()->device_viewport_size);
    force_drawing_frame_framebuffer_unflipped_ = true;
    InitializeViewport(&dummy_frame, frame_rect, frame_rect, frame_rect.size());
    force_drawing_frame_framebuffer_unflipped_ = false;
    params.projection_matrix = dummy_frame.projection_matrix;
    params.window_matrix = dummy_frame.window_matrix;
  }

  // Perform basic initialization with the screen-sized viewport.
  if (!InitializeRPDQParameters(&params))
    return;

  if (!UpdateRPDQWithSkiaFilters(&params))
    return;

  // |params.dst_rect| now contain values that reflect a potentially increased
  // size quad.
  gfx::RectF updated_dst_rect = params.dst_rect;

  // Round the size of the IOSurface to a multiple of 64 pixels. This reduces
  // memory fragmentation. https://crbug.com/146070. This also allows IOSurfaces
  // to be more easily reused during a resize operation.
  uint32_t iosurface_multiple = 64;
  uint32_t iosurface_width = cc::MathUtil::UncheckedRoundUp(
      static_cast<uint32_t>(updated_dst_rect.width()), iosurface_multiple);
  uint32_t iosurface_height = cc::MathUtil::UncheckedRoundUp(
      static_cast<uint32_t>(updated_dst_rect.height()), iosurface_multiple);

  *resource = overlay_resource_pool_->AcquireResource(
      gfx::Size(iosurface_width, iosurface_height), ResourceFormat::RGBA_8888,
      current_frame()->current_render_pass->color_space);
  *new_bounds =
      gfx::RectF(updated_dst_rect.x(), updated_dst_rect.y(),
                 (*resource)->size().width(), (*resource)->size().height());

  // Calculate new projection and window matrices for a minimally sized viewport
  // using InitializeViewport(). This requires creating a dummy DrawingFrame.
  {
    DrawingFrame dummy_frame;
    force_drawing_frame_framebuffer_unflipped_ = true;
    gfx::Rect frame_rect =
        gfx::Rect(0, 0, updated_dst_rect.width(), updated_dst_rect.height());
    InitializeViewport(&dummy_frame, frame_rect, frame_rect, frame_rect.size());
    force_drawing_frame_framebuffer_unflipped_ = false;
    params.projection_matrix = dummy_frame.projection_matrix;
    params.window_matrix = dummy_frame.window_matrix;
  }

  // Calculate a new quad_to_target_transform.
  params.quad_to_target_transform = gfx::Transform();
  params.quad_to_target_transform.Translate(-updated_dst_rect.x(),
                                            -updated_dst_rect.y());

  // Antialiasing works by fading out content that is close to the edge of the
  // viewport. All of these values need to be recalculated.
  if (params.use_aa) {
    current_window_space_viewport_ =
        gfx::Rect(0, 0, updated_dst_rect.width(), updated_dst_rect.height());
    gfx::Transform quad_rect_matrix;
    QuadRectTransform(&quad_rect_matrix, params.quad_to_target_transform,
                      updated_dst_rect);
    params.contents_device_transform =
        params.window_matrix * params.projection_matrix * quad_rect_matrix;
    bool clipped = false;
    params.contents_device_transform.FlattenTo2d();
    gfx::QuadF device_layer_quad = cc::MathUtil::MapQuad(
        params.contents_device_transform, SharedGeometryQuad(), &clipped);
    cc::LayerQuad device_layer_edges(device_layer_quad);
    InflateAntiAliasingDistances(device_layer_quad, &device_layer_edges,
                                 params.edge);
  }

  // Establish destination texture.
  cc::ResourceProvider::ScopedWriteLockGL destination(resource_provider_,
                                                      (*resource)->id(), false);
  GLuint temp_fbo;

  gl_->GenFramebuffers(1, &temp_fbo);
  gl_->BindFramebuffer(GL_FRAMEBUFFER, temp_fbo);
  gl_->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                            destination.target(), destination.texture_id(), 0);
  DCHECK(gl_->CheckFramebufferStatus(GL_FRAMEBUFFER) ==
         GL_FRAMEBUFFER_COMPLETE);

  // Clear to 0 to ensure the background is transparent.
  gl_->ClearColor(0, 0, 0, 0);
  gl_->Clear(GL_COLOR_BUFFER_BIT);

  UpdateRPDQTexturesForSampling(&params);
  UpdateRPDQBlendMode(&params);
  ChooseRPDQProgram(&params);
  UpdateRPDQUniforms(&params);

  // Prior to drawing, set up the destination framebuffer and viewport.
  gl_->BindFramebuffer(GL_FRAMEBUFFER, temp_fbo);
  gl_->Viewport(0, 0, updated_dst_rect.width(), updated_dst_rect.height());

  DrawRPDQ(params);
  gl_->DeleteFramebuffers(1, &temp_fbo);
}

void GLRenderer::ScheduleRenderPassDrawQuad(
    const cc::CALayerOverlay* ca_layer_overlay) {
  DCHECK(ca_layer_overlay->rpdq);

  if (!overlay_resource_pool_) {
    overlay_resource_pool_ =
        cc::ResourcePool::CreateForGpuMemoryBufferResources(
            resource_provider_, base::ThreadTaskRunnerHandle::Get().get(),
            gfx::BufferUsage::SCANOUT, base::TimeDelta::FromSeconds(3),
            settings_->disallow_non_exact_resource_reuse);
  }

  cc::Resource* resource = nullptr;
  gfx::RectF new_bounds;
  CopyRenderPassDrawQuadToOverlayResource(ca_layer_overlay, &resource,
                                          &new_bounds);
  if (!resource || !resource->id())
    return;

  pending_overlay_resources_.push_back(
      base::MakeUnique<cc::ResourceProvider::ScopedReadLockGL>(
          resource_provider_, resource->id()));
  unsigned texture_id = pending_overlay_resources_.back()->texture_id();

  // Once a resource is released, it is marked as "busy". It will be
  // available for reuse after the ScopedReadLockGL is destroyed.
  overlay_resource_pool_->ReleaseResource(resource);

  GLfloat contents_rect[4] = {
      ca_layer_overlay->contents_rect.x(), ca_layer_overlay->contents_rect.y(),
      ca_layer_overlay->contents_rect.width(),
      ca_layer_overlay->contents_rect.height(),
  };
  GLfloat bounds_rect[4] = {
      new_bounds.x(), new_bounds.y(), new_bounds.width(), new_bounds.height(),
  };
  GLboolean is_clipped = ca_layer_overlay->shared_state->is_clipped;
  GLfloat clip_rect[4] = {ca_layer_overlay->shared_state->clip_rect.x(),
                          ca_layer_overlay->shared_state->clip_rect.y(),
                          ca_layer_overlay->shared_state->clip_rect.width(),
                          ca_layer_overlay->shared_state->clip_rect.height()};
  GLint sorting_context_id = ca_layer_overlay->shared_state->sorting_context_id;
  SkMatrix44 transform = ca_layer_overlay->shared_state->transform;
  GLfloat gl_transform[16];
  transform.asColMajorf(gl_transform);
  unsigned filter = ca_layer_overlay->filter;

  // The alpha has already been applied when copying the RPDQ to an IOSurface.
  GLfloat alpha = 1;
  gl_->ScheduleCALayerSharedStateCHROMIUM(alpha, is_clipped, clip_rect,
                                          sorting_context_id, gl_transform);
  gl_->ScheduleCALayerCHROMIUM(
      texture_id, contents_rect, ca_layer_overlay->background_color,
      ca_layer_overlay->edge_aa_mask, bounds_rect, filter);
}

void GLRenderer::SetupOverdrawFeedback() {
  gl_->StencilFunc(GL_ALWAYS, 1, 0xffffffff);
  // First two values are ignored as test always passes.
  gl_->StencilOp(GL_KEEP, GL_KEEP, GL_INCR);
  gl_->StencilMask(0xffffffff);
}

void GLRenderer::FlushOverdrawFeedback(const gfx::Rect& output_rect) {
  DCHECK(stencil_shadow_);

  // Test only, keep everything.
  gl_->StencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

  EnsureScissorTestDisabled();
  SetBlendEnabled(true);

  PrepareGeometry(SHARED_BINDING);

  SetUseProgram(ProgramKey::DebugBorder(), gfx::ColorSpace::CreateSRGB());

  gfx::Transform render_matrix;
  render_matrix.Translate(0.5 * output_rect.width() + output_rect.x(),
                          0.5 * output_rect.height() + output_rect.y());
  render_matrix.Scale(output_rect.width(), output_rect.height());
  SetShaderMatrix(current_frame()->projection_matrix * render_matrix);

  // Produce hinting for the amount of overdraw on screen for each pixel by
  // drawing hint colors to the framebuffer based on the current stencil value.
  struct {
    int multiplier;
    GLenum func;
    GLint ref;
    SkColor color;
  } stencil_tests[] = {
      {1, GL_EQUAL, 2, 0x2f0000ff},  // Blue: Overdrawn once.
      {2, GL_EQUAL, 3, 0x2f00ff00},  // Green: Overdrawn twice.
      {3, GL_EQUAL, 4, 0x3fff0000},  // Pink: Overdrawn three times.
      {4, GL_LESS, 4, 0x7fff0000},   // Red: Overdrawn four or more times.
  };

  // Occlusion queries can be expensive, so only collect trace data if we select
  // cc.debug.overdraw.
  bool tracing_enabled;
  TRACE_EVENT_CATEGORY_GROUP_ENABLED(
      TRACE_DISABLED_BY_DEFAULT("cc.debug.overdraw"), &tracing_enabled);

  // Trace only the root render pass.
  if (current_frame()->current_render_pass != current_frame()->root_render_pass)
    tracing_enabled = false;

  // ARB_occlusion_query is required for tracing.
  if (!use_occlusion_query_)
    tracing_enabled = false;

  // Use the current surface area as max result. The effect is that overdraw
  // is reported as a percentage of the output surface size. ie. 2x overdraw
  // for the whole screen is reported as 200.
  int max_result = current_surface_size_.GetArea();
  DCHECK_GT(max_result, 0);

  OverdrawFeedbackCallback overdraw_feedback_callback = base::Bind(
      &GLRenderer::ProcessOverdrawFeedback, weak_ptr_factory_.GetWeakPtr(),
      base::Owned(new std::vector<int>), arraysize(stencil_tests), max_result);

  for (const auto& test : stencil_tests) {
    GLuint query = 0;
    if (tracing_enabled) {
      gl_->GenQueriesEXT(1, &query);
      gl_->BeginQueryEXT(GL_SAMPLES_PASSED_ARB, query);
    }

    gl_->StencilFunc(test.func, test.ref, 0xffffffff);
    // Transparent color unless color-coding of overdraw is enabled.
    SetShaderColor(settings_->show_overdraw_feedback ? test.color : 0, 1.f);
    gl_->DrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    if (query) {
      gl_->EndQueryEXT(GL_SAMPLES_PASSED_ARB);
      context_support_->SignalQuery(
          query,
          base::Bind(overdraw_feedback_callback, query, test.multiplier));
    }
  }
}

void GLRenderer::ProcessOverdrawFeedback(std::vector<int>* overdraw,
                                         size_t num_expected_results,
                                         int max_result,
                                         unsigned query,
                                         int multiplier) {
  unsigned result = 0;
  if (query) {
    gl_->GetQueryObjectuivEXT(query, GL_QUERY_RESULT_EXT, &result);
    gl_->DeleteQueriesEXT(1, &query);
  }

  // Apply multiplier to get the amount of overdraw.
  overdraw->push_back(result * multiplier);

  // Return early if we are expecting more results.
  if (overdraw->size() < num_expected_results)
    return;

  // Report GPU overdraw as a percentage of |max_result|.
  TRACE_COUNTER1(
      TRACE_DISABLED_BY_DEFAULT("cc.debug.overdraw"), "GPU Overdraw",
      (std::accumulate(overdraw->begin(), overdraw->end(), 0) * 100) /
          max_result);
}

}  // namespace viz
