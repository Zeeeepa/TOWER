#include "owl_app.h"
#include "owl_browser_manager.h"
#include "stealth/owl_virtual_machine.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include <iostream>

#if defined(OS_WIN)
int APIENTRY wWinMain(HINSTANCE hInstance) {
  CefMainArgs main_args(hInstance);
#else
int main(int argc, char* argv[]) {
  CefMainArgs main_args(argc, argv);
#endif

  // Parse command line
  CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
#if defined(OS_WIN)
  command_line->InitFromString(::GetCommandLineW());
#else
  command_line->InitFromArgv(argc, argv);
#endif

  // Create application
  CefRefPtr<OwlApp> app(new OwlApp);

  // Execute sub-process if needed
  int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  // CEF settings
  CefSettings settings;
  settings.no_sandbox = true;  // Disable sandbox for simplicity
  settings.remote_debugging_port = 0;  // Disable remote debugging for stealth
  settings.log_severity = LOGSEVERITY_WARNING;
  settings.windowless_rendering_enabled = true;  // Enable off-screen rendering

  // Set user agent - MUST match actual CEF version to avoid API mismatch detection
  // CRITICAL: Must match what we spoof in JavaScript (Windows, Chrome = actual CEF version)
  CefString(&settings.user_agent) = owl::VirtualMachineDB::Instance().GetDefaultUserAgent();

  // Set locale
  CefString(&settings.locale) = "en-US";

  // Disable cache for now
  CefString(&settings.cache_path) = "";

  // Initialize CEF
  if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
    std::cerr << "Failed to initialize CEF" << std::endl;
    return 1;
  }

  // Initialize browser manager
  OwlBrowserManager::GetInstance()->Initialize();

  std::cout << "Owl Browser initialized successfully" << std::endl;

  // Signal that browser is ready to accept commands
  std::cout << "READY" << std::endl;
  std::cout.flush();

  // Run message loop
  CefRunMessageLoop();

  // Shutdown
  OwlBrowserManager::GetInstance()->Shutdown();
  CefShutdown();

  return 0;
}
