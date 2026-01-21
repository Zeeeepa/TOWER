/**
 * Owl Browser UI Toolbar - Linux (GTK3)
 *
 * This provides the browser toolbar with navigation buttons, address bar,
 * TLD autocomplete, AI Agent button, and proxy controls.
 *
 * Translated from macOS (owl_ui_toolbar.mm) to maintain feature parity.
 */

#include "owl_ui_toolbar.h"

#if defined(OS_LINUX)

#include "logger.h"
#include "../resources/icons/icons.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <librsvg/rsvg.h>
#include <string>
#include <functional>
#include <vector>
#include <algorithm>

// ============================================================================
// SVG Icon Rendering Helper
// ============================================================================

static GdkPixbuf* CreatePixbufFromSVG(const std::string& svgString, int width, int height) {
    if (svgString.empty()) return nullptr;

    GError* error = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_data(
        reinterpret_cast<const guint8*>(svgString.c_str()),
        svgString.length(),
        &error
    );

    if (!handle || error) {
        if (error) {
            LOG_ERROR("UIToolbar", "Failed to parse SVG: " + std::string(error->message));
            g_error_free(error);
        }
        return nullptr;
    }

    // Create a cairo surface and context
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    cairo_t* cr = cairo_create(surface);

    // Get SVG dimensions and scale appropriately
    RsvgDimensionData dim;
    rsvg_handle_get_dimensions(handle, &dim);

    double scale_x = static_cast<double>(width) / dim.width;
    double scale_y = static_cast<double>(height) / dim.height;
    double scale = std::min(scale_x, scale_y);

    // Center the icon
    double offset_x = (width - dim.width * scale) / 2;
    double offset_y = (height - dim.height * scale) / 2;

    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);

    // Render the SVG
    rsvg_handle_render_cairo(handle, cr);

    // Create pixbuf from surface
    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, width, height);

    // Cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    g_object_unref(handle);

    return pixbuf;
}

static GtkWidget* CreateIconButton(const std::string& svgIcon, const char* tooltip, int iconSize = 18) {
    GtkWidget* button = gtk_button_new();
    gtk_widget_set_tooltip_text(button, tooltip);

    GdkPixbuf* pixbuf = CreatePixbufFromSVG(svgIcon, iconSize, iconSize);
    if (pixbuf) {
        GtkWidget* image = gtk_image_new_from_pixbuf(pixbuf);
        gtk_button_set_image(GTK_BUTTON(button), image);
        g_object_unref(pixbuf);
    }

    return button;
}

// ============================================================================
// TLD Autocomplete Helper
// ============================================================================

struct TLDEntry {
    std::string tld;
    std::string description;
};

struct OlibPageEntry {
    std::string page;
    std::string description;
};

class TLDAutocompleteHelper {
public:
    TLDAutocompleteHelper(GtkWidget* entry, OwlUIToolbar* toolbar)
        : entry_(entry), toolbar_(toolbar), popup_window_(nullptr),
          tree_view_(nullptr), selected_index_(-1) {

        // Initialize TLD list
        tlds_ = {
            {".com", "Commercial"},
            {".org", "Organization"},
            {".net", "Network"},
            {".io", "Tech startups"},
            {".co", "Company"},
            {".ai", "Artificial Intelligence"},
            {".dev", "Developers"},
            {".app", "Applications"},
            {".tech", "Technology"},
            {".me", "Personal"},
            {".info", "Information"},
            {".biz", "Business"},
            {".ca", "Canada"},
            {".uk", "United Kingdom"},
            {".de", "Germany"},
            {".fr", "France"},
            {".jp", "Japan"},
            {".cn", "China"},
            {".in", "India"},
            {".br", "Brazil"}
        };

        // Initialize owl:// pages
        olib_pages_ = {
            {"homepage.html", "Browser Homepage"},
            {"signin_form.html", "Sign In Form Test Page"},
            {"user_form.html", "User Form Test Page"}
        };

        // Connect signals
        g_signal_connect(entry_, "changed", G_CALLBACK(OnEntryChanged), this);
        g_signal_connect(entry_, "key-press-event", G_CALLBACK(OnKeyPress), this);
        g_signal_connect(entry_, "focus-out-event", G_CALLBACK(OnFocusOut), this);
    }

    ~TLDAutocompleteHelper() {
        HideSuggestions();
        if (popup_window_) {
            gtk_widget_destroy(popup_window_);
        }
    }

    void ShowSuggestions(const std::string& domain, const std::string& filter, bool is_olib = false) {
        suggestions_.clear();

        if (is_olib) {
            // Filter owl:// pages
            std::string filter_lower = filter;
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

            for (const auto& page : olib_pages_) {
                std::string page_lower = page.page;
                std::transform(page_lower.begin(), page_lower.end(), page_lower.begin(), ::tolower);

                if (filter.empty() || page_lower.find(filter_lower) == 0) {
                    // Exclude exact matches
                    if (page_lower != filter_lower) {
                        suggestions_.push_back({"owl://" + page.page, page.description});
                    }
                }
            }
        } else {
            // Filter TLDs
            std::string filter_lower = filter;
            std::transform(filter_lower.begin(), filter_lower.end(), filter_lower.begin(), ::tolower);

            for (const auto& tld : tlds_) {
                std::string tld_without_dot = tld.tld.substr(1);
                std::string tld_lower = tld_without_dot;
                std::transform(tld_lower.begin(), tld_lower.end(), tld_lower.begin(), ::tolower);

                if (filter.empty() || tld_lower.find(filter_lower) == 0) {
                    // Exclude exact matches
                    if (tld_lower != filter_lower) {
                        suggestions_.push_back({domain + tld.tld, tld.description});
                    }
                }
            }
        }

        // Limit suggestions
        if (suggestions_.size() > 5) {
            suggestions_.resize(5);
        }

        if (suggestions_.empty()) {
            HideSuggestions();
            return;
        }

        ShowPopup();
    }

    void HideSuggestions() {
        if (popup_window_ && gtk_widget_get_visible(popup_window_)) {
            gtk_widget_hide(popup_window_);
        }
        selected_index_ = -1;
    }

    void SelectSuggestion(int index) {
        if (index < 0 || index >= static_cast<int>(suggestions_.size())) return;

        const auto& suggestion = suggestions_[index];
        gtk_entry_set_text(GTK_ENTRY(entry_), suggestion.first.c_str());
        HideSuggestions();

        // Navigate to the URL
        if (toolbar_) {
            toolbar_->ExecuteNavigateCallback(suggestion.first);
        }
    }

private:
    GtkWidget* entry_;
    OwlUIToolbar* toolbar_;
    GtkWidget* popup_window_;
    GtkWidget* tree_view_;
    std::vector<TLDEntry> tlds_;
    std::vector<OlibPageEntry> olib_pages_;
    std::vector<std::pair<std::string, std::string>> suggestions_;
    int selected_index_;

    void ShowPopup() {
        if (!popup_window_) {
            CreatePopupWindow();
        }

        // Update list store
        GtkListStore* store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree_view_)));
        gtk_list_store_clear(store);

        GtkTreeIter iter;
        for (const auto& suggestion : suggestions_) {
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter,
                0, suggestion.first.c_str(),
                1, suggestion.second.c_str(),
                -1);
        }

        // Position popup below entry
        GtkAllocation alloc;
        gtk_widget_get_allocation(entry_, &alloc);

        gint x, y;
        GdkWindow* gdk_window = gtk_widget_get_window(entry_);
        gdk_window_get_origin(gdk_window, &x, &y);

        int popup_height = std::min(static_cast<int>(suggestions_.size()) * 32 + 4, 160);
        gtk_window_move(GTK_WINDOW(popup_window_), x, y + alloc.height + 2);
        gtk_window_resize(GTK_WINDOW(popup_window_), alloc.width, popup_height);

        gtk_widget_show_all(popup_window_);
        selected_index_ = -1;
    }

    void CreatePopupWindow() {
        popup_window_ = gtk_window_new(GTK_WINDOW_POPUP);
        gtk_window_set_type_hint(GTK_WINDOW(popup_window_), GDK_WINDOW_TYPE_HINT_COMBO);

        // Create scrolled window
        GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
            GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
        gtk_container_add(GTK_CONTAINER(popup_window_), scrolled);

        // Create list store and tree view
        GtkListStore* store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
        tree_view_ = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        g_object_unref(store);

        gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree_view_), FALSE);
        gtk_tree_view_set_hover_selection(GTK_TREE_VIEW(tree_view_), TRUE);

        // URL column
        GtkCellRenderer* url_renderer = gtk_cell_renderer_text_new();
        g_object_set(url_renderer, "foreground", "#4287f5", "weight", PANGO_WEIGHT_SEMIBOLD, nullptr);
        GtkTreeViewColumn* url_column = gtk_tree_view_column_new_with_attributes(
            "URL", url_renderer, "text", 0, nullptr);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view_), url_column);

        // Description column
        GtkCellRenderer* desc_renderer = gtk_cell_renderer_text_new();
        g_object_set(desc_renderer, "foreground", "#888888", nullptr);
        GtkTreeViewColumn* desc_column = gtk_tree_view_column_new_with_attributes(
            "Description", desc_renderer, "text", 1, nullptr);
        gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view_), desc_column);

        // Connect row activation
        g_signal_connect(tree_view_, "row-activated", G_CALLBACK(OnRowActivated), this);

        gtk_container_add(GTK_CONTAINER(scrolled), tree_view_);

        // Apply dark styling
        GtkCssProvider* css = gtk_css_provider_new();
        gtk_css_provider_load_from_data(css,
            "window { background-color: #1e1e1e; border: 1px solid #333; }"
            "treeview { background-color: #1e1e1e; color: white; }"
            "treeview:selected { background-color: #3a3a3a; }",
            -1, nullptr);
        gtk_style_context_add_provider_for_screen(
            gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(css),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(css);
    }

    static void OnEntryChanged(GtkEditable* editable, gpointer user_data) {
        TLDAutocompleteHelper* helper = static_cast<TLDAutocompleteHelper*>(user_data);
        const gchar* text = gtk_entry_get_text(GTK_ENTRY(helper->entry_));
        std::string value(text);

        // Check for owl:// schema
        if (value.find("owl://") == 0) {
            std::string after_schema = value.substr(7);
            helper->ShowSuggestions("", after_schema, true);
            return;
        }

        // Find last dot for TLD autocomplete
        size_t last_dot = value.rfind('.');
        if (last_dot == std::string::npos) {
            helper->HideSuggestions();
            return;
        }

        std::string domain = value.substr(0, last_dot);
        std::string after_dot = value.substr(last_dot + 1);

        helper->ShowSuggestions(domain, after_dot, false);
    }

    static gboolean OnKeyPress(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
        TLDAutocompleteHelper* helper = static_cast<TLDAutocompleteHelper*>(user_data);

        if (!helper->popup_window_ || !gtk_widget_get_visible(helper->popup_window_)) {
            return FALSE;
        }

        if (event->keyval == GDK_KEY_Down) {
            helper->selected_index_ = std::min(helper->selected_index_ + 1,
                static_cast<int>(helper->suggestions_.size()) - 1);
            GtkTreePath* path = gtk_tree_path_new_from_indices(helper->selected_index_, -1);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(helper->tree_view_), path, nullptr, FALSE);
            gtk_tree_path_free(path);
            return TRUE;
        } else if (event->keyval == GDK_KEY_Up) {
            helper->selected_index_ = std::max(helper->selected_index_ - 1, 0);
            GtkTreePath* path = gtk_tree_path_new_from_indices(helper->selected_index_, -1);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(helper->tree_view_), path, nullptr, FALSE);
            gtk_tree_path_free(path);
            return TRUE;
        } else if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
            if (helper->selected_index_ >= 0) {
                helper->SelectSuggestion(helper->selected_index_);
                return TRUE;
            }
        } else if (event->keyval == GDK_KEY_Escape) {
            helper->HideSuggestions();
            return TRUE;
        }

        return FALSE;
    }

    static gboolean OnFocusOut(GtkWidget* widget, GdkEventFocus* event, gpointer user_data) {
        TLDAutocompleteHelper* helper = static_cast<TLDAutocompleteHelper*>(user_data);
        // Delay hiding to allow click on popup
        g_timeout_add(100, [](gpointer data) -> gboolean {
            TLDAutocompleteHelper* h = static_cast<TLDAutocompleteHelper*>(data);
            h->HideSuggestions();
            return G_SOURCE_REMOVE;
        }, helper);
        return FALSE;
    }

    static void OnRowActivated(GtkTreeView* tree_view, GtkTreePath* path,
                               GtkTreeViewColumn* column, gpointer user_data) {
        TLDAutocompleteHelper* helper = static_cast<TLDAutocompleteHelper*>(user_data);
        gint* indices = gtk_tree_path_get_indices(path);
        if (indices) {
            helper->SelectSuggestion(indices[0]);
        }
    }
};

// ============================================================================
// Forward declarations for GTK callbacks
// ============================================================================

static void on_back_clicked(GtkButton* button, gpointer user_data);
static void on_forward_clicked(GtkButton* button, gpointer user_data);
static void on_reload_clicked(GtkButton* button, gpointer user_data);
static void on_stop_clicked(GtkButton* button, gpointer user_data);
static void on_home_clicked(GtkButton* button, gpointer user_data);
static void on_go_clicked(GtkButton* button, gpointer user_data);
static void on_agent_clicked(GtkButton* button, gpointer user_data);
static void on_new_tab_clicked(GtkButton* button, gpointer user_data);
static void on_proxy_clicked(GtkButton* button, gpointer user_data);
static gboolean on_address_bar_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data);
static gboolean on_address_bar_activate(GtkEntry* entry, gpointer user_data);

// ============================================================================
// OwlUIToolbar Implementation
// ============================================================================

OwlUIToolbar::OwlUIToolbar()
    : toolbar_view_(nullptr),
      back_button_(nullptr),
      forward_button_(nullptr),
      reload_button_(nullptr),
      stop_button_(nullptr),
      home_button_(nullptr),
      address_bar_(nullptr),
      go_button_(nullptr),
      agent_button_(nullptr),
      proxy_button_(nullptr),
      loading_indicator_(nullptr),
      tld_autocomplete_helper_(nullptr),
      agent_mode_active_(false),
      is_loading_(false),
      proxy_connected_(false) {
    LOG_DEBUG("UIToolbar", "Toolbar initialized");
}

OwlUIToolbar::~OwlUIToolbar() {
    if (tld_autocomplete_helper_) {
        delete static_cast<TLDAutocompleteHelper*>(tld_autocomplete_helper_);
    }
    // GTK widgets are automatically destroyed when their parent is destroyed
    LOG_DEBUG("UIToolbar", "Toolbar destroyed");
}

void* OwlUIToolbar::CreateToolbarView(int width, int height) {
    LOG_DEBUG("UIToolbar", "Creating toolbar view: " + std::to_string(width) + "x" + std::to_string(height));

    // Create main toolbar container (horizontal box)
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_size_request(toolbar, width, height);

    // Apply CSS styling
    GtkCssProvider* css_provider = gtk_css_provider_new();
    const char* css = R"(
        .toolbar {
            background-color: white;
            padding: 8px 16px;
            border-bottom: 1px solid #d9d9d9;
        }
        .toolbar-button {
            min-width: 36px;
            min-height: 36px;
            border-radius: 8px;
            border: none;
            background: transparent;
            padding: 8px;
        }
        .toolbar-button:hover {
            background-color: rgba(0, 0, 0, 0.05);
        }
        .toolbar-button:active {
            background-color: rgba(0, 0, 0, 0.08);
        }
        .toolbar-button:disabled {
            opacity: 0.4;
        }
        .address-container {
            background-color: #f5f5f5;
            border: 1px solid #d1d1d1;
            border-radius: 10px;
            padding: 0 8px;
        }
        .address-container:focus-within {
            border-color: #007acc;
            box-shadow: 0 0 0 2px rgba(0, 122, 204, 0.2);
        }
        .address-bar {
            border: none;
            background: transparent;
            font-size: 14px;
            padding: 8px 4px;
        }
        .address-bar:focus {
            outline: none;
            box-shadow: none;
        }
        .go-button {
            background-color: #2d2d2d;
            color: white;
            border-radius: 6px;
            padding: 4px 16px;
            font-weight: 500;
            font-size: 12px;
            min-height: 26px;
            border: none;
        }
        .go-button:hover {
            background-color: #1a1a1a;
        }
        .go-button.loading {
            background-color: #cc3333;
        }
        .agent-button {
            background-color: #2d2d2d;
            color: white;
            border-radius: 10px;
            padding: 8px 16px;
            font-weight: 600;
            font-size: 13px;
            border: none;
            box-shadow: 0 1px 3px rgba(0, 0, 0, 0.15);
        }
        .agent-button:hover {
            background-color: #1a1a1a;
        }
        .agent-button.active {
            background-color: #1976d2;
        }
        .proxy-button {
            min-width: 36px;
            min-height: 36px;
            border-radius: 8px;
            border: none;
            background: transparent;
            padding: 8px;
        }
        .proxy-button.connected {
            color: #4caf50;
        }
    )";

    gtk_css_provider_load_from_data(css_provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css_provider);

    GtkStyleContext* toolbar_context = gtk_widget_get_style_context(toolbar);
    gtk_style_context_add_class(toolbar_context, "toolbar");

    // Back button
    back_button_ = CreateIconButton(OlibIcons::ANGLE_LEFT, "Go Back (Ctrl+[)");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(back_button_)), "toolbar-button");
    g_signal_connect(back_button_, "clicked", G_CALLBACK(on_back_clicked), this);
    gtk_widget_set_sensitive(GTK_WIDGET(back_button_), FALSE);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(back_button_), FALSE, FALSE, 0);

    // Forward button
    forward_button_ = CreateIconButton(OlibIcons::ANGLE_RIGHT, "Go Forward (Ctrl+])");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(forward_button_)), "toolbar-button");
    g_signal_connect(forward_button_, "clicked", G_CALLBACK(on_forward_clicked), this);
    gtk_widget_set_sensitive(GTK_WIDGET(forward_button_), FALSE);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(forward_button_), FALSE, FALSE, 0);

    // Reload button
    reload_button_ = CreateIconButton(OlibIcons::ARROWS_ROTATE, "Reload (Ctrl+R)");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(reload_button_)), "toolbar-button");
    g_signal_connect(reload_button_, "clicked", G_CALLBACK(on_reload_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(reload_button_), FALSE, FALSE, 0);

    // Stop button (same position as reload, hidden by default)
    stop_button_ = CreateIconButton(OlibIcons::XMARK, "Stop Loading");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(stop_button_)), "toolbar-button");
    g_signal_connect(stop_button_, "clicked", G_CALLBACK(on_stop_clicked), this);
    gtk_widget_set_no_show_all(GTK_WIDGET(stop_button_), TRUE);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(stop_button_), FALSE, FALSE, 0);

    // Home button
    home_button_ = CreateIconButton(OlibIcons::HOME, "Home");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(home_button_)), "toolbar-button");
    g_signal_connect(home_button_, "clicked", G_CALLBACK(on_home_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(home_button_), FALSE, FALSE, 0);

    // Spacer
    GtkWidget* spacer1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer1, 8, 1);
    gtk_box_pack_start(GTK_BOX(toolbar), spacer1, FALSE, FALSE, 0);

    // Address bar container
    GtkWidget* address_container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(address_container), "address-container");

    // Search icon / Loading indicator
    loading_indicator_ = gtk_image_new();
    GdkPixbuf* search_pixbuf = CreatePixbufFromSVG(OlibIcons::SEARCH, 16, 16);
    if (search_pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(loading_indicator_), search_pixbuf);
        g_object_unref(search_pixbuf);
    }
    gtk_widget_set_margin_start(GTK_WIDGET(loading_indicator_), 8);
    gtk_box_pack_start(GTK_BOX(address_container), GTK_WIDGET(loading_indicator_), FALSE, FALSE, 0);

    // Address bar entry
    address_bar_ = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(address_bar_), "Search or enter address");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(address_bar_)), "address-bar");
    gtk_widget_set_hexpand(GTK_WIDGET(address_bar_), TRUE);
    g_signal_connect(address_bar_, "key-press-event", G_CALLBACK(on_address_bar_key_press), this);
    g_signal_connect(address_bar_, "activate", G_CALLBACK(on_address_bar_activate), this);
    gtk_box_pack_start(GTK_BOX(address_container), GTK_WIDGET(address_bar_), TRUE, TRUE, 0);

    // Go button (inside address container)
    go_button_ = gtk_button_new_with_label("Go");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(go_button_)), "go-button");
    gtk_widget_set_margin_end(GTK_WIDGET(go_button_), 4);
    g_signal_connect(go_button_, "clicked", G_CALLBACK(on_go_clicked), this);
    gtk_box_pack_start(GTK_BOX(address_container), GTK_WIDGET(go_button_), FALSE, FALSE, 0);

    gtk_widget_set_hexpand(address_container, TRUE);
    gtk_box_pack_start(GTK_BOX(toolbar), address_container, TRUE, TRUE, 0);

    // Spacer
    GtkWidget* spacer2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer2, 8, 1);
    gtk_box_pack_start(GTK_BOX(toolbar), spacer2, FALSE, FALSE, 0);

    // New Tab button
    GtkWidget* new_tab_button = CreateIconButton(OlibIcons::PLUS, "New Tab (Ctrl+T)", 14);
    gtk_style_context_add_class(gtk_widget_get_style_context(new_tab_button), "toolbar-button");
    g_signal_connect(new_tab_button, "clicked", G_CALLBACK(on_new_tab_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), new_tab_button, FALSE, FALSE, 0);

    // Proxy button
    proxy_button_ = CreateIconButton(OlibIcons::SHIELD_BLANK, "Proxy Settings");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(proxy_button_)), "toolbar-button");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(proxy_button_)), "proxy-button");
    g_signal_connect(proxy_button_, "clicked", G_CALLBACK(on_proxy_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(proxy_button_), FALSE, FALSE, 0);

    // AI Agent button
    agent_button_ = gtk_button_new();
    GtkWidget* agent_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    // Agent icon
    GdkPixbuf* magic_pixbuf = CreatePixbufFromSVG(OlibIcons::MAGIC_WAND_SPARKLES, 16, 16);
    if (magic_pixbuf) {
        GtkWidget* agent_icon = gtk_image_new_from_pixbuf(magic_pixbuf);
        gtk_box_pack_start(GTK_BOX(agent_box), agent_icon, FALSE, FALSE, 0);
        g_object_unref(magic_pixbuf);
    }

    // Agent label
    GtkWidget* agent_label = gtk_label_new("AI Agent");
    gtk_box_pack_start(GTK_BOX(agent_box), agent_label, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(agent_button_), agent_box);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(agent_button_)), "agent-button");
    gtk_widget_set_tooltip_text(GTK_WIDGET(agent_button_), "Toggle AI Agent Mode");
    g_signal_connect(agent_button_, "clicked", G_CALLBACK(on_agent_clicked), this);
    gtk_box_pack_start(GTK_BOX(toolbar), GTK_WIDGET(agent_button_), FALSE, FALSE, 0);

    // Initialize TLD autocomplete helper
    tld_autocomplete_helper_ = new TLDAutocompleteHelper(GTK_WIDGET(address_bar_), this);

    toolbar_view_ = toolbar;

    LOG_DEBUG("UIToolbar", "Toolbar view created successfully");
    return toolbar_view_;
}

void OwlUIToolbar::SetBackCallback(ToolbarCallback callback) {
    back_callback_ = callback;
}

void OwlUIToolbar::SetForwardCallback(ToolbarCallback callback) {
    forward_callback_ = callback;
}

void OwlUIToolbar::SetReloadCallback(ToolbarCallback callback) {
    reload_callback_ = callback;
}

void OwlUIToolbar::SetHomeCallback(ToolbarCallback callback) {
    home_callback_ = callback;
}

void OwlUIToolbar::SetNavigateCallback(NavigateCallback callback) {
    navigate_callback_ = callback;
}

void OwlUIToolbar::SetAgentToggleCallback(ToolbarCallback callback) {
    agent_toggle_callback_ = callback;
}

void OwlUIToolbar::SetStopLoadingCallback(ToolbarCallback callback) {
    stop_loading_callback_ = callback;
}

void OwlUIToolbar::SetNewTabCallback(ToolbarCallback callback) {
    new_tab_callback_ = callback;
}

void OwlUIToolbar::SetProxyToggleCallback(ToolbarCallback callback) {
    proxy_toggle_callback_ = callback;
}

void OwlUIToolbar::UpdateNavigationButtons(bool can_go_back, bool can_go_forward) {
    if (back_button_) {
        gtk_widget_set_sensitive(GTK_WIDGET(back_button_), can_go_back);
    }
    if (forward_button_) {
        gtk_widget_set_sensitive(GTK_WIDGET(forward_button_), can_go_forward);
    }

    LOG_DEBUG("UIToolbar", "Navigation buttons updated: back=" + std::string(can_go_back ? "enabled" : "disabled") +
              ", forward=" + std::string(can_go_forward ? "enabled" : "disabled"));
}

void OwlUIToolbar::UpdateAddressBar(const std::string& url) {
    if (address_bar_) {
        gtk_entry_set_text(GTK_ENTRY(address_bar_), url.c_str());
        LOG_DEBUG("UIToolbar", "Address bar updated: " + url);
    }
}

void OwlUIToolbar::SetAgentModeActive(bool active) {
    agent_mode_active_ = active;

    if (agent_button_) {
        GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(agent_button_));

        // Find the label widget in the button
        GtkWidget* box = gtk_bin_get_child(GTK_BIN(agent_button_));
        if (GTK_IS_BOX(box)) {
            GList* children = gtk_container_get_children(GTK_CONTAINER(box));
            for (GList* l = children; l != NULL; l = l->next) {
                GtkWidget* child = GTK_WIDGET(l->data);
                if (GTK_IS_LABEL(child)) {
                    gtk_label_set_text(GTK_LABEL(child), active ? "AI Active" : "AI Agent");
                    break;
                }
            }
            g_list_free(children);
        }

        if (active) {
            gtk_style_context_add_class(context, "active");
            gtk_widget_set_tooltip_text(GTK_WIDGET(agent_button_), "AI Agent Mode Active (Click to disable)");
        } else {
            gtk_style_context_remove_class(context, "active");
            gtk_widget_set_tooltip_text(GTK_WIDGET(agent_button_), "Toggle AI Agent Mode");
        }

        LOG_DEBUG("UIToolbar", "Agent mode set to: " + std::string(active ? "active" : "inactive"));
    }
}

void OwlUIToolbar::SetLoadingState(bool is_loading) {
    is_loading_ = is_loading;

    if (is_loading) {
        // Show stop button, hide reload button
        if (stop_button_) {
            gtk_widget_show(GTK_WIDGET(stop_button_));
        }
        if (reload_button_) {
            gtk_widget_hide(GTK_WIDGET(reload_button_));
        }

        // Change Go button to "Stop"
        if (go_button_) {
            gtk_button_set_label(GTK_BUTTON(go_button_), "Stop");
            GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(go_button_));
            gtk_style_context_add_class(context, "loading");
        }

        // Change icon to hourglass
        if (loading_indicator_) {
            GdkPixbuf* hourglass_pixbuf = CreatePixbufFromSVG(OlibIcons::HOURGLASS, 16, 16);
            if (hourglass_pixbuf) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(loading_indicator_), hourglass_pixbuf);
                g_object_unref(hourglass_pixbuf);
            }
        }
    } else {
        // Hide stop button, show reload button
        if (stop_button_) {
            gtk_widget_hide(GTK_WIDGET(stop_button_));
        }
        if (reload_button_) {
            gtk_widget_show(GTK_WIDGET(reload_button_));
        }

        // Change Go button back to "Go"
        if (go_button_) {
            gtk_button_set_label(GTK_BUTTON(go_button_), "Go");
            GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(go_button_));
            gtk_style_context_remove_class(context, "loading");
        }

        // Change icon back to search
        if (loading_indicator_) {
            GdkPixbuf* search_pixbuf = CreatePixbufFromSVG(OlibIcons::SEARCH, 16, 16);
            if (search_pixbuf) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(loading_indicator_), search_pixbuf);
                g_object_unref(search_pixbuf);
            }
        }
    }

    LOG_DEBUG("UIToolbar", "Loading state set to: " + std::string(is_loading ? "loading" : "not loading"));
}

void OwlUIToolbar::SetProxyConnected(bool connected) {
    proxy_connected_ = connected;

    if (proxy_button_) {
        GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(proxy_button_));

        // Update icon
        const std::string& icon = connected ? OlibIcons::SHIELD : OlibIcons::SHIELD_BLANK;
        GdkPixbuf* pixbuf = CreatePixbufFromSVG(icon, 16, 16);
        if (pixbuf) {
            GtkWidget* image = gtk_button_get_image(GTK_BUTTON(proxy_button_));
            if (GTK_IS_IMAGE(image)) {
                gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
            }
            g_object_unref(pixbuf);
        }

        if (connected) {
            gtk_style_context_add_class(context, "connected");
            gtk_widget_set_tooltip_text(GTK_WIDGET(proxy_button_), "Proxy Connected - Click to Disconnect");
        } else {
            gtk_style_context_remove_class(context, "connected");
            gtk_widget_set_tooltip_text(GTK_WIDGET(proxy_button_), "Proxy Settings - Click to Connect");
        }

        LOG_DEBUG("UIToolbar", "Proxy connected set to: " + std::string(connected ? "true" : "false"));
    }
}

// ============================================================================
// GTK Callback Implementations
// ============================================================================

static void on_back_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "Back button clicked");
    toolbar->ExecuteBackCallback();
}

static void on_forward_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "Forward button clicked");
    toolbar->ExecuteForwardCallback();
}

static void on_reload_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "Reload button clicked");
    toolbar->ExecuteReloadCallback();
}

static void on_stop_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "Stop button clicked");
    toolbar->ExecuteStopLoadingCallback();
}

static void on_home_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "Home button clicked");
    toolbar->ExecuteHomeCallback();
}

static void on_go_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);

    // Check if we're in loading state (button says "Stop")
    const gchar* label = gtk_button_get_label(button);
    if (label && g_strcmp0(label, "Stop") == 0) {
        LOG_DEBUG("UIToolbar", "Stop button clicked (via Go button)");
        toolbar->ExecuteStopLoadingCallback();
        return;
    }

    // Get URL from address bar
    GtkWidget* toolbar_widget = static_cast<GtkWidget*>(toolbar->GetView());
    if (!toolbar_widget) return;

    // Find address bar in the toolbar
    void* address_bar_ptr = nullptr;

    // Traverse to find address bar
    GList* children = gtk_container_get_children(GTK_CONTAINER(toolbar_widget));
    for (GList* l = children; l != NULL; l = l->next) {
        GtkWidget* child = GTK_WIDGET(l->data);
        if (GTK_IS_BOX(child)) {
            // This might be the address container
            GList* sub_children = gtk_container_get_children(GTK_CONTAINER(child));
            for (GList* sl = sub_children; sl != NULL; sl = sl->next) {
                GtkWidget* sub_child = GTK_WIDGET(sl->data);
                if (GTK_IS_ENTRY(sub_child)) {
                    address_bar_ptr = sub_child;
                    break;
                }
            }
            g_list_free(sub_children);
        }
        if (address_bar_ptr) break;
    }
    g_list_free(children);

    if (address_bar_ptr) {
        const gchar* url = gtk_entry_get_text(GTK_ENTRY(address_bar_ptr));
        if (url && strlen(url) > 0) {
            std::string url_str(url);

            // Auto-add https:// if no protocol
            if (url_str.find("://") == std::string::npos) {
                url_str = "https://" + url_str;
                gtk_entry_set_text(GTK_ENTRY(address_bar_ptr), url_str.c_str());
            }

            LOG_DEBUG("UIToolbar", "Go button clicked: " + url_str);
            toolbar->ExecuteNavigateCallback(url_str);
        }
    }
}

static void on_agent_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "AI Agent button clicked");
    toolbar->ExecuteAgentToggleCallback();
}

static void on_new_tab_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "New Tab button clicked");
    toolbar->ExecuteNewTabCallback();
}

static void on_proxy_clicked(GtkButton* button, gpointer user_data) {
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    LOG_DEBUG("UIToolbar", "Proxy button clicked");
    toolbar->ExecuteProxyToggleCallback();
}

static gboolean on_address_bar_key_press(GtkWidget* widget, GdkEventKey* event, gpointer user_data) {
    // Handle Enter key press in address bar
    if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
        OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
        const gchar* url = gtk_entry_get_text(GTK_ENTRY(widget));
        if (url && strlen(url) > 0) {
            std::string url_str(url);

            // Auto-add https:// if no protocol
            if (url_str.find("://") == std::string::npos) {
                url_str = "https://" + url_str;
                gtk_entry_set_text(GTK_ENTRY(widget), url_str.c_str());
            }

            LOG_DEBUG("UIToolbar", "Address bar Enter: " + url_str);
            toolbar->ExecuteNavigateCallback(url_str);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean on_address_bar_activate(GtkEntry* entry, gpointer user_data) {
    // This is called when Enter is pressed
    OwlUIToolbar* toolbar = static_cast<OwlUIToolbar*>(user_data);
    const gchar* url = gtk_entry_get_text(entry);
    if (url && strlen(url) > 0) {
        std::string url_str(url);

        // Auto-add https:// if no protocol
        if (url_str.find("://") == std::string::npos) {
            url_str = "https://" + url_str;
            gtk_entry_set_text(entry, url_str.c_str());
        }

        toolbar->ExecuteNavigateCallback(url_str);
    }
    return FALSE;
}

#endif  // OS_LINUX
