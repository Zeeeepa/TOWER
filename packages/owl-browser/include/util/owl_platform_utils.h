#pragma once

#include <string>

namespace OlibPlatform {

// Cross-platform function to get the executable directory
std::string GetExecutableDir();

// Cross-platform function to get the Resources directory
// (for macOS app bundles or Linux equivalent)
std::string GetResourcesDir();

// Cross-platform function to get the executable path
std::string GetExecutablePath();

}  // namespace OlibPlatform
