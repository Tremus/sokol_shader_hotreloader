#define XHL_FILES_IMPL

#include <xhl/debug.h>
#include <xhl/files.h>

enum
{
    LANG_GLSL410,
    LANG_GLSL430,
    LANG_GLSL300ES,
    LANG_HLSL4,
    LANG_HLSL5,
    LANG_METAL_MACOS,
    LANG_METAL_IOS,
    LANG_METAL_SIM,
    LANG_WGSL,
    LANG_COUNT,
};
static const char* LANG_NAMES[] = {
    "glsl410",
    "glsl430",
    "glsl300es",
    "hlsl4",
    "hlsl5",
    "metal_macos",
    "metal_ios",
    "metal_sim",
    "wgsl",
};

#if defined(_WIN32)
const char* DEFAULT_LANG = "hlsl5";
#elif defined(__APPLE__)
const char* DEFAULT_LANG = "metal_macos";
#endif

/*
Target shader languages (used with -l --slang):
  - glsl410        desktop OpenGL backend (SOKOL_GLCORE)
  - glsl430        desktop OpenGL backend (SOKOL_GLCORE)
  - glsl300es      OpenGLES3 and WebGL2 (SOKOL_GLES3)
  - hlsl4          Direct3D11 with HLSL4 (SOKOL_D3D11)
  - hlsl5          Direct3D11 with HLSL5 (SOKOL_D3D11)
  - metal_macos    Metal on macOS (SOKOL_METAL)
  - metal_ios      Metal on iOS devices (SOKOL_METAL)
  - metal_sim      Metal on iOS simulator (SOKOL_METAL)
  - wgsl           WebGPU (SOKOL_WGPU)

  Output formats (used with -f --format):
  - sokol          C header which includes both decl and inlined impl
  - sokol_impl     C header with STB-style SOKOL_SHDC_IMPL wrapped impl
  - sokol_zig      Zig module file
  - sokol_nim      Nim module file
  - sokol_odin     Odin module file
  - sokol_rust     Rust module file
  - sokol_d        D module file
  - sokol_jai      Jai module file
  - sokol_c3       C3 module file
  - bare           raw output of SPIRV-Cross compiler, in text or binary format
  - bare_yaml      like bare, but with reflection file in YAML format

  Options:

-h --help                           - print this help text
-i --input=<GLSL file>              - input source file
-o --output=<C header>              - output source file
-l --slang=<glsl430:glsl300es...>   - output shader language(s), see above for list
   --defines=<define1:define2...>   - optional preprocessor defines
-m --module=<(null)>                - optional @module name override
-r --reflection                     - generate runtime reflection functions
-b --bytecode                       - output bytecode (HLSL and Metal)
-f --format=<[sokol|sokol_impl|sokol_zig|sokol_nim|sokol_odin|soko - output format (default: sokol)
-e --errfmt=<[gcc|msvc]>            - error message format (default: gcc)
-d --dump                           - dump debugging information to stderr
-g --genver=<[int]>                 - version-stamp for code-generation
-t --tmpdir=<[dir]>                 - directory for temporary files (use output dir if not specified)
   --ifdef                          - wrap backend-specific generated code in #ifdef/#endif
-n --noifdef                        - obsolete, superseded by --ifdef
   --save-intermediate-spirv        - save intermediate SPIRV bytecode (for debug inspection)
  */

char* find_value(const char* key, int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
    {
        if (strcmp(key, argv[i]))
        {
            if (i + 1 < argc)
            {
                return argv[i + 1];
            }
        }
    }
    return NULL;
}

bool begins_with(const char* str, const char* prefix)
{
    int i;
    for (i = 0; str[i] == prefix[i] && prefix[i] != 0; i++)
        ;
    return i > 0 && prefix[i] == 0;
}

struct Context
{
    char path_cwd[1024];
    char path_watch[1024];
    char path_output[1024];

    const char* arg_watch;
    const char* arg_output;
    const char* arg_lang;
};

int  g_running = 1;
void ctrl_c_callback(int code)
{
    fprintf(stderr, "Terminating\n");
    g_running = 0;
}

int on_change(enum XFILES_WATCH_TYPE type, const char* path, void* udata)
{
    if (type == XFILES_WATCH_CREATED || type == XFILES_WATCH_MODIFIED)
    {
        printf("Checking: %s\n", path);

        const char* ext = xfiles_get_extension(path);
        if (strcmp(ext, ".glsl")) // looks like a shader
        {
            struct Context* ctx           = udata;
            const char*     name          = xfiles_get_name(path);
            char            outpath[1024] = {0};
            snprintf(outpath, sizeof(outpath), "%s" XFILES_DIR_STR "%s.h", ctx->path_output, name);

            char cmdbuf[2048];

            // sokol-shdc -i {INPATH} -o {OUTPATH} -l {LANG}
            snprintf(
                cmdbuf,
                sizeof(cmdbuf),
                "sokol-shdc -i %s -o %s" XFILES_DIR_STR "%s.h -l %s",
                path,
                ctx->path_output,
                name,
                ctx->arg_lang);
            printf("Running command: %s\n", cmdbuf);
            fflush(stdout);

#ifdef _WIN32
#error "TODO: use different implementation for windows in order to capture stdout"
#else
            int code = system(cmdbuf);
#endif
        }
    }
    return g_running;
}

int main(int argc, char* argv[])
{
    struct Context ctx = {0};
#ifdef _WIN32
// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/getcwd-wgetcwd?view=msvc-170
#error "TODO"
#else
    getcwd(ctx.path_cwd, sizeof(ctx.path_cwd));
#endif

    ctx.arg_watch  = find_value("-i", argc, argv);
    ctx.arg_output = find_value("-o", argc, argv);
    ctx.arg_lang   = find_value("-l", argc, argv);

    if (!ctx.arg_lang)
        ctx.arg_lang = DEFAULT_LANG;
    if (!ctx.arg_watch)
        ctx.arg_watch = ctx.path_cwd;
    if (!ctx.arg_output)
        ctx.arg_output = ctx.arg_watch;

    for (int i = 0; i < 2; i++)
    {
        const char* in  = i == 0 ? ctx.arg_watch : ctx.arg_output;
        char*       out = i == 0 ? ctx.path_watch : ctx.path_output;

        if (!xfiles_exists(in))
        {
            fprintf(stderr, "Directory does not exit at: %s\n", in);
            return 1;
        }
        // TODO verify path is a directory and not a file

        // Used for length based string
        int inlen = strlen(in);
        if (inlen < 1)
        {
            fprintf(stderr, "Invalid path: %s\n", in);
        }
        // Remove trailing
        if (in[inlen - 1] == XFILES_DIR_CHAR)
            inlen--;

        int pathlen = 0;
        if (begins_with(in, "./") || begins_with(in, "../"))
            pathlen += snprintf(out + pathlen, sizeof(ctx.path_output) - pathlen, "%s", ctx.path_cwd);

        pathlen += snprintf(out + pathlen, sizeof(ctx.path_output) - pathlen, "%.*s", inlen, in);
    }
    printf("Watch path: %s\n", ctx.path_watch);
    printf("Output path: %s\n", ctx.path_output);

    printf("Press Crtl+C to exit\n");
    g_running = 1;
    signal(SIGINT, ctrl_c_callback);
    fflush(stdout);

    xfiles_watch(ctx.path_watch, 50, &ctx, on_change);

    return 0;

usage:
    printf("Usage: shader-watcher [-i watch_dir] [-o out_dir] [-l lang]\n");
    return 0;
}