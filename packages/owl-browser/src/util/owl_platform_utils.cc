#include "owl_platform_utils.h"
#include <limits.h>
#include <unistd.h>

#if defined(OS_MACOS)
#include <mach-o/dyld.h>
#elif defined(OS_LINUX)
#include <linux/limits.h>
#endif

namespace OlibPlatform {

std::string GetExecutablePath() {
  char path[PATH_MAX];

#if defined(OS_MACOS)
  uint32_t size = sizeof(path);
  if (_NSGetExecutablePath(path, &size) == 0) {
    return std::string(path);
  }
#elif defined(OS_LINUX)
  ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
  if (len != -1) {
    path[len] = '\0';
    return std::string(path);
  }
#elif defined(OS_WINDOWS)
  DWORD len = GetModuleFileNameA(NULL, path, sizeof(path));
  if (len > 0) {
    return std::string(path);
  }
#endif

  return "";
}

std::string GetExecutableDir() {
  std::string exe_path = GetExecutablePath();
  if (exe_path.empty()) {
    return "";
  }

  size_t last_slash = exe_path.rfind('/');
#if defined(OS_WINDOWS)
  if (last_slash == std::string::npos) {
    last_slash = exe_path.rfind('\\');
  }
#endif

  if (last_slash != std::string::npos) {
    return exe_path.substr(0, last_slash);
  }

  return "";
}

std::string GetResourcesDir() {
  std::string exe_path = GetExecutablePath();
  if (exe_path.empty()) {
    return "";
  }

#if defined(OS_MACOS)
  // On macOS: /path/to/app.app/Contents/MacOS/executable
  // We want: /path/to/app.app/Contents/Resources
  size_t contents_pos = exe_path.find("/Contents/MacOS");
  if (contents_pos != std::string::npos) {
    return exe_path.substr(0, contents_pos) + "/Contents/Resources";
  }
#elif defined(OS_LINUX)
  // On Linux: Typically /usr/local/bin/executable or similar
  // Resources are usually in /usr/local/share/owl-browser or next to executable
  std::string exe_dir = GetExecutableDir();
  if (!exe_dir.empty()) {
    // Try ../share/owl-browser first (standard Linux layout)
    std::string share_dir = exe_dir + "/../share/owl-browser";
    if (access(share_dir.c_str(), R_OK) == 0) {
      return share_dir;
    }
    // Fallback to same directory as executable
    return exe_dir;
  }
#elif defined(OS_WINDOWS)
  // On Windows: Resources are typically in the same directory as executable
  return GetExecutableDir();
#endif

  return "";
}

}  // namespace OlibPlatform
