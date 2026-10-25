#pragma once
#include <stddef.h>

namespace rcar {

constexpr size_t kNameBytes = 8;  // 이름 최대 UTF-8 바이트 수

// 이름 검사. 0~2글자, 글자는 영문 대소문자·숫자 또는 한글 음절(U+AC00~U+D7A3), 8바이트 이하.
// nullptr·잘못된 UTF-8·그 밖의 글자(따옴표, 공백, 이모지, 자모)는 false. 빈 문자열은 true.
bool validName(const char* utf8);

}  // namespace rcar
