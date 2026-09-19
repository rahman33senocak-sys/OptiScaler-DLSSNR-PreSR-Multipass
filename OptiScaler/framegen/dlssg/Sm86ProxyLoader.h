#pragma once

#include <string>

namespace Sm86ProxyLoader
{
struct Status
{
    bool Enabled = false;
    bool DllFound = false;
    bool IniWritten = false;
    bool DllLoaded = false;
    std::string ErrorMessage;
};

Status LastStatus();
void TrySetup();
} // namespace Sm86ProxyLoader
