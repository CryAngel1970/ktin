#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "numpad.h"
#include "resource.h"
#include "settings.h"

// 키 이름 배열 (UI 표시용)
const wchar_t* kNpNames[15] = {
    L"숫자 0", L"숫자 1", L"숫자 2", L"숫자 3", L"숫자 4",
    L"숫자 5", L"숫자 6", L"숫자 7", L"숫자 8", L"숫자 9",
    L"나누기 ( / )", L"곱하기 ( * )", L"빼기 ( - )", L"더하기 ( + )", L"점 ( . )"
};

// 기본 매크로 명령어 (파일에 값이 없을 때 사용)
const wchar_t* kDefaultCmds[15] = {
    L"소지품", L"남서", L"남", L"남동", L"서",
    L"봐", L"동", L"북서", L"북", L"북동",
    L"누구", L"정보", L"위", L"밑", L"점수"
};

// ==============================================
// 설정 로드/저장
// ==============================================
void SaveNumpadSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();

    WritePrivateProfileStringW(L"Numpad", L"Enabled",
        g_app->numpadMacroEnabled ? L"1" : L"0", path.c_str());

    for (int i = 0; i < 15; ++i)
    {
        wchar_t key[32];
        wsprintfW(key, L"Macro_%d", i);
        WritePrivateProfileStringW(L"Numpad", key,
            g_app->numpadMacros[i].c_str(), path.c_str());
    }
}

void LoadNumpadSettings()
{
    if (!g_app) return;
    std::wstring path = GetSettingsPath();

    g_app->numpadMacroEnabled = GetPrivateProfileIntW(L"Numpad", L"Enabled", 1, path.c_str()) != 0;

    for (int i = 0; i < 15; ++i)
    {
        wchar_t key[32], buf[1024] = {};
        wsprintfW(key, L"Macro_%d", i);
        GetPrivateProfileStringW(L"Numpad", key, kDefaultCmds[i], buf, 1024, path.c_str());
        g_app->numpadMacros[i] = buf;
    }
}

// ==============================================
// 오른쪽 패널 업데이트
// ==============================================
void UpdateNumpadRightPanel(HWND hwnd, int idx)
{
    if (idx < 0 || idx > 14 || !g_app) return;

    std::wstring title = L"선택된 키: [ " + std::wstring(kNpNames[idx]) + L" ]";
    SetWindowTextW(GetDlgItem(hwnd, ID_NP_LBL_CURRENT), title.c_str());
    SetWindowTextW(GetDlgItem(hwnd, ID_NP_EDIT_CMD), g_app->numpadMacros[idx].c_str());

    SetFocus(GetDlgItem(hwnd, ID_NP_BTN_BASE + idx));
}

// ==============================================
// 팝업 프로시저
// ==============================================
LRESULT CALLBACK NumpadPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    NumpadDialogState* state = (NumpadDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    case WM_CREATE:
    {
        state = (NumpadDialogState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        HFONT hFont = GetPopupUIFont(hwnd);
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        // 사용 체크박스
        HWND hChk = CreateWindowExW(0, L"BUTTON",
            L"숫자 키패드 매크로 사용 (NumLock 켜짐 상태에서 작동)",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, 15, 350, 24, hwnd, (HMENU)ID_NP_CHK_ENABLE, hInst, nullptr);
        if (g_app)
            SendMessageW(hChk, BM_SETCHECK, g_app->numpadMacroEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        // 왼쪽 키패드 UI 버튼 생성
        int sx = 20, sy = 55, bw = 46, bh = 46, gap = 4;
        auto CreateBtn = [&](int id, const wchar_t* txt, int col, int row, int wMul = 1, int hMul = 1, bool disabled = false) {
            HWND b = CreateWindowExW(0, L"BUTTON", txt,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | (disabled ? WS_DISABLED : 0),
                sx + col * (bw + gap), sy + row * (bh + gap),
                bw * wMul + gap * (wMul - 1), bh * hMul + gap * (hMul - 1),
                hwnd, (HMENU)(UINT_PTR)(disabled ? 0 : ID_NP_BTN_BASE + id), hInst, nullptr);
            SendMessageW(b, WM_SETFONT, (WPARAM)hFont, TRUE);
            };

        // 키패드 버튼 배치
        CreateBtn(-1, L"Num", 0, 0, 1, 1, true);
        CreateBtn(10, L"/", 1, 0); CreateBtn(11, L"*", 2, 0); CreateBtn(12, L"-", 3, 0);
        CreateBtn(7, L"7\n↖", 0, 1); CreateBtn(8, L"8\n↑", 1, 1); CreateBtn(9, L"9\n↗", 2, 1);
        CreateBtn(13, L"+", 3, 1, 1, 2);
        CreateBtn(4, L"4\n←", 0, 2); CreateBtn(5, L"5", 1, 2); CreateBtn(6, L"6\n→", 2, 2);
        CreateBtn(1, L"1\n↙", 0, 3); CreateBtn(2, L"2\n↓", 1, 3); CreateBtn(3, L"3\n↘", 2, 3);
        CreateBtn(-1, L"Ent", 3, 3, 1, 2, true);
        CreateBtn(0, L"0", 0, 4, 2, 1);
        CreateBtn(14, L".", 2, 4);

        // 오른쪽 패널
        int rx = 240;
        CreateWindowExW(0, L"STATIC", L"선택된 키: [ 8 ]", WS_CHILD | WS_VISIBLE,
            rx, 60, 200, 20, hwnd, (HMENU)ID_NP_LBL_CURRENT, hInst, nullptr);
        CreateWindowExW(0, L"STATIC", L"전송할 명령어:", WS_CHILD | WS_VISIBLE,
            rx, 100, 100, 20, hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            rx, 120, 220, 24, hwnd, (HMENU)ID_NP_EDIT_CMD, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"이 키에 저장(&S)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            rx, 155, 220, 32, hwnd, (HMENU)ID_NP_BTN_SAVE_CMD, hInst, nullptr);

        // 안내 문구
        CreateWindowExW(0, L"STATIC",
            L"※ 방향키(6,4,2,8) 등에 동,서,남,북 등을",
            WS_CHILD | WS_VISIBLE,
            rx, 210, 230, 18,
            hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(0, L"STATIC",
            L" 넣어두면 이동이 매우 편해집니다.",
            WS_CHILD | WS_VISIBLE,
            rx, 230, 230, 18,
            hwnd, nullptr, hInst, nullptr);
        CreateWindowExW(0, L"STATIC", L"※ NumLock을 켠 상태에서 작동합니다.",
            WS_CHILD | WS_VISIBLE, rx, 260, 230, 18, hwnd, nullptr, hInst, nullptr);

        // 닫기 버튼
        CreateWindowExW(0, L"BUTTON", L"창 닫기(&C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            370, 310, 90, 30, hwnd, (HMENU)IDCANCEL, hInst, nullptr);

        EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL {
            SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE;
            }, (LPARAM)hFont);

        UpdateNumpadRightPanel(hwnd, state->currentIndex);
        return 0;
    }

    // ★★★ ALT 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 's') {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_NP_BTN_SAVE_CMD, BN_CLICKED), 0);
            return 0;
        }
        if (ch == 'c') {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        // 키패드 버튼 클릭
        if (id >= ID_NP_BTN_BASE && id <= ID_NP_BTN_BASE + 14)
        {
            int newIdx = id - ID_NP_BTN_BASE;
            if (state->currentIndex != newIdx)
            {
                state->currentIndex = newIdx;
                UpdateNumpadRightPanel(hwnd, newIdx);
            }
            return 0;
        }
        // 저장 버튼
        if (id == ID_NP_BTN_SAVE_CMD)
        {
            wchar_t buf[1024] = {};
            GetWindowTextW(GetDlgItem(hwnd, ID_NP_EDIT_CMD), buf, 1024);
            if (g_app)
                g_app->numpadMacros[state->currentIndex] = Trim(buf);
            MessageBeep(MB_OK);
            SetFocus(GetDlgItem(hwnd, ID_NP_EDIT_CMD));
            return 0;
        }
        // 닫기
        if (id == IDCANCEL || id == IDOK)
        {
            if (g_app)
            {
                wchar_t buf[1024] = {};
                GetWindowTextW(GetDlgItem(hwnd, ID_NP_EDIT_CMD), buf, 1024);
                g_app->numpadMacros[state->currentIndex] = Trim(buf);
                g_app->numpadMacroEnabled = (SendMessageW(GetDlgItem(hwnd, ID_NP_CHK_ENABLE),
                    BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            SaveNumpadSettings();
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:
        SendMessageW(hwnd, WM_COMMAND, IDCANCEL, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// 외부 호출 함수
// ==============================================
void PromptNumpadDialog(HWND owner)
{
    const wchar_t kClass[] = L"TTGuiNumpadPopupClass";
    static bool reg = false;

    if (!reg)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = NumpadPopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        reg = true;
    }

    NumpadDialogState state;

    RECT rcOwner = {};
    if (owner) GetWindowRect(owner, &rcOwner);

    int dlgW = 490, dlgH = 390;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClass,
        L"숫자 키패드 매크로 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, dlgW, dlgH,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        &state);

    ApplyPopupTitleBarTheme(hwnd);
    EnableWindow(owner, FALSE);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
}