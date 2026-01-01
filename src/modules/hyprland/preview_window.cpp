#include "modules/hyprland/preview_window.hpp"
#include <spdlog/spdlog.h>
#include <gtkmm/main.h>
#include <gtkmm/drawingarea.h>
#include <glibmm/main.h>
#include <gdkmm/general.h>
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
  std::string temp_path = "/tmp/waybar_workspace_" + std::to_string(workspace_id) + ".png";
  std::string temp_write_path = temp_path + ".tmp";
  std::string cmd = "grim " + temp_write_path + " 2>/dev/null && mv " + temp_write_path + " " + temp_path + " &";
  
  spdlog::debug("[PreviewWindow] Taking screenshot for workspace {}", workspace_id);
  
  // Run in background (non-blocking)
  system(cmd.c_str());
  
  // Schedule loading the screenshot after a short delay
  Glib::signal_timeout().connect_once([workspace_id, temp_path]() {
    try {
      auto pixbuf = Gdk::Pixbuf::create_from_file(temp_path);
      
      WorkspaceScreenshot screenshot;
      screenshot.workspace_id = workspace_id;
      screenshot.screenshot_path = temp_path;
      screenshot.timestamp = time(nullptr);
      screenshot.pixbuf = pixbuf;
      
      s_screenshotCache[workspace_id] = screenshot;
      
      spdlog::debug("[PreviewWindow] Cached screenshot for workspace {} ({}x{})", 
                   workspace_id, pixbuf->get_width(), pixbuf->get_height());
    } catch (const Glib::Error& e) {
      spdlog::debug("[PreviewWindow] Screenshot not ready yet for workspace {}", workspace_id);
    }
  }, 200);  // Wait 200ms for grim to finish
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
  
  // Enable transparency for proper rounded corners (GTK3 approach)
  auto screen = m_window.get_screen();
  auto visual = screen->get_rgba_visual();
  if (visual) {
    gtk_widget_set_visual(GTK_WIDGET(m_window.gobj()), visual->gobj());
  }
  m_window.set_app_paintable(true);
  
  // Connect draw signal to manually paint with shadow, background, and border
  m_window.signal_draw().connect([this](const Cairo::RefPtr<Cairo::Context>& cr) {
    auto allocation = m_window.get_allocation();
    double width = allocation.get_width();
    double height = allocation.get_height();
    double radius = 6.0;
    double border_width = 2.0;
    
    // Shadow space - content is inset by this amount
    double shadow_space = 3.0;
    
    // Clear the surface
    cr->save();
    cr->set_operator(Cairo::OPERATOR_CLEAR);
    cr->paint();
    cr->restore();
    
    cr->set_operator(Cairo::OPERATOR_OVER);
    
    // Content area (inset by shadow space)
    double content_x = shadow_space;
    double content_y = shadow_space;
    double content_width = width - (shadow_space * 2);
    double content_height = height - (shadow_space * 2);
    
    // Draw shadow using Gaussian-like blur
    cr->save();
    for (double i = 0; i < shadow_space; i += 0.3) {
      double progress = i / shadow_space;
      double alpha = 0.1 * (1.0 - progress * progress);  // Halved opacity
      cr->set_source_rgba(0, 0, 0, alpha);
      
      double offset = shadow_space - i;
      double x = content_x - offset;
      double y = content_y - offset;  // No downward offset - keep shadow uniform
      double w = content_width + (offset * 2);
      double h = content_height + (offset * 2);
      double r = radius + offset;
      
      // Draw rounded rect for this shadow layer
      cr->arc(x + r, y + r, r, M_PI, 3 * M_PI / 2);
      cr->arc(x + w - r, y + r, r, 3 * M_PI / 2, 2 * M_PI);
      cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
      cr->arc(x + r, y + h - r, r, M_PI / 2, M_PI);
      cr->close_path();
      cr->fill();
    }
    cr->restore();
    
    // Draw rounded rectangle background
    cr->save();
    cr->set_source_rgba(30.0/255.0, 30.0/255.0, 46.0/255.0, 0.95);
    
    cr->arc(content_x + radius, content_y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->arc(content_x + content_width - radius, content_y + radius, radius, 3 * M_PI / 2, 2 * M_PI);
    cr->arc(content_x + content_width - radius, content_y + content_height - radius, radius, 0, M_PI / 2);
    cr->arc(content_x + radius, content_y + content_height - radius, radius, M_PI / 2, M_PI);
    cr->close_path();
    
    cr->fill();
    cr->restore();
    
    // Draw border - make it more visible and on top
    cr->save();
    cr->set_source_rgb(137.0/255.0, 180.0/255.0, 250.0/255.0);
    cr->set_line_width(border_width);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
    cr->set_line_join(Cairo::LINE_JOIN_ROUND);
    
    // Draw border on the outer edge of the content area (no inset)
    cr->arc(content_x + radius, content_y + radius, radius, M_PI, 3 * M_PI / 2);
    cr->arc(content_x + content_width - radius, content_y + radius, radius, 3 * M_PI / 2, 2 * M_PI);
    cr->arc(content_x + content_width - radius, content_y + content_height - radius, radius, 0, M_PI / 2);
    cr->arc(content_x + radius, content_y + content_height - radius, radius, M_PI / 2, M_PI);
    cr->close_path();
    
    cr->stroke();
    cr->restore();
    
    return false;
  }, false);
  
  // Setup content box with minimal margins - just enough for shadow
  m_contentBox.set_margin_top(3);    // Just the shadow space
  m_contentBox.set_margin_bottom(3);
  m_contentBox.set_margin_start(3);
  m_contentBox.set_margin_end(3);
  
  // Add thumbnails box (no title label)
  m_thumbnailsBox.set_homogeneous(false);
  m_thumbnailsBox.get_style_context()->add_class("preview-thumbnails");
  m_contentBox.pack_start(m_thumbnailsBox, false, false);  // Don't expand
  
  m_window.add(m_contentBox);
  
  // Add CSS class for styling
  m_window.get_style_context()->add_class("workspace-preview");
  
  spdlog::debug("[PreviewWindow] Window setup complete with GTK Layer Shell");
}

void PreviewWindow::showPreview(int workspace_id, const std::string& workspace_name) {
  if (m_isVisible && m_currentWorkspaceId == workspace_id) {
    spdlog::trace("[PreviewWindow] Preview already shown for workspace {}", workspace_id);
    return;
  }
  
  // Only show preview if screenshot is already cached - avoids lag
  if (s_screenshotCache.find(workspace_id) == s_screenshotCache.end()) {
    spdlog::debug("[PreviewWindow] No cached screenshot for workspace {}, skipping preview", workspace_id);
    hidePreview();
    return;
  }
  
  spdlog::debug("[PreviewWindow] Showing preview for workspace {} ({})", workspace_id, workspace_name);
  
  bool wasVisible = m_isVisible;
  m_currentWorkspaceId = workspace_id;
  
  // Clear existing thumbnails efficiently
  auto children = m_thumbnailsBox.get_children();
  for (auto child : children) {
    m_thumbnailsBox.remove(*child);
  }
  
  // Show cached screenshot
  spdlog::debug("[PreviewWindow] Found cached screenshot for workspace {}", workspace_id);
  showCachedScreenshot(workspace_id);
  
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
  
  // Scale screenshot to a reasonable preview size
  int original_width = cached.pixbuf->get_width();
  int original_height = cached.pixbuf->get_height();
  
  // Target size for preview (keep aspect ratio) - 1.5x larger
  int target_width = 480;
  int target_height = 270;
  
  double scale = std::min(
    static_cast<double>(target_width) / original_width,
    static_cast<double>(target_height) / original_height
  );
  
  int scaled_width = static_cast<int>(original_width * scale);
  int scaled_height = static_cast<int>(original_height * scale);
  
  auto scaled_pixbuf = cached.pixbuf->scale_simple(
    scaled_width, scaled_height, Gdk::INTERP_BILINEAR
  );
  
  // Create a rounded version of the pixbuf
  auto rounded_pixbuf = Gdk::Pixbuf::create(
    Gdk::COLORSPACE_RGB, true, 8, scaled_width, scaled_height
  );
  
  // Use Cairo to draw the pixbuf with rounded corners
  auto surface = Cairo::ImageSurface::create(
    Cairo::FORMAT_ARGB32, scaled_width, scaled_height
  );
  auto cr = Cairo::Context::create(surface);
  
  // Clear with transparency
  cr->set_operator(Cairo::OPERATOR_CLEAR);
  cr->paint();
  cr->set_operator(Cairo::OPERATOR_OVER);
  
  // Create rounded rectangle clip path
  double radius = 4.0;  // Slightly smaller radius for the screenshot
  cr->arc(radius, radius, radius, M_PI, 3 * M_PI / 2);
  cr->arc(scaled_width - radius, radius, radius, 3 * M_PI / 2, 2 * M_PI);
  cr->arc(scaled_width - radius, scaled_height - radius, radius, 0, M_PI / 2);
  cr->arc(radius, scaled_height - radius, radius, M_PI / 2, M_PI);
  cr->close_path();
  cr->clip();
  
  // Draw the pixbuf
  Gdk::Cairo::set_source_pixbuf(cr, scaled_pixbuf, 0, 0);
  cr->paint();
  
  // Convert surface back to pixbuf
  unsigned char* data = surface->get_data();
  int stride = surface->get_stride();
  
  for (int y = 0; y < scaled_height; y++) {
    for (int x = 0; x < scaled_width; x++) {
      unsigned char* pixel = data + y * stride + x * 4;
      guchar* out_pixel = rounded_pixbuf->get_pixels() + y * rounded_pixbuf->get_rowstride() + x * 4;
      
      // Cairo uses BGRA, Pixbuf uses RGBA
      out_pixel[0] = pixel[2];  // R
      out_pixel[1] = pixel[1];  // G
      out_pixel[2] = pixel[0];  // B
      out_pixel[3] = pixel[3];  // A
    }
  }
  
  auto image = Gtk::manage(new Gtk::Image(rounded_pixbuf));
  m_thumbnailsBox.pack_start(*image, false, false);  // Don't expand
  
  // Resize window to fit screenshot + margins + border + shadow
  // Shadow extends 3px on all sides, plus 4px margin, plus 2px border
  int shadow_space = 6;  // 3px shadow on each side  
  int window_width = scaled_width + 12 + shadow_space;
  int window_height = scaled_height + 12 + shadow_space;
  m_window.resize(window_width, window_height);
  
  spdlog::debug("[PreviewWindow] Showing cached screenshot for workspace {} ({}x{} -> {}x{})", 
                workspace_id, original_width, original_height, scaled_width, scaled_height);
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
  // Position between waybar and first window with 20px left offset
  // Move up significantly to sit higher in the gap
  int margin_left = ref_x + 20;  // 20px left margin from button position
  int margin_top = ref_y + ref_height - 25;  // Move up 25px to sit much higher between waybar and windows
  
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
