#include "owl_https_server.h"
#include "owl_spoof_manager.h"
#include "owl_virtual_machine.h"
#include "owl_stealth.h"
#include "owl_seed_api.h"
#include "stealth/workers/owl_worker_patcher.h"
#include "logger.h"

#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/evp.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <regex>

namespace {

// Generate a self-signed certificate for .owl domains
bool GenerateSelfSignedCert(SSL_CTX* ctx) {
  LOG_INFO("HttpsServer", "Generating self-signed certificate for .owl domains");

  // Generate RSA key
  EVP_PKEY* pkey = EVP_PKEY_new();
  if (!pkey) {
    LOG_ERROR("HttpsServer", "Failed to create EVP_PKEY");
    return false;
  }

  EVP_PKEY_CTX* pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
  if (!pkey_ctx) {
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Failed to create EVP_PKEY_CTX");
    return false;
  }

  if (EVP_PKEY_keygen_init(pkey_ctx) <= 0 ||
      EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) <= 0 ||
      EVP_PKEY_keygen(pkey_ctx, &pkey) <= 0) {
    EVP_PKEY_CTX_free(pkey_ctx);
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Failed to generate RSA key");
    return false;
  }
  EVP_PKEY_CTX_free(pkey_ctx);

  // Create X509 certificate
  X509* x509 = X509_new();
  if (!x509) {
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Failed to create X509");
    return false;
  }

  // Set certificate version (v3)
  X509_set_version(x509, 2);

  // Set serial number
  ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);

  // Set validity period (1 year)
  X509_gmtime_adj(X509_get_notBefore(x509), 0);
  X509_gmtime_adj(X509_get_notAfter(x509), 365 * 24 * 60 * 60);

  // Set public key
  X509_set_pubkey(x509, pkey);

  // Set subject name
  X509_NAME* name = X509_get_subject_name(x509);
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*)"US", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, (unsigned char*)"Owl Browser", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*)"*.owl", -1, -1, 0);

  // Self-signed: issuer = subject
  X509_set_issuer_name(x509, name);

  // Add Subject Alternative Names for .owl domains
  X509_EXTENSION* ext = nullptr;
  X509V3_CTX ctx_ext;
  X509V3_set_ctx_nodb(&ctx_ext);
  X509V3_set_ctx(&ctx_ext, x509, x509, nullptr, nullptr, 0);

  ext = X509V3_EXT_conf_nid(nullptr, &ctx_ext, NID_subject_alt_name,
    "DNS:*.owl, DNS:lie-detector.owl, DNS:user-form.owl, DNS:test.owl");
  if (ext) {
    X509_add_ext(x509, ext, -1);
    X509_EXTENSION_free(ext);
  }

  // Sign the certificate
  if (!X509_sign(x509, pkey, EVP_sha256())) {
    X509_free(x509);
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Failed to sign certificate");
    return false;
  }

  // Use the certificate and key
  if (SSL_CTX_use_certificate(ctx, x509) != 1) {
    X509_free(x509);
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Failed to use certificate");
    return false;
  }

  if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
    X509_free(x509);
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Failed to use private key");
    return false;
  }

  // Verify that the private key matches the certificate
  if (SSL_CTX_check_private_key(ctx) != 1) {
    X509_free(x509);
    EVP_PKEY_free(pkey);
    LOG_ERROR("HttpsServer", "Private key does not match certificate");
    return false;
  }

  X509_free(x509);
  EVP_PKEY_free(pkey);

  // Set SSL options for compatibility
  SSL_CTX_set_options(ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3);
  SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

  LOG_INFO("HttpsServer", "Self-signed certificate generated successfully");
  return true;
}

// Set socket non-blocking
bool SetNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) return false;
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

} // namespace

OwlHttpsServer& OwlHttpsServer::Instance() {
  static OwlHttpsServer instance;
  return instance;
}

OwlHttpsServer::OwlHttpsServer()
    : running_(false), port_(0), server_socket_(-1), ssl_ctx_(nullptr) {
}

OwlHttpsServer::~OwlHttpsServer() {
  Stop();
}

bool OwlHttpsServer::Start(int port, const std::string& statics_path) {
  if (running_) {
    LOG_WARN("HttpsServer", "Server already running");
    return true;
  }

  port_ = port;
  statics_path_ = statics_path;

  LOG_INFO("HttpsServer", "Starting HTTPS server on port " + std::to_string(port));
  LOG_INFO("HttpsServer", "Statics path: " + statics_path);

  // Initialize OpenSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();

  // Create SSL context
  const SSL_METHOD* method = TLS_server_method();
  ssl_ctx_ = SSL_CTX_new(method);
  if (!ssl_ctx_) {
    LOG_ERROR("HttpsServer", "Failed to create SSL context");
    return false;
  }

  // Generate self-signed certificate
  if (!GenerateSelfSignedCert(static_cast<SSL_CTX*>(ssl_ctx_))) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
    return false;
  }

  // Create server socket
  server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_socket_ < 0) {
    LOG_ERROR("HttpsServer", "Failed to create socket");
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
    return false;
  }

  // Set socket options
  int opt = 1;
  setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  setsockopt(server_socket_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

  // Bind to port
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only
  addr.sin_port = htons(port);

  if (bind(server_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    LOG_ERROR("HttpsServer", "Failed to bind to port " + std::to_string(port) + ": " + strerror(errno));
    close(server_socket_);
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
    server_socket_ = -1;
    return false;
  }

  // Listen
  if (listen(server_socket_, 10) < 0) {
    LOG_ERROR("HttpsServer", "Failed to listen");
    close(server_socket_);
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
    server_socket_ = -1;
    return false;
  }

  // Set non-blocking
  SetNonBlocking(server_socket_);

  // Start server thread
  running_ = true;
  server_thread_ = std::thread(&OwlHttpsServer::ServerThread, this);

  LOG_INFO("HttpsServer", "HTTPS server started on https://127.0.0.1:" + std::to_string(port));
  return true;
}

void OwlHttpsServer::Stop() {
  if (!running_) return;

  LOG_INFO("HttpsServer", "Stopping HTTPS server");
  running_ = false;

  if (server_socket_ >= 0) {
    close(server_socket_);
    server_socket_ = -1;
  }

  if (server_thread_.joinable()) {
    server_thread_.join();
  }

  if (ssl_ctx_) {
    SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
    ssl_ctx_ = nullptr;
  }

  LOG_INFO("HttpsServer", "HTTPS server stopped");
}

bool OwlHttpsServer::IsRunning() const {
  return running_;
}

int OwlHttpsServer::GetPort() const {
  return port_;
}

const std::string& OwlHttpsServer::GetStaticsPath() const {
  return statics_path_;
}

std::string OwlHttpsServer::MapUrlToFile(const std::string& url) {
  // Parse URL: /path -> statics/<domain>/path
  // The Host header tells us which .owl domain was requested

  // For now, extract path from URL
  std::string path = url;

  // Remove query string
  size_t query_pos = path.find('?');
  if (query_pos != std::string::npos) {
    path = path.substr(0, query_pos);
  }

  // Remove leading /
  if (!path.empty() && path[0] == '/') {
    path = path.substr(1);
  }

  // Default to index.html
  if (path.empty()) {
    path = "index.html";
  }

  return path;
}

std::string OwlHttpsServer::GetMimeType(const std::string& path) {
  size_t dot_pos = path.rfind('.');
  if (dot_pos == std::string::npos) {
    return "application/octet-stream";
  }

  std::string ext = path.substr(dot_pos);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

  if (ext == ".html" || ext == ".htm") return "text/html";
  if (ext == ".css") return "text/css";
  if (ext == ".js") return "application/javascript";
  if (ext == ".json") return "application/json";
  if (ext == ".png") return "image/png";
  if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
  if (ext == ".gif") return "image/gif";
  if (ext == ".svg") return "image/svg+xml";
  if (ext == ".webp") return "image/webp";
  if (ext == ".ico") return "image/x-icon";
  if (ext == ".woff") return "font/woff";
  if (ext == ".woff2") return "font/woff2";

  return "application/octet-stream";
}

std::string OwlHttpsServer::LoadFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return "";
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void OwlHttpsServer::ServerThread() {
  LOG_INFO("HttpsServer", "Server thread started");

  while (running_) {
    // Poll for connections
    struct pollfd pfd;
    pfd.fd = server_socket_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 100);  // 100ms timeout
    if (ret <= 0) continue;

    // Accept connection
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
    if (client_socket < 0) continue;

    LOG_INFO("HttpsServer", "New connection accepted");

    // Make client socket blocking for SSL handshake
    // (server socket is non-blocking, but we need blocking for SSL_accept)
    int flags = fcntl(client_socket, F_GETFL, 0);
    fcntl(client_socket, F_SETFL, flags & ~O_NONBLOCK);

    // Create SSL connection
    SSL* ssl = SSL_new(static_cast<SSL_CTX*>(ssl_ctx_));
    if (!ssl) {
      close(client_socket);
      continue;
    }

    SSL_set_fd(ssl, client_socket);

    // SSL handshake
    int ssl_ret = SSL_accept(ssl);
    if (ssl_ret <= 0) {
      int ssl_err = SSL_get_error(ssl, ssl_ret);
      unsigned long err_code = ERR_get_error();
      char err_buf[256];
      ERR_error_string_n(err_code, err_buf, sizeof(err_buf));
      LOG_ERROR("HttpsServer", "SSL handshake failed - SSL_error: " + std::to_string(ssl_err) +
                " ERR: " + std::string(err_buf));
      SSL_free(ssl);
      close(client_socket);
      continue;
    }

    // Read request
    char buffer[8192];
    int bytes = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
      SSL_free(ssl);
      close(client_socket);
      continue;
    }
    buffer[bytes] = '\0';

    // Parse request
    std::string request(buffer);
    LOG_INFO("HttpsServer", "Request: " + request.substr(0, request.find('\r')));

    // Extract method and path
    std::string method, path, host;
    std::istringstream req_stream(request);
    req_stream >> method >> path;

    // Extract Host header to determine .owl domain
    size_t host_pos = request.find("Host: ");
    if (host_pos != std::string::npos) {
      size_t host_end = request.find("\r\n", host_pos);
      host = request.substr(host_pos + 6, host_end - host_pos - 6);
      // Remove port if present
      size_t port_pos = host.find(':');
      if (port_pos != std::string::npos) {
        host = host.substr(0, port_pos);
      }
    }

    // Get browser_id for VM profile lookup
    // Option 1: From X-Owl-Browser-Id header (if available)
    // Option 2: From owl_seed_get_current_browser_id() (active context)
    int browser_id = 0;
    size_t bid_pos = request.find("X-Owl-Browser-Id: ");
    if (bid_pos != std::string::npos) {
      size_t bid_end = request.find("\r\n", bid_pos);
      std::string bid_str = request.substr(bid_pos + 18, bid_end - bid_pos - 18);
      browser_id = std::atoi(bid_str.c_str());
      if (browser_id > 0) {
        LOG_INFO("HttpsServer", "Got browser_id=" + std::to_string(browser_id) + " from X-Owl-Browser-Id header");
      }
    }
    // Fallback: use current active browser context
    if (browser_id <= 0) {
      browser_id = owl_seed_get_current_browser_id();
      if (browser_id > 0) {
        LOG_INFO("HttpsServer", "Got browser_id=" + std::to_string(browser_id) + " from owl_seed_get_current_browser_id()");
      }
    }

    LOG_INFO("HttpsServer", "Host: " + host + ", Path: " + path + ", browser_id: " + std::to_string(browser_id));

    // Map host to directory (e.g., lie-detector.owl -> lie_detector)
    std::string domain_dir = host;
    // Remove .owl suffix
    if (domain_dir.size() > 4 && domain_dir.substr(domain_dir.size() - 4) == ".owl") {
      domain_dir = domain_dir.substr(0, domain_dir.size() - 4);
    }
    // Convert hyphens to underscores
    std::replace(domain_dir.begin(), domain_dir.end(), '-', '_');

    // Build file path
    std::string file_path = MapUrlToFile(path);
    std::string full_path = statics_path_ + "/" + domain_dir + "/" + file_path;

    LOG_INFO("HttpsServer", "Serving file: " + full_path);

    // Load file
    std::string content = LoadFile(full_path);
    std::string mime_type = GetMimeType(full_path);

    // ========================================================================
    // WORKER SCRIPT PATCHING
    // For ServiceWorker/Worker scripts, prepend spoofing code to intercept
    // navigator properties before the script runs.
    //
    // Uses the unified WorkerPatcher classes from stealth/workers/
    // ========================================================================
    if (mime_type == "application/javascript" &&
        owl::workers::WorkerPatcher::IsWorkerScript(path) &&
        !content.empty()) {

      LOG_INFO("HttpsServer", ">>> PATCHING WORKER SCRIPT via WorkerPatcher: " + path);

      // Use ServiceWorkerPatcher for HTTPS server requests
      // ServiceWorkers can't spawn other ServiceWorkers, so no blob interception needed
      owl::workers::ServiceWorkerPatcher patcher;
      content = patcher.PatchScript(content, path, browser_id);

      LOG_INFO("HttpsServer", "Successfully patched via WorkerPatcher. Size: " +
               std::to_string(content.size()) + " bytes");
    }

    // Build response
    std::string response;
    if (!content.empty()) {
      response = "HTTP/1.1 200 OK\r\n";
      response += "Content-Type: " + mime_type + "\r\n";
      response += "Content-Length: " + std::to_string(content.size()) + "\r\n";
      response += "Access-Control-Allow-Origin: *\r\n";
      response += "Cache-Control: no-cache\r\n";
      response += "Connection: close\r\n";
      response += "\r\n";
      response += content;
    } else {
      LOG_WARN("HttpsServer", "File not found: " + full_path);
      std::string body = "<!DOCTYPE html><html><body><h1>404 Not Found</h1></body></html>";
      response = "HTTP/1.1 404 Not Found\r\n";
      response += "Content-Type: text/html\r\n";
      response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
      response += "Connection: close\r\n";
      response += "\r\n";
      response += body;
    }

    // Send response
    SSL_write(ssl, response.c_str(), response.size());

    // Cleanup
    SSL_shutdown(ssl);
    SSL_free(ssl);
    close(client_socket);
  }

  LOG_INFO("HttpsServer", "Server thread exiting");
}
