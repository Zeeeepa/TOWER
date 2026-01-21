/**
 * Owl Browser UI Main Entry Point - Linux (GTK3)
 *
 * This is the main entry point for the Owl Browser UI version on Linux.
 * It provides license validation, CLI commands, and GTK3 integration.
 *
 * Translated from macOS (owl_ui_main.mm) to maintain feature parity.
 */

#include "owl_app.h"
#include "owl_ui_browser.h"
#include "owl_browser_manager.h"
#include "owl_license.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "logger.h"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

#if defined(OS_LINUX) && defined(BUILD_UI)

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

// ============================================================================
// License Activation Window for UI Version (GTK3)
// ============================================================================

struct LicenseActivationData {
    GtkWidget* window;
    GtkWidget* status_label;
    GtkWidget* fingerprint_entry;
    std::string fingerprint;
    olib::license::LicenseStatus initial_status;
    bool license_activated;
};

static const char* GetStatusMessage(olib::license::LicenseStatus status) {
    switch (status) {
        case olib::license::LicenseStatus::NOT_FOUND:
            return "No license file found. Please select your license file (.olic) to activate Owl Browser.";
        case olib::license::LicenseStatus::EXPIRED:
            return "Your license has expired. Please renew your license at www.owlbrowser.net or select a new license file.";
        case olib::license::LicenseStatus::INVALID_SIGNATURE:
            return "The license file signature is invalid. Please download a valid license file from www.owlbrowser.net.";
        case olib::license::LicenseStatus::CORRUPTED:
            return "The license file is corrupted. Please re-download your license file from www.owlbrowser.net.";
        case olib::license::LicenseStatus::HARDWARE_MISMATCH:
            return "This license is bound to different hardware. Contact support@olib.ai to transfer your license.";
        default:
            return "License validation failed. Please select a valid license file or visit www.owlbrowser.net for assistance.";
    }
}

static void OnCopyFingerprint(GtkWidget* widget, gpointer data) {
    LicenseActivationData* activation_data = static_cast<LicenseActivationData*>(data);
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, activation_data->fingerprint.c_str(), -1);
    gtk_clipboard_store(clipboard);

    // Visual feedback
    gtk_button_set_label(GTK_BUTTON(widget), "Copied!");
    g_timeout_add(1500, [](gpointer user_data) -> gboolean {
        gtk_button_set_label(GTK_BUTTON(user_data), "Copy");
        return G_SOURCE_REMOVE;
    }, widget);
}

static void OnOpenWebsite(GtkWidget* widget, gpointer data) {
    GError* error = nullptr;
    gtk_show_uri_on_window(nullptr, "https://www.owlbrowser.net", GDK_CURRENT_TIME, &error);
    if (error) {
        std::cerr << "Failed to open website: " << error->message << std::endl;
        g_error_free(error);
    }
}

static void OnQuit(GtkWidget* widget, gpointer data) {
    LicenseActivationData* activation_data = static_cast<LicenseActivationData*>(data);
    activation_data->license_activated = false;
    gtk_main_quit();
}

static void OnWindowDestroy(GtkWidget* widget, gpointer data) {
    gtk_main_quit();
}

static void OnBrowseLicenseFile(GtkWidget* widget, gpointer data) {
    LicenseActivationData* activation_data = static_cast<LicenseActivationData*>(data);

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Select License File",
        GTK_WINDOW(activation_data->window),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        nullptr
    );

    // Add file filter for .olic files
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Owl Browser License Files (*.olic)");
    gtk_file_filter_add_pattern(filter, "*.olic");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All Files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_widget_destroy(dialog);

        if (filename) {
            // Try to activate the license
            auto* manager = olib::license::LicenseManager::GetInstance();
            olib::license::LicenseStatus status = manager->AddLicense(filename);

            if (status == olib::license::LicenseStatus::VALID) {
                // Success - show confirmation
                GtkWidget* success_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(activation_data->window),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_INFO,
                    GTK_BUTTONS_OK,
                    "License Activated"
                );
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(success_dialog),
                    "Your license has been activated successfully!\n\nOwl Browser will now start."
                );
                gtk_dialog_run(GTK_DIALOG(success_dialog));
                gtk_widget_destroy(success_dialog);

                activation_data->license_activated = true;
                gtk_main_quit();
            } else {
                // Failed - show error
                const char* error_message;
                switch (status) {
                    case olib::license::LicenseStatus::EXPIRED:
                        error_message = "This license has expired. Please obtain a new license from www.owlbrowser.net.";
                        break;
                    case olib::license::LicenseStatus::INVALID_SIGNATURE:
                        error_message = "This license file is invalid. Please ensure you have the correct license file.";
                        break;
                    case olib::license::LicenseStatus::CORRUPTED:
                        error_message = "This license file is corrupted. Please re-download it from your account.";
                        break;
                    case olib::license::LicenseStatus::HARDWARE_MISMATCH:
                        error_message = "This license is bound to different hardware. Contact support@olib.ai to transfer it.";
                        break;
                    default:
                        error_message = "Failed to activate the license. Please try again or contact support@olib.ai.";
                        break;
                }

                // Update status label
                gtk_label_set_text(GTK_LABEL(activation_data->status_label), error_message);

                // Show error dialog
                GtkWidget* error_dialog = gtk_message_dialog_new(
                    GTK_WINDOW(activation_data->window),
                    GTK_DIALOG_MODAL,
                    GTK_MESSAGE_WARNING,
                    GTK_BUTTONS_OK,
                    "Activation Failed"
                );
                gtk_message_dialog_format_secondary_text(
                    GTK_MESSAGE_DIALOG(error_dialog),
                    "%s", error_message
                );
                gtk_dialog_run(GTK_DIALOG(error_dialog));
                gtk_widget_destroy(error_dialog);
            }

            g_free(filename);
        }
    } else {
        gtk_widget_destroy(dialog);
    }
}

static bool ShowLicenseActivationWindow(olib::license::LicenseStatus status, const std::string& fingerprint) {
    // Initialize GTK if not already initialized
    if (!gtk_init_check(nullptr, nullptr)) {
        std::cerr << "Failed to initialize GTK for license window" << std::endl;
        return false;
    }

    LicenseActivationData activation_data;
    activation_data.fingerprint = fingerprint;
    activation_data.initial_status = status;
    activation_data.license_activated = false;

    // Create main window
    activation_data.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(activation_data.window), "Owl Browser - License Activation");
    gtk_window_set_default_size(GTK_WINDOW(activation_data.window), 520, 400);
    gtk_window_set_position(GTK_WINDOW(activation_data.window), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(activation_data.window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(activation_data.window), 24);
    g_signal_connect(activation_data.window, "destroy", G_CALLBACK(OnWindowDestroy), &activation_data);

    // Main vertical box
    GtkWidget* main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_add(GTK_CONTAINER(activation_data.window), main_box);

    // Header with title
    GtkWidget* title_label = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title_label), "<span size='xx-large' weight='bold'>Welcome to Owl Browser</span>");
    gtk_widget_set_halign(title_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), title_label, FALSE, FALSE, 0);

    GtkWidget* subtitle_label = gtk_label_new("Activate your license to get started");
    gtk_widget_set_halign(subtitle_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), subtitle_label, FALSE, FALSE, 0);

    // Separator
    GtkWidget* sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep1, FALSE, FALSE, 8);

    // Status message
    activation_data.status_label = gtk_label_new(GetStatusMessage(status));
    gtk_label_set_line_wrap(GTK_LABEL(activation_data.status_label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(activation_data.status_label), 60);
    gtk_widget_set_halign(activation_data.status_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), activation_data.status_label, FALSE, FALSE, 8);

    // License file section
    GtkWidget* license_label = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(license_label), "<b>License File (.olic):</b>");
    gtk_widget_set_halign(license_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), license_label, FALSE, FALSE, 4);

    // Browse button
    GtkWidget* browse_button = gtk_button_new_with_label("Select License File...");
    gtk_widget_set_size_request(browse_button, -1, 36);
    g_signal_connect(browse_button, "clicked", G_CALLBACK(OnBrowseLicenseFile), &activation_data);
    gtk_box_pack_start(GTK_BOX(main_box), browse_button, FALSE, FALSE, 0);

    // Fingerprint section
    GtkWidget* fp_label = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(fp_label), "<b>Hardware Fingerprint (for license request):</b>");
    gtk_widget_set_halign(fp_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(main_box), fp_label, FALSE, FALSE, 8);

    // Fingerprint entry with copy button
    GtkWidget* fp_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(main_box), fp_box, FALSE, FALSE, 0);

    activation_data.fingerprint_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(activation_data.fingerprint_entry), fingerprint.c_str());
    gtk_editable_set_editable(GTK_EDITABLE(activation_data.fingerprint_entry), FALSE);
    gtk_widget_set_hexpand(activation_data.fingerprint_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(fp_box), activation_data.fingerprint_entry, TRUE, TRUE, 0);

    GtkWidget* copy_button = gtk_button_new_with_label("Copy");
    g_signal_connect(copy_button, "clicked", G_CALLBACK(OnCopyFingerprint), &activation_data);
    gtk_box_pack_start(GTK_BOX(fp_box), copy_button, FALSE, FALSE, 0);

    // Separator
    GtkWidget* sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), sep2, FALSE, FALSE, 8);

    // Get license section
    GtkWidget* get_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(main_box), get_box, FALSE, FALSE, 0);

    GtkWidget* get_label = gtk_label_new("Don't have a license?");
    gtk_box_pack_start(GTK_BOX(get_box), get_label, FALSE, FALSE, 0);

    GtkWidget* website_button = gtk_link_button_new_with_label("https://www.owlbrowser.net", "Get one at www.owlbrowser.net");
    gtk_box_pack_start(GTK_BOX(get_box), website_button, FALSE, FALSE, 0);

    // Spacer
    GtkWidget* spacer = gtk_label_new("");
    gtk_widget_set_vexpand(spacer, TRUE);
    gtk_box_pack_start(GTK_BOX(main_box), spacer, TRUE, TRUE, 0);

    // Bottom buttons
    GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(button_box, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(main_box), button_box, FALSE, FALSE, 0);

    GtkWidget* quit_button = gtk_button_new_with_label("Quit");
    gtk_widget_set_size_request(quit_button, 80, -1);
    g_signal_connect(quit_button, "clicked", G_CALLBACK(OnQuit), &activation_data);
    gtk_box_pack_start(GTK_BOX(button_box), quit_button, FALSE, FALSE, 0);

    // Show all widgets
    gtk_widget_show_all(activation_data.window);

    // Run GTK main loop
    gtk_main();

    return activation_data.license_activated;
}

// ============================================================================
// Application State
// ============================================================================

static guint g_cef_work_source = 0;
static bool g_should_quit = false;

// ============================================================================
// CEF Message Loop Work Timer
// ============================================================================

static gboolean OnCefWorkTimer(gpointer data) {
    if (g_should_quit) {
        return G_SOURCE_REMOVE;
    }
    CefDoMessageLoopWork();
    return G_SOURCE_CONTINUE;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    // =========================================================================
    // License CLI Commands for UI Version (--license add/remove/info/status)
    // =========================================================================
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--license" && i + 1 < argc) {
            std::string license_cmd = argv[i + 1];

            if (license_cmd == "add" && i + 2 < argc) {
                // Add license: owl_browser_ui --license add /path/to/license.olic
                std::string license_path = argv[i + 2];
                auto* manager = olib::license::LicenseManager::GetInstance();
                olib::license::LicenseStatus status = manager->AddLicense(license_path);

                if (status == olib::license::LicenseStatus::VALID) {
                    std::cout << "License activated successfully!" << std::endl;
                    const auto* data = manager->GetLicenseData();
                    if (data) {
                        std::cout << "{\"status\":\"valid\",\"valid\":true,"
                                  << "\"license_id\":\"" << data->license_id << "\","
                                  << "\"name\":\"" << data->name << "\","
                                  << "\"organization\":\"" << data->organization << "\","
                                  << "\"email\":\"" << data->email << "\","
                                  << "\"type\":" << static_cast<int>(data->type) << ","
                                  << "\"max_seats\":" << data->max_seats << ","
                                  << "\"issue_date\":" << data->issue_timestamp << ","
                                  << "\"expiry_date\":" << data->expiry_timestamp << ","
                                  << "\"hardware_bound\":" << (data->hardware_bound ? "true" : "false")
                                  << "}" << std::endl;
                    }
                    return 0;
                } else {
                    std::cerr << "Failed to activate license: "
                              << olib::license::LicenseStatusToString(status) << std::endl;
                    return 1;
                }
            }
            else if (license_cmd == "remove") {
                auto* manager = olib::license::LicenseManager::GetInstance();
                olib::license::LicenseStatus status = manager->RemoveLicense();
                if (status == olib::license::LicenseStatus::NOT_FOUND ||
                    status == olib::license::LicenseStatus::VALID) {
                    std::cout << "License removed successfully." << std::endl;
                    return 0;
                } else {
                    std::cerr << "Failed to remove license." << std::endl;
                    return 1;
                }
            }
            else if (license_cmd == "info") {
                auto* manager = olib::license::LicenseManager::GetInstance();
                olib::license::LicenseStatus status = manager->Validate();
                if (status == olib::license::LicenseStatus::VALID) {
                    const auto* data = manager->GetLicenseData();
                    if (data) {
                        std::cout << "License Information:" << std::endl;
                        std::cout << "  ID: " << data->license_id << std::endl;
                        std::cout << "  Name: " << data->name << std::endl;
                        std::cout << "  Organization: " << data->organization << std::endl;
                        std::cout << "  Email: " << data->email << std::endl;
                        std::cout << "  Type: " << static_cast<int>(data->type) << std::endl;
                        std::cout << "  Max Seats: " << data->max_seats << std::endl;
                        std::cout << "  Hardware Bound: " << (data->hardware_bound ? "Yes" : "No") << std::endl;
                    }
                    return 0;
                } else {
                    std::cerr << "No valid license found: "
                              << olib::license::LicenseStatusToString(status) << std::endl;
                    return 1;
                }
            }
            else if (license_cmd == "status") {
                auto* manager = olib::license::LicenseManager::GetInstance();
                olib::license::LicenseStatus status = manager->Validate();
                std::cout << "License Status: "
                          << olib::license::LicenseStatusToString(status) << std::endl;
                return (status == olib::license::LicenseStatus::VALID) ? 0 : 1;
            }
            else if (license_cmd == "fingerprint") {
                std::string fp = olib::license::HardwareFingerprint::Generate();
                std::cout << "Hardware Fingerprint: " << fp << std::endl;
                return 0;
            }
        }
    }

    // =========================================================================
    // License Validation - UI will not start without valid license
    // =========================================================================
    auto* license_manager = olib::license::LicenseManager::GetInstance();
    olib::license::LicenseStatus license_status = license_manager->Validate();

    if (license_status != olib::license::LicenseStatus::VALID) {
        std::string fingerprint = olib::license::HardwareFingerprint::Generate();
        LOG_WARN("UI", "License validation failed: " + std::string(olib::license::LicenseStatusToString(license_status)));

        // Show license activation window - allows user to select and activate license
        bool activated = ShowLicenseActivationWindow(license_status, fingerprint);
        if (!activated) {
            // User quit without activating
            return 1;
        }
        // License was activated successfully, continue with app startup
        LOG_DEBUG("UI", "License activated via activation window");
    } else {
        LOG_DEBUG("UI", "License validated successfully");
    }

    // =========================================================================
    // CEF Initialization
    // =========================================================================
    CefMainArgs main_args(argc, argv);

    // Parse command line
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
    command_line->InitFromArgv(argc, argv);

    // Create application
    CefRefPtr<OwlApp> app(new OwlApp);

    // Execute sub-process if needed (for helper processes)
    int exit_code = CefExecuteProcess(main_args, app.get(), nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }

    // Initialize GTK (required before creating any windows)
    gtk_init(&argc, &argv);

    LOG_DEBUG("UI", "Initializing Owl Browser UI (GTK3)");
    LOG_DEBUG("UI", "GTK Version: " + std::to_string(gtk_get_major_version()) + "." +
             std::to_string(gtk_get_minor_version()) + "." +
             std::to_string(gtk_get_micro_version()));

    // CEF settings for UI version
    CefSettings settings;
    settings.no_sandbox = true;
    settings.remote_debugging_port = 9223;  // Enable DevTools
    settings.log_severity = LOGSEVERITY_WARNING;
    settings.windowless_rendering_enabled = false;  // Use windowed rendering for visible UI

    // DO NOT set custom UserAgent - let CEF use its default
    // This ensures UserAgent matches actual navigator properties
    // Custom UA causes "Different browser name/version" detection

    // Set locale
    CefString(&settings.locale) = "en-US";

    // Set cache path for UI version (use default cache directory)
    CefString(&settings.cache_path) = "";  // Use temporary cache

    // Use external message pump for GTK integration
    settings.multi_threaded_message_loop = false;
    settings.external_message_pump = true;

    // Set subprocess path to this executable
    std::string exe_path(argv[0]);
    CefString(&settings.browser_subprocess_path).FromString(exe_path);

    // Initialize logger for main UI process
    std::string log_file = "/tmp/owl_browser_ui_main.log";
    OlibLogger::Logger::Init(log_file);
    LOG_DEBUG("UI", "Logger initialized: " + log_file);

    // Initialize CEF
    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        LOG_ERROR("UI", "Failed to initialize CEF");
        return 1;
    }

    // Set message loop mode to use external message pump
    OwlBrowserManager::SetUsesRunMessageLoop(false);

    // Initialize browser manager (starts LLM service)
    OwlBrowserManager::GetInstance()->Initialize();

    LOG_DEBUG("UI", "Owl Browser UI initialized successfully");

    // Create UI browser window with custom homepage
    CefRefPtr<OwlUIBrowser> ui_browser(new OwlUIBrowser);
    ui_browser->CreateBrowserWindow("owl://homepage.html");

    // CEF work scheduling for external message pump
    // We need to periodically call CefDoMessageLoopWork()
    g_cef_work_source = g_timeout_add(10, OnCefWorkTimer, nullptr);

    LOG_DEBUG("UI", "Starting GTK main loop...");

    // Run GTK main loop
    gtk_main();

    LOG_DEBUG("UI", "GTK main loop exited");

    // Remove CEF work source
    if (g_cef_work_source > 0) {
        g_source_remove(g_cef_work_source);
        g_cef_work_source = 0;
    }

    // Shutdown browser manager
    OwlBrowserManager::GetInstance()->Shutdown();

    // Shutdown CEF
    LOG_DEBUG("UI", "Shutting down CEF...");
    CefShutdown();

    LOG_DEBUG("UI", "Owl Browser UI shutdown complete");
    return 0;
}

#endif  // OS_LINUX && BUILD_UI
