/* ============================================================
   PUDIS-2.0 (Portable USB Drive Integrity Suite)
   File: corruptor.cpp
   Author: sussjb99
   Version: 2.0
   Last Modified: 2026-04-12
   Copyright (c) 2026 sussjb99. All rights reserved.
   Licensed under the MIT License. See LICENSE.txt for details.

   Purpose: In order to confirm the system is working properly
            we need to generated sample corrupted files to test
            if the system can detect and repair them.
   ============================================================ */


#include <windows.h>
#include <iostream>
#include <string>

void corrupt_fat32_file(const std::string& filePath) {
    // 1. Open the file with GENERIC_READ and GENERIC_WRITE
    HANDLE hFile = CreateFileA(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        std::cerr << "Error: Could not open file. Check if the USB is plugged in." << std::endl;
        return;
    }

    // 2. Capture the original timestamps
    FILETIME ftCreate, ftAccess, ftWrite;
    if (!GetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite)) {
        std::cerr << "Error: Could not retrieve timestamps." << std::endl;
        CloseHandle(hFile);
        return;
    }

    // 3. Read and Flip a bit
    DWORD bytesRead, bytesWritten;
    unsigned char buffer[1];
    
    if (ReadFile(hFile, buffer, 1, &bytesRead, NULL) && bytesRead > 0) {
        buffer[0] ^= 0x01; // Flip the first bit of the first byte
        
        // Move pointer back to the beginning to overwrite
        SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
        WriteFile(hFile, buffer, 1, &bytesWritten, NULL);
        
        std::cout << "Bit flipped. Original hash is now invalid." << std::endl;
    }

    // 4. Force the old timestamps back onto the file
    // This prevents the OS from updating 'mtime' to "Now"
    if (SetFileTime(hFile, &ftCreate, &ftAccess, &ftWrite)) {
        std::cout << "Success: Original timestamps restored (FAT32 2s precision applied)." << std::endl;
    } else {
        std::cerr << "Error: Could not restore timestamps." << std::endl;
    }

    CloseHandle(hFile);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: corruptor.exe <path_to_usb_file>" << std::endl;
        return 1;
    }
    corrupt_fat32_file(argv[1]);
    return 0;
}