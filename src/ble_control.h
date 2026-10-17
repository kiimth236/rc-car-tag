#pragma once

struct PadInput {
  bool connected;   // 폰 Dabble 앱이 연결돼 있음
  float throttle;   // -1(후진) ~ 1(전진)
  float steer;      // -1(왼쪽) ~ 1(오른쪽), STEER_GAIN 적용
  bool startEdge;   // START 를 막 누름
};

void bleBegin(const char* name);
PadInput blePoll();  // loop 마다 한 번
