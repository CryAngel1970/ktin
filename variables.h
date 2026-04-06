#pragma once
#include "constants.h"
#include "types.h" // ★ 구조체 인식!

struct VariableDialogState
{
    bool* globalEnabled = nullptr;
    std::vector<VariableItem>* items = nullptr;
    bool accepted = false;
};

struct VarItemEditState
{
    std::wstring* name = nullptr;
    std::wstring* value = nullptr;
    int* type = nullptr;
    bool accepted = false;
};

// ※ g_app->variables 와 g_app->variableGlobalEnabled 로 통합되었으므로
// 기존의 extern 변수 선언은 삭제하여 충돌을 막았습니다.

// 공개 함수
void LoadVariableSettings();
void SaveVariableSettings();
void ApplyVariablesToMud();
bool PromptVariableDialog(HWND owner);