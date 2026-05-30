📦 **PUDIS Installation Manifest** <br>
This document describes all files included in the PUDIS distribution folders. <br>
It including their description, and SHA‑256 hash values.

📁 **Directory Tree** <br>
```text
PUDIS/
│   Drive_Status.xml
│   Help.html
│   PUDIS.exe
│
├── bin/
│       baselineXML_estimate.exe
│       calibrate_baselineXML_estimate.exe
│       calibrate_hardware.exe
│       calibrate_scan_estimate.exe
│       corruptor.exe
│       create_baselineXML.exe
│       create_recovery.exe
│       create_recovery_estimate.exe
│       deviceinfo.exe
│       FileListGen.exe
│       full_probe.exe
│       generate_report.exe
│       hashdeep64.exe
│       par2.exe
│       PUDIS.exe
│       quick_file_check.exe
│       recovery_estimate.exe
│       scantime_estimate.exe
│       smartctl.exe
│       surface_scan.exe
│
├── config/
│       config.ini
│       drivedb.txt
│
├── data/
│
├── logs/
│
├── reports/
│
└── scripts/
        pshell.bat
        replace_ampersands.ps1
```

📄 **File Manifest Table**

🔹 Root Files

| File | Description | SHA‑256 |
| --- | --- | --- |
| ``Drive_Status.xml`` | Current drive identity, SMART data, calibration results | ``---`` |
| ``Help.html`` | Offline help documentation for PUDIS | ``---`` |
| ``PUDIS.exe`` | Main launcher and UI front‑end | ``---`` |


🔹 bin/ — Executables
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``baselineXML_estimate.exe`` | Estimates baseline creation time | ``---`` |
| ``calibrate_baselineXML_estimate.exe`` | Calibrates baseline estimation model | ``---`` |
| ``calibrate_hardware.exe`` | Measures hardware timing, latency, throughput | ``---`` |
| ``calibrate_scan_estimate.exe`` | Calibrates scan‑time estimator | ``---`` |
| ``corruptor.exe`` | Test tool to intentionally corrupt files for validation | ``---`` |
| ``create_baselineXML.exe`` | Generates baseline XML file for integrity tracking | ``---`` |
| ``create_recovery.exe`` | Generates PAR2 recovery data | ``---`` |
| ``create_recovery_estimate.exe`` | Estimates recovery generation time | ``---`` |
| ``deviceinfo.exe`` | Collects drive identity, SMART, USB bridge info | ``---`` |
| ``FileListGen.exe`` | Generates file list for baseline and scanning | ``---`` |
| ``full_probe.exe`` | Performs full drive probe (SMART + USB + health) | ``---`` |
| ``generate_report.exe`` | Produces final PUDIS report | ``---`` |
| ``hashdeep64.exe`` | External hashing tool used for file integrity | ``---`` |
| ``par2.exe`` | PAR2 engine for redundancy and recovery | ``---`` |
| ``PUDIS.exe`` | Duplicate of root launcher (kept for compatibility) | ``---`` |
| ``quick_file_check.exe`` | Fast integrity check using baseline | ``---`` |
| ``recovery_estimate.exe`` | Estimates recovery time | ``---`` |
| ``scantime_estimate.exe`` | Estimates scan duration | ``---`` |
| ``smartctl.exe`` | SMART data extraction tool | ``---`` |
| ``surface_scan.exe`` | Performs surface scan on the drive | ``---`` |

🔹 config/
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``config.ini`` | Main configuration file controlling all PUDIS modules | ``---`` |
| ``drivedb.txt`` | Drive database for SMART attribute interpretation | ``---`` |

🔹 data/
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``logfiles.txt`` | Placeholder log file | ``---`` |


🔹 logs/
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``logfiles.txt`` | Placeholder log file | ``---`` |


🔹 reports/
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``reportfiles.txt`` | Placeholder report file | ``---`` |


🔹 scripts/
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``pshell.bat`` | Batch script wrapper for PowerShell execution | ``---`` |
| ``replace_ampersands.ps1`` | Script to sanitize XML output | ``---`` |


✔ Notes
Executables in bin/ are the core of the PUDIS toolchain.
Root PUDIS.exe is the user‑facing launcher.
XML, logs, and reports are generated at runtime.
