#pragma once

void sonarBegin();
float sonarReadCm();  // 거리 cm, 에코 없으면 -1 (최대 약 12ms 기다림)
