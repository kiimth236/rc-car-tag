#include "web_control.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <atomic>
#include <string.h>
#include "rcar_config.h"

// platformio.ini 의 board_build.embed_txtfiles / embed_files 로 펌웨어에 들어간 파일 (플래시에 그대로 있음)
// txt 파일은 끝에 NUL 1바이트가 붙어 있어서 보낼 때 뺀다.
extern const uint8_t kIndexStart[] asm("_binary_web_index_html_start");
extern const uint8_t kIndexEnd[] asm("_binary_web_index_html_end");
extern const uint8_t kOflStart[] asm("_binary_web_OFL_txt_start");
extern const uint8_t kOflEnd[] asm("_binary_web_OFL_txt_end");
extern const uint8_t kFontStart[] asm("_binary_web_Galmuri11_woff2_start");
extern const uint8_t kFontEnd[] asm("_binary_web_Galmuri11_woff2_end");

static constexpr size_t kJsonCap = 640;

// 메인 루프 ↔ 웹 태스크 사이에 오가는 것은 이 셋뿐
static QueueHandle_t s_cmdQueue = nullptr;          // 폰 명령 (웹 태스크 → 메인 루프)
static SemaphoreHandle_t s_jsonMutex = nullptr;     // s_json 보호 (메인 루프가 쓰고 웹 태스크가 읽음)
static char s_json[kJsonCap] = "";
static size_t s_jsonLen = 0;
static std::atomic<bool> s_connected{false};

// 아래는 웹 태스크 안에서만 씀
static WebServer* s_http = nullptr;
static WebSocketsServer* s_ws = nullptr;
static int s_client = -1;  // 지금 붙어 있는 WebSocket 번호 (-1 = 없음)

static void sendEmbedded(const char* type, const uint8_t* start, size_t len, const char* cache) {
  s_http->sendHeader("Cache-Control", cache);
  s_http->send_P(200, type, (PGM_P)start, len);  // 플래시에서 바로 보냄 (String 으로 복사하지 않음)
}

// 최신 JSON 을 지금 클라이언트에 한 번 보냄
static void pushLatest() {
  static char buf[kJsonCap];
  if (s_client < 0) return;
  size_t n = 0;
  xSemaphoreTake(s_jsonMutex, portMAX_DELAY);
  n = s_jsonLen;
  memcpy(buf, s_json, n);
  xSemaphoreGive(s_jsonMutex);
  // 못 보냈으면(앱 전환으로 멈춘 아이폰 탭 등) 끊음 → 다음 보낼 때마다 몇 초씩 막히지 않게. 폰은 다시 붙으면 됨
  if (n > 0 && !s_ws->sendTXT((uint8_t)s_client, buf, n)) s_ws->disconnect((uint8_t)s_client);
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED: {
      int prev = s_client;
      s_client = num;  // 먼저 바꿔 둬야 아래 disconnect 의 DISCONNECTED 가 새 연결을 지우지 않음
      if (prev >= 0 && prev != num) s_ws->disconnect((uint8_t)prev);  // 같은 폰이 다시 붙은 경우 등: 옛 연결 끊기
      s_connected = true;
      pushLatest();
      break;
    }
    case WStype_DISCONNECTED:
      if (num == s_client) {
        s_client = -1;
        s_connected = false;
      }
      break;
    case WStype_TEXT: {
      if (num != s_client) break;
      rcar::WebCmd c;
      if (rcar::parseWebCmd((const char*)payload, length, c)) xQueueSend(s_cmdQueue, &c, 0);  // 가득 차면 버림
      break;
    }
    default:
      break;
  }
}

static void webTask(void*) {
  s_http = new WebServer(cfg::HTTP_PORT);
  s_http->on("/", HTTP_GET, [] {
    sendEmbedded("text/html; charset=utf-8", kIndexStart, (size_t)(kIndexEnd - kIndexStart) - 1, "no-cache");
  });
  s_http->on("/Galmuri11.woff2", HTTP_GET, [] {
    sendEmbedded("font/woff2", kFontStart, (size_t)(kFontEnd - kFontStart), "max-age=31536000");
  });
  s_http->on("/OFL.txt", HTTP_GET, [] {
    sendEmbedded("text/plain; charset=utf-8", kOflStart, (size_t)(kOflEnd - kOflStart) - 1, "max-age=86400");
  });
  s_http->onNotFound([] { s_http->send(404, "text/plain", "404"); });
  s_http->begin();

  s_ws = new WebSocketsServer(cfg::WS_PORT);
  s_ws->onEvent(onWsEvent);
  s_ws->begin();

  uint32_t nextPush = millis();
  for (;;) {
    s_http->handleClient();  // 글꼴(504KB)을 보내는 동안은 여기서 오래 머묾 → 메인 루프는 영향 없음
    s_ws->loop();
    uint32_t now = millis();
    if ((int32_t)(now - nextPush) >= 0) {
      nextPush = now + cfg::UI_PUSH_MS;
      pushLatest();
    }
    vTaskDelay(1);  // 코어 0 IDLE 워치독이 굶지 않게
  }
}

// 실패하면 아무것도 안 함 (폰 없음 → 차는 서 있음). 멈추거나 재부팅하지 않음
void webBegin() {
  if (s_cmdQueue) return;
  QueueHandle_t q = xQueueCreate(16, sizeof(rcar::WebCmd));
  SemaphoreHandle_t mx = xSemaphoreCreateMutex();
  if (!q || !mx) {
    // "[웹] 큐·뮤텍스를 못 만듦 - 폰 조종 없음"
    Serial.println("[\xEC\x9B\xB9] \xED\x81\x90\xC2\xB7\xEB\xAE\xA4\xED\x85\x8D\xEC\x8A\xA4\xEB\xA5\xBC \xEB\xAA\xBB \xEB\xA7\x8C\xEB\x93\xA6 - \xED\x8F\xB0 \xEC\xA1\xB0\xEC\xA2\x85 \xEC\x97\x86\xEC\x9D\x8C");
    if (q) vQueueDelete(q);
    if (mx) vSemaphoreDelete(mx);
    return;
  }
  s_cmdQueue = q;
  s_jsonMutex = mx;
  if (xTaskCreatePinnedToCore(webTask, "web", 8192, nullptr, 1, nullptr, 0) != pdPASS) {
    // "[웹] 태스크를 못 만듦 - 폰 조종 없음"
    Serial.println("[\xEC\x9B\xB9] \xED\x83\x9C\xEC\x8A\xA4\xED\x81\xAC\xEB\xA5\xBC \xEB\xAA\xBB \xEB\xA7\x8C\xEB\x93\xA6 - \xED\x8F\xB0 \xEC\xA1\xB0\xEC\xA2\x85 \xEC\x97\x86\xEC\x9D\x8C");
    s_cmdQueue = nullptr;
    s_jsonMutex = nullptr;
    vQueueDelete(q);
    vSemaphoreDelete(mx);
  }
}

bool webPollCmd(rcar::WebCmd& out) {
  return s_cmdQueue != nullptr && xQueueReceive(s_cmdQueue, &out, 0) == pdTRUE;
}

void webPublish(const char* json) {
  if (!s_jsonMutex || !json) return;
  size_t n = strnlen(json, kJsonCap);
  if (n >= kJsonCap) return;  // 잘린 JSON 은 보내지 않음 (지난 값 유지)
  xSemaphoreTake(s_jsonMutex, portMAX_DELAY);  // 웹 태스크는 640바이트 복사하는 동안만 잡음
  memcpy(s_json, json, n);
  s_jsonLen = n;
  xSemaphoreGive(s_jsonMutex);
}

bool webClientConnected() { return s_connected; }
