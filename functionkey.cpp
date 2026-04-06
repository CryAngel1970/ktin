#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "functionkey.h"
#include "shortcut_bar.h"
#include "settings.h"
#include "resource.h"

// ==============================================
// 기능키 설정 로드/저장
// ==============================================
void LoadFunctionKeySettings()
{
    std::wstring path = GetSessionsPath();

    for (int i = 0; i < 48; ++i)
    {
        ShortcutKeyBinding& sc = g_shortcuts[i];

        std::wstring name = GetShortcutName(sc.vk, sc.mod);
        std::wstring keyEnabled = name + L"_Enabled";
        std::wstring keyCommand = name + L"_Command";

        sc.enabled = (GetPrivateProfileIntW(L"FunctionKeys", keyEnabled.c_str(), 
                      sc.enabled ? 1 : 0, path.c_str()) != 0);

        wchar_t buf[512] = {};
        GetPrivateProfileStringW(L"FunctionKeys", keyCommand.c_str(), L"", buf, 512, path.c_str());
        lstrcpynW(sc.command, buf, 512);

        // 예약 키는 강제로 비활성화
        if (sc.reserved)
        {
            sc.enabled = false;
            sc.command[0] = 0;
        }
    }
}

void SaveFunctionKeySettings()
{
    std::wstring path = GetSessionsPath();

    for (int i = 0; i < 48; ++i)
    {
        const ShortcutKeyBinding& sc = g_shortcuts[i];

        std::wstring name = GetShortcutName(sc.vk, sc.mod);
        std::wstring keyEnabled = name + L"_Enabled";
        std::wstring keyCommand = name + L"_Command";

        WritePrivateProfileStringW(L"FunctionKeys", keyEnabled.c_str(),
            sc.enabled ? L"1" : L"0", path.c_str());

        WritePrivateProfileStringW(L"FunctionKeys", keyCommand.c_str(),
            sc.command, path.c_str());
    }
}

// ==============================================
// 기능키 입력 처리
// ==============================================
void ExecuteShortcut(const wchar_t* cmd) { // static 지우기
    if (!cmd || !*cmd) return;
    SendTextToMud(std::wstring(cmd)); // std::wstring() 으로 감싸주기!
}

bool HandleFunctionKey(int vk)
{
    if (vk < VK_F1 || vk > VK_F12)
        return false;

    int mod = GetShortcutModState();

    // F4 단독은 시스템 예약 (창 닫기 등)으로 제외
    if (vk == VK_F4 && mod == SCMOD_NONE)
        return false;

    ShortcutKeyBinding* sc = FindShortcut(vk, mod);
    if (!sc || sc->reserved || !sc->enabled || sc->command[0] == 0)
        return false;

    ExecuteShortcut(sc->command);
    return true;
}