# Copyright 2014 The Crashpad Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

{
  'includes': [
    '../build/crashpad.gypi',
  ],
  'targets': [
    {
      'target_name': 'crashpad_snapshot',
      'type': 'static_library',
      'dependencies': [
        '../client/client.gyp:crashpad_client',
        '../compat/compat.gyp:crashpad_compat',
        '../third_party/mini_chromium/mini_chromium.gyp:base',
        '../util/util.gyp:crashpad_util',
      ],
      'include_dirs': [
        '..',
      ],
      'sources': [
        'capture_memory.cc',
        'capture_memory.h',
        'cpu_architecture.h',
        'cpu_context.cc',
        'cpu_context.h',
        'crashpad_info_client_options.cc',
        'crashpad_info_client_options.h',
        'exception_snapshot.h',
        'handle_snapshot.cc',
        'handle_snapshot.h',
        'linux/cpu_context_linux.cc',
        'linux/cpu_context_linux.h',
        'linux/debug_rendezvous.cc',
        'linux/debug_rendezvous.h',
        'linux/elf_dynamic_array_reader.cc',
        'linux/elf_dynamic_array_reader.h',
        'linux/elf_image_reader.cc',
        'linux/elf_image_reader.h',
        'linux/elf_symbol_table_reader.cc',
        'linux/elf_symbol_table_reader.h',
        'linux/memory_snapshot_linux.cc',
        'linux/memory_snapshot_linux.h',
        'linux/process_reader.cc',
        'linux/process_reader.h',
        'linux/thread_snapshot_linux.cc',
        'linux/thread_snapshot_linux.h',
        'mac/cpu_context_mac.cc',
        'mac/cpu_context_mac.h',
        'mac/exception_snapshot_mac.cc',
        'mac/exception_snapshot_mac.h',
        'mac/mach_o_image_annotations_reader.cc',
        'mac/mach_o_image_annotations_reader.h',
        'mac/mach_o_image_reader.cc',
        'mac/mach_o_image_reader.h',
        'mac/mach_o_image_segment_reader.cc',
        'mac/mach_o_image_segment_reader.h',
        'mac/mach_o_image_symbol_table_reader.cc',
        'mac/mach_o_image_symbol_table_reader.h',
        'mac/memory_snapshot_mac.cc',
        'mac/memory_snapshot_mac.h',
        'mac/module_snapshot_mac.cc',
        'mac/module_snapshot_mac.h',
        'mac/process_reader.cc',
        'mac/process_reader.h',
        'mac/process_snapshot_mac.cc',
        'mac/process_snapshot_mac.h',
        'mac/process_types.cc',
        'mac/process_types.h',
        'mac/process_types/all.proctype',
        'mac/process_types/crashpad_info.proctype',
        'mac/process_types/crashreporterclient.proctype',
        'mac/process_types/custom.cc',
        'mac/process_types/dyld_images.proctype',
        'mac/process_types/flavors.h',
        'mac/process_types/internal.h',
        'mac/process_types/loader.proctype',
        'mac/process_types/nlist.proctype',
        'mac/process_types/traits.h',
        'mac/system_snapshot_mac.cc',
        'mac/system_snapshot_mac.h',
        'mac/thread_snapshot_mac.cc',
        'mac/thread_snapshot_mac.h',
        'memory_snapshot.h',
        'minidump/minidump_simple_string_dictionary_reader.cc',
        'minidump/minidump_simple_string_dictionary_reader.h',
        'minidump/minidump_string_list_reader.cc',
        'minidump/minidump_string_list_reader.h',
        'minidump/minidump_string_reader.cc',
        'minidump/minidump_string_reader.h',
        'minidump/module_snapshot_minidump.cc',
        'minidump/module_snapshot_minidump.h',
        'minidump/process_snapshot_minidump.cc',
        'minidump/process_snapshot_minidump.h',
        'module_snapshot.h',
        'process_snapshot.h',
        'system_snapshot.h',
        'thread_snapshot.h',
        'unloaded_module_snapshot.cc',
        'unloaded_module_snapshot.h',
        'win/cpu_context_win.cc',
        'win/cpu_context_win.h',
        'win/exception_snapshot_win.cc',
        'win/exception_snapshot_win.h',
        'win/capture_memory_delegate_win.cc',
        'win/capture_memory_delegate_win.h',
        'win/memory_map_region_snapshot_win.cc',
        'win/memory_map_region_snapshot_win.h',
        'win/memory_snapshot_win.cc',
        'win/memory_snapshot_win.h',
        'win/module_snapshot_win.cc',
        'win/module_snapshot_win.h',
        'win/pe_image_annotations_reader.cc',
        'win/pe_image_annotations_reader.h',
        'win/pe_image_reader.cc',
        'win/pe_image_reader.h',
        'win/pe_image_resource_reader.cc',
        'win/pe_image_resource_reader.h',
        'win/process_reader_win.cc',
        'win/process_reader_win.h',
        'win/process_snapshot_win.cc',
        'win/process_snapshot_win.h',
        'win/process_subrange_reader.cc',
        'win/process_subrange_reader.h',
        'win/system_snapshot_win.cc',
        'win/system_snapshot_win.h',
        'win/thread_snapshot_win.cc',
        'win/thread_snapshot_win.h',
      ],
      'conditions': [
        ['OS=="win"', {
          'link_settings': {
            'libraries': [
              '-lpowrprof.lib',
            ],
          },
        }],
        ['OS=="linux" or OS=="android"', {
          'sources!': [
            'capture_memory.cc',
            'capture_memory.h',
          ],
        }],
      ],
      'target_conditions': [
        ['OS=="android"', {
          'sources/': [
            ['include', '^linux/'],
          ],
        }],
      ],
    },
    {
      'variables': {
        'conditions': [
          ['OS == "win"', {
            'snapshot_api_target_type%': 'static_library',
          }, {
            # There are no source files except on Windows.
            'snapshot_api_target_type%': 'none',
          }],
        ],
      },
      'target_name': 'crashpad_snapshot_api',
      'type': '<(snapshot_api_target_type)',
      'dependencies': [
        'crashpad_snapshot',
        '../compat/compat.gyp:crashpad_compat',
        '../third_party/mini_chromium/mini_chromium.gyp:base',
        '../util/util.gyp:crashpad_util',
      ],
      'include_dirs': [
        '..',
      ],
      'sources': [
        'api/module_annotations_win.cc',
        'api/module_annotations_win.h',
      ],
    },
  ],
}
