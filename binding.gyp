{
  "targets": [
    {
      "target_name": "pdfium",
      "sources": [
        "src/pdfium_addon.cc",
        "src/stb_image_write.cc"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "deps/pdfium/include",
        "src"
      ],
      "defines": [
        "NAPI_VERSION=8"
      ],
      "cflags!": [
        "-fno-exceptions"
      ],
      "cflags": [
        "-Os",
        "-flto",
        "-ffunction-sections",
        "-fdata-sections"
      ],
      "cflags_cc!": [
        "-fno-exceptions"
      ],
      "cflags_cc": [
        "-std=c++17",
        "-fvisibility=hidden"
      ],
      "conditions": [
        [
          "OS=='mac'",
          {
            "xcode_settings": {
              "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
              "CLANG_CXX_LANGUAGE_STANDARD": "c++17",
              "GCC_SYMBOLS_PRIVATE_EXTERN": "YES",
              "DEAD_CODE_STRIPPING": "YES",
              "GCC_OPTIMIZATION_LEVEL": "s",
              "LLVM_LTO": "YES",
              "OTHER_LDFLAGS": [
                "-L<(module_root_dir)/deps/pdfium/lib",
                "-lpdfium",
                "-Wl,-rpath,@loader_path",
                "-Wl,-dead_strip",
                "-Wl,-S",
                "-flto",
                "-framework CoreFoundation",
                "-framework CoreGraphics"
              ]
            }
          }
        ],
        [
          "OS=='linux'",
          {
            "libraries": [
              "-L<(module_root_dir)/deps/pdfium/lib",
              "-lpdfium",
              "-Wl,-rpath,'$$ORIGIN'",
              "-Wl,--gc-sections",
              "-Wl,-S",
              "-flto",
              "-lpthread",
              "-ldl"
            ]
          }
        ],
        [
          "OS=='win'",
          {
            "defines": [
              "_HAS_EXCEPTIONS=1"
            ],
            "msvs_settings": {
              "VCCLCompilerTool": {
                "ExceptionHandling": 1,
                "Optimization": 1,
                "WholeProgramOptimization": "true",
                "AdditionalOptions": [
                  "/std:c++17"
                ]
              },
              "VCLinkerTool": {
                "LinkTimeCodeGeneration": 1
              }
            },
            "libraries": [
              "<(module_root_dir)/deps/pdfium/lib/pdfium.dll.lib"
            ],
            "copies": [
              {
                "destination": "<(module_root_dir)/build/Release",
                "files": [
                  "<(module_root_dir)/deps/pdfium/bin/pdfium.dll"
                ]
              }
            ]
          }
        ]
      ]
    }
  ]
}