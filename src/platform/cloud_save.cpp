// cloud_save.cpp — Sistema de Backup e Sincronização Cruzada na Nuvem via Google Drive (appDataFolder)
#include "cloud_save.h"
#include "platform.h"
#include "vm/vm.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <sys/stat.h>

#if defined(_WIN32)
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <winhttp.h>
#elif defined(__SWITCH__)
  #include <switch.h>
  #include <curl/curl.h>
#elif defined(__ANDROID__)
  #include <jni.h>
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
#endif

namespace hl {

// -----------------------------------------------------------------------------
// Variáveis de Estado Internas
// -----------------------------------------------------------------------------
static CloudSaveState s_state = CloudSaveState::NOT_LOGGED_IN;
static DeviceCodeInfo s_deviceCode;
static CloudBackupInfo s_cloudBackup;
static std::string s_statusMessage = "";
static int s_statusMsgId = -1;
static std::mutex s_cloudMutex;
static bool s_modalActive = false;
static std::atomic<bool> s_loginActive{false};
static std::atomic<bool> s_pendingVmReload{false};
static std::string s_accessToken = "";
static std::string s_refreshToken = "";
static uint64_t s_tokenExpiryEpoch = 0;
static VM* s_activeVm = nullptr;
static std::string s_appFolderId = "";

static SDL_Rect s_modalRect = {0, 0, 0, 0};
static SDL_Rect s_btnAction1 = {0, 0, 0, 0};
static SDL_Rect s_btnAction2 = {0, 0, 0, 0};
static SDL_Rect s_btnAction3 = {0, 0, 0, 0};
static SDL_Rect s_btnCancel  = {0, 0, 0, 0};
static SDL_Rect s_btnCloseX  = {0, 0, 0, 0};

// Forward declarations
static void saveTokensToFile();
static void loadTokensFromFile();
static bool ensureValidAccessToken();

std::string getGoogleClientId() {
  static const uint8_t enc[] = {
    108, 104, 108, 99, 104, 98, 111, 110, 98, 107, 111, 107, 119, 107, 104, 98, 59, 42, 50, 54, 59, 52, 46, 110, 104, 111, 54, 61, 109, 47, 57, 108, 55, 106, 43, 53, 44, 48, 47, 108, 52, 63, 60, 41, 61, 116, 59, 42, 42, 41, 116, 61, 53, 53, 61, 54, 63, 47, 41, 63, 40, 57, 53, 52, 46, 63, 52, 46, 116, 57, 53, 55
  };
  std::string s;
  s.reserve(sizeof(enc));
  for (uint8_t b : enc) s.push_back((char)(b ^ 0x5A));
  return s;
}

std::string getGoogleClientSecret() {
  static const uint8_t enc[] = {
    29, 21, 25, 9, 10, 2, 119, 45, 107, 28, 106, 56, 21, 48, 106, 99, 109, 107, 44, 11, 98, 119, 119, 61, 23, 24, 48, 43, 106, 8, 25, 109, 104, 45, 10
  };
  std::string s;
  s.reserve(sizeof(enc));
  for (uint8_t b : enc) s.push_back((char)(b ^ 0x5A));
  return s;
}

// -----------------------------------------------------------------------------
// Passo 10 / Passo 9: Localização Multilíngue (PT, EN, IT, ES)
// -----------------------------------------------------------------------------
enum class CloudStr {
  TITLE,
  SUBTITLE_1,
  SUBTITLE_2,
  CARD_CLOUD,
  CARD_LOCAL,
  DATE_PREFIX,
  NO_CLOUD_BACKUP,
  NO_LOCAL_SAVE,
  STATUS_DISCONNECTED,
  STATUS_CONNECTED,
  STATUS_REQUESTING_CODE,
  STEP1_ACCESS_LINK,
  STEP2_ENTER_CODE,
  WAITING_BROWSER_AUTH,
  STATUS_UPLOADING,
  STATUS_UPLOAD_SUCCESS,
  STATUS_DOWNLOADING,
  STATUS_DOWNLOAD_SUCCESS,
  STATUS_AUTH_EXPIRED,
  STATUS_LOGGED_OUT,
  WARN_RESTORE_TITLE,
  WARN_RESTORE_LINE1,
  WARN_RESTORE_LINE2,
  WARN_RESTORE_LINE3,
  BTN_CONNECT,
  BTN_CLOSE,
  BTN_CANCEL,
  BTN_UPLOAD,
  BTN_RESTORE,
  BTN_CONFIRM,
  BTN_DISCONNECT,
  LEVEL_PREFIX,
  OSD_CONNECTED,
  OSD_DISCONNECTED,
  OSD_UPLOAD_SUCCESS,
  OSD_UPLOAD_ERROR,
  OSD_DOWNLOAD_SUCCESS,
  OSD_DOWNLOAD_ERROR,
  STR_COUNT
};

static const char* s_translations[(size_t)CloudStr::STR_COUNT][4] = {
  /* TITLE */ {
    "GOOGLE DRIVE - CLOUD SAVE",
    "GOOGLE DRIVE - CLOUD SAVE",
    "GOOGLE DRIVE - CLOUD SAVE",
    "GOOGLE DRIVE - CLOUD SAVE"
  },
  /* SUBTITLE_1 */ {
    "Sincronize seus dados de jogo entre",
    "Sync your game saves across",
    "Sincronizza i tuoi salvataggi tra",
    "Sincroniza tus partidas entre"
  },
  /* SUBTITLE_2 */ {
    "PC, Android e Nintendo Switch!",
    "PC, Android and Nintendo Switch!",
    "PC, Android e Nintendo Switch!",
    "PC, Android y Nintendo Switch!"
  },
  /* CARD_CLOUD */ {
    "NUVEM (Google Drive):",
    "CLOUD (Google Drive):",
    "CLOUD (Google Drive):",
    "NUBE (Google Drive):"
  },
  /* CARD_LOCAL */ {
    "LOCAL (%s):",
    "LOCAL (%s):",
    "LOCALE (%s):",
    "LOCAL (%s):"
  },
  /* DATE_PREFIX */ {
    "Data: ",
    "Date: ",
    "Data: ",
    "Fecha: "
  },
  /* NO_CLOUD_BACKUP */ {
    "Nenhum backup encontrado na nuvem.",
    "No cloud backup found.",
    "Nessun backup trovato nel cloud.",
    "No se encontro copia en la nube."
  },
  /* NO_LOCAL_SAVE */ {
    "Nenhum save local",
    "No local save",
    "Nessun salvataggio locale",
    "Ninguna partida local"
  },
  /* STATUS_DISCONNECTED */ {
    "Status: Desconectado",
    "Status: Disconnected",
    "Stato: Disconnesso",
    "Estado: Desconectado"
  },
  /* STATUS_CONNECTED */ {
    "STATUS: CONECTADO AO GOOGLE DRIVE",
    "STATUS: CONNECTED TO GOOGLE DRIVE",
    "STATO: CONNESSO A GOOGLE DRIVE",
    "ESTADO: CONECTADO A GOOGLE DRIVE"
  },
  /* STATUS_REQUESTING_CODE */ {
    "Solicitando codigo de acesso ao Google...",
    "Requesting access code from Google...",
    "Richiesta codice di accesso a Google...",
    "Solicitando codigo de acceso a Google..."
  },
  /* STEP1_ACCESS_LINK */ {
    "1. No celular ou PC, acesse o link:",
    "1. On phone or PC, open the link:",
    "1. Su telefono o PC, apri il link:",
    "1. En celular o PC, entra al enlace:"
  },
  /* STEP2_ENTER_CODE */ {
    "2. Digite este codigo de autorizacao:",
    "2. Enter this authorization code:",
    "2. Inserisci questo codice di autorizzazione:",
    "2. Ingresa este codigo de autorizacion:"
  },
  /* WAITING_BROWSER_AUTH */ {
    "Aguardando confirmacao no navegador...",
    "Waiting for confirmation in browser...",
    "In attesa di conferma nel browser...",
    "Esperando confirmacion en el navegador..."
  },
  /* STATUS_UPLOADING */ {
    "Empacotando e enviando saves para o Google Drive...",
    "Packing and uploading saves to Google Drive...",
    "Invio dei salvataggi su Google Drive...",
    "Empaquetando y enviando partidas a Google Drive..."
  },
  /* STATUS_UPLOAD_SUCCESS */ {
    "Backup enviado para o Google Drive com sucesso!",
    "Backup successfully uploaded to Google Drive!",
    "Backup caricato su Google Drive con successo!",
    "Copia enviada a Google Drive con exito!"
  },
  /* STATUS_DOWNLOADING */ {
    "Baixando save da nuvem e restaurando arquivos...",
    "Downloading cloud save and restoring files...",
    "Download del salvataggio e ripristino file...",
    "Descargando partida de la nube y restaurando..."
  },
  /* STATUS_DOWNLOAD_SUCCESS */ {
    "Save restaurado com sucesso!",
    "Save successfully restored!",
    "Salvataggio ripristinato con successo!",
    "Partida restaurada con exito!"
  },
  /* STATUS_AUTH_EXPIRED */ {
    "Autorizacao cancelada ou expirada.",
    "Authorization cancelled or expired.",
    "Autorizzazione annullata o scaduta.",
    "Autorizacion cancelada o expirada."
  },
  /* STATUS_LOGGED_OUT */ {
    "Conta desconectada.",
    "Account disconnected.",
    "Account disconnesso.",
    "Cuenta desconectada."
  },
  /* WARN_RESTORE_TITLE */ {
    "ATENCAO: RESTAURAR SAVE DA NUVEM",
    "WARNING: RESTORE CLOUD SAVE",
    "ATTENZIONE: RIPRISTINA DA CLOUD",
    "ATENCION: RESTAURAR DE LA NUBE"
  },
  /* WARN_RESTORE_LINE1 */ {
    "A restauracao substituira todos os saves locais",
    "Restoring will overwrite all local game saves",
    "Il ripristino sovrascrivera tutti i salvataggi locali",
    "La restauracion sobrescribira todas las partidas locales"
  },
  /* WARN_RESTORE_LINE2 */ {
    "pelos dados salvos na sua nuvem Google Drive.",
    "with the backup stored on your Google Drive.",
    "con i dati salvati sul tuo Google Drive.",
    "con los datos guardados en tu Google Drive."
  },
  /* WARN_RESTORE_LINE3 */ {
    "Se estiver em partida, o jogo sera reiniciado.",
    "If in-game, the match will be cleanly reset.",
    "Se sei in partita, il gioco sara riavviato.",
    "Si estas en partida, el juego se reiniciara."
  },
  /* BTN_CONNECT */ {
    "CONECTAR",
    "CONNECT",
    "CONNETTI",
    "CONECTAR"
  },
  /* BTN_CLOSE */ {
    "FECHAR",
    "CLOSE",
    "CHIUDI",
    "CERRAR"
  },
  /* BTN_CANCEL */ {
    "CANCELAR",
    "CANCEL",
    "ANNULLA",
    "CANCELAR"
  },
  /* BTN_UPLOAD */ {
    "ENVIAR BACKUP",
    "UPLOAD BACKUP",
    "CARICA BACKUP",
    "SUBIR COPIA"
  },
  /* BTN_RESTORE */ {
    "RESTAURAR",
    "RESTORE",
    "RIPRISTINA",
    "RESTAURAR"
  },
  /* BTN_CONFIRM */ {
    "CONFIRMAR",
    "CONFIRM",
    "CONFERMA",
    "CONFIRMAR"
  },
  /* BTN_DISCONNECT */ {
    "DESCONECTAR",
    "DISCONNECT",
    "DISCONNETTI",
    "DESCONECTAR"
  },
  /* LEVEL_PREFIX */ {
    " Nv.",
    " Lv.",
    " Liv.",
    " Nv."
  },
  /* OSD_CONNECTED */ {
    "Google Drive conectado!",
    "Google Drive connected!",
    "Google Drive connesso!",
    "¡Google Drive conectado!"
  },
  /* OSD_DISCONNECTED */ {
    "Conta Google Drive desconectada.",
    "Google Drive account disconnected.",
    "Account Google Drive disconnesso.",
    "Cuenta Google Drive desconectada."
  },
  /* OSD_UPLOAD_SUCCESS */ {
    "Backup na nuvem realizado com sucesso!",
    "Cloud backup completed successfully!",
    "Backup su cloud completato con successo!",
    "¡Copia en la nube completada con exito!"
  },
  /* OSD_UPLOAD_ERROR */ {
    "Erro ao enviar backup para a nuvem.",
    "Error uploading backup to cloud.",
    "Errore caricamento backup su cloud.",
    "Error al subir copia a la nube."
  },
  /* OSD_DOWNLOAD_SUCCESS */ {
    "Save restaurado da nuvem!",
    "Save restored from cloud!",
    "Salvataggio ripristinato dal cloud!",
    "¡Partida restaurada de la nube!"
  },
  /* OSD_DOWNLOAD_ERROR */ {
    "Erro ao baixar save da nuvem.",
    "Error downloading save from cloud.",
    "Errore download save da cloud.",
    "Error al descargar partida de la nube."
  }
};

static int getLangIndex() {
  std::string lang = Platform::getCurrentLanguage();
  if (lang == "en") return 1;
  if (lang == "it") return 2;
  if (lang == "es") return 3;
  return 0; // "pt"
}

static const char* tr(CloudStr id) {
  int l = getLangIndex();
  return s_translations[(size_t)id][l];
}

// -----------------------------------------------------------------------------
// Utilitários de String e JSON
// -----------------------------------------------------------------------------
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

static std::string extractFirstParentId(const std::string& json) {
  size_t p = json.find("\"parents\"");
  if (p == std::string::npos) return "";
  size_t ob = json.find('[', p);
  if (ob == std::string::npos) return "";
  size_t cb = json.find(']', ob);
  if (cb == std::string::npos) return "";
  size_t q1 = json.find('"', ob);
  if (q1 == std::string::npos || q1 > cb) return "";
  size_t q2 = json.find('"', q1 + 1);
  if (q2 == std::string::npos || q2 > cb) return "";
  return json.substr(q1 + 1, q2 - q1 - 1);
}

static int extractJsonInt(const std::string& json, const std::string& key, int defaultVal = 0) {
  size_t k = json.find("\"" + key + "\"");
  if (k == std::string::npos) return defaultVal;
  size_t colon = json.find(':', k);
  if (colon == std::string::npos) return defaultVal;
  size_t start = json.find_first_of("0123456789-", colon);
  if (start == std::string::npos) return defaultVal;
  return std::atoi(json.c_str() + start);
}

static std::string jsonEscape(const std::string& s) {
  std::ostringstream o;
  for (char c : s) {
    if (c == '"') o << "\\\"";
    else if (c == '\\') o << "\\\\";
    else if (c == '\b') o << "\\b";
    else if (c == '\f') o << "\\f";
    else if (c == '\n') o << "\\n";
    else if (c == '\r') o << "\\r";
    else if (c == '\t') o << "\\t";
    else if ((unsigned char)c < 32) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
      o << buf;
    } else {
      o << c;
    }
  }
  return o.str();
}

static std::string urlEncode(const std::string& s) {
  std::ostringstream escaped;
  escaped.fill('0');
  escaped << std::hex;
  for (char c : s) {
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      escaped << c;
    } else {
      escaped << '%' << std::uppercase << std::setw(2) << int((unsigned char)c) << std::nouppercase;
    }
  }
  return escaped.str();
}

// -----------------------------------------------------------------------------
// Base64 Encode / Decode
// -----------------------------------------------------------------------------
static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::vector<uint8_t>& data) {
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : data) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(B64_CHARS[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(B64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

static std::vector<uint8_t> base64Decode(const std::string& in) {
  std::vector<uint8_t> out;
  std::vector<int> T(256, -1);
  for (int i = 0; i < 64; i++) T[(unsigned char)B64_CHARS[i]] = i;

  int val = 0, valb = -8;
  for (char c : in) {
    if (c == '=' || (unsigned char)c > 255 || T[(unsigned char)c] == -1) continue;
    val = (val << 6) + T[(unsigned char)c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(uint8_t((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

// -----------------------------------------------------------------------------
// Cliente HTTP Multiplataforma (GET, POST, PATCH)
// -----------------------------------------------------------------------------
struct HttpResponse {
  int statusCode = 0;
  std::string body;
};

#if defined(_WIN32)
static HttpResponse winHttpRequest(const std::string& method, const std::string& url,
                                  const std::vector<std::string>& headers, const std::string& bodyData) {
  HttpResponse resp;
  std::wstring wurl(url.begin(), url.end());
  URL_COMPONENTS urlComp;
  ZeroMemory(&urlComp, sizeof(urlComp));
  urlComp.dwStructSize = sizeof(urlComp);
  urlComp.dwSchemeLength = (DWORD)-1;
  urlComp.dwHostNameLength = (DWORD)-1;
  urlComp.dwUrlPathLength = (DWORD)-1;
  urlComp.dwExtraInfoLength = (DWORD)-1;

  if (!WinHttpCrackUrl(wurl.c_str(), (DWORD)wurl.length(), 0, &urlComp)) return resp;

  HINTERNET hSession = WinHttpOpen(L"Heroes-Lore-Cloud/1.0",
                                   WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                   WINHTTP_NO_PROXY_NAME,
                                   WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return resp;

  std::wstring host(urlComp.lpszHostName, urlComp.dwHostNameLength);
  HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), urlComp.nPort, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return resp; }

  std::wstring path(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
  if (urlComp.dwExtraInfoLength > 0) {
    path.append(urlComp.lpszExtraInfo, urlComp.dwExtraInfoLength);
  }

  std::wstring wMethod(method.begin(), method.end());
  DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
  HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), path.c_str(),
                                         NULL, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
  if (!hRequest) {
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return resp;
  }

  // Adiciona cabeçalhos
  for (const auto& h : headers) {
    std::wstring wh(h.begin(), h.end());
    WinHttpAddRequestHeaders(hRequest, wh.c_str(), (ULONG)-1L, WINHTTP_ADDREQ_FLAG_ADD);
  }

  // Timeouts: 10s connect, 15s send, 20s receive
  WinHttpSetTimeouts(hRequest, 10000, 10000, 15000, 20000);

  DWORD bodyLen = (DWORD)bodyData.size();
  BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                bodyLen > 0 ? (LPVOID)bodyData.data() : WINHTTP_NO_REQUEST_DATA,
                                bodyLen, bodyLen, 0);

  if (sent && WinHttpReceiveResponse(hRequest, NULL)) {
    DWORD dwStatusCode = 0;
    DWORD dwSize = sizeof(dwStatusCode);
    if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX)) {
      resp.statusCode = (int)dwStatusCode;
    }

    std::vector<char> buffer(4096);
    DWORD dwDownloaded = 0;
    while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
      if (dwSize > buffer.size()) buffer.resize(dwSize);
      if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded) && dwDownloaded > 0) {
        resp.body.append(buffer.data(), dwDownloaded);
      } else {
        break;
      }
    }
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return resp;
}

#elif defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))

static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  std::string* str = static_cast<std::string*>(userp);
  str->append(static_cast<char*>(contents), total);
  return total;
}

static HttpResponse curlHttpRequest(const std::string& method, const std::string& url,
                                   const std::vector<std::string>& headers, const std::string& bodyData) {
  HttpResponse resp;
  CURL* curl = curl_easy_init();
  if (!curl) return resp;

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Heroes-Lore-Cloud/1.0");
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);

  struct curl_slist* hList = nullptr;
  for (const auto& h : headers) {
    hList = curl_slist_append(hList, h.c_str());
  }
  if (hList) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hList);

  if (!bodyData.empty()) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)bodyData.size());
  }

  CURLcode res = curl_easy_perform(curl);
  if (res == CURLE_OK) {
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    resp.statusCode = (int)code;
  }

  if (hList) curl_slist_free_all(hList);
  curl_easy_cleanup(curl);
  return resp;
}

#elif defined(__ANDROID__)

static jclass s_cloudActivityClass = nullptr;
static jmethodID s_midHttpExecute = nullptr;

static void androidCloudInitJni() {
  if (s_cloudActivityClass && s_midHttpExecute) return;
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return;

  jobject activityObj = (jobject)SDL_AndroidGetActivity();
  if (activityObj && !s_cloudActivityClass) {
    jclass localClass = env->GetObjectClass(activityObj);
    if (localClass) {
      s_cloudActivityClass = (jclass)env->NewGlobalRef(localClass);
      env->DeleteLocalRef(localClass);
    }
  }

  if (!s_cloudActivityClass) {
    jclass localClass = env->FindClass("org/libsdl/app/SDLActivity");
    if (localClass) {
      s_cloudActivityClass = (jclass)env->NewGlobalRef(localClass);
      env->DeleteLocalRef(localClass);
    }
  }

  if (s_cloudActivityClass && !s_midHttpExecute) {
    s_midHttpExecute = env->GetStaticMethodID(s_cloudActivityClass, "httpExecute",
                                             "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");
  }

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }
}

static HttpResponse androidHttpRequest(const std::string& method, const std::string& url,
                                      const std::vector<std::string>& headers, const std::string& bodyData) {
  HttpResponse resp;
  JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
  if (!env) return resp;

  if (!s_cloudActivityClass || !s_midHttpExecute) {
    androidCloudInitJni();
  }
  if (!s_cloudActivityClass || !s_midHttpExecute) {
    if (env->ExceptionCheck()) env->ExceptionClear();
    return resp;
  }

  std::string headersConcat;
  for (const auto& h : headers) {
    headersConcat += h + "\n";
  }

  jstring jUrl = env->NewStringUTF(url.c_str());
  jstring jMethod = env->NewStringUTF(method.c_str());
  jstring jHeaders = env->NewStringUTF(headersConcat.c_str());
  jstring jBody = env->NewStringUTF(bodyData.c_str());

  if (!jUrl || !jMethod || !jHeaders || !jBody) {
    if (jUrl) env->DeleteLocalRef(jUrl);
    if (jMethod) env->DeleteLocalRef(jMethod);
    if (jHeaders) env->DeleteLocalRef(jHeaders);
    if (jBody) env->DeleteLocalRef(jBody);
    if (env->ExceptionCheck()) env->ExceptionClear();
    return resp;
  }

  jstring jResult = (jstring)env->CallStaticObjectMethod(s_cloudActivityClass, s_midHttpExecute, jUrl, jMethod, jHeaders, jBody);

  env->DeleteLocalRef(jUrl);
  env->DeleteLocalRef(jMethod);
  env->DeleteLocalRef(jHeaders);
  env->DeleteLocalRef(jBody);

  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    return resp;
  }

  if (jResult) {
    const char* utf = env->GetStringUTFChars(jResult, nullptr);
    if (utf) {
      std::string fullResult(utf);
      env->ReleaseStringUTFChars(jResult, utf);
      env->DeleteLocalRef(jResult);

      size_t nl = fullResult.find('\n');
      if (nl != std::string::npos) {
        resp.statusCode = std::atoi(fullResult.substr(0, nl).c_str());
        resp.body = fullResult.substr(nl + 1);
      } else {
        resp.statusCode = std::atoi(fullResult.c_str());
      }
    }
  }

  return resp;
}
#endif

static HttpResponse httpExecute(const std::string& method, const std::string& url,
                               const std::vector<std::string>& headers, const std::string& bodyData) {
#if defined(_WIN32)
  return winHttpRequest(method, url, headers, bodyData);
#elif defined(__SWITCH__) || (defined(__linux__) && !defined(__ANDROID__))
  return curlHttpRequest(method, url, headers, bodyData);
#elif defined(__ANDROID__)
  return androidHttpRequest(method, url, headers, bodyData);
#else
  return HttpResponse{};
#endif
}

// -----------------------------------------------------------------------------
// Persistência de Tokens Locais (cloud_auth.json)
// -----------------------------------------------------------------------------
static std::string getAuthFilePath() {
  return Platform::getStorageDir() + "/cloud_auth.json";
}

static void saveTokensToFile() {
  std::ofstream f(getAuthFilePath(), std::ios::trunc);
  if (!f) return;
  f << "{\n";
  f << "  \"refresh_token\": \"" << jsonEscape(s_refreshToken) << "\",\n";
  f << "  \"access_token\": \"" << jsonEscape(s_accessToken) << "\",\n";
  f << "  \"expires_at\": " << s_tokenExpiryEpoch << "\n";
  f << "}\n";
}

static void loadTokensFromFile() {
  std::ifstream f(getAuthFilePath());
  if (!f) return;
  std::stringstream ss;
  ss << f.rdbuf();
  std::string json = ss.str();
  s_refreshToken = extractJsonString(json, "refresh_token");
  s_accessToken = extractJsonString(json, "access_token");
  s_tokenExpiryEpoch = (uint64_t)extractJsonInt(json, "expires_at", 0);
}

static bool ensureValidAccessToken() {
  if (s_refreshToken.empty()) return false;

  auto now = (uint64_t)std::time(nullptr);
  if (!s_accessToken.empty() && s_tokenExpiryEpoch > now + 60) {
    return true; // Token ainda é válido por mais de 1 minuto
  }

  // Renova o access_token usando o refresh_token
  std::string url = "https://oauth2.googleapis.com/token";
  std::vector<std::string> headers = {
    "Content-Type: application/x-www-form-urlencoded"
  };
  std::string body = "client_id=" + urlEncode(getGoogleClientId()) +
                     "&client_secret=" + urlEncode(getGoogleClientSecret()) +
                     "&refresh_token=" + urlEncode(s_refreshToken) +
                     "&grant_type=refresh_token";

  HttpResponse resp = httpExecute("POST", url, headers, body);
  if (resp.statusCode == 200) {
    std::string newTok = extractJsonString(resp.body, "access_token");
    int expIn = extractJsonInt(resp.body, "expires_in", 3600);
    if (!newTok.empty()) {
      s_accessToken = newTok;
      s_tokenExpiryEpoch = (uint64_t)std::time(nullptr) + expIn;
      saveTokensToFile();
      return true;
    }
  }
  return false;
}

// -----------------------------------------------------------------------------
// Resumo dos Saves Locais e Conversão de Datas (Ronin, Reah, Aramor)
// -----------------------------------------------------------------------------
static inline time_t portableTimegm(int year, int mon, int day, int hour, int min, int sec) {
  static const int daysBeforeMonth[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };
  int y = year - (mon <= 2 ? 1 : 0);
  int leaps = (y / 4) - (y / 100) + (y / 400) - (1970 / 4 - 1970 / 100 + 1970 / 400);
  int days = (year - 1970) * 365 + leaps + daysBeforeMonth[mon - 1] + (day - 1);
  if (mon > 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
    days += 1;
  }
  return (time_t)days * 86400 + (time_t)hour * 3600 + (time_t)min * 60 + (time_t)sec;
}

static std::string formatUtcIsoToLocalDate(const std::string& iso) {
  if (iso.size() < 16) return iso;
  int y = 0, m = 0, d = 0, hr = 0, mn = 0, sec = 0;
  if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hr, &mn, &sec) >= 5) {
    time_t utcTime = portableTimegm(y, m, d, hr, mn, sec);
    if (utcTime > 0) {
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&utcTime));
      return std::string(buf);
    }
  }
  return iso.substr(0, 10) + " " + iso.substr(11, 5);
}

static void sanitizeCloudSummary(std::string& s) {
  auto replaceAll = [](std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.length(), to);
      pos += to.length();
    }
  };
  replaceAll(s, "Karis", "Ronin");
  replaceAll(s, "Shion", "Reah");
  replaceAll(s, "Luiel", "Aramor");
}

static int decodeHeroLevel(const std::vector<uint8_t>& rec) {
  if (rec.size() < 4) return 0;
  int s2 = (rec[0] << 8) | rec[1];
  if (s2 <= 2 || s2 > (int)rec.size() - 2) return 0;

  static const uint8_t key[] = {5, 11, 8, 81, 3, 20};
  int n4 = 0;
  uint8_t level = 0;

  for (int n2 = 0; n2 < s2 - 1; ++n2) {
    if (++n4 == 6) n4 = 0;
    uint8_t decByte = rec[2 + n2] ^ key[n4];
    if (n2 == 1) {
      level = decByte; // object[1] em n.java é o nível autêntico do herói
      break;
    }
  }

  if (level >= 1 && level <= 99) return (int)level;
  return 0;
}

static std::string getLocalSavesDate() {
  std::string rmsBase = Platform::getRmsDir(s_activeVm);
  // Apenas arquivos reais de progresso de heróis determinam a data/hora do save
  const char* slotFiles[] = { "_k.rms", "_s.rms", "_w.rms" };
  time_t newestTime = 0;

  for (const char* fn : slotFiles) {
    std::string path = (rmsBase.empty() || rmsBase.back() == '/' ? rmsBase : rmsBase + "/") + fn;
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
      if (st.st_mtime > newestTime) {
        newestTime = st.st_mtime;
      }
    }
  }

  if (newestTime == 0) return "";
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&newestTime));
  return std::string(buf);
}

static std::string getLocalSavesSummary() {
  std::string rmsBase = Platform::getRmsDir(s_activeVm);
  std::string summary = "";

  struct SlotCheck {
    const char* filename;
    const char* defaultHero;
  };
  SlotCheck slots[] = {
    {"_k.rms", "Ronin"},
    {"_s.rms", "Reah"},
    {"_w.rms", "Aramor"}
  };

  int foundCount = 0;
  for (const auto& sc : slots) {
    std::string path = (rmsBase.empty() || rmsBase.back() == '/' ? rmsBase : rmsBase + "/") + sc.filename;
    std::ifstream f(path, std::ios::binary);
    if (!f) continue;

    foundCount++;
    if (!summary.empty()) summary += ", ";
    summary += sc.defaultHero;

    // Tenta ler o primeiro record para obter o nível usando bq.b
    uint32_t count = 0;
    if (f.read((char*)&count, 4) && count > 0) {
      uint32_t sz = 0;
      if (f.read((char*)&sz, 4) && sz >= 4) {
        std::vector<uint8_t> rec(sz);
        f.read((char*)rec.data(), sz);
        int lvl = decodeHeroLevel(rec);
        if (lvl > 0) {
          summary += std::string(tr(CloudStr::LEVEL_PREFIX)) + std::to_string(lvl);
        }
      }
    }
  }

  if (foundCount == 0) return tr(CloudStr::NO_LOCAL_SAVE);
  return summary;
}

// -----------------------------------------------------------------------------
// Nome da Plataforma Atual
// -----------------------------------------------------------------------------
static std::string getPlatformName() {
#if defined(__SWITCH__)
  return "Nintendo Switch";
#elif defined(__ANDROID__)
  return "Android";
#elif defined(_WIN32)
  return "Windows";
#elif defined(__linux__)
  return "Linux";
#else
  return "Outro";
#endif
}

// -----------------------------------------------------------------------------
// Despachador de Threads em Segundo Plano Seguro para SDL2/Android JNI
// -----------------------------------------------------------------------------
template<typename F>
static void runAsync(F&& f) {
  auto* fnPtr = new std::decay_t<F>(std::forward<F>(f));
  SDL_Thread* th = SDL_CreateThread([](void* data) -> int {
    auto* fn = static_cast<std::decay_t<F>*>(data);
    (*fn)();
    delete fn;
    return 0;
  }, "HL_CloudThread", fnPtr);
  if (th) {
    SDL_DetachThread(th);
  } else {
    std::thread([fnPtr]() {
      (*fnPtr)();
      delete fnPtr;
    }).detach();
  }
}

// -----------------------------------------------------------------------------
// Inicialização e Shutdown
// -----------------------------------------------------------------------------
void CloudSave::init() {
#if defined(__ANDROID__)
  androidCloudInitJni();
#endif
  loadTokensFromFile();
  if (!s_refreshToken.empty()) {
    s_state = CloudSaveState::LOGGED_IN;
    // Em thread separada, checa se há backup recente no drive
    runAsync([]() {
      queryCloudBackupAsync();
    });
  } else {
    s_state = CloudSaveState::NOT_LOGGED_IN;
  }
}

void CloudSave::shutdown() {
  s_loginActive = false;
  s_modalActive = false;
}

void CloudSave::update(VM* vm) {
  if (vm) s_activeVm = vm;
  if (s_pendingVmReload.exchange(false)) {
    if (s_activeVm) {
      s_activeVm->gilLock();
      try {
        ClassInfo* nClass = s_activeVm->findClass("n");
        FieldInfo* fAoA = nClass ? s_activeVm->findField(nClass, "a:Lao;") : nullptr;
        bool inGame = false;

        if (fAoA && fAoA->isStatic && fAoA->index >= 0 && fAoA->index < (int)nClass->statics.size()) {
          inGame = (nClass->statics[fAoA->index].o != nullptr);
        }

        if (inGame) {
          ClassInfo* buClass = s_activeVm->findClass("bu");
          Method* mBuD = buClass ? s_activeVm->findMethod(buClass, "d:()V") : nullptr;
          if (mBuD) {
            Value ret[2];
            s_activeVm->invoke(mBuD, nullptr, ret);
          }
        } else {
          Method* mNp = nClass ? s_activeVm->findMethod(nClass, "p:()V") : nullptr;
          if (mNp) {
            Value ret[2];
            s_activeVm->invoke(mNp, nullptr, ret);
          }
        }
      } catch (...) {}
      s_activeVm->gilUnlock();
    }
  }
}

bool CloudSave::isLoggedIn() {
  return !s_refreshToken.empty();
}

CloudSaveState CloudSave::getState() {
  return s_state;
}

std::string CloudSave::getStatusMessage() {
  return s_statusMessage;
}

const DeviceCodeInfo& CloudSave::getDeviceCodeInfo() {
  return s_deviceCode;
}

const CloudBackupInfo& CloudSave::getCloudBackupInfo() {
  return s_cloudBackup;
}

bool CloudSave::isModalActive() {
  return s_modalActive;
}

void CloudSave::openModal(VM* vm) {
#if defined(__ANDROID__)
  androidCloudInitJni();
#endif
  if (vm) s_activeVm = vm;
  s_modalActive = true;
  s_statusMessage.clear();
  s_statusMsgId = -1;
  if (s_state == CloudSaveState::SUCCESS_NOTIFICATION || s_state == CloudSaveState::ERROR_NOTIFICATION) {
    s_state = isLoggedIn() ? CloudSaveState::LOGGED_IN : CloudSaveState::NOT_LOGGED_IN;
  }
  if (isLoggedIn() && !s_cloudBackup.exists) {
    queryCloudBackupAsync();
  }
}

void CloudSave::closeModal() {
  s_modalActive = false;
  s_statusMessage.clear();
  s_statusMsgId = -1;
  if (s_state == CloudSaveState::WAITING_USER_AUTH || s_state == CloudSaveState::REQUESTING_CODE) {
    cancelLogin();
  } else if (s_state == CloudSaveState::SUCCESS_NOTIFICATION || s_state == CloudSaveState::ERROR_NOTIFICATION) {
    s_state = isLoggedIn() ? CloudSaveState::LOGGED_IN : CloudSaveState::NOT_LOGGED_IN;
  }
}

// -----------------------------------------------------------------------------
// Autenticação OAuth 2.0 Device Flow
// -----------------------------------------------------------------------------
void CloudSave::startLogin() {
  if (s_loginActive) return;
  s_loginActive = true;
  s_state = CloudSaveState::REQUESTING_CODE;
  s_statusMessage = "Solicitando codigo de acesso ao Google...";

  runAsync([]() {
    std::string url = "https://oauth2.googleapis.com/device/code";
    std::vector<std::string> headers = {
      "Content-Type: application/x-www-form-urlencoded"
    };
    std::string body = "client_id=" + urlEncode(getGoogleClientId()) +
                       "&scope=" + urlEncode("https://www.googleapis.com/auth/drive.file");

    HttpResponse resp = httpExecute("POST", url, headers, body);
    if (resp.statusCode != 200) {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_state = CloudSaveState::ERROR_NOTIFICATION;
      s_statusMessage = "Erro ao conectar com Google Cloud (" + std::to_string(resp.statusCode) + ")";
      s_loginActive = false;
      return;
    }

    {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_deviceCode.deviceCode = extractJsonString(resp.body, "device_code");
      s_deviceCode.userCode = extractJsonString(resp.body, "user_code");
      s_deviceCode.verificationUrl = extractJsonString(resp.body, "verification_url");
      s_deviceCode.interval = extractJsonInt(resp.body, "interval", 5);
      s_deviceCode.expiresIn = extractJsonInt(resp.body, "expires_in", 1800);

      if (s_deviceCode.verificationUrl.empty()) {
        s_deviceCode.verificationUrl = "https://www.google.com/device";
      }

      s_state = CloudSaveState::WAITING_USER_AUTH;
      s_statusMessage = "Aguardando confirmacao em " + s_deviceCode.verificationUrl;
    }

    // Loop de polling pelo token
    int pollInterval = std::max(s_deviceCode.interval, 5);
    auto expireTime = std::chrono::steady_clock::now() + std::chrono::seconds(s_deviceCode.expiresIn);

    while (s_loginActive && std::chrono::steady_clock::now() < expireTime) {
      std::this_thread::sleep_for(std::chrono::seconds(pollInterval));
      if (!s_loginActive) break;

      std::string tUrl = "https://oauth2.googleapis.com/token";
      std::vector<std::string> tHeaders = {
        "Content-Type: application/x-www-form-urlencoded"
      };
      std::string tBody = "client_id=" + urlEncode(getGoogleClientId()) +
                          "&client_secret=" + urlEncode(getGoogleClientSecret()) +
                          "&device_code=" + urlEncode(s_deviceCode.deviceCode) +
                          "&grant_type=urn:ietf:params:oauth:grant-type:device_code";

      HttpResponse tResp = httpExecute("POST", tUrl, tHeaders, tBody);
      if (tResp.statusCode == 200) {
        std::string accTok = extractJsonString(tResp.body, "access_token");
        std::string refTok = extractJsonString(tResp.body, "refresh_token");
        int expIn = extractJsonInt(tResp.body, "expires_in", 3600);

        if (!accTok.empty() && !refTok.empty()) {
          std::lock_guard<std::mutex> lock(s_cloudMutex);
          s_accessToken = accTok;
          s_refreshToken = refTok;
          s_tokenExpiryEpoch = (uint64_t)std::time(nullptr) + expIn;
          saveTokensToFile();

          s_state = CloudSaveState::LOGGED_IN;
          s_statusMessage = "Conectado ao Google Drive com sucesso!";
          s_loginActive = false;
          Platform::showOsdMessage("Google Drive conectado!");
          queryCloudBackupAsync();
          return;
        }
      } else {
        std::string err = extractJsonString(tResp.body, "error");
        if (err == "authorization_pending") {
          // Usuário ainda está digitando no celular/PC
          continue;
        } else if (err == "slow_down") {
          pollInterval += 2;
          continue;
        } else {
          // Erro fatal / código expirado
          std::lock_guard<std::mutex> lock(s_cloudMutex);
          s_state = CloudSaveState::ERROR_NOTIFICATION;
          s_statusMessage = "Autorizacao cancelada ou expirada.";
          s_loginActive = false;
          return;
        }
      }
    }

    std::lock_guard<std::mutex> lock(s_cloudMutex);
    s_state = CloudSaveState::NOT_LOGGED_IN;
    s_loginActive = false;
  });
}

void CloudSave::cancelLogin() {
  s_loginActive = false;
  s_state = CloudSaveState::NOT_LOGGED_IN;
  s_statusMessage = "";
}

void CloudSave::logout() {
  s_refreshToken = "";
  s_accessToken = "";
  s_tokenExpiryEpoch = 0;
  s_appFolderId = "";
  s_cloudBackup = CloudBackupInfo{};
  std::error_code ec;
  std::filesystem::remove(getAuthFilePath(), ec);
  s_state = CloudSaveState::NOT_LOGGED_IN;
  s_statusMessage = "Conta desconectada.";
  Platform::showOsdMessage("Conta Google Drive desconectada.");
}

// -----------------------------------------------------------------------------
// Pasta dedicada "Heroes Lore - Wind of Soltia" no Google Drive
// -----------------------------------------------------------------------------
static std::string getOrCreateAppFolder() {
  if (!s_appFolderId.empty()) {
    return s_appFolderId;
  }
  if (!ensureValidAccessToken()) return "";

  // 1. Busca se a pasta já existe
  std::string q = "name='Heroes Lore - Wind of Soltia' and mimeType='application/vnd.google-apps.folder' and trashed=false";
  std::string url = "https://www.googleapis.com/drive/v3/files?fields=" + urlEncode("files(id,name)") +
                    "&q=" + urlEncode(q);
  std::vector<std::string> headers = {
    "Authorization: Bearer " + s_accessToken
  };

  HttpResponse resp = httpExecute("GET", url, headers, "");
  if (resp.statusCode == 200) {
    std::string fid = extractJsonString(resp.body, "id");
    if (!fid.empty()) {
      s_appFolderId = fid;
      return s_appFolderId;
    }
  }

  // 2. Se não existir, cria a pasta dedicada
  std::string createUrl = "https://www.googleapis.com/drive/v3/files";
  std::string createBody = "{\"name\": \"Heroes Lore - Wind of Soltia\", "
                           "\"mimeType\": \"application/vnd.google-apps.folder\"}";
  std::vector<std::string> createHeaders = {
    "Authorization: Bearer " + s_accessToken,
    "Content-Type: application/json; charset=UTF-8"
  };

  HttpResponse cResp = httpExecute("POST", createUrl, createHeaders, createBody);
  if (cResp.statusCode == 200) {
    std::string fid = extractJsonString(cResp.body, "id");
    if (!fid.empty()) {
      s_appFolderId = fid;
      printf("[CloudSave] Pasta criada no Google Drive: 'Heroes Lore - Wind of Soltia' (id=%s)\n", fid.c_str());
      return s_appFolderId;
    }
  }

  return "";
}

// -----------------------------------------------------------------------------
// Consulta de Backup no Google Drive
// -----------------------------------------------------------------------------
void CloudSave::queryCloudBackupAsync() {
  if (!isLoggedIn()) return;

  runAsync([]() {
    if (!ensureValidAccessToken()) return;

    // Garante que a pasta dedicada do jogo exista
    std::string folderId = getOrCreateAppFolder();

    // Busca arquivo heroes_lore_save.json
    std::string url = "https://www.googleapis.com/drive/v3/files?orderBy=modifiedTime%20desc"
                      "&fields=" + urlEncode("files(id,name,modifiedTime,size,description,parents)") +
                      "&q=" + urlEncode("name='heroes_lore_save.json' and trashed=false");
    std::vector<std::string> headers = {
      "Authorization: Bearer " + s_accessToken
    };

    HttpResponse resp = httpExecute("GET", url, headers, "");
    if (resp.statusCode == 200) {
      std::string id = extractJsonString(resp.body, "id");
      if (!id.empty()) {
        std::lock_guard<std::mutex> lock(s_cloudMutex);
        s_cloudBackup.exists = true;
        s_cloudBackup.fileId = id;
        std::string rawIso = extractJsonString(resp.body, "modifiedTime");
        s_cloudBackup.modifiedTime = formatUtcIsoToLocalDate(rawIso);
        s_cloudBackup.summary = extractJsonString(resp.body, "description");
        sanitizeCloudSummary(s_cloudBackup.summary);
        s_cloudBackup.fileSize = (size_t)extractJsonInt(resp.body, "size", 0);

        // Se o arquivo estiver solto fora da pasta dedicada, migra-o automaticamente para dentro dela!
        if (!folderId.empty()) {
          std::string parentId = extractFirstParentId(resp.body);
          if (parentId != folderId) {
            std::string moveUrl = "https://www.googleapis.com/drive/v3/files/" + id +
                                  "?addParents=" + folderId +
                                  (parentId.empty() ? "" : "&removeParents=" + parentId);
            std::vector<std::string> moveHeaders = {
              "Authorization: Bearer " + s_accessToken
            };
            HttpResponse moveResp = httpExecute("PATCH", moveUrl, moveHeaders, "");
            if (moveResp.statusCode == 200) {
              printf("[CloudSave] Arquivo 'heroes_lore_save.json' migrado com sucesso para a pasta 'Heroes Lore - Wind of Soltia'!\n");
            }
          }
        }
      } else {
        std::lock_guard<std::mutex> lock(s_cloudMutex);
        s_cloudBackup.exists = false;
        s_cloudBackup.fileId = "";
      }
    }
  });
}

// -----------------------------------------------------------------------------
// Upload / Fazer Backup
// -----------------------------------------------------------------------------
void CloudSave::uploadBackupAsync() {
  if (!isLoggedIn()) return;
  s_state = CloudSaveState::UPLOADING;
  s_statusMessage = "Empacotando e enviando saves para o Google Drive...";

  runAsync([]() {
    if (!ensureValidAccessToken()) {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_state = CloudSaveState::ERROR_NOTIFICATION;
      s_statusMessage = "Falha de autenticacao com Google Drive.";
      return;
    }

    std::string folderId = getOrCreateAppFolder();

    std::string rmsBase = Platform::getRmsDir(s_activeVm);
    const char* filesToPack[] = {
      "_k.rms", "_s.rms", "_w.rms", "_o.rms", "_c.rms"
    };

    std::ostringstream jsonPkg;
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    char dateBuf[32];
    std::strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d %H:%M", std::localtime(&nowTime));

    std::string localSummary = getLocalSavesSummary();
    std::string plat = getPlatformName();

    jsonPkg << "{\n";
    jsonPkg << "  \"app\": \"Heroes Lore: Wind of Soltia\",\n";
    jsonPkg << "  \"version\": 1,\n";
    jsonPkg << "  \"platform\": \"" << jsonEscape(plat) << "\",\n";
    jsonPkg << "  \"date\": \"" << jsonEscape(dateBuf) << "\",\n";
    jsonPkg << "  \"summary\": \"" << jsonEscape(localSummary) << "\",\n";
    jsonPkg << "  \"files\": {\n";

    bool firstFile = true;
    for (const char* fn : filesToPack) {
      std::string path = (rmsBase.empty() || rmsBase.back() == '/' ? rmsBase : rmsBase + "/") + fn;
      std::ifstream f(path, std::ios::binary);
      if (!f) continue;

      std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      if (data.empty()) continue;

      if (!firstFile) jsonPkg << ",\n";
      firstFile = false;
      jsonPkg << "    \"" << fn << "\": \"" << base64Encode(data) << "\"";
    }
    jsonPkg << "\n  }\n}\n";

    std::string pkgStr = jsonPkg.str();

    // Envia para o Google Drive (PATCH se já existe arquivo, ou POST multipart se novo)
    HttpResponse resp;
    if (s_cloudBackup.exists && !s_cloudBackup.fileId.empty()) {
      std::string patchUrl = "https://www.googleapis.com/upload/drive/v3/files/" +
                             s_cloudBackup.fileId + "?uploadType=media";
      std::vector<std::string> headers = {
        "Authorization: Bearer " + s_accessToken,
        "Content-Type: application/json; charset=UTF-8"
      };
      resp = httpExecute("PATCH", patchUrl, headers, pkgStr);

      // Atualiza também a descrição (resumo dos slots) e garante o pai na pasta dedicada
      std::string metaUrl = "https://www.googleapis.com/drive/v3/files/" + s_cloudBackup.fileId;
      if (!folderId.empty()) {
        metaUrl += "?addParents=" + folderId;
      }
      std::string metaBody = "{\"description\": \"" + jsonEscape(plat + " | " + localSummary) + "\"}";
      std::vector<std::string> metaHeaders = {
        "Authorization: Bearer " + s_accessToken,
        "Content-Type: application/json; charset=UTF-8"
      };
      httpExecute("PATCH", metaUrl, metaHeaders, metaBody);
    } else {
      std::string postUrl = "https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart";
      std::string boundary = "HL_CLOUD_BOUNDARY_1289";
      std::string metaJson;
      if (!folderId.empty()) {
        metaJson = "{\"name\": \"heroes_lore_save.json\", "
                   "\"description\": \"" + jsonEscape(plat + " | " + localSummary) + "\", "
                   "\"parents\": [\"" + folderId + "\"]}";
      } else {
        metaJson = "{\"name\": \"heroes_lore_save.json\", "
                   "\"description\": \"" + jsonEscape(plat + " | " + localSummary) + "\"}";
      }

      std::ostringstream multipart;
      multipart << "--" << boundary << "\r\n";
      multipart << "Content-Type: application/json; charset=UTF-8\r\n\r\n";
      multipart << metaJson << "\r\n";
      multipart << "--" << boundary << "\r\n";
      multipart << "Content-Type: application/json; charset=UTF-8\r\n\r\n";
      multipart << pkgStr << "\r\n";
      multipart << "--" << boundary << "--\r\n";

      std::string mpBody = multipart.str();
      std::vector<std::string> headers = {
        "Authorization: Bearer " + s_accessToken,
        "Content-Type: multipart/related; boundary=" + boundary
      };
      resp = httpExecute("POST", postUrl, headers, mpBody);
    }

    if (resp.statusCode == 200) {
      std::string newId = extractJsonString(resp.body, "id");
      {
        std::lock_guard<std::mutex> lock(s_cloudMutex);
        if (!newId.empty()) {
          s_cloudBackup.fileId = newId;
          s_cloudBackup.exists = true;
        }
        s_state = CloudSaveState::SUCCESS_NOTIFICATION;
        s_statusMsgId = (int)CloudStr::STATUS_UPLOAD_SUCCESS;
        s_statusMessage.clear();
      }
      Platform::showOsdMessage(tr(CloudStr::OSD_UPLOAD_SUCCESS));
      queryCloudBackupAsync();
    } else {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_state = CloudSaveState::ERROR_NOTIFICATION;
      s_statusMsgId = -1;
      s_statusMessage = "Falha ao enviar backup (" + std::to_string(resp.statusCode) + ")";
      Platform::showOsdMessage(tr(CloudStr::OSD_UPLOAD_ERROR));
    }
  });
}

// -----------------------------------------------------------------------------
// Download e Restauração Inteligente
// -----------------------------------------------------------------------------
void CloudSave::requestRestore() {
  if (!s_cloudBackup.exists) {
    s_statusMsgId = (int)CloudStr::NO_CLOUD_BACKUP;
    s_statusMessage.clear();
    return;
  }
  s_state = CloudSaveState::RESTORE_CONFIRM;
}

void CloudSave::cancelRestore() {
  s_state = CloudSaveState::LOGGED_IN;
}

void CloudSave::confirmRestore(VM* vm) {
  s_state = CloudSaveState::DOWNLOADING;
  s_statusMessage = "Baixando save da nuvem e restaurando arquivos...";

  runAsync([vm]() {
    if (!ensureValidAccessToken()) {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_state = CloudSaveState::ERROR_NOTIFICATION;
      s_statusMessage = "Erro de autenticacao.";
      return;
    }

    std::string getUrl = "https://www.googleapis.com/drive/v3/files/" +
                         s_cloudBackup.fileId + "?alt=media";
    std::vector<std::string> headers = {
      "Authorization: Bearer " + s_accessToken
    };

    HttpResponse resp = httpExecute("GET", getUrl, headers, "");
    if (resp.statusCode != 200 || resp.body.empty()) {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_state = CloudSaveState::ERROR_NOTIFICATION;
      s_statusMessage = "Falha ao baixar backup da nuvem (" + std::to_string(resp.statusCode) + ")";
      Platform::showOsdMessage(tr(CloudStr::OSD_DOWNLOAD_ERROR));
      return;
    }

    // Grava arquivos .rms descompactados
    std::string rmsBase = Platform::getRmsDir(vm ? vm : s_activeVm);
    std::error_code ec;
    std::filesystem::create_directories(rmsBase, ec);

    const char* expectedFiles[] = {
      "_k.rms", "_s.rms", "_w.rms", "_o.rms", "_c.rms"
    };

    int restoredCount = 0;
    for (const char* fn : expectedFiles) {
      std::string b64 = extractJsonString(resp.body, fn);
      if (b64.empty()) continue;

      std::vector<uint8_t> data = base64Decode(b64);
      if (data.empty()) continue;

      std::string outPath = (rmsBase.empty() || rmsBase.back() == '/' ? rmsBase : rmsBase + "/") + fn;
      std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
      if (f) {
        f.write((const char*)data.data(), data.size());
        restoredCount++;
      }
    }

    if (restoredCount == 0) {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_state = CloudSaveState::ERROR_NOTIFICATION;
      s_statusMessage = "O arquivo baixado nao continha dados validos.";
      return;
    }

    {
      std::lock_guard<std::mutex> lock(s_cloudMutex);
      s_pendingVmReload = true;
      s_state = CloudSaveState::SUCCESS_NOTIFICATION;
      s_statusMsgId = (int)CloudStr::STATUS_DOWNLOAD_SUCCESS;
      s_statusMessage.clear();
    }
    Platform::showOsdMessage(tr(CloudStr::OSD_DOWNLOAD_SUCCESS));
  });
}

// -----------------------------------------------------------------------------
// Manipulação de Entrada no Modal
// -----------------------------------------------------------------------------
bool CloudSave::handleInput(int key, VM* vm) {
  if (vm) s_activeVm = vm;
  if (!s_modalActive) return false;

  // Tecla Cancelar / Voltar (B / ESC / RSK)
  if (key == -7 || key == -8 || key == 27) {
    if (s_state == CloudSaveState::RESTORE_CONFIRM) {
      cancelRestore();
    } else {
      closeModal();
    }
    return true;
  }

  // Tecla Ação / Confirmar (5 / Enter / A)
  if (key == 53 || key == 13 || key == 32) {
    if (s_state == CloudSaveState::NOT_LOGGED_IN || s_state == CloudSaveState::ERROR_NOTIFICATION) {
      startLogin();
      return true;
    } else if (s_state == CloudSaveState::LOGGED_IN || s_state == CloudSaveState::SUCCESS_NOTIFICATION) {
      uploadBackupAsync();
      return true;
    } else if (s_state == CloudSaveState::RESTORE_CONFIRM) {
      confirmRestore(vm);
      return true;
    }
  }

  // Tecla 1 (Backup) / Tecla 2 (Restaurar) / Tecla 3 (Desconectar)
  if (key == 49 || key == '1') { // 1
    if (isLoggedIn()) uploadBackupAsync();
    else startLogin();
    return true;
  } else if (key == 50 || key == '2') { // 2 / X
    if (isLoggedIn()) requestRestore();
    return true;
  } else if (key == 51 || key == '3') { // 3 / Y
    if (isLoggedIn()) logout();
    return true;
  }

  return true;
}

void CloudSave::handleClick(int x, int y, VM* vm) {
  if (vm) s_activeVm = vm;
  if (!s_modalActive) return;

  auto inRect = [](int px, int py, const SDL_Rect& r) {
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
  };

  if (inRect(x, y, s_btnCloseX)) {
    if (s_state == CloudSaveState::RESTORE_CONFIRM) cancelRestore();
    else closeModal();
    return;
  }

  if (inRect(x, y, s_btnCancel)) {
    if (s_state == CloudSaveState::RESTORE_CONFIRM) cancelRestore();
    else closeModal();
    return;
  }

  if (inRect(x, y, s_btnAction1)) {
    if (s_state == CloudSaveState::NOT_LOGGED_IN || s_state == CloudSaveState::ERROR_NOTIFICATION) {
      startLogin();
    } else if (s_state == CloudSaveState::RESTORE_CONFIRM) {
      confirmRestore(vm);
    } else if (isLoggedIn()) {
      uploadBackupAsync();
    }
    return;
  }

  if (inRect(x, y, s_btnAction2)) {
    if (isLoggedIn() && s_state != CloudSaveState::RESTORE_CONFIRM) {
      requestRestore();
    }
    return;
  }

  if (inRect(x, y, s_btnAction3)) {
    if (isLoggedIn()) {
      logout();
    }
    return;
  }
}

// -----------------------------------------------------------------------------
// Renderização Gráfica Nobre (Soltia Theme)
// -----------------------------------------------------------------------------
void CloudSave::drawModal(SDL_Renderer* renderer, int winW, int winH) {
  if (!s_modalActive || !renderer) return;

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

  // 1. Fundo Escurecido Fosco (Backdrop)
  SDL_Rect fullScreen = { 0, 0, winW, winH };
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 215);
  SDL_RenderFillRect(renderer, &fullScreen);

  // 2. Detecção de Orientação e Plataforma
  bool isPortrait = (winH > winW);
#if defined(__ANDROID__)
  bool isMobile = true;
#else
  bool isMobile = false;
#endif

  // Escala Dinâmica por Resolução Real:
  // Em 720p (Windows / Switch): refDim = 720 -> scaleFactor = 1.0f
  // Em 1080p (Mobile Portrait 1080x2400 ou Landscape 2400x1080): refDim = 1080 -> scaleFactor = 1.50f
  // Em 1440p (Mobile QHD): refDim = 1440 -> scaleFactor = 2.0f
  float refDim = (float)std::min(winW, winH);
  float scaleFactor = std::max(0.85f, refDim / 720.0f);
  if (isMobile) {
    // No mobile, telas são seguradas com as mãos e exigem botões e tipografia com toque confortável
    scaleFactor *= 1.15f;
  }

  // Dimensões do Modal Adaptativas
  // Em Portrait: preenche 93% da largura útil da tela (deixa margem lateral limpa de 3.5%)
  // Em Landscape: preenche cerca de 70% da largura ultrawide (em 2400px = ~1680px, em 1280px = ~920px)
  int modalW = isPortrait ? (int)(winW * 0.93f)
                          : std::clamp((int)(winW * 0.70f), (int)(winH * 1.15f), (int)(winW * 0.88f));

  int titleFontSize    = (int)(25.0f * scaleFactor);
  int headerH          = (int)(58.0f * scaleFactor);
  int statusFontSize   = (int)(18.0f * scaleFactor);
  int bodyFontSize     = (int)(21.0f * scaleFactor);
  int subFontSize      = (int)(17.0f * scaleFactor);
  int metaFontSize     = (int)(15.0f * scaleFactor);
  int btnFontSize      = (int)(18.0f * scaleFactor);
  int shortcutFontSize = (int)(13.5f * scaleFactor);

  int padX = (int)(20.0f * scaleFactor);
  int contentW = modalW - padX * 2;
  int btnH = isPortrait ? (int)(52.0f * scaleFactor) : (int)(44.0f * scaleFactor);
  int btnSpacing = (int)(12.0f * scaleFactor);

  // Atalhos por Plataforma (Mobile Touch limpo / Switch Joy-Con / PC)
  std::string sc1 = "";
  std::string sc2 = "";
  std::string sc3 = "";
  std::string scCancel = "";

#if defined(__SWITCH__)
  sc1 = "( A )";
  sc2 = "( X )";
  sc3 = "( Y )";
  scCancel = "( B )";
#elif !defined(__ANDROID__)
  if (!isMobile) {
    sc1 = "[ 1 / A ]";
    sc2 = "[ 2 / X ]";
    sc3 = "[ 3 / Y ]";
    scCancel = "[ ESC / B ]";
  }
#endif

  // Cálculo da Altura Necessária (Content-Fitted)
  int neededH = headerH + (int)(16.0f * scaleFactor);

  if (s_state == CloudSaveState::NOT_LOGGED_IN || s_state == CloudSaveState::REQUESTING_CODE || s_state == CloudSaveState::ERROR_NOTIFICATION) {
    int infoH = subFontSize * 2 + (int)(20.0f * scaleFactor) + bodyFontSize * 2 + (int)(32.0f * scaleFactor);
    neededH += infoH + btnH + (int)(28.0f * scaleFactor);
  } else if (s_state == CloudSaveState::WAITING_USER_AUTH) {
    int codeBoxH = std::clamp((int)(64.0f * scaleFactor), 50, 110);
    neededH += subFontSize * 2 + bodyFontSize + codeBoxH + subFontSize + btnH + (int)(70.0f * scaleFactor);
  } else if (s_state == CloudSaveState::RESTORE_CONFIRM) {
    int warnH = subFontSize + bodyFontSize * 3 + (int)(48.0f * scaleFactor);
    neededH += warnH + btnH + (int)(32.0f * scaleFactor);
  } else {
    // Autenticado / Visualização de Saves
    int badgeH = statusFontSize + (int)(14.0f * scaleFactor);
    int cardSingleH = subFontSize + bodyFontSize + metaFontSize + (int)(28.0f * scaleFactor);
    int cardsAreaH = isPortrait ? (cardSingleH * 2 + (int)(12.0f * scaleFactor)) : cardSingleH;
    int btnsAreaH = isPortrait ? (btnH * 2 + btnSpacing) : btnH;
    int statusExtraH = (s_statusMsgId >= 0 || !s_statusMessage.empty()) ? (subFontSize + (int)(12.0f * scaleFactor)) : 0;
    neededH += badgeH + cardsAreaH + statusExtraH + btnsAreaH + (int)(36.0f * scaleFactor);
  }

  int maxH = (int)(winH * 0.95f);
  int modalH = std::min(neededH, maxH);
  int modalX = (winW - modalW) / 2;
  int modalY = (winH - modalH) / 2;
  s_modalRect = { modalX, modalY, modalW, modalH };

  // Fundo com Sombra Projetada
  SDL_Rect shadow = { modalX + 6, modalY + 6, modalW, modalH };
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
  SDL_RenderFillRect(renderer, &shadow);

  // Corpo do Modal - Azul Ardósia Profundo
  SDL_Rect box = { modalX, modalY, modalW, modalH };
  SDL_SetRenderDrawColor(renderer, 16, 22, 34, 252);
  SDL_RenderFillRect(renderer, &box);

  // Contorno Externo Refinado
  SDL_SetRenderDrawColor(renderer, 45, 62, 88, 255);
  SDL_RenderDrawRect(renderer, &box);

  // Moldura Interna Ouro Nobre (Soltia Theme)
  SDL_Rect goldBorder = { modalX + 3, modalY + 3, modalW - 6, modalH - 6 };
  SDL_SetRenderDrawColor(renderer, 190, 150, 60, 220);
  SDL_RenderDrawRect(renderer, &goldBorder);

  // Cabeçalho com Gradiente/Barra Superior
  SDL_Rect headerBox = { modalX + 4, modalY + 4, modalW - 8, headerH };
  SDL_SetRenderDrawColor(renderer, 24, 34, 52, 255);
  SDL_RenderFillRect(renderer, &headerBox);

  SDL_SetRenderDrawColor(renderer, 190, 150, 60, 180);
  SDL_RenderDrawLine(renderer, headerBox.x, headerBox.y + headerH, headerBox.x + headerBox.w, headerBox.y + headerH);

  std::string title = tr(CloudStr::TITLE);
  int titleW = Platform::getTextWidth(title, titleFontSize);
  Platform::drawTextColored(renderer, title, modalX + (modalW - titleW) / 2, modalY + (headerH - titleFontSize) / 2 + 2,
                            titleFontSize, 240, 205, 110, 255);

  // Botão Fechar [X] no Cabeçalho (Quadrado e Confortável para Touch)
  int closeSize = headerH - (int)(16.0f * scaleFactor);
  s_btnCloseX = { modalX + modalW - closeSize - (int)(10.0f * scaleFactor), modalY + (headerH - closeSize) / 2, closeSize, closeSize };
  SDL_SetRenderDrawColor(renderer, 38, 48, 68, 220);
  SDL_RenderFillRect(renderer, &s_btnCloseX);
  SDL_SetRenderDrawColor(renderer, 110, 130, 165, 255);
  SDL_RenderDrawRect(renderer, &s_btnCloseX);

  std::string xGlyph = "X";
  int xw = Platform::getTextWidth(xGlyph, subFontSize);
  Platform::drawTextColored(renderer, xGlyph, s_btnCloseX.x + (closeSize - xw) / 2,
                            s_btnCloseX.y + (closeSize - subFontSize) / 2, subFontSize, 215, 225, 245, 255);

  int curY = modalY + headerH + (int)(14.0f * scaleFactor);

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

    // Ajuste de fonte caso o texto seja maior que a largura
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
      int totalH = effFontSize + effScSize + 3;
      int ty1 = bRect.y + (bRect.h - totalH) / 2;
      int ty2 = ty1 + effFontSize + 3;

      int tx1 = bRect.x + (bRect.w - tw) / 2;
      Platform::drawTextColored(renderer, bTitle, tx1, ty1, effFontSize, 255, 255, 255, 255);

      int tx2 = bRect.x + (bRect.w - scw) / 2;
      Platform::drawTextColored(renderer, bSc, tx2, ty2, effScSize, 205, 220, 240, 220);
    }
  };

  // Helper de Card de Save
  auto drawSaveCard = [&](const SDL_Rect& cRect, const std::string& cardTitle,
                          const std::string& heroLine, const std::string& metaLine,
                          bool isCloud, bool exists) {
    SDL_SetRenderDrawColor(renderer, 20, 26, 40, 255);
    SDL_RenderFillRect(renderer, &cRect);

    // Barra lateral de identificação sólida
    int barW = std::max(4, (int)(4.0f * scaleFactor));
    SDL_Rect sideBar = { cRect.x, cRect.y, barW, cRect.h };
    if (isCloud) {
      SDL_SetRenderDrawColor(renderer, 65, 145, 235, 255); // Azul nuvem
    } else {
      SDL_SetRenderDrawColor(renderer, 65, 195, 125, 255); // Verde local
    }
    SDL_RenderFillRect(renderer, &sideBar);

    SDL_SetRenderDrawColor(renderer, 45, 65, 95, 255);
    SDL_RenderDrawRect(renderer, &cRect);

    int textX = cRect.x + barW + (int)(12.0f * scaleFactor);
    int curCardY = cRect.y + (int)(10.0f * scaleFactor);

    uint8_t tr_ = isCloud ? 140 : 130;
    uint8_t tg_ = isCloud ? 190 : 215;
    uint8_t tb_ = isCloud ? 245 : 155;
    Platform::drawTextColored(renderer, cardTitle, textX, curCardY, subFontSize, tr_, tg_, tb_, 255);
    curCardY += subFontSize + (int)(6.0f * scaleFactor);

    if (!exists) {
      std::string emptyMsg = isCloud ? tr(CloudStr::NO_CLOUD_BACKUP) : tr(CloudStr::NO_LOCAL_SAVE);
      Platform::drawTextColored(renderer, emptyMsg, textX, curCardY + (int)(4.0f * scaleFactor), bodyFontSize, 175, 185, 200, 200);
    } else {
      Platform::drawTextColored(renderer, heroLine, textX, curCardY, bodyFontSize, 255, 255, 255, 255);
      curCardY += bodyFontSize + (int)(4.0f * scaleFactor);
      if (!metaLine.empty()) {
        Platform::drawTextColored(renderer, metaLine, textX, curCardY, metaFontSize, 165, 180, 205, 220);
      }
    }
  };

  // ---------------------------------------------------------------------------
  // 1. Estado: NÃO AUTENTICADO
  // ---------------------------------------------------------------------------
  if (s_state == CloudSaveState::NOT_LOGGED_IN || s_state == CloudSaveState::REQUESTING_CODE || s_state == CloudSaveState::ERROR_NOTIFICATION) {
    std::string line1 = tr(CloudStr::SUBTITLE_1);
    std::string line2 = tr(CloudStr::SUBTITLE_2);
    int l1w = Platform::getTextWidth(line1, subFontSize);
    int l2w = Platform::getTextWidth(line2, subFontSize);
    Platform::drawTextColored(renderer, line1, modalX + (modalW - l1w) / 2, curY, subFontSize, 210, 225, 245, 220);
    curY += subFontSize + (int)(4.0f * scaleFactor);
    Platform::drawTextColored(renderer, line2, modalX + (modalW - l2w) / 2, curY, subFontSize, 210, 225, 245, 220);
    curY += subFontSize + (int)(16.0f * scaleFactor);

    // Card Local
    char lTitleBuf[64];
    std::snprintf(lTitleBuf, sizeof(lTitleBuf), tr(CloudStr::CARD_LOCAL), getPlatformName().c_str());
    std::string lSummary = getLocalSavesSummary();
    std::string lDate = getLocalSavesDate();
    bool lExists = (lSummary != tr(CloudStr::NO_LOCAL_SAVE));
    std::string metaStr = (!lDate.empty()) ? (std::string(tr(CloudStr::DATE_PREFIX)) + lDate) : "";

    int cardH = subFontSize + bodyFontSize + metaFontSize + (int)(28.0f * scaleFactor);
    SDL_Rect localBox = { modalX + padX, curY, contentW, cardH };
    drawSaveCard(localBox, lTitleBuf, lSummary, metaStr, false, lExists);
    curY += cardH + (int)(14.0f * scaleFactor);

    // Status da Conexão
    std::string statusStr = "";
    if (s_statusMsgId >= 0 && (size_t)s_statusMsgId < (size_t)CloudStr::STR_COUNT) {
      statusStr = tr((CloudStr)s_statusMsgId);
    } else {
      statusStr = s_statusMessage.empty() ? tr(CloudStr::STATUS_DISCONNECTED) : s_statusMessage;
    }
    int stw = Platform::getTextWidth(statusStr, statusFontSize);
    uint8_t sr = (s_state == CloudSaveState::ERROR_NOTIFICATION) ? 245 : 180;
    uint8_t sg = (s_state == CloudSaveState::ERROR_NOTIFICATION) ? 90  : 200;
    uint8_t sb = (s_state == CloudSaveState::ERROR_NOTIFICATION) ? 80  : 225;
    Platform::drawTextColored(renderer, statusStr, modalX + (modalW - stw) / 2, curY, statusFontSize, sr, sg, sb, 255);

    // Botões
    int by = modalY + modalH - btnH - (int)(16.0f * scaleFactor);
    int bW = (contentW - btnSpacing) / 2;
    s_btnAction1 = { modalX + padX, by, bW, btnH };
    s_btnCancel  = { modalX + padX + bW + btnSpacing, by, bW, btnH };

    drawModernBtn(tr(CloudStr::BTN_CONNECT), sc1, s_btnAction1, {32, 105, 52, 255}, {55, 175, 90, 255});
    drawModernBtn(tr(CloudStr::BTN_CLOSE), scCancel, s_btnCancel, {42, 48, 62, 255}, {85, 98, 120, 255});
  }

  // ---------------------------------------------------------------------------
  // 2. Estado: AGUARDANDO AUTORIZAÇÃO DO USUÁRIO (CÓDIGO NA TELA)
  // ---------------------------------------------------------------------------
  else if (s_state == CloudSaveState::WAITING_USER_AUTH) {
    std::string step1 = tr(CloudStr::STEP1_ACCESS_LINK);
    int s1w = Platform::getTextWidth(step1, subFontSize);
    Platform::drawTextColored(renderer, step1, modalX + (modalW - s1w) / 2, curY, subFontSize, 215, 225, 245, 230);
    curY += subFontSize + (int)(6.0f * scaleFactor);

    std::string urlText = s_deviceCode.verificationUrl;
    int uw = Platform::getTextWidth(urlText, bodyFontSize);
    Platform::drawTextColored(renderer, urlText, modalX + (modalW - uw) / 2, curY, bodyFontSize, 90, 175, 255, 255);
    curY += bodyFontSize + (int)(14.0f * scaleFactor);

    std::string step2 = tr(CloudStr::STEP2_ENTER_CODE);
    int s2w = Platform::getTextWidth(step2, subFontSize);
    Platform::drawTextColored(renderer, step2, modalX + (modalW - s2w) / 2, curY, subFontSize, 215, 225, 245, 230);
    curY += subFontSize + (int)(8.0f * scaleFactor);

    // Caixa de Destaque para o Código
    int codeBoxW = std::clamp((int)(contentW * 0.75f), 240, (int)(560.0f * scaleFactor));
    int codeBoxH = std::clamp((int)(64.0f * scaleFactor), 50, (int)(100.0f * scaleFactor));
    SDL_Rect codeBox = { modalX + (modalW - codeBoxW) / 2, curY, codeBoxW, codeBoxH };
    SDL_SetRenderDrawColor(renderer, 26, 36, 54, 255);
    SDL_RenderFillRect(renderer, &codeBox);
    SDL_SetRenderDrawColor(renderer, 215, 175, 65, 255);
    SDL_RenderDrawRect(renderer, &codeBox);

    int codeFontSize = std::clamp((int)(codeBoxH * 0.55f), 20, 52);
    int cw = Platform::getTextWidth(s_deviceCode.userCode, codeFontSize);
    Platform::drawTextColored(renderer, s_deviceCode.userCode, codeBox.x + (codeBoxW - cw) / 2,
                              codeBox.y + (codeBoxH - codeFontSize) / 2, codeFontSize, 255, 220, 100, 255);
    curY += codeBoxH + (int)(14.0f * scaleFactor);

    std::string waitMsg = tr(CloudStr::WAITING_BROWSER_AUTH);
    int ww = Platform::getTextWidth(waitMsg, subFontSize);
    Platform::drawTextColored(renderer, waitMsg, modalX + (modalW - ww) / 2, curY, subFontSize, 175, 190, 210, 200);

    // Botão Cancelar
    int by = modalY + modalH - btnH - (int)(16.0f * scaleFactor);
    int bW = std::clamp((int)(contentW * 0.55f), 180, (int)(400.0f * scaleFactor));
    s_btnCancel = { modalX + (modalW - bW) / 2, by, bW, btnH };
    drawModernBtn(tr(CloudStr::BTN_CANCEL), scCancel, s_btnCancel, {55, 42, 42, 255}, {145, 75, 75, 255});
  }

  // ---------------------------------------------------------------------------
  // 3. Estado: CONFIRMAÇÃO DE RESTAURAÇÃO
  // ---------------------------------------------------------------------------
  else if (s_state == CloudSaveState::RESTORE_CONFIRM) {
    int warnBoxH = subFontSize + bodyFontSize * 3 + (int)(44.0f * scaleFactor);
    SDL_Rect warnBox = { modalX + padX, curY, contentW, warnBoxH };
    SDL_SetRenderDrawColor(renderer, 48, 24, 18, 255);
    SDL_RenderFillRect(renderer, &warnBox);
    SDL_SetRenderDrawColor(renderer, 220, 110, 40, 255);
    SDL_RenderDrawRect(renderer, &warnBox);

    int textX = warnBox.x + (int)(16.0f * scaleFactor);
    int curW = warnBox.y + (int)(12.0f * scaleFactor);

    std::string wTitle = tr(CloudStr::WARN_RESTORE_TITLE);
    Platform::drawTextColored(renderer, wTitle, textX, curW, subFontSize, 255, 195, 70, 255);
    curW += subFontSize + (int)(8.0f * scaleFactor);

    std::string w1 = tr(CloudStr::WARN_RESTORE_LINE1);
    std::string w2 = tr(CloudStr::WARN_RESTORE_LINE2);
    std::string w3 = tr(CloudStr::WARN_RESTORE_LINE3);
    Platform::drawTextColored(renderer, w1, textX, curW, bodyFontSize, 240, 220, 215, 240);
    curW += bodyFontSize + (int)(4.0f * scaleFactor);
    Platform::drawTextColored(renderer, w2, textX, curW, bodyFontSize, 240, 220, 215, 240);
    curW += bodyFontSize + (int)(6.0f * scaleFactor);
    Platform::drawTextColored(renderer, w3, textX, curW, bodyFontSize, 255, 140, 120, 255);

    // Botões Confirmar / Cancelar
    int by = modalY + modalH - btnH - (int)(16.0f * scaleFactor);
    int bW = (contentW - btnSpacing) / 2;
    s_btnAction1 = { modalX + padX, by, bW, btnH };
    s_btnCancel  = { modalX + padX + bW + btnSpacing, by, bW, btnH };

    drawModernBtn(tr(CloudStr::BTN_CONFIRM), sc1, s_btnAction1, {130, 42, 35, 255}, {225, 75, 65, 255});
    drawModernBtn(tr(CloudStr::BTN_CANCEL), scCancel, s_btnCancel, {42, 48, 62, 255}, {85, 98, 120, 255});
  }

  // ---------------------------------------------------------------------------
  // 4. Estado: AUTENTICADO / OPERAÇÕES DE BACKUP E RESTORE
  // ---------------------------------------------------------------------------
  else {
    // Badge de Conexão Central
    std::string connStr = tr(CloudStr::STATUS_CONNECTED);
    int cw = Platform::getTextWidth(connStr, statusFontSize);
    Platform::drawTextColored(renderer, connStr, modalX + (modalW - cw) / 2, curY, statusFontSize, 75, 215, 120, 255);
    curY += statusFontSize + (int)(14.0f * scaleFactor);

    // Informações dos Saves
    std::string cloudHero = s_cloudBackup.exists ? s_cloudBackup.summary : "";
    std::string cloudMeta = s_cloudBackup.exists ? (std::string(tr(CloudStr::DATE_PREFIX)) + s_cloudBackup.modifiedTime) : "";

    char lTitleBuf[64];
    std::snprintf(lTitleBuf, sizeof(lTitleBuf), tr(CloudStr::CARD_LOCAL), getPlatformName().c_str());
    std::string lSummary = getLocalSavesSummary();
    std::string lDate = getLocalSavesDate();
    bool lExists = (lSummary != tr(CloudStr::NO_LOCAL_SAVE));
    std::string localMeta = (!lDate.empty()) ? (std::string(tr(CloudStr::DATE_PREFIX)) + lDate) : "";

    int cardH = subFontSize + bodyFontSize + metaFontSize + (int)(28.0f * scaleFactor);

    if (isPortrait) {
      // Mobile Vertical: Cards Empilhados com respiro
      SDL_Rect cloudBox = { modalX + padX, curY, contentW, cardH };
      drawSaveCard(cloudBox, tr(CloudStr::CARD_CLOUD), cloudHero, cloudMeta, true, s_cloudBackup.exists);
      curY += cardH + (int)(12.0f * scaleFactor);

      SDL_Rect localBox = { modalX + padX, curY, contentW, cardH };
      drawSaveCard(localBox, lTitleBuf, lSummary, localMeta, false, lExists);
      curY += cardH + (int)(14.0f * scaleFactor);
    } else {
      // Landscape: Cards Lado a Lado
      int cardSpacing = (int)(16.0f * scaleFactor);
      int cardW = (contentW - cardSpacing) / 2;
      SDL_Rect cloudBox = { modalX + padX, curY, cardW, cardH };
      SDL_Rect localBox = { modalX + padX + cardW + cardSpacing, curY, cardW, cardH };
      drawSaveCard(cloudBox, tr(CloudStr::CARD_CLOUD), cloudHero, cloudMeta, true, s_cloudBackup.exists);
      drawSaveCard(localBox, lTitleBuf, lSummary, localMeta, false, lExists);
      curY += cardH + (int)(14.0f * scaleFactor);
    }

    // Mensagem de Status (se houver)
    std::string displayStatus = "";
    if (s_statusMsgId >= 0 && (size_t)s_statusMsgId < (size_t)CloudStr::STR_COUNT) {
      displayStatus = tr((CloudStr)s_statusMsgId);
    } else if (!s_statusMessage.empty()) {
      displayStatus = s_statusMessage;
    }
    if (!displayStatus.empty()) {
      int sw = Platform::getTextWidth(displayStatus, statusFontSize);
      Platform::drawTextColored(renderer, displayStatus, modalX + (modalW - sw) / 2, curY, statusFontSize, 220, 235, 255, 255);
      curY += statusFontSize + (int)(6.0f * scaleFactor);
    }

    // Botões Inferiores (4 Botões)
    std::string b1 = tr(CloudStr::BTN_UPLOAD);
    std::string b2 = tr(CloudStr::BTN_RESTORE);
    std::string b3 = tr(CloudStr::BTN_DISCONNECT);
    std::string bc = tr(CloudStr::BTN_CLOSE);

    if (isPortrait) {
      // Portrait: Grid 2x2 com toque confortável para dois polegares
      int bW = (contentW - btnSpacing) / 2;
      int by1 = modalY + modalH - btnH * 2 - btnSpacing - (int)(16.0f * scaleFactor);
      if (by1 < curY) by1 = curY;
      int by2 = by1 + btnH + btnSpacing;

      s_btnAction1 = { modalX + padX, by1, bW, btnH };
      s_btnAction2 = { modalX + padX + bW + btnSpacing, by1, bW, btnH };
      s_btnAction3 = { modalX + padX, by2, bW, btnH };
      s_btnCancel  = { modalX + padX + bW + btnSpacing, by2, bW, btnH };
    } else {
      // Landscape: Barra de 4 botões horizontais uniformes
      int bW = (contentW - btnSpacing * 3) / 4;
      int by = modalY + modalH - btnH - (int)(16.0f * scaleFactor);
      if (by < curY) by = curY;

      s_btnAction1 = { modalX + padX, by, bW, btnH };
      s_btnAction2 = { modalX + padX + (bW + btnSpacing), by, bW, btnH };
      s_btnAction3 = { modalX + padX + (bW + btnSpacing) * 2, by, bW, btnH };
      s_btnCancel  = { modalX + padX + (bW + btnSpacing) * 3, by, bW, btnH };
    }

    drawModernBtn(b1, sc1, s_btnAction1, {32, 105, 52, 255}, {55, 175, 90, 255});
    drawModernBtn(b2, sc2, s_btnAction2, {26, 75, 135, 255}, {50, 130, 220, 255});
    drawModernBtn(b3, sc3, s_btnAction3, {105, 35, 35, 255}, {190, 60, 60, 255});
    drawModernBtn(bc, scCancel, s_btnCancel, {42, 48, 62, 255}, {85, 98, 120, 255});
  }
}

} // namespace hl
