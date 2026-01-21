#pragma once

/**
 * OWL GL Hooks
 *
 * Runtime GL function interception for GPU virtualization.
 * Uses fishhook on macOS to rebind GL symbols at runtime.
 */

#include <string>

namespace owl {
namespace gpu {
namespace hooks {

/**
 * Install GL function hooks
 * Must be called early in process initialization, before any GL calls
 * @return true if hooks were installed successfully
 */
bool InstallGLHooks();

/**
 * Remove GL function hooks
 * Note: Due to fishhook limitations, this only disables the hooks
 */
void RemoveGLHooks();

/**
 * Enable or disable GL hooks at runtime
 * Useful for temporarily bypassing virtualization
 */
void EnableGLHooks(bool enable);

/**
 * Check if GL hooks are currently installed
 */
bool AreGLHooksInstalled();

/**
 * Set default spoofed strings for when no context is active
 * These are used as fallback values
 */
void SetDefaultSpoofedStrings(const std::string& vendor,
                               const std::string& renderer,
                               const std::string& version = "OpenGL ES 3.0",
                               const std::string& glsl_version = "OpenGL ES GLSL ES 3.00");

} // namespace hooks
} // namespace gpu
} // namespace owl
