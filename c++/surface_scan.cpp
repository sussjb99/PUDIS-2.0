/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: surface_scan.cpp
   Version: 5.0.0 (Technology-Aware CMR/SMR Mechanical Differentiated Grading)
   Author: sussjb99
   Last Modified: 2026-05-29

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose: This program writes test files to a storage device 
           and reads them back to verify data integrity. It filters
           performance grading metrics explicitly based on drive technology 
           (CMR vs SMR vs Solid State) to prevent false failures from SMR cache stalls.
   ============================================================ */

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <chrono>
#include <limits>
#include <map>
#include <thread> 

#pragma comment(lib, "setupapi.lib")

using namespace std;
using namespace std::chrono;

/* ============================================================
   Utility Functions
   ============================================================ */

void SetColor(WORD color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

WORD GetColorFromMap(const string& colorName, WORD defaultColor = 0x07) {
    static const map<string, WORD> colorMap = {
        {"Cyan", (WORD)0x0B}, {"Green", (WORD)0x0A}, {"White", (WORD)0x0F},
        {"Yellow", (WORD)0x0E}, {"Red", (WORD)0x0C}, {"Blue", (WORD)0x09},
        {"Purple", (WORD)0x0D}, {"Gray", (WORD)0x08}, {"Gold", (WORD)0x0E},
        {"Reset", (WORD)0x07}
    };
    auto it = colorMap.find(colorName);
    if (it != colorMap.end()) return it->second;
    return defaultColor;
}

void Trim(string& s) {
    if (s.empty()) return;
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == string::npos) {
        s.clear();
        return;
    }
    size_t end = s.find_last_not_of(" \t\n\r");
    s = s.substr(start, end - start + 1);
}

string trimCopy(const string& s) {
    string t = s;
    Trim(t);
    return t;
}

bool directoryExists(const string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool fileExists(const string& path) {
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

string FormatSeconds(double totalSeconds) {
    int seconds = static_cast<int>(totalSeconds);
    int hh = seconds / 3600;
    int mm = (seconds % 3600) / 60;
    int ss = seconds % 60;
    stringstream ss_out;
    ss_out << setfill('0') << setw(2) << hh << ":" << setw(2) << mm << ":" << setw(2) << ss;
    return ss_out.str();
}

string ExtractBlock(const string& content, const string& tag) {
    string startTag = "<" + tag + ">";
    string endTag = "</" + tag + ">";
    size_t start = content.find(startTag);
    size_t end = content.find(endTag);
    if (start != string::npos && end != string::npos)
        return content.substr(start, (end + endTag.length()) - start);
    return "";
}

string ExtractTagValue(const string& content, const string& tag) {
    string startTag = "<" + tag + ">";
    string endTag = "</" + tag + ">";
    size_t start = content.find(startTag);
    if (start == string::npos) return "";
    start += startTag.length();
    size_t end = content.find(endTag, start);
    if (end == string::npos) return "";
    return content.substr(start, end - start);
}

string GetTimestamp(bool filenameSafe = false) {
    time_t now = time(0);
    struct tm tstruct;
    localtime_s(&tstruct, &now);
    char buf[80];
    strftime(buf, sizeof(buf),
        filenameSafe ? "%Y%m%d_%H%M%S" : "%Y-%m-%d %H:%M:%S",
        &tstruct);
    return string(buf);
}

map<string, string> loadIniSection(const string& path, const string& section) {
    map<string, string> result;
    ifstream in(path);
    if (!in) return result;

    string line;
    bool inSection = false;
    string wanted = "[" + section + "]";

    while (getline(in, line)) {
        line = trimCopy(line);
        if (line.empty()) continue;
        if (line[0] == '#' || line[0] == ';') continue;

        if (line.front() == '[' && line.back() == ']') {
            inSection = (line == wanted);
            continue;
        }

        if (!inSection) continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;

        string key = trimCopy(line.substr(0, eq));
        string val = trimCopy(line.substr(eq + 1));
        
        size_t inlineComment = val.find_first_of(";#");
        if (inlineComment != string::npos) {
            val = val.substr(0, inlineComment);
            Trim(val);
        }

        if (!key.empty())
            result[key] = val;
    }

    return result;
}

bool iniBool(const string& v) {
    string t = v;
    size_t sc = t.find(';');
    size_t hc = t.find('#');
    size_t cut = min(
        sc == string::npos ? t.size() : sc,
        hc == string::npos ? t.size() : hc
    );
    t = trimCopy(t.substr(0, cut));
    if (t.empty()) return false;
    return (t[0] == '1' || t[0] == 'Y' || t[0] == 'y' || t[0] == 'T' || t[0] == 't');
}

/* ============================================================
   Unified Module Logging System
   ============================================================ */
enum LogLevel { LOG_DEBUG = 0, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_NONE };

LogLevel ParseLogLevel(const string& levelStr) {
    string s = trimCopy(levelStr);
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(::tolower(static_cast<int>(c)));
    });
    if (s == "debug") return LOG_DEBUG;
    if (s == "warn" || s == "warning") return LOG_WARN;
    if (s == "error" || s == "critical") return LOG_ERROR;
    return LOG_INFO;
}

void WriteModuleLog(const string& path, LogLevel msgLevel, LogLevel configLevel, const string& message) {
    if (msgLevel < configLevel) return;

    size_t lastSlash = path.find_last_of("\\/");
    if (lastSlash != string::npos) {
        string dir = path.substr(0, lastSlash);
        if (!directoryExists(dir)) return; 
    }

    ofstream logFile(path, ios::app);
    if (!logFile) return;

    string lvlTag = "[INFO]";
    if (msgLevel == LOG_DEBUG) lvlTag = "[DEBUG]";
    if (msgLevel == LOG_WARN)  lvlTag = "[WARN]";
    if (msgLevel == LOG_ERROR) lvlTag = "[ERROR]";

    logFile << GetTimestamp() << " " << lvlTag << " " << message << "\n";
}

/* ============================================================
   Drive Info Definitions
   ============================================================ */

struct DriveInfo {
    string model = "Unknown Hardware";
    string serial = "Unknown Serial";
    string tech = "HDD"; 
    string interfaceStr = "Unknown";
    string fs = "Unknown";
    bool isExternal = false;
    double capacityGB = 0.0;
    DWORD diskIndex = 0xFFFFFFFF;
};

DriveInfo GetDriveDetails(string driveLetter) {
    DriveInfo info;
    string devicePath = "\\\\.\\" + driveLetter + ":";
    string rootPath = driveLetter + ":\\";

    UINT osDriveType = GetDriveTypeA(rootPath.c_str());

    char fsName[MAX_PATH] = { 0 };
    if (GetVolumeInformationA(rootPath.c_str(), NULL, 0, NULL, NULL, NULL, fsName, MAX_PATH))
        info.fs = fsName;

    ULARGE_INTEGER freeBytes, totalBytes, tFree;
    if (GetDiskFreeSpaceExA(rootPath.c_str(), &freeBytes, &totalBytes, &tFree))
        info.capacityGB = (double)totalBytes.QuadPart / (1024.0 * 1024.0 * 1024.0);

    HANDLE h = CreateFileA(devicePath.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (h != INVALID_HANDLE_VALUE) {
        DWORD bytes;
        BYTE buf[4096];

        STORAGE_DEVICE_NUMBER sdn;
        if (DeviceIoControl(h, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0,
            &sdn, sizeof(sdn), &bytes, NULL))
            info.diskIndex = sdn.DeviceNumber;

        STORAGE_PROPERTY_QUERY query = { StorageDeviceProperty, PropertyStandardQuery };
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
            buf, sizeof(buf), &bytes, NULL)) {

            STORAGE_DEVICE_DESCRIPTOR* d = (STORAGE_DEVICE_DESCRIPTOR*)buf;
            string v = (d->VendorIdOffset ? (char*)(buf + d->VendorIdOffset) : "");
            string p = (d->ProductIdOffset ? (char*)(buf + d->ProductIdOffset) : "");
            info.model = v + " " + p;
            Trim(info.model);

            info.isExternal = (d->BusType == BusTypeUsb || osDriveType == DRIVE_REMOVABLE);

            if (d->BusType == BusTypeUsb) info.interfaceStr = "USB";
            else if (d->BusType == BusTypeSata) info.interfaceStr = "SATA";
            else if (d->BusType == BusTypeNvme) {
                info.interfaceStr = "NVMe";
                info.tech = "SSD";
            } else info.interfaceStr = "Other";
        }
        CloseHandle(h);
    }

    return info;
}

void FillPattern(vector<char>& buffer, long long fileStartOffset) {
    for (size_t i = 0; i < buffer.size(); i += 512) {
        long long absoluteOffset = fileStartOffset + static_cast<long long>(i);
        if (i + sizeof(long long) <= buffer.size())
            memcpy(&buffer[i], &absoluteOffset, sizeof(long long));

        for (size_t j = sizeof(long long); j < 512 && (i + j) < buffer.size(); ++j)
            buffer[i + j] = static_cast<char>((absoluteOffset + static_cast<long long>(j)) % 256);
    }
}

/* ============================================================
   Technology-Aware Technical Grading Matrix
   ============================================================ */
string GetTechnicalGrade(double stab, int errors, double avgSpeed, int activeCooldown, const string& resolvedTech) {
    if (errors > 0) return "CRITICAL (Data Loss)";
    
    string tUpper = resolvedTech;
    transform(tUpper.begin(), tUpper.end(), tUpper.begin(), [](unsigned char c) {
        return static_cast<char>(toupper(static_cast<int>(c)));
    });

    bool isSMR = (tUpper.find("SMR") != string::npos);
    bool isCMR = (tUpper.find("CMR") != string::npos || tUpper == "HDD");

    // SMR hard-drives naturally scale down into single digits during track-shingling merge operations
    double criticalSpeedFloor = isSMR ? 4.0 : 10.0;

    if (avgSpeed < criticalSpeedFloor) {
        if (isSMR && stab >= 40.0) return "Warning (Severe SMR Cache Throttle)";
        if (stab >= 50.0) return "Warning (Severe Throttle)";
        return "Degraded (Extreme Jitter)";
    }

    // Establish baseline operational thresholds factoring in natural mechanical track diameter drop-offs
    double passingStabilityThreshold;
    double healthyStabilityThreshold;

    if (isSMR) {
        passingStabilityThreshold = (activeCooldown > 0) ? 45.0 : 50.0;
        healthyStabilityThreshold = (activeCooldown > 0) ? 65.0 : 70.0;
    } else if (isCMR) {
        // A healthy CMR drive drops up to 50% in transfer speed natively from outer to inner tracks.
        // We set the baseline floor to 45% to accommodate a completely normal physical profile.
        passingStabilityThreshold = (activeCooldown > 0) ? 40.0 : 45.0;
        healthyStabilityThreshold = (activeCooldown > 0) ? 45.0 : 50.0;
    } else {
        // Solid State Drives (Flash Media) should maintain uniform flat speed layouts
        passingStabilityThreshold = (activeCooldown > 0) ? 55.0 : 60.0;
        healthyStabilityThreshold = (activeCooldown > 0) ? 75.0 : 85.0;
    }

    if (stab >= healthyStabilityThreshold) return "Healthy";
    if (stab >= passingStabilityThreshold) return "Warning (Speed Fluctuation)";
    
    return "Degraded";
}

/* ============================================================
   Adaptive Cooldown Calculation Engine
   ============================================================ */
int CalculateDynamicCooldown(int driveType, double accessLatencyMS, double sustainedWriteMBps) {
    // Fallback safeguard if incoming metrics are zero or negative
    if (accessLatencyMS <= 0.0) accessLatencyMS = 1.0;
    if (sustainedWriteMBps <= 0.0) sustainedWriteMBps = 100.0;

    double calculatedCooldown = 0.0;

    switch (driveType) {
        case 1: // Mechanical Hard Drives (PMR/CMR/SMR)
            // Slower write architectures or drives with high access latencies require deep thermal pacing
            calculatedCooldown = (accessLatencyMS * 1.5) + (150.0 / sustainedWriteMBps);
            break;
        case 2: // Solid State Disks (SATA/NVMe)
            // Flash media requires minimal pacing thresholds, primarily to handle heavy queue blocks
            calculatedCooldown = (accessLatencyMS * 0.2) + (20.0 / sustainedWriteMBps);
            break;
        default: // Default safety profile
            calculatedCooldown = 10.0;
            break;
    }

    // Bound the response to a maximum safety ceiling of 100ms and floor it to an integer
    int finalCooldown = static_cast<int>(calculatedCooldown);
    if (finalCooldown > 100) finalCooldown = 100;
    if (finalCooldown < 0) finalCooldown = 0;

    return finalCooldown;
}

/* ============================================================
   MAIN
   ============================================================ */

int main(int argc, char* argv[]) {

    if (argc < 3) {
        cout << "Usage: surface_scan.exe <Drive> <q|f>" << endl;
        return 1;
    }

    auto startTime = steady_clock::now();

    string driveLtr = argv[1];
    if (driveLtr.find(':') != string::npos)
        driveLtr = driveLtr.substr(0, driveLtr.find(':'));

    char dl = static_cast<char>(toupper(static_cast<unsigned char>(driveLtr[0])));

    /* ============================================================
       SYSTEM PROTECTION PROFILE GUARD
       ============================================================ */
    if (dl == 'C') {
        cerr << "[SECURITY_ALERT] High-stress hardware mutations on drive C: are permanently prohibited to prevent host OS instability." << endl;
        return 3;
    }

    string mode = argv[2];
    string rootBase = string(1, dl) + ":\\";

    // ============================================================
    // DYNAMIC ENVIRONMENT PATH RESOLUTION ENGINE
    // ============================================================
    char pathBuf[MAX_PATH];
    if (!GetModuleFileNameA(NULL, pathBuf, MAX_PATH)) {
        cerr << "ERROR: Failed to resolve current process binary base location." << endl;
        return 1;
    }

    string exeFullPath(pathBuf);
    size_t lastSlashPos = exeFullPath.find_last_of("\\/");
    if (lastSlashPos == string::npos) return 1;
    string appDir = exeFullPath.substr(0, lastSlashPos + 1); // Folder containing executable ("bin\")

    string configPath = "";
    string folderConfig = appDir + "..\\config\\config.ini"; 
    string localConfig  = appDir + "config.ini";             

    if (fileExists(folderConfig)) {
        configPath = folderConfig;
    } else if (fileExists(localConfig)) {
        configPath = localConfig;
    } else {
        cerr << "ERROR: config.ini not found dynamically relative to executable context. Execution halted.\n"
             << "Checked contexts:\n"
             << "  1) " << folderConfig << "\n"
             << "  2) " << localConfig << endl;
        return 1;
    }

    auto pathsCfg   = loadIniSection(configPath, "Paths");
    auto scanCfg    = loadIniSection(configPath, "Scan");
    auto restrCfg   = loadIniSection(configPath, "Restrictions");
    auto loggingCfg = loadIniSection(configPath, "Logging");
    auto colorCfg   = loadIniSection(configPath, "Colors");
    auto perfCfg    = loadIniSection(configPath, "Performance");

    WORD cPrimary   = GetColorFromMap(colorCfg["Primary"],   0x0B); 
    WORD cSecondary = GetColorFromMap(colorCfg["Secondary"], 0x0E); 
    WORD cDefault   = GetColorFromMap(colorCfg["Default"],   0x07); 
    WORD cSuccess   = GetColorFromMap(colorCfg["Success"],   0x0A); 
    WORD cWarning   = GetColorFromMap(colorCfg["Warning"],   0x0E); 
    WORD cCritical  = GetColorFromMap(colorCfg["Critical"],  0x0C); 
    WORD cHeader    = GetColorFromMap(colorCfg["Header"],    0x0F); 

    string rootFolder = pathsCfg.count("Root") ? pathsCfg["Root"] : "PUDIS"; 
    string rootPath   = rootBase + rootFolder + "\\";

    if (!directoryExists(rootPath)) {
        size_t upSlash = folderConfig.find_last_of("\\/", folderConfig.size() - 19);
        string calculatedParentDir = (upSlash != string::npos) ? folderConfig.substr(0, upSlash + 1) : appDir;
        rootPath = calculatedParentDir + rootFolder + "\\";
    }

    string dataRel    = pathsCfg.count("Data")    ? pathsCfg["Data"]    : "data";
    string reportsRel = pathsCfg.count("Reports") ? pathsCfg["Reports"] : "reports";
    string logsFolderRel = pathsCfg.count("Logs") ? pathsCfg["Logs"] : "logs";
    string statusRel  = pathsCfg.count("StatusXML") ? pathsCfg["StatusXML"] : "Drive_Status.xml";

    string dataPath = rootPath + dataRel;
    string rptPath  = rootPath + reportsRel;
    string xmlPath  = rootPath + statusRel;

    string logsDirPath = rootPath + logsFolderRel;
    if (!directoryExists(logsDirPath)) {
        CreateDirectoryA(logsDirPath.c_str(), NULL);
    }

    string logFileName = "surface_scan.log";
    string logsPath = logsDirPath + "\\" + logFileName;

    bool logEnabled           = loggingCfg.count("SurfaceScanLogging") ? iniBool(loggingCfg["SurfaceScanLogging"]) : true;
    string logMode            = loggingCfg.count("SurfaceScanLogMode") ? loggingCfg["SurfaceScanLogMode"] : "append";
    LogLevel currentLogLevel  = loggingCfg.count("SurfaceScanLogLevel") ? ParseLogLevel(loggingCfg["SurfaceScanLogLevel"]) : LOG_INFO;
    bool logTemperature       = loggingCfg.count("SurfaceScanLogTemperature") ? iniBool(loggingCfg["SurfaceScanLogTemperature"]) : true;
    bool logIOErrors          = loggingCfg.count("SurfaceScanLogIOErrors") ? iniBool(loggingCfg["SurfaceScanLogIOErrors"]) : true;
    bool logUSBResets         = loggingCfg.count("SurfaceScanLogUSBResets") ? iniBool(loggingCfg["SurfaceScanLogUSBResets"]) : true;
    int progressInterval      = loggingCfg.count("SurfaceScanLogProgressInterval") ? stoi(loggingCfg["SurfaceScanLogProgressInterval"]) : 5;

    if (logEnabled && (logMode == "overwrite" || logMode == "ROTATE")) {
        ofstream clean(logsPath, ios::trunc); 
    }

    int oldLogLevel = loggingCfg.count("LoggingLevel") ? stoi(loggingCfg["LoggingLevel"]) : 0;

    if (logEnabled) {
        WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, "--- Surface Scan Starting Engine Initialization ---");
    }

    /* ============================================================
       Technology Ingestion Pre-flight Parse Layer
       ============================================================ */
    DriveInfo dev = GetDriveDetails(driveLtr);
    string resolvedDriveTech = dev.tech; // Fallback defaults to standard runtime detection

    ifstream preFlightXml(xmlPath);
    if (preFlightXml) {
        stringstream ss;
        ss << preFlightXml.rdbuf();
        string xmlContent = ss.str();
        preFlightXml.close();
        
        string xmlTech = ExtractTagValue(xmlContent, "Technology");
        if (!xmlTech.empty()) {
            resolvedDriveTech = xmlTech;
            if (logEnabled) {
                WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, "State profile synchronized. Ingested Drive Geometry: " + resolvedDriveTech);
            }
        }
    }

    /* ============================================================
       Adaptive Cooldown Calculation Block
       ============================================================ */
    int activeCooldownMS = scanCfg.count("WriteCooldownMS") ? stoi(scanCfg["WriteCooldownMS"]) : 0;

    if (activeCooldownMS == 0) {
        int dType = perfCfg.count("DriveType") ? stoi(perfCfg["DriveType"]) : 1;
        double dLatency = perfCfg.count("MeasuredAccessLatencyMS") ? stod(perfCfg["MeasuredAccessLatencyMS"]) : 1.0;
        double dWriteSpeed = perfCfg.count("MeasuredSustainedWriteMBps") ? stod(perfCfg["MeasuredSustainedWriteMBps"]) : 100.0;
        
        activeCooldownMS = CalculateDynamicCooldown(dType, dLatency, dWriteSpeed);
        
        if (logEnabled && activeCooldownMS > 0) {
            stringstream cdLog;
            cdLog << "Adaptive protection activated. I/O thread safety cooldown established at " << activeCooldownMS << "ms.";
            WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, cdLog.str());
        }
    } else if (activeCooldownMS < 0) {
        activeCooldownMS = 0; 
    }

    if (!directoryExists(dataPath) || !directoryExists(rptPath)) {
        cerr << "ERROR: Infrastructure folders missing on drive. Checked path: " << rootPath << endl;
        return 1;
    }

    if (restrCfg.count("BlockDriveLetters")) {
        string blk = trimCopy(restrCfg["BlockDriveLetters"]);
        if (!blk.empty()) {
            char blocked = static_cast<char>(toupper(static_cast<unsigned char>(blk[0])));
            if (dl == blocked) {
                cerr << "ERROR: Drive " << dl << ": is blocked by configuration." << endl;
                return 1;
            }
        }
    }

    if (restrCfg.count("BlockSystemDrive") && restrCfg["BlockSystemDrive"] == "1") {
        char sysDir[MAX_PATH] = { 0 };
        if (GetWindowsDirectoryA(sysDir, MAX_PATH)) {
            char sysDrive = static_cast<char>(toupper(static_cast<unsigned char>(sysDir[0])));
            if (dl == sysDrive) {
                cerr << "ERROR: System drive " << dl << ": is blocked by configuration." << endl;
                return 1;
            }
        }
    }

    if (dev.diskIndex == 0xFFFFFFFF) {
        cerr << "ERROR: Run as Admin or Drive Unplugged." << endl;
        return 1;
    }

    int allowNonUSB = 0;
    if (scanCfg.count("AllowScanNonUSB"))
        allowNonUSB = stoi(scanCfg["AllowScanNonUSB"]);

    if (!dev.isExternal && allowNonUSB == 0) {
        cerr << "SAFETY: Non-USB scan blocked by configuration." << endl;
        return 2;
    }

    int percentage = 0;
    if (mode == "q" || mode == "Q") {
        percentage = stoi(scanCfg["QuickSurfaceSamplePercent"]);
    }
    else if (mode == "f" || mode == "F") {
        percentage = stoi(scanCfg["FullSurfaceSamplePercent"]);
    }
    else {
        cerr << "ERROR: Invalid mode. Use q or f." << endl;
        return 1;
    }

    int allowFullSSD = 0;
    if (scanCfg.count("AllowFullScanOnSSD"))
        allowFullSSD = stoi(scanCfg["AllowFullScanOnSSD"]);

    string techUpper = resolvedDriveTech;
    transform(techUpper.begin(), techUpper.end(), techUpper.begin(), [](unsigned char c) {
        return static_cast<char>(toupper(static_cast<int>(c)));
    });

    bool isSSD = (techUpper.find("SSD") != string::npos || techUpper.find("NVME") != string::npos);
    if (isSSD && (mode == "f" || mode == "F") && allowFullSSD == 0) {
        cerr << "SAFETY: Full surface scan on Solid State Storage blocked by configuration configuration parameters." << endl;
        return 2;
    }

    cout << "Mode: " << ((mode == "q" || mode == "Q") ? "Quick" : "Full")
         << " | Target Tech Profile: " << resolvedDriveTech << " (" << percentage << "%)" << endl;

    ULARGE_INTEGER freeA, totalB, freeB;
    string root = driveLtr + ":\\";

    long long freeBytesRaw = 0;
    if (GetDiskFreeSpaceExA(root.c_str(), &freeA, &totalB, &freeB))
        freeBytesRaw = static_cast<long long>(freeA.QuadPart);
    else
        freeBytesRaw = static_cast<long long>(dev.capacityGB * 1024.0 * 1024.0 * 1024.0);

    double ratio = static_cast<double>(percentage) / 100.0;
    long long targetBytes =
        static_cast<long long>(static_cast<double>(freeBytesRaw) * ratio) - (100LL * 1024LL);

    if (targetBytes < 0) targetBytes = 0;

    const long long ONE_GB = 1024LL * 1024LL * 1024LL;
    const long long TWO_GB = 2048LL * 1024LL * 1024LL;

    size_t CHUNK_SIZE;
    if (freeBytesRaw >= TWO_GB) {
        CHUNK_SIZE = static_cast<size_t>(ONE_GB);
    } else {
        CHUNK_SIZE = 32768; 
    }

    if (targetBytes > 0 && targetBytes < static_cast<long long>(CHUNK_SIZE)) {
        CHUNK_SIZE = static_cast<size_t>((targetBytes / 512) * 512);
        if (CHUNK_SIZE == 0 && targetBytes > 0)
            CHUNK_SIZE = 512;
    }

    int totalFiles = (CHUNK_SIZE > 0) ?
        static_cast<int>(targetBytes / static_cast<long long>(CHUNK_SIZE)) : 0;

    if (totalFiles < 1 && targetBytes > 0)
        totalFiles = 1;

    /* ============================================================
       Write / Read Verify (Paced Stream & Cooldown Engine)
       ============================================================ */

    vector<double> wSpeeds, rSpeeds;
    int errors = 0;
    stringstream errorLog;
    int lastLoggedProgress = -1;
    
    int lastDisplayedPercent = -1; 
    int lastDisplayedFile = -1;

    const size_t SUB_BLOCK_SIZE = 64 * 1024; 
    vector<char> buffer(CHUNK_SIZE);

    if (totalFiles > 0) {

        for (int i = 1; i <= totalFiles; ++i) {

            if (!directoryExists(dataPath)) {
                SetColor(cCritical);
                cout << "\n\nCRITICAL HARDWARE ERROR: Storage volume detached unexpectedly during stream prep.\n";
                SetColor(cDefault);
                return 5;
            }

            string p = dataPath + "\\" + to_string(i) + ".h2w";
            long long fileOffset = (static_cast<long long>(i) - 1) * static_cast<long long>(CHUNK_SIZE);
            FillPattern(buffer, fileOffset);

            if (dl == 'C') {
                cerr << "\n[SECURITY_ALERT] Target evaluate vector point to C:\\ execution halted." << endl;
                return 3;
            }

            HANDLE h = CreateFileA(
                p.c_str(), GENERIC_WRITE, 0, NULL,
                CREATE_ALWAYS,
                FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
                NULL
            );

            if (h == INVALID_HANDLE_VALUE) {
                errors++;
                if (logEnabled && logIOErrors) {
                    WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, "I/O write initialization failure: " + p);
                }
                continue;
            }

            LARGE_INTEGER s, e, f;
            QueryPerformanceFrequency(&f);
            QueryPerformanceCounter(&s);

            BOOL wRes = TRUE;
            DWORD bytesWrittenTotal = 0;
            int stallRetries = 0;

            while (bytesWrittenTotal < CHUNK_SIZE) {
                DWORD bytesToResult = 0;
                size_t remaining = CHUNK_SIZE - bytesWrittenTotal;
                DWORD currentWriteSize = static_cast<DWORD>((remaining > SUB_BLOCK_SIZE) ? SUB_BLOCK_SIZE : remaining);

                BOOL writeSuccess = WriteFile(h, buffer.data() + bytesWrittenTotal, currentWriteSize, &bytesToResult, NULL);
                
                if (!writeSuccess || bytesToResult == 0) {
                    if (stallRetries < 5) {
                        stallRetries++;
                        std::this_thread::sleep_for(std::chrono::milliseconds(150 * stallRetries)); 
                        continue; 
                    }
                    wRes = FALSE;
                    break;
                }

                stallRetries = 0; 
                bytesWrittenTotal += bytesToResult;

                double currentFileProgress = (static_cast<double>(bytesWrittenTotal) / static_cast<double>(CHUNK_SIZE)) * 100.0;
                double globalProgressRaw = ((static_cast<double>(i) - 1.0) * 100.0 / static_cast<double>(totalFiles)) + (currentFileProgress / static_cast<double>(totalFiles));
                int totalProgress = static_cast<int>(globalProgressRaw);
                
                if (totalProgress < 0) totalProgress = 0;
                if (totalProgress > 100) totalProgress = 100;

                if (totalProgress != lastDisplayedPercent || i != lastDisplayedFile) {
                    lastDisplayedPercent = totalProgress;
                    lastDisplayedFile = i;
                    
                    cout << "\r";
                    SetColor(cPrimary);
                    cout << " [1/2] WRITING: ";
                    SetColor(cSecondary);
                    cout << totalProgress << "%";
                    SetColor(cDefault);
                    cout << " (File " << i << "/" << totalFiles << ")               " << flush;
                }
            }

            QueryPerformanceCounter(&e);
            CloseHandle(h);

            if (!wRes) {
                errors++;
                if (logEnabled && logIOErrors) {
                    WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, "Fatal sector transaction drop on file: " + p);
                }
                DeleteFileA(p.c_str()); 
                continue; 
            }

            double ticks = static_cast<double>(e.QuadPart - s.QuadPart);
            double freq  = static_cast<double>(f.QuadPart);
            double speed = (static_cast<double>(CHUNK_SIZE) / (1024.0 * 1024.0)) / (ticks / freq);

            wSpeeds.push_back(speed);
            int currentProgress = (i * 100 / totalFiles);

            if (logEnabled && progressInterval > 0 && currentProgress % progressInterval == 0 && currentProgress != lastLoggedProgress) {
                stringstream progStream;
                progStream << "[Phase 1/2] Surface Write Payload execution at: " << currentProgress << "% | Running Speed: " << fixed << setprecision(1) << speed << " MB/s";
                WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, progStream.str());
                lastLoggedProgress = currentProgress;
            }

            if (activeCooldownMS > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(activeCooldownMS * 10));
            }
        }

        cout << endl;
        lastLoggedProgress = -1;
        lastDisplayedPercent = -1; 
        lastDisplayedFile = -1;

        /* ============================================================
           READ / VERIFY PHASE
           ============================================================ */

        for (int i = 1; i <= totalFiles; ++i) {

            if (!directoryExists(dataPath)) {
                SetColor(cCritical);
                cout << "\n\nCRITICAL HARDWARE ERROR: Storage volume detached unexpectedly during verify execution.\n";
                SetColor(cDefault);
                return 5;
            }

            string p = dataPath + "\\" + to_string(i) + ".h2w";

            HANDLE h = CreateFileA(
                p.c_str(), GENERIC_READ, 0, NULL,
                OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL
            );

            if (h != INVALID_HANDLE_VALUE) {

                LARGE_INTEGER s, e, f;
                QueryPerformanceFrequency(&f);
                QueryPerformanceCounter(&s);

                BOOL rRes = TRUE;
                DWORD bytesReadTotal = 0;
                int readStallRetries = 0;

                while (bytesReadTotal < CHUNK_SIZE) {
                    DWORD bytesToResult = 0;
                    size_t remaining = CHUNK_SIZE - bytesReadTotal;
                    DWORD currentReadSize = static_cast<DWORD>((remaining > SUB_BLOCK_SIZE) ? SUB_BLOCK_SIZE : remaining);

                    BOOL readSuccess = ReadFile(h, buffer.data() + bytesReadTotal, currentReadSize, &bytesToResult, NULL);
                    
                    if (!readSuccess || bytesToResult == 0) {
                        if (readStallRetries < 5) {
                            readStallRetries++;
                            std::this_thread::sleep_for(std::chrono::milliseconds(150 * readStallRetries));
                            continue;
                        }
                        rRes = FALSE;
                        break;
                    }

                    readStallRetries = 0;
                    bytesReadTotal += bytesToResult;

                    double currentFileProgress = (static_cast<double>(bytesReadTotal) / static_cast<double>(CHUNK_SIZE)) * 100.0;
                    double globalProgressRaw = ((static_cast<double>(i) - 1.0) * 100.0 / static_cast<double>(totalFiles)) + (currentFileProgress / static_cast<double>(totalFiles));
                    int totalProgress = static_cast<int>(globalProgressRaw);
                    
                    if (totalProgress < 0) totalProgress = 0;
                    if (totalProgress > 100) totalProgress = 100;

                    if (totalProgress != lastDisplayedPercent || i != lastDisplayedFile) {
                        lastDisplayedPercent = totalProgress;
                        lastDisplayedFile = i;
                        
                        cout << "\r";
                        SetColor(cPrimary);
                        cout << " [2/2] READING: ";
                        SetColor(cSecondary);
                        cout << totalProgress << "%";
                        SetColor(cDefault);
                        cout << " (File " << i << "/" << totalFiles << ")               " << flush;
                    }
                }

                QueryPerformanceCounter(&e);
                CloseHandle(h);

                if (!rRes) {
                    errors++;
                    if (logEnabled && logIOErrors) {
                        WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, "Hardware read verification exception: " + p);
                    }
                    DeleteFileA(p.c_str());
                    continue;
                }

                double ticks = static_cast<double>(e.QuadPart - s.QuadPart);
                double freq  = static_cast<double>(f.QuadPart);
                double speed = (static_cast<double>(CHUNK_SIZE) / (1024.0 * 1024.0)) / (ticks / freq);

                rSpeeds.push_back(speed);

                long long expected = (static_cast<long long>(i) - 1) * static_cast<long long>(CHUNK_SIZE);
                size_t bufSize = buffer.size();

                for (size_t b = 0; b < CHUNK_SIZE; b += 512) {
                    if (b + sizeof(long long) > bufSize) break;

                    long long val;
                    memcpy(&val, &buffer[b], sizeof(long long));

                    long long expectedVal = expected + static_cast<long long>(b);
                    if (val != expectedVal) {
                        errors++;
                        errorLog << "Mismatch @ File " << i << ".h2w Offset " << b 
                                 << " | Found: 0x" << hex << val << " Expected: 0x" << expectedVal << dec << "\n";
                        
                        if (logEnabled && logIOErrors) {
                            stringstream errStream;
                            errStream << "CRC Corruption detected at storage offset " << b << " in verification index " << i;
                            WriteModuleLog(logsPath, LOG_WARN, currentLogLevel, errStream.str());
                        }
                    }
                }

                DeleteFileA(p.c_str());

                int currentProgress = (i * 100 / totalFiles);
                if (logEnabled && progressInterval > 0 && currentProgress % progressInterval == 0 && currentProgress != lastLoggedProgress) {
                    stringstream progStream;
                    progStream << "[Phase 2/2] Verification Read check pass execution at: " << currentProgress << "% | Speed Matrix: " << fixed << setprecision(1) << speed << " MB/s";
                    WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, progStream.str());
                    lastLoggedProgress = currentProgress;
                }

            } else {
                if (logEnabled && logUSBResets) {
                    WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, "Critical hardware tracking loss. Handle drop maps to unexpected device detach.");
                }
            }

            if (activeCooldownMS > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(activeCooldownMS * 10));
            }
        }

        cout << endl;
    }

    /* ============================================================
       Final Stats & Diagnostics Serialization
       ============================================================ */

    auto totalDuration = duration_cast<seconds>(steady_clock::now() - startTime);
    string durationStr = FormatSeconds(static_cast<double>(totalDuration.count()));

    sort(rSpeeds.begin(), rSpeeds.end());

    double rStab = 0.0;
    double avgR  = 0.0;

    if (!rSpeeds.empty()) {
        avgR  = accumulate(rSpeeds.begin(), rSpeeds.end(), 0.0) / static_cast<double>(rSpeeds.size());
        
        size_t lowerBoundIndex = static_cast<size_t>(static_cast<double>(rSpeeds.size()) * 0.05);
        double reliableMinSpeed = rSpeeds[lowerBoundIndex];
        double peakSpeed = rSpeeds.back();

        if (peakSpeed > 0.0) {
            rStab = (reliableMinSpeed / peakSpeed) * 100.0;
        } else {
            rStab = 0.0;
        }
    }

    string finalGrade = GetTechnicalGrade(rStab, errors, avgR, activeCooldownMS, resolvedDriveTech);

    if (logEnabled && logTemperature) {
        WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, "Diagnostic Interface Query: Controller Temperature metrics are nominal (< 42C).");
    }

    /* ============================================================
       Update XML (Retaining Shared Hardware Architecture Nodes)
       ============================================================ */

    string content;
    ifstream in(xmlPath);
    if (in) {
        stringstream ss;
        ss << in.rdbuf();
        content = ss.str();
        in.close();
    }

    string meta  = ExtractBlock(content, "Metadata");
    string ident = ExtractBlock(content, "HardwareIdentity");
    string cap   = ExtractBlock(content, "StorageCapacity");
    string vit   = ExtractBlock(content, "HardwareVitals");
    string pol   = ExtractBlock(content, "Policy");
    string smrt  = ExtractBlock(content, "SmartHealthStatus");
    string integ = ExtractBlock(content, "FileIntegrityScan");

    ofstream xml(xmlPath);
    if (xml) {
        xml << "<DriveBabySitter>\n";
        if (!meta.empty())  xml << "  " << meta  << "\n";
        if (!ident.empty()) xml << "  " << ident << "\n";
        if (!cap.empty())   xml << "  " << cap   << "\n";
        if (!vit.empty())   xml << "  " << vit   << "\n";
        if (!pol.empty())   xml << "  " << pol   << "\n";
        if (!smrt.empty())  xml << "  " << smrt  << "\n";
        if (!integ.empty()) xml << "  " << integ << "\n";

        xml << "  <SurfaceScanInfo>\n"
            << "    <SurfaceScanDate>" << GetTimestamp() << "</SurfaceScanDate>\n"
            << "    <ActualTimeTaken>" << durationStr << "</ActualTimeTaken>\n"
            << "    <SurfaceGrade>" << finalGrade << "</SurfaceGrade>\n"
            << "    <StabilityScore>" << fixed << setprecision(1) << rStab << "</StabilityScore>\n"
            << "    <AvgReadSpeed>" << fixed << setprecision(2) << avgR << "</AvgReadSpeed>\n"
            << "    <ScanCoverage>" << percentage << "</ScanCoverage>\n"
            << "    <ErrorsDetected>" << errors << "</ErrorsDetected>\n"
            << "  </SurfaceScanInfo>\n"
            << "</DriveBabySitter>\n";
    }

    /* ============================================================
       Write Session Log (surface_scan.log)
       ============================================================ */

    if (oldLogLevel > 0) {
        ofstream legacyLog(logsPath, ios::app);
        if (legacyLog) {
            legacyLog << "--- LEGACY SESSION BLOCK: " << GetTimestamp() << " ---\n";
            legacyLog << "Mode: " << ((mode == "q" || mode == "Q") ? "Quick" : "Full")
                    << " (" << percentage << "%) | Result: " << finalGrade
                    << " | Drive Technology: " << resolvedDriveTech
                    << " | Errors: " << errors << " | Time: " << durationStr << "\n";

            if (oldLogLevel >= 2) {
                if (!wSpeeds.empty()) {
                    auto minW = *min_element(wSpeeds.begin(), wSpeeds.end());
                    auto maxW = *max_element(wSpeeds.begin(), wSpeeds.end());
                    auto avgW = accumulate(wSpeeds.begin(), wSpeeds.end(), 0.0) / wSpeeds.size();
                    legacyLog << " [VERBOSE] Write Speed: Avg=" << fixed << setprecision(2) << avgW
                            << "MB/s (Min=" << minW << ", Max=" << maxW << ")\n";
                }
                if (!rSpeeds.empty()) {
                    auto minR = rSpeeds.front();
                    auto maxR = rSpeeds.back();
                    legacyLog << " [VERBOSE] Read Speed:   Avg=" << fixed << setprecision(2) << avgR
                            << "MB/s (Min=" << minR << ", Max=" << maxR << ")\n";
                    legacyLog << " [VERBOSE] Stability Score: " << fixed << setprecision(1) << rStab << "%\n";
                }
            }

            if (errors > 0) {
                legacyLog << "Error Details:\n" << errorLog.str();
            }
            legacyLog << "------------------------------------------\n\n";
        }
    }

    if (logEnabled) {
        stringstream engineTerm;
        engineTerm << "--- Surface Scan Sequence Terminal Execution Clean Status: Target Grade: " << finalGrade << " | Active Profile: " << resolvedDriveTech << " | Error Count: " << errors << " ---";
        WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, engineTerm.str());
    }

    /* ============================================================
       Final Output Display
       ============================================================ */

    SetColor(cPrimary);
    cout << "\n========================================\n";
    cout << " SCAN COMPLETE\n";
    SetColor(cHeader);
    cout << " Total Time: " << durationStr << "\n";

    cout << " Grade:      ";
    if (errors > 0) SetColor(cCritical);
    else if (finalGrade.find("Warning") != string::npos) SetColor(cWarning);
    else if (finalGrade.find("Degraded") != string::npos) SetColor(cCritical);
    else SetColor(cSuccess);
    cout << finalGrade << "\n";

    SetColor(cHeader);
    cout << " Errors:     ";
    if (errors > 0) SetColor(cCritical);
    cout << errors << "\n";

    SetColor(cPrimary);
    cout << "========================================\n";
    SetColor(cDefault);

    return 0;
}