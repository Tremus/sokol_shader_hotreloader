#define XHL_FILES_IMPL

#include <xhl/debug.h>
#include <xhl/files.h>

#if defined(_WIN32)
const char* DEFAULT_LANG = "hlsl5";
#elif defined(__APPLE__)
const char* DEFAULT_LANG = "metal_macos";
#endif

struct Context
{
    char path_cwd[1024];
    char path_watch[1024];
    char path_output[1024];

    const char* arg_watch;
    const char* arg_output;
    const char* arg_lang;

    char cmdbuf[2048];
};

int  g_running = 1;
void ctrl_c_callback(int code)
{
    fprintf(stderr, "Terminating\n");
    g_running = 0;
}

// ARG PARSING
int find_key_idx(const char* key, int argc, char* argv[])
{
    for (int i = 0; i < argc; i++)
        if (0 == strcmp(key, argv[i]))
            return i;
    return -1;
}
bool has_key(const char* key, int argc, char* argv[]) { return find_key_idx(key, argc, argv) >= 0; }

char* find_value(const char* key, int argc, char* argv[])
{
    int key_idx = find_key_idx(key, argc, argv);
    if (key_idx >= 0 && key_idx + 1 < argc)
        return argv[key_idx + 1];
    return NULL;
}

bool begins_with(const char* str, const char* prefix)
{
    int i;
    for (i = 0; str[i] == prefix[i] && prefix[i] != 0; i++)
        ;
    return i > 0 && prefix[i] == 0;
}

int on_change(enum XFILES_WATCH_TYPE type, const char* path, void* udata)
{
    if (type == XFILES_WATCH_CREATED || type == XFILES_WATCH_MODIFIED)
    {
        const char* ext = xfiles_get_extension(path);
        if (strcmp(ext, ".glsl") == 0) // looks like a shader
        {
            const char* label = type == XFILES_WATCH_CREATED ? "New File" : "Changed";
            printf("%s: %s\n", label, path);

            struct Context* ctx  = udata;
            const char*     name = xfiles_get_name(path);

            // sokol-shdc -i {INPATH} -o {OUTPATH} -l {LANG}
            if (ctx->arg_output != NULL) // output dir was specified
            {
                snprintf(
                    ctx->cmdbuf,
                    sizeof(ctx->cmdbuf),
                    "sokol-shdc -i %s -o %s" XFILES_DIR_STR "%s.h -l %s",
                    path,
                    ctx->path_output,
                    name,
                    ctx->arg_lang);
            }
            else // if (ctx->arg_output == NULL) // no output dir was specified.
            {
                // Default to generating header in same dir as shader
                snprintf(ctx->cmdbuf, sizeof(ctx->cmdbuf), "sokol-shdc -i %s -o %s.h -l %s", path, path, ctx->arg_lang);
            }
            printf("Running command: %s\n", ctx->cmdbuf);
            fflush(stdout);

#ifdef _WIN32
#error "TODO: use different implementation for windows in order to capture stdout"
#else
            int code = system(ctx->cmdbuf);
#endif
            if (code) // non-zero error
            {
                printf("[ERROR] Returned with code: %d\n", code);
            }
            else // (code == 0)
            {
                if (ctx->arg_output != NULL)
                    printf("Generated: %s" XFILES_DIR_STR "%s.h\n", ctx->path_output, name);
                else
                    printf("Generated: %s.h\n", path);
            }
            fflush(stdout);
        }
    }
    return g_running;
}

int main(int argc, char* argv[])
{
    if (has_key("-h", argc, argv) || has_key("--help", argc, argv))
    {
        const char* executable_name = xfiles_get_name(argv[0]);
        printf(
            "\n"
            "Usage: %s [-i watch_dir] [-o out_dir] [-l lang]\n"
            "\n"
            "Command line tool to compile shaders written for sokol-shdc as C header files\n"
            "(.h). To learn more about sokol-shdc, visit this URL:\n"
            "https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md\n"
            "\n"
            "Shaders are required to use the \".glsl\" extension, and the generated headers\n"
            "use the suffix/extension \".glsl.h\"\n"
            "\n"
            "Options:\n"
            "\n"
            "  -i/-w     Input/watch directory. Defaults to your current working directory\n"
            "  -o        Output directory. Defaults generating headers in the same\n"
            "            directory as the shader.\n"
            "  -l        Shader language. Use the same options as with sokol-shdc\n"
            "            eg. glsl430:wgsl:hlsl5:metal_macos:...etc\n"
            "            Defaults to using metal_macos on macOS, and hlsl5 on Windows\n"
            "\n"
            "All arguments are optional.\n"
            "\n",
            executable_name);
        return 0;
    }

    struct Context ctx = {0};
#ifdef _WIN32
// https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/getcwd-wgetcwd?view=msvc-170
#error "TODO"
#else
    getcwd(ctx.path_cwd, sizeof(ctx.path_cwd));
#endif

    ctx.arg_watch = find_value("-i", argc, argv);
    if (!ctx.arg_watch)
        ctx.arg_watch = find_value("-w", argc, argv);
    ctx.arg_output = find_value("-o", argc, argv);
    ctx.arg_lang   = find_value("-l", argc, argv);

    if (!ctx.arg_lang)
        ctx.arg_lang = DEFAULT_LANG;

    for (int i = 0; i < 2; i++)
    {
        const char* in  = i == 0 ? ctx.arg_watch : ctx.arg_output;
        char*       out = i == 0 ? ctx.path_watch : ctx.path_output;

        if (!in)
            continue;

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
        // Remove trailing '/' || '\\'
        if (in[inlen - 1] == XFILES_DIR_CHAR)
            inlen--;

        int pathlen = 0;
        if (begins_with(in, "./") || begins_with(in, "../"))
            pathlen += snprintf(out + pathlen, sizeof(ctx.path_output) - pathlen, "%s", ctx.path_cwd);

        pathlen += snprintf(out + pathlen, sizeof(ctx.path_output) - pathlen, "%.*s", inlen, in);
    }
    if (ctx.arg_watch == NULL)
        memcpy(ctx.path_watch, ctx.path_cwd, sizeof(ctx.path_cwd));

    printf("Watching directory: %s\n", ctx.path_watch);
    if (ctx.arg_output)
        printf("Output path: %s\n", ctx.path_output);
    else
        printf("Output path: Same directory as source shader\n");

    printf("Press Crtl+C to exit\n");
    signal(SIGINT, ctrl_c_callback);
    fflush(stdout);

    xfiles_watch(ctx.path_watch, 50, &ctx, on_change);

    return 0;
}