// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Multiply-included message file, hence no include guard here.

#include "gpu/command_buffer/common/capabilities.h"
#include "gpu/command_buffer/common/command_buffer.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/gles2_cmd_utils.h"
#include "gpu/gpu_export.h"
#include "ipc/ipc_message_utils.h"
#include "ipc/param_traits_macros.h"
#include "ui/gfx/ipc/geometry/gfx_param_traits.h"
#include "ui/gl/gpu_preference.h"

#undef IPC_MESSAGE_EXPORT
#define IPC_MESSAGE_EXPORT GPU_EXPORT

IPC_ENUM_TRAITS_MAX_VALUE(gpu::error::Error, gpu::error::kErrorLast)
IPC_ENUM_TRAITS_MAX_VALUE(gpu::error::ContextLostReason,
                          gpu::error::kContextLostReasonLast)
IPC_ENUM_TRAITS_MIN_MAX_VALUE(
    gpu::CommandBufferNamespace,
    gpu::CommandBufferNamespace::INVALID,
    gpu::CommandBufferNamespace::NUM_COMMAND_BUFFER_NAMESPACES - 1)
IPC_ENUM_TRAITS_MAX_VALUE(gl::GpuPreference, gl::GpuPreferenceLast)
IPC_ENUM_TRAITS_MAX_VALUE(gpu::gles2::ContextType,
                          gpu::gles2::CONTEXT_TYPE_LAST)
IPC_ENUM_TRAITS_MAX_VALUE(gpu::gles2::ColorSpace, gpu::gles2::COLOR_SPACE_LAST)

IPC_STRUCT_TRAITS_BEGIN(gpu::Capabilities::ShaderPrecision)
  IPC_STRUCT_TRAITS_MEMBER(min_range)
  IPC_STRUCT_TRAITS_MEMBER(max_range)
  IPC_STRUCT_TRAITS_MEMBER(precision)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::Capabilities::PerStagePrecisions)
  IPC_STRUCT_TRAITS_MEMBER(low_int)
  IPC_STRUCT_TRAITS_MEMBER(medium_int)
  IPC_STRUCT_TRAITS_MEMBER(high_int)
  IPC_STRUCT_TRAITS_MEMBER(low_float)
  IPC_STRUCT_TRAITS_MEMBER(medium_float)
  IPC_STRUCT_TRAITS_MEMBER(high_float)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::Capabilities)
  IPC_STRUCT_TRAITS_MEMBER(vertex_shader_precisions)
  IPC_STRUCT_TRAITS_MEMBER(fragment_shader_precisions)
  IPC_STRUCT_TRAITS_MEMBER(max_combined_texture_image_units)
  IPC_STRUCT_TRAITS_MEMBER(max_cube_map_texture_size)
  IPC_STRUCT_TRAITS_MEMBER(max_fragment_uniform_vectors)
  IPC_STRUCT_TRAITS_MEMBER(max_renderbuffer_size)
  IPC_STRUCT_TRAITS_MEMBER(max_texture_image_units)
  IPC_STRUCT_TRAITS_MEMBER(max_texture_size)
  IPC_STRUCT_TRAITS_MEMBER(max_varying_vectors)
  IPC_STRUCT_TRAITS_MEMBER(max_vertex_attribs)
  IPC_STRUCT_TRAITS_MEMBER(max_vertex_texture_image_units)
  IPC_STRUCT_TRAITS_MEMBER(max_vertex_uniform_vectors)
  IPC_STRUCT_TRAITS_MEMBER(num_compressed_texture_formats)
  IPC_STRUCT_TRAITS_MEMBER(num_shader_binary_formats)
  IPC_STRUCT_TRAITS_MEMBER(num_stencil_bits)
  IPC_STRUCT_TRAITS_MEMBER(bind_generates_resource_chromium)

  IPC_STRUCT_TRAITS_MEMBER(max_3d_texture_size)
  IPC_STRUCT_TRAITS_MEMBER(max_array_texture_layers)
  IPC_STRUCT_TRAITS_MEMBER(max_color_attachments)
  IPC_STRUCT_TRAITS_MEMBER(max_combined_fragment_uniform_components)
  IPC_STRUCT_TRAITS_MEMBER(max_combined_uniform_blocks)
  IPC_STRUCT_TRAITS_MEMBER(max_combined_vertex_uniform_components)
  IPC_STRUCT_TRAITS_MEMBER(max_copy_texture_chromium_size)
  IPC_STRUCT_TRAITS_MEMBER(max_draw_buffers)
  IPC_STRUCT_TRAITS_MEMBER(max_element_index)
  IPC_STRUCT_TRAITS_MEMBER(max_elements_indices)
  IPC_STRUCT_TRAITS_MEMBER(max_elements_vertices)
  IPC_STRUCT_TRAITS_MEMBER(max_fragment_input_components)
  IPC_STRUCT_TRAITS_MEMBER(max_fragment_uniform_blocks)
  IPC_STRUCT_TRAITS_MEMBER(max_fragment_uniform_components)
  IPC_STRUCT_TRAITS_MEMBER(max_program_texel_offset)
  IPC_STRUCT_TRAITS_MEMBER(max_samples)
  IPC_STRUCT_TRAITS_MEMBER(max_server_wait_timeout)
  IPC_STRUCT_TRAITS_MEMBER(max_texture_lod_bias)
  IPC_STRUCT_TRAITS_MEMBER(max_transform_feedback_interleaved_components)
  IPC_STRUCT_TRAITS_MEMBER(max_transform_feedback_separate_attribs)
  IPC_STRUCT_TRAITS_MEMBER(max_transform_feedback_separate_components)
  IPC_STRUCT_TRAITS_MEMBER(max_uniform_block_size)
  IPC_STRUCT_TRAITS_MEMBER(max_uniform_buffer_bindings)
  IPC_STRUCT_TRAITS_MEMBER(max_varying_components)
  IPC_STRUCT_TRAITS_MEMBER(max_vertex_output_components)
  IPC_STRUCT_TRAITS_MEMBER(max_vertex_uniform_blocks)
  IPC_STRUCT_TRAITS_MEMBER(max_vertex_uniform_components)
  IPC_STRUCT_TRAITS_MEMBER(min_program_texel_offset)
  IPC_STRUCT_TRAITS_MEMBER(num_extensions)
  IPC_STRUCT_TRAITS_MEMBER(num_program_binary_formats)
  IPC_STRUCT_TRAITS_MEMBER(uniform_buffer_offset_alignment)

  IPC_STRUCT_TRAITS_MEMBER(post_sub_buffer)
  IPC_STRUCT_TRAITS_MEMBER(swap_buffers_with_bounds)
  IPC_STRUCT_TRAITS_MEMBER(commit_overlay_planes)
  IPC_STRUCT_TRAITS_MEMBER(egl_image_external)
  IPC_STRUCT_TRAITS_MEMBER(texture_format_atc)
  IPC_STRUCT_TRAITS_MEMBER(texture_format_bgra8888)
  IPC_STRUCT_TRAITS_MEMBER(texture_format_dxt1)
  IPC_STRUCT_TRAITS_MEMBER(texture_format_dxt5)
  IPC_STRUCT_TRAITS_MEMBER(texture_format_etc1)
  IPC_STRUCT_TRAITS_MEMBER(texture_format_etc1_npot)
  IPC_STRUCT_TRAITS_MEMBER(texture_rectangle)
  IPC_STRUCT_TRAITS_MEMBER(iosurface)
  IPC_STRUCT_TRAITS_MEMBER(texture_usage)
  IPC_STRUCT_TRAITS_MEMBER(texture_storage)
  IPC_STRUCT_TRAITS_MEMBER(discard_framebuffer)
  IPC_STRUCT_TRAITS_MEMBER(sync_query)
  IPC_STRUCT_TRAITS_MEMBER(future_sync_points)
  IPC_STRUCT_TRAITS_MEMBER(blend_equation_advanced)
  IPC_STRUCT_TRAITS_MEMBER(blend_equation_advanced_coherent)
  IPC_STRUCT_TRAITS_MEMBER(texture_rg)
  IPC_STRUCT_TRAITS_MEMBER(texture_norm16)
  IPC_STRUCT_TRAITS_MEMBER(texture_half_float_linear)
  IPC_STRUCT_TRAITS_MEMBER(color_buffer_half_float_rgba)
  IPC_STRUCT_TRAITS_MEMBER(image_ycbcr_422)
  IPC_STRUCT_TRAITS_MEMBER(image_ycbcr_420v)
  IPC_STRUCT_TRAITS_MEMBER(render_buffer_format_bgra8888)
  IPC_STRUCT_TRAITS_MEMBER(occlusion_query)
  IPC_STRUCT_TRAITS_MEMBER(occlusion_query_boolean)
  IPC_STRUCT_TRAITS_MEMBER(timer_queries)
  IPC_STRUCT_TRAITS_MEMBER(surfaceless)
  IPC_STRUCT_TRAITS_MEMBER(flips_vertically)
  IPC_STRUCT_TRAITS_MEMBER(disable_multisampling_color_mask_usage)
  IPC_STRUCT_TRAITS_MEMBER(disable_webgl_rgb_multisampling_usage)
  IPC_STRUCT_TRAITS_MEMBER(msaa_is_slow)
  IPC_STRUCT_TRAITS_MEMBER(disable_one_component_textures)
  IPC_STRUCT_TRAITS_MEMBER(gpu_rasterization)
  IPC_STRUCT_TRAITS_MEMBER(chromium_image_rgb_emulation)
  IPC_STRUCT_TRAITS_MEMBER(emulate_rgb_buffer_with_rgba)
  IPC_STRUCT_TRAITS_MEMBER(software_to_accelerated_canvas_upgrade)
  IPC_STRUCT_TRAITS_MEMBER(dc_layers)
  IPC_STRUCT_TRAITS_MEMBER(use_dc_overlays_for_video)
  IPC_STRUCT_TRAITS_MEMBER(disable_non_empty_post_sub_buffers)
  IPC_STRUCT_TRAITS_MEMBER(avoid_stencil_buffers)

  IPC_STRUCT_TRAITS_MEMBER(major_version)
  IPC_STRUCT_TRAITS_MEMBER(minor_version)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::CommandBuffer::State)
  IPC_STRUCT_TRAITS_MEMBER(get_offset)
  IPC_STRUCT_TRAITS_MEMBER(token)
  IPC_STRUCT_TRAITS_MEMBER(release_count)
  IPC_STRUCT_TRAITS_MEMBER(error)
  IPC_STRUCT_TRAITS_MEMBER(context_lost_reason)
  IPC_STRUCT_TRAITS_MEMBER(generation)
  IPC_STRUCT_TRAITS_MEMBER(set_get_buffer_count)
IPC_STRUCT_TRAITS_END()

IPC_STRUCT_TRAITS_BEGIN(gpu::gles2::ContextCreationAttribHelper)
  IPC_STRUCT_TRAITS_MEMBER(offscreen_framebuffer_size)
  IPC_STRUCT_TRAITS_MEMBER(gpu_preference)
  IPC_STRUCT_TRAITS_MEMBER(alpha_size)
  IPC_STRUCT_TRAITS_MEMBER(blue_size)
  IPC_STRUCT_TRAITS_MEMBER(green_size)
  IPC_STRUCT_TRAITS_MEMBER(red_size)
  IPC_STRUCT_TRAITS_MEMBER(depth_size)
  IPC_STRUCT_TRAITS_MEMBER(stencil_size)
  IPC_STRUCT_TRAITS_MEMBER(samples)
  IPC_STRUCT_TRAITS_MEMBER(sample_buffers)
  IPC_STRUCT_TRAITS_MEMBER(buffer_preserved)
  IPC_STRUCT_TRAITS_MEMBER(bind_generates_resource)
  IPC_STRUCT_TRAITS_MEMBER(fail_if_major_perf_caveat)
  IPC_STRUCT_TRAITS_MEMBER(lose_context_when_out_of_memory)
  IPC_STRUCT_TRAITS_MEMBER(context_type)
  IPC_STRUCT_TRAITS_MEMBER(should_use_native_gmb_for_backbuffer)
  IPC_STRUCT_TRAITS_MEMBER(own_offscreen_surface)
  IPC_STRUCT_TRAITS_MEMBER(single_buffer)
  IPC_STRUCT_TRAITS_MEMBER(color_space)
IPC_STRUCT_TRAITS_END()
