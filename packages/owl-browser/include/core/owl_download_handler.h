#ifndef OWL_DOWNLOAD_HANDLER_H_
#define OWL_DOWNLOAD_HANDLER_H_

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <atomic>

// Download state
enum class DownloadState {
  PENDING,
  IN_PROGRESS,
  PAUSED,
  COMPLETED,
  CANCELLED,
  FAILED
};

// Download item information
struct DownloadInfo {
  std::string download_id;
  std::string url;
  std::string suggested_filename;
  std::string full_path;           // Final saved path
  std::string mime_type;
  int64_t total_bytes;
  int64_t received_bytes;
  int percent_complete;
  int64_t current_speed;           // bytes/sec
  DownloadState state;
  std::string error_message;
  int64_t start_time;
  int64_t end_time;
  std::string context_id;
};

// Download manager singleton class
class OwlDownloadManager {
 public:
  static OwlDownloadManager* GetInstance();

  // Configuration
  void SetDownloadPath(const std::string& context_id, const std::string& path);
  std::string GetDownloadPath(const std::string& context_id) const;
  void SetAutoDownload(const std::string& context_id, bool auto_download);
  bool IsAutoDownload(const std::string& context_id) const;

  // Download tracking
  void OnDownloadStarted(const std::string& context_id, const DownloadInfo& info);
  void OnDownloadUpdated(const std::string& download_id, const DownloadInfo& info);
  void OnDownloadCompleted(const std::string& download_id, const std::string& full_path);
  void OnDownloadFailed(const std::string& download_id, const std::string& error);
  void OnDownloadCancelled(const std::string& download_id);

  // Query downloads
  DownloadInfo GetDownload(const std::string& download_id) const;
  std::vector<DownloadInfo> GetDownloads(const std::string& context_id) const;
  std::vector<DownloadInfo> GetActiveDownloads(const std::string& context_id) const;
  std::vector<DownloadInfo> GetCompletedDownloads(const std::string& context_id) const;

  // Wait for download to complete
  bool WaitForDownload(const std::string& download_id, int timeout_ms);

  // Get downloads as JSON
  std::string GetDownloadsJSON(const std::string& context_id) const;

  // Clear download history
  void ClearDownloads(const std::string& context_id);

  // Generate unique download ID
  std::string GenerateDownloadId();

 private:
  OwlDownloadManager();
  ~OwlDownloadManager();

  mutable std::mutex mutex_;

  // Context ID -> download path
  std::map<std::string, std::string> download_paths_;
  std::map<std::string, bool> auto_download_;

  // Download ID -> download info
  std::map<std::string, DownloadInfo> downloads_;

  // Context ID -> list of download IDs
  std::map<std::string, std::vector<std::string>> context_downloads_;

  std::atomic<uint64_t> download_counter_{0};
};

#endif  // OWL_DOWNLOAD_HANDLER_H_
