# Drag and Drop Workspace Reordering Feature

## Overview
This feature adds the ability to reorder Hyprland workspaces in Waybar by dragging and dropping them.

## What Was Implemented

### 1. Core Functionality
- **Drag Source**: Each workspace button can be dragged
- **Drop Target**: Each workspace button can receive drops
- **Visual Feedback**: Hover state (green highlight with `drag-hover` class)
- **Reordering Logic**: Workspace order is updated both in the internal vector and the UI

### 2. Files Modified

#### `/include/modules/hyprland/workspaces.hpp`
- Added drag and drop method declarations:
  - `setupWorkspaceDragAndDrop()` - Sets up DND for each workspace
  - `onDragDataGet()` - Handles drag initiation
  - `onDragDataReceived()` - Handles drop completion
  - `onDragMotion()` - Handles drag hover
  - `onDragLeave()` - Handles drag leave
  - `reorderWorkspace()` - Performs the actual reordering
- Added member variables:
  - `m_draggedWorkspace` - Tracks currently dragged workspace
  - `m_enableDragReorder` - Configuration option to enable/disable

#### `/src/modules/hyprland/workspaces.cpp`
- Implemented all drag and drop methods
- Integrated DND setup in `createWorkspace()` method
- Added `enable-drag-reorder` config option parsing in `parseConfig()`
- Used GTK's drag and drop API with custom target type `WAYBAR_WORKSPACE`

#### `/resources/style.css`
- Added `.drag-hover` CSS class for visual feedback:
  ```css
  #workspaces button.drag-hover {
      background-color: #5b9c5f;
      box-shadow: inset 0 -3px #8be38c;
  }
  ```

#### `/man/waybar-hyprland-workspaces.5.scd`
- Added documentation for the new `enable-drag-reorder` configuration option

## Configuration

### Enable/Disable
Add to your Waybar config:

```json
"hyprland/workspaces": {
    "enable-drag-reorder": true  // Default is true
}
```

### Styling
Customize the drag hover appearance in your CSS:

```css
#workspaces button.drag-hover {
    background-color: #your-color;
    box-shadow: inset 0 -3px #your-highlight;
}
```

## How It Works

1. **User drags a workspace**: The `onDragDataGet()` method is called, storing the source workspace
2. **User hovers over target**: The `onDragMotion()` method adds the `drag-hover` class
3. **User drops**: The `onDragDataReceived()` method calls `reorderWorkspace()`
4. **Reordering**: 
   - The workspace is moved in the `m_workspaces` vector using `std::rotate`
   - The UI is updated by calling `m_box.reorder_child()` for each workspace
   - The visual order persists until workspaces are recreated

## Important Notes

- **Visual Only**: This changes the order in Waybar's UI, not the actual workspace IDs in Hyprland
- **Persistence**: Order persists during the Waybar session but resets when workspaces are recreated
- **Thread Safety**: Uses `std::lock_guard` to protect the workspace vector during reordering
- **GTK DND**: Uses GTK's native drag and drop with `Gdk::ACTION_MOVE`

## Testing

1. Build Waybar: `meson compile -C build`
2. Restart Waybar: `killall waybar && hyprctl dispatch exec "/path/to/build/waybar"`
3. Drag and drop workspaces to reorder them
4. Observe the green hover state and new ordering

## Future Enhancements

Possible improvements:
- Persist order across Waybar restarts (save to config file)
- Actually renumber workspaces in Hyprland (if desired)
- Animate the reordering transition
- Add keybindings for reordering without mouse
