#pragma once

#include <windows.h>

// 현재 주소록 세션의 TinTin 스크립트(없으면 자동 생성)를 대상으로
// 폴더/트리거를 편집한다. 주소록 세션이 없으면 ktin_triggers.tin을 사용한다.
void PromptTriggerEditor(HWND owner);

// TinTin++가 main.tin을 읽은 직후, 전역 기본 트리거 파일이 있으면
// ConPTY 입력으로 #read 명령을 보내 로드한다.
void LoadDefaultTriggerScriptIfPresent();
