/* ============================================================
   PUDIS (Portable USB Drive Integrity Suite)
   File: create_recovery.cpp
   Author: sussjb99
   Version: 2.0
   Last Modified: 2026-04-12

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.


Purpose: Creates the par2 recovery set for file corruption recovery.
         Optimized for par2cmdline-turbo 1.4.0 with Space Guard
   ============================================================ */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

namespace Color {
    const wchar_t* Reset  = L"\033[0m";
    const wchar_t* Cyan   = L"\033[36m";
    const wchar_t* Yellow = L"\033[33m";
    const wchar_t* Green  = L"\033[32m";
    const wchar_t* Red    = L"\033[31m";
    const wchar_t* Gray   = L"\033[90m";
}

// Helper to get path relative to the EXE
std::wstring GetSuitePath(const std::wstring& subDir = L"") {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    s = s.substr(0, s.find_last_of(L"\\/")); // bin
    s = s.substr(0, s.find_last_of(L"\\/") + 1); // root/
    return s + subDir;
}

// --- RESTORED REQUIRED LOGIC: CLEANUP FUNCTION ---
void DeletePattern(const std::wstring& dir, const std::wstring& pattern) {
    std::wstring search = dir + (dir.back() == L'\\' ? L"" : L"\\") + pattern;
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring full = dir + (dir.back() == L'\\' ? L"" : L"\\") + ffd.cFileName;
            DeleteFileW(full.c_str());
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
}

std::wstring trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    return (start == std::wstring::npos) ? L"" : s.substr(start, s.find_last_not_of(L" \t\r\n") - start + 1);
}

std::map<std::wstring, std::map<std::wstring, std::wstring>> loadIni(const std::wstring& path) {
    std::wifstream file(path);
    std::map<std::wstring, std::map<std::wstring, std::wstring>> ini;
    std::wstring line, section;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        if (line.front() == L'[' && line.back() == L']') section = line.substr(1, line.size() - 2);
        else {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) ini[section][trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
        }
    }
    return ini;
}

bool RunPar2(const std::wstring& exe, const std::wstring& args, const std::wstring& dir, const std::wstring& log, bool enableLog) {
    std::wstring cmd = enableLog ? L"cmd.exe /c \"\"" + exe + L"\" " + args + L" > \"" + log + L"\" 2>&1\"" : L"\"" + exe + L"\" " + args;
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);

    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi)) return false;

    const wchar_t anim[] = { L'|', L'/', L'-', L'\\' };
    int i = 0;
    while (WaitForSingleObject(pi.hProcess, 150) == WAIT_TIMEOUT) {
        std::wcout << L"\r    " << Color::Yellow << L"Running PAR2... " << Color::Reset << anim[i++ % 4] << std::flush;
    }

    DWORD exit; GetExitCodeProcess(pi.hProcess, &exit);
    std::wcout << L"\r    " << (exit == 0 ? Color::Green : Color::Red) << L"Running PAR2... " << (exit == 0 ? L"Done!   \n" : L"Failed! \n");
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    return exit == 0;
}

int wmain(int argc, wchar_t* argv[]) {
    // Setup Console
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    if (argc < 2 || towupper(argv[1][0]) == L'C') {
        std::wcerr << Color::Red << L"Error: Invalid or Prohibited Drive.\n";
        return 1;
    }

    std::wstring driveRoot = std::wstring(1, towupper(argv[1][0])) + L":\\";
    auto ini = loadIni(GetSuitePath(L"config\\config.ini"));

    // Extract Basic Settings
    bool enableLog     = ini[L"Logging"][L"EnablePar2Logging"] == L"1";
    std::wstring bin   = GetSuitePath(ini[L"Paths"][L"Bin"] + L"\\");
    std::wstring data  = GetSuitePath(ini[L"Paths"][L"Data"] + L"\\");
    std::wstring logs  = GetSuitePath(ini[L"Paths"][L"Logs"] + L"\\");
    std::wstring par2  = bin + ini[L"Tools"][L"Par2"];

    // Extract PAR2 Engine Settings
    std::wstring threads    = ini[L"Par2Settings"][L"Threads"];
    std::wstring memory     = ini[L"Par2Settings"][L"MemoryLimit"];
    std::wstring bSize      = ini[L"Par2Settings"][L"BlockSize"];
    std::wstring redundancy = ini[L"Par2Settings"][L"Redundancy"];
    std::wstring parallel   = ini[L"Par2Settings"][L"ParallelHashing"];

    if (threads.empty())    threads    = L"0"; 
    if (memory.empty())     memory     = L"2048";
    if (bSize.empty())      bSize      = L"4194304";
    if (redundancy.empty()) redundancy = L"5";
    if (parallel.empty())   parallel   = L"4";

    CreateDirectoryW(data.c_str(), nullptr);
    CreateDirectoryW(logs.c_str(), nullptr);

    // --- RESTORED REQUIRED LOGIC: PRE-SCAN CLEANUP ---
    std::wcout << Color::Cyan << L"Step 1: Cleaning old recovery data..." << Color::Reset << L"\n";
    DeletePattern(data, L"*.par2");
    DeletePattern(data, L"files_part*.txt");

    // --- NEW LOGIC: SPACE GUARD (Using FileSystemProfile) ---
    try {
        long long totalBytes = std::stoll(ini[L"FileSystemProfile"][L"TotalBytes"]);
        double redRatio = std::stod(redundancy) / 100.0;
        long long totalFiles = std::stoll(ini[L"FileSystemProfile"][L"TotalFiles"]);
        
        // Estimate: (Data * Redundancy) + (1KB Metadata per file) + 512MB Buffer
        long long estimatedRequired = (long long)(totalBytes * redRatio) + (totalFiles * 1024);
        long long safetyBuffer = 512ULL * 1024 * 1024;

        ULARGE_INTEGER freeBytes;
        if (GetDiskFreeSpaceExW(driveRoot.c_str(), &freeBytes, nullptr, nullptr)) {
            if (freeBytes.QuadPart < (unsigned long long)(estimatedRequired + safetyBuffer)) {
                std::wcerr << L"\n" << Color::Red << L"CRITICAL ERROR: Insufficient Disk Space!" << Color::Reset << L"\n"
                           << L"Target Redundancy: " << redundancy << L"%\n"
                           << L"Estimated Needed:  " << (estimatedRequired / 1024 / 1024) << L" MB\n"
                           << L"Drive Available:   " << (freeBytes.QuadPart / 1024 / 1024) << L" MB\n"
                           << L"Operation Aborted to prevent drive saturation.\n";
                return 6;
            }
        }
    } catch (...) { /* Fallback if INI values are non-numeric */ }

    // Step 2: File List Extraction
    std::vector<std::wstring> files;
    std::wifstream xmlIn(data + L"baseline.xml");
    std::wstring line, open = L"<filename>", close = L"</filename>";
    while (std::getline(xmlIn, line)) {
        size_t s = line.find(open);
        //if (s != std::npos) files.push_back(line.substr(s + open.size(), line.find(close, s) - (s + open.size())));
		if (s != std::wstring::npos) files.push_back(line.substr(s + open.size(), line.find(close, s) - (s + open.size())));
    }

    if (files.empty()) return 3;

    // Step 3: Processing Loop (25,000 row chunks)
    const size_t chunkSize = 25000;
    for (size_t i = 0; i < files.size(); i += chunkSize) {
        size_t end = (i + chunkSize > files.size()) ? files.size() : i + chunkSize;
        std::vector<std::wstring> chunk(files.begin() + i, files.begin() + end);
        wchar_t pNum[8]; swprintf(pNum, 8, L"%02zu", (i / chunkSize) + 1);

        std::wstring listP = data + L"files_part" + pNum + L".txt";
        std::wstring recvP = data + L"recovery_data_part" + pNum + L".par2";
        std::wstring logP  = logs + L"par2_debug_part" + pNum + L".log";

        // Save file list for PAR2
        std::ofstream out(listP, std::ios::binary);
        for (const auto& f : chunk) {
            int sz = WideCharToMultiByte(CP_UTF8, 0, f.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string u8(sz, 0);
            WideCharToMultiByte(CP_UTF8, 0, f.c_str(), -1, &u8[0], sz, nullptr, nullptr);
            out << u8.c_str() << "\n";
        }
        out.close();

        std::wcout << L"\n" << Color::Cyan << L"Processing Set " << pNum << L" (" << chunk.size() << L" files)" << Color::Reset << L"\n";
        
        // Build dynamic arguments for Turbo-PAR2
        std::wstring args = L"c -t" + threads + 
                            L" -m" + memory + 
                            L" -r" + redundancy + 
                            L" -s" + bSize + 
                            L" -T" + parallel + 
                            L" -B. \"" + recvP + L"\" \"@" + listP + L"\"";
        
        if (!RunPar2(par2, args, driveRoot, logP, enableLog)) return 5;
    }

    return 0;
}