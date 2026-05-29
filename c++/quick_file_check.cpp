/* ============================================================
    PUDIS-2.0 (Portable USB Drive Integrity Suite)
    File: quick_file_check.cpp
    Version: 2.2 (Dynamic Path Resolution)
    Author: sussjb99
    Last Modified: 2026-05-28

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.
    
    Purpose: - Check files for bit-rot via hash changes
             - Facilate file recovery procedure
    ============================================================ */

#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <ctime>
#include <conio.h>
#include <atomic>

#pragma comment(lib, "Shlwapi.lib")

namespace Color {
    std::wstring Reset  = L"\033[0m";
    std::wstring Cyan   = L"\033[36m";
    std::wstring Yellow = L"\033[33m";
    std::wstring Green  = L"\033[32m";
    std::wstring Red    = L"\033[31m";
    std::wstring Gray   = L"\033[90m";
    std::wstring White  = L"\033[37m";
    std::wstring Blue   = L"\033[34m";
    std::wstring Purple = L"\033[35m";

    std::wstring MapStringToAnsi(const std::wstring& colorName, const std::wstring& defaultAnsi) {
        if (colorName == L"Cyan")   return L"\033[36m";
        if (colorName == L"Green")  return L"\033[32m";
        if (colorName == L"Yellow") return L"\033[33m";
        if (colorName == L"Gold")   return L"\033[33m";
        if (colorName == L"Red")    return L"\033[31m";
        if (colorName == L"White")  return L"\033[37m";
        if (colorName == L"Blue")   return L"\033[34m";
        if (colorName == L"Purple") return L"\033[35m";
        if (colorName == L"Gray")   return L"\033[90m";
        return defaultAnsi;
    }

    void LoadColors(const std::wstring& iniPath) {
        wchar_t buf[64];
        auto GetCol = [&](const wchar_t* key, const wchar_t* defVal, std::wstring& target, const wchar_t* defaultAnsi) {
            GetPrivateProfileStringW(L"UI", key, defVal, buf, 64, iniPath.c_str());
            target = MapStringToAnsi(buf, defaultAnsi);
        };
        GetCol(L"TitleColor",    L"Cyan",   Cyan,   L"\033[36m");
        GetCol(L"LabelColor",    L"Green",  Green,  L"\033[32m");
        GetCol(L"ValueColor",    L"White",  White,  L"\033[37m");
        GetCol(L"StatusOKColor", L"Green",  Green,  L"\033[32m");
        GetCol(L"FooterColor",   L"Purple", Purple, L"\033[35m"); 
    }
}

void RunSpinner(std::atomic<bool>& keepRunning) {
    const wchar_t* frames[] = { L"|", L"/", L"-", L"\\" };
    int i = 0;
    while (keepRunning) {
        std::wcout << frames[i++ % 4] << L"\b" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::wcout << L" " << L"\b";
}

struct FileEntry { std::string rawName, md5, mtime; };
using InventoryMap = std::unordered_map<std::string, FileEntry>;

void FatalExit(const std::wstring& msg, int code = 1) {
    std::wcerr << Color::Red << L"\n[ERROR] " << Color::Reset << msg << L"\nPress any key to exit...";
    (void)_getwch();
    ExitProcess(code);
}

std::wstring GetConfigString(const std::wstring& section, const std::wstring& key, const std::wstring& defaultVal, const std::wstring& iniPath) {
    wchar_t buf[MAX_PATH];
    GetPrivateProfileStringW(section.c_str(), key.c_str(), defaultVal.c_str(), buf, MAX_PATH, iniPath.c_str());
    return std::wstring(buf);
}

void EnableVirtualTerminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    if (GetConsoleMode(hOut, &dwMode)) {
        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    }
}

double IsoToUnixTime(const std::string& iso) {
    if (iso.empty()) return 0;
    std::tm tm = {};
    std::istringstream ss(iso);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return ss.fail() ? 0 : static_cast<double>(_mkgmtime(&tm));
}

bool RunTool(const std::wstring& cmdLine, const std::wstring& workDir, bool hidden = true) {
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::vector<wchar_t> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back(L'\0');
    DWORD flags = hidden ? CREATE_NO_WINDOW : 0;
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, workDir.c_str(), &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (exitCode == 0);
}

std::string ExtractTag(const std::string& block, const std::string& tag) {
    std::string open = "<" + tag + ">", close = "</" + tag + ">";
    size_t s = block.find(open);
    if (s == std::string::npos) return "";
    s += open.size();
    size_t e = block.find(close, s);
    return (e == std::string::npos) ? "" : block.substr(s, e - s);
}

std::string ExtractMd5(const std::string& block) {
    size_t pos = block.find("type=");
    while (pos != std::string::npos) {
        size_t md5Check = block.find("MD5", pos);
        if (md5Check != std::string::npos && md5Check < pos + 15) {
            size_t start = block.find(">", md5Check) + 1;
            size_t end = block.find("</hashdigest>", start);
            if (end != std::string::npos) return block.substr(start, end - start);
        }
        pos = block.find("type=", pos + 5);
    }
    return "";
}

InventoryMap ParseDfxml(const std::wstring& path) {
    InventoryMap inv;
    std::ifstream file(path, std::ios::binary);
    if (!file) return inv;
    std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t pos = 0;
    while ((pos = xml.find("<fileobject>", pos)) != std::string::npos) {
        size_t end = xml.find("</fileobject>", pos);
        if (end == std::string::npos) break;
        std::string block = xml.substr(pos, end - pos);
        FileEntry fe;
        fe.rawName = ExtractTag(block, "filename");
        fe.md5 = ExtractMd5(block);
        fe.mtime = ExtractTag(block, "mtime");
        if (!fe.rawName.empty()) {
            std::string p = fe.rawName;
            if (p.size() > 2 && p[1] == ':') p = p.substr(2);
            if (p.substr(0, 2) == ".\\" || p.substr(0, 2) == "./") p = p.substr(2);
            std::replace(p.begin(), p.end(), '\\', '/');
            std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            inv[p] = fe;
        }
        pos = end + 13;
    }
    return inv;
}

int wmain(int argc, wchar_t* argv[]) {
    EnableVirtualTerminal();

    if (argc < 2) {
        std::wcout << L"Usage: quick_file_check.exe <DriveLetter>\n";
        return 1;
    }

    wchar_t dl = towupper(argv[1][0]);
    if (dl == L'C') FatalExit(L"Safety Triggered: Operation on C: is prohibited.");

    // --- Dynamic Path Resolution ---
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathRemoveFileSpecW(exePath); // exePath is now bin folder
    PathRemoveFileSpecW(exePath); // exePath is now application root
    std::wstring baseDir = exePath;
    std::wstring configIni = baseDir + L"\\config\\config.ini";

    if (!PathFileExistsW(configIni.c_str())) {
        FatalExit(L"Configuration file missing at expected location: " + configIni);
    }

    Color::LoadColors(configIni);

    std::wstring root = std::wstring(1, dl) + L":";
    std::wstring binDir = baseDir + L"\\" + GetConfigString(L"Paths", L"Bin", L"bin", configIni);
    std::wstring dataDir = baseDir + L"\\" + GetConfigString(L"Paths", L"Data", L"data", configIni);
    std::wstring baselineXml = baseDir + L"\\" + GetConfigString(L"Paths", L"BaselineXML", L"data\\baseline.xml", configIni);
    std::wstring currentXml = baseDir + L"\\" + GetConfigString(L"Paths", L"CurrentCheck", L"data\\current_check.xml", configIni);
    std::wstring statusFile = baseDir + L"\\" + GetConfigString(L"Paths", L"StatusXML", L"Drive_Status.xml", configIni);
    
    std::wstring fileGenExe = binDir + L"\\" + GetConfigString(L"Tools", L"FileListGen", L"FileListGen.exe", configIni);
    std::wstring hashDeepExe = binDir + L"\\" + GetConfigString(L"Tools", L"HashDeep", L"hashdeep64.exe", configIni);
    std::wstring par2Exe = binDir + L"\\" + GetConfigString(L"Tools", L"Par2", L"par2.exe", configIni);

    std::wstring parVolume1 = dataDir + L"\\recovery_data_part01.par2";
    bool baselineExists = PathFileExistsW(baselineXml.c_str());
    bool parityExists = PathFileExistsW(parVolume1.c_str());

    if (!baselineExists || !parityExists) {
        std::wcout << Color::Red << L"\n[PRE-FLIGHT FAILED] Critical Integrity Data Missing!\n" << Color::Reset;
        if (!baselineExists) std::wcout << L" -> Missing Baseline: " << baselineXml << L"\n";
        if (!parityExists)   std::wcout << L" -> Missing Parity: " << parVolume1 << L"\n";
        std::wcout << L"\nAction Required: Run the full Integrity Setup to generate these files.\n";
        FatalExit(L"Scan aborted due to missing prerequisites.");
    }

    std::wstring auditFile = dataDir + L"\\quick_check_files.txt";
    std::wcout << L"\n" << Color::Cyan << L"Step 1: Inventorying Drive...\n" << Color::Reset;
    std::wstring fileGenCmd = L"\"" + fileGenExe + L"\" \"" + root + L"\" \"" + auditFile + L"\" \"" + configIni + L"\"";
    if (!RunTool(fileGenCmd, binDir)) FatalExit(L"FileListGen failed.");

    std::wcout << Color::Yellow << L"Step 2: Hashing files... " << Color::Reset;
    std::atomic<bool> hashActive{ true };
    std::thread spinnerThread(RunSpinner, std::ref(hashActive));

    std::wstring hashCmd = L"cmd.exe /c \"\"" + hashDeepExe + L"\" -c md5 -l -d -f \"" + auditFile + L"\" > \"" + currentXml + L"\"\"";
    bool hashSuccess = RunTool(hashCmd, root + L"\\");
    
    hashActive = false; 
    if (spinnerThread.joinable()) spinnerThread.join();

    if (!hashSuccess) FatalExit(L"Hashdeep failed.");
    std::wcout << L"Done!\n";

    std::wcout << Color::Cyan << L"Step 3: Comparing Inventories...\n" << Color::Reset;
    InventoryMap bFiles = ParseDfxml(baselineXml);
    InventoryMap cFiles = ParseDfxml(currentXml);
    
    if (bFiles.empty()) FatalExit(L"Baseline is empty.");

    std::vector<std::string> newList, modList, delList, corList;
    size_t totalChecked = cFiles.size();

    for (auto const& [key, newFe] : cFiles) {
        if (bFiles.count(key)) {
            FileEntry oldFe = bFiles[key];
            if (!oldFe.md5.empty() && !newFe.md5.empty() && oldFe.md5 != newFe.md5) {
                double diff = std::abs(IsoToUnixTime(newFe.mtime) - IsoToUnixTime(oldFe.mtime));
                if (diff < 2.1) corList.push_back(newFe.rawName);
                else modList.push_back(newFe.rawName);
            }
            bFiles.erase(key);
        } else newList.push_back(newFe.rawName);
    }
    for (auto const& [key, oldFe] : bFiles) delList.push_back(oldFe.rawName);

    std::wcout << Color::Purple << L"\nScan complete. Files processed: " << Color::White << totalChecked << Color::Reset << L"\n";
    std::wcout << L"Results: " << Color::Cyan << L"New=" << newList.size() 
               << Color::Yellow << L", Mod=" << modList.size() 
               << L", Del=" << delList.size() 
               << Color::Red << L", CORRUPT=" << corList.size() << Color::Reset << L"\n\n";

    std::time_t now = std::time(nullptr);
    struct tm timeinfo;
    char timeBuf[20];
    localtime_s(&timeinfo, &now);
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    std::ofstream statusOut(statusFile, std::ios::trunc);
    statusOut << "<DriveBabySitter>\n  <FileIntegrityScan>\n"
              << "    <LastScanDate>" << timeBuf << "</LastScanDate>\n"
              << "    <FilesChecked>" << totalChecked << "</FilesChecked>\n"
              << "    <CorruptFiles>" << corList.size() << "</CorruptFiles>\n"
              << "    <IntegrityGrade>" << (corList.empty() ? "High" : "Low") << "</IntegrityGrade>\n"
              << "  </FileIntegrityScan>\n</DriveBabySitter>";
    statusOut.close();

    if (!corList.empty()) {
        std::wcout << Color::Red << L"Silent corruption detected. Start repair? (Y/N): " << Color::Reset;
        if (towupper(_getwche()) == L'Y') {
            std::wstring masterListPath = dataDir + L"\\baseline_files.txt";
            std::vector<std::string> masterList;
            std::ifstream masterIn(masterListPath);
            
            if (masterIn) {
                std::string line;
                while (std::getline(masterIn, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    masterList.push_back(line);
                }
                masterIn.close();

                for (const auto& corruptFile : corList) {
                    auto it = std::find(masterList.begin(), masterList.end(), corruptFile);
                    if (it != masterList.end()) {
                        size_t idx = std::distance(masterList.begin(), it);
                        int pNum = static_cast<int>(std::floor(idx / 25000.0)) + 1;
                        std::wstring pStr = (pNum < 10) ? L"0" + std::to_wstring(pNum) : std::to_wstring(pNum);
                        std::wstring parFile = dataDir + L"\\recovery_data_part" + pStr + L".par2";

                        if (PathFileExistsW(parFile.c_str())) {
                            std::wcout << Color::Green << L"\n[REPAIRING] " << Color::Reset 
                                       << std::wstring(corruptFile.begin(), corruptFile.end()) << L"\n";
                            
                            std::wstring tempR = dataDir + L"\\temp_r.txt";
                            std::ofstream out(tempR); out << corruptFile; out.close();

                            std::wstring repairCmd = L"\"" + par2Exe + L"\" r -B . \"" + parFile + L"\" \"@" + tempR + L"\"";
                            RunTool(repairCmd, root + L"\\", false);
                        } else {
                            std::wcout << Color::Red << L"\n[FAILED] Cannot repair: " << Color::Reset 
                                       << std::wstring(corruptFile.begin(), corruptFile.end()) 
                                       << L"\nMissing recovery volume: " << parFile << L"\n";
                        }
                    }
                }
            } else {
                std::wcout << Color::Red << L"\nRepair failed: baseline_files.txt not found." << Color::Reset;
            }
        }
    }

    return 0;
}