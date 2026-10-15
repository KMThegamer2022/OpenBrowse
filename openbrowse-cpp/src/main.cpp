// OpenBrowse entry point.
//
// Creates the WebView2 environment with the browser command line that matches
// the current preferences, then shows the main window.
#include "build_config.h"

#include <windows.h>

#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <wrl/implements.h>

#include "WebView2.h"
// Supplies CoreWebView2EnvironmentOptions, the WRL class that carries the
// user data folder and the Chromium command line.
#include "WebView2EnvironmentOptions.h"

#include "app_paths.hpp"
#include "browser_window.h"
#include "settings.hpp"

using Microsoft::WRL::ComPtr;

namespace {

// Builds the Chromium command line from the saved preferences.
//
// WebView2 exposes these as process-wide launch flags rather than per-page
// settings, which is why changing JavaScript, smooth scrolling or hardware
// acceleration in the settings page asks for a restart.
std::wstring buildBrowserArguments(const ob::AppSettings& settings) {
  std::vector<std::wstring> arguments;

  if (!settings.javascriptEnabled) {
    arguments.push_back(L"--blink-settings=scriptEnabled=false");
  }

  if (settings.smoothScrollingEnabled) {
    arguments.push_back(L"--enable-smooth-scrolling");
  }

  // OPENBROWSE_FORCE_SOFTWARE_RENDERING is the escape hatch the Python build
  // offered through an environment variable, for machines with broken drivers.
  const bool useGpu = settings.hardwareAccelerationEnabled &&
                      !ob::envFlagEnabled("OPENBROWSE_FORCE_SOFTWARE_RENDERING");
  if (!useGpu) {
    arguments.push_back(L"--disable-gpu");
    arguments.push_back(L"--disable-gpu-compositing");
  }

  std::wstring joined;
  for (size_t i = 0; i < arguments.size(); ++i) {
    if (i > 0) joined += L' ';
    joined += arguments[i];
  }
  return joined;
}

// Append a startup milestone. Cheap enough to leave on permanently: it is the
// only evidence available when the browser dies before a window appears.
void logStartup(const std::string& line) {
  try {
    ob::ensureRuntimeDirectories();
    std::ofstream out(ob::logPath(), std::ios::app);
    if (out) out << line << "\n";
  } catch (...) {
  }
}

void writeStartupError(const std::string& message) {
  try {
    ob::ensureRuntimeDirectories();
    std::ofstream out(ob::logPath(), std::ios::trunc);
    if (out) out << message << "\n";
  } catch (...) {
    // Error reporting must never throw.
  }
}

void reportStartupFailure(const std::string& detail) {
  const std::string message =
      "OpenBrowse could not start the Edge WebView2 runtime.\n\n" + detail +
      "\n\nInstall the WebView2 Runtime from:\n"
      "https://developer.microsoft.com/microsoft-edge/webview2/\n\n"
      "Details were saved to:\n" +
      ob::logPath();

  writeStartupError(message);
  MessageBoxW(nullptr, ob::widen(message).c_str(), L"OpenBrowse failed to start",
              MB_OK | MB_ICONERROR);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int showCommand) {
  // WebView2 is COM based, so initialise OLE before anything else.
  const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(comResult)) {
    writeStartupError("CoInitializeEx failed: " + ob::describeHresult(comResult));
    MessageBoxW(nullptr, L"OpenBrowse could not initialise COM.", L"OpenBrowse failed to start",
                MB_OK | MB_ICONERROR);
    return 1;
  }

  ob::ensureRuntimeDirectories();
  logStartup("OpenBrowse starting, build " __DATE__ " " __TIME__);

  ob::SettingsStore settingsStore(ob::settingsPath());
  const ob::AppSettings settings = settingsStore.load();
  logStartup("settings loaded; home=" + settings.homeUrl +
             " smooth=" + (settings.smoothScrollingEnabled ? "1" : "0") +
             " js=" + (settings.javascriptEnabled ? "1" : "0") +
             " gpu=" + (settings.hardwareAccelerationEnabled ? "1" : "0"));
  logStartup("browser arguments: " + ob::narrow(buildBrowserArguments(settings)));

  ComPtr<ICoreWebView2EnvironmentOptions> options =
      Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
  if (!options) {
    reportStartupFailure("the WebView2 environment options could not be created.");
    CoUninitialize();
    return 1;
  }
  options->put_AdditionalBrowserArguments(buildBrowserArguments(settings).c_str());


  // Created before the environment so it outlives the completion callback.
  auto window = std::make_unique<ob::BrowserWindow>();
  auto failure = std::make_shared<std::string>();
  bool windowLogged = false;

  const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
      nullptr, ob::widen(ob::webViewUserDataFolder()).c_str(), options.Get(),
      Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [&window, &failure, instance, showCommand](HRESULT result,
                                                    ICoreWebView2Environment* environment)
              -> HRESULT {
            if (FAILED(result) || environment == nullptr) {
              *failure = ob::describeHresult(result);
              return S_OK;
            }
            logStartup(std::string("environment ready: ") + ob::describeHresult(result));
            if (!window->create(instance, showCommand, environment)) {
              *failure = "the main window could not be created.";
              logStartup("FATAL: main window creation returned false");
              return S_OK;
            }
            logStartup("main window created and shown");
            return S_OK;
          })
          .Get());

  logStartup(std::string("CreateCoreWebView2EnvironmentWithOptions hr=") +
             ob::describeHresult(hr));
  if (FAILED(hr)) {
    reportStartupFailure(ob::describeHresult(hr));
    CoUninitialize();
    return 1;
  }

  MSG message;
  BOOL result = 0;
  while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
    if (window->hwnd() == nullptr && !windowLogged) {
      windowLogged = true;
      logStartup("first message delivered, window created");
    }
    // Menu commands already arrive as WM_COMMAND, so only the accelerator
    // table needs translating here.
    if (window->hwnd() != nullptr &&
        TranslateAcceleratorW(window->hwnd(), window->accelerators(), &message)) {
      continue;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  logStartup("message loop ended");
  window.reset();

  if (!failure->empty()) {
    reportStartupFailure(*failure);
    CoUninitialize();
    return 1;
  }

  CoUninitialize();
  return static_cast<int>(message.wParam);
}