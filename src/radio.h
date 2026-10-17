#pragma once
#include "protocol.h"

bool radioBegin();                         // Wi-Fi STA · 채널 고정 · ESP-NOW · broadcast 상대 등록
bool radioSend(const rcar::Message& m);    // broadcast
bool radioReceive(rcar::Message& out);     // 받은 메시지 하나 (없으면 false)
