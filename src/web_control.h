#pragma once
#include "web_proto.h"

// 폰 웹 조종: HTTP(화면·글꼴) + WebSocket(명령·상태). 네트워크 일은 전부 웹 태스크(코어 0)에서만 한다.
void webBegin();                          // radioBegin 뒤에 부름. HTTP 80, WebSocket 81, 태스크(코어 0) 시작
bool webPollCmd(rcar::WebCmd& out);       // 메인 루프: 받은 명령 하나 (없으면 false)
void webPublish(const char* json);        // 메인 루프: 최신 상태 JSON 넘김 (태스크가 UI_PUSH_MS마다 보냄)
bool webClientConnected();                // WebSocket 클라이언트가 붙어 있음
