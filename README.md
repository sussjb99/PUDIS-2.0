# PUDIS-2.0
PUDIS 2.0 (Portable USB Drive Integrity Suite)

PUDIS (Portable-USB-Drive-Integrity-Suite) is a maintenance and monitoring toolkit designed to keep external storage devices healthy over prolonged periods of time. It achieves this object by performing two major tasks:

- **File Integrity Testing (Bit‑Rot Detection)**
- **Surface Analysis Testing (Performance & Stability Scans)**

The suite is designed to work with USB flash drives and portable exteranal HDDs/SSD devices. 

This software is designed to be small in size and should be installed and run directly from the external storage devices.

This software was written in C++

---

## 🛡️ Security & Integrity

- To retrieve SMART data from USB devices that support it, this software requires elevated administrator permissions.
- Standard USB flash drives do not provide SMART data, so they do not require elevation.
- Permission requirements are controlled through the config.ini setting:
```ini
[Permissions]
RequireAdmin=1
```


To ensure your safety and maintain full transparency:
- **Verified Hashes:** Every binary and script in this suite is documented in the [manifest.md](./manifest.md) file with its corresponding **SHA-256 hash**.
- **Self-Signed:** As this is an independent project currently in the funding phase, files are self-signed. You can manually verify the integrity of any file using the hashes provided in the manifest.
- **Safety First:** The software is designed to identify and target removable drives. It will **never** perform a stress test on your system drive (C:).

---

## Support the Project

PUDIS is an independent, open‑source project built to help people keep their data safe.  
If this tool has helped you detect issues, avoid data loss, or simply feel more confident about your drives, you can support ongoing development here:

[☕ Support on Ko‑fi](https://ko-fi.com/sussjb99)

Every contribution helps fund testing hardware, new features, and the procurement of a professional code-signing certificate to reduce antivirus false positives.

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
- New, deleted, or modified files.
- **Corrupted files (bit rot):** Files whose hash value has changed even though the timestamp remains identical.

If PAR2 recovery data exists, the suite can automatically repair corrupted files. Recovered files are written back safely, and corrupted originals are preserved with a `.1` suffix.

---

## Tasks Overview

1. **Scan for Bit Rot:** Compares all files against the stored baseline.
2. **Re-Create Baseline:** Creates or updates the integrity baseline.
3. **Re‑Generate Recovery Data:** Creates or updates PAR2 recovery files.
4. **Calibrate:** Calibrates estimated times based on individual system performance metrics
5. **Quick Surface Scan:** Fast stability and performance sampling.
6. **Full Surface Scan:** Sequential read/write validation across all free space.
7. **Generate Report** Useful for assessing health of device over time.
8. **VIew Logs** Helpful for assessing technical issues.

---

## Recommended Usage

PUDIS automatically guides you through the required steps in the correct order.

- **First‑time setup:** <br>
  Calibrate Hardware → Re‑Create Baseline → Re‑Generate Recovery → Bit‑Rot Detection

- **Routine integrity checks:** <br>
  Run Bit‑Rot Detection to verify file integrity.

- **Drive health maintenance:** <br>
  Perform a Quick Surface Scan monthly and a Full Surface Scan every 6 months.

- **After adding, removing, or modifying files:** <br>
  Update the File Baseline and Re‑Generate Recovery Data.
   
**Note** <br> The software will automatically notify you if a required step has not yet been completed.

---

## Building From Source

To build from source:
1. Clone this repository.
2. Ensure you have a C++ compiler similar to "Microsoft (R) C/C++ Optimizing Compiler Version 19.50.35730 for x64"
3. Review the scripts in `Integrity_Check/scripts/` to understand the execution flow.
4. Review `manifest.md` to ensure your compiled binaries match the expected project structure.

---

## Contributing

Contributions are welcome. By submitting code, you agree that your contributions will be licensed under the MIT License. See `CONTRIBUTING.md` for details.

---

## Disclaimer

The software is provided **“AS IS”**, without warranty of any kind. You assume all risks associated with using the software. See the `LICENSE` and `AUA` for full details.

---

## Contact

For questions, suggestions, or feedback, please open an issue on GitHub.
