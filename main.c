#define XHL_FILES_IMPL

#ifdef NDEBUG
#define XFILES_ASSERT(cond)                                                                                            \
    if (!(cond))                                                                                                       \
    {                                                                                                                  \
        fprintf(stderr, "[Error]: %s - %s:%d" #cond, __FUNCTION__, __LINE__);                                          \
        exit(1);                                                                                                       \
    }
#endif

#if defined(_WIN32)

#define _CRT_SECURE_NO_WARNINGS
#define UNICODE
#define _UNICODE
#include <wchar.h>
const char* DEFAULT_LANG = "hlsl5";

#elif defined(__APPLE__)

const char* DEFAULT_LANG = "metal_macos";

#endif // win/mac

#include <signal.h>
#include <stdio.h>
#include <xhl/debug.h>
#include <xhl/files.h>

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
            // Using 'system()' to call our build command is way simpler, but creates some stdout buffering problems...
            // Windows prefer that you use CreateProcessW.
            // https://learn.microsoft.com/en-us/windows/win32/procthread/creating-processes
            // https://learn.microsoft.com/en-au/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output?redirectedfrom=MSDN
            STARTUPINFO         si;
            PROCESS_INFORMATION pi;
            SECURITY_ATTRIBUTES sa;
            BOOL                ok = 0;
            memset(&si, 0, sizeof(si));
            memset(&pi, 0, sizeof(pi));
            memset(&sa, 0, sizeof(sa));

            sa.nLength              = sizeof(sa);
            sa.bInheritHandle       = TRUE;
            sa.lpSecurityDescriptor = NULL;

            HANDLE hChildStdoutRd, hChildStdoutWr;
            BOOL   CreatePipe_Result = CreatePipe(&hChildStdoutRd, &hChildStdoutWr, &sa, 0);
            XFILES_ASSERT(CreatePipe_Result);
            BOOL SetHandleInformation_Result = SetHandleInformation(hChildStdoutRd, HANDLE_FLAG_INHERIT, 0);
            XFILES_ASSERT(SetHandleInformation_Result);

            si.cb          = sizeof(si);
            si.dwFlags    |= STARTF_USESHOWWINDOW; // Stops a terminal window popping up as it runs the command
            si.hStdError   = hChildStdoutWr;
            si.hStdOutput  = hChildStdoutWr;
            si.dwFlags    |= STARTF_USESTDHANDLES; // Lets us use the stdout pipe

            // Run build command in child process.
            WCHAR wcmdbuf[XFILES_ARRLEN(ctx->cmdbuf)] = {0};
            int   MultiByteToWideChar_Result =
                MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, ctx->cmdbuf, -1, wcmdbuf, XFILES_ARRLEN(wcmdbuf));
            XFILES_ASSERT(MultiByteToWideChar_Result);
            BOOL CreateProcessW_Result = CreateProcessW(0, wcmdbuf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
            XFILES_ASSERT(CreateProcessW_Result);

            // Wait until child process exits
            // https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
            WaitForSingleObject(pi.hProcess, 1000); // 1sec. Shouldn't take longer

#define EXIT_CODE_FORMAT "%lu"
            DWORD code = 0;
            // https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getexitcodeprocess
            GetExitCodeProcess(pi.hProcess, &code);

            // Cleanup process
            CloseHandle(hChildStdoutWr);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);

            char   buffer[4096]  = {0};
            DWORD  bytesRead     = 0;
            DWORD  bytesWritten  = 0;
            HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

            for (;;)
            {
                ok = ReadFile(hChildStdoutRd, buffer, sizeof(buffer), &bytesRead, NULL);
                if (!ok || bytesRead == 0)
                    break;

                ok = WriteFile(hParentStdOut, buffer, bytesRead, &bytesWritten, NULL);
                if (!ok)
                    break;
            }
#else
#define EXIT_CODE_FORMAT "%d"
            int code = system(ctx->cmdbuf);
#endif
            if (code) // non-zero error
            {
                printf("[ERROR] Returned with code: " EXIT_CODE_FORMAT "\n", code);
            }
            else // (code == 0) // Success!
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
    {
        // https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/getcwd-wgetcwd?view=msvc-170
        wchar_t  wcwd[MAX_PATH];
        wchar_t* _wgetcwd_ret = _wgetcwd(wcwd, XFILES_ARRLEN(wcwd));
        XFILES_ASSERT(_wgetcwd_ret);
        // https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-widechartomultibyte
        int WideCharToMultiByte_Return = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wcwd,
            -1,
            ctx.path_cwd,
            XFILES_ARRLEN(ctx.path_cwd),
            NULL,
            NULL);
        XFILES_ASSERT(WideCharToMultiByte_Return);
    }
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
        int strlen_ret = strlen(in);
        XFILES_ASSERT(strlen_ret >= 1);
        // Remove trailing '/' || '\\'
        if (in[strlen_ret - 1] == XFILES_DIR_CHAR)
            strlen_ret--;

        int pathlen = 0;
        if (begins_with(in, "./") || begins_with(in, "../"))
            pathlen += snprintf(out + pathlen, sizeof(ctx.path_output) - pathlen, "%s", ctx.path_cwd);

        pathlen += snprintf(out + pathlen, sizeof(ctx.path_output) - pathlen, "%.*s", strlen_ret, in);
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