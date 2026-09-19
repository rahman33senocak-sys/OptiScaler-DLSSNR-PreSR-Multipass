#include "pch.h"

#include "Sm86ProxyLoader.h"

#include <Config.h>
#include <Util.h>
#include <proxies/Ntdll_Proxy.h>

#include <filesystem>
#include <fstream>
#include <mutex>

namespace Sm86ProxyLoader
{
namespace
{
Status s_status;
bool s_attempted = false;
std::recursive_mutex s_mutex;
}

Status LastStatus()
{
    std::lock_guard lock(s_mutex);
    return s_status;
}

static std::filesystem::path ResolveProxyPath()
{
    auto basePath = Util::DllPath().parent_path();
    auto* cfg = Config::Instance();

    const auto optiPath = std::filesystem::path(cfg->MainDllPath.value_or(basePath.wstring()));

    const std::filesystem::path candidates[] = {
        // The SM86 package is commonly distributed as version.dll but many OptiScaler
        // setups rename plugin DLLs to .asi so the ASI loader can discover them.
        // LoadLibrary accepts either extension, so support both forms explicitly.
        optiPath / L"plugins" / L"version.asi",
        optiPath / L"plugins" / L"version.dll",
        optiPath / L"dlssg_sm86" / L"version.asi",
        optiPath / L"dlssg_sm86" / L"version.dll",
        basePath / L"OptiScaler" / L"plugins" / L"version.asi",
        basePath / L"OptiScaler" / L"plugins" / L"version.dll",
        basePath / L"OptiScaler" / L"dlssg_sm86" / L"version.asi",
        basePath / L"OptiScaler" / L"dlssg_sm86" / L"version.dll",
        basePath / L"plugins" / L"version.asi",
        basePath / L"plugins" / L"version.dll",
    };

    std::error_code ec;
    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate, ec))
            return candidate;
        ec.clear();
    }

    return {};
}

static bool EnsureIni(const std::filesystem::path& proxyPath)
{
    auto iniPath = proxyPath.parent_path() / L"dlssg_sm86.ini";
    std::error_code ec;

    // Respect a user-supplied INI. The user's SM86 package knows its own runtime best.
    if (std::filesystem::exists(iniPath, ec))
        return true;

    int maxFrames = Config::Instance()->FGDLSSGAmpereMfgMaxFrames.value_or_default();
    if (maxFrames < 1)
        maxFrames = 1;
    if (maxFrames > 5)
        maxFrames = 5;

    std::ofstream out(iniPath, std::ios::out | std::ios::trunc);
    if (!out.is_open())
        return false;

    out << "; OptiScaler v0.8.4-SM86-OptiFG companion configuration\n";
    out << "[General]\nEnabled=1\n\n";
    out << "[FrameGeneration]\n";
    out << "Optimized=1\n";
    out << "MaxGeneratedFrames=" << maxFrames << "\n\n";
    out << "[Compatibility]\nPreset=Auto\n\n";
    out << "[Logging]\nLevel=3\nDirectory=dlssg_sm86\\logs\n\n";
    out << "[Runtime]\nMode=Bundled\nCacheDirectory=\n";
    out.close();

    return static_cast<bool>(out);
}

void TrySetup()
{
    std::lock_guard lock(s_mutex);
    if (s_attempted)
        return;
    s_attempted = true;

    auto* cfg = Config::Instance();
    if (!cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
        return;

    s_status.Enabled = true;

    const auto proxyPath = ResolveProxyPath();
    if (proxyPath.empty())
    {
        s_status.ErrorMessage =
            "SM86 proxy not found. Put it at OptiScaler/plugins/version.asi (preferred for ASI plugin setups), "
            "OptiScaler/plugins/version.dll, or the equivalent OptiScaler/dlssg_sm86/ path.";
        LOG_ERROR("Sm86ProxyLoader: {}", s_status.ErrorMessage);
        return;
    }

    s_status.DllFound = true;
    s_status.IniWritten = EnsureIni(proxyPath);
    if (!s_status.IniWritten)
    {
        s_status.ErrorMessage = "Could not create/read dlssg_sm86.ini beside the SM86 proxy.";
        LOG_ERROR("Sm86ProxyLoader: {}", s_status.ErrorMessage);
        return;
    }

    // Load outside DLL_PROCESS_ATTACH (called by getGpuInfo worker).
    // The proxy installs its own DLSS-G/SM86 routing before OptiScaler creates its DLSSG output.
    NtdllProxy::Init();

    LOG_INFO("Sm86ProxyLoader: loading {}", wstring_to_string(proxyPath.wstring()));
    HMODULE module = NtdllProxy::LoadLibraryExW_Ldr(proxyPath.c_str(), NULL, 0);
    if (!module)
        module = LoadLibraryW(proxyPath.c_str());

    if (!module)
    {
        const DWORD err = GetLastError();
        s_status.ErrorMessage = "LoadLibrary failed for SM86 version.dll, Win32 error " + std::to_string(err) + ".";
        LOG_ERROR("Sm86ProxyLoader: {}", s_status.ErrorMessage);
        return;
    }

    s_status.DllLoaded = true;
    s_status.ErrorMessage.clear();
    LOG_INFO("Sm86ProxyLoader: SM86 proxy loaded successfully; OptiFG -> DLSSG native path remains enabled");
}
} // namespace Sm86ProxyLoader
