#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <functional>

/**
 * Embedded HTTPS server for serving .owl domain content.
 *
 * This server enables ServiceWorker testing by providing a real HTTPS
 * endpoint that DNS can resolve to via host-resolver-rules.
 *
 * Usage:
 *   1. Start server: OwlHttpsServer::Instance().Start(8443, "/path/to/statics")
 *   2. Map domains: --host-resolver-rules="MAP *.owl 127.0.0.1:8443"
 *   3. Trust cert: OnCertificateError allows .owl domains
 *   4. Navigate to https://lie-detector.owl -> ServiceWorker works!
 */
class OwlHttpsServer {
public:
  static OwlHttpsServer& Instance();

  /**
   * Start the HTTPS server.
   * @param port Port to listen on (default 8443)
   * @param statics_path Path to statics directory
   * @return true if server started successfully
   */
  bool Start(int port, const std::string& statics_path);

  /**
   * Stop the server.
   */
  void Stop();

  /**
   * Check if server is running.
   */
  bool IsRunning() const;

  /**
   * Get the server port.
   */
  int GetPort() const;

  /**
   * Get the statics path.
   */
  const std::string& GetStaticsPath() const;

private:
  OwlHttpsServer();
  ~OwlHttpsServer();

  // Non-copyable
  OwlHttpsServer(const OwlHttpsServer&) = delete;
  OwlHttpsServer& operator=(const OwlHttpsServer&) = delete;

  void ServerThread();
  std::string MapUrlToFile(const std::string& url);
  std::string GetMimeType(const std::string& path);
  std::string LoadFile(const std::string& path);

  std::thread server_thread_;
  std::atomic<bool> running_;
  int port_;
  std::string statics_path_;
  int server_socket_;
  void* ssl_ctx_;  // SSL_CTX*
};
