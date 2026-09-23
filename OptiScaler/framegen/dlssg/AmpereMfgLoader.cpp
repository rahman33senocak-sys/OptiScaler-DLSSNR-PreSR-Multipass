#include "pch.h"

#include "AmpereMfgLoader.h"

#include <Config.h>
#include <State.h>
#include <Util.h>
#include <misc/IdentifyGpu.h>
#include <proxies/Ntdll_Proxy.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <vector>

namespace AmpereMfgLoader
{
namespace
{
Status s_status;
bool s_nativeAttempted = false;
bool s_providerAttempted = false;
HMODULE s_module = nullptr;
std::recursive_mutex s_mutex;

std::optional<std::filesystem::path> FindPluginPath()
{
    auto* cfg = Config::Instance();
    const auto basePath = Util::DllPath().parent_path();

    std::vector<std::filesystem::path> candidates;
    if (cfg->PluginPath.has_value())
        candidates.push_back(std::filesystem::path(cfg->PluginPath.value()) / L"dlssg_sm86.asi");

    if (cfg->MainDllPath.has_value())
        candidates.push_back(std::filesystem::path(cfg->MainDllPath.value()) / L"plugins" / L"dlssg_sm86.asi");

    candidates.push_back(basePath / L"OptiScaler" / L"plugins" / L"dlssg_sm86.asi");
    candidates.push_back(basePath / L"plugins" / L"dlssg_sm86.asi");

    // Development-only compatibility with the earlier all-in-one layout.
    if (cfg->MainDllPath.has_value())
        candidates.push_back(std::filesystem::path(cfg->MainDllPath.value()) / L"dlssg_sm86" / L"dlssg_sm86.dll");
    candidates.push_back(basePath / L"OptiScaler" / L"dlssg_sm86" / L"dlssg_sm86.dll");
    candidates.push_back(basePath / L"dlssg_sm86" / L"dlssg_sm86.dll");

    std::error_code ec;
    for (const auto& candidate : candidates)
    {
        if (std::filesystem::exists(candidate, ec) && !ec)
            return candidate;
        ec.clear();
    }

    return std::nullopt;
}

bool ValidateGpu()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    if (gpu.vendorId != VendorId::Nvidia)
    {
        s_status.ErrorMessage = "SM86/SM75 requires an NVIDIA GPU.";
        return false;
    }

    const uint32_t archId = static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id);
    const bool isAmpere = IsAmpereArch(archId) || gpu.name.find("RTX 30") != std::string::npos;
    const bool isTuring = IsTuringArch(archId) || gpu.name.find("RTX 20") != std::string::npos ||
                          gpu.name.find("GTX 16") != std::string::npos;

    if (!isAmpere && !isTuring)
    {
        s_status.ErrorMessage =
            std::format("SM86/SM75 requires RTX 20/Turing or RTX 30/Ampere. Detected arch 0x{:x} ({}).", archId,
                        gpu.name);
        return false;
    }

    return true;
}

bool TrySetupInternal(LoadMode mode)
{
    std::lock_guard lock(s_mutex);
    auto* cfg = Config::Instance();

    if (s_module)
    {
        s_status.PluginLoaded = true;
        return true;
    }

    if (mode == LoadMode::NativeUnlock)
    {
        if (!cfg->FGDLSSGAmpereMfgUnlock.value_or_default())
            return false;
        if (s_nativeAttempted)
            return s_status.PluginLoaded;
        s_nativeAttempted = true;
    }
    else if (mode == LoadMode::Provider)
    {
        if (State::Instance().activeFgOutput != FGOutput::DLSSG ||
            State::Instance().activeFgNvngx != FGNvngxReplacement::SM86)
            return false;
        if (s_providerAttempted)
            return s_status.PluginLoaded;
        s_providerAttempted = true;
    }
    else
    {
        return false;
    }

    s_status.Enabled = true;
    s_status.RequestedMode = mode;
    s_status.ErrorMessage.clear();

#if defined(OPTISCALER_RTX40_MFG)
    if (cfg->FGDLSSGAdaMfgUnlock.value_or_default())
    {
        s_status.ErrorMessage = "SM86/SM75 cannot be combined with the RTX 40 MFG unlock.";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return false;
    }
#endif

    // Provider mode is explicitly an RTX 20/30 path and is initialized after a D3D device exists,
    // so validate the physical GPU there. NativeUnlock must load before slInit and therefore does
    // not wait for OptiScaler's asynchronous GPU enumeration; the SM86 runtime performs its own guard.
    if (mode == LoadMode::Provider)
    {
        if (State::Instance().swapchainApi == API::Vulkan)
        {
            s_status.ErrorMessage = "SM86 provider mode is D3D12-only.";
            LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
            return false;
        }

        if (!ValidateGpu())
        {
            LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
            return false;
        }
    }

    const auto pluginPath = FindPluginPath();
    if (!pluginPath.has_value())
    {
        s_status.PluginFound = false;
        s_status.ErrorMessage = "dlssg_sm86.asi not found in OptiScaler/plugins.";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return false;
    }

    s_status.PluginFound = true;

    const auto iniPath = pluginPath->parent_path() / L"dlssg_sm86.ini";
    try
    {
        std::filesystem::create_directories(iniPath.parent_path());
        std::ofstream iniFile(iniPath, std::ios::out | std::ios::trunc);
        if (!iniFile.is_open())
            throw std::runtime_error("Failed to open dlssg_sm86.ini for writing");

        iniFile << GenerateIniContent(mode);
        iniFile.close();
        if (!iniFile)
            throw std::runtime_error("Could not finish writing dlssg_sm86.ini");

        s_status.IniWritten = true;
    }
    catch (const std::exception& ex)
    {
        s_status.IniWritten = false;
        s_status.ErrorMessage = std::string("Error writing dlssg_sm86.ini: ") + ex.what();
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return false;
    }

    NtdllProxy::Init();
    LOG_INFO("AmpereMfgLoader: loading {} in {} mode", wstring_to_string(pluginPath->wstring()), ModeName(mode));

    s_module = NtdllProxy::LoadLibraryExW_Ldr(pluginPath->c_str(), NULL, 0);
    if (!s_module)
        s_module = LoadLibraryW(pluginPath->c_str());

    if (!s_module)
    {
        const DWORD err = GetLastError();
        s_status.PluginLoaded = false;
        s_status.ErrorMessage = "Failed to load dlssg_sm86 ASI (error " + std::to_string(err) + ").";
        LOG_ERROR("AmpereMfgLoader: {}", s_status.ErrorMessage);
        return false;
    }

    s_status.PluginLoaded = true;
    s_status.LoadedMode = mode;
    s_status.ErrorMessage.clear();

    LOG_INFO("AmpereMfgLoader: SM86 0.3.5 loaded successfully in {} mode; SpoofArchToGame={}", ModeName(mode),
             mode == LoadMode::NativeUnlock ? 1 : 0);
    return true;
}
} // namespace

const char* ModeName(LoadMode mode)
{
    switch (mode)
    {
    case LoadMode::NativeUnlock:
        return "NativeUnlock";
    case LoadMode::Provider:
        return "Provider";
    case LoadMode::None:
    default:
        return "None";
    }
}

Status LastStatus()
{
    std::lock_guard lock(s_mutex);
    return s_status;
}

bool IsPluginAvailable()
{
    std::lock_guard lock(s_mutex);
    return FindPluginPath().has_value();
}

std::string ResolveAutoKernelImage()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    const bool onLinux = State::Instance().isRunningOnLinux || gpu.usesVkd3dProton;
    return ResolveAutoKernelImage(static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id), gpu.name, onLinux);
}

std::string ResolveRouter()
{
    const auto& gpu = IdentifyGpu::getPrimaryGpu();
    return ResolveRouter(static_cast<uint32_t>(gpu.nvidiaArchInfo.architecture_id), gpu.name);
}

std::string GenerateIniContent(LoadMode mode)
{
    auto* cfg = Config::Instance();
    const int maxFrames = cfg->FGDLSSGAmpereMfgMaxFrames.value_or_default();

    std::string kernelImg = cfg->FGDLSSGAmpereMfgKernelImage.value_or("Auto");
    if (kernelImg != "PTX" && kernelImg != "Cubin")
        kernelImg = "Auto";

    const int hwBilinear = cfg->FGDLSSGAmpereMfgHardwareBilinear.value_or_default() ? 1 : 0;
    // 0.3.5 can identify the physical architecture itself. Keep Router=Auto so NativeUnlock can
    // be loaded before OptiScaler's asynchronous GPU enumeration without guessing SM75 vs SM86.
    const std::string router = "Auto";
    constexpr int logLevel = 1;
    const bool spoofArchToGame = mode == LoadMode::NativeUnlock;

    LOG_INFO("AmpereMfgLoader: router Auto (runtime physical-GPU selection)");
    if (maxFrames == 0)
        LOG_INFO("AmpereMfgLoader: SM86 0.3.5 ceiling = runtime default, Optimized=1, mode {}", ModeName(mode));
    else
        LOG_INFO("AmpereMfgLoader: SM86 0.3.5 ceiling {} generated frames ({}X total), Optimized=1, mode {}",
                 maxFrames, maxFrames + 1, ModeName(mode));

    return FormatIniContent(maxFrames, kernelImg, hwBilinear, router, logLevel, spoofArchToGame);
}

bool TrySetupNativeUnlock() { return TrySetupInternal(LoadMode::NativeUnlock); }

bool TrySetupProvider() { return TrySetupInternal(LoadMode::Provider); }

} // namespace AmpereMfgLoader
