:: ============================================================
:: Portable USB Drive Integrity Suite
:: File: pshell.bat
:: Author: sussjb99
:: Version: 1.0
:: Last Modified: 2026-04-12
:: Copyright (c) 2026 sussjb99. All rights reserved.
:: Licensed under the MIT License. See LICENSE.txt for details.
:: ============================================================


@echo off
set "script=%~dp0%1"
:: Remove the first argument (the script name) from the list
shift
:: Pass the remaining arguments (%1, %2, etc.) to PowerShell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%script%" %1 %2 %3 %4 %5 %6 %7 %8 %9
