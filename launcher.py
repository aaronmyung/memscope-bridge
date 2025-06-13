import subprocess
import os
import sys
import time
import re
import ctypes

# This script orchestrates the startup of the entire application suite.
# It ensures that the C++ backend components are running and properly
# initialised before launching the main Python client application.

# --- Configuration ---
MEMORY_BRIDGE_EXE = "MemoryBridge.exe"
HANDLE_FORWARDER_EXE = "HandleForwarder.exe"
PYTHON_SCRIPT = "memory_overlay.py"

def main():
    ctypes.windll.kernel32.SetConsoleTitleW("Framework Launcher")
    print("--- IPC Launcher Initialising ---")

    # Verify that all required files exist before starting
    if not os.path.exists(MEMORY_BRIDGE_EXE):
        print(f"[FATAL] Memory Bridge executable '{MEMORY_BRIDGE_EXE}' not found.", file=sys.stderr)
        return
    if not os.path.exists(HANDLE_FORWARDER_EXE):
        print(f"[FATAL] Handle Forwarder executable '{HANDLE_FORWARDER_EXE}' not found.", file=sys.stderr)
        return
    if not os.path.exists(PYTHON_SCRIPT):
        print(f"[FATAL] Main Python script '{PYTHON_SCRIPT}' not found.", file=sys.stderr)
        return

    # 1. Launch the Memory Bridge. It will create a named pipe and wait.
    #    We capture its output to get its Process ID (PID).
    print(f"[*] Starting Memory Bridge process: {MEMORY_BRIDGE_EXE}")
    # CREATE_NEW_CONSOLE allows it to run in its own window for debug output.
    # For a fully silent launch, one could use subprocess.CREATE_NO_WINDOW and
    # redirect output to a log file.
    bridge_process = subprocess.Popen(
        [MEMORY_BRIDGE_EXE], 
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        creationflags=subprocess.CREATE_NEW_CONSOLE
    )

    # 2. Read the Memory Bridge's stdout to find its PID.
    bridge_pid = None
    print("[*] Waiting for Memory Bridge PID...")
    try:
        # Read line-by-line until we find the PID announcement.
        for line in iter(bridge_process.stdout.readline, ''):
            print(f"    (Bridge): {line.strip()}")
            match = re.search(r"Memory Bridge PID: (\d+)", line)
            if match:
                bridge_pid = int(match.group(1))
                print(f"[+] Memory Bridge PID captured: {bridge_pid}")
                break
        if bridge_pid is None:
             raise RuntimeError("Could not capture Memory Bridge PID from its output.")
    except Exception as e:
        print(f"[FATAL] Failed to get PID from Memory Bridge process: {e}", file=sys.stderr)
        bridge_process.terminate()
        return

    # 3. Launch the Handle Forwarder, passing the Memory Bridge's PID as an argument.
    print(f"[*] Starting Handle Forwarder: {HANDLE_FORWARDER_EXE} for bridge PID {bridge_pid}")
    forwarder_process = subprocess.Popen(
        [HANDLE_FORWARDER_EXE, str(bridge_pid)],
        creationflags=subprocess.CREATE_NEW_CONSOLE # The forwarder can also have its own console
    )
    
    # The forwarder will find the target process, duplicate the handle, send it to
    # the bridge, and then exit. We wait for it to complete its task.
    forwarder_process.wait(timeout=30)
    if forwarder_process.returncode != 0:
        print("[FATAL] Handle Forwarder process failed. Check its logs or run it manually.", file=sys.stderr)
        bridge_process.terminate()
        return
    print("[+] Handle Forwarder has completed its task.")
    
    # 4. The Memory Bridge now has its handle. We can launch the main Python script.
    print(f"[*] All prerequisites met. Starting main application: {PYTHON_SCRIPT}")
    time.sleep(1) # Sleep a moment for all processes to be fully ready.

    # The main Python script runs in this same console.
    # We use subprocess.run() which waits for the script to complete.
    python_process = subprocess.run([sys.executable, PYTHON_SCRIPT])
    
    # 5. Cleanup
    print("[*] Main application terminated. Cleaning up background processes...")
    bridge_process.terminate()
    print("--- Shutdown Complete ---")


if __name__ == "__main__":
    main()