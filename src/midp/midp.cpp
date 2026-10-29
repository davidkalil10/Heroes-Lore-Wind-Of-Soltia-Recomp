// midp.cpp — implementação das APIs J2ME MIDP 2.0 (LCDUI, RecordStore, Audio)
#include "midp.h"
#include "../platform/platform.h"
#include "../platform/midi_synth.h"
#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb_image.h"

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
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>

namespace hl {

uint32_t g_screenBuffer[MAX_SCREEN_WIDTH * SCREEN_HEIGHT];
int g_screenWidth = SCREEN_W_ORIGINAL;
int g_screenHeight = SCREEN_HEIGHT;
GraphicsObj* g_screenGraphics = nullptr;
DisplayObj* g_display = nullptr;

void ImageObj::trace(std::vector<Object*>& o) {
  if (graphics) o.push_back(graphics);
}

void GraphicsObj::trace(std::vector<Object*>& o) {
  if (target) o.push_back(target);
}

static PlayerObj* s_activeMusicPlayer = nullptr;
static PlayerObj* s_bgMusicPlayer = nullptr;

PlayerObj::~PlayerObj() {
  if (s_activeMusicPlayer == this) s_activeMusicPlayer = nullptr;
  if (s_bgMusicPlayer == this) s_bgMusicPlayer = nullptr;
  if (handle) {
    if (isMusic) Mix_FreeMusic((Mix_Music*)handle);
    else Mix_FreeChunk((Mix_Chunk*)handle);
    handle = nullptr;
  }
}

void GraphicsObj::resetClip() {
  clipX = 0;
  clipY = 0;
  clipW = target ? target->width : g_screenWidth;
  clipH = target ? target->height : g_screenHeight;
}

void GraphicsObj::setColor(uint32_t rgb) {
  color = 0xFF000000 | (rgb & 0x00FFFFFF);
}

void GraphicsObj::setClip(int x, int y, int w, int h) {
  clipX = x + transX;
  clipY = y + transY;
  clipW = w;
  clipH = h;
}

void GraphicsObj::clipRect(int x, int y, int w, int h) {
  int nx0 = std::max(clipX, x + transX);
  int ny0 = std::max(clipY, y + transY);
  int nx1 = std::min(clipX + clipW, x + transX + w);
  int ny1 = std::min(clipY + clipH, y + transY + h);
  clipX = nx0;
  clipY = ny0;
  clipW = std::max(0, nx1 - nx0);
  clipH = std::max(0, ny1 - ny0);
}

void GraphicsObj::fillRect(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  x += transX; y += transY;
  int x0 = std::max(x, clipX);
  int y0 = std::max(y, clipY);
  int x1 = std::min(x + w, clipX + clipW);
  int y1 = std::min(y + h, clipY + clipH);
  int tW = target ? target->width : g_screenWidth;
  int tH = target ? target->height : g_screenHeight;
  uint32_t* dst = target ? target->pixels.data() : g_screenBuffer;
  x0 = std::max(0, std::min(x0, tW));
  x1 = std::max(0, std::min(x1, tW));
  y0 = std::max(0, std::min(y0, tH));
  y1 = std::max(0, std::min(y1, tH));
  for (int r = y0; r < y1; r++) {
    uint32_t* line = dst + r * tW;
    for (int c = x0; c < x1; c++) {
      line[c] = color;
    }
  }
}

void GraphicsObj::drawLine(int x0, int y0, int x1, int y1) {
  x0 += transX; y0 += transY;
  x1 += transX; y1 += transY;
  int tW = target ? target->width : g_screenWidth;
  int tH = target ? target->height : g_screenHeight;
  uint32_t* dst = target ? target->pixels.data() : g_screenBuffer;

  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    if (x0 >= clipX && x0 < clipX + clipW && y0 >= clipY && y0 < clipY + clipH &&
        x0 >= 0 && x0 < tW && y0 >= 0 && y0 < tH) {
      dst[y0 * tW + x0] = color;
    }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void GraphicsObj::drawRect(int x, int y, int w, int h) {
  if (w < 0 || h < 0) return;
  if (w == 0 || h == 0) {
    fillRect(x, y, w + 1, h + 1);
    return;
  }
  drawLine(x, y, x + w, y);
  drawLine(x + w, y, x + w, y + h);
  drawLine(x + w, y + h, x, y + h);
  drawLine(x, y + h, x, y);
}

void GraphicsObj::fillArc(int x, int y, int w, int h, int, int) {
  fillRect(x, y, w, h);
}

void GraphicsObj::drawImage(ImageObj* img, int x, int y, int anchor) {
  if (!img || img->width <= 0 || img->height <= 0) return;
  int w = img->width;
  int h = img->height;
  int destX = x;
  int destY = y;

  if ((anchor & 1) != 0) destX -= w / 2; // HCENTER
  else if ((anchor & 8) != 0) destX -= w; // RIGHT

  if ((anchor & 2) != 0) destY -= h / 2; // VCENTER
  else if ((anchor & 32) != 0) destY -= h; // BOTTOM

  destX += transX;
  destY += transY;

  int tW = target ? target->width : g_screenWidth;
  int tH = target ? target->height : g_screenHeight;
  uint32_t* dst = target ? target->pixels.data() : g_screenBuffer;
  const uint32_t* src = img->pixels.data();

  int minX = std::max({destX, clipX, 0});
  int maxX = std::min({destX + w, clipX + clipW, tW});
  int minY = std::max({destY, clipY, 0});
  int maxY = std::min({destY + h, clipY + clipH, tH});

  if (minX >= maxX || minY >= maxY) return;

  for (int dy = minY; dy < maxY; dy++) {
    int sy = dy - destY;
    uint32_t* dstLine = dst + dy * tW;
    const uint32_t* srcLine = src + sy * w;
    for (int dx = minX; dx < maxX; dx++) {
      int sx = dx - destX;
      uint32_t p = srcLine[sx];
      uint32_t a = (p >> 24) & 0xFF;
      if (a == 0) continue;
      if (a == 255) {
        dstLine[dx] = p;
      } else {
        uint32_t dp = dstLine[dx];
        uint32_t sr = (p >> 16) & 0xFF, sg = (p >> 8) & 0xFF, sb = p & 0xFF;
        uint32_t dr = (dp >> 16) & 0xFF, dg = (dp >> 8) & 0xFF, db = dp & 0xFF;
        uint32_t invA = 255 - a;
        uint32_t r = (sr * a + dr * invA) / 255;
        uint32_t g = (sg * a + dg * invA) / 255;
        uint32_t b = (sb * a + db * invA) / 255;
        dstLine[dx] = 0xFF000000 | (r << 16) | (g << 8) | b;
      }
    }
  }
}

// -------------------------------------------------------------
// javax/microedition/lcdui/Display
// -------------------------------------------------------------
static void Display_getDisplay(VM& vm, Value*, Value* ret) {
  if (!g_display) {
    ClassInfo* cd = vm.mustClass("javax/microedition/lcdui/Display");
    g_display = vm.alloc<DisplayObj>(cd, K_DISPLAY);
    vm.roots.push_back(g_display);
  }
  ret[0].o = g_display;
}

static void Display_setCurrent(VM&, Value* args, Value*) {
  if (g_display) {
    g_display->current = args[1].o;
  }
}

Object* g_serialRunnable = nullptr;

static void Display_callSerially(VM&, Value* args, Value*) {
  g_serialRunnable = args[1].o;
}

// -------------------------------------------------------------
// javax/microedition/lcdui/Displayable
// -------------------------------------------------------------
static void Displayable_getWidth(VM&, Value*, Value* ret) { ret[0].i = g_screenWidth; }
static void Displayable_getHeight(VM&, Value*, Value* ret) { ret[0].i = g_screenHeight; }

// -------------------------------------------------------------
// javax/microedition/lcdui/Canvas
// -------------------------------------------------------------
static void Canvas_init(VM&, Value*, Value*) {}

static void Canvas_setFullScreenMode(VM&, Value*, Value*) {}

static void Canvas_repaint(VM&, Value*, Value*) {}

static void Canvas_getGameAction(VM&, Value* args, Value* ret) {
  int keyCode = args[1].i;
  int action = 0;
  switch (keyCode) {
    case -1: case '2': action = 1; break; // UP
    case -2: case '8': action = 6; break; // DOWN
    case -3: case '4': action = 2; break; // LEFT
    case -4: case '6': action = 5; break; // RIGHT
    case -5: case '5': action = 8; break; // FIRE
    default: action = 0; break;
  }
  ret[0].i = action;
}

static void Canvas_keyReleased(VM&, Value*, Value*) {}

// -------------------------------------------------------------
// javax/microedition/lcdui/Graphics
// -------------------------------------------------------------
static void Graphics_setColor_rgb(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->setColor(args[1].i);
}

static void Graphics_setColor_components(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  uint32_t r = (uint32_t)args[1].i & 0xFF;
  uint32_t gr = (uint32_t)args[2].i & 0xFF;
  uint32_t b = (uint32_t)args[3].i & 0xFF;
  g->color = 0xFF000000 | (r << 16) | (gr << 8) | b;
}

static void Graphics_getColor(VM& vm, Value* args, Value* ret) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  ret[0].i = (int)(g->color & 0x00FFFFFF);
}

static void Graphics_setClip(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->setClip(args[1].i, args[2].i, args[3].i, args[4].i);
}

static void Graphics_clipRect(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->clipRect(args[1].i, args[2].i, args[3].i, args[4].i);
}

static void Graphics_getClipX(VM& vm, Value* args, Value* ret) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  ret[0].i = g->clipX - g->transX;
}

static void Graphics_getClipY(VM& vm, Value* args, Value* ret) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  ret[0].i = g->clipY - g->transY;
}

static void Graphics_getClipWidth(VM& vm, Value* args, Value* ret) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  ret[0].i = g->clipW;
}

static void Graphics_getClipHeight(VM& vm, Value* args, Value* ret) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  ret[0].i = g->clipH;
}

static void Graphics_translate(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->transX += args[1].i;
  g->transY += args[2].i;
}

static void Graphics_fillRect(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->fillRect(args[1].i, args[2].i, args[3].i, args[4].i);
}

static void Graphics_drawRect(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->drawRect(args[1].i, args[2].i, args[3].i, args[4].i);
}

static void Graphics_drawLine(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->drawLine(args[1].i, args[2].i, args[3].i, args[4].i);
}

static void Graphics_fillArc(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  if (!g) vm.npe();
  g->fillArc(args[1].i, args[2].i, args[3].i, args[4].i, args[5].i, args[6].i);
}

static void Graphics_drawImage(VM& vm, Value* args, Value*) {
  GraphicsObj* g = static_cast<GraphicsObj*>(args[0].o);
  ImageObj* img = static_cast<ImageObj*>(args[1].o);
  if (!g) vm.npe();
  if (!img) return;
  g->drawImage(img, args[2].i, args[3].i, args[4].i);
}

// -------------------------------------------------------------
// javax/microedition/lcdui/Image
// -------------------------------------------------------------
static ImageObj* decodeImageBytes(VM& vm, const uint8_t* data, int len) {
  int w = 0, h = 0, comp = 0;
  uint8_t* rgba = stbi_load_from_memory(data, len, &w, &h, &comp, 4);
  if (!rgba) return nullptr;

  ClassInfo* cimg = vm.mustClass("javax/microedition/lcdui/Image");
  ImageObj* img = vm.alloc<ImageObj>(cimg, K_IMAGE);
  img->width = w;
  img->height = h;
  img->pixels.resize(w * h);

  for (int i = 0; i < w * h; i++) {
    uint8_t r = rgba[i * 4];
    uint8_t g = rgba[i * 4 + 1];
    uint8_t b = rgba[i * 4 + 2];
    uint8_t a = rgba[i * 4 + 3];
    img->pixels[i] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
  }

  stbi_image_free(rgba);
  return img;
}

static void Image_createImage_str(VM& vm, Value* args, Value* ret) {
  Object* strObj = args[0].o;
  if (!strObj) vm.npe();
  std::string name = toUtf8(static_cast<Str*>(strObj)->s);
  std::string rel = name;
  if (rel[0] == '/' || rel[0] == '\\') rel = rel.substr(1);

  std::string fullPath = vm.dataDir.empty() ? rel : (vm.dataDir + "/" + rel);
  std::vector<uint8_t> buf = Platform::readAsset(fullPath);
  if (buf.empty()) {
    buf = Platform::readAsset(rel);
  }
  if (buf.empty()) {
    vm.throwNew("java/io/IOException", "Image not found: " + name);
    return;
  }

  ImageObj* img = decodeImageBytes(vm, buf.data(), (int)buf.size());
  if (!img) {
    vm.throwNew("java/io/IOException", "Failed to decode image: " + name);
    return;
  }
  ret[0].o = img;
}

static void Image_createImage_bytes(VM& vm, Value* args, Value* ret) {
  Array* arr = static_cast<Array*>(args[0].o);
  int off = args[1].i;
  int len = args[2].i;
  if (!arr) vm.npe();
  if (off < 0 || len < 0 || off + len > arr->len) vm.throwNew("java/lang/ArrayIndexOutOfBoundsException");

  ImageObj* img = decodeImageBytes(vm, arr->data.data() + off, len);
  if (!img) {
    vm.throwNew("java/lang/IllegalArgumentException", "Bad image bytes");
  }
  ret[0].o = img;
}

static void Image_createImage_dim(VM& vm, Value* args, Value* ret) {
  int w = args[0].i;
  int h = args[1].i;
  if (w <= 0 || h <= 0) vm.throwNew("java/lang/IllegalArgumentException");

  ClassInfo* cimg = vm.mustClass("javax/microedition/lcdui/Image");
  ImageObj* img = vm.alloc<ImageObj>(cimg, K_IMAGE);
  img->width = w;
  img->height = h;
  img->pixels.assign(w * h, 0xFFFFFFFF); // branco opaco por padrão
  img->mutable_img = true;
  ret[0].o = img;
}

static void Image_getGraphics(VM& vm, Value* args, Value* ret) {
  ImageObj* img = static_cast<ImageObj*>(args[0].o);
  if (!img) vm.npe();
  if (!img->mutable_img) vm.throwNew("java/lang/IllegalStateException", "Image is immutable");
  if (!img->graphics) {
    ClassInfo* cg = vm.mustClass("javax/microedition/lcdui/Graphics");
    img->graphics = vm.alloc<GraphicsObj>(cg, K_GRAPHICS);
    img->graphics->target = img;
    img->graphics->resetClip();
  }
  ret[0].o = img->graphics;
}

static void Image_getWidth(VM& vm, Value* args, Value* ret) {
  ImageObj* img = static_cast<ImageObj*>(args[0].o);
  if (!img) vm.npe();
  ret[0].i = img->width;
}

static void Image_getHeight(VM& vm, Value* args, Value* ret) {
  ImageObj* img = static_cast<ImageObj*>(args[0].o);
  if (!img) vm.npe();
  ret[0].i = img->height;
}

// -------------------------------------------------------------
// javax/microedition/midlet/MIDlet
// -------------------------------------------------------------
static void MIDlet_init(VM&, Value*, Value*) {}

static void MIDlet_getAppProperty(VM& vm, Value* args, Value* ret) {
  Object* strObj = args[1].o;
  if (!strObj) vm.npe();
  std::string k = toUtf8(static_cast<Str*>(strObj)->s);
  auto it = vm.props.find(k);
  if (it != vm.props.end()) {
    ret[0].o = vm.newStrUtf8(it->second);
  } else {
    ret[0].o = nullptr;
  }
}

static void MIDlet_notifyDestroyed(VM&, Value*, Value*) {
  exit(0);
}

static void MIDlet_platformRequest(VM&, Value*, Value* ret) {
  ret[0].i = 0;
}

// -------------------------------------------------------------
// javax/microedition/rms/RecordStore
// -------------------------------------------------------------
static std::string rmsDir(VM& vm) {
  return Platform::getRmsDir(&vm);
}

static void RecordStore_openRecordStore(VM& vm, Value* args, Value* ret) {
  std::string name = toUtf8(static_cast<Str*>(args[0].o)->s);
  bool create = args[1].i != 0;

  std::string path = rmsDir(vm) + "/" + name + ".rms";
  std::ifstream f(path, std::ios::binary);
  if (!f && !create) {
    vm.throwNew("javax/microedition/rms/RecordStoreNotFoundException", name);
  }

  ClassInfo* crs = vm.mustClass("javax/microedition/rms/RecordStore");
  RecordStoreObj* rs = vm.alloc<RecordStoreObj>(crs, K_RS);
  rs->name = name;
  rs->records.push_back({}); // record 0 vazio (RMS usa 1-based)

  if (f) {
    uint32_t count = 0;
    if (f.read((char*)&count, 4) && f.gcount() == 4) {
      for (uint32_t i = 0; i < count; i++) {
        uint32_t sz = 0;
        if (!f.read((char*)&sz, 4) || f.gcount() != 4) break;
        std::vector<uint8_t> rec(sz);
        if (sz > 0) f.read((char*)rec.data(), sz);
        rs->records.push_back(std::move(rec));
      }
    }
  }

  ret[0].o = rs;
}

static void RecordStore_addRecord(VM& vm, Value* args, Value* ret) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  Array* arr = static_cast<Array*>(args[1].o);
  int off = args[2].i;
  int len = args[3].i;
  if (!rs) vm.npe();
  if (off < 0 || len < 0) vm.throwNew("java/lang/ArrayIndexOutOfBoundsException");
  if (len > 0 && !arr) vm.npe();
  if (arr && (off + len > arr->len)) vm.throwNew("java/lang/ArrayIndexOutOfBoundsException");

  std::vector<uint8_t> rec;
  if (arr && len > 0) {
    rec.assign(arr->data.begin() + off, arr->data.begin() + off + len);
  }
  rs->records.push_back(std::move(rec));
  rs->dirty = true;
  ret[0].i = (int)rs->records.size() - 1;
}

static void RecordStore_setRecord(VM& vm, Value* args, Value*) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  int id = args[1].i;
  Array* arr = static_cast<Array*>(args[2].o);
  int off = args[3].i;
  int len = args[4].i;
  if (!rs) vm.npe();
  if (id <= 0 || (size_t)id >= rs->records.size()) vm.throwNew("javax/microedition/rms/InvalidRecordIDException");
  if (off < 0 || len < 0) vm.throwNew("java/lang/ArrayIndexOutOfBoundsException");
  if (len > 0 && !arr) vm.npe();
  if (arr && (off + len > arr->len)) vm.throwNew("java/lang/ArrayIndexOutOfBoundsException");

  std::vector<uint8_t> rec;
  if (arr && len > 0) {
    rec.assign(arr->data.begin() + off, arr->data.begin() + off + len);
  }
  rs->records[id] = std::move(rec);
  rs->dirty = true;
}

static void RecordStore_getRecord(VM& vm, Value* args, Value* ret) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  int id = args[1].i;
  if (!rs) vm.npe();
  if (id <= 0 || (size_t)id >= rs->records.size()) vm.throwNew("javax/microedition/rms/InvalidRecordIDException");

  const auto& rec = rs->records[id];
  Array* arr = vm.newArray('B', (int)rec.size());
  if (!rec.empty()) std::memcpy(arr->data.data(), rec.data(), rec.size());
  ret[0].o = arr;
}

static void RecordStore_getRecordSize(VM& vm, Value* args, Value* ret) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  int id = args[1].i;
  if (!rs) vm.npe();
  if (id <= 0 || (size_t)id >= rs->records.size()) vm.throwNew("javax/microedition/rms/InvalidRecordIDException");
  ret[0].i = (int)rs->records[id].size();
}

static void RecordStore_getNextRecordID(VM& vm, Value* args, Value* ret) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  if (!rs) vm.npe();
  ret[0].i = (int)rs->records.size();
}

static void RecordStore_getNumRecords(VM& vm, Value* args, Value* ret) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  if (!rs) vm.npe();
  ret[0].i = (rs->records.size() > 0) ? (int)(rs->records.size() - 1) : 0;
}

static void RecordStore_closeRecordStore(VM& vm, Value* args, Value*) {
  RecordStoreObj* rs = static_cast<RecordStoreObj*>(args[0].o);
  if (!rs || !rs->open) return;
  rs->open = false;

  if (!rs->dirty) {
    return; // Não houve alterações; preserva a data/hora original de modificação do arquivo em disco!
  }
  rs->dirty = false;

  std::string path = rmsDir(vm) + "/" + rs->name + ".rms";
  std::ofstream f(path, std::ios::binary);
  if (f) {
    uint32_t count = (rs->records.size() > 0) ? (uint32_t)(rs->records.size() - 1) : 0;
    f.write((const char*)&count, 4);
    for (size_t i = 1; i < rs->records.size(); i++) {
      uint32_t sz = (uint32_t)rs->records[i].size();
      f.write((const char*)&sz, 4);
      if (sz > 0) f.write((const char*)rs->records[i].data(), sz);
    }
  }
}

static void RecordStore_deleteRecordStore(VM& vm, Value* args, Value*) {
  std::string name = toUtf8(static_cast<Str*>(args[0].o)->s);
  std::string path = rmsDir(vm) + "/" + name + ".rms";
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

// -------------------------------------------------------------
// javax/microedition/media (Audio)
// -------------------------------------------------------------
static void Manager_createPlayer_stream(VM& vm, Value* args, Value* ret) {
  Object* streamObj = args[0].o;
  std::string type = args[1].o ? toUtf8(static_cast<Str*>(args[1].o)->s) : "";
  if (!streamObj) vm.npe();

  Bais* b = nullptr;
  if (streamObj->kind == K_BAIS) b = static_cast<Bais*>(streamObj);
  else if (streamObj->kind == K_DIS) b = static_cast<Bais*>(static_cast<Dis*>(streamObj)->in);

  ClassInfo* cpi = vm.mustClass("javax/microedition/media/PlayerImpl");
  PlayerObj* p = vm.alloc<PlayerObj>(cpi, K_PLAYER);
  if (b) {
    p->data = b->data;
  }
  p->type = type;
  p->state = 100;
  ret[0].o = p;
}

static void Manager_createPlayer_url(VM& vm, Value* args, Value* ret) {
  std::string url = toUtf8(static_cast<Str*>(args[0].o)->s);
  ClassInfo* cpi = vm.mustClass("javax/microedition/media/PlayerImpl");
  PlayerObj* p = vm.alloc<PlayerObj>(cpi, K_PLAYER);
  p->type = url;
  p->state = 100;
  ret[0].o = p;
}

static void Player_addPlayerListener(VM&, Value*, Value*) {}

static void Player_close(VM&, Value* args, Value*) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  if (p) {
    if (p->isMusic) {
      if (s_bgMusicPlayer == p) s_bgMusicPlayer = nullptr;
      if (s_activeMusicPlayer == p) {
        MidiSynth::stop();
        if (p->handle) Mix_HaltMusic();
        s_activeMusicPlayer = nullptr;
      }
      if (p->handle) {
        Mix_FreeMusic((Mix_Music*)p->handle);
        p->handle = nullptr;
      }
    } else if (p->handle) {
      Mix_HaltChannel(-1);
      Mix_FreeChunk((Mix_Chunk*)p->handle);
      p->handle = nullptr;
    }
    p->state = 0;
  }
}

static void Player_getState(VM&, Value* args, Value* ret) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  ret[0].i = p ? p->state : 0;
}

static void Player_prefetch(VM&, Value* args, Value*) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  if (p && p->state < 300) p->state = 300;
}

static void Player_realize(VM&, Value* args, Value*) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  if (!p) return;
  if (p->state < 200) {
    p->state = 200;
    if (!p->data.empty() && !p->handle) {
      bool isMidi = (p->type.find("midi") != std::string::npos ||
                     p->type.find(".mid") != std::string::npos);
      if (!isMidi && p->data.size() >= 4) {
        if (p->data[0] == 'M' && p->data[1] == 'T' && p->data[2] == 'h' && p->data[3] == 'd') {
          isMidi = true;
        }
      }
      if (isMidi) {
        p->isMusic = true;
        SDL_RWops* rw = SDL_RWFromConstMem(p->data.data(), (int)p->data.size());
        p->handle = Mix_LoadMUS_RW(rw, 1);
        boot_log("[Audio] Carregado MIDI %s (%zu bytes, Mix_LoadMUS=%p)\n",
                 p->type.c_str(), p->data.size(), p->handle);
      } else {
        SDL_RWops* rw = SDL_RWFromConstMem(p->data.data(), (int)p->data.size());
        p->handle = Mix_LoadWAV_RW(rw, 1);
        p->isMusic = false;
        if (!p->handle) boot_log("[Audio] Falha ao carregar WAV %s (%zu bytes): %s\n",
                                 p->type.c_str(), p->data.size(), Mix_GetError());
      }
    }
  }
}

static void Player_setLoopCount(VM&, Value* args, Value*) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  if (p) p->loopCount = args[1].i;
}

static void Player_start(VM&, Value* args, Value*) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  if (!p) return;
  p->state = 400;
  if (p->isMusic) {
    if (s_activeMusicPlayer && s_activeMusicPlayer != p) {
      if (s_activeMusicPlayer->state == 400 && s_activeMusicPlayer->loopCount == -1 && !s_bgMusicPlayer) {
        s_bgMusicPlayer = s_activeMusicPlayer;
      } else {
        s_activeMusicPlayer->state = 300;
      }
    }
    s_activeMusicPlayer = p;
    if (!MidiSynth::play(p->data.data(), p->data.size(), (p->loopCount == -1) ? -1 : p->loopCount)) {
      if (p->handle) {
        Mix_PlayMusic((Mix_Music*)p->handle, (p->loopCount == -1) ? -1 : p->loopCount);
      }
    }
  } else if (p->handle) {
    Mix_PlayChannel(-1, (Mix_Chunk*)p->handle, (p->loopCount == -1) ? -1 : 0);
  }
}

static void Player_stop(VM&, Value* args, Value*) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  if (p) {
    p->state = 300;
    if (p->isMusic) {
      if (s_bgMusicPlayer == p) {
        s_bgMusicPlayer = nullptr;
        if (s_activeMusicPlayer) {
          s_activeMusicPlayer->state = 300;
          s_activeMusicPlayer = nullptr;
        }
        MidiSynth::stop();
        if (p->handle) Mix_HaltMusic();
      } else if (s_activeMusicPlayer == p) {
        MidiSynth::stop();
        if (p->handle) Mix_HaltMusic();
        s_activeMusicPlayer = nullptr;
        if (s_bgMusicPlayer && s_bgMusicPlayer->state == 400) {
          PlayerObj* bg = s_bgMusicPlayer;
          s_bgMusicPlayer = nullptr;
          s_activeMusicPlayer = bg;
          if (!MidiSynth::play(bg->data.data(), bg->data.size(), (bg->loopCount == -1) ? -1 : bg->loopCount)) {
            if (bg->handle) {
              Mix_PlayMusic((Mix_Music*)bg->handle, (bg->loopCount == -1) ? -1 : bg->loopCount);
            }
          }
        }
      }
    } else if (p->handle) {
      Mix_HaltChannel(-1);
    }
  }
}

static void Player_getControl(VM& vm, Value* args, Value* ret) {
  PlayerObj* p = static_cast<PlayerObj*>(args[0].o);
  std::string name = toUtf8(static_cast<Str*>(args[1].o)->s);
  if (name == "VolumeControl") {
    ClassInfo* cvc = vm.mustClass("javax/microedition/media/VolumeControlImpl");
    VolumeControlObj* vc = vm.alloc<VolumeControlObj>(cvc, K_VOLCTL);
    vc->player = p;
    ret[0].o = vc;
  } else {
    ret[0].o = nullptr;
  }
}

static void VolumeControl_setLevel(VM&, Value* args, Value* ret) {
  VolumeControlObj* vc = static_cast<VolumeControlObj*>(args[0].o);
  int level = args[1].i;
  if (vc && vc->player) {
    vc->player->volume = level;
    int sdlVol = (level * MIX_MAX_VOLUME) / 100;
    if (vc->player->isMusic) {
      if (vc->player->handle) Mix_VolumeMusic(sdlVol);
      MidiSynth::setVolume(level);
    } else {
      Mix_Volume(-1, sdlVol);
    }
  }
  ret[0].i = level;
}

// -------------------------------------------------------------
// Registro das APIs MIDP
// -------------------------------------------------------------
void VM::registerMidp() {
  auto reg = [this](const std::string& key, NativeFn fn) {
    natives[key] = fn;
  };

  // Display
  reg("javax/microedition/lcdui/Display.getDisplay:(Ljavax/microedition/midlet/MIDlet;)Ljavax/microedition/lcdui/Display;", Display_getDisplay);
  reg("javax/microedition/lcdui/Display.setCurrent:(Ljavax/microedition/lcdui/Displayable;)V", Display_setCurrent);
  reg("javax/microedition/lcdui/Display.callSerially:(Ljava/lang/Runnable;)V", Display_callSerially);

  // Displayable
  reg("javax/microedition/lcdui/Displayable.getWidth:()I", Displayable_getWidth);
  reg("javax/microedition/lcdui/Displayable.getHeight:()I", Displayable_getHeight);

  // Canvas
  reg("javax/microedition/lcdui/Canvas.<init>:()V", Canvas_init);
  reg("javax/microedition/lcdui/Canvas.setFullScreenMode:(Z)V", Canvas_setFullScreenMode);
  reg("javax/microedition/lcdui/Canvas.repaint:()V", Canvas_repaint);
  reg("javax/microedition/lcdui/Canvas.getGameAction:(I)I", Canvas_getGameAction);
  reg("javax/microedition/lcdui/Canvas.keyReleased:(I)V", Canvas_keyReleased);

  // Graphics
  reg("javax/microedition/lcdui/Graphics.setColor:(I)V", Graphics_setColor_rgb);
  reg("javax/microedition/lcdui/Graphics.setColor:(III)V", Graphics_setColor_components);
  reg("javax/microedition/lcdui/Graphics.getColor:()I", Graphics_getColor);
  reg("javax/microedition/lcdui/Graphics.setClip:(IIII)V", Graphics_setClip);
  reg("javax/microedition/lcdui/Graphics.clipRect:(IIII)V", Graphics_clipRect);
  reg("javax/microedition/lcdui/Graphics.getClipX:()I", Graphics_getClipX);
  reg("javax/microedition/lcdui/Graphics.getClipY:()I", Graphics_getClipY);
  reg("javax/microedition/lcdui/Graphics.getClipWidth:()I", Graphics_getClipWidth);
  reg("javax/microedition/lcdui/Graphics.getClipHeight:()I", Graphics_getClipHeight);
  reg("javax/microedition/lcdui/Graphics.translate:(II)V", Graphics_translate);
  reg("javax/microedition/lcdui/Graphics.fillRect:(IIII)V", Graphics_fillRect);
  reg("javax/microedition/lcdui/Graphics.drawRect:(IIII)V", Graphics_drawRect);
  reg("javax/microedition/lcdui/Graphics.drawLine:(IIII)V", Graphics_drawLine);
  reg("javax/microedition/lcdui/Graphics.fillArc:(IIIIII)V", Graphics_fillArc);
  reg("javax/microedition/lcdui/Graphics.drawImage:(Ljavax/microedition/lcdui/Image;III)V", Graphics_drawImage);

  // Image
  reg("javax/microedition/lcdui/Image.createImage:(Ljava/lang/String;)Ljavax/microedition/lcdui/Image;", Image_createImage_str);
  reg("javax/microedition/lcdui/Image.createImage:([BII)Ljavax/microedition/lcdui/Image;", Image_createImage_bytes);
  reg("javax/microedition/lcdui/Image.createImage:(II)Ljavax/microedition/lcdui/Image;", Image_createImage_dim);
  reg("javax/microedition/lcdui/Image.getGraphics:()Ljavax/microedition/lcdui/Graphics;", Image_getGraphics);
  reg("javax/microedition/lcdui/Image.getWidth:()I", Image_getWidth);
  reg("javax/microedition/lcdui/Image.getHeight:()I", Image_getHeight);

  // MIDlet
  reg("javax/microedition/midlet/MIDlet.<init>:()V", MIDlet_init);
  reg("javax/microedition/midlet/MIDlet.getAppProperty:(Ljava/lang/String;)Ljava/lang/String;", MIDlet_getAppProperty);
  reg("javax/microedition/midlet/MIDlet.notifyDestroyed:()V", MIDlet_notifyDestroyed);
  reg("javax/microedition/midlet/MIDlet.platformRequest:(Ljava/lang/String;)Z", MIDlet_platformRequest);

  // RecordStore
  reg("javax/microedition/rms/RecordStore.openRecordStore:(Ljava/lang/String;Z)Ljavax/microedition/rms/RecordStore;", RecordStore_openRecordStore);
  reg("javax/microedition/rms/RecordStore.addRecord:([BII)I", RecordStore_addRecord);
  reg("javax/microedition/rms/RecordStore.setRecord:(I[BII)V", RecordStore_setRecord);
  reg("javax/microedition/rms/RecordStore.getRecord:(I)[B", RecordStore_getRecord);
  reg("javax/microedition/rms/RecordStore.getRecordSize:(I)I", RecordStore_getRecordSize);
  reg("javax/microedition/rms/RecordStore.getNextRecordID:()I", RecordStore_getNextRecordID);
  reg("javax/microedition/rms/RecordStore.getNumRecords:()I", RecordStore_getNumRecords);
  reg("javax/microedition/rms/RecordStore.closeRecordStore:()V", RecordStore_closeRecordStore);
  reg("javax/microedition/rms/RecordStore.deleteRecordStore:(Ljava/lang/String;)V", RecordStore_deleteRecordStore);

  // Media
  reg("javax/microedition/media/Manager.createPlayer:(Ljava/io/InputStream;Ljava/lang/String;)Ljavax/microedition/media/Player;", Manager_createPlayer_stream);
  reg("javax/microedition/media/Manager.createPlayer:(Ljava/lang/String;)Ljavax/microedition/media/Player;", Manager_createPlayer_url);
  reg("javax/microedition/media/Player.addPlayerListener:(Ljavax/microedition/media/PlayerListener;)V", Player_addPlayerListener);
  reg("javax/microedition/media/Player.close:()V", Player_close);
  reg("javax/microedition/media/Player.getState:()I", Player_getState);
  reg("javax/microedition/media/Player.prefetch:()V", Player_prefetch);
  reg("javax/microedition/media/Player.realize:()V", Player_realize);
  reg("javax/microedition/media/Player.setLoopCount:(I)V", Player_setLoopCount);
  reg("javax/microedition/media/Player.start:()V", Player_start);
  reg("javax/microedition/media/Player.stop:()V", Player_stop);
  reg("javax/microedition/media/Controllable.getControl:(Ljava/lang/String;)Ljavax/microedition/media/Control;", Player_getControl);
  reg("javax/microedition/media/PlayerImpl.getControl:(Ljava/lang/String;)Ljavax/microedition/media/Control;", Player_getControl);
  reg("javax/microedition/media/control/VolumeControl.setLevel:(I)I", VolumeControl_setLevel);
  reg("javax/microedition/media/VolumeControlImpl.setLevel:(I)I", VolumeControl_setLevel);
}

} // namespace hl
