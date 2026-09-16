# Toolchain file for the portable GCC/MinGW-w64 kit in C:\workenv\w64devkit.
# Everything is open source: GCC (GPL, runtime exception), mingw-w64 (permissive).
# The BH_WORKENV environment variable moves the whole kit elsewhere; it is read
# from the environment so CMake's own try-compile projects see it too.
if(DEFINED ENV{BH_WORKENV} AND NOT "$ENV{BH_WORKENV}" STREQUAL "")
  file(TO_CMAKE_PATH "$ENV{BH_WORKENV}/w64devkit" _bh_w64_default)
else()
  set(_bh_w64_default "C:/workenv/w64devkit")
endif()
set(W64DEVKIT_ROOT "${_bh_w64_default}" CACHE PATH "w64devkit location")
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER   "${W64DEVKIT_ROOT}/bin/gcc.exe")
set(CMAKE_CXX_COMPILER "${W64DEVKIT_ROOT}/bin/g++.exe")
set(CMAKE_RC_COMPILER  "${W64DEVKIT_ROOT}/bin/windres.exe")
set(CMAKE_AR           "${W64DEVKIT_ROOT}/bin/ar.exe")
set(CMAKE_RANLIB       "${W64DEVKIT_ROOT}/bin/ranlib.exe")
