#pragma once

#include <windows.h>
#include <string>

// 대화상자 호출 함수
bool PromptAbbreviationDialog(HWND owner);
bool PromptAbbreviationItemEdit(HWND owner, std::wstring& shortText, std::wstring& replaceText, bool isEdit);

// 줄임말 변환 코어 함수
bool TryExpandAbbreviation(const std::wstring& input, std::wstring& output);

// 설정 파일 I/O
void LoadAbbreviationSettings();
void SaveAbbreviationSettings();