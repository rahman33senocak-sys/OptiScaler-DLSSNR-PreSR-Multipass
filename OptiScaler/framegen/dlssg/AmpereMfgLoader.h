#pragma once

#include <cstdint>
#include <sstream>
#include <string>

namespace AmpereMfgLoader
{
enum class LoadMode : uint32_t
{
    None,
    NativeUnlock,
    Provider,
};

struct Status
{
    bool Enabled = false;
    bool PluginFound = false;
    bool IniWritten = false;
    bool PluginLoaded = false;
    LoadMode RequestedMode = LoadMode::None;
    LoadMode LoadedMode = LoadMode::None;
    std::string ErrorMessage;
};

Status LastStatus();

/// Early/native mode: loads SM75/SM86 before the game's Streamline capability decision.
/// This is the meaning of [DLSSG] AmpereMfgUnlock=true.
bool TrySetupNativeUnlock();

/// Provider mode: loads SM75/SM86 only for OptiScaler's own DLSSG output.
/// In this mode SpoofArchToGame=0 so the SM86 proxy itself does not globally advertise RTX 50 to the game.
bool TrySetupProvider();

/// True when the bundled/installed SM86 ASI (or a legacy development DLL) can be found.
bool IsPluginAvailable();

const char* ModeName(LoadMode mode);

/// Formats the pinned dlssg_for_sm86 0.3.5 configuration.
/// 310.9 supports up to five generated frames (6X) when the game's Streamline plugin also supports it.
/// native/global mode writes SpoofArchToGame=1; OptiScaler provider mode writes 0.
inline std::string FormatIniContent(int maxFrames, const std::string& kernelImg, int hwBilinear = 0,
                                    const std::string& router = "SM86", int logLevel = 1,
                                    bool spoofArchToGame = true)
{
    if (maxFrames <= 0 || maxFrames > 5)
        maxFrames = 3;

    std::string validKernel = kernelImg;
    if (validKernel != "PTX" && validKernel != "Cubin")
        validKernel = "Auto";

    std::string validRouter = router;
    if (validRouter != "SM75" && validRouter != "SM86")
        validRouter = "SM86";

    int validHwBilinear = (hwBilinear == 1) ? 1 : 0;
    int validLogLevel = (logLevel >= 0 && logLevel <= 3) ? logLevel : 1;

    std::ostringstream ss;
    ss << "; dlssg_for_sm86 0.3.5 / bundled 310.9 runtime. Restart after changes.\n";
    ss << "[General]\n";
    ss << "Enabled=1\n\n";
    ss << "[FrameGeneration]\n";
    ss << "Optimized=1\n";
    ss << "MaxGeneratedFrames=" << maxFrames << "\n\n";
    ss << "[Compatibility]\n";
    ss << "Preset=Auto\n";
    ss << "Router=" << validRouter << "\n";
    ss << "KernelImage=" << validKernel << "\n";
    ss << "HardwareBilinear=" << validHwBilinear << "\n";
    ss << "SpoofArchToGame=" << (spoofArchToGame ? 1 : 0) << "\n\n";
    ss << "[Logging]\n";
    ss << "Level=" << validLogLevel << "\n";
    ss << "Directory=dlssg_sm86\\logs\n\n";
    ss << "[Runtime]\n";
    ss << "Mode=Bundled\n";
    ss << "CacheDirectory=\n";

    return ss.str();
}

inline bool IsTuringArch(uint32_t archId) { return (archId == 0x00000160) || ((archId & 0xFFF0) == 0x0160); }
inline bool IsAmpereArch(uint32_t archId) { return (archId == 0x00000170) || ((archId & 0xFFF0) == 0x0170); }

inline std::string ResolveRouter(uint32_t archId, const std::string& gpuName = "")
{
    if (IsTuringArch(archId))
        return "SM75";
    if (IsAmpereArch(archId))
        return "SM86";

    if (!gpuName.empty())
    {
        if (gpuName.find("RTX 20") != std::string::npos || gpuName.find("GTX 16") != std::string::npos ||
            gpuName.find("Turing") != std::string::npos)
            return "SM75";

        if (gpuName.find("RTX 30") != std::string::npos || gpuName.find("Ampere") != std::string::npos)
            return "SM86";
    }

    return "SM86";
}

std::string ResolveRouter();
std::string GenerateIniContent(LoadMode mode = LoadMode::NativeUnlock);
std::string ResolveAutoKernelImage();

inline std::string ResolveAutoKernelImage(uint32_t archId, const std::string& name, bool onLinux)
{
    return onLinux || IsTuringArch(archId) || name.find("RTX 20") != std::string::npos ||
                   name.find("GTX 16") != std::string::npos || name.find("3080 Ti") != std::string::npos ||
                   name.find("3080Ti") != std::string::npos || name.find("Laptop") != std::string::npos ||
                   name.find("Mobile") != std::string::npos
               ? "PTX"
               : "Auto";
}
} // namespace AmpereMfgLoader
