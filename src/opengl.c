#include "third_party/gl_core/gl_core_3_1.h"
#include <SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "types.h"
#include "util.h"
#include "glsl_shader.h"
#include "config.h"
#include "snes/ppu.h"

#define CODE(...) #__VA_ARGS__

static SDL_Window *g_window;
static uint8 *g_screen_buffer;
static size_t g_screen_buffer_size;
static int g_draw_width, g_draw_height;
static unsigned int g_program, g_VAO;
static GlTextureWithSize g_texture;
static GlslShader *g_glsl_shader;
static bool g_glsl_shader_enabled;
static bool g_startup_presentation_active, g_startup_saved_shader_enabled;
static int g_selected_shader_param;
static char g_shader_status[128];
static bool g_opengl_es;
static unsigned int g_mode7_program, g_mode7_fbo, g_mode7_texture;
static unsigned int g_mode7_vram_texture, g_mode7_line_texture;
static unsigned int g_mode7_sprite_texture, g_mode7_color_texture;
static int g_mode7_texture_width, g_mode7_texture_height;
static Ppu *g_mode7_frame;
static GLint g_mode7_uniform_source, g_mode7_uniform_vram, g_mode7_uniform_lines;
static GLint g_mode7_uniform_sprites, g_mode7_uniform_colors;
static GLint g_mode7_uniform_height, g_mode7_uniform_width;

static const char *kMode7VertexShader = "#version 330 core\n" CODE(
  layout(location = 0) in vec3 aPos;
  layout(location = 1) in vec2 aTexCoord;
  out vec2 TexCoord;
  void main() { gl_Position = vec4(aPos, 1.0); TexCoord = aTexCoord; }
);

static const char *kMode7FragmentShader = "#version 330 core\n" CODE(
  in vec2 TexCoord;
  out vec4 FragColor;
  uniform sampler2D texture1;
  uniform usampler2D vramTexture;
  uniform isampler2D lineTexture;
  uniform usampler2D spriteTexture;
  uniform usampler2D colorTexture;
  uniform int outputHeight;
  uniform int logicalWidth;
  void main() {
    ivec2 outPos = ivec2(gl_FragCoord.xy);
    outPos.y = outputHeight - 1 - outPos.y;
    int lineIndex = outPos.y / 4;
    ivec4 m0v = texelFetch(lineTexture, ivec2(0, lineIndex), 0);
    ivec4 matrix = texelFetch(lineTexture, ivec2(1, lineIndex), 0);
    ivec4 bounds = texelFetch(lineTexture, ivec2(2, lineIndex), 0);
    ivec4 flags = texelFetch(lineTexture, ivec2(3, lineIndex), 0);
    if (flags.y == 2) {
      FragColor = vec4(0.0, 0.0, 0.0, 1.0);
      return;
    }
    if (flags.y == 0) {
      FragColor = texelFetch(texture1, outPos, 0);
      return;
    }
    int logicalX = outPos.x / 4;
    int startX = (logicalWidth - 256) / 2 - bounds.z;
    int drawWidth = 256 + bounds.z + bounds.w;
    int x = logicalX - startX;
    if (x < 0 || x >= drawWidth) {
      FragColor = vec4(0.0, 0.0, 0.0, 1.0);
      return;
    }
    uint m0 = uint(m0v[outPos.y & 3]);
    uint m1 = uint(matrix.x), m2 = uint(matrix.y);
    uint clippedH = uint(bounds.x), clippedV = uint(bounds.y);
    uint xCenter = uint(matrix.z), yCenter = uint(matrix.w);
    uint y = uint(lineIndex + 1);
    uint xpos = m0 * clippedH + m1 * (clippedV + y) + (xCenter << 20);
    uint ypos = m2 * clippedH + m0 * (clippedV + y) + (yCenter << 20);
    xpos -= (m0 + m1) >> 1;
    ypos -= (m2 + m0) >> 1;
    uint xcur = (xpos << 2) + uint(outPos.y & 3) * m1;
    uint ycur = (ypos << 2) + uint(outPos.y & 3) * m0;
    xcur -= uint(bounds.z) * 4u * m0;
    ycur -= uint(bounds.z) * 4u * m2;
    uint tileMap = texelFetch(vramTexture, ivec2(int((xcur >> 25) & 127u), int((ycur >> 25) & 127u)), 0).r;
    int pixelIndex = int(tileMap & 255u) * 64 + int((ycur >> 22) & 7u) * 8 + int((xcur >> 22) & 7u);
    uint pixel = texelFetch(vramTexture, ivec2(pixelIndex & 127, pixelIndex >> 7), 0).r >> 8;
    if ((xcur & 0x80000000u) != 0u)
      pixel = 0u;
    uint color = texelFetch(colorTexture, ivec2(int(pixel), 0), 0).r;
    if (flags.x != 0)
      color = (color & 0x00fefefeu) >> 1;
    uint sprite = texelFetch(spriteTexture, ivec2(logicalX, lineIndex), 0).r;
    if (sprite != 0u)
      color = texelFetch(colorTexture, ivec2(int(sprite), 0), 0).r;
    vec3 rgb = vec3(float((color >> 16) & 255u), float((color >> 8) & 255u), float(color & 255u)) / 255.0;
    FragColor = vec4(rgb, 1.0);
  }
);

static unsigned int OpenGLRenderer_CompileMode7Shader(unsigned int type, const char *source) {
  unsigned int shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, NULL);
  glCompileShader(shader);
  GLint success = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char log[512];
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    fprintf(stderr, "Enhanced Mode 7 shader compile failed: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static bool OpenGLRenderer_InitMode7Gpu() {
  if (g_opengl_es)
    return false;
  unsigned int vs = OpenGLRenderer_CompileMode7Shader(GL_VERTEX_SHADER, kMode7VertexShader);
  unsigned int fs = OpenGLRenderer_CompileMode7Shader(GL_FRAGMENT_SHADER, kMode7FragmentShader);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }
  g_mode7_program = glCreateProgram();
  glAttachShader(g_mode7_program, vs);
  glAttachShader(g_mode7_program, fs);
  glLinkProgram(g_mode7_program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint linked = GL_FALSE;
  glGetProgramiv(g_mode7_program, GL_LINK_STATUS, &linked);
  if (!linked) {
    char log[512];
    glGetProgramInfoLog(g_mode7_program, sizeof(log), NULL, log);
    fprintf(stderr, "Enhanced Mode 7 shader link failed: %s\n", log);
    glDeleteProgram(g_mode7_program);
    g_mode7_program = 0;
    return false;
  }
  g_mode7_uniform_source = glGetUniformLocation(g_mode7_program, "texture1");
  g_mode7_uniform_vram = glGetUniformLocation(g_mode7_program, "vramTexture");
  g_mode7_uniform_lines = glGetUniformLocation(g_mode7_program, "lineTexture");
  g_mode7_uniform_sprites = glGetUniformLocation(g_mode7_program, "spriteTexture");
  g_mode7_uniform_colors = glGetUniformLocation(g_mode7_program, "colorTexture");
  g_mode7_uniform_height = glGetUniformLocation(g_mode7_program, "outputHeight");
  g_mode7_uniform_width = glGetUniformLocation(g_mode7_program, "logicalWidth");

  glGenFramebuffers(1, &g_mode7_fbo);
  glGenTextures(1, &g_mode7_texture);
  glBindTexture(GL_TEXTURE_2D, g_mode7_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glBindFramebuffer(GL_FRAMEBUFFER, g_mode7_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_mode7_texture, 0);
  bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!complete) {
    fprintf(stderr, "Enhanced Mode 7 framebuffer is unavailable; using CPU rendering.\n");
    glDeleteTextures(1, &g_mode7_texture);
    glDeleteFramebuffers(1, &g_mode7_fbo);
    glDeleteProgram(g_mode7_program);
    g_mode7_texture = g_mode7_fbo = g_mode7_program = 0;
    return false;
  }

  glGenTextures(1, &g_mode7_vram_texture);
  glBindTexture(GL_TEXTURE_2D, g_mode7_vram_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 128, 128, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, NULL);

  glGenTextures(1, &g_mode7_line_texture);
  glBindTexture(GL_TEXTURE_2D, g_mode7_line_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32I, 4, 240, 0, GL_RGBA_INTEGER, GL_INT, NULL);

  glGenTextures(1, &g_mode7_sprite_texture);
  glBindTexture(GL_TEXTURE_2D, g_mode7_sprite_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, kPpuXPixels, 240, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);

  glGenTextures(1, &g_mode7_color_texture);
  glBindTexture(GL_TEXTURE_2D, g_mode7_color_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 256, 1, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

static bool OpenGLRenderer_RenderMode7Gpu(Ppu *ppu) {
  if (!ppu || !g_mode7_program || ppu->mode7GpuLineCount == 0)
    return false;

  int32_t line_data[240 * 4 * 4] = { 0 };
  for (int y = 0; y < 240; y++) {
    const PpuMode7GpuLine *line = &ppu->mode7GpuLines[y];
    int32_t *row = &line_data[y * 16];
    memcpy(row, line->m0, sizeof(line->m0));
    row[4] = line->m1; row[5] = line->m2; row[6] = line->xCenter; row[7] = line->yCenter;
    row[8] = line->clippedH; row[9] = line->clippedV;
    row[10] = line->extraLeftCur; row[11] = line->extraRightCur;
    row[12] = line->halfColor; row[13] = line->active;
  }

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, g_mode7_vram_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RED_INTEGER, GL_UNSIGNED_SHORT, ppu->vram);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, g_mode7_line_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 240, GL_RGBA_INTEGER, GL_INT, line_data);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, g_mode7_sprite_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kPpuXPixels, 240, GL_RED_INTEGER, GL_UNSIGNED_BYTE, ppu->mode7GpuSprites);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, g_mode7_color_texture);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, ppu->colorMapRgb);

  glActiveTexture(GL_TEXTURE0);
  if (g_mode7_texture_width != g_draw_width || g_mode7_texture_height != g_draw_height) {
    glBindTexture(GL_TEXTURE_2D, g_mode7_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g_draw_width, g_draw_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    g_mode7_texture_width = g_draw_width;
    g_mode7_texture_height = g_draw_height;
  }

  GLint previous_framebuffer;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, g_mode7_fbo);
  glViewport(0, 0, g_draw_width, g_draw_height);
  glUseProgram(g_mode7_program);
  glUniform1i(g_mode7_uniform_source, 0);
  glUniform1i(g_mode7_uniform_vram, 1);
  glUniform1i(g_mode7_uniform_lines, 2);
  glUniform1i(g_mode7_uniform_sprites, 3);
  glUniform1i(g_mode7_uniform_colors, 4);
  glUniform1i(g_mode7_uniform_height, g_draw_height);
  glUniform1i(g_mode7_uniform_width, g_draw_width / 4);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, g_mode7_vram_texture);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, g_mode7_line_texture);
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, g_mode7_sprite_texture);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_2D, g_mode7_color_texture);
  glBindVertexArray(g_VAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
  glUseProgram(0);
  glActiveTexture(GL_TEXTURE0);
  glBindFramebuffer(GL_FRAMEBUFFER, previous_framebuffer);
  return true;
}

static void GL_APIENTRY MessageCallback(GLenum source,
                GLenum type,
                GLuint id,
                GLenum severity,
                GLsizei length,
                const GLchar *message,
                const void *userParam) {
  if (type == GL_DEBUG_TYPE_OTHER)
    return;

  fprintf(stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
          (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
          type, severity, message);
  if (type == GL_DEBUG_TYPE_ERROR)
    Die("OpenGL error!\n");
}

static bool OpenGLRenderer_Init(SDL_Window *window) {
  g_window = window;
  SDL_GLContext context = SDL_GL_CreateContext(window);
  (void)context;

  SDL_GL_SetSwapInterval(1);
  ogl_LoadFunctions();

  if (!g_opengl_es) {
    if (!ogl_IsVersionGEQ(3, 3))
      Die("You need OpenGL 3.3");
  } else {
    int majorVersion = 0, minorVersion = 0;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &majorVersion);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minorVersion);
    if (majorVersion < 3)
      Die("You need OpenGL ES 3.0");

  }

  if (kDebugFlag) {
    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(MessageCallback, 0);
  }

  glGenTextures(1, &g_texture.gl_texture);

  static const float kVertices[] = {
    // positions          // texture coords
    -1.0f,  1.0f, 0.0f,   0.0f, 0.0f, // top left
    -1.0f, -1.0f, 0.0f,   0.0f, 1.0f, // bottom left
     1.0f,  1.0f, 0.0f,   1.0f, 0.0f, // top right
     1.0f, -1.0f, 0.0f,   1.0f, 1.0f,  // bottom right
  };

  // create a vertex buffer object
  unsigned int vbo;
  glGenBuffers(1, &vbo);

  // vertex array object
  glGenVertexArrays(1, &g_VAO);
  // 1. bind Vertex Array Object
  glBindVertexArray(g_VAO);
  // 2. copy our vertices array in a buffer for OpenGL to use
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(kVertices), kVertices, GL_STATIC_DRAW);
  // position attribute
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);
  // texture coord attribute
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)(3 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // vertex shader
  const GLchar *vs_code_core = "#version 330 core\n" CODE(
  layout(location = 0) in vec3 aPos;
  layout(location = 1) in vec2 aTexCoord;
  out vec2 TexCoord;
  void main() {
    gl_Position = vec4(aPos, 1.0);
    TexCoord = vec2(aTexCoord.x, aTexCoord.y);
  }
);

  const GLchar *vs_code_es = "#version 300 es\n" CODE(
  layout(location = 0) in vec3 aPos;
  layout(location = 1) in vec2 aTexCoord;
  out vec2 TexCoord;
  void main() {
    gl_Position = vec4(aPos, 1.0);
    TexCoord = vec2(aTexCoord.x, aTexCoord.y);
  }
);

  const GLchar *vs_code = g_opengl_es ? vs_code_es : vs_code_core;
  unsigned int vs = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vs, 1, &vs_code, NULL);
  glCompileShader(vs);

  int success;
  char infolog[512];
  glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // fragment shader
  const GLchar *fs_code_core = "#version 330 core\n" CODE(
  out vec4 FragColor;
  in vec2 TexCoord;
  // texture samplers
  uniform sampler2D texture1;
  void main() {
    FragColor = texture(texture1, TexCoord);
  }
);

  const GLchar *fs_code_es = "#version 300 es\n" CODE(
  precision mediump float;
  out vec4 FragColor;
  in vec2 TexCoord;
  // texture samplers
  uniform sampler2D texture1;
  void main() {
    FragColor = texture(texture1, TexCoord);
  }
);


  const GLchar *fs_code = g_opengl_es ? fs_code_es : fs_code_core;
  unsigned int fs = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fs, 1, &fs_code, NULL);
  glCompileShader(fs);

  glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fs, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  // create program
  int program = g_program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glGetProgramiv(program, GL_LINK_STATUS, &success);

  if (!success) {
    glGetProgramInfoLog(program, 512, NULL, infolog);
    printf("%s\n", infolog);
  }

  if (g_config.shader)
    g_glsl_shader = GlslShader_CreateFromFile(g_config.shader, g_opengl_es);
  g_glsl_shader_enabled = g_glsl_shader != NULL;
  OpenGLRenderer_InitMode7Gpu();
  
  return true;
}

static void OpenGLRenderer_Destroy() {
  if (g_glsl_shader) {
    GlslShader_Destroy(g_glsl_shader);
    g_glsl_shader = NULL;
  }
  glDeleteProgram(g_mode7_program);
  glDeleteTextures(1, &g_mode7_texture);
  glDeleteTextures(1, &g_mode7_vram_texture);
  glDeleteTextures(1, &g_mode7_line_texture);
  glDeleteTextures(1, &g_mode7_sprite_texture);
  glDeleteTextures(1, &g_mode7_color_texture);
  glDeleteFramebuffers(1, &g_mode7_fbo);
  g_mode7_program = g_mode7_texture = g_mode7_fbo = 0;
  g_mode7_vram_texture = g_mode7_line_texture = 0;
  g_mode7_sprite_texture = g_mode7_color_texture = 0;
}

bool OpenGLRenderer_CanRenderMode7Gpu() {
  return g_mode7_program != 0;
}

void OpenGLRenderer_SetMode7Frame(Ppu *ppu) {
  g_mode7_frame = ppu && ppu->mode7GpuLineCount ? ppu : NULL;
}

static int OpenGLRenderer_GetShaderParamCount() {
  int count = 0;
  if (g_glsl_shader)
    for (GlslParam *param = g_glsl_shader->first_param; param; param = param->next)
      count++;
  return count;
}

static GlslParam *OpenGLRenderer_GetShaderParam(int index) {
  if (!g_glsl_shader)
    return NULL;
  GlslParam *param = g_glsl_shader->first_param;
  while (param && index-- > 0)
    param = param->next;
  return param;
}

const char *OpenGLRenderer_HandleShaderCommand(uint32 command) {
  if (!g_window || (g_config.output_method != kOutputMethod_OpenGL && g_config.output_method != kOutputMethod_OpenGL_ES))
    return "OPENGL BACKEND REQUIRED";

  switch (command) {
  case kKeys_ReloadShader: {
    if (!g_config.shader) {
      snprintf(g_shader_status, sizeof(g_shader_status), "SHADER PATH EMPTY");
      break;
    }
    GlslShader *shader = GlslShader_CreateFromFile(g_config.shader, g_opengl_es);
    if (!shader) {
      snprintf(g_shader_status, sizeof(g_shader_status), "SHADER LOAD FAILED");
      break;
    }
    if (g_glsl_shader)
      GlslShader_Destroy(g_glsl_shader);
    g_glsl_shader = shader;
    g_glsl_shader_enabled = true;
    g_selected_shader_param = 0;
    snprintf(g_shader_status, sizeof(g_shader_status), "SHADER RELOADED");
    break;
  }
  case kKeys_ToggleShader:
    if (!g_glsl_shader) {
      snprintf(g_shader_status, sizeof(g_shader_status), "NO SHADER LOADED");
      break;
    }
    g_glsl_shader_enabled = !g_glsl_shader_enabled;
    snprintf(g_shader_status, sizeof(g_shader_status), "SHADER %s", g_glsl_shader_enabled ? "ON" : "OFF");
    break;
  case kKeys_ShaderParamPrevious:
  case kKeys_ShaderParamNext: {
    int count = OpenGLRenderer_GetShaderParamCount();
    if (count == 0) {
      snprintf(g_shader_status, sizeof(g_shader_status), "SHADER HAS NO PARAMETERS");
      break;
    }
    g_selected_shader_param = (g_selected_shader_param + count + (command == kKeys_ShaderParamNext ? 1 : -1)) % count;
    GlslParam *param = OpenGLRenderer_GetShaderParam(g_selected_shader_param);
    snprintf(g_shader_status, sizeof(g_shader_status), "PARAM %.32s %.3f", param->id, param->value);
    break;
  }
  case kKeys_ShaderParamDecrease:
  case kKeys_ShaderParamIncrease: {
    GlslParam *param = OpenGLRenderer_GetShaderParam(g_selected_shader_param);
    if (!param) {
      snprintf(g_shader_status, sizeof(g_shader_status), "SHADER HAS NO PARAMETERS");
      break;
    }
    float step = param->step > 0.0f ? param->step : (param->max - param->min) / 100.0f;
    if (step > 0.0f) {
      param->value += command == kKeys_ShaderParamIncrease ? step : -step;
      if (param->value < param->min)
        param->value = param->min;
      if (param->value > param->max)
        param->value = param->max;
    }
    snprintf(g_shader_status, sizeof(g_shader_status), "PARAM %.32s %.3f", param->id, param->value);
    break;
  }
  default:
    snprintf(g_shader_status, sizeof(g_shader_status), "UNKNOWN SHADER COMMAND");
    break;
  }
  return g_shader_status;
}

static void OpenGLRenderer_BeginDraw(int width, int height, uint8 **pixels, int *pitch) {
  *pixels = NULL;
  *pitch = 0;
  if (width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / (size_t)height / 4)
    return;
  size_t size = (size_t)width * height;

  if (size > g_screen_buffer_size) {
    uint8 *buffer = malloc(size * 4);
    if (!buffer)
      return;
    free(g_screen_buffer);
    g_screen_buffer = buffer;
    g_screen_buffer_size = size;
  }

  g_draw_width = width;
  g_draw_height = height;
  *pixels = g_screen_buffer;
  *pitch = width * 4;
}

void OpenGLRenderer_SetStartupPresentation(bool enabled) {
  if (enabled) {
    if (!g_startup_presentation_active) {
      g_startup_saved_shader_enabled = g_glsl_shader_enabled;
      g_startup_presentation_active = true;
    }
    g_glsl_shader_enabled = false;
  } else if (g_startup_presentation_active) {
    g_glsl_shader_enabled = g_startup_saved_shader_enabled;
    g_startup_presentation_active = false;
  }
}

static void OpenGLRenderer_EndDraw() {
  int drawable_width, drawable_height;

  SDL_GL_GetDrawableSize(g_window, &drawable_width, &drawable_height);
  
  int viewport_width = drawable_width, viewport_height = drawable_height;

  if (!g_config.ignore_aspect_ratio) {
    if (viewport_width * g_draw_height < viewport_height * g_draw_width)
      viewport_height = viewport_width * g_draw_height / g_draw_width;  // limit height
    else
      viewport_width = viewport_height * g_draw_width / g_draw_height;  // limit width
  }

  if (g_config.integer_scaling && !g_config.ignore_aspect_ratio) {
    int scale = IntMin(drawable_width / g_draw_width, drawable_height / g_draw_height);
    if (scale > 0) {
      viewport_width = g_draw_width * scale;
      viewport_height = g_draw_height * scale;
    }
  }

  int viewport_x = (drawable_width - viewport_width) >> 1;
  int viewport_y = (drawable_height - viewport_height) >> 1;

  glBindTexture(GL_TEXTURE_2D, g_texture.gl_texture);
  if (g_draw_width == g_texture.width && g_draw_height == g_texture.height) {
    if (!g_opengl_es)
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_draw_width, g_draw_height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_screen_buffer);
    else
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_draw_width, g_draw_height, GL_BGRA, GL_UNSIGNED_BYTE, g_screen_buffer);
  } else {
    g_texture.width = g_draw_width;
    g_texture.height = g_draw_height;
    if (!g_opengl_es)
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_draw_width, g_draw_height, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, g_screen_buffer);
    else
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_draw_width, g_draw_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, g_screen_buffer);
  }

  bool mode7_gpu = OpenGLRenderer_RenderMode7Gpu(g_mode7_frame);
  g_mode7_frame = NULL;

  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);

  GlTextureWithSize mode7_texture = { g_mode7_texture, (uint16)g_draw_width, (uint16)g_draw_height };
  GlTextureWithSize *draw_texture = mode7_gpu ? &mode7_texture : &g_texture;
  if (g_glsl_shader == NULL || !g_glsl_shader_enabled) {
    glViewport(viewport_x, viewport_y, viewport_width, viewport_height);
    glUseProgram(g_program);
    int filter = g_config.linear_filtering && !g_config.integer_scaling ? GL_LINEAR : GL_NEAREST;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, draw_texture->gl_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindVertexArray(g_VAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  } else {
    GlslShader_Render(g_glsl_shader, draw_texture, viewport_x, viewport_y, viewport_width, viewport_height);
  }

  SDL_GL_SwapWindow(g_window);
}

static const struct RendererFuncs kOpenGLRendererFuncs = {
  &OpenGLRenderer_Init,
  &OpenGLRenderer_Destroy,
  &OpenGLRenderer_BeginDraw,
  &OpenGLRenderer_EndDraw,
};

void OpenGLRenderer_Create(struct RendererFuncs *funcs, bool use_opengl_es) {
  g_opengl_es = use_opengl_es;
  if (!g_opengl_es) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  } else {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  }
  *funcs = kOpenGLRendererFuncs;
}

