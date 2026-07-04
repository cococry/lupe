#include <GL/gl.h>
#include <GL/glx.h>
#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xdamage.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/shape.h>

#include <X11/Xlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NOB_IMPLEMENTATION
#include "../third_party/nob.h"

#include <sys/time.h>

static float zoom = 1.0f;
static float target_zoom = 1.0f;
static float image_pos_x = 0.0f;
static float image_pos_y = 0.0f;
static float target_image_pos_x = 0.0f;
static float target_image_pos_y = 0.0f;
static int dragging = 0;
static float last_x = 0.0f;
static float last_y = 0.0f;
static KeyCode escape_keycode;

static int needs_rerender = 1;

static int no_lerping = 0;

static bool running = true;

void lupe_log_handler(Nob_Log_Level level, const char *fmt, va_list args) {
  if (level < nob_minimal_log_level)
    return;

  const char *level_name = NULL;

  switch (level) {
  case NOB_INFO:
    level_name = "INFO";
    break;
  case NOB_WARNING:
    level_name = "WARNING";
    break;
  case NOB_ERROR:
    level_name = "ERROR";
    break;
  case NOB_NO_LOGS:
    return;
  default:
    NOB_UNREACHABLE("Nob_Log_Level");
  }

  struct timeval tv;
  gettimeofday(&tv, NULL);

  time_t now = tv.tv_sec;
  struct tm tm_utc;

#if defined(_WIN32)
  gmtime_s(&tm_utc, &now);
#else
  gmtime_r(&now, &tm_utc);
#endif

  char timestamp[32];

  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &tm_utc);

  fprintf(stderr, "%s.%03ldZ [%s]%*s ", timestamp, tv.tv_usec / 1000,
          level_name, (int)(7 - strlen(level_name)), "");

  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
}

GLuint lupe_screenshot_root_to_gl_texture(Display *dpy) {
  Window root = DefaultRootWindow(dpy);

  XWindowAttributes attrs;
  if (!XGetWindowAttributes(dpy, root, &attrs)) {
    fprintf(stderr, "XGetWindowAttributes failed\n");
    return 0;
  }

  int width = attrs.width;
  int height = attrs.height;

  XImage *img = XGetImage(dpy, root, 0, 0, width, height, AllPlanes, ZPixmap);

  if (!img) {
    fprintf(stderr, "XGetImage failed\n");
    return 0;
  }

  uint8_t *rgba = malloc((size_t)width * height * 4);
  if (!rgba) {
    XDestroyImage(img);
    return 0;
  }

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      unsigned long p = XGetPixel(img, x, y);

      uint8_t r = (uint8_t)(((p & img->red_mask) * 255) / img->red_mask);
      uint8_t g = (uint8_t)(((p & img->green_mask) * 255) / img->green_mask);
      uint8_t b = (uint8_t)(((p & img->blue_mask) * 255) / img->blue_mask);

      size_t i = ((size_t)y * width + x) * 4;

      rgba[i + 0] = r;
      rgba[i + 1] = g;
      rgba[i + 2] = b;
      rgba[i + 3] = 255;
    }
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, rgba);

  free(rgba);
  XDestroyImage(img);

  return tex;
}

static int lupe_renderer_init(Display *dpy, Window overlay, GLXContext *o_ctx) {
  XWindowAttributes overlay_attr;
  if (!XGetWindowAttributes(dpy, overlay, &overlay_attr)) {
    nob_log(ERROR, "Failed to get window attributes of overlay window.");
    return 1;
  }

  XVisualInfo match_vis = {0};
  match_vis.visualid = XVisualIDFromVisual(overlay_attr.visual);
  match_vis.screen = DefaultScreen(dpy);

  int n_matches = 0;
  XVisualInfo *vis = XGetVisualInfo(dpy, VisualIDMask | VisualScreenMask,
                                    &match_vis, &n_matches);

  if (!vis || n_matches < 1) {
    nob_log(ERROR, "Overlay window has no X visual info.");
    if (vis) {
      XFree(vis);
    }
    return 1;
  }

  GLXContext ctx = glXCreateContext(dpy, vis, NULL, True);

  XFree(vis);

  if (!ctx) {
    nob_log(ERROR, "Failed to create GLX context.");
    return 1;
  }

  *o_ctx = ctx;

  if (!glXMakeCurrent(dpy, overlay, ctx)) {
    nob_log(ERROR, "Failed to set GLX context to overlay window.");
    glXDestroyContext(dpy, ctx);
    return 1;
  }

  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  return 0;
}

void lupe_render_texture(GLuint texture, float x, float y, float width,
                         float height) {
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, texture);
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

  glBegin(GL_QUADS);
  glTexCoord2f(0, 0);
  glVertex2d(x, y);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2d(x + width, y);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2d(x + width, y + height);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2d(x, y + height);
  glEnd();
}

void lupe_render_scene(Display *dpy, Window overlay, GLuint root_texture,
                       int root_width, int root_height) {
  glViewport(0, 0, root_width, root_height);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, (double)root_width, (double)root_height, 0.0, -1.0, 1.0);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  lupe_render_texture(root_texture, image_pos_x, image_pos_y, root_width * zoom,
                      root_height * zoom);

  glXSwapBuffers(dpy, overlay);
  XFlush(dpy);
}

static inline float clampf(float x, float min, float max) {
  if (x < min)
    return min;
  if (x > max)
    return max;
  return x;
}

static void lupe_handle_scrollwheel(int up, int mouse_x, int mouse_y) {
  float old_zoom = target_zoom;

  float new_zoom = target_zoom + (up ? 0.75f : -0.75f);

  if (new_zoom > 100.0f) {
    new_zoom = 100.0f;
  }

  if (new_zoom < 0.25f) {
    new_zoom = 0.25f;
  }

  target_zoom = new_zoom;

  float zoom_factor = target_zoom / old_zoom;

  target_image_pos_x = mouse_x - (mouse_x - target_image_pos_x) * zoom_factor;

  target_image_pos_y = mouse_y - (mouse_y - target_image_pos_y) * zoom_factor;
}

static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static inline float approach_lerpf(float current, float target) {
  return lerpf(current, target, 0.2f);
}

static int lupe_update_animation(void) {
  float old_zoom = zoom;
  float old_x = image_pos_x;
  float old_y = image_pos_y;

  zoom = approach_lerpf(zoom, target_zoom);
  image_pos_x = approach_lerpf(image_pos_x, target_image_pos_x);
  image_pos_y = approach_lerpf(image_pos_y, target_image_pos_y);

  float epsilon = 0.001f;

  if (fabsf(zoom - target_zoom) < epsilon) {
    zoom = target_zoom;
  }

  if (fabsf(image_pos_x - target_image_pos_x) < epsilon) {
    image_pos_x = target_image_pos_x;
  }

  if (fabsf(image_pos_y - target_image_pos_y) < epsilon) {
    image_pos_y = target_image_pos_y;
  }

  return old_zoom != zoom || old_x != image_pos_x || old_y != image_pos_y;
}

static int lupe_is_animating(void) {
  if (no_lerping)
    return 0;

  const float epsilon = 0.001f;

  return fabsf(zoom - target_zoom) > epsilon ||
         fabsf(image_pos_x - target_image_pos_x) > epsilon ||
         fabsf(image_pos_y - target_image_pos_y) > epsilon;
}

void lupe_handle_event(XEvent *ev, Display *dpy, Window win) {
  switch (ev->type) {
  case KeyPress:
    if (ev->xkey.keycode == escape_keycode) {
      running = false;
    }
    break;
  case Expose:
    needs_rerender = 1;
    break;
  case ButtonPress: {
    unsigned int button = ev->xbutton.button;
    if (button == Button4 || button == Button5) {
      int up = button == Button4;
      lupe_handle_scrollwheel(up, ev->xbutton.x, ev->xbutton.y);
      needs_rerender = 1;
    }
    if (ev->xbutton.button == Button1) {
      dragging = 1;
      last_x = ev->xbutton.x;
      last_y = ev->xbutton.y;
    }
    break;
  }
  case ButtonRelease: {
    if (ev->xbutton.button == Button1) {
      dragging = 0;
    }
    break;
  }
  case MotionNotify: {
    if (!dragging)
      break;

    // drain all queued MotionNotify events
    while (XCheckTypedWindowEvent(dpy, win, MotionNotify, ev)) {
    }

    // this is the top one

    int x = ev->xmotion.x; // window-relative, 0,0 top-left
    int y = ev->xmotion.y;

    int dx = x - last_x;
    int dy = y - last_y;

    target_image_pos_x += dx;
    target_image_pos_y += dy;

    last_x = x;
    last_y = y;

    needs_rerender = 1;

    break;
  }
  default:
    break;
  }
}

static void print_usage(const char *prog) {
  printf("Usage: %s [options]\n"
         "\n"
         "Options:\n"
         "  -nl, --no-lerp    Disable smooth scrolling & panning\n"
         "  -h, --help        Show this help message\n",
         prog);
}

int main(int argc, char **argv) {
  if (argc > 1) {
    if (strcmp(argv[1], "--no-lerp") == 0 || strcmp(argv[1], "-nl") == 0) {
      no_lerping = 1;
    } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
      print_usage(argv[0]);
      return EXIT_SUCCESS;
    }
  }
  nob_set_log_handler(lupe_log_handler);
  Display *dpy = XOpenDisplay(NULL);
  if (!dpy) {
    nob_log(ERROR, "Failed to open X display.");
    return EXIT_FAILURE;
  }

  Window root = DefaultRootWindow(dpy);
  XWindowAttributes attr;
  XGetWindowAttributes(dpy, root, &attr);

  int root_width = attr.width;
  int root_height = attr.height;

  int screen = DefaultScreen(dpy);
  Window win =
      XCreateSimpleWindow(dpy, root, 0, 0, root_width, root_height, 0,
                          BlackPixel(dpy, screen), BlackPixel(dpy, screen));
  XSelectInput(dpy, win,
               StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                   ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                   ExposureMask | LeaveWindowMask);

  XSetWindowAttributes attributes;
  attributes.override_redirect = True;
  XChangeWindowAttributes(dpy, win, CWOverrideRedirect, &attributes);

  GLXContext glx_ctx;
  if (lupe_renderer_init(dpy, win, &glx_ctx) != 0) {
    nob_log(ERROR, "Failed to create OpenGL context.");
    return EXIT_FAILURE;
  }

  GLuint root_texture = lupe_screenshot_root_to_gl_texture(dpy);

  XMapRaised(dpy, win);
  XFlush(dpy);

  /* Wait for map */
  for (;;) {
    XEvent e;
    XNextEvent(dpy, &e);
    if (e.type == MapNotify && e.xmap.window == win) {
      break;
    }
  }

  int grab_result =
      XGrabKeyboard(dpy, win, True, GrabModeAsync, GrabModeAsync, CurrentTime);

  if (grab_result != GrabSuccess) {
    nob_log(ERROR, "Failed to grab keyboard");
    return EXIT_FAILURE;
  }

  escape_keycode = XKeysymToKeycode(dpy, XK_Escape);

  XEvent ev;
  while (running) {
    if (lupe_is_animating()) {
      while (XPending(dpy) > 0) {
        XNextEvent(dpy, &ev);
        lupe_handle_event(&ev, dpy, win); 
      }

      lupe_update_animation();
      lupe_render_scene(dpy, win, root_texture, root_width, root_height);

    } else {
      XNextEvent(dpy, &ev);

      lupe_handle_event(&ev, dpy, win); 

      if (needs_rerender) {
        if (!no_lerping) {
          lupe_update_animation();
        } else {
          zoom = target_zoom;
          image_pos_x = target_image_pos_x;
          image_pos_y = target_image_pos_y;
        }
        lupe_render_scene(dpy, win, root_texture, root_width, root_height);
        needs_rerender = 0;
      }
    }
  }

  XUngrabKeyboard(dpy, CurrentTime);
  XDestroyWindow(dpy, win);
  XCloseDisplay(dpy);

  return EXIT_SUCCESS;
}
