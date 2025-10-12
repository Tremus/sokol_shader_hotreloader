WIP

CLI tool for hotreloading when editing shaders.

Intended for use with `sokol_gfx`.

Watches a directory for changes to `.glsl` code, then compiles them with `sokol-shdc`. `sokol-shdc` will generate a `.h` file, then presumably your C/C++ hotreloader detects this and recompiles/hotreloads your program.

```
Usage: {PROGRAM_NAME} [-i watch_dir] [-o out_dir] [-l lang]

Command line tool to compile shaders written for sokol-shdc as C header filess
(.h). To learn more about sokol-shdc, visit this URL:
https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md

Shaders are required to use the ".glsl" extension, and the generated
headers use the suffix/extension ".glsl.h"

Options:

  -i/-w     Input/watch directory. Defaults to your current working directory
  -o        Output directory. Defaults generating headers in the same
            directory as the shader.
  -l        Shader language. Use the same options as with sokol-shdc
            eg. glsl430:wgsl:hlsl5:metal_macos:...etc
            Defaults to using metal_macos on macOS, and hlsl5 on Windows

All arguments are optional.
```
