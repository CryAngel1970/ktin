#pragma once
#include <windows.h>
#include <string>

// 함수 선언
void ShowQuickConnectDialog(HWND owner);
void ShowChatCaptureDialog(HWND owner);
void ShowFindDialog(HWND owner);
void ShowSymbolDialog(HWND owner);
void ShowInfoPopup(HWND owner);

bool PromptShortcutEditor(HWND hwnd);
bool PromptMemoAutoSaveInterval(HWND owner, int& sec);
bool ShowLineSelectDialog(HWND owner);

LRESULT CALLBACK QuickConnectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ChatCaptureDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK FindDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK SymbolDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK InfoPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK ShortcutEditorPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK AutoSaveIntervalProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK LineSelectPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);