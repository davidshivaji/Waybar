# Drag and Drop Reordering Branch

This branch (`drag-drop-reorder`) implements drag and drop functionality for reordering Hyprland workspaces in Waybar.

## Quick Start

### Building
```bash
cd /home/shiv/projects/waybar
meson compile -C build
```

### Running
```bash
killall waybar 2>/dev/null
sleep 0.5
hyprctl dispatch exec "/home/shiv/projects/waybar/build/waybar"
```

### Testing Drag and Drop
1. Open multiple workspaces in Hyprland
2. In Waybar, click and hold on a workspace button
3. Drag it over another workspace button
4. You'll see a green highlight on the hover target
5. Release to drop and reorder

## Configuration

Add to your Waybar config (`~/.config/waybar/config`):

```json
"hyprland/workspaces": {
    "enable-drag-reorder": true  // Enable drag and drop (default: true)
}
```

## Visual Customization

Edit your Waybar CSS (`~/.config/waybar/style.css`):

```css
#workspaces button.drag-hover {
    background-color: #5b9c5f;
    box-shadow: inset 0 -3px #8be38c;
}
```

## Features Implemented

- ✅ Drag workspace buttons to reorder
- ✅ Visual feedback on hover (green highlight)
- ✅ Smooth reordering with GTK's native DND
- ✅ Configuration option to enable/disable
- ✅ Thread-safe reordering
- ✅ Documentation in man page

## How It Works

The implementation uses GTK's drag-and-drop API:
- Each workspace button acts as both a drag source and drop target
- Custom target type `WAYBAR_WORKSPACE` ensures drops only work between workspaces
- Visual order is maintained in the `m_workspaces` vector
- UI updates via `Gtk::Box::reorder_child()`

## Important Notes

⚠️ **Visual Reordering Only**: This changes the display order in Waybar, NOT the actual workspace IDs in Hyprland. The workspace numbers remain the same in Hyprland.

📝 **Persistence**: The order persists during your Waybar session but will reset when:
- Waybar is restarted
- Workspaces are recreated by Hyprland events
- You reload your Hyprland config

## Files Changed

- `include/modules/hyprland/workspaces.hpp` - Added DND method declarations
- `src/modules/hyprland/workspaces.cpp` - Implemented DND functionality
- `resources/style.css` - Added drag-hover styling
- `man/waybar-hyprland-workspaces.5.scd` - Added documentation

## See Also

- [DRAG_DROP_FEATURE.md](./DRAG_DROP_FEATURE.md) - Detailed technical documentation
