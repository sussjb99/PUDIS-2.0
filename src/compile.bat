@echo off

rem ============================================================
rem   PUDIS-2.0 (Portable USB Drive Integrity Suite)
rem   File: compile.bat
rem    Author: sussjb99
rem   Version: 2.0
rem   Last Modified: 2026-05-07
rem   Copyright (c) 2026 sussjb99. All rights reserved.
rem   Licensed under the MIT License. See LICENSE.txt for details.
rem 
rem   Purpose: batch file to compile all the required executables
rem
   ============================================================ */

REM baselineXML_estimate.exe
rc baselineXML_estiamte.rc
cl /EHsc /W4 /Fe:baselineXML_estimate.exe baselineXML_estimate.cpp baselineXML_estimate.res

rem calibrate_hardware.exe
rc calibrate_hardware.rc
rem cl /EHsc /W4 /Fe:calibrate_hardware.exe calibrate_hardware.cpp calibrate_hardware.res
cl /EHsc /W4 /O2 /Fe:calibrate_hardware.exe calibrate_hardware.cpp calibrate_hardware.res /link user32.lib

rem corruptor.exe		#used for creating test files that are corrupted
rc corruptor.rc
cl /EHsc /W4 /Fe:corruptor.exe corruptor.cpp corruptor.res


REM create_baselineXML.exe
rc create_baselineXML.rc
cl /EHsc /W4 /Fe:create_baselineXML.exe create_baselineXML.cpp create_baselineXML.res


REM create_recovery.exe
rc create_recovery.rc
cl /EHsc /W4 /Fe:create_recovery.exe create_recovery.cpp create_recovery.res


rem create_recovery_estimate.exe
rc create_recovery_estimate.rc
cl /EHsc /W4 /Fe:create_recovery_estimate.exe create_recovery_estimate.cpp create_recovery_estimate.res


rem deviceinfo.exe
rc deviceinfo.rc
rem cl /EHsc /W4 /Fe:deviceinfo.exe deviceinfo.cpp deviceinfo.res
cl /EHsc /W4 /std:c++17 /Fe:deviceinfo.exe deviceinfo.cpp deviceinfo.res



rem FileListGen.exe
rc FileListGen.rc
cl /EHsc /W4 /Fe:FileListGen.exe FileListGen.cpp FileListGen.res

rem full_probe.exe
rc full_probe.rc
cl /EHsc full_probe.cpp full_probe.res /Fe:full_probe.exe


rem generate_report.exe
rc generate_report.rc
cl /EHsc /W4 /Fe:generate_report.exe generate_report.cpp generate_report.res


REM PUDIS.exe  			#Main Launcher
rc PUDIS_Launcher.rc
cl /EHsc /W4 /Fe:PUDIS.exe PUDIS_Launcher.cpp PUDIS_Launcher.res

rem quick_file_check.exe	#Check if there any file that need repair.
rc quick_file_check.rc
cl /EHsc /W4 /std:c++17 /O2 /Fe:quick_file_check.exe quick_file_check.cpp quick_file_check.res /link Shlwapi.lib User32.lib

rem scantime_estimate.exe
rc scantime_estimate.rc
cl /EHsc /W4 /Fe:scantime_estimate.exe scantime_estimate.cpp scantime_estimate.res

rem surface_scan.exe
rc surface_scan.rc
cl /EHsc /W4 /Fe:surface_scan.exe surface_scan.cpp surface_scan.res











