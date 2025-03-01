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
      'target_name': 'crashpad_snapshot_test_lib',
      'type': 'static_library',
      'dependencies': [
        'snapshot.gyp:crashpad_snapshot',
        '../compat/compat.gyp:crashpad_compat',
        '../third_party/mini_chromium/mini_chromium.gyp:base',
        '../util/util.gyp:crashpad_util',
      ],
      'include_dirs': [
        '..',
      ],
      'sources': [
        'test/test_cpu_context.cc',
        'test/test_cpu_context.h',
        'test/test_exception_snapshot.cc',
        'test/test_exception_snapshot.h',
        'test/test_memory_map_region_snapshot.cc',
        'test/test_memory_map_region_snapshot.h',
        'test/test_memory_snapshot.cc',
        'test/test_memory_snapshot.h',
        'test/test_module_snapshot.cc',
        'test/test_module_snapshot.h',
        'test/test_process_snapshot.cc',
        'test/test_process_snapshot.h',
        'test/test_system_snapshot.cc',
        'test/test_system_snapshot.h',
        'test/test_thread_snapshot.cc',
        'test/test_thread_snapshot.h',
      ],
    },
    {
      'target_name': 'crashpad_snapshot_test',
      'type': 'executable',
      'dependencies': [
        'crashpad_snapshot_test_module',
        'snapshot.gyp:crashpad_snapshot',
        'snapshot.gyp:crashpad_snapshot_api',
        '../client/client.gyp:crashpad_client',
        '../compat/compat.gyp:crashpad_compat',
        '../test/test.gyp:crashpad_gtest_main',
        '../test/test.gyp:crashpad_test',
        '../third_party/gtest/gtest.gyp:gtest',
        '../third_party/mini_chromium/mini_chromium.gyp:base',
        '../util/util.gyp:crashpad_util',
      ],
      'include_dirs': [
        '..',
      ],
      'sources': [
        'cpu_context_test.cc',
        'crashpad_info_client_options_test.cc',
        'api/module_annotations_win_test.cc',
        'linux/debug_rendezvous_test.cc',
        'linux/elf_image_reader_test.cc',
        'linux/process_reader_test.cc',
        'mac/cpu_context_mac_test.cc',
        'mac/mach_o_image_annotations_reader_test.cc',
        'mac/mach_o_image_reader_test.cc',
        'mac/mach_o_image_segment_reader_test.cc',
        'mac/process_reader_test.cc',
        'mac/process_types_test.cc',
        'mac/system_snapshot_mac_test.cc',
        'minidump/process_snapshot_minidump_test.cc',
        'win/cpu_context_win_test.cc',
        'win/exception_snapshot_win_test.cc',
        'win/extra_memory_ranges_test.cc',
        'win/pe_image_annotations_reader_test.cc',
        'win/pe_image_reader_test.cc',
        'win/process_reader_win_test.cc',
        'win/process_snapshot_win_test.cc',
        'win/system_snapshot_win_test.cc',
      ],
      'conditions': [
        ['OS=="mac"', {
          'dependencies': [
            'crashpad_snapshot_test_module_crashy_initializer',
            'crashpad_snapshot_test_no_op',
          ],
          'link_settings': {
            'libraries': [
              '$(SDKROOT)/System/Library/Frameworks/OpenCL.framework',
            ],
          },
        }],
        ['OS=="win"', {
          'dependencies': [
            'crashpad_snapshot_test_crashing_child',
            'crashpad_snapshot_test_dump_without_crashing',
            'crashpad_snapshot_test_image_reader',
            'crashpad_snapshot_test_image_reader_module',
          ],
        }],
        ['OS=="linux" or OS=="android"', {
          'sources!': [
            'crashpad_info_client_options_test.cc',
          ],
          'copies': [{
            'destination': '<(PRODUCT_DIR)',
            'files': [
              'linux/test_exported_symbols.sym',
            ],
          }],
          'ldflags': [
            '-Wl,--dynamic-list=test_exported_symbols.sym',
          ],
          'link_settings': {
            'libraries': [
              '-ldl',
            ],
          },
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
      'target_name': 'crashpad_snapshot_test_module',
      'type': 'loadable_module',
      'dependencies': [
        '../client/client.gyp:crashpad_client',
        '../third_party/mini_chromium/mini_chromium.gyp:base',
      ],
      'include_dirs': [
        '..',
      ],
      'sources': [
        'crashpad_info_client_options_test_module.cc',
      ],
    },
  ],
  'conditions': [
    ['OS=="mac"', {
      'targets': [
        {
          'target_name': 'crashpad_snapshot_test_module_crashy_initializer',
          'type': 'loadable_module',
          'sources': [
            'mac/mach_o_image_annotations_reader_test_module_crashy_initializer.cc',
          ],
        },
        {
          'target_name': 'crashpad_snapshot_test_no_op',
          'type': 'executable',
          'sources': [
            'mac/mach_o_image_annotations_reader_test_no_op.cc',
          ],
        },
      ],
    }],
    ['OS=="win"', {
      'targets': [
        {
          'target_name': 'crashpad_snapshot_test_crashing_child',
          'type': 'executable',
          'dependencies': [
            '../client/client.gyp:crashpad_client',
            '../compat/compat.gyp:crashpad_compat',
            '../third_party/mini_chromium/mini_chromium.gyp:base',
            '../util/util.gyp:crashpad_util',
          ],
          'sources': [
            'win/crashpad_snapshot_test_crashing_child.cc',
          ],
        },
        {
          'target_name': 'crashpad_snapshot_test_dump_without_crashing',
          'type': 'executable',
          'dependencies': [
            '../client/client.gyp:crashpad_client',
            '../compat/compat.gyp:crashpad_compat',
            '../third_party/mini_chromium/mini_chromium.gyp:base',
            '../util/util.gyp:crashpad_util',
          ],
          'sources': [
            'win/crashpad_snapshot_test_dump_without_crashing.cc',
          ],
        },
        {
          'target_name': 'crashpad_snapshot_test_extra_memory_ranges',
          'type': 'executable',
          'dependencies': [
            '../client/client.gyp:crashpad_client',
            '../compat/compat.gyp:crashpad_compat',
            '../third_party/mini_chromium/mini_chromium.gyp:base',
          ],
          'sources': [
            'win/crashpad_snapshot_test_extra_memory_ranges.cc',
          ],
        },
        {
          'target_name': 'crashpad_snapshot_test_image_reader',
          'type': 'executable',
          'dependencies': [
            '../client/client.gyp:crashpad_client',
            '../compat/compat.gyp:crashpad_compat',
            '../third_party/mini_chromium/mini_chromium.gyp:base',
            '../util/util.gyp:crashpad_util',
          ],
          'sources': [
            'win/crashpad_snapshot_test_image_reader.cc',
          ],
        },
        {
          'target_name': 'crashpad_snapshot_test_image_reader_module',
          'type': 'loadable_module',
          'dependencies': [
            '../client/client.gyp:crashpad_client',
            '../third_party/mini_chromium/mini_chromium.gyp:base',
          ],
          'sources': [
            'win/crashpad_snapshot_test_image_reader_module.cc',
          ],
          'msvs_settings': {
            'NoImportLibrary': 'true',
          },
        },
        {
          'target_name': 'crashpad_snapshot_test_simple_annotations',
          'type': 'executable',
          'dependencies': [
            '../client/client.gyp:crashpad_client',
            '../compat/compat.gyp:crashpad_compat',
            '../third_party/mini_chromium/mini_chromium.gyp:base',
          ],
          'sources': [
            'win/crashpad_snapshot_test_simple_annotations.cc',
          ],
        },
      ],
    }],
  ],
}
