/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/windowed_app.h>
#include <rex/ui/windowed_app_context_win.h>

namespace {

class ScopedModule {
 public:
  explicit ScopedModule(const wchar_t* name) : handle_(LoadLibraryW(name)) {}
  ~ScopedModule() {
    if (handle_) {
      FreeLibrary(handle_);
    }
  }

  ScopedModule(const ScopedModule&) = delete;
  ScopedModule& operator=(const ScopedModule&) = delete;

  template <typename Fn>
  Fn GetProcAddressAs(const char* name) const {
    return handle_ ? reinterpret_cast<Fn>(GetProcAddress(handle_, name)) : nullptr;
  }

 private:
  HMODULE handle_ = nullptr;
};

// Convert wide argv from CommandLineToArgvW to UTF-8 argc/argv for cvar::Init
std::vector<std::string> WideArgsToUtf8(int argc, wchar_t** wargv) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int i = 0; i < argc; ++i) {
    std::wstring wide(wargv[i]);
    if (wide.empty()) {
      args.emplace_back();
      continue;
    }
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr,
                                   0, nullptr, nullptr);
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), size,
                        nullptr, nullptr);
    args.push_back(std::move(utf8));
  }
  return args;
}

void SetProcessDpiAwarenessForHost() {
  ScopedModule user32(L"user32.dll");
  {
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto set_process_dpi_awareness_context = user32.GetProcAddressAs<SetProcessDpiAwarenessContextFn>(
        "SetProcessDpiAwarenessContext");
    if (set_process_dpi_awareness_context &&
        set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
      return;
    }
  }

  ScopedModule shcore(L"shcore.dll");
  {
    using SetProcessDpiAwarenessFn = HRESULT(WINAPI*)(int);
    auto set_process_dpi_awareness =
        shcore.GetProcAddressAs<SetProcessDpiAwarenessFn>("SetProcessDpiAwareness");
    constexpr int kProcessPerMonitorDpiAware = 2;
    if (set_process_dpi_awareness &&
        SUCCEEDED(set_process_dpi_awareness(kProcessPerMonitorDpiAware))) {
      return;
    }
  }

  using SetProcessDPIAwareFn = BOOL(WINAPI*)();
  auto set_process_dpi_aware = user32.GetProcAddressAs<SetProcessDPIAwareFn>("SetProcessDPIAware");
  if (set_process_dpi_aware) {
    set_process_dpi_aware();
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE hinstance_prev, LPWSTR command_line,
                    int show_cmd) {
  (void)hinstance_prev;
  (void)command_line;

  SetProcessDpiAwarenessForHost();

  // Convert wide command line to UTF-8 argc/argv and parse CVARs
  int wargc = 0;
  wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
  auto utf8_args = WideArgsToUtf8(wargc, wargv);
  LocalFree(wargv);

  // Build char* argv for cvar::Init
  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(utf8_args.size());
  for (auto& s : utf8_args) {
    argv_ptrs.push_back(s.data());
  }
  auto remaining = rex::cvar::Init(static_cast<int>(argv_ptrs.size()), argv_ptrs.data());
  rex::cvar::ApplyEnvironment();
  rex::InitLoggingEarly();

  int result;

  {
    rex::ui::Win32WindowedAppContext app_context(hinstance, show_cmd);
    // DPI awareness is set before context initialization because Initialize
    // creates the first window.
    if (!app_context.Initialize()) {
      return EXIT_FAILURE;
    }

    std::unique_ptr<rex::ui::WindowedApp> app = rex::ui::GetWindowedAppCreator()(app_context);

    // Match remaining positional args to app's expected options
    const auto& option_names = app->GetPositionalOptions();
    std::map<std::string, std::string> parsed;
    size_t count = std::min(remaining.size(), option_names.size());
    for (size_t i = 0; i < count; ++i) {
      parsed[option_names[i]] = remaining[i];
    }
    app->SetParsedArguments(std::move(parsed));

    // Initialize COM on the UI thread with the apartment-threaded concurrency
    // model, so dialogs can be used.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
      return EXIT_FAILURE;
    }

    // TODO: Port InitializeWin32App from Xenia
    // rex::InitializeWin32App(app->GetName());

    result = app->OnInitialize() ? app_context.RunMainMessageLoop() : EXIT_FAILURE;

    app->InvokeOnDestroy();
  }

  // TODO: Port ShutdownWin32App from Xenia
  // Logging may still be needed in the destructors.
  // rex::ShutdownWin32App();

  CoUninitialize();

  return result;
}
