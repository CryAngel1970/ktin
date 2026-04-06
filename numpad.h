#pragma once
#include <windows.h>
#include <string>

// NumpadDialogState 구조체
struct NumpadDialogState {
    int currentIndex = 8;   // 기본 선택은 '8(북)'
    bool accepted = false;
};

// 함수 선언
void PromptNumpadDialog(HWND owner);

LRESULT CALLBACK NumpadPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 내부 헬퍼 함수

void UpdateNumpadRightPanel(HWND hwnd, int idx);