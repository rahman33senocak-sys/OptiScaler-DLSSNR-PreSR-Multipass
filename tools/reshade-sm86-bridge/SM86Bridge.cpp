#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <cstdint>
#include <cwchar>

namespace
{
    HMODULE g_sm86 = nullptr;

    using LdrLoadDll_t = NTSTATUS (NTAPI *)(PWSTR, PULONG, PUNICODE_STRING, PHANDLE);
    using RtlNtStatusToDosError_t = ULONG (WINAPI *)(NTSTATUS);

    template <typename T, size_t N>
    constexpr size_t CountOf(T (&)[N]) noexcept { return N; }

    bool GetExeDirectory(wchar_t *out, size_t outCount)
    {
        if (!out || outCount == 0)
            return false;

        DWORD len = GetModuleFileNameW(nullptr, out, static_cast<DWORD>(outCount));
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

    void WriteLog(const wchar_t *message)
    {
        wchar_t base[MAX_PATH] = {};
        if (!GetExeDirectory(base, CountOf(base)))
            return;

        wchar_t path[MAX_PATH] = {};
        swprintf_s(path, L"%sSM86Bridge.log", base);

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
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            message ? message : L"");

        char utf8[4096] = {};
        int bytes = WideCharToMultiByte(
            CP_UTF8, 0, line, -1, utf8,
            static_cast<int>(sizeof(utf8)), nullptr, nullptr);

        if (bytes > 1)
        {
            DWORD written = 0;
            WriteFile(file, utf8, static_cast<DWORD>(bytes - 1), &written, nullptr);
        }

        CloseHandle(file);
        OutputDebugStringW(line);
    }

    bool SamePath(const wchar_t *a, const wchar_t *b)
    {
        wchar_t pa[MAX_PATH] = {};
        wchar_t pb[MAX_PATH] = {};

        DWORD na = GetFullPathNameW(a, static_cast<DWORD>(CountOf(pa)), pa, nullptr);
        DWORD nb = GetFullPathNameW(b, static_cast<DWORD>(CountOf(pb)), pb, nullptr);
        if (na == 0 || nb == 0 || na >= CountOf(pa) || nb >= CountOf(pb))
            return false;

        return _wcsicmp(pa, pb) == 0;
    }

    bool VerifyLoadedModule(HMODULE module, const wchar_t *expected)
    {
        if (!module)
            return false;

        wchar_t actual[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(module, actual, static_cast<DWORD>(CountOf(actual)));
        if (len == 0 || len >= CountOf(actual))
        {
            WriteLog(L"Loaded a module, but GetModuleFileNameW failed.");
            return false;
        }

        wchar_t msg[2048] = {};
        swprintf_s(msg, L"Loader returned module: %s", actual);
        WriteLog(msg);

        if (!SamePath(actual, expected))
        {
            swprintf_s(msg, L"WARNING: resolved module is not the requested SM86 proxy. Expected: %s", expected);
            WriteLog(msg);
            return false;
        }

        return true;
    }

    HMODULE LoadViaNtdll(const wchar_t *fullPath)
    {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll)
        {
            WriteLog(L"ntdll.dll is not loaded.");
            return nullptr;
        }

        auto ldrLoadDll = reinterpret_cast<LdrLoadDll_t>(
            GetProcAddress(ntdll, "LdrLoadDll"));
        auto rtlNtStatusToDosError = reinterpret_cast<RtlNtStatusToDosError_t>(
            GetProcAddress(ntdll, "RtlNtStatusToDosError"));

        if (!ldrLoadDll)
        {
            WriteLog(L"LdrLoadDll export not found.");
            return nullptr;
        }

        UNICODE_STRING name {};
        name.Buffer = const_cast<PWSTR>(fullPath);
        name.Length = static_cast<USHORT>(wcslen(fullPath) * sizeof(wchar_t));
        name.MaximumLength = name.Length + sizeof(wchar_t);

        HANDLE handle = nullptr;
        NTSTATUS status = ldrLoadDll(nullptr, nullptr, &name, &handle);

        wchar_t msg[1024] = {};
        if (status < 0 || !handle)
        {
            ULONG dos = rtlNtStatusToDosError ? rtlNtStatusToDosError(status) : 0;
            swprintf_s(
                msg,
                L"LdrLoadDll failed: NTSTATUS=0x%08X, Win32=%lu",
                static_cast<unsigned int>(status),
                static_cast<unsigned long>(dos));
            WriteLog(msg);
            return nullptr;
        }

        HMODULE module = reinterpret_cast<HMODULE>(handle);
        swprintf_s(msg, L"LdrLoadDll succeeded: module=0x%p", module);
        WriteLog(msg);
        return module;
    }

    HMODULE LoadViaKernel32(const wchar_t *fullPath)
    {
        SetLastError(ERROR_SUCCESS);

        // Deliberately use plain LoadLibraryW with a full path.
        // ReShade's LoadFromDllMain path uses LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR,
        // which can alter dependency resolution for a proxy named version.dll.
        HMODULE module = LoadLibraryW(fullPath);
        if (module)
        {
            wchar_t msg[512] = {};
            swprintf_s(msg, L"LoadLibraryW fallback succeeded: module=0x%p", module);
            WriteLog(msg);
            return module;
        }

        DWORD err = GetLastError();
        wchar_t msg[1024] = {};
        swprintf_s(msg, L"LoadLibraryW fallback failed: Win32 error=%lu", err);
        WriteLog(msg);
        return nullptr;
    }

    void LoadSm86Early()
    {
        wchar_t base[MAX_PATH] = {};
        if (!GetExeDirectory(base, CountOf(base)))
        {
            WriteLog(L"Could not resolve game executable directory.");
            return;
        }

        wchar_t target[MAX_PATH] = {};
        swprintf_s(target, L"%sSM86\\version.dll", base);

        wchar_t msg[2048] = {};
        swprintf_s(msg, L"Target SM86 proxy: %s", target);
        WriteLog(msg);

        DWORD attrs = GetFileAttributesW(target);
        if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        {
            WriteLog(L"SM86\\version.dll not found.");
            return;
        }

        // Avoid duplicate load of the exact file if this bridge is loaded twice.
        HMODULE existing = nullptr;
        if (GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                target,
                &existing) &&
            existing)
        {
            WriteLog(L"SM86 proxy already loaded.");
            g_sm86 = existing;
            return;
        }

        WriteLog(L"Attempt 1: direct ntdll LdrLoadDll (bypasses ReShade LoadLibrary hooks).");
        HMODULE module = LoadViaNtdll(target);

        if (module && VerifyLoadedModule(module, target))
        {
            g_sm86 = module;
            WriteLog(L"SM86 proxy loaded successfully via LdrLoadDll.");
            return;
        }

        if (module)
        {
            // Do not unload under loader lock. Keep it resident and continue diagnostics.
            WriteLog(L"LdrLoadDll returned a different module; leaving it loaded and trying fallback.");
        }

        WriteLog(L"Attempt 2: plain LoadLibraryW(full path), without DLL_LOAD_DIR flags.");
        module = LoadViaKernel32(target);

        if (module && VerifyLoadedModule(module, target))
        {
            g_sm86 = module;
            WriteLog(L"SM86 proxy loaded successfully via LoadLibraryW fallback.");
            return;
        }

        WriteLog(L"SM86 proxy could not be loaded. Check the errors above.");
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        WriteLog(L"SM86 Bridge attached in ReShade DllMain stage.");
        LoadSm86Early();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        WriteLog(L"SM86 Bridge detached. SM86 is intentionally not unloaded.");
    }

    return TRUE;
}
