#!/usr/bin/env python3
"""Wrapper script to set up ESP-IDF environment and run idf.py commands."""
import os
import sys
import subprocess

# ESP-IDF environment setup
os.environ["IDF_PATH"] = r"D:\ProgramDev\ESP-IDF\.espressif\v6.0.1\esp-idf"
os.environ["IDF_TOOLS_PATH"] = r"C:\Espressif\tools"
os.environ["IDF_PYTHON_ENV_PATH"] = r"C:\Espressif\tools\python\v6.0.1\venv"
os.environ["ESP_ROM_ELF_DIR"] = r"C:\Espressif\tools\esp-rom-elfs\20241011/"
os.environ["OPENOCD_SCRIPTS"] = r"C:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20260304\openocd-esp32\share\openocd\scripts"
os.environ["IDF_CCACHE_ENABLE"] = "1"
os.environ["IDF_COMPONENT_LOCAL_STORAGE_URL"] = "file://C:\Espressif\tools"
os.environ["ESP_IDF_VERSION"] = "6.0.1"
os.environ["ESP_CLANG_LIBS_PATH"] = r"C:\Espressif\tools\esp-clang-libs\esp-20.1.1_20250829\esp-clang\lib"

# Toolchain paths
toolchain_bin = r"C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin"
toolchain_inner_bin = r"C:\Espressif\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\xtensa-esp-elf\bin"

# Build the PATH
idf_path_dirs = [
    r"C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64",
    r"C:\Espressif\tools\cmake\4.0.3\bin",
    r"C:\Espressif\tools\ninja\1.12.1",
    r"C:\Espressif\tools\idf-exe\1.0.3",
    toolchain_bin,
    toolchain_inner_bin,
    r"C:\Espressif\tools\xtensa-esp-elf-gdb\16.3_20250913\xtensa-esp-elf-gdb\bin",
    r"C:\Espressif\tools\python\v6.0.1\venv\Scripts",
    r"C:\Espressif\tools\esp-rom-elfs\20241011",
    r"C:\Espressif\tools\esp-clang\esp-20.1.1_20250829\esp-clang\bin",
    r"C:\Espressif\tools\esp32ulp-elf\2.38_20240113\esp32ulp-elf\bin",
    r"C:\Espressif\tools\dfu-util\0.11\dfu-util-0.11-win64",
]

# Prepend IDF paths to existing PATH
current_path = os.environ.get("PATH", "")
new_path = ";".join(idf_path_dirs) + ";" + current_path
os.environ["PATH"] = new_path

# Verify compiler exists
cc_path = os.path.join(toolchain_bin, "xtensa-esp32s3-elf-gcc.exe")
if os.path.exists(cc_path):
    print(f"Compiler found: {cc_path}", flush=True)
else:
    print(f"ERROR: Compiler not found at {cc_path}", flush=True)
    sys.exit(1)

# Run idf.py with the given arguments
idf_py = os.path.join(os.environ["IDF_PATH"], "tools", "idf.py")
python_exe = os.path.join(os.environ["IDF_PYTHON_ENV_PATH"], "Scripts", "python.exe")

# Pass through all command line arguments
cmd = [python_exe, idf_py] + sys.argv[1:]
print(f"Running: {' '.join(cmd)}", flush=True)

# Run with inherited environment
result = subprocess.run(cmd, env=os.environ)
sys.exit(result.returncode)
