/**
 * OwlWorkerPatcher Implementation
 *
 * Unified worker script patching for anti-fingerprinting.
 * Centralizes all worker detection and patching logic from:
 * - owl_https_server.cc
 * - owl_test_scheme_handler.cc
 * - owl_client.cc
 */

#include "stealth/workers/owl_worker_patcher.h"
#include "stealth/owl_spoof_manager.h"
#include "stealth/owl_stealth.h"
#include "stealth/owl_virtual_machine.h"
#include "logger.h"

#include <algorithm>
#include <regex>

namespace owl {
namespace workers {

// ============================================================================
// WorkerPatcher - Base class implementation
// ============================================================================

bool WorkerPatcher::IsWorkerScript(const std::string& url) {
  // Convert URL to lowercase for case-insensitive matching
  std::string lower_url = url;
  std::transform(lower_url.begin(), lower_url.end(), lower_url.begin(), ::tolower);

  // Check common worker script URL patterns
  return (lower_url.find("creep.js") != std::string::npos ||
          lower_url.find("/sw.js") != std::string::npos ||
          lower_url.find("service-worker") != std::string::npos ||
          lower_url.find("serviceworker") != std::string::npos ||
          lower_url.find("/worker.js") != std::string::npos);
}

std::string WorkerPatcher::StripComments(const std::string& content) {
  std::string cleaned = content;
  size_t pos = 0;

  // Remove single-line comments
  while ((pos = cleaned.find("//", pos)) != std::string::npos) {
    size_t end = cleaned.find('\n', pos);
    if (end == std::string::npos) {
      end = cleaned.length();
    }
    cleaned.erase(pos, end - pos);
  }

  // Remove multi-line comments
  pos = 0;
  while ((pos = cleaned.find("/*", pos)) != std::string::npos) {
    size_t end = cleaned.find("*/", pos);
    if (end == std::string::npos) {
      end = cleaned.length();
    } else {
      end += 2;
    }
    cleaned.erase(pos, end - pos);
  }

  return cleaned;
}

bool WorkerPatcher::IsESModule(const std::string& content) {
  if (content.empty()) {
    return false;
  }

  // Only analyze first ~4KB for performance
  std::string preview = content.substr(0, std::min(content.size(), size_t(4000)));

  // Remove comments for accurate detection
  std::string cleaned = StripComments(preview);

  // Use regex to detect import/export at statement positions
  // Matches: start of string, newline, semicolon, or braces followed by import/export
  std::regex esm_pattern(R"((?:^|[\n\r;{}])\s*(import|export)\s)");
  bool is_esm = std::regex_search(cleaned, esm_pattern);

  LOG_DEBUG("WorkerPatcher", "ES module detection: " + std::string(is_esm ? "YES" : "NO"));
  return is_esm;
}

const VirtualMachine* WorkerPatcher::GetVM(int browser_id) {
  const VirtualMachine* vm = nullptr;

  // Try to get VM from browser context first
  if (browser_id > 0) {
    vm = OwlStealth::GetContextVM(browser_id);
    if (vm) {
      LOG_DEBUG("WorkerPatcher", "Using VM profile '" + vm->id +
                                 "' from browser_id=" + std::to_string(browser_id));
      return vm;
    }
    LOG_WARN("WorkerPatcher", "No VM found for browser_id=" + std::to_string(browser_id) +
                              ", falling back to random");
  }

  // Fallback to random VM
  vm = VirtualMachineDB::Instance().SelectRandomVM();
  if (vm) {
    LOG_WARN("WorkerPatcher", "Using RANDOM VM profile '" + vm->id + "' (no browser context)");
  } else {
    LOG_WARN("WorkerPatcher", "No VM profile available");
  }

  return vm;
}

// ============================================================================
// ServiceWorkerPatcher - ServiceWorker specific implementation
// ============================================================================

std::string ServiceWorkerPatcher::PatchScript(const std::string& content,
                                              const std::string& url,
                                              int browser_id) {
  if (content.empty()) {
    return content;
  }

  LOG_INFO("WorkerPatcher", ">>> PATCHING SERVICE WORKER: " + url);

  const VirtualMachine* vm = GetVM(browser_id);
  if (!vm) {
    LOG_WARN("WorkerPatcher", "No VM profile available for ServiceWorker patching");
    return content;
  }

  LOG_INFO("WorkerPatcher", "Using VM profile '" + vm->id + "' for ServiceWorker");

  // Detect ES module (for logging only - we always use IIFE for SW compatibility)
  bool is_es_module = IsESModule(content);
  if (is_es_module) {
    LOG_INFO("WorkerPatcher", "ES module detected but using IIFE patches (ServiceWorker compatibility)");
  }

  // Generate worker script patch
  // NOTE: ServiceWorkers ONLY get worker_script, NOT early_blob_script!
  // ServiceWorkers cannot create other ServiceWorkers, so blob interception is unnecessary.
  std::string worker_script = spoofs::SpoofManager::Instance().GenerateWorkerScript(
      *vm,
      spoofs::ContextType::SERVICE_WORKER);

  std::string patch_code = worker_script + "\n";

  LOG_INFO("WorkerPatcher", "Generated ServiceWorker patch: " +
                            std::to_string(patch_code.size()) + " bytes");

  // Prepend patch to content
  std::string patched = patch_code + content;

  LOG_INFO("WorkerPatcher", "Successfully patched ServiceWorker. New size: " +
                            std::to_string(patched.size()) + " bytes");

  return patched;
}

std::string ServiceWorkerPatcher::GetPatchCode(int browser_id) {
  const VirtualMachine* vm = GetVM(browser_id);
  if (!vm) {
    LOG_WARN("WorkerPatcher", "No VM profile available for ServiceWorker patch code");
    return "";
  }

  LOG_DEBUG("WorkerPatcher", "Generating ServiceWorker patch code for browser_id=" +
                             std::to_string(browser_id));

  // ServiceWorkers ONLY get worker_script, NOT early_blob_script
  std::string worker_script = spoofs::SpoofManager::Instance().GenerateWorkerScript(
      *vm,
      spoofs::ContextType::SERVICE_WORKER);

  return worker_script + "\n";
}

// ============================================================================
// DedicatedWorkerPatcher - Dedicated Worker specific implementation
// ============================================================================

std::string DedicatedWorkerPatcher::PatchScript(const std::string& content,
                                                const std::string& url,
                                                int browser_id) {
  if (content.empty()) {
    return content;
  }

  LOG_INFO("WorkerPatcher", ">>> PATCHING DEDICATED WORKER: " + url);

  const VirtualMachine* vm = GetVM(browser_id);
  if (!vm) {
    LOG_WARN("WorkerPatcher", "No VM profile available for DedicatedWorker patching");
    return content;
  }

  LOG_INFO("WorkerPatcher", "Using VM profile '" + vm->id + "' for DedicatedWorker");

  // Detect ES module (for logging)
  bool is_es_module = IsESModule(content);
  if (is_es_module) {
    LOG_INFO("WorkerPatcher", "ES module detected, using IIFE patches for compatibility");
  }

  // Generate patches
  // IMPORTANT: worker_script MUST come BEFORE early_blob_script!
  // worker_script defines self._owl_workerScript which early_blob_script captures
  std::string worker_script = spoofs::SpoofManager::Instance().GenerateWorkerScript(
      *vm,
      spoofs::ContextType::DEDICATED_WORKER);
  std::string early_blob_script = spoofs::SpoofManager::Instance().GenerateEarlyBlobInterceptor(*vm);

  std::string patch_code = worker_script + "\n" + early_blob_script + "\n";

  LOG_INFO("WorkerPatcher", "Generated DedicatedWorker patch: " +
                            std::to_string(patch_code.size()) + " bytes");

  // Prepend patch to content
  std::string patched = patch_code + content;

  LOG_INFO("WorkerPatcher", "Successfully patched DedicatedWorker. New size: " +
                            std::to_string(patched.size()) + " bytes");

  return patched;
}

std::string DedicatedWorkerPatcher::GetPatchCode(int browser_id) {
  const VirtualMachine* vm = GetVM(browser_id);
  if (!vm) {
    LOG_WARN("WorkerPatcher", "No VM profile available for DedicatedWorker patch code");
    return "";
  }

  LOG_DEBUG("WorkerPatcher", "Generating DedicatedWorker patch code for browser_id=" +
                             std::to_string(browser_id));

  // DedicatedWorkers get both worker_script AND early_blob_script
  std::string worker_script = spoofs::SpoofManager::Instance().GenerateWorkerScript(
      *vm,
      spoofs::ContextType::DEDICATED_WORKER);
  std::string early_blob_script = spoofs::SpoofManager::Instance().GenerateEarlyBlobInterceptor(*vm);

  return worker_script + "\n" + early_blob_script + "\n";
}

// ============================================================================
// SharedWorkerPatcher - Shared Worker specific implementation
// ============================================================================

std::string SharedWorkerPatcher::PatchScript(const std::string& content,
                                             const std::string& url,
                                             int browser_id) {
  if (content.empty()) {
    return content;
  }

  LOG_INFO("WorkerPatcher", ">>> PATCHING SHARED WORKER: " + url);

  const VirtualMachine* vm = GetVM(browser_id);
  if (!vm) {
    LOG_WARN("WorkerPatcher", "No VM profile available for SharedWorker patching");
    return content;
  }

  LOG_INFO("WorkerPatcher", "Using VM profile '" + vm->id + "' for SharedWorker");

  // Detect ES module (for logging)
  bool is_es_module = IsESModule(content);
  if (is_es_module) {
    LOG_INFO("WorkerPatcher", "ES module detected, using IIFE patches for compatibility");
  }

  // Generate patches
  // IMPORTANT: worker_script MUST come BEFORE early_blob_script!
  std::string worker_script = spoofs::SpoofManager::Instance().GenerateWorkerScript(
      *vm,
      spoofs::ContextType::SHARED_WORKER);
  std::string early_blob_script = spoofs::SpoofManager::Instance().GenerateEarlyBlobInterceptor(*vm);

  std::string patch_code = worker_script + "\n" + early_blob_script + "\n";

  LOG_INFO("WorkerPatcher", "Generated SharedWorker patch: " +
                            std::to_string(patch_code.size()) + " bytes");

  // Prepend patch to content
  std::string patched = patch_code + content;

  LOG_INFO("WorkerPatcher", "Successfully patched SharedWorker. New size: " +
                            std::to_string(patched.size()) + " bytes");

  return patched;
}

std::string SharedWorkerPatcher::GetPatchCode(int browser_id) {
  const VirtualMachine* vm = GetVM(browser_id);
  if (!vm) {
    LOG_WARN("WorkerPatcher", "No VM profile available for SharedWorker patch code");
    return "";
  }

  LOG_DEBUG("WorkerPatcher", "Generating SharedWorker patch code for browser_id=" +
                             std::to_string(browser_id));

  // SharedWorkers get both worker_script AND early_blob_script
  std::string worker_script = spoofs::SpoofManager::Instance().GenerateWorkerScript(
      *vm,
      spoofs::ContextType::SHARED_WORKER);
  std::string early_blob_script = spoofs::SpoofManager::Instance().GenerateEarlyBlobInterceptor(*vm);

  return worker_script + "\n" + early_blob_script + "\n";
}

}  // namespace workers
}  // namespace owl
