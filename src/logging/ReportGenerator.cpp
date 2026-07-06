#include "ReportGenerator.h"
#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <numeric>

ReportGenerator::ReportGenerator(const LogConfig& config) : m_config(config) {}
ReportGenerator::~ReportGenerator() = default;

std::string ReportGenerator::statusColor(const AnalysisResult& result) const {
    switch (result.status) {
        case FrameStatus::PASS: return "#4CAF50";
        case FrameStatus::WARNING: return "#FF9800";
        case FrameStatus::FAIL: return "#f44336";
        default: return "#9E9E9E";
    }
}

std::string ReportGenerator::buildHealthChart(const std::vector<AnalysisResult>& results) const {
    std::stringstream ss;
    ss << R"({
        type: 'line',
        data: {
            labels: [)";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) ss << ",";
        ss << "'" << i << "'";
    }
    ss << R"(],
            datasets: [{
                label: 'Stereo Health Score',
                data: [)";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) ss << ",";
        ss << results[i].stereoHealthScore;
    }
    ss << R"(],
                borderColor: '#2196F3',
                backgroundColor: 'rgba(33, 150, 243, 0.1)',
                fill: true,
                tension: 0.4,
                pointRadius: 3
            }, {
                label: 'SSIM',
                data: [)";
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) ss << ",";
        ss << results[i].ssim * 100.0;
    }
    ss << R"(],
                borderColor: '#4CAF50',
                backgroundColor: 'rgba(76, 175, 80, 0.1)',
                fill: true,
                tension: 0.4,
                pointRadius: 2,
                yAxisID: 'y1'
            }]
        },
        options: {
            responsive: true,
            animation: { duration: 0 },
            plugins: {
                title: { display: true, text: 'Stereo Health Score Over Time' },
                legend: { position: 'bottom' }
            },
            scales: {
                y: { min: 0, max: 100, title: { display: true, text: 'Score' } },
                y1: { min: 0, max: 100, title: { display: true, text: 'SSIM x100' }, position: 'right', grid: { drawOnChartArea: false } },
                x: { title: { display: true, text: 'Frame Number' } }
            }
        }
    })";

    return ss.str();
}

std::string ReportGenerator::buildHtml(const std::vector<AnalysisResult>& results,
                                        const std::vector<std::string>& screenshotPaths) const {
    std::stringstream html;

    html << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Stereo Inspector Report</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #1a1a2e; color: #e0e0e0; padding: 20px; }
h1 { color: #2196F3; margin-bottom: 20px; }
h2 { color: #64B5F6; margin: 30px 0 15px; }
.summary { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 30px; }
.card { background: #16213e; border-radius: 10px; padding: 20px; text-align: center; }
.card h3 { font-size: 14px; color: #888; margin-bottom: 8px; }
.card .value { font-size: 28px; font-weight: bold; }
.card .value.good { color: #4CAF50; }
.card .value.warn { color: #FF9800; }
.card .value.bad { color: #f44336; }
.chart-container { background: #16213e; border-radius: 10px; padding: 20px; margin-bottom: 30px; }
table { width: 100%; border-collapse: collapse; background: #16213e; border-radius: 10px; overflow: hidden; }
th { background: #0f3460; padding: 12px 15px; text-align: left; font-size: 13px; }
td { padding: 10px 15px; border-bottom: 1px solid #0f3460; font-size: 13px; }
tr:hover { background: #1a1a3e; }
.badge { display: inline-block; padding: 2px 10px; border-radius: 12px; font-size: 12px; font-weight: bold; }
.badge.pass { background: #4CAF50; color: white; }
.badge.warning { background: #FF9800; color: white; }
.badge.fail { background: #f44336; color: white; }
.screenshots { display: grid; grid-template-columns: repeat(auto-fill, minmax(320px, 1fr)); gap: 15px; }
.screenshot-card { background: #16213e; border-radius: 10px; overflow: hidden; }
.screenshot-card img { width: 100%; display: block; }
.screenshot-card .info { padding: 10px 15px; display: flex; justify-content: space-between; align-items: center; }
.issue-list { list-style: none; padding: 0; }
.issue-list li { padding: 4px 0; font-size: 12px; color: #ff8a80; }
</style>
</head>
<body>
<h1>Stereo Inspector Analysis Report</h1>
)";

    double avgHealth = 0, avgSsim = 0, avgPixDiff = 0;
    int failCount = 0, warnCount = 0, passCount = 0;
    double minHealth = 100, maxHealth = 0;
    AnalysisResult worstFrame;

    for (const auto& r : results) {
        avgHealth += r.stereoHealthScore;
        avgSsim += r.ssim;
        avgPixDiff += r.pixelDiffPercent;
        if (r.status == FrameStatus::FAIL) failCount++;
        else if (r.status == FrameStatus::WARNING) warnCount++;
        else passCount++;
        if (r.stereoHealthScore < minHealth) { minHealth = r.stereoHealthScore; worstFrame = r; }
        if (r.stereoHealthScore > maxHealth) maxHealth = r.stereoHealthScore;
    }

    if (!results.empty()) {
        avgHealth /= results.size();
        avgSsim /= results.size();
        avgPixDiff /= results.size();
    }

    html << "<div class=\"summary\">";
    html << "<div class=\"card\"><h3>Total Frames</h3><div class=\"value\">" << results.size() << "</div></div>";
    html << "<div class=\"card\"><h3>Avg Health Score</h3><div class=\"value " << (avgHealth >= 80 ? "good" : avgHealth >= 50 ? "warn" : "bad") << "\">" << std::fixed << std::setprecision(1) << avgHealth << "</div></div>";
    html << "<div class=\"card\"><h3>Avg SSIM</h3><div class=\"value\">" << std::fixed << std::setprecision(3) << avgSsim << "</div></div>";
    html << "<div class=\"card\"><h3>Avg Pixel Diff</h3><div class=\"value\">" << std::fixed << std::setprecision(2) << avgPixDiff << "%</div></div>";
    html << "<div class=\"card\"><h3>Min/Max Health</h3><div class=\"value\">" << std::fixed << std::setprecision(1) << minHealth << "/" << maxHealth << "</div></div>";
    html << "<div class=\"card\"><h3>Results</h3><div class=\"value\"><span style=\"color:#4CAF50;\">" << passCount << "</span> / <span style=\"color:#FF9800;\">" << warnCount << "</span> / <span style=\"color:#f44336;\">" << failCount << "</span></div></div>";
    html << "</div>";

    if (!results.empty()) {
        html << "<div class=\"chart-container\"><canvas id=\"healthChart\"></canvas></div>";
    }

    html << "<h2>Detailed Results (" << results.size() << " frames)</h2>";
    html << "<div style=\"overflow-x: auto;\"><table><thead><tr>";
    html << "<th>Frame</th><th>Health</th><th>SSIM</th><th>Pixel Diff</th><th>Edge Sim</th><th>Features</th><th>Brightness</th><th>Blur</th><th>Offset</th><th>Status</th><th>Issues</th>";
    html << "</tr></thead><tbody>";

    size_t step = results.size() > 500 ? results.size() / 500 : 1;
    for (size_t i = 0; i < results.size(); i += step) {
        const auto& r = results[i];
        html << "<tr>";
        html << "<td>" << r.frameNumber << "</td>";
        html << "<td>" << std::fixed << std::setprecision(1) << r.stereoHealthScore << "</td>";
        html << "<td>" << std::fixed << std::setprecision(3) << r.ssim << "</td>";
        html << "<td>" << std::fixed << std::setprecision(2) << r.pixelDiffPercent << "%</td>";
        html << "<td>" << std::fixed << std::setprecision(3) << r.edgeSimilarity << "</td>";
        html << "<td>" << r.featureMatchCount << "</td>";
        html << "<td>" << std::fixed << std::setprecision(3) << r.brightnessDelta << "</td>";
        html << "<td>" << std::fixed << std::setprecision(3) << r.blurDelta << "</td>";
        html << "<td>" << std::fixed << std::setprecision(1) << r.stereoOffset << "</td>";

        std::string statusStr = (r.status == FrameStatus::PASS ? "pass" :
                                 r.status == FrameStatus::WARNING ? "warning" : "fail");
        html << "<td><span class=\"badge " << statusStr << "\">" << statusStr << "</span></td>";

        html << "<td><ul class=\"issue-list\">";
        int maxIssues = std::min((int)r.issues.size(), 3);
        for (int j = 0; j < maxIssues; j++) {
            html << "<li>" << r.issues[j] << "</li>";
        }
        if (r.issues.size() > 3) {
            html << "<li>+ " << (r.issues.size() - 3) << " more</li>";
        }
        html << "</ul></td>";
        html << "</tr>";
    }
    html << "</tbody></table></div>";

    if (!screenshotPaths.empty()) {
        html << "<h2>Captured Screenshots (" << screenshotPaths.size() << ")</h2>";
        html << "<div class=\"screenshots\">";
        for (const auto& path : screenshotPaths) {
            html << "<div class=\"screenshot-card\">";
            html << "<img src=\"" << path << "\" alt=\"Screenshot\">";
            html << "</div>";
        }
        html << "</div>";
    }

    html << "<script>";
    if (!results.empty()) {
        html << "new Chart(document.getElementById('healthChart'), " << buildHealthChart(results) << ");";
    }
    html << "</script>";
    html << "</body></html>";

    return html.str();
}

void ReportGenerator::generate(const std::vector<AnalysisResult>& results,
                                const std::vector<std::string>& screenshotPaths,
                                const std::string& outputPath) {
    std::string outPath = outputPath.empty() ? m_config.reportPath : outputPath;

    if (results.empty()) {
        spdlog::warn("No results to generate report");
        return;
    }

    std::string html = buildHtml(results, screenshotPaths);

    std::ofstream file(outPath);
    if (!file.is_open()) {
        spdlog::error("Failed to write report to {}", outPath);
        return;
    }

    file << html;
    spdlog::info("Report generated: {} ({} frames, {} screenshots)", outPath, results.size(), screenshotPaths.size());
}
