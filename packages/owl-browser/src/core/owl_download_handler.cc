#include "owl_download_handler.h"
#include "logger.h"
#include <sstream>
#include <chrono>
#include <thread>

OwlDownloadManager* OwlDownloadManager::GetInstance() {
  static OwlDownloadManager instance;
  return &instance;
}

OwlDownloadManager::OwlDownloadManager() {
  LOG_DEBUG("DownloadManager", "Download manager initialized");
}

OwlDownloadManager::~OwlDownloadManager() {}

void OwlDownloadManager::SetDownloadPath(const std::string& context_id,
                                          const std::string& path) {
  std::lock_guard<std::mutex> lock(mutex_);
  download_paths_[context_id] = path;
  LOG_DEBUG("DownloadManager",
           "Set download path for " + context_id + ": " + path);
}

std::string OwlDownloadManager::GetDownloadPath(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = download_paths_.find(context_id);
  if (it != download_paths_.end()) {
    return it->second;
  }
  // Default to /tmp
  return "/tmp";
}

void OwlDownloadManager::SetAutoDownload(const std::string& context_id,
                                          bool auto_download) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto_download_[context_id] = auto_download;
}

bool OwlDownloadManager::IsAutoDownload(const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = auto_download_.find(context_id);
  return it != auto_download_.end() && it->second;
}

std::string OwlDownloadManager::GenerateDownloadId() {
  return "dl_" + std::to_string(++download_counter_);
}

void OwlDownloadManager::OnDownloadStarted(const std::string& context_id,
                                            const DownloadInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);

  downloads_[info.download_id] = info;
  context_downloads_[context_id].push_back(info.download_id);

  LOG_DEBUG("DownloadManager", "Download started: " + info.download_id +
                                  " url=" + info.url +
                                  " filename=" + info.suggested_filename);
}

void OwlDownloadManager::OnDownloadUpdated(const std::string& download_id,
                                            const DownloadInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = downloads_.find(download_id);
  if (it != downloads_.end()) {
    it->second.received_bytes = info.received_bytes;
    it->second.percent_complete = info.percent_complete;
    it->second.current_speed = info.current_speed;
    it->second.state = info.state;

    if (info.percent_complete % 20 == 0) {
      LOG_DEBUG("DownloadManager", "Download progress: " + download_id + " " +
                                       std::to_string(info.percent_complete) +
                                       "%");
    }
  }
}

void OwlDownloadManager::OnDownloadCompleted(const std::string& download_id,
                                              const std::string& full_path) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = downloads_.find(download_id);
  if (it != downloads_.end()) {
    it->second.state = DownloadState::COMPLETED;
    it->second.full_path = full_path;
    it->second.percent_complete = 100;
    it->second.end_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    LOG_DEBUG("DownloadManager",
             "Download completed: " + download_id + " -> " + full_path);
  }
}

void OwlDownloadManager::OnDownloadFailed(const std::string& download_id,
                                           const std::string& error) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = downloads_.find(download_id);
  if (it != downloads_.end()) {
    it->second.state = DownloadState::FAILED;
    it->second.error_message = error;
    it->second.end_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    LOG_ERROR("DownloadManager",
              "Download failed: " + download_id + " - " + error);
  }
}

void OwlDownloadManager::OnDownloadCancelled(const std::string& download_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = downloads_.find(download_id);
  if (it != downloads_.end()) {
    it->second.state = DownloadState::CANCELLED;
    it->second.end_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();

    LOG_DEBUG("DownloadManager", "Download cancelled: " + download_id);
  }
}

DownloadInfo OwlDownloadManager::GetDownload(
    const std::string& download_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = downloads_.find(download_id);
  if (it != downloads_.end()) {
    return it->second;
  }
  return DownloadInfo();
}

std::vector<DownloadInfo> OwlDownloadManager::GetDownloads(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<DownloadInfo> result;
  auto ctx_it = context_downloads_.find(context_id);
  if (ctx_it != context_downloads_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = downloads_.find(dl_id);
      if (dl_it != downloads_.end()) {
        result.push_back(dl_it->second);
      }
    }
  }
  return result;
}

std::vector<DownloadInfo> OwlDownloadManager::GetActiveDownloads(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<DownloadInfo> result;
  auto ctx_it = context_downloads_.find(context_id);
  if (ctx_it != context_downloads_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = downloads_.find(dl_id);
      if (dl_it != downloads_.end() &&
          (dl_it->second.state == DownloadState::PENDING ||
           dl_it->second.state == DownloadState::IN_PROGRESS ||
           dl_it->second.state == DownloadState::PAUSED)) {
        result.push_back(dl_it->second);
      }
    }
  }
  return result;
}

std::vector<DownloadInfo> OwlDownloadManager::GetCompletedDownloads(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::vector<DownloadInfo> result;
  auto ctx_it = context_downloads_.find(context_id);
  if (ctx_it != context_downloads_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = downloads_.find(dl_id);
      if (dl_it != downloads_.end() &&
          dl_it->second.state == DownloadState::COMPLETED) {
        result.push_back(dl_it->second);
      }
    }
  }
  return result;
}

bool OwlDownloadManager::WaitForDownload(const std::string& download_id,
                                          int timeout_ms) {
  auto start = std::chrono::steady_clock::now();
  auto timeout = std::chrono::milliseconds(timeout_ms);

  while (true) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = downloads_.find(download_id);
      if (it == downloads_.end()) {
        return false;  // Download not found
      }

      if (it->second.state == DownloadState::COMPLETED) {
        return true;
      }
      if (it->second.state == DownloadState::FAILED ||
          it->second.state == DownloadState::CANCELLED) {
        return false;
      }
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed > timeout) {
      return false;  // Timeout
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

std::string OwlDownloadManager::GetDownloadsJSON(
    const std::string& context_id) const {
  std::lock_guard<std::mutex> lock(mutex_);

  std::stringstream ss;
  ss << "[";

  auto ctx_it = context_downloads_.find(context_id);
  if (ctx_it != context_downloads_.end()) {
    bool first = true;
    for (const auto& dl_id : ctx_it->second) {
      auto dl_it = downloads_.find(dl_id);
      if (dl_it != downloads_.end()) {
        if (!first) ss << ",";
        first = false;

        const auto& dl = dl_it->second;
        ss << "{\"id\":\"" << dl.download_id << "\""
           << ",\"url\":\"" << dl.url << "\""
           << ",\"filename\":\"" << dl.suggested_filename << "\""
           << ",\"path\":\"" << dl.full_path << "\""
           << ",\"totalBytes\":" << dl.total_bytes
           << ",\"receivedBytes\":" << dl.received_bytes
           << ",\"percent\":" << dl.percent_complete
           << ",\"state\":\"" << static_cast<int>(dl.state) << "\""
           << ",\"error\":\"" << dl.error_message << "\"}";
      }
    }
  }

  ss << "]";
  return ss.str();
}

void OwlDownloadManager::ClearDownloads(const std::string& context_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto ctx_it = context_downloads_.find(context_id);
  if (ctx_it != context_downloads_.end()) {
    for (const auto& dl_id : ctx_it->second) {
      downloads_.erase(dl_id);
    }
    context_downloads_.erase(ctx_it);
  }

  LOG_DEBUG("DownloadManager", "Cleared downloads for context: " + context_id);
}
