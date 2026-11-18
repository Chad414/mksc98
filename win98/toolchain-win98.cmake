# Cross toolchain for the Windows 98 SE / Athlon (no SSE) target.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER i686-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER i686-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/i686-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# x87 only: the Thunderbird has MMX/3DNow! but neither SSE nor SSE2.
set(CMAKE_C_FLAGS_INIT "-march=i686 -mtune=athlon-tbird -mno-sse -mno-sse2 -mfpmath=387")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc")
