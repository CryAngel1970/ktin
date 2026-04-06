#pragma once
#include "constants.h"
#include "types.h"

// 단축키 구조체 및 배열 선언
struct ShortcutKeyBinding {
    int vk;
    int mod;
    bool reserved;
    bool enabled;
    wchar_t command[512];
};

extern ShortcutKeyBinding g_shortcuts[48];

// 함수 선언
void ExecuteShortcut(const wchar_t* command);
ShortcutKeyBinding* FindShortcut(int vk, int mod);
int GetShortcutModState();
std::wstring GetShortcutName(int vk, int mod);
bool HandleFunctionKey(int vk);
void LoadFunctionKeySettings();
void SaveFunctionKeySettings();