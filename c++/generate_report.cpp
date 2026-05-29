/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File:      generate_report.cpp
   Version:   3.9.6 (Technology-Aware Reporting Integration)
   Author:    sussjb99
   Last Modified: 2026-05-28

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose:
     Generates a high-quality console-styled HTML report mirroring
     the structural aesthetics, typography, and spacing of Help.html.
     Fully extracts and maps all hardware identity, bridge capabilities,
     capacity metrics, performance calibration, and extended filesystem
     profile metrics directly from the state-tracking XML.
     Incorporates an abstract mapping translation layer to ensure
     native console configuration names output beautifully in web layout formats.
     Dynamically maps paths relative to process base context, breaking dependency
     on static folder naming schemes.
     
     UPDATED: Added an architectural interpretation layer to differentiate
     and correctly assess performance/integrity thresholds based on target
     hardware technology classifications (Flash, SSD, SMR, CMR). Cleaned up
     std::transform conversions to resolve compiler narrowing warning C4244.

   ============================================================ */

#pragma comment(lib, "Shell32.lib")

#include <windows.h>
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <algorithm>
#include <ctime>
#include <cctype>

// ------------------------------------------------------------
// Utility helpers
// ------------------------------------------------------------

static std::string trim(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace((unsigned char)s[start])) ++start;
    size_t end = s.size();
    while (end > start && std::isspace((unsigned char)s[end - 1])) --end;
    return s.substr(start, end - start);
}

static bool fileExists(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

static bool dirExists(const std::string& path)
{
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

static bool ensureDirectory(const std::string& path)
{
    if (dirExists(path)) return true;
    if (CreateDirectoryA(path.c_str(), nullptr)) return true;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return true;
    return false;
}

static std::string readFileToString(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

static bool writeStringToFileUTF8(const std::string& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(content.data(), (std::streamsize)content.size());
    return true;
}

static std::string replaceAll(std::string text, const std::string& token, const std::string& value)
{
    if (token.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        text.replace(pos, token.size(), value);
        pos += value.size();
    }
    return text;
}

// ------------------------------------------------------------
// INI parsing
// ------------------------------------------------------------

static std::map<std::string, std::string> loadIniSection(
    const std::string& path, const std::string& sectionName)
{
    std::map<std::string, std::string> result;
    std::ifstream in(path);
    if (!in) return result;

    std::string line;
    bool inSection = false;
    std::string wanted = "[" + sectionName + "]";

    while (std::getline(in, line))
    {
        line = trim(line);
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']')
        {
            inSection = (line == wanted);
            continue;
        }

        if (!inSection) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        
        size_t inlineComment = val.find_first_of(";#");
        if (inlineComment != std::string::npos) {
            val = val.substr(0, inlineComment);
            val = trim(val);
        }
        
        result[key] = val;
    }

    return result;
}

// ------------------------------------------------------------
// Minimal XML tag extraction
// ------------------------------------------------------------

static std::string getTagValue(const std::string& xml, const std::string& tag)
{
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";
    size_t start = xml.find(openTag);
    if (start == std::string::npos) return {};
    start += openTag.size();
    size_t end = xml.find(closeTag, start);
    if (end == std::string::npos) return {};
    return xml.substr(start, end - start);
}

// ------------------------------------------------------------
// Time formatting
// ------------------------------------------------------------

static std::string getTimestampForFilename()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

static std::string getDisplayTime()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

// ------------------------------------------------------------
// Browser launcher
// ------------------------------------------------------------

static void launchBrowserWithConfig(
    const std::string& configPath,
    const std::string& htmlPath)
{
    auto browserCfg = loadIniSection(configPath, "Browser");

    std::string browser = "default";
    if (browserCfg.count("Browser"))
        browser = browserCfg["Browser"];

    std::string url = "file:///" + htmlPath;
    std::replace(url.begin(), url.end(), '\\', '/');

    if (browser == "default")
    {
        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }

    std::string args = "\"" + url + "\"";

    if (browser == "edge")
        ShellExecuteA(nullptr, "open", "msedge.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    else if (browser == "chrome")
        ShellExecuteA(nullptr, "open", "chrome.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    else if (browser == "brave")
        ShellExecuteA(nullptr, "open", "brave.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    else if (browser == "firefox")
        ShellExecuteA(nullptr, "open", "firefox.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    else
        ShellExecuteA(nullptr, "open", browser.c_str(), url.c_str(), nullptr, SW_SHOWNORMAL);
}

// ------------------------------------------------------------
// Cross-Platform Color Translation Setup
// ------------------------------------------------------------

struct PlatformColors {
    std::string webHex;
};

static const std::map<std::string, PlatformColors> WEB_COLOR_LOOKUP = {
    {"Green",       {"#00FF00"}}, // Upgrades dull standard web green to crisp terminal green
    {"LightGreen",  {"#90EE90"}},
    {"Cyan",        {"#00FFFF"}},
    {"Yellow",      {"#FFFF00"}},
    {"Magenta",     {"#FF00FF"}},
    {"White",       {"#FFFFFF"}},
    {"Gray",        {"#808080"}},
    {"Blue",        {"#0000FF"}},
    {"Purple",      {"#800080"}},
    {"Red",         {"#FF0000"}}
};

// ------------------------------------------------------------
// HTML template
// ------------------------------------------------------------

const char* REPORT_TEMPLATE = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<style>
    body {
        background-color: #000000;
        color: #ffffff;
        font-family: "Consolas", "Lucida Console", "Courier New", monospace;
        font-size: 13px;
        line-height: 1.5;
        margin: 0;
        padding: 12px;
        
        display: flex;
        flex-direction: column;
        align-items: center;
        min-height: 100vh;
        box-sizing: border-box;
    }

    .terminal-container {
        max-width: 650px;       
        width: 100%;            
        padding: 0px;
        box-sizing: border-box;
    }

    .color-title {
        color: {{TITLE_COLOR}};
        font-weight: bold;
        text-transform: uppercase;
    }

    .color-label {
        color: {{LABEL_COLOR}};
        font-weight: bold;
    }

    .color-value {
        color: {{VALUE_COLOR}};
    }

    .separator {
        border-bottom: 2px solid {{SEPARATOR_COLOR}};
        margin: 16px 0;
        width: 100%;
        height: 0px;
    }

    h1 {
        font-size: 13px;
        margin: 5px 0 10px 0;
        padding: 0;
        text-transform: uppercase;
    }

    h2 {
        font-size: 13px;
        color: {{SUBHEADING_COLOR}}; 
        margin: 18px 0 6px 0;
        padding: 0;
        text-transform: uppercase;
        font-weight: bold;
    }

    table {
        width: 100%;
        border-collapse: collapse;
        margin: 12px 0;
    }

    td {
        padding: 4px 6px 4px 0;
        vertical-align: top;
        font-size: 13px;
    }

    .device-info-table td:first-child {
        width: 190px; 
        font-weight: bold;
    }

    .ok   { color: {{STATUS_OK}}; font-weight: bold; }
    .warn { color: {{STATUS_WARN}}; font-weight: bold; }
    .err  { color: {{STATUS_ERR}}; font-weight: bold; }
</style>
</head>
<body>

<div class="terminal-container">
    <div><span class="color-title">PORTABLE USB DRIVE REPORT</span></div>
    <div><span class="color-label">Copyright(c) 2026 sussjb99</span></div>
    <div><span class="color-value">Last Updated 2026/04/12</span></div>

    <div class="separator"></div>

    <h2>OVERVIEW</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">DRIVE LETTER</td>
            <td class="color-value">{{DRIVE}}</td>
        </tr>
        <tr>
            <td class="color-label">MODEL</td>
            <td class="color-value">{{MODEL}}</td>
        </tr>
        <tr>
            <td class="color-label">SERIAL</td>
            <td class="color-value">{{SERIAL}}</td>
        </tr>
        <tr>
            <td class="color-label">GENERATED</td>
            <td class="color-value">{{TIME}}</td>
        </tr>
        <tr>
            <td class="color-label">STATUS</td>
            <td><span class="{{STATUS_CLASS}}">{{STATUS}}</span></td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>HARDWARE IDENTITY</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">TECHNOLOGY</td>
            <td class="color-value">{{TECH}}</td>
        </tr>
        <tr>
            <td class="color-label">ARCHITECTURAL PROFILE</td>
            <td class="color-value" style="font-style: italic; color: #8a8a8a;">{{TECH_NOTE}}</td>
        </tr>
        <tr>
            <td class="color-label">INTERFACE / TRANSPORT</td>
            <td class="color-value">{{INTERFACE}} / {{USB_TRANSPORT}}</td>
        </tr>
        <tr>
            <td class="color-label">DRIVE TYPE</td>
            <td class="color-value">{{DRIVE_TYPE}}</td>
        </tr>
        <tr>
            <td class="color-label">FORM FACTOR</td>
            <td class="color-value">{{FORM_FACTOR}}</td>
        </tr>
        <tr>
            <td class="color-label">FILE SYSTEM</td>
            <td class="color-value">{{FS}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>BRIDGE CAPABILITIES</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">BRIDGE SAT</td>
            <td class="color-value">{{BRIDGE_SAT}}</td>
        </tr>
        <tr>
            <td class="color-label">SMART PASSTHROUGH</td>
            <td class="color-value">{{SMART_PASSTHROUGH}}</td>
        </tr>
        <tr>
            <td class="color-label">TRIM SUPPORTED</td>
            <td class="color-value">{{TRIM_SUPPORTED}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>STORAGE CAPACITY</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">TOTAL CAPACITY</td>
            <td class="color-value">{{TOTAL}}</td>
        </tr>
        <tr>
            <td class="color-label">USED SPACE</td>
            <td class="color-value">{{USED}}</td>
        </tr>
        <tr>
            <td class="color-label">FREE SPACE</td>
            <td class="color-value">{{FREE}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>PERFORMANCE & CALIBRATION</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">CALIBRATED STATUS</td>
            <td class="color-value">{{CALIBRATED}}</td>
        </tr>
        <tr>
            <td class="color-label">SEQUENTIAL READ</td>
            <td class="color-value">{{READ_SPEED}}</td>
        </tr>
        <tr>
            <td class="color-label">SEQUENTIAL WRITE</td>
            <td class="color-value">{{WRITE_SPEED}}</td>
        </tr>
        <tr>
            <td class="color-label">RANDOM READ IOPS</td>
            <td class="color-value">{{RANDOM_IOPS}}</td>
        </tr>
        <tr>
            <td class="color-label">ACCESS LATENCY</td>
            <td class="color-value">{{LATENCY_MS}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>FILE SYSTEM PROFILE</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">TOTAL INDEXED FILES</td>
            <td class="color-value">{{FILE_COUNT}}</td>
        </tr>
        <tr>
            <td class="color-label">TOTAL DIRECTORIES</td>
            <td class="color-value">{{DIR_COUNT}}</td>
        </tr>
        <tr>
            <td class="color-label">TOTAL FILE BYTES</td>
            <td class="color-value">{{TOTAL_BYTES}}</td>
        </tr>
        <tr>
            <td class="color-label">AVERAGE FILE SIZE</td>
            <td class="color-value">{{AVG_FILE_SIZE}}</td>
        </tr>
        <tr>
            <td class="color-label">SMALL FILE RATIO</td>
            <td class="color-value">{{SMALL_FILE_RATIO}}</td>
        </tr>
        <tr>
            <td class="color-label">MAX DIRECTORY DEPTH</td>
            <td class="color-value">{{MAX_DEPTH}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>FILE INTEGRITY SCAN</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">INTEGRITY GRADE</td>
            <td class="color-value">{{F_GRADE}}</td>
        </tr>
        <tr>
            <td class="color-label">FILES CHECKED</td>
            <td class="color-value">{{FILES_CHECKED}}</td>
        </tr>
        <tr>
            <td class="color-label">CORRUPT FILES</td>
            <td class="color-value">{{CORRUPT_FILES}}</td>
        </tr>
        <tr>
            <td class="color-label">LAST SCAN DATE</td>
            <td class="color-value">{{LAST_SCAN_DATE}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>SURFACE ANALYSIS</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">SURFACE GRADE</td>
            <td class="color-value">{{S_GRADE}}</td>
        </tr>
        <tr>
            <td class="color-label">STABILITY SCORE</td>
            <td class="color-value">{{S_STAB}}</td>
        </tr>
        <tr>
            <td class="color-label">AVERAGE READ SPEED</td>
            <td class="color-value">{{S_SPEED}}</td>
        </tr>
        <tr>
            <td class="color-label">SCAN COVERAGE</td>
            <td class="color-value">{{S_COV}}</td>
        </tr>
        <tr>
            <td class="color-label">ERRORS DETECTED</td>
            <td class="color-value">{{ERRORS}}</td>
        </tr>
        <tr>
            <td class="color-label">SCAN TIME TAKEN</td>
            <td class="color-value">{{TIME_TAKEN}}</td>
        </tr>
    </table>

    <div class="separator"></div>

    <h2>TEMPERATURE & SMART</h2>
    <table class="device-info-table">
        <tr>
            <td class="color-label">TEMPERATURE</td>
            <td class="color-value">{{TEMP}}</td>
        </tr>
        <tr>
            <td class="color-label">SMART STATUS</td>
            <td class="color-value">{{SMART}}</td>
        </tr>
        <tr>
            <td class="color-label">TELEMETRY NOTES</td>
            <td class="color-value">{{SMART_NOTES}}</td>
        </tr>
        <tr>
            <td class="color-label">POLICY STRESS PROFILE</td>
            <td class="color-value">{{STRESS_PROFILE}} (Allowed: {{STRESS_ALLOWED}})</td>
        </tr>
    </table>

    <div class="separator"></div>

    <div class="color-title" style="text-align: center; margin-top: 20px;">END OF REPORT</div>
</div>

</body>
</html>
)HTML";

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main(int argc, char* argv[])
{
    std::cout << "[generate_report] Starting...\n";

    if (argc < 2)
    {
        std::cerr << "Usage: generate_report.exe <DriveLetter>\n";
        return 1;
    }

    char driveChar = argv[1][0];
    if (!std::isalpha((unsigned char)driveChar))
    {
        std::cerr << "[generate_report] Invalid drive letter.\n";
        return 1;
    }
    driveChar = (char)std::toupper((unsigned char)driveChar);

    // SYSTEM SAFETY PROFILE GUARD
    if (driveChar == 'C')
    {
        std::cerr << "[SECURITY_ALERT] Target drive C: is protected. Operation aborted.\n";
        return 3;
    }

    std::string drive = "";
    drive.push_back(driveChar);
    drive.append(":");

    std::string rootBase;
    rootBase.push_back(driveChar);
    rootBase.append(":\\");

    // ============================================================
    // DYNAMIC ENVIRONMENT PATH RESOLUTION ENGINE
    // ============================================================
    char pathBuf[MAX_PATH];
    if (!GetModuleFileNameA(NULL, pathBuf, MAX_PATH)) {
        std::cerr << "ERROR: Failed to resolve current process binary base location.\n";
        return 1;
    }

    std::string exeFullPath(pathBuf);
    size_t lastSlashPos = exeFullPath.find_last_of("\\/");
    if (lastSlashPos == std::string::npos) return 1;
    std::string appDir = exeFullPath.substr(0, lastSlashPos + 1); // Folder containing executable ("bin\")

    // Multiplex Context Strategy to target config subfolder dynamically without static name assumptions
    std::string configPath = "";
    std::string folderConfig = appDir + "..\\config\\config.ini"; // Check out of bin\ into config\ folder
    std::string localConfig  = appDir + "config.ini";             // Check adjacent to exe inside bin\ as backup

    if (fileExists(folderConfig)) {
        configPath = folderConfig;
    } else if (fileExists(localConfig)) {
        configPath = localConfig;
    } else {
        std::cerr << "ERROR: config.ini not found dynamically relative to executable context. Execution halted.\n"
                  << "Checked contexts:\n"
                  << "  1) " << folderConfig << "\n"
                  << "  2) " << localConfig << std::endl;
        return 1;
    }

    auto paths = loadIniSection(configPath, "Paths");

    // Compute active suite root base directory context dynamically from config parameter
    std::string rootFolder = paths.count("Root") ? paths["Root"] : "PUDIS";
    std::string rootPath   = rootBase + rootFolder + "\\";

    // If target directory infrastructure does not yet exist on target drive, resolve relative to the running suite
    if (!dirExists(rootPath)) {
        size_t upSlash = folderConfig.find_last_of("\\/", folderConfig.size() - 19);
        std::string calculatedParentDir = (upSlash != std::string::npos) ? folderConfig.substr(0, upSlash + 1) : appDir;
        rootPath = calculatedParentDir + rootFolder + "\\";
    }

    std::string statusXmlRel = paths.count("StatusXML") ? paths["StatusXML"] : "Drive_Status.xml";
    std::string reportsRel   = paths.count("Reports")   ? paths["Reports"]   : "reports";

    std::string statusXmlPath = (statusXmlRel.find(':') != std::string::npos) ? statusXmlRel : rootPath + statusXmlRel;
    std::string reportDir = (reportsRel.find(':') != std::string::npos) ? reportsRel : rootPath + reportsRel;

    if (!reportDir.empty() && reportDir.back() != '\\')
        reportDir.push_back('\\');

    if (!fileExists(statusXmlPath))
    {
        std::cerr << "[generate_report] Drive_Status.xml not found. Checked: " << statusXmlPath << "\n";
        return 1;
    }

    ensureDirectory(reportDir);

    auto ui = loadIniSection(configPath, "UI");

    auto getColor = [&](const std::string& key, const std::string& fallback)
    {
        if (!ui.count(key)) return fallback;
        return ui[key];
    };

    // 1. Fetch raw strings from ini
    std::string rawTitle = getColor("TitleColor", "Cyan");
    std::string rawSep   = getColor("SeparatorColor", "Blue");
    std::string rawLabel = getColor("LabelColor", "Green"); 
    std::string rawSub   = getColor("MenuColor", "Magenta"); 
    std::string rawValue = getColor("ValueColor", "White");
    std::string rawOk    = getColor("StatusOKColor", "Green");
    std::string rawWarn  = getColor("WarningColor", "Yellow");
    std::string rawErr   = getColor("ErrorColor", "Red");

    // 2. Map translation lambda to transform standard console strings to web colors
    auto toWebColor = [](const std::string& rawColor) {
        if (WEB_COLOR_LOOKUP.count(rawColor)) {
            return WEB_COLOR_LOOKUP.at(rawColor).webHex;
        }
        return rawColor; // Fallback to raw value (preserves literal hex entries like #55ff55)
    };

    std::string titleColor      = toWebColor(rawTitle);
    std::string sepColor        = toWebColor(rawSep);
    std::string labelColor      = toWebColor(rawLabel); 
    std::string subheadingColor = toWebColor(rawSub); 
    std::string valueColor      = toWebColor(rawValue);
    std::string okColor         = toWebColor(rawOk);
    std::string warnColor       = toWebColor(rawWarn);
    std::string errColor        = toWebColor(rawErr);

    std::string xml = readFileToString(statusXmlPath);
    if (xml.empty())
    {
        std::cerr << "[generate_report] XML empty.\n";
        return 1;
    }

    auto X = [&](const std::string& tag) { return getTagValue(xml, tag); };

    // Parse hardware variables
    std::string model        = X("Model");
    std::string serial       = X("Serial");
    std::string tech         = X("Technology");
    std::string interfaceD   = X("Interface");
    std::string usbTransport = X("UsbTransport");
    std::string driveType    = X("DriveType");
    std::string formFactor   = X("FormFactor");
    std::string fs           = X("FileSystem");

    std::string bridgeSat     = X("BridgeSAT");
    std::string smartPass     = X("SmartPassthrough");
    std::string trimSupported = X("TrimSupported");

    // Storage formatting
    std::string total       = X("TotalGB").empty() ? "N/A" : X("TotalGB") + " GB";
    std::string used        = X("UsedGB").empty() ? "N/A" : X("UsedGB") + " GB (" + (X("PercentUsed").empty() ? "N/A" : X("PercentUsed")) + "%)";
    std::string freeGB      = X("FreeGB").empty() ? "N/A" : X("FreeGB") + " GB";

    // Performance formatting
    std::string calibrated = X("Calibrated").empty() ? "False" : X("Calibrated");
    std::string readSpeed  = X("ReadSpeed").empty() ? "N/A" : X("ReadSpeed") + " MB/s";
    std::string writeSpeed = X("WriteSpeed").empty() ? "N/A" : X("WriteSpeed") + " MB/s"; 
    std::string randomIops = X("RandomIOPS").empty() ? "N/A" : X("RandomIOPS") + " IOPS";
    std::string latencyMs  = X("LatencyMS").empty() ? "N/A" : X("LatencyMS") + " ms";

    // File System Profile formatting
    std::string fileCount  = X("TotalFiles").empty() ? (X("FileCount").empty() ? "N/A" : X("FileCount") + " files") : X("TotalFiles") + " files";
    std::string dirCount   = X("DirectoryCount").empty() ? "N/A" : X("DirectoryCount") + " directories";
    std::string totalBytes = X("TotalBytes").empty() ? "N/A" : X("TotalBytes") + " bytes";
    std::string avgSize    = X("AverageFileSize").empty() ? "N/A" : X("AverageFileSize") + " bytes";
    std::string ratio      = X("SmallFileRatio").empty() ? "N/A" : X("SmallFileRatio");
    std::string maxDepth   = X("MaxDepth").empty() ? "N/A" : X("MaxDepth") + " layers";

    std::string fGrade        = X("IntegrityGrade");
    std::string filesChecked  = X("FilesChecked");
    std::string corruptFiles  = X("CorruptFiles");
    std::string lastScanDate  = X("LastScanDate");

    std::string sGrade     = X("SurfaceGrade");
    std::string sStab      = X("StabilityScore").empty() ? "N/A" : X("StabilityScore") + " %";
    std::string sSpeed     = X("AvgReadSpeed").empty() ? "N/A" : X("AvgReadSpeed") + " MB/s";
    std::string sCov       = X("ScanCoverage").empty() ? "N/A" : X("ScanCoverage") + " %";
    std::string errors      = X("ErrorsDetected");
    std::string timeTaken  = X("ActualTimeTaken");

    std::string temp          = X("Temperature").empty() ? "N/A" : X("Temperature") + " C";
    std::string smart         = X("SmartStatus");
    std::string smartNotes    = X("Notes"); 
    std::string stressProfile = X("StressProfile");
    std::string stressAllowed = X("StressTestAllowed");

    // Clean Fallbacks
    if (interfaceD.empty())   interfaceD   = "USB";
    if (usbTransport.empty()) usbTransport = "N/A";
    if (driveType.empty())    driveType    = "N/A";
    if (formFactor.empty())   formFactor   = "N/A";
    if (bridgeSat.empty())    bridgeSat    = "False";
    if (smartPass.empty())    smartPass    = "False";
    if (trimSupported.empty()) trimSupported = "False";
    if (dirCount == " directories" || dirCount.empty()) dirCount = "N/A"; 
    if (filesChecked.empty()) filesChecked = "N/A";
    if (corruptFiles.empty()) corruptFiles = "N/A";
    if (lastScanDate.empty()) lastScanDate = "N/A";
    if (timeTaken.empty())    timeTaken    = "N/A";
    if (smart.empty())        smart        = "N/A";
    if (smartNotes.empty())   smartNotes   = "No telemetry notes available.";
    if (stressProfile.empty()) stressProfile = "External";
    if (stressAllowed.empty()) stressAllowed = "True";

    // ============================================================
    // TECHNOLOGY-AWARE INTERPRETATION ENGINE
    // ============================================================
    std::string techNote = "Standard Legacy Storage Profile. Default baseline threshold criteria applied.";
    
    std::string techUpper = tech;
    std::transform(techUpper.begin(), techUpper.end(), techUpper.begin(), 
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::string driveTypeUpper = driveType;
    std::transform(driveTypeUpper.begin(), driveTypeUpper.end(), driveTypeUpper.begin(), 
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (techUpper.find("FLASH") != std::string::npos || driveTypeUpper == "REMOVABLE") 
    {
        techNote = "Flash Storage Architecture. Health evaluated via write-then-read block cycle integrity. "
                   "Linear cache drops are a normal trait; erratic multi-second stutters reveal cell retention decay.";
    } 
    else if (techUpper.find("SMR") != std::string::npos) 
    {
        techNote = "Shingled Magnetic Recording (SMR). Overlapping data track geometry. "
                   "Sustained sequential multi-file writes exhaust the PMR sector cache zone, creating normal layout reorganization latencies.";
    } 
    else if (techUpper.find("CMR") != std::string::npos) 
    {
        techNote = "Conventional Magnetic Recording (CMR). Standard parallel track mechanics. "
                   "Requires highly consistent read/write bandwidth timelines. Localized drop-outs indicate physical surface damage.";
    } 
    else if (techUpper.find("SSD") != std::string::npos) 
    {
        techNote = "Solid State Drive (SSD). Dynamic flash matrix controller layer. "
                   "Performance metrics scale via wear leveling logs. Real-time health maps to active bad block reassignments.";
    }

    bool caution = (sGrade == "Degraded" || fGrade == "Critical" || smart == "FAILING");
    try {
        if (!errors.empty() && std::stoi(errors) > 0) caution = true;
    } catch (...) {}

    // Flash-Specific Assessment Logic Overrides (Handles absent/empty SMART implementations on low-cost media)
    if (techUpper.find("FLASH") != std::string::npos || driveTypeUpper == "REMOVABLE") 
    {
        if (!corruptFiles.empty() && corruptFiles != "0" && corruptFiles != "N/A") 
        {
            caution = true;
            if (smartNotes == "No telemetry notes available.") {
                smartNotes = "CRITICAL TRANSPOSITION ERROR: Read block contents fail verification check against initial write data. Storage blocks are losing structural retention state.";
            }
        }
        try {
            if (!sStab.empty() && sStab != "N/A") {
                std::string stabNum = sStab;
                stabNum.erase(std::remove(stabNum.begin(), stabNum.end(), '%'), stabNum.end());
                if (std::stoi(trim(stabNum)) < 60) {
                    caution = true;
                    if (smartNotes == "No telemetry notes available.") {
                        smartNotes = "PERFORMANCE WARNING: Low stability index observed. Severe multi-file throughput stuttering indicates controller read-retry adjustments.";
                    }
                }
            }
        } catch (...) {}
    }

    std::string status = caution ? "CAUTION" : "HEALTHY";
    std::string statusClass = caution ? "warn" : "ok";

    std::string html = REPORT_TEMPLATE;

    html = replaceAll(html, "{{TITLE_COLOR}}", titleColor);
    html = replaceAll(html, "{{SEPARATOR_COLOR}}", sepColor);
    html = replaceAll(html, "{{LABEL_COLOR}}", labelColor);
    html = replaceAll(html, "{{SUBHEADING_COLOR}}", subheadingColor);
    html = replaceAll(html, "{{VALUE_COLOR}}", valueColor);
    html = replaceAll(html, "{{STATUS_OK}}", okColor);
    html = replaceAll(html, "{{STATUS_WARN}}", warnColor);
    html = replaceAll(html, "{{STATUS_ERR}}", errColor);

    html = replaceAll(html, "{{DRIVE}}", drive);
    html = replaceAll(html, "{{MODEL}}", model);
    html = replaceAll(html, "{{SERIAL}}", serial);
    html = replaceAll(html, "{{TIME}}", getDisplayTime());
    html = replaceAll(html, "{{STATUS}}", status);
    html = replaceAll(html, "{{STATUS_CLASS}}", statusClass);

    html = replaceAll(html, "{{TECH}}", tech);
    html = replaceAll(html, "{{TECH_NOTE}}", techNote);
    html = replaceAll(html, "{{INTERFACE}}", interfaceD);
    html = replaceAll(html, "{{USB_TRANSPORT}}", usbTransport);
    html = replaceAll(html, "{{DRIVE_TYPE}}", driveType);
    html = replaceAll(html, "{{FORM_FACTOR}}", formFactor);
    html = replaceAll(html, "{{FS}}", fs);

    html = replaceAll(html, "{{BRIDGE_SAT}}", bridgeSat);
    html = replaceAll(html, "{{SMART_PASSTHROUGH}}", smartPass);
    html = replaceAll(html, "{{TRIM_SUPPORTED}}", trimSupported);

    html = replaceAll(html, "{{TOTAL}}", total);
    html = replaceAll(html, "{{USED}}", used);
    html = replaceAll(html, "{{FREE}}", freeGB);

    html = replaceAll(html, "{{CALIBRATED}}", calibrated);
    html = replaceAll(html, "{{READ_SPEED}}", readSpeed);
    html = replaceAll(html, "{{WRITE_SPEED}}", writeSpeed);
    html = replaceAll(html, "{{RANDOM_IOPS}}", randomIops);
    html = replaceAll(html, "{{LATENCY_MS}}", latencyMs);
    
    // File System Profile Mappings
    html = replaceAll(html, "{{FILE_COUNT}}", fileCount);
    html = replaceAll(html, "{{DIR_COUNT}}", dirCount);
    html = replaceAll(html, "{{TOTAL_BYTES}}", totalBytes);
    html = replaceAll(html, "{{AVG_FILE_SIZE}}", avgSize);
    html = replaceAll(html, "{{SMALL_FILE_RATIO}}", ratio);
    html = replaceAll(html, "{{MAX_DEPTH}}", maxDepth);

    html = replaceAll(html, "{{F_GRADE}}", fGrade);
    html = replaceAll(html, "{{FILES_CHECKED}}", filesChecked);
    html = replaceAll(html, "{{CORRUPT_FILES}}", corruptFiles);
    html = replaceAll(html, "{{LAST_SCAN_DATE}}", lastScanDate);

    html = replaceAll(html, "{{S_GRADE}}", sGrade);
    html = replaceAll(html, "{{S_STAB}}", sStab);
    html = replaceAll(html, "{{S_SPEED}}", sSpeed);
    html = replaceAll(html, "{{S_COV}}", sCov);
    html = replaceAll(html, "{{ERRORS}}", errors);
    html = replaceAll(html, "{{TIME_TAKEN}}", timeTaken);

    html = replaceAll(html, "{{TEMP}}", temp);
    html = replaceAll(html, "{{SMART}}", smart);
    html = replaceAll(html, "{{SMART_NOTES}}", smartNotes);
    html = replaceAll(html, "{{STRESS_PROFILE}}", stressProfile);
    html = replaceAll(html, "{{STRESS_ALLOWED}}", stressAllowed);

    std::string filename = "REPORT_" + getTimestampForFilename() + ".html";
    std::string outPath = reportDir + filename;

    if (!writeStringToFileUTF8(outPath, html))
    {
        std::cerr << "[generate_report] Failed to write report.\n";
        return 1;
    }

    std::cout << "[generate_report] Report created: " << outPath << "\n";
    launchBrowserWithConfig(configPath, outPath);

    return 0;
}