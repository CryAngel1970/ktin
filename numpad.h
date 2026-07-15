#pragma once
#include <windows.h>
#include <string>

// NumpadDialogState 구조체
struct NumpadDialogState {
    int currentIndex = 8;   // 기본 선택은 '8(북)'
    bool accepted = false;
};

// 숫자 키패드 매크로 설정창
void PromptNumpadDialog(HWND owner);
LRESULT CALLBACK NumpadPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void UpdateNumpadRightPanel(HWND hwnd, int idx);

// 보기 메뉴에서 여는 접근성용 화면 키패드
void ShowNumpadViewWindow(HWND owner);
void CloseNumpadViewWindow();
void RefreshNumpadViewWindow();
void SyncNumpadViewToMain();
