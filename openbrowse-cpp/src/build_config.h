// Build configuration that must be settled before any Windows header is read.
#pragma once

// The application only calls the wide-character API. Without UNICODE the plain
// names resolve to the ANSI variants, which take different arguments and reject
// the LPCWSTR values passed here.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Target Windows 10. commctrl.h compiles the whole tab-control section -
// TCM_*, TabStrip_*, TCIF*, NMTABCTRL - only when the SDK version macros say so,
// and without this the control that the whole browser is built around compiles
// as an empty header.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

// The SDK headers gate on NTDDI_VERSION rather than _WIN32_WINNT directly, so
// it has to be set as well. sdkddkver.h supplies the NTDDI_* constants.
#ifndef NTDDI_VERSION
#include <sdkddkver.h>
#define NTDDI_VERSION NTDDI_WIN10
#endif

// WIN32_LEAN_AND_MEAN must NOT be defined before including WebView2.h: it
// makes windows.h skip the headers that declare the `interface` keyword macro,
// and WebView2.h only includes rpc.h/rpcndr.h, so the macro never becomes
// available and every typedef in that header fails to parse.