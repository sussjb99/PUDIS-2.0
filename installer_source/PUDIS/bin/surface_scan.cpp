/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: surface_scan.cpp
   Version: 5.1.2 (Suite Integrated Native Toolchain Telemetry Sync)
   Author: sussjb99
   Last Modified: 2026-06-02

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
   Real-Time Native Telemetry via Dedicated Probe
   ============================================================ */
int GetDriveTemperatureCelsius(DWORD diskIndex, string driveLetter, string dataPath) {
    if (diskIndex == 0xFFFFFFFF) return -1;

    char pathBuf[MAX_PATH];
    if (!GetModuleFileNameA(NULL, pathBuf, MAX_PATH)) return -1;
    
    string exeFullPath(pathBuf);
    size_t lastSlashPos = exeFullPath.find_last_of("\\/");
    if (lastSlashPos == string::npos) return -1;
    string appDir = exeFullPath.substr(0, lastSlashPos + 1);

    string deviceInfoExe = appDir + "deviceinfo.exe";
    
    // Construct path to dedicated probe.xml using dataPath context
    string probeXmlPath = dataPath + "\\probe.xml";

    if (!fileExists(deviceInfoExe)) {
        deviceInfoExe = appDir + "deviceinfo.exe";
        if (!fileExists(deviceInfoExe)) return -1; 
    }

    // Call deviceinfo.exe with an explicit drive letter and target output xml file parameter
    string cmd = "\"" + deviceInfoExe + "\" " + driveLetter + " \"" + probeXmlPath + "\"";
    
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE; 
    ZeroMemory(&pi, sizeof(pi));

    if (CreateProcessA(NULL, const_cast<char*>(cmd.c_str()), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 3000); // 3 sec execution guard time-out
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        return -1; 
    }

    // Parse fresh real-time metrics out of refreshed probe XML structure
    ifstream xmlFile(probeXmlPath);
    if (!xmlFile) return -1;

    stringstream ss;
    ss << xmlFile.rdbuf();
    string content = ss.str();
    xmlFile.close();

    string startTag = "<Temperature>";
    string endTag = "</Temperature>";
    
    size_t start = content.find(startTag);
    if (start == string::npos) return -1;
    start += startTag.length();
    
    size_t end = content.find(endTag, start);
    if (end == string::npos) return -1;
    
    string tempStr = content.substr(start, end - start);
    
    try {
        tempStr.erase(remove_if(tempStr.begin(), tempStr.end(), ::isspace), tempStr.end());
        int freshTemp = stoi(tempStr);
        if (freshTemp >= -20 && freshTemp <= 100) {
            return freshTemp;
        }
    } catch (...) {}

    return -1;
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

    double criticalSpeedFloor = isSMR ? 4.0 : 10.0;

    if (avgSpeed < criticalSpeedFloor) {
        if (isSMR && stab >= 40.0) return "Warning (Severe SMR Cache Throttle)";
        if (stab >= 50.0) return "Warning (Severe Throttle)";
        return "Degraded (Extreme Jitter)";
    }

    double passingStabilityThreshold;
    double healthyStabilityThreshold;

    if (isSMR) {
        passingStabilityThreshold = (activeCooldown > 0) ? 45.0 : 50.0;
        healthyStabilityThreshold = (activeCooldown > 0) ? 65.0 : 70.0;
    } else if (isCMR) {
        passingStabilityThreshold = (activeCooldown > 0) ? 40.0 : 45.0;
        healthyStabilityThreshold = (activeCooldown > 0) ? 45.0 : 50.0;
    } else {
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
    if (accessLatencyMS <= 0.0) accessLatencyMS = 1.0;
    if (sustainedWriteMBps <= 0.0) sustainedWriteMBps = 100.0;

    double calculatedCooldown = 0.0;

    switch (driveType) {
        case 1: 
            calculatedCooldown = (accessLatencyMS * 1.5) + (150.0 / sustainedWriteMBps);
            break;
        case 2: 
            calculatedCooldown = (accessLatencyMS * 0.2) + (20.0 / sustainedWriteMBps);
            break;
        default: 
            calculatedCooldown = 10.0;
            break;
    }

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

    char pathBuf[MAX_PATH];
    if (!GetModuleFileNameA(NULL, pathBuf, MAX_PATH)) {
        cerr << "ERROR: Failed to resolve current process binary base location." << endl;
        return 1;
    }

    string exeFullPath(pathBuf);
    size_t lastSlashPos = exeFullPath.find_last_of("\\/");
    if (lastSlashPos == string::npos) return 1;
    string appDir = exeFullPath.substr(0, lastSlashPos + 1); 

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
    int progressInterval      = loggingCfg.count("SurfaceScanLogProgressInterval") ? stoi(loggingCfg["SurfaceScanLogProgressInterval"]) : 5;

    // Load Thermal Constraints
    bool warnOnHighTemp       = scanCfg.count("WarnOnHighTemp") ? (stoi(scanCfg["WarnOnHighTemp"]) == 1) : true;
    int thermalCeilingCelsius = scanCfg.count("WarningTemp") ? stoi(scanCfg["WarningTemp"]) : 55;
    int thermalTargetCoolDown = thermalCeilingCelsius - 5; 

    if (logEnabled && (logMode == "overwrite" || logMode == "ROTATE")) {
        ofstream clean(logsPath, ios::trunc); 
    }

    int oldLogLevel = loggingCfg.count("LoggingLevel") ? stoi(loggingCfg["LoggingLevel"]) : 0;

    if (logEnabled) {
        WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, "--- Surface Scan Starting Engine Initialization ---");
    }

    DriveInfo dev = GetDriveDetails(driveLtr);
    string resolvedDriveTech = dev.tech; 

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

        string xmlSerial = ExtractTagValue(xmlContent, "Serial");
        if (!xmlSerial.empty()) {
            dev.serial = xmlSerial;
        }
    }

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
        cerr << "ERROR: Block engine indexing fault. Native reference handle mapped invalid (-1)." << endl;
        return 1;
    }

    int percentage = 100;
    if (mode == "q" || mode == "Q") {
        percentage = scanCfg.count("QuickScanPercent") ? stoi(scanCfg["QuickScanPercent"]) : 10;
    } else {
        percentage = scanCfg.count("FullScanPercent") ? stoi(scanCfg["FullScanPercent"]) : 100;
    }

    if (percentage <= 0 || percentage > 100) percentage = 100;

    bool isSSD = (resolvedDriveTech.find("SSD") != string::npos || resolvedDriveTech.find("Solid State") != string::npos);

    cout << "\n===========================================\n";
    SetColor(cHeader);
    cout << " PUDIS SURFACE INTEGRITY TEST ENGINE v5.1.2\n";
    SetColor(cDefault);
    cout << "===========================================\n";
    cout << "Target Drive: " << dl << ": [" << dev.model << "]\n";
    cout << "Serial Num:   " << dev.serial << "\n";
    cout << "File System:  " << dev.fs << "\n";
    cout << "Bus Interface: " << dev.interfaceStr << " (" << (dev.isExternal ? "External" : "Internal") << ")\n";
    cout << "Capacity:     " << fixed << setprecision(2) << dev.capacityGB << " GB\n";
    cout << "Mode:         " << ((mode == "q" || mode == "Q") ? "Quick" : "Full") << " | Target Tech Profile: " << resolvedDriveTech << " (" << percentage << "%)" << endl;

    ULARGE_INTEGER freeA, totalB, freeB;
    string root = driveLtr + ":\\";
    long long freeBytesRaw = 0;
    if (GetDiskFreeSpaceExA(root.c_str(), &freeA, &totalB, &freeB))
        freeBytesRaw = static_cast<long long>(freeA.QuadPart);
    else
        freeBytesRaw = static_cast<long long>(dev.capacityGB * 1024.0 * 1024.0 * 1024.0);

    double ratio = static_cast<double>(percentage) / 100.0;
    long long targetBytes = static_cast<long long>(static_cast<double>(freeBytesRaw) * ratio) - (100LL * 1024LL);
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
        if (CHUNK_SIZE == 0 && targetBytes > 0) CHUNK_SIZE = 512;
    }

    int totalFiles = (CHUNK_SIZE > 0) ? static_cast<int>(targetBytes / static_cast<long long>(CHUNK_SIZE)) : 0;
    if (totalFiles < 1 && targetBytes > 0) totalFiles = 1;

    /* ============================================================
       Write / Read Verify Phase
       ============================================================ */
    vector<double> wSpeeds;
    vector<double> rSpeeds;
    int errors = 0;
    stringstream errorLog;

    const size_t SUB_BLOCK_SIZE = 4 * 1024 * 1024; // 4MB chunks
    vector<char> buffer(CHUNK_SIZE);

    int lastDisplayedPercent = -1;
    int lastDisplayedFile = -1;
    int lastLoggedProgress = -1;

    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);

    double movingAvgWriteSpeed = 0.0;
    const size_t ROLLING_SAMPLE_SIZE = 3;

    // Write Phase Loop
    for (int i = 1; i <= totalFiles; ++i) {
        string p = dataPath + "\\" + to_string(i) + ".h2w";
        long long startOffset = static_cast<long long>(i - 1) * static_cast<long long>(CHUNK_SIZE);
        FillPattern(buffer, startOffset);

        LARGE_INTEGER s, e;
        QueryPerformanceCounter(&s);

        HANDLE h = CreateFileA(p.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            errors++;
            if (logEnabled && logIOErrors) {
                WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, "Hardware write infrastructure allocation fault: " + p);
            }
            continue;
        }

        BOOL wRes = TRUE;
        size_t bytesWrittenTotal = 0;
        int stallRetries = 0;

        while (bytesWrittenTotal < CHUNK_SIZE) {
            DWORD bytesToResult = 0;
            size_t remaining = CHUNK_SIZE - bytesWrittenTotal;
            DWORD currentWriteSize = static_cast<DWORD>((remaining > SUB_BLOCK_SIZE) ? SUB_BLOCK_SIZE : remaining);

            auto subBlockStart = steady_clock::now();
            BOOL writeSuccess = WriteFile(h, buffer.data() + bytesWrittenTotal, currentWriteSize, &bytesToResult, NULL);
            auto subBlockEnd = steady_clock::now();
            auto elapsedSeconds = duration_cast<seconds>(subBlockEnd - subBlockStart).count();

            if (elapsedSeconds > 30) {
                if (logEnabled) {
                    WriteModuleLog(logsPath, LOG_WARN, currentLogLevel, "SMR Hardware Cache Exhaustion detected. Transaction timed out at file " + to_string(i));
                }
                wRes = FALSE;
                break;
            }

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
                
                int currentUiTemp = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
                string uiTempStr = (logTemperature && currentUiTemp > 0) ? to_string(currentUiTemp) + "C" : "N/A";

                cout << "\r";
                SetColor(cPrimary);
                cout << " [1/2] WRITING: ";
                SetColor(cSecondary);
                cout << totalProgress << "%";
                SetColor(cDefault);
                cout << " (File " << i << "/" << totalFiles << ") TEMP " << uiTempStr << " " << flush;
            }
        }

        QueryPerformanceCounter(&e);
        CloseHandle(h);

        if (!wRes) {
            errors++;
            DeleteFileA(p.c_str());
            continue;
        }

        double ticks = static_cast<double>(e.QuadPart - s.QuadPart);
        double freq = static_cast<double>(f.QuadPart);
        double speed = (static_cast<double>(CHUNK_SIZE) / (1024.0 * 1024.0)) / (ticks / freq);
        wSpeeds.push_back(speed);

        int currentProgress = static_cast<int>((static_cast<double>(i) * 100.0) / static_cast<double>(totalFiles));
        if (logEnabled && (currentProgress % progressInterval == 0) && currentProgress != lastLoggedProgress) {
            int currentTemp = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
            string tempStr = (logTemperature && currentTemp > 0) ? to_string(currentTemp) + "C" : "N/A";

            stringstream progStream;
            progStream << "Write Phase Progression Milestone: " << currentProgress << "% Complete | Target Node: " << i << "/" << totalFiles << " | Speed Metrics: " << fixed << setprecision(2) << speed << " MB/s | Temperature: " << tempStr;
            WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, progStream.str());
            lastLoggedProgress = currentProgress;

            // Active Thermal Governance Safety Check
            if (warnOnHighTemp && currentTemp >= thermalCeilingCelsius && currentTemp > 0) {
                cout << "\r\n";
                SetColor(cWarning);
                cout << " [WARN] Thermal threshold reached (" << currentTemp << "C). Pausing I/O engine for cooldown recovery..." << endl;
                SetColor(cDefault);
                if (logEnabled) {
                    WriteModuleLog(logsPath, LOG_WARN, currentLogLevel, "CRITICAL THERMAL METRIC ENCOUNTERED (" + to_string(currentTemp) + "C). Initializing safety pacing stall loop.");
                }
                while (currentTemp > thermalTargetCoolDown && currentTemp > 0) {
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    currentTemp = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
                }
                if (logEnabled) {
                    WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, "Thermal recovery sequence verified. Resuming operation stream metrics at: " + to_string(currentTemp) + "C");
                }
            }
        }

        if (wSpeeds.size() >= ROLLING_SAMPLE_SIZE) {
            movingAvgWriteSpeed = (wSpeeds[wSpeeds.size() - 1] + wSpeeds[wSpeeds.size() - 2] + wSpeeds[wSpeeds.size() - 3]) / 3.0;
            if (movingAvgWriteSpeed < 15.0 && !isSSD) {
                std::this_thread::sleep_for(std::chrono::milliseconds(800));
            } else if (isSSD) {
                if (activeCooldownMS > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(activeCooldownMS * 12));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
            }
        } else {
            if (activeCooldownMS > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(activeCooldownMS * 10));
            }
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
        string p = dataPath + "\\" + to_string(i) + ".h2w";
        LARGE_INTEGER s, e;
        QueryPerformanceCounter(&s);

        HANDLE h = CreateFileA(p.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            errors++;
            if (logEnabled && logIOErrors) {
                WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, "Missing transaction sequence reference node on verify pass: " + p);
            }
            continue;
        }

        BOOL rRes = TRUE;
        size_t bytesReadTotal = 0;

        while (bytesReadTotal < CHUNK_SIZE) {
            DWORD bytesToResult = 0;
            size_t remaining = CHUNK_SIZE - bytesReadTotal;
            DWORD currentReadSize = static_cast<DWORD>((remaining > SUB_BLOCK_SIZE) ? SUB_BLOCK_SIZE : remaining);

            BOOL readSuccess = ReadFile(h, buffer.data() + bytesReadTotal, currentReadSize, &bytesToResult, NULL);
            if (!readSuccess || bytesToResult == 0) {
                rRes = FALSE;
                break;
            }
            bytesReadTotal += bytesToResult;

            double currentFileProgress = (static_cast<double>(bytesReadTotal) / static_cast<double>(CHUNK_SIZE)) * 100.0;
            double globalProgressRaw = ((static_cast<double>(i) - 1.0) * 100.0 / static_cast<double>(totalFiles)) + (currentFileProgress / static_cast<double>(totalFiles));
            int totalProgress = static_cast<int>(globalProgressRaw);

            if (totalProgress < 0) totalProgress = 0;
            if (totalProgress > 100) totalProgress = 100;

            if (totalProgress != lastDisplayedPercent || i != lastDisplayedFile) {
                lastDisplayedPercent = totalProgress;
                lastDisplayedFile = i;
                
                int currentUiTemp = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
                string uiTempStr = (logTemperature && currentUiTemp > 0) ? to_string(currentUiTemp) + "C" : "N/A";

                cout << "\r";
                SetColor(cPrimary);
                cout << " [2/2] READING: ";
                SetColor(cSecondary);
                cout << totalProgress << "%";
                SetColor(cDefault);
                cout << " (File " << i << "/" << totalFiles << ") TEMP " << uiTempStr << " " << flush;
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
        double freq = static_cast<double>(f.QuadPart);
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
                errorLog << "Mismatch @ File " << i << ".h2w Offset " << b << " | Found: 0x" << hex << val << " Expected: 0x" << expectedVal << dec << "\n";
                if (logEnabled && logIOErrors) {
                    stringstream errStream;
                    errStream << "Integrity verification fault encountered. Block structural parity failed at segment " << b << " on node file " << i;
                    WriteModuleLog(logsPath, LOG_ERROR, currentLogLevel, errStream.str());
                }
            }
        }

        int currentProgress = static_cast<int>((static_cast<double>(i) * 100.0) / static_cast<double>(totalFiles));
        if (logEnabled && (currentProgress % progressInterval == 0) && currentProgress != lastLoggedProgress) {
            int currentTemp = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
            string tempStr = (logTemperature && currentTemp > 0) ? to_string(currentTemp) + "C" : "N/A";

            stringstream progStream;
            progStream << "Read Phase Progression Milestone: " << currentProgress << "% Complete | Target Node: " << i << "/" << totalFiles << " | Speed Metrics: " << fixed << setprecision(2) << speed << " MB/s | Temperature: " << tempStr;
            WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, progStream.str());
            lastLoggedProgress = currentProgress;

            if (warnOnHighTemp && currentTemp >= thermalCeilingCelsius && currentTemp > 0) {
                cout << "\r\n";
                SetColor(cWarning);
                cout << " [WARN] Thermal threshold reached (" << currentTemp << "C). Pausing read engine for cooldown recovery..." << endl;
                SetColor(cDefault);
                if (logEnabled) {
                    WriteModuleLog(logsPath, LOG_WARN, currentLogLevel, "CRITICAL THERMAL METRIC ENCOUNTERED DURING READ (" + to_string(currentTemp) + "C). Initializing safety pacing stall loop.");
                }
                while (currentTemp > thermalTargetCoolDown && currentTemp > 0) {
                    std::this_thread::sleep_for(std::chrono::seconds(10));
                    currentTemp = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
                }
                if (logEnabled) {
                    WriteModuleLog(logsPath, LOG_INFO, currentLogLevel, "Thermal recovery sequence verified. Resuming read verification pass at: " + to_string(currentTemp) + "C");
                }
            }
        }

        DeleteFileA(p.c_str());

        if (activeCooldownMS > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(activeCooldownMS * 10));
        }
    }

    cout << endl;

    /* ============================================================
       Final Stats & Diagnostics Serialization
       ============================================================ */
    auto totalDuration = duration_cast<seconds>(steady_clock::now() - startTime);
    string durationStr = FormatSeconds(static_cast<double>(totalDuration.count()));

    sort(rSpeeds.begin(), rSpeeds.end());
    double rStab = 0.0;
    double avgR = 0.0;

    if (!rSpeeds.empty()) {
        avgR = accumulate(rSpeeds.begin(), rSpeeds.end(), 0.0) / static_cast<double>(rSpeeds.size());
        size_t lowerBoundIndex = static_cast<size_t>(static_cast<double>(rSpeeds.size()) * 0.05);
        double reliableMinSpeed = rSpeeds[lowerBoundIndex];
        double peakSpeed = rSpeeds.back();
        if (peakSpeed > 0.0) {
            rStab = (reliableMinSpeed / peakSpeed) * 100.0;
        } else {
            rStab = 0.0;
        }
    }

    sort(wSpeeds.begin(), wSpeeds.end());
    double wStab = 0.0;
    double avgW = 0.0;

    if (!wSpeeds.empty()) {
        avgW = accumulate(wSpeeds.begin(), wSpeeds.end(), 0.0) / static_cast<double>(wSpeeds.size());
        size_t lowerBoundIndex = static_cast<size_t>(static_cast<double>(wSpeeds.size()) * 0.05);
        double reliableMinSpeed = wSpeeds[lowerBoundIndex];
        double peakSpeed = wSpeeds.back();
        if (peakSpeed > 0.0) {
            wStab = (reliableMinSpeed / peakSpeed) * 100.0;
        } else {
            wStab = 0.0;
        }
    }

    string finalGrade = GetTechnicalGrade(rStab, errors, avgR, activeCooldownMS, resolvedDriveTech);

    int finalTempMetric = GetDriveTemperatureCelsius(dev.diskIndex, driveLtr, dataPath);
    string finalTempStr = (logTemperature && finalTempMetric > 0) ? to_string(finalTempMetric) + "C" : "N/A";

    string metadataBlock = "";
    string hwIdentBlock = "";
    string capBlock = "";
    string vitalsBlock = "";
    string pol = "";
    string smrt = "";
    string integ = "";

    ifstream masterXmlIn(xmlPath);
    if (masterXmlIn) {
        stringstream ss;
        ss << masterXmlIn.rdbuf();
        string masterXmlContent = ss.str();
        masterXmlIn.close();

        metadataBlock = ExtractBlock(masterXmlContent, "Metadata");
        hwIdentBlock  = ExtractBlock(masterXmlContent, "HardwareIdentity");
        capBlock      = ExtractBlock(masterXmlContent, "StorageCapacity");
        vitalsBlock   = ExtractBlock(masterXmlContent, "HardwareVitals");
        pol           = ExtractBlock(masterXmlContent, "Policy");
        smrt          = ExtractBlock(masterXmlContent, "SmartHealthStatus");
        integ         = ExtractBlock(masterXmlContent, "FileIntegrityScan");
    }

    ofstream xml(xmlPath, ios::trunc);
    if (xml) {
        xml << "<DriveBabySitter>\n";
        if (!metadataBlock.empty()) xml << "  " << metadataBlock << "\n";
        if (!hwIdentBlock.empty())  xml << "  " << hwIdentBlock << "\n";
        if (!capBlock.empty())      xml << "  " << capBlock << "\n";
        if (!vitalsBlock.empty())   xml << "  " << vitalsBlock << "\n";
        if (!pol.empty())           xml << "  " << pol << "\n";
        if (!smrt.empty())          xml << "  " << smrt << "\n";
        if (!integ.empty())         xml << "  " << integ << "\n";
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
            legacyLog << "--- LEGACY SESSION REPORT BLOCK METRICS ---\n"
                      << "Session ID Timestamp: " << GetTimestamp() << "\n"
                      << "Device Context Model: " << dev.model << " | Drive Letter Mapping: " << dl << ":\n"
                      << "Device Serial Number: " << dev.serial << "\n" // Option 1 Integration Anchor Added
                      << "Total Verification Run Time Duration: " << durationStr << "\n"
                      << "Write Metric Baseline Speed Average: " << fixed << setprecision(2) << avgW << " MB/s | Parity Stability Delta: " << fixed << setprecision(1) << wStab << "%\n"
                      << "Read Metric Verification Speed Average: " << fixed << setprecision(2) << avgR << " MB/s | Parity Stability Delta: " << fixed << setprecision(1) << rStab << "%\n"
                      << "Terminal Execution Evaluation Grade: " << finalGrade << "\n"
                      << "Hardware Metrics: Terminal Temperature: " << finalTempStr << " | IO Error Count: " << errors << "\n";

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
    else SetColor(cSuccess);
    cout << errors << "\n";

    SetColor(cHeader);
    cout << " Write Speed: " << fixed << setprecision(2) << avgW << " MB/s [Stability: " << setprecision(1) << wStab << "%]\n";
    cout << " Read Speed:  " << fixed << setprecision(2) << avgR << " MB/s [Stability: " << setprecision(1) << rStab << "%]\n";
    cout << " Temp Delta:  " << finalTempStr << "\n";
    SetColor(cPrimary);
    cout << "========================================\n";
    SetColor(cDefault);

    if (errors > 0) {
        SetColor(cCritical);
        cout << "\n[!] WARNING: Block parity mismatch errors detected during validation pass. Check surface_scan.log for offsets.\n\n";
        SetColor(cDefault);
    }

    return 0;
}