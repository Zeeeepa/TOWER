#pragma once

/**
 * OwlWorkerPatcher: Unified Worker Script Patching System
 *
 * This module provides a centralized system for patching worker scripts
 * (ServiceWorker, DedicatedWorker, SharedWorker) with anti-fingerprinting
 * spoofing code.
 *
 * DESIGN PRINCIPLES:
 * 1. Single Source of Truth: All worker detection and patching logic is here
 * 2. Worker-Type Specific: Different worker types get different patches
 * 3. VM Profile Aware: Uses consistent VM profiles across all contexts
 * 4. ES Module Safe: Handles both classic and ES module scripts
 *
 * USAGE:
 *   ServiceWorkerPatcher patcher;
 *   std::string patched = patcher.PatchScript(content, url, browser_id);
 *
 * WORKER TYPE DIFFERENCES:
 * - ServiceWorker: Only worker_script (cannot spawn other ServiceWorkers)
 * - DedicatedWorker: worker_script + early_blob_script (can spawn nested workers)
 * - SharedWorker: worker_script + early_blob_script (can spawn nested workers)
 */

#include <string>
#include <cstdint>

namespace owl {

// Forward declarations
struct VirtualMachine;

namespace workers {

/**
 * WorkerPatcher: Base class for worker script patching
 *
 * Provides common functionality for detecting and patching worker scripts.
 * Subclasses specialize for different worker types.
 */
class WorkerPatcher {
 public:
  WorkerPatcher() = default;
  virtual ~WorkerPatcher() = default;

  /**
   * Check if a URL matches worker script patterns.
   * Patterns: creep.js, /sw.js, service-worker, serviceworker, /worker.js
   *
   * @param url The URL to check
   * @return true if URL matches worker script patterns
   */
  static bool IsWorkerScript(const std::string& url);

  /**
   * Detect if script content is an ES module.
   * Looks for import/export statements outside of strings/comments.
   *
   * @param content The script content (only first ~4KB is analyzed)
   * @return true if script appears to be an ES module
   */
  static bool IsESModule(const std::string& content);

  /**
   * Patch a worker script with spoofing code.
   *
   * @param content The original script content
   * @param url The script URL (for logging)
   * @param browser_id The browser context ID for VM profile lookup
   * @return Patched script content, or original if patching fails
   */
  virtual std::string PatchScript(const std::string& content,
                                  const std::string& url,
                                  int browser_id) = 0;

  /**
   * Get just the patch code prefix (without content).
   * Used by streaming filters that need to prepend patch code.
   *
   * @param browser_id The browser context ID for VM profile lookup
   * @return The patch code to prepend, or empty if no VM available
   */
  virtual std::string GetPatchCode(int browser_id) = 0;

 protected:
  /**
   * Get VM profile for a browser context.
   * Falls back to random VM if context VM not found.
   *
   * @param browser_id The browser context ID
   * @return VM profile pointer, or nullptr if none available
   */
  static const VirtualMachine* GetVM(int browser_id);

  /**
   * Remove comments from script content for ES module detection.
   * Handles both single-line (//) and multi-line comments.
   *
   * @param content The script content
   * @return Content with comments removed
   */
  static std::string StripComments(const std::string& content);
};

/**
 * ServiceWorkerPatcher: Patches ServiceWorker scripts
 *
 * ServiceWorkers can NOT spawn other ServiceWorkers, so they only
 * need the worker_script patch (navigator spoofing), not the
 * early_blob_script (blob/Worker interception).
 */
class ServiceWorkerPatcher : public WorkerPatcher {
 public:
  ServiceWorkerPatcher() = default;

  std::string PatchScript(const std::string& content,
                          const std::string& url,
                          int browser_id) override;

  std::string GetPatchCode(int browser_id) override;
};

/**
 * DedicatedWorkerPatcher: Patches Dedicated Worker scripts
 *
 * Dedicated Workers CAN spawn nested workers via new Worker() or
 * blob URLs, so they need both:
 * - worker_script: navigator/canvas/audio spoofing
 * - early_blob_script: Blob/Worker constructor interception
 */
class DedicatedWorkerPatcher : public WorkerPatcher {
 public:
  DedicatedWorkerPatcher() = default;

  std::string PatchScript(const std::string& content,
                          const std::string& url,
                          int browser_id) override;

  std::string GetPatchCode(int browser_id) override;
};

/**
 * SharedWorkerPatcher: Patches Shared Worker scripts
 *
 * Shared Workers CAN spawn nested workers via new Worker() or
 * blob URLs, so they need both:
 * - worker_script: navigator/canvas/audio spoofing
 * - early_blob_script: Blob/Worker constructor interception
 */
class SharedWorkerPatcher : public WorkerPatcher {
 public:
  SharedWorkerPatcher() = default;

  std::string PatchScript(const std::string& content,
                          const std::string& url,
                          int browser_id) override;

  std::string GetPatchCode(int browser_id) override;
};

}  // namespace workers
}  // namespace owl
