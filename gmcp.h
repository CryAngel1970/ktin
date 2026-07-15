#pragma once

#include "constants.h"

#include <string_view>

// TinTin++가 OSC 777;KTIN_GMCP;<base64> 형태로 넘긴 GMCP 패킷을
// 메인 UI 스레드에 안전하게 전달합니다.
bool PostGmcpPacketFromOsc(std::string_view encoded);
bool HandleMainGmcpUpdate(HWND hwnd, LPARAM lParam);

void ShowGmcpMapWindow(HWND owner);
void ShowGmcpInfoWindow(HWND owner);
void CloseGmcpWindows();

// 메인창 폰트/색상 변경, 이동/크기 변경과 GMCP 보조창을 동기화합니다.
void RefreshGmcpWindowStyles();
void RefreshGmcpInfoWindowContent();
void SyncGmcpWindowsToMain();

// 환경설정의 GMCP 모듈 표시 선택을 config.ini에 저장/복원합니다.
void LoadGmcpDisplaySettings();
void SaveGmcpDisplaySettings();
