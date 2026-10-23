#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <SDL.h>
#ifdef _WIN32
#include "platform/win32/volume_control.h"
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "snes/ppu.h"

#include "types.h"
#include "variables.h"

#include "zelda_rtl.h"
#include "zelda_cpu_infra.h"

#include "config.h"
#include "extra_tools.h"
#include "save_metadata.h"
#include "hd_texture_pack.h"
#include "assets.h"
#include "load_gfx.h"
#include "util.h"
#include "audio.h"

static bool g_run_without_emu = 0;

// Forwards
static bool LoadRom(const char *filename);
static void LoadLinkGraphics();
static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big);
static void RenderText(uint8 *dst, size_t pitch, int width, int height, int scale, const char *text);
static void RenderTextAt(uint8 *dst, size_t pitch, int width, int height, int scale, const char *text, int x, int y, uint32 background, uint32 foreground);
static void FillRectNative(uint8 *dst, size_t pitch, int width, int height, int scale, int x, int y, int rect_width, int rect_height, uint32 color);
static void HandleInput(int keyCode, int modCode, bool pressed);
static void HandleCommand(uint32 j, bool pressed);
static int RemapSdlButton(int button);
static void HandleGamepadInput(int button, bool pressed);
static void HandleGamepadAxisInput(int gamepad_id, int axis, int value);
static void OpenOneGamepad(int i);
static void HandleVolumeAdjustment(int volume_adjustment);
static void LoadAssets();
static void SwitchDirectory();
static void ApplyPixelLighting(uint8 *pixels, size_t pitch, int width, int height, int scale);
static void DrawEnvironmentAnimations(uint8 *pixels, size_t pitch, int width, int height, int scale);
static void DrawWidescreenHud(uint8 *pixels, size_t pitch, int width, int height, int scale);
static void DrawReplayGhost(uint8 *pixels, size_t pitch, int width, int height, int scale);
static void DrawFramePacingGraph(uint8 *pixels, size_t pitch, int width, int height, int scale);
static void DrawGraphicsMenu(uint8 *pixels, size_t pitch, int width, int height, int scale);
static void CacheGraphicsMenuBackground(uint8 *pixels, size_t pitch, int width, int height, int scale);
static bool DrawCachedGraphicsMenuFrame(bool show_menu);
static bool SaveFrameBmp(const char *path, uint8 *pixels, int pitch, int width, int height);
static void SavePendingScreenshot(uint8 *pixels, int pitch, int width, int height);
static void SaveSlotPreview(int slot);
static void DrawPhotoFrame(uint8 *pixels, size_t pitch, int width, int height);
static void SaveManagerRestoreLatest(void);
static void GraphicsMenuBack(void);
static void ClearAtlasWithConfirmation(void);
static bool HandleGraphicsMenuKey(SDL_Keycode key, SDL_Keymod mod);
static bool HandleGraphicsMenuButton(int button);
static void SetGraphicsMenuOpen(bool open);
const char *OpenGLRenderer_HandleShaderCommand(uint32 command);
bool OpenGLRenderer_CanRenderMode7Gpu();
void OpenGLRenderer_SetMode7Frame(Ppu *ppu);

enum {
  kDefaultFullscreen = 0,
  kMaxWindowScale = 10,
  kDefaultFreq = 44100,
  kDefaultChannels = 2,
  kDefaultSamples = 2048,
};

static const char kWindowTitle[] = "The Legend of Zelda: A Link to the Past";
static uint32 g_win_flags = SDL_WINDOW_RESIZABLE;
static SDL_Window *g_window;

static uint8 g_paused, g_turbo, g_replay_turbo = true, g_cursor = true;
static bool g_graphics_menu_open, g_menu_was_paused, g_redraw_paused_frame, g_menu_frame_ready;
static bool g_screenshot_requested, g_frame_step_requested;
static uint8 *g_graphics_menu_background;
static size_t g_graphics_menu_background_size;
static uint8 *g_photo_frame_source;
static size_t g_photo_frame_source_size;
static int g_graphics_menu_background_width, g_graphics_menu_background_height, g_graphics_menu_background_scale;
static int g_graphics_menu_selection;
static int g_graphics_menu_page, g_remap_mode, g_save_overwrite_confirm;
static bool g_save_label_editing, g_photo_hide_hud = true, g_tools_confirm_reset, g_atlas_confirm_clear;
static char g_save_label_buffer[32];
static int g_photo_zoom = 1, g_photo_pan_x, g_photo_pan_y, g_atlas_world;
static uint32 g_atlas_confirm_deadline;
static float g_frame_time_history[120];
static int g_frame_time_history_pos, g_frame_time_history_count;
static uint32 g_last_display_tick;
static SDL_Surface *g_save_preview_surface;
static int g_save_preview_slot = -1;
static uint8 g_current_window_scale;
static uint8 g_gamepad_buttons;
static int g_input1_state;
static uint8 g_display_perf_mode;
static int g_curr_fps;
static float g_frame_time_ms;
static char g_shader_status[128];
static uint16 g_shader_status_frames;
static int g_ppu_render_flags = 0;
static int g_snes_width, g_snes_height;
static int g_sdl_audio_mixer_volume = SDL_MIX_MAXVOLUME;
static struct RendererFuncs g_renderer_funcs;
static uint32 g_gamepad_modifiers;
static uint16 g_gamepad_last_cmd[kGamepadBtn_Count];

enum {
  kGraphicsPage_Main,
  kGraphicsPage_Saves,
  kGraphicsPage_Controls,
  kGraphicsPage_Hud,
  kGraphicsPage_HdTextures,
  kGraphicsPage_Photo,
  kGraphicsPage_Atlas,
  kGraphicsPage_Tools,
  kGraphicsPage_Challenge,
};

void NORETURN Die(const char *error) {
#if defined(NDEBUG) && defined(_WIN32)
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, kWindowTitle, error, NULL);
#endif
  fprintf(stderr, "Error: %s\n", error);
  exit(1);
}

void ChangeWindowScale(int scale_step) {
  if ((SDL_GetWindowFlags(g_window) & (SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_MAXIMIZED)) != 0)
    return;
  int screen = SDL_GetWindowDisplayIndex(g_window);
  if (screen < 0) screen = 0;
  int max_scale = kMaxWindowScale;
  SDL_Rect bounds;
  int bt = -1, bl, bb, br;
  // note this takes into effect Windows display scaling, i.e., resolution is divided by scale factor
  if (SDL_GetDisplayUsableBounds(screen, &bounds) == 0) {
    // this call may take a while before it is reported by Windows (or not at all in my testing)
    if (SDL_GetWindowBordersSize(g_window, &bt, &bl, &bb, &br) != 0) {
      // guess based on Windows 10/11 defaults
      bl = br = bb = 1;
      bt = 31;
    }
    // Allow a scale level slightly above the max that fits on screen
    int mw = (bounds.w - bl - br + g_snes_width / 4) / g_snes_width;
    int mh = (bounds.h - bt - bb + g_snes_height / 4) / g_snes_height;
    max_scale = IntMin(mw, mh);
  }
  int new_scale = IntMax(IntMin(g_current_window_scale + scale_step, max_scale), 1);
  g_current_window_scale = new_scale;
  int w = new_scale * g_snes_width;
  int h = new_scale * g_snes_height;

  //SDL_RenderSetLogicalSize(g_renderer, w, h);
  SDL_SetWindowSize(g_window, w, h);
  if (bt >= 0) {
    // Center the window on top of the mouse
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    int wx = IntMax(IntMin(mx - w / 2, bounds.x + bounds.w - bl - br - w), bounds.x + bl);
    int wy = IntMax(IntMin(my - h / 2, bounds.y + bounds.h - bt - bb - h), bounds.y + bt);
    SDL_SetWindowPosition(g_window, wx, wy);
  } else {
    SDL_SetWindowPosition(g_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }
}

#define RESIZE_BORDER 20
static SDL_HitTestResult HitTestCallback(SDL_Window *win, const SDL_Point *pt, void *data) {
  uint32 flags = SDL_GetWindowFlags(win);
  if ((flags & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0 || (flags & SDL_WINDOW_FULLSCREEN) != 0)
    return SDL_HITTEST_NORMAL;

  if ((SDL_GetModState() & KMOD_CTRL) != 0)
    return SDL_HITTEST_DRAGGABLE;

  int w, h;
  SDL_GetWindowSize(win, &w, &h);

  if (pt->y < RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_TOPRIGHT : SDL_HITTEST_RESIZE_TOP;
  } else if (pt->y >= h - RESIZE_BORDER) {
    return (pt->x < RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMLEFT :
           (pt->x >= w - RESIZE_BORDER) ? SDL_HITTEST_RESIZE_BOTTOMRIGHT : SDL_HITTEST_RESIZE_BOTTOM;
  } else {
    if (pt->x < RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_LEFT;
    } else if (pt->x >= w - RESIZE_BORDER) {
      return SDL_HITTEST_RESIZE_RIGHT;
    }
  }
  return SDL_HITTEST_NORMAL;
}

static void DrawPpuFrameWithPerf() {
  uint32 render_flags = g_ppu_render_flags;
  bool draw_hud = g_config.widescreen_hud && g_config.extended_aspect_ratio &&
                  (main_module_index == 7 || main_module_index == 9) && submodule_index == 0;
  bool draw_lighting = g_config.pixel_lighting && player_is_indoors && link_item_torch &&
                       main_module_index == 7 && submodule_index == 0;
  bool draw_animations = g_config.environment_animations && main_module_index == 9 && submodule_index == 0;
  if (g_graphics_menu_open || draw_hud || draw_lighting || draw_animations)
    render_flags &= ~kPpuRenderFlags_GpuMode7;
  if (HdTexturePack_Enabled())
    render_flags &= ~kPpuRenderFlags_GpuMode7;
  if (g_display_perf_mode || g_shader_status_frames)
    render_flags &= ~kPpuRenderFlags_GpuMode7;
  int render_scale = PpuGetCurrentRenderScale(g_zenv.ppu, render_flags);
  if (render_scale != 4)
    render_flags &= ~kPpuRenderFlags_GpuMode7;
  uint8 *pixel_buffer = 0;
  int pitch = 0;

  g_renderer_funcs.BeginDraw(g_snes_width * render_scale,
                             g_snes_height * render_scale,
                             &pixel_buffer, &pitch);
  if (g_display_perf_mode || g_config.display_perf_title) {
    static float history[64], average;
    static int history_pos;
    uint64 before = SDL_GetPerformanceCounter();
    ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
    uint64 after = SDL_GetPerformanceCounter();
    uint64 frequency = SDL_GetPerformanceFrequency();
    g_frame_time_ms = frequency ? (float)((after - before) * 1000.0 / frequency) : 0;
    float v = after != before ? (double)frequency / (after - before) : 0;
    average += v - history[history_pos];
    history[history_pos] = v;
    history_pos = (history_pos + 1) & 63;
    g_curr_fps = average * (1.0f / 64);
  } else {
    ZeldaDrawPpuFrame(pixel_buffer, pitch, render_flags);
  }
  if (g_graphics_menu_open)
    CacheGraphicsMenuBackground(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_config.pixel_lighting)
    ApplyPixelLighting(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_config.environment_animations)
    DrawEnvironmentAnimations(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_config.ghost_trail_enabled && !(g_graphics_menu_open && g_graphics_menu_page == kGraphicsPage_Photo))
    DrawReplayGhost(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_graphics_menu_open && g_graphics_menu_page == kGraphicsPage_Photo)
    DrawPhotoFrame(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  if (g_config.widescreen_hud && g_config.extended_aspect_ratio &&
      !(g_graphics_menu_open && g_graphics_menu_page == kGraphicsPage_Photo && g_photo_hide_hud))
    DrawWidescreenHud(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_screenshot_requested && g_graphics_menu_open && g_graphics_menu_page == kGraphicsPage_Photo)
    SavePendingScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  OpenGLRenderer_SetMode7Frame(g_zenv.ppu);
  if (g_display_perf_mode)
    RenderNumber(pixel_buffer + pitch * render_scale, pitch, g_curr_fps, render_scale == 4);
  if (g_display_perf_mode >= 2) {
    char stats[96];
    const char *backend = g_config.output_method == kOutputMethod_OpenGL ? "OPENGL" :
                          g_config.output_method == kOutputMethod_OpenGL_ES ? "OPENGL ES" :
                          g_config.output_method == kOutputMethod_SDLSoftware ? "SDL SW" : "SDL";
    snprintf(stats, sizeof(stats), "FPS %d  PPU %.2f MS", g_curr_fps, g_frame_time_ms);
    RenderTextAt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale,
                 stats, 3, 4, 0xff101018, 0xffffffff);
    snprintf(stats, sizeof(stats), "%s  %d X %d", backend, g_snes_width * render_scale, g_snes_height * render_scale);
    RenderTextAt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale,
                 stats, 3, 15, 0xff101018, 0xffc8e8ff);
    snprintf(stats, sizeof(stats), "MODE7 GPU %s", OpenGLRenderer_CanRenderMode7Gpu() ? "DISPONIBLE" : "INDISPONIBLE");
    RenderTextAt(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale,
                 stats, 3, 26, 0xff101018, 0xffc8e8ff);
  }
  if (g_display_perf_mode >= 3)
    DrawFramePacingGraph(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  if (g_shader_status_frames) {
    RenderText(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale, g_shader_status);
    g_shader_status_frames--;
  }
  if (g_graphics_menu_open) {
    DrawGraphicsMenu(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale, render_scale);
  } else {
    g_menu_frame_ready = false;
  }
  if (g_screenshot_requested)
    SavePendingScreenshot(pixel_buffer, pitch, g_snes_width * render_scale, g_snes_height * render_scale);
  g_renderer_funcs.EndDraw();
}

static SDL_mutex *g_audio_mutex;
static uint8 *g_audiobuffer, *g_audiobuffer_cur, *g_audiobuffer_end;
static int g_frames_per_block;
static uint8 g_audio_channels;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  if (SDL_LockMutex(g_audio_mutex)) Die("Mutex lock failed!");
  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      ZeldaRenderAudio((int16*)g_audiobuffer, g_frames_per_block, g_audio_channels);
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16);
    }
    int n = IntMin(len, g_audiobuffer_end - g_audiobuffer_cur);
    if (g_sdl_audio_mixer_volume == SDL_MIX_MAXVOLUME) {
      memcpy(stream, g_audiobuffer_cur, n);
    } else {
      SDL_memset(stream, 0, n);
      SDL_MixAudioFormat(stream, g_audiobuffer_cur, AUDIO_S16, n, g_sdl_audio_mixer_volume);
    }
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

  ZeldaDiscardUnusedAudioFrames();
  SDL_UnlockMutex(g_audio_mutex);
}

// State for sdl renderer
static SDL_Renderer *g_renderer;
static SDL_Texture *g_texture;
static SDL_Rect g_sdl_renderer_rect;

static bool SdlRenderer_Init(SDL_Window *window) {

  if (g_config.shader)
    fprintf(stderr, "Warning: Shaders are supported only with the OpenGL backend\n");

  SDL_Renderer *renderer = SDL_CreateRenderer(g_window, -1,
                                              g_config.output_method == kOutputMethod_SDLSoftware ? SDL_RENDERER_SOFTWARE :
                                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (renderer == NULL) {
    printf("Failed to create renderer: %s\n", SDL_GetError());
    return false;
  }
  SDL_RendererInfo renderer_info;
  SDL_GetRendererInfo(renderer, &renderer_info);
  if (kDebugFlag) {
    printf("Supported texture formats:");
    for (int i = 0; i < renderer_info.num_texture_formats; i++)
      printf(" %s", SDL_GetPixelFormatName(renderer_info.texture_formats[i]));
    printf("\n");
  }
  g_renderer = renderer;
  if (!g_config.ignore_aspect_ratio) {
    SDL_RenderSetLogicalSize(renderer, g_snes_width, g_snes_height);
    if (SDL_RenderSetIntegerScale(renderer, g_config.integer_scaling ? SDL_TRUE : SDL_FALSE) != 0)
      fprintf(stderr, "Warning: Unable to set integer scaling: %s\n", SDL_GetError());
  }
  if (g_config.linear_filtering && !g_config.integer_scaling)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
  else
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");

  int tex_mult = (g_ppu_render_flags & kPpuRenderFlags_4x4Mode7) ? 4 : 1;
  g_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                g_snes_width * tex_mult, g_snes_height * tex_mult);
  if (g_texture == NULL) {
    printf("Failed to create texture: %s\n", SDL_GetError());
    return false;
  }
  return true;
}

static void SdlRenderer_Destroy() {
  SDL_DestroyTexture(g_texture);
  SDL_DestroyRenderer(g_renderer);
}

static void SdlRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  g_sdl_renderer_rect.w = width;
  g_sdl_renderer_rect.h = height;
  if (SDL_LockTexture(g_texture, &g_sdl_renderer_rect, (void **)pixels, pitch) != 0) {
    printf("Failed to lock texture: %s\n", SDL_GetError());
    return;
  }
}

static void SdlRenderer_EndDraw() {

//  uint64 before = SDL_GetPerformanceCounter();
  SDL_UnlockTexture(g_texture);
//  uint64 after = SDL_GetPerformanceCounter();
//  float v = (double)(after - before) / SDL_GetPerformanceFrequency();
//  printf("%f ms\n", v * 1000);
  SDL_RenderClear(g_renderer);
  SDL_RenderCopy(g_renderer, g_texture, &g_sdl_renderer_rect, NULL);
  SDL_RenderPresent(g_renderer); // vsyncs to 60 FPS?
}

static const struct RendererFuncs kSdlRendererFuncs  = {
  &SdlRenderer_Init,
  &SdlRenderer_Destroy,
  &SdlRenderer_BeginDraw,
  &SdlRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es);

enum { kStartupLanguageRowHeight = 18 };

static int StartupLanguageRowsTop(void) {
  int panel_height = g_snes_height - 18;
  int panel_top = (g_snes_height - panel_height) / 2;
  return panel_top + 41;
}

static void DrawStartupTriforce(uint8 *pixels, size_t pitch, int width, int height, int x, int y) {
  const uint32 gold = 0xffffd65a;
  const int size = 8;
  for (int row = 0; row < size; row++) {
    int span = row * 2 + 1;
    FillRectNative(pixels, pitch, width, height, 1, x + 14 - row, y + row, span, 1, gold);
    FillRectNative(pixels, pitch, width, height, 1, x + 7 - row, y + size + row, span, 1, gold);
    FillRectNative(pixels, pitch, width, height, 1, x + 21 - row, y + size + row, span, 1, gold);
  }
}

static void DrawStartupLanguageScreen(int selected, int saved_selection, const char *const *labels,
                                      const char *const *descriptions, int language_count) {
  const int width = g_snes_width;
  const int height = g_snes_height;
  const uint32 background = 0xff081318;
  const uint32 panel = 0xff10251f;
  const uint32 gold = 0xffc8a54a;
  const uint32 bright_gold = 0xffffdc78;
  const uint32 cream = 0xffeee6c8;
  const uint32 muted = 0xffa3b3a0;
  const int panel_width = IntMin(width - 20, 400);
  const int panel_height = height - 18;
  const int left = (width - panel_width) / 2;
  const int top = (height - panel_height) / 2;
  const int row_top = StartupLanguageRowsTop();
  uint8 *pixels;
  int pitch;

  g_renderer_funcs.BeginDraw(width, height, &pixels, &pitch);
  FillRectNative(pixels, pitch, width, height, 1, 0, 0, width, height, background);
  for (int i = 0; i < width / 19 + 1; i++) {
    int x = (i * 47 + 13) % width;
    int y = (i * 71 + 17) % height;
    FillRectNative(pixels, pitch, width, height, 1, x, y, 1, 1, 0xff29463b);
    if ((i & 3) == 0)
      FillRectNative(pixels, pitch, width, height, 1, x + 2, y - 1, 1, 1, 0xff536143);
  }

  FillRectNative(pixels, pitch, width, height, 1, left - 3, top - 3, panel_width + 6, panel_height + 6, 0xff020708);
  FillRectNative(pixels, pitch, width, height, 1, left - 2, top - 2, panel_width + 4, panel_height + 4, gold);
  FillRectNative(pixels, pitch, width, height, 1, left, top, panel_width, panel_height, panel);
  FillRectNative(pixels, pitch, width, height, 1, left + 3, top + 3, panel_width - 6, panel_height - 6, 0xff1b3027);
  FillRectNative(pixels, pitch, width, height, 1, left + 5, top + 5, panel_width - 10, panel_height - 10, panel);

  RenderTextAt(pixels, pitch, width, height, 1, "THE LEGEND OF ZELDA", left + 11, top + 8, 0, bright_gold);
  RenderTextAt(pixels, pitch, width, height, 1, "CHOIX DE LA LANGUE", left + 11, top + 21, 0, cream);
  DrawStartupTriforce(pixels, pitch, width, height, left + panel_width - 36, top + 9);
  FillRectNative(pixels, pitch, width, height, 1, left + 9, top + 35, panel_width - 18, 1, gold);

  for (int i = 0; i < language_count; i++) {
    int y = row_top + i * kStartupLanguageRowHeight;
    bool is_selected = i == selected;
    FillRectNative(pixels, pitch, width, height, 1, left + 8, y, panel_width - 16, 16,
                   is_selected ? 0xff31533d : (i & 1) ? 0xff142a23 : panel);
    if (is_selected) {
      uint32 pulse = (SDL_GetTicks() / 240) & 1 ? bright_gold : gold;
      FillRectNative(pixels, pitch, width, height, 1, left + 8, y, panel_width - 16, 1, pulse);
      FillRectNative(pixels, pitch, width, height, 1, left + 8, y + 15, panel_width - 16, 1, pulse);
      for (int row = 0; row < 5; row++) {
        int span = row < 3 ? row + 1 : 5 - row;
        FillRectNative(pixels, pitch, width, height, 1, left + 13, y + 5 + row, span, 1, bright_gold);
      }
    }
    RenderTextAt(pixels, pitch, width, height, 1, labels[i], left + 23, y + 3, 0,
                 is_selected ? cream : muted);
    if (i == saved_selection)
      RenderTextAt(pixels, pitch, width, height, 1, "MEMORISEE", left + panel_width - 66, y + 3, 0, bright_gold);
  }

  int detail_top = row_top + language_count * kStartupLanguageRowHeight + 3;
  FillRectNative(pixels, pitch, width, height, 1, left + 9, detail_top, panel_width - 18, 1, 0xff526b4c);
  RenderTextAt(pixels, pitch, width, height, 1, "APERCU", left + 12, detail_top + 4, 0, bright_gold);
  RenderTextAt(pixels, pitch, width, height, 1, descriptions[selected], left + 12, detail_top + 15, 0, cream);
  RenderTextAt(pixels, pitch, width, height, 1, "LANGUE SAUVEGARDEE POUR LA SUITE", left + 12,
               detail_top + 27, 0, muted);

  int footer_top = top + panel_height - 23;
  FillRectNative(pixels, pitch, width, height, 1, left + 9, footer_top - 4, panel_width - 18, 1, 0xff526b4c);
  RenderTextAt(pixels, pitch, width, height, 1, "HAUT / BAS OU SOURIS   CHOISIR", left + 12, footer_top, 0, cream);
  RenderTextAt(pixels, pitch, width, height, 1, "ENTREE / A / START   VALIDER", left + 12, footer_top + 11, 0, bright_gold);
  g_renderer_funcs.EndDraw();
}

static bool MapStartupMouseToScreen(SDL_Window *window, int width, int height, int mouse_x, int mouse_y,
                                    int *screen_x, int *screen_y) {
  int window_width, window_height;
  SDL_GetWindowSize(window, &window_width, &window_height);
  if (window_width <= 0 || window_height <= 0)
    return false;
  float scale_x = (float)window_width / width;
  float scale_y = (float)window_height / height;
  if (!g_config.ignore_aspect_ratio) {
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (g_config.integer_scaling && scale >= 1.0f)
      scale = (float)(int)scale;
    float draw_width = width * scale;
    float draw_height = height * scale;
    *screen_x = (int)((mouse_x - (window_width - draw_width) * 0.5f) / scale);
    *screen_y = (int)((mouse_y - (window_height - draw_height) * 0.5f) / scale);
  } else {
    *screen_x = mouse_x / scale_x;
    *screen_y = mouse_y / scale_y;
  }
  return *screen_x >= 0 && *screen_x < width && *screen_y >= 0 && *screen_y < height;
}

static bool SelectStartupLanguage(SDL_Window *window) {
  static const char *const language_codes[] = { "us", "fr", "fr-c", "fr-beauce", "fr-ca-sacres" };
  static const char *const language_labels[] = {
    "ENGLISH", "FRANCAIS (FRANCE)", "QUEBECOIS FAMILIER", "BEAUCE (SLANG)", "QUEBECOIS (SACRES)"
  };
  static const char *const language_descriptions[] = {
    "ORIGINAL US ENGLISH TEXT",
    "TRADUCTION FRANCAISE STANDARD",
    "FRANCAIS QUEBECOIS FAMILIER",
    "EXPRESSIONS REGIONALES DE BEAUCE",
    "QUEBECOIS AVEC SACRES ET JURONS"
  };
  const int language_count = sizeof(language_codes) / sizeof(language_codes[0]);
  int selected = 0;
  int saved_selection = -1;
  for (int i = 0; i < language_count; i++) {
    if (g_config.language && strcmp(g_config.language, language_codes[i]) == 0) {
      selected = i;
      saved_selection = i;
      break;
    }
  }

  bool confirmed = false;
  int axis_navigation[SDL_CONTROLLER_AXIS_MAX] = { 0 };
  while (!confirmed) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      switch (event.type) {
      case SDL_QUIT:
        return false;
      case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_CLOSE)
          return false;
        break;
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_KEYDOWN:
        if (event.key.repeat)
          break;
        switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_LEFT:
          selected = (selected + language_count - 1) % language_count;
          break;
        case SDLK_DOWN:
        case SDLK_RIGHT:
          selected = (selected + 1) % language_count;
          break;
        case SDLK_HOME:
          selected = 0;
          break;
        case SDLK_END:
          selected = language_count - 1;
          break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_SPACE:
        case SDLK_ESCAPE:
          confirmed = true;
          break;
        }
        break;
      case SDL_CONTROLLERBUTTONDOWN:
        switch (event.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
          selected = (selected + language_count - 1) % language_count;
          break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
          selected = (selected + 1) % language_count;
          break;
        case SDL_CONTROLLER_BUTTON_A:
        case SDL_CONTROLLER_BUTTON_START:
          confirmed = true;
          break;
        case SDL_CONTROLLER_BUTTON_B:
          confirmed = true;
          break;
        }
        break;
      case SDL_CONTROLLERAXISMOTION:
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY || event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) {
          int direction = event.caxis.value > 16000 ? 1 : event.caxis.value < -16000 ? -1 : 0;
          if (direction != 0 && axis_navigation[event.caxis.axis] == 0)
            selected = (selected + language_count + direction) % language_count;
          axis_navigation[event.caxis.axis] = direction;
        }
        break;
      case SDL_MOUSEBUTTONDOWN: {
        int mouse_x, mouse_y;
        if (event.button.button == SDL_BUTTON_LEFT &&
            MapStartupMouseToScreen(window, g_snes_width, g_snes_height, event.button.x, event.button.y, &mouse_x, &mouse_y)) {
          int row_top = StartupLanguageRowsTop();
          int row = (mouse_y - row_top) / kStartupLanguageRowHeight;
          if (row >= 0 && row < language_count && mouse_y >= row_top && mouse_y < row_top + language_count * kStartupLanguageRowHeight) {
            selected = row;
            confirmed = true;
          }
        }
        break;
      }
      case SDL_MOUSEMOTION: {
        int mouse_x, mouse_y;
        if (MapStartupMouseToScreen(window, g_snes_width, g_snes_height, event.motion.x, event.motion.y, &mouse_x, &mouse_y)) {
          int row_top = StartupLanguageRowsTop();
          int row = (mouse_y - row_top) / kStartupLanguageRowHeight;
          if (row >= 0 && row < language_count && mouse_y >= row_top && mouse_y < row_top + language_count * kStartupLanguageRowHeight)
            selected = row;
        }
        break;
      }
      }
    }
    DrawStartupLanguageScreen(selected, saved_selection, language_labels, language_descriptions, language_count);
    SDL_Delay(16);
  }

  bool choice_changed = saved_selection < 0 || selected != saved_selection;
  g_config.language = language_codes[selected];
  ZeldaSetLanguage(g_config.language);
  if (choice_changed && !Config_SaveUserSettings())
    fprintf(stderr, "Warning: unable to save selected language.\n");
  return true;
}

#undef main
int main(int argc, char** argv) {
  argc--, argv++;
  const char *config_file = NULL;
  if (argc >= 2 && strcmp(argv[0], "--config") == 0) {
    config_file = argv[1];
    argc -= 2, argv += 2;
  } else {
    SwitchDirectory();
  }
  ParseConfigFile(config_file);
  LoadAssets();
  LoadLinkGraphics();

  ZeldaInitialize();
  HdTexturePack_Init(g_zenv.ppu);
  int saved_pack = g_config.hd_texture_pack_folder[0] ?
                   HdTexturePack_FindFolder(g_config.hd_texture_pack_folder) : g_config.hd_texture_pack_index;
  if (g_config.hd_texture_pack_folder[0] && saved_pack < 0) {
    snprintf(g_shader_status, sizeof(g_shader_status), "PACK HD INTROUVABLE: %s", g_config.hd_texture_pack_folder);
    g_shader_status_frames = 240;
  }
  if (saved_pack >= 0 && HdTexturePack_Select(saved_pack) && HdTexturePack_Enabled()) {
    g_config.new_renderer = true;
    g_config.enhanced_mode7 = true;
  }
  g_zenv.ppu->extraLeftRight = UintMin(g_config.extended_aspect_ratio, kPpuExtraLeftRight);
  g_snes_width = (g_config.extended_aspect_ratio * 2 + 256);
  g_snes_height = (g_config.extend_y ? 240 : 224);


  // Delay actually setting those features in ram until any snapshots finish playing.
  g_wanted_zelda_features = g_config.features0;

  g_ppu_render_flags = g_config.new_renderer * kPpuRenderFlags_NewRenderer |
                       g_config.enhanced_mode7 * kPpuRenderFlags_4x4Mode7 |
                       g_config.extend_y * kPpuRenderFlags_Height240 |
                       g_config.no_sprite_limits * kPpuRenderFlags_NoSpriteLimits;
  ZeldaEnableMsu(g_config.enable_msu);

  if (g_config.fullscreen == 1)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
  else if (g_config.fullscreen == 2)
    g_win_flags ^= SDL_WINDOW_FULLSCREEN;

  // Window scale (1=100%, 2=200%, 3=300%, etc.)
  g_current_window_scale = (g_config.window_scale == 0) ? 2 : IntMin(g_config.window_scale, kMaxWindowScale);

  // audio_freq: Use common sampling rates (see user config file. values higher than 48000 are not supported.)
  if (g_config.audio_freq < 11025 || g_config.audio_freq > 48000)
    g_config.audio_freq = kDefaultFreq;

  // Currently, the SPC/DSP implementation only supports up to stereo.
  if (g_config.audio_channels < 1 || g_config.audio_channels > 2)
    g_config.audio_channels = kDefaultChannels;

  // audio_samples: power of 2
  if (g_config.audio_samples <= 0 || ((g_config.audio_samples & (g_config.audio_samples - 1)) != 0))
    g_config.audio_samples = kDefaultSamples;

  // set up SDL
  if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    printf("Failed to init SDL: %s\n", SDL_GetError());
    return 1;
  }

  bool custom_size  = g_config.window_width != 0 && g_config.window_height != 0;
  int window_width  = custom_size ? g_config.window_width  : g_current_window_scale * g_snes_width;
  int window_height = custom_size ? g_config.window_height : g_current_window_scale * g_snes_height;

  if (g_config.output_method == kOutputMethod_OpenGL ||
      g_config.output_method == kOutputMethod_OpenGL_ES) {
    g_win_flags |= SDL_WINDOW_OPENGL;
    OpenGLRenderer_Create(&g_renderer_funcs, (g_config.output_method == kOutputMethod_OpenGL_ES));
  } else {
    g_renderer_funcs = kSdlRendererFuncs;
  }

  SDL_Window* window = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width, window_height, g_win_flags);
  if(window == NULL) {
    printf("Failed to create window: %s\n", SDL_GetError());
    return 1;
  }
  g_window = window;
  SDL_SetWindowHitTest(window, HitTestCallback, NULL);

  if (!g_renderer_funcs.Initialize(window))
    return 1;
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    OpenOneGamepad(i);
  if (!SelectStartupLanguage(window)) {
    g_renderer_funcs.Destroy();
    HdTexturePack_Shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  }
  if (g_config.enhanced_mode7 && g_config.new_renderer && OpenGLRenderer_CanRenderMode7Gpu())
    g_ppu_render_flags |= kPpuRenderFlags_GpuMode7;

  SDL_AudioDeviceID device = 0;
  SDL_AudioSpec want = { 0 }, have;
  g_audio_mutex = SDL_CreateMutex();
  if (!g_audio_mutex) Die("No mutex");

  if (g_config.enable_audio) {
    want.freq = g_config.audio_freq;
    want.format = AUDIO_S16;
    want.channels = g_config.audio_channels;
    want.samples = g_config.audio_samples;
    want.callback = &AudioCallback;
    device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (device == 0) {
      printf("Failed to open audio device: %s\n", SDL_GetError());
      return 1;
    }
    g_audio_channels = have.channels;
    g_frames_per_block = (534 * have.freq) / 32000;
    g_audiobuffer = malloc(g_frames_per_block * have.channels * sizeof(int16));
  }

  if (argc >= 1 && !g_run_without_emu)
    LoadRom(argv[0]);

#if defined(_WIN32)
  _mkdir("saves");
#else
  mkdir("saves", 0755);
#endif

  SaveMetadata_Init();
  ExtraTools_Init();
  ZeldaReadSram();

  bool running = true;
  SDL_Event event;
  uint32 lastTick = SDL_GetTicks();
  uint32 curTick = 0;
  uint32 frameCtr = 0;
  bool audiopaused = true;

  if (g_config.autosave)
    HandleCommand(kKeys_Load + 0, true);

  while(running) {
    while(SDL_PollEvent(&event)) {
      switch(event.type) {
      case SDL_CONTROLLERDEVICEADDED:
        OpenOneGamepad(event.cdevice.which);
        break;
      case SDL_CONTROLLERAXISMOTION:
        HandleGamepadAxisInput(event.caxis.which, event.caxis.axis, event.caxis.value);
        break;
      case SDL_CONTROLLERBUTTONDOWN:
      case SDL_CONTROLLERBUTTONUP: {
        int b = RemapSdlButton(event.cbutton.button);
        if (b >= 0)
          HandleGamepadInput(b, event.type == SDL_CONTROLLERBUTTONDOWN);
        break;
      }
      case SDL_MOUSEWHEEL:
        if (SDL_GetModState() & KMOD_CTRL && event.wheel.y != 0)
          ChangeWindowScale(event.wheel.y > 0 ? 1 : -1);
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT && event.button.state == SDL_PRESSED && event.button.clicks == 2) {
          if ((g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0 && (g_win_flags & SDL_WINDOW_FULLSCREEN) == 0 && SDL_GetModState() & KMOD_SHIFT) {
            g_win_flags ^= SDL_WINDOW_BORDERLESS;
            SDL_SetWindowBordered(g_window, (g_win_flags & SDL_WINDOW_BORDERLESS) == 0);
          }
        }
        break;
      case SDL_KEYDOWN:
        if (g_graphics_menu_open) {
          if (!event.key.repeat)
            HandleGraphicsMenuKey(event.key.keysym.sym, event.key.keysym.mod);
        } else {
          HandleInput(event.key.keysym.sym, event.key.keysym.mod, true);
        }
        break;
      case SDL_KEYUP:
        if (!g_graphics_menu_open)
          HandleInput(event.key.keysym.sym, event.key.keysym.mod, false);
        break;
      case SDL_TEXTINPUT:
        if (g_save_label_editing) {
          size_t used = strlen(g_save_label_buffer);
          for (const unsigned char *p = (const unsigned char *)event.text.text; *p && used < 24; p++) {
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                (*p >= '0' && *p <= '9') || *p == ' ' || *p == '_' || *p == '-' || *p == '.')
              g_save_label_buffer[used++] = (char)*p;
          }
          g_save_label_buffer[used] = 0;
        }
        break;
      case SDL_QUIT:
        running = false;
        break;
      }
    }

    if (g_paused != audiopaused) {
      audiopaused = g_paused;
      if (device)
        SDL_PauseAudioDevice(device, audiopaused);
    }

    if (g_paused) {
      if (g_frame_step_requested && !g_graphics_menu_open) {
        g_frame_step_requested = false;
        SDL_LockMutex(g_audio_mutex);
        ZeldaRunFrame(g_input1_state | g_gamepad_buttons);
        SDL_UnlockMutex(g_audio_mutex);
        ExtraTools_RecordFrame((main_module_index == 7 || main_module_index == 9),
                               main_module_index == 9 ? (uint8)overworld_screen_index & 0x7f : (uint8)(0x80 | (dungeon_room_index & 0x7f)),
                               link_x_coord, link_y_coord, link_health_current, link_rupees_actual);
        ZeldaRewindCaptureFrame();
        g_menu_frame_ready = false;
        g_redraw_paused_frame = true;
      }
      if (g_graphics_menu_open) {
        if (!DrawCachedGraphicsMenuFrame(true))
          DrawPpuFrameWithPerf();
        g_redraw_paused_frame = false;
      } else if (g_redraw_paused_frame) {
        if (!DrawCachedGraphicsMenuFrame(false))
          DrawPpuFrameWithPerf();
        g_redraw_paused_frame = false;
      }
      SDL_Delay(16);
      continue;
    }

    // Clear gamepad inputs when joypad directional inputs to avoid wonkiness
    int inputs = g_input1_state;
    if (g_input1_state & 0xf0)
      g_gamepad_buttons = 0;
    inputs |= g_gamepad_buttons;

    SDL_LockMutex(g_audio_mutex);
    bool is_replay = ZeldaRunFrame(inputs);
    SDL_UnlockMutex(g_audio_mutex);
    ExtraTools_RecordFrame((main_module_index == 7 || main_module_index == 9),
                           main_module_index == 9 ? (uint8)overworld_screen_index & 0x7f : (uint8)(0x80 | (dungeon_room_index & 0x7f)),
                           link_x_coord, link_y_coord, link_health_current, link_rupees_actual);
    if (frameCtr % 60 == 0)
      ExtraTools_Save();
    ZeldaRewindCaptureFrame();

    frameCtr++;

    if ((g_turbo ^ (is_replay & g_replay_turbo)) && (frameCtr & (g_turbo ? 0xf : 0x7f)) != 0) {
      continue;
    }

    DrawPpuFrameWithPerf();

    if (g_config.display_perf_title) {
      char title[60];
      snprintf(title, sizeof(title), "%s | FPS: %d", kWindowTitle, g_curr_fps);
      SDL_SetWindowTitle(g_window, title);
    }

    // if vsync isn't working, delay manually
    curTick = SDL_GetTicks();

    if (!g_config.disable_frame_delay) {
      static const uint8 delays[3] = { 17, 17, 16 }; // 60 fps
      lastTick += delays[frameCtr % 3];

      if (lastTick > curTick) {
        uint32 delta = lastTick - curTick;
        if (delta > 500) {
          lastTick = curTick - 500;
          delta = 500;
        }
//        printf("Sleeping %d\n", delta);
        SDL_Delay(delta);
      } else if (curTick - lastTick > 500) {
        lastTick = curTick;
      }
    }
    uint32 display_tick = SDL_GetTicks();
    if (g_last_display_tick) {
      float interval = (float)(display_tick - g_last_display_tick);
      g_frame_time_history[g_frame_time_history_pos] = interval;
      g_frame_time_history_pos = (g_frame_time_history_pos + 1) % countof(g_frame_time_history);
      if (g_frame_time_history_count < countof(g_frame_time_history))
        g_frame_time_history_count++;
    }
    g_last_display_tick = display_tick;
  }
  ExtraTools_Save();
  if (g_config.autosave)
    HandleCommand(kKeys_Save + 0, true);

  // clean sdl
  if (g_config.enable_audio) {
    SDL_PauseAudioDevice(device, 1);
    SDL_CloseAudioDevice(device);
  }

  SDL_DestroyMutex(g_audio_mutex);
  free(g_audiobuffer);

  g_renderer_funcs.Destroy();
  HdTexturePack_Shutdown();
  free(g_graphics_menu_background);
  free(g_photo_frame_source);

  SDL_DestroyWindow(window);
  SDL_Quit();
  //SaveConfigFile();
  return 0;
}

static void RenderDigit(uint8 *dst, size_t pitch, int digit, uint32 color, bool big) {
  static const uint8 kFont[] = {
    0x1c, 0x36, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x36, 0x1c,
    0x18, 0x1c, 0x1e, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7e,
    0x3e, 0x63, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x63, 0x7f,
    0x3e, 0x63, 0x60, 0x60, 0x3c, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x30, 0x38, 0x3c, 0x36, 0x33, 0x7f, 0x30, 0x30, 0x30, 0x78,
    0x7f, 0x03, 0x03, 0x03, 0x3f, 0x60, 0x60, 0x60, 0x63, 0x3e,
    0x1c, 0x06, 0x03, 0x03, 0x3f, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x7f, 0x63, 0x60, 0x60, 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x0c,
    0x3e, 0x63, 0x63, 0x63, 0x3e, 0x63, 0x63, 0x63, 0x63, 0x3e,
    0x3e, 0x63, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x60, 0x30, 0x1e,
  };
  const uint8 *p = kFont + digit * 10;
  if (!big) {
    for (int y = 0; y < 10; y++, dst += pitch) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1)
          ((uint32 *)dst)[x] = color;
      }
    }
  } else {
    for (int y = 0; y < 10; y++, dst += pitch * 2) {
      int v = *p++;
      for (int x = 0; v; x++, v >>= 1) {
        if (v & 1) {
          ((uint32 *)dst)[x * 2 + 1] = ((uint32 *)dst)[x * 2] = color;
          ((uint32 *)(dst+pitch))[x * 2 + 1] = ((uint32 *)(dst + pitch))[x * 2] = color;
        }
      }
    }
  }
}

static void RenderNumber(uint8 *dst, size_t pitch, int n, bool big) {
  char buf[32], *s;
  int i;
  sprintf(buf, "%d", n);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + ((pitch + i + 4) << big), pitch, *s - '0', 0x404040, big);
  for (s = buf, i = 2 * 4; *s; s++, i += 8 * 4)
    RenderDigit(dst + (i << big), pitch, *s - '0', 0xffffff, big);
}

static int GetStatusGlyphIndex(char c) {
  if (c >= 'a' && c <= 'z')
    c -= 'a' - 'A';
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= '0' && c <= '9')
    return 26 + c - '0';
  switch (c) {
  case ' ': return 36;
  case ':': return 37;
  case '.': return 38;
  case '-': return 39;
  case '_': return 40;
  case '/': return 41;
  default: return 36;
  }
}

static void RenderTextAt(uint8 *dst, size_t pitch, int width, int height, int scale, const char *text, int x, int y, uint32 background, uint32 foreground) {
  static const uint8 kFont[42][7] = {
    { 14,17,17,31,17,17,17 }, { 30,17,17,30,17,17,30 }, { 14,17,16,16,16,17,14 },
    { 30,17,17,17,17,17,30 }, { 31,16,16,30,16,16,31 }, { 31,16,16,30,16,16,16 },
    { 14,17,16,23,17,17,15 }, { 17,17,17,31,17,17,17 }, { 31,4,4,4,4,4,31 },
    { 7,2,2,2,18,18,12 }, { 17,18,20,24,20,18,17 }, { 16,16,16,16,16,16,31 },
    { 17,27,21,21,17,17,17 }, { 17,25,21,19,17,17,17 }, { 14,17,17,17,17,17,14 },
    { 30,17,17,30,16,16,16 }, { 14,17,17,17,21,18,13 }, { 30,17,17,30,20,18,17 },
    { 15,16,16,14,1,1,30 }, { 31,4,4,4,4,4,4 }, { 17,17,17,17,17,17,14 },
    { 17,17,17,17,17,10,4 }, { 17,17,17,21,21,21,10 }, { 17,17,10,4,10,17,17 },
    { 17,17,10,4,4,4,4 }, { 31,1,2,4,8,16,31 },
    { 14,17,19,21,25,17,14 }, { 4,12,4,4,4,4,14 }, { 14,17,1,2,4,8,31 },
    { 30,1,1,14,1,1,30 }, { 2,6,10,18,31,2,2 }, { 31,16,16,30,1,1,30 },
    { 14,16,16,30,17,17,14 }, { 31,1,2,4,8,8,8 }, { 14,17,17,14,17,17,14 },
    { 14,17,17,15,1,1,14 },
    { 0,0,0,0,0,0,0 }, { 0,4,4,0,4,4,0 }, { 0,0,0,0,0,12,12 },
    { 0,0,0,31,0,0,0 }, { 0,0,0,0,0,0,31 }, { 1,2,4,8,16,0,0 }
  };
  int max_chars = (width / scale - x - 2) / 6;
  int length = 0;
  while (text[length] && length < max_chars)
    length++;
  if (length == 0)
    return;

  int left = x * scale, top = y * scale;
  if (background != 0)
    FillRectNative(dst, pitch, width, height, scale, x, y, length * 6 + 2, 11, background);
  for (int i = 0; i < length; i++) {
    int glyph = GetStatusGlyphIndex(text[i]);
    for (int y = 0; y < 7; y++) {
      for (int x = 0; x < 5; x++) {
        if ((kFont[glyph][y] & (1 << (4 - x))) == 0)
          continue;
        for (int sy = 0; sy < scale; sy++) {
          int py = top + (2 + y) * scale + sy;
          if (py >= height)
            continue;
          uint32 *row = (uint32 *)(dst + py * pitch);
          for (int sx = 0; sx < scale; sx++) {
            int px = left + (2 + i * 6 + x) * scale + sx;
            if (px < width)
              row[px] = foreground;
          }
        }
      }
    }
  }
}

static void RenderText(uint8 *dst, size_t pitch, int width, int height, int scale, const char *text) {
  RenderTextAt(dst, pitch, width, height, scale, text, 2, 2, 0xff101018, 0xffffffff);
}

static void FillRectNative(uint8 *dst, size_t pitch, int width, int height, int scale, int x, int y, int rect_width, int rect_height, uint32 color) {
  int left = IntMax(x * scale, 0);
  int top = IntMax(y * scale, 0);
  int right = IntMin((x + rect_width) * scale, width);
  int bottom = IntMin((y + rect_height) * scale, height);
  for (int py = top; py < bottom; py++) {
    uint32 *row = (uint32 *)(dst + py * pitch);
    for (int px = left; px < right; px++)
      row[px] = color;
  }
}

static void ApplyPixelLighting(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  if (!player_is_indoors || !link_item_torch || main_module_index != 7 || submodule_index != 0)
    return;

  int extra_left = g_zenv.ppu->extraLeftRight;
  int player_height = ((int16)link_z_coord >= 0 || BYTE(link_z_coord) < 0xf0) ? BYTE(link_z_coord) : 0;
  int center_x = (int16)(link_x_coord - BG2HOFS_copy2) + extra_left + 8;
  int center_y = (int16)(link_y_coord - BG2VOFS_copy2) - player_height + 8;
  int radius = 34;
  int radius_squared = radius * radius;

  for (int y = -radius; y <= radius; y++) {
    int py = (center_y + y) * scale;
    if (py + scale <= 0 || py >= height)
      continue;
    int top = IntMax(py, 0);
    int bottom = IntMin(py + scale, height);
    for (int x = -radius; x <= radius; x++) {
      int distance_squared = x * x + y * y;
      if (distance_squared >= radius_squared)
        continue;
      int px = (center_x + x) * scale;
      if (px < 0 || px >= width)
        continue;
      int strength = (radius_squared - distance_squared) * 30 / radius_squared;
      for (int sy = top; sy < bottom; sy++) {
        uint32 *row = (uint32 *)(pixels + sy * pitch);
        for (int sx = 0; sx < scale && px + sx < width; sx++) {
          uint32 color = row[px + sx];
          int red = IntMin(((color >> 16) & 255) + strength, 255);
          int green = IntMin(((color >> 8) & 255) + strength / 2, 255);
          row[px + sx] = (color & 0xff000000) | red << 16 | green << 8 | (color & 255);
        }
      }
    }
  }
}

static void DrawEnvironmentAnimations(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  if (main_module_index != 9 || submodule_index != 0)
    return;

  int native_width = width / scale;
  int native_height = height / scale;
  uint32 tick = frame_counter;
  for (int i = 0; i < 6; i++) {
    uint32 seed = (uint32)(i * 977 + 211);
    int x = (int)((tick * (i % 3 + 1) + seed) % native_width);
    int y = (int)((tick / (i % 2 + 3) + seed * 3) % native_height);
    int px = x * scale, py = y * scale;
    uint32 color = *(uint32 *)(pixels + py * pitch + px * 4);
    int red = (color >> 16) & 255, green = (color >> 8) & 255, blue = color & 255;
    if (green < 35 || green < red + 10 || green < blue + 12)
      continue;

    uint32 leaf = (i & 1) ? 0xffc8d878 : 0xffe6db87;
    for (int sy = 0; sy < scale && py + sy < height; sy++) {
      uint32 *row = (uint32 *)(pixels + (py + sy) * pitch);
      for (int sx = 0; sx < scale && px + sx < width; sx++)
        row[px + sx] = leaf;
      if (px + scale < width)
        row[px + scale] = leaf;
    }
  }
}

static bool SaveFrameBmp(const char *path, uint8 *pixels, int pitch, int width, int height) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels, width, height, 32, pitch, SDL_PIXELFORMAT_ARGB8888);
  if (!surface)
    return false;
  bool ok = SDL_SaveBMP(surface, path) == 0;
  SDL_FreeSurface(surface);
  return ok;
}

static void EnsureDirectory(const char *path) {
#ifdef _WIN32
  _mkdir(path);
#else
  mkdir(path, 0777);
#endif
}

static void SavePendingScreenshot(uint8 *pixels, int pitch, int width, int height) {
  static unsigned int counter;
  char filename[128], stamp[48];
  time_t now = time(NULL);
  struct tm *tm_now = localtime(&now);
  if (tm_now)
    strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", tm_now);
  else
    snprintf(stamp, sizeof(stamp), "unknown-time");
  EnsureDirectory("screenshots");
  bool ok = false;
  for (int attempt = 0; attempt < 100; attempt++) {
    snprintf(filename, sizeof(filename), "screenshots/zelda3-%s-%03u.bmp", stamp, counter++);
    struct stat info;
    if (stat(filename, &info) == 0)
      continue;
    ok = SaveFrameBmp(filename, pixels, pitch, width, height);
    break;
  }
  snprintf(g_shader_status, sizeof(g_shader_status), ok ? "CAPTURE: %s" : "ECHEC DE LA CAPTURE", ok ? filename : "");
  g_shader_status_frames = 180;
  g_screenshot_requested = false;
}

static void SaveSlotPreview(int slot) {
  if (!g_menu_frame_ready || !g_graphics_menu_background || slot < 0 || slot > 19)
    return;
  EnsureDirectory("saves");
  uint32 *thumbnail = (uint32*)malloc(80 * 45 * sizeof(uint32));
  if (!thumbnail)
    return;
  int width = g_graphics_menu_background_width, height = g_graphics_menu_background_height;
  size_t pitch = width * 4;
  for (int y = 0; y < 45; y++) {
    int sy = (y * height) / 45;
    const uint32 *source = (const uint32*)(g_graphics_menu_background + sy * pitch);
    for (int x = 0; x < 80; x++)
      thumbnail[y * 80 + x] = source[(x * width) / 80];
  }
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(thumbnail, 80, 45, 32, 80 * 4, SDL_PIXELFORMAT_ARGB8888);
  if (surface) {
    char filename[64];
    snprintf(filename, sizeof(filename), "saves/save%d.bmp", slot);
    SDL_SaveBMP(surface, filename);
    SDL_FreeSurface(surface);
  }
  free(thumbnail);
}

static void LoadSavePreview(int slot) {
  if (slot == g_save_preview_slot)
    return;
  if (g_save_preview_surface) {
    SDL_FreeSurface(g_save_preview_surface);
    g_save_preview_surface = NULL;
  }
  g_save_preview_slot = slot;
  char filename[64];
  snprintf(filename, sizeof(filename), "saves/save%d.bmp", slot);
  SDL_Surface *loaded = SDL_LoadBMP(filename);
  if (loaded) {
    g_save_preview_surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(loaded);
  }
}

static bool GetSaveSlotDate(int slot, char *date, size_t date_size) {
  char filename[64];
  struct stat info;
  snprintf(filename, sizeof(filename), "saves/save%d.sav", slot);
  if (stat(filename, &info) != 0) {
    snprintf(date, date_size, "VIDE");
    return false;
  }
  struct tm *tm_info = localtime(&info.st_mtime);
  if (tm_info)
    strftime(date, date_size, "%Y-%m-%d %H:%M", tm_info);
  else
    snprintf(date, date_size, "PRESENT");
  return true;
}

static void DrawSavePreview(uint8 *pixels, size_t pitch, int width, int height, int scale, int x, int y) {
  if (!g_save_preview_surface)
    return;
  int max_width = IntMin(80, width / scale - x - 3);
  int max_height = IntMin(45, height / scale - y - 3);
  if (max_width <= 0 || max_height <= 0)
    return;
  int draw_width = IntMin(max_width, g_save_preview_surface->w);
  int draw_height = IntMin(max_height, g_save_preview_surface->h);
  for (int sy = 0; sy < draw_height; sy++) {
    const uint32 *source = (const uint32*)((const uint8*)g_save_preview_surface->pixels + sy * g_save_preview_surface->pitch);
    for (int sx = 0; sx < draw_width; sx++) {
      uint32 color = source[sx];
      for (int yy = 0; yy < scale; yy++) {
        uint32 *destination = (uint32*)(pixels + (y + sy) * scale * pitch + yy * pitch) + (x + sx) * scale;
        for (int xx = 0; xx < scale; xx++)
          destination[xx] = color;
      }
    }
  }
}

static void DrawHdPackPreview(uint8 *pixels, size_t pitch, int width, int height, int scale,
                              int x, int y, const uint32_t *preview, int preview_width, int preview_height) {
  if (!preview || preview_width <= 0 || preview_height <= 0)
    return;
  FillRectNative(pixels, pitch, width, height, scale, x - 2, y - 2, preview_width + 4, preview_height + 4, 0xffa0a0a0);
  for (int sy = 0; sy < preview_height; sy++) {
    for (int sx = 0; sx < preview_width; sx++) {
      uint32 color = preview[sy * preview_width + sx] | 0xff000000;
      for (int yy = 0; yy < scale; yy++) {
        uint32 *row = (uint32*)(pixels + (y + sy) * scale * pitch + yy * pitch) + (x + sx) * scale;
        for (int xx = 0; xx < scale; xx++)
          if ((x + sx) * scale + xx < width && (y + sy) * scale + yy < height)
            row[xx] = color;
      }
    }
  }
}

static void DrawHdPackDescription(uint8 *pixels, size_t pitch, int width, int height, int scale,
                                 int x, int y, int max_chars, const char *description) {
  if (!description || !*description || max_chars < 1)
    return;
  const char *p = description;
  for (int line_index = 0; line_index < 9 && *p; line_index++) {
    char line[64];
    int length = 0, last_space = -1;
    while (p[length] && length < max_chars) {
      if (p[length] == ' ') last_space = length;
      length++;
    }
    if (p[length] && last_space > 0)
      length = last_space;
    memcpy(line, p, length);
    line[length] = 0;
    RenderTextAt(pixels, pitch, width, height, scale, line, x, y + line_index * 9, 0, 0xffdddddd);
    p += length;
    while (*p == ' ') p++;
  }
}

static void DrawWidescreenHud(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  if ((main_module_index != 7 && main_module_index != 9) || submodule_index != 0)
    return;

  char text[40];
  int native_width = width / scale, native_height = height / scale;
  int top = g_config.hud_position_bottom ? native_height - 24 : 3;
  int line = 0;
  if (g_config.hud_show_health) {
    snprintf(text, sizeof(text), "PV %d/%d", (link_health_current + 7) >> 3, (link_health_capacity + 7) >> 3);
    RenderTextAt(pixels, pitch, width, height, scale, text, 3, top + line++ * 11, 0xff101018, 0xffd8f0d0);
  }
  if (g_config.hud_show_magic) {
    snprintf(text, sizeof(text), "MAGIE %d", link_magic_power);
    RenderTextAt(pixels, pitch, width, height, scale, text, 3, top + line++ * 11, 0xff101018, 0xff9edaff);
  }
  line = 0;
  if (g_config.hud_show_rupees) {
    snprintf(text, sizeof(text), "RUBIS %d", link_rupees_actual);
    RenderTextAt(pixels, pitch, width, height, scale, text, native_width - (int)strlen(text) * 6 - 4,
                 top + line++ * 11, 0xff101018, 0xffffdf86);
  }
  if (g_config.hud_show_keys) {
    snprintf(text, sizeof(text), link_num_keys == 0xff ? "CLES -" : "CLES %d", link_num_keys);
    RenderTextAt(pixels, pitch, width, height, scale, text, native_width - (int)strlen(text) * 6 - 4,
                 top + line * 11, 0xff101018, 0xffffdf86);
  }
}

static void DrawReplayGhost(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  if ((main_module_index != 7 && main_module_index != 9) || submodule_index != 0)
    return;
  uint8 current_screen = main_module_index == 9 ? ((uint8)overworld_screen_index & 0x7f) :
                        (uint8)(0x80 | (dungeon_room_index & 0x7f));
  int extra_left = g_zenv.ppu->extraLeftRight;
  for (int age = 120; age >= 0; age -= 4) {
    ExtraToolsGhostPoint point;
    if (!ExtraTools_GetGhostPoint(age, &point) || point.screen != current_screen)
      continue;
    int x = (int16)(point.x - BG2HOFS_copy2) + extra_left + 8;
    int y = (int16)(point.y - BG2VOFS_copy2) + 8;
    uint32 color = age < 12 ? 0xffe8fff0 : age < 40 ? 0xff76dca0 : 0xff46845a;
    FillRectNative(pixels, pitch, width, height, scale, x - 2, y - 2, 5, 5, color);
  }
}

static void DrawFramePacingGraph(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  int native_width = width / scale, native_height = height / scale;
  int graph_width = IntMin(120, native_width - 12);
  int graph_height = 30;
  int left = 5, top = 43;
  if (graph_width < 8 || top + graph_height + 20 >= native_height)
    return;
  FillRectNative(pixels, pitch, width, height, scale, left - 2, top - 2, graph_width + 4, graph_height + 20, 0xff101018);
  int target_y = top + graph_height - (int)(16.7f * graph_height / 33.0f);
  FillRectNative(pixels, pitch, width, height, scale, left, target_y, graph_width, 1, 0xffd0bd50);

  float total = 0, maximum = 0;
  int count = IntMin(g_frame_time_history_count, graph_width);
  for (int i = 0; i < count; i++) {
    int sample = g_frame_time_history_pos - count + i;
    if (sample < 0) sample += countof(g_frame_time_history);
    float value = g_frame_time_history[sample];
    total += value;
    if (value > maximum) maximum = value;
    int bar_height = IntMax(1, IntMin(graph_height, (int)(value * graph_height / 33.0f)));
    uint32 color = value > 20.0f ? 0xffe45c50 : value > 16.7f ? 0xffffc050 : 0xff58bc74;
    FillRectNative(pixels, pitch, width, height, scale, left + i, top + graph_height - bar_height,
                   1, bar_height, color);
  }
  char label[64];
  snprintf(label, sizeof(label), "AVG %.1f MAX %.1f MS", count ? total / count : 0, maximum);
  RenderTextAt(pixels, pitch, width, height, scale, label, left, top + graph_height + 1, 0, 0xffffffff);
  RenderTextAt(pixels, pitch, width, height, scale, "CIBLE 16.7 MS", left, top + graph_height + 11, 0, 0xffd0bd50);
}

static void DrawMenuRow(uint8 *pixels, size_t pitch, int width, int height, int scale,
                        int left, int top, int panel_width, int row, int row_height,
                        const char *text, bool selected) {
  int y = top + row;
  uint32 selection_color = g_config.accessibility_profile == 1 ? 0xff005f9e : 0xff304b35;
  FillRectNative(pixels, pitch, width, height, scale, left + 3, y, panel_width - 6, row_height,
                 selected ? selection_color : 0xff101018);
  RenderTextAt(pixels, pitch, width, height, scale, text, left + 6, y, 0, 0xffffffff);
}

static int GraphicsMenuItemCount(void) {
  switch (g_graphics_menu_page) {
  case kGraphicsPage_Main: return 16;
  case kGraphicsPage_Saves: return 20;
  case kGraphicsPage_Controls: return 12;
  case kGraphicsPage_Hud: return 6;
  case kGraphicsPage_HdTextures: return IntMin(HdTexturePack_Count() + 1, 17);
  case kGraphicsPage_Photo: return 6;
  case kGraphicsPage_Atlas: return 1;
  case kGraphicsPage_Tools: return 8;
  case kGraphicsPage_Challenge: return 4;
  default: return 1;
  }
}

static void DrawGraphicsMenu(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  static const char *const main_labels[] = {
    "HUD 16:9", "ECLAIRAGE PIXEL", "ANIM ENVIRONNEMENT", "OMBRES / HAUTEUR", "ECHELLE ENTIERE",
    "PACKS TEXTURES HD", "GESTION SAUVEGARDES", "REMAPPER COMMANDES", "PERSONNALISER HUD",
    "MODE PHOTO", "ATLAS EXPLORE", "OUTILS ET DEFIS", "SAUVEGARDE RAPIDE", "CHARGEMENT RAPIDE",
    "RETOUR 0.25 SEC", "FERMER MENU"
  };
  int native_width = width / scale, native_height = height / scale;
  int count = GraphicsMenuItemCount();
  int row_height = g_graphics_menu_page == kGraphicsPage_Main ? 9 :
                   g_graphics_menu_page == kGraphicsPage_Saves ? 9 : 10;
  int panel_width = IntMin(native_width - 8, g_graphics_menu_page == kGraphicsPage_Saves ? 312 :
                           g_graphics_menu_page == kGraphicsPage_HdTextures ? 310 : 300);
  int panel_height = 25 + count * row_height + 14;
  if (g_graphics_menu_page == kGraphicsPage_Atlas) panel_height = 142;
  if (g_graphics_menu_page == kGraphicsPage_Challenge) panel_height = 176;
  panel_height = IntMin(panel_height, native_height - 4);
  int left = (native_width - panel_width) / 2;
  int top = IntMax((native_height - panel_height) / 2, 2);
  const char *title = g_graphics_menu_page == kGraphicsPage_Main ? "MENU GRAPHIQUE" :
                      g_graphics_menu_page == kGraphicsPage_Saves ? "GESTION DES SAUVEGARDES" :
                      g_graphics_menu_page == kGraphicsPage_Controls ? "COMMANDES" :
                      g_graphics_menu_page == kGraphicsPage_Hud ? "PERSONNALISER LE HUD" :
                      g_graphics_menu_page == kGraphicsPage_HdTextures ? "PACKS TEXTURES HD" :
                      g_graphics_menu_page == kGraphicsPage_Photo ? "MODE PHOTO" :
                      g_graphics_menu_page == kGraphicsPage_Atlas ? "ATLAS DES ZONES EXPLOREES" :
                      g_graphics_menu_page == kGraphicsPage_Tools ? "OUTILS ET DEFIS" : "DEFIS LOCAUX";
  FillRectNative(pixels, pitch, width, height, scale, left, top, panel_width, panel_height,
                 g_config.accessibility_profile == 1 ? 0xff000000 : 0xff101018);
  RenderTextAt(pixels, pitch, width, height, scale, title, left + 5, top + 4, 0,
               g_config.accessibility_profile == 1 ? 0xffffffff : 0xff98e89c);

  if (g_graphics_menu_page == kGraphicsPage_Main) {
    for (int i = 0; i < count; i++) {
      char line[64];
      if (i < 5) {
        bool enabled = i == 0 ? g_config.widescreen_hud : i == 1 ? g_config.pixel_lighting :
                       i == 2 ? g_config.environment_animations : i == 3 ? g_config.height_cues : g_config.integer_scaling;
        snprintf(line, sizeof(line), "%s: %s", main_labels[i], enabled ? "ON" : "OFF");
      } else {
        snprintf(line, sizeof(line), "%s", main_labels[i]);
      }
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, panel_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
  } else if (g_graphics_menu_page == kGraphicsPage_HdTextures) {
    int selected_pack = g_graphics_menu_selection - 1;
    int preview_width = 0, preview_height = 0;
    const uint32_t *preview = HdTexturePack_Preview(selected_pack, &preview_width, &preview_height);
    const char *description = HdTexturePack_Description(selected_pack);
    bool has_details = preview != NULL || (description && *description);
    int list_width = has_details ? panel_width - 108 : panel_width;
    int detail_x = left + panel_width - 100;
    for (int i = 0; i < count; i++) {
      char line[64];
      const char *name = i == 0 ? "ORIGINAL SANS PACK" : HdTexturePack_Name(i - 1);
      bool active = i == 0 ? HdTexturePack_Selected() < 0 : HdTexturePack_Selected() == i - 1;
      int name_chars = IntMax(0, (list_width - 15) / 6 - (active ? 2 : 0));
      snprintf(line, sizeof(line), "%.*s%s", name_chars, name, active ? " *" : "");
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, list_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
    if (preview)
      DrawHdPackPreview(pixels, pitch, width, height, scale, detail_x, top + 30, preview, preview_width, preview_height);
    if (description && *description) {
      int description_y = top + (preview ? 89 : 31);
      int description_chars = IntMax(1, (panel_width - (detail_x - left) - 8) / 6);
      DrawHdPackDescription(pixels, pitch, width, height, scale, detail_x, description_y,
                            description_chars, description);
    }
    RenderTextAt(pixels, pitch, width, height, scale, HdTexturePack_Status(), left + 5,
                 top + panel_height - 12, 0, 0xffc8e8ff);
  } else if (g_graphics_menu_page == kGraphicsPage_Saves) {
    LoadSavePreview(g_graphics_menu_selection);
    for (int i = 0; i < count; i++) {
      char line[48];
      snprintf(line, sizeof(line), "%02d  %.15s", i + 1, SaveMetadata_Label(i));
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, panel_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
    int preview_x = left + panel_width - 88;
    int preview_y = top + 34;
    FillRectNative(pixels, pitch, width, height, scale, preview_x - 2, preview_y - 2, 84, 49, 0xff363638);
    DrawSavePreview(pixels, pitch, width, height, scale, preview_x, preview_y);
    char date[32], backup_date[32], backup_text[40];
    GetSaveSlotDate(g_graphics_menu_selection, date, sizeof(date));
    RenderTextAt(pixels, pitch, width, height, scale, date, preview_x - 1, preview_y + 51, 0, 0xffdddddd);
    if (!SaveMetadata_LatestBackupDate(g_graphics_menu_selection, backup_date, sizeof(backup_date)))
      snprintf(backup_date, sizeof(backup_date), "--");
    snprintf(backup_text, sizeof(backup_text), "HIST %d %s", SaveMetadata_BackupCount(g_graphics_menu_selection), backup_date);
    RenderTextAt(pixels, pitch, width, height, scale, backup_text, preview_x - 1, preview_y + 63, 0, 0xffbbbbbb);
    if (g_save_label_editing)
      RenderTextAt(pixels, pitch, width, height, scale, g_save_label_buffer, left + 6, top + panel_height - 12, 0, 0xffffdf86);
    else if (g_save_overwrite_confirm)
      RenderTextAt(pixels, pitch, width, height, scale, "MAJ+ENTREE / X: ECRASER", left + 6, top + panel_height - 12, 0, 0xffffbd78);
  } else if (g_graphics_menu_page == kGraphicsPage_Controls) {
    for (int i = 0; i < count; i++) {
      char line[96];
      snprintf(line, sizeof(line), "%s  CLAVIER:%s  PAD:%s", Config_GetControlName(i),
               Config_GetControlKeyboardName(i), Config_GetControlGamepadName(i));
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, panel_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
    if (g_remap_mode)
      RenderTextAt(pixels, pitch, width, height, scale,
                   g_remap_mode == 1 ? "APPUYEZ SUR UNE TOUCHE (ESC ANNULER)" : "APPUYEZ SUR UN BOUTON (ESC ANNULER)",
                   left + 5, top + panel_height - 12, 0, 0xffffbd78);
  } else if (g_graphics_menu_page == kGraphicsPage_Hud) {
    static const char *const labels[] = { "AFFICHER VIE", "AFFICHER MAGIE", "AFFICHER RUBIS", "AFFICHER CLES", "POSITION BASSE", "RETOUR" };
    for (int i = 0; i < count; i++) {
      bool enabled = i == 0 ? g_config.hud_show_health : i == 1 ? g_config.hud_show_magic :
                     i == 2 ? g_config.hud_show_rupees : i == 3 ? g_config.hud_show_keys :
                     i == 4 ? g_config.hud_position_bottom : false;
      char line[48];
      if (i < 5) snprintf(line, sizeof(line), "%s: %s", labels[i], enabled ? "OUI" : "NON");
      else snprintf(line, sizeof(line), "%s", labels[i]);
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, panel_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
  } else if (g_graphics_menu_page == kGraphicsPage_Photo) {
    static const char *const labels[] = { "MASQUER LE HUD", "ZOOM", "CADRAGE X", "CADRAGE Y", "CAPTURER BMP", "RETOUR" };
    for (int i = 0; i < count; i++) {
      char line[48];
      if (i == 0) snprintf(line, sizeof(line), "%s: %s", labels[i], g_photo_hide_hud ? "OUI" : "NON");
      else if (i == 1) snprintf(line, sizeof(line), "%s: %dX", labels[i], g_photo_zoom);
      else if (i == 2) snprintf(line, sizeof(line), "%s: %+d", labels[i], g_photo_pan_x);
      else if (i == 3) snprintf(line, sizeof(line), "%s: %+d", labels[i], g_photo_pan_y);
      else snprintf(line, sizeof(line), "%s", labels[i]);
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, panel_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
  } else if (g_graphics_menu_page == kGraphicsPage_Atlas) {
    int grid_x = left + (panel_width - 120) / 2;
    int grid_y = top + 27;
    uint8 current_screen = (uint8)overworld_screen_index & 0x7f;
    for (int i = 0; i < 64; i++) {
      uint8 screen = (uint8)(g_atlas_world ? 0x40 + i : i);
      bool visited = ExtraTools_IsVisited(screen);
      bool current = main_module_index == 9 && screen == current_screen;
      uint32 color = current ? 0xffffd050 : visited ? 0xff50aa72 : 0xff27343c;
      FillRectNative(pixels, pitch, width, height, scale, grid_x + (i & 7) * 15, grid_y + (i >> 3) * 10,
                     12, 8, color);
    }
    RenderTextAt(pixels, pitch, width, height, scale, g_atlas_world ? "MONDE DES TENEBRES" : "MONDE DE LA LUMIERE",
                 left + 5, top + 111, 0, 0xffdddddd);
    char count_text[48];
    snprintf(count_text, sizeof(count_text), "ECRANS DECOUVERTS: %d", ExtraTools_GetVisitedCount());
    RenderTextAt(pixels, pitch, width, height, scale, count_text, left + 5, top + 123, 0, 0xffa0d0ff);
  } else if (g_graphics_menu_page == kGraphicsPage_Tools) {
    static const char *const labels[] = { "FANTOME DE REPLAY", "SAUVER CHECKPOINT BOSS", "RESTAURER CHECKPOINT", "PROFIL ACCESSIBILITE", "FOCUS CAMERA", "RESET FOCUS CAMERA", "DEFIS LOCAUX", "RETOUR" };
    static const char *const profiles[] = { "STANDARD", "HAUT CONTRASTE", "FLASH REDUIT" };
    for (int i = 0; i < count; i++) {
      char line[64];
      if (i == 0) snprintf(line, sizeof(line), "%s: %s", labels[i], g_config.ghost_trail_enabled ? "ON" : "OFF");
      else if (i == 3) snprintf(line, sizeof(line), "%s: %s", labels[i], profiles[g_config.accessibility_profile]);
      else if (i == 4) snprintf(line, sizeof(line), "%s: %+d", labels[i], g_config.camera_bias);
      else snprintf(line, sizeof(line), "%s", labels[i]);
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 18, panel_width,
                  i * row_height, row_height, line, i == g_graphics_menu_selection);
    }
  } else {
    const ExtraToolsStats *stats = ExtraTools_GetStats();
    char line[64];
    unsigned int seconds = stats->frames_played / 60;
    snprintf(line, sizeof(line), "TEMPS %02u:%02u:%02u", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    RenderTextAt(pixels, pitch, width, height, scale, line, left + 6, top + 22, 0, 0xffffffff);
    snprintf(line, sizeof(line), "MORTS %u  ECRANS %u", stats->deaths, stats->visited_screens);
    RenderTextAt(pixels, pitch, width, height, scale, line, left + 6, top + 34, 0, 0xffc8e8ff);
    snprintf(line, sizeof(line), "PIC RUBIS %u", stats->peak_rupees);
    RenderTextAt(pixels, pitch, width, height, scale, line, left + 6, top + 46, 0, 0xffffdf86);
    RenderTextAt(pixels, pitch, width, height, scale, "CIBLES: 20 ECRANS + OPTIONS", left + 6, top + 59, 0, 0xff98e89c);
    snprintf(line, sizeof(line), "PROGRESSION %u/20  MORTS %u", IntMin(stats->challenge_screens, 20), stats->deaths);
    RenderTextAt(pixels, pitch, width, height, scale, line, left + 6, top + 71, 0, 0xffdddddd);
    bool time_ok = !g_config.challenge_time_limit_enabled || stats->frames_played < 600 * 60;
    bool deaths_ok = !g_config.challenge_no_death_enabled || stats->deaths == 0;
    bool completed = stats->challenge_screens >= 20 && time_ok && deaths_ok;
    RenderTextAt(pixels, pitch, width, height, scale, completed ? "DEFI REUSSI" : "EN COURS", left + 6, top + 82, 0,
                 completed ? 0xff98e89c : 0xffc8e8ff);
    if (stats->best_challenge_frames) {
      unsigned int best_seconds = stats->best_challenge_frames / 60;
      snprintf(line, sizeof(line), "RECORD %02u:%02u", best_seconds / 60, best_seconds % 60);
    } else {
      snprintf(line, sizeof(line), "RECORD --:--");
    }
    RenderTextAt(pixels, pitch, width, height, scale, line, left + 6, top + 94, 0, 0xffffdf86);
    static const char *const labels[] = { "LIMITE DE TEMPS 10 MIN", "SANS MORT", "REINITIALISER LE DEFI", "RETOUR" };
    for (int i = 0; i < count; i++) {
      char option[48];
      if (i == 0) snprintf(option, sizeof(option), "%s: %s", labels[i], g_config.challenge_time_limit_enabled ? "OUI" : "NON");
      else if (i == 1) snprintf(option, sizeof(option), "%s: %s", labels[i], g_config.challenge_no_death_enabled ? "OUI" : "NON");
      else if (i == 2 && g_tools_confirm_reset) snprintf(option, sizeof(option), "CONFIRMER RESET");
      else snprintf(option, sizeof(option), "%s", labels[i]);
      DrawMenuRow(pixels, pitch, width, height, scale, left, top + 105, panel_width,
                  i * row_height, row_height, option, i == g_graphics_menu_selection);
    }
  }
  const char *help = g_graphics_menu_page == kGraphicsPage_Controls ? "ENTREE CLAVIER, A PAD, ESC RETOUR" :
                     g_graphics_menu_page == kGraphicsPage_Saves ? (g_save_label_editing ? "ENTREE VALIDER, ESC ANNULER" : "ENTREE CHARGER, MAJ+ENTREE/X SAUVER, F2 NOM, R HISTO") :
                     g_graphics_menu_page == kGraphicsPage_Photo ? "G/D REGLER, FLECHES CADRER, ENTREE CAPTURE" :
                     g_graphics_menu_page == kGraphicsPage_Atlas ? "G/D MONDE, C/X EFFACER ATLAS" :
                     g_graphics_menu_page == kGraphicsPage_HdTextures ? "ENTREE APPLIQUER, ESC RETOUR" :
                     "FLECHES NAVIGUER, ENTREE/A CHOISIR, ESC/B RETOUR";
  if (g_graphics_menu_page != kGraphicsPage_HdTextures &&
      !(g_graphics_menu_page == kGraphicsPage_Saves && (g_save_overwrite_confirm || g_save_label_editing)))
    RenderTextAt(pixels, pitch, width, height, scale, help, left + 5, top + panel_height - 11, 0, 0xffaaaaaa);
}
static void CacheGraphicsMenuBackground(uint8 *pixels, size_t pitch, int width, int height, int scale) {
  size_t row_size = width * 4;
  size_t required = row_size * height;
  if (required > g_graphics_menu_background_size) {
    uint8 *background = realloc(g_graphics_menu_background, required);
    if (!background) {
      g_menu_frame_ready = false;
      return;
    }
    g_graphics_menu_background = background;
    g_graphics_menu_background_size = required;
  }
  for (int y = 0; y < height; y++)
    memcpy(g_graphics_menu_background + y * row_size, pixels + y * pitch, row_size);
  g_graphics_menu_background_width = width;
  g_graphics_menu_background_height = height;
  g_graphics_menu_background_scale = scale;
  g_menu_frame_ready = true;
}

static void DrawPhotoFrame(uint8 *pixels, size_t pitch, int width, int height) {
  if (!g_graphics_menu_background || (g_photo_zoom <= 1 && g_photo_pan_x == 0 && g_photo_pan_y == 0))
    return;
  size_t required = (size_t)width * height * 4;
  if (required > g_photo_frame_source_size) {
    uint8 *source = realloc(g_photo_frame_source, required);
    if (!source)
      return;
    g_photo_frame_source = source;
    g_photo_frame_source_size = required;
  }
  for (int y = 0; y < height; y++)
    memcpy(g_photo_frame_source + (size_t)y * width * 4, pixels + y * pitch, width * 4);
  int center_x = width / 2 + g_photo_pan_x * g_graphics_menu_background_scale;
  int center_y = height / 2 + g_photo_pan_y * g_graphics_menu_background_scale;
  for (int y = 0; y < height; y++) {
    uint32 *dst = (uint32*)(pixels + y * pitch);
    int sy = center_y + (y - height / 2) / g_photo_zoom;
    sy = IntMax(0, IntMin(height - 1, sy));
    const uint32 *src = (const uint32*)(g_photo_frame_source + sy * width * 4);
    for (int x = 0; x < width; x++) {
      int sx = center_x + (x - width / 2) / g_photo_zoom;
      sx = IntMax(0, IntMin(width - 1, sx));
      dst[x] = src[sx];
    }
  }
}

static bool DrawCachedGraphicsMenuFrame(bool show_menu) {
  if (!g_menu_frame_ready || !g_graphics_menu_background)
    return false;

  int width = g_graphics_menu_background_width;
  int height = g_graphics_menu_background_height;
  int scale = g_graphics_menu_background_scale;
  uint8 *pixels = NULL;
  int pitch = 0;
  g_renderer_funcs.BeginDraw(width, height, &pixels, &pitch);
  if (!pixels)
    return false;
  size_t row_size = width * 4;
  for (int y = 0; y < height; y++)
    memcpy(pixels + y * pitch, g_graphics_menu_background + y * row_size, row_size);
  if (g_config.pixel_lighting)
    ApplyPixelLighting(pixels, pitch, width, height, scale);
  if (g_config.environment_animations)
    DrawEnvironmentAnimations(pixels, pitch, width, height, scale);
  if (show_menu && g_config.ghost_trail_enabled && g_graphics_menu_page != kGraphicsPage_Photo)
    DrawReplayGhost(pixels, pitch, width, height, scale);
  if (show_menu && g_graphics_menu_page == kGraphicsPage_Photo)
    DrawPhotoFrame(pixels, pitch, width, height);
  if (g_config.widescreen_hud && g_config.extended_aspect_ratio &&
      !(show_menu && g_graphics_menu_page == kGraphicsPage_Photo && g_photo_hide_hud))
    DrawWidescreenHud(pixels, pitch, width, height, scale);
  if (show_menu && g_screenshot_requested && g_graphics_menu_page == kGraphicsPage_Photo)
    SavePendingScreenshot(pixels, pitch, width, height);
  if (show_menu)
    DrawGraphicsMenu(pixels, pitch, width, height, scale);
  if (show_menu && g_shader_status_frames) {
    RenderText(pixels, pitch, width, height, scale, g_shader_status);
    g_shader_status_frames--;
  }
  if (g_screenshot_requested)
    SavePendingScreenshot(pixels, pitch, width, height);
  g_renderer_funcs.EndDraw();
  if (!show_menu)
    g_menu_frame_ready = false;
  return true;
}

static void SetGraphicsMenuOpen(bool open) {
  if (open == g_graphics_menu_open)
    return;
  if (open) {
    g_menu_was_paused = g_paused != 0;
    g_paused = 1;
    g_graphics_menu_selection = 0;
    g_graphics_menu_page = kGraphicsPage_Main;
    g_remap_mode = 0;
    g_save_overwrite_confirm = 0;
    g_save_label_editing = false;
    g_tools_confirm_reset = false;
    g_atlas_confirm_clear = false;
    g_input1_state = 0;
    g_gamepad_buttons = 0;
    g_menu_frame_ready = false;
  } else {
    if (g_save_label_editing) {
      SDL_StopTextInput();
      g_save_label_editing = false;
    }
    g_paused = g_menu_was_paused;
    g_redraw_paused_frame = g_menu_was_paused;
  }
  g_graphics_menu_open = open;
}

static bool PersistMenuSettings(void) {
  if (!Config_SaveUserSettings()) {
    snprintf(g_shader_status, sizeof(g_shader_status), "ECHEC ENREGISTREMENT REGLAGES");
    g_shader_status_frames = 120;
    return false;
  }
  return true;
}

static void ToggleGraphicsMenuItem(int item) {
  if (g_graphics_menu_page == kGraphicsPage_Main) {
    switch (item) {
    case 0: g_config.widescreen_hud = !g_config.widescreen_hud; break;
    case 1: g_config.pixel_lighting = !g_config.pixel_lighting; break;
    case 2: g_config.environment_animations = !g_config.environment_animations; break;
    case 3:
      g_config.height_cues = !g_config.height_cues;
      if (g_config.height_cues) g_config.features0 |= kFeatures0_HeightCues;
      else g_config.features0 &= ~kFeatures0_HeightCues;
      ZeldaSetWantedFeatures(g_config.features0);
      g_menu_frame_ready = false;
      break;
    case 4:
      g_config.integer_scaling = !g_config.integer_scaling;
      if (g_renderer && !g_config.ignore_aspect_ratio)
        SDL_RenderSetIntegerScale(g_renderer, g_config.integer_scaling ? SDL_TRUE : SDL_FALSE);
      break;
    }
  } else if (g_graphics_menu_page == kGraphicsPage_Hud) {
    switch (item) {
    case 0: g_config.hud_show_health = !g_config.hud_show_health; break;
    case 1: g_config.hud_show_magic = !g_config.hud_show_magic; break;
    case 2: g_config.hud_show_rupees = !g_config.hud_show_rupees; break;
    case 3: g_config.hud_show_keys = !g_config.hud_show_keys; break;
    case 4: g_config.hud_position_bottom = !g_config.hud_position_bottom; break;
    }
  }
  PersistMenuSettings();
}

static bool SaveSlotExists(int slot) {
  char filename[64];
  struct stat info;
  snprintf(filename, sizeof(filename), "saves/save%d.sav", slot);
  return stat(filename, &info) == 0;
}

static void SaveManagerActivate(bool save) {
  int slot = g_graphics_menu_selection;
  if (save) {
    if (SaveSlotExists(slot) && !g_save_overwrite_confirm) {
      g_save_overwrite_confirm = 1;
      snprintf(g_shader_status, sizeof(g_shader_status), "APPUYEZ A NOUVEAU POUR ECRASER LA SAUVEGARDE");
      g_shader_status_frames = 90;
      return;
    }
    EnsureDirectory("saves");
    if (!SaveMetadata_BackupBeforeOverwrite(slot)) {
      snprintf(g_shader_status, sizeof(g_shader_status), "ECHEC HISTORIQUE: SAUVEGARDE CONSERVEE");
      g_shader_status_frames = 120;
      return;
    }
    SDL_LockMutex(g_audio_mutex);
    bool ok = SaveLoadSlot(kSaveLoad_Save, slot);
    SDL_UnlockMutex(g_audio_mutex);
    if (ok) SaveSlotPreview(slot);
    snprintf(g_shader_status, sizeof(g_shader_status), ok ? "SAUVEGARDE %02d OK" : "ECHEC SAUVEGARDE %02d", slot + 1);
    g_shader_status_frames = 120;
    g_save_overwrite_confirm = 0;
    g_save_preview_slot = -1;
  } else {
    if (!SaveSlotExists(slot)) {
      snprintf(g_shader_status, sizeof(g_shader_status), "EMPLACEMENT %02d VIDE", slot + 1);
      g_shader_status_frames = 90;
      return;
    }
    SDL_LockMutex(g_audio_mutex);
    bool loaded = SaveLoadSlot(kSaveLoad_Load, slot);
    SDL_UnlockMutex(g_audio_mutex);
    if (loaded) {
      ZeldaSetWantedFeatures(g_config.features0);
      snprintf(g_shader_status, sizeof(g_shader_status), "SAUVEGARDE %02d CHARGEE", slot + 1);
      g_menu_frame_ready = false;
    } else {
      snprintf(g_shader_status, sizeof(g_shader_status), "ECHEC CHARGEMENT %02d", slot + 1);
    }
    g_shader_status_frames = 120;
    g_save_overwrite_confirm = 0;
  }
}

static void SaveManagerRestoreLatest(void) {
  int slot = g_graphics_menu_selection;
  SDL_LockMutex(g_audio_mutex);
  bool restored = SaveMetadata_RestoreLatest(slot);
  bool loaded = restored && SaveLoadSlot(kSaveLoad_Load, slot);
  SDL_UnlockMutex(g_audio_mutex);
  if (loaded) {
    ZeldaSetWantedFeatures(g_config.features0);
    snprintf(g_shader_status, sizeof(g_shader_status), "HISTORIQUE RESTAURE POUR EMPLACEMENT %02d", slot + 1);
    g_menu_frame_ready = false;
  } else {
    snprintf(g_shader_status, sizeof(g_shader_status), "AUCUNE HISTORIQUE RESTAURABLE");
  }
  g_shader_status_frames = 120;
}

static void SaveManagerBeginLabelEdit(void) {
  g_save_label_buffer[0] = 0;
  g_save_label_editing = true;
  SDL_StartTextInput();
}

static void SaveManagerCommitLabel(void) {
  SDL_StopTextInput();
  g_save_label_editing = false;
  if (SaveMetadata_SetLabel(g_graphics_menu_selection, g_save_label_buffer)) {
    snprintf(g_shader_status, sizeof(g_shader_status), "NOM DE SAUVEGARDE ENREGISTRE");
  } else {
    snprintf(g_shader_status, sizeof(g_shader_status), "NOM INVALIDE (1-24 CARACTERES ASCII)");
  }
  g_shader_status_frames = 120;
}

static void SaveBossCheckpoint(bool restore) {
  EnsureDirectory("saves");
  SDL_LockMutex(g_audio_mutex);
  bool ok = restore ? SaveLoadSlot(kSaveLoad_Load, 11) : SaveLoadSlot(kSaveLoad_Save, 11);
  SDL_UnlockMutex(g_audio_mutex);
  if (ok && restore) {
    ZeldaSetWantedFeatures(g_config.features0);
    g_menu_frame_ready = false;
  }
  snprintf(g_shader_status, sizeof(g_shader_status), ok ?
           (restore ? "CHECKPOINT BOSS RESTAURE" : "CHECKPOINT BOSS ENREGISTRE") :
           (restore ? "AUCUN CHECKPOINT BOSS" : "ECHEC CHECKPOINT BOSS"));
  g_shader_status_frames = 120;
}

static void ClearAtlasWithConfirmation(void) {
  uint32 now = SDL_GetTicks();
  if (!g_atlas_confirm_clear || (int32)(now - g_atlas_confirm_deadline) >= 0) {
    g_atlas_confirm_clear = true;
    g_atlas_confirm_deadline = now + 5000;
    snprintf(g_shader_status, sizeof(g_shader_status), "APPUIEZ A NOUVEAU POUR EFFACER L ATLAS");
    g_shader_status_frames = 300;
  } else {
    ExtraTools_ClearAtlas();
    ExtraTools_Save();
    g_atlas_confirm_clear = false;
    snprintf(g_shader_status, sizeof(g_shader_status), "ATLAS EFFACE");
    g_shader_status_frames = 120;
  }
}

static void SetAccessibilityProfile(void) {
  g_config.accessibility_profile = (g_config.accessibility_profile + 1) % 3;
  if (g_config.accessibility_profile == 2)
    g_config.features0 |= kFeatures0_DimFlashes;
  else
    g_config.features0 &= ~kFeatures0_DimFlashes;
  ZeldaSetWantedFeatures(g_config.features0);
  PersistMenuSettings();
  g_menu_frame_ready = false;
}

static void AdjustCameraBias(int direction) {
  g_config.camera_bias = IntMax(-32, IntMin(32, g_config.camera_bias + direction * 4));
  PersistMenuSettings();
  g_menu_frame_ready = false;
}

static void SelectGraphicsMenuItem(void) {
  if (g_graphics_menu_page == kGraphicsPage_Main) {
    switch (g_graphics_menu_selection) {
    case 0: case 1: case 2: case 3: case 4: ToggleGraphicsMenuItem(g_graphics_menu_selection); break;
    case 5: g_graphics_menu_page = kGraphicsPage_HdTextures; g_graphics_menu_selection = HdTexturePack_Selected() + 1; break;
    case 6: g_graphics_menu_page = kGraphicsPage_Saves; g_graphics_menu_selection = 0; g_save_overwrite_confirm = 0; break;
    case 7: g_graphics_menu_page = kGraphicsPage_Controls; g_graphics_menu_selection = 0; break;
    case 8: g_graphics_menu_page = kGraphicsPage_Hud; g_graphics_menu_selection = 0; break;
    case 9: g_graphics_menu_page = kGraphicsPage_Photo; g_graphics_menu_selection = 0; g_photo_pan_x = g_photo_pan_y = 0; g_photo_zoom = 1; break;
    case 10: g_graphics_menu_page = kGraphicsPage_Atlas; g_graphics_menu_selection = 0; break;
    case 11: g_graphics_menu_page = kGraphicsPage_Tools; g_graphics_menu_selection = 0; break;
    case 12: HandleCommand(kKeys_QuickSave, true); break;
    case 13: HandleCommand(kKeys_QuickLoad, true); break;
    case 14: HandleCommand(kKeys_Rewind, true); break;
    default: SetGraphicsMenuOpen(false); break;
    }
  } else if (g_graphics_menu_page == kGraphicsPage_HdTextures) {
    int index = g_graphics_menu_selection - 1;
    if (HdTexturePack_Select(index)) {
      g_config.hd_texture_pack_index = index;
      const char *folder = HdTexturePack_Folder(index);
      snprintf(g_config.hd_texture_pack_folder, sizeof(g_config.hd_texture_pack_folder), "%s", folder ? folder : "");
      PersistMenuSettings();
      if (index >= 0 && (!g_config.new_renderer || !g_config.enhanced_mode7)) {
        snprintf(g_shader_status, sizeof(g_shader_status), "PACK HD CHARGE: REDEMARRER POUR ACTIVER LE MODE HD");
        g_shader_status_frames = 240;
      }
      g_menu_frame_ready = false;
    }
  } else if (g_graphics_menu_page == kGraphicsPage_Saves) {
    SaveManagerActivate(false);
  } else if (g_graphics_menu_page == kGraphicsPage_Controls) {
    g_remap_mode = 1;
  } else if (g_graphics_menu_page == kGraphicsPage_Hud) {
    if (g_graphics_menu_selection < 5) ToggleGraphicsMenuItem(g_graphics_menu_selection);
    else GraphicsMenuBack();
  } else if (g_graphics_menu_page == kGraphicsPage_Photo) {
    if (g_graphics_menu_selection == 0) g_photo_hide_hud = !g_photo_hide_hud;
    else if (g_graphics_menu_selection == 4) g_screenshot_requested = true;
    else if (g_graphics_menu_selection == 5) GraphicsMenuBack();
  } else if (g_graphics_menu_page == kGraphicsPage_Atlas) {
    g_atlas_world = !g_atlas_world;
    g_atlas_confirm_clear = false;
  } else if (g_graphics_menu_page == kGraphicsPage_Tools) {
    switch (g_graphics_menu_selection) {
    case 0: g_config.ghost_trail_enabled = !g_config.ghost_trail_enabled; PersistMenuSettings(); break;
    case 1: SaveBossCheckpoint(false); break;
    case 2: SaveBossCheckpoint(true); break;
    case 3: SetAccessibilityProfile(); break;
    case 4: AdjustCameraBias(1); break;
    case 5: g_config.camera_bias = 0; PersistMenuSettings(); g_menu_frame_ready = false; break;
    case 6: g_graphics_menu_page = kGraphicsPage_Challenge; g_graphics_menu_selection = 0; g_tools_confirm_reset = false; break;
    default: GraphicsMenuBack(); break;
    }
  } else if (g_graphics_menu_page == kGraphicsPage_Challenge) {
    if (g_graphics_menu_selection == 0) {
      g_config.challenge_time_limit_enabled = !g_config.challenge_time_limit_enabled;
      PersistMenuSettings();
    } else if (g_graphics_menu_selection == 1) {
      g_config.challenge_no_death_enabled = !g_config.challenge_no_death_enabled;
      PersistMenuSettings();
    } else if (g_graphics_menu_selection == 2) {
      if (g_tools_confirm_reset) {
        ExtraTools_ResetChallenge();
        ExtraTools_Save();
        g_tools_confirm_reset = false;
      } else {
        g_tools_confirm_reset = true;
      }
    } else if (g_graphics_menu_selection == 3) {
      GraphicsMenuBack();
    }
    g_menu_frame_ready = false;
  }
}

static void GraphicsMenuBack(void) {
  g_atlas_confirm_clear = false;
  if (g_save_label_editing) {
    SDL_StopTextInput();
    g_save_label_editing = false;
    return;
  }
  if (g_remap_mode) {
    g_remap_mode = 0;
    return;
  }
  if (g_graphics_menu_page != kGraphicsPage_Main) {
    int old_page = g_graphics_menu_page;
    g_graphics_menu_page = old_page == kGraphicsPage_Challenge ? kGraphicsPage_Tools : kGraphicsPage_Main;
    g_graphics_menu_selection = old_page == kGraphicsPage_Challenge ? 6 :
                                old_page == kGraphicsPage_Hud ? 8 : old_page == kGraphicsPage_Controls ? 7 :
                                old_page == kGraphicsPage_Saves ? 6 : old_page == kGraphicsPage_HdTextures ? 5 :
                                old_page == kGraphicsPage_Photo ? 9 : old_page == kGraphicsPage_Atlas ? 10 :
                                old_page == kGraphicsPage_Tools ? 11 : 0;
    g_save_overwrite_confirm = 0;
    g_tools_confirm_reset = false;
  } else {
    SetGraphicsMenuOpen(false);
  }
}

static void RemapFinished(bool ok) {
  if (ok) {
    if (PersistMenuSettings()) snprintf(g_shader_status, sizeof(g_shader_status), "COMMANDE MODIFIEE");
    else snprintf(g_shader_status, sizeof(g_shader_status), "COMMANDE MODIFIEE, NON ENREGISTREE");
  } else {
    snprintf(g_shader_status, sizeof(g_shader_status), "TOUCHE DEJA UTILISEE OU INVALIDE");
  }
  g_shader_status_frames = 120;
  g_remap_mode = 0;
}

static void MovePhotoFrame(int dx, int dy) {
  g_photo_pan_x = IntMax(-112, IntMin(112, g_photo_pan_x + dx * 8));
  g_photo_pan_y = IntMax(-80, IntMin(80, g_photo_pan_y + dy * 8));
}

static void AdjustSelectedGraphicsMenuItem(int direction) {
  if (g_graphics_menu_page == kGraphicsPage_Main && g_graphics_menu_selection < 5)
    ToggleGraphicsMenuItem(g_graphics_menu_selection);
  else if (g_graphics_menu_page == kGraphicsPage_Hud && g_graphics_menu_selection < 5)
    ToggleGraphicsMenuItem(g_graphics_menu_selection);
  else if (g_graphics_menu_page == kGraphicsPage_Photo) {
    if (g_graphics_menu_selection == 0) g_photo_hide_hud = !g_photo_hide_hud;
    else if (g_graphics_menu_selection == 1) g_photo_zoom = IntMax(1, IntMin(4, g_photo_zoom + direction));
    else if (g_graphics_menu_selection == 2) MovePhotoFrame(direction, 0);
    else if (g_graphics_menu_selection == 3) MovePhotoFrame(0, direction);
  } else if (g_graphics_menu_page == kGraphicsPage_Atlas) {
    g_atlas_world = !g_atlas_world;
    g_atlas_confirm_clear = false;
  } else if (g_graphics_menu_page == kGraphicsPage_Tools) {
    if (g_graphics_menu_selection == 0) SelectGraphicsMenuItem();
    else if (g_graphics_menu_selection == 3) SetAccessibilityProfile();
    else if (g_graphics_menu_selection == 4) AdjustCameraBias(direction);
  } else if (g_graphics_menu_page == kGraphicsPage_Challenge) {
    if (g_graphics_menu_selection == 0 || g_graphics_menu_selection == 1)
      SelectGraphicsMenuItem();
  } else if (g_graphics_menu_page == kGraphicsPage_HdTextures) {
    int selection = IntMax(0, IntMin(GraphicsMenuItemCount() - 1, g_graphics_menu_selection + direction));
    g_graphics_menu_selection = selection;
  }
}

static bool HandleGraphicsMenuKey(SDL_Keycode key, SDL_Keymod mod) {
  if (!g_graphics_menu_open)
    return false;
  if (g_save_label_editing) {
    if (key == SDLK_ESCAPE) GraphicsMenuBack();
    else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) SaveManagerCommitLabel();
    else if (key == SDLK_BACKSPACE) {
      size_t length = strlen(g_save_label_buffer);
      if (length) g_save_label_buffer[length - 1] = 0;
    }
    return true;
  }
  if (g_remap_mode == 1) {
    if (key == SDLK_ESCAPE) g_remap_mode = 0;
    else RemapFinished(Config_SetControlKeyboard(g_graphics_menu_selection, key, mod));
    return true;
  }
  if (g_remap_mode == 2) {
    if (key == SDLK_ESCAPE) g_remap_mode = 0;
    return true;
  }
  if (g_graphics_menu_page == kGraphicsPage_Atlas && key != SDLK_c)
    g_atlas_confirm_clear = false;
  if (key == SDLK_PRINTSCREEN) { g_screenshot_requested = true; return true; }
  if (g_graphics_menu_page == kGraphicsPage_Saves && key == SDLK_F2) { SaveManagerBeginLabelEdit(); return true; }
  if (g_graphics_menu_page == kGraphicsPage_Saves && key == SDLK_r) { SaveManagerRestoreLatest(); return true; }
  if (g_graphics_menu_page == kGraphicsPage_Atlas && key == SDLK_c) { ClearAtlasWithConfirmation(); return true; }
  switch (key) {
  case SDLK_ESCAPE: GraphicsMenuBack(); break;
  case SDLK_UP:
    g_graphics_menu_selection = (g_graphics_menu_selection + GraphicsMenuItemCount() - 1) % GraphicsMenuItemCount();
    g_save_overwrite_confirm = 0; g_tools_confirm_reset = false; break;
  case SDLK_DOWN:
    g_graphics_menu_selection = (g_graphics_menu_selection + 1) % GraphicsMenuItemCount();
    g_save_overwrite_confirm = 0; g_tools_confirm_reset = false; break;
  case SDLK_LEFT: AdjustSelectedGraphicsMenuItem(-1); break;
  case SDLK_RIGHT: AdjustSelectedGraphicsMenuItem(1); break;
  case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_SPACE:
    if (g_graphics_menu_page == kGraphicsPage_Saves && (mod & KMOD_SHIFT)) SaveManagerActivate(true);
    else if (g_graphics_menu_page == kGraphicsPage_Controls && key == SDLK_SPACE) g_remap_mode = 2;
    else SelectGraphicsMenuItem();
    break;
  default: break;
  }
  return true;
}

static bool HandleGraphicsMenuButton(int button) {
  if (!g_graphics_menu_open)
    return false;
  if (g_save_label_editing || g_remap_mode)
    return true;
  if (g_graphics_menu_page == kGraphicsPage_Atlas && button != kGamepadBtn_X)
    g_atlas_confirm_clear = false;
  switch (button) {
  case kGamepadBtn_DpadUp:
    g_graphics_menu_selection = (g_graphics_menu_selection + GraphicsMenuItemCount() - 1) % GraphicsMenuItemCount();
    g_save_overwrite_confirm = 0; g_tools_confirm_reset = false; break;
  case kGamepadBtn_DpadDown:
    g_graphics_menu_selection = (g_graphics_menu_selection + 1) % GraphicsMenuItemCount();
    g_save_overwrite_confirm = 0; g_tools_confirm_reset = false; break;
  case kGamepadBtn_DpadLeft: AdjustSelectedGraphicsMenuItem(-1); break;
  case kGamepadBtn_DpadRight: AdjustSelectedGraphicsMenuItem(1); break;
  case kGamepadBtn_A:
    if (g_graphics_menu_page == kGraphicsPage_Controls) g_remap_mode = 2;
    else SelectGraphicsMenuItem();
    break;
  case kGamepadBtn_X:
    if (g_graphics_menu_page == kGraphicsPage_Saves) SaveManagerActivate(true);
    else if (g_graphics_menu_page == kGraphicsPage_Atlas) ClearAtlasWithConfirmation();
    else if (g_graphics_menu_page == kGraphicsPage_Controls) g_remap_mode = 1;
    break;
  case kGamepadBtn_Y:
    if (g_graphics_menu_page == kGraphicsPage_Saves) SaveManagerRestoreLatest();
    break;
  case kGamepadBtn_B:
    if (g_graphics_menu_page == kGraphicsPage_Saves && g_save_overwrite_confirm) g_save_overwrite_confirm = 0;
    else GraphicsMenuBack();
    break;
  case kGamepadBtn_L3: SetGraphicsMenuOpen(false); break;
  default: break;
  }
  return true;
}
static void HandleCommand_Locked(uint32 j, bool pressed);

static void HandleCommand(uint32 j, bool pressed) {
  if (j <= kKeys_Controls_Last) {
    static const uint8 kKbdRemap[] = { 0, 4, 5, 6, 7, 2, 3, 8, 0, 9, 1, 10, 11 };
    if (pressed)
      g_input1_state |= 1 << kKbdRemap[j];
    else
      g_input1_state &= ~(1 << kKbdRemap[j]);
    return;
  }

  if (j == kKeys_Turbo) {
    g_turbo = pressed;
    return;
  }

  // Everything that might access audio state
  // (like SaveLoad and Reset) must have the lock.
  SDL_LockMutex(g_audio_mutex);
  HandleCommand_Locked(j, pressed);
  SDL_UnlockMutex(g_audio_mutex);
}

void ZeldaApuLock() {
  SDL_LockMutex(g_audio_mutex);
}

void ZeldaApuUnlock() {
  SDL_UnlockMutex(g_audio_mutex);
}


static void HandleCommand_Locked(uint32 j, bool pressed) {
  if (!pressed)
    return;
  if (j <= kKeys_Load_Last) {
    SaveLoadSlot(kSaveLoad_Load, j - kKeys_Load);
  } else if (j <= kKeys_Save_Last) {
    int slot = j - kKeys_Save;
    bool backup_ok = SaveMetadata_BackupBeforeOverwrite(slot);
    if (!backup_ok || !SaveLoadSlot(kSaveLoad_Save, slot)) {
      snprintf(g_shader_status, sizeof(g_shader_status), "ECHEC SAUVEGARDE %02d", slot + 1);
      g_shader_status_frames = 120;
    }
  } else if (j <= kKeys_Replay_Last) {
    SaveLoadSlot(kSaveLoad_Replay, j - kKeys_Replay);
  } else if (j <= kKeys_LoadRef_Last) {
    SaveLoadSlot(kSaveLoad_Load, 256 + j - kKeys_LoadRef);
  } else if (j <= kKeys_ReplayRef_Last) {
    SaveLoadSlot(kSaveLoad_Replay, 256 + j - kKeys_ReplayRef);
  } else {
    switch (j) {
    case kKeys_CheatLife: PatchCommand('w'); break;
    case kKeys_CheatEquipment: PatchCommand('W'); break;
    case kKeys_CheatKeys: PatchCommand('o'); break;
    case kKeys_CheatWalkThroughWalls: PatchCommand('E'); break;
    case kKeys_ClearKeyLog: PatchCommand('k'); break;
    case kKeys_StopReplay: PatchCommand('l'); break;
    case kKeys_Fullscreen:
      g_win_flags ^= SDL_WINDOW_FULLSCREEN_DESKTOP;
      SDL_SetWindowFullscreen(g_window, g_win_flags & SDL_WINDOW_FULLSCREEN_DESKTOP);
      g_cursor = !g_cursor;
      SDL_ShowCursor(g_cursor);
      break;
    case kKeys_Reset:
      ZeldaReset(true);
      break;
    case kKeys_Pause: g_paused = !g_paused; break;
    case kKeys_PauseDimmed:
      g_paused = !g_paused;
      // SDL_RenderPresent may not be called more than once per frame.
      // Seems to work on Windows still. Temporary measure until it's fixed.
#ifdef _WIN32
      if (g_paused) {
        SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 159);
        SDL_RenderFillRect(g_renderer, NULL);
        SDL_RenderPresent(g_renderer);
      }
#endif
      break;
    case kKeys_ReplayTurbo: g_replay_turbo = !g_replay_turbo; break;
    case kKeys_WindowBigger: ChangeWindowScale(1); break;
    case kKeys_WindowSmaller: ChangeWindowScale(-1); break;
    case kKeys_DisplayPerf: g_display_perf_mode = (g_display_perf_mode + 1) % 4; break;
    case kKeys_ToggleRenderer: g_ppu_render_flags ^= kPpuRenderFlags_NewRenderer; break;
    case kKeys_VolumeUp:
    case kKeys_VolumeDown: HandleVolumeAdjustment(j == kKeys_VolumeUp ? 1 : -1); break;
    case kKeys_ReloadShader:
    case kKeys_ToggleShader:
    case kKeys_ShaderParamPrevious:
    case kKeys_ShaderParamNext:
    case kKeys_ShaderParamDecrease:
    case kKeys_ShaderParamIncrease: {
      const char *status = OpenGLRenderer_HandleShaderCommand(j);
      snprintf(g_shader_status, sizeof(g_shader_status), "%s", status);
      g_shader_status_frames = 180;
      break;
    }
    case kKeys_GraphicsMenu:
      SetGraphicsMenuOpen(!g_graphics_menu_open);
      break;
    case kKeys_QuickSave:
      if (SaveMetadata_BackupBeforeOverwrite(10) && SaveLoadSlot(kSaveLoad_Save, 10)) {
        SaveSlotPreview(10);
        snprintf(g_shader_status, sizeof(g_shader_status), "SAUVEGARDE RAPIDE 11");
      } else {
        snprintf(g_shader_status, sizeof(g_shader_status), "ECHEC SAUVEGARDE RAPIDE");
      }
      g_shader_status_frames = 120;
      break;
    case kKeys_QuickLoad:
      if (SaveLoadSlot(kSaveLoad_Load, 10)) {
        ZeldaSetWantedFeatures(g_config.features0);
        snprintf(g_shader_status, sizeof(g_shader_status), "CHARGEMENT RAPIDE 11");
        g_menu_frame_ready = false;
      } else {
        snprintf(g_shader_status, sizeof(g_shader_status), "SAUVEGARDE 11 INTROUVABLE");
      }
      g_shader_status_frames = 120;
      break;
    case kKeys_Rewind:
      if (ZeldaRewindStep()) {
        ZeldaSetWantedFeatures(g_config.features0);
        snprintf(g_shader_status, sizeof(g_shader_status), "RETOUR 0.25 SEC");
        g_menu_frame_ready = false;
      } else {
        snprintf(g_shader_status, sizeof(g_shader_status), "AUCUN RETOUR DISPONIBLE");
      }
      g_shader_status_frames = 120;
      break;
    case kKeys_Screenshot:
      g_screenshot_requested = true;
      g_redraw_paused_frame = true;
      break;
    case kKeys_FrameAdvance:
      if (g_paused) {
        g_frame_step_requested = true;
        g_redraw_paused_frame = true;
      } else {
        snprintf(g_shader_status, sizeof(g_shader_status), "METTRE EN PAUSE POUR AVANCER IMAGE PAR IMAGE");
        g_shader_status_frames = 120;
      }
      break;
    default: assert(0);
    }
  }
}

static void HandleInput(int keyCode, int keyMod, bool pressed) {
  int j = FindCmdForSdlKey(keyCode, keyMod);
  if (j != 0)
    HandleCommand(j, pressed);
}

static void OpenOneGamepad(int i) {
  if (SDL_IsGameController(i)) {
    SDL_GameController *controller = SDL_GameControllerOpen(i);
    if (!controller)
      fprintf(stderr, "Could not open gamepad %d: %s\n", i, SDL_GetError());
  }
}

static int RemapSdlButton(int button) {
  switch (button) {
  case SDL_CONTROLLER_BUTTON_A: return kGamepadBtn_A;
  case SDL_CONTROLLER_BUTTON_B: return kGamepadBtn_B;
  case SDL_CONTROLLER_BUTTON_X: return kGamepadBtn_X;
  case SDL_CONTROLLER_BUTTON_Y: return kGamepadBtn_Y;
  case SDL_CONTROLLER_BUTTON_BACK: return kGamepadBtn_Back;
  case SDL_CONTROLLER_BUTTON_GUIDE: return kGamepadBtn_Guide;
  case SDL_CONTROLLER_BUTTON_START: return kGamepadBtn_Start;
  case SDL_CONTROLLER_BUTTON_LEFTSTICK: return kGamepadBtn_L3;
  case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return kGamepadBtn_R3;
  case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return kGamepadBtn_L1;
  case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return kGamepadBtn_R1;
  case SDL_CONTROLLER_BUTTON_DPAD_UP: return kGamepadBtn_DpadUp;
  case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return kGamepadBtn_DpadDown;
  case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return kGamepadBtn_DpadLeft;
  case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return kGamepadBtn_DpadRight;
  default: return -1;
  }
}

static void HandleGamepadInput(int button, bool pressed) {
  if (g_graphics_menu_open) {
    if (!!(g_gamepad_modifiers & (1 << button)) != pressed)
      g_gamepad_modifiers ^= 1 << button;
    if (g_remap_mode == 2) {
      if (pressed)
        RemapFinished(Config_SetControlGamepad(g_graphics_menu_selection, button));
      return;
    }
    if (pressed) {
      g_gamepad_last_cmd[button] = 0;
      HandleGraphicsMenuButton(button);
    }
    return;
  }
  if (!!(g_gamepad_modifiers & (1 << button)) == pressed)
    return;
  g_gamepad_modifiers ^= 1 << button;
  if (pressed)
    g_gamepad_last_cmd[button] = FindCmdForGamepadButton(button, g_gamepad_modifiers);
  if (g_gamepad_last_cmd[button] != 0)
    HandleCommand(g_gamepad_last_cmd[button], pressed);
}

static void HandleVolumeAdjustment(int volume_adjustment) {
#if SYSTEM_VOLUME_MIXER_AVAILABLE
  int current_volume = GetApplicationVolume();
  int new_volume = IntMin(IntMax(0, current_volume + volume_adjustment * 5), 100);
  SetApplicationVolume(new_volume);
  printf("[System Volume]=%i\n", new_volume);
#else
  g_sdl_audio_mixer_volume = IntMin(IntMax(0, g_sdl_audio_mixer_volume + volume_adjustment * (SDL_MIX_MAXVOLUME >> 4)), SDL_MIX_MAXVOLUME);
  printf("[SDL mixer volume]=%i\n", g_sdl_audio_mixer_volume);
#endif
}

// Approximates atan2(y, x) normalized to the [0,4) range
// with a maximum error of 0.1620 degrees
// normalized_atan(x) ~ (b x + x^2) / (1 + 2 b x + x^2)
static float ApproximateAtan2(float y, float x) {
  uint32 sign_mask = 0x80000000;
  float b = 0.596227f;
  // Extract the sign bits
  uint32 ux_s = sign_mask & *(uint32 *)&x;
  uint32 uy_s = sign_mask & *(uint32 *)&y;
  // Determine the quadrant offset
  float q = (float)((~ux_s & uy_s) >> 29 | ux_s >> 30);
  // Calculate the arctangent in the first quadrant
  float bxy_a = b * x * y;
  if (bxy_a < 0.0f) bxy_a = -bxy_a;  // avoid fabs
  float num = bxy_a + y * y;
  float atan_1q = num / (x * x + bxy_a + num + 0.000001f);
  // Translate it to the proper quadrant
  uint32_t uatan_2q = (ux_s ^ uy_s) | *(uint32 *)&atan_1q;
  return q + *(float *)&uatan_2q;
}

static void HandleGamepadAxisInput(int gamepad_id, int axis, int value) {
  static int last_gamepad_id, last_x, last_y;
  if (g_graphics_menu_open) {
    g_gamepad_buttons = 0;
    return;
  }
  if (axis == SDL_CONTROLLER_AXIS_LEFTX || axis == SDL_CONTROLLER_AXIS_LEFTY) {
    // ignore other gamepads unless they have a big input
    if (last_gamepad_id != gamepad_id) {
      if (value > -16000 && value < 16000)
        return;
      last_gamepad_id = gamepad_id;
      last_x = last_y = 0;
    }
    *(axis == SDL_CONTROLLER_AXIS_LEFTX ? &last_x : &last_y) = value;
    int buttons = 0;
    if (last_x * last_x + last_y * last_y >= 10000 * 10000) {
      // in the non deadzone part, divide the circle into eight 45 degree
      // segments rotated by 22.5 degrees that control which direction to move.
      // todo: do this without floats?
      static const uint8 kSegmentToButtons[8] = {
        1 << 4,           // 0 = up
        1 << 4 | 1 << 7,  // 1 = up, right
        1 << 7,           // 2 = right
        1 << 7 | 1 << 5,  // 3 = right, down
        1 << 5,           // 4 = down
        1 << 5 | 1 << 6,  // 5 = down, left
        1 << 6,           // 6 = left
        1 << 6 | 1 << 4,  // 7 = left, up
      };
      uint8 angle = (uint8)(int)(ApproximateAtan2(last_y, last_x) * 64.0f + 0.5f);
      buttons = kSegmentToButtons[(uint8)(angle + 16 + 64) >> 5];
    }
    g_gamepad_buttons = buttons;
  } else if ((axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)) {
    if (value < 12000 || value >= 16000)  // hysteresis
      HandleGamepadInput(axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? kGamepadBtn_L2 : kGamepadBtn_R2, value >= 12000);
  }
}

static bool LoadRom(const char *filename) {
  size_t length = 0;
  uint8 *file = ReadWholeFile(filename, &length);
  if(!file) Die("Failed to read file");
  bool result = EmuInitialize(file, length);
  free(file);
  return result;
}

static bool ParseLinkGraphics(uint8 *file, size_t length) {
  if (length < 27 || memcmp(file, "ZSPR", 4) != 0)
    return false;
  uint32 pixel_offs = DWORD(file[9]);
  uint32 pixel_length = WORD(file[13]);
  uint32 palette_offs = DWORD(file[15]);
  uint32 palette_length = WORD(file[19]);
  if ((uint64)pixel_offs + pixel_length > length ||
      (uint64)palette_offs + palette_length > length ||
      pixel_length != 0x7000)
    return false;
  if (kPalette_ArmorAndGloves_SIZE != 150 || kLinkGraphics_SIZE != 0x7000)
    Die("ParseLinkGraphics: Invalid asset sizes");
  memcpy(kLinkGraphics, file + pixel_offs, 0x7000);
  if (palette_length >= 120)
    memcpy(kPalette_ArmorAndGloves, file + palette_offs, 120);
  if (palette_length >= 124)
    memcpy(kGlovesColor, file + palette_offs + 120, 4);
  return true;
}

static void LoadLinkGraphics() {
  if (g_config.link_graphics) {
    fprintf(stderr, "Loading Link Graphics: %s\n", g_config.link_graphics);
    size_t length = 0;
    uint8 *file = ReadWholeFile(g_config.link_graphics, &length);
    if (file == NULL || !ParseLinkGraphics(file, length))
      Die("Unable to load file");
    free(file);
  }
}


const uint8 *g_asset_ptrs[kNumberOfAssets];
uint32 g_asset_sizes[kNumberOfAssets];

static void LoadAssets() {
  size_t length = 0;
  uint8 *data = ReadWholeFile("zelda3_assets.dat", &length);
  if (!data) {
    size_t bps_length, bps_src_length;
    uint8 *bps, *bps_src;
    bps = ReadWholeFile("zelda3_assets.bps", &bps_length);
    if (!bps)
      Die("Failed to read zelda3_assets.dat. Please see the README for information about how you get this file.");
    bps_src = ReadWholeFile("zelda3.sfc", &bps_src_length);
    if (!bps_src)
      Die("Missing file: zelda3.sfc");
    data = ApplyBps(bps_src, bps_src_length, bps, bps_length, &length);
    if (!data)
      Die("Unable to apply zelda3_assets.bps. Please make sure you got the right version of 'zelda3.sfc'");
  }

  static const char kAssetsSig[] = { kAssets_Sig };

  if (length < 16 + 32 + 32 + 8 + kNumberOfAssets * 4 ||
      memcmp(data, kAssetsSig, 48) != 0 ||
      *(uint32*)(data + 80) != kNumberOfAssets)
    Die("Invalid assets file");

  uint32 offset = 88 + kNumberOfAssets * 4 + *(uint32 *)(data + 84);

  for (size_t i = 0; i < kNumberOfAssets; i++) {
    uint32 size = *(uint32 *)(data + 88 + i * 4);
    offset = (offset + 3) & ~3;
    if ((uint64)offset + size > length)
      Die("Assets file corruption");
    g_asset_sizes[i] = size;
    g_asset_ptrs[i] = data + offset;
    offset += size;
  }

  if (g_config.features0 & kFeatures0_DimFlashes) { // patch dungeon floor palettes
    kPalette_DungBgMain[0x484] = 0x70;
    kPalette_DungBgMain[0x485] = 0x95;
    kPalette_DungBgMain[0x486] = 0x57;
  }
}

// Go some steps up and find zelda3.ini
static void SwitchDirectory() {
  char buf[4096];
  if (!getcwd(buf, sizeof(buf) - 32))
    return;
  size_t pos = strlen(buf);

  for (int step = 0; pos != 0 && step < 3; step++) {
    memcpy(buf + pos, "/zelda3.ini", 12);
    FILE *f = fopen(buf, "rb");
    if (f) {
      fclose(f);
      buf[pos] = 0;
      if (step != 0) {
        printf("Found zelda3.ini in %s\n", buf);
        int err = chdir(buf);
        (void)err;
      }
      return;
    }
    pos--;
    while (pos != 0 && buf[pos] != '/' && buf[pos] != '\\')
      pos--;
  }
}

MemBlk FindInAssetArray(int asset, int idx) {
  return FindIndexInMemblk((MemBlk) { g_asset_ptrs[asset], g_asset_sizes[asset] }, idx);
}
