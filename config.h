#pragma once

#define HIGHLIGHT_KEYBIND XK_space
#define QUIT_KEYBIND      XK_Escape

#define DEFAULT_HIGHLIGHT_SIZE 100.0f // in pixels
#define HIGHLIGHT_ADJUST_STEP  30.0f  // in pixels
#define ZOOM_ADJUST_STEP       0.25f  // in % but from 0 to 1
#define MAX_ZOOM               100.0f // in % but from 0 to 1
#define MIN_ZOOM               0.05f  // in % but from 0 to 1
// t value of the lerping function when lerping is enabled
#define LERPING_T 0.3f
// defines how much darker everything outside the torch
// highlight should be when not in torch mode. In %
#define TORCH_DARKNESS 0.95f
