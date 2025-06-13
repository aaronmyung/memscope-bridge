#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <cstdint>
#include <vector>

// This macro is standard in the Windows DDK/SDK but may be missing in some MinGW setups.
// An NTSTATUS is successful if the value is non-negative.
#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

// --- Configuration ---
const char modName[] = { 'm', 'a', 'i', 'n', '.', 'd', 'l', 'l', '\0' };
const char handlePipeName[] = { '\\', '\\', '.', '\\', 'p', 'i', 'p', 'e', '\\', 'D', 'e', 'm', 'o', 'H', 'a', 'n', 'd', 'l', 'e', 'P', 'i', 'p', 'e', '\0' }; 
const char pythonPipeName[] = { '\\', '\\', '.', '\\', 'p', 'i', 'p', 'e', '\\', 'm', 'e', 'm', 'o', 'r', 'y', '_', 'p', 'i', 'p', 'e', '_', 'm', 'a', 'i', 'n', '\0' };

// Function pointer for NtReadVirtualMemory for direct system calls.
using pNtReadVirtualMemory = NTSTATUS(NTAPI*)(HANDLE, PVOID, PVOID, ULONG, PULONG);

// IPC Command Protocol with Python client
enum class Command : uint8_t { INIT = 0x01, READ = 0x02 };
#pragma pack(push, 1)
struct ReadRequest { uint64_t address; uint32_t size; };
struct InitResponse { uint64_t moduleBaseAddress; };
#pragma pack(pop)

// Utility to find the base address of a loaded module in a given process.
uintptr_t GetModuleBaseAddress(HANDLE hProcess, const std::string& modName) {
    // Note: To read modules from another 32-bit process from a 64-bit process (or vice versa),
    // TH32CS_SNAPMODULE32 must be included.
    DWORD procId = GetProcessId(hProcess);
    if (procId == 0) return 0;
    
    MODULEENTRY32 modEntry;
    modEntry.dwSize = sizeof(modEntry);
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    if (Module32First(hSnap, &modEntry)) {
        do {
            if (!_stricmp(modEntry.szModule, modName.c_str())) {
                CloseHandle(hSnap);
                return (uintptr_t)modEntry.modBaseAddr;
            }
        } while (Module32Next(hSnap, &modEntry));
    }
    CloseHandle(hSnap);
    return 0;
}

// Main loop to handle communication with the Python client.
void HandlePythonConnection(HANDLE hPipe, HANDLE hProcess, pNtReadVirtualMemory NtRead) {
    char buffer[1024];
    DWORD bytesRead;

    while (ReadFile(hPipe, buffer, sizeof(buffer), &bytesRead, NULL) != FALSE) {
        if (bytesRead == 0) continue;

        Command cmd = *reinterpret_cast<Command*>(buffer);
        if (cmd == Command::INIT) {
            uintptr_t moduleBase = GetModuleBaseAddress(hProcess, modName);
            InitResponse response = { moduleBase };
            DWORD bytesWritten;
            WriteFile(hPipe, &response, sizeof(response), &bytesWritten, NULL);
        } else if (cmd == Command::READ) {
            ReadRequest* request = reinterpret_cast<ReadRequest*>(buffer + 1);
            std::vector<BYTE> read_buffer(request->size);
            SIZE_T numBytesRead = 0;
            NTSTATUS status = NtRead(hProcess, (PVOID)request->address, read_buffer.data(), request->size, (PULONG)&numBytesRead);
            
            BYTE response_status = (NT_SUCCESS(status) && numBytesRead == request->size) ? 0 : 1;
            DWORD bytesWritten;

            if (response_status == 0) {
                // On success, send status byte (0) followed by the data.
                std::vector<BYTE> response_payload;
                response_payload.reserve(1 + request->size);
                response_payload.push_back(response_status); // Status byte
                response_payload.insert(response_payload.end(), read_buffer.begin(), read_buffer.end()); // Data
                WriteFile(hPipe, response_payload.data(), response_payload.size(), &bytesWritten, NULL);
            } else {
                // On failure, just send the failure status byte (1).
                WriteFile(hPipe, &response_status, 1, &bytesWritten, NULL);
            }
        }
        FlushFileBuffers(hPipe);
    }
    DisconnectNamedPipe(hPipe);
}

// Sets up a pipe to wait for and receive the process handle from the forwarder.
HANDLE ReceiveHandleFromForwarder() {
    HANDLE hPipe = CreateNamedPipeA(
        handlePipeName, PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_WAIT,
        1, sizeof(HANDLE), sizeof(HANDLE), 0, NULL);
    
    if (hPipe == INVALID_HANDLE_VALUE) {
        std::cerr << "Memory Bridge: Failed to create handle transfer pipe. Error: " << GetLastError() << std::endl;
        return NULL;
    }

    std::cout << "Memory Bridge: Handle pipe created. Waiting for forwarder to connect..." << std::endl;
    if (!ConnectNamedPipe(hPipe, NULL)) {
        std::cerr << "Memory Bridge: Forwarder failed to connect to handle pipe. Error: " << GetLastError() << std::endl;
        CloseHandle(hPipe);
        return NULL;
    }

    std::cout << "Memory Bridge: Handle forwarder connected." << std::endl;
    HANDLE receivedHandle = NULL;
    DWORD bytesRead;
    if (!ReadFile(hPipe, &receivedHandle, sizeof(receivedHandle), &bytesRead, NULL) || bytesRead != sizeof(receivedHandle)) {
        std::cerr << "Memory Bridge: Failed to read handle from pipe. Error: " << GetLastError() << std::endl;
        CloseHandle(hPipe);
        return NULL;
    }
    
    std::cout << "Memory Bridge: Received handle from forwarder." << std::endl;
    
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
    
    return receivedHandle;
}

int main() {
    DWORD myPid = GetCurrentProcessId();
    std::cout << "Memory Bridge PID: " << myPid << std::endl;
    std::cout.flush(); // Ensures the launcher can read the PID immediately.

    auto NtRead = (pNtReadVirtualMemory)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtReadVirtualMemory");
    if (!NtRead) {
        std::cerr << "Memory Bridge: Failed to get address of NtReadVirtualMemory" << std::endl;
        return 1;
    }

    HANDLE hProcess = ReceiveHandleFromForwarder();
    if (hProcess == NULL) {
        std::cerr << "Memory Bridge: Failed to receive a valid process handle. Exiting." << std::endl;
        return 1;
    }
    
    DWORD targetProcId = GetProcessId(hProcess);
    std::cout << "Memory Bridge: Now operating with handle to process PID: " << targetProcId << std::endl;

    std::cout << "Memory Bridge: Waiting for module '" << modName << "' to load in target process..." << std::endl;
    uintptr_t moduleBaseAddress = 0;
    while(moduleBaseAddress == 0) {
        moduleBaseAddress = GetModuleBaseAddress(hProcess, modName);
        Sleep(500);
    }
    std::cout << "Memory Bridge: Module '" << modName << "' found at: 0x" << std::hex << moduleBaseAddress << std::dec << std::endl;

    std::cout << "Memory Bridge: Initialised. Creating Python IPC channel..." << std::endl;
    while (true) {
        HANDLE hPythonPipe = CreateNamedPipeA(
            pythonPipeName, PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 1024 * 16, 1024 * 16, 0, NULL);
        if (hPythonPipe == INVALID_HANDLE_VALUE) { return 1; }

        std::cout << "Memory Bridge: Python pipe server listening..." << std::endl;
        if (ConnectNamedPipe(hPythonPipe, NULL) != FALSE) {
            std::cout << "Memory Bridge: Python client connected." << std::endl;
            HandlePythonConnection(hPythonPipe, hProcess, NtRead);
            std::cout << "Memory Bridge: Python client disconnected." << std::endl;
        }
        CloseHandle(hPythonPipe);
    }

    CloseHandle(hProcess);
    return 0;
}