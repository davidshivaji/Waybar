#pragma once

#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/cssprovider.h>
#include <json/value.h>
#include <gtk-layer-shell.h>

#include <memory>
#include <string>
#include <vector>
#include <map>

namespace waybar::modules::hyprland {

struct WorkspaceScreenshot {
  int workspace_id;
  std::string screenshot_path;
  Glib::RefPtr<Gdk::Pixbuf> pixbuf;
  time_t timestamp;
};

struct WindowThumbnail {
  std::string address;
  std::string title;
  std::string class_name;
  Glib::RefPtr<Gdk::Pixbuf> pixbuf;
  int x, y, width, height;  // Window geometry
};

class PreviewWindow {
 public:
  PreviewWindow();
  ~PreviewWindow();

  // Cache screenshot for a workspace (call when leaving workspace)
  static void cacheWorkspaceScreenshot(int workspace_id);
  
  // Start periodic background caching for all workspaces
  static void startPeriodicCaching();
  static void pausePeriodicCaching();
  static void resumePeriodicCaching();
  static void updateAllWorkspaceCache();
  
  // Handle workspace transitions
  static void onWorkspaceTransitionStart();
  static void onWorkspaceTransitionEnd();
  
  // Track current active workspace
  static int s_currentActiveWorkspace;
  static bool s_inTransition;
  
  // Show preview for a workspace
  void showPreview(int workspace_id, const std::string& workspace_name);
  
  // Hide the preview window
  void hidePreview();
  
  // Position the preview window relative to a widget
  void positionNear(Gtk::Widget& reference_widget);
  
  // Update preview content with window thumbnails
  void updateThumbnails(const std::vector<WindowThumbnail>& thumbnails);
  
  // Check if preview is currently visible
  bool isVisible() const { return m_isVisible; }

 private:
  void setupWindow();
  void createPlaceholderContent(int workspace_id, const std::string& workspace_name);
  void captureWorkspaceThumbnails(int workspace_id);
  void createWindowThumbnail(const Json::Value& window_data);
  void createStyledThumbnail(Gtk::Box* container, const std::string& title,
                             const std::string& class_name);
  void showCachedScreenshot(int workspace_id);
  
  Gtk::Window m_window;
  Gtk::Box m_contentBox;
  Gtk::Label m_titleLabel;
  Gtk::Box m_thumbnailsBox;
  
  bool m_isVisible;
  int m_currentWorkspaceId;
  std::vector<WindowThumbnail> m_thumbnails;
  
  // Static cache shared across all preview instances
  static std::map<int, WorkspaceScreenshot> s_screenshotCache;
  static sigc::connection s_periodicCacheTimer;
};

}  // namespace waybar::modules::hyprland
