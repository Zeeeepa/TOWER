// Linux implementation for dev network
// This is platform-independent since it only generates HTML
#include "owl_dev_network.h"

#if defined(OS_LINUX)

#include "logger.h"
#include "../resources/icons/icons.h"
#include <sstream>
#include <iomanip>

OwlDevNetwork::OwlDevNetwork() {
  LOG_DEBUG("DevNetwork", "Network tab initialized");
}

OwlDevNetwork::~OwlDevNetwork() {
}

void OwlDevNetwork::AddRequest(const NetworkRequest& request) {
  std::lock_guard<std::mutex> lock(requests_mutex_);
  requests_.push_back(request);
}

void OwlDevNetwork::ClearRequests() {
  std::lock_guard<std::mutex> lock(requests_mutex_);
  requests_.clear();
}

std::string OwlDevNetwork::FormatSize(size_t bytes) {
  if (bytes < 1024) {
    return std::to_string(bytes) + " B";
  } else if (bytes < 1024 * 1024) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
    return oss.str();
  } else {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
    return oss.str();
  }
}

std::string OwlDevNetwork::FormatDuration(std::chrono::milliseconds ms) {
  if (ms.count() < 1000) {
    return std::to_string(ms.count()) + " ms";
  } else {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << (ms.count() / 1000.0) << " s";
    return oss.str();
  }
}

std::string OwlDevNetwork::GetRequestsJSON() {
  std::lock_guard<std::mutex> lock(requests_mutex_);

  std::ostringstream json;
  json << "[";

  for (size_t i = 0; i < requests_.size(); i++) {
    const auto& req = requests_[i];

    if (i > 0) json << ",";
    json << "{";
    json << "\"url\":\"" << req.url << "\",";
    json << "\"method\":\"" << req.method << "\",";
    json << "\"type\":\"" << req.type << "\",";
    json << "\"status\":" << req.status_code << ",";
    json << "\"statusText\":\"" << req.status_text << "\",";
    json << "\"size\":" << req.size << ",";
    json << "\"duration\":" << req.duration.count();
    json << "}";
  }

  json << "]";
  return json.str();
}

std::string OwlDevNetwork::GenerateHTML() {
  std::ostringstream html;

  html << R"HTML(
<div class="network-container">
  <div class="network-toolbar">
    <button class="btn btn-secondary" onclick="clearRequests()">
      )HTML" << OlibIcons::TRASH << R"HTML(
      Clear
    </button>
    <div style="flex: 1;"></div>
    <input type="text" class="filter-input" id="network-filter" placeholder="Filter URLs..." oninput="filterRequests()">
  </div>

  <div class="network-table-container">
    <table class="network-table" id="network-table">
      <thead>
        <tr>
          <th>Name</th>
          <th>Method</th>
          <th>Type</th>
          <th>Status</th>
          <th>Size</th>
          <th>Time</th>
        </tr>
      </thead>
      <tbody id="network-tbody">
        <tr>
          <td colspan="6" class="empty-state">
            Network requests will appear here when you navigate
          </td>
        </tr>
      </tbody>
    </table>
  </div>

  <div class="network-details" id="network-details">
    <div class="inspector-section">
      <div class="inspector-title">Request Details</div>
      <div id="request-details" class="inspector-content">
        <div class="empty-state">Select a request to view details</div>
      </div>
    </div>
  </div>
</div>

<style>
  .network-container {
    display: flex;
    flex-direction: column;
    height: 100%;
    overflow: hidden;
  }

  .network-toolbar {
    background: #252526;
    border-bottom: 1px solid #3c3c3c;
    padding: 8px 12px;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .network-table-container {
    flex: 2;
    overflow-y: auto;
    background: #1e1e1e;
    border-bottom: 1px solid #3c3c3c;
  }

  .network-table {
    width: 100%;
    border-collapse: collapse;
    font-family: 'Monaco', 'Menlo', 'Consolas', monospace;
    font-size: 11px;
    table-layout: fixed;
  }

  .network-table thead {
    background: #252526;
    position: sticky;
    top: 0;
    z-index: 1;
  }

  .network-table th {
    text-align: left;
    padding: 8px 12px;
    color: #cccccc;
    font-weight: 600;
    border-bottom: 1px solid #3c3c3c;
  }

  .network-table th:nth-child(1) { width: 40%; }  /* Name - largest for URLs */
  .network-table th:nth-child(2) { width: 10%; }  /* Method */
  .network-table th:nth-child(3) { width: 15%; }  /* Type */
  .network-table th:nth-child(4) { width: 10%; }  /* Status */
  .network-table th:nth-child(5) { width: 12%; }  /* Size */
  .network-table th:nth-child(6) { width: 13%; }  /* Time */

  .network-table td {
    padding: 6px 12px;
    color: #cccccc;
    border-bottom: 1px solid #2d2d30;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .network-table tbody tr {
    cursor: pointer;
  }

  .network-table tbody tr:hover {
    background: #2a2a2a;
  }

  .network-table tbody tr.selected {
    background: #094771;
  }

  .network-details {
    flex: 1;
    overflow-y: auto;
    background: #252526;
  }

  .status-success {
    color: #4ec9b0;
  }

  .status-redirect {
    color: #ce9178;
  }

  .status-error {
    color: #f48771;
  }

  .method-get {
    color: #4ec9b0;
  }

  .method-post {
    color: #dcdcaa;
  }

  .method-put {
    color: #569cd6;
  }

  .method-delete {
    color: #f48771;
  }
</style>

<script>
  let networkRequests = [];

  // Function called from C++ to add network request
  function addNetworkRequest(requestData) {
    try {
      const req = JSON.parse(requestData);
      networkRequests.push(req);
      renderRequests();
    } catch (e) {
      console.error('Failed to parse network request data:', e);
    }
  }

  // Function called from C++ to clear all requests
  function clearAllRequests() {
    networkRequests = [];
    renderRequests();
  }

  function renderRequests() {
    const tbody = document.getElementById('network-tbody');

    if (networkRequests.length === 0) {
      tbody.innerHTML = `
        <tr>
          <td colspan="6" class="empty-state">
            No network requests captured yet.<br>
            Navigate to a page to see requests.
          </td>
        </tr>
      `;
      return;
    }

    const html = networkRequests.map((req, index) => {
      const filename = req.url.split('/').pop() || req.url;
      const statusClass = req.status >= 200 && req.status < 300 ? 'status-success' :
                          req.status >= 300 && req.status < 400 ? 'status-redirect' :
                          'status-error';
      const methodClass = `method-${req.method.toLowerCase()}`;

      return `
        <tr onclick="selectRequest(${index})" data-index="${index}">
          <td title="${req.url}">${filename}</td>
          <td class="${methodClass}">${req.method}</td>
          <td>${req.type}</td>
          <td class="${statusClass}">${req.status}</td>
          <td>${formatSize(req.size)}</td>
          <td>${req.duration} ms</td>
        </tr>
      `;
    }).join('');

    tbody.innerHTML = html;
  }

  function selectRequest(index) {
    // Remove previous selection
    document.querySelectorAll('.network-table tbody tr').forEach(el => el.classList.remove('selected'));

    // Add selection to clicked row
    const row = document.querySelector(`.network-table tbody tr[data-index="${index}"]`);
    if (row) {
      row.classList.add('selected');
    }

    // Show request details
    const req = networkRequests[index];

    // Format headers as a table
    const formatHeaders = (headers) => {
      if (!headers || Object.keys(headers).length === 0) {
        return '<div style="color:#888;">No headers</div>';
      }
      return `
        <table style="width:100%; border-collapse: collapse; font-size: 10px; margin-top: 4px;">
          ${Object.entries(headers).map(([key, value]) => `
            <tr style="border-bottom: 1px solid #3c3c3c;">
              <td style="padding: 4px; color: #9cdcfe; width: 30%;">${key}:</td>
              <td style="padding: 4px; color: #cccccc; word-break: break-all;">${value}</td>
            </tr>
          `).join('')}
        </table>
      `;
    };

    // Format URL parameters
    const formatUrlParams = (paramsStr) => {
      if (!paramsStr) return '<div style="color:#888;">No parameters</div>';
      const params = new URLSearchParams(paramsStr);
      if (params.toString() === '') return '<div style="color:#888;">No parameters</div>';

      return `
        <table style="width:100%; border-collapse: collapse; font-size: 10px; margin-top: 4px;">
          ${Array.from(params.entries()).map(([key, value]) => `
            <tr style="border-bottom: 1px solid #3c3c3c;">
              <td style="padding: 4px; color: #9cdcfe; width: 30%;">${key}:</td>
              <td style="padding: 4px; color: #cccccc; word-break: break-all;">${value}</td>
            </tr>
          `).join('')}
        </table>
      `;
    };

    // Format POST data
    const formatPostData = (data) => {
      if (!data) return '<div style="color:#888;">No payload</div>';
      // Try to parse as JSON for pretty display
      try {
        const parsed = JSON.parse(data);
        return `<pre style="margin: 4px 0; color: #ce9178; font-size: 10px; overflow-x: auto;">${JSON.stringify(parsed, null, 2)}</pre>`;
      } catch (e) {
        // Not JSON, display as-is
        return `<pre style="margin: 4px 0; color: #cccccc; font-size: 10px; overflow-x: auto;">${data}</pre>`;
      }
    };

    const detailsHtml = `
      <div style="padding: 8px; font-size: 11px;">
        <div style="margin-bottom: 12px;">
          <div style="color: #569cd6; font-weight: 600; margin-bottom: 4px;">General</div>
          <div><strong>URL:</strong> <span style="color: #ce9178;">${req.url}</span></div>
          <div><strong>Method:</strong> <span style="color: #dcdcaa;">${req.method}</span></div>
          <div><strong>Status:</strong> <span style="color: ${req.status >= 200 && req.status < 300 ? '#4ec9b0' : '#f48771'};">${req.status} ${req.statusText}</span></div>
          <div><strong>Type:</strong> ${req.type}</div>
          <div><strong>Size:</strong> ${formatSize(req.size)}</div>
          <div><strong>Duration:</strong> ${req.duration} ms</div>
        </div>

        ${req.urlParams ? `
          <div style="margin-bottom: 12px;">
            <div style="color: #569cd6; font-weight: 600; margin-bottom: 4px;">URL Parameters</div>
            ${formatUrlParams(req.urlParams)}
          </div>
        ` : ''}

        ${req.postData ? `
          <div style="margin-bottom: 12px;">
            <div style="color: #569cd6; font-weight: 600; margin-bottom: 4px;">Request Payload</div>
            ${formatPostData(req.postData)}
          </div>
        ` : ''}

        ${req.requestHeaders ? `
          <div style="margin-bottom: 12px;">
            <div style="color: #569cd6; font-weight: 600; margin-bottom: 4px;">Request Headers</div>
            ${formatHeaders(req.requestHeaders)}
          </div>
        ` : ''}

        ${req.responseHeaders ? `
          <div style="margin-bottom: 12px;">
            <div style="color: #569cd6; font-weight: 600; margin-bottom: 4px;">Response Headers</div>
            ${formatHeaders(req.responseHeaders)}
          </div>
        ` : ''}
      </div>
    `;

    document.getElementById('request-details').innerHTML = detailsHtml;
  }

  function clearRequests() {
    networkRequests = [];
    renderRequests();
    document.getElementById('request-details').innerHTML = '<div class="empty-state">Select a request to view details</div>';
  }

  function filterRequests() {
    const filter = document.getElementById('network-filter').value.toLowerCase();
    const rows = document.querySelectorAll('.network-table tbody tr');

    rows.forEach(row => {
      const url = row.querySelector('td')?.textContent.toLowerCase() || '';
      row.style.display = url.includes(filter) ? '' : 'none';
    });
  }

  function formatSize(bytes) {
    if (bytes < 1024) {
      return bytes + ' B';
    } else if (bytes < 1024 * 1024) {
      return (bytes / 1024).toFixed(1) + ' KB';
    } else {
      return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    }
  }

  // Initial render to show empty state
  renderRequests();
</script>
)HTML";

  return html.str();
}

#endif  // OS_LINUX
