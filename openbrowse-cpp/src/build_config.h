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

// commctrl.h compiles the whole tab-control section - TCM_*, TabStrip_*,
// TCIF*, NMTABCTRL, NMCDS_* and CDRF_* - only when _WIN32_WINNT and WINVER
// are at least 0x0500. Without these the header is effectively empty for the
// control this browser is built around.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00  // Windows 10
#endif
#ifndef WINVER
#define WINVER 0x0A00
#endif

// WIN32_LEAN_AND_MEAN must NOT be defined before including WebView2.h: it
// makes windows.h skip the headers that declare the `interface` keyword macro,
// and WebView2.h only includes rpc.h/rpcndr.h, so the macro never becomes
// available and every typedef in that header fails to parse.