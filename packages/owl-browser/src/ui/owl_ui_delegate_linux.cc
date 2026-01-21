// Linux implementation using GTK3
#include "owl_ui_delegate.h"

#if defined(OS_LINUX)

#include "owl_ui_browser.h"
#include "owl_ui_toolbar.h"
#include "owl_agent_controller.h"
#include "owl_task_state.h"
#include "owl_proxy_manager.h"
#include "../resources/icons/icons.h"
#include "logger.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <librsvg/rsvg.h>
#include <iostream>
#include <sstream>
#include <cmath>
#include <regex>

// Helper: Create GdkPixbuf from SVG string
static GdkPixbuf* CreatePixbufFromSVG(const std::string& svgString, int width, int height) {
  GError* error = nullptr;
  RsvgHandle* handle = rsvg_handle_new_from_data(
    (const guint8*)svgString.c_str(), svgString.length(), &error);

  if (!handle) {
    if (error) {
      LOG_ERROR("UIDelegate", "Failed to parse SVG: " + std::string(error->message));
      g_error_free(error);
    }
    return nullptr;
  }

  // Create Cairo surface
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
  cairo_t* cr = cairo_create(surface);

  // Get SVG dimensions and scale
  RsvgDimensionData dim;
  rsvg_handle_get_dimensions(handle, &dim);
  double scale_x = (double)width / dim.width;
  double scale_y = (double)height / dim.height;
  double scale = (scale_x < scale_y) ? scale_x : scale_y;

  // Center the icon
  double offset_x = (width - dim.width * scale) / 2;
  double offset_y = (height - dim.height * scale) / 2;

  cairo_translate(cr, offset_x, offset_y);
  cairo_scale(cr, scale, scale);

  // Render SVG
  rsvg_handle_render_cairo(handle, cr);

  // Convert to pixbuf
  GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);

  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  g_object_unref(handle);

  return pixbuf;
}

// Forward declarations for GTK callbacks
static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data);
static void on_window_destroy(GtkWidget* widget, gpointer data);
static void on_window_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer data);
static void on_prompt_send_clicked(GtkButton* button, gpointer data);
static void on_prompt_close_clicked(GtkButton* button, gpointer data);
static void on_tasks_button_clicked(GtkButton* button, gpointer data);
static gboolean on_prompt_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data);

// Structure to hold GTK-specific UI components
struct GTKUIComponents {
  GtkWidget* window;
  GtkWidget* overlay;  // GtkOverlay for layering overlays
  GtkWidget* main_vbox;  // Vertical box containing toolbar and content
  GtkWidget* toolbar_container;
  GtkWidget* content_area;
  // Agent prompt overlay components
  GtkWidget* agent_prompt_overlay;
  GtkWidget* prompt_input;
  GtkWidget* prompt_send_button;
  GtkWidget* prompt_stop_button;  // Stop button for canceling execution
  GtkWidget* tasks_button;
  GtkWidget* status_dot;
  GtkWidget* progress_border;  // Animated border during execution
  // Tasks panel
  GtkWidget* tasks_panel;
  GtkWidget* tasks_scroll;
  GtkWidget* tasks_container;  // Container for task items
  // Response area
  GtkWidget* response_area;
  GtkWidget* response_scroll;
  GtkWidget* response_text_view;
  GtkTextBuffer* response_text_buffer;
  // Proxy overlay components
  GtkWidget* proxy_overlay;
  GtkWidget* proxy_type_combo;
  GtkWidget* proxy_host_entry;
  GtkWidget* proxy_port_entry;
  GtkWidget* proxy_username_entry;
  GtkWidget* proxy_password_entry;
  GtkWidget* proxy_timezone_entry;  // Timezone override
  GtkWidget* proxy_stealth_checkbox;  // Stealth mode
  GtkWidget* proxy_ca_checkbox;  // Trust custom CA
  GtkWidget* proxy_ca_path_label;  // CA certificate path
  GtkWidget* proxy_status_label;
  GtkWidget* proxy_save_button;
  GtkWidget* proxy_connect_button;
  bool is_closing;
  bool cef_ready;
  bool proxy_settings_saved;
  guint pulse_animation_id;  // For status dot pulsing
  guint progress_animation_id;  // For progress border animation
};

// Global storage for main window (singleton pattern)
static GTKUIComponents* g_main_window = nullptr;

OwlUIDelegate* OwlUIDelegate::instance_ = nullptr;

OwlUIDelegate::OwlUIDelegate()
    : gtk_window_(nullptr),
      toolbar_(nullptr),
      sidebar_visible_(false),
      agent_prompt_visible_(false),
      task_executing_(false),
      tasks_list_visible_(false),
      proxy_overlay_visible_(false),
      browser_handler_(nullptr) {
  LOG_DEBUG("UIDelegate", "GTK UI delegate created");
}

OwlUIDelegate::~OwlUIDelegate() {
  if (g_main_window) {
    if (g_main_window->window) {
      gtk_widget_destroy(g_main_window->window);
    }
    delete g_main_window;
    g_main_window = nullptr;
  }
  if (toolbar_) {
    delete toolbar_;
    toolbar_ = nullptr;
  }
  LOG_DEBUG("UIDelegate", "GTK UI delegate destroyed");
}

OwlUIDelegate* OwlUIDelegate::GetInstance() {
  if (!instance_) {
    instance_ = new OwlUIDelegate();
  }
  return instance_;
}

void* OwlUIDelegate::CreateWindowWithToolbar(OwlUIBrowser* browser_handler, int width, int height) {
  LOG_DEBUG("UIDelegate", "Creating main window with toolbar: " + std::to_string(width) + "x" + std::to_string(height));

  browser_handler_ = browser_handler;

  // Create GTK window
  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Owl Browser");
  gtk_window_set_default_size(GTK_WINDOW(window), width, height);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

  // Create components structure
  g_main_window = new GTKUIComponents();
  g_main_window->window = window;
  g_main_window->is_closing = false;
  g_main_window->cef_ready = false;

  gtk_window_ = window;

  // Create overlay container (allows stacking widgets on top of each other)
  GtkWidget* overlay = gtk_overlay_new();
  g_main_window->overlay = overlay;
  gtk_container_add(GTK_CONTAINER(window), overlay);

  // Create main vertical box (toolbar + content)
  GtkWidget* main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  g_main_window->main_vbox = main_vbox;
  gtk_container_add(GTK_CONTAINER(overlay), main_vbox);

  // Create toolbar
  toolbar_ = new OwlUIToolbar();
  int toolbar_height = toolbar_->GetHeight();

  // Set up toolbar callbacks
  toolbar_->SetBackCallback([browser_handler]() {
    browser_handler->GoBack();
  });

  toolbar_->SetForwardCallback([browser_handler]() {
    browser_handler->GoForward();
  });

  toolbar_->SetReloadCallback([browser_handler]() {
    browser_handler->Reload();
  });

  toolbar_->SetStopLoadingCallback([browser_handler]() {
    browser_handler->StopLoading();
  });

  toolbar_->SetHomeCallback([browser_handler]() {
    browser_handler->Navigate("owl://homepage.html");
  });

  toolbar_->SetNavigateCallback([browser_handler](const std::string& url) {
    browser_handler->Navigate(url);
  });

  toolbar_->SetAgentToggleCallback([browser_handler]() {
    browser_handler->ToggleAgentMode();
  });

  toolbar_->SetNewTabCallback([]() {
    OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
    if (delegate) {
      delegate->NewTab();
    }
  });

  toolbar_->SetProxyToggleCallback([]() {
    OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
    if (delegate) {
      delegate->ToggleProxyOverlay();
    }
  });

  // Create toolbar view
  GtkWidget* toolbar_view = (GtkWidget*)toolbar_->CreateToolbarView(width, toolbar_height);
  g_main_window->toolbar_container = toolbar_view;
  gtk_box_pack_start(GTK_BOX(main_vbox), toolbar_view, FALSE, FALSE, 0);

  // Create content area for CEF browser
  GtkWidget* content_area = gtk_fixed_new();
  g_main_window->content_area = content_area;
  gtk_widget_set_size_request(content_area, width, height - toolbar_height);
  gtk_box_pack_start(GTK_BOX(main_vbox), content_area, TRUE, TRUE, 0);

  // Create agent prompt overlay (hidden by default)
  CreateAgentPromptOverlay();

  // Create tasks panel (hidden by default)
  CreateTasksPanel();

  // Create response area (hidden by default)
  CreateResponseArea();

  // Connect window signals
  g_signal_connect(window, "delete-event", G_CALLBACK(on_window_delete), g_main_window);
  g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), g_main_window);
  g_signal_connect(window, "size-allocate", G_CALLBACK(on_window_size_allocate), this);

  // Show window
  gtk_widget_show_all(window);

  // Hide overlays initially
  if (g_main_window->agent_prompt_overlay) {
    gtk_widget_hide(g_main_window->agent_prompt_overlay);
  }
  if (g_main_window->tasks_panel) {
    gtk_widget_hide(g_main_window->tasks_panel);
  }
  if (g_main_window->response_area) {
    gtk_widget_hide(g_main_window->response_area);
  }

  LOG_DEBUG("UIDelegate", "Main window with toolbar created successfully");

  // Return content area for CEF to use as parent
  return content_area;
}

void* OwlUIDelegate::CreateWindow(OwlUIBrowser* browser_handler, int width, int height) {
  LOG_DEBUG("UIDelegate", "Creating window without toolbar: " + std::to_string(width) + "x" + std::to_string(height));

  browser_handler_ = browser_handler;

  // Create simple window without toolbar (for developer playground)
  GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(window), "Developer Playground");
  gtk_window_set_default_size(GTK_WINDOW(window), width, height);
  gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

  gtk_window_ = window;

  // Create content area
  GtkWidget* content_area = gtk_fixed_new();
  gtk_widget_set_size_request(content_area, width, height);
  gtk_container_add(GTK_CONTAINER(window), content_area);

  gtk_widget_show_all(window);

  LOG_DEBUG("UIDelegate", "Window without toolbar created successfully");

  return content_area;
}

void OwlUIDelegate::CreateAgentPromptOverlay() {
  if (!g_main_window || !g_main_window->overlay) return;

  // Apply CSS styling matching macOS homepage design
  GtkCssProvider* css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider,
    // Main prompt overlay - white box with blue shadow
    ".agent-prompt { "
    "  background-color: white; "
    "  border-radius: 28px; "
    "  border: 2px solid rgba(32,32,32,0.15); "
    "  box-shadow: 0 4px 24px rgba(66,133,244,0.2); "
    "}\n"
    // Tasks button - circular gray button
    ".tasks-button { "
    "  background-color: #f2f2f2; "
    "  border-radius: 20px; "
    "  min-width: 40px; min-height: 40px; "
    "  padding: 0; "
    "}\n"
    ".tasks-button:hover { background-color: #e5e5e5; }\n"
    // Status dot - small colored indicator
    ".status-dot { "
    "  border-radius: 5px; "
    "  min-width: 10px; min-height: 10px; "
    "  border: 2px solid white; "
    "}\n"
    ".status-idle { background-color: #999999; }\n"
    ".status-planning { background-color: #ffcc00; }\n"
    ".status-executing { background-color: #4285f4; }\n"
    ".status-waiting { background-color: #cc66ff; }\n"
    ".status-completed { background-color: #33cc33; }\n"
    ".status-error { background-color: #ff3333; }\n"
    // Input entry - clean design
    ".prompt-entry { "
    "  border: none; "
    "  background: transparent; "
    "  font-size: 16px; "
    "  color: #202124; "
    "  padding: 8px 0; "
    "}\n"
    ".prompt-entry:focus { border: none; box-shadow: none; }\n"
    // Go button - blue rounded button
    ".go-button { "
    "  background-color: #4285f4; "
    "  color: white; "
    "  border-radius: 20px; "
    "  font-weight: 600; "
    "  font-size: 15px; "
    "  min-width: 100px; min-height: 40px; "
    "  padding: 0 20px; "
    "}\n"
    ".go-button:hover { background-color: #357abd; }\n"
    // Stop button - red variant
    ".stop-button { "
    "  background-color: #e63333; "
    "  color: white; "
    "  border-radius: 20px; "
    "  font-weight: 600; "
    "  font-size: 15px; "
    "  min-width: 100px; min-height: 40px; "
    "  padding: 0 20px; "
    "}\n"
    ".stop-button:hover { background-color: #cc2929; }\n"
    // Progress border animation
    ".progress-border { "
    "  background-color: transparent; "
    "  min-height: 3px; "
    "}\n",
    -1, NULL);

  gtk_style_context_add_provider_for_screen(
    gdk_screen_get_default(),
    GTK_STYLE_PROVIDER(css_provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  // Create main container - positioned at bottom center
  GtkWidget* prompt_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_halign(prompt_box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(prompt_box, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(prompt_box, 40);
  gtk_widget_set_size_request(prompt_box, 700, 80);

  GtkStyleContext* context = gtk_widget_get_style_context(prompt_box);
  gtk_style_context_add_class(context, "agent-prompt");

  // Inner container for proper padding
  GtkWidget* inner_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_start(inner_box, 20);
  gtk_widget_set_margin_end(inner_box, 24);
  gtk_widget_set_margin_top(inner_box, 10);
  gtk_widget_set_margin_bottom(inner_box, 10);
  gtk_box_pack_start(GTK_BOX(prompt_box), inner_box, TRUE, TRUE, 0);

  // Tasks button with overlay for status dot
  GtkWidget* tasks_overlay = gtk_overlay_new();

  GtkWidget* tasks_btn = gtk_button_new();
  gtk_widget_set_size_request(tasks_btn, 40, 40);
  gtk_style_context_add_class(gtk_widget_get_style_context(tasks_btn), "tasks-button");

  // Add bars icon to tasks button
  GdkPixbuf* bars_pixbuf = CreatePixbufFromSVG(OlibIcons::BARS, 18, 18);
  if (bars_pixbuf) {
    GtkWidget* icon = gtk_image_new_from_pixbuf(bars_pixbuf);
    gtk_button_set_image(GTK_BUTTON(tasks_btn), icon);
    g_object_unref(bars_pixbuf);
  }

  g_signal_connect(tasks_btn, "clicked", G_CALLBACK(on_tasks_button_clicked), this);
  g_main_window->tasks_button = tasks_btn;
  gtk_container_add(GTK_CONTAINER(tasks_overlay), tasks_btn);

  // Status dot (positioned at top-right of tasks button)
  GtkWidget* status_dot = gtk_drawing_area_new();
  gtk_widget_set_size_request(status_dot, 10, 10);
  gtk_widget_set_halign(status_dot, GTK_ALIGN_END);
  gtk_widget_set_valign(status_dot, GTK_ALIGN_START);
  gtk_widget_set_margin_top(status_dot, 2);
  gtk_widget_set_margin_end(status_dot, 2);
  gtk_style_context_add_class(gtk_widget_get_style_context(status_dot), "status-dot");
  gtk_style_context_add_class(gtk_widget_get_style_context(status_dot), "status-idle");
  g_main_window->status_dot = status_dot;
  gtk_overlay_add_overlay(GTK_OVERLAY(tasks_overlay), status_dot);

  gtk_box_pack_start(GTK_BOX(inner_box), tasks_overlay, FALSE, FALSE, 0);

  // Input entry - single line, matching homepage design
  GtkWidget* input_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(input_entry),
    "Tell me what to do... (e.g., 'go to google.com and search for banana')");
  gtk_style_context_add_class(gtk_widget_get_style_context(input_entry), "prompt-entry");
  gtk_widget_set_hexpand(input_entry, TRUE);
  g_main_window->prompt_input = input_entry;

  // Connect Enter key to submit
  g_signal_connect(input_entry, "activate", G_CALLBACK(on_prompt_send_clicked), this);
  g_signal_connect(input_entry, "key-press-event", G_CALLBACK(on_prompt_key_press), this);

  gtk_box_pack_start(GTK_BOX(inner_box), input_entry, TRUE, TRUE, 0);

  // Go/Stop button
  GtkWidget* go_btn = gtk_button_new_with_label("Go");
  gtk_widget_set_size_request(go_btn, 100, 40);
  gtk_style_context_add_class(gtk_widget_get_style_context(go_btn), "go-button");
  g_main_window->prompt_send_button = go_btn;
  g_signal_connect(go_btn, "clicked", G_CALLBACK(on_prompt_send_clicked), this);
  gtk_box_pack_start(GTK_BOX(inner_box), go_btn, FALSE, FALSE, 0);

  // Progress border (hidden by default) - will be added at bottom
  GtkWidget* progress_border = gtk_drawing_area_new();
  gtk_widget_set_size_request(progress_border, -1, 3);
  gtk_widget_set_valign(progress_border, GTK_ALIGN_END);
  gtk_style_context_add_class(gtk_widget_get_style_context(progress_border), "progress-border");
  g_main_window->progress_border = progress_border;

  g_main_window->agent_prompt_overlay = prompt_box;
  g_main_window->pulse_animation_id = 0;
  g_main_window->progress_animation_id = 0;
  gtk_overlay_add_overlay(GTK_OVERLAY(g_main_window->overlay), prompt_box);

  LOG_DEBUG("UIDelegate", "Created agent prompt overlay matching macOS design");
}

void OwlUIDelegate::CreateTasksPanel() {
  if (!g_main_window || !g_main_window->overlay) return;

  // Create tasks panel (positioned on the right side)
  GtkWidget* tasks_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(tasks_box, 300, 400);
  gtk_widget_set_halign(tasks_box, GTK_ALIGN_END);
  gtk_widget_set_valign(tasks_box, GTK_ALIGN_START);
  gtk_widget_set_margin_top(tasks_box, 80);
  gtk_widget_set_margin_end(tasks_box, 20);

  // Apply CSS styling
  GtkCssProvider* css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider,
    ".tasks-panel { background-color: white; border-radius: 8px; padding: 16px; box-shadow: 0 4px 16px rgba(0,0,0,0.2); }\n"
    ".tasks-title { font-size: 16px; font-weight: bold; color: #333; }\n"
    ".task-item { padding: 8px; margin: 4px 0; background-color: #f5f5f5; border-radius: 4px; }\n",
    -1, NULL);

  GtkStyleContext* context = gtk_widget_get_style_context(tasks_box);
  gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_class(context, "tasks-panel");

  // Title
  GtkWidget* title = gtk_label_new("Tasks");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(title), "tasks-title");
  gtk_box_pack_start(GTK_BOX(tasks_box), title, FALSE, FALSE, 0);

  // Tasks list (scrollable)
  GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);

  GtkWidget* tasks_list = gtk_label_new("No tasks yet");
  gtk_label_set_xalign(GTK_LABEL(tasks_list), 0.0);
  gtk_label_set_yalign(GTK_LABEL(tasks_list), 0.0);
  gtk_label_set_line_wrap(GTK_LABEL(tasks_list), TRUE);
  gtk_container_add(GTK_CONTAINER(scroll), tasks_list);

  gtk_box_pack_start(GTK_BOX(tasks_box), scroll, TRUE, TRUE, 0);

  g_main_window->tasks_panel = tasks_box;
  gtk_overlay_add_overlay(GTK_OVERLAY(g_main_window->overlay), tasks_box);
}

void OwlUIDelegate::CreateResponseArea() {
  if (!g_main_window || !g_main_window->overlay) return;

  // Create response area (bottom of screen)
  GtkWidget* response_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_size_request(response_box, 600, 150);
  gtk_widget_set_halign(response_box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(response_box, GTK_ALIGN_END);
  gtk_widget_set_margin_bottom(response_box, 20);

  // Apply CSS styling
  GtkCssProvider* css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider,
    ".response-area { background-color: white; border-radius: 8px; padding: 16px; box-shadow: 0 4px 16px rgba(0,0,0,0.2); }\n"
    ".response-title { font-size: 14px; font-weight: bold; color: #007acc; }\n"
    ".response-text { font-size: 13px; color: #333; }\n",
    -1, NULL);

  GtkStyleContext* context = gtk_widget_get_style_context(response_box);
  gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  gtk_style_context_add_class(context, "response-area");

  // Title
  GtkWidget* title = gtk_label_new("AI Response");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(title), "response-title");
  gtk_box_pack_start(GTK_BOX(response_box), title, FALSE, FALSE, 0);

  // Response text (scrollable)
  GtkWidget* scroll = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_widget_set_vexpand(scroll, TRUE);

  // Create text view for response with scrolling
  GtkWidget* response_text_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(response_text_view), FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(response_text_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(response_text_view), GTK_WRAP_WORD_CHAR);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(response_text_view), 8);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(response_text_view), 8);
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(response_text_view), 8);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(response_text_view), 8);
  gtk_style_context_add_class(gtk_widget_get_style_context(response_text_view), "response-text");

  GtkTextBuffer* text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(response_text_view));
  g_main_window->response_text_view = response_text_view;
  g_main_window->response_text_buffer = text_buffer;
  gtk_container_add(GTK_CONTAINER(scroll), response_text_view);

  gtk_box_pack_start(GTK_BOX(response_box), scroll, TRUE, TRUE, 0);

  g_main_window->response_area = response_box;
  gtk_overlay_add_overlay(GTK_OVERLAY(g_main_window->overlay), response_box);
}

void OwlUIDelegate::FocusWindow() {
  if (gtk_window_) {
    gtk_window_present(GTK_WINDOW(gtk_window_));
  }
}

void OwlUIDelegate::SetBrowser(CefRefPtr<CefBrowser> browser) {
  // Store browser reference (used for future interactions)
  LOG_DEBUG("UIDelegate", "SetBrowser called - browser ID: " + std::to_string(browser->GetIdentifier()));
}

void OwlUIDelegate::ShowWindow() {
  if (gtk_window_) {
    gtk_widget_show(GTK_WIDGET(gtk_window_));
  }
}

void OwlUIDelegate::HideWindow() {
  if (gtk_window_) {
    gtk_widget_hide(GTK_WIDGET(gtk_window_));
  }
}

void OwlUIDelegate::CloseWindow() {
  if (gtk_window_) {
    gtk_widget_destroy(GTK_WIDGET(gtk_window_));
  }
}

void OwlUIDelegate::SetWindowTitle(const std::string& title) {
  if (gtk_window_) {
    gtk_window_set_title(GTK_WINDOW(gtk_window_), title.c_str());
    LOG_DEBUG("UIDelegate", "Window title set to: " + title);
  }
}

void OwlUIDelegate::NewTab(const std::string& url) {
  LOG_DEBUG("UIDelegate", "NewTab called: " + url);
  // On Linux, create a new window (GTK doesn't have native tab support like macOS)
  CefRefPtr<OwlUIBrowser> ui_browser(new OwlUIBrowser);
  ui_browser->CreateBrowserWindow(url);
}

void OwlUIDelegate::ShowSidebar() {
  sidebar_visible_ = true;
  LOG_DEBUG("UIDelegate", "Sidebar shown");
}

void OwlUIDelegate::HideSidebar() {
  sidebar_visible_ = false;
  LOG_DEBUG("UIDelegate", "Sidebar hidden");
}

void OwlUIDelegate::ToggleSidebar() {
  sidebar_visible_ = !sidebar_visible_;
  LOG_DEBUG("UIDelegate", "Sidebar toggled: " + std::string(sidebar_visible_ ? "visible" : "hidden"));
}

void OwlUIDelegate::ShowAgentPrompt() {
  if (!g_main_window || !g_main_window->agent_prompt_overlay) return;

  gtk_widget_show_all(g_main_window->agent_prompt_overlay);
  agent_prompt_visible_ = true;

  // Focus the text input
  if (g_main_window->prompt_input) {
    gtk_widget_grab_focus(g_main_window->prompt_input);
  }

  LOG_DEBUG("UIDelegate", "Agent prompt shown");
}

void OwlUIDelegate::HideAgentPrompt() {
  if (!g_main_window || !g_main_window->agent_prompt_overlay) return;

  gtk_widget_hide(g_main_window->agent_prompt_overlay);
  agent_prompt_visible_ = false;

  // Clear the input
  if (g_main_window->prompt_input) {
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(g_main_window->prompt_input));
    gtk_text_buffer_set_text(buffer, "", -1);
  }

  LOG_DEBUG("UIDelegate", "Agent prompt hidden");
}

void OwlUIDelegate::UpdateAgentStatus(const std::string& status) {
  LOG_DEBUG("UIDelegate", "Agent status: " + status);
  // Could update a status label if we add one
}

void OwlUIDelegate::ExecutePrompt(const std::string& prompt) {
  if (!browser_handler_) {
    LOG_ERROR("UIDelegate", "Cannot execute prompt - no browser handler");
    return;
  }

  if (prompt.empty()) {
    LOG_ERROR("UIDelegate", "Prompt is empty");
    return;
  }

  LOG_DEBUG("UIDelegate", "Executing prompt: " + prompt);

  // DO NOT hide overlay - keep it visible to show task progress (matching macOS)
  // Execute through browser handler
  browser_handler_->ExecuteAgentPrompt(prompt);
}

void OwlUIDelegate::StopExecution() {
  LOG_DEBUG("UIDelegate", "Stop execution requested");

  // Call agent controller to stop execution
  OwlAgentController::GetInstance()->StopExecution();

  // Clear all tasks from task manager
  OwlTaskState::GetInstance()->Clear();

  // Update tasks list display
  UpdateTasksList();

  // NOTE: Do NOT call SetTaskExecuting(false) here
  // The status callback in owl_ui_browser.cc will call it when the agent
  // controller's state changes to IDLE, ensuring the UI updates only after
  // execution has fully stopped.

  LOG_DEBUG("UIDelegate", "Execution stop requested, tasks cleared");
}

void OwlUIDelegate::SetTaskExecuting(bool executing) {
  task_executing_ = executing;

  if (!g_main_window) return;

  GtkWidget* send_btn = g_main_window->prompt_send_button;
  GtkWidget* input = g_main_window->prompt_input;

  if (send_btn) {
    GtkStyleContext* ctx = gtk_widget_get_style_context(send_btn);

    if (executing) {
      // Change to Stop button
      gtk_button_set_label(GTK_BUTTON(send_btn), "Stop");
      gtk_style_context_remove_class(ctx, "go-button");
      gtk_style_context_add_class(ctx, "stop-button");
    } else {
      // Change back to Go button
      gtk_button_set_label(GTK_BUTTON(send_btn), "Go");
      gtk_style_context_remove_class(ctx, "stop-button");
      gtk_style_context_add_class(ctx, "go-button");
    }
  }

  // Disable/enable input during execution
  if (input && GTK_IS_ENTRY(input)) {
    gtk_widget_set_sensitive(input, !executing);
  }

  UpdateTaskStatusDot();
  LOG_DEBUG("UIDelegate", "Task executing: " + std::string(executing ? "yes" : "no"));
}

void OwlUIDelegate::UpdateTaskStatusDot() {
  if (!g_main_window || !g_main_window->status_dot) return;

  GtkStyleContext* ctx = gtk_widget_get_style_context(g_main_window->status_dot);

  // Remove all status classes
  gtk_style_context_remove_class(ctx, "status-idle");
  gtk_style_context_remove_class(ctx, "status-planning");
  gtk_style_context_remove_class(ctx, "status-executing");
  gtk_style_context_remove_class(ctx, "status-waiting");
  gtk_style_context_remove_class(ctx, "status-completed");
  gtk_style_context_remove_class(ctx, "status-error");

  // Get current agent state and apply corresponding class
  auto status = OwlAgentController::GetInstance()->GetStatus();
  std::string state_name;

  switch (status.state) {
    case OwlAgentController::AgentState::IDLE:
      gtk_style_context_add_class(ctx, "status-idle");
      state_name = "IDLE";
      break;
    case OwlAgentController::AgentState::PLANNING:
      gtk_style_context_add_class(ctx, "status-planning");
      state_name = "PLANNING";
      break;
    case OwlAgentController::AgentState::EXECUTING:
      gtk_style_context_add_class(ctx, "status-executing");
      state_name = "EXECUTING";
      break;
    case OwlAgentController::AgentState::WAITING_FOR_USER:
      gtk_style_context_add_class(ctx, "status-waiting");
      state_name = "WAITING_FOR_USER";
      break;
    case OwlAgentController::AgentState::COMPLETED:
      gtk_style_context_add_class(ctx, "status-completed");
      state_name = "COMPLETED";
      break;
    case OwlAgentController::AgentState::ERROR:
      gtk_style_context_add_class(ctx, "status-error");
      state_name = "ERROR";
      break;
  }

  gtk_widget_queue_draw(g_main_window->status_dot);
  LOG_DEBUG("UIDelegate", "Task status dot updated to: " + state_name);
}

void OwlUIDelegate::UpdateTasksList() {
  if (!g_main_window || !g_main_window->tasks_panel) return;

  // Get tasks from OwlTaskState
  OwlTaskState* task_state = OwlTaskState::GetInstance();
  if (!task_state) return;

  // Build HTML representation of tasks using actual API
  std::vector<TaskInfo> tasks = task_state->GetTasks();
  std::string tasks_html;
  for (size_t i = 0; i < tasks.size(); i++) {
    const auto& task = tasks[i];
    std::string status_icon;
    std::string status_color;
    switch (task.status) {
      case TaskStatus::PENDING:
        status_icon = "○";
        status_color = "#888";
        break;
      case TaskStatus::ACTIVE:
        status_icon = "●";
        status_color = "#007acc";
        break;
      case TaskStatus::COMPLETED:
        status_icon = "✓";
        status_color = "#4caf50";
        break;
      case TaskStatus::FAILED:
        status_icon = "✗";
        status_color = "#f44336";
        break;
    }
    tasks_html += "<span foreground='" + status_color + "'>" + status_icon + "</span> ";
    tasks_html += task.description;
    if (!task.result.empty()) {
      tasks_html += " <small><span foreground='#666'>(" + task.result + ")</span></small>";
    }
    tasks_html += "\n";
  }

  // Find the label inside the scrolled window
  GList* children = gtk_container_get_children(GTK_CONTAINER(g_main_window->tasks_panel));
  for (GList* l = children; l != NULL; l = l->next) {
    GtkWidget* child = GTK_WIDGET(l->data);
    if (GTK_IS_SCROLLED_WINDOW(child)) {
      GtkWidget* label = gtk_bin_get_child(GTK_BIN(child));
      if (label && GTK_IS_LABEL(label)) {
        gtk_label_set_markup(GTK_LABEL(label), tasks_html.c_str());
      }
      break;
    }
  }
  g_list_free(children);

  LOG_DEBUG("UIDelegate", "Tasks list updated");
}

void OwlUIDelegate::ShowTasksList() {
  if (!g_main_window || !g_main_window->tasks_panel) return;

  UpdateTasksList();
  gtk_widget_show_all(g_main_window->tasks_panel);
  tasks_list_visible_ = true;

  LOG_DEBUG("UIDelegate", "Tasks list shown");
}

void OwlUIDelegate::HideTasksList() {
  if (!g_main_window || !g_main_window->tasks_panel) return;

  gtk_widget_hide(g_main_window->tasks_panel);
  tasks_list_visible_ = false;

  LOG_DEBUG("UIDelegate", "Tasks list hidden");
}

void OwlUIDelegate::ToggleTasksList() {
  if (tasks_list_visible_) {
    HideTasksList();
  } else {
    ShowTasksList();
  }
}

void OwlUIDelegate::ShowResponseArea(const std::string& response_text) {
  if (!g_main_window || !g_main_window->response_area) return;

  // Update text in the text view
  if (g_main_window->response_text_buffer) {
    gtk_text_buffer_set_text(g_main_window->response_text_buffer, response_text.c_str(), -1);
  }

  gtk_widget_show_all(g_main_window->response_area);

  LOG_DEBUG("UIDelegate", "Response area shown");
}

void OwlUIDelegate::HideResponseArea() {
  if (!g_main_window || !g_main_window->response_area) return;

  gtk_widget_hide(g_main_window->response_area);

  LOG_DEBUG("UIDelegate", "Response area hidden");
}

void OwlUIDelegate::UpdateResponseText(const std::string& text) {
  if (!g_main_window || !g_main_window->response_text_buffer) return;

  gtk_text_buffer_set_text(g_main_window->response_text_buffer, text.c_str(), -1);
}

void OwlUIDelegate::RepositionOverlaysForResize() {
  // GTK overlay automatically repositions overlays
  LOG_DEBUG("UIDelegate", "Overlays repositioned for window resize");
}

void OwlUIDelegate::CleanupOverlays() {
  HideAgentPrompt();
  HideTasksList();
  HideResponseArea();
  LOG_DEBUG("UIDelegate", "Overlays cleaned up");
}

// GTK callback implementations
static gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer data) {
  GTKUIComponents* components = static_cast<GTKUIComponents*>(data);

  LOG_DEBUG("UIDelegate", "Window delete event, is_closing=" + std::string(components->is_closing ? "true" : "false"));

  // Check if agent prompt is visible - hide it instead of closing
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate && delegate->IsAgentPromptVisible()) {
    LOG_DEBUG("UIDelegate", "Agent prompt visible, hiding instead of closing");
    delegate->HideAgentPrompt();
    return TRUE;  // Don't close window
  }

  // If already closing, allow close
  if (components->is_closing) {
    LOG_DEBUG("UIDelegate", "Already closing, allowing window to close");
    return FALSE;
  }

  // Request browser close
  components->is_closing = true;
  LOG_DEBUG("UIDelegate", "Requesting browser close");

  // CEF will handle the actual close through DoClose callback
  // For now, just allow the window to close
  return FALSE;
}

static void on_window_destroy(GtkWidget* widget, gpointer data) {
  (void)data;  // Unused parameter

  LOG_DEBUG("UIDelegate", "Window destroy event");

  // Cleanup
  OwlUIDelegate* delegate = OwlUIDelegate::GetInstance();
  if (delegate) {
    delegate->CleanupOverlays();
  }

  // Quit application
  gtk_main_quit();
}

static void on_window_size_allocate(GtkWidget* widget, GdkRectangle* allocation, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (delegate) {
    delegate->RepositionOverlaysForResize();
  }
}

static void on_prompt_send_clicked(GtkButton* button, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (!delegate || !g_main_window || !g_main_window->prompt_input) return;

  // Check if task is executing - if so, this is a stop request
  if (OwlAgentController::GetInstance()->IsExecuting()) {
    LOG_DEBUG("UIDelegate", "Stop button clicked - stopping execution");
    delegate->StopExecution();
    return;
  }

  // Get text from input (now using GtkEntry instead of GtkTextView)
  const gchar* text = nullptr;
  if (GTK_IS_ENTRY(g_main_window->prompt_input)) {
    text = gtk_entry_get_text(GTK_ENTRY(g_main_window->prompt_input));
  }

  if (text && strlen(text) > 0) {
    std::string prompt(text);

    // Clear input
    if (GTK_IS_ENTRY(g_main_window->prompt_input)) {
      gtk_entry_set_text(GTK_ENTRY(g_main_window->prompt_input), "");
    }

    // Set executing state
    delegate->SetTaskExecuting(true);

    // Execute the prompt
    delegate->ExecutePrompt(prompt);
  }
}

static void on_prompt_close_clicked(GtkButton* button, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (delegate) {
    delegate->HideAgentPrompt();
  }
}

// Reserved for future use when tasks button is added
__attribute__((unused))
static void on_tasks_button_clicked(GtkButton* button, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (delegate) {
    delegate->ToggleTasksList();
  }
}

static gboolean on_prompt_key_press(GtkWidget* widget, GdkEventKey* event, gpointer data) {
  // Handle Ctrl+Enter to submit
  if ((event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) &&
      (event->state & GDK_CONTROL_MASK)) {
    on_prompt_send_clicked(NULL, data);
    return TRUE;
  }
  // Handle Escape to close
  if (event->keyval == GDK_KEY_Escape) {
    on_prompt_close_clicked(NULL, data);
    return TRUE;
  }
  return FALSE;
}

// Proxy overlay callback forward declarations
static void on_proxy_save_clicked(GtkButton* button, gpointer data);
static void on_proxy_connect_clicked(GtkButton* button, gpointer data);
static void on_proxy_close_clicked(GtkButton* button, gpointer data);

// Forward declaration for CA certificate browse callback
static void on_proxy_ca_browse_clicked(GtkButton* button, gpointer data);
static void on_proxy_ca_clear_clicked(GtkButton* button, gpointer data);

void OwlUIDelegate::CreateProxyOverlay() {
  if (!g_main_window || !g_main_window->overlay) return;

  // Create proxy overlay container - matching macOS design
  GtkWidget* proxy_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_halign(proxy_box, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(proxy_box, GTK_ALIGN_CENTER);
  gtk_widget_set_size_request(proxy_box, 420, -1);

  // Apply CSS styling matching macOS
  GtkCssProvider* css_provider = gtk_css_provider_new();
  gtk_css_provider_load_from_data(css_provider,
    ".proxy-overlay { "
    "  background-color: white; "
    "  border-radius: 16px; "
    "  border: 1px solid #d9d9d9; "
    "  padding: 24px; "
    "  box-shadow: 0 2px 20px rgba(0,0,0,0.15); "
    "}\n"
    ".proxy-title { font-size: 20px; font-weight: bold; color: #1a1a1a; }\n"
    ".proxy-label { font-size: 14px; font-weight: 500; color: #404040; margin-top: 4px; }\n"
    ".proxy-entry { "
    "  border-radius: 6px; "
    "  border: 1px solid #d0d0d0; "
    "  padding: 8px 10px; "
    "  font-size: 13px; "
    "  min-height: 32px; "
    "}\n"
    ".proxy-entry:focus { border-color: #4285f4; }\n"
    ".proxy-save-button { "
    "  background-color: #d9d9d9; "
    "  color: #333; "
    "  border-radius: 10px; "
    "  font-weight: 500; "
    "  padding: 10px 24px; "
    "  min-height: 44px; "
    "}\n"
    ".proxy-save-button:hover { background-color: #c9c9c9; }\n"
    ".proxy-connect-button { "
    "  background-color: #3380cc; "
    "  color: white; "
    "  border-radius: 10px; "
    "  font-weight: 500; "
    "  padding: 10px 24px; "
    "  min-height: 44px; "
    "}\n"
    ".proxy-connect-button:hover { background-color: #2970b9; }\n"
    ".proxy-connect-button:disabled { background-color: #b3b3b3; }\n"
    ".proxy-close-button { "
    "  background-color: #ebebeb; "
    "  border-radius: 13px; "
    "  min-width: 26px; min-height: 26px; "
    "  padding: 0; "
    "}\n"
    ".proxy-close-button:hover { background-color: #ddd; }\n"
    ".proxy-status { font-size: 13px; color: #808080; margin-top: 4px; }\n"
    ".proxy-status-connected { color: #4caf50; }\n"
    ".proxy-status-disconnected { color: #808080; }\n"
    ".proxy-checkbox { font-size: 13px; }\n"
    ".proxy-ca-path { font-size: 12px; color: #808080; }\n"
    ".proxy-browse-button { font-size: 11px; padding: 4px 8px; }\n",
    -1, NULL);

  gtk_style_context_add_provider_for_screen(
    gdk_screen_get_default(),
    GTK_STYLE_PROVIDER(css_provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  GtkStyleContext* context = gtk_widget_get_style_context(proxy_box);
  gtk_style_context_add_class(context, "proxy-overlay");

  // Header with title and close button
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  gtk_widget_set_margin_bottom(header, 8);

  GtkWidget* title = gtk_label_new("Proxy Settings");
  gtk_style_context_add_class(gtk_widget_get_style_context(title), "proxy-title");
  gtk_label_set_xalign(GTK_LABEL(title), 0.0);
  gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);

  GtkWidget* close_btn = gtk_button_new();
  gtk_widget_set_size_request(close_btn, 26, 26);
  gtk_style_context_add_class(gtk_widget_get_style_context(close_btn), "proxy-close-button");
  GdkPixbuf* close_pixbuf = CreatePixbufFromSVG(OlibIcons::XMARK, 11, 11);
  if (close_pixbuf) {
    GtkWidget* close_icon = gtk_image_new_from_pixbuf(close_pixbuf);
    gtk_button_set_image(GTK_BUTTON(close_btn), close_icon);
    g_object_unref(close_pixbuf);
  } else {
    gtk_button_set_label(GTK_BUTTON(close_btn), "×");
  }
  g_signal_connect(close_btn, "clicked", G_CALLBACK(on_proxy_close_clicked), this);
  gtk_box_pack_end(GTK_BOX(header), close_btn, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(proxy_box), header, FALSE, FALSE, 0);

  // Proxy type dropdown - with more options matching macOS
  GtkWidget* type_label = gtk_label_new("Type");
  gtk_label_set_xalign(GTK_LABEL(type_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(type_label), "proxy-label");
  gtk_box_pack_start(GTK_BOX(proxy_box), type_label, FALSE, FALSE, 0);

  GtkWidget* type_combo = gtk_combo_box_text_new();
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "HTTP");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "HTTPS");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "SOCKS4");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "SOCKS5");
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(type_combo), "SOCKS5H (Stealth)");
  gtk_combo_box_set_active(GTK_COMBO_BOX(type_combo), 4);  // Default to SOCKS5H for stealth
  g_main_window->proxy_type_combo = type_combo;
  gtk_box_pack_start(GTK_BOX(proxy_box), type_combo, FALSE, FALSE, 0);

  // Host input
  GtkWidget* host_label = gtk_label_new("Host");
  gtk_label_set_xalign(GTK_LABEL(host_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(host_label), "proxy-label");
  gtk_box_pack_start(GTK_BOX(proxy_box), host_label, FALSE, FALSE, 0);

  GtkWidget* host_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(host_entry), "proxy.example.com");
  gtk_style_context_add_class(gtk_widget_get_style_context(host_entry), "proxy-entry");
  g_main_window->proxy_host_entry = host_entry;
  gtk_box_pack_start(GTK_BOX(proxy_box), host_entry, FALSE, FALSE, 0);

  // Port input
  GtkWidget* port_label = gtk_label_new("Port");
  gtk_label_set_xalign(GTK_LABEL(port_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(port_label), "proxy-label");
  gtk_box_pack_start(GTK_BOX(proxy_box), port_label, FALSE, FALSE, 0);

  GtkWidget* port_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(port_entry), "1080");
  gtk_widget_set_size_request(port_entry, 100, -1);
  gtk_style_context_add_class(gtk_widget_get_style_context(port_entry), "proxy-entry");
  g_main_window->proxy_port_entry = port_entry;
  gtk_box_pack_start(GTK_BOX(proxy_box), port_entry, FALSE, FALSE, 0);

  // Username (optional)
  GtkWidget* user_label = gtk_label_new("Username");
  gtk_label_set_xalign(GTK_LABEL(user_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(user_label), "proxy-label");
  gtk_box_pack_start(GTK_BOX(proxy_box), user_label, FALSE, FALSE, 0);

  GtkWidget* user_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(user_entry), "Optional");
  gtk_style_context_add_class(gtk_widget_get_style_context(user_entry), "proxy-entry");
  g_main_window->proxy_username_entry = user_entry;
  gtk_box_pack_start(GTK_BOX(proxy_box), user_entry, FALSE, FALSE, 0);

  // Password (optional)
  GtkWidget* pass_label = gtk_label_new("Password");
  gtk_label_set_xalign(GTK_LABEL(pass_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(pass_label), "proxy-label");
  gtk_box_pack_start(GTK_BOX(proxy_box), pass_label, FALSE, FALSE, 0);

  GtkWidget* pass_entry = gtk_entry_new();
  gtk_entry_set_visibility(GTK_ENTRY(pass_entry), FALSE);
  gtk_entry_set_placeholder_text(GTK_ENTRY(pass_entry), "Optional");
  gtk_style_context_add_class(gtk_widget_get_style_context(pass_entry), "proxy-entry");
  g_main_window->proxy_password_entry = pass_entry;
  gtk_box_pack_start(GTK_BOX(proxy_box), pass_entry, FALSE, FALSE, 0);

  // Timezone input (for stealth mode)
  GtkWidget* tz_label = gtk_label_new("Timezone");
  gtk_label_set_xalign(GTK_LABEL(tz_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(tz_label), "proxy-label");
  gtk_box_pack_start(GTK_BOX(proxy_box), tz_label, FALSE, FALSE, 0);

  GtkWidget* tz_entry = gtk_entry_new();
  gtk_entry_set_placeholder_text(GTK_ENTRY(tz_entry), "e.g., America/New_York");
  gtk_style_context_add_class(gtk_widget_get_style_context(tz_entry), "proxy-entry");
  g_main_window->proxy_timezone_entry = tz_entry;
  gtk_box_pack_start(GTK_BOX(proxy_box), tz_entry, FALSE, FALSE, 0);

  // Stealth mode checkbox
  GtkWidget* stealth_check = gtk_check_button_new_with_label("Enable Stealth Mode (WebRTC block, fingerprint)");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stealth_check), TRUE);  // Default enabled
  gtk_style_context_add_class(gtk_widget_get_style_context(stealth_check), "proxy-checkbox");
  g_main_window->proxy_stealth_checkbox = stealth_check;
  gtk_box_pack_start(GTK_BOX(proxy_box), stealth_check, FALSE, FALSE, 0);

  // CA Certificate checkbox
  GtkWidget* ca_check = gtk_check_button_new_with_label("Trust Custom CA (for Charles, mitmproxy, etc.)");
  gtk_style_context_add_class(gtk_widget_get_style_context(ca_check), "proxy-checkbox");
  g_main_window->proxy_ca_checkbox = ca_check;
  gtk_box_pack_start(GTK_BOX(proxy_box), ca_check, FALSE, FALSE, 0);

  // CA Certificate path with Browse and Clear buttons
  GtkWidget* ca_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

  GtkWidget* ca_path_label = gtk_label_new("No certificate selected");
  gtk_label_set_xalign(GTK_LABEL(ca_path_label), 0.0);
  gtk_label_set_ellipsize(GTK_LABEL(ca_path_label), PANGO_ELLIPSIZE_START);
  gtk_widget_set_hexpand(ca_path_label, TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(ca_path_label), "proxy-ca-path");
  g_main_window->proxy_ca_path_label = ca_path_label;
  gtk_box_pack_start(GTK_BOX(ca_box), ca_path_label, TRUE, TRUE, 0);

  GtkWidget* browse_btn = gtk_button_new_with_label("Browse");
  gtk_style_context_add_class(gtk_widget_get_style_context(browse_btn), "proxy-browse-button");
  g_signal_connect(browse_btn, "clicked", G_CALLBACK(on_proxy_ca_browse_clicked), this);
  gtk_box_pack_start(GTK_BOX(ca_box), browse_btn, FALSE, FALSE, 0);

  GtkWidget* clear_btn = gtk_button_new_with_label("Clear");
  gtk_style_context_add_class(gtk_widget_get_style_context(clear_btn), "proxy-browse-button");
  g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_proxy_ca_clear_clicked), this);
  gtk_box_pack_start(GTK_BOX(ca_box), clear_btn, FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(proxy_box), ca_box, FALSE, FALSE, 0);

  // Status label
  GtkWidget* status_label = gtk_label_new("Status: Disconnected");
  gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
  gtk_style_context_add_class(gtk_widget_get_style_context(status_label), "proxy-status");
  gtk_widget_set_margin_top(status_label, 8);
  g_main_window->proxy_status_label = status_label;
  gtk_box_pack_start(GTK_BOX(proxy_box), status_label, FALSE, FALSE, 0);

  // Buttons - Save and Connect side by side
  GtkWidget* button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_widget_set_margin_top(button_box, 12);

  GtkWidget* save_btn = gtk_button_new_with_label("Save");
  gtk_widget_set_hexpand(save_btn, TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(save_btn), "proxy-save-button");
  g_main_window->proxy_save_button = save_btn;
  g_signal_connect(save_btn, "clicked", G_CALLBACK(on_proxy_save_clicked), this);
  gtk_box_pack_start(GTK_BOX(button_box), save_btn, TRUE, TRUE, 0);

  GtkWidget* connect_btn = gtk_button_new_with_label("Connect");
  gtk_widget_set_hexpand(connect_btn, TRUE);
  gtk_style_context_add_class(gtk_widget_get_style_context(connect_btn), "proxy-connect-button");
  gtk_widget_set_sensitive(connect_btn, FALSE);  // Disabled until settings saved
  g_main_window->proxy_connect_button = connect_btn;
  g_signal_connect(connect_btn, "clicked", G_CALLBACK(on_proxy_connect_clicked), this);
  gtk_box_pack_start(GTK_BOX(button_box), connect_btn, TRUE, TRUE, 0);

  gtk_box_pack_start(GTK_BOX(proxy_box), button_box, FALSE, FALSE, 0);

  g_main_window->proxy_overlay = proxy_box;
  g_main_window->proxy_settings_saved = false;
  gtk_overlay_add_overlay(GTK_OVERLAY(g_main_window->overlay), proxy_box);

  LOG_DEBUG("UIDelegate", "Proxy overlay created with stealth and CA options");
}

void OwlUIDelegate::ShowProxyOverlay() {
  if (!g_main_window) return;

  // Create proxy overlay if it doesn't exist
  if (!g_main_window->proxy_overlay) {
    CreateProxyOverlay();
  }

  if (g_main_window->proxy_overlay) {
    gtk_widget_show_all(g_main_window->proxy_overlay);
    proxy_overlay_visible_ = true;
    UpdateProxyStatus();
    LOG_DEBUG("UIDelegate", "Proxy overlay shown");
  }
}

void OwlUIDelegate::HideProxyOverlay() {
  if (!g_main_window || !g_main_window->proxy_overlay) return;

  gtk_widget_hide(g_main_window->proxy_overlay);
  proxy_overlay_visible_ = false;
  LOG_DEBUG("UIDelegate", "Proxy overlay hidden");
}

void OwlUIDelegate::ToggleProxyOverlay() {
  if (proxy_overlay_visible_) {
    HideProxyOverlay();
  } else {
    ShowProxyOverlay();
  }
}

void OwlUIDelegate::SaveProxySettings() {
  if (!g_main_window) return;

  // Get values from UI
  const gchar* host = gtk_entry_get_text(GTK_ENTRY(g_main_window->proxy_host_entry));
  const gchar* port_str = gtk_entry_get_text(GTK_ENTRY(g_main_window->proxy_port_entry));
  const gchar* username = gtk_entry_get_text(GTK_ENTRY(g_main_window->proxy_username_entry));
  const gchar* password = gtk_entry_get_text(GTK_ENTRY(g_main_window->proxy_password_entry));
  int type_idx = gtk_combo_box_get_active(GTK_COMBO_BOX(g_main_window->proxy_type_combo));

  if (!host || strlen(host) == 0) {
    LOG_ERROR("UIDelegate", "Proxy host is required");
    return;
  }

  int port = 1080;  // Default SOCKS port
  if (port_str && strlen(port_str) > 0) {
    port = atoi(port_str);
  }

  // Map combo box index to ProxyType
  // 0: HTTP, 1: HTTPS, 2: SOCKS4, 3: SOCKS5, 4: SOCKS5H
  ProxyType proxy_type = ProxyType::SOCKS5;  // Default
  switch (type_idx) {
    case 0: proxy_type = ProxyType::HTTP; break;
    case 1: proxy_type = ProxyType::HTTPS; break;
    case 2: proxy_type = ProxyType::SOCKS4; break;
    case 3: proxy_type = ProxyType::SOCKS5; break;
    case 4: proxy_type = ProxyType::SOCKS5H; break;
  }

  ProxyConfig config;
  config.type = proxy_type;
  config.host = std::string(host);
  config.port = port;
  if (username && strlen(username) > 0) {
    config.username = std::string(username);
  }
  if (password && strlen(password) > 0) {
    config.password = std::string(password);
  }

  // Get timezone if specified
  if (g_main_window->proxy_timezone_entry) {
    const gchar* timezone = gtk_entry_get_text(GTK_ENTRY(g_main_window->proxy_timezone_entry));
    if (timezone && strlen(timezone) > 0) {
      config.timezone_override = std::string(timezone);
      config.spoof_timezone = true;
    }
  }

  // Get stealth mode setting
  if (g_main_window->proxy_stealth_checkbox) {
    config.stealth_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_main_window->proxy_stealth_checkbox));
  }

  // Get CA certificate settings
  if (g_main_window->proxy_ca_checkbox && g_main_window->proxy_ca_path_label) {
    config.trust_custom_ca = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(g_main_window->proxy_ca_checkbox));
    if (config.trust_custom_ca) {
      const gchar* ca_path = gtk_label_get_text(GTK_LABEL(g_main_window->proxy_ca_path_label));
      if (ca_path && strcmp(ca_path, "No certificate selected") != 0) {
        config.ca_cert_path = std::string(ca_path);
      }
    }
  }

  OwlProxyManager::GetInstance()->SetProxyConfig(config);
  g_main_window->proxy_settings_saved = true;

  // Enable the Connect button
  if (g_main_window->proxy_connect_button) {
    gtk_widget_set_sensitive(g_main_window->proxy_connect_button, TRUE);
  }

  LOG_DEBUG("UIDelegate", "Proxy settings saved: " + config.host + ":" + std::to_string(config.port) +
           " (stealth=" + std::string(config.stealth_mode ? "true" : "false") + ")");
  UpdateProxyStatus();
}

void OwlUIDelegate::ConnectProxy() {
  if (!g_main_window || !g_main_window->proxy_settings_saved) {
    LOG_ERROR("UIDelegate", "Save proxy settings first");
    return;
  }

  auto* proxy_manager = OwlProxyManager::GetInstance();
  bool is_connected = (proxy_manager->GetStatus() == ProxyStatus::CONNECTED);
  if (is_connected) {
    // Disconnect
    proxy_manager->Disconnect();
    LOG_DEBUG("UIDelegate", "Proxy disconnected");
  } else {
    // Connect
    proxy_manager->Connect();
    LOG_DEBUG("UIDelegate", "Proxy connected");
  }

  UpdateProxyStatus();

  // Update toolbar proxy button state
  bool now_connected = (proxy_manager->GetStatus() == ProxyStatus::CONNECTED);
  if (toolbar_) {
    toolbar_->SetProxyConnected(now_connected);
  }
}

void OwlUIDelegate::DisconnectProxy() {
  auto* proxy_manager = OwlProxyManager::GetInstance();
  proxy_manager->Disconnect();
  UpdateProxyStatus();

  if (toolbar_) {
    toolbar_->SetProxyConnected(false);
  }

  LOG_DEBUG("UIDelegate", "Proxy disconnected");
}

void OwlUIDelegate::UpdateProxyStatus() {
  if (!g_main_window || !g_main_window->proxy_status_label) return;

  auto* proxy_manager = OwlProxyManager::GetInstance();
  bool connected = (proxy_manager->GetStatus() == ProxyStatus::CONNECTED);

  std::string status_text = connected ? "Status: Connected" : "Status: Not connected";
  gtk_label_set_text(GTK_LABEL(g_main_window->proxy_status_label), status_text.c_str());

  // Update connect button text
  if (g_main_window->proxy_connect_button) {
    gtk_button_set_label(GTK_BUTTON(g_main_window->proxy_connect_button),
                         connected ? "Disconnect" : "Connect");
  }

  // Update style class for status
  GtkStyleContext* ctx = gtk_widget_get_style_context(g_main_window->proxy_status_label);
  gtk_style_context_remove_class(ctx, "proxy-status-connected");
  gtk_style_context_remove_class(ctx, "proxy-status-disconnected");
  gtk_style_context_add_class(ctx, connected ? "proxy-status-connected" : "proxy-status-disconnected");
}

// CA Certificate browse callback
static void on_proxy_ca_browse_clicked(GtkButton* button, gpointer data) {
  (void)data;  // Unused
  if (!g_main_window || !g_main_window->proxy_ca_path_label) return;

  GtkWidget* dialog = gtk_file_chooser_dialog_new(
    "Select CA Certificate",
    GTK_WINDOW(g_main_window->window),
    GTK_FILE_CHOOSER_ACTION_OPEN,
    "_Cancel", GTK_RESPONSE_CANCEL,
    "_Open", GTK_RESPONSE_ACCEPT,
    NULL);

  // Add file filter for certificate files
  GtkFileFilter* filter = gtk_file_filter_new();
  gtk_file_filter_set_name(filter, "Certificate files (*.pem, *.crt, *.cer)");
  gtk_file_filter_add_pattern(filter, "*.pem");
  gtk_file_filter_add_pattern(filter, "*.crt");
  gtk_file_filter_add_pattern(filter, "*.cer");
  gtk_file_filter_add_pattern(filter, "*.der");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  GtkFileFilter* all_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(all_filter, "All files");
  gtk_file_filter_add_pattern(all_filter, "*");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), all_filter);

  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    gchar* filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    if (filename) {
      gtk_label_set_text(GTK_LABEL(g_main_window->proxy_ca_path_label), filename);
      gtk_widget_set_tooltip_text(g_main_window->proxy_ca_path_label, filename);
      LOG_DEBUG("UIDelegate", "Selected CA cert: " + std::string(filename));
      g_free(filename);
    }
  }

  gtk_widget_destroy(dialog);
}

// CA Certificate clear callback
static void on_proxy_ca_clear_clicked(GtkButton* button, gpointer data) {
  (void)data;  // Unused
  if (!g_main_window || !g_main_window->proxy_ca_path_label) return;

  gtk_label_set_text(GTK_LABEL(g_main_window->proxy_ca_path_label), "No certificate selected");
  gtk_widget_set_tooltip_text(g_main_window->proxy_ca_path_label, "");
  LOG_DEBUG("UIDelegate", "CA certificate cleared");
}

// Proxy overlay callbacks
static void on_proxy_save_clicked(GtkButton* button, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (delegate) {
    delegate->SaveProxySettings();
  }
}

static void on_proxy_connect_clicked(GtkButton* button, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (delegate) {
    delegate->ConnectProxy();
  }
}

static void on_proxy_close_clicked(GtkButton* button, gpointer data) {
  OwlUIDelegate* delegate = static_cast<OwlUIDelegate*>(data);
  if (delegate) {
    delegate->HideProxyOverlay();
  }
}

#endif  // OS_LINUX
