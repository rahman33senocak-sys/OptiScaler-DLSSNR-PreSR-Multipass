#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cwchar>

extern "C" __declspec(dllexport) const char *NAME = "SM86 Loader";
extern "C" __declspec(dllexport) const char *DESCRIPTION =
    "Loads dlssg_sm86.asi from the game directory or plugins directory through ReShade.";

namespace
{
    HMODULE g_addon = nullptr;
    HMODULE g_reshade = nullptr;
    HMODULE g_sm86 = nullptr;

    using ReShadeRegisterAddon_t = bool (*)(void *, uint32_t);
    using ReShadeUnregisterAddon_t = void (*)(void *);
    using ReShadeLogMessage_t = void (*)(void *, int, const char *);

    ReShadeLogMessage_t g_reshadeLog = nullptr;

    template <typename T, size_t N>
    constexpr size_t CountOf(T (&)[N]) noexcept
    {
        return N;
    }

    bool GetExeDirectory(wchar_t *out, size_t outCount)
    {
        if (out == nullptr || outCount == 0)
            return false;

        const DWORD len = GetModuleFileNameW(nullptr, out, static_cast<DWORD>(outCount));
        if (len == 0 || len >= outCount)
            return false;

        for (DWORD i = len; i > 0; --i)
        {
            if (out[i - 1] == L'\\' || out[i - 1] == L'/')
            {
                out[i] = L'\0';
                return true;
            }
        }

        return false;
    }

    void WriteDiskLog(const wchar_t *message)
    {
        wchar_t base[MAX_PATH] = {};
        if (!GetExeDirectory(base, CountOf(base)))
            return;

        wchar_t path[MAX_PATH] = {};
        if (swprintf_s(path, L"%sSM86Loader.log", base) <= 0)
            return;

        HANDLE file = CreateFileW(
            path,
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);

        if (file == INVALID_HANDLE_VALUE)
            return;

        SYSTEMTIME st {};
        GetLocalTime(&st);

        wchar_t line[2048] = {};
        swprintf_s(
            line,
            L"[%02u:%02u:%02u.%03u] %s\r\n",
            st.wHour,
            st.wMinute,
            st.wSecond,
            st.wMilliseconds,
            message != nullptr ? message : L"");

        DWORD written = 0;
        WriteFile(file, line, static_cast<DWORD>(wcslen(line) * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(file);
    }

    void Log(const wchar_t *message)
    {
        WriteDiskLog(message);

        if (g_reshadeLog == nullptr || message == nullptr)
            return;

        char utf8[2048] = {};
        const int count = WideCharToMultiByte(
            CP_UTF8,
            0,
            message,
            -1,
            utf8,
            static_cast<int>(sizeof(utf8)),
            nullptr,
            nullptr);

        if (count > 0)
            g_reshadeLog(g_addon, 2, utf8);
    }

    HMODULE TryLoad(const wchar_t *path)
    {
        if (path == nullptr || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            return nullptr;

        wchar_t message[2048] = {};
        swprintf_s(message, L"Trying %s", path);
        Log(message);

        HMODULE module = LoadLibraryW(path);
        if (module != nullptr)
        {
            swprintf_s(message, L"Loaded dlssg_sm86.asi at 0x%p from %s", module, path);
            Log(message);
            return module;
        }

        const DWORD error = GetLastError();
        swprintf_s(message, L"LoadLibraryW failed for %s (Win32 error %lu)", path, error);
        Log(message);
        return nullptr;
    }

    HMODULE LoadSm86()
    {
        if (HMODULE existing = GetModuleHandleW(L"dlssg_sm86.asi"))
        {
            wchar_t message[512] = {};
            swprintf_s(message, L"dlssg_sm86.asi is already loaded at 0x%p", existing);
            Log(message);
            return existing;
        }

        wchar_t base[MAX_PATH] = {};
        if (!GetExeDirectory(base, CountOf(base)))
        {
            Log(L"Could not resolve the game executable directory.");
            return nullptr;
        }

        wchar_t candidate[MAX_PATH] = {};
        swprintf_s(candidate, L"%splugins\\dlssg_sm86.asi", base);
        if (HMODULE module = TryLoad(candidate))
            return module;

        swprintf_s(candidate, L"%sdlssg_sm86.asi", base);
        if (HMODULE module = TryLoad(candidate))
            return module;

        Log(L"dlssg_sm86.asi was not found. Expected plugins\\dlssg_sm86.asi or dlssg_sm86.asi beside the game EXE.");
        return nullptr;
    }
}

extern "C" __declspec(dllexport) bool AddonInit(HMODULE addon_module, HMODULE reshade_module)
{
    g_addon = addon_module;
    g_reshade = reshade_module;

    if (g_reshade == nullptr)
        return false;

    auto registerAddon = reinterpret_cast<ReShadeRegisterAddon_t>(
        GetProcAddress(g_reshade, "ReShadeRegisterAddon"));

    if (registerAddon == nullptr)
        return false;

    if (!registerAddon(g_addon, 1))
        return false;

    g_reshadeLog = reinterpret_cast<ReShadeLogMessage_t>(
        GetProcAddress(g_reshade, "ReShadeLogMessage"));

    Log(L"SM86 Loader ReShade add-on initialized.");

    g_sm86 = LoadSm86();

    if (g_sm86 == nullptr)
        Log(L"SM86 Loader stayed active, but dlssg_sm86.asi was not loaded.");
    else
        Log(L"SM86 Loader initialization completed successfully.");

    return true;
}

extern "C" __declspec(dllexport) void AddonUninit(HMODULE addon_module, HMODULE reshade_module)
{
    Log(L"SM86 Loader ReShade add-on unloading.");

    g_sm86 = nullptr;

    if (reshade_module != nullptr)
    {
        auto unregisterAddon = reinterpret_cast<ReShadeUnregisterAddon_t>(
            GetProcAddress(reshade_module, "ReShadeUnregisterAddon"));

        if (unregisterAddon != nullptr)
            unregisterAddon(addon_module);
    }

    g_reshadeLog = nullptr;
    g_reshade = nullptr;
    g_addon = nullptr;
}
