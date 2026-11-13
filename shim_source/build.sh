#!/bin/sh
# Cross-builds the 32-bit shim DLL on Linux/macOS with LLVM (clang, lld-link, llvm-dlltool).
# On Windows you can instead compile facade.c + shim_win.c with any C compiler (see README).
set -e
cd "$(dirname "$0")"
OUT=${1:-X3DAudio1_5.dll}
CORE=${CORE_NAME:-x3daudio_hrtf_core}

llvm-dlltool -m i386 -k -d kernel32_min.def -l kernel32_min.lib
clang -target i686-pc-windows-msvc -O2 -std=gnu11 -ffreestanding -fno-stack-protector \
      -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter -c facade.c -o facade.obj
clang -target i686-pc-windows-msvc -O2 -std=gnu11 -ffreestanding -fno-stack-protector \
      -fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter -c shim_win.c -o shim_win.obj
lld-link /dll /nodefaultlib /machine:x86 /subsystem:windows /entry:DllMainCRTStartup@12 /safeseh:no \
      /out:"$OUT" facade.obj shim_win.obj kernel32_min.lib \
      /export:X3DAudioInitialize="$CORE".X3DAudioInitialize \
      /export:X3DAudioCalculate="$CORE".X3DAudioCalculate
echo "built $OUT"
