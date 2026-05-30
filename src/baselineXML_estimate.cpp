/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: baselineXML_estimate.cpp
   Version: 2.1
   Author: sussjb99
   Last Modified: 2026-05-10
   
   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.


   Purpose: Create an estimate of time required to re-create 
            the baseline.xml file using theme-driven colors.
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
        // Loading TitleColor for headers and MenuColor for notices
        GetPrivateProfileStringW(L"UI", L"TitleColor", L"Cyan", buf, 64, ini.c_str());
        Title = MapStringToAnsi(buf, L"\033[36m");
        
        GetPrivateProfileStringW(L"UI", L"MenuColor", L"Gold", buf, 64, ini.c_str());
        Menu = MapStringToAnsi(buf, L"\033[33m");
    }
}

// ------------------------------------------------------------
// Strict INI numeric reader
// ------------------------------------------------------------
bool ReadIniDouble(const wchar_t* section,
                   const wchar_t* key,
                   const std::wstring& path,
                   double& outValue)
{
    wchar_t buf[64];
    buf[0] = L'\0';

    GetPrivateProfileStringW(
        section,
        key,
        L"",
        buf,
        64,
        path.c_str()
    );

    if (buf[0] == L'\0') {
        std::wcerr << L"[ERROR] Missing required key in config.ini: ["
                   << section << L"] " << key << L"\n";
        return false;
    }

    try {
        outValue = std::stod(buf);
    } catch (...) {
        std::wcerr << L"[ERROR] Invalid numeric value in config.ini: ["
                   << section << L"] " << key << L" = " << buf << L"\n";
        return false;
    }

    return true;
}

// ------------------------------------------------------------
// Lightweight filesystem scan
// ------------------------------------------------------------
struct ScanResult {
    unsigned __int64 totalBytes = 0;
    unsigned int fileCount = 0;
};

void CalculateSize(std::wstring path, ScanResult& result)
{
    if (path.empty()) return;
    if (path.back() != L'\\') path += L'\\';

    WIN32_FIND_DATAW fd;
    std::wstring searchPath = path + L"*";

    HANDLE hFind = FindFirstFileExW(
        searchPath.c_str(),
        FindExInfoBasic,
        &fd,
        FindExSearchNameMatch,
        nullptr,
        0
    );

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..")
            continue;

        if (_wcsicmp(name.c_str(), L"integrity_check") == 0 ||
            _wcsicmp(name.c_str(), L"System Volume Information") == 0 ||
            _wcsicmp(name.c_str(), L"$RECYCLE.BIN") == 0)
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CalculateSize(path + name, result);
        } else {
            result.totalBytes +=
                ((unsigned __int64)fd.nFileSizeHigh << 32) |
                (unsigned __int64)fd.nFileSizeLow;
            result.fileCount++;
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int wmain(int argc, wchar_t* argv[])
{
    if (argc != 2) {
        std::wcout << L"Usage: baselineXML_estimate.exe <DriveLetter>\n";
        return 1;
    }

    wchar_t dl = towupper(argv[1][0]);
    if (dl < L'A' || dl > L'Z') {
        std::wcerr << L"[ERROR] Invalid drive letter.\n";
        return 1;
    }

    if (dl == L'C') {
        std::wcerr << L"[CRITICAL] Operations on C: are prohibited.\n";
        return 1;
    }

    std::wstring driveRoot = std::wstring(1, dl) + L":\\";

    wchar_t exePathBuf[MAX_PATH];
    GetModuleFileNameW(nullptr, exePathBuf, MAX_PATH);

    std::wstring fullExePath = exePathBuf;
    size_t pos = fullExePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        std::wcerr << L"[ERROR] Unable to determine executable path.\n";
        return 1;
    }

    std::wstring binDir = fullExePath.substr(0, pos);
    pos = binDir.find_last_of(L"\\/");
    if (pos == std::wstring::npos) {
        std::wcerr << L"[ERROR] Unable to determine suite root.\n";
        return 1;
    }

    std::wstring suiteRoot = binDir.substr(0, pos + 1);
    std::wstring configIni = suiteRoot + L"config\\config.ini";

    DWORD attr = GetFileAttributesW(configIni.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
    {
        std::wcerr << L"[ERROR] config.ini not found at: " << configIni << L"\n";
        return 1;
    }

    // --- Initialize Colors ---
    Color::Load(configIni);

    double seqReadMBps = 0.0, smallFileEfficiency = 0.0, accessLatencyMS = 0.0;
    double busLimitMBps = 0.0, md5RateMBps = 0.0, xmlRateMBps = 0.0;
    double perFileOverheadMS = 0.0, baselinePenalty = 0.0, parityPenalty = 0.0;
    double scanTimeTuning = 0.0, minRecoverySeconds = 0.0;
    double fsTotalFiles = 0.0, fsTotalBytes = 0.0, fsSmallFileRatio = 0.0;

    if (!ReadIniDouble(L"Performance", L"MeasuredSequentialReadMBps",    configIni, seqReadMBps)         ||
        !ReadIniDouble(L"Performance", L"SmallFileEfficiency",           configIni, smallFileEfficiency) ||
        !ReadIniDouble(L"Performance", L"MeasuredAccessLatencyMS",       configIni, accessLatencyMS)     ||
        !ReadIniDouble(L"BusCharacteristics", L"BusSaturationLimitMBps", configIni, busLimitMBps)        ||
        !ReadIniDouble(L"Compute", L"MD5_ProcessingRate_MBps",           configIni, md5RateMBps)         ||
        !ReadIniDouble(L"Compute", L"XML_SerializationRate_MBps",        configIni, xmlRateMBps)         ||
        !ReadIniDouble(L"Tuning", L"PerFileOverheadMS",                  configIni, perFileOverheadMS)   ||
        !ReadIniDouble(L"Tuning", L"BaselineReadPenalty",                configIni, baselinePenalty)     ||
        !ReadIniDouble(L"Tuning", L"ParityPenalty",                      configIni, parityPenalty)       ||
        !ReadIniDouble(L"Tuning", L"ScanTimeEstimateTuning",              configIni, scanTimeTuning)      ||
        !ReadIniDouble(L"Tuning", L"MinRecoverySeconds",                 configIni, minRecoverySeconds)  ||
        !ReadIniDouble(L"FileSystemProfile", L"TotalFiles",              configIni, fsTotalFiles)        ||
        !ReadIniDouble(L"FileSystemProfile", L"TotalBytes",              configIni, fsTotalBytes)        ||
        !ReadIniDouble(L"FileSystemProfile", L"SmallFileRatio",          configIni, fsSmallFileRatio))
    {
        return 1;
    }

    ScanResult scan;
    CalculateSize(driveRoot, scan);

    bool useProfile = false;
    if (fsTotalFiles > 0.0 && fsTotalBytes > 0.0 && scan.fileCount > 0 && scan.totalBytes > 0)
    {
        double diffFiles = std::abs((double)scan.fileCount - fsTotalFiles) / fsTotalFiles;
        double diffBytes = std::abs((double)scan.totalBytes - fsTotalBytes) / fsTotalBytes;
        if (diffFiles < 0.02 && diffBytes < 0.02) useProfile = true;
    }

    unsigned __int64 totalBytes = useProfile ? (unsigned __int64)fsTotalBytes : scan.totalBytes;
    unsigned int fileCount = useProfile ? (unsigned int)fsTotalFiles : scan.fileCount;
    double smallRatio = useProfile ? fsSmallFileRatio : smallFileEfficiency;
    double totalMB = (double)totalBytes / (1024.0 * 1024.0);

    double inventorySeconds = (fileCount * (perFileOverheadMS / 1000.0)) * scanTimeTuning;
    double penalty = baselinePenalty + (parityPenalty * smallRatio);
    if (penalty > 0.90) penalty = 0.90;

    double effectiveReadMBps = seqReadMBps * (1.0 - penalty);
    if (effectiveReadMBps < 1.0) effectiveReadMBps = 1.0;

    double effectiveSpeed = effectiveReadMBps;
    if (busLimitMBps > 0.0 && busLimitMBps < effectiveSpeed) effectiveSpeed = busLimitMBps;
    if (md5RateMBps > 0.0 && md5RateMBps < effectiveSpeed)   effectiveSpeed = md5RateMBps;
    if (xmlRateMBps > 0.0 && xmlRateMBps < effectiveSpeed)   effectiveSpeed = xmlRateMBps;
    if (effectiveSpeed < 1.0) effectiveSpeed = 1.0;

    double hashingSeconds = (totalMB > 0.0) ? (totalMB / effectiveSpeed) : 0.0;
    double totalSeconds = inventorySeconds + hashingSeconds;
    if (totalSeconds < minRecoverySeconds) totalSeconds = minRecoverySeconds;

    unsigned int displayMinutes = (unsigned int)(totalSeconds / 60.0) + 1;

    // --- Colored Output ---
    std::wcout << L"\n" << Color::Title << L"--- Baseline Rebuild Estimate ---" << Color::Reset << L"\n";

    if (useProfile) {
        std::wcout << Color::Menu << L" Using calibrated filesystem profile \n - no significant changes detected" << Color::Reset << L"\n";
    } else {
        std::wcout << Color::Menu << L"(Filesystem changed - using fresh scan results)" << Color::Reset << L"\n";
    }

    std::wcout << L"Estimated time to build baseline.xml: ~"
               << Color::Title << displayMinutes << Color::Reset << L" minutes\n";

    return 0;
}