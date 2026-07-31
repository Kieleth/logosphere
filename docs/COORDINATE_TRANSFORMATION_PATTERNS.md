# Mouse Coordinate Transformation Patterns

**Purpose**: Reference guide for correct mouse-to-world coordinate transformations.

## The Problem

Mouse coordinate transformation has **three distinct coordinate spaces** that must be correctly mapped:

1. **Window Coordinates** (from GLFW `glfwGetCursorPos()`)
   - Mouse position in window points
   - Example: 1600×1051 on standard display

2. **Framebuffer Coordinates** (DPI-scaled pixels)
   - Actual pixel buffer size
   - Example: 3200×2102 on 2× Retina display (window × DPI scale)

3. **Render Coordinates** (internal render resolution)
   - Resolution used for rendering calculations
   - Example: 1600×1051 (independent of window/framebuffer)

## Common Bug Patterns

### ❌ **Bug Pattern 1: Wrong coordinate space for screen_to_world()**

```cpp
// WRONG - Passes framebuffer coords to screen_to_world()
double mouse_x, mouse_y;
glfwGetCursorPos(window, &mouse_x, &mouse_y);

int fb_x, fb_y;
render_system.mouse_to_framebuffer(mouse_x, mouse_y, fb_x, fb_y);  // Scales by DPI
render_system.screen_to_world(fb_x, fb_y, world_x, world_y);       // Expects RENDER coords!
// Result: 2× scaling error (coordinates appear doubled/offset)
```

**Why this fails**: `mouse_to_framebuffer()` scales coordinates by DPI (e.g., ×2 on Retina), but `screen_to_world()` expects render coordinates (which typically match window size, not framebuffer).

### ❌ **Bug Pattern 2: Clamping to wrong bounds**

```cpp
// WRONG - Clamps to viewport bounds instead of framebuffer bounds
void mouse_to_framebuffer(double mouse_x, double mouse_y, int& fb_x, int& fb_y) {
    fb_x = static_cast<int>(mouse_x * dpi_scale_);  // Correct scaling
    fb_y = static_cast<int>(mouse_y * dpi_scale_);

    // BUG: Clamping to viewport (1600) instead of framebuffer (3200)!
    if (fb_x >= viewport_width_) fb_x = viewport_width_ - 1;
    if (fb_y >= viewport_height_) fb_y = viewport_height_ - 1;
}
// Result: Mouse coordinates beyond viewport bounds get clamped prematurely
```

**Why this fails**: After DPI scaling, coordinates are in framebuffer space (e.g., 0-3200), not viewport space (0-1600). Clamping to viewport bounds causes coordinates to stick at boundaries.

**Fixed version** (from `coordinate_transformer.cpp:220-228`):
```cpp
void mouse_to_framebuffer(double mouse_x, double mouse_y, int& fb_x, int& fb_y) {
    fb_x = static_cast<int>(mouse_x * dpi_scale_);
    fb_y = static_cast<int>(mouse_y * dpi_scale_);

    // CORRECT: Calculate framebuffer bounds and clamp to those
    int fb_width = static_cast<int>(viewport_width_ * dpi_scale_);
    int fb_height = static_cast<int>(viewport_height_ * dpi_scale_);

    if (fb_x >= fb_width) fb_x = fb_width - 1;
    if (fb_y >= fb_height) fb_y = fb_height - 1;
}
```

## Correct Transformation Patterns

### ✅ **Pattern 1: Mouse → Framebuffer → Object Query**

**Use case**: Querying object/particle at mouse position (KG inspector, picking)

```cpp
// Get mouse position in window coordinates
double mouse_x, mouse_y;
glfwGetCursorPos(window, &mouse_x, &mouse_y);

// Convert to framebuffer coordinates (DPI-scaled pixels)
int fb_x, fb_y;
render_system.mouse_to_framebuffer(mouse_x, mouse_y, fb_x, fb_y);

// Query object map at framebuffer coordinates
int object_id = render_system.get_object_at_pixel(fb_x, fb_y);
int particle_id = ObjectID::get_particle_id(object_id);
```

**Why this works**:
- Object map is stored at **render resolution** (1600×1051)
- `get_object_at_pixel()` internally converts framebuffer → render coordinates
- Encapsulation ensures correct mapping

**Example**: `ui_system.cpp:1765-1770` (KG inspector hover detection)

### ✅ **Pattern 2: Mouse → Render → World Coordinates**

**Use case**: Placing objects/lights at mouse cursor, world position queries

```cpp
// Get mouse position in window coordinates
double mouse_x, mouse_y;
glfwGetCursorPos(window, &mouse_x, &mouse_y);

// When window size == render size, mouse coords ARE render coords
// Just cast to int (no scaling needed)
int render_x = static_cast<int>(mouse_x);
int render_y = static_cast<int>(mouse_y);

// Convert render coordinates to world coordinates
float world_x, world_y;
render_system.screen_to_world(render_x, render_y, world_x, world_y);

// Use world coordinates for game logic
create_light(world_x, world_y, world_z);
```

**Why this works**:
- Window coordinates match render coordinates when window size == render size
- `screen_to_world()` expects **render coordinates**, not framebuffer
- No DPI scaling applied before world transformation

**Example**: `main_key_handler.cpp:139-150` (Key '6' light creation fix)

### ⚠️ **Pattern 3: Mouse → Render (General Case)**

**Use case**: When window size ≠ render size (e.g., dynamic resolution scaling)

```cpp
// Get mouse position in window coordinates
double mouse_x, mouse_y;
glfwGetCursorPos(window, &mouse_x, &mouse_y);

// Get window and render dimensions
int window_w = platform.get_window_width();
int window_h = platform.get_window_height();
int render_w = render_system.get_render_width();
int render_h = render_system.get_render_height();

// Scale from window space to render space
int render_x = static_cast<int>(mouse_x * render_w / window_w);
int render_y = static_cast<int>(mouse_y * render_h / window_h);

// Now safe to use with screen_to_world()
render_system.screen_to_world(render_x, render_y, world_x, world_y);
```

**When to use**:
- Dynamic resolution scaling (e.g., 720p render on 1080p window)
- Quality presets that change render resolution
- Not needed in Eden (window size == render size)

## Decision Tree: Which Pattern to Use?

```
Do you need pixel-perfect object queries (mouse picking)?
├─ YES → Use Pattern 1 (Mouse → Framebuffer → Object Query)
│         Example: KG inspector, UI element picking, particle selection
└─ NO  → Do you need world coordinates (gameplay logic)?
          └─ YES → Use Pattern 2 (Mouse → Render → World)
                   Example: Light placement, particle creation, camera movement
```

## Implementation Checklist

When implementing mouse-to-world transformations:

- [ ] Identify target coordinate space (framebuffer vs render vs world)
- [ ] Verify window size == render size (or implement Pattern 3 scaling)
- [ ] Use `glfwGetCursorPos()` for mouse position (never raw events)
- [ ] For object queries: `mouse_to_framebuffer()` → `get_object_at_pixel()`
- [ ] For world coords: cast to int → `screen_to_world()`
- [ ] Never pass framebuffer coords to `screen_to_world()`
- [ ] Add debug logging for first implementation (remove after verification)

## Debugging Mouse Coordinate Issues

### Symptoms of Wrong Coordinate Space

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| Objects created at 2× offset from cursor | Framebuffer coords passed to `screen_to_world()` | Use Pattern 2 |
| Mouse detection only works in top-left corner | Clamping to viewport instead of framebuffer | Fix clamp bounds |
| No object detection on right half of screen | Premature coordinate clamping | Check `mouse_to_framebuffer()` clamps |
| Objects appear in "weird places" | Mixed coordinate spaces | Verify entire pipeline |

### Debug Logging Template

```cpp
// Add temporary logging to trace coordinate transformation
std::cout << "[DEBUG] Mouse window: (" << mouse_x << "," << mouse_y << ")" << std::endl;
std::cout << "[DEBUG] Framebuffer: (" << fb_x << "," << fb_y << ")" << std::endl;
std::cout << "[DEBUG] Render: (" << render_x << "," << render_y << ")" << std::endl;
std::cout << "[DEBUG] World: (" << world_x << "," << world_y << ")" << std::endl;

// Expected values (example for center of 1600×1051 screen):
// Mouse: (800, 525)
// Framebuffer: (1600, 1050) [if DPI=2.0]
// Render: (800, 525)
// World: depends on camera position + projection
```

## Related Files

- `src/rendering/coordinate_transformer.cpp` - Core transformation logic
- `src/rendering/coordinate_transformer.h` - Interface
- `src/main_key_handler.cpp` - Pattern 2 usage (light/particle creation)
- `src/core/input_system.cpp` - Pattern 2 usage (character look direction)
- `src/ui/ui_system.cpp` - Pattern 1 usage (mouse picking)

## Summary

**Key Insight**: Different operations need different coordinate spaces!

- **Object Queries**: Need framebuffer coordinates (DPI-scaled pixels)
- **World Transformations**: Need render coordinates (NOT framebuffer!)
- **Clamping**: Always clamp to bounds of target coordinate space

**Rule of Thumb**: If you're calling `screen_to_world()`, you should NOT be calling `mouse_to_framebuffer()` first!
