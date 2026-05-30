/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: deviceinfo.cpp
   Author: sussjb99
   Version: 2.7.0 (Unified Structural Gatekeeper)
   Last Modified: 2026-05-26
   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose: Retrieve drive technical information and act as the 
            sole structural state gatekeeper for Drive_Status.xml.
   Mode: C++17 (MSVC Compatible), no <filesystem>
   ============================================================ */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <array>
#include <memory>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <map>
#include <cctype>

// ----------------- Utility helpers -----------------

std::string ToUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return (char)std::toupper(c); });
    return s;
}

std::string Trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string XmlEscape(std::string data) {
    size_t pos = 0;
    while ((pos = data.find('&', pos)) != std::string::npos) {
        data.replace(pos, 1, "&amp;");
        pos += 5;
    }
    return data;
}

std::string ExtractBlock(const std::string& content, const std::string& tag) {
    std::string startTag = "<" + tag + ">";
    std::string endTag   = "</" + tag + ">";
    size_t start = content.find(startTag);
    size_t end   = content.find(endTag);
    if (start != std::string::npos && end != std::string::npos) {
        return content.substr(start, (end + endTag.length()) - start);
    }
    return "";
}

// ----------------- INI handling -----------------

std::map<std::string, std::string> LoadIniSection(const std::string& path, const std::string& section) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) return out;

    std::string line;
    std::string want = "[" + section + "]";
    bool active = false;

    while (std::getline(in, line)) {
        std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#')
            continue;

        if (t == want) {
            active = true;
            continue;
        }
        if (t.size() > 2 && t[0] == '[' && t.back() == ']') {
            active = false;
            continue;
        }
        if (!active)
            continue;

        size_t eq = t.find('=');
        if (eq != std::string::npos) {
            std::string key = Trim(t.substr(0, eq));
            std::string val = Trim(t.substr(eq + 1));
            out[key] = val;
        }
    }
    return out;
}

bool FileExists(const std::string& path) {
    std::ifstream f(path.c_str());
    return f.good();
}

// ----------------- Config discovery -----------------

std::string GetExeDirectory() {
    char buf[MAX_PATH] = {0};
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return "";

    std::string full(buf);
    size_t pos = full.find_last_of("\\/");
    if (pos == std::string::npos) return "";
    return full.substr(0, pos); 
}

std::string GetParentDirectory(const std::string& path) {
    size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return "";
    return path.substr(0, pos);
}

std::string FindConfigPath(const std::string& driveRoot) {
    std::string candidate1 = driveRoot + "\\Integrity_Check\\config\\config.ini";
    if (FileExists(candidate1))
        return candidate1;

    std::string exeDir = GetExeDirectory();
    if (!exeDir.empty()) {
        std::string rootDir = GetParentDirectory(exeDir); 
        if (!rootDir.empty()) {
            std::string candidate2 = rootDir + "\\config\\config.ini";
            if (FileExists(candidate2))
                return candidate2;
        }
    }

    std::string candidate3 = driveRoot + "\\config\\config.ini";
    if (FileExists(candidate3))
        return candidate3;

    return "";
}

// ----------------- Probe handling -----------------

std::string GetProbeOutput() {
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(
        _popen("full_probe.exe", "r"), _pclose);
    if (!pipe) return "";
    while (fgets(buffer.data(), (int)buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string GetJsonValue(const std::string& block, const std::string& key) {
    std::string searchKey = "\"" + key + "\":";
    size_t pos = block.find(searchKey);
    if (pos == std::string::npos) return "N/A";

    size_t valStart = block.find_first_not_of(" \t\n\r", pos + searchKey.length());
    if (valStart == std::string::npos) return "N/A";

    if (block[valStart] == '\"') {
        size_t start = valStart + 1;
        size_t end = block.find('\"', start);
        return (end == std::string::npos) ? "N/A" : block.substr(start, end - start);
    } else {
        size_t end = block.find_first_of(",\n\r}", valStart);
        std::string val = block.substr(valStart, end - valStart);
        val.erase(std::remove_if(val.begin(), val.end(), [](unsigned char c) { return std::isspace(c); }), val.end());
        return val;
    }
}

// ----------------- Main -----------------

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: deviceinfo.exe [DriveLetter] [PathToXML]\n";
        return 1;
    }

    std::string targetDrive = ToUpper(argv[1]);
    if (!targetDrive.empty() && targetDrive.back() != ':')
        targetDrive += ":";

    std::string xmlPath = argv[2];

    // Read existing XML to safely keep rolling updates
    std::string existingContent;
    {
        std::ifstream existingFile(xmlPath);
        if (existingFile) {
            std::stringstream ss;
            ss << existingFile.rdbuf();
            existingContent = ss.str();
        }
    }

    // -------- Find config.ini --------
    std::string configPath = FindConfigPath(targetDrive);
    std::map<std::string, std::string> perf;
    std::map<std::string, std::string> fsProfile;

    if (!configPath.empty()) {
        perf      = LoadIniSection(configPath, "Performance");
        fsProfile = LoadIniSection(configPath, "FileSystemProfile");
    }

    auto getMetric = [](const std::map<std::string, std::string>& m, const std::string& k) {
        auto it = m.find(k);
        return (it == m.end() || it->second.empty()) ? std::string("N/A") : it->second;
    };

    // Extract hardware performance metrics
    std::string seqRead     = getMetric(perf, "MeasuredSequentialReadMBps");
    std::string seqWrite    = getMetric(perf, "MeasuredSustainedWriteMBps"); 
    std::string randIOPS    = getMetric(perf, "MeasuredRandomReadIOPS");
    std::string latency     = getMetric(perf, "MeasuredAccessLatencyMS");
    
    // Extract complete FileSystemProfile snapshots from operational workspace
    std::string totalFiles  = getMetric(fsProfile, "TotalFiles");
    std::string totalBytes  = getMetric(fsProfile, "TotalBytes");
    std::string avgFileSize = getMetric(fsProfile, "AverageFileSize");
    std::string smFileRatio = getMetric(fsProfile, "SmallFileRatio");
    std::string dirCount    = getMetric(fsProfile, "DirectoryCount");
    std::string maxDepth    = getMetric(fsProfile, "MaxDepth");

    bool isCalibrated = (seqRead != "N/A" || seqWrite != "N/A" || randIOPS != "N/A" || latency != "N/A");

    // -------- Get probe JSON --------
    std::string json = GetProbeOutput();
    if (json.empty()) {
        std::cerr << "Error: No output from full_probe.exe\n";
        return 1;
    }

    std::string integrityBlock = ExtractBlock(existingContent, "FileIntegrityScan");
    std::string surfaceBlock   = ExtractBlock(existingContent, "SurfaceScanInfo");

    std::string block;
    bool found = false;
    size_t start = 0;

    while ((start = json.find("{", start)) != std::string::npos) {
        size_t end = json.find("}", start);
        if (end == std::string::npos) break;

        block = json.substr(start, end - start + 1);
        std::string foundLetter = ToUpper(GetJsonValue(block, "Letter"));

        if (foundLetter == targetDrive) {
            found = true;
            break;
        }
        start = end + 1;
    }

    if (!found) {
        std::cerr << "Drive " << targetDrive << " not found in probe data.\n";
        return 1;
    }

    auto J = [&](const std::string& key) { return GetJsonValue(block, key); };

    std::string model       = J("Model");
    std::string serial      = J("Serial");
    std::string iface       = J("Interface");
    std::string tech        = J("Technology");
    std::string fsType      = J("FsType");
    std::string totalGB     = J("TotalGB");
    std::string usedGB      = J("UsedGB");
    std::string freeGB      = J("FreeGB");
    std::string percentUsed = J("PercentUsed");
    std::string hours       = J("Hours");
    std::string temp        = J("Temp");
    std::string reallocated = J("Reallocated_Sectors_Ct");
    std::string pending     = J("Current_Pending_Sector");
    std::string uncorrect   = J("Offline_Uncorrectable");
    std::string crc         = J("CrcErrors");
    std::string loadCycles  = J("Load_Cycles");
    std::string rpm         = J("RPM");
    std::string formFactor  = J("Form_Factor");
    std::string health      = J("Health");
    std::string notes       = J("Notes");

    std::string driveType = "HDD";
    std::string usbTransport = "N/A";
    std::string bridgeSAT = "N/A";
    std::string smartPass = "N/A";
    std::string trimSupport = "N/A";

    bool isUSB = (iface == "SCSI" || iface.find("USB") != std::string::npos);

    if (isUSB) {
        if (rpm != "N/A")
            driveType = "USB_HDD";
        else
            driveType = "USB_UNKNOWN";

        usbTransport = (iface == "SCSI") ? "UASP" : "BOT";
        bridgeSAT = "False";
        smartPass = "False";
        trimSupport = "False";
    }

    std::string stressProfile = isUSB ? "External" : "Internal";

    // Establish rigid safety flags based on context rules
    bool stressAllowed = (stressProfile == "Internal") ? false : true;
    if (targetDrive == "C:") {
        stressAllowed = false;
    }

    char tB[64];
    time_t n = time(0);
    tm l;
    localtime_s(&l, &n);
    strftime(tB, sizeof(tB), "%Y-%m-%d %H:%M:%S", &l);

    // -------- Write XML File --------
    std::ofstream xml(xmlPath);
    if (!xml) {
        std::cerr << "Error: Unable to open XML path for writing: " << xmlPath << "\n";
        return 1;
    }

    xml << "<DriveBabySitter>\n";

    xml << "  <Metadata>\n";
    xml << "    <LastHardwareProbe>" << tB << "</LastHardwareProbe>\n";
    xml << "    <LastReportGenerated>N/A</LastReportGenerated>\n";
    xml << "  </Metadata>\n";

    xml << "  <HardwareIdentity>\n";
    xml << "    <DriveLetter>" << targetDrive << "</DriveLetter>\n";
    xml << "    <Model>" << XmlEscape(model) << "</Model>\n";
    xml << "    <Serial>" << XmlEscape(serial) << "</Serial>\n";
    xml << "    <Interface>" << iface << "</Interface>\n";
    xml << "    <UsbTransport>" << usbTransport << "</UsbTransport>\n";
    xml << "    <DriveType>" << driveType << "</DriveType>\n";
    xml << "    <Technology>" << tech << "</Technology>\n";
    xml << "    <RotationRate>" << rpm << "</RotationRate>\n";
    xml << "    <FormFactor>" << formFactor << "</FormFactor>\n";
    xml << "    <FileSystem>" << fsType << "</FileSystem>\n";
    xml << "  </HardwareIdentity>\n";

    xml << "  <BridgeCapabilities>\n";
    xml << "    <BridgeSAT>" << bridgeSAT << "</BridgeSAT>\n";
    xml << "    <SmartPassthrough>" << smartPass << "</SmartPassthrough>\n";
    xml << "    <TrimSupported>" << trimSupport << "</TrimSupported>\n";
    xml << "  </BridgeCapabilities>\n";

    xml << "  <StorageCapacity>\n";
    xml << "    <TotalGB>" << totalGB << "</TotalGB>\n";
    xml << "    <UsedGB>" << usedGB << "</UsedGB>\n";
    xml << "    <FreeGB>" << freeGB << "</FreeGB>\n";
    xml << "    <PercentUsed>" << percentUsed << "</PercentUsed>\n";
    xml << "  </StorageCapacity>\n";

    xml << "  <HardwareVitals>\n";
    xml << "    <PowerOnHours>" << hours << "</PowerOnHours>\n";
    xml << "    <Temperature>" << temp << "</Temperature>\n";
    xml << "    <LoadCycles>" << loadCycles << "</LoadCycles>\n";
    xml << "    <ReallocatedSectors>" << reallocated << "</ReallocatedSectors>\n";
    xml << "    <PendingSectors>" << pending << "</PendingSectors>\n";
    xml << "    <UncorrectableSectors>" << uncorrect << "</UncorrectableSectors>\n";
    xml << "    <CrcErrors>" << crc << "</CrcErrors>\n";
    xml << "    <HealthRating>" << health << "</HealthRating>\n";
    xml << "  </HardwareVitals>\n";

    xml << "  <SmartHealthStatus>\n";
    xml << "    <SmartStatus>" << health << "</SmartStatus>\n";
    xml << "    <Notes>" << notes << "</Notes>\n";
    xml << "  </SmartHealthStatus>\n";

    xml << "  <Policy>\n";
    xml << "    <StressProfile>" << stressProfile << "</StressProfile>\n";
    xml << "    <StressTestAllowed>" << (stressAllowed ? "True" : "False") << "</StressTestAllowed>\n";
    xml << "    <Notes>" << (targetDrive == "C:" ? "CRITICAL: System Drive Lockout Active" : (stressProfile == "Internal" ? "System Drive Protection Active" : "Standard Drive")) << "</Notes>\n";
    xml << "  </Policy>\n";

    xml << "  <Calibration>\n";
    xml << "    <Calibrated>" << (isCalibrated ? "True" : "False") << "</Calibrated>\n";
    xml << "    <ReadSpeed>" << seqRead << "</ReadSpeed>\n";
    xml << "    <WriteSpeed>" << seqWrite << "</WriteSpeed>\n"; 
    xml << "    <RandomIOPS>" << randIOPS << "</RandomIOPS>\n";
    xml << "    <LatencyMS>" << latency << "</LatencyMS>\n";
    xml << "    <FileCount>" << totalFiles << "</FileCount>\n";
    xml << "  </Calibration>\n";

    // -------- New File System Profile Section Sync --------
    xml << "  <FileSystemProfile>\n";
    xml << "    <TotalFiles>" << totalFiles << "</TotalFiles>\n";
    xml << "    <TotalBytes>" << totalBytes << "</TotalBytes>\n";
    xml << "    <AverageFileSize>" << avgFileSize << "</AverageFileSize>\n";
    xml << "    <SmallFileRatio>" << smFileRatio << "</SmallFileRatio>\n";
    xml << "    <DirectoryCount>" << dirCount << "</DirectoryCount>\n";
    xml << "    <MaxDepth>" << maxDepth << "</MaxDepth>\n";
    xml << "  </FileSystemProfile>\n";

    if (!integrityBlock.empty())
        xml << "  " << integrityBlock << "\n";
    else {
        xml << "  <FileIntegrityScan>\n";
        xml << "    <LastScanDate>N/A</LastScanDate>\n";
        xml << "    <FilesChecked>N/A</FilesChecked>\n";
        xml << "    <CorruptFiles>N/A</CorruptFiles>\n";
        xml << "    <IntegrityGrade>NotPerformed</IntegrityGrade>\n";
        xml << "  </FileIntegrityScan>\n";
    }

    if (!surfaceBlock.empty())
        xml << "  " << surfaceBlock << "\n";
    else {
        xml << "  <SurfaceScanInfo>\n";
        xml << "    <SurfaceScanDate>N/A</SurfaceScanDate>\n";
        xml << "    <ActualTimeTaken>N/A</ActualTimeTaken>\n";
        xml << "    <SurfaceGrade>NotPerformed</SurfaceGrade>\n";
        xml << "    <StabilityScore>N/A</StabilityScore>\n";
        xml << "    <AvgReadSpeed>N/A</AvgReadSpeed>\n";
        xml << "    <ScanCoverage>N/A</ScanCoverage>\n";
        xml << "    <ErrorsDetected>N/A</ErrorsDetected>\n";
        xml << "  </SurfaceScanInfo>\n";
    }

    xml << "</DriveBabySitter>\n";
    xml.close();

    std::cout << "SUCCESS: Hardware data updated for " << targetDrive
              << " [" << tB << "]\n";
    return 0;
}