# This file is used to manage the dependencies of the Chromium src repo. It is
# used by gclient to determine what version of each dependency to check out, and
# where.
#
# For more information, please refer to the official documentation:
#   https://sites.google.com/a/chromium.org/dev/developers/how-tos/get-the-code
#
# When adding a new dependency, please update the top-level .gitignore file
# to list the dependency's destination directory.
#
# -----------------------------------------------------------------------------
# Rolling deps
# -----------------------------------------------------------------------------
# All repositories in this file are git-based, using Chromium git mirrors where
# necessary (e.g., a git mirror is used when the source project is SVN-based).
# To update the revision that Chromium pulls for a given dependency:
#
#  # Create and switch to a new branch
#  git new-branch depsroll
#  # Run roll-dep (provided by depot_tools) giving the dep's path and optionally
#  # a regex that will match the line in this file that contains the current
#  # revision. The script ALWAYS rolls the dependency to the latest revision
#  # in origin/master. The path for the dep should start with src/.
#  roll-dep src/third_party/foo_package/src foo_package.git
#  # You should now have a modified DEPS file; commit and upload as normal
#  git commit -a
#  git cl upload


vars = {
  'chromium_git': 'https://chromium.googlesource.com',
  'swiftshader_git': 'https://swiftshader.googlesource.com',
  'pdfium_git': 'https://pdfium.googlesource.com',
  'boringssl_git': 'https://boringssl.googlesource.com',
  'skia_git': 'https://skia.googlesource.com',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling sfntly
  # and whatever else without interference from each other.
  'sfntly_revision': '2439bd08ff93d4dce761dd6b825917938bd35a4f',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling Skia
  # and whatever else without interference from each other.
  'skia_revision': '249ee1f985b4bad78db2d5cfdd14ce38edb2c23a',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling V8
  # and whatever else without interference from each other.
  'v8_revision': '6329c5bddf1f7bae17b2f9a091a3ee94a169e545',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling swarming_client
  # and whatever else without interference from each other.
  'swarming_revision': 'a56c2b39ca23bdf41458421a7f825ddbf3f43f28',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling ANGLE
  # and whatever else without interference from each other.
  'angle_revision': '5788d24bb00c8bd148981b5a6680d0aadd8b8fc2',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling build tools
  # and whatever else without interference from each other.
  'buildtools_revision': 'd511e4d53d6fc8037badbdf4b225c137e94b52fb',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling SwiftShader
  # and whatever else without interference from each other.
  'swiftshader_revision': '4d3efedb10ce60bdb7847336eba0360fdc9af9d3',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling PDFium
  # and whatever else without interference from each other.
  'pdfium_revision': 'de7c9620c37486413e1f7db4567b4b0cea6a857f',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling openmax_dl
  # and whatever else without interference from each other.
  'openmax_dl_revision': '7acede9c039ea5d14cf326f44aad1245b9e674a7',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling BoringSSL
  # and whatever else without interference from each other.
  'boringssl_revision': '0e4a448ab8aa66a38593f68d19fa0a2e340833e4',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling google-toolbox-for-mac
  # and whatever else without interference from each other.
  'google_toolbox_for_mac_revision': '3c3111d3aefe907c8c0f0e933029608d96ceefeb',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling lighttpd
  # and whatever else without interference from each other.
  'lighttpd_revision': '9dfa55d15937a688a92cbf2b7a8621b0927d06eb',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling lss
  # and whatever else without interference from each other.
  'lss_revision': '63f24c8221a229f677d26ebe8f3d1528a9d787ac',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling NaCl
  # and whatever else without interference from each other.
  'nacl_revision': '01a28a6069a98718184e31c10c5880496a1a9d95',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling freetype
  # and whatever else without interference from each other.
  'freetype_revision': '38bdf22bfe68432aebdd33c198a0bd11b4ebb96f',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling catapult
  # and whatever else without interference from each other.
  'catapult_revision': '3ec578154f4f25deb8b42f58b27d380c3b5ee2e9',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling libFuzzer
  # and whatever else without interference from each other.
  'libfuzzer_revision': '16f5f743c188c836d32cdaf349d5d3effb8a3518',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling devtools-node-modules
  # and whatever else without interference from each other.
  'devtools_node_modules_revision': '6226d6cd80aaf2e5295ed460cf73ef6a582e4d78',
  # Three lines of non-changing comments so that
  # the commit queue can handle CLs rolling libprotobuf-mutator
  # and whatever else without interference from each other.
  'libprotobuf-mutator': 'b2c4fb59135711122b800376d8dc46af2fc9d060',
}

# Only these hosts are allowed for dependencies in this DEPS file.
# If you need to add a new host, contact chrome infrastracture team.
allowed_hosts = [
  'android.googlesource.com',
  'boringssl.googlesource.com',
  'chromium.googlesource.com',
  'pdfium.googlesource.com',
  'skia.googlesource.com',
  'swiftshader.googlesource.com',
]

deps = {
  'src/breakpad/src':
    Var('chromium_git') + '/breakpad/breakpad/src.git' + '@' + '7a65a47345a86c9e9a3fbc2e92a756a429a0c82f',

  'src/buildtools':
    Var('chromium_git') + '/chromium/buildtools.git' + '@' +  Var('buildtools_revision'),

  'src/sdch/open-vcdiff':
    Var('chromium_git') + '/external/github.com/google/open-vcdiff.git' + '@' + '2b9bd1fe548520e9355e457a134bab7e2f9c56c0',

  'src/third_party/freetype/src':
    Var('chromium_git') + '/chromium/src/third_party/freetype2.git' + '@' + Var('freetype_revision'),

  'src/third_party/glslang/src':
    Var('chromium_git') + '/external/github.com/google/glslang.git' + '@' + '210c6bf4d8119dc5f8ac21da2d4c87184f7015e0',

  'src/third_party/shaderc/src':
    Var('chromium_git') + '/external/github.com/google/shaderc.git' + '@' + 'cd8793c34907073025af2622c28bcee64e9879a4',

  'src/third_party/SPIRV-Tools/src':
    Var('chromium_git') + '/external/github.com/KhronosGroup/SPIRV-Tools.git' + '@' + '9166854ac93ef81b026e943ccd230fed6c8b8d3c',

  'src/third_party/angle':
    Var('chromium_git') + '/angle/angle.git' + '@' +  Var('angle_revision'),

  'src/third_party/colorama/src':
    Var('chromium_git') + '/external/colorama.git' + '@' + '799604a1041e9b3bc5d2789ecbd7e8db2e18e6b8',

  'src/third_party/depot_tools':
    Var('chromium_git') + '/chromium/tools/depot_tools.git' + '@' + '6d0d04458d9c345bc7d77681996d89d6e5fc742c',

  'src/third_party/googletest/src':
    Var('chromium_git') + '/external/github.com/google/googletest.git' + '@' + '42bc671f47b122fad36db5eccbc06868afdf7862',

  'src/third_party/icu':
    Var('chromium_git') + '/chromium/deps/icu.git' + '@' + '1fec0c83e9ad7f5a075ae0b50af9a3889f54be0e',

  'src/third_party/hunspell_dictionaries':
    Var('chromium_git') + '/chromium/deps/hunspell_dictionaries.git' + '@' + 'dc6e7c25bf47cbfb466e0701fd2728b4a12e79d5',

  'src/third_party/leveldatabase/src':
    Var('chromium_git') + '/external/leveldb.git' + '@' + '8415f00eeedd96934d3578572d3802900e61a556',

  'src/third_party/snappy/src':
    Var('chromium_git') + '/external/github.com/google/snappy.git' + '@' + '513df5fb5a2d51146f409141f9eb8736935cc486',

  'src/tools/gyp':
    Var('chromium_git') + '/external/gyp.git' + '@' + 'd61a9397e668fa9843c4aa7da9e79460fe590bfb',

  'src/tools/swarming_client':
    Var('chromium_git') + '/external/swarming.client.git' + '@' +  Var('swarming_revision'),

  'src/v8':
    Var('chromium_git') + '/v8/v8.git' + '@' +  Var('v8_revision'),

  'src/native_client':
    Var('chromium_git') + '/native_client/src/native_client.git' + '@' + Var('nacl_revision'),

  'src/third_party/sfntly/src':
    Var('chromium_git') + '/external/github.com/googlei18n/sfntly.git' + '@' + Var('sfntly_revision'),

  'src/third_party/skia':
    Var('skia_git') + '/skia.git' + '@' +  Var('skia_revision'),

  'src/tools/page_cycler/acid3':
    Var('chromium_git') + '/chromium/deps/acid3.git' + '@' + '6be0a66a1ebd7ebc5abc1b2f405a945f6d871521',

  'src/chrome/test/data/perf/canvas_bench':
    Var('chromium_git') + '/chromium/canvas_bench.git' + '@' + 'a7b40ea5ae0239517d78845a5fc9b12976bfc732',

  'src/chrome/test/data/perf/frame_rate/content':
    Var('chromium_git') + '/chromium/frame_rate/content.git' + '@' + 'c10272c88463efeef6bb19c9ec07c42bc8fe22b9',

  'src/chrome/test/data/vr/webvr_info':
    Var('chromium_git') + '/external/github.com/toji/webvr.info.git' + '@' + '932fd58075e564727b5525681d6b9293b41a90f7',

  'src/third_party/bidichecker':
    Var('chromium_git') + '/external/bidichecker/lib.git' + '@' + '97f2aa645b74c28c57eca56992235c79850fa9e0',

  'src/third_party/webgl/src':
    Var('chromium_git') + '/external/khronosgroup/webgl.git' + '@' + '72eda82d069da578af04e5c4e8e411ae006b6a18',

  'src/third_party/webdriver/pylib':
    Var('chromium_git') + '/external/selenium/py.git' + '@' + '5fd78261a75fe08d27ca4835fb6c5ce4b42275bd',

  'src/third_party/libvpx/source/libvpx':
    Var('chromium_git') + '/webm/libvpx.git' + '@' +  '8836e46ffd84f3e272ea9e5efefa143c2437829c',

  'src/third_party/ffmpeg':
    Var('chromium_git') + '/chromium/third_party/ffmpeg.git' + '@' + 'a53b8db560396601d286de993a709e1a3e651829',

  'src/third_party/usrsctp/usrsctplib':
    Var('chromium_git') + '/external/github.com/sctplab/usrsctp' + '@' + '76bea922c32b742ecf5c4a0c64dcfb95ab5387c0',

  'src/third_party/libsrtp':
    Var('chromium_git') + '/chromium/deps/libsrtp.git' + '@' + '1d45b8e599dc2db6ea3ae22dbc94a8c504652423',

  'src/third_party/yasm/source/patched-yasm':
    Var('chromium_git') + '/chromium/deps/yasm/patched-yasm.git' + '@' + '7da28c6c7c6a1387217352ce02b31754deb54d2a',

  'src/third_party/libjpeg_turbo':
    Var('chromium_git') + '/chromium/deps/libjpeg_turbo.git' + '@' + 'a1750dbc79a8792dde3d3f7d7d8ac28ba01ac9dd',

  'src/third_party/flac':
    Var('chromium_git') + '/chromium/deps/flac.git' + '@' + '7d0f5b3a173ffe98db08057d1f52b7787569e0a6',

  'src/third_party/flatbuffers/src':
    Var('chromium_git') + '/external/github.com/google/flatbuffers.git' + '@' + '01c50d57a67a52ee3cddd81b54d4647e9123a290',

  'src/third_party/pyftpdlib/src':
    Var('chromium_git') + '/external/pyftpdlib.git' + '@' + '2be6d65e31c7ee6320d059f581f05ae8d89d7e45',

  'src/third_party/scons-2.0.1':
    Var('chromium_git') + '/native_client/src/third_party/scons-2.0.1.git' + '@' + '1c1550e17fc26355d08627fbdec13d8291227067',

  'src/third_party/webrtc':
    Var('chromium_git') + '/external/webrtc/trunk/webrtc.git' + '@' + 'a7461e3086866272cb69fbfa23c7fc8b0321a425', # commit position 19097

  'src/third_party/openmax_dl':
    Var('chromium_git') + '/external/webrtc/deps/third_party/openmax.git' + '@' +  Var('openmax_dl_revision'),

  'src/third_party/jsoncpp/source':
    Var('chromium_git') + '/external/github.com/open-source-parsers/jsoncpp.git' + '@' + 'f572e8e42e22cfcf5ab0aea26574f408943edfa4', # from svn 248

  'src/third_party/libyuv':
    Var('chromium_git') + '/libyuv/libyuv.git' + '@' + '56022ef77fab3ce5a7c8ee44bd095c22a94bff5b',

  'src/third_party/smhasher/src':
    Var('chromium_git') + '/external/smhasher.git' + '@' + 'e87738e57558e0ec472b2fc3a643b838e5b6e88f',

  'src/third_party/libaddressinput/src':
    Var('chromium_git') + '/external/libaddressinput.git' + '@' + '4d18a0d4be9add0dc479e7b939ed8d39f6ec0d73',

  'src/third_party/libphonenumber/dist':
    Var('chromium_git') + '/external/libphonenumber.git' + '@' + 'a4da30df63a097d67e3c429ead6790ad91d36cf4',

  'src/third_party/webpagereplay':
    Var('chromium_git') + '/external/github.com/chromium/web-page-replay.git' + '@' + '3cd3a3f6f06a1b87b14b9162c7eb16d23d141241',

  'src/third_party/pywebsocket/src':
    Var('chromium_git') + '/external/github.com/google/pywebsocket.git' + '@' + '2d7b73c3acbd0f41dcab487ae5c97c6feae06ce2',

  'src/media/cdm/api':
    Var('chromium_git') + '/chromium/cdm.git' + '@' + '6e4c388c0117fe408b66fbede91081fb1018c5fe',

  'src/third_party/mesa/src':
    Var('chromium_git') + '/chromium/deps/mesa.git' + '@' + 'ef811c6bd4de74e13e7035ca882cc77f85793fef',

  'src/third_party/ced/src':
    Var('chromium_git') + '/external/github.com/google/compact_enc_det.git' + '@' + '910cca22d881b02cbc8950fa02ccbcdcfb782456',

  'src/third_party/swiftshader':
    Var('swiftshader_git') + '/SwiftShader.git' + '@' +  Var('swiftshader_revision'),

  'src/third_party/cld_2/src':
    Var('chromium_git') + '/external/github.com/CLD2Owners/cld2.git' + '@' + '84b58a5d7690ebf05a91406f371ce00c3daf31c0',

  'src/third_party/cld_3/src':
    Var('chromium_git') + '/external/github.com/google/cld_3.git' + '@' + 'ae02d6b8a2af41e87c956c7c7d3f651a8b7b9e79',

  'src/third_party/libwebm/source':
    Var('chromium_git') + '/webm/libwebm.git' + '@' + '4956b2dec65352af32dc71bab553acb631c64177',

  'src/third_party/pdfium':
    Var('pdfium_git') + '/pdfium.git' + '@' +  Var('pdfium_revision'),

  'src/third_party/boringssl/src':
    Var('boringssl_git') + '/boringssl.git' + '@' +  Var('boringssl_revision'),

  'src/third_party/dom_distiller_js/dist':
    Var('chromium_git') + '/external/github.com/chromium/dom-distiller-dist.git' + '@' + '232c293a4d3ebcbc4320f642af017ee054b3be93',

  'src/third_party/catapult':
    Var('chromium_git') + '/external/github.com/catapult-project/catapult.git' + '@' +
    Var('catapult_revision'),

  'src/third_party/openh264/src':
    Var('chromium_git') + '/external/github.com/cisco/openh264' + '@' + '0fd88df93c5dcaf858c57eb7892bd27763f0f0ac',

  'src/third_party/re2/src':
    Var('chromium_git') + '/external/github.com/google/re2.git' + '@' + 'ae9cb49a2e2ba95de4f0c6ec5a8afd039996d2c7',

  # Used for building libFuzzers (only supports Linux).
  'src/third_party/libFuzzer/src':
    Var('chromium_git') + '/chromium/llvm-project/llvm/lib/Fuzzer.git' + '@' +  Var('libfuzzer_revision'),

  'src/third_party/libprotobuf-mutator/src':
    Var('chromium_git') + '/external/github.com/google/libprotobuf-mutator.git' + '@' +  Var('libprotobuf-mutator'),

  'src/third_party/visualmetrics/src':
    Var('chromium_git') + '/external/github.com/WPO-Foundation/visualmetrics.git' + '@' +  '1edde9d2fe203229c895b648fdec355917200ad6',
}


deps_os = {
  'win': {
    'src/third_party/cygwin':
      Var('chromium_git') + '/chromium/deps/cygwin.git' + '@' + 'c89e446b273697fadf3a10ff1007a97c0b7de6df',

    'src/third_party/psyco_win32':
      Var('chromium_git') + '/chromium/deps/psyco_win32.git' + '@' + 'f5af9f6910ee5a8075bbaeed0591469f1661d868',

    'src/third_party/bison':
      Var('chromium_git') + '/chromium/deps/bison.git' + '@' + '083c9a45e4affdd5464ee2b224c2df649c6e26c3',

    'src/third_party/gperf':
      Var('chromium_git') + '/chromium/deps/gperf.git' + '@' + 'd892d79f64f9449770443fb06da49b5a1e5d33c1',

    'src/third_party/perl':
      Var('chromium_git') + '/chromium/deps/perl.git' + '@' + 'ac0d98b5cee6c024b0cffeb4f8f45b6fc5ccdb78',

    'src/third_party/lighttpd':
      Var('chromium_git') + '/chromium/deps/lighttpd.git' + '@' + Var('lighttpd_revision'),

    # Parses Windows PE/COFF executable format.
    'src/third_party/pefile':
      Var('chromium_git') + '/external/pefile.git' + '@' + '72c6ae42396cb913bcab63c15585dc3b5c3f92f1',

    # GNU binutils assembler for x86-32.
    'src/third_party/gnu_binutils':
      Var('chromium_git') + '/native_client/deps/third_party/gnu_binutils.git' + '@' + 'f4003433b61b25666565690caf3d7a7a1a4ec436',
    # GNU binutils assembler for x86-64.
    'src/third_party/mingw-w64/mingw/bin':
      Var('chromium_git') + '/native_client/deps/third_party/mingw-w64/mingw/bin.git' + '@' + '3cc8b140b883a9fe4986d12cfd46c16a093d3527',

    # Dependencies used by libjpeg-turbo
    'src/third_party/yasm/binaries':
      Var('chromium_git') + '/chromium/deps/yasm/binaries.git' + '@' + '52f9b3f4b0aa06da24ef8b123058bb61ee468881',

    # Binaries for nacl sdk.
    'src/third_party/nacl_sdk_binaries':
      Var('chromium_git') + '/chromium/deps/nacl_sdk_binaries.git' + '@' + '759dfca03bdc774da7ecbf974f6e2b84f43699a5',
  },
  'ios': {
    'src/ios/third_party/earl_grey/src':
      Var('chromium_git') + '/external/github.com/google/EarlGrey.git' + '@' + 'b8f1ed850b3b8c059d2912ac4c25a2480ddba826',

    'src/ios/third_party/fishhook/src':
      Var('chromium_git') + '/external/github.com/facebook/fishhook.git' + '@' + 'd172d5247aa590c25d0b1885448bae76036ea22c',

    'src/ios/third_party/gcdwebserver/src':
      Var('chromium_git') + '/external/github.com/swisspol/GCDWebServer.git' + '@' + '43555c66627f6ed44817855a0f6d465f559d30e0',

    'src/ios/third_party/material_components_ios/src':
      Var('chromium_git') + '/external/github.com/material-components/material-components-ios.git' + '@' + '4c47b824c4b63c98c7f4e7d54aad4be15e95beb3',

    'src/ios/third_party/material_font_disk_loader_ios/src':
      Var('chromium_git') + '/external/github.com/material-foundation/material-font-disk-loader-ios.git' + '@' + '8e30188777b016182658fbaa0a4a020a48183224',

    'src/ios/third_party/material_roboto_font_loader_ios/src':
      Var('chromium_git') + '/external/github.com/material-foundation/material-roboto-font-loader-ios.git' + '@' + '4aa51e906e5671c71d24e991f1f10d782a58409f',

    'src/ios/third_party/material_sprited_animation_view_ios/src':
      Var('chromium_git') + '/external/github.com/material-foundation/material-sprited-animation-view-ios.git' + '@' + 'c6e16d06bdafd95540c62b3402d9414692fbca81',

    'src/ios/third_party/material_text_accessibility_ios/src':
      Var('chromium_git') + '/external/github.com/material-foundation/material-text-accessibility-ios.git' + '@' + '7340b22cc589101ba0b11516afe4f3a733041951',

    'src/ios/third_party/ochamcrest/src':
      Var('chromium_git') + '/external/github.com/hamcrest/OCHamcrest.git' + '@' + 'd7ee4ecfb6bd13c3c8d364682b6228ccd86e1e1a',

    'src/third_party/google_toolbox_for_mac/src':
      Var('chromium_git') + '/external/github.com/google/google-toolbox-for-mac.git' + '@' + Var('google_toolbox_for_mac_revision'),

    'src/third_party/material_design_icons/src':
      Var('chromium_git') + '/external/github.com/google/material-design-icons.git' + '@' +
      '5ab428852e35dc177a8c37a2df9dc9ccf768c65a',
  },
  'mac': {
    'src/chrome/installer/mac/third_party/xz/xz':
      Var('chromium_git') + '/chromium/deps/xz.git' + '@' + 'eecaf55632ca72e90eb2641376bce7cdbc7284f7',

    'src/third_party/google_toolbox_for_mac/src':
      Var('chromium_git') + '/external/github.com/google/google-toolbox-for-mac.git' + '@' + Var('google_toolbox_for_mac_revision'),

    'src/third_party/lighttpd':
      Var('chromium_git') + '/chromium/deps/lighttpd.git' + '@' + Var('lighttpd_revision'),
  },
  'unix': {
    # Linux, really.
    'src/third_party/xdg-utils':
      Var('chromium_git') + '/chromium/deps/xdg-utils.git' + '@' + 'd80274d5869b17b8c9067a1022e4416ee7ed5e0d',

    'src/third_party/lss':
      Var('chromium_git') + '/linux-syscall-support.git' + '@' + Var('lss_revision'),

    # For Linux and Chromium OS.
    'src/third_party/cros_system_api':
      Var('chromium_git') + '/chromiumos/platform/system_api.git' + '@' + '3eac6aef29eb1219c7db9c7c15d42bcdd8f22cee',

    # Build tools for Chrome OS. Note: This depends on third_party/pyelftools.
    'src/third_party/chromite':
      Var('chromium_git') + '/chromiumos/chromite.git' + '@' + '80232cab3e7df136f6f2b10cc015fe2120694c73',

    # Dependency of chromite.git and skia.
    'src/third_party/pyelftools':
      Var('chromium_git') + '/chromiumos/third_party/pyelftools.git' + '@' + '19b3e610c86fcadb837d252c794cb5e8008826ae',

    'src/third_party/liblouis/src':
      Var('chromium_git') + '/external/liblouis-github.git' + '@' + '5f9c03f2a3478561deb6ae4798175094be8a26c2',

    # Used for embedded builds. CrOS & Linux use the system version.
    'src/third_party/fontconfig/src':
      Var('chromium_git') + '/external/fontconfig.git' + '@' + 'f16c3118e25546c1b749f9823c51827a60aeb5c1',

    # Graphics buffer allocator for Chrome OS.
    'src/third_party/minigbm/src':
      Var('chromium_git') + '/chromiumos/platform/minigbm.git' + '@' + '3909dc011bf32c13858fb80c33a3cce68769cb26',

    # Userspace interface to kernel DRM services.
    'src/third_party/libdrm/src':
      Var('chromium_git') + '/chromiumos/third_party/libdrm.git' + '@' + '0ce18bedd3e62d4784fa755403801934ba171084',

    # Display server protocol for Linux.
    'src/third_party/wayland/src':
      Var('chromium_git') + '/external/anongit.freedesktop.org/git/wayland/wayland.git' + '@' + '1361da9cd5a719b32d978485a29920429a31ed25',

    # Wayland protocols that add functionality not available in the core protocol.
    'src/third_party/wayland-protocols/src':
      Var('chromium_git') + '/external/anongit.freedesktop.org/git/wayland/wayland-protocols.git' + '@' + '26c99346ab5f2273fe5581bc4f6397bbb834f747',

    # The libevdev library (Chrome OS version).
    'src/third_party/libevdev/src':
      Var('chromium_git') + '/chromiumos/platform/libevdev.git' + '@' + '9f7a1961eb4726211e18abd147d5a11a4ea86744',

    # Chrome OS touchpad gestures library.
    'src/third_party/gestures/gestures':
      Var('chromium_git') + '/chromiumos/platform/gestures.git' + '@' + '5a656849c7d2b0d0ddbe0ac6d300c1e2fada0bb4',

    # Wireless Display Software. Used on Chrome OS.
    'src/third_party/wds/src':
      Var('chromium_git') + '/external/github.com/01org/wds' + '@' + 'ac3d8210d95f3000bf5c8e16a79dbbbf22d554a5',

    # DevTools node modules. Used on Linux buildbots only.
    'src/third_party/WebKit/Source/devtools/devtools-node-modules':
      Var('chromium_git') + '/external/github.com/ChromeDevTools/devtools-node-modules' + '@' + Var('devtools_node_modules_revision'),

    # Minizip library. Used on Chrome OS.
    'src/third_party/minizip/src':
      Var('chromium_git') + '/external/github.com/nmoinvaz/minizip' + '@' + 'dc3ad01e3d5928e9105f770b7e896a8e9fe0d3b4',

    # Android Explicit Synchronization.
    'src/third_party/libsync/src':
      Var('chromium_git') + '/aosp/platform/system/core/libsync.git' + '@' + 'aa6cda6f638bd57d3a024f0d201f723a5c3bb875',
  },
  'android': {
    'src/third_party/android_protobuf/src':
      Var('chromium_git') + '/external/android_protobuf.git' + '@' + '7fca48d8ce97f7ba3ab8eea5c472f1ad3711762f',

    'src/third_party/android_tools':
      Var('chromium_git') + '/android_tools.git' + '@' + 'e9d4018e149d50172ed462a7c21137aa915940ec',

    'src/third_party/apache-portable-runtime/src':
      Var('chromium_git') + '/external/apache-portable-runtime.git' + '@' + 'c76a8c4277e09a82eaa229e35246edea1ee0a6a1',

    'src/third_party/errorprone/lib':
      Var('chromium_git') + '/chromium/third_party/errorprone.git' + '@' + '0eea83b66343133b9c76b7d3288c30321818ebcf',

    'src/third_party/findbugs':
      Var('chromium_git') + '/chromium/deps/findbugs.git' + '@' + '57f05238d3ac77ea0a194813d3065dd780c6e566',

    'src/third_party/elfutils/src':
      Var('chromium_git') + '/external/elfutils.git' + '@' + '249673729a7e5dbd5de4f3760bdcaa3d23d154d7',

    'src/third_party/jsr-305/src':
      Var('chromium_git') + '/external/jsr-305.git' + '@' + '642c508235471f7220af6d5df2d3210e3bfc0919',

    'src/third_party/junit/src':
      Var('chromium_git') + '/external/junit.git' + '@' + '64155f8a9babcfcf4263cf4d08253a1556e75481',

    'src/third_party/mockito/src':
      Var('chromium_git') + '/external/mockito/mockito.git' + '@' + 'de83ad4598ad4cf5ea53c69a8a8053780b04b850',

    'src/third_party/netty-tcnative/src':
      Var('chromium_git') + '/external/netty-tcnative.git' + '@' + '5b46a8ef4a39c39c576fcdaaf718b585d75df463',

    'src/third_party/netty4/src':
      Var('chromium_git') + '/external/netty4.git' + '@' + 'cc4420b13bb4eeea5b1cf4f93b2755644cd3b120',

    'src/third_party/robolectric/robolectric':
      Var('chromium_git') + '/external/robolectric.git' + '@' + '0ccaf33cf6a6d98cbef7d0f3604fda45b13cce54',

    'src/third_party/ub-uiautomator/lib':
      Var('chromium_git') + '/chromium/third_party/ub-uiautomator.git' + '@' + '00270549ce3161ae72ceb24712618ea28b4f9434',

    'src/third_party/leakcanary/src':
      Var('chromium_git') + '/external/github.com/square/leakcanary.git' + '@' + '608ded739e036a3aa69db47ac43777dcee506f8e',

    'src/third_party/lss':
      Var('chromium_git') + '/linux-syscall-support.git' + '@' + Var('lss_revision'),

    'src/third_party/requests/src':
      Var('chromium_git') + '/external/github.com/kennethreitz/requests.git' + '@' + 'f172b30356d821d180fa4ecfa3e71c7274a32de4',

    'src/third_party/custom_tabs_client/src':
      Var('chromium_git') + '/external/github.com/GoogleChrome/custom-tabs-client.git' + '@' + 'b1019f46ee9a0a873333958bb23e4260d112c8da',

    'src/third_party/gvr-android-sdk/src':
      Var('chromium_git') + '/external/github.com/googlevr/gvr-android-sdk.git' + '@' + 'a27f768b13682189c23eed69656319db5ef7cbfc',
  },
}

include_rules = [
  # Everybody can use some things.
  # NOTE: THIS HAS TO STAY IN SYNC WITH third_party/DEPS which disallows these.
  '+base',
  '+build',
  '+ipc',

  # Everybody can use headers generated by tools/generate_library_loader.
  '+library_loaders',

  '+testing',
  '+third_party/icu/source/common/unicode',
  '+third_party/icu/source/i18n/unicode',
  '+url',
]


# checkdeps.py shouldn't check include paths for files in these dirs:
skip_child_includes = [
  'breakpad',
  'native_client_sdk',
  'out',
  'sdch',
  'skia',
  'testing',
  'v8',
  'win8',
]


hooks = [
  {
    # This clobbers when necessary (based on get_landmines.py). It must be the
    # first hook so that other things that get/generate into the output
    # directory will not subsequently be clobbered.
    'name': 'landmines',
    'pattern': '.',
    'action': [
        'python',
        'src/build/landmines.py',
    ],
  },
  {
    # Ensure that the DEPS'd "depot_tools" has its self-update capability
    # disabled.
    'name': 'disable_depot_tools_selfupdate',
    'pattern': '.',
    'action': [
        'python',
        'src/third_party/depot_tools/update_depot_tools_toggle.py',
        '--disable',
    ],
  },
  {
    # Ensure that while generating dependencies lists in .gyp files we don't
    # accidentally reference any .pyc files whose corresponding .py files have
    # already been deleted.
    # We should actually try to avoid generating .pyc files, crbug.com/500078.
    'name': 'remove_stale_pyc_files',
    'pattern': '.',
    'action': [
        'python',
        'src/tools/remove_stale_pyc_files.py',
        'src/android_webview/tools',
        'src/build/android',
        'src/gpu/gles2_conform_support',
        'src/infra',
        'src/ppapi',
        'src/printing',
        'src/third_party/catapult',
        'src/third_party/closure_compiler/build',
        'src/third_party/WebKit/Tools/Scripts',  # See http://crbug.com/625877.
        'src/tools',
    ],
  },
  {
    # This downloads binaries for Native Client's newlib toolchain.
    # Done in lieu of building the toolchain from scratch as it can take
    # anywhere from 30 minutes to 4 hours depending on platform to build.
    'name': 'nacltools',
    'pattern': '.',
    'action': [
        'python',
        'src/build/download_nacl_toolchains.py',
        '--mode', 'nacl_core_sdk',
        'sync', '--extract',
    ],
  },
  {
    # Downloads the current stable linux sysroot to build/linux/ if needed.
    # This sysroot updates at about the same rate that the chrome build deps
    # change. This script is a no-op except for linux users who are doing
    # official chrome builds or cross compiling.
    'name': 'sysroot',
    'pattern': '.',
    'action': ['python', 'src/build/linux/sysroot_scripts/install-sysroot.py',
               '--running-as-hook'],
  },
  {
    # Update the Windows toolchain if necessary.
    'name': 'win_toolchain',
    'pattern': '.',
    'action': ['python', 'src/build/vs_toolchain.py', 'update'],
  },
  {
    # Update the Mac toolchain if necessary.
    'name': 'mac_toolchain',
    'pattern': '.',
    'action': ['python', 'src/build/mac_toolchain.py'],
  },
  # Pull binutils for linux, enabled debug fission for faster linking /
  # debugging when used with clang on Ubuntu Precise.
  # https://code.google.com/p/chromium/issues/detail?id=352046
  {
    'name': 'binutils',
    'pattern': 'src/third_party/binutils',
    'action': [
        'python',
        'src/third_party/binutils/download.py',
    ],
  },
  {
    # Pull clang if needed or requested via GYP_DEFINES.
    # Note: On Win, this should run after win_toolchain, as it may use it.
    'name': 'clang',
    'pattern': '.',
    'action': ['python', 'src/tools/clang/scripts/update.py', '--if-needed'],
  },
  {
    # Update LASTCHANGE.
    'name': 'lastchange',
    'pattern': '.',
    'action': ['python', 'src/build/util/lastchange.py',
               '-o', 'src/build/util/LASTCHANGE'],
  },
  {
    # Update skia_commit_hash.h.
    'name': 'lastchange_skia',
    'pattern': '.',
    'action': ['python', 'src/build/util/lastchange.py',
               '-m', 'SKIA_COMMIT_HASH',
               '-s', 'src/third_party/skia',
               '--header', 'src/skia/ext/skia_commit_hash.h'],
  },
  # Pull GN binaries. This needs to be before running GYP below.
  {
    'name': 'gn_win',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-gn',
                '-s', 'src/buildtools/win/gn.exe.sha1',
    ],
  },
  {
    'name': 'gn_mac',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=darwin',
                '--no_auth',
                '--bucket', 'chromium-gn',
                '-s', 'src/buildtools/mac/gn.sha1',
    ],
  },
  {
    'name': 'gn_linux64',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=linux*',
                '--no_auth',
                '--bucket', 'chromium-gn',
                '-s', 'src/buildtools/linux64/gn.sha1',
    ],
  },
  # Pull clang-format binaries using checked-in hashes.
  {
    'name': 'clang_format_win',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'src/buildtools/win/clang-format.exe.sha1',
    ],
  },
  {
    'name': 'clang_format_mac',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=darwin',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'src/buildtools/mac/clang-format.sha1',
    ],
  },
  {
    'name': 'clang_format_linux',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=linux*',
                '--no_auth',
                '--bucket', 'chromium-clang-format',
                '-s', 'src/buildtools/linux64/clang-format.sha1',
    ],
  },
  # Pull order files for the win/clang build.
  {
    'name': 'orderfiles_win',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-browser-clang/orderfiles',
                '-d', 'src/chrome/build',
    ],
  },
  # Pull luci-go binaries (isolate, swarming) using checked-in hashes.
  {
    'name': 'luci-go_win',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-luci',
                '-d', 'src/tools/luci-go/win64',
    ],
  },
  {
    'name': 'luci-go_mac',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=darwin',
                '--no_auth',
                '--bucket', 'chromium-luci',
                '-d', 'src/tools/luci-go/mac64',
    ],
  },
  {
    'name': 'luci-go_linux',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=linux*',
                '--no_auth',
                '--bucket', 'chromium-luci',
                '-d', 'src/tools/luci-go/linux64',
    ],
  },
  {
    'name': 'drmemory',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-drmemory',
                '-s', 'src/third_party/drmemory/drmemory-windows-sfx.exe.sha1',
              ],
  },
  # Pull the Syzygy binaries, used for optimization and instrumentation.
  {
    'name': 'syzygy-binaries',
    'pattern': '.',
    'action': ['python',
               'src/build/get_syzygy_binaries.py',
               '--output-dir=src/third_party/syzygy/binaries',
               '--revision=190dbfe74c6f5b5913820fa66d9176877924d7c5',
               '--overwrite',
               '--copy-dia-binaries',
    ],
  },
  {
    'name': 'apache_win32',
    'pattern': '\\.sha1',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--directory',
                '--recursive',
                '--no_auth',
                '--num_threads=16',
                '--bucket', 'chromium-apache-win32',
                'src/third_party/apache-win32',
    ],
  },
  {
    # Pull sanitizer-instrumented third-party libraries if requested via
    # GYP_DEFINES.
    'name': 'instrumented_libraries',
    'pattern': '\\.sha1',
    'action': ['python', 'src/third_party/instrumented_libraries/scripts/download_binaries.py'],
  },
  {
    "name": "wasm_fuzzer",
    "pattern": ".",
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                "--no_resume",
                "--no_auth",
                "-u",
                "--bucket", "v8-wasm-fuzzer",
                "-s", "src/v8/test/fuzzer/wasm_corpus.tar.gz.sha1",
    ],
  },
  {
    'name': 'devtools_install_node',
    'action': [ 'python',
                'src/third_party/WebKit/Source/devtools/scripts/local_node/node.py',
                '--running-as-hook',
                '--version',
    ],
  },

  # Pull down Node binaries for WebUI toolchain.
  {
    'name': 'node_linux64',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=linux*',
                '--extract',
                '--no_auth',
                '--bucket', 'chromium-nodejs/6.9.4',
                '-s', 'src/third_party/node/linux/node-linux-x64.tar.gz.sha1',
    ],
  },
  {
    'name': 'node_mac',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=darwin',
                '--extract',
                '--no_auth',
                '--bucket', 'chromium-nodejs/6.9.4',
                '-s', 'src/third_party/node/mac/node-darwin-x64.tar.gz.sha1',
    ],
  },
  {
    'name': 'node_win',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--platform=win32',
                '--no_auth',
                '--bucket', 'chromium-nodejs/6.9.4',
                '-s', 'src/third_party/node/win/node.exe.sha1',
    ],
  },

  # Pull down NPM dependencies for WebUI toolchain.
  {
    'name': 'webui_node_modules',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--extract',
                '--no_auth',
                '--bucket', 'chromium-nodejs',
                '-s', 'src/third_party/node/node_modules.tar.gz.sha1',
    ],
  },

  # Download Telemetry's binary dependencies
  {
    'name': 'fetch_telemetry_binary_dependencies',
    'pattern': '.',
    'action': [ 'python',
                'src/tools/perf/conditionally_execute',
                '--gyp-condition', 'fetch_telemetry_dependencies=1',
                'src/third_party/catapult/telemetry/bin/fetch_telemetry_binary_dependencies',
    ],
  },

  # Download checkstyle for use in PRESUBMIT for Java changes.
  # TODO(jbudorick): Move this back down to the android section of hooks_os
  # once it's no longer necessary for the chromium_presubmit bot.
  {
    'name': 'checkstyle',
    'pattern': '.',
    'action': [ 'python',
                'src/third_party/depot_tools/download_from_google_storage.py',
                '--no_resume',
                '--no_auth',
                '--bucket', 'chromium-android-tools/checkstyle',
                '-s', 'src/third_party/checkstyle/checkstyle-8.0-all.jar.sha1'
    ],
  },
]

# Note: These are keyed off target os, not host os. So don't move things here
# that depend on the target os.
hooks_os = {
  'android': [
    {
      # This downloads SDK extras and puts them in the
      # third_party/android_tools/sdk/extras directory.
      'name': 'sdkextras',
      'pattern': '.',
      # When adding a new sdk extras package to download, add the package
      # directory and zip file to .gitignore in third_party/android_tools.
      'action': ['python',
                 'src/build/android/play_services/update.py',
                 'download'
      ],
    },
    {
      'name': 'intellij',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-intellij',
                 '-l', 'third_party/intellij'
      ],
    },
    {
      'name': 'javax_inject',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-javax-inject',
                 '-l', 'third_party/javax_inject'
      ],
    },
    {
      'name': 'hamcrest',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-hamcrest',
                 '-l', 'third_party/hamcrest'
      ],
    },
    {
      'name': 'guava',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-guava',
                 '-l', 'third_party/guava'
      ],
    },
    {
      'name': 'android_support_test_runner',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-android-support-test-runner',
                 '-l', 'third_party/android_support_test_runner'
      ],
    },
    {
      'name': 'byte_buddy',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-byte-buddy',
                 '-l', 'third_party/byte_buddy'
      ],
    },
    {
      'name': 'espresso',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-espresso',
                 '-l', 'third_party/espresso'
      ],
    },
    {
      'name': 'robolectric_libs',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-robolectric',
                 '-l', 'third_party/robolectric'
      ],
    },
    {
      'name': 'apache_velocity',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-apache-velocity',
                 '-l', 'third_party/apache_velocity'
      ],
    },
    {
      'name': 'ow2_asm',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-ow2-asm',
                 '-l', 'third_party/ow2_asm'
      ],
    },
    {
      'name': 'retrolambda',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-android-tools/retrolambda',
                 '-l', 'third_party/retrolambda'
      ],
    },
    {
      'name': 'desugar',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-android-tools/bazel/desugar',
                 '-l', 'third_party/bazel/desugar'
      ],
    },
    {
      'name': 'apk-patch-size-estimator',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-android-tools/apk-patch-size-estimator',
                 '-l', 'third_party/apk-patch-size-estimator/lib'
      ],
    },
    {
      'name': 'icu4j',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-icu4j',
                 '-l', 'third_party/icu4j'
      ],
    },
    {
      'name': 'accessibility_test_framework',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-accessibility-test-framework',
                 '-l', 'third_party/accessibility_test_framework'
      ],
    },
    {
      'name': 'bouncycastle',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-bouncycastle',
                 '-l', 'third_party/bouncycastle'
      ],
    },
    {
      'name': 'sqlite4java',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-sqlite4java',
                 '-l', 'third_party/sqlite4java'
      ],
    },
    {
      'name': 'objenesis',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-objenesis',
                 '-l', 'third_party/objenesis'
      ],
    },
    {
      'name': 'xstream',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/update_deps/update_third_party_deps.py',
                 'download',
                 '-b', 'chromium-robolectric',
                 '-l', 'third_party/xstream'
      ],
    },
    {
      'name': 'gvr_static_shim_android_arm',
      'pattern': '\\.sha1',
    'action': [   'python',
                  'src/third_party/depot_tools/download_from_google_storage.py',
                  '--no_resume',
                  '--platform=linux*',
                  '--no_auth',
                  '--bucket', 'chromium-gvr-static-shim',
                  '-s', 'src/third_party/gvr-android-sdk/libgvr_shim_static_arm.a.sha1',
      ],
    },
    {
      'name': 'gvr_static_shim_android_arm64',
      'pattern': '\\.sha1',
    'action': [   'python',
                  'src/third_party/depot_tools/download_from_google_storage.py',
                  '--no_resume',
                  '--platform=linux*',
                  '--no_auth',
                  '--bucket', 'chromium-gvr-static-shim',
                  '-s', 'src/third_party/gvr-android-sdk/libgvr_shim_static_arm64.a.sha1',
      ],
    },
    {
      'name': 'gvr_common_aar',
      'pattern': '\\.sha1',
    'action': [   'python',
                  'src/third_party/depot_tools/download_from_google_storage.py',
                  '--no_resume',
                  '--platform=linux*',
                  '--no_auth',
                  '--bucket', 'chromium-gvr-static-shim',
                  '-s', 'src/third_party/gvr-android-sdk/common_library.aar.sha1',
      ],
    },
    {
      'name': 'vr_controller_test_api',
      'pattern': '\\.sha1',
    'action': [   'python',
                  'src/third_party/depot_tools/download_from_google_storage.py',
                  '--no_resume',
                  '--platform=linux*',
                  '--no_auth',
                  '--bucket', 'chromium-gvr-static-shim/controller_test_api',
                  '-s', 'src/third_party/gvr-android-sdk/test-libraries/controller_test_api.aar.sha1',
      ],
    },
    # Download VR test APKs only if the environment variable is set
    {
      'name': 'vr_test_apks',
      'pattern': '.',
      'action': [ 'python',
                 'src/third_party/gvr-android-sdk/test-apks/update.py',
      ],
    },
    {
      # Pull doclava binaries if building for Android.
      'name': 'doclava',
      'pattern': '.',
      'action': ['python',
                 'src/build/android/download_doclava.py',
      ],
    },
  ],
  'fuchsia': [
    {
      'name': 'fuchsia_sdk',
      'pattern': '.',
      'action': [
        'python',
        'src/build/fuchsia/update_sdk.py',
        '2bebd264dfe3bec89469a4179a8292b416cdf2fa',
      ],
    },
  ],
}

recursedeps = [
  # buildtools provides clang_format, libc++, and libc++abi
  'src/buildtools',
  # android_tools manages the NDK.
  'src/third_party/android_tools',
  # ANGLE manages DEPS that it also owns the build files for, such as dEQP.
  ("src/third_party/angle", "DEPS.chromium"),
]
