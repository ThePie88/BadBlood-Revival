"""
Live memory scanner for BadBloodGame.exe
Searches process memory for URLs, domains, connection strings.
Created by MrPie — DLBB Revival Project
"""
import ctypes
import ctypes.wintypes as wt
import struct
import re
import sys

# Windows API constants
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400
MEM_COMMIT = 0x1000
# Accept any protection that isn't PAGE_NOACCESS (0x01) or PAGE_GUARD (0x100)
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

kernel32 = ctypes.windll.kernel32

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_uint64),
        ("AllocationBase", ctypes.c_uint64),
        ("AllocationProtect", wt.DWORD),
        ("_pad1", wt.DWORD),
        ("RegionSize", ctypes.c_uint64),
        ("State", wt.DWORD),
        ("Protect", wt.DWORD),
        ("Type", wt.DWORD),
        ("_pad2", wt.DWORD),
    ]


def get_pid_by_name(name):
    import subprocess
    out = subprocess.check_output(
        ["powershell", "-Command",
         f"(Get-Process -Name '{name}' -ErrorAction SilentlyContinue).Id"],
        text=True
    ).strip()
    if out:
        return int(out.split()[0])
    return None


def scan_memory(pid, patterns):
    """Scan process memory for byte patterns."""
    access = PROCESS_VM_READ | PROCESS_QUERY_INFORMATION
    handle = kernel32.OpenProcess(access, False, pid)
    err = ctypes.GetLastError()
    print(f"OpenProcess({pid}, access=0x{access:X}) -> handle={handle}, err={err}")
    if not handle:
        print(f"Cannot open process {pid}: error {err}")
        return []

    results = []
    mbi = MEMORY_BASIC_INFORMATION()
    address = 0
    max_addr = 0x7FFFFFFFFFFF  # x64 user space

    regions_scanned = 0
    bytes_scanned = 0

    while address < max_addr:
        ret = kernel32.VirtualQueryEx(
            handle, ctypes.c_void_p(address), ctypes.byref(mbi), ctypes.sizeof(mbi)
        )
        if ret == 0:
            break

        prot = mbi.Protect or 0
        if (mbi.State == MEM_COMMIT and
            prot != PAGE_NOACCESS and
            not (prot & PAGE_GUARD) and
            mbi.RegionSize < 256 * 1024 * 1024):  # skip regions > 256MB

            buf = ctypes.create_string_buffer(mbi.RegionSize)
            bytes_read = ctypes.c_size_t(0)
            ok = kernel32.ReadProcessMemory(
                handle, ctypes.c_void_p(mbi.BaseAddress),
                buf, mbi.RegionSize, ctypes.byref(bytes_read)
            )
            if ok and bytes_read.value > 0:
                data = buf.raw[:bytes_read.value]
                regions_scanned += 1
                bytes_scanned += bytes_read.value

                for name, pattern in patterns:
                    for m in pattern.finditer(data):
                        # Extract context around match
                        start = max(0, m.start() - 32)
                        end = min(len(data), m.end() + 128)
                        ctx = data[start:end]
                        # Clean to printable ASCII
                        printable = bytes(b if 32 <= b < 127 else 46 for b in ctx).decode('ascii')
                        addr = mbi.BaseAddress + m.start()
                        results.append((name, addr, printable, ctx))

        address = mbi.BaseAddress + mbi.RegionSize
        if address <= mbi.BaseAddress:
            break

    kernel32.CloseHandle(handle)
    print(f"Scanned {regions_scanned} regions, {bytes_scanned / 1024 / 1024:.1f} MB")
    return results


def main():
    pid = get_pid_by_name("BadBloodGame")
    if not pid:
        print("BadBloodGame.exe not running!")
        sys.exit(1)

    print(f"Found BadBloodGame.exe PID: {pid}")
    print("Scanning memory...")

    patterns = [
        ("URL_HTTPS", re.compile(rb'https?://[a-zA-Z0-9._\-/:%?&=]{5,200}')),
        ("DLBB_DOMAIN", re.compile(rb'[a-zA-Z0-9.-]*dlbb\.com[a-zA-Z0-9./:_\-?&=]*')),
        ("PLS_HEADER", re.compile(rb'PLS-[A-Za-z\-]+:[^\x00]{1,100}')),
        ("AUTH_PATH", re.compile(rb'/auth/login/[a-z]+/')),
        ("DLBB_PATH", re.compile(rb'/dlbb/[a-z]+[a-zA-Z0-9/]*')),
        ("REST_URL", re.compile(rb'rest[a-zA-Z]*\.?[a-zA-Z]*\.[a-z]{2,4}[:/]')),
        ("CURL_ERR", re.compile(rb'CURL Error[^\x00]{1,100}')),
        ("SCHANNEL", re.compile(rb'schannel:[^\x00]{1,150}')),
        ("HOST_HDR", re.compile(rb'Host: [a-zA-Z0-9._\-:]+', re.IGNORECASE)),
    ]

    results = scan_memory(pid, patterns)

    # Deduplicate and sort
    seen = set()
    unique = []
    for name, addr, printable, raw in results:
        key = (name, printable[:80])
        if key not in seen:
            seen.add(key)
            unique.append((name, addr, printable))

    # Print results grouped by type
    print(f"\n{'='*80}")
    print(f"Found {len(unique)} unique matches")
    print(f"{'='*80}\n")

    for group_name in ["URL_HTTPS", "DLBB_DOMAIN", "PLS_HEADER", "AUTH_PATH",
                        "DLBB_PATH", "HOST_HDR", "CURL_ERR", "SCHANNEL", "REST_URL"]:
        group = [(n, a, p) for n, a, p in unique if n == group_name]
        if group:
            print(f"\n--- {group_name} ({len(group)} hits) ---")
            for name, addr, printable in group[:30]:
                print(f"  0x{addr:016X}: {printable[:120]}")


if __name__ == "__main__":
    main()
