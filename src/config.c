#include "config.h"
#include "types.h"
#include <stdio.h>
#include <string.h>
#include <SDL.h>
#include "features.h"
#include "util.h"
#ifdef _WIN32
__declspec(dllimport) int __stdcall MoveFileExA(const char *existing, const char *replacement, unsigned long flags);
#endif

enum {
  kKeyMod_ScanCode = 0x200,
  kKeyMod_Alt = 0x400,
  kKeyMod_Shift = 0x800,
  kKeyMod_Ctrl = 0x1000,
};

Config g_config;

#define REMAP_SDL_KEYCODE(key) ((key) & SDLK_SCANCODE_MASK ? kKeyMod_ScanCode : 0) | (key) & (kKeyMod_ScanCode - 1)
#define _(x) REMAP_SDL_KEYCODE(x)
#define S(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Shift
#define A(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Alt
#define C(x) REMAP_SDL_KEYCODE(x) | kKeyMod_Ctrl
#define N 0
static const uint16 kDefaultKbdControls[kKeys_Total] = {
  0,
  // Controls
  _(SDLK_UP), _(SDLK_DOWN), _(SDLK_LEFT), _(SDLK_RIGHT), _(SDLK_RSHIFT), _(SDLK_RETURN), _(SDLK_x), _(SDLK_z), _(SDLK_s), _(SDLK_a), _(SDLK_c), _(SDLK_v),
  // LoadState
  _(SDLK_F1), _(SDLK_F2), _(SDLK_F3), _(SDLK_F4), _(SDLK_F5), _(SDLK_F6), _(SDLK_F7), _(SDLK_F8), _(SDLK_F9), _(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // SaveState
  S(SDLK_F1), S(SDLK_F2), S(SDLK_F3), S(SDLK_F4), S(SDLK_F5), S(SDLK_F6), S(SDLK_F7), S(SDLK_F8), S(SDLK_F9), S(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Replay State
  C(SDLK_F1), C(SDLK_F2), C(SDLK_F3), C(SDLK_F4), C(SDLK_F5), C(SDLK_F6), C(SDLK_F7), C(SDLK_F8), C(SDLK_F9), C(SDLK_F10), N, N, N, N, N, N, N, N, N, N,
  // Load Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // Replay Ref State
  N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N, N,
  // CheatLife, CheatKeys, CheatEquipment, CheatWalkThroughWalls
  _(SDLK_w), _(SDLK_o), S(SDLK_w), C(SDLK_e),
  // ClearKeyLog, StopReplay, Fullscreen, Reset, Pause, PauseDimmed, Turbo, ReplayTurbo, WindowBigger, WindowSmaller, DisplayPerf, ToggleRenderer
  _(SDLK_k), _(SDLK_l), A(SDLK_RETURN), C(SDLK_r), S(SDLK_p), _(SDLK_p), _(SDLK_TAB), _(SDLK_t), N, N, _(SDLK_f), _(SDLK_r),
  // VolumeUp, VolumeDown, shader reload/toggle and parameter controls
  S(SDLK_EQUALS), S(SDLK_MINUS), _(SDLK_F11), _(SDLK_F12), C(SDLK_F11), S(SDLK_F11), C(SDLK_F12), S(SDLK_F12),
  // Graphics menu, quick save/load, rewind, screenshot, frame advance
  _(SDLK_ESCAPE), C(SDLK_s), C(SDLK_l), _(SDLK_BACKSPACE), _(SDLK_PRINTSCREEN), _(SDLK_PERIOD),
};
#undef _
#undef A
#undef C
#undef S
#undef N

typedef struct KeyNameId {
  const char *name;
  uint16 id, size;
} KeyNameId;

#define M(n) {#n, kKeys_##n, kKeys_##n##_Last - kKeys_##n + 1}
#define S(n) {#n, kKeys_##n, 1}
static const KeyNameId kKeyNameId[] = {
  {"Null", kKeys_Null, 65535},
  M(Controls), M(Load), M(Save), M(Replay), M(LoadRef), M(ReplayRef),
  S(CheatLife), S(CheatKeys), S(CheatEquipment), S(CheatWalkThroughWalls),
  S(ClearKeyLog), S(StopReplay), S(Fullscreen), S(Reset),
  S(Pause), S(PauseDimmed), S(Turbo), S(ReplayTurbo), S(WindowBigger), S(WindowSmaller), S(VolumeUp), S(VolumeDown), S(DisplayPerf), S(ToggleRenderer),
  S(ReloadShader), S(ToggleShader), S(ShaderParamPrevious), S(ShaderParamNext), S(ShaderParamDecrease), S(ShaderParamIncrease),
  S(GraphicsMenu), S(QuickSave), S(QuickLoad), S(Rewind), S(Screenshot), S(FrameAdvance),
};
#undef S
#undef M
typedef struct KeyMapHashEnt {
  uint16 key, cmd, next;
} KeyMapHashEnt;

static uint16 keymap_hash_first[255];
static KeyMapHashEnt *keymap_hash;
static int keymap_hash_size;
static bool has_keynameid[countof(kKeyNameId)];
static uint16 g_control_keyboard[12];

static bool KeyMapHash_Add(uint16 key, uint16 cmd) {
  if ((keymap_hash_size & 0xff) == 0) {
    if (keymap_hash_size > 10000)
      Die("Too many keys");
    keymap_hash = realloc(keymap_hash, sizeof(KeyMapHashEnt) * (keymap_hash_size + 256));
  }
  int i = keymap_hash_size++;
  KeyMapHashEnt *ent = &keymap_hash[i];
  ent->key = key;
  ent->cmd = cmd;
  ent->next = 0;
  int j = (uint32)key % 255;

  uint16 *cur = &keymap_hash_first[j];
  while (*cur) {
    KeyMapHashEnt *ent = &keymap_hash[*cur - 1];
    if (ent->key == key)
      return false;
    cur = &ent->next;
  }
  *cur = i + 1;
  return true;
}

static int KeyMapHash_Find(uint16 key) {
  int i = keymap_hash_first[key % 255];
  while (i) {
    KeyMapHashEnt *ent = &keymap_hash[i - 1];
    if (ent->key == key)
      return ent->cmd;
    i = ent->next;
  }
  return 0;
}

static void KeyMapHash_RemoveCommand(int cmd, int size) {
  for (int bucket = 0; bucket < countof(keymap_hash_first); bucket++) {
    uint16 *p = &keymap_hash_first[bucket];
    while (*p) {
      KeyMapHashEnt *ent = &keymap_hash[*p - 1];
      if (ent->cmd >= cmd && ent->cmd < cmd + size)
        *p = ent->next;
      else
        p = &ent->next;
    }
  }
}

int FindCmdForSdlKey(SDL_Keycode code, SDL_Keymod mod) {
  if (code & ~(SDLK_SCANCODE_MASK | 0x1ff))
    return 0;
  int key = 0;
  if (code != SDLK_LALT && code != SDLK_RALT)
    key |=  mod & KMOD_ALT ? kKeyMod_Alt : 0;
  if (code != SDLK_LCTRL && code != SDLK_RCTRL)
    key |= mod & KMOD_CTRL ? kKeyMod_Ctrl : 0;
  if (code != SDLK_LSHIFT && code != SDLK_RSHIFT)
    key |= mod & KMOD_SHIFT ? kKeyMod_Shift : 0;
  key |= REMAP_SDL_KEYCODE(code);
  return KeyMapHash_Find(key);
}

static void ParseKeyArray(char *value, int cmd, int size) {
  KeyMapHash_RemoveCommand(cmd, size);
  if (cmd >= kKeys_Controls && cmd <= kKeys_Controls_Last)
    memset(g_control_keyboard, 0, sizeof(g_control_keyboard));
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    int key_with_mod = 0;
    for (;;) {
      if (StringStartsWithNoCase(s, "Shift+")) {
        key_with_mod |= kKeyMod_Shift, s += 6;
      } else if (StringStartsWithNoCase(s, "Ctrl+")) {
        key_with_mod |= kKeyMod_Ctrl, s += 5;
      } else if (StringStartsWithNoCase(s, "Alt+")) {
        key_with_mod |= kKeyMod_Alt, s += 4;
      } else {
        break;
      }
    }
    SDL_Keycode key = SDL_GetKeyFromName(s);
    if (key == SDLK_UNKNOWN) {
      fprintf(stderr, "Unknown key: '%s'\n", s);
      continue;
    }
    if (!KeyMapHash_Add(key_with_mod | REMAP_SDL_KEYCODE(key), cmd))
      fprintf(stderr, "Duplicate key: '%s'\n", s);
    if (cmd >= kKeys_Controls && cmd <= kKeys_Controls_Last)
      g_control_keyboard[cmd - kKeys_Controls] = key_with_mod | REMAP_SDL_KEYCODE(key);
  }
}

typedef struct GamepadMapEnt {
  uint32 modifiers;
  uint16 cmd, next;
} GamepadMapEnt;

static uint16 joymap_first[kGamepadBtn_Count];
static GamepadMapEnt *joymap_ents;
static int joymap_size;
static bool has_joypad_controls;
static bool has_joypad_graphics_menu;
static uint32 g_control_gamepad[12];

static int CountBits32(uint32 n) {
  int count = 0;
  for (; n != 0; count++)
    n &= (n - 1);
  return count;
}

static void GamepadMap_Add(int button, uint32 modifiers, uint16 cmd) {
  if ((joymap_size & 0xff) == 0) {
    if (joymap_size > 1000)
      Die("Too many joypad keys");
    joymap_ents = realloc(joymap_ents, sizeof(GamepadMapEnt) * (joymap_size + 64));
    if (!joymap_ents) Die("realloc failure");
  }
  uint16 *p = &joymap_first[button];
  // Insert it as early as possible but before after any entry with more modifiers.
  int cb = CountBits32(modifiers);
  while (*p && cb < CountBits32(joymap_ents[*p - 1].modifiers))
    p = &joymap_ents[*p - 1].next;
  int i = joymap_size++;
  GamepadMapEnt *ent = &joymap_ents[i];
  ent->modifiers = modifiers;
  ent->cmd = cmd;
  ent->next = *p;
  *p = i + 1;
}

int FindCmdForGamepadButton(int button, uint32 modifiers) {
  if (button < 0 || button >= kGamepadBtn_Count)
    return 0;
  GamepadMapEnt *ent;
  for(int e = joymap_first[button]; e != 0; e = ent->next) {
    ent = &joymap_ents[e - 1];
    if ((modifiers & ent->modifiers) == ent->modifiers)
      return ent->cmd;
  }
  return 0;
}

static void GamepadMap_RemoveCommands(int cmd, int size) {
  for (int button = 0; button < kGamepadBtn_Count; button++) {
    uint16 *p = &joymap_first[button];
    while (*p) {
      GamepadMapEnt *ent = &joymap_ents[*p - 1];
      if (ent->cmd >= cmd && ent->cmd < cmd + size)
        *p = ent->next;
      else
        p = &ent->next;
    }
  }
}

static int ParseGamepadButtonName(const char **value) {
  const char *s = *value;
  // Longest substring first
  static const char *const kGamepadKeyNames[] = {
    "Back", "Guide", "Start", "L3", "R3",
    "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2",
    "Lb", "Rb", "A", "B", "X", "Y"
  };
  static const uint8 kGamepadKeyIds[] = {
    kGamepadBtn_Back, kGamepadBtn_Guide, kGamepadBtn_Start, kGamepadBtn_L3, kGamepadBtn_R3,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_L2, kGamepadBtn_R2,
    kGamepadBtn_L1, kGamepadBtn_R1, kGamepadBtn_A, kGamepadBtn_B, kGamepadBtn_X, kGamepadBtn_Y,
  };
  for (size_t i = 0; i != countof(kGamepadKeyNames); i++) {
    const char *r = StringStartsWithNoCase(s, kGamepadKeyNames[i]);
    if (r) {
      *value = r;
      return kGamepadKeyIds[i];
    }
  }
  return kGamepadBtn_Invalid;
}

static const uint8 kDefaultGamepadCmds[] = {
  kGamepadBtn_DpadUp, kGamepadBtn_DpadDown, kGamepadBtn_DpadLeft, kGamepadBtn_DpadRight, kGamepadBtn_Back, kGamepadBtn_Start,
  kGamepadBtn_B, kGamepadBtn_A, kGamepadBtn_Y, kGamepadBtn_X, kGamepadBtn_L1, kGamepadBtn_R1,
};

static void ParseGamepadArray(char *value, int cmd, int size) {
  GamepadMap_RemoveCommands(cmd, size);
  if (cmd >= kKeys_Controls && cmd <= kKeys_Controls_Last)
    memset(g_control_gamepad, 0, sizeof(g_control_gamepad));
  char *s;
  int i = 0;
  for (; i < size && (s = NextDelim(&value, ',')) != NULL; i++, cmd += (cmd != 0)) {
    if (*s == 0)
      continue;
    uint32 modifiers = 0;
    const char *ss = s;
    for (;;) {
      int button = ParseGamepadButtonName(&ss);
      if (button == kGamepadBtn_Invalid) BAD: {
        fprintf(stderr, "Unknown gamepad button: '%s'\n", s);
        break;
      }
      while (*ss == ' ' || *ss == '\t') ss++;
      if (*ss == '+') {
        ss++;
        modifiers |= 1 << button;
      } else if (*ss == 0) {
        GamepadMap_Add(button, modifiers, cmd);
        if (cmd >= kKeys_Controls && cmd <= kKeys_Controls_Last)
          g_control_gamepad[cmd - kKeys_Controls] = (uint32)(button + 1) | (modifiers << 8);
        break;
      } else
        goto BAD;
    }
  }
}

static void RegisterDefaultKeys() {
  for (int i = 1; i < countof(kKeyNameId); i++) {
    if (!has_keynameid[i]) {
      int size = kKeyNameId[i].size, k = kKeyNameId[i].id;
      for (int j = 0; j < size; j++, k++)
        KeyMapHash_Add(kDefaultKbdControls[k], k);
    }
  }
  if (!has_joypad_controls) {
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
      GamepadMap_Add(kDefaultGamepadCmds[i], 0, kKeys_Controls + i);
    for (int i = 0; i < countof(kDefaultGamepadCmds); i++)
      g_control_gamepad[i] = kDefaultGamepadCmds[i] + 1;
  }
  if (!has_keynameid[1])
    for (int i = 0; i < 12; i++)
      g_control_keyboard[i] = kDefaultKbdControls[kKeys_Controls + i];
  if (!has_joypad_graphics_menu)
    GamepadMap_Add(kGamepadBtn_L3, 0, kKeys_GraphicsMenu);
  if (!FindCmdForGamepadButton(kGamepadBtn_R3, 0))
    GamepadMap_Add(kGamepadBtn_R3, 0, kKeys_FrameAdvance);
}

static int GetIniSection(const char *s) {
  if (StringEqualsNoCase(s, "[KeyMap]"))
    return 0;
  if (StringEqualsNoCase(s, "[Graphics]"))
    return 1;
  if (StringEqualsNoCase(s, "[Sound]"))
    return 2;
  if (StringEqualsNoCase(s, "[General]"))
    return 3;
  if (StringEqualsNoCase(s, "[Features]"))
    return 4;
  if (StringEqualsNoCase(s, "[GamepadMap]"))
    return 5;
  return -1;
}

bool ParseBool(const char *value, bool *result) {
  bool rv = false;
  switch (*value++ | 32) {
  case '0': if (*value == 0) break; return false;
  case 'f': if (StringEqualsNoCase(value, "alse")) break; return false;
  case 'n': if (StringEqualsNoCase(value, "o")) break; return false;
  case 'o':
    rv = (*value | 32) == 'n';
    if (StringEqualsNoCase(value, rv ? "n" : "ff")) break;
    return false;
  case '1': rv = true; if (*value == 0) break; return false;
  case 'y': rv = true; if (StringEqualsNoCase(value, "es")) break; return false;
  case 't': rv = true; if (StringEqualsNoCase(value, "rue")) break; return false;
  default: return false;
  }
  if (result) {
    *result = rv;
    return true;
  }
  return rv;
}

static bool ParseBoolBit(const char *value, uint32 *data, uint32 mask) {
  bool tmp;
  if (!ParseBool(value, &tmp))
    return false;
  *data = *data & ~mask | (tmp ? mask : 0);
  return true;
}

static bool ParseExtendedAspectRatio(const char *value, int height, uint16 *extra_pixels) {
  if (StringEqualsNoCase(value, "4:3")) {
    *extra_pixels = 0;
    return true;
  }

  char *end;
  long numerator = strtol(value, &end, 10);
  if (end == value || *end != ':')
    return false;
  char *denominator_end;
  long denominator = strtol(end + 1, &denominator_end, 10);
  if (*denominator_end != 0 || numerator <= 0 || denominator <= 0 || numerator > 10000 || denominator > 10000)
    return false;

  long width = (long)height * numerator / denominator;
  long extra = (width - 256) / 2;
  if (extra < 0)
    extra = 0;
  if (extra > kPpuMaxExtraLeftRight)
    return false;

  *extra_pixels = (uint16)extra;
  return true;
}

static bool HandleIniConfig(int section, const char *key, char *value) {
  if (section == 0) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        has_keynameid[i] = true;
        ParseKeyArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 5) {
    for (int i = 0; i < countof(kKeyNameId); i++) {
      if (StringEqualsNoCase(key, kKeyNameId[i].name)) {
        if (i == 1)
          has_joypad_controls = true;
        if (kKeyNameId[i].id == kKeys_GraphicsMenu)
          has_joypad_graphics_menu = true;
        ParseGamepadArray(value, kKeyNameId[i].id, kKeyNameId[i].size);
        return true;
      }
    }
  } else if (section == 1) {
    if (StringEqualsNoCase(key, "WindowSize")) {
      char *s;
      if (StringEqualsNoCase(value, "Auto")){
        g_config.window_width  = 0;
        g_config.window_height = 0;
        return true;
      }
      while ((s = NextDelim(&value, 'x')) != NULL) {
        if(g_config.window_width == 0) {
          g_config.window_width = atoi(s);
        } else {
          g_config.window_height = atoi(s);
          return true;
        }
      }
    } else if (StringEqualsNoCase(key, "EnhancedMode7")) {
      return ParseBool(value, &g_config.enhanced_mode7);
    } else if (StringEqualsNoCase(key, "NewRenderer")) {
      return ParseBool(value, &g_config.new_renderer);
    } else if (StringEqualsNoCase(key, "IgnoreAspectRatio")) {
      return ParseBool(value, &g_config.ignore_aspect_ratio);
    } else if (StringEqualsNoCase(key, "IntegerScaling")) {
      return ParseBool(value, &g_config.integer_scaling);
    } else if (StringEqualsNoCase(key, "Fullscreen")) {
      g_config.fullscreen = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "WindowScale")) {
      g_config.window_scale = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "OutputMethod")) {
      g_config.output_method = StringEqualsNoCase(value, "SDL-Software") ? kOutputMethod_SDLSoftware :
                               StringEqualsNoCase(value, "OpenGL") ? kOutputMethod_OpenGL : 
                               StringEqualsNoCase(value, "OpenGL ES") ? kOutputMethod_OpenGL_ES :
                                                                        kOutputMethod_SDL;
      return true;
    } else if (StringEqualsNoCase(key, "LinearFiltering")) {
      return ParseBool(value, &g_config.linear_filtering);
    } else if (StringEqualsNoCase(key, "NoSpriteLimits")) {
      return ParseBool(value, &g_config.no_sprite_limits);
    } else if (StringEqualsNoCase(key, "LinkGraphics")) {
      g_config.link_graphics = value;
      return true;
    } else if (StringEqualsNoCase(key, "Shader")) {
      g_config.shader = *value ? value : NULL;
      return true;
    } else if (StringEqualsNoCase(key, "DimFlashes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_DimFlashes);
    } else if (StringEqualsNoCase(key, "WidescreenHud")) {
      return ParseBool(value, &g_config.widescreen_hud);
    } else if (StringEqualsNoCase(key, "PixelLighting")) {
      return ParseBool(value, &g_config.pixel_lighting);
    } else if (StringEqualsNoCase(key, "EnvironmentAnimations")) {
      return ParseBool(value, &g_config.environment_animations);
    } else if (StringEqualsNoCase(key, "HeightCues")) {
      return ParseBool(value, &g_config.height_cues);
    } else if (StringEqualsNoCase(key, "HudShowHealth")) {
      return ParseBool(value, &g_config.hud_show_health);
    } else if (StringEqualsNoCase(key, "HudShowMagic")) {
      return ParseBool(value, &g_config.hud_show_magic);
    } else if (StringEqualsNoCase(key, "HudShowRubies")) {
      return ParseBool(value, &g_config.hud_show_rupees);
    } else if (StringEqualsNoCase(key, "HudShowKeys")) {
      return ParseBool(value, &g_config.hud_show_keys);
    } else if (StringEqualsNoCase(key, "HudPosition")) {
      if (StringEqualsNoCase(value, "bottom"))
        g_config.hud_position_bottom = true;
      else if (StringEqualsNoCase(value, "top"))
        g_config.hud_position_bottom = false;
      else
        return false;
      return true;
    } else if (StringEqualsNoCase(key, "AccessibilityProfile")) {
      int profile = atoi(value);
      if (profile < 0 || profile > 2) return false;
      g_config.accessibility_profile = profile;
      return true;
    } else if (StringEqualsNoCase(key, "CameraBias")) {
      int bias = atoi(value);
      g_config.camera_bias = IntMax(-32, IntMin(32, bias));
      return true;
    } else if (StringEqualsNoCase(key, "HDTexturePack")) {
      int index = atoi(value);
      if (index < -1 || index > 15) return false;
      g_config.hd_texture_pack_index = index;
      return true;
    } else if (StringEqualsNoCase(key, "HDTexturePackFolder")) {
      size_t length = strlen(value);
      if (length >= sizeof(g_config.hd_texture_pack_folder)) return false;
      for (const char *p = value; *p; p++)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-'))
          return false;
      snprintf(g_config.hd_texture_pack_folder, sizeof(g_config.hd_texture_pack_folder), "%s", value);
      return true;
    } else if (StringEqualsNoCase(key, "GhostTrail")) {
      return ParseBool(value, &g_config.ghost_trail_enabled);
    } else if (StringEqualsNoCase(key, "ChallengeTimeLimit")) {
      return ParseBool(value, &g_config.challenge_time_limit_enabled);
    } else if (StringEqualsNoCase(key, "ChallengeNoDeath")) {
      return ParseBool(value, &g_config.challenge_no_death_enabled);
    }
  } else if (section == 2) {
    if (StringEqualsNoCase(key, "EnableAudio")) {
      return ParseBool(value, &g_config.enable_audio);
    } else if (StringEqualsNoCase(key, "AudioFreq")) {
      g_config.audio_freq = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioChannels")) {
      g_config.audio_channels = (uint8)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "AudioSamples")) {
      g_config.audio_samples = (uint16)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "EnableMSU")) {
        if (StringEqualsNoCase(value, "opuz"))
        g_config.enable_msu = kMsuEnabled_Opuz;
      else if (StringEqualsNoCase(value, "deluxe"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe;
      else if (StringEqualsNoCase(value, "deluxe-opuz"))
        g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
      else 
        return ParseBool(value, (bool*)&g_config.enable_msu);
      return true;
    } else if (StringEqualsNoCase(key, "MSUPath")) {
      g_config.msu_path = value;
      return true;
    } else if (StringEqualsNoCase(key, "MSUVolume")) {
      g_config.msuvolume = atoi(value);
      return true;
    } else if (StringEqualsNoCase(key, "ResumeMSU")) {
      return ParseBool(value, &g_config.resume_msu);
    }
  } else if (section == 3) {
    if (StringEqualsNoCase(key, "Autosave")) {
      g_config.autosave = (bool)strtol(value, (char**)NULL, 10);
      return true;
    } else if (StringEqualsNoCase(key, "ExtendedAspectRatio")) {
      const char* s;
      char *ratio = NULL;
      bool extend_y = false, nospr = false, novis = false;
      while ((s = NextDelim(&value, ',')) != NULL) {
        if (StringEqualsNoCase(s, "extend_y"))
          extend_y = true;
        else if (strchr(s, ':') != NULL)
          ratio = (char *)s;
        else if (StringEqualsNoCase(s, "unchanged_sprites"))
          nospr = true;
        else if (StringEqualsNoCase(s, "no_visual_fixes"))
          novis = true;
        else
          return false;
      }
      g_config.extend_y = extend_y;
      if (ratio && !ParseExtendedAspectRatio(ratio, extend_y ? 240 : 224, &g_config.extended_aspect_ratio))
        return false;
      if (g_config.extended_aspect_ratio && !nospr)
        g_config.features0 |= kFeatures0_ExtendScreen64;
      if (g_config.extended_aspect_ratio && !novis)
        g_config.features0 |= kFeatures0_WidescreenVisualFixes;
      return true;
    } else if (StringEqualsNoCase(key, "DisplayPerfInTitle")) {
      return ParseBool(value, &g_config.display_perf_title);
    } else if (StringEqualsNoCase(key, "DisableFrameDelay")) {
      return ParseBool(value, &g_config.disable_frame_delay);
    } else if (StringEqualsNoCase(key, "Language")) {
      g_config.language = value;
      return true;
    } else if (StringEqualsNoCase(key, "Game")) {
      if (StringEqualsNoCase(value, "alttp"))
        g_config.game = "alttp";
      else if (StringEqualsNoCase(value, "parallel-worlds"))
        g_config.game = "parallel-worlds";
      else
        return false;
      return true;
    }
  } else if (section == 4) {
    if (StringEqualsNoCase(key, "ItemSwitchLR")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SwitchLR);
    } else if (StringEqualsNoCase(key, "ItemSwitchLRLimit")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SwitchLRLimit);
    } else if (StringEqualsNoCase(key, "TurnWhileDashing")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_TurnWhileDashing);
    } else if (StringEqualsNoCase(key, "MirrorToDarkworld")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MirrorToDarkworld);
    } else if (StringEqualsNoCase(key, "CollectItemsWithSword")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CollectItemsWithSword);
    } else if (StringEqualsNoCase(key, "BreakPotsWithSword")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_BreakPotsWithSword);
    } else if (StringEqualsNoCase(key, "DisableLowHealthBeep")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_DisableLowHealthBeep);
    } else if (StringEqualsNoCase(key, "SkipIntroOnKeypress")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_SkipIntroOnKeypress);
    } else if (StringEqualsNoCase(key, "ShowMaxItemsInYellow")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_ShowMaxItemsInYellow);
    } else if (StringEqualsNoCase(key, "MoreActiveBombs")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MoreActiveBombs);
    } else if (StringEqualsNoCase(key, "CarryMoreRupees")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CarryMoreRupees);
    } else if (StringEqualsNoCase(key, "MiscBugFixes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_MiscBugFixes);
    } else if (StringEqualsNoCase(key, "GameChangingBugFixes")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_GameChangingBugFixes);
    } else if (StringEqualsNoCase(key, "CancelBirdTravel")) {
      return ParseBoolBit(value, &g_config.features0, kFeatures0_CancelBirdTravel);
    }
  }
  return false;
}

static bool ParseOneConfigFile(const char *filename, int depth) {
  char *filedata = (char*)ReadWholeFile(filename, NULL), *p;
  if (!filedata)
    return false;
  
  int section = -2;
  g_config.memory_buffer = filedata;

  for (int lineno = 1; (p = NextLineStripComments(&filedata)) != NULL; lineno++) {
    if (*p == 0)
      continue; // empty line
    if (*p == '[') {
      section = GetIniSection(p);
      if (section < 0)
        fprintf(stderr, "%s:%d: Invalid .ini section %s\n", filename, lineno, p);
    } else if (*p == '!' && SkipPrefix(p + 1, "include ")) {
      char *tt = p + 8;
      char *new_filename = ReplaceFilenameWithNewPath(filename, NextPossiblyQuotedString(&tt));
      if (depth > 10 || !ParseOneConfigFile(new_filename, depth + 1))
        fprintf(stderr, "Warning: Unable to read %s\n", new_filename);
      free(new_filename);
    } else if (section == -2) {
      fprintf(stderr, "%s:%d: Expecting [section]\n", filename, lineno);
    } else {
      char *v = SplitKeyValue(p);
      if (v == NULL) {
        fprintf(stderr, "%s:%d: Expecting 'key=value'\n", filename, lineno);
        continue;
      }
      if (section >= 0 && !HandleIniConfig(section, p, v))
        fprintf(stderr, "%s:%d: Can't parse '%s'\n", filename, lineno, p);
    }
  }
  return true;
}

void ParseConfigFile(const char *filename) {
  g_config.game = "alttp";
  g_config.msuvolume = 100;  // default msu volume, 100%
  g_config.widescreen_hud = true;
  g_config.pixel_lighting = true;
  g_config.environment_animations = true;
  g_config.height_cues = true;
  g_config.hud_show_health = true;
  g_config.hud_show_magic = false;
  g_config.hud_show_rupees = true;
  g_config.hud_show_keys = true;
  g_config.hud_position_bottom = false;
  g_config.hd_texture_pack_index = -1;
  g_config.challenge_time_limit_enabled = true;
  g_config.challenge_no_death_enabled = true;

  if (filename != NULL || !ParseOneConfigFile("zelda3.user.ini", 0)) {
    if (filename == NULL)
      filename = "zelda3.ini";
    if (!ParseOneConfigFile(filename, 0))
      fprintf(stderr, "Warning: Unable to read config file %s\n", filename);
  }
  if (g_config.height_cues)
    g_config.features0 |= kFeatures0_HeightCues;
  if (g_config.accessibility_profile == 2)
    g_config.features0 |= kFeatures0_DimFlashes;
  RegisterDefaultKeys();
}

const char *Config_GetControlName(int index) {
  static const char *const names[12] = {
    "HAUT", "BAS", "GAUCHE", "DROITE", "SELECT", "START",
    "A", "B", "X", "Y", "L", "R"
  };
  return index >= 0 && index < 12 ? names[index] : "?";
}

const char *Config_GetControlKeyboardName(int index) {
  static char name[64];
  if (index < 0 || index >= 12 || !g_control_keyboard[index])
    return "--";
  uint16 value = g_control_keyboard[index];
  int key = value & 0x1ff;
  SDL_Keycode code = (value & kKeyMod_ScanCode) ? (SDLK_SCANCODE_MASK | key) : key;
  name[0] = 0;
  if (value & kKeyMod_Ctrl) strcat(name, "CTRL+");
  if (value & kKeyMod_Alt) strcat(name, "ALT+");
  if (value & kKeyMod_Shift) strcat(name, "MAJ+");
  const char *key_name = SDL_GetKeyName(code);
  if (!key_name || !*key_name) key_name = "?";
  strncat(name, key_name, sizeof(name) - strlen(name) - 1);
  return name;
}

static const char *Config_GamepadName(int button) {
  static const char *const names[kGamepadBtn_Count] = {
    "A", "B", "X", "Y", "Back", "Guide", "Start", "L3", "R3",
    "L1", "R1", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "L2", "R2"
  };
  return button >= 0 && button < kGamepadBtn_Count ? names[button] : "--";
}

const char *Config_GetControlGamepadName(int index) {
  static char name[64];
  if (index < 0 || index >= 12 || !(g_control_gamepad[index] & 0xff))
    return "--";
  uint32 value = g_control_gamepad[index];
  int button = (value & 0xff) - 1;
  uint32 modifiers = value >> 8;
  name[0] = 0;
  for (int i = 0; i < kGamepadBtn_Count; i++)
    if (modifiers & (1u << i)) {
      if (*name) strcat(name, "+");
      strcat(name, Config_GamepadName(i));
    }
  if (*name) strcat(name, "+");
  strcat(name, Config_GamepadName(button));
  return name;
}

bool Config_SetControlKeyboard(int index, SDL_Keycode code, SDL_Keymod mod) {
  if (index < 0 || index >= 12 || code == SDLK_UNKNOWN)
    return false;
  uint16 mapped = REMAP_SDL_KEYCODE(code);
  if (code != SDLK_LALT && code != SDLK_RALT && (mod & KMOD_ALT)) mapped |= kKeyMod_Alt;
  if (code != SDLK_LCTRL && code != SDLK_RCTRL && (mod & KMOD_CTRL)) mapped |= kKeyMod_Ctrl;
  if (code != SDLK_LSHIFT && code != SDLK_RSHIFT && (mod & KMOD_SHIFT)) mapped |= kKeyMod_Shift;
  int command = kKeys_Controls + index;
  int existing = KeyMapHash_Find(mapped);
  int other_index = existing - kKeys_Controls;
  if (existing && existing != command && (other_index < 0 || other_index >= 12))
    return false;
  uint16 previous = g_control_keyboard[index];
  KeyMapHash_RemoveCommand(command, 1);
  if (existing && existing != command)
    KeyMapHash_RemoveCommand(existing, 1);
  if (!KeyMapHash_Add(mapped, command))
    return false;
  g_control_keyboard[index] = mapped;
  if (existing && existing != command) {
    g_control_keyboard[other_index] = previous;
    if (previous)
      KeyMapHash_Add(previous, existing);
  }
  return true;
}

bool Config_SetControlGamepad(int index, int button) {
  if (index < 0 || index >= 12 || button < 0 || button >= kGamepadBtn_Count)
    return false;
  int command = kKeys_Controls + index;
  int existing = FindCmdForGamepadButton(button, 0);
  int other_index = existing - kKeys_Controls;
  if (existing && existing != command && (other_index < 0 || other_index >= 12))
    return false;
  uint32 previous = g_control_gamepad[index];
  GamepadMap_RemoveCommands(command, 1);
  if (existing && existing != command)
    GamepadMap_RemoveCommands(existing, 1);
  GamepadMap_Add(button, 0, command);
  g_control_gamepad[index] = button + 1;
  if (existing && existing != command) {
    g_control_gamepad[other_index] = previous;
    if (previous & 0xff)
      GamepadMap_Add((previous & 0xff) - 1, previous >> 8, existing);
  }
  return true;
}

static bool Config_WriteKeyName(FILE *f, uint16 value) {
  if (value & kKeyMod_Ctrl) fputs("Ctrl+", f);
  if (value & kKeyMod_Alt) fputs("Alt+", f);
  if (value & kKeyMod_Shift) fputs("Shift+", f);
  int key = value & 0x1ff;
  SDL_Keycode code = (value & kKeyMod_ScanCode) ? (SDLK_SCANCODE_MASK | key) : key;
  const char *name = SDL_GetKeyName(code);
  if (!name || !*name) return false;
  for (const char *p = name; *p; p++)
    fputc((*p >= 'A' && *p <= 'Z') ? (*p + ('a' - 'A')) : *p, f);
  return true;
}

static void Config_WriteGamepadName(FILE *f, uint32 value) {
  if (!(value & 0xff))
    return;
  int button = (value & 0xff) ? (value & 0xff) - 1 : -1;
  uint32 modifiers = value >> 8;
  bool first = true;
  for (int i = 0; i < kGamepadBtn_Count; i++)
    if (modifiers & (1u << i)) {
      if (!first) fputc('+', f);
      fputs(Config_GamepadName(i), f);
      first = false;
    }
  if (!first) fputc('+', f);
  fputs(Config_GamepadName(button), f);
}

bool Config_SaveUserSettings(void) {
  const char *filename = "zelda3.user.ini";
  const char *temporary = "zelda3.user.ini.tmp";
  const char *begin_marker = "# BEGIN ZELDA3 RUNTIME SETTINGS";
  const char *end_marker = "# END ZELDA3 RUNTIME SETTINGS";
  size_t old_length = 0;
  char *old_data = (char*)ReadWholeFile(filename, &old_length);
  if (!old_data) old_data = (char*)calloc(1, 1);
  if (!old_data) return false;
  char *begin = strstr(old_data, begin_marker);
  char *end = begin ? strstr(begin, end_marker) : NULL;
  if (begin && end) {
    end += strlen(end_marker);
    if (*end == '\r') end++;
    if (*end == '\n') end++;
    memmove(begin, end, strlen(end) + 1);
    old_length = strlen(old_data);
  }
  bool has_include = strstr(old_data, "!include") != NULL;
  FILE *f = fopen(temporary, "wb");
  if (!f) { free(old_data); return false; }
  if (!has_include)
    fputs("!include \"zelda3.ini\"\n\n", f);
  if (old_length)
    fwrite(old_data, 1, old_length, f);
  if (old_length && old_data[old_length - 1] != '\n') fputc('\n', f);
  fprintf(f, "%s\n[General]\nLanguage = %s\nGame = %s\n\n[Graphics]\n",
          begin_marker, g_config.language ? g_config.language : "us",
          g_config.game ? g_config.game : "alttp");
  fprintf(f, "WidescreenHud = %d\nPixelLighting = %d\nEnvironmentAnimations = %d\nHeightCues = %d\nIntegerScaling = %d\n",
          g_config.widescreen_hud, g_config.pixel_lighting, g_config.environment_animations,
          g_config.height_cues, g_config.integer_scaling);
  fprintf(f, "HudShowHealth = %d\nHudShowMagic = %d\nHudShowRubies = %d\nHudShowKeys = %d\nHudPosition = %s\n",
          g_config.hud_show_health, g_config.hud_show_magic, g_config.hud_show_rupees,
          g_config.hud_show_keys, g_config.hud_position_bottom ? "bottom" : "top");
  fprintf(f, "AccessibilityProfile = %u\nCameraBias = %d\nHDTexturePack = %d\nHDTexturePackFolder = %s\nGhostTrail = %d\n",
          (unsigned int)g_config.accessibility_profile, g_config.camera_bias, g_config.hd_texture_pack_index,
          g_config.hd_texture_pack_folder, g_config.ghost_trail_enabled);
  fprintf(f, "ChallengeTimeLimit = %d\nChallengeNoDeath = %d\n\n",
          g_config.challenge_time_limit_enabled, g_config.challenge_no_death_enabled);
  fprintf(f, "DimFlashes = %d\n", (g_config.features0 & kFeatures0_DimFlashes) != 0);
  fputs("[KeyMap]\nControls = ", f);
  for (int i = 0; i < 12; i++) {
    if (i) fputs(", ", f);
    if (g_control_keyboard[i]) Config_WriteKeyName(f, g_control_keyboard[i]);
  }
  fputs("\n\n[GamepadMap]\nControls = ", f);
  for (int i = 0; i < 12; i++) {
    if (i) fputs(", ", f);
    Config_WriteGamepadName(f, g_control_gamepad[i]);
  }
  fprintf(f, "\n%s\n", end_marker);
  bool ok = ferror(f) == 0;
  if (fclose(f) != 0) ok = false;
#ifdef _WIN32
  if (ok)
    ok = MoveFileExA(temporary, filename, 0x1 | 0x8) != 0;
#else
  if (ok)
    ok = rename(temporary, filename) == 0;
#endif
  if (!ok) remove(temporary);
  free(old_data);
  return ok;
}
