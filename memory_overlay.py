import struct
import sys
import time
import traceback
import argparse

import win32pipe
import win32file

PIPE_NAME = "memory_pipe_main"
DUMMY_OFFSET_1 = 0x123456
DUMMY_OFFSET_2 = 0x789ABC

class MemoryInterface:
    """
    A Python client for the C++ MemoryBridge. This class encapsulates all
    IPC logic, providing a clean, high-level interface for memory operations.
    """

    def __init__(self, pipe_name):
        self.pipe_name = f"\\\\.\\pipe\\{pipe_name}"
        self.pipe_handle = None

    def connect(self):
        """Waits for the C++ pipe server and connects to it."""
        try:
            win32pipe.WaitNamedPipe(self.pipe_name, 10000)
            self.pipe_handle = win32file.CreateFile(
                self.pipe_name,
                win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                0, None,
                win32file.OPEN_EXISTING,
                0, None
            )
            return True
        except Exception as e:
            print(f"Failed to connect to pipe '{self.pipe_name}': {e}", file=sys.stderr)
            self.pipe_handle = None
            return False

    def _send_request(self, command, data=b''):
        """Internal method to send a command and data, and get a response."""
        if self.pipe_handle is None:
            raise ConnectionError("IPC pipe is not connected.")

        try:
            request = command + data
            win32file.WriteFile(self.pipe_handle, request)
            return win32file.ReadFile(self.pipe_handle, 4096)
        except Exception as e:
            print(f"Pipe communication error: {e}", file=sys.stderr)
            self.close_pipe()
            raise IOError("IPC request failed.") from e

    def initialize(self):
        """Sends INIT command and retrieves the target module's base address."""
        print("Requesting initial context from Memory Bridge...")
        init_command = struct.pack('B', 0x01)
        err, response_data = self._send_request(init_command)

        if err != 0 or len(response_data) != 8:
            raise RuntimeError("Failed to get initial context from memory bridge.")

        module_base = struct.unpack('Q', response_data)[0]
        return module_base

    def read_bytes(self, address, size):
        """Requests a memory read via command 0x02."""
        read_command = struct.pack('B', 0x02)
        request_data = struct.pack('QI', address, size)

        try:
            err, response = self._send_request(read_command, request_data)
        except IOError:
            return b''

        if not response or response[0] != 0:
            return b''

        return response[1:]

    def read_int(self, address):
        """Reads a 4-byte signed integer from the target process."""
        data = self.read_bytes(address, 4)
        return struct.unpack('i', data)[0] if len(data) == 4 else None

    def close_pipe(self):
        """Closes the connection to the named pipe."""
        if self.pipe_handle:
            win32file.CloseHandle(self.pipe_handle)
            self.pipe_handle = None
            print("IPC pipe connection closed.")

def main_loop(mem_handler, module_base, run_forever=False):
    """
    Continuously reads and displays memory values from two dummy offsets.
    """
    print("Starting memory read loop. Press Ctrl+C to exit.")
    count = 0

    while run_forever or count < 10:
        try:
            address1 = module_base + DUMMY_OFFSET_1
            value1 = mem_handler.read_int(address1)

            address2 = module_base + DUMMY_OFFSET_2
            value2 = mem_handler.read_int(address2)

            print(
                f"[{time.strftime('%H:%M:%S')}] "
                f"Read {hex(address1)} → {value1 if value1 is not None else 'FAIL'} | "
                f"Read {hex(address2)} → {value2 if value2 is not None else 'FAIL'}"
            )

            count += 1
            time.sleep(2)

        except IOError:
            print("\nPipe communication lost. Aborting.", file=sys.stderr)
            break
        except KeyboardInterrupt:
            print("\nLoop interrupted by user.")
            break

# --- Entry Point ---
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--loop', action='store_true', help='Run indefinitely instead of 10 reads')
    args = parser.parse_args()

    mem_handler = None

    try:
        mem_handler = MemoryInterface(PIPE_NAME)
        print(f"Attempting to connect to pipe: \\\\.\\pipe\\{PIPE_NAME}...")
        if not mem_handler.connect():
            raise ConnectionError("Could not connect to the Memory Bridge's named pipe.\n"
                                  "Please ensure MemoryBridge.exe is running.")
        print("Connection successful.")

        module_base = mem_handler.initialize()
        if module_base == 0:
            raise RuntimeError("Memory Bridge returned null base address. "
                               "Is the target application running?")

        print(f"Module base address: {hex(module_base)}")
        print("-" * 50)
        main_loop(mem_handler, module_base, run_forever=args.loop)

    except (ConnectionError, RuntimeError) as e:
        print(f"\n[FATAL ERROR] {e}", file=sys.stderr)
    except Exception:
        print("\nUnexpected error occurred:", file=sys.stderr)
        traceback.print_exc()
    finally:
        if mem_handler:
            mem_handler.close_pipe()
        print("Script has shut down.")