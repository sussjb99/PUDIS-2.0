📦 **PUDIS Installation Manifest** <br>
This document describes all the files included in the PUDIS distribution folders. <br>
It including their description and SHA‑256 hash values.

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
| ``Drive_Status.xml`` | Current drive identity, SMART data, calibration results | ``9AF2A0DE3476F25AF52F477160EC97D5698717AB604B0C0E802DABFE1F8ED94E`` |
| ``Help.html`` | Offline help documentation for PUDIS | ``F3F238F38E0B98C67F6414593CC3199EC326F06E05FC712E52A991CD38D8E5F9`` |
| ``PUDIS.exe`` | Main launcher and UI front‑end | ``70F41A85D66193ADEAC1FEFC91F7BA23E74AC09F6DA214105D4F3A12261C71A5`` |


🔹 bin/  — Executables
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``baselineXML_estimate.exe`` | Estimates baseline creation time | ``A316DFD15783759FEFA00E678FD7954A25B63289BEBA51E481E4CA7DE3EC39F4`` |
| ``calibrate_baselineXML_estimate.exe`` | Calibrates baseline estimation model | ``6703C1CC93D5A8EE898D816FA24E58BC4C20F56E66A1866C89AA7B92D8D0144D`` |
| ``calibrate_hardware.exe`` | Measures hardware timing, latency, throughput | ``568A0ADB59C0038CD7EBE14718BAAAC1F40435739CBBD9B4E621552339464EE4`` |
| ``calibrate_scan_estimate.exe`` | Calibrates scan‑time estimator | ``3BF6AA146165AD8BA86A4B738A50DE203E5BAE59BEF5EF8BBD3F95FAD332D598`` |
| ``corruptor.exe`` | Test tool to intentionally corrupt files for validation | ``3A401DDDF918D5D820A88F76AC202BF186A869415A16B1801D39AF592C861D01`` |
| ``create_baselineXML.exe`` | Generates baseline XML file for integrity tracking | ``618BDF6FA065F6C8AB3DF0028F6E462960BF0ACB9D47405E33907595F8AC9625`` |
| ``create_recovery.exe`` | Generates PAR2 recovery data | ``CD94B9A1D1B3461F9FE96BA0641AF6EB87E2605131433D707058EA4985F12726`` |
| ``create_recovery_estimate.exe`` | Estimates recovery generation time | ``94E51021A58DC2AF07443CBEFF9C34E9F4D7EC868EEF0AFCDAB0CCC684AEDD0A`` |
| ``deviceinfo.exe`` | Collects drive identity, SMART, USB bridge info | ``601DB389763D075F528193F167E0F47C29F4FAE7C0E0F882ECF650256B1B75BF`` |
| ``FileListGen.exe`` | Generates file list for baseline and scanning | ``306B718DF67D529CDCAD7A74697B08AD0AAF9A488F0D7C09E6BE6856153DFBBD`` |
| ``full_probe.exe`` | Performs full drive probe (SMART + USB + health) | ``B7F4627CD88A8AD2083F91E0274C43CC7957C12C7BC483A1C50E1982DE089BA9`` |
| ``generate_report.exe`` | Produces final PUDIS report | ``6421286E114EC1D2331B1F25DC0BC9E547A7D3CBC64134768CFB00C1BC2FBB35`` |
| ``hashdeep64.exe`` | External hashing tool used for file integrity | ``5F52886614DE94F51742051A3F6A89872901D0286FEBDE13ABDD997997124AE9`` |
| ``par2.exe`` | PAR2 engine for redundancy and recovery | ``E33C333ED6C1CE36C1252DB0B59DE160C0846E56C8FDB8752582B5FDDD40D976`` |
| ``PUDIS.exe`` | Main Application Launcher for PUDIS Application Suite | ``4068D5B61055BE2AAFE6249680E1AD953E50899373F3AF7486038060ABF48511`` |
| ``quick_file_check.exe`` | Fast integrity check using baseline | ``A7EDFA7C3D2E454DC8BC916D116C4D5D46DCB6BF3874AE5F1A5B4C465C3ED7E8`` |
| ``recovery_estimate.exe`` | Estimates recovery time | ``979046146C40C9572F48B16B06D2E34AF928B8F5EC14AC8DB9F20F410BB3B474`` |
| ``scantime_estimate.exe`` | Estimates scan duration | ``513B8E8C20B4B7DAB20D365FEF1888D5DB43CCD6F36DFB5472B18A8BC0512595`` |
| ``smartctl.exe`` | SMART data extraction tool | ``B5DB94E5082C042BE44994B7A4FA8F7B5C8E713B2AB1C9A560D8F7A7995EA27D`` |
| ``surface_scan.exe`` | Performs surface scan on the drive | ``4EA9D6AC351956995A5CA728C7CFBA4BDD5C35821C69053E6D87C405C9CA2478`` |

🔹 config/ — Configuration Files
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``config.ini`` | Main configuration file controlling all PUDIS modules | ``6E653C687B7E66CBDA6FC27B8B1282DE5E626552908D3008453D483E7ACD9DA5`` |
| ``drivedb.txt`` | Drive database for SMART attribute interpretation | ``612FC34980C0DA7D7163473A12C01095B579F85388A7CFA3CBADAC6C0D914B83`` |

🔹 data/ — Data Repository
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``logfiles.txt`` | Placeholder log file | ``---`` |


🔹 logs/ — Log File Repository
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``logfiles.txt`` | Placeholder log file | ``---`` |


🔹 reports/ — Report File Repository
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``reportfiles.txt`` | Placeholder report file | ``---`` |


🔹 scripts/ — Scripting File Repository
| File | Description | SHA‑256 |
| --- | --- | --- |
| ``pshell.bat`` | Batch script wrapper for PowerShell execution | ``8F947B928411A768D7B07F8F3797233B6EB568C019525071A3ACF1EE2A08A541`` |
| ``replace_ampersands.ps1`` | Script to sanitize XML output | ``40D4ED3428854FAD94CE05498FD921F7B453AED7F442D344E4A6DC25D0AA2367`` |


✔ **Notes**
* Executables in /bin folder are the core of the PUDIS toolchain.
* Root PUDIS.exe is the user‑facing launcher.
* XML, logs, and reports are generated at runtime.

