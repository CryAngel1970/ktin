#pragma once

#include <windows.h>

// 하이라이트는 TinTin++ 스크립트 파일 안의 #highlight/#class 구문으로 관리합니다.
// 이전 config.ini 기반 하이라이트 설정 함수는 호환용으로 남겨 두되 더 이상 자료를 저장하지 않습니다.
void LoadHighlightSettings();
void SaveHighlightSettings();
void ShowHighlightDialog(HWND owner);
