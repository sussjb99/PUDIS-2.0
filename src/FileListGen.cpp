/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: FileListGen.cpp
   Author: sussjb99
   Version: 2.7
   Last Modified: 2026-05-15
   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose:
     Generates a UTF‑8 file list for PAR2 processing.
     Adds:
       - Poison character detection (fatal)
       - Long path detection (warning + skip)
       - Logging to FileListGenErrors.log (config.ini driven)
       - All parameters driven by config.ini
       - Log path resolved relative to suite root (correct)
   ============================================================ */

#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "Shlwapi.lib")

// ------------------------------------------------------------
// Exclusion + Validation Rules
// ------------------------------------------------------------
struct Exclusions {
    std::vector<std::wstring> folders;
    std::vector<std::wstring> filePrefixes;
    std::wstring poisonChars;
    int maxPathLength = 260;
};

// ------------------------------------------------------------
// UTF‑8 encoder
// ------------------------------------------------------------
std::string utf8_encode(const std::wstring& wstr) {
    if (wstr.empty()) return {};

    int size_needed = WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), (int)wstr.size(),
        nullptr, 0,
        nullptr, nullptr
    );

    std::string result(size_needed, '\0');

    WideCharToMultiByte(
        CP_UTF8, 0,
        wstr.c_str(), (int)wstr.size(),
        &result[0], size_needed,
        nullptr, nullptr
    );

    return result;
}

// ------------------------------------------------------------
// Trim whitespace
// ------------------------------------------------------------
std::wstring trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ------------------------------------------------------------
// Split semicolon‑separated list
// ------------------------------------------------------------
std::vector<std::wstring> splitList(const std::wstring& s) {
    std::vector<std::wstring> result;
    size_t start = 0, pos;

    while ((pos = s.find(L';', start)) != std::wstring::npos) {
        result.push_back(trim(s.substr(start, pos - start)));
        start = pos + 1;
    }

    result.push_back(trim(s.substr(start)));
    return result;
}

// ------------------------------------------------------------
// Load exclusions + poison chars + max path from config.ini
// ------------------------------------------------------------
Exclusions loadExclusions(const std::wstring& configPath) {
    std::wifstream file(configPath);
    Exclusions ex;

    if (!file.is_open()) {
        std::wcerr << L"[ERROR] Unable to open config.ini at: " << configPath << L"\n";
        return ex;
    }

    std::wstring line;
    bool inSection = false;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == L';' || line[0] == L'#')
            continue;

        if (line == L"[Exclusions]") {
            inSection = true;
            continue;
        }

        if (line.size() > 0 && line[0] == L'[') {
            inSection = false;
            continue;
        }

        if (inSection) {
            size_t eq = line.find(L'=');
            if (eq == std::wstring::npos) continue;

            std::wstring key = trim(line.substr(0, eq));
            std::wstring val = trim(line.substr(eq + 1));

            if (_wcsicmp(key.c_str(), L"Folders") == 0) {
                ex.folders = splitList(val);
            }
            else if (_wcsicmp(key.c_str(), L"FilePrefixes") == 0) {
                ex.filePrefixes = splitList(val);
            }
            else if (_wcsicmp(key.c_str(), L"MaxPathLength") == 0) {
                try { ex.maxPathLength = std::stoi(val); }
                catch (...) { ex.maxPathLength = 260; }
            }
            else if (_wcsicmp(key.c_str(), L"PoisonChars") == 0) {
                ex.poisonChars = val;
            }
        }
    }

    return ex;
}

// ------------------------------------------------------------
// Resolve suite root = parent directory of config folder
// ------------------------------------------------------------
std::wstring getSuiteRoot(const std::wstring& configPath) {
    // configPath = I:\Integrity_Check\config\config.ini
    std::wstring configDir = configPath;

    // Remove "config.ini"
    size_t pos = configDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        configDir.erase(pos); // I:\Integrity_Check\config

    // Remove trailing "\config"
    pos = configDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
        configDir.erase(pos + 1); // I:\Integrity_Check\

    return configDir;
}

// ------------------------------------------------------------
// Resolve FileListGenErrors path relative to suite root
// ------------------------------------------------------------
std::wstring getLogPathFromConfig(const std::wstring& configPath) {
    wchar_t logBuf[MAX_PATH];

    GetPrivateProfileStringW(
        L"Paths", L"FileListGenErrors",
        L"logs\\FileListGenErrors.log",
        logBuf, MAX_PATH,
        configPath.c_str()
    );

    std::wstring relLog = logBuf;
    std::wstring suiteRoot = getSuiteRoot(configPath);

    // Final: <suiteRoot> + <relative log path>
    return suiteRoot + relLog;
}

// ------------------------------------------------------------
// Poison character detection (full path)
// ------------------------------------------------------------
bool containsPoison(const std::wstring& fullPath, const std::wstring& poisonChars, wchar_t& badChar) {
    for (wchar_t c : poisonChars) {
        if (c == 0) continue;
        if (fullPath.find(c) != std::wstring::npos) {
            badChar = c;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------
// Recursive file listing with validation + logging
// ------------------------------------------------------------
void listFiles(std::wstring& path,
               std::ofstream& outFile,
               const Exclusions& exclusions,
               std::ofstream& logFile)
{
    size_t baseLength = path.length();

    if (path.back() != L'\\') {
        path += L'\\';
        baseLength++;
    }

    std::wstring searchPath = path + L"*";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(searchPath.c_str(),
                                    FindExInfoBasic,
                                    &fd,
                                    FindExSearchNameMatch,
                                    nullptr,
                                    0);

    if (hFind == INVALID_HANDLE_VALUE)
        return;

    do {
        std::wstring name = fd.cFileName;

        if (name == L"." || name == L"..")
            continue;

        // 1. Prefix exclusions
        bool prefixExcluded = false;
        for (const auto& pre : exclusions.filePrefixes) {
            std::wstring p = trim(pre);
            if (!p.empty() && name.compare(0, p.length(), p) == 0) {
                prefixExcluded = true;
                break;
            }
        }
        if (prefixExcluded) continue;

        // 2. Folder exclusions
        bool folderExcluded = false;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            for (const auto& ex : exclusions.folders) {
                if (_wcsicmp(name.c_str(), ex.c_str()) == 0) {
                    folderExcluded = true;
                    break;
                }
            }
        }
        if (folderExcluded) continue;

        // 3. Build full path
        std::wstring fullPath = path + name;

        // 4. Poison character detection (fatal)
        wchar_t badChar = 0;
        if (containsPoison(fullPath, exclusions.poisonChars, badChar)) {
            logFile << "[ERROR] Poison character '" << (char)badChar << "' found in path:\n";
            logFile << utf8_encode(fullPath) << "\n";
            logFile << "Action: Aborted. Please rename the file and re-run FileListGen.\n";
            logFile.flush();
            exit(2);
        }

        // 5. Path length check (warning + skip)
        if ((int)fullPath.length() >= exclusions.maxPathLength) {
            logFile << "[WARNING] Path length " << fullPath.length()
                    << " exceeds limit (" << exclusions.maxPathLength << "):\n";
            logFile << utf8_encode(fullPath) << "\n";
            logFile << "Action: Skipped.\n\n";
            continue;
        }

        // 6. Recurse or write file
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            path.append(name);
            listFiles(path, outFile, exclusions, logFile);
        } else {
            outFile << utf8_encode(fullPath) << "\n";
        }

        path.erase(baseLength);

    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int wmain(int argc, wchar_t* argv[]) {
    if (argc < 4) {
        std::wcout << L"Usage: FileListGen.exe <Directory> <OutputFile> <ConfigINI>\n";
        return 1;
    }

    std::wstring root = argv[1];
    std::wstring outPath = argv[2];
    std::wstring configPath = argv[3];

    if (root.length() > 3 && root.back() == L'\\')
        root.pop_back();

    Exclusions exclusions = loadExclusions(configPath);

    std::wcout << L"[INFO] Loaded " << exclusions.folders.size() << L" folder exclusions.\n";
    std::wcout << L"[INFO] Loaded " << exclusions.filePrefixes.size() << L" prefix exclusions.\n";
    std::wcout << L"[INFO] Max Path Length: " << exclusions.maxPathLength << L"\n";
    std::wcout << L"[INFO] Poison Chars: " << exclusions.poisonChars << L"\n";

    // --------------------------------------------------------
    // Open output file
    // --------------------------------------------------------
    std::ofstream outFile(outPath, std::ios::out | std::ios::binary);
    std::vector<char> buffer(1024 * 1024);
    if (outFile.is_open()) {
        outFile.rdbuf()->pubsetbuf(buffer.data(), buffer.size());
    } else {
        std::wcerr << L"[ERROR] Unable to open output file.\n";
        return 1;
    }

    // --------------------------------------------------------
    // Open log file (overwrite, suite-root-relative)
    // --------------------------------------------------------
    std::wstring logPath = getLogPathFromConfig(configPath);
    std::ofstream logFile(logPath, std::ios::out | std::ios::trunc);

    if (!logFile.is_open()) {
        std::wcerr << L"[ERROR] Unable to open log file at: " << logPath << L"\n";
        return 1;
    }

    std::wcout << L"Scanning: " << root << L"\n";

    listFiles(root, outFile, exclusions, logFile);

    outFile.close();
    logFile.close();

    std::wcout << L"Done!\n";
    return 0;
}
