#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>

#if defined(__has_include)
  #if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #else
    #include <SDL.h>
  #endif
#else
  #include <SDL.h>
#endif

namespace hl {

#define HL_VERSION_TAG "v1.2.5"
#define HL_VERSION_NUM "1.2.5"

enum class UpdateState {
  IDLE,
  CHECKING,
  UPDATE_AVAILABLE,
  NO_UPDATE,
  CHECK_FAILED,
  CONFIRM_PROMPT,
  DOWNLOADING,
  DOWNLOAD_COMPLETE,
  DOWNLOAD_FAILED,
  APPLYING,
  RESTART_READY
};

struct UpdateReleaseInfo {
  std::string tagName;
  std::string releaseName;
  std::string releaseNotes;
  std::string assetName;
  std::string assetUrl;
  size_t assetSize = 0;
};

class Updater {
public:
  static void init();
  static void shutdown();

  // Versão local atual do jogo
  static std::string getLocalVersion();

  // Inicia verificação assíncrona na API do GitHub
  static void checkAsync(bool notifyIfNoUpdate = true);

  // Inicia o download da atualização encontrada
  static void startDownload();

  // Aplica o update baixado conforme a plataforma
  static bool applyUpdate();

  // Estado atual e informações do release
  static UpdateState getState();
  static const UpdateReleaseInfo& getReleaseInfo();
  static float getDownloadProgress();
  static std::string getStatusMessage();

  // Controle de diálogo e confirmação
  static bool isPromptActive();
  static void showPrompt();
  static void dismissPrompt();
  static void confirmUpdate();

  // Manipulação de entrada no diálogo (teclas 5/Enter/A e 7/ESC/B e cliques/toque)
  static bool handleInput(int key);
  static void handleClick(int x, int y);

  // Renderização da interface modal e barra de progresso sobre o renderer SDL
  static void drawModal(SDL_Renderer* renderer, int winW, int winH);
};

} // namespace hl
