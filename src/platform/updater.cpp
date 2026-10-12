// updater.cpp — Sistema de Verificação e Atualização OTA via GitHub Releases
#include "updater.h"
#include "platform.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <sstream>

#if defined(__has_include)
  #if __has_include(<SDL2/SDL.h>)
    #include <SDL2/SDL.h>
  #else
    #include <SDL.h>
  #endif
#else
  #include <SDL.h>
#endif

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <winhttp.h>
  #include <shellapi.h>
#elif defined(__SWITCH__)
  #include <switch.h>
  #include <curl/curl.h>
  #include <unistd.h>
#elif defined(__ANDROID__)
  #include <jni.h>
  #include <unistd.h>
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL_system.h>)
      #include <SDL2/SDL_system.h>
    #else
      #include <SDL_system.h>
    #endif
  #else
    #include <SDL_system.h>
  #endif
#elif defined(__linux__)
  #include <curl/curl.h>
  #include <unistd.h>
  #include <sys/stat.h>
#endif

namespace hl {

void boot_log(const char* fmt, ...);

static UpdateState s_state = UpdateState::IDLE;
static UpdateReleaseInfo s_releaseInfo;
static std::atomic<float> s_downloadProgress{0.0f};
static std::atomic<size_t> s_downloadedBytes{0};
static std::atomic<size_t> s_totalBytes{0};
static std::string s_statusMessage = "";
static std::mutex s_updaterMutex;
static bool s_promptActive = false;

extern SDL_Renderer* s_renderer;

// Helper para comparar tags semânticas (v1.0.3 vs v1.0.2)
static bool isNewerVersion(const std::string& remoteTag, const std::string& localTag) {
  int rMaj = 0, rMin = 0, rPatch = 0;
  int lMaj = 0, lMin = 0, lPatch = 0;
  const char* rStr = remoteTag.c_str();
  if (rStr[0] == 'v' || rStr[0] == 'V') rStr++;
  const char* lStr = localTag.c_str();
  if (lStr[0] == 'v' || lStr[0] == 'V') lStr++;
  if (sscanf(rStr, "%d.%d.%d", &rMaj, &rMin, &rPatch) < 1) return false;
  if (sscanf(lStr, "%d.%d.%d", &lMaj, &lMin, &lPatch) < 1) return false;
  if (rMaj != lMaj) return rMaj > lMaj;
  if (rMin != lMin) return rMin > lMin;
  return rPatch > lPatch;
}

// Extrator simples de valores em JSON sem dependências externas
static std::string extractJsonString(const std::string& json, const std::string& key) {
  size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return "";
  size_t colon = json.find(':', k);
  if (colon == std::string::npos) return "";
  size_t q1 = json.find('"', colon);
  if (q1 == std::string::npos) return "";
  size_t q2 = json.find('"', q1 + 1);
  if (q2 == std::string::npos) return "";
  return json.substr(q1 + 1, q2 - q1 - 1);
}

// Retorna lista de nomes de arquivos candidatos no release do GitHub para a plataforma atual (por ordem de prioridade)
static std::vector<std::string> getCandidateAssetNames() {
#if defined(__SWITCH__)
  return { "heroes_lore.nro" };
#elif defined(__ANDROID__)
  return { "heroes_lore_android_universal.apk", "heroes_lore_android_arm64.apk" };
#elif defined(_WIN32)
  return { "heroes_lore_windows_x64.zip", "heroes_lore.zip", "heroes_lore.exe" };
#elif defined(__linux__)
  return { "heroes_lore_linux_x86_64.AppImage" };
#else
  return { "heroes_lore" };
#endif
}

// Parser da resposta do GitHub para extrair informações do release e do asset da plataforma
static bool parseReleaseJson(const std::string& json, UpdateReleaseInfo& out) {
  out.tagName = extractJsonString(json, "tag_name");
  out.releaseName = extractJsonString(json, "name");
  out.releaseNotes = extractJsonString(json, "body");
  if (out.tagName.empty()) return false;

  std::vector<std::string> candidates = getCandidateAssetNames();

  struct FoundAsset {
    std::string name;
    std::string url;
    size_t size = 0;
  };
  std::vector<FoundAsset> foundAssets;

  // Varre todos os assets disponíveis no JSON do release
  size_t pos = 0;
  while ((pos = json.find("\"name\"", pos)) != std::string::npos) {
    size_t colon = json.find(':', pos);
    if (colon == std::string::npos) break;
    size_t q1 = json.find('"', colon);
    if (q1 == std::string::npos) break;
    size_t q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) break;

    std::string aname = json.substr(q1 + 1, q2 - q1 - 1);
    std::string aurl = "";
    size_t asize = 0;

    // Procura URL de download no mesmo bloco do asset
    size_t urlKey = json.find("\"browser_download_url\"", q2);
    if (urlKey != std::string::npos) {
      size_t uColon = json.find(':', urlKey);
      size_t uQ1 = json.find('"', uColon);
      size_t uQ2 = json.find('"', uQ1 + 1);
      if (uQ1 != std::string::npos && uQ2 != std::string::npos) {
        aurl = json.substr(uQ1 + 1, uQ2 - uQ1 - 1);
      }
    }

    // Procura tamanho do asset
    size_t sizeKey = json.find("\"size\"", q2);
    if (sizeKey != std::string::npos && (urlKey == std::string::npos || sizeKey < urlKey)) {
      size_t sColon = json.find(':', sizeKey);
      if (sColon != std::string::npos) {
        asize = std::strtoul(json.c_str() + sColon + 1, nullptr, 10);
      }
    }

    if (!aname.empty() && !aurl.empty()) {
      foundAssets.push_back({ aname, aurl, asize });
    }
    pos = q2 + 1;
  }

  // Seleciona o melhor asset baseado na lista de prioridades de candidatos
  for (const auto& cand : candidates) {
    for (const auto& fa : foundAssets) {
      if (fa.name == cand || fa.name.find(cand) != std::string::npos) {
        out.assetName = fa.name;
        out.assetUrl = fa.url;
        out.assetSize = fa.size;
        return true;
      }
    }
  }

  return false;
}

#if defined(_WIN32)
// Implementação nativa de requisição HTTP(S) no Windows via WinHTTP
static std::string winHttpGet(const std::wstring& host, const std::wstring& path, bool followRedirects = true) {
  std::string response;
  HINTERNET hSession = WinHttpOpen(L"Heroes-Lore-Updater/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return "";

  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return ""; }

  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                         NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
  }

  if (followRedirects) {
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
  }

  // Header do GitHub
  LPCWSTR headers = L"User-Agent: Heroes-Lore-Updater/1.0\r\nAccept: application/vnd.github.v3+json\r\n";
  WinHttpAddRequestHeaders(hRequest, headers, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

  // Timeouts curtos (3s conexão, 5s envio/recepção)
  WinHttpSetTimeouts(hRequest, 3000, 3000, 5000, 5000);

  if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(hRequest, NULL)) {
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    do {
      dwSize = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
      if (dwSize == 0) break;
      std::vector<char> buffer(dwSize + 1);
      if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
        response.append(buffer.data(), dwDownloaded);
      }
    } while (dwSize > 0);
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return response;
}

// Download de arquivo no Windows com progresso
static bool winHttpDownloadFile(const std::string& url, const std::string& outPath) {
  // Converte URL completa em host e path
  std::wstring wurl(url.begin(), url.end());
  URL_COMPONENTS urlComp;
  memset(&urlComp, 0, sizeof(urlComp));
  urlComp.dwStructSize = sizeof(urlComp);
  wchar_t hostName[256] = {0};
  wchar_t urlPath[2048] = {0};
  urlComp.lpszHostName = hostName;
  urlComp.dwHostNameLength = 256;
  urlComp.lpszUrlPath = urlPath;
  urlComp.dwUrlPathLength = 2048;

  if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &urlComp)) return false;

  HINTERNET hSession = WinHttpOpen(L"Heroes-Lore-Updater/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return false;

  HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

  DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath,
                                         NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return false;
  }

  DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

  LPCWSTR headers = L"User-Agent: Heroes-Lore-Updater/1.0\r\n";
  WinHttpAddRequestHeaders(hRequest, headers, (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);

  bool success = false;
  if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
      WinHttpReceiveResponse(hRequest, NULL)) {
    
    // Obtém Content-Length se disponível
    wchar_t szContentLength[32] = {0};
    DWORD cchContentLength = sizeof(szContentLength);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, NULL, szContentLength, &cchContentLength, NULL)) {
      size_t total = (size_t)_wtoi64(szContentLength);
      if (total > 0) s_totalBytes = total;
    }

    FILE* fp = fopen(outPath.c_str(), "wb");
    if (fp) {
      DWORD dwSize = 0;
      DWORD dwDownloaded = 0;
      size_t current = 0;
      do {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
        if (dwSize == 0) break;
        std::vector<char> buffer(dwSize);
        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
          fwrite(buffer.data(), 1, dwDownloaded, fp);
          current += dwDownloaded;
          s_downloadedBytes = current;
          if (s_totalBytes > 0) {
            s_downloadProgress = (float)current / (float)s_totalBytes.load();
          }
        }
      } while (dwSize > 0);
      fclose(fp);
      success = (current > 0);
    }
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return success;
}
#endif

#if defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))
#if defined(__SWITCH__)
static bool s_socketInitialized = false;
#endif

// Callback de escrita de dados com libcurl
static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  std::string* str = static_cast<std::string*>(userp);
  str->append(static_cast<char*>(contents), total);
  return total;
}

static size_t curlWriteFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  FILE* fp = static_cast<FILE*>(userp);
  return fwrite(contents, size, nmemb, fp);
}

static int curlProgressCallback(void*, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
  if (dltotal > 0) {
    s_totalBytes = (size_t)dltotal;
    s_downloadedBytes = (size_t)dlnow;
    s_downloadProgress = (float)dlnow / (float)dltotal;
  }
  return 0;
}

static std::string curlHttpGet(const std::string& url) {
#if defined(__SWITCH__)
  if (!s_socketInitialized) return "";
#endif
  CURL* curl = curl_easy_init();
  if (!curl) return "";
  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Heroes-Lore-Updater/1.0");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  return (res == CURLE_OK) ? response : "";
}

static bool curlDownloadFile(const std::string& url, const std::string& outPath) {
#if defined(__SWITCH__)
  if (!s_socketInitialized) return false;
#endif
  FILE* fp = fopen(outPath.c_str(), "wb");
  if (!fp) return false;

  CURL* curl = curl_easy_init();
  if (!curl) { fclose(fp); return false; }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Heroes-Lore-Updater/1.0");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteFileCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

  CURLcode res = curl_easy_perform(curl);
  fclose(fp);
  curl_easy_cleanup(curl);
  return (res == CURLE_OK);
}
#endif

#if defined(__ANDROID__)
static jclass s_activityClass = nullptr;
static jmethodID s_midHttpGet = nullptr;
static jmethodID s_midDownloadFile = nullptr;
static jmethodID s_midInstallApk = nullptr;

static void androidInitJni() {
  if (s_activityClass) return;
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return;

  // Tenta obter a instância viva do SDLActivity
  jobject activityObj = (jobject)SDL_AndroidGetActivity();
  if (activityObj) {
    jclass localClass = env->GetObjectClass(activityObj);
    if (localClass) {
      s_activityClass = (jclass)env->NewGlobalRef(localClass);
      env->DeleteLocalRef(localClass);
    }
  }

  // Fallback para FindClass se GetActivity ainda não estiver pronto
  if (!s_activityClass) {
    jclass localClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (localClass) {
      s_activityClass = (jclass)env->NewGlobalRef(localClass);
      env->DeleteLocalRef(localClass);
    }
  }

  if (s_activityClass) {
    s_midHttpGet = env->GetStaticMethodID(s_activityClass, "httpGet", "(Ljava/lang/String;)Ljava/lang/String;");
    s_midDownloadFile = env->GetStaticMethodID(s_activityClass, "downloadFile", "(Ljava/lang/String;Ljava/lang/String;)Z");
    s_midInstallApk = env->GetStaticMethodID(s_activityClass, "installApk", "(Ljava/lang/String;)V");
  }

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}

static std::string androidHttpGet(const std::string& url) {
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return "";

  if (!s_activityClass || !s_midHttpGet) {
    androidInitJni();
  }
  if (!s_activityClass || !s_midHttpGet) return "";

  jstring jUrl = env->NewStringUTF(url.c_str());
  if (!jUrl) return "";

  jstring jRes = (jstring)env->CallStaticObjectMethod(s_activityClass, s_midHttpGet, jUrl);
  env->DeleteLocalRef(jUrl);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return "";
  }
  if (!jRes) return "";

  const char* utf = env->GetStringUTFChars(jRes, nullptr);
  std::string result = utf ? utf : "";
  if (utf) env->ReleaseStringUTFChars(jRes, utf);
  env->DeleteLocalRef(jRes);

  return result;
}

static bool androidDownloadFile(const std::string& url, const std::string& outPath) {
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return false;

  if (!s_activityClass || !s_midDownloadFile) {
    androidInitJni();
  }
  if (!s_activityClass || !s_midDownloadFile) return false;

  jstring jUrl = env->NewStringUTF(url.c_str());
  jstring jPath = env->NewStringUTF(outPath.c_str());
  if (!jUrl || !jPath) {
    if (jUrl) env->DeleteLocalRef(jUrl);
    if (jPath) env->DeleteLocalRef(jPath);
    return false;
  }

  jboolean res = env->CallStaticBooleanMethod(s_activityClass, s_midDownloadFile, jUrl, jPath);
  env->DeleteLocalRef(jUrl);
  env->DeleteLocalRef(jPath);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return false;
  }

  return res == JNI_TRUE;
}

static void androidInstallApk(const std::string& apkPath) {
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return;

  if (!s_activityClass || !s_midInstallApk) {
    androidInitJni();
  }
  if (!s_activityClass || !s_midInstallApk) return;

  jstring jPath = env->NewStringUTF(apkPath.c_str());
  if (jPath) {
    env->CallStaticVoidMethod(s_activityClass, s_midInstallApk, jPath);
    env->DeleteLocalRef(jPath);
  }

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}
#endif

void Updater::init() {
#if defined(__SWITCH__)
  Result rc = socketInitializeDefault();
  if (R_SUCCEEDED(rc)) {
    s_socketInitialized = true;
    curl_global_init(CURL_GLOBAL_ALL);
  }
#elif defined(__linux__) && !defined(__ANDROID__)
  curl_global_init(CURL_GLOBAL_ALL);
#elif defined(__ANDROID__)
  androidInitJni();
#endif
  s_state = UpdateState::IDLE;
  s_promptActive = false;
}

void Updater::shutdown() {
#if defined(__SWITCH__)
  if (s_socketInitialized) {
    curl_global_cleanup();
    socketExit();
    s_socketInitialized = false;
  }
#elif defined(__linux__) && !defined(__ANDROID__)
  curl_global_cleanup();
#elif defined(__ANDROID__)
  if (s_activityClass) {
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    if (env) {
      env->DeleteGlobalRef(s_activityClass);
    }
    s_activityClass = nullptr;
    s_midHttpGet = nullptr;
    s_midDownloadFile = nullptr;
    s_midInstallApk = nullptr;
  }
#endif
}

std::string Updater::getLocalVersion() {
  return HL_VERSION_TAG;
}

struct CheckUpdateThreadArgs {
  bool notifyIfNoUpdate;
};

static int runCheckUpdateThread(void* data) {
  CheckUpdateThreadArgs* args = static_cast<CheckUpdateThreadArgs*>(data);
  bool notifyIfNoUpdate = args ? args->notifyIfNoUpdate : false;
  delete args;

  std::string json;
#if defined(_WIN32)
  json = winHttpGet(L"api.github.com", L"/repos/davidkalil10/Heroes-Lore-Recomp/releases/latest");
#elif defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))
  json = curlHttpGet("https://api.github.com/repos/davidkalil10/Heroes-Lore-Recomp/releases/latest");
#elif defined(__ANDROID__)
  json = androidHttpGet("https://api.github.com/repos/davidkalil10/Heroes-Lore-Recomp/releases/latest");
#endif

  std::lock_guard<std::mutex> lock(s_updaterMutex);
  if (json.empty()) {
    s_state = UpdateState::CHECK_FAILED;
    s_statusMessage = "Nao foi possivel conectar ao GitHub.";
    if (notifyIfNoUpdate) {
      Platform::showOsdMessage("Atualizacao: Sem conexao com a internet.");
    }
    return 0;
  }

  // Se o repositório for privado ou o endpoint não tiver release público (404 Not Found)
  if (json.find("\"Not Found\"") != std::string::npos || json.find("\"status\":\"404\"") != std::string::npos) {
    s_state = UpdateState::NO_UPDATE;
    s_statusMessage = "Jogo atualizado (" + std::string(HL_VERSION_TAG) + ").";
    if (notifyIfNoUpdate) {
      Platform::showOsdMessage("Jogo atualizado (" + std::string(HL_VERSION_TAG) + ").\nNenhuma versao nova encontrada.");
    }
    return 0;
  }

  UpdateReleaseInfo info;
  if (parseReleaseJson(json, info)) {
    s_releaseInfo = info;
    if (isNewerVersion(info.tagName, HL_VERSION_TAG)) {
      s_state = UpdateState::UPDATE_AVAILABLE;
      s_statusMessage = "Nova versao disponivel: " + info.tagName;
      s_promptActive = true; // Abre o diálogo nobre com o aviso crucial de salvar o jogo
      Platform::showOsdMessage("Nova versao " + info.tagName + " disponivel!");
    } else {
      s_state = UpdateState::NO_UPDATE;
      s_statusMessage = "Voce ja possui a versao mais recente (" + std::string(HL_VERSION_TAG) + ").";
      if (notifyIfNoUpdate) {
        Platform::showOsdMessage("Jogo atualizado (" + std::string(HL_VERSION_TAG) + ").\nNenhuma versao nova encontrada.");
      }
    }
  } else {
    s_state = UpdateState::CHECK_FAILED;
    s_statusMessage = "Falha ao processar dados de release.";
    if (notifyIfNoUpdate) {
      Platform::showOsdMessage("Jogo atualizado (" + std::string(HL_VERSION_TAG) + ").\nNenhuma versao nova encontrada.");
    }
  }
  return 0;
}

void Updater::checkAsync(bool notifyIfNoUpdate) {
  if (s_state == UpdateState::CHECKING || s_state == UpdateState::DOWNLOADING) {
    if (notifyIfNoUpdate) {
      Platform::showOsdMessage("Verificacao de atualizacao ja em andamento...");
    }
    return;
  }
  s_state = UpdateState::CHECKING;
  s_statusMessage = "Verificando atualizacoes no GitHub...";
  if (notifyIfNoUpdate) {
    Platform::showOsdMessage("Verificando atualizacoes no GitHub...");
  }

  CheckUpdateThreadArgs* args = new CheckUpdateThreadArgs{notifyIfNoUpdate};
  SDL_Thread* th = SDL_CreateThreadWithStackSize(runCheckUpdateThread, "HL_CheckUpdate", 1024 * 1024, args);
  if (!th) {
    th = SDL_CreateThread(runCheckUpdateThread, "HL_CheckUpdate", args);
  }
  if (th) {
    SDL_DetachThread(th);
  } else {
    delete args;
    s_state = UpdateState::CHECK_FAILED;
  }
}

struct DownloadThreadArgs {
  std::string downloadUrl;
  std::string assetName;
};

static int runDownloadThread(void* data) {
  DownloadThreadArgs* args = static_cast<DownloadThreadArgs*>(data);
  std::string downloadUrl = args->downloadUrl;
  std::string assetName = args->assetName;
  delete args;

  std::string destPath = Platform::getStorageDir() + "/" + assetName + ".download";
  bool ok = false;
#if defined(_WIN32)
  ok = winHttpDownloadFile(downloadUrl, destPath);
#elif defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))
  ok = curlDownloadFile(downloadUrl, destPath);
#elif defined(__ANDROID__)
  ok = androidDownloadFile(downloadUrl, destPath);
#endif

  std::lock_guard<std::mutex> lock(s_updaterMutex);
  if (ok) {
    s_downloadProgress = 1.0f;
    s_statusMessage = "Download concluido! Aplicando...";
    bool applied = Updater::applyUpdate();
    if (applied) {
      s_state = UpdateState::RESTART_READY;
      s_statusMessage = "Atualizacao concluida! Reinicie o aplicativo.";
    } else {
      s_state = UpdateState::DOWNLOAD_FAILED;
      s_statusMessage = "Falha ao gravar arquivo de atualizacao.";
      Platform::showOsdMessage("Falha ao gravar atualizacao.");
    }
  } else {
    s_state = UpdateState::DOWNLOAD_FAILED;
    s_statusMessage = "Falha no download da atualizacao.";
    Platform::showOsdMessage("Falha no download da atualizacao.");
  }
  return 0;
}

void Updater::startDownload() {
  if (s_releaseInfo.assetUrl.empty()) return;
  s_state = UpdateState::DOWNLOADING;
  s_downloadProgress = 0.0f;
  s_downloadedBytes = 0;
  s_totalBytes = s_releaseInfo.assetSize;
  s_statusMessage = "Baixando atualizacao: " + s_releaseInfo.assetName;

  DownloadThreadArgs* args = new DownloadThreadArgs{s_releaseInfo.assetUrl, s_releaseInfo.assetName};
  SDL_Thread* th = SDL_CreateThreadWithStackSize(runDownloadThread, "HL_Download", 1024 * 1024, args);
  if (!th) {
    th = SDL_CreateThread(runDownloadThread, "HL_Download", args);
  }
  if (th) {
    SDL_DetachThread(th);
  } else {
    delete args;
    s_state = UpdateState::DOWNLOAD_FAILED;
  }
}

static bool copyFile(const std::string& src, const std::string& dst) {
  FILE* in = fopen(src.c_str(), "rb");
  if (!in) return false;
  FILE* out = fopen(dst.c_str(), "wb");
  if (!out) {
    fclose(in);
    return false;
  }
  char buf[65536];
  size_t n = 0;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return false;
    }
  }
  fclose(in);
  fclose(out);
  return true;
}

static SDL_Rect s_modalRect = { 0, 0, 0, 0 };
static SDL_Rect s_btnConfirmRect = { 0, 0, 0, 0 };
static SDL_Rect s_btnCancelRect = { 0, 0, 0, 0 };
static SDL_Rect s_btnCloseX = { 0, 0, 0, 0 };

#if defined(__SWITCH__)
static std::string resolveSwitchNroPath() {
  std::string execPath = Platform::getExecutablePath();
  boot_log("[Updater-Switch] execPath bruto: '%s'\n", execPath.c_str());

  // Lista de candidatos com garantia absoluta do prefixo sdmc:/
  std::vector<std::string> candidates;

  if (!execPath.empty()) {
    std::string norm = execPath;
    if (norm.rfind("sdmc:/", 0) == 0) {
      candidates.push_back(norm);
    } else {
      if (norm[0] == '/') {
        candidates.push_back("sdmc:" + norm);
      } else {
        candidates.push_back("sdmc:/switch/" + norm);
        candidates.push_back("sdmc:/switch/heroes_lore/" + norm);
        candidates.push_back("sdmc:/" + norm);
      }
    }
  }

  candidates.push_back("sdmc:/switch/heroes_lore/heroes_lore.nro");
  candidates.push_back("sdmc:/switch/heroes_lore.nro");
  candidates.push_back("sdmc:/switch/HeroesLore.nro");
  candidates.push_back("sdmc:/heroes_lore.nro");

  for (const auto& path : candidates) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (fp) {
      fclose(fp);
      boot_log("[Updater-Switch] NRO confirmado em: %s\n", path.c_str());
      return path;
    }
  }

  boot_log("[Updater-Switch] Fallback para sdmc:/switch/heroes_lore/heroes_lore.nro\n");
  return "sdmc:/switch/heroes_lore/heroes_lore.nro";
}
#endif

bool Updater::applyUpdate() {
  std::string storage = Platform::getStorageDir();
  std::string tmpFile = storage + "/" + s_releaseInfo.assetName + ".download";

#if defined(__SWITCH__)
  // Libera qualquer descritor mantido aberto pela libnx no NRO para permitir gravação/substituição de forma segura
  Platform::cleanupRomfs();

  std::string targetNro = resolveSwitchNroPath();
  boot_log("[Updater-Switch] Substituindo NRO alvo: %s (a partir de %s)\n", targetNro.c_str(), tmpFile.c_str());

  FILE* chk = fopen(tmpFile.c_str(), "rb");
  if (!chk) {
    boot_log("[Updater-Switch] ERRO: arquivo temporario %s nao abre!\n", tmpFile.c_str());
    return false;
  }
  fseek(chk, 0, SEEK_END);
  long tmpSize = ftell(chk);
  fclose(chk);
  boot_log("[Updater-Switch] Tamanho do NRO baixado: %ld bytes\n", tmpSize);
  if (tmpSize <= 1024 * 1024) {
    boot_log("[Updater-Switch] ERRO: NRO baixado muito pequeno (%ld bytes)\n", tmpSize);
    remove(tmpFile.c_str());
    return false;
  }

  // 1. Tenta copia direta primeiro
  bool ok = copyFile(tmpFile, targetNro);
  boot_log("[Updater-Switch] copyFile direto para %s: %s (errno=%d: %s)\n",
           targetNro.c_str(), ok ? "SUCESSO" : "FALHA", errno, strerror(errno));

  // 2. Se copyFile direto falhou, tenta rotacionar com .old
  if (!ok) {
    std::string oldNro = targetNro + ".old";
    remove(oldNro.c_str());
    if (rename(targetNro.c_str(), oldNro.c_str()) == 0) {
      if (rename(tmpFile.c_str(), targetNro.c_str()) == 0) {
        ok = true;
        remove(oldNro.c_str());
        boot_log("[Updater-Switch] rename atomico tmpFile -> targetNro SUCESSO!\n");
      } else {
        ok = copyFile(tmpFile, targetNro);
        if (ok) {
          remove(oldNro.c_str());
          remove(tmpFile.c_str());
          boot_log("[Updater-Switch] copyFile pos-rename SUCESSO!\n");
        } else {
          rename(oldNro.c_str(), targetNro.c_str());
          boot_log("[Updater-Switch] Falhou gravar novo NRO, backup .old restaurado.\n");
        }
      }
    } else {
      boot_log("[Updater-Switch] rename de %s para %s falhou (errno=%d: %s)\n",
               targetNro.c_str(), oldNro.c_str(), errno, strerror(errno));
    }
  } else {
    remove(tmpFile.c_str());
  }

  if (ok) {
    envSetNextLoad(targetNro.c_str(), targetNro.c_str());
    Platform::showOsdMessage("Atualizacao concluida! Reinicie o aplicativo.");
    return true;
  }
  return false;

#elif defined(_WIN32)
  bool isZip = (s_releaseInfo.assetName.rfind(".zip") != std::string::npos ||
                s_releaseInfo.assetName.rfind(".ZIP") != std::string::npos);
  std::string targetExe = "heroes_lore.exe";
  std::string execPath = Platform::getExecutablePath();
  if (!execPath.empty() && (execPath.rfind(".exe") != std::string::npos || execPath.rfind(".EXE") != std::string::npos)) {
    targetExe = execPath;
  }

  if (isZip) {
    std::string appDir = ".";
    size_t lastSlash = targetExe.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
      appDir = targetExe.substr(0, lastSlash);
    }

    std::string extractDir = appDir + "/_update_extract";
    CreateDirectoryA(extractDir.c_str(), NULL);

    // Lambda para executar processos ocultos diretamente via CreateProcessA (sem cmd.exe para evitar corrupção de aspas)
    auto runSilently = [](const std::string& command, DWORD timeoutMs) -> bool {
      STARTUPINFOA si;
      ZeroMemory(&si, sizeof(si));
      si.cb = sizeof(si);
      si.dwFlags = STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_HIDE;
      PROCESS_INFORMATION pi;
      ZeroMemory(&pi, sizeof(pi));
      std::vector<char> buf(command.begin(), command.end());
      buf.push_back('\0');
      if (!CreateProcessA(NULL, buf.data(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        return false;
      }
      WaitForSingleObject(pi.hProcess, timeoutMs);
      DWORD exitCode = 1;
      GetExitCodeProcess(pi.hProcess, &exitCode);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return (exitCode == 0);
    };

    // 1. Tenta extrair para a pasta isolada com tar.exe
    std::string tarCmd = "tar.exe -xf \"" + tmpFile + "\" -C \"" + extractDir + "\"";
    bool ok = runSilently(tarCmd, 60000);
    if (!ok) {
      // 2. Fallback: PowerShell Expand-Archive
      std::string psCmd = "powershell.exe -NoProfile -NonInteractive -Command \"Expand-Archive -Force -LiteralPath '" + tmpFile + "' -DestinationPath '" + extractDir + "'\"";
      ok = runSilently(psCmd, 90000);
    }

    if (ok) {
      // 1. Atualiza heroes_lore.exe
      std::string newExe = extractDir + "/heroes_lore.exe";
      std::string oldExe = targetExe + ".old";
      remove(oldExe.c_str());
      MoveFileA(targetExe.c_str(), oldExe.c_str());
      MoveFileA(newExe.c_str(), targetExe.c_str());

      // 2. Mescla a pasta assets/ atualizada
      std::string newAssets = extractDir + "/assets";
      std::string targetAssets = appDir + "/assets";
      std::string roboCmd = "robocopy.exe \"" + newAssets + "\" \"" + targetAssets + "\" /E /MOVE /NFL /NDL /NJH /NJS";
      runSilently(roboCmd, 30000);

      // 3. Atualiza DLLs se houver novas
      WIN32_FIND_DATAA fd;
      HANDLE hFind = FindFirstFileA((extractDir + "/*.dll").c_str(), &fd);
      if (hFind != INVALID_HANDLE_VALUE) {
        do {
          std::string dllName = fd.cFileName;
          std::string srcDll = extractDir + "/" + dllName;
          std::string dstDll = appDir + "/" + dllName;
          std::string oldDll = dstDll + ".old";
          remove(oldDll.c_str());
          MoveFileA(dstDll.c_str(), oldDll.c_str());
          MoveFileA(srcDll.c_str(), dstDll.c_str());
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
      }

      // Limpeza da pasta de extração temporária
      std::string cleanCmd = "powershell.exe -NoProfile -NonInteractive -Command \"Remove-Item -Recurse -Force -LiteralPath '" + extractDir + "'\"";
      runSilently(cleanCmd, 15000);
    }

    remove(tmpFile.c_str());
    if (ok) {
      Platform::showOsdMessage("Atualizacao concluida! Reinicie o jogo.");
      return true;
    } else {
      std::string oldExe = targetExe + ".old";
      MoveFileA(oldExe.c_str(), targetExe.c_str());
      return false;
    }
  } else {
    std::string oldExe = targetExe + ".old";
    remove(oldExe.c_str());
    MoveFileA(targetExe.c_str(), oldExe.c_str());
    if (MoveFileA(tmpFile.c_str(), targetExe.c_str()) || copyFile(tmpFile, targetExe)) {
      remove(tmpFile.c_str());
      Platform::showOsdMessage("Atualizacao concluida! Reinicie o jogo.");
      return true;
    }
    return false;
  }

#elif defined(__linux__) && !defined(__ANDROID__)
  chmod(tmpFile.c_str(), 0755);
  std::string targetAppImage = "heroes_lore.AppImage";
  const char* appimageEnv = getenv("APPIMAGE");
  if (appimageEnv && appimageEnv[0] != '\0') {
    targetAppImage = appimageEnv;
  } else {
    std::string execPath = Platform::getExecutablePath();
    if (!execPath.empty() && execPath.find(".AppImage") != std::string::npos) {
      targetAppImage = execPath;
    }
  }
  remove(targetAppImage.c_str());
  if (rename(tmpFile.c_str(), targetAppImage.c_str()) == 0 || copyFile(tmpFile, targetAppImage)) {
    remove(tmpFile.c_str());
    chmod(targetAppImage.c_str(), 0755);
    Platform::showOsdMessage("Atualizado! Reinicie o aplicativo.");
    return true;
  }
  return false;

#elif defined(__ANDROID__)
  std::string targetApk = storage + "/" + s_releaseInfo.assetName;
  remove(targetApk.c_str());
  rename(tmpFile.c_str(), targetApk.c_str());
  androidInstallApk(targetApk);
  Platform::showOsdMessage("Instalador aberto! Conclua a atualizacao.");
  return true;
#endif

  return false;
}

UpdateState Updater::getState() { return s_state; }
const UpdateReleaseInfo& Updater::getReleaseInfo() { return s_releaseInfo; }
float Updater::getDownloadProgress() { return s_downloadProgress.load(); }
std::string Updater::getStatusMessage() { return s_statusMessage; }

bool Updater::isPromptActive() { return s_promptActive; }
void Updater::showPrompt() { s_promptActive = true; }
void Updater::dismissPrompt() {
  s_promptActive = false;
  s_btnConfirmRect = { 0, 0, 0, 0 };
  s_btnCancelRect = { 0, 0, 0, 0 };
  s_btnCloseX = { 0, 0, 0, 0 };
  s_modalRect = { 0, 0, 0, 0 };
  if (s_state == UpdateState::UPDATE_AVAILABLE || s_state == UpdateState::CHECK_FAILED) {
    s_state = UpdateState::IDLE;
  }
}

void Updater::confirmUpdate() {
  if (s_state == UpdateState::UPDATE_AVAILABLE) {
    startDownload();
  } else if (s_state == UpdateState::RESTART_READY) {
#if defined(_WIN32)
    std::string targetExe = "heroes_lore.exe";
    std::string execPath = Platform::getExecutablePath();
    if (!execPath.empty() && execPath.rfind(".exe") != std::string::npos) {
      targetExe = execPath;
    }
    ShellExecuteA(NULL, "open", targetExe.c_str(), NULL, NULL, SW_SHOWNORMAL);
    Platform::requestQuit();
#elif defined(__SWITCH__)
    Platform::requestQuit();
#elif defined(__linux__) && !defined(__ANDROID__)
    const char* appimage = getenv("APPIMAGE");
    if (appimage && appimage[0] != '\0') {
      execl(appimage, appimage, NULL);
    }
    Platform::requestQuit();
#elif defined(__ANDROID__)
    dismissPrompt();
#endif
  }
}

bool Updater::handleInput(int key) {
  if (!s_promptActive) return false;

  // Confirmar: 53 ('5'), 13 (Enter), 5 (Joypad A / Key 5), 32 (Space)
  if (key == 53 || key == 13 || key == 5 || key == 32) {
    confirmUpdate();
    return true;
  }
  // Cancelar: -7 (Joypad B / RSK), 7, 55 ('7'), 27 (ESC), 8 (Backspace)
  if (key == -7 || key == 7 || key == 55 || key == 27 || key == 8) {
    dismissPrompt();
    return true;
  }
  return true; // Bloqueia outros inputs de passarem para o jogo enquanto o modal estiver aberto
}

void Updater::handleClick(int x, int y) {
  if (!s_promptActive) return;

  SDL_Point pt = { x, y };
  if (s_btnConfirmRect.w > 0 && SDL_PointInRect(&pt, &s_btnConfirmRect)) {
    confirmUpdate();
    return;
  }
  if (s_btnCancelRect.w > 0 && SDL_PointInRect(&pt, &s_btnCancelRect)) {
    dismissPrompt();
    return;
  }
  if (s_btnCloseX.w > 0 && SDL_PointInRect(&pt, &s_btnCloseX)) {
    dismissPrompt();
    return;
  }
  // Se clicou fora da janela do modal: fecha o aviso com segurança
  if (s_modalRect.w > 0 && !SDL_PointInRect(&pt, &s_modalRect)) {
    if (s_state == UpdateState::UPDATE_AVAILABLE || s_state == UpdateState::CHECK_FAILED || s_state == UpdateState::NO_UPDATE) {
      dismissPrompt();
      return;
    }
  }
}

enum class UpdateStr {
  TITLE_AVAILABLE,
  WARN_TITLE,
  WARN_LINE1,
  WARN_LINE2,
  BTN_UPDATE,
  BTN_CANCEL,
  DOWNLOADING_TITLE,
  WAIT_NOTE,
  COMPLETE_TITLE,
  COMPLETE_LINE1,
  COMPLETE_LINE2,
  BTN_RESTART,
  ERROR_TITLE,
  DEFAULT_ERROR,
  BTN_CLOSE,
  STR_COUNT
};

static const char* s_updateTranslations[(size_t)UpdateStr::STR_COUNT][4] = {
  /* TITLE_AVAILABLE */ {
    "ATUALIZAÇÃO DISPONÍVEL",
    "UPDATE AVAILABLE",
    "AGGIORNAMENTO DISPONIBILE",
    "ACTUALIZACIÓN DISPONIBLE"
  },
  /* WARN_TITLE */ {
    "AVISO IMPORTANTE:",
    "IMPORTANT NOTICE:",
    "AVVISO IMPORTANTE:",
    "AVISO IMPORTANTE:"
  },
  /* WARN_LINE1 */ {
    "Salve o jogo antes de prosseguir.",
    "Save your game before proceeding.",
    "Salva il gioco prima di procedere.",
    "Guarda tu partida antes de continuar."
  },
  /* WARN_LINE2 */ {
    "O aplicativo será reiniciado.",
    "The application will restart.",
    "L'applicazione verrà riavviata.",
    "La aplicación se reiniciará."
  },
  /* BTN_UPDATE */ {
    "ATUALIZAR",
    "UPDATE",
    "AGGIORNA",
    "ACTUALIZAR"
  },
  /* BTN_CANCEL */ {
    "CANCELAR",
    "CANCEL",
    "ANNULLA",
    "CANCELAR"
  },
  /* DOWNLOADING_TITLE */ {
    "BAIXANDO ATUALIZAÇÃO",
    "DOWNLOADING UPDATE",
    "SCARICAMENTO IN CORSO",
    "DESCARGANDO ACTUALIZACIÓN"
  },
  /* WAIT_NOTE */ {
    "Aguarde... Não feche o jogo.",
    "Please wait... Do not close the game.",
    "Attendere... Non chiudere il gioco.",
    "Espere... No cierre el juego."
  },
  /* COMPLETE_TITLE */ {
    "ATUALIZAÇÃO CONCLUÍDA",
    "UPDATE COMPLETE",
    "AGGIORNAMENTO COMPLETATO",
    "ACTUALIZACIÓN COMPLETADA"
  },
  /* COMPLETE_LINE1 */ {
    "Arquivo instalado com sucesso!",
    "File installed successfully!",
    "File installato con successo!",
    "¡Archivo instalado con éxito!"
  },
  /* COMPLETE_LINE2 */ {
    "Reinicie para aplicar a nova versão.",
    "Restart to apply the new version.",
    "Riavvia per applicare la nuova versione.",
    "Reinicia para aplicar la nueva versión."
  },
  /* BTN_RESTART */ {
    "REINICIAR",
    "RESTART",
    "RIAVVIA",
    "REINICIAR"
  },
  /* ERROR_TITLE */ {
    "AVISO DE ATUALIZAÇÃO",
    "UPDATE NOTICE",
    "AVVISO DI AGGIORNAMENTO",
    "AVISO DE ACTUALIZACIÓN"
  },
  /* DEFAULT_ERROR */ {
    "Não foi possível concluir a atualização.",
    "Could not complete update.",
    "Impossibile completare l'aggiornamento.",
    "No se pudo completar la actualización."
  },
  /* BTN_CLOSE */ {
    "FECHAR",
    "CLOSE",
    "CHIUDI",
    "CERRAR"
  }
};

static std::string tr(UpdateStr id) {
  int lIdx = 0; // 0: pt, 1: en, 2: it, 3: es
  std::string cur = Platform::getCurrentLanguage();
  if (cur == "en") lIdx = 1;
  else if (cur == "it") lIdx = 2;
  else if (cur == "es") lIdx = 3;
  return s_updateTranslations[(size_t)id][lIdx];
}

// Renderização gráfica nobre da janela modal de atualização (Soltia Theme)
void Updater::drawModal(SDL_Renderer* renderer, int winW, int winH) {
  if (!s_promptActive || !renderer) return;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // 1. Escurecimento translúcido do fundo (Backdrop)
  SDL_Rect fullScreen = { 0, 0, winW, winH };
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 205);
  SDL_RenderFillRect(renderer, &fullScreen);

  // 2. Parâmetros de Dispositivo e Orientação
  bool isPortrait = (winH > winW);
#if defined(__ANDROID__)
  bool isMobile = true;
#else
  bool isMobile = false;
#endif

  // Escala Dinâmica Proporcional (Mesmo algoritmo calibrado do Cloud Save)
  float refDim = (float)std::min(winW, winH);
  float scaleFactor = std::max(0.85f, refDim / 720.0f);
  if (isMobile) {
    scaleFactor *= 1.15f;
  }

  // Largura Proporcional do Modal:
  // - Portrait Mobile: 92% da largura útil da tela (espaçoso e agradável)
  // - Landscape Mobile: proporcional à altura (winH * 1.05f), evitando o formato achatado
  // - Switch e PC Desktop: janela compacta, nobre e focada (entre 580px e 640px em 720p; ~900px em 1080p)
  int modalW;
  if (isPortrait) {
    modalW = (int)(winW * 0.92f);
  } else {
    if (isMobile) {
      modalW = std::clamp((int)(winH * 1.05f), 640, 1150);
    } else {
      modalW = std::clamp((int)(620.0f * scaleFactor), 540, (int)(winW * 0.65f));
    }
  }

  // Tipografia Vetorial Nítida
  int titleFontSize    = (int)(24.0f * scaleFactor);
  int headerH          = (int)(54.0f * scaleFactor);
  int badgeFontSize    = (int)(20.0f * scaleFactor);
  int warnTitleSize    = (int)(18.0f * scaleFactor);
  int bodyFontSize     = (int)(16.5f * scaleFactor);
  int subFontSize      = (int)(15.0f * scaleFactor);
  int btnFontSize      = (int)(19.0f * scaleFactor);
  int shortcutFontSize = (int)(13.5f * scaleFactor);

  int padX = (int)(22.0f * scaleFactor);
  int contentW = modalW - padX * 2;
  int btnH = (int)(50.0f * scaleFactor);
  int btnGap = (int)(14.0f * scaleFactor);

  // Atalhos por Plataforma (Mobile Touch limpo / Switch Joy-Con / PC)
  std::string scConfirm = "";
  std::string scCancel  = "";
#if defined(__SWITCH__)
  scConfirm = "( A )";
  scCancel  = "( B )";
#elif !defined(__ANDROID__)
  if (!isMobile) {
    scConfirm = "[ A / ENTER ]";
    scCancel  = "[ ESC / B ]";
  }
#endif

  // Cálculo Dinâmico de Altura (Content-Fitted)
  int neededH = headerH + (int)(18.0f * scaleFactor);

  if (s_state == UpdateState::UPDATE_AVAILABLE || s_state == UpdateState::CONFIRM_PROMPT) {
    int badgeH = (int)(38.0f * scaleFactor);
    int noteH = (int)(warnTitleSize + bodyFontSize * 2 + 42.0f * scaleFactor);
    neededH += badgeH + (int)(14.0f * scaleFactor) + noteH + (int)(18.0f * scaleFactor) + btnH + (int)(22.0f * scaleFactor);
  } else if (s_state == UpdateState::DOWNLOADING) {
    int barH = (int)(26.0f * scaleFactor);
    neededH += bodyFontSize + (int)(14.0f * scaleFactor) + barH + (int)(12.0f * scaleFactor) + bodyFontSize + (int)(12.0f * scaleFactor) + subFontSize + (int)(24.0f * scaleFactor);
  } else if (s_state == UpdateState::RESTART_READY || s_state == UpdateState::DOWNLOAD_COMPLETE) {
    neededH += bodyFontSize + (int)(12.0f * scaleFactor) + bodyFontSize + (int)(20.0f * scaleFactor) + btnH + (int)(24.0f * scaleFactor);
  } else {
    // Falha / Erro
    neededH += bodyFontSize + (int)(24.0f * scaleFactor) + btnH + (int)(24.0f * scaleFactor);
  }

  int maxH = (int)(winH * 0.94f);
  int modalH = std::min(neededH, maxH);
  int modalX = (winW - modalW) / 2;
  int modalY = (winH - modalH) / 2;
  s_modalRect = { modalX, modalY, modalW, modalH };

  // Sombra suave da caixa
  SDL_Rect shadow = { modalX + 6, modalY + 6, modalW, modalH };
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
  SDL_RenderFillRect(renderer, &shadow);

  // Fundo ardósia nobre de Soltia
  SDL_Rect box = { modalX, modalY, modalW, modalH };
  SDL_SetRenderDrawColor(renderer, 15, 20, 30, 252);
  SDL_RenderFillRect(renderer, &box);

  // Borda externa de aço
  SDL_SetRenderDrawColor(renderer, 50, 70, 95, 255);
  SDL_RenderDrawRect(renderer, &box);

  // Filete interno em ouro nobre
  int borderPad = 4;
  SDL_Rect goldBorder = { modalX + borderPad, modalY + borderPad, modalW - borderPad * 2, modalH - borderPad * 2 };
  SDL_SetRenderDrawColor(renderer, 190, 150, 60, 220);
  SDL_RenderDrawRect(renderer, &goldBorder);

  // Faixa de cabeçalho proporcional
  SDL_Rect headerBox = { modalX + 4, modalY + 4, modalW - 8, headerH };
  SDL_SetRenderDrawColor(renderer, 24, 34, 52, 255);
  SDL_RenderFillRect(renderer, &headerBox);
  SDL_SetRenderDrawColor(renderer, 190, 150, 60, 200);
  SDL_RenderDrawLine(renderer, headerBox.x, headerBox.y + headerH, headerBox.x + headerBox.w, headerBox.y + headerH);

  // Botão Fechar [X] no Cabeçalho (Confortável para touch no celular)
  if (s_state != UpdateState::DOWNLOADING) {
    int closeSize = headerH - (int)(16.0f * scaleFactor);
    if (closeSize < 24) closeSize = 24;
    s_btnCloseX = { modalX + modalW - closeSize - (int)(10.0f * scaleFactor), modalY + (headerH - closeSize) / 2, closeSize, closeSize };
    SDL_SetRenderDrawColor(renderer, 38, 48, 68, 220);
    SDL_RenderFillRect(renderer, &s_btnCloseX);
    SDL_SetRenderDrawColor(renderer, 110, 130, 165, 255);
    SDL_RenderDrawRect(renderer, &s_btnCloseX);

    std::string xGlyph = "X";
    int xw = Platform::getTextWidth(xGlyph, subFontSize);
    Platform::drawTextColored(renderer, xGlyph, s_btnCloseX.x + (closeSize - xw) / 2,
                              s_btnCloseX.y + (closeSize - subFontSize) / 2, subFontSize, 215, 225, 245, 255);
  } else {
    s_btnCloseX = { 0, 0, 0, 0 };
  }

  int curY = modalY + headerH + (int)(16.0f * scaleFactor);

  // Helper de Botão Moderno
  auto drawModernBtn = [&](const std::string& bTitle, const std::string& bSc, const SDL_Rect& bRect,
                           SDL_Color bg, SDL_Color border) {
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
    SDL_RenderFillRect(renderer, &bRect);

    // Chanfro superior
    SDL_SetRenderDrawColor(renderer, std::min(255, bg.r + 35), std::min(255, bg.g + 35), std::min(255, bg.b + 35), 255);
    SDL_RenderDrawLine(renderer, bRect.x + 1, bRect.y + 1, bRect.x + bRect.w - 2, bRect.y + 1);

    // Chanfro inferior
    SDL_SetRenderDrawColor(renderer, bg.r / 2, bg.g / 2, bg.b / 2, 255);
    SDL_RenderDrawLine(renderer, bRect.x + 1, bRect.y + bRect.h - 1, bRect.x + bRect.w - 2, bRect.y + bRect.h - 1);

    // Borda
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, 255);
    SDL_RenderDrawRect(renderer, &bRect);

    int effFontSize = btnFontSize;
    int tw = Platform::getTextWidth(bTitle, effFontSize);
    if (tw > bRect.w - (int)(16.0f * scaleFactor) && bRect.w > 20) {
      effFontSize = std::max(12, (int)(effFontSize * (float)(bRect.w - (int)(16.0f * scaleFactor)) / (float)tw));
      tw = Platform::getTextWidth(bTitle, effFontSize);
    }

    if (bSc.empty()) {
      int tx = bRect.x + (bRect.w - tw) / 2;
      int ty = bRect.y + (bRect.h - effFontSize) / 2;
      Platform::drawTextColored(renderer, bTitle, tx, ty, effFontSize, 255, 255, 255, 255);
    } else {
      int effScSize = shortcutFontSize;
      int scw = Platform::getTextWidth(bSc, effScSize);
      int totalH = effFontSize + effScSize + 4;
      int ty1 = bRect.y + (bRect.h - totalH) / 2;
      int ty2 = ty1 + effFontSize + 4;

      int tx1 = bRect.x + (bRect.w - tw) / 2;
      Platform::drawTextColored(renderer, bTitle, tx1, ty1, effFontSize, 255, 255, 255, 255);

      int tx2 = bRect.x + (bRect.w - scw) / 2;
      Platform::drawTextColored(renderer, bSc, tx2, ty2, effScSize, 205, 220, 240, 220);
    }
  };

  if (s_state == UpdateState::UPDATE_AVAILABLE || s_state == UpdateState::CONFIRM_PROMPT) {
    // Título do Cabeçalho
    std::string title = tr(UpdateStr::TITLE_AVAILABLE);
    int tw = Platform::getTextWidth(title, titleFontSize);
    Platform::drawTextColored(renderer, title, modalX + (modalW - tw) / 2, modalY + (headerH - titleFontSize) / 2 + 2,
                              titleFontSize, 240, 205, 110, 255);

    // Faixa/Badge de Versão (Estilo Pill Elegante)
    std::string vLine = std::string(HL_VERSION_TAG) + "  ->  " + s_releaseInfo.tagName;
    int vw = Platform::getTextWidth(vLine, badgeFontSize);
    int badgeW = std::min(contentW, vw + (int)(36.0f * scaleFactor));
    int badgeH = (int)(36.0f * scaleFactor);
    SDL_Rect verBox = { modalX + (modalW - badgeW) / 2, curY, badgeW, badgeH };

    SDL_SetRenderDrawColor(renderer, 18, 28, 44, 255);
    SDL_RenderFillRect(renderer, &verBox);
    SDL_SetRenderDrawColor(renderer, 65, 135, 195, 255);
    SDL_RenderDrawRect(renderer, &verBox);

    Platform::drawTextColored(renderer, vLine, verBox.x + (badgeW - vw) / 2,
                              verBox.y + (badgeH - badgeFontSize) / 2,
                              badgeFontSize, 180, 225, 255, 255);
    curY += badgeH + (int)(14.0f * scaleFactor);

    // Caixa de Alerta Âmbar de Salvamento
    int noteW = contentW;
    int noteH = (int)(warnTitleSize + bodyFontSize * 2 + 42.0f * scaleFactor);
    SDL_Rect noteBox = { modalX + padX, curY, noteW, noteH };
    SDL_SetRenderDrawColor(renderer, 28, 22, 14, 240);
    SDL_RenderFillRect(renderer, &noteBox);
    SDL_SetRenderDrawColor(renderer, 185, 135, 45, 255);
    SDL_RenderDrawRect(renderer, &noteBox);

    // Filete decorativo dourado na esquerda
    SDL_Rect noteBar = { noteBox.x, noteBox.y, (int)(4.0f * scaleFactor) + 1, noteBox.h };
    SDL_SetRenderDrawColor(renderer, 240, 185, 60, 255);
    SDL_RenderFillRect(renderer, &noteBar);

    std::string a1 = tr(UpdateStr::WARN_TITLE);
    std::string a2 = tr(UpdateStr::WARN_LINE1);
    std::string a3 = tr(UpdateStr::WARN_LINE2);

    int totalTextH = warnTitleSize + (int)(8.0f * scaleFactor) + bodyFontSize + (int)(4.0f * scaleFactor) + bodyFontSize;
    int startY = noteBox.y + (noteBox.h - totalTextH) / 2;

    int a1w = Platform::getTextWidth(a1, warnTitleSize);
    Platform::drawTextColored(renderer, a1, noteBox.x + (noteW - a1w) / 2, startY, warnTitleSize, 255, 200, 75, 255);

    startY += warnTitleSize + (int)(8.0f * scaleFactor);
    int a2w = Platform::getTextWidth(a2, bodyFontSize);
    Platform::drawTextColored(renderer, a2, noteBox.x + (noteW - a2w) / 2, startY, bodyFontSize, 240, 230, 220, 240);

    startY += bodyFontSize + (int)(4.0f * scaleFactor);
    int a3w = Platform::getTextWidth(a3, bodyFontSize);
    Platform::drawTextColored(renderer, a3, noteBox.x + (noteW - a3w) / 2, startY, bodyFontSize, 255, 145, 125, 240);
    curY += noteH + (int)(18.0f * scaleFactor);

    // Botões Interativos (Lado a Lado)
    int btnW = (contentW - btnGap) / 2;
    int by = modalY + modalH - btnH - (int)(18.0f * scaleFactor);
    if (by < curY) by = curY;

    s_btnConfirmRect = { modalX + padX, by, btnW, btnH };
    s_btnCancelRect = { modalX + padX + btnW + btnGap, by, btnW, btnH };

    // 1. Botão Confirmar (Ciano / Ouro Real)
    drawModernBtn(tr(UpdateStr::BTN_UPDATE), scConfirm, s_btnConfirmRect,
                  { 18, 55, 80, 255 }, { 45, 190, 240, 255 });

    // 2. Botão Cancelar (Ardósia Carmesim)
    drawModernBtn(tr(UpdateStr::BTN_CANCEL), scCancel, s_btnCancelRect,
                  { 48, 20, 26, 255 }, { 175, 65, 75, 255 });

  } else if (s_state == UpdateState::DOWNLOADING) {
    std::string title = tr(UpdateStr::DOWNLOADING_TITLE);
    int tw = Platform::getTextWidth(title, titleFontSize);
    Platform::drawTextColored(renderer, title, modalX + (modalW - tw) / 2, modalY + (headerH - titleFontSize) / 2 + 2,
                              titleFontSize, 240, 205, 110, 255);

    std::string fLine = s_releaseInfo.assetName;
    int fw = Platform::getTextWidth(fLine, bodyFontSize);
    Platform::drawTextColored(renderer, fLine, modalX + (modalW - fw) / 2, curY, bodyFontSize, 215, 230, 250, 240);
    curY += bodyFontSize + (int)(14.0f * scaleFactor);

    // Barra de progresso gráfica
    int barW = contentW;
    int barH = (int)(24.0f * scaleFactor);
    int barX = modalX + padX;
    int barY = curY;

    SDL_Rect barBg = { barX, barY, barW, barH };
    SDL_SetRenderDrawColor(renderer, 10, 14, 20, 255);
    SDL_RenderFillRect(renderer, &barBg);
    SDL_SetRenderDrawColor(renderer, 70, 100, 135, 255);
    SDL_RenderDrawRect(renderer, &barBg);

    float prog = std::min(1.0f, std::max(0.0f, s_downloadProgress.load()));
    int fillW = (int)((float)(barW - 4) * prog);
    if (fillW > 0) {
      SDL_Rect barFill = { barX + 2, barY + 2, fillW, barH - 4 };
      SDL_SetRenderDrawColor(renderer, 0, 215, 255, 255);
      SDL_RenderFillRect(renderer, &barFill);
    }
    curY += barH + (int)(12.0f * scaleFactor);

    char pBuf[64];
    float mbDown = (float)s_downloadedBytes.load() / (1024.0f * 1024.0f);
    float mbTotal = (float)s_totalBytes.load() / (1024.0f * 1024.0f);
    snprintf(pBuf, sizeof(pBuf), "%.1f MB / %.1f MB (%d%%)", mbDown, mbTotal, (int)(prog * 100.0f));
    std::string pStr = pBuf;
    int pw = Platform::getTextWidth(pStr, bodyFontSize);
    Platform::drawTextColored(renderer, pStr, modalX + (modalW - pw) / 2, curY, bodyFontSize, 255, 255, 255, 255);
    curY += bodyFontSize + (int)(12.0f * scaleFactor);

    std::string wLine = tr(UpdateStr::WAIT_NOTE);
    int ww = Platform::getTextWidth(wLine, subFontSize);
    Platform::drawTextColored(renderer, wLine, modalX + (modalW - ww) / 2, curY, subFontSize, 175, 190, 215, 200);

    s_btnConfirmRect = { 0, 0, 0, 0 };
    s_btnCancelRect = { 0, 0, 0, 0 };

  } else if (s_state == UpdateState::RESTART_READY || s_state == UpdateState::DOWNLOAD_COMPLETE) {
    std::string title = tr(UpdateStr::COMPLETE_TITLE);
    int tw = Platform::getTextWidth(title, titleFontSize);
    Platform::drawTextColored(renderer, title, modalX + (modalW - tw) / 2, modalY + (headerH - titleFontSize) / 2 + 2,
                              titleFontSize, 240, 205, 110, 255);

    std::string s1 = tr(UpdateStr::COMPLETE_LINE1);
    int sw1 = Platform::getTextWidth(s1, bodyFontSize);
    Platform::drawTextColored(renderer, s1, modalX + (modalW - sw1) / 2, curY, bodyFontSize, 255, 255, 255, 255);
    curY += bodyFontSize + (int)(12.0f * scaleFactor);

    std::string s2 = tr(UpdateStr::COMPLETE_LINE2);
    int sw2 = Platform::getTextWidth(s2, bodyFontSize);
    Platform::drawTextColored(renderer, s2, modalX + (modalW - sw2) / 2, curY, bodyFontSize, 200, 220, 245, 220);
    curY += bodyFontSize + (int)(18.0f * scaleFactor);

    int btnW = std::clamp((int)(contentW * 0.70f), 180, 420);
    int by = modalY + modalH - btnH - (int)(18.0f * scaleFactor);
    if (by < curY) by = curY;
    int btnX = modalX + (modalW - btnW) / 2;
    s_btnConfirmRect = { btnX, by, btnW, btnH };
    s_btnCancelRect = { 0, 0, 0, 0 };

    drawModernBtn(tr(UpdateStr::BTN_RESTART), scConfirm, s_btnConfirmRect,
                  { 18, 68, 38, 255 }, { 45, 210, 110, 255 });

  } else if (s_state == UpdateState::CHECK_FAILED || s_state == UpdateState::DOWNLOAD_FAILED) {
    std::string title = tr(UpdateStr::ERROR_TITLE);
    int tw = Platform::getTextWidth(title, titleFontSize);
    Platform::drawTextColored(renderer, title, modalX + (modalW - tw) / 2, modalY + (headerH - titleFontSize) / 2 + 2,
                              titleFontSize, 240, 205, 110, 255);

    std::string s1 = s_statusMessage.empty() ? tr(UpdateStr::DEFAULT_ERROR) : s_statusMessage;
    int sw1 = Platform::getTextWidth(s1, bodyFontSize);
    Platform::drawTextColored(renderer, s1, modalX + (modalW - sw1) / 2, curY, bodyFontSize, 255, 255, 255, 255);
    curY += bodyFontSize + (int)(18.0f * scaleFactor);

    int btnW = std::clamp((int)(contentW * 0.55f), 160, 360);
    int by = modalY + modalH - btnH - (int)(18.0f * scaleFactor);
    if (by < curY) by = curY;
    int btnX = modalX + (modalW - btnW) / 2;
    s_btnCancelRect = { btnX, by, btnW, btnH };
    s_btnConfirmRect = { 0, 0, 0, 0 };

    drawModernBtn(tr(UpdateStr::BTN_CLOSE), scCancel, s_btnCancelRect,
                  { 48, 20, 26, 255 }, { 175, 65, 75, 255 });
  }
}

} // namespace hl

