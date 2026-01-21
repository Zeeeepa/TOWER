#include "html_report_generator.h"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>

std::string HTMLReportGenerator::EscapeHTML(const std::string& str) {
    std::string result;
    result.reserve(str.size());
    for (char c : str) {
        switch (c) {
            case '&':  result += "&amp;"; break;
            case '<':  result += "&lt;"; break;
            case '>':  result += "&gt;"; break;
            case '"':  result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default:   result += c;
        }
    }
    return result;
}

std::string HTMLReportGenerator::GenerateHeader() {
    return R"(<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Owl Browser IPC Test Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
)";
}

std::string HTMLReportGenerator::GenerateStyles() {
    return R"(
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            line-height: 1.6;
            color: #333;
            background: #f5f5f5;
            padding: 20px;
        }
        .container { max-width: 1400px; margin: 0 auto; }
        h1 { color: #2c3e50; margin-bottom: 10px; }
        h2 { color: #34495e; margin: 20px 0 10px; border-bottom: 2px solid #3498db; padding-bottom: 5px; }
        .summary-cards {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        .card {
            background: white;
            border-radius: 8px;
            padding: 20px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .card-title { font-size: 14px; color: #7f8c8d; text-transform: uppercase; }
        .card-value { font-size: 28px; font-weight: bold; color: #2c3e50; }
        .card-value.success { color: #27ae60; }
        .card-value.failure { color: #e74c3c; }
        .charts-row {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(400px, 1fr));
            gap: 20px;
            margin: 20px 0;
        }
        .chart-container {
            background: white;
            border-radius: 8px;
            padding: 20px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        table {
            width: 100%;
            border-collapse: collapse;
            background: white;
            border-radius: 8px;
            overflow: hidden;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        th, td { padding: 12px; text-align: left; border-bottom: 1px solid #ecf0f1; }
        th { background: #3498db; color: white; font-weight: 600; }
        tr:hover { background: #f8f9fa; }
        .status-pass { color: #27ae60; font-weight: bold; }
        .status-fail { color: #e74c3c; font-weight: bold; }
        .latency-fast { color: #27ae60; }
        .latency-medium { color: #f39c12; }
        .latency-slow { color: #e74c3c; }
        .failure-box {
            background: #fdf2f2;
            border: 1px solid #f5c6cb;
            border-radius: 8px;
            padding: 15px;
            margin: 10px 0;
        }
        .failure-method { font-weight: bold; color: #e74c3c; }
        .failure-message { color: #721c24; margin-top: 5px; }
        .metadata { font-size: 12px; color: #7f8c8d; margin-bottom: 20px; }
        .filter-bar {
            background: white;
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .filter-bar input, .filter-bar select {
            padding: 8px 12px;
            border: 1px solid #ddd;
            border-radius: 4px;
            margin-right: 10px;
        }
        .progress-bar {
            height: 20px;
            background: #ecf0f1;
            border-radius: 10px;
            overflow: hidden;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #27ae60, #2ecc71);
            transition: width 0.3s;
        }
        .category-badge {
            display: inline-block;
            padding: 2px 8px;
            border-radius: 12px;
            font-size: 11px;
            background: #3498db;
            color: white;
        }
    </style>
)";
}

std::string HTMLReportGenerator::GenerateScripts() {
    return R"(
    <script>
        function filterTable() {
            const search = document.getElementById('searchInput').value.toLowerCase();
            const status = document.getElementById('statusFilter').value;
            const category = document.getElementById('categoryFilter').value;
            const rows = document.querySelectorAll('#methodTable tbody tr');

            rows.forEach(row => {
                const method = row.cells[0].textContent.toLowerCase();
                const rowStatus = row.cells[2].textContent.toLowerCase();
                const rowCategory = row.cells[1].textContent;

                const matchSearch = method.includes(search);
                const matchStatus = status === 'all' || rowStatus.includes(status);
                const matchCategory = category === 'all' || rowCategory === category;

                row.style.display = (matchSearch && matchStatus && matchCategory) ? '' : 'none';
            });
        }

        function sortTable(column) {
            const table = document.getElementById('methodTable');
            const tbody = table.querySelector('tbody');
            const rows = Array.from(tbody.querySelectorAll('tr'));

            const sortKey = table.dataset.sortKey;
            const sortDir = table.dataset.sortDir === 'asc' ? 'desc' : 'asc';
            table.dataset.sortKey = column;
            table.dataset.sortDir = sortDir;

            rows.sort((a, b) => {
                let aVal = a.cells[column].textContent;
                let bVal = b.cells[column].textContent;

                // Numeric sort for latency column
                if (column === 3) {
                    aVal = parseFloat(aVal) || 0;
                    bVal = parseFloat(bVal) || 0;
                    return sortDir === 'asc' ? aVal - bVal : bVal - aVal;
                }

                return sortDir === 'asc' ? aVal.localeCompare(bVal) : bVal.localeCompare(aVal);
            });

            rows.forEach(row => tbody.appendChild(row));
        }
    </script>
)";
}

std::string HTMLReportGenerator::GenerateSummarySection(const json& data) {
    std::ostringstream ss;
    const auto& summary = data["summary"];
    int total = summary["total_tests"].get<int>();
    int passed = summary["passed"].get<int>();
    int failed = summary["failed"].get<int>();
    double passRate = total > 0 ? (passed * 100.0 / total) : 0;

    ss << "<h2>Test Summary</h2>\n";
    ss << "<div class=\"summary-cards\">\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Total Tests</div>"
       << "<div class=\"card-value\">" << total << "</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Passed</div>"
       << "<div class=\"card-value success\">" << passed << "</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Failed</div>"
       << "<div class=\"card-value" << (failed > 0 ? " failure" : "") << "\">" << failed << "</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Pass Rate</div>"
       << "<div class=\"card-value\">" << std::fixed << std::setprecision(1) << passRate << "%</div>"
       << "<div class=\"progress-bar\"><div class=\"progress-fill\" style=\"width:" << passRate << "%\"></div></div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Duration</div>"
       << "<div class=\"card-value\">" << std::setprecision(2) << summary["total_duration_sec"].get<double>() << "s</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Commands/sec</div>"
       << "<div class=\"card-value\">" << std::setprecision(1) << summary["commands_per_second"].get<double>() << "</div></div>\n";

    ss << "</div>\n";

    return ss.str();
}

std::string HTMLReportGenerator::GenerateLatencySection(const json& data) {
    std::ostringstream ss;
    const auto& stats = data["latency_stats"];

    ss << "<h2>Latency Statistics</h2>\n";
    ss << "<div class=\"summary-cards\">\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Min</div>"
       << "<div class=\"card-value latency-fast\">" << std::fixed << std::setprecision(2)
       << stats["min_ms"].get<double>() << "ms</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Avg</div>"
       << "<div class=\"card-value\">" << stats["avg_ms"].get<double>() << "ms</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Median</div>"
       << "<div class=\"card-value\">" << stats["median_ms"].get<double>() << "ms</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">P95</div>"
       << "<div class=\"card-value latency-medium\">" << stats["p95_ms"].get<double>() << "ms</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">P99</div>"
       << "<div class=\"card-value latency-slow\">" << stats["p99_ms"].get<double>() << "ms</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Max</div>"
       << "<div class=\"card-value latency-slow\">" << stats["max_ms"].get<double>() << "ms</div></div>\n";

    ss << "</div>\n";

    // Latency chart
    ss << "<div class=\"charts-row\">\n";
    ss << "<div class=\"chart-container\">\n";
    ss << "<canvas id=\"latencyChart\"></canvas>\n";
    ss << "</div>\n";
    ss << "<div class=\"chart-container\">\n";
    ss << "<canvas id=\"categoryChart\"></canvas>\n";
    ss << "</div>\n";
    ss << "</div>\n";

    return ss.str();
}

std::string HTMLReportGenerator::GenerateResourceSection(const json& data) {
    std::ostringstream ss;

    if (!data.contains("resource_stats") || data["resource_stats"].empty()) {
        return "";
    }

    const auto& stats = data["resource_stats"];

    ss << "<h2>Resource Usage</h2>\n";
    ss << "<div class=\"summary-cards\">\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Peak Memory</div>"
       << "<div class=\"card-value\">" << std::fixed << std::setprecision(1)
       << stats["peak_memory_mb"].get<double>() << " MB</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Avg Memory</div>"
       << "<div class=\"card-value\">" << stats["avg_memory_mb"].get<double>() << " MB</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Peak CPU</div>"
       << "<div class=\"card-value\">" << std::setprecision(1)
       << stats["peak_cpu_percent"].get<double>() << "%</div></div>\n";

    ss << "<div class=\"card\"><div class=\"card-title\">Avg CPU</div>"
       << "<div class=\"card-value\">" << stats["avg_cpu_percent"].get<double>() << "%</div></div>\n";

    ss << "</div>\n";

    // Only show chart if we have timeline data
    if (data.contains("resource_timeline") && !data["resource_timeline"].empty()) {
        // Embed timeline data as JSON for JavaScript to use
        ss << "<script>var resourceTimelineData = " << data["resource_timeline"].dump() << ";</script>\n";

        // Resource timeline chart
        ss << "<div class=\"chart-container\">\n";
        ss << "<canvas id=\"resourceChart\"></canvas>\n";
        ss << "</div>\n";
    }

    return ss.str();
}

std::string HTMLReportGenerator::GenerateCategorySection(const json& data) {
    std::ostringstream ss;

    if (!data.contains("by_category") || data["by_category"].empty()) {
        return "";
    }

    ss << "<h2>Results by Category</h2>\n";
    ss << "<table>\n";
    ss << "<thead><tr><th>Category</th><th>Total</th><th>Passed</th><th>Failed</th><th>Avg Latency</th></tr></thead>\n";
    ss << "<tbody>\n";

    for (auto& [name, cat] : data["by_category"].items()) {
        ss << "<tr>";
        ss << "<td><span class=\"category-badge\">" << EscapeHTML(name) << "</span></td>";
        ss << "<td>" << cat["total"].get<int>() << "</td>";
        ss << "<td class=\"status-pass\">" << cat["passed"].get<int>() << "</td>";
        int failed = cat["failed"].get<int>();
        ss << "<td class=\"" << (failed > 0 ? "status-fail" : "") << "\">" << failed << "</td>";
        ss << "<td>" << std::fixed << std::setprecision(2) << cat["avg_latency_ms"].get<double>() << "ms</td>";
        ss << "</tr>\n";
    }

    ss << "</tbody></table>\n";

    return ss.str();
}

std::string HTMLReportGenerator::GenerateMethodTable(const json& data) {
    std::ostringstream ss;

    ss << "<h2>Method Details</h2>\n";

    // Filter bar
    ss << "<div class=\"filter-bar\">\n";
    ss << "<input type=\"text\" id=\"searchInput\" placeholder=\"Search method...\" onkeyup=\"filterTable()\">\n";
    ss << "<select id=\"statusFilter\" onchange=\"filterTable()\">\n";
    ss << "<option value=\"all\">All Status</option>\n";
    ss << "<option value=\"pass\">Passed</option>\n";
    ss << "<option value=\"fail\">Failed</option>\n";
    ss << "</select>\n";
    ss << "<select id=\"categoryFilter\" onchange=\"filterTable()\">\n";
    ss << "<option value=\"all\">All Categories</option>\n";

    // Collect unique categories
    std::set<std::string> categories;
    for (const auto& cmd : data["commands"]) {
        categories.insert(cmd["category"].get<std::string>());
    }
    for (const auto& cat : categories) {
        ss << "<option value=\"" << EscapeHTML(cat) << "\">" << EscapeHTML(cat) << "</option>\n";
    }

    ss << "</select>\n";
    ss << "</div>\n";

    // Table
    ss << "<table id=\"methodTable\" data-sort-key=\"0\" data-sort-dir=\"asc\">\n";
    ss << "<thead><tr>";
    ss << "<th onclick=\"sortTable(0)\" style=\"cursor:pointer\">Method</th>";
    ss << "<th onclick=\"sortTable(1)\" style=\"cursor:pointer\">Category</th>";
    ss << "<th onclick=\"sortTable(2)\" style=\"cursor:pointer\">Status</th>";
    ss << "<th onclick=\"sortTable(3)\" style=\"cursor:pointer\">Latency</th>";
    ss << "<th>Expected</th>";
    ss << "<th>Memory Delta</th>";
    ss << "</tr></thead>\n";
    ss << "<tbody>\n";

    for (const auto& cmd : data["commands"]) {
        bool success = cmd["success"].get<bool>();
        double latency = cmd["latency_ms"].get<double>();
        double expected = cmd.contains("expected_latency_ms") ? cmd["expected_latency_ms"].get<double>() : 0.0;
        double memBefore = cmd["memory_before_mb"].get<double>();
        double memAfter = cmd["memory_after_mb"].get<double>();
        double memDelta = memAfter - memBefore;

        // Determine latency class based on expected time or default thresholds
        std::string latencyClass;
        if (expected > 0) {
            // Has expected time - compare against it
            latencyClass = latency <= expected ? "latency-fast" : "latency-slow";
        } else {
            // No expected time - use default thresholds
            latencyClass = latency < 50 ? "latency-fast" : (latency < 500 ? "latency-medium" : "latency-slow");
        }

        ss << "<tr>";
        ss << "<td>" << EscapeHTML(cmd["method"].get<std::string>()) << "</td>";
        ss << "<td><span class=\"category-badge\">" << EscapeHTML(cmd["category"].get<std::string>()) << "</span></td>";
        ss << "<td class=\"" << (success ? "status-pass" : "status-fail") << "\">" << (success ? "PASS" : "FAIL") << "</td>";
        ss << "<td class=\"" << latencyClass << "\">" << std::fixed << std::setprecision(2) << latency << "ms</td>";
        ss << "<td>" << std::noshowpos;
        if (expected > 0) {
            ss << std::setprecision(0) << expected << "ms";
        } else {
            ss << "-";
        }
        ss << "</td>";
        ss << "<td>" << std::showpos << std::setprecision(1) << memDelta << " MB</td>";
        ss << "</tr>\n";
    }

    ss << "</tbody></table>\n";

    return ss.str();
}

std::string HTMLReportGenerator::GenerateFailureSection(const json& data) {
    std::ostringstream ss;

    if (!data.contains("failures") || data["failures"].empty()) {
        return "";
    }

    ss << "<h2>Failures</h2>\n";

    for (const auto& failure : data["failures"]) {
        ss << "<div class=\"failure-box\">\n";
        ss << "<div class=\"failure-method\">" << EscapeHTML(failure["method"].get<std::string>()) << "</div>\n";
        ss << "<div class=\"failure-message\">";
        ss << "Expected: " << EscapeHTML(failure["expected"].get<std::string>()) << " | ";
        ss << "Actual: " << EscapeHTML(failure["actual"].get<std::string>());
        ss << "</div>\n";
        if (failure.contains("message") && !failure["message"].get<std::string>().empty()) {
            ss << "<div class=\"failure-message\">" << EscapeHTML(failure["message"].get<std::string>()) << "</div>\n";
        }
        ss << "</div>\n";
    }

    return ss.str();
}

std::string HTMLReportGenerator::GenerateFooter() {
    return R"(
    <script>
        // Latency distribution chart
        const latencyData = [];
        document.querySelectorAll('#methodTable tbody tr').forEach(row => {
            latencyData.push(parseFloat(row.cells[3].textContent) || 0);
        });

        // Create histogram bins
        const bins = [0, 10, 50, 100, 500, 1000, 5000, Infinity];
        const binLabels = ['<10ms', '10-50ms', '50-100ms', '100-500ms', '500ms-1s', '1-5s', '>5s'];
        const binCounts = new Array(bins.length - 1).fill(0);

        latencyData.forEach(lat => {
            for (let i = 0; i < bins.length - 1; i++) {
                if (lat >= bins[i] && lat < bins[i + 1]) {
                    binCounts[i]++;
                    break;
                }
            }
        });

        new Chart(document.getElementById('latencyChart'), {
            type: 'bar',
            data: {
                labels: binLabels,
                datasets: [{
                    label: 'Number of Commands',
                    data: binCounts,
                    backgroundColor: ['#27ae60', '#2ecc71', '#f1c40f', '#e67e22', '#e74c3c', '#c0392b', '#8e44ad']
                }]
            },
            options: {
                plugins: { title: { display: true, text: 'Latency Distribution' } },
                scales: { y: { beginAtZero: true } }
            }
        });

        // Category chart
        const categoryLabels = [];
        const categoryPassed = [];
        const categoryFailed = [];

        document.querySelectorAll('table:not(#methodTable) tbody tr').forEach(row => {
            if (row.cells.length >= 4) {
                categoryLabels.push(row.cells[0].textContent.trim());
                categoryPassed.push(parseInt(row.cells[2].textContent) || 0);
                categoryFailed.push(parseInt(row.cells[3].textContent) || 0);
            }
        });

        if (categoryLabels.length > 0) {
            new Chart(document.getElementById('categoryChart'), {
                type: 'bar',
                data: {
                    labels: categoryLabels,
                    datasets: [
                        { label: 'Passed', data: categoryPassed, backgroundColor: '#27ae60' },
                        { label: 'Failed', data: categoryFailed, backgroundColor: '#e74c3c' }
                    ]
                },
                options: {
                    plugins: { title: { display: true, text: 'Results by Category' } },
                    scales: { x: { stacked: true }, y: { stacked: true, beginAtZero: true } }
                }
            });
        }

        // Resource timeline chart
        if (typeof resourceTimelineData !== 'undefined' && resourceTimelineData.length > 0) {
            const resourceCanvas = document.getElementById('resourceChart');
            if (resourceCanvas) {
                // Normalize timestamps to start from 0
                const startTime = resourceTimelineData[0].timestamp_ms;
                const labels = resourceTimelineData.map(d => ((d.timestamp_ms - startTime) / 1000).toFixed(1) + 's');
                const memoryData = resourceTimelineData.map(d => d.memory_mb);
                const cpuData = resourceTimelineData.map(d => d.cpu_percent);

                new Chart(resourceCanvas, {
                    type: 'line',
                    data: {
                        labels: labels,
                        datasets: [
                            {
                                label: 'Memory (MB)',
                                data: memoryData,
                                borderColor: '#3498db',
                                backgroundColor: 'rgba(52, 152, 219, 0.1)',
                                fill: true,
                                tension: 0.3,
                                yAxisID: 'y'
                            },
                            {
                                label: 'CPU (%)',
                                data: cpuData,
                                borderColor: '#e74c3c',
                                backgroundColor: 'rgba(231, 76, 60, 0.1)',
                                fill: true,
                                tension: 0.3,
                                yAxisID: 'y1'
                            }
                        ]
                    },
                    options: {
                        responsive: true,
                        interaction: {
                            mode: 'index',
                            intersect: false
                        },
                        plugins: {
                            title: { display: true, text: 'Resource Usage Over Time' }
                        },
                        scales: {
                            x: {
                                title: { display: true, text: 'Time' }
                            },
                            y: {
                                type: 'linear',
                                display: true,
                                position: 'left',
                                title: { display: true, text: 'Memory (MB)' },
                                beginAtZero: true
                            },
                            y1: {
                                type: 'linear',
                                display: true,
                                position: 'right',
                                title: { display: true, text: 'CPU (%)' },
                                beginAtZero: true,
                                max: 100,
                                grid: { drawOnChartArea: false }
                            }
                        }
                    }
                });
            }
        }
    </script>
</body>
</html>
)";
}

std::string HTMLReportGenerator::GenerateHTML(const json& report_data) {
    std::ostringstream html;

    html << GenerateHeader();
    html << GenerateStyles();
    html << "</head>\n<body>\n";
    html << "<div class=\"container\">\n";

    // Title and metadata
    html << "<h1>Owl Browser IPC Test Report</h1>\n";
    if (report_data.contains("metadata")) {
        const auto& meta = report_data["metadata"];
        html << "<div class=\"metadata\">";
        html << "Generated: " << meta["timestamp"].get<std::string>() << " | ";
        html << "Platform: " << meta["platform"].get<std::string>() << " | ";
        html << "Browser: " << meta["browser_path"].get<std::string>();
        html << "</div>\n";
    }

    html << GenerateSummarySection(report_data);
    html << GenerateLatencySection(report_data);
    html << GenerateResourceSection(report_data);
    html << GenerateCategorySection(report_data);
    html << GenerateMethodTable(report_data);
    html << GenerateFailureSection(report_data);

    html << "</div>\n";
    html << GenerateScripts();
    html << GenerateFooter();

    return html.str();
}

bool HTMLReportGenerator::SaveHTML(const json& report_data, const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }

    file << GenerateHTML(report_data);
    return true;
}
