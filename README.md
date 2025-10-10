WIP

CLI tool for hotreloading when editing shaders.

Intended for use with `sokol_gfx`.

Watches a directory for changes to `.glsl` code, then compiles them with `sokol-shdc`. `sokol-shdc` will generate a `.h` file, then presumably your C/C++ hotreloader detects this and recompiles/hotreloads your program.
