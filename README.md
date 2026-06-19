# PUDIS-2.0
PUDIS 2.0 (Portable USB Drive Integrity Suite)

PUDIS (Portable-USB-Drive-Integrity-Suite) is a maintenance and monitoring toolkit designed to keep external storage devices healthy over prolonged periods of time. It achieves this object by performing two major tasks:

- **File Integrity Testing (Bit‑Rot Detection)**
- **Surface Analysis Testing (Performance & Stability Scans)**

This suite is designed to work with USB flash drives and portable external HDDs/SSD devices.

This software is designed to be small in size and should be installed and run directly from the external storage devices.

This software was written in C++.

---

## 🛡️ Security & Integrity

### ⚠️ Windows SmartScreen Warning (Important)

Because PUDIS is an independent open‑source project and does **not yet have a commercial code‑signing certificate**, Windows may show the following message when you run `PUDIS.exe`:

> **“Microsoft Defender SmartScreen prevented an unrecognized app from starting.”**  
> **Publisher: Unknown**

This is expected behavior for unsigned open‑source applications.

To run the program safely:

1. Click **More info**  
2. Click **Run anyway**

You may verify the authenticity of the executable using the SHA‑256 hashes listed in `manifest.md`.

A trusted code‑signing certificate is planned once project funding allows, which will eliminate this warning in future releases.

---

### Permissions

- Retrieving SMART health data from USB devices requires elevated administrator permissions.  
  Standard USB flash drives do not provide SMART data and therefore do not require elevation.
- Permission requirements are controlled through the `config.ini` file:


```
config.ini
[Permissions]
RequireAdmin=1
```

### Integrity & Safety

- **Verified Hashes:** All binaries in this suite are documented in the [manifest.md](./manifest.md) file with their corresponding **SHA‑256 hash**.
- **Self‑Signed:** Files are currently self‑signed due to the project’s funding phase. You can manually verify integrity using the provided hashes.
- **Safety First:** PUDIS is designed to identify, target, and test only removable storage devices. It will **never** perform stress testing on the system’s main (C:) drive.

---

## 🚀 Installation Methods

### **Automated Installation (Recommended)**
Use this method if you want a guided installer that ensures PUDIS is placed on a USB storage device only.

1. **Download** `PUDIS-2.0-Setup.exe` from the Releases page.  
2. **Run** the installer.  
3. **Accept** the License Agreement.  
4. **Select** the correct **USB storage device letter** when prompted.  
5. **Click Install** to complete the setup.

> **Note:** The installer is designed to prevent installation onto internal system drives (e.g., `C:\`).  
> Only removable USB storage devices will be accepted.

---

### **ZIP File Installation (Manual Method)**

1. **Download** the latest `PUDIS.zip` from the Releases page.  
2. **Copy** the ZIP file to the **root directory** of your USB drive.  
3. **Extract** the ZIP file on the USB drive.  
4. **Open** the newly created `PUDIS` folder.  
5. **Run** `PUDIS.exe`.

---

### **Drive Safety**
PUDIS automatically detects the drive it is running from and ensures all operations are safely targeted to that device.


---

## ❤️ Support the Project

PUDIS is an independent, open‑source project built to help people keep their data safe.  
If this tool has helped you detect issues, avoid data loss, or simply feel more confident about your drives, you can leave a tip to support ongoing development:

[☕ Tip the Developer on Ko‑fi](https://ko-fi.com/sussjb99)

Your support helps fund testing hardware, new features, and a professional code‑signing certificate to reduce antivirus false positives.

---

## Features

### 🔍 Device Information
Displays detailed information about the currently mounted drive:
- Device model
- Hardware serial number
- Drive letter
- Filesystem type (FAT32, exFAT, NTFS, etc.)
- Storage technology (Flash, HDD, SSD, SD)
- SMART health (if supported)

### 📦 Capacity Validation
Validates whether the drive’s reported capacity matches its actual usable capacity. This helps detect counterfeit or defective flash drives that silently discard data once their true limit is exceeded.

During a **Full Surface Scan**, the suite writes controlled test files across free space and reads them back to verify:
- All regions of the drive are real and readable
- No hidden capacity limits
- No controller failures
- No unstable regions under load

### 🧬 File Integrity (Bit‑Rot Detection)
The suite maintains a cryptographic baseline of all files, including:
- File paths
- File sizes
- Timestamps
- Hash values

**It detects:**  
- New, deleted, or modified files  
- **Corrupted files (bit rot):** files whose hash value has changed even though the timestamp remains identical  

If PAR2 recovery data exists, the suite can automatically repair corrupted files. Recovered files are written back safely, and corrupted originals are preserved with a `.1` suffix.

---

## Tasks Overview

1. **Scan for Bit Rot:** Compares all files against the stored baseline.  
2. **Re-Create Baseline:** Creates or updates the integrity baseline.  
3. **Re‑Generate Recovery Data:** Creates or updates PAR2 recovery files.  
4. **Calibrate:** Calibrates estimated times based on individual system performance metrics.  
5. **Quick Surface Scan:** Fast stability and performance sampling.  
6. **Full Surface Scan:** Sequential read/write validation across all free space.  
7. **Generate Report:** Useful for assessing health of the device over time.  
8. **View Logs:** Helpful for assessing technical issues.

---

## Recommended Usage

PUDIS automatically guides you through the required steps in the correct order.

- **First‑time setup:**  
  Calibrate Hardware → Re‑Create Baseline → Re‑Generate Recovery → Bit‑Rot Detection

- **Routine integrity checks:**  
  Run Bit‑Rot Detection to verify file integrity.

- **Drive health maintenance:**  
  Perform a Quick Surface Scan monthly and a Full Surface Scan every 6 months.

- **After adding, removing, or modifying files:**  
  Update the File Baseline and Re‑Generate Recovery Data.

**Note:** The software will automatically notify you if a required step has not yet been completed.

---

## Building From Source

To build from source:
1. Clone this repository.
2. Ensure you have a C++ compiler similar to  
   `Microsoft (R) C/C++ Optimizing Compiler Version 19.50.35730 for x64`
3. Compile the source code using the `compile.bat` script.
4. Review `manifest.md` to ensure your compiled binaries match the expected project structure.
5. Acquire the missing required 3rd‑party executables from their designated source locations.

---

## Contributing

Contributions are welcome. By submitting code, you agree that your contributions will be licensed under the MIT License. See `CONTRIBUTING.md` for details.

---

## Disclaimer

The software is provided **“AS IS”**, without warranty of any kind. You assume all risks associated with using the software. See the `LICENSE` and `AUA` for full details.

---

## Contact

For questions, suggestions, or feedback, please open an issue on GitHub.
