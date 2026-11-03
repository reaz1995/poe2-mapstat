cmake_minimum_required(VERSION 3.16)

#########################################################################
# Toolchain (llvm-mingw). Must be set BEFORE project(), otherwise CMake
# already detected Linux: no .exe suffix, WIN32 flag and .rc ignored.

# Machine-specific paths live in .env (git-ignored). Copy .env.example to .env and edit it.
# Keys missing in .env can be given on the command line: cmake -DGITHUB_REPO=owner/repo ...
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.env")
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/.env" _env_lines REGEX "^[A-Za-z_][A-Za-z0-9_]*=")
  foreach(_line IN LISTS _env_lines)
    string(REGEX MATCH "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$" _ "${_line}")
    set(_key "${CMAKE_MATCH_1}")
    string(STRIP "${CMAKE_MATCH_2}" _val)
    string(REGEX REPLACE "^\"(.*)\"$" "\\1" _val "${_val}")
    # .env is the source of truth: its value replaces what an earlier configure cached
    set(${_key} "${_val}" CACHE STRING "from .env" FORCE)
  endforeach()
  # editing .env re-runs the configure step on the next build (no need to delete build/)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/.env")
endif()

set(LLVM_MINGW_DIR "${LLVM_MINGW_DIR}" CACHE PATH "llvm-mingw root (from .env)")
set(DEPLOY_DIR "${DEPLOY_DIR}" CACHE PATH "Where the exe is copied after build (empty = no copy)")
set(GITHUB_REPO "${GITHUB_REPO}" CACHE STRING "owner/repo for self-update (Release builds only; empty = no updater)")
if(NOT LLVM_MINGW_DIR)
  message(FATAL_ERROR "LLVM_MINGW_DIR is not set. Copy .env.example to .env and set it.")
endif()

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER   "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-clang++")
set(CMAKE_RC_COMPILER  "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-windres")
set(CMAKE_AR           "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-ar"     CACHE FILEPATH "")
set(CMAKE_RANLIB       "${LLVM_MINGW_DIR}/bin/x86_64-w64-mingw32-ranlib" CACHE FILEPATH "")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_DIR}/x86_64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# App version comes from the VERSION file. Pushing a new version to main makes the GitHub workflow publish a release.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION_FILE LIMIT_COUNT 1)
string(STRIP "${APP_VERSION_FILE}" APP_VERSION_FILE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
project(Poe2MapStat VERSION ${APP_VERSION_FILE} LANGUAGES C CXX RC)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

#########################################################################
# Executable

# Themes are baked into the exe: themes/*.yaml -> themes_gen.h (rebuilt when a theme file changes)
file(GLOB THEME_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/themes/*.yaml")
set(THEMES_HEADER "${CMAKE_BINARY_DIR}/generated/themes_gen.h")
add_custom_command(
  OUTPUT "${THEMES_HEADER}"
  COMMAND ${CMAKE_COMMAND} -DTHEMES_DIR=${CMAKE_SOURCE_DIR}/themes -DOUT=${THEMES_HEADER}
          -P ${CMAKE_SOURCE_DIR}/cmake/embed_themes.cmake
  DEPENDS ${THEME_FILES} ${CMAKE_SOURCE_DIR}/cmake/embed_themes.cmake
  COMMENT "Embedding themes"
  VERBATIM)

file(GLOB APP_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/src/app/*.cpp" "${CMAKE_SOURCE_DIR}/src/app/*.h"       # Windows UI
  "${CMAKE_SOURCE_DIR}/src/core/*.cpp" "${CMAKE_SOURCE_DIR}/src/core/*.h"     # tracker (portable)
  "${CMAKE_SOURCE_DIR}/src/shared/*.cpp" "${CMAKE_SOURCE_DIR}/src/shared/*.h") # theme parser, updater
add_executable(Poe2MapStat WIN32
  ${APP_SOURCES}
  ${THEMES_HEADER}
  res/app.rc            # manifest: visual styles + DPI aware
)
target_compile_options(Poe2MapStat PRIVATE -Wall -Wextra)
target_include_directories(Poe2MapStat PRIVATE
  ${CMAKE_SOURCE_DIR}/src/app ${CMAKE_SOURCE_DIR}/src/core ${CMAKE_SOURCE_DIR}/src/shared ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(Poe2MapStat PRIVATE _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
target_link_options(Poe2MapStat PRIVATE -fuse-ld=lld -municode -static -static-libstdc++ $<$<CONFIG:Release>:-s>)
target_link_libraries(Poe2MapStat PRIVATE comctl32 comdlg32 shell32 dwmapi uxtheme winhttp bcrypt gdiplus ole32)

# Version + self-update. The GitHub repo is baked in only for Release builds (Debug builds never update).
target_compile_definitions(Poe2MapStat PRIVATE APP_VERSION="${PROJECT_VERSION}")
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND GITHUB_REPO)
  target_compile_definitions(Poe2MapStat PRIVATE UPDATE_REPO="${GITHUB_REPO}")
  message(STATUS "Self-update: github.com/${GITHUB_REPO} (v${PROJECT_VERSION})")
else()
  message(STATUS "Self-update: off (needs Release build + GITHUB_REPO in .env)")
endif()


#########################################################################
# Copy to Windows after build

if(DEPLOY_DIR)
  add_custom_command(TARGET Poe2MapStat POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Poe2MapStat>" "${DEPLOY_DIR}/"
    COMMENT "Copy exe -> ${DEPLOY_DIR}"
    VERBATIM)
endif()
