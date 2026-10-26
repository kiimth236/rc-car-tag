#include "radio.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>
#include "rcar_config.h"

static QueueHandle_t s_queue = nullptr;
static const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// 무선 태스크(코어 0)에서 불림 → 검사 후 큐에 넣기만
static void onRecv(const uint8_t* mac, const uint8_t* data, int len) {
  (void)mac;
  rcar::Message m;
  if (len > 0 && rcar::decode(data, (size_t)len, rcar::kTeamMagic, m)) xQueueSend(s_queue, &m, 0);
}

bool radioBegin(uint8_t carId) {
  s_queue = xQueueCreate(16, sizeof(rcar::Message));
  WiFi.persistent(false);  // AP 설정을 매번 NVS 에 쓰지 않고, 예전 STA 설정이 채널을 끌고 가지 않게
  WiFi.mode(WIFI_AP_STA);  // AP = 폰 접속, STA 인터페이스 = ESP-NOW broadcast
  char ssid[16], pass[16];
  snprintf(ssid, sizeof ssid, "RCAR-%u", carId);
  snprintf(pass, sizeof pass, "%s%u", cfg::AP_PASS_PREFIX, carId);  // 8글자 (WPA2 최소)
  if (!WiFi.softAP(ssid, pass, cfg::WIFI_CHANNEL, 0, cfg::AP_MAX_CLIENTS)) return false;
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  esp_wifi_set_channel(cfg::WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof peer);
  memcpy(peer.peer_addr, kBroadcast, 6);
  peer.channel = cfg::WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  return esp_now_add_peer(&peer) == ESP_OK;
}

bool radioSend(const rcar::Message& m) {
  uint8_t buf[sizeof(rcar::Message)];
  size_t n = rcar::encode(m, buf, sizeof buf);
  return n > 0 && esp_now_send(kBroadcast, buf, n) == ESP_OK;
}

bool radioReceive(rcar::Message& out) {
  return s_queue != nullptr && xQueueReceive(s_queue, &out, 0) == pdTRUE;
}
