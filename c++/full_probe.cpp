/* ============================================================
   Portable USB Drive Integrity Suite
   File: full_probe.cpp
   Author: sussjb99
   Version: 1.9 (Strict Regex Compliance & Precise String Trimming)
   Last Modified: 2026-05-28
   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.
   ============================================================ */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <shellapi.h>
#include <winioctl.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <comdef.h>
#include <WbemIdl.h>
#include <iomanip>
#include <ctime>
#include <sstream>
#include <array>
#include <algorithm>
#include <regex>
#include <cctype>

#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

struct SmartData {
    std::string model = "", serial = "", family = "", formFactor = "Unknown";
    long hours = -1, temp = -1, reallocated = -1, pending = -1, offlineUnc = -1, loadCycles = -1;
    int rpm = 0, percentageUsed = -1; 
    long long dataUnitsWritten = -1; 
};

struct DbEntry {
    std::string pattern;
    std::string techType;
};

// Enhanced trimming to wipe out all tabs, spaces, returns, quotes, and null terminators
std::string CleanStr(std::string s) {
    size_t first = s.find_first_not_of(" \t\r\n\"\0");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n\"\0");
    return s.substr(first, (last - first + 1));
}

std::string SanitizePathString(const std::string& raw) {
    std::string clean = "";
    for (char c : raw) {
        if (std::isprint(static_cast<unsigned char>(c)) && c != '"' && c != '\'' && c != '\r' && c != '\n') {
            clean += c;
        }
    }
    return CleanStr(clean);
}

std::string bstrToStr(const _bstr_t& b) { 
    if (b.length() == 0) return "";
    return CleanStr(static_cast<const char*>(b));
}

std::string ExtractSerialFromPnP(std::string pnp) {
    size_t lastSlash = pnp.find_last_of('\\');
    if (lastSlash == std::string::npos) return CleanStr(pnp);
    std::string serial = pnp.substr(lastSlash + 1);
    size_t firstAmp = serial.find('&');
    if (firstAmp != std::string::npos) serial = serial.substr(0, firstAmp);
    return CleanStr(serial);
}

std::string getJsonVal(const std::string& json, const std::string& key) {
    size_t pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    size_t start = json.find(":", pos) + 1;
    size_t end = json.find_first_of(",}\n", start);
    return CleanStr(json.substr(start, end - start));
}

void ParseSmart(const std::string& json, SmartData& sd) {
    if (json.length() < 50) return;
    std::string m = getJsonVal(json, "model_name"); if(!m.empty()) sd.model = m;
    std::string s = getJsonVal(json, "serial_number"); if(!s.empty()) sd.serial = s;
    sd.family = getJsonVal(json, "model_family");
    
    std::string rot = getJsonVal(json, "rotation_rate");
    if (!rot.empty()) try { sd.rpm = std::stoi(rot); } catch(...) {}
    size_t ffPos = json.find("\"form_factor\"");
    if (ffPos != std::string::npos) sd.formFactor = getJsonVal(json.substr(ffPos), "name");

    size_t pot = json.find("\"power_on_time\"");
    std::string h = (pot != std::string::npos) ? getJsonVal(json.substr(pot), "hours") : getJsonVal(json, "power_on_hours");
    if (!h.empty()) try { sd.hours = std::stol(h); } catch(...) {}
    size_t tPos = json.find("\"temperature\"");
    if (tPos != std::string::npos) {
        std::string cur = getJsonVal(json.substr(tPos), "current");
        if (!cur.empty()) try { sd.temp = std::stol(cur); } catch(...) {}
    }

    if (json.find("\"nvme_smart_health_information_log\"") != std::string::npos) {
        std::string pu = getJsonVal(json, "percentage_used");
        if (!pu.empty()) try { sd.percentageUsed = std::stoi(pu); } catch(...) {}
        std::string duw = getJsonVal(json, "data_units_written");
        if (!duw.empty()) try { sd.dataUnitsWritten = std::stoll(duw); } catch(...) {}
        std::string me = getJsonVal(json, "media_errors");
        if (!me.empty()) try { sd.reallocated = std::stol(me); } catch(...) {}
        return; 
    }

    auto getRaw = [&](int id) -> long {
        size_t p = json.find("\"id\": " + std::to_string(id));
        if (p == std::string::npos) return -1;
        size_t r = json.find("\"raw\"", p);
        if (r == std::string::npos) return -1;
        std::string v = getJsonVal(json.substr(r), "value");
        try { return v.empty() ? -1 : std::stol(v); } catch(...) { return -1; }
    };
    
    sd.reallocated = getRaw(5);
    sd.pending = getRaw(197);
    sd.offlineUnc = getRaw(198);
    sd.loadCycles = getRaw(193);
}

std::string IdentifyDriveTechnology(const std::string& rawModel, double totGB, const std::vector<DbEntry>& dbCache) {
    std::string model = CleanStr(rawModel);

    std::string upperModel = model;
    std::transform(upperModel.begin(), upperModel.end(), upperModel.begin(), ::toupper);
    if (upperModel.find("FLASH") != std::string::npos || upperModel.find("USB DISK") != std::string::npos) {
        return "Flash";
    }

    // Retained strict full-string std::regex_match for exact alignment validation
    for (const auto& entry : dbCache) {
        try {
            std::regex reg(entry.pattern, std::regex_constants::icase);
            if (std::regex_match(model, reg)) {
                return entry.techType;
            }
        } catch (const std::regex_error&) {
            continue;
        }
    }

    return (totGB > 256.0) ? "Unknown (PMR/SMR Unverified)" : "SSD";
}

int main() {
    // --------------------------------------------------------
    // Absolute Canonical Path Resolution Workspace
    // --------------------------------------------------------
    char rawExePath[MAX_PATH];
    GetModuleFileNameA(NULL, rawExePath, MAX_PATH);
    std::string exeFullPath(rawExePath);
    size_t lastSlash = exeFullPath.find_last_of("\\/");
    std::string binDir = exeFullPath.substr(0, lastSlash + 1);
    
    char canonicalRoot[MAX_PATH];
    std::string relativeRoot = binDir + "..\\";
    GetFullPathNameA(relativeRoot.c_str(), MAX_PATH, canonicalRoot, NULL);
    std::string rootDir = std::string(canonicalRoot);
    if (rootDir.back() != '\\' && rootDir.back() != '/') {
        rootDir += "\\";
    }

    std::string iniPath = rootDir + "config\\config.ini";

    char dbPathBuffer[MAX_PATH] = { 0 };
    GetPrivateProfileStringA("Paths", "drivedb", "config\\drivedb.txt", dbPathBuffer, MAX_PATH, iniPath.c_str());
    
    std::string sanitizedIniValue = SanitizePathString(std::string(dbPathBuffer));
    std::string fullyFormedTarget = rootDir + sanitizedIniValue;
    char canonicalDb[MAX_PATH];
    GetFullPathNameA(fullyFormedTarget.c_str(), MAX_PATH, canonicalDb, NULL);
    std::string absoluteDbPath = std::string(canonicalDb);

    // --------------------------------------------------------
    // Database Parsing and In-Memory Caching (Single Pass)
    // --------------------------------------------------------
    std::vector<DbEntry> dbCache;
    std::ifstream dbFile(absoluteDbPath);
    if (dbFile.is_open()) {
        std::string line;
        while (std::getline(dbFile, line)) {
            line = CleanStr(line);
            if (line.empty() || line[0] == '#') continue;

            size_t pipe = line.find('|');
            if (pipe == std::string::npos) continue;

            DbEntry entry;
            entry.pattern = CleanStr(line.substr(0, pipe));
            entry.techType = CleanStr(line.substr(pipe + 1));
            dbCache.push_back(entry);
        }
        dbFile.close();
    } else {
        std::cerr << "\n/* CRITICAL STORAGE FAULT: Unable to find hardware lookup matrix at: " << absoluteDbPath << " */\n";
    }

    // --------------------------------------------------------
    // Initialize Windows Management Instrumentation Interface
    // --------------------------------------------------------
    CoInitializeEx(0, COINIT_MULTITHREADED);
    CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_WINNT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    IWbemLocator* pLoc = NULL; CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    IWbemServices* pSvc = NULL; pLoc->ConnectServer(_bstr_t(L"ROOT\\CIMV2"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
    CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

    IEnumWbemClassObject* pEnum = NULL;
    pSvc->ExecQuery(bstr_t("WQL"), bstr_t("SELECT * FROM Win32_DiskDrive"), WBEM_FLAG_FORWARD_ONLY, NULL, &pEnum);

    std::cout << "[\n";
    IWbemClassObject* pDisk = NULL; ULONG uRet = 0; bool first = true;
    char tB[64]; time_t n = time(0); tm l; localtime_s(&l, &n); strftime(tB, 64, "%Y-%m-%d %H:%M:%S", &l);
    
    while (pEnum && pEnum->Next(WBEM_INFINITE, 1, &pDisk, &uRet) == WBEM_S_NO_ERROR) {
        VARIANT vM, vS, vI, vSz, vIf, vPnP;
        pDisk->Get(L"Model", 0, &vM, 0, 0); pDisk->Get(L"SerialNumber", 0, &vS, 0, 0);
        pDisk->Get(L"Index", 0, &vI, 0, 0); pDisk->Get(L"Size", 0, &vSz, 0, 0);
        pDisk->Get(L"InterfaceType", 0, &vIf, 0, 0); pDisk->Get(L"PNPDeviceID", 0, &vPnP, 0, 0);

        double totGB = (vSz.vt == VT_BSTR || vSz.vt == VT_LPWSTR) ? std::stod(bstrToStr(_bstr_t(vSz))) / 1073741824.0 : 0;
        if (totGB <= 0.01) { pDisk->Release(); continue; }

        int idx = (int)vI.lVal;
        SmartData sd;
        std::string cmd = "smartctl.exe --json -a /dev/pd" + std::to_string(idx);
        std::string output; std::array<char, 4096> buf;
        FILE* pipe = _popen(cmd.c_str(), "r");
        if (pipe) { while (fgets(buf.data(), 4096, pipe)) output += buf.data(); _pclose(pipe); }
        ParseSmart(output, sd);

        std::string interfaceStr = bstrToStr(_bstr_t(vIf));
        
        // Force thorough dual-source validation to clear away any hidden raw buffer space padding
        std::string modelStr = sd.model.empty() ? bstrToStr(_bstr_t(vM)) : sd.model;
        modelStr = CleanStr(modelStr);
        
        if (interfaceStr == "IDE" && totGB > 500.0) {
            interfaceStr = "SATA";
        }

        std::string tech = IdentifyDriveTechnology(modelStr, totGB, dbCache);
        std::string finalForm = sd.formFactor;

        if (modelStr.find("Flash") != std::string::npos || (interfaceStr == "USB" && totGB < 256)) {
            tech = "Flash"; finalForm = "Thumb Drive";
        }
        if (finalForm == "Unknown" && (tech == "CMR" || tech == "SMR" || tech.find("Unknown") != std::string::npos)) {
            finalForm = (totGB > 1000 ? "3.5\" Drive" : "2.5\" Drive");
        }

        std::string finalFamily = sd.family.length() > 1 ? sd.family : (tech == "Flash" ? "USB Flash Drive" : "N/A");

        std::string letter = "N/A", fs = "N/A", notes = ""; double freeGB = -1.0;
        char dq[512]; sprintf_s(dq, "ASSOCIATORS OF {Win32_DiskDrive.DeviceID='\\\\.\\PHYSICALDRIVE%d'} WHERE AssocClass = Win32_DiskDriveToDiskPartition", idx);
        IEnumWbemClassObject* pPEnum = NULL;
        if (SUCCEEDED(pSvc->ExecQuery(bstr_t("WQL"), bstr_t(dq), WBEM_FLAG_FORWARD_ONLY, NULL, &pPEnum))) {
            IWbemClassObject* pP = NULL;
            while (pPEnum->Next(WBEM_INFINITE, 1, &pP, &uRet) == WBEM_S_NO_ERROR) {
                VARIANT vPID; pP->Get(L"DeviceID", 0, &vPID, 0, 0);
                std::string lq = "ASSOCIATORS OF {Win32_DiskPartition.DeviceID='" + bstrToStr(_bstr_t(vPID)) + "'} WHERE AssocClass = Win32_LogicalDiskToPartition";
                IEnumWbemClassObject* pLEnum = NULL;
                if (SUCCEEDED(pSvc->ExecQuery(bstr_t("WQL"), bstr_t(lq.c_str()), WBEM_FLAG_FORWARD_ONLY, NULL, &pLEnum))) {
                    IWbemClassObject* pL = NULL;
                    if (pLEnum->Next(WBEM_INFINITE, 1, &pL, &uRet) == WBEM_S_NO_ERROR) {
                        VARIANT vD, vF, vFs; pL->Get(L"DeviceID", 0, &vD, 0, 0); pL->Get(L"FreeSpace", 0, &vF, 0, 0); pL->Get(L"FileSystem", 0, &vFs, 0, 0);
                        letter = bstrToStr(_bstr_t(vD)); fs = bstrToStr(_bstr_t(vFs));
                        if (vF.vt == VT_BSTR || vF.vt == VT_LPWSTR) freeGB = std::stod(bstrToStr(_bstr_t(vF))) / 1073741824.0;
                        pL->Release();
                    }
                    pLEnum->Release();
                }
                pP->Release(); if (letter != "N/A") break;
            }
            pPEnum->Release();
        }

        std::string profile = (interfaceStr == "USB" || interfaceStr == "SCSI") ? "External" : "Internal";
        if (letter == "C:" || (idx == 0 && letter == "N/A")) { profile = "System Drive"; notes = "System Drive - Stress Test Forbidden"; }

        std::string healthStatus = "N/A";
        bool hasTelemetry = (sd.hours >= 0 || sd.temp >= 0 || sd.reallocated >= 0);

        if (!hasTelemetry) {
            notes += (notes.empty() ? "" : " | ") + std::string("No SMART telemetry available");
        } else {
            healthStatus = "PASSED";

            if (sd.reallocated > 0 || sd.pending > 0 || sd.offlineUnc > 0) {
                healthStatus = "WARNING"; 
                notes += (notes.empty() ? "" : " | ") + std::string("Hardware Sectors Failing");
            } else if (tech != "Flash" && sd.hours > 35000) {
                healthStatus = "PASSED"; 
                notes += (notes.empty() ? "" : " | ") + std::string("High Power-On Hours (Aged Profile)");
            } else if (sd.percentageUsed > 90) {
                healthStatus = "DEGRADED (WEAR)"; 
                notes += (notes.empty() ? "" : " | ") + std::string("High Percentage Used");
            } else if ((tech == "CMR" || tech == "SMR") && sd.loadCycles > 300000) {
                healthStatus = "DEGRADED (WEAR)"; 
                notes += (notes.empty() ? "" : " | ") + std::string("High Load Cycle Count");
            }
        }

        auto toVal = [&](long v) { return (v < 0) ? "\"N/A\"" : std::to_string(v); };
        auto toTB = [&](long long u) -> std::string { 
            if (u < 0) return "\"N/A\"";
            double tb = (u * 512000.0) / 1099511627776.0;
            std::stringstream ss; ss << std::fixed << std::setprecision(2) << tb << " TB";
            return "\"" + ss.str() + "\"";
        };

        if (!first) std::cout << ",\n";
        std::cout << "    {\n"
            << "        \"Letter\": \"" << letter << "\",\n"
            << "        \"FsType\": \"" << fs << "\",\n"
            << "        \"TotalGB\": " << std::fixed << std::setprecision(2) << totGB << ",\n"
            << "        \"UsedGB\": " << (freeGB >= 0 ? (totGB - freeGB) : 0.00) << ",\n"
            << "        \"FreeGB\": " << (freeGB >= 0 ? freeGB : 0.00) << ",\n"
            << "        \"PercentUsed\": " << (freeGB >= 0 ? ((totGB - freeGB) / totGB) * 100 : 0) << ",\n"
            << "        \"Model\": \"" << modelStr << "\",\n"
            << "        \"Serial\": \"" << (sd.serial.empty() ? ExtractSerialFromPnP(bstrToStr(_bstr_t(vPnP))) : sd.serial) << "\",\n"
            << "        \"Interface\": \"" << interfaceStr << "\",\n"
            << "        \"LinuxDevice\": \"/dev/pd" << idx << "\",\n"
            << "        \"Family\": \"" << finalFamily << "\",\n"
            << "        \"Technology\": \"" << tech << "\",\n"
            << "        \"Form_Factor\": \"" << finalForm << "\",\n"
            << "        \"Stress_Profile\": \"" << profile << "\",\n"
            << "        \"RPM\": \"" << (sd.rpm > 0 ? std::to_string(sd.rpm) : "N/A") << "\",\n"
            << "        \"Health\": \"" << healthStatus << "\",\n"
            << "        \"Hours\": " << toVal(sd.hours) << ",\n"
            << "        \"Temp\": \"" << (sd.temp >= 0 ? std::to_string(sd.temp) : "N/A") << "\",\n"
            << "        \"Load_Cycles\": " << toVal(sd.loadCycles) << ",\n"
            << "        \"Total_Data_Written\": " << toTB(sd.dataUnitsWritten) << ",\n"
            << "        \"Reallocated_Sectors_Ct\": " << toVal(sd.reallocated) << ",\n"
            << "        \"Current_Pending_Sector\": " << toVal(sd.pending) << ",\n"
            << "        \"Offline_Uncorrectable\": " << toVal(sd.offlineUnc) << ",\n"
            << "        \"Notes\": \"" << notes << "\",\n"
            << "        \"Timestamp\": \"" << tB << "\"\n"
            << "    }";
        first = false; 
        pDisk->Release();
    }
    
    std::cout << "\n]" << std::endl;
    pEnum->Release(); pSvc->Release(); pLoc->Release(); CoUninitialize();
    return 0;
}