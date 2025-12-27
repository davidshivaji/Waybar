#include "modules/hyprland/preview_window.hpp"
#include <spdlog/spdlog.h>
#include <gtkmm/main.h>
#include <gtkmm/drawingarea.h>
#include <glibmm/main.h>
#include <json/json.h>
#include <sstream>
#include <functional>
#include <cmath>
#include <ctime>
#include <chrono>
#include <cstdio>

namespace waybar::modules::hyprland {

// Initialize static members
std::map<int, WorkspaceScreenshot> PreviewWindow::s_screenshotCache;
sigc::connection PreviewWindow::s_periodicCacheTimer;
int PreviewWindow::s_currentActiveWorkspace = 1;
bool PreviewWindow::s_activeWorkspaceHasWindows = false;
bool PreviewWindow::s_inTransition = false;

PreviewWindow::PreviewWindow() 
    : m_contentBox(Gtk::ORIENTATION_VERTICAL, 10),
      m_thumbnailsBox(Gtk::ORIENTATION_HORIZONTAL, 5),
      m_isVisible(false),
      m_currentWorkspaceId(-1) {
  setupWindow();
}

PreviewWindow::~PreviewWindow() {
  hidePreview();
}

void PreviewWindow::cacheWorkspaceScreenshot(int workspace_id) {
  auto start_time = std::chrono::high_resolution_clock::now();
  
  std::string temp_path = "/tmp/waybar_workspace_" + std::to_string(workspace_id) + ".png";
  std::string temp_write_path = temp_path + ".tmp";
  std::string cmd = "grim " + temp_write_path + " 2>/dev/null && mv " + temp_write_path + " " + temp_path;
  
  spdlog::debug("[PreviewWindow] Taking screenshot for workspace {}", workspace_id);
  
  // Run synchronously - grim is fast enough
  int result = system(cmd.c_str());
  
  if (result == 0) {
    WorkspaceScreenshot screenshot;
    screenshot.workspace_id = workspace_id;
    screenshot.screenshot_path = temp_path;
    screenshot.timestamp = time(nullptr);
    
    try {
      screenshot.pixbuf = Gdk::Pixbuf::create_from_file(temp_path);
      s_screenshotCache[workspace_id] = screenshot;
      
      auto end_time = std::chrono::high_resolution_clock::now();
      auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
      
      spdlog::info("[PreviewWindow] Cached screenshot for workspace {} ({}x{}) in {}ms", 
                   workspace_id, screenshot.pixbuf->get_width(), screenshot.pixbuf->get_height(), duration.count());
    } catch (const Glib::Error& e) {
      spdlog::warn("[PreviewWindow] Failed to load screenshot: {}", e.what().c_str());
    }
  } else {
    spdlog::warn("[PreviewWindow] grim failed for workspace {}", workspace_id);
  }
}

void PreviewWindow::startPeriodicCaching() {
  // Cache current workspace more frequently for fresher previews
  // Only cache workspaces with windows (checked at hover time)
  s_periodicCacheTimer = Glib::signal_timeout().connect(
    []() -> bool {
      // Don't cache during transitions to avoid capturing mid-animation
      // Don't cache empty workspaces (they take 665ms vs 175ms for full ones)
      if (s_currentActiveWorkspace > 0 && !s_inTransition && s_activeWorkspaceHasWindows) {
        cacheWorkspaceScreenshot(s_currentActiveWorkspace);
      }
      return true;  // Keep timer running
    },
    500  // Every 500ms (0.5 seconds) - much more responsive
  );
  
  spdlog::info("[PreviewWindow] Started periodic workspace caching (every 500ms)");
}

void PreviewWindow::pausePeriodicCaching() {
  if (s_periodicCacheTimer.connected()) {
    s_periodicCacheTimer.disconnect();
    spdlog::debug("[PreviewWindow] Paused periodic caching (preview visible)");
  }
}

void PreviewWindow::resumePeriodicCaching() {
  if (!s_periodicCacheTimer.connected()) {
    startPeriodicCaching();
    spdlog::debug("[PreviewWindow] Resumed periodic caching");
  }
}

void PreviewWindow::updateAllWorkspaceCache() {
  // Update cache for current workspace immediately
  if (s_currentActiveWorkspace > 0) {
    cacheWorkspaceScreenshot(s_currentActiveWorkspace);
  }
}

void PreviewWindow::onWorkspaceTransitionStart() {
  s_inTransition = true;
  spdlog::debug("[PreviewWindow] Workspace transition started - pausing screenshots");
}

void PreviewWindow::onWorkspaceTransitionEnd() {
  // Small delay to ensure animation is complete
  Glib::signal_timeout().connect_once(
    []() {
      s_inTransition = false;
      spdlog::debug("[PreviewWindow] Workspace transition ended - resuming screenshots");
    },
    200  // 200ms delay after transition event
  );
}

void PreviewWindow::setupWindow() {
  // Initialize GTK Layer Shell for this window
  gtk_layer_init_for_window(m_window.gobj());
  
  // Set as overlay layer (above normal windows, below notifications typically)
  gtk_layer_set_layer(m_window.gobj(), GTK_LAYER_SHELL_LAYER_OVERLAY);
  
  // Don't take keyboard focus
  gtk_layer_set_keyboard_mode(m_window.gobj(), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  
  // Configure window properties
  m_window.set_decorated(false);
  m_window.set_resizable(false);
  
  // Setup content box
  m_contentBox.set_margin_top(10);
  m_contentBox.set_margin_bottom(10);
  m_contentBox.set_margin_start(10);
  m_contentBox.set_margin_end(10);
  
  // Add title label
  m_titleLabel.set_text("Workspace Preview");
  m_titleLabel.get_style_context()->add_class("preview-title");
  m_contentBox.pack_start(m_titleLabel, false, false);
  
  // Add thumbnails box
  m_thumbnailsBox.set_homogeneous(true);
  m_thumbnailsBox.get_style_context()->add_class("preview-thumbnails");
  m_contentBox.pack_start(m_thumbnailsBox, true, true);
  
  m_window.add(m_contentBox);
  
  // Add CSS class for styling
  m_window.get_style_context()->add_class("workspace-preview");
  
  // Add default styling to make window visible
  auto css_provider = Gtk::CssProvider::create();
  css_provider->load_from_data(
    ".workspace-preview { "
    "  background-color: rgba(30, 30, 30, 0.95); "
    "  border-radius: 12px; "
    "  border: 2px solid rgba(80, 80, 80, 0.8); "
    "} "
    ".preview-title { "
    "  color: #ffffff; "
    "  font-weight: bold; "
    "  font-size: 14px; "
    "}"
  );
  m_window.get_style_context()->add_provider(css_provider, 
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  
  // Set a default size for the preview window
  m_window.set_default_size(400, 250);
  
  spdlog::debug("[PreviewWindow] Window setup complete with GTK Layer Shell");
}

void PreviewWindow::showPreview(int workspace_id, const std::string& workspace_name) {
  if (m_isVisible && m_currentWorkspaceId == workspace_id) {
    spdlog::trace("[PreviewWindow] Preview already shown for workspace {}", workspace_id);
    return;
  }
  
  // Force hide first to ensure clean state
  if (m_isVisible) {
    m_window.hide();
    m_isVisible = false;
  }
  
  spdlog::debug("[PreviewWindow] Showing preview for workspace {} ({})", workspace_id, workspace_name);
  
  bool wasVisible = m_isVisible;
  m_currentWorkspaceId = workspace_id;
  m_titleLabel.set_text("Workspace: " + workspace_name);
  
  // Clear existing thumbnails efficiently
  auto children = m_thumbnailsBox.get_children();
  for (auto child : children) {
    m_thumbnailsBox.remove(*child);
  }
  
  // Try to show cached screenshot first
  if (s_screenshotCache.find(workspace_id) != s_screenshotCache.end()) {
    spdlog::debug("[PreviewWindow] Found cached screenshot for workspace {}", workspace_id);
    showCachedScreenshot(workspace_id);
  } else {
    spdlog::debug("[PreviewWindow] No cached screenshot for workspace {}, showing placeholder", workspace_id);
    // Show placeholder immediately without querying hyprctl (which can be slow)
    createPlaceholderContent(workspace_id, workspace_name);
  }
  
  // Only show_all if window wasn't already visible (avoids re-rendering)
  if (!wasVisible) {
    m_window.show_all();
    // Pause periodic caching while preview is visible
    pausePeriodicCaching();
  } else {
    // Just show new children without full window re-render
    m_thumbnailsBox.show_all();
  }
  
  m_isVisible = true;
  
  spdlog::debug("[PreviewWindow] Preview window shown at workspace {}", workspace_id);
}

void PreviewWindow::showCachedScreenshot(int workspace_id) {
  auto& cached = s_screenshotCache[workspace_id];
  
  if (!cached.pixbuf) {
    spdlog::warn("[PreviewWindow] Cached screenshot has no pixbuf");
    return;
  }
  
  // Scale screenshot to fit preview window
  int original_width = cached.pixbuf->get_width();
  int original_height = cached.pixbuf->get_height();
  
  // Target size for preview (keep aspect ratio)
  int target_width = 360;
  int target_height = 200;
  
  double scale = std::min(
    static_cast<double>(target_width) / original_width,
    static_cast<double>(target_height) / original_height
  );
  
  int scaled_width = static_cast<int>(original_width * scale);
  int scaled_height = static_cast<int>(original_height * scale);
  
  auto scaled_pixbuf = cached.pixbuf->scale_simple(
    scaled_width, scaled_height, Gdk::INTERP_BILINEAR
  );
  
  auto image = Gtk::manage(new Gtk::Image(scaled_pixbuf));
  m_thumbnailsBox.pack_start(*image, true, true);
  
  spdlog::debug("[PreviewWindow] Showing cached screenshot for workspace {}", workspace_id);
}

void PreviewWindow::hidePreview() {
  if (!m_isVisible) {
    return;
  }
  
  spdlog::debug("[PreviewWindow] Hiding preview for workspace {}", m_currentWorkspaceId);
  
  m_window.hide();
  m_isVisible = false;
  m_currentWorkspaceId = -1;
  
  // Resume periodic caching after preview is hidden
  resumePeriodicCaching();
}

void PreviewWindow::positionNear(Gtk::Widget& reference_widget) {
  // Get the reference widget's position and size
  int ref_x, ref_y, ref_width, ref_height;
  auto ref_window = reference_widget.get_window();
  
  if (!ref_window) {
    spdlog::warn("[PreviewWindow] Cannot position: reference widget has no window");
    return;
  }
  
  ref_window->get_origin(ref_x, ref_y);
  ref_width = reference_widget.get_allocated_width();
  ref_height = reference_widget.get_allocated_height();
  
  spdlog::debug("[PreviewWindow] Reference widget at ({}, {}) size {}x{}", 
                ref_x, ref_y, ref_width, ref_height);
  
  // With GTK Layer Shell, we set margins from edges
  // Position below the workspace button with appropriate margins
  int margin_left = ref_x;
  int margin_top = ref_y + ref_height + 5;  // 5px gap below button
  
  // Set margins to position the window
  gtk_layer_set_margin(m_window.gobj(), GTK_LAYER_SHELL_EDGE_LEFT, margin_left);
  gtk_layer_set_margin(m_window.gobj(), GTK_LAYER_SHELL_EDGE_TOP, margin_top);
  
  // Anchor to top-left corner
  gtk_layer_set_anchor(m_window.gobj(), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(m_window.gobj(), GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(m_window.gobj(), GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
  gtk_layer_set_anchor(m_window.gobj(), GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
  
  spdlog::info("[PreviewWindow] Positioned with margins: left={}, top={}", 
               margin_left, margin_top);
}

void PreviewWindow::updateThumbnails(const std::vector<WindowThumbnail>& thumbnails) {
  m_thumbnails = thumbnails;
  
  // Clear existing thumbnails
  auto children = m_thumbnailsBox.get_children();
  for (auto child : children) {
    m_thumbnailsBox.remove(*child);
  }
  
  // Add new thumbnails
  for (const auto& thumbnail : thumbnails) {
    auto image = Gtk::manage(new Gtk::Image(thumbnail.pixbuf));
    image->set_tooltip_text(thumbnail.title);
    image->get_style_context()->add_class("window-thumbnail");
    m_thumbnailsBox.pack_start(*image, true, true);
  }
  
  m_window.show_all();
}

void PreviewWindow::createPlaceholderContent(int workspace_id, const std::string& workspace_name) {
  // Create a visible placeholder with styling
  auto placeholder = Gtk::manage(new Gtk::Label());
  placeholder->set_markup("<big><b>Workspace " + std::to_string(workspace_id) + 
                          "</b></big>\n\n<i>" + workspace_name + "</i>\n\n(Preview Coming Soon)");
  placeholder->set_justify(Gtk::JUSTIFY_CENTER);
  placeholder->set_size_request(350, 200);
  placeholder->get_style_context()->add_class("preview-placeholder");
  
  // Make sure it's visible with some default styling
  auto css_provider = Gtk::CssProvider::create();
  css_provider->load_from_data(
    ".preview-placeholder { "
    "  background-color: rgba(40, 40, 40, 0.95); "
    "  color: #ffffff; "
    "  padding: 20px; "
    "  border-radius: 10px; "
    "  border: 2px solid rgba(100, 100, 100, 0.8); "
    "}"
  );
  placeholder->get_style_context()->add_provider(css_provider, 
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  
  m_thumbnailsBox.pack_start(*placeholder, true, true);
  
  spdlog::info("[PreviewWindow] Created visible placeholder for workspace {}", workspace_id);
}

void PreviewWindow::captureWorkspaceThumbnails(int workspace_id) {
  // Query Hyprland for windows in this workspace
  try {
    std::string cmd = "hyprctl clients -j";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      spdlog::warn("[PreviewWindow] Failed to execute hyprctl command");
      return;
    }
    
    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      result += buffer;
    }
    pclose(pipe);
    
    // Parse JSON response
    Json::Value clients;
    Json::CharReaderBuilder builder;
    std::string errs;
    std::istringstream stream(result);
    
    if (!Json::parseFromStream(builder, stream, &clients, &errs)) {
      spdlog::warn("[PreviewWindow] Failed to parse hyprctl output: {}", errs);
      return;
    }
    
    spdlog::info("[PreviewWindow] Looking for windows in workspace {}", workspace_id);
    
    // Filter windows by workspace
    int window_count = 0;
    for (const auto& client : clients) {
      int client_workspace = client["workspace"]["id"].asInt();
      std::string client_title = client["title"].asString();
      
      spdlog::debug("[PreviewWindow] Found window: '{}' on workspace {}", 
                    client_title, client_workspace);
      
      if (client_workspace == workspace_id) {
        spdlog::info("[PreviewWindow] Match! Creating thumbnail for: '{}'", client_title);
        createWindowThumbnail(client);
        window_count++;
        if (window_count >= 4) break; // Limit to 4 windows for now
      }
    }
    
    spdlog::info("[PreviewWindow] Created {} window thumbnails for workspace {}", 
                 window_count, workspace_id);
    
  } catch (const std::exception& e) {
    spdlog::error("[PreviewWindow] Exception capturing thumbnails: {}", e.what());
  }
}

void PreviewWindow::createWindowThumbnail(const Json::Value& window_data) {
  // Get window information
  std::string title = window_data["title"].asString();
  std::string class_name = window_data["class"].asString();
  std::string address = window_data["address"].asString();
  int workspace_id = window_data["workspace"]["id"].asInt();
  
  if (title.length() > 20) {
    title = title.substr(0, 20) + "...";
  }
  
  // Create thumbnail container
  auto thumbnail_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 5));
  thumbnail_box->set_size_request(150, 100);
  
  // For now, always use styled representation since we can't screenshot non-visible windows
  // In the future, we could implement workspace switching + capture + switch back
  createStyledThumbnail(thumbnail_box, title, class_name);
  
  m_thumbnailsBox.pack_start(*thumbnail_box, true, true, 5);
  
  spdlog::debug("[PreviewWindow] Created thumbnail for: {} ({}) on workspace {}", 
                title, class_name, workspace_id);
}

void PreviewWindow::createStyledThumbnail(Gtk::Box* container, const std::string& title,
                                          const std::string& class_name) {
  // Create a styled representation of the window
  auto drawing_area = Gtk::manage(new Gtk::DrawingArea());
  drawing_area->set_size_request(140, 80);
  
  drawing_area->signal_draw().connect([class_name, title](const Cairo::RefPtr<Cairo::Context>& cr) {
    // Create a unique color based on window class
    std::hash<std::string> hasher;
    size_t hash = hasher(class_name);
    double r = ((hash & 0xFF0000) >> 16) / 255.0 * 0.5 + 0.3;
    double g = ((hash & 0x00FF00) >> 8) / 255.0 * 0.5 + 0.3;
    double b = (hash & 0x0000FF) / 255.0 * 0.5 + 0.3;
    
    // Draw rounded rectangle with gradient
    cr->set_source_rgba(r, g, b, 0.9);
    double radius = 8.0;
    double x = 5, y = 5, width = 130, height = 70;
    
    cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->arc(x + width - radius, y + radius, radius, 3 * M_PI / 2, 0);
    cr->arc(x + width - radius, y + height - radius, radius, 0, M_PI / 2);
    cr->arc(x + radius, y + height - radius, radius, M_PI / 2, M_PI);
    cr->close_path();
    cr->fill();
    
    // Draw window "title bar"
    cr->set_source_rgba(r * 0.7, g * 0.7, b * 0.7, 0.95);
    cr->rectangle(x, y, width, 15);
    cr->fill();
    
    // Draw border
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.4);
    cr->set_line_width(2);
    cr->arc(x + radius, y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->arc(x + width - radius, y + radius, radius, 3 * M_PI / 2, 0);
    cr->arc(x + width - radius, y + height - radius, radius, 0, M_PI / 2);
    cr->arc(x + radius, y + height - radius, radius, M_PI / 2, M_PI);
    cr->close_path();
    cr->stroke();
    
    // Draw app name in center
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.9);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL, Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(12);
    
    Cairo::TextExtents extents;
    cr->get_text_extents(class_name, extents);
    cr->move_to(x + (width - extents.width) / 2, y + (height + extents.height) / 2);
    cr->show_text(class_name);
    
    return true;
  });
  
  // Add title label
  auto label = Gtk::manage(new Gtk::Label(title));
  label->set_ellipsize(Pango::ELLIPSIZE_END);
  label->get_style_context()->add_class("window-title");
  
  auto css_provider = Gtk::CssProvider::create();
  css_provider->load_from_data(
    ".window-title { color: #ffffff; font-size: 11px; }"
  );
  label->get_style_context()->add_provider(css_provider, 
                                           GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  
  container->pack_start(*drawing_area, true, true);
  container->pack_start(*label, false, false);
}

}  // namespace waybar::modules::hyprland
