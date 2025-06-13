#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <vector>

// --- Configuration ---
const char targetProcName[] = { 'e', 'x', 'a', 'm', 'p', 'l', 'e', '_', 'a', 'p', 'p', '.', 'e', 'x', 'e', '\0' };
const char handlePipeName[] = { '\\', '\\', '.', '\\', 'p', 'i', 'p', 'e', '\\', 'D', 'e', 'm', 'o', 'H', 'a', 'n', 'd', 'l', 'e', 'P', 'i', 'p', 'e', '\0' };

// Utility to find a process ID by its executable name.
DWORD FindProcessId(const std::string& processName) {
    PROCESSENTRY32 processInfo;
    processInfo.dwSize = sizeof(processInfo);

    HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processesSnapshot == INVALID_HANDLE_VALUE) return 0;

    if (Process32First(processesSnapshot, &processInfo)) {
        do {
            if (processName.compare(processInfo.szExeFile) == 0) {
                CloseHandle(processesSnapshot);
                return processInfo.th32ProcessID;
            }
        } while (Process32Next(processesSnapshot, &processInfo));
    }
    
    CloseHandle(processesSnapshot);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Error: Missing target Memory Bridge PID argument." << std::endl;
        return 1;
    }

    DWORD bridgePid = std::stoul(argv[1]);
    if (bridgePid == 0) {
        std::cerr << "Error: Invalid Memory Bridge PID provided." << std::endl;
        return 1;
    }
    
    std::cout << "Handle Forwarder: Targeting Memory Bridge process with PID: " << bridgePid << std::endl;

    DWORD targetPid = 0;
    std::cout << "Handle Forwarder: Searching for target process '" << targetProcName << "'..." << std::endl;
    while (targetPid == 0) {
        targetPid = FindProcessId(targetProcName);
        Sleep(500);
    }
    std::cout << "Handle Forwarder: Found " << targetProcName << " with PID: " << targetPid << std::endl;

    // Open a handle to the target application with desired permissions.
    HANDLE hTargetOriginal = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, targetPid);
    if (hTargetOriginal == NULL) {
        std::cerr << "Handle Forwarder: Failed to open handle to target process. Error: " << GetLastError() << std::endl;
        return 1;
    }
    std::cout << "Handle Forwarder: Successfully opened handle to target process." << std::endl;

    // Open a handle to the Memory Bridge process, requesting permission to duplicate a handle into it.
    HANDLE hMemoryBridge = OpenProcess(PROCESS_DUP_HANDLE, FALSE, bridgePid);
    if (hMemoryBridge == NULL) {
        std::cerr << "Handle Forwarder: Failed to open handle to MemoryBridge.exe. Error: " << GetLastError() << std::endl;
        CloseHandle(hTargetOriginal);
        return 1;
    }

    HANDLE hTargetDuplicated = NULL;
    if (!DuplicateHandle(
            GetCurrentProcess(),      // Source process
            hTargetOriginal,          // Handle to duplicate
            hMemoryBridge,            // Target process to grant access
            &hTargetDuplicated,       // New handle value, valid in the target process
            0,
            FALSE,
            DUPLICATE_SAME_ACCESS
    )) {
        std::cerr << "Handle Forwarder: Failed to duplicate handle. Error: " << GetLastError() << std::endl;
        CloseHandle(hTargetOriginal);
        CloseHandle(hMemoryBridge);
        return 1;
    }
    std::cout << "Handle Forwarder: Successfully duplicated handle for Memory Bridge process." << std::endl;

    // Connect to the named pipe hosted by the Memory Bridge to send the handle.
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    while (hPipe == INVALID_HANDLE_VALUE) {
        hPipe = CreateFileA(handlePipeName, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hPipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY) {
                std::cerr << "Handle Forwarder: Could not open pipe. Is MemoryBridge.exe running? Error: " << GetLastError() << std::endl;
            }
            Sleep(250); 
        }
    }
    
    // Write the duplicated handle value to the pipe.
    DWORD bytesWritten;
    if (!WriteFile(hPipe, &hTargetDuplicated, sizeof(hTargetDuplicated), &bytesWritten, NULL)) {
        std::cerr << "Handle Forwarder: Failed to write handle to pipe. Error: " << GetLastError() << std::endl;
    } else {
        std::cout << "Handle Forwarder: Handle sent successfully. Exiting." << std::endl;
    }

    // Clean up
    CloseHandle(hTargetOriginal);
    CloseHandle(hMemoryBridge);
    CloseHandle(hPipe);

    return 0;
}