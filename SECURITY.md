# Security Policy for PUDIS‑2.0

PUDIS‑2.0 (Portable USB Drive Integrity Suite) is designed with safety, transparency, and user control as core principles. This document explains how the software handles security‑sensitive operations, how users can verify the authenticity of releases, and how to report potential vulnerabilities.

---

## 🔐 Trust Model

PUDIS‑2.0 is designed to operate **only on removable storage devices**.  
The software enforces the following security boundaries:

- It will **never** perform destructive operations on the system drive (C:).
- It will **never** modify files unless the user explicitly selects an operation that requires it.
- It does **not** transmit data over the network.
- It does **not** collect telemetry or analytics.
- All operations are performed locally on the user’s machine.

These boundaries are intentional and considered part of the project’s security guarantees.

---

## ⚠️ Windows SmartScreen & Antivirus Warnings

Because PUDIS‑2.0 is an independent open‑source project and does **not yet include a commercial code‑signing certificate**, Windows may display the following warning when launching the executable:

> “Microsoft Defender SmartScreen prevented an unrecognized app from starting.”

This occurs because:
- Unsigned applications are automatically classified as “unknown publisher.”
- SmartScreen reputation is based on download volume and certificate trust.
- Self‑signed certificates **do not** establish SmartScreen reputation.

### How to run the application safely

1. Click **More info**  
2. Click **Run anyway**

### Why this is safe

You can verify the authenticity of the executable using the SHA‑256 hashes in `manifest.md`.  
If the hash matches, the file has not been tampered with.

A trusted code‑signing certificate is planned once project funding allows.

---

## 🧪 Antivirus False Positives

Tools that perform:
- direct disk access  
- SMART queries  
- raw read/write operations  

…are sometimes flagged by heuristic antivirus engines.

This does **not** indicate malicious behavior.

### If you encounter a false positive

- Verify the SHA‑256 hash against `manifest.md`
- Submit the file to your antivirus vendor as a false positive
- Optionally report the issue to this repository (see below)

---

## 🔍 Verifying File Integrity (SHA‑256)

### Windows PowerShell
```powershell
Get-FileHash .\PUDIS.exe -Algorithm SHA256
