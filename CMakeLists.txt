cmake_minimum_required(VERSION 3.25)
project(TheBlackHole VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(BH_SYSTEM_SDL "Link against an SDL3 already installed on the system" OFF)

# ---------------------------------------------------------------------------
# SDL3 (zlib license). Statically linked so the screensaver is a single file.
# ---------------------------------------------------------------------------
if(BH_SYSTEM_SDL)
  find_package(SDL3 REQUIRED CONFIG)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
  # Only what a screensaver needs: video + OpenGL + events + timers.
  foreach(sub AUDIO GPU RENDER CAMERA JOYSTICK HAPTIC HIDAPI SENSOR DIALOG POWER OPENGLES TRAY)
    set(SDL_${sub} OFF CACHE BOOL "" FORCE)
  endforeach()
  set(SDL_OPENGL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(SDL3)
  # SDL's "dynamic API" is off in this single-file build. With it on, every
  # SDL function is referenced from one jump table, so the linker keeps all of
  # them - including process spawning and URL opening, which a screensaver
  # never calls - and SDL will load a DLL named in an environment variable at
  # startup. Both are what antivirus heuristics look for. SDL only offers
  # the switch through this define (it has no other effect under GCC).
  # Per-function sections then let the linker drop everything unused.
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_definitions(SDL3-static PRIVATE SDL_THREAD_SAFETY_ANALYSIS)
    target_compile_options(SDL3-static PRIVATE -ffunction-sections -fdata-sections)
  endif()
endif()

# ---------------------------------------------------------------------------
# Shaders are embedded as C byte arrays at build time (see cmake/embed.cmake),
# so the .scr stays a single file with nothing to install beside it.
# ---------------------------------------------------------------------------
set(BH_SHADERS
  src/shaders/star.vert src/shaders/star.frag
  src/shaders/lens.frag
  src/shaders/post.vert src/shaders/ghost.frag
  src/shaders/blur.frag src/shaders/blit.frag src/shaders/damage.frag
  src/shaders/dust.vert src/shaders/bright.frag src/shaders/binary.frag)
set(BH_SHADER_ABS)
foreach(s ${BH_SHADERS})
  list(APPEND BH_SHADER_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${s})
endforeach()
set(BH_SHADERS_GEN ${CMAKE_CURRENT_BINARY_DIR}/gen/shaders_gen.c)
add_custom_command(
  OUTPUT  ${BH_SHADERS_GEN}
  COMMAND ${CMAKE_COMMAND} -DOUT=${BH_SHADERS_GEN} "-DINPUTS=${BH_SHADER_ABS}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS ${BH_SHADER_ABS} cmake/embed.cmake
  COMMENT "Embedding GLSL shaders")

# ---------------------------------------------------------------------------
# The screensaver executable
# ---------------------------------------------------------------------------
set(BH_SOURCES
  src/main.c src/app.c src/settings.c src/scene.c
  src/render.c src/gl_loader.c
  ${BH_SHADERS_GEN})
if(WIN32)
  list(APPEND BH_SOURCES src/platform_win32.c res/win32/theblackhole.rc)
else()
  list(APPEND BH_SOURCES src/platform_linux.c)
endif()

add_executable(TheBlackHole ${BH_SOURCES})
target_include_directories(TheBlackHole PRIVATE src third_party res/win32)
target_link_libraries(TheBlackHole PRIVATE SDL3::SDL3)
target_compile_definitions(TheBlackHole PRIVATE
  BH_VERSION="${PROJECT_VERSION}" $<$<CONFIG:Debug>:BH_DEBUG=1>)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(TheBlackHole PRIVATE -Wall -Wextra -Wshadow -Wno-unused-parameter
    -ffunction-sections -fdata-sections)
  target_link_options(TheBlackHole PRIVATE $<$<CONFIG:Release>:-Wl,--gc-sections>)
endif()

if(WIN32)
  # A Windows screensaver is a GUI-subsystem exe with the .scr extension.
  set_target_properties(TheBlackHole PROPERTIES WIN32_EXECUTABLE TRUE SUFFIX ".scr")
  target_link_libraries(TheBlackHole PRIVATE comctl32 comdlg32 opengl32 gdi32 user32 advapi32 shell32)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU")
    # Fully static CRT so the .scr can be dropped anywhere on its own.
    # Stripped, with the standard hardening flags set explicitly: antivirus
    # heuristics score unstripped MinGW binaries and missing DEP/ASLR flags
    # as suspicious. The relocation table is kept so ASLR can really move the
    # image, and the subsystem version says Windows 7 rather than MinGW's
    # default of XP-era 5.2, which is another "odd old binary" signal.
    target_link_options(TheBlackHole PRIVATE -static -static-libgcc
      $<$<CONFIG:Release>:-s>
      -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va
      -Wl,--enable-reloc-section
      -Wl,--major-subsystem-version,6 -Wl,--minor-subsystem-version,1
      -Wl,--major-os-version,6 -Wl,--minor-os-version,1)
  endif()
  target_compile_definitions(TheBlackHole PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
else()
  target_link_libraries(TheBlackHole PRIVATE m)
  set_target_properties(TheBlackHole PROPERTIES OUTPUT_NAME theblackhole)
  include(GNUInstallDirs)
  # XScreenSaver looks for hacks in its libexec dir and their config in share.
  set(BH_XSS_HACKDIR "${CMAKE_INSTALL_LIBEXECDIR}/xscreensaver" CACHE PATH "XScreenSaver hack directory")
  set(BH_XSS_CONFDIR "${CMAKE_INSTALL_DATADIR}/xscreensaver/config" CACHE PATH "XScreenSaver config directory")
  install(TARGETS TheBlackHole RUNTIME DESTINATION ${BH_XSS_HACKDIR})
  # The settings page xscreensaver-settings shows for this hack.
  install(FILES res/linux/theblackhole.xml DESTINATION ${BH_XSS_CONFDIR})
endif()
