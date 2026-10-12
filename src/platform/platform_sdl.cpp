// platform_sdl.cpp — backend SDL2 com renderização 240x320 escalada, áudio SDL_mixer e mapeamento de teclado
#include "platform.h"
#include "updater.h"
#include "cloud_save.h"
#include "midi_synth.h"
#include "midp/midp.h"
#include "vm/vm.h"

#if defined(__has_include)
  #if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #else
    #include <SDL.h>
  #endif
  #if __has_include(<SDL2/SDL_mixer.h>)
    #include <SDL2/SDL_mixer.h>
  #elif __has_include(<SDL_mixer.h>)
    #include <SDL_mixer.h>
  #endif
#else
  #include <SDL.h>
  #include <SDL_mixer.h>
#endif
#include <cstdio>
#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <SDL_syswm.h>
#endif

#ifdef __SWITCH__
#if defined(__has_include)
  #if __has_include(<switch.h>)
    #include <switch.h>
  #endif
#else
  #include <switch.h>
#endif
#endif

#if defined(__SWITCH__) || defined(__linux__) || defined(__unix__)
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace hl {

static SDL_Window* s_window = nullptr;
static SDL_Renderer* s_renderer = nullptr;
static SDL_Texture* s_screenTexture = nullptr;
static bool s_quit = false;
static std::string s_executablePath = "";

void Platform::setExecutablePath(const std::string& path) { s_executablePath = path; }
std::string Platform::getExecutablePath() { return s_executablePath; }
void Platform::requestQuit() { s_quit = true; }

static std::vector<SDL_GameController*> s_controllers;
static bool s_ltHeld = false;
static bool s_rtHeld = false;

static int mapKey(SDL_Keycode k) {
  switch (k) {
    case SDLK_UP: case SDLK_w: return -1;
    case SDLK_DOWN: case SDLK_s: return -2;
    case SDLK_LEFT: case SDLK_a: return -3;
    case SDLK_RIGHT: case SDLK_d: return -4;
    case SDLK_RETURN: case SDLK_SPACE: case SDLK_j: case SDLK_z: return 53; // '5' / Action
    case SDLK_k: case SDLK_x: return 55; // '7'
    case SDLK_l: case SDLK_c: return 57; // '9'
    case SDLK_u: case SDLK_q: return 49; // '1'
    case SDLK_i: case SDLK_e: return 51; // '3'
    case SDLK_m: case SDLK_o: case SDLK_r: return 48; // '0' / Mapa
    case SDLK_ESCAPE: case SDLK_BACKSPACE: case SDLK_TAB: case SDLK_F1: return -8; // Menu Principal / Cancelar / Fechar (CLR)
    case SDLK_F2: return -7; // RSK

    // Alternância de Poção / Item Rápido
    case SDLK_LEFTBRACKET: case SDLK_COMMA: return -101; // Poção Anterior (Esquerda)
    case SDLK_RIGHTBRACKET: case SDLK_PERIOD: return 35; // Próxima Poção (Direita)

    // Teclado numérico
    case SDLK_KP_0: case SDLK_0: return 48;
    case SDLK_KP_1: case SDLK_1: return 49;
    case SDLK_KP_2: case SDLK_2: return 50;
    case SDLK_KP_3: case SDLK_3: return 51;
    case SDLK_KP_4: case SDLK_4: return 52;
    case SDLK_KP_5: case SDLK_5: return 53;
    case SDLK_KP_6: case SDLK_6: return 54;
    case SDLK_KP_7: case SDLK_7: return 55;
    case SDLK_KP_8: case SDLK_8: return 56;
    case SDLK_KP_9: case SDLK_9: return 57;
    case SDLK_KP_MULTIPLY: case SDLK_ASTERISK: return 42; // '*'
    case SDLK_KP_HASH: case SDLK_HASH: return 35; // '#'
    default: return 0;
  }
}

static int mapControllerButton(Uint8 btn) {
  switch (btn) {
    // D-Pad
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return -1;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return -2;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return -3;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return -4;

    // Botões de Ação Frontais
#ifdef __SWITCH__
    // Switch: o SDL usa layout posicional Xbox (A = botão de baixo = "B" do Switch).
    // Trocamos para seguir os rótulos físicos do Switch: A (direita) confirma, B (baixo) cancela.
    case SDL_CONTROLLER_BUTTON_B: return 53; // Switch A -> Atacar / Interagir / Confirmar ('5')
    case SDL_CONTROLLER_BUTTON_A: return -7; // Switch B -> Status / Cancelar (RSK)
    case SDL_CONTROLLER_BUTTON_Y: return 49; // Switch X (topo) -> Ataque 1 do Guardião ('1')
    case SDL_CONTROLLER_BUTTON_X: return 51; // Switch Y (esquerda) -> Ataque 2 do Guardião ('3')
#else
    case SDL_CONTROLLER_BUTTON_A: return 53; // A (Xbox) / X (PS) -> Atacar com Arma / Interagir / Confirmar ('5')
    case SDL_CONTROLLER_BUTTON_B: return -7; // B (Xbox) / O (PS) -> Status / Cancelar (RSK)
    case SDL_CONTROLLER_BUTTON_X: return 49; // X (Xbox) / Quad (PS) -> Ataque 1 do Guardião ('1')
    case SDL_CONTROLLER_BUTTON_Y: return 51; // Y (Xbox) / Tri (PS) -> Ataque 2 do Guardião ('3')
#endif

    // Botões de Ombro (Shoulders)
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return 55;  // L1 -> Ataque Secundário / Habilidade ('7')
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return 57; // R1 -> Usar Poção / Item Rápido ('9')

    // Menus de Sistema e Mapa
    case SDL_CONTROLLER_BUTTON_START: return -8; // Start / + -> Menu Principal / Inventário (CLR)
    case SDL_CONTROLLER_BUTTON_BACK: return 48;  // Back / Select / Touchpad / - -> Abrir/Fechar Minimapa ('0')

    // Cliques dos Analógicos
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return 48;  // L3 -> Abrir Minimapa da Região ('0')
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return 35; // R3 -> Próxima Poção ('#')

    default: return 0;
  }
}

// =============================================================
// Passo 7: Molduras Temáticas (Bezels), Aspect Ratio e FPS Limiter
// =============================================================
enum BezelMode {
  BEZEL_SOLTIA = 0,
  BEZEL_SLATE = 1,
  BEZEL_BLACK = 2,
  BEZEL_COUNT = 3
};

enum AspectMode {
  ASPECT_AUTO = 0,       // Auto: dinâmico pela orientação da tela (Horizontal -> 16:9, Vertical -> 3:4)
  ASPECT_ORIGINAL = 1,   // 3:4 Original com Molduras de Soltia
  ASPECT_WIDESCREEN = 2, // 16:9 True Widescreen (Expansão Real de Viewport)
  ASPECT_COUNT = 3
};

static BezelMode s_bezelMode = BEZEL_SOLTIA;
static int s_targetFps = 15; // 15 (padrão nostalgia J2ME) ou 30 (turbo fluido)
static AspectMode s_aspectMode = ASPECT_AUTO;
static bool s_isFullscreen = false;
static int s_savedWinW = 0;
static int s_savedWinH = 0;
static int s_screenRotation = 0;
static uint64_t s_frameStartCounter = 0;
static VM* s_lastVm = nullptr;

static int determineTargetWidth(AspectMode mode) {
  if (mode == ASPECT_ORIGINAL) {
    return SCREEN_W_ORIGINAL;
  }
  if (mode == ASPECT_WIDESCREEN) {
    return SCREEN_W_WIDESCREEN;
  }
  // ASPECT_AUTO:
  int winW = 0, winH = 0;
  if (s_window) {
    SDL_GetWindowSize(s_window, &winW, &winH);
  }
#ifdef __SWITCH__
  if (s_screenRotation != 0) {
    winW = 720;
    winH = 1280;
  }
#endif
  if (winW <= 0 || winH <= 0) {
    return SCREEN_W_WIDESCREEN;
  }
  return (winW >= winH) ? SCREEN_W_WIDESCREEN : SCREEN_W_ORIGINAL;
}

static void setVmStaticInt(VM& vm, const std::string& className, const std::string& fieldName, int32_t val) {
  auto it = vm.classes.find(className);
  if (it == vm.classes.end()) return;
  ClassInfo* cls = it->second;
  FieldInfo* targetField = nullptr;

  std::vector<std::string> candidateNames = { fieldName };
  if (fieldName.rfind("var_int_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(8)); // "var_int_a" -> "a"
  } else if (fieldName.rfind("var_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(4));
  }

  for (const auto& name : candidateNames) {
    for (auto& f : cls->fields) {
      if (f.name == name && f.desc == "I" && f.isStatic) {
        targetField = &f;
        break;
      }
    }
    if (targetField) break;
  }

  if (targetField && targetField->isStatic && targetField->index >= 0 && targetField->index < (int)cls->statics.size()) {
    int32_t oldVal = cls->statics[targetField->index].i;
    cls->statics[targetField->index].i = val;
    printf("[Viewport] %s.%s:I (%d -> %d)\n", className.c_str(), targetField->name.c_str(), oldVal, val);
  }
}

static int32_t getVmStaticInt(VM& vm, const std::string& className, const std::string& fieldName, int32_t defVal = 0) {
  auto it = vm.classes.find(className);
  if (it == vm.classes.end()) return defVal;
  ClassInfo* cls = it->second;
  FieldInfo* targetField = nullptr;

  std::vector<std::string> candidateNames = { fieldName };
  if (fieldName.rfind("var_int_", 0) == 0) candidateNames.push_back(fieldName.substr(8));
  else if (fieldName.rfind("var_", 0) == 0) candidateNames.push_back(fieldName.substr(4));

  for (const auto& name : candidateNames) {
    for (auto& f : cls->fields) {
      if (f.name == name && f.desc == "I" && f.isStatic) {
        targetField = &f;
        break;
      }
    }
    if (targetField) break;
  }
  if (targetField && targetField->isStatic && targetField->index >= 0 && targetField->index < (int)cls->statics.size()) {
    return cls->statics[targetField->index].i;
  }
  return defVal;
}

static void setVmInstanceBool(Object* obj, const std::string& fieldName, bool val) {
  if (!obj || obj->kind != K_INST || !obj->cls) return;
  Instance* inst = static_cast<Instance*>(obj);
  ClassInfo* cls = obj->cls;
  FieldInfo* targetField = nullptr;

  std::vector<std::string> candidateNames = { fieldName };
  if (fieldName.rfind("var_boolean_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(12)); // "var_boolean_e" -> "e"
  } else if (fieldName.rfind("var_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(4));
  }

  // Percorre toda a hierarquia de classes (incluindo superclasses como cb)
  for (ClassInfo* cur = cls; cur; cur = cur->super) {
    for (const auto& name : candidateNames) {
      for (auto& f : cur->fields) {
        if (f.name == name && f.desc == "Z" && !f.isStatic) {
          targetField = &f;
          break;
        }
      }
      if (targetField) break;
    }
    if (targetField) break;
  }

  if (targetField && !targetField->isStatic && targetField->index >= 0 && targetField->index < (int)inst->f.size()) {
    inst->f[targetField->index].i = val ? 1 : 0;
  }
}

static void setVmInstanceShort(Object* obj, const std::string& fieldName, int16_t val) {
  if (!obj || obj->kind != K_INST || !obj->cls) return;
  Instance* inst = static_cast<Instance*>(obj);
  ClassInfo* cls = obj->cls;
  FieldInfo* targetField = nullptr;

  std::vector<std::string> candidateNames = { fieldName };
  if (fieldName.rfind("var_short_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(10));
  } else if (fieldName.rfind("var_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(4));
  }

  for (ClassInfo* cur = cls; cur; cur = cur->super) {
    for (const auto& name : candidateNames) {
      for (auto& f : cur->fields) {
        if (f.name == name && f.desc == "S" && !f.isStatic) {
          targetField = &f;
          break;
        }
      }
      if (targetField) break;
    }
    if (targetField) break;
  }

  if (targetField && !targetField->isStatic && targetField->index >= 0 && targetField->index < (int)inst->f.size()) {
    inst->f[targetField->index].i = val;
  }
}

static Object* getVmStaticObjectByDesc(VM& vm, const std::string& className, const std::string& fieldName, const std::string& desc) {
  auto it = vm.classes.find(className);
  if (it == vm.classes.end()) return nullptr;
  ClassInfo* cls = it->second;

  std::vector<std::string> candidateNames = { fieldName };
  if (fieldName.rfind("var_", 0) == 0) {
    candidateNames.push_back(fieldName.substr(4));
  }

  for (const auto& name : candidateNames) {
    for (auto& f : cls->fields) {
      if (f.name == name && f.desc == desc && f.isStatic && f.isRef) {
        if (f.index >= 0 && f.index < (int)cls->statics.size()) {
          return cls->statics[f.index].o;
        }
      }
    }
  }
  return nullptr;
}

static void invalidateCbHierarchy(Object* obj) {
  while (obj && obj->kind == K_INST) {
    setVmInstanceBool(obj, "a", true);
    setVmInstanceBool(obj, "var_boolean_a", true);
    setVmInstanceBool(obj, "b", true);
    setVmInstanceBool(obj, "var_boolean_b", true);

    Object* nextCb = nullptr;
    Instance* inst = static_cast<Instance*>(obj);
    for (ClassInfo* cur = inst->cls; cur; cur = cur->super) {
      for (auto& f : cur->fields) {
        if (!f.isStatic && f.isRef && f.desc == "Lcb;" && (f.name == "b" || f.name == "var_cb_b")) {
          if (f.index >= 0 && f.index < (int)inst->f.size()) {
            nextCb = inst->f[f.index].o;
            break;
          }
        }
      }
      if (nextCb) break;
    }
    obj = nextCb;
  }
}

static void updateJavaViewportVariables(VM& vm) {
  // 1. Classe r (base de Canvas, compartilhada por todo o motor J2ME)
  setVmStaticInt(vm, "r", "g", g_screenWidth);
  setVmStaticInt(vm, "r", "i", g_screenWidth / 2);
  setVmStaticInt(vm, "r", "h", g_screenHeight);
  setVmStaticInt(vm, "r", "j", g_screenHeight / 2);

  // 2. Classe as (Canvas principal do jogo e HUD)
  // No bytecode original: a = var_int_a, b = b, c = c, d = var_int_d, n = n, o = o, p = p
  int32_t oldAsc = getVmStaticInt(vm, "as", "c", g_screenWidth / 2 - 8);
  int32_t newAsc = g_screenWidth / 2 - 8;
  int32_t deltaC = newAsc - oldAsc;

  setVmStaticInt(vm, "as", "a", g_screenWidth);
  setVmStaticInt(vm, "as", "var_int_a", g_screenWidth);
  setVmStaticInt(vm, "as", "b", g_screenHeight - 21);
  setVmStaticInt(vm, "as", "c", newAsc);
  setVmStaticInt(vm, "as", "d", (g_screenHeight - 21) / 2);
  setVmStaticInt(vm, "as", "var_int_d", (g_screenHeight - 21) / 2);
  setVmStaticInt(vm, "as", "n", (g_screenWidth - 74) / 6);
  setVmStaticInt(vm, "as", "o", g_screenWidth - 67);
  setVmStaticInt(vm, "as", "p", g_screenWidth - 6);

  // 3. Força redesenho completo de HUD no Canvas ativo
  if (g_display && g_display->current) {
    setVmInstanceBool(g_display->current, "e", true); // var_boolean_e
    setVmInstanceBool(g_display->current, "var_boolean_e", true);
    setVmInstanceBool(g_display->current, "f", true);
    setVmInstanceBool(g_display->current, "g", true);
    setVmInstanceBool(g_display->current, "h", true);
  }

  // 4. Se estiver em jogo ativo, desloca a câmera pelo delta do centro da tela sem invocar bytecode
  if (deltaC != 0) {
    int32_t camX = getVmStaticInt(vm, "n", "a");
    int32_t prevCamX = getVmStaticInt(vm, "n", "c");
    setVmStaticInt(vm, "n", "a", camX + deltaC);
    setVmStaticInt(vm, "n", "c", prevCamX + deltaC);
  }

  // 5. Se houver menus/diálogos modais abertos (ai: Status/Item/Equip, bp: Loja, bf: Baú, ax: Refino, aa: Forja),
  // atualiza suas coordenadas X e Y centralizadas para que não fiquem deslocados ao alternar a proporção
  int32_t menuX = (g_screenWidth / 2) - 100;
  int32_t menuY = (g_screenHeight / 2) - 122;

  const char* menuClasses[] = { "ai", "bp", "bf", "ax", "aa" };
  for (const char* mcls : menuClasses) {
    setVmStaticInt(vm, mcls, "a", menuX);
    setVmStaticInt(vm, mcls, "var_int_a", menuX);
    setVmStaticInt(vm, mcls, "b", menuY);
    setVmStaticInt(vm, mcls, "var_int_b", menuY);

    std::string desc = std::string("L") + mcls + ";";
    Object* inst = getVmStaticObjectByDesc(vm, mcls, "a", desc);
    if (!inst) {
      inst = getVmStaticObjectByDesc(vm, mcls, std::string("var_") + mcls + "_a", desc);
    }
    if (inst) {
      invalidateCbHierarchy(inst);
    }
  }

  // 6. Atualiza limites de culling dos objetos de cenário (classe aj) já carregados no mapa
  // No bytecode original, o construtor de aj pré-calcula this.b = (short)(as.a + imgW/2) e this.e = (short)(as.b + imgH).
  // Ao alternar para widescreen (ex: 240 -> 568), objetos que ficam além de 240px eram descartados pelo teste n4 > this.b.
  for (Object* obj : vm.allObjs) {
    if (!obj || obj->kind != K_INST || !obj->cls) continue;
    if (obj->cls->name == "aj") {
      Instance* inst = static_cast<Instance*>(obj);
      int imgW = 32, imgH = 32;
      for (ClassInfo* cur = inst->cls; cur; cur = cur->super) {
        for (auto& f : cur->fields) {
          if (!f.isStatic && f.isRef && f.desc == "Ljavax/microedition/lcdui/Image;") {
            if (f.index >= 0 && f.index < (int)inst->f.size()) {
              Object* imgO = inst->f[f.index].o;
              if (imgO && imgO->kind == K_IMAGE) {
                ImageObj* img = static_cast<ImageObj*>(imgO);
                if (img->width > 0) imgW = img->width;
                if (img->height > 0) imgH = img->height;
              }
            }
            break;
          }
        }
      }
      int16_t newB = (int16_t)(g_screenWidth + (imgW >> 1));
      int16_t newE = (int16_t)((g_screenHeight - 21) + imgH);
      setVmInstanceShort(obj, "b", newB);
      setVmInstanceShort(obj, "e", newE);
    }
  }

  // 7. Redefine o clip do Graphics principal
  if (g_screenGraphics) {
    g_screenGraphics->resetClip();
  }
}

void Platform::updateViewport(VM& vm) {
  s_lastVm = &vm;
  updateJavaViewportVariables(vm);
}

static SDL_Texture* s_texBezelSoltia = nullptr;
static SDL_Texture* s_texBezelSlate = nullptr;
static SDL_Texture* s_texFontOsd = nullptr;

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

struct TtfFontState {
  bool initialized = false;
  bool available = false;
  SDL_Texture* texture = nullptr;
  stbtt_bakedchar bakedChars[224]; // Codepoints 32 a 255 (ASCII + Latin-1 Supplement: á, é, í, ó, ú, ã, õ, ç, etc.)
  float bakedSize = 48.0f;
  int atlasW = 1024;
  int atlasH = 1024;
};
static TtfFontState s_ttf;

static uint32_t decodeNextUtf8(const std::string& str, size_t& i) {
  if (i >= str.length()) return 0;
  uint8_t c = (uint8_t)str[i++];
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && i < str.length()) {
    uint8_t c2 = (uint8_t)str[i++];
    return ((c & 0x1F) << 6) | (c2 & 0x3F);
  }
  if ((c & 0xF0) == 0xE0 && i + 1 < str.length()) {
    uint8_t c2 = (uint8_t)str[i++];
    uint8_t c3 = (uint8_t)str[i++];
    return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
  }
  return c;
}

static bool initTtfFont(SDL_Renderer* rend) {
  if (s_ttf.initialized) return s_ttf.available;
  s_ttf.initialized = true;

  auto ttfBytes = Platform::readAsset("fonts/ui_font.ttf");
  if (ttfBytes.empty()) {
    ttfBytes = Platform::readAsset("assets/fonts/ui_font.ttf");
  }
  if (ttfBytes.empty()) {
    boot_log("[Platform] ui_font.ttf nao encontrado, usando fonte bitmap OSD como fallback.\n");
    s_ttf.available = false;
    return false;
  }

  std::vector<uint8_t> monoBitmap(s_ttf.atlasW * s_ttf.atlasH);
  int res = stbtt_BakeFontBitmap(ttfBytes.data(), 0, s_ttf.bakedSize,
                                 monoBitmap.data(), s_ttf.atlasW, s_ttf.atlasH,
                                 32, 224, s_ttf.bakedChars);
  if (res <= 0) {
    boot_log("[Platform] stbtt_BakeFontBitmap falhou (res=%d).\n", res);
    s_ttf.available = false;
    return false;
  }

  std::vector<uint8_t> rgba(s_ttf.atlasW * s_ttf.atlasH * 4);
  for (size_t i = 0; i < monoBitmap.size(); i++) {
    uint8_t a = monoBitmap[i];
    rgba[i * 4 + 0] = 255;
    rgba[i * 4 + 1] = 255;
    rgba[i * 4 + 2] = 255;
    rgba[i * 4 + 3] = a;
  }

  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
      rgba.data(), s_ttf.atlasW, s_ttf.atlasH, 32, s_ttf.atlasW * 4, SDL_PIXELFORMAT_RGBA32);
  if (!surf) {
    s_ttf.available = false;
    return false;
  }

  s_ttf.texture = SDL_CreateTextureFromSurface(rend, surf);
  SDL_FreeSurface(surf);
  if (!s_ttf.texture) {
    s_ttf.available = false;
    return false;
  }

  SDL_SetTextureBlendMode(s_ttf.texture, SDL_BLENDMODE_BLEND);
  s_ttf.available = true;
  boot_log("[Platform] Fonte TrueType UI (Inter SemiBold) inicializada com sucesso!\n");
  return true;
}

static std::string s_osdMessage = "";
static uint32_t s_osdExpireTime = 0;

static void loadSettings();

#ifdef __SWITCH__
static bool s_romfsInitialized = false;
#endif

bool Platform::init(int scale) {
#ifdef __SWITCH__
  Result rc = romfsInit();
  if (R_FAILED(rc)) {
    fprintf(stderr, "[RomFS] Falha ao inicializar romfsInit: 0x%x\n", rc);
    s_romfsInitialized = false;
  } else {
    printf("[RomFS] RomFS inicializado com sucesso!\n");
    s_romfsInitialized = true;
  }
#endif
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
    fprintf(stderr, "Aviso: Falha ao inicializar SDL completo: %s. Tentando apenas video e eventos...\n", SDL_GetError());
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      fprintf(stderr, "Erro ao inicializar SDL video: %s\n", SDL_GetError());
      return false;
    }
  }

  // Inicializa áudio
  if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
    fprintf(stderr, "Aviso: Mixer audio não inicializou: %s\n", Mix_GetError());
  } else {
    Mix_AllocateChannels(16);
    MidiSynth::init();
  }

#ifdef __SWITCH__
  s_window = SDL_CreateWindow(
      "Heroes Lore: Wind of Soltia",
      0, 0,
      1280, 720,
      0);
  if (!s_window) {
    s_window = SDL_CreateWindow(
        "Heroes Lore: Wind of Soltia",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        1280, 720,
        SDL_WINDOW_FULLSCREEN);
  }
#else
  loadSettings();
  int winW = (s_savedWinW >= 240) ? s_savedWinW : 960;
  int winH = (s_savedWinH >= 320) ? s_savedWinH : 720;

  s_window = SDL_CreateWindow(
      "Heroes Lore: Wind of Soltia (Native Recomp)",
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      winW, winH,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
#endif
  if (!s_window) {
    fprintf(stderr, "Erro ao criar janela: %s\n", SDL_GetError());
    return false;
  }

  // Configura hints antes de criar renderizador e texturas
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Pixel-perfect nearest neighbor
  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");   // Desativa cliques de mouse sintéticos ao tocar na tela
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");   // Desativa toques sintéticos a partir de mouse

#ifdef _WIN32
  // Prioriza direct3d11 no Windows para estabilidade moderna de GPU
  SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
#endif
  s_renderer = SDL_CreateRenderer(
      s_window, -1,
      SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!s_renderer) {
#ifndef __SWITCH__
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
#else
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengles2");
#endif
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  }
  if (!s_renderer) {
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_ACCELERATED);
  }
  if (!s_renderer) {
    s_renderer = SDL_CreateRenderer(s_window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!s_renderer) {
    fprintf(stderr, "Erro ao criar renderizador: %s\n", SDL_GetError());
    return false;
  }

  // Textura streaming dinâmica com resolução nativa (240x320 clássico, 568x320 widescreen ou auto)
  g_screenWidth = determineTargetWidth(s_aspectMode);
  g_screenHeight = SCREEN_HEIGHT;

  s_screenTexture = SDL_CreateTexture(
      s_renderer,
      SDL_PIXELFORMAT_ARGB8888,
      SDL_TEXTUREACCESS_STREAMING,
      g_screenWidth, g_screenHeight);
  if (!s_screenTexture) {
    fprintf(stderr, "Erro ao criar textura de tela: %s\n", SDL_GetError());
    return false;
  }
  SDL_SetTextureBlendMode(s_screenTexture, SDL_BLENDMODE_NONE);

  SDL_RaiseWindow(s_window);
#ifdef _WIN32
  SDL_SysWMinfo wm;
  SDL_VERSION(&wm.version);
  if (SDL_GetWindowWMInfo(s_window, &wm)) {
    HWND hwnd = wm.info.win.window;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
    printf("[SDL] Win32 HWND: %p (janela trazida para o primeiro plano)\n", (void*)hwnd);
  }
#endif
  printf("[SDL] Driver de Video: %s, Audio: %s\n", SDL_GetCurrentVideoDriver(), SDL_GetCurrentAudioDriver());
  SDL_RendererInfo rinfo;
  if (SDL_GetRendererInfo(s_renderer, &rinfo) == 0) {
    printf("[SDL] Renderer: %s (flags: 0x%X)\n", rinfo.name, rinfo.flags);
  }

  // Inicializa controles conectados no momento do boot
  int numJoysticks = SDL_NumJoysticks();
  for (int i = 0; i < numJoysticks; i++) {
    if (SDL_IsGameController(i)) {
      SDL_GameController* pad = SDL_GameControllerOpen(i);
      if (pad) {
        s_controllers.push_back(pad);
        printf("[Gamepad] Controle detectado na inicializacao: %s\n", SDL_GameControllerName(pad));
        SDL_GameControllerRumble(pad, 0x3000, 0x3000, 120);
      }
    }
  }

  // Carrega preferências do usuário (Bezel, FPS, Aspect, Fullscreen)
  loadSettings();
#ifndef __SWITCH__
  if (s_isFullscreen) {
    SDL_SetWindowFullscreen(s_window, SDL_WINDOW_FULLSCREEN_DESKTOP);
  }
#endif
  s_frameStartCounter = SDL_GetPerformanceCounter();
#if defined(__ANDROID__)
  Platform::showOsdMessage("Heroes Lore: Wind of Soltia\nPort Nativo Mobile [PT-BR]");
#elif defined(__SWITCH__)
  Platform::showOsdMessage("Heroes Lore: Wind of Soltia\n[R3: FPS | Sel+Start: 16:9 | Sel+R3: Moldura]");
#else
  Platform::showOsdMessage("Heroes Lore: Wind of Soltia\n[F4: Nuvem | F5: Moldura | F6: FPS | F7: 16:9 | F9: Atualizar | F11: Tela Cheia]");
#endif

  // Inicializa o subsistema de Cloud Save (Google Drive)
  CloudSave::init();

  // Inicializa o subsistema de atualização OTA e dispara checagem em background
  Updater::init();
  Updater::checkAsync(false);

  return true;
}

static int s_activeDirection = 0;
static uint32_t s_directionPressTime = 0;
static uint32_t s_directionLastRepeatTime = 0;

static bool isDirectionKey(int key) {
  return key == -1 || key == -2 || key == -3 || key == -4 ||
         key == 50 || key == 52 || key == 54 || key == 56;
}

static int getGamepadHeldDirection() {
  for (auto* pad : s_controllers) {
    if (!pad) continue;
    // D-Pad
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) return -1;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) return -2;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) return -3;
    if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) return -4;

    // Analog Stick Esquerdo (Left X & Y)
    int16_t ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    int16_t ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
    const int16_t DEADZONE = 12000;
    if (std::abs(ax) > DEADZONE || std::abs(ay) > DEADZONE) {
      if (std::abs(ax) > std::abs(ay)) {
        return (ax < 0) ? -3 : -4; // Left / Right
      } else {
        return (ay < 0) ? -1 : -2; // Up / Down
      }
    }
  }
  return 0;
}

// --- Texturas e Layout do Gamepad Virtual (Console Handheld UI) ---
static SDL_Texture* s_texDpadBase = nullptr;
static SDL_Texture* s_texDpadArrowActive = nullptr;
static SDL_Texture* s_texBtn5 = nullptr;
static SDL_Texture* s_texBtn1 = nullptr;
static SDL_Texture* s_texBtn3 = nullptr;
static SDL_Texture* s_texBtn7 = nullptr;
static SDL_Texture* s_texBtn9 = nullptr;
static SDL_Texture* s_texBtnMenu = nullptr;
static SDL_Texture* s_texBtnMap = nullptr;
static SDL_Texture* s_texBtnRsk = nullptr;
static SDL_Texture* s_texBtnRskRound = nullptr;
static SDL_Texture* s_texBtnPrev = nullptr;
static SDL_Texture* s_texBtnNext = nullptr;
static SDL_Texture* s_texBtnEyeOpen = nullptr;
static SDL_Texture* s_texBtnEyeClosed = nullptr;
static SDL_Texture* s_texBtnRotate = nullptr;
static SDL_Texture* s_texBtnAspect = nullptr;
static SDL_Texture* s_texBtnFps = nullptr;
static SDL_Texture* s_texBtnGlow = nullptr;
static bool s_gamepadTexturesLoaded = false;

// Estado de combos do Gamepad fisico (Select+Start = Widescreen, Select+R3 = Bezel, R3 = Turbo FPS)
static bool s_selectButtonHeld = false;
static bool s_selectUsedInCombo = false;
static bool s_startUsedInCombo = false;

static const int KEY_TOGGLE_TOUCH_UI = -999;
static const int KEY_TOGGLE_ORIENTATION = -998;
static const int KEY_TOGGLE_TOUCH_ASPECT = -997;
static const int KEY_TOGGLE_TOUCH_FPS = -996;

// Orientação de tela no Switch (s_screenRotation declarada no topo):
// 0 = Paisagem horizontal normal (1280x720)
// 1 = Retrato vertical (TATE 90° CW, 720x1280)
// 2 = Retrato vertical invertido (TATE 270° CCW / Flip Grip, 720x1280)
static SDL_Texture* s_rotateTarget = nullptr;

struct ActiveFinger {
  SDL_FingerID id;
  int key;
};

static std::vector<ActiveFinger> s_activeFingers;
static int s_touchHeldDirection = 0;
#ifdef __ANDROID__
static bool s_touchOverlayEnabled = true;
#else
static bool s_touchOverlayEnabled = false;
#endif

// Passo 10: Localização e Idiomas
static std::string s_currentLanguage = "pt"; // "pt", "en", "it", "es"

static bool isTouchKeyHeld(int key) {
  for (const auto& f : s_activeFingers) {
    if (f.key == key) return true;
  }
  return false;
}

static SDL_Texture* loadRgbaTexture(const char* name) {
  auto bytes = Platform::readAsset(std::string("ui/") + name + ".rgba");
  if (bytes.size() < 8) return nullptr;
  uint32_t w = *(const uint32_t*)(bytes.data());
  uint32_t h = *(const uint32_t*)(bytes.data() + 4);
  if (bytes.size() < 8 + (size_t)w * h * 4) return nullptr;

  SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormatFrom(
      (void*)(bytes.data() + 8), w, h, 32, w * 4, SDL_PIXELFORMAT_RGBA32);
  if (!surf) return nullptr;

  SDL_Texture* tex = SDL_CreateTextureFromSurface(s_renderer, surf);
  SDL_FreeSurface(surf);
  if (tex) {
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  }
  return tex;
}

static void loadSettings() {
  std::string path = Platform::getStorageDir() + "/hl_settings.ini";
  FILE* f = fopen(path.c_str(), "r");
  if (!f) return;
  char line[256];
  while (fgets(line, sizeof(line), f)) {
    int val = 0;
    char strVal[64] = {0};
    if (sscanf(line, "language=%63s", strVal) == 1) {
      if (strcmp(strVal, "pt") == 0 || strcmp(strVal, "en") == 0 ||
          strcmp(strVal, "it") == 0 || strcmp(strVal, "es") == 0) {
        s_currentLanguage = strVal;
      }
    } else if (sscanf(line, "bezel_mode=%d", &val) == 1) {
      if (val >= 0 && val < BEZEL_COUNT) s_bezelMode = static_cast<BezelMode>(val);
    } else if (sscanf(line, "target_fps=%d", &val) == 1) {
      if (val == 15 || val == 30) s_targetFps = val;
      else s_targetFps = 15;
    } else if (sscanf(line, "aspect_mode=%d", &val) == 1) {
      if (val >= 0 && val < ASPECT_COUNT) s_aspectMode = static_cast<AspectMode>(val);
    } else if (sscanf(line, "fullscreen=%d", &val) == 1) {
      s_isFullscreen = (val != 0);
    } else if (sscanf(line, "window_w=%d", &val) == 1) {
      if (val >= 240) s_savedWinW = val;
    } else if (sscanf(line, "window_h=%d", &val) == 1) {
      if (val >= 320) s_savedWinH = val;
    }
  }
  fclose(f);
}

static void saveSettings() {
  std::string path = Platform::getStorageDir() + "/hl_settings.ini";
  FILE* f = fopen(path.c_str(), "w");
  if (!f) return;
  int curW = s_savedWinW, curH = s_savedWinH;
  if (s_window && !s_isFullscreen) {
    int w = 0, h = 0;
    SDL_GetWindowSize(s_window, &w, &h);
    if (w >= 240 && h >= 320) {
      curW = w;
      curH = h;
    }
  }
  fprintf(f, "[Video]\n");
  fprintf(f, "bezel_mode=%d\n", static_cast<int>(s_bezelMode));
  fprintf(f, "target_fps=%d\n", s_targetFps);
  fprintf(f, "aspect_mode=%d\n", static_cast<int>(s_aspectMode));
  fprintf(f, "fullscreen=%d\n", s_isFullscreen ? 1 : 0);
  if (curW > 0 && curH > 0) {
    fprintf(f, "window_w=%d\n", curW);
    fprintf(f, "window_h=%d\n", curH);
  }
  fprintf(f, "[Localization]\n");
  fprintf(f, "language=%s\n", s_currentLanguage.c_str());
  fclose(f);
}

static void loadBezelTextures() {
  if (!s_renderer) return;
  if (!s_texFontOsd) {
    s_texFontOsd = loadRgbaTexture("font_osd");
  }
  if (s_bezelMode == BEZEL_SOLTIA && !s_texBezelSoltia) {
    s_texBezelSoltia = loadRgbaTexture("bezel_soltia");
  } else if (s_bezelMode == BEZEL_SLATE && !s_texBezelSlate) {
    s_texBezelSlate = loadRgbaTexture("bezel_slate");
  }
}

static void drawOsd(int winW, int winH) {
  if (s_osdMessage.empty() || SDL_GetTicks() >= s_osdExpireTime || !s_renderer) return;

  uint32_t now = SDL_GetTicks();
  uint32_t timeLeft = s_osdExpireTime - now;
  Uint8 alpha = 255;
  if (timeLeft < 500) {
    alpha = static_cast<Uint8>((timeLeft * 255) / 500);
  }

  // Identifica resolução da textura de fonte (suporta dinamicamente 22x36 ou 44x72)
  int texW = 0, texH = 0;
  if (s_texFontOsd) {
    SDL_QueryTexture(s_texFontOsd, nullptr, nullptr, &texW, &texH);
  }
  int srcCellW = (texW > 0) ? (texW / 16) : 22;
  int srcCellH = (texH > 0) ? (texH / 6) : 36;
  float cellAspect = (float)srcCellW / (float)srcCellH; // ~0.611f

  bool isPortrait = (winH > winW);
  int minDim = std::min(winW, winH);

  // Escala responsiva baseada na menor dimensão da tela:
  // No Switch 720p: baseScale = 1.0f -> charH = 34px (+62% maior que os 21px anteriores)
  // No Celular 1080p: baseScale = 1.5f -> charH = 51px (+142% maior que os 21px anteriores)
  // No Celular 1440p: baseScale = 2.0f -> charH = 68px
  float baseScale = std::max(1.0f, (float)minDim / 720.0f);
  float targetCharH = std::round(34.0f * baseScale);
  float targetCharW = std::round(targetCharH * cellAspect);

  // 1. Divide a mensagem em linhas (por quebra explícita \n ou quebra automática de palavras)
  std::vector<std::string> rawLines;
  size_t startPos = 0;
  while (startPos < s_osdMessage.length()) {
    size_t nlPos = s_osdMessage.find('\n', startPos);
    if (nlPos == std::string::npos) {
      rawLines.push_back(s_osdMessage.substr(startPos));
      break;
    }
    rawLines.push_back(s_osdMessage.substr(startPos, nlPos - startPos));
    startPos = nlPos + 1;
  }
  if (rawLines.empty()) return;

  float maxTextW = (float)winW * 0.90f - 48.0f;
  int maxCharsPerLine = std::max(18, (int)(maxTextW / targetCharW));

  std::vector<std::string> lines;
  for (const auto& rline : rawLines) {
    if ((int)rline.length() <= maxCharsPerLine) {
      lines.push_back(rline);
    } else {
      // Quebra inteligente por palavras para manter a fonte sempre ampla e legível
      size_t lineStart = 0;
      while (lineStart < rline.length()) {
        if (rline.length() - lineStart <= (size_t)maxCharsPerLine) {
          lines.push_back(rline.substr(lineStart));
          break;
        }
        size_t splitAt = lineStart + maxCharsPerLine;
        size_t spacePos = rline.rfind(' ', splitAt);
        if (spacePos != std::string::npos && spacePos > lineStart) {
          lines.push_back(rline.substr(lineStart, spacePos - lineStart));
          lineStart = spacePos + 1;
        } else {
          lines.push_back(rline.substr(lineStart, maxCharsPerLine));
          lineStart += maxCharsPerLine;
        }
      }
    }
  }

  // Verifica o comprimento máximo entre as linhas geradas
  size_t maxLineLen = 0;
  for (const auto& l : lines) {
    if (l.length() > maxLineLen) maxLineLen = l.length();
  }

  float totalTextW = (float)maxLineLen * targetCharW;
  if (totalTextW > maxTextW && maxLineLen > 0) {
    targetCharW = maxTextW / (float)maxLineLen;
    targetCharH = targetCharW / cellAspect;
    if (targetCharH < 26.0f) targetCharH = 26.0f;
    if (targetCharW < 15.0f) targetCharW = 15.0f;
  }

  int charW = (int)targetCharW;
  int charH = (int)targetCharH;
  int lineSpacing = (int)(6.0f * baseScale);
  int textW = (int)maxLineLen * charW;
  int padX = (int)(22.0f * baseScale);
  int padY = (int)(12.0f * baseScale);
  int bannerW = textW + padX * 2;
  int bannerH = (int)lines.size() * charH + (int)(lines.size() - 1) * lineSpacing + padY * 2;
  int bannerX = (winW - bannerW) / 2;

  // Posição vertical: em modo retrato, fica seguro abaixo da barra de status/notch (6.5% de winH)
  // em modo paisagem fica a 4.5% de winH
  int bannerY = isPortrait ? std::max(56, (int)(winH * 0.065f)) : std::max(28, (int)(winH * 0.045f));

  SDL_SetRenderDrawBlendMode(s_renderer, SDL_BLENDMODE_BLEND);

  // Sombra suave do banner (offset proporcional)
  int shadowOffset = std::max(3, (int)(4.0f * baseScale));
  SDL_Rect shadowRect = { bannerX + shadowOffset, bannerY + shadowOffset, bannerW, bannerH };
  SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, (Uint8)((alpha * 160) / 255));
  SDL_RenderFillRect(s_renderer, &shadowRect);

  // Fundo escuro do banner (Frosted Dark Glass)
  SDL_Rect bgRect = { bannerX, bannerY, bannerW, bannerH };
  SDL_SetRenderDrawColor(s_renderer, 14, 18, 26, (Uint8)((alpha * 235) / 255));
  SDL_RenderFillRect(s_renderer, &bgRect);

  // Borda brilhante em cyan neon (dupla se alta resolução)
  SDL_SetRenderDrawColor(s_renderer, 0, 210, 255, (Uint8)((alpha * 220) / 255));
  SDL_RenderDrawRect(s_renderer, &bgRect);
  if (baseScale >= 1.3f) {
    SDL_Rect innerBorder = { bannerX + 1, bannerY + 1, bannerW - 2, bannerH - 2 };
    SDL_SetRenderDrawColor(s_renderer, 0, 180, 230, (Uint8)((alpha * 160) / 255));
    SDL_RenderDrawRect(s_renderer, &innerBorder);
  }

  // Texto centralizado por linha
  if (s_texFontOsd) {
    SDL_SetTextureAlphaMod(s_texFontOsd, alpha);
    for (size_t lineIdx = 0; lineIdx < lines.size(); lineIdx++) {
      const std::string& line = lines[lineIdx];
      int lineTextW = (int)line.length() * charW;
      int lineStartX = bannerX + (bannerW - lineTextW) / 2;
      int lineY = bannerY + padY + (int)lineIdx * (charH + lineSpacing);
      for (size_t i = 0; i < line.length(); i++) {
        char c = line[i];
        if (c < 32 || c > 126) c = ' ';
        int idx = c - 32;
        int col = idx % 16;
        int row = idx / 16;
        SDL_Rect src = { col * srcCellW, row * srcCellH, srcCellW, srcCellH };
        SDL_Rect dst = { lineStartX + (int)i * charW, lineY, charW, charH };
        SDL_RenderCopy(s_renderer, s_texFontOsd, &src, &dst);
      }
    }
  }
}

static void loadGamepadTextures() {
  if (s_gamepadTexturesLoaded || !s_renderer) return;
  s_texDpadBase = loadRgbaTexture("dpad_base");
  s_texDpadArrowActive = loadRgbaTexture("dpad_arrow_active");
  s_texBtn5 = loadRgbaTexture("btn_5");
  s_texBtn1 = loadRgbaTexture("btn_1");
  s_texBtn3 = loadRgbaTexture("btn_3");
  s_texBtn7 = loadRgbaTexture("btn_7");
  s_texBtn9 = loadRgbaTexture("btn_9");
  s_texBtnMenu = loadRgbaTexture("btn_menu");
  s_texBtnMap = loadRgbaTexture("btn_map");
  s_texBtnRsk = loadRgbaTexture("btn_rsk");
  s_texBtnRskRound = loadRgbaTexture("btn_rsk_round");
  s_texBtnPrev = loadRgbaTexture("btn_prev");
  s_texBtnNext = loadRgbaTexture("btn_next");
  s_texBtnEyeOpen = loadRgbaTexture("btn_eye_open");
  s_texBtnEyeClosed = loadRgbaTexture("btn_eye_closed");
  s_texBtnRotate = loadRgbaTexture("btn_rotate");
  s_texBtnAspect = loadRgbaTexture("btn_aspect");
  s_texBtnFps = loadRgbaTexture("btn_fps");
  s_texBtnGlow = loadRgbaTexture("btn_glow");
  loadBezelTextures();
  s_gamepadTexturesLoaded = true;
}

struct TouchButtonDef {
  int key;
  int cx, cy;
  int r;
  int w, h;
  SDL_Texture* tex;
};

struct GamepadLayout {
  SDL_Rect gameRect;
  SDL_Rect controllerBgRect;
  bool isPortrait;
  int dpadX, dpadY, dpadR;
  std::vector<TouchButtonDef> buttons;
};

static GamepadLayout calculateLayout(int winW, int winH) {
  GamepadLayout layout;
  layout.isPortrait = (winH > winW);
  layout.dpadR = 0;

  if (layout.isPortrait) {
    // Modo Retrato (Smartphone em pé / TATE)
    // O jogo ocupa a tela no maior tamanho possível mantendo a proporção exata 240x320
    float scaleW = (float)winW / 240.0f;
    float scaleH = (float)winH / 320.0f;
    float scale = std::min(scaleW, scaleH);
    int gameW = (int)(240.0f * scale);
    int gameH = (int)(320.0f * scale);
    int gameX = (winW - gameW) / 2;

    // Barra de Sistema no topo (MENU e MAPA) com proporção ampla e abaixo da status bar
    int pillW = (int)(winW * 0.28f);
    int pillH = (int)(pillW * (100.0f / 240.0f));
    int topY = std::max((int)(winH * 0.070f), pillH / 2 + 36);

    // Subir a tela do jogo logo abaixo dos botões superiores com margem confortável
    int topLimit = topY + pillH / 2 + (int)(winW * 0.025f);
    int maxGameY = (winH - gameH) / 2;
    int gameY = std::max(0, std::min(topLimit, maxGameY));
    layout.gameRect = { gameX, gameY, gameW, gameH };
    layout.controllerBgRect = { 0, 0, 0, 0 }; // Sem fundo opaco, overlay translúcido sobre o jogo!

    // Botão de Ocultar/Reexibir Controles Virtuais no canto inferior esquerdo
    int rToggle = (int)(winW * 0.050f);
    int toggleX = (int)(winW * 0.08f);
    int toggleY = winH - (int)(winW * 0.070f);
    int stepToggle = (int)(rToggle * 2.25f);
    int curUtilX = toggleX;

    layout.buttons.push_back({ KEY_TOGGLE_TOUCH_UI, curUtilX, toggleY, rToggle, rToggle * 2, rToggle * 2,
                               s_touchOverlayEnabled ? s_texBtnEyeOpen : s_texBtnEyeClosed });

#ifdef __SWITCH__
    curUtilX += stepToggle;
    layout.buttons.push_back({ KEY_TOGGLE_ORIENTATION, curUtilX, toggleY, rToggle, rToggle * 2, rToggle * 2, s_texBtnRotate });
#endif

    if (s_touchOverlayEnabled) {
      // D-Pad e Cluster de Ação posicionados na zona inferior confortável
      // (abaixados para dar visibilidade máxima à tela e diálogos do jogo)
      layout.dpadX = (int)(winW * 0.22f);
      layout.dpadY = winH - (int)(winW * 0.31f);
      layout.dpadR = (int)(winW * 0.175f);

      // Disposição clássica ergonômica em arco mantida (5 no centro, 1 e 7 na coluna esquerda, 3 no topo, 9 no topo-direito, R na direita-baixo):
      int actX = (int)(winW * 0.74f);
      int actY = winH - (int)(winW * 0.31f);
      int r5 = (int)(winW * 0.11f);
      int rSub = (int)(r5 * 0.74f);

      // 5 - Ataque (Centro, grande)
      layout.buttons.push_back({ 53, actX, actY, r5, r5 * 2, r5 * 2, s_texBtn5 });
      // 1 - Skill 1 (Esquerda-cima)
      layout.buttons.push_back({ 49, actX - (int)(r5 * 1.95f), actY - (int)(r5 * 0.95f), rSub, rSub * 2, rSub * 2, s_texBtn1 });
      // 3 - Skill 2 (Topo)
      layout.buttons.push_back({ 51, actX - (int)(r5 * 0.20f), actY - (int)(r5 * 2.10f), rSub, rSub * 2, rSub * 2, s_texBtn3 });
      // 7 - Poção (Esquerda-baixo, abaixo de 1)
      layout.buttons.push_back({ 55, actX - (int)(r5 * 1.85f), actY + (int)(r5 * 1.15f), rSub, rSub * 2, rSub * 2, s_texBtn7 });
      // 9 - Item (Direita-cima)
      layout.buttons.push_back({ 57, actX + (int)(r5 * 1.35f), actY - (int)(r5 * 1.65f), rSub, rSub * 2, rSub * 2, s_texBtn9 });
      // R - Voltar / Cancelar / Status (Direita-baixo, abaixo de 9 e alinhado ao 7)
      layout.buttons.push_back({ -7, actX + (int)(r5 * 1.35f), actY + (int)(r5 * 1.15f), rSub, rSub * 2, rSub * 2, s_texBtnRskRound ? s_texBtnRskRound : s_texBtnRsk });

      // Barra de Sistema no topo (apenas MENU e MAPA, proporcionais e espaçados)
      layout.buttons.push_back({ -8, (int)(winW * 0.26f), topY, 0, pillW, pillH, s_texBtnMenu });
      layout.buttons.push_back({ 48, (int)(winW * 0.74f), topY, 0, pillW, pillH, s_texBtnMap });

      // Alternância de Poção (< e >) perfeitamente centralizadas no eixo horizontal da tela
      int rArrow = (int)(winW * 0.055f);
      int arrowSpacing = (int)(rArrow * 1.25f);
      int potY = winH - (int)(winW * 0.070f);
      layout.buttons.push_back({ -101, winW / 2 - arrowSpacing, potY, rArrow, rArrow * 2, rArrow * 2, s_texBtnPrev });
      layout.buttons.push_back({ 35, winW / 2 + arrowSpacing, potY, rArrow, rArrow * 2, rArrow * 2, s_texBtnNext });
    }

  } else {
    // Modo Paisagem (Landscape)
    int gameH = winH;
    int gameW = (g_screenWidth == SCREEN_W_WIDESCREEN) ? (int)(gameH * (568.0f / 320.0f)) : (int)(gameH * (240.0f / 320.0f));
    if (gameW > winW) gameW = winW;
    int gameX = (winW - gameW) / 2;
    int gameY = (winH - gameH) / 2;
    layout.gameRect = { gameX, gameY, gameW, gameH };
    layout.controllerBgRect = { 0, 0, 0, 0 };

    int leftW = gameX;
    int rightX = gameX + gameW;
    int rightW = winW - rightX;

    int controlLeftW = (leftW >= (int)(winW * 0.20f)) ? leftW : (int)(winW * 0.26f);
    int controlRightW = (rightW >= (int)(winW * 0.20f)) ? rightW : (int)(winW * 0.26f);
    int controlRightX = winW - controlRightW;

    // Botao de Ocultar/Reexibir Controles Virtuais no canto inferior esquerdo
    int rToggleLand = (int)(winH * 0.055f);
    int toggleX = (int)(controlLeftW * 0.16f);
    int toggleY = winH - (int)(winH * 0.09f);
    int stepToggleLand = (int)(rToggleLand * 2.25f);
    int curUtilXLand = toggleX;

    layout.buttons.push_back({ KEY_TOGGLE_TOUCH_UI, curUtilXLand, toggleY, rToggleLand, rToggleLand * 2, rToggleLand * 2,
                               s_touchOverlayEnabled ? s_texBtnEyeOpen : s_texBtnEyeClosed });

#ifdef __SWITCH__
    curUtilXLand += stepToggleLand;
    layout.buttons.push_back({ KEY_TOGGLE_ORIENTATION, curUtilXLand, toggleY, rToggleLand, rToggleLand * 2, rToggleLand * 2, s_texBtnRotate });
#endif

    if (s_touchOverlayEnabled) {
      // D-Pad na coluna esquerda (grande e confortavel)
      layout.dpadX = controlLeftW / 2;
      layout.dpadY = (int)(winH * 0.65f);
      layout.dpadR = std::min((int)(controlLeftW * 0.35f), (int)(winH * 0.25f));

      // MENU proporcional e grande no topo da coluna esquerda
      int menuW = std::min(240, (int)(controlLeftW * 0.48f));
      int menuH = (int)(menuW * (100.0f / 240.0f));
      layout.buttons.push_back({ -8, controlLeftW / 2, (int)(winH * 0.14f), 0, menuW, menuH, s_texBtnMenu });

      // Setas circulares para pocoes (< e >) na coluna ESQUERDA (entre MENU e D-Pad, super ergonomico)
      int rArrowLand = (int)(winH * 0.075f);
      int arrowSpacingLand = (int)(rArrowLand * 1.35f);
      layout.buttons.push_back({ -101, controlLeftW / 2 - arrowSpacingLand, (int)(winH * 0.33f), rArrowLand, rArrowLand * 2, rArrowLand * 2, s_texBtnPrev });
      layout.buttons.push_back({ 35, controlLeftW / 2 + arrowSpacingLand, (int)(winH * 0.33f), rArrowLand, rArrowLand * 2, rArrowLand * 2, s_texBtnNext });

      // MAPA e R no topo da coluna direita (grandes e legiveis)
      int topPillW = std::min(200, (int)(controlRightW * 0.38f));
      int topPillH = (int)(topPillW * (100.0f / 240.0f));
      layout.buttons.push_back({ 48, controlRightX + (int)(controlRightW * 0.28f), (int)(winH * 0.14f), 0, topPillW, topPillH, s_texBtnMap });
      layout.buttons.push_back({ -7, controlRightX + (int)(controlRightW * 0.72f), (int)(winH * 0.14f), 0, topPillW, topPillH, s_texBtnRsk });

      // Botoes de acao no formato ergonomico favorito na coluna direita (amplo e espacoso sem as setas!)
      int actX = controlRightX + (int)(controlRightW * 0.50f);
      int actY = (int)(winH * 0.65f);
      int r5 = std::min((int)(controlRightW * 0.17f), (int)(winH * 0.13f));
      int rSub = (int)(r5 * 0.74f);

      layout.buttons.push_back({ 53, actX, actY, r5, r5 * 2, r5 * 2, s_texBtn5 });
      layout.buttons.push_back({ 49, actX - (int)(r5 * 1.95f), actY - (int)(r5 * 0.95f), rSub, rSub * 2, rSub * 2, s_texBtn1 });
      layout.buttons.push_back({ 51, actX - (int)(r5 * 0.20f), actY - (int)(r5 * 2.10f), rSub, rSub * 2, rSub * 2, s_texBtn3 });
      layout.buttons.push_back({ 55, actX - (int)(r5 * 1.85f), actY + (int)(r5 * 1.15f), rSub, rSub * 2, rSub * 2, s_texBtn7 });
      layout.buttons.push_back({ 57, actX + (int)(r5 * 1.35f), actY - (int)(r5 * 1.65f), rSub, rSub * 2, rSub * 2, s_texBtn9 });
    }
  }

  return layout;
}

static int hitTestTouch(float touchX, float touchY, const GamepadLayout& layout) {
  // 1. Testa D-Pad primeiro se o toque estiver na sua vizinhança e D-Pad estiver ativo
  if (layout.dpadR > 0) {
    float ddx = touchX - layout.dpadX;
    float ddy = touchY - layout.dpadY;
    float distDpad = sqrt(ddx * ddx + ddy * ddy);
    if (distDpad <= layout.dpadR * 1.25f && distDpad >= layout.dpadR * 0.08f) {
      if (std::abs(ddx) > std::abs(ddy)) {
        return (ddx < 0) ? -3 : -4; // Left / Right
      } else {
        return (ddy < 0) ? -1 : -2; // Up / Down
      }
    }
  }

  // 2. Para os botões: seleciona o botão com menor proporção de distância (o mais próximo exato do dedo)
  int bestKey = 0;
  float bestDistRatio = 1.0f;

  for (const auto& b : layout.buttons) {
    if (b.r > 0) {
      float bx = touchX - b.cx;
      float by = touchY - b.cy;
      float d = sqrt(bx * bx + by * by);
      float maxR = b.r * 1.30f;
      if (d <= maxR) {
        float ratio = d / maxR;
        if (ratio < bestDistRatio) {
          bestDistRatio = ratio;
          bestKey = b.key;
        }
      }
    } else {
      float dx = std::abs(touchX - b.cx);
      float dy = std::abs(touchY - b.cy);
      float maxW = b.w * 0.70f;
      float maxH = b.h * 0.70f;
      if (dx <= maxW && dy <= maxH) {
        float ratio = std::max(dx / maxW, dy / maxH);
        if (ratio < bestDistRatio) {
          bestDistRatio = ratio;
          bestKey = b.key;
        }
      }
    }
  }

  return bestKey;
}

static void drawGamepad(const GamepadLayout& layout) {
  if (!s_renderer) return;

  // 1. D-Pad Virtual (Translúcido / Frosted Glass Overlay) se controles habilitados
  if (s_touchOverlayEnabled && s_texDpadBase && layout.dpadR > 0) {
    bool dpadActive = (s_touchHeldDirection != 0);
    SDL_SetTextureAlphaMod(s_texDpadBase, dpadActive ? 230 : 155);

    SDL_Rect dstDpad = {
      layout.dpadX - layout.dpadR,
      layout.dpadY - layout.dpadR,
      layout.dpadR * 2,
      layout.dpadR * 2
    };
    SDL_RenderCopy(s_renderer, s_texDpadBase, nullptr, &dstDpad);

    // Seta direcional ativa iluminada em cyan neon
    if (dpadActive && s_texDpadArrowActive) {
      SDL_SetTextureAlphaMod(s_texDpadArrowActive, 255);
      int arrDist = (int)(layout.dpadR * 0.58f);
      int arrSz = (int)(layout.dpadR * 0.40f);
      double angle = 0;
      int ax = layout.dpadX, ay = layout.dpadY;
      if (s_touchHeldDirection == -1) { ay -= arrDist; angle = 0; }
      else if (s_touchHeldDirection == -2) { ay += arrDist; angle = 180; }
      else if (s_touchHeldDirection == -3) { ax -= arrDist; angle = 270; }
      else if (s_touchHeldDirection == -4) { ax += arrDist; angle = 90; }

      SDL_Rect dstArr = { ax - arrSz / 2, ay - arrSz / 2, arrSz, arrSz };
      SDL_RenderCopyEx(s_renderer, s_texDpadArrowActive, nullptr, &dstArr, angle, nullptr, SDL_FLIP_NONE);
    }
  }

  // 2. Botões de Ação e Sistema (Translúcidos em repouso, iluminados ao toque)
  for (const auto& b : layout.buttons) {
    if (!b.tex) continue;
    bool isHeld = isTouchKeyHeld(b.key);
    
    if (b.key == KEY_TOGGLE_TOUCH_UI || b.key == KEY_TOGGLE_ORIENTATION ||
        b.key == KEY_TOGGLE_TOUCH_ASPECT || b.key == KEY_TOGGLE_TOUCH_FPS) {
      Uint8 alpha = isHeld ? 255 : (s_touchOverlayEnabled ? 150 : 110);
      SDL_SetTextureAlphaMod(b.tex, alpha);
    } else {
      SDL_SetTextureAlphaMod(b.tex, isHeld ? 255 : 155);
    }

    SDL_Rect dst = { b.cx - b.w / 2, b.cy - b.h / 2, b.w, b.h };
    SDL_RenderCopy(s_renderer, b.tex, nullptr, &dst);

    // Halo / Brilho Neon Cyan quando o botão é pressionado
    if (isHeld && s_texBtnGlow) {
      SDL_SetTextureAlphaMod(s_texBtnGlow, 220);
      int glowSz = (int)(std::max(b.w, b.h) * 1.35f);
      SDL_Rect dstGlow = { b.cx - glowSz / 2, b.cy - glowSz / 2, glowSz, glowSz };
      SDL_RenderCopy(s_renderer, s_texBtnGlow, nullptr, &dstGlow);
    }
  }
}

static int getHeldDirection() {
  if (s_touchHeldDirection != 0) return s_touchHeldDirection;
  const Uint8* k = SDL_GetKeyboardState(nullptr);
  if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP]) return -1;
  if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN]) return -2;
  if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT]) return -3;
  if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) return -4;
  if (k[SDL_SCANCODE_KP_2]) return 50;
  if (k[SDL_SCANCODE_KP_8]) return 56;
  if (k[SDL_SCANCODE_KP_4]) return 52;
  if (k[SDL_SCANCODE_KP_6]) return 54;

  return getGamepadHeldDirection();
}

static void cyclePotionPrev(VM& vm) {
  if (g_display && g_display->current) {
    vm.gilLock();
    try {
      Value args[1]; args[0].i = 35; Value ret[2];
      // '#' invocado 3 vezes avança 3 slots, equivalente a recuar 1 slot (4 - 1 = 3)
      for (int rep = 0; rep < 3; ++rep) {
        vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
        vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
      }
    } catch (...) {}
    vm.gilUnlock();
  }
}

static void cyclePotionNext(VM& vm) {
  if (g_display && g_display->current) {
    vm.gilLock();
    try {
      Value args[1]; args[0].i = 35; Value ret[2];
      vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
      vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
    } catch (...) {}
    vm.gilUnlock();
  }
}

static void getTouchCoords(const SDL_TouchFingerEvent& tf, int winW, int winH, float& outX, float& outY, int& outW, int& outH) {
#ifdef __SWITCH__
  if (s_screenRotation == 1) { // 90° CW
    outW = 720;
    outH = 1280;
    outX = tf.y * 720.0f;
    outY = (1.0f - tf.x) * 1280.0f;
    return;
  } else if (s_screenRotation == 2) { // 270° CCW / Flip Grip
    outW = 720;
    outH = 1280;
    outX = (1.0f - tf.y) * 720.0f;
    outY = tf.x * 1280.0f;
    return;
  }
#endif
  outW = winW;
  outH = winH;
  outX = tf.x * winW;
  outY = tf.y * winH;
}

bool Platform::pollEvents(VM& vm) {
  s_lastVm = &vm;
  if (s_quit) return false;
#ifdef __SWITCH__
  if (!appletMainLoop()) {
    s_quit = true;
    return false;
  }
#endif
  CloudSave::update(&vm);
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    // Se o diálogo modal do Updater estiver ativo, direciona os eventos com prioridade total
    if (Updater::isPromptActive()) {
      int key = 0;
      if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER ||
            ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_5 || ev.key.keysym.sym == SDLK_KP_5) {
          key = 53; // Confirmar
        } else if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_BACKSPACE ||
                   ev.key.keysym.sym == SDLK_7 || ev.key.keysym.sym == SDLK_KP_7) {
          key = -7; // Cancelar
        }
      } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
        int mkey = mapControllerButton(ev.cbutton.button);
        if (mkey == 53) {
          key = 53; // Confirmar (A no Switch, A no Xbox/PC)
        } else if (mkey == -7 || mkey == -8) {
          key = -7; // Cancelar (B no Switch, B no Xbox/PC)
        }
      } else if (ev.type == SDL_FINGERDOWN) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(s_window, &winW, &winH);
        float tx = 0, ty = 0;
        int tw = 0, th = 0;
        getTouchCoords(ev.tfinger, winW, winH, tx, ty, tw, th);
        Updater::handleClick((int)tx, (int)ty);
        continue;
      } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
        Updater::handleClick(ev.button.x, ev.button.y);
        continue;
      }

      if (key != 0) {
        Updater::handleInput(key);
      }
      continue; // Bloqueia propagação para o motor de jogo enquanto o modal estiver aberto
    }

    // Se o diálogo modal do CloudSave estiver ativo, direciona os eventos com prioridade total
    if (CloudSave::isModalActive()) {
      int key = 0;
      if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_KP_ENTER ||
            ev.key.keysym.sym == SDLK_SPACE || ev.key.keysym.sym == SDLK_5 || ev.key.keysym.sym == SDLK_KP_5) {
          key = 53; // Confirmar
        } else if (ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_BACKSPACE ||
                   ev.key.keysym.sym == SDLK_7 || ev.key.keysym.sym == SDLK_KP_7) {
          key = -7; // Cancelar
        } else if (ev.key.keysym.sym == SDLK_1 || ev.key.keysym.sym == SDLK_KP_1) {
          key = 49; // 1 (Backup)
        } else if (ev.key.keysym.sym == SDLK_2 || ev.key.keysym.sym == SDLK_KP_2) {
          key = 50; // 2 (Restaurar)
        } else if (ev.key.keysym.sym == SDLK_3 || ev.key.keysym.sym == SDLK_KP_3) {
          key = 51; // 3 (Desconectar)
        }
      } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
        int mkey = mapControllerButton(ev.cbutton.button);
        if (mkey == 53) {
          key = 53; // Confirmar (A no Switch, A no Xbox/PC)
        } else if (mkey == -7 || mkey == -8) {
          key = -7; // Cancelar (B no Switch, B no Xbox/PC)
        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_X) {
          key = 50; // X -> Restaurar
        } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_Y) {
          key = 51; // Y -> Desconectar
        }
      } else if (ev.type == SDL_FINGERDOWN) {
        int winW = 0, winH = 0;
        SDL_GetWindowSize(s_window, &winW, &winH);
        float tx = 0, ty = 0;
        int tw = 0, th = 0;
        getTouchCoords(ev.tfinger, winW, winH, tx, ty, tw, th);
        CloudSave::handleClick((int)tx, (int)ty, &vm);
        continue;
      } else if (ev.type == SDL_MOUSEBUTTONDOWN) {
        CloudSave::handleClick(ev.button.x, ev.button.y, &vm);
        continue;
      }

      if (key != 0) {
        CloudSave::handleInput(key, &vm);
      }
      continue; // Bloqueia propagação para o motor de jogo enquanto o modal estiver aberto
    }

    if (ev.type == SDL_QUIT) {
      s_quit = true;
      return false;
    }

    else if (ev.type == SDL_WINDOWEVENT) {
      if (ev.window.event == SDL_WINDOWEVENT_RESIZED || ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        if (!s_isFullscreen) {
          s_savedWinW = ev.window.data1;
          s_savedWinH = ev.window.data2;
          saveSettings();
        }
      }
    }

    // Gerenciamento de conexão quente de Gamepads (Hotplug)
    if (ev.type == SDL_CONTROLLERDEVICEADDED) {
      int idx = ev.cdevice.which;
      if (SDL_IsGameController(idx)) {
        SDL_GameController* pad = SDL_GameControllerOpen(idx);
        if (pad) {
          s_controllers.push_back(pad);
          printf("[Gamepad] Controle conectado: %s (slot %d)\n", SDL_GameControllerName(pad), idx);
          SDL_GameControllerRumble(pad, 0x4000, 0x4000, 150);
        }
      }
    } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
      SDL_JoystickID jid = ev.cdevice.which;
      for (auto it = s_controllers.begin(); it != s_controllers.end(); ) {
        SDL_Joystick* j = SDL_GameControllerGetJoystick(*it);
        if (j && SDL_JoystickInstanceID(j) == jid) {
          printf("[Gamepad] Controle desconectado: %s\n", SDL_GameControllerName(*it));
          SDL_GameControllerClose(*it);
          it = s_controllers.erase(it);
        } else {
          ++it;
        }
      }
    }

    // Botões do Gamepad
    else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
      if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
        // SELECT segurado: inicia monitoramento de combo
        s_selectButtonHeld = true;
        s_selectUsedInCombo = false;
        continue;
      } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        // Combo SELECT + START -> Alterna Proporção (3:4 Original <-> 16:9 True Widescreen)
        if (s_selectButtonHeld) {
          s_selectUsedInCombo = true;
          s_startUsedInCombo = true;
          Platform::toggleAspect(&vm);
          continue;
        }
        s_startUsedInCombo = false;
      } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
        // Combo SELECT + R3 -> Alterna Moldura (Bezel: Soltia <-> Ardósia <-> Preto)
        // R3 isolado -> Alterna Velocidade (15 FPS Padrão <-> 30 FPS Turbo)
        if (s_selectButtonHeld) {
          s_selectUsedInCombo = true;
          Platform::toggleBezel();
        } else {
          Platform::toggleFps();
        }
        continue;
      } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
        Platform::toggleAspect(&vm);
        continue;
      }
      int key = mapControllerButton(ev.cbutton.button);
      if (key != 0 && g_display && g_display->current) {
        if (isDirectionKey(key)) {
          if (s_activeDirection != key) {
            s_activeDirection = key;
            s_directionPressTime = SDL_GetTicks();
            s_directionLastRepeatTime = s_directionPressTime;
            vm.gilLock();
            try {
              Value args[1]; args[0].i = key; Value ret[2];
              vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          }
        } else {
          vm.gilLock();
          try {
            Value args[1]; args[0].i = key; Value ret[2];
            vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
          } catch (...) {}
          vm.gilUnlock();
        }
      }
    } else if (ev.type == SDL_CONTROLLERBUTTONUP) {
      if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK) {
        s_selectButtonHeld = false;
        // Se SELECT foi solto sem uso em combo, envia comando do Minimapa ('0')
        if (!s_selectUsedInCombo && g_display && g_display->current) {
          vm.gilLock();
          try {
            Value args[1]; args[0].i = 48; Value ret[2];
            vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
            vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
          } catch (...) {}
          vm.gilUnlock();
        }
        continue;
      } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
        if (s_startUsedInCombo) {
          s_startUsedInCombo = false;
          continue;
        }
      } else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
        continue;
      }
      int key = mapControllerButton(ev.cbutton.button);
      if (key != 0 && g_display && g_display->current) {
        if (isDirectionKey(key)) {
          int held = getHeldDirection();
          if (held != 0) {
            s_activeDirection = held;
            s_directionPressTime = SDL_GetTicks();
            s_directionLastRepeatTime = s_directionPressTime;
            vm.gilLock();
            try {
              Value args[1]; args[0].i = held; Value ret[2];
              vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          } else {
            s_activeDirection = 0;
            vm.gilLock();
            try {
              Value args[1]; args[0].i = key; Value ret[2];
              vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          }
        } else {
          vm.gilLock();
          try {
            Value args[1]; args[0].i = key; Value ret[2];
            vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
          } catch (...) {}
          vm.gilUnlock();
        }
      }
    }

    // Eixos Analógicos do Gamepad (Gatilhos L2/R2 e Analógico Esquerdo)
    else if (ev.type == SDL_CONTROLLERAXISMOTION) {
      if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
        bool down = ev.caxis.value > 16000;
        if (down != s_ltHeld) {
          s_ltHeld = down;
          if (down) {
            cyclePotionPrev(vm); // LT -> Alternar Poção / Item para a ESQUERDA (slot anterior)
          }
        }
      } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
        bool down = ev.caxis.value > 16000;
        if (down != s_rtHeld) {
          s_rtHeld = down;
          if (down) {
            cyclePotionNext(vm); // RT -> Alternar Poção / Item para a DIREITA (slot seguinte)
          }
        }
      } else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX || ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) {
        int held = getGamepadHeldDirection();
        if (held != s_activeDirection) {
          if (held != 0) {
            s_activeDirection = held;
            s_directionPressTime = SDL_GetTicks();
            s_directionLastRepeatTime = s_directionPressTime;
            if (g_display && g_display->current) {
              vm.gilLock();
              try {
                Value args[1]; args[0].i = held; Value ret[2];
                vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
              } catch (...) {}
              vm.gilUnlock();
            }
          } else if (s_activeDirection != 0 && getHeldDirection() == 0) {
            int oldKey = s_activeDirection;
            s_activeDirection = 0;
            if (g_display && g_display->current) {
              vm.gilLock();
              try {
                Value args[1]; args[0].i = oldKey; Value ret[2];
                vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
              } catch (...) {}
              vm.gilUnlock();
            }
          }
        }
      }
    }

    // Teclado
    else if (ev.type == SDL_KEYDOWN) {
      if (ev.key.keysym.sym == SDLK_F2) {
        if (!ev.key.repeat) Platform::nextLanguage(&vm);
        continue;
      } else if (ev.key.keysym.sym == SDLK_F4) {
        if (!ev.key.repeat) Platform::openCloudSave(&vm);
        continue;
      } else if (ev.key.keysym.sym == SDLK_F5) {
        if (!ev.key.repeat) Platform::toggleBezel();
        continue;
      } else if (ev.key.keysym.sym == SDLK_F6 || ev.key.keysym.sym == SDLK_F8) {
        if (!ev.key.repeat) Platform::toggleFps();
        continue;
      } else if (ev.key.keysym.sym == SDLK_F7 || ev.key.keysym.sym == SDLK_F10) {
        if (!ev.key.repeat) Platform::toggleAspect(&vm);
        continue;
      } else if (ev.key.keysym.sym == SDLK_F9) {
        if (!ev.key.repeat) Platform::checkForUpdates();
        continue;
      } else if (ev.key.keysym.sym == SDLK_F11 ||
                 (ev.key.keysym.sym == SDLK_RETURN && (ev.key.keysym.mod & KMOD_ALT))) {
        if (!ev.key.repeat) Platform::toggleFullscreen();
        continue;
      }
      int key = mapKey(ev.key.keysym.sym);
      if (key == -101) {
        if (!ev.key.repeat) {
          cyclePotionPrev(vm);
        }
      } else if (key != 0 && g_display && g_display->current) {
        if (isDirectionKey(key)) {
          if (!ev.key.repeat || s_activeDirection != key) {
            s_activeDirection = key;
            s_directionPressTime = SDL_GetTicks();
            s_directionLastRepeatTime = s_directionPressTime;
            vm.gilLock();
            try {
              Value args[1];
              args[0].i = key;
              Value ret[2];
              vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          }
        } else if (!ev.key.repeat) {
          vm.gilLock();
          try {
            Value args[1];
            args[0].i = key;
            Value ret[2];
            vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
          } catch (...) {}
          vm.gilUnlock();
        }
      }
    } else if (ev.type == SDL_KEYUP) {
      int key = mapKey(ev.key.keysym.sym);
      if (key != 0 && g_display && g_display->current) {
        if (isDirectionKey(key)) {
          int held = getHeldDirection();
          if (held != 0) {
            s_activeDirection = held;
            s_directionPressTime = SDL_GetTicks();
            s_directionLastRepeatTime = s_directionPressTime;
            vm.gilLock();
            try {
              Value args[1];
              args[0].i = held;
              Value ret[2];
              vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          } else {
            s_activeDirection = 0;
            vm.gilLock();
            try {
              Value args[1];
              args[0].i = key;
              Value ret[2];
              vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          }
        } else {
          vm.gilLock();
          try {
            Value args[1];
            args[0].i = key;
            Value ret[2];
            vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
          } catch (...) {}
          vm.gilUnlock();
        }
      }
    }

#if !defined(__ANDROID__) && !defined(__SWITCH__)
    // Cliques de mouse no PC Desktop (ignora toques sintetizados de touchscreen)
    else if (ev.type == SDL_MOUSEBUTTONDOWN) {
      if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT && g_display && g_display->current) {
        vm.gilLock();
        try {
          Value args[1]; args[0].i = 53; Value ret[2];
          vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
        } catch (...) {}
        vm.gilUnlock();
      }
    }
    else if (ev.type == SDL_MOUSEBUTTONUP) {
      if (ev.button.which != SDL_TOUCH_MOUSEID && ev.button.button == SDL_BUTTON_LEFT && g_display && g_display->current) {
        vm.gilLock();
        try {
          Value args[1]; args[0].i = 53; Value ret[2];
          vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
        } catch (...) {}
        vm.gilUnlock();
      }
    }
#endif

    // Toques na tela (Touchscreen Mobile / Virtual Controller)
    else if (ev.type == SDL_FINGERDOWN) {
      int winW = 0, winH = 0;
      SDL_GetRendererOutputSize(s_renderer, &winW, &winH);
      if (winW <= 0 || winH <= 0) SDL_GetWindowSize(s_window, &winW, &winH);
      float touchX = 0, touchY = 0;
      int layoutW = 0, layoutH = 0;
      getTouchCoords(ev.tfinger, winW, winH, touchX, touchY, layoutW, layoutH);
      GamepadLayout layout = calculateLayout(layoutW, layoutH);

      int key = hitTestTouch(touchX, touchY, layout);
      if (key == KEY_TOGGLE_TOUCH_UI) {
        s_touchOverlayEnabled = !s_touchOverlayEnabled;
        if (!s_touchOverlayEnabled) {
          // Solta direcional ou teclas mantidas para não travar andando
          if (s_activeDirection != 0 && g_display && g_display->current) {
            vm.gilLock();
            try {
              Value args[1]; args[0].i = s_activeDirection; Value ret[2];
              vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          }
          s_activeDirection = 0;
          s_touchHeldDirection = 0;
          s_activeFingers.clear();
        }
        Platform::rumble(0.20f, 40);
      } else if (key == KEY_TOGGLE_ORIENTATION) {
        s_screenRotation = (s_screenRotation + 1) % 3;
        Platform::rumble(0.25f, 50);
        const char* rotNames[] = {
          "Orientacao: Paisagem Normal (1280x720)",
          "Orientacao: Retrato Vertical (TATE 90)",
          "Orientacao: Retrato Invertido (Flip Grip 270)"
        };
        Platform::showOsdMessage(rotNames[s_screenRotation]);
      } else if (key == KEY_TOGGLE_TOUCH_ASPECT) {
        Platform::toggleAspect(&vm);
        Platform::rumble(0.20f, 40);
      } else if (key == KEY_TOGGLE_TOUCH_FPS) {
        Platform::toggleFps();
        Platform::rumble(0.20f, 40);
      } else if (key != 0 && s_touchOverlayEnabled) {
        s_activeFingers.push_back({ev.tfinger.fingerId, key});
        if (key == -101) {
          cyclePotionPrev(vm);
        } else if (key == 35) {
          cyclePotionNext(vm);
        } else if (isDirectionKey(key)) {
          s_touchHeldDirection = key;
          if (s_activeDirection != key) {
            s_activeDirection = key;
            s_directionPressTime = SDL_GetTicks();
            s_directionLastRepeatTime = s_directionPressTime;
            if (g_display && g_display->current) {
              vm.gilLock();
              try {
                Value args[1]; args[0].i = key; Value ret[2];
                vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
              } catch (...) {}
              vm.gilUnlock();
            }
          }
        } else {
          if (g_display && g_display->current) {
            vm.gilLock();
            try {
              Value args[1]; args[0].i = key; Value ret[2];
              vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
            } catch (...) {}
            vm.gilUnlock();
          }
        }
      }
    } else if (ev.type == SDL_FINGERMOTION) {
      int winW = 0, winH = 0;
      SDL_GetRendererOutputSize(s_renderer, &winW, &winH);
      if (winW <= 0 || winH <= 0) SDL_GetWindowSize(s_window, &winW, &winH);
      float touchX = 0, touchY = 0;
      int layoutW = 0, layoutH = 0;
      getTouchCoords(ev.tfinger, winW, winH, touchX, touchY, layoutW, layoutH);
      GamepadLayout layout = calculateLayout(layoutW, layoutH);

      for (auto& f : s_activeFingers) {
        if (f.id == ev.tfinger.fingerId) {
          int newKey = hitTestTouch(touchX, touchY, layout);
          if (newKey == KEY_TOGGLE_TOUCH_UI || newKey == KEY_TOGGLE_ORIENTATION ||
              newKey == KEY_TOGGLE_TOUCH_ASPECT || newKey == KEY_TOGGLE_TOUCH_FPS) newKey = 0;
          if (newKey != f.key) {
            int oldKey = f.key;
            f.key = newKey;
            if (isDirectionKey(oldKey)) {
              if (isDirectionKey(newKey)) {
                s_touchHeldDirection = newKey;
                s_activeDirection = newKey;
                s_directionPressTime = SDL_GetTicks();
                s_directionLastRepeatTime = s_directionPressTime;
                if (g_display && g_display->current) {
                  vm.gilLock();
                  try {
                    Value args[1]; args[0].i = newKey; Value ret[2];
                    vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
                  } catch (...) {}
                  vm.gilUnlock();
                }
              } else {
                s_touchHeldDirection = 0;
                if (g_display && g_display->current) {
                  vm.gilLock();
                  try {
                    Value args[1]; args[0].i = oldKey; Value ret[2];
                    vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
                  } catch (...) {}
                  vm.gilUnlock();
                }
              }
            } else if (oldKey != 0 && oldKey != -101 && oldKey != 35) {
              if (g_display && g_display->current) {
                vm.gilLock();
                try {
                  Value args[1]; args[0].i = oldKey; Value ret[2];
                  vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
                } catch (...) {}
                vm.gilUnlock();
              }
            }

            if (newKey != 0 && !isDirectionKey(newKey) && newKey != -101 && newKey != 35) {
              if (g_display && g_display->current) {
                vm.gilLock();
                try {
                  Value args[1]; args[0].i = newKey; Value ret[2];
                  vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
                } catch (...) {}
                vm.gilUnlock();
              }
            }
          }
          break;
        }
      }
    } else if (ev.type == SDL_FINGERUP) {
      for (auto it = s_activeFingers.begin(); it != s_activeFingers.end(); ) {
        if (it->id == ev.tfinger.fingerId) {
          int key = it->key;
          it = s_activeFingers.erase(it);
          if (isDirectionKey(key)) {
            int otherDir = 0;
            for (const auto& af : s_activeFingers) {
              if (isDirectionKey(af.key)) { otherDir = af.key; break; }
            }
            s_touchHeldDirection = otherDir;
            if (otherDir == 0) {
              s_activeDirection = 0;
              if (g_display && g_display->current) {
                vm.gilLock();
                try {
                  Value args[1]; args[0].i = key; Value ret[2];
                  vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
                } catch (...) {}
                vm.gilUnlock();
              }
            }
          } else if (key != 0 && key != -101 && key != 35) {
            if (g_display && g_display->current) {
              vm.gilLock();
              try {
                Value args[1]; args[0].i = key; Value ret[2];
                vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
              } catch (...) {}
              vm.gilUnlock();
            }
          }
        } else {
          ++it;
        }
      }
    }
  }

  // Movimentação contínua fluida: enquanto uma direção for mantida pressionada (seja por teclado ou gamepad),
  // alimenta o loop do jogo com repetições suaves no ritmo exato da taxa de passos
  if (s_activeDirection != 0 && g_display && g_display->current) {
    int held = getHeldDirection();
    if (held == 0) {
      int oldKey = s_activeDirection;
      s_activeDirection = 0;
      vm.gilLock();
      try {
        Value args[1];
        args[0].i = oldKey;
        Value ret[2];
        vm.invokeVirtual(g_display->current, "keyReleased:(I)V", args, 1, ret);
      } catch (...) {}
      vm.gilUnlock();
    } else {
      if (held != s_activeDirection) {
        s_activeDirection = held;
        s_directionPressTime = SDL_GetTicks();
        s_directionLastRepeatTime = s_directionPressTime;
        vm.gilLock();
        try {
          Value args[1];
          args[0].i = held;
          Value ret[2];
          vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
        } catch (...) {}
        vm.gilUnlock();
      } else {
        uint32_t now = SDL_GetTicks();
        const uint32_t INITIAL_DELAY_MS = 160;
        const uint32_t REPEAT_INTERVAL_MS = 40;
        if (now - s_directionPressTime >= INITIAL_DELAY_MS && now - s_directionLastRepeatTime >= REPEAT_INTERVAL_MS) {
          s_directionLastRepeatTime = now;
          vm.gilLock();
          try {
            Value args[1];
            args[0].i = s_activeDirection;
            Value ret[2];
            vm.invokeVirtual(g_display->current, "keyPressed:(I)V", args, 1, ret);
          } catch (...) {}
          vm.gilUnlock();
        }
      }
    }
  }

  return !s_quit;
}

static void applyAspectWidth(int newWidth, VM* pVm) {
  if (g_screenWidth == newWidth && s_screenTexture) return;

  g_screenWidth = newWidth;
  g_screenHeight = SCREEN_HEIGHT;

  if (s_renderer) {
    if (s_screenTexture) SDL_DestroyTexture(s_screenTexture);
    s_screenTexture = SDL_CreateTexture(
        s_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        g_screenWidth, g_screenHeight);
    if (s_screenTexture) {
      SDL_SetTextureBlendMode(s_screenTexture, SDL_BLENDMODE_NONE);
    }
  }

  if (pVm) {
    updateJavaViewportVariables(*pVm);
  }

  memset(g_screenBuffer, 0, sizeof(g_screenBuffer));
}

void Platform::present() {
  if (!s_renderer) return;

  if (s_aspectMode == ASPECT_AUTO) {
    int targetW = determineTargetWidth(ASPECT_AUTO);
    if (g_screenWidth != targetW) {
      applyAspectWidth(targetW, s_lastVm);
    }
  }

  if (!s_screenTexture) return;

  void* pixels = nullptr;
  int pitch = 0;
  if (SDL_LockTexture(s_screenTexture, nullptr, &pixels, &pitch) == 0) {
    for (int y = 0; y < g_screenHeight; y++) {
      memcpy((uint8_t*)pixels + y * pitch, g_screenBuffer + y * g_screenWidth, g_screenWidth * sizeof(uint32_t));
    }
    SDL_UnlockTexture(s_screenTexture);
  } else {
    SDL_UpdateTexture(s_screenTexture, nullptr, g_screenBuffer, g_screenWidth * sizeof(uint32_t));
  }

  int winW = 0, winH = 0;
  SDL_GetRendererOutputSize(s_renderer, &winW, &winH);
  if (winW <= 0 || winH <= 0) {
    SDL_GetWindowSize(s_window, &winW, &winH);
  }

  loadGamepadTextures();

#ifdef __SWITCH__
  if (s_screenRotation != 0) {
    if (!s_rotateTarget) {
      s_rotateTarget = SDL_CreateTexture(
          s_renderer,
          SDL_PIXELFORMAT_RGBA8888,
          SDL_TEXTUREACCESS_TARGET,
          720, 1280);
    }
    if (s_rotateTarget) {
      SDL_SetRenderTarget(s_renderer, s_rotateTarget);
      SDL_SetRenderDrawColor(s_renderer, 10, 12, 16, 255);
      SDL_RenderClear(s_renderer);

      GamepadLayout layout = calculateLayout(720, 1280);
      SDL_RenderCopy(s_renderer, s_screenTexture, nullptr, &layout.gameRect);
      drawGamepad(layout);
      drawOsd(720, 1280);

      SDL_SetRenderTarget(s_renderer, nullptr);
      SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
      SDL_RenderClear(s_renderer);

      double angle = (s_screenRotation == 1) ? 90.0 : 270.0;
      SDL_Rect dstRect = { 280, -280, 720, 1280 };
      SDL_RenderCopyEx(s_renderer, s_rotateTarget, nullptr, &dstRect, angle, nullptr, SDL_FLIP_NONE);
      SDL_RenderPresent(s_renderer);
      return;
    }
  }
#endif

  SDL_SetRenderDrawColor(s_renderer, 0, 0, 0, 255);
  SDL_RenderClear(s_renderer);

  // Calcula geometria da tela de jogo com escala perfeitamente quadrada (pixel-perfect) sem distorção
  float scaleX = (float)winW / (float)g_screenWidth;
  float scaleY = (float)winH / (float)g_screenHeight;
  float scale = std::min(scaleX, scaleY);
  int dstW = (int)((float)g_screenWidth * scale);
  int dstH = (int)((float)g_screenHeight * scale);
  SDL_Rect dstGame = { (winW - dstW) / 2, (winH - dstH) / 2, dstW, dstH };

  GamepadLayout layout = calculateLayout(winW, winH);
  if (layout.isPortrait) {
    dstGame = layout.gameRect;
  }

  // Renderiza moldura temática nas laterais (pillarbox) se houver espaço
  if (g_screenWidth == SCREEN_W_ORIGINAL && dstGame.x > 0) {
    SDL_Texture* activeBezel = nullptr;
    if (s_bezelMode == BEZEL_SOLTIA) activeBezel = s_texBezelSoltia;
    else if (s_bezelMode == BEZEL_SLATE) activeBezel = s_texBezelSlate;

    if (activeBezel) {
      // Textura base de 1920x1080: coluna esquerda = [0..555], coluna direita = [1365..1920]
      SDL_Rect srcLeft = { 0, 0, 555, 1080 };
      SDL_Rect dstLeft = { 0, 0, dstGame.x, winH };
      SDL_RenderCopy(s_renderer, activeBezel, &srcLeft, &dstLeft);

      SDL_Rect srcRight = { 1365, 0, 555, 1080 };
      SDL_Rect dstRight = { dstGame.x + dstGame.w, 0, winW - (dstGame.x + dstGame.w), winH };
      SDL_RenderCopy(s_renderer, activeBezel, &srcRight, &dstRight);
    }
  }

  // Renderiza o buffer do jogo J2ME 240x320
  SDL_RenderCopy(s_renderer, s_screenTexture, nullptr, &dstGame);

#if defined(__ANDROID__) || defined(__SWITCH__)
  {
    drawGamepad(layout);
  }
#else
  if (s_touchOverlayEnabled) {
    drawGamepad(layout);
  }
#endif

  // Desenha banner de notificação OSD
  drawOsd(winW, winH);

  // Renderiza diálogo modal do atualizador OTA se estiver ativo
  if (Updater::isPromptActive()) {
    Updater::drawModal(s_renderer, winW, winH);
  }

  // Renderiza diálogo modal do Cloud Save se estiver ativo
  if (CloudSave::isModalActive()) {
    CloudSave::drawModal(s_renderer, winW, winH);
  }

  SDL_RenderPresent(s_renderer);
}

void Platform::shutdown() {
  CloudSave::shutdown();
  Updater::shutdown();
  MidiSynth::shutdown();
  for (auto* pad : s_controllers) {
    if (pad) SDL_GameControllerClose(pad);
  }
  s_controllers.clear();
  if (s_rotateTarget) { SDL_DestroyTexture(s_rotateTarget); s_rotateTarget = nullptr; }
  if (s_texDpadBase) { SDL_DestroyTexture(s_texDpadBase); s_texDpadBase = nullptr; }
  if (s_texDpadArrowActive) { SDL_DestroyTexture(s_texDpadArrowActive); s_texDpadArrowActive = nullptr; }
  if (s_texBtn5) { SDL_DestroyTexture(s_texBtn5); s_texBtn5 = nullptr; }
  if (s_texBtn1) { SDL_DestroyTexture(s_texBtn1); s_texBtn1 = nullptr; }
  if (s_texBtn3) { SDL_DestroyTexture(s_texBtn3); s_texBtn3 = nullptr; }
  if (s_texBtn7) { SDL_DestroyTexture(s_texBtn7); s_texBtn7 = nullptr; }
  if (s_texBtn9) { SDL_DestroyTexture(s_texBtn9); s_texBtn9 = nullptr; }
  if (s_texBtnMenu) { SDL_DestroyTexture(s_texBtnMenu); s_texBtnMenu = nullptr; }
  if (s_texBtnMap) { SDL_DestroyTexture(s_texBtnMap); s_texBtnMap = nullptr; }
  if (s_texBtnRsk) { SDL_DestroyTexture(s_texBtnRsk); s_texBtnRsk = nullptr; }
  if (s_texBtnRskRound) { SDL_DestroyTexture(s_texBtnRskRound); s_texBtnRskRound = nullptr; }
  if (s_texBtnPrev) { SDL_DestroyTexture(s_texBtnPrev); s_texBtnPrev = nullptr; }
  if (s_texBtnNext) { SDL_DestroyTexture(s_texBtnNext); s_texBtnNext = nullptr; }
  if (s_texBtnEyeOpen) { SDL_DestroyTexture(s_texBtnEyeOpen); s_texBtnEyeOpen = nullptr; }
  if (s_texBtnEyeClosed) { SDL_DestroyTexture(s_texBtnEyeClosed); s_texBtnEyeClosed = nullptr; }
  if (s_texBtnRotate) { SDL_DestroyTexture(s_texBtnRotate); s_texBtnRotate = nullptr; }
  if (s_texBtnAspect) { SDL_DestroyTexture(s_texBtnAspect); s_texBtnAspect = nullptr; }
  if (s_texBtnFps) { SDL_DestroyTexture(s_texBtnFps); s_texBtnFps = nullptr; }
  if (s_texBtnGlow) { SDL_DestroyTexture(s_texBtnGlow); s_texBtnGlow = nullptr; }
  if (s_texBezelSoltia) { SDL_DestroyTexture(s_texBezelSoltia); s_texBezelSoltia = nullptr; }
  if (s_texBezelSlate) { SDL_DestroyTexture(s_texBezelSlate); s_texBezelSlate = nullptr; }
  if (s_texFontOsd) { SDL_DestroyTexture(s_texFontOsd); s_texFontOsd = nullptr; }
  if (s_ttf.texture) {
    SDL_DestroyTexture(s_ttf.texture);
    s_ttf.texture = nullptr;
    s_ttf.initialized = false;
    s_ttf.available = false;
  }
  s_gamepadTexturesLoaded = false;
  if (s_screenTexture) { SDL_DestroyTexture(s_screenTexture); s_screenTexture = nullptr; }
  if (s_renderer) { SDL_DestroyRenderer(s_renderer); s_renderer = nullptr; }
  if (s_window) { SDL_DestroyWindow(s_window); s_window = nullptr; }
  Mix_CloseAudio();
  SDL_Quit();
  Platform::cleanupRomfs();
}

void Platform::cleanupRomfs() {
#ifdef __SWITCH__
  if (s_romfsInitialized) {
    romfsExit();
    s_romfsInitialized = false;
  }
#endif
}

void Platform::toggleBezel() {
  s_bezelMode = static_cast<BezelMode>((s_bezelMode + 1) % BEZEL_COUNT);
  saveSettings();
  loadBezelTextures();
  const char* names[] = {
    "Moldura: Soltia Ancestral",
    "Moldura: Ardosia Escura",
    "Moldura: Preto Classico"
  };
  showOsdMessage(names[s_bezelMode]);
}

void Platform::toggleFps() {
  s_targetFps = (s_targetFps == 15) ? 30 : 15;
  saveSettings();
  if (s_targetFps == 15) {
    showOsdMessage("Taxa de Quadros: 15 FPS (Padrao Original J2ME)");
  } else {
    showOsdMessage("Taxa de Quadros: 30 FPS (Modo Turbo / Fluido)");
  }
}

int Platform::getTargetFps() {
  return s_targetFps;
}

void Platform::setAspectMode(int mode, VM* pVm) {
  if (mode < 0 || mode >= ASPECT_COUNT) mode = ASPECT_AUTO;
  s_aspectMode = static_cast<AspectMode>(mode);
  saveSettings();

  int targetW = determineTargetWidth(s_aspectMode);
  applyAspectWidth(targetW, pVm ? pVm : s_lastVm);

  if (s_aspectMode == ASPECT_AUTO) {
    if (targetW == SCREEN_W_WIDESCREEN) {
      showOsdMessage("Proporcao: Auto (16:9 Widescreen Detectado)");
    } else {
      showOsdMessage("Proporcao: Auto (3:4 Retrato Detectado)");
    }
  } else if (s_aspectMode == ASPECT_ORIGINAL) {
    showOsdMessage("Proporcao: 3:4 Original (Molduras Soltia)");
  } else {
    showOsdMessage("Proporcao: 16:9 True Widescreen (Mais Mapa)");
  }
}

void Platform::nextAspect(VM* pVm) {
  int next = (static_cast<int>(s_aspectMode) + 1) % ASPECT_COUNT;
  setAspectMode(next, pVm);
}

void Platform::prevAspect(VM* pVm) {
  int prev = (static_cast<int>(s_aspectMode) + ASPECT_COUNT - 1) % ASPECT_COUNT;
  setAspectMode(prev, pVm);
}

int Platform::getAspectMode() {
  return static_cast<int>(s_aspectMode);
}

void Platform::toggleAspect(VM* pVm) {
  nextAspect(pVm);
}

void Platform::toggleFullscreen() {
#ifndef __SWITCH__
  if (!s_window) return;
  s_isFullscreen = !s_isFullscreen;
  SDL_SetWindowFullscreen(s_window, s_isFullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
  saveSettings();
  showOsdMessage(s_isFullscreen ? "Tela Cheia: Ativada" : "Tela Cheia: Modo Janela");
#endif
}

void Platform::showOsdMessage(const std::string& msg) {
  s_osdMessage = msg;
  s_osdExpireTime = SDL_GetTicks() + 2500;
  printf("[OSD] %s\n", msg.c_str());
}

void Platform::framePacerWait() {
  if (s_frameStartCounter == 0) {
    s_frameStartCounter = SDL_GetPerformanceCounter();
    return;
  }

  uint64_t freq = SDL_GetPerformanceFrequency();
  double targetSec = 1.0 / static_cast<double>(s_targetFps);
  uint64_t targetCounts = static_cast<uint64_t>(targetSec * static_cast<double>(freq));

  uint64_t now = SDL_GetPerformanceCounter();
  uint64_t elapsedCounts = now - s_frameStartCounter;

  if (elapsedCounts < targetCounts) {
    double remainingSec = static_cast<double>(targetCounts - elapsedCounts) / static_cast<double>(freq);
    double remainingMs = remainingSec * 1000.0;
    if (remainingMs > 2.0) {
      SDL_Delay(static_cast<Uint32>(remainingMs - 1.5));
    }
    while (true) {
      now = SDL_GetPerformanceCounter();
      if ((now - s_frameStartCounter) >= targetCounts) break;
      SDL_Delay(0);
    }
  }

  s_frameStartCounter = SDL_GetPerformanceCounter();
}

void Platform::rumble(float strength, int durationMs) {
  uint16_t low = (uint16_t)(std::min(1.0f, std::max(0.0f, strength)) * 0xFFFF);
  uint16_t high = (uint16_t)(std::min(1.0f, std::max(0.0f, strength)) * 0x7FFF);
  for (auto* pad : s_controllers) {
    if (pad) {
      SDL_GameControllerRumble(pad, low, high, durationMs);
    }
  }
}

bool Platform::shouldQuit() {
#ifdef __SWITCH__
  if (!appletMainLoop()) return true;
#endif
  return s_quit;
}

std::vector<uint8_t> Platform::readAsset(const std::string& path) {
  if (path.empty()) return {};

  std::string clean = path;
  while (!clean.empty() && (clean[0] == '/' || clean[0] == '\\')) {
    clean = clean.substr(1);
  }

  // Intercepta leitura de idioma para fornecer o pacote ativo (Passo 10)
  if (clean == "lang.en-GB" || clean.rfind("/lang.en-GB") != std::string::npos || clean.rfind("\\lang.en-GB") != std::string::npos) {
    std::string candidate = "lang/lang_" + s_currentLanguage + ".bin";
    std::vector<uint8_t> langData = Platform::readAsset(candidate);
    if (!langData.empty()) {
      return langData;
    }
    langData = Platform::readAsset("lang/lang_pt.bin");
    if (!langData.empty()) {
      return langData;
    }
  }

  // 1. Tenta o caminho exato
  SDL_RWops* rw = SDL_RWFromFile(clean.c_str(), "rb");
  
  // 2. Se falhar e começar com "./", tenta sem
  if (!rw && clean.rfind("./", 0) == 0) {
    rw = SDL_RWFromFile(clean.substr(2).c_str(), "rb");
  }
  
  // 3. Tenta prefixando "assets/"
  if (!rw) {
    std::string alt = "assets/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }

  // 4. Tenta prefixando "../assets/"
  if (!rw) {
    std::string alt = "../assets/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }

  // 5. Tenta prefixando "reference/extracted/"
  if (!rw) {
    std::string alt = "reference/extracted/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }

  // 6. Tenta prefixando "../reference/extracted/"
  if (!rw) {
    std::string alt = "../reference/extracted/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }

#ifdef __SWITCH__
  // 5. No Nintendo Switch, tenta via romfs:/
  if (!rw) {
    std::string alt = "romfs:/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }
  if (!rw) {
    std::string alt = "romfs:/reference/extracted/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }
  if (!rw) {
    std::string alt = "romfs:/extracted/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }
  if (!rw) {
    std::string alt = "romfs:/assets/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }
  if (!rw) {
    std::string alt = "romfs:/ui/" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }
#endif

  // 6. Tenta em ../assets/ ou ../reference/extracted/
  if (!rw) {
    std::string alt = "../" + clean;
    rw = SDL_RWFromFile(alt.c_str(), "rb");
  }

  if (!rw) return {};

  Sint64 size = SDL_RWsize(rw);
  if (size <= 0) {
    SDL_RWclose(rw);
    return {};
  }

  std::vector<uint8_t> data(static_cast<size_t>(size));
  size_t readBytes = SDL_RWread(rw, data.data(), 1, static_cast<size_t>(size));
  SDL_RWclose(rw);

  if (readBytes != static_cast<size_t>(size)) {
    data.resize(readBytes);
  }
  return data;
}

std::string Platform::getStorageDir() {
#ifdef __SWITCH__
  mkdir("sdmc:/switch", 0777);
  mkdir("sdmc:/switch/heroes_lore", 0777);
  return "sdmc:/switch/heroes_lore";
#elif defined(__ANDROID__)
  const char* path = SDL_AndroidGetInternalStoragePath();
  if (path && path[0] != '\0') return std::string(path);
  return ".";
#elif defined(__linux__)
  const char* xdg = getenv("XDG_DATA_HOME");
  std::string dir;
  if (xdg && xdg[0] != '\0') {
    dir = std::string(xdg) + "/heroes_lore";
  } else {
    const char* home = getenv("HOME");
    if (home && home[0] != '\0') {
      dir = std::string(home) + "/.local/share/heroes_lore";
    } else {
      dir = "/tmp/heroes_lore";
    }
  }
  mkdir(dir.c_str(), 0777);
  return dir;
#else
  return ".";
#endif
}

std::string Platform::getRmsDir(VM* vm) {
  static std::string s_cachedRmsDir;

  std::string base = getStorageDir();
  if (base != ".") {
    std::string d = base + "/rms";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    return d;
  }

  // No PC (Windows/Desktop onde getStorageDir() retorna ".")
  if (vm && !vm->dataDir.empty()) {
    std::string d = vm->dataDir + "/rms";
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    s_cachedRmsDir = d;
    return d;
  }

  if (!s_cachedRmsDir.empty() && std::filesystem::exists(s_cachedRmsDir)) {
    return s_cachedRmsDir;
  }

  // Procura pastas RMS que já possuam saves de jogo (.rms)
  const char* candidates[] = {
    "assets/rms",
    "build/assets/rms",
    "reference/extracted/rms",
    "build/reference/extracted/rms",
    "../reference/extracted/rms",
    "../assets/rms",
    "rms"
  };
  for (const char* c : candidates) {
    if (std::filesystem::exists(std::string(c) + "/_k.rms") ||
        std::filesystem::exists(std::string(c) + "/_c.rms") ||
        std::filesystem::exists(std::string(c) + "/_s.rms") ||
        std::filesystem::exists(std::string(c) + "/_w.rms") ||
        std::filesystem::exists(std::string(c) + "/_o.rms")) {
      s_cachedRmsDir = c;
      return c;
    }
  }

  if (std::filesystem::exists("assets")) {
    s_cachedRmsDir = "assets/rms";
  } else if (std::filesystem::exists("build/assets")) {
    s_cachedRmsDir = "build/assets/rms";
  } else if (std::filesystem::exists("reference/extracted")) {
    s_cachedRmsDir = "reference/extracted/rms";
  } else {
    s_cachedRmsDir = "rms";
  }

  std::error_code ec;
  std::filesystem::create_directories(s_cachedRmsDir, ec);
  return s_cachedRmsDir;
}

void Platform::checkForUpdates() {
  Updater::checkAsync(true);
}

void Platform::drawTextColored(void* rendererPtr, const std::string& text, int x, int y, int fontSize, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  SDL_Renderer* rend = (SDL_Renderer*)rendererPtr;
  if (!rend) rend = s_renderer;
  if (!rend || text.empty()) return;

  if (initTtfFont(rend) && s_ttf.available) {
    float scale = (float)fontSize / s_ttf.bakedSize;
    float curX = (float)x;
    float baselineY = (float)y + fontSize * 0.82f;

    SDL_SetTextureColorMod(s_ttf.texture, r, g, b);
    SDL_SetTextureAlphaMod(s_ttf.texture, a);

    for (size_t i = 0; i < text.length(); ) {
      uint32_t cp = decodeNextUtf8(text, i);
      if (cp == ' ') {
        curX += (14.0f * scale);
        continue;
      }
      if (cp < 32 || cp > 255) continue;
      const auto& b = s_ttf.bakedChars[cp - 32];

      SDL_Rect src = { (int)b.x0, (int)b.y0, (int)(b.x1 - b.x0), (int)(b.y1 - b.y0) };
      SDL_Rect dst = {
        (int)std::round(curX + b.xoff * scale),
        (int)std::round(baselineY + b.yoff * scale),
        (int)std::round((b.x1 - b.x0) * scale),
        (int)std::round((b.y1 - b.y0) * scale)
      };
      SDL_RenderCopy(rend, s_ttf.texture, &src, &dst);
      curX += (b.xadvance * scale);
    }
    return;
  }

  Platform::drawText(rend, text, x, y, (int)(fontSize * 0.60f), fontSize, a);
}

void Platform::drawText(void* rendererPtr, const std::string& text, int x, int y, int charW, int charH, uint8_t alpha, int stepX) {
  SDL_Renderer* rend = (SDL_Renderer*)rendererPtr;
  if (!rend) rend = s_renderer;
  if (!rend || text.empty()) return;

  if (initTtfFont(rend) && s_ttf.available) {
    int fontSize = charH > 0 ? charH : charW;
    drawTextColored(rend, text, x, y, fontSize, 255, 255, 255, alpha);
    return;
  }

  if (!s_texFontOsd) {
    s_texFontOsd = loadRgbaTexture("font_osd");
  }
  if (!s_texFontOsd) return;

  int texW = 0, texH = 0;
  SDL_QueryTexture(s_texFontOsd, nullptr, nullptr, &texW, &texH);
  int srcCellW = (texW > 0) ? (texW / 16) : 22;
  int srcCellH = (texH > 0) ? (texH / 6) : 36;

  if (stepX <= 0) {
    stepX = charW;
  }

  SDL_SetTextureAlphaMod(s_texFontOsd, alpha);
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c < 32 || c > 126) c = ' ';
    int idx = c - 32;
    int col = idx % 16;
    int row = idx / 16;
    SDL_Rect src = { col * srcCellW, row * srcCellH, srcCellW, srcCellH };
    SDL_Rect dst = { x + (int)i * stepX, y, charW, charH };
    SDL_RenderCopy(rend, s_texFontOsd, &src, &dst);
  }
}

int Platform::getTextWidth(const std::string& text, int charW, int stepX) {
  if (text.empty()) return 0;
  if (!s_ttf.available && s_renderer) {
    initTtfFont(s_renderer);
  }
  if (s_ttf.available) {
    int fontSize = charW;
    float scale = (float)fontSize / s_ttf.bakedSize;
    float totalW = 0.0f;
    for (size_t i = 0; i < text.length(); ) {
      uint32_t cp = decodeNextUtf8(text, i);
      if (cp == ' ') {
        totalW += (14.0f * scale);
        continue;
      }
      if (cp < 32 || cp > 255) continue;
      const auto& b = s_ttf.bakedChars[cp - 32];
      totalW += (b.xadvance * scale);
    }
    return (int)std::ceil(totalW);
  }
  if (stepX <= 0) stepX = charW;
  return (int)(text.length() - 1) * stepX + charW;
}

// -------------------------------------------------------------
// Passo 10: Localização e Gerenciamento de Idiomas
// -------------------------------------------------------------
std::string Platform::getCurrentLanguage() {
  return s_currentLanguage;
}

void Platform::setLanguage(const std::string& langCode) {
  if (langCode == "pt" || langCode == "en" || langCode == "it" || langCode == "es") {
    s_currentLanguage = langCode;
    saveSettings();
  }
}

std::string Platform::getLanguageDisplayName(const std::string& langCode) {
  if (langCode == "pt") return "Portugues";
  if (langCode == "en") return "English";
  if (langCode == "it") return "Italiano";
  if (langCode == "es") return "Espanol";
  return langCode;
}

void Platform::nextLanguage(VM* vm) {
  const char* langs[] = { "pt", "en", "it", "es" };
  int cur = 0;
  for (int i = 0; i < 4; ++i) {
    if (s_currentLanguage == langs[i]) { cur = i; break; }
  }
  cur = (cur + 1) % 4;
  s_currentLanguage = langs[cur];
  saveSettings();
  if (vm) reloadLanguage(*vm);
  showOsdMessage("Idioma: " + getLanguageDisplayName(s_currentLanguage));
}

void Platform::prevLanguage(VM* vm) {
  const char* langs[] = { "pt", "en", "it", "es" };
  int cur = 0;
  for (int i = 0; i < 4; ++i) {
    if (s_currentLanguage == langs[i]) { cur = i; break; }
  }
  cur = (cur + 3) % 4;
  s_currentLanguage = langs[cur];
  saveSettings();
  if (vm) reloadLanguage(*vm);
  showOsdMessage("Idioma: " + getLanguageDisplayName(s_currentLanguage));
}

static std::string getBabbleString(const std::vector<uint8_t>& data, int id) {
  if (id < 0 || (size_t)(id * 4 + 4) > data.size()) return "";
  uint32_t relOffset = ((uint32_t)data[id * 4] << 24) |
                       ((uint32_t)data[id * 4 + 1] << 16) |
                       ((uint32_t)data[id * 4 + 2] << 8) |
                       ((uint32_t)data[id * 4 + 3]);
  size_t pos = (size_t)(id * 4) + 4 + (size_t)(int32_t)relOffset;
  if (pos + 4 > data.size()) return "";
  pos += 2; // pula block_len (uint16)
  uint16_t utfLen = ((uint16_t)data[pos] << 8) | data[pos + 1];
  pos += 2;
  if (pos + utfLen > data.size()) return "";
  std::string s(reinterpret_cast<const char*>(data.data() + pos), utfLen);
  for (char& c : s) if (c == ';') c = '\n';
  return s;
}

void Platform::reloadLanguage(VM& vm) {
  bool needUnlock = false;
  if (!vm.isGilOwner()) {
    vm.gilLock();
    needUnlock = true;
  }

  try {
    std::string candidate = "lang/lang_" + s_currentLanguage + ".bin";
    std::vector<uint8_t> langData = Platform::readAsset(candidate);
    if (langData.empty()) {
      langData = Platform::readAsset("lang/lang_pt.bin");
    }
    if (langData.size() >= 4) {
      // Pula os primeiros 4 bytes (cabeçalho n4 de tamanho do payload)
      std::vector<uint8_t> babbleBytes(langData.begin() + 4, langData.end());

      // 1. Atualiza buffer de strings do singleton cj.var_cj_a
      ClassInfo* cjClass = vm.findClass("cj");
      if (cjClass) {
        FieldInfo* fCjA = vm.findField(cjClass, "a:Lcj;");
        if (fCjA && fCjA->isStatic && fCjA->index >= 0 && fCjA->index < (int)cjClass->statics.size()) {
          Object* cjInstObj = cjClass->statics[fCjA->index].o;
          if (cjInstObj && cjInstObj->kind == K_INST) {
            Instance* cjInst = static_cast<Instance*>(cjInstObj);

            // Atualiza DataInputStream (campo a:Ljava/io/DataInputStream;)
            FieldInfo* fDisA = vm.findField(cjClass, "a:Ljava/io/DataInputStream;");
            if (fDisA && fDisA->index >= 0 && fDisA->index < (int)cjInst->f.size()) {
              Object* disObj = cjInst->f[fDisA->index].o;
              if (disObj && disObj->kind == K_DIS) {
                Dis* dis = static_cast<Dis*>(disObj);
                if (dis->in && dis->in->kind == K_BAIS) {
                  Bais* bais = static_cast<Bais*>(dis->in);
                  bais->data = babbleBytes;
                  bais->pos = 0;
                }
              }
            }

            // Atualiza byte[] (campo a:[B)
            FieldInfo* fByteArrA = vm.findField(cjClass, "a:[B");
            if (fByteArrA && fByteArrA->index >= 0 && fByteArrA->index < (int)cjInst->f.size()) {
              Object* byteArrObj = cjInst->f[fByteArrA->index].o;
              if (byteArrObj && byteArrObj->kind == K_ARRAY) {
                Array* byteArr = static_cast<Array*>(byteArrObj);
                byteArr->len = (int)babbleBytes.size();
                byteArr->data = babbleBytes;
              }
            }
          }
        }
      }

      // 2. Atualiza textos e matriz de botões da UI em bh
      ClassInfo* bhClass = vm.findClass("bh");
      if (bhClass) {
        auto makeCharArr = [&](int strId) -> Array* {
          std::string s = getBabbleString(babbleBytes, strId);
          std::u16string u16 = fromUtf8(s);
          Array* a = vm.newArray('C', (int)u16.size());
          for (size_t i = 0; i < u16.size(); ++i) a->as<uint16_t>()[i] = u16[i];
          return a;
        };

        auto makeStr = [&](int strId, const char* suffix = "") -> Str* {
          std::string s = getBabbleString(babbleBytes, strId) + suffix;
          return vm.newStr(fromUtf8(s));
        };

        auto setField = [&](const char* name, Object* val) {
          FieldInfo* fi = vm.findField(bhClass, name);
          if (fi && fi->isStatic && fi->index >= 0 && fi->index < (int)bhClass->statics.size()) {
            bhClass->statics[fi->index].o = val;
          }
        };

        // Strings globais
        setField("a:Ljava/lang/String;", makeStr(3902, " "));
        setField("b:Ljava/lang/String;", makeStr(3903));
        setField("c:Ljava/lang/String;", makeStr(3949));
        setField("d:Ljava/lang/String;", makeStr(3948));

        // Arrays de char[] da UI
        setField("a:[C", makeCharArr(3904));
        setField("b:[C", makeCharArr(3906));
        setField("c:[C", makeCharArr(3907));
        setField("d:[C", makeCharArr(3908));
        setField("e:[C", makeCharArr(3909));
        setField("f:[C", makeCharArr(3910));
        setField("g:[C", makeCharArr(3911));
        setField("h:[C", makeCharArr(3912));
        setField("i:[C", makeCharArr(3913));
        setField("j:[C", makeCharArr(3914));
        setField("k:[C", makeCharArr(3915));
        setField("l:[C", makeCharArr(3916));
        setField("s:[C", makeCharArr(3932));
        setField("n:[C", makeCharArr(3946));
        setField("t:[C", makeCharArr(3947));
        setField("q:[C", makeCharArr(3950));

        // Matriz de botões do menu principal: bh.var_char_arr_arr_a (a:[[C)
        FieldInfo* fMat = vm.findField(bhClass, "a:[[C");
        if (fMat && fMat->isStatic && fMat->index >= 0 && fMat->index < (int)bhClass->statics.size()) {
          Object* matObj = bhClass->statics[fMat->index].o;
          if (matObj && matObj->kind == K_ARRAY) {
            Array* mat = static_cast<Array*>(matObj);
            int ids[7] = { 3920, 3921, 3922, 3923, 3924, 3925, 3926 };
            for (int i = 0; i < 7 && i < mat->len; ++i) {
              mat->as<Object*>()[i] = makeCharArr(ids[i]);
            }
          }
        }
      }

      // 3. Atualiza nomes e descrições de todos os itens e equipamentos em memória (ad, e, t, l)
      ClassInfo* adClass = vm.findClass("ad");
      if (adClass) {
        FieldInfo* fF = vm.findField(adClass, "f:B");
        FieldInfo* fG = vm.findField(adClass, "g:B");
        FieldInfo* fA = vm.findField(adClass, "a:[C");
        FieldInfo* fB = vm.findField(adClass, "b:[C");
        if (fF && fG && fA && fB) {
          std::unordered_map<int, std::vector<uint8_t>> itmCache;
          for (Object* obj : vm.allObjs) {
            if (!obj || obj->kind != K_INST || !vm.isSubclass(obj->cls, adClass)) continue;
            Instance* itemInst = static_cast<Instance*>(obj);
            if (fF->index < 0 || fF->index >= (int)itemInst->f.size() ||
                fG->index < 0 || fG->index >= (int)itemInst->f.size() ||
                fA->index < 0 || fA->index >= (int)itemInst->f.size() ||
                fB->index < 0 || fB->index >= (int)itemInst->f.size()) continue;

            int f = itemInst->f[fF->index].i;
            int g = itemInst->f[fG->index].i;
            if (f < 0 || g < 0) continue;

            if (itmCache.find(f) == itmCache.end()) {
              char itmPath[32];
              snprintf(itmPath, sizeof(itmPath), "itm/%02d", f);
              itmCache[f] = Platform::readAsset(itmPath);
            }
            const auto& itmData = itmCache[f];
            if (itmData.empty()) continue;

            // Encontra a entrada g em itmData
            size_t pos = 0;
            for (int i = 0; i < g && pos < itmData.size(); ++i) {
              uint8_t sz = itmData[pos++];
              pos += sz;
            }
            if (pos >= itmData.size()) continue;
            uint8_t entryLen = itmData[pos++];
            if (pos + entryLen > itmData.size() || entryLen < 3) continue;

            size_t epos = pos + 1;
            size_t eEnd = pos + entryLen;
            if (epos >= eEnd) continue;

            uint8_t nameIdLen = itmData[epos++];
            if (epos + nameIdLen > eEnd) continue;
            std::string nameIdStr(reinterpret_cast<const char*>(itmData.data() + epos), nameIdLen);
            epos += nameIdLen;

            if (epos >= eEnd) continue;
            uint8_t descIdLen = itmData[epos++];
            if (epos + descIdLen > eEnd) continue;
            std::string descIdStr(reinterpret_cast<const char*>(itmData.data() + epos), descIdLen);
            epos += descIdLen;

            int nameId = std::atoi(nameIdStr.c_str());
            int descId = std::atoi(descIdStr.c_str());

            std::string nameStr = getBabbleString(babbleBytes, nameId);
            std::string descStr = getBabbleString(babbleBytes, descId);

            std::u16string u16Name = fromUtf8(nameStr);
            Array* arrName = vm.newArray('C', (int)u16Name.size());
            for (size_t k = 0; k < u16Name.size(); ++k) arrName->as<uint16_t>()[k] = u16Name[k];

            std::u16string u16Desc = fromUtf8(descStr);
            Array* arrDesc = vm.newArray('C', (int)u16Desc.size());
            for (size_t k = 0; k < u16Desc.size(); ++k) arrDesc->as<uint16_t>()[k] = u16Desc[k];

            itemInst->f[fA->index].o = arrName;
            itemInst->f[fB->index].o = arrDesc;
          }
        }
      }

      // 4. Atualiza diálogos do mapa ativo atual (n.var_ae_a.var_java_lang_Object_arr_c)
      ClassInfo* nClass = vm.findClass("n");
      if (nClass) {
        FieldInfo* fMap = vm.findField(nClass, "a:Lae;");
        FieldInfo* fWorld = vm.findField(nClass, "a:B");
        FieldInfo* fMapId = vm.findField(nClass, "f:B");
        if (fMap && fMap->isStatic && fMap->index >= 0 && fMap->index < (int)nClass->statics.size()) {
          Object* mapObj = nClass->statics[fMap->index].o;
          if (mapObj && mapObj->kind == K_INST) {
            Instance* aeInst = static_cast<Instance*>(mapObj);
            int worldId = (fWorld && fWorld->isStatic && fWorld->index >= 0 && fWorld->index < (int)nClass->statics.size()) ? nClass->statics[fWorld->index].i : 6;
            if (worldId < 6 || worldId > 8) worldId = 6;

            int mapId = -1;
            FieldInfo* fMapByteA = vm.findField(aeInst->cls, "a:B");
            if (fMapByteA && fMapByteA->index >= 0 && fMapByteA->index < (int)aeInst->f.size()) {
              mapId = aeInst->f[fMapByteA->index].i;
            }
            if (mapId < 0 && fMapId && fMapId->isStatic && fMapId->index >= 0 && fMapId->index < (int)nClass->statics.size()) {
              mapId = nClass->statics[fMapId->index].i;
            }

            if (mapId >= 0) {
              FieldInfo* fW = vm.findField(aeInst->cls, "a:I");
              FieldInfo* fH = vm.findField(aeInst->cls, "b:I");
              if (fW && fH && fW->index >= 0 && fW->index < (int)aeInst->f.size() &&
                  fH->index >= 0 && fH->index < (int)aeInst->f.size()) {
                int w = aeInst->f[fW->index].i;
                int h = aeInst->f[fH->index].i;

                char evtPath[64];
                snprintf(evtPath, sizeof(evtPath), "m/%d/%02d.evt", worldId, mapId);
                std::vector<uint8_t> evtData = Platform::readAsset(evtPath);
                if (evtData.empty()) {
                  snprintf(evtPath, sizeof(evtPath), "reference/extracted/m/%d/%02d.evt", worldId, mapId);
                  evtData = Platform::readAsset(evtPath);
                }

              size_t pos = (size_t)w * h;
              if (pos < evtData.size()) {
                int n4 = evtData[pos++];
                pos += n4;
                if (pos < evtData.size()) {
                  int n5 = evtData[pos++];
                  pos += n5 * 5;
                  if (pos < evtData.size()) {
                    int n6 = evtData[pos++];
                    pos += n6;
                    if (pos < evtData.size()) {
                      int n7 = evtData[pos++];
                      pos += n7 * 3;
                      if (pos < evtData.size()) {
                        int by3 = evtData[pos++];
                        pos += by3;
                        if (pos < evtData.size()) {
                          int n5_c = evtData[pos++];
                          pos += n5_c * 3;
                          if (pos < evtData.size()) {
                            int n3_d = evtData[pos++];
                            pos += n3_d;
                            if (pos < evtData.size()) {
                              int n6_e = evtData[pos++];
                              for (int i = 0; i < n6_e && pos < evtData.size(); ++i) {
                                int n3_sub = evtData[pos++];
                                if (n3_sub > 0) pos += n3_sub * 7;
                              }
                              if (pos < evtData.size()) {
                                int n4_e = evtData[pos++];
                                for (int i = 0; i < n4_e && pos < evtData.size(); ++i) {
                                  int n7_sub = evtData[pos++];
                                  if (n7_sub > 0) pos += n7_sub * 3;
                                }
                                if (pos < evtData.size()) {
                                  int dialogCount = evtData[pos++];
                                  FieldInfo* fDialogArr = vm.findField(aeInst->cls, "c:[Ljava/lang/Object;");
                                  if (fDialogArr && fDialogArr->index >= 0 && fDialogArr->index < (int)aeInst->f.size()) {
                                    Object* curArrObj = aeInst->f[fDialogArr->index].o;
                                    Array* dialogArr = (curArrObj && curArrObj->kind == K_ARRAY) ? static_cast<Array*>(curArrObj) : nullptr;
                                    if (!dialogArr || dialogArr->len != dialogCount) {
                                      dialogArr = vm.newRefArray(dialogCount);
                                      aeInst->f[fDialogArr->index].o = dialogArr;
                                    }
                                    for (int i = 0; i < dialogCount && pos < evtData.size(); ++i) {
                                      int idLen = evtData[pos++];
                                      if (pos + idLen > evtData.size()) break;
                                      std::string idStr(reinterpret_cast<const char*>(evtData.data() + pos), idLen);
                                      pos += idLen;
                                      int strId = std::atoi(idStr.c_str());
                                      std::string s = getBabbleString(babbleBytes, strId);
                                      std::u16string u16 = fromUtf8(s);
                                      Array* cArr = vm.newArray('C', (int)u16.size());
                                      for (size_t k = 0; k < u16.size(); ++k) cArr->as<uint16_t>()[k] = u16[k];
                                      dialogArr->as<Object*>()[i] = cArr;
                                    }
                                    ClassInfo* ahClass = vm.findClass("ah");
                                    if (ahClass) {
                                      FieldInfo* fAhA = vm.findField(ahClass, "a:[C");
                                      FieldInfo* fAhStep = vm.findField(ahClass, "a:I");
                                      FieldInfo* fAhMat = vm.findField(ahClass, "b:[[B");
                                      if (fAhA && fAhA->isStatic && fAhA->index >= 0 && fAhA->index < (int)ahClass->statics.size() &&
                                          fAhStep && fAhStep->isStatic && fAhStep->index >= 0 && fAhStep->index < (int)ahClass->statics.size() &&
                                          fAhMat && fAhMat->isStatic && fAhMat->index >= 0 && fAhMat->index < (int)ahClass->statics.size()) {
                                        Object* curTxt = ahClass->statics[fAhA->index].o;
                                        if (curTxt) {
                                          int step = ahClass->statics[fAhStep->index].i;
                                          Object* matObj = ahClass->statics[fAhMat->index].o;
                                          if (matObj && matObj->kind == K_ARRAY) {
                                            Array* mat = static_cast<Array*>(matObj);
                                            if (step >= 0 && step < mat->len) {
                                              Object* rowObj = mat->as<Object*>()[step];
                                              if (rowObj && rowObj->kind == K_ARRAY) {
                                                Array* row = static_cast<Array*>(rowObj);
                                                if (row->len >= 2) {
                                                  int dlgIdx = row->as<int8_t>()[1];
                                                  if (dlgIdx >= 0 && dlgIdx < dialogArr->len) {
                                                    ahClass->statics[fAhA->index].o = dialogArr->as<Object*>()[dlgIdx];
                                                  }
                                                }
                                              }
                                            }
                                          }
                                        }
                                      }
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }

            // Atualiza o nome do mapa ativo em ae.var_char_arr_a
            FieldInfo* fMapName = vm.findField(aeInst->cls, "a:[C");
            if (fMapName && fMapByteA && fMapName->index >= 0 && fMapName->index < (int)aeInst->f.size() &&
                fMapByteA->index >= 0 && fMapByteA->index < (int)aeInst->f.size()) {
              int mapByteA = aeInst->f[fMapByteA->index].i;
              int strId = (worldId == 8 && mapByteA == 65) ? 890 : (805 + mapByteA);
              std::string s = getBabbleString(babbleBytes, strId);
              std::u16string u16 = fromUtf8(s);
              Array* arr = vm.newArray('C', (int)u16.size());
              for (size_t k = 0; k < u16.size(); ++k) arr->as<uint16_t>()[k] = u16[k];
              aeInst->f[fMapName->index].o = arr;
            }
          }
        }
      }

      // 5. Invalida flags de dirty repainting de menus abertos (cb) para forçar repintura imediata
      ClassInfo* cbClass = vm.findClass("cb");
      if (cbClass) {
        FieldInfo* fBoolA = vm.findField(cbClass, "a:Z");
        FieldInfo* fBoolB = vm.findField(cbClass, "b:Z");
        ClassInfo* qClass = vm.findClass("q");
        FieldInfo* fQA = qClass ? vm.findField(qClass, "a:[C") : nullptr;
        FieldInfo* fQB = qClass ? vm.findField(qClass, "b:[C") : nullptr;
        ClassInfo* btClass = vm.findClass("bt");
        ClassInfo* sClass = vm.findClass("s");

        for (Object* obj : vm.allObjs) {
          if (!obj || obj->kind != K_INST || !vm.isSubclass(obj->cls, cbClass)) continue;
          Instance* cbInst = static_cast<Instance*>(obj);
          if (fBoolA && fBoolA->index >= 0 && fBoolA->index < (int)cbInst->f.size()) cbInst->f[fBoolA->index].i = 1;
          if (fBoolB && fBoolB->index >= 0 && fBoolB->index < (int)cbInst->f.size()) cbInst->f[fBoolB->index].i = 1;

          // Se for janela 'bt' (menu de opções/ajuda), atualiza array de textos das opções (a:[[C)
          if (btClass && obj->cls == btClass) {
            FieldInfo* fBtA = vm.findField(btClass, "a:[[C");
            FieldInfo* fBtByteA = vm.findField(btClass, "a:B");
            if (fBtA && fBtByteA && fBtA->index >= 0 && fBtA->index < (int)cbInst->f.size() &&
                fBtByteA->index >= 0 && fBtByteA->index < (int)cbInst->f.size()) {
              Object* aObj = cbInst->f[fBtA->index].o;
              if (aObj && aObj->kind == K_ARRAY) {
                Array* arr = static_cast<Array*>(aObj);
                int count = cbInst->f[fBtByteA->index].i;
                for (int i = 0; i < count && i < arr->len; ++i) {
                  int strId = (i == 4) ? 3925 : (1228 + i);
                  std::string s = getBabbleString(babbleBytes, strId);
                  std::u16string u16 = fromUtf8(s);
                  Array* cArr = vm.newArray('C', (int)u16.size());
                  for (size_t k = 0; k < u16.size(); ++k) cArr->as<uint16_t>()[k] = u16[k];
                  arr->as<Object*>()[i] = cArr;
                }
              }
            }
          }

          // Se for janela 's' (tela de quests/missões), atualiza título (a:[C) e descrição (b:[C)
          if (sClass && obj->cls == sClass) {
            FieldInfo* fSA = vm.findField(sClass, "a:[C");
            FieldInfo* fSB = vm.findField(sClass, "b:[C");
            FieldInfo* fSC = vm.findField(sClass, "c:B");
            FieldInfo* fSD = vm.findField(sClass, "d:B");
            ClassInfo* ceClass = vm.findClass("ce");
            if (fSA && fSB && fSC && fSD && ceClass) {
              FieldInfo* fZf = vm.findField(ceClass, "f:Lz;");
              if (fZf && fZf->isStatic && fZf->index >= 0 && fZf->index < (int)ceClass->statics.size()) {
                Object* zObj = ceClass->statics[fZf->index].o;
                if (zObj && zObj->kind == K_INST) {
                  Instance* zInst = static_cast<Instance*>(zObj);
                  FieldInfo* fZArr = vm.findField(zInst->cls, "a:[I");
                  if (fZArr && fZArr->index >= 0 && fZArr->index < (int)zInst->f.size()) {
                    Array* zArr = static_cast<Array*>(zInst->f[fZArr->index].o);
                    if (zArr) {
                      int cVal = cbInst->f[fSC->index].i;
                      int dVal = cbInst->f[fSD->index].i;
                      int n2 = (dVal == 2) ? (cVal * 7 + 2) : (cVal * 7);
                      if (n2 >= 0 && n2 + 1 < zArr->len) {
                        int idA = zArr->as<int32_t>()[n2];
                        int idB = zArr->as<int32_t>()[n2 + 1];
                        std::string sA = getBabbleString(babbleBytes, idA);
                        std::string sB = getBabbleString(babbleBytes, idB);
                        std::u16string u16A = fromUtf8(sA);
                        std::u16string u16B = fromUtf8(sB);
                        Array* arrA = vm.newArray('C', (int)u16A.size());
                        for (size_t k = 0; k < u16A.size(); ++k) arrA->as<uint16_t>()[k] = u16A[k];
                        Array* arrB = vm.newArray('C', (int)u16B.size());
                        for (size_t k = 0; k < u16B.size(); ++k) arrB->as<uint16_t>()[k] = u16B[k];
                        if (fSA->index >= 0 && fSA->index < (int)cbInst->f.size()) cbInst->f[fSA->index].o = arrA;
                        if (fSB->index >= 0 && fSB->index < (int)cbInst->f.size()) cbInst->f[fSB->index].o = arrB;
                      }
                    }
                  }
                }
              }
            }
          }

          // Se for janela 'q' (aba de status do herói), atualiza rótulos instanciados de herói e classe
          if (qClass && obj->cls == qClass && fQA && fQB) {
            ClassInfo* ceClass = vm.findClass("ce");
            ClassInfo* nCls = vm.findClass("n");
            if (ceClass && nCls) {
              FieldInfo* fZeA = vm.findField(ceClass, "a:Lz;");
              FieldInfo* fWorld = vm.findField(nCls, "a:B");
              FieldInfo* fNpcG = vm.findField(nCls, "g:B");
              if (fZeA && fZeA->isStatic && fZeA->index >= 0 && fZeA->index < (int)ceClass->statics.size() &&
                  fWorld && fWorld->isStatic && fWorld->index >= 0 && fWorld->index < (int)nCls->statics.size()) {
                Object* zObj = ceClass->statics[fZeA->index].o;
                if (zObj && zObj->kind == K_INST) {
                  Instance* zInst = static_cast<Instance*>(zObj);
                  FieldInfo* fZArr = vm.findField(zInst->cls, "a:[I");
                  if (fZArr && fZArr->index >= 0 && fZArr->index < (int)zInst->f.size()) {
                    Array* zArr = static_cast<Array*>(zInst->f[fZArr->index].o);
                    if (zArr && zArr->len > 0) {
                      int worldVal = nCls->statics[fWorld->index].i;
                      int heroNameIdx = worldVal - 6;
                      if (heroNameIdx >= 0 && heroNameIdx < zArr->len) {
                        int strId = zArr->as<int32_t>()[heroNameIdx];
                        std::string s = getBabbleString(babbleBytes, strId);
                        std::u16string u16 = fromUtf8(s);
                        Array* a = vm.newArray('C', (int)u16.size());
                        for (size_t k = 0; k < u16.size(); ++k) a->as<uint16_t>()[k] = u16[k];
                        if (fQA->index >= 0 && fQA->index < (int)cbInst->f.size()) cbInst->f[fQA->index].o = a;
                      }
                      int titleIdx = 3 + worldVal - 6;
                      int gVal = (fNpcG && fNpcG->isStatic && fNpcG->index >= 0 && fNpcG->index < (int)nCls->statics.size()) ? nCls->statics[fNpcG->index].i : 0;
                      if (gVal == 1) titleIdx += 15;
                      else if (gVal >= 2) titleIdx += 18;
                      if (titleIdx >= 0 && titleIdx < zArr->len) {
                        int strId = zArr->as<int32_t>()[titleIdx];
                        std::string s = getBabbleString(babbleBytes, strId);
                        std::u16string u16 = fromUtf8(s);
                        Array* a = vm.newArray('C', (int)u16.size());
                        for (size_t k = 0; k < u16.size(); ++k) a->as<uint16_t>()[k] = u16[k];
                        if (fQB->index >= 0 && fQB->index < (int)cbInst->f.size()) cbInst->f[fQB->index].o = a;
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  } catch (...) {}

  if (needUnlock) {
    vm.gilUnlock();
  }
}

void Platform::openCloudSave(VM* vm) {
  CloudSave::openModal(vm);
}

} // namespace hl
