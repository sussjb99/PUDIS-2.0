/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: PUDIS_Launcher.cpp
   Version: 3.0 (Dynamic Windowed/App Mode Help Architecture)
   Last Modified: 2026-05-25
   Author: sussjb99

   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose: FrontEnd for PUDIS. 
            - Displays Storage Device's Technical Details
            - Displays menu to facilitate Integrity repair and
              Validation

   ============================================================ */

#include <windows.h>
#include <shlwapi.h>
#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <conio.h>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Advapi32.lib")

// ============================================================
// ADMIN PRIVILEGE DETECTION + ELEVATION
// ============================================================

bool IsAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;

    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0,
        &adminGroup))
    {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin;
}

void RelaunchAsAdmin(const std::wstring& exePath)
{
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
    {
        MessageBoxW(NULL, L"Admin elevation was cancelled or failed.", L"PUDIS", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }

    ExitProcess(0);
}

void OpenLogsFolderStyled(const std::wstring& folder)
{
    HINSTANCE h = ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32) {
        MessageBoxW(nullptr, L"Failed to open logs folder.", L"PUDIS", MB_ICONERROR);
        return;
    }

    Sleep(350);

    HWND hwnd = FindWindowW(L"CabinetWClass", nullptr);
    if (!hwnd) return;

    RECT rc;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);

    int screenW = rc.right - rc.left;
    int screenH = rc.bottom - rc.top;

    int width  = (int)(screenW * 0.40);
    int height = (int)(screenH * 0.60);

    int x = rc.right - width - 20;
    int y = rc.top + (screenH - height) / 4;

    MoveWindow(hwnd, x, y, width, height, TRUE);

    HWND console = GetConsoleWindow();
    if (console)
        SetForegroundWindow(console);
}

void OpenHelpStyled(const std::wstring& helpFile, int consoleWidth)
{
    HINSTANCE h = ShellExecuteW(nullptr, L"open", helpFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if ((INT_PTR)h <= 32) {
        MessageBoxW(nullptr, L"Failed to open Help file.", L"PUDIS", MB_ICONERROR);
        return;
    }

    Sleep(350);

    HWND hwnd = FindWindowW(L"Notepad", nullptr);
    if (!hwnd) return;

    RECT rc;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rc, 0);

    int screenW = rc.right - rc.left;
    int screenH = rc.bottom - rc.top;

    int charWidth = 8;
    int targetWidth = consoleWidth * charWidth;

    if (targetWidth > screenW * 0.45)
        targetWidth = (int)(screenW * 0.45);

    int width  = targetWidth;
    int height = (int)(screenH * 0.60);

    int x = rc.right - width - 20;
    int y = rc.top + 20;

    MoveWindow(hwnd, x, y, width, height, TRUE);

    HWND console = GetConsoleWindow();
    if (console)
        SetForegroundWindow(console);
}

std::wstring s2ws(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), NULL, 0);
    std::wstring ws(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), &ws[0], len);
    return ws;
}

void FatalExit(const std::wstring& msg, int code = 1) {
    std::wcerr << L"\n[ERROR] " << msg << L"\n";
    std::wcerr << L"Press any key to exit...";
    (void)_getwch();
    ExitProcess(code);
}

bool RunTool(const std::wstring& exePath, const std::wstring& args, const std::wstring& workDir) {
    std::wstring cmd = L"\"" + exePath + L"\" " + args;
    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        workDir.empty() ? nullptr : workDir.c_str(), &si, &pi)) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

std::string ExtractTag(const std::string& xml, const std::string& tag) {
    std::string openTag = "<" + tag + ">";
    std::string closeTag = "</" + tag + ">";
    size_t s = xml.find(openTag);
    if (s == std::string::npos) return "";
    s += openTag.length();
    size_t e = xml.find(closeTag, s);
    if (e == std::string::npos) return "";
    std::string val = xml.substr(s, e - s);

    val.erase(std::remove(val.begin(), val.end(), '\r'), val.end());
    val.erase(std::remove(val.begin(), val.end(), '\n'), val.end());
    size_t first = val.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    size_t last = val.find_last_not_of(" \t");
    return val.substr(first, (last - first + 1));
}

struct DriveStatus {
    std::wstring model, serial, fileSystem, smartStatus, healthRating;
    std::wstring totalGB, freeGB, integrityGrade, filesChecked, surfaceGrade, scanCoverage;
    std::wstring technology;
};

DriveStatus LoadDriveStatus(const std::wstring& xmlPath) {
    DriveStatus ds;
    std::ifstream file(xmlPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return ds;

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string xml(size, '\0');
    if (!file.read(&xml[0], size)) return ds;

    xml.erase(std::remove(xml.begin(), xml.end(), '\0'), xml.end());

    ds.model          = s2ws(ExtractTag(xml, "Model"));
    ds.serial         = s2ws(ExtractTag(xml, "Serial"));
    ds.fileSystem     = s2ws(ExtractTag(xml, "FileSystem"));
    ds.smartStatus    = s2ws(ExtractTag(xml, "SmartStatus"));
    ds.healthRating   = s2ws(ExtractTag(xml, "HealthRating"));
    ds.totalGB        = s2ws(ExtractTag(xml, "TotalGB"));
    ds.freeGB         = s2ws(ExtractTag(xml, "FreeGB"));
    ds.integrityGrade = s2ws(ExtractTag(xml, "IntegrityGrade"));
    ds.filesChecked   = s2ws(ExtractTag(xml, "FilesChecked"));
    ds.surfaceGrade   = s2ws(ExtractTag(xml, "SurfaceGrade"));
    ds.scanCoverage   = s2ws(ExtractTag(xml, "ScanCoverage"));
    ds.technology     = s2ws(ExtractTag(xml, "Technology"));

    return ds;
}

bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES &&
            !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

bool CalibrationComplete(const std::wstring& iniPath) {
    int readMBps   = GetPrivateProfileIntW(L"Performance",       L"MeasuredSequentialReadMBps", 0, iniPath.c_str());
    int totalFiles = GetPrivateProfileIntW(L"FileSystemProfile", L"TotalFiles",                  0, iniPath.c_str());
    return (readMBps > 0 && totalFiles > 0);
}

WORD ResolveColor(const std::wstring& nameRaw)
{
    std::wstring name = nameRaw;
    std::transform(name.begin(), name.end(), name.begin(), ::towupper);

    if (name == L"CYAN")      return FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (name == L"GREEN")     return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (name == L"WHITE")     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    if (name == L"BLUE")      return FOREGROUND_BLUE;
    if (name == L"GOLD" || name == L"YELLOW") return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    if (name == L"PURPLE")    return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    if (name == L"RED")       return FOREGROUND_RED | FOREGROUND_INTENSITY;
    if (name == L"GRAY")      return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;

    return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
}

int wmain() {
    wchar_t pathBuf[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, pathBuf, MAX_PATH)) return 1;

    // ============================================================
    // FIXED PATH RESOLUTION: RUNTIME DIRECTORY CONTEXT AWARE
    // ============================================================

    // 1. Determine exactly where PUDIS.exe is running from
    std::wstring exeFullPath(pathBuf);
    size_t lastSlash = exeFullPath.find_last_of(L"\\/");
    if (lastSlash == std::wstring::npos) return 1;
    std::wstring appDir = exeFullPath.substr(0, lastSlash + 1); // Extract runtime path folder

    // Enforce system drive safeguard relative to app target
    wchar_t driveRoot[MAX_PATH];
    wcscpy_s(driveRoot, appDir.c_str());
    PathStripToRootW(driveRoot); 
    if (towupper(driveRoot[0]) == L'C')
        FatalExit(L"PUDIS is restricted from running on the system drive (C:).");

    // 2. Locate config.ini directly inside the config subfolder where PUDIS is executed
    std::wstring iniPath = appDir + L"config\\config.ini";

    // Fallback: If config.ini isn't local, check the legacy folder configuration structure
    if (!FileExists(iniPath)) {
        wchar_t rootBuf[MAX_PATH];
        GetPrivateProfileStringW(
            L"Paths", L"Root", L"Integrity_Check",
            rootBuf, MAX_PATH,
            (std::wstring(driveRoot) + L"Integrity_Check\\config\\config.ini").c_str()
        );
        std::wstring legacyBaseDir = std::wstring(driveRoot) + rootBuf + L"\\";
        iniPath = legacyBaseDir + L"config\\config.ini";
    }

    // 3. Extract subfolder configurations relative to the real application context directory
    wchar_t binBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Bin", L"bin",
                             binBuf, MAX_PATH, iniPath.c_str());
    std::wstring absBin = appDir + binBuf + L"\\";

    wchar_t scriptsBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Scripts", L"scripts",
                             scriptsBuf, MAX_PATH, iniPath.c_str());
    std::wstring scriptsDir = appDir + scriptsBuf + L"\\";

    wchar_t dataBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Data", L"data",
                             dataBuf, MAX_PATH, iniPath.c_str());
    std::wstring dataDir = appDir + dataBuf + L"\\";

    wchar_t reportsBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Reports", L"reports",
                             reportsBuf, MAX_PATH, iniPath.c_str());
    std::wstring reportsDir = appDir + reportsBuf + L"\\";

    wchar_t logsBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Logs", L"logs",
                             logsBuf, MAX_PATH, iniPath.c_str());
    std::wstring logsDir = appDir + logsBuf;
    if (!logsDir.empty() && logsDir.back() != L'\\')
        logsDir += L"\\";

    wchar_t statusBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"StatusXML", L"Drive_Status.xml",
                             statusBuf, MAX_PATH, iniPath.c_str());
    std::wstring absStatus = appDir + statusBuf;

    wchar_t baselineBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"BaselineXML", L"data\\baseline.xml",
                             baselineBuf, MAX_PATH, iniPath.c_str());
    std::wstring baselinePath = appDir + baselineBuf;

    wchar_t currentBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"CurrentCheck", L"data\\current_check.xml",
                             currentBuf, MAX_PATH, iniPath.c_str());
    std::wstring currentCheckPath = appDir + currentBuf;

    wchar_t par2logBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Par2log", L"logs\\par2.log",
                             par2logBuf, MAX_PATH, iniPath.c_str());
    std::wstring par2LogPath = appDir + par2logBuf;

    wchar_t helpBuf[MAX_PATH];
    GetPrivateProfileStringW(L"Paths", L"Help", L"Help.txt",
                             helpBuf, MAX_PATH, iniPath.c_str());
    std::wstring helpFile = appDir + helpBuf;

    std::wstring driveLtr(1, towupper(driveRoot[0]));

    // ============================================================
    // UI SETTINGS
    // ============================================================

    int uiWidth  = GetPrivateProfileIntW(L"UI", L"WindowWidth", 70, iniPath.c_str());
    int uiHeight = GetPrivateProfileIntW(L"UI", L"WindowHeight", 45, iniPath.c_str());

    wchar_t titleBuf[256];
    GetPrivateProfileStringW(L"UI", L"Title", L"PUDIS Launcher",
                             titleBuf, 256, iniPath.c_str());

    wchar_t colorBuf[16];
    GetPrivateProfileStringW(L"UI", L"Color", L"0F",
                             colorBuf, 16, iniPath.c_str());

    wchar_t colBuf[64];

    GetPrivateProfileStringW(L"UI", L"TitleColor", L"Cyan",
                             colBuf, 64, iniPath.c_str());
    WORD TITLE = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"LabelColor", L"Green",
                             colBuf, 64, iniPath.c_str());
    WORD LABEL = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"ValueColor", L"White",
                             colBuf, 64, iniPath.c_str());
    WORD VALUE = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"StatusOKColor", L"Green",
                             colBuf, 64, iniPath.c_str());
    WORD STATUS_OK = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"SeparatorColor", L"Blue",
                             colBuf, 64, iniPath.c_str());
    WORD SEPARATOR = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"MenuColor", L"Yellow",
                             colBuf, 64, iniPath.c_str());
    WORD MENU = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"FooterColor", L"Purple",
                             colBuf, 64, iniPath.c_str());
    WORD FOOTER = ResolveColor(colBuf);

    GetPrivateProfileStringW(L"UI", L"WarningColor", L"Yellow",
                             colBuf, 64, iniPath.c_str());
    WORD WARN_COLOR = ResolveColor(colBuf);

    auto C = [&](WORD col) {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), col);
    };
    auto R = [&]() {
        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE),
                                FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
    };

    // ============================================================
    // PERMISSIONS SECTION
    // ============================================================

    bool requireAdmin = false;
    {
        wchar_t permBuf[8];
        GetPrivateProfileStringW(
            L"Permissions", L"RequireAdmin", L"0",
            permBuf, 8, iniPath.c_str()
        );
        requireAdmin = (wcscmp(permBuf, L"1") == 0);
    }

    // ============================================================
    // AUTO-ELEVATE IF REQUIRED BY CONFIG
    // ============================================================

    wchar_t exeBuf[MAX_PATH];
    GetModuleFileNameW(NULL, exeBuf, MAX_PATH);
    std::wstring exePath = exeBuf;

    if (requireAdmin && !IsAdmin())
    {
        RelaunchAsAdmin(exePath);
    }

    // ============================================================
    // MAIN LOOP
    // ============================================================

    while (true) {

        std::wstring modeCmd =
            L"mode con cols=" + std::to_wstring(uiWidth) +
            L" lines=" + std::to_wstring(uiHeight);
        _wsystem(modeCmd.c_str());

        std::wstring colorCmd = L"color ";
        colorCmd += colorBuf;
        _wsystem(colorCmd.c_str());

        SetConsoleTitleW(titleBuf);

        RunTool(absBin + L"deviceinfo.exe",
                driveLtr + L": \"" + absStatus + L"\"",
                absBin);

        DriveStatus ds = LoadDriveStatus(absStatus);

        system("cls");

        // DASHBOARD OUTPUT
        C(SEPARATOR); std::wcout << L"====================================================================\n"; R();
        C(TITLE);     std::wcout << L"                " << titleBuf << L"\n"; R();
        C(SEPARATOR); std::wcout << L"====================================================================\n"; R();

        C(LABEL); std::wcout << L"  DEVICE      : ";
        C(VALUE); std::wcout << (ds.model.empty() ? L"N/A" : ds.model) << L"\n"; R();

        C(LABEL); std::wcout << L"  SERIAL      : ";
        C(VALUE); std::wcout << (ds.serial.empty() ? L"N/A" : ds.serial) << L"\n"; R();

        C(LABEL); std::wcout << L"  MOUNTED     : ";
        C(VALUE); std::wcout << L"[" << driveRoot << L"] ("
                             << (ds.fileSystem.empty() ? L"Unknown" : ds.fileSystem) << L")\n"; R();

        std::wstring healthDisplay;

        if (!ds.smartStatus.empty()) {
            healthDisplay = ds.smartStatus;
        }
        else if (!ds.healthRating.empty()) {
            healthDisplay = ds.healthRating;
        }
        else {
            healthDisplay = L"Unavailable";
        }

        C(LABEL); std::wcout << L"  SMARTHEALTH : ";
        C(VALUE); std::wcout << healthDisplay << L"\n"; 
        R();

        std::wstring techUpper = ds.technology;
        std::transform(techUpper.begin(), techUpper.end(), techUpper.begin(), ::towupper);

        C(LABEL); std::wcout << L"  TECHNOLOGY  : ";
        C(VALUE); std::wcout << (techUpper.empty() ? L"N/A" : techUpper) << L"\n"; 
        R();

        C(LABEL); std::wcout << L"  CAPACITY    : ";
        C(VALUE); std::wcout << (ds.totalGB.empty() ? L"0" : ds.totalGB)
                             << L" GB (" << (ds.freeGB.empty() ? L"0" : ds.freeGB)
                             << L" GB Free)\n\n"; R();

        C(LABEL); std::wcout << L"  INTEGRITY   : ";
        C(STATUS_OK); std::wcout << (ds.integrityGrade.empty() ? L"No Baseline" : ds.integrityGrade);
        C(VALUE); std::wcout << L" (" << (ds.filesChecked.empty() ? L"0" : ds.filesChecked)
                             << L" Files)\n"; R();

        C(LABEL); std::wcout << L"  SURFACE     : ";
        C(STATUS_OK); std::wcout << (ds.surfaceGrade.empty() ? L"Unscanned" : ds.surfaceGrade);
        C(VALUE); std::wcout << L" (" << (ds.scanCoverage.empty() ? L"0" : ds.scanCoverage)
                             << L"% Covered)\n"; R();

        C(SEPARATOR); std::wcout << L"====================================================================\n"; R();

        C(MENU); std::wcout << L"  [1] Bit-Rot Detection            [5] Quick Surface Scan\n"; R();
        C(MENU); std::wcout << L"  [2] Re-Create Baseline           [6] Full Surface Scan\n"; R();
        C(MENU); std::wcout << L"  [3] Re-Generate Recovery         [7] Generate Report\n"; R();
        C(MENU); std::wcout << L"  [4] Calibrate                    [8] View Logs\n"; R();

        C(SEPARATOR); std::wcout << L"====================================================================\n\n"; R();

        C(FOOTER); std::wcout << L"  [H] Help / Documentation         [E] Exit Suite\n"; R();
        C(LABEL);  std::wcout << L"  Select Option: "; R();

        wchar_t c = towupper(_getwch());
        if (c == L'E') break;

        ReEvaluateOption:

        if (c == L'1') {

            if (!CalibrationComplete(iniPath)) {
                C(WARN_COLOR); std::wcout << L"\n\n [WARNING] Hardware calibration has not been completed.\n";
                C(VALUE);      std::wcout << L"  Press [4] to Calibrate now,\n";
                               std::wcout << L"     or press <Space Key> to return to menu: ";
                wchar_t shortcut = towupper(_getwch());
                if (shortcut == L'4') {
                    c = L'4';
                    goto ReEvaluateOption;
                }
                continue;
            }

            if (!FileExists(baselinePath)) {
                C(WARN_COLOR); std::wcout << L"\n\n [WARNING] Baseline verification matrix not found.\n";
                C(VALUE);      std::wcout << L"  Press [2] to Create Baseline now,\n";
                               std::wcout << L"     or press <Space Key> to return to menu: ";
                wchar_t shortcut = towupper(_getwch());
                if (shortcut == L'2') {
                    c = L'2';
                    goto ReEvaluateOption;
                }
                continue;
            }

            if (!FileExists(dataDir + L"recovery_data_part01.par2")) {
                C(WARN_COLOR); std::wcout << L"\n\n [WARNING] Redundancy recovery data blocks are missing.\n";
                C(VALUE);      std::wcout << L"  Press [3] to Generate Recovery Data now,\n";
                               std::wcout << L"     or press <Space Key> to return to menu: ";
                wchar_t shortcut = towupper(_getwch());
                if (shortcut == L'3') {
                    c = L'3';
                    goto ReEvaluateOption;
                }
                continue;
            }

            std::wcout << L"\n\n==============================================================\n";
            std::wcout << L"                 Bit-Rot Detection Estimate\n";
            std::wcout << L"==============================================================\n\n";

            std::wstring estimator = absBin + L"baselineXML_estimate.exe";
            std::wstring estArgs   = driveLtr + L":";
            RunTool(estimator, estArgs, absBin);

            std::wcout << L"\n==============================================================\n";
            std::wcout << L"Proceed with Bit-Rot Detection? (Y/N) ";

            wchar_t yn = towupper(_getwch());
            if (yn != L'Y') {
                std::wcout << L"\nOperation cancelled.\nPress any key to return...";
                _getwch();
                continue;
            }

            std::wcout << L"\n\n==============================================================\n";
            std::wcout << L"                 Running Bit-Rot Detection\n";
            std::wcout << L"==============================================================\n\n";

            std::wstring quickCheckExe = absBin + L"quick_file_check.exe";
            RunTool(quickCheckExe, driveLtr + L":", absBin);

            std::wcout << L"\n[FINISH] Bit Rot Scan complete.\nPress any key to refresh dashboard...";
            _getwch();
            continue;
        }

        if (c == L'2') {

            if (!CalibrationComplete(iniPath)) {
                C(WARN_COLOR); std::wcout << L"\n\n [WARNING] Drive hardware performance calibration required before tracking baselines.\n";
                C(VALUE);      std::wcout << L"  Press [4] to Calibrate now,\n";
                               std::wcout << L"     or press <Space Key> to return to menu: ";
                wchar_t shortcut = towupper(_getwch());
                if (shortcut == L'4') {
                    c = L'4';
                    goto ReEvaluateOption;
                }
                continue;
            }

            std::wcout << L"\n\n==============================================================\n";
            std::wcout << L"                 Baseline Rebuild Estimate\n";
            std::wcout << L"==============================================================\n\n";

            std::wstring estimator = absBin + L"baselineXML_estimate.exe";
            std::wstring estArgs   = driveLtr + L":\\";
            RunTool(estimator, estArgs, absBin);

            std::wcout << L"\n==============================================================\n";
            std::wcout << L"Proceed with rebuilding the baseline? (Y/N) ";

            wchar_t yn = towupper(_getwch());
            if (yn != L'Y') {
                std::wcout << L"\nBaseline update cancelled.\nPress any key to return...";
                _getwch();
                continue;
            }

            std::wcout << L"\n\n==============================================================\n";
            std::wcout << L"                 Building New Baseline\n";
            std::wcout << L"==============================================================\n\n";

            std::wstring createBase = absBin + L"create_baselineXML.exe";
            std::wstring baseArgs   = driveLtr + L":";
            RunTool(createBase, baseArgs, absBin);

            std::wcout << L"\n[FINISH] Baseline rebuild complete.\nPress any key to refresh dashboard...";
            _getwch();
            continue;
        }

        if (c == L'3') {

            if (!FileExists(baselinePath)) {
                C(WARN_COLOR); std::wcout << L"\n\n [WARNING] Cannot generate redundancy verification tables without an active baseline matrix.\n";
                C(VALUE);      std::wcout << L"  Press [2] to Create Baseline now,\n";
                               std::wcout << L"     or press <Space Key> to return to menu: ";
                wchar_t shortcut = towupper(_getwch());
                if (shortcut == L'2') {
                    c = L'2';
                    goto ReEvaluateOption;
                }
                continue;
            }

            RunTool(absBin + L"create_recovery_estimate.exe",
                    driveLtr + L":", absBin);

            std::wcout << L"\nContinue with Generate Recovery Data (Y/N): ";
            wchar_t ans = towupper(_getwch());
            std::wcout << ans << L"\n";

            if (ans == L'Y') {
                RunTool(absBin + L"create_recovery.exe",
                        driveLtr + L":", absBin);
                std::wcout << L"\nRecovery Data generation complete.";
            } else {
                std::wcout << L"\nOperation cancelled.";
            }

            std::wcout << L"\nPress any key to return to the menu...";
            _getwch();
            continue;
        }

        if (c == L'4') {
            RunTool(absBin + L"calibrate_hardware.exe",
                    driveLtr + L":", absBin);

            std::wcout << L"\nCalibration complete. Press any key to return...";
            _getwch();
            continue;
        }

        if (c == L'5' || c == L'6') {

            std::wstring mode = (c == L'5') ? L"q" : L"f i";
            std::wstring tool = L"scantime_estimate.exe";
            std::wstring args = driveLtr + L": " + mode;

            RunTool(absBin + tool, args, absBin);

            std::wcout << L"\nProceed? (Y/N) ";
            if (towupper(_getwch()) == L'Y') {

                RunTool(absBin + L"surface_scan.exe",
                        driveLtr + L": " + mode, absBin);

                std::wcout << L"\n[FINISH] Surface scan sequence complete.\nPress any key...";
                _getwch();
            }
            continue;
        }

        if (c == L'7') {

            std::wstring reportExe = absBin + L"generate_report.exe";
            std::wstring args      = driveLtr + L":";

            std::wcout << L"\nGenerating report...\n";

            if (!RunTool(reportExe, args, absBin)) {
                std::wcout << L"\n[ERROR] Failed to run generate_report.exe\n";
            } else {
                std::wcout << L"\n[FINISH] Report generation complete.\n";
            }

            std::wcout << L"Press any key to return...";
            _getwch();
            continue;
        }

        if (c == L'8') {
            OpenLogsFolderStyled(logsDir);
            continue;
        }

        if (c == L'H') {
            std::wcout << L"\nParsing configuration and initializing web browser...\n";
            std::wcout.flush();

            // 1. Read Browser settings dynamically from config.ini
            wchar_t browserType[128];
            GetPrivateProfileStringW(L"Browser", L"Browser", L"default", browserType, 128, iniPath.c_str());

            int targetW = GetPrivateProfileIntW(L"Browser", L"WindowWidth", 800, iniPath.c_str());
            int targetH = GetPrivateProfileIntW(L"Browser", L"WindowHeight", 600, iniPath.c_str());
            
            // Fetch WindowedMode parameter (defaults to 1 = Windowed/App Mode enabled)
            int windowedMode = GetPrivateProfileIntW(L"Browser", L"WindowedMode", 1, iniPath.c_str());

            std::wstring browserChoice(browserType);
            std::transform(browserChoice.begin(), browserChoice.end(), browserChoice.begin(), ::towlower);

            bool executionSuccess = false;

            // 2. Evaluate target path or explicit browser engine mapping
            if (browserChoice == L"default") {
                // System fallback uses standard OS shell registration association (ignores sizes natively)
                HINSTANCE h = ShellExecuteW(nullptr, L"open", helpFile.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                executionSuccess = ((INT_PTR)h > 32);
            } 
            else {
                std::wstring exeTarget;
                std::wstring cmdArguments;

                if (browserChoice == L"edge" || browserChoice == L"chrome" || browserChoice == L"brave") {
                    if (browserChoice == L"edge") exeTarget = L"msedge.exe";
                    else if (browserChoice == L"chrome") exeTarget = L"chrome.exe";
                    else exeTarget = L"brave.exe";

                    std::wstring sizeString = L" --window-size=" + std::to_wstring(targetW) + L"," + std::to_wstring(targetH);

                    if (windowedMode == 1) {
                        // Clean standalone App Frame window (strips out tab bars/address inputs)
                        cmdArguments = L"--app=\"" + helpFile + L"\"" + sizeString;
                    } else {
                        // Standard browser window tab wrapper layout
                        cmdArguments = L"--new-window" + sizeString + L" \"" + helpFile + L"\"";
                    }
                } 
                else if (browserChoice == L"firefox") {
                    exeTarget = L"firefox.exe";
                    // Firefox relies on standard dimension sizing tags
                    cmdArguments = L"-new-window \"" + helpFile + L"\" -width " + std::to_wstring(targetW) + L" -height " + std::to_wstring(targetH);
                } 
                else {
                    // Absolute path manual overwrite custom mapping fallback
                    exeTarget = browserType; 
                    cmdArguments = L" \"" + helpFile + L"\"";
                }

                // Fire process inside normal environment paths
                executionSuccess = RunTool(exeTarget, cmdArguments, L"");
                
                // If direct working folder lookup fails, attempt explicit OS Shell environment resolution
                if (!executionSuccess) {
                    HINSTANCE h = ShellExecuteW(nullptr, L"open", exeTarget.c_str(), cmdArguments.c_str(), nullptr, SW_SHOWNORMAL);
                    executionSuccess = ((INT_PTR)h > 32);
                }
            }

            // 3. Focus recovery check routing
            if (!executionSuccess) {
                C(WARN_COLOR);
                std::wcout << L"  [ERROR] Failed to map or launch target browser configuration.\n";
                R();
                std::wcout << L"  Press any key to return...";
                _getwch();
            } else {
                Sleep(350); // Small pause allowing the window context thread to initiate layout frames
                
                // Keep launcher navigation active by pulling keyboard focus back immediately
                HWND consoleWindow = GetConsoleWindow();
                if (consoleWindow) {
                    SetForegroundWindow(consoleWindow);
                }
            }
            continue;
        }

        std::wcout << L"\n[ERROR] Invalid selection.\nPress any key...";
        _getwch();
    }

    return 0;
}