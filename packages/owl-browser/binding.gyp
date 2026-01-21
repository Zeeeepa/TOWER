{
  "targets": [
    {
      "target_name": "owl_browser",
      "sources": [
        "bindings/owl_bindings.cc",
        "src/owl_browser_manager.cc",
        "src/owl_client.cc",
        "src/owl_app.cc",
        "src/owl_automation_handler.cc",
        "src/owl_stealth.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "<(module_root_dir)/include"
      ],
      "libraries": [],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "cflags": [ "-std=c++17" ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "conditions": [
        ["OS=='mac'", {
          "include_dirs": [
            "<(module_root_dir)/third_party/cef_macos"
          ],
          "libraries": [
            "<(module_root_dir)/build/libcef_dll_wrapper/libcef_dll_wrapper/libcef_dll_wrapper.a"
          ],
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "MACOSX_DEPLOYMENT_TARGET": "11.0",
            "OTHER_LDFLAGS": [
              "-F<(module_root_dir)/third_party/cef_macos/Release",
              "-framework 'Chromium Embedded Framework'",
              "-Wl,-rpath,@loader_path/../../../third_party/cef_macos/Release",
              "-lz"
            ]
          }
        }],
        ["OS=='linux'", {
          "include_dirs": [
            "<(module_root_dir)/third_party/cef_linux"
          ],
          "libraries": [
            "<(module_root_dir)/third_party/cef_linux/Release/libcef.so"
          ]
        }],
        ["OS=='win'", {
          "include_dirs": [
            "<(module_root_dir)/third_party/cef_windows"
          ],
          "libraries": [
            "<(module_root_dir)/third_party/cef_windows/Release/libcef.lib"
          ]
        }]
      ]
    }
  ]
}
