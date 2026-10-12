// platform.h — interface de plataforma (janela, entrada, renderização)
#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace hl {

struct VM;

void boot_log(const char* fmt, ...);

class Platform {
public:
  static bool init(int scale = 2);
  static bool pollEvents(VM& vm);
  static void present();
  static void shutdown();
  static bool shouldQuit();
  static void rumble(float strength = 0.5f, int durationMs = 150);

  static void requestQuit();
  static void setExecutablePath(const std::string& path);
  static std::string getExecutablePath();

  // Recursos Passo 7: Molduras Temáticas (Bezels), Aspect Ratio e FPS Limiter
  static void toggleBezel();
  static void toggleFps();
  static void toggleAspect(VM* vm = nullptr);
  static void nextAspect(VM* vm = nullptr);
  static void prevAspect(VM* vm = nullptr);
  static int getAspectMode();
  static void setAspectMode(int mode, VM* vm = nullptr);
  static void updateViewport(VM& vm);
  static void toggleFullscreen();
  static void showOsdMessage(const std::string& msg);
  static void framePacerWait();
  static int getTargetFps();

  // Recursos Passo 8: Detecção e Atualização OTA via GitHub Releases
  static void checkForUpdates();
  static void drawText(void* renderer, const std::string& text, int x, int y, int charW, int charH, uint8_t alpha = 255, int stepX = 0);
  static void drawTextColored(void* renderer, const std::string& text, int x, int y, int fontSize, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
  static int getTextWidth(const std::string& text, int charW, int stepX = 0);

  // Recursos Passo 9: Cloud Save & Sincronização Cruzada (Google Drive)
  static void openCloudSave(VM* vm = nullptr);

  // Passo 10: Localização e Idiomas (PT, EN, IT, ES)
  static std::string getCurrentLanguage();
  static void setLanguage(const std::string& langCode);
  static void nextLanguage(VM* vm = nullptr);
  static void prevLanguage(VM* vm = nullptr);
  static std::string getLanguageDisplayName(const std::string& langCode);
  static void reloadLanguage(VM& vm);

  // Sistema de Arquivos / Assets Portável (PC, Android APK, Switch RomFS)
  static std::vector<uint8_t> readAsset(const std::string& path);
  static std::string getStorageDir();
  static std::string getRmsDir(VM* vm = nullptr);
  static void cleanupRomfs();
};

} // namespace hl
