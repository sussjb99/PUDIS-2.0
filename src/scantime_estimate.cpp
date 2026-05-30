/* ============================================================
   PUDIS-2/0 (Portable USB Drive Integrity Suite)
   File: scantime_estimate.cpp
   Version: 2.0
   Author: sussjb99
   Last Modified: 2026-05-07

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.
   
   Purpose: Running surface scans can potentially
            consume a lot of time. Therefore, this program 
            estimates the amount of time it will take before 
            users pull the trigger.
			
   ============================================================ */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <iomanip>
#include <algorithm>
#include <cctype>

using namespace std;

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

// Trim whitespace
static inline string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Safe uppercase transform (no warnings)
static inline void toUpperString(string& s) {
    transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(toupper(c)); });
}

// Simple INI loader
bool loadIni(const string& path, map<string,string>& out) {
    ifstream f(path);
    if (!f.is_open()) return false;

    string line;
    while (getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[' && line.back() == ']') continue;

        size_t eq = line.find('=');
        if (eq == string::npos) continue;

        string key = trim(line.substr(0, eq));
        string val = trim(line.substr(eq + 1));
        if (!key.empty()) out[key] = val;
    }
    return true;
}

// Check if directory exists
bool directoryExists(const string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

// Get system drive letter
string getSystemDriveLetter() {
    char winPath[MAX_PATH];
    if (GetWindowsDirectoryA(winPath, MAX_PATH)) {
        if (winPath[1] == ':')
            return string(1, winPath[0]);
    }
    return "C";
}

// Check if drive letter is in BlockDriveLetters list
bool isBlockedByLetter(const string& driveLetter, const string& blockList) {
    string upperList = blockList;
    toUpperString(upperList);

    string dl = driveLetter;
    toUpperString(dl);

    string token;
    stringstream ss(upperList);
    while (getline(ss, token, ';')) {
        token = trim(token);
        if (!token.empty()) {
            toUpperString(token);
            if (token == dl)
                return true;
        }
    }
    return false;
}

// ------------------------------------------------------------
// main
// ------------------------------------------------------------
int main(int argc, char* argv[]) {

    // 1. Args
    if (argc < 3) {
        cout << "Usage: scantime_estimate.exe <Drive> <q|f>\n";
        return 1;
    }

    string driveLetter = argv[1];
    if (driveLetter.find(':') != string::npos)
        driveLetter = driveLetter.substr(0, driveLetter.find(':'));

    if (driveLetter.empty()) {
        cerr << "ERROR: Invalid drive.\n";
        return 1;
    }

    string mode = argv[2];
    toUpperString(mode);
    toUpperString(driveLetter);

    if (mode != "Q" && mode != "F") {
        cerr << "ERROR: Mode must be q or f.\n";
        return 1;
    }

    string rootPath = driveLetter + ":\\";

    // 2. Derive suite root from EXE path
    char exeBuf[MAX_PATH];
    if (!GetModuleFileNameA(NULL, exeBuf, MAX_PATH)) {
        cerr << "ERROR: Unable to determine executable path.\n";
        return 1;
    }
    string exePath = exeBuf;

    size_t pos = exePath.find_last_of("\\/");
    string exeDir = exePath.substr(0, pos);

    pos = exeDir.find_last_of("\\/");
    string suiteRoot = exeDir.substr(0, pos + 1);

    string configPath = suiteRoot + "config\\config.ini";

    // 3. Load config.ini
    map<string,string> ini;
    if (!loadIni(configPath, ini)) {
        cerr << "ERROR: Unable to load config.ini at: " << configPath << "\n";
        return 1;
    }

    // Required keys
    if (!ini.count("QuickSurfaceSamplePercent") ||
        !ini.count("FullSurfaceSamplePercent") ||
        !ini.count("AllowFullScanOnSSD") ||
        !ini.count("Root") ||
        !ini.count("Data"))
    {
        cerr << "ERROR: Missing required keys in config.ini.\n";
        return 1;
    }

    int quickPct        = stoi(ini["QuickSurfaceSamplePercent"]);
    int fullPct         = stoi(ini["FullSurfaceSamplePercent"]);
    int allowFullOnSSD  = stoi(ini["AllowFullScanOnSSD"]);

    string rootFolder = ini["Root"];
    string dataFolder = ini["Data"];

    // NEW: Optional tuning factor
    double tuningFactor = 1.0;
    if (ini.count("ScanTimeEstimateTuning")) {
        try {
            tuningFactor = stod(ini["ScanTimeEstimateTuning"]);
        } catch (...) {
            tuningFactor = 1.0;
        }
    }

    // Optional restrictions
    int blockSystemDrive = 0;
    if (ini.count("BlockSystemDrive")) {
        blockSystemDrive = stoi(ini["BlockSystemDrive"]);
    }
    string blockDriveLetters;
    if (ini.count("BlockDriveLetters")) {
        blockDriveLetters = ini["BlockDriveLetters"];
    }

    // 4. Eligibility checks
    string dlUpper = driveLetter;
    toUpperString(dlUpper);

    if (blockSystemDrive) {
        string sys = getSystemDriveLetter();
        toUpperString(sys);
        if (dlUpper == sys) {
            cerr << "ERROR: System drive " << dlUpper << ": is blocked.\n";
            return 1;
        }
    }

    if (!blockDriveLetters.empty() && isBlockedByLetter(dlUpper, blockDriveLetters)) {
        cerr << "ERROR: Drive " << dlUpper << ": is blocked.\n";
        return 1;
    }

    // Only block full scans on the system drive when AllowFullScanOnSSD=0
    if (mode == "F" && !allowFullOnSSD) {
        string sys = getSystemDriveLetter();
        toUpperString(sys);
        if (dlUpper == sys) {
            cerr << "ERROR: Full scans on the system drive are disabled.\n";
            return 1;
        }
    }

    // 5. Determine percentage
    int percentage = (mode == "Q") ? quickPct : fullPct;
    if (percentage <= 0 || percentage > 100) {
        cerr << "ERROR: Invalid scan percentage.\n";
        return 1;
    }

    // 6. Build test directory
    string testDir;
    testDir.reserve(128);
    testDir.push_back(dlUpper[0]);
    testDir.append(":\\");
    testDir.append(rootFolder);
    testDir.append("\\");
    testDir.append(dataFolder);
    testDir.append("\\");

    if (!directoryExists(testDir)) {
        cerr << "ERROR: Required directory missing: " << testDir << "\n";
        return 1;
    }

    string testFile = testDir + "estimate_temp.dat";

    // 7. Free space
    ULARGE_INTEGER freeBytes, totalBytes, tFree;
    if (!GetDiskFreeSpaceExA(rootPath.c_str(), &freeBytes, &totalBytes, &tFree)) {
        cerr << "ERROR: Drive not ready.\n";
        return 1;
    }

    // 8. Compute scan size
    const double CHUNK_SIZE_MB = 1024.0;
    double totalMBToScan = (freeBytes.QuadPart * (percentage / 100.0)) / (1024.0 * 1024.0);

    int totalChunks = static_cast<int>(totalMBToScan / CHUNK_SIZE_MB);
    if (totalChunks < 1 && totalMBToScan > 0) totalChunks = 1;

    // 9. Benchmark (128MB sprint)
    const size_t TEST_MB   = 128;
    const size_t TEST_SIZE = TEST_MB * 1024 * 1024;

    vector<char> buffer(TEST_SIZE, 'A');

    cout << "Benchmarking " << dlUpper << ": free space ("
         << fixed << setprecision(2) << totalMBToScan/1024.0 << " GB)..." << endl;

    LARGE_INTEGER s, e, f;
    QueryPerformanceFrequency(&f);

    // Write benchmark
    HANDLE hW = CreateFileA(
        testFile.c_str(),
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL
    );

    if (hW == INVALID_HANDLE_VALUE) {
        cerr << "ERROR: Cannot write test file in: " << testDir << "\n";
        return 1;
    }

    QueryPerformanceCounter(&s);
    DWORD bw = 0;
    WriteFile(hW, buffer.data(), (DWORD)TEST_SIZE, &bw, NULL);
    FlushFileBuffers(hW);
    QueryPerformanceCounter(&e);
    CloseHandle(hW);

    double wSpeed = (TEST_MB) / (double(e.QuadPart - s.QuadPart) / f.QuadPart);

    // Read benchmark
    HANDLE hR = CreateFileA(
        testFile.c_str(),
        GENERIC_READ,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        NULL
    );

    QueryPerformanceCounter(&s);
    DWORD br = 0;
    ReadFile(hR, buffer.data(), (DWORD)TEST_SIZE, &br, NULL);
    QueryPerformanceCounter(&e);
    CloseHandle(hR);
    DeleteFileA(testFile.c_str());

    double rSpeed = (TEST_MB) / (double(e.QuadPart - s.QuadPart) / f.QuadPart);

    // 10. Estimate time
    double timeTransfer   = ((totalMBToScan / wSpeed) + (totalMBToScan / rSpeed)) * 1.15;
    double timeProcessing = totalChunks * 4.0;
    double timeCommit     = totalChunks * 7.0;
    double timeInit       = 8.0;

    double totalSeconds = timeTransfer + timeProcessing + timeCommit + timeInit;

    // NEW: Apply tuning factor
    totalSeconds *= tuningFactor;

    int hh = (int)totalSeconds / 3600;
    int mm = ((int)totalSeconds % 3600) / 60;
    int ss = (int)totalSeconds % 60;

    // 11. Output
    cout << "------------------------------------------\n";
    cout << "Mode           : " << (mode == "Q" ? "Quick" : "Full") << "\n";
    cout << "Config Percent : " << percentage << " % of free space\n";
    cout << "Testable Space : " << fixed << setprecision(2) << (totalMBToScan / 1024.0) << " GB\n";
    cout << "Write Speed    : " << fixed << setprecision(1) << wSpeed << " MB/s\n";
    cout << "Read Speed     : " << fixed << setprecision(1) << rSpeed << " MB/s\n";
    cout << "Estimated Time : " << setfill('0') << setw(2) << hh << ":"
         << setw(2) << mm << ":" << setw(2) << ss << "\n";
    cout << "------------------------------------------\n";

    return 0;
}
