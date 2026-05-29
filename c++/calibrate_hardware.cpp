/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: calibrate_hardware.cpp
   Version: 2.8.0 (Telemetry Isolation Clean)
   Author: sussjb99
   Last Modified: 2026-05-26

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.
   
   Purpose: Test Hardware and set parameters used for creating 
            Estimates for how long tasks will take. Also collects
            telemetry information exclusively written to config.ini.
   ============================================================ */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <winioctl.h>
#include <algorithm>

using namespace std;

// --- STRUCTURES ---
struct UI {
    wstring Title;
    wstring Label;
    wstring Value;
    wstring Success;
    wstring Warning;
    wstring Error;
    wstring Info;
    wstring Reset = L"\033[0m";
};

struct DriveMetrics {
    double  DriveType;      
    wstring Interface;      
    DWORD   ClusterSize;
    bool    IsOnBattery;
};

struct FileSystemStats {
    unsigned __int64 totalBytes = 0;
    unsigned __int64 smallFiles = 0;
    unsigned int totalFiles = 0;
    unsigned int totalDirs = 0;
    unsigned int maxDepth = 0;
};

// --- CONSTANTS ---
const size_t SEQ_BUFFER_SIZE = 1024 * 1024;
const size_t RND_BUFFER_SIZE = 4096;
const int TEST_DURATION_MS   = 20000;
const int DISCARD_MS         = 5000;
const unsigned __int64 SMALL_FILE_THRESHOLD = 64ULL * 1024ULL;

// --- UTILITIES & HELPERS ---

wstring GetANSI(wstring name) {
    if (name == L"Cyan")   return L"\033[36m";
    if (name == L"Green")  return L"\033[32m";
    if (name == L"Yellow" || name == L"Gold") return L"\033[33m";
    if (name == L"Red")    return L"\033[31m";
    if (name == L"Gray")   return L"\033[90m";
    if (name == L"Blue")   return L"\033[34m";
    if (name == L"Purple") return L"\033[35m";
    return L"\033[37m"; 
}

void EnableVirtualTerminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
}

wstring FormatDouble(double v, int decimals) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%.*f", max(0, min(10, decimals)), v);
    return wstring(buf);
}

UI LoadUI(const wstring& ini) {
    UI ui;
    wchar_t b[32];
    auto GetCol = [&](const wchar_t* key, const wchar_t* def) {
        GetPrivateProfileStringW(L"UI", key, def, b, 32, ini.c_str());
        return GetANSI(b);
    };
    ui.Title   = GetCol(L"TitleColor", L"Cyan");
    ui.Label   = GetCol(L"LabelColor", L"Green");
    ui.Value   = GetCol(L"ValueColor", L"White");
    ui.Success = GetCol(L"StatusOKColor", L"Green");
    ui.Warning = GetCol(L"WarningColor", L"Yellow");
    ui.Error   = GetCol(L"ErrorColor", L"Red");
    ui.Info    = GetCol(L"InfoColor", L"Gray");
    return ui;
}

void WriteIni(const wstring& sect, const wstring& key, double val, const wstring& path, int decimals = 3) {
    WritePrivateProfileStringW(sect.c_str(), key.c_str(), FormatDouble(val, decimals).c_str(), path.c_str());
}

void WriteIniStr(const wstring& sect, const wstring& key, const wstring& val, const wstring& path) {
    WritePrivateProfileStringW(sect.c_str(), key.c_str(), val.c_str(), path.c_str());
}

// --- HARDWARE DETECTION IMPLEMENTATIONS ---

DriveMetrics GetAdvancedMetrics(const wstring& driveRoot) {
    DriveMetrics m = { 1.0, L"Unknown", 4096, false };
    SYSTEM_POWER_STATUS sps;
    if (GetSystemPowerStatus(&sps)) m.IsOnBattery = (sps.ACLineStatus == 0);

    DWORD spc, bps, fc, tc;
    if (GetDiskFreeSpaceW(driveRoot.c_str(), &spc, &bps, &fc, &tc)) m.ClusterSize = spc * bps;

    wstring volume = L"\\\\.\\" + driveRoot.substr(0, 2);
    HANDLE h = CreateFileW(volume.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (h != INVALID_HANDLE_VALUE) {
        STORAGE_PROPERTY_QUERY q = { StorageDeviceProperty, PropertyStandardQuery };
        STORAGE_DEVICE_DESCRIPTOR d = { 0 };
        DWORD b;
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof(q), &d, sizeof(d), &b, NULL)) {
            if (d.BusType == BusTypeUsb) m.Interface = d.RemovableMedia ? L"USB_FLASH_STICK" : L"USB_EXTERNAL_DRIVE";
            else if (d.BusType == BusTypeNvme) { m.DriveType = 2.0; m.Interface = L"NVME_INTERNAL"; }
            else m.Interface = L"SATA_INTERNAL";
        }
        STORAGE_PROPERTY_QUERY sq = { StorageDeviceSeekPenaltyProperty, PropertyStandardQuery };
        DEVICE_SEEK_PENALTY_DESCRIPTOR sd = { 0 };
        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &sq, sizeof(sq), &sd, sizeof(sd), &b, NULL)) {
            if (sd.IncursSeekPenalty) m.DriveType = 0.0;
        }
        CloseHandle(h);
    }
    return m;
}

double MeasureComputeRate() {
    const size_t testSize = 128 * 1024 * 1024;
    void* buffer = malloc(testSize);
    if (!buffer) return 150.0;
    memset(buffer, 0xCC, testSize);
    DWORD start = GetTickCount();
    volatile BYTE hash = 0;
    for (size_t i = 0; i < testSize; i++) hash ^= ((BYTE*)buffer)[i] + (BYTE)(i % 255);
    DWORD duration = GetTickCount() - start;
    free(buffer);
    return (duration == 0) ? 1000.0 : (double)(testSize / (1024.0 * 1024.0)) / (duration / 1000.0);
}

double RunSustainedTest(const wstring& path, bool isWrite) {
    void* buffer = _aligned_malloc(SEQ_BUFFER_SIZE, 4096);
    if (!buffer) return 0.0;
    memset(buffer, 0xAF, SEQ_BUFFER_SIZE);
    HANDLE hFile = CreateFileW(path.c_str(), isWrite ? GENERIC_WRITE : GENERIC_READ, 0, NULL,
                               isWrite ? CREATE_ALWAYS : OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { _aligned_free(buffer); return 0.0; }
    DWORD bytesProcessed = 0;
    unsigned __int64 totalBytes = 0;
    DWORD start = GetTickCount();
    while ((GetTickCount() - start) < (DWORD)TEST_DURATION_MS) {
        if (isWrite) WriteFile(hFile, buffer, (DWORD)SEQ_BUFFER_SIZE, &bytesProcessed, NULL);
        else if (!ReadFile(hFile, buffer, (DWORD)SEQ_BUFFER_SIZE, &bytesProcessed, NULL) || bytesProcessed == 0) SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
        if ((GetTickCount() - start) > (DWORD)DISCARD_MS) totalBytes += bytesProcessed;
    }
    CloseHandle(hFile);
    _aligned_free(buffer);
    return (double)totalBytes / (1024.0 * 1024.0) / ((TEST_DURATION_MS - DISCARD_MS) / 1000.0);
}

double RunRandomTest(const wstring& path) {
    void* buffer = _aligned_malloc(RND_BUFFER_SIZE, 4096);
    if (!buffer) return 0.0;
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { _aligned_free(buffer); return 0.0; }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= (LONGLONG)RND_BUFFER_SIZE) { CloseHandle(hFile); _aligned_free(buffer); return 0.0; }
    DWORD start = GetTickCount();
    int iops = 0;
    while ((GetTickCount() - start) < 10000) {
        LARGE_INTEGER offset;
        offset.QuadPart = (rand() % (max<LONGLONG>(1, fileSize.QuadPart / RND_BUFFER_SIZE))) * RND_BUFFER_SIZE;
        SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN);
        DWORD read = 0;
        ReadFile(hFile, buffer, (DWORD)RND_BUFFER_SIZE, &read, NULL);
        iops++;
    }
    CloseHandle(hFile);
    _aligned_free(buffer);
    return (double)iops / 10.0;
}

void ScanDirectoryFast(const wstring& path, FileSystemStats& stats, const vector<wstring>& excludes, unsigned int depth) {
    if (depth > stats.maxDepth) stats.maxDepth = depth;
    wstring searchPath = path + (path.back() != L'\\' ? L"\\*" : L"*");
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(searchPath.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, 0);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        bool skip = false;
        for (const auto& ex : excludes) if (_wcsicmp(name.c_str(), ex.c_str()) == 0) { skip = true; break; }
        if (skip) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            stats.totalDirs++;
            ScanDirectoryFast(path + (path.back() != L'\\' ? L"\\" : L"") + name, stats, excludes, depth + 1);
        } else {
            stats.totalFiles++;
            unsigned __int64 size = ((unsigned __int64)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            stats.totalBytes += size;
            if (size <= SMALL_FILE_THRESHOLD) stats.smallFiles++;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

// --- MAIN ENTRY POINT ---

int wmain(int argc, wchar_t* argv[]) {
    EnableVirtualTerminal();
    if (argc < 2) { wcout << L"Usage: calibrate_hardware.exe <DriveLetter>\n"; return 1; }
    wstring root = argv[1];
    if (root.length() == 1) root += L":\\";
    else if (root.length() == 2 && root[1] == L':') root += L'\\';

    wchar_t exeBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exeBuf, MAX_PATH);
    wstring binDir = wstring(exeBuf).substr(0, wstring(exeBuf).find_last_of(L"\\/"));
    wstring iniPath = binDir + L"\\..\\config\\config.ini";
    wstring dataDir = binDir + L"\\..\\data\\";

    UI ui = LoadUI(iniPath);
    if (towupper(root[0]) == L'C') { 
        wcerr << ui.Error << L"[CRITICAL] " << ui.Reset << L"Stress tests on C: are prohibited.\n"; 
        return 3; 
    }

    // STEP 1: Hardware Detection
    wcout << L"\n" << ui.Title << L"Step 1: Detecting hardware characteristics..." << ui.Reset << L"\n";
    DriveMetrics metrics = GetAdvancedMetrics(root);
    wcout << ui.Info << L"Interface: " << ui.Reset << metrics.Interface 
          << L" | Type: " << ui.Value << metrics.DriveType << ui.Reset << L"\n"; 

    CreateDirectoryW(dataDir.c_str(), NULL);
    wstring testFile = dataDir + L"temp_calib_stress.bin";

    // STEP 2: Sustained Tests
    wcout << L"\n" << ui.Title << L"Step 2: Running sustained throughput tests..." << ui.Reset << L"\n";
    wcout << ui.Label << L"  Writing... " << ui.Reset << flush;
    double writeSpeed = RunSustainedTest(testFile, true);
    wcout << ui.Success << L"Done (" << ui.Reset << FormatDouble(writeSpeed, 1) << L" MB/s" << ui.Success << L")" << ui.Reset << L"\n";
    
    wcout << ui.Label << L"  Reading... " << ui.Reset << flush;
    double readSpeed = RunSustainedTest(testFile, false);
    wcout << ui.Success << L"Done (" << ui.Reset << FormatDouble(readSpeed, 1) << L" MB/s" << ui.Success << L")" << ui.Reset << L"\n";

    // STEP 3: Latency & CPU
    wcout << L"\n" << ui.Title << L"Step 3: Measuring Latency and CPU Compute Rate..." << ui.Reset << L"\n";
    double iops = RunRandomTest(testFile);
    double latency = (iops > 0.0) ? (1000.0 / iops) : 0.0;
    double cpuRate = MeasureComputeRate();
    wcout << ui.Info << L"Latency: " << ui.Reset << FormatDouble(latency, 2) << L" ms | " 
          << ui.Info << L"Compute: " << ui.Reset << FormatDouble(cpuRate, 0) << L" MB/s\n";

    // STEP 4: Filesystem Scan
    wcout << L"\n" << ui.Title << L"Step 4: Scanning filesystem structure..." << ui.Reset << L"\n";
    FileSystemStats fs;
    ScanDirectoryFast(root, fs, {L"Integrity_Check", L"System Volume Information", L"$RECYCLE.BIN"}, 0);
    wcout << ui.Info << L"Found: " << ui.Reset << fs.totalFiles << L" files (" << (fs.totalBytes / 1024 / 1024) << L" MB)\n";

    // STEP 5: Full Parameter Sync to INI
    wcout << L"\n" << ui.Title << L"Step 5: Updating all configuration parameters inside config.ini..." << ui.Reset << L"\n";
    double rndEff = (readSpeed > 0 ? ((iops * 4.0 / 1024.0) / readSpeed) : 0.01);
    bool isHDD = (metrics.DriveType == 0.0);

    WriteIni(L"Performance", L"DriveType", metrics.DriveType, iniPath, 0);
    WriteIni(L"Performance", L"MeasuredSequentialReadMBps", readSpeed, iniPath, 1);
    WriteIni(L"Performance", L"MeasuredSustainedReadMBps", readSpeed * 0.92, iniPath, 1);
    WriteIni(L"Performance", L"MeasuredRandomReadIOPS", iops, iniPath, 1);
    WriteIni(L"Performance", L"MeasuredAccessLatencyMS", latency, iniPath, 2);
    WriteIni(L"Performance", L"SmallFileEfficiency", rndEff, iniPath, 3);
    WriteIni(L"Performance", L"MeasuredSustainedWriteMBps", writeSpeed, iniPath, 1);

    double busLimit = (metrics.Interface.find(L"NVME") != wstring::npos) ? 3500.0 : (metrics.Interface.find(L"USB") != wstring::npos ? 480.0 : 550.0);
    WriteIniStr(L"BusCharacteristics", L"InterfaceType", metrics.Interface, iniPath);
    WriteIni(L"BusCharacteristics", L"MeasuredBusLatencyMS", (metrics.Interface.find(L"USB") != wstring::npos ? 1.2 : 0.1), iniPath, 2);
    WriteIni(L"BusCharacteristics", L"ProtocolOverheadFactor", (metrics.Interface.find(L"USB") != wstring::npos ? 0.12 : 0.05), iniPath, 2);
    WriteIni(L"BusCharacteristics", L"BusSaturationLimitMBps", busLimit, iniPath, 1);
    WriteIni(L"BusCharacteristics", L"ClusterSize", (double)metrics.ClusterSize, iniPath, 0);

    WriteIni(L"Compute", L"MD5_ProcessingRate_MBps", cpuRate, iniPath, 1);
    WriteIni(L"Compute", L"XML_SerializationRate_MBps", cpuRate * 0.45, iniPath, 1);
    WriteIni(L"Compute", L"ThreadEfficiencyFactor", 0.85, iniPath, 2);
    WriteIni(L"Compute", L"PowerThrottlingActive", metrics.IsOnBattery ? 1.0 : 0.0, iniPath, 0);

    // Tuning Multipliers
    WriteIni(L"Tuning", L"BaselineReadPenalty", isHDD ? 0.420 : 0.310, iniPath, 3);
    WriteIni(L"Tuning", L"ParityPenalty", isHDD ? 0.350 : 0.200, iniPath, 3);
    WriteIni(L"Tuning", L"HDD_BaselinePenalty", 0.450, iniPath, 3);
    WriteIni(L"Tuning", L"HDD_ParityPenalty", 0.380, iniPath, 3);
    WriteIni(L"Tuning", L"MinRecoverySeconds", isHDD ? 600.0 : 300.0, iniPath, 0);
    WriteIni(L"Tuning", L"RecoveryFloorBase", isHDD ? 900.0 : 600.0, iniPath, 0);
    WriteIni(L"Tuning", L"RecoveryFloorMultiplier", 1.25, iniPath, 2);
    WriteIni(L"Tuning", L"PerFileOverheadMS", (latency * 0.5) + 4.5, iniPath, 2);
    WriteIni(L"Tuning", L"ScanTimeEstimateTuning", (readSpeed > 100 ? 1.0 : 1.2), iniPath, 3);

    // Profile statistics
    WriteIni(L"FileSystemProfile", L"TotalFiles", (double)fs.totalFiles, iniPath, 0);
    WriteIni(L"FileSystemProfile", L"TotalBytes", (double)fs.totalBytes, iniPath, 0);
    WriteIni(L"FileSystemProfile", L"AverageFileSize", (fs.totalFiles > 0 ? (double)fs.totalBytes / fs.totalFiles : 0), iniPath, 0);
    WriteIni(L"FileSystemProfile", L"SmallFileRatio", (double)fs.smallFiles / max(1u, fs.totalFiles), iniPath, 3);
    WriteIni(L"FileSystemProfile", L"DirectoryCount", (double)fs.totalDirs, iniPath, 0);
    WriteIni(L"FileSystemProfile", L"MaxDepth", (double)fs.maxDepth, iniPath, 0);

    DeleteFileW(testFile.c_str());
    wcout << L"\n" << ui.Success << L"Calibration Telemetry Sync Complete!" << ui.Reset << L"\n";
    return 0;
}