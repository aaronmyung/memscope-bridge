# memscope-bridge | Multi-Process Memory Reading Framework

This project is a demonstration of a robust, multi-process architecture for reading memory from a target application. It decouples the low-level memory access logic (C++) from the high-level application logic (Python) using Inter-Process Communication (IPC) with named pipes.

This design pattern is commonly used in advanced systems development where isolating low-level operations from high-level logic improves modularity, stability, and maintainability. By using IPC to separate memory access from application logic, it enables safer experimentation, clearer boundaries between components, and easier cross-language integration — making it suitable for tooling, automation, or diagnostics in performance-critical environments.

## Core Concepts

The framework is built on a few key principles:

1.  **Separation of Concerns**:
    *   The **Memory Bridge** (`MemoryBridge.exe`) is a C++ application responsible for all direct interactions with the target process's memory. It holds the process handle and executes read commands.
    *   The **Python Client** (`memory_overlay.py`) contains the high-level logic. It decides *what* memory to read and *how* to interpret/display it, but it never directly accesses the target process.
    *   This separation means the Python script can be reloaded or modified on the fly without restarting the core memory-reading component.

2.  **Handle Forwarding**:
    *   Instead of having the `MemoryBridge` find the target process and open a handle itself, this task is delegated to a separate, short-lived utility: the `HandleForwarder`.
    *   The `HandleForwarder` finds the target process, opens a handle with the necessary permissions, and then uses the `DuplicateHandle` WinAPI function to "give" a copy of that handle to the `MemoryBridge` process.
    *   This is a common technique to compartmentalise privileges and responsibilities. The `HandleForwarder`'s only job is to provide this handle, after which it terminates.

3.  **IPC via Named Pipes**:
    *   The framework uses two separate named pipes for communication:
        *   **Handle Pipe (`\\.\pipe\DemoHandlePipe`)**: A temporary, one-way pipe used by the `HandleForwarder` to send the duplicated process handle to the `MemoryBridge`.
        *   **Main IPC Pipe (`\\.\pipe\memory_pipe_main`)**: A persistent, duplex pipe for the main command-and-control communication between the Python client and the `MemoryBridge`.

## Components

The project consists of four main files:

| File                  | Language | Role                                                                                                                                                             |
| --------------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `launcher.py`         | Python   | **Orchestrator**. The main entry point. It launches the C++ components in the correct order, manages their PIDs, and finally starts the Python client.             |
| `MemoryBridge.cpp`    | C++      | **The Server/Backend**. A long-running process that holds the handle to the target application and listens for memory read commands from the Python client via a named pipe. |
| `HandleForwarder.cpp` | C++      | **One-Shot Utility**. Finds the target process, gets a handle, duplicates it for the `MemoryBridge`, sends it over a pipe, and then exits.                  |
| `memory_overlay.py`   | Python   | **The Client/Frontend**. Connects to the `MemoryBridge` and sends requests to read memory. This is where you would build your application/overlay logic.       |

## Execution Flow

The `launcher.py` script ensures all components are started and connected in the correct sequence.

1.  `launcher.py` starts `MemoryBridge.exe`.
2.  `MemoryBridge.exe` creates the *handle pipe*, prints its own Process ID (PID) to the console, and waits for a connection.
3.  `launcher.py` captures the PID from the `MemoryBridge`'s output.
4.  `launcher.py` starts `HandleForwarder.exe`, passing the `MemoryBridge`'s PID as a command-line argument.
5.  `HandleForwarder.exe` finds the target process (`example_app.exe`).
6.  It opens a handle to the target and duplicates it into the `MemoryBridge` process.
7.  It connects to the *handle pipe* and writes the value of the new handle.
8.  `HandleForwarder.exe` completes its job and exits.
9.  `MemoryBridge.exe` receives the handle, closes the *handle pipe*, and creates the main *IPC pipe* for the Python client. It then waits for a connection on this new pipe.
10. `launcher.py` starts `memory_overlay.py`.
11. `memory_overlay.py` connects to the main *IPC pipe*.
12. The Python client sends an `INIT` command. `MemoryBridge` responds with the base address of the target module (`main.dll`).
13. The Python client enters a loop, sending `READ` commands for specific memory addresses. The `MemoryBridge` reads the memory and sends the data back.
14. When `memory_overlay.py` is closed, `launcher.py` terminates the `MemoryBridge.exe` process to clean up.

## Prerequisites

*   **OS**: Windows
*   **Compiler**: A C++ compiler for Window.
*   **Python**: Python 3.x.
*   **Python Packages**:
    ```shell
    pip install pywin32
    ```

## How to Build

You can compile the C++ executables using g++ (MinGW-w64) or the Visual Studio compiler.

#### Using g++:

```sh
# Compile the Memory Bridge
g++ -o MemoryBridge.exe MemoryBridge.cpp -static -lntdll

# Compile the Handle Forwarder
g++ -o HandleForwarder.exe HandleForwarder.cpp -static
```

## Ethical Use Notice

This project is a technical proof-of-concept, designed for safe environments and educational inspection. It does not ship with any target offsets or behaviour logic.
