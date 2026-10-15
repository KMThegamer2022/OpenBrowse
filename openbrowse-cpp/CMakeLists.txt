cmake_minimum_required(VERSION 3.20)
project(OpenBrowseWin32 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  # The application targets the Windows-only WebView2 runtime, but the
  # portable half (JSON, URL heuristics, settings, history) is host agnostic
  # and is unit tested from any machine.
  message(STATUS "Non-Windows host: configuring the portable-layer tests only")
  option(OPENBROWSE_BUILD_TESTS "Build the host-side unit tests" ON)
  if(OPENBROWSE_BUILD_TESTS)
    enable_testing()
    add_executable(OpenBrowseTests src/tests_portable.cpp)
    target_include_directories(OpenBrowseTests PRIVATE src)
    add_test(NAME portable COMMAND OpenBrowseTests)
  endif()
  return()
endif()

# ---------------------------------------------------------------------------
# WebView2 SDK, restored from NuGet so no Visual Studio workload is needed.
# ---------------------------------------------------------------------------

set(WEBVIEW2_VERSION "1.0.2903.40" CACHE STRING "WebView2 SDK package version")

find_program(NUGET_EXECUTABLE
  NAMES nuget nuget.exe
  HINTS "$ENV{USERPROFILE}/.nuget/packages" "$ENV{CHOCOLATEY_INSTALL}/bin"
  PATH_SUFFIXES tools
)

set(WEBVIEW2_PACKAGE_DIR "${CMAKE_BINARY_DIR}/webview2")
if(NOT NUGET_EXECUTABLE)
  message(FATAL_ERROR
    "nuget.exe was not found.\n"
    "Install it with:  winget install Microsoft.NuGet\n"
    "or from https://dist.nuget.org/win-x86-commandline/latest/nuget.exe")
endif()

message(STATUS "Restoring Microsoft.Web.WebView2 ${WEBVIEW2_VERSION}")
execute_process(
  COMMAND "${NUGET_EXECUTABLE}" install Microsoft.Web.WebView2
          -Version ${WEBVIEW2_VERSION}
          -OutputDirectory "${WEBVIEW2_PACKAGE_DIR}"
          -NonInteractive
  RESULT_VARIABLE NUGET_RESULT
)
if(NOT NUGET_RESULT EQUAL 0)
  message(FATAL_ERROR "nuget restore of the WebView2 SDK failed with ${NUGET_RESULT}")
endif()

set(WEBVIEW2_ROOT "${WEBVIEW2_PACKAGE_DIR}/Microsoft.Web.WebView2.${WEBVIEW2_VERSION}")
set(WEBVIEW2_INCLUDE "${WEBVIEW2_ROOT}/build/native/include")
set(WEBVIEW2_LIB_DIR "${WEBVIEW2_ROOT}/build/native/x64")

if(NOT EXISTS "${WEBVIEW2_INCLUDE}/WebView2.h")
  message(FATAL_ERROR "WebView2.h not found under ${WEBVIEW2_INCLUDE}")
endif()

# WebView2LoaderStatic links the loader in, so the result is a single .exe
# with no companion DLL to deploy.
if(EXISTS "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
  set(WEBVIEW2_LOADER_LIB "${WEBVIEW2_LIB_DIR}/WebView2LoaderStatic.lib")
else()
  set(WEBVIEW2_LOADER_LIB "${WEBVIEW2_LIB_DIR}/WebView2Loader.dll.lib")
endif()

add_library(OpenBrowseWebView2 INTERFACE)
target_include_directories(OpenBrowseWebView2 SYSTEM INTERFACE "${WEBVIEW2_INCLUDE}")
target_link_libraries(OpenBrowseWebView2 INTERFACE "${WEBVIEW2_LOADER_LIB}")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------

add_executable(OpenBrowse WIN32
  src/main.cpp
  src/tab.cpp
  src/browser_window.cpp
)

target_include_directories(OpenBrowse PRIVATE src)
target_link_libraries(OpenBrowse PRIVATE
  OpenBrowseWebView2
  comctl32
  comdlg32
  shell32
  ole32
  uuid
)

# The application is written entirely against the wide-character API, and
# IDC_ARROW & friends only resolve to the W variants when UNICODE is defined.
target_compile_definitions(OpenBrowse PRIVATE UNICODE _UNICODE NOMINMAX)

if(MSVC)
  target_compile_options(OpenBrowse PRIVATE /W4 /permissive- /utf-8 /EHsc)
  # WebView2 headers trip these; they are not defects in this code.
  target_compile_options(OpenBrowse PRIVATE /wd4996)
endif()

# ---------------------------------------------------------------------------
# Portable-layer tests. These build and run on any host, including macOS and
# Linux, because they touch no Windows APIs.
# ---------------------------------------------------------------------------

option(OPENBROWSE_BUILD_TESTS "Build the host-side unit tests" ON)
if(OPENBROWSE_BUILD_TESTS)
  enable_testing()
  add_executable(OpenBrowseTests src/tests_portable.cpp)
  target_include_directories(OpenBrowseTests PRIVATE src)
  add_test(NAME portable COMMAND OpenBrowseTests)
endif()