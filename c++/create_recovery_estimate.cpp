/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: create_recovery_estimate.cpp
   Author: sussjb99
   Version: 2.2 (Theme-Driven)
   Last Modified: 2026-05-10

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose: Provide estimate on the amount of time expected
            to complete a recovery process.
   ============================================================ */

#include <windows.h>
#include <iostream>
#include <string>
#include <cmath>

// ------------------------------------------------------------
// Color Mapping Logic
// ------------------------------------------------------------
namespace Color {
    std::wstring Title  = L"\033[36m"; // Default Cyan
    std::wstring Menu   = L"\033[33m"; // Default Gold/Yellow
    std::wstring Reset  = L"\033[0m";

    std::wstring MapStringToAnsi(const std::wstring& name, const std::wstring& def) {
        if (name == L"Cyan")   return L"\033[36m";
        if (name == L"Green")  return L"\033[32m";
        if (name == L"Yellow") return L"\033[33m";
        if (name == L"Gold")   return L"\033[33m";
        if (name == L"White")  return L"\033[37m";
        if (name == L"Blue")   return L"\033[34m";
        if (name == L"Purple") return L"\033[35m";
        return def;
    }

    void Load(const std::wstring& ini) {
        wchar_t buf[64];
        GetPrivateProfileStringW(L"UI", L"TitleColor", L"Cyan", buf, 64, ini.c_str());
        Title = MapStringToAnsi(buf, L"\033[36m");
        
        GetPrivateProfileStringW(L"UI", L"MenuColor", L"Gold", buf, 64, ini.c_str());
        Menu = MapStringToAnsi(buf, L"\033[33m");
    }
}

bool ReadIniDouble(const wchar_t* section, const wchar_t* key, const std::wstring& path, double& outValue) {
    wchar_t buf[64];
    GetPrivateProfileStringW(section, key, L"", buf, 64, path.c_str());
    if (buf[0] == L'\0') return false;
    try { outValue = std::stod(buf); return true; } 
    catch (...) { return false; }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        std::wcout << L"Usage: recovery_estimate.exe <DriveLetter>\n";
        return 1;
    }

    wchar_t dl = towupper(argv[1][0]);
    if (dl == L'C') {
        std::wcerr << L"[CRITICAL] Operations on C: are prohibited.\n";
        return 1;
    }

    // --- Path Logic ---
    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);
    std::wstring fullExePath = exePathBuf;
    size_t pos = fullExePath.find_last_of(L"\\/");
    std::wstring binDir = fullExePath.substr(0, pos);
    pos = binDir.find_last_of(L"\\/");
    std::wstring suiteRoot = binDir.substr(0, pos + 1);
    std::wstring configIni = suiteRoot + L"config\\config.ini";

    // Initialize Theme Colors from INI
    Color::Load(configIni);

    // --- Parameter Extraction (Using original keys) ---
    double driveType = 0.0, seqReadMBps = 0.0, sustainedWriteMBps = 0.0, perFileOverheadMS = 0.0;
    double fsTotalFiles = 0.0, fsTotalBytes = 0.0, smallEfficiency = 0.0, protocolFactor = 0.0;
    double parityPenalty = 0.0, hddParityPenalty = 0.0, md5Rate = 0.0;

    ReadIniDouble(L"Performance", L"DriveType", configIni, driveType);
    ReadIniDouble(L"Performance", L"MeasuredSequentialReadMBps", configIni, seqReadMBps);
    ReadIniDouble(L"Performance", L"MeasuredSustainedWriteMBps", configIni, sustainedWriteMBps);
    ReadIniDouble(L"Performance", L"SmallFileEfficiency", configIni, smallEfficiency);
    ReadIniDouble(L"Compute", L"MD5_ProcessingRate_MBps", configIni, md5Rate);
    ReadIniDouble(L"Tuning", L"ParityPenalty", configIni, parityPenalty);
    ReadIniDouble(L"Tuning", L"HDD_ParityPenalty", configIni, hddParityPenalty);
    ReadIniDouble(L"Tuning", L"PerFileOverheadMS", configIni, perFileOverheadMS);
    ReadIniDouble(L"FileSystemProfile", L"TotalFiles", configIni, fsTotalFiles);
    ReadIniDouble(L"FileSystemProfile", L"TotalBytes", configIni, fsTotalBytes);
    ReadIniDouble(L"BusCharacteristics", L"ProtocolOverheadFactor", configIni, protocolFactor);

    // --- Final Tally Logic ---
    double totalMB = fsTotalBytes / (1024.0 * 1024.0);
    double activePenalty = (driveType == 1.0) ? parityPenalty : hddParityPenalty;
    double par2ComputeRate = md5Rate / 1.8; 
    double diskSpeed = 0.0;

    if (driveType == 1.0) {
        diskSpeed = seqReadMBps * (smallEfficiency * 10.5); 
    } else {
        diskSpeed = seqReadMBps * 0.31; 
    }

    double effectiveSpeed = (par2ComputeRate < diskSpeed) ? par2ComputeRate : diskSpeed;
    effectiveSpeed *= (1.0 - activePenalty);
    if (effectiveSpeed < 2.0) effectiveSpeed = 2.0;

    double processSeconds = totalMB / effectiveSpeed;

    double chunkCount = std::ceil(fsTotalFiles / 25000.0);
    double chunkWaitSeconds = chunkCount * 12.0; 
    double inventorySeconds = (fsTotalFiles * perFileOverheadMS) / 1000.0;

    double recoveryMB = totalMB * 0.10;
    double writeSeconds = recoveryMB / (sustainedWriteMBps > 0 ? sustainedWriteMBps : 15.0);

    double totalSeconds = (processSeconds + chunkWaitSeconds + inventorySeconds + writeSeconds) * (1.0 + protocolFactor);
    unsigned int displayMinutes = (unsigned int)(totalSeconds / 60.0) + 1;

    // --- Themed Output ---
    std::wcout << L"\n" << Color::Title << L"--- Recovery Creation Estimate for Drive " << dl << L": ---" << Color::Reset << L"\n";
    std::wcout << L"Estimated time to complete: ~" << Color::Title << displayMinutes << Color::Reset << L" minutes\n";
    
    // Additional detail line in MenuColor
    std::wcout << Color::Menu << L"(Redundancy target: 10% for " << (unsigned int)totalMB << L" MB)" << Color::Reset << L"\n";

    return 0;
}