/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: create_baselineXML.cpp
   Version: 2.0
   Author: sussjb99
   Last Modified: 2026-04-12
   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose: Creates the baseline.xml file which is used for
            determining if the files have become corrupted.
            
   ============================================================ */



/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: create_baselineXML.cpp
   ============================================================ */

#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>

// ------------------------------------------------------------
// Color namespace for UI consistency
// ------------------------------------------------------------
namespace Color {
    const wchar_t* Reset  = L"\033[0m";
    const wchar_t* Cyan   = L"\033[36m";
    const wchar_t* Yellow = L"\033[33m";
    const wchar_t* Green  = L"\033[32m";
    const wchar_t* Red    = L"\033[31m";
    const wchar_t* Gray   = L"\033[90m";
}

// ------------------------------------------------------------
// Enable Virtual Terminal Processing for ANSI colors
// ------------------------------------------------------------
void EnableVirtualTerminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
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
// Simple INI loader
// ------------------------------------------------------------
std::map<std::wstring, std::map<std::wstring, std::wstring>>
loadIni(const std::wstring& path)
{
    std::wifstream file(path);
    std::map<std::wstring, std::map<std::wstring, std::wstring>> ini;

    if (!file.is_open()) {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"Unable to open config.ini at: " << path << L"\n";
        return ini;
    }

    std::wstring line, section;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == L';' || line[0] == L'#')
            continue;

        if (line.front() == L'[' && line.back() == L']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = trim(line.substr(0, eq));
        std::wstring val = trim(line.substr(eq + 1));

        ini[section][key] = val;
    }

    return ini;
}

// ------------------------------------------------------------
// Ensure directory exists
// ------------------------------------------------------------
bool EnsureDirectory(const std::wstring& path)
{
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
        return true;

    if (CreateDirectoryW(path.c_str(), nullptr))
        return true;

    attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

// ------------------------------------------------------------
// Launch a process and wait for completion
// ------------------------------------------------------------
bool RunProcess(const std::wstring& exePath,
                const std::wstring& args,
                const std::wstring& workDir,
                DWORD& exitCode)
{
    std::wstring cmd = L"\"" + exePath + L"\" " + args;

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(
            nullptr,
            cmdBuf.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            workDir.empty() ? nullptr : workDir.c_str(),
            &si,
            &pi))
    {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"Failed to launch: " << exePath << L"\n";
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// ------------------------------------------------------------
// Hashdeep runner with non-blocking pipe + high-speed draining
// ------------------------------------------------------------
bool RunHashdeepWithSpinner(const std::wstring& exePath,
                            const std::wstring& args,
                            const std::wstring& workDir,
                            const std::wstring& outputFile)
{
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"CreatePipe failed.\n";
        return false;
    }

    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    std::wstring cmd = L"\"" + exePath + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (!CreateProcessW(
            nullptr,
            cmdBuf.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            workDir.empty() ? nullptr : workDir.c_str(),
            &si,
            &pi))
    {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"Failed to launch hashdeep.\n";
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }

    CloseHandle(hWrite);

    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"Unable to open output file: " << outputFile << L"\n";
        CloseHandle(hRead);
        return false;
    }

    const wchar_t spinnerChars[4] = { L'|', L'/', L'-', L'\\' };
    int spinIndex = 0;

    DWORD bytesRead = 0, avail = 0;
    char buffer[16384]; 
    ULONGLONG lastUpdate = GetTickCount64();

    std::wcout << Color::Yellow << L"Step 2: Hashing files... " << Color::Reset << spinnerChars[spinIndex] << std::flush;

    while (true)
    {
        while (PeekNamedPipe(hRead, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD toRead = (avail > sizeof(buffer)) ? sizeof(buffer) : avail;
            if (ReadFile(hRead, buffer, toRead, &bytesRead, nullptr) && bytesRead > 0) {
                out.write(buffer, bytesRead);
            } else break;
        }

        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            break;

        ULONGLONG now = GetTickCount64();
        if (now - lastUpdate > 150) {
            spinIndex = (spinIndex + 1) % 4;
            std::wcout << L"\r" << Color::Yellow << L"Step 2: Hashing files... " << Color::Reset << spinnerChars[spinIndex] << std::flush;
            lastUpdate = now;
        }

        Sleep(1);
    }

    while (ReadFile(hRead, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0)
        out.write(buffer, bytesRead);

    std::wcout << L"\r" << Color::Yellow << L"Step 2: Hashing files... " << Color::Reset << L"Done!   \n";

    out.close();
    CloseHandle(hRead);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (exitCode == 0);
}

// ------------------------------------------------------------
// MAIN
// ------------------------------------------------------------
int wmain(int argc, wchar_t* argv[])
{
    EnableVirtualTerminal();

    if (argc < 2) {
        std::wcout << L"Usage: create_baselineXML.exe <DriveLetter>\n";
        return 1;
    }

    wchar_t dl = towupper(argv[1][0]);
    if (dl < L'A' || dl > L'Z') {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"Invalid drive letter.\n";
        return 1;
    }

    if (dl == L'C') {
        std::wcerr << Color::Red << L"[CRITICAL] " << Color::Reset << L"Operations on C: are prohibited.\n";
        return 1;
    }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::wstring fullExePath = exePath;
    size_t pos = fullExePath.find_last_of(L"\\/");
    std::wstring binDir = fullExePath.substr(0, pos);

    pos = binDir.find_last_of(L"\\/");
    std::wstring suiteRoot = binDir.substr(0, pos + 1);

    std::wstring configIni = suiteRoot + L"config\\config.ini";

    auto ini = loadIni(configIni);
    if (ini.empty()) return 1;

    auto& paths = ini[L"Paths"];
    auto& tools = ini[L"Tools"];

    std::wstring realBinDir  = suiteRoot + paths[L"Bin"] + L"\\";
    std::wstring dataDir      = suiteRoot + paths[L"Data"] + L"\\";
    std::wstring logsDir      = suiteRoot + paths[L"Logs"] + L"\\";

    std::wstring fileListGen = realBinDir + tools[L"FileListGen"];
    std::wstring hashDeep    = realBinDir + tools[L"HashDeep"];

    std::wstring auditList   = dataDir + L"baseline_files.txt";
    std::wstring baselineXML = suiteRoot + paths[L"BaselineXML"];

    EnsureDirectory(dataDir);
    EnsureDirectory(logsDir);

    // Step 1
    std::wstring driveRoot = std::wstring(1, dl) + L":\\";
    std::wcout << L"\n" << Color::Cyan << L"Step 1: Inventorying " << driveRoot << L"..." << Color::Reset << L"\n";

    DWORD exitCode = 0;
    std::wstring fgArgs = driveRoot + L" \"" + auditList + L"\" \"" + configIni + L"\"";

    if (!RunProcess(fileListGen, fgArgs, realBinDir, exitCode) || exitCode != 0) {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"FileListGen failed.\n";
        return 4;
    }

    // Step 2
    std::wstring hdArgs = L"-c md5 -j0 -l -d -f \"" + auditList + L"\"";

    if (!RunHashdeepWithSpinner(hashDeep, hdArgs, realBinDir, baselineXML)) {
        std::wcerr << Color::Red << L"[ERROR] " << Color::Reset << L"Hashdeep failed.\n";
        return 5;
    }

    std::wcout << L"\n" << Color::Green << L"Success!" << Color::Reset << L" Baseline created at: " << Color::Gray << baselineXML << Color::Reset << L"\n";
    return 0;
}