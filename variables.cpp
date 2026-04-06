#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "variables.h"
#include "resource.h"
#include "settings.h"
#include <commctrl.h>

// ==============================================
// 전역 변수 (extern 선언)
// ==============================================
bool variableGlobalEnabled = true;
std::vector<VariableItem> variables;

// ==============================================
// 내부 헬퍼 함수 (static)
// ==============================================
void RefreshVariableList(HWND hList, const std::vector<VariableItem>& items);

LRESULT CALLBACK VarItemEditPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK VariablePopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool PromptVariableItemEdit(HWND owner, std::wstring& name, std::wstring& value, int& type, bool isEdit);

// ==============================================
// 설정 저장 / 로드
// ==============================================
void SaveVariableSettings() {
    std::wstring ini = GetSessionsPath();
    WritePrivateProfileStringW(L"Variable", L"GlobalEnabled", g_app && g_app->variableGlobalEnabled ? L"1" : L"0", ini.c_str());
    int count = g_app ? (int)g_app->variables.size() : 0;
    wchar_t numBuf[32] = {}; swprintf_s(numBuf, L"%d", count);
    WritePrivateProfileStringW(L"Variable", L"Count", numBuf, ini.c_str());

    for (int i = 0; i < count; ++i) {
        wchar_t key[64] = {};
        swprintf_s(key, L"Enabled%d", i);
        WritePrivateProfileStringW(L"Variable", key, g_app->variables[i].enabled ? L"1" : L"0", ini.c_str());
        swprintf_s(key, L"Type%d", i);
        swprintf_s(numBuf, L"%d", g_app->variables[i].type);
        WritePrivateProfileStringW(L"Variable", key, numBuf, ini.c_str());
        swprintf_s(key, L"Name%d", i);
        WritePrivateProfileStringW(L"Variable", key, g_app->variables[i].name.c_str(), ini.c_str());
        swprintf_s(key, L"Value%d", i);
        WritePrivateProfileStringW(L"Variable", key, g_app->variables[i].value.c_str(), ini.c_str());
    }
    for (int i = count; i < 1000; ++i) {
        wchar_t key[64] = {};
        swprintf_s(key, L"Enabled%d", i); WritePrivateProfileStringW(L"Variable", key, nullptr, ini.c_str());
        swprintf_s(key, L"Type%d", i); WritePrivateProfileStringW(L"Variable", key, nullptr, ini.c_str());
        swprintf_s(key, L"Name%d", i); WritePrivateProfileStringW(L"Variable", key, nullptr, ini.c_str());
        swprintf_s(key, L"Value%d", i); WritePrivateProfileStringW(L"Variable", key, nullptr, ini.c_str());
    }
}

void LoadVariableSettings() {
    if (!g_app) return;
    std::wstring ini = GetSessionsPath();
    g_app->variableGlobalEnabled = GetPrivateProfileIntW(L"Variable", L"GlobalEnabled", 1, ini.c_str()) != 0;
    int count = GetPrivateProfileIntW(L"Variable", L"Count", 0, ini.c_str());
    g_app->variables.clear(); g_app->variables.reserve(count);

    for (int i = 0; i < count; ++i) {
        wchar_t key[64] = {}, nameBuf[1024] = {}, valBuf[2048] = {};
        VariableItem item;
        swprintf_s(key, L"Enabled%d", i); item.enabled = GetPrivateProfileIntW(L"Variable", key, 1, ini.c_str()) != 0;
        swprintf_s(key, L"Type%d", i); item.type = GetPrivateProfileIntW(L"Variable", key, 0, ini.c_str());
        swprintf_s(key, L"Name%d", i); GetPrivateProfileStringW(L"Variable", key, L"", nameBuf, 1024, ini.c_str());
        swprintf_s(key, L"Value%d", i); GetPrivateProfileStringW(L"Variable", key, L"", valBuf, 2048, ini.c_str());
        item.name = nameBuf; item.value = valBuf;
        if (!item.name.empty()) g_app->variables.push_back(item);
    }
}

void RefreshVariableList(HWND hList, const std::vector<VariableItem>& items) {
    ListView_DeleteAllItems(hList);
    for (int i = 0; i < (int)items.size(); ++i) {
        LVITEMW lvi = {}; lvi.mask = LVIF_TEXT; lvi.iItem = i;
        lvi.pszText = (LPWSTR)items[i].name.c_str();
        ListView_InsertItem(hList, &lvi);

        const wchar_t* typeStr = L"문자열";
        if (items[i].type == 1) typeStr = L"숫자";
        else if (items[i].type == 2) typeStr = L"참/거짓";
        else if (items[i].type == 3) typeStr = L"리스트";

        ListView_SetItemText(hList, i, 1, (LPWSTR)typeStr);
        ListView_SetItemText(hList, i, 2, (LPWSTR)items[i].value.c_str());
        ListView_SetCheckState(hList, i, items[i].enabled ? TRUE : FALSE);
    }
}


// ==============================================
// 변수 아이템 편집 팝업
// ==============================================
LRESULT CALLBACK VarItemEditPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    VarItemEditState* state = (VarItemEditState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_SYSCHAR:  // ← ALT 단축키 처리 추가
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'o') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            if (state && state->name && state->value && state->type) {
                wchar_t nameBuf[1024] = {}, valBuf[2048] = {};
                GetWindowTextW(GetDlgItem(hwnd, ID_VAR_EDIT_NAME), nameBuf, 1024);
                GetWindowTextW(GetDlgItem(hwnd, ID_VAR_EDIT_VALUE), valBuf, 2048);
                *state->name = nameBuf; *state->value = valBuf;
                *state->type = (int)SendMessageW(GetDlgItem(hwnd, ID_VAR_EDIT_TYPE), CB_GETCURSEL, 0, 0);
                state->accepted = true;
            }
            DestroyWindow(hwnd); return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd); return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT); return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PromptVariableItemEdit(HWND owner, std::wstring& name, std::wstring& value, int& type, bool isEdit) {
    const wchar_t kClass[] = L"TTGuiVarItemEditClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = VarItemEditPopupProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }
    VarItemEditState state = { &name, &value, &type, false };
    RECT rcOwner; GetWindowRect(owner, &rcOwner);
    int w = 450, h = 210;
    int x = rcOwner.left + (rcOwner.right - rcOwner.left - w) / 2;
    int y = rcOwner.top + (rcOwner.bottom - rcOwner.top - h) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, isEdit ? L"변수 수정" : L"변수 추가",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return false;
    ApplyPopupTitleBarTheme(hwnd);
    CreateWindowExW(0, L"STATIC", L"자료형:", WS_CHILD | WS_VISIBLE, 15, 20, 60, 20, hwnd, nullptr, nullptr, nullptr);
    HWND hType = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
        80, 16, 150, 100, hwnd, (HMENU)ID_VAR_EDIT_TYPE, nullptr, nullptr);
    SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"문자열 (String)");
    SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"숫자 (Number)");
    SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"참/거짓 (Boolean)");
    SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"리스트 (List)");
    SendMessageW(hType, CB_SETCURSEL, type, 0);
    CreateWindowExW(0, L"STATIC", L"변수명:", WS_CHILD | WS_VISIBLE, 15, 55, 60, 20, hwnd, nullptr, nullptr, nullptr);
    HWND hName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", name.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        80, 51, 150, 24, hwnd, (HMENU)ID_VAR_EDIT_NAME, nullptr, nullptr);
    CreateWindowExW(0, L"STATIC", L"변수값:", WS_CHILD | WS_VISIBLE, 15, 90, 60, 20, hwnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        80, 86, 315, 24, hwnd, (HMENU)ID_VAR_EDIT_VALUE, nullptr, nullptr);
    // ★ 단축키 추가
    CreateWindowExW(0, L"BUTTON", L"확인(&O)", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 245, 125, 80, 28, hwnd, (HMENU)IDOK, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"취소(&C)", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 335, 125, 80, 28, hwnd, (HMENU)IDCANCEL, nullptr, nullptr);
    HFONT hFont = GetPopupUIFont(hwnd);
    EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
    EnableWindow(owner, FALSE); SetFocus(hName); SendMessageW(hName, EM_SETSEL, 0, -1);
    MSG msg; while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return state.accepted;
}

// ==============================================
// 변수 대화상자
// ==============================================
LRESULT CALLBACK VariablePopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    VariableDialogState* state = (VariableDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lParam)->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    case WM_CREATE: {
        state = (VariableDialogState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        HFONT hFont = GetPopupUIFont(hwnd); HINSTANCE hInst = GetModuleHandleW(nullptr);
        CreateWindowExW(0, L"BUTTON", L"변수 전역 사용", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 15, 15, 150, 24, hwnd, (HMENU)ID_VAR_GLOBAL_ENABLE, hInst, nullptr);
        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            15, 50, 710, 320, hwnd, (HMENU)ID_VAR_LIST, hInst, nullptr);
        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
        LVCOLUMNW col = {}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        col.pszText = (LPWSTR)L"변수명"; col.cx = 120; col.iSubItem = 0; ListView_InsertColumn(hList, 0, &col);
        col.pszText = (LPWSTR)L"자료형"; col.cx = 100; col.iSubItem = 1; ListView_InsertColumn(hList, 1, &col);
        col.pszText = (LPWSTR)L"변수값"; col.cx = 466; col.iSubItem = 2; ListView_InsertColumn(hList, 2, &col);
        // ★ 단축키 추가
        CreateWindowExW(0, L"BUTTON", L"추가(&A)", WS_CHILD | WS_VISIBLE, 15, 382, 70, 28, hwnd, (HMENU)ID_VAR_ADD, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"삭제(&D)", WS_CHILD | WS_VISIBLE, 90, 382, 70, 28, hwnd, (HMENU)ID_VAR_DELETE, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"적용 (서버 전송)(&Y)", WS_CHILD | WS_VISIBLE, 500, 382, 160, 28, hwnd, (HMENU)ID_VAR_APPLY, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"닫기(&C)", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 670, 382, 55, 28, hwnd, (HMENU)IDCANCEL, hInst, nullptr);
        EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
        if (state && state->globalEnabled) SendMessageW(GetDlgItem(hwnd, ID_VAR_GLOBAL_ENABLE), BM_SETCHECK, *state->globalEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        if (state && state->items) RefreshVariableList(hList, *state->items);
        return 0;
    }

                  // ★★★ ALT 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'a') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_VAR_ADD, BN_CLICKED), 0); return 0; }
        if (ch == 'd') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_VAR_DELETE, BN_CLICKED), 0); return 0; }
        if (ch == 'y') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(ID_VAR_APPLY, BN_CLICKED), 0); return 0; }
        if (ch == 'c') { SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0); return 0; }
        break;
    }

    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->idFrom == ID_VAR_LIST && hdr->code == NM_DBLCLK) {
            HWND hList = GetDlgItem(hwnd, ID_VAR_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && state && state->items && sel < (int)state->items->size()) {
                std::wstring n = (*state->items)[sel].name; std::wstring v = (*state->items)[sel].value; int t = (*state->items)[sel].type;
                if (PromptVariableItemEdit(hwnd, n, v, t, true)) {
                    (*state->items)[sel].name = n; (*state->items)[sel].value = v; (*state->items)[sel].type = t;
                    RefreshVariableList(hList, *state->items);
                    ListView_SetItemState(hList, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_VAR_ADD && state && state->items) {
            std::wstring n, v; int t = 0;
            if (PromptVariableItemEdit(hwnd, n, v, t, false)) {
                VariableItem item; item.enabled = true; item.name = n; item.value = v; item.type = t;
                state->items->push_back(item);
                RefreshVariableList(GetDlgItem(hwnd, ID_VAR_LIST), *state->items);
            }
            return 0;
        }
        else if (id == ID_VAR_DELETE && state && state->items) {
            HWND hList = GetDlgItem(hwnd, ID_VAR_LIST);
            int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)state->items->size()) {
                state->items->erase(state->items->begin() + sel);
                RefreshVariableList(hList, *state->items);
            }
            return 0;
        }
        else if (id == ID_VAR_APPLY) {
            if (state && state->globalEnabled) *state->globalEnabled = (SendMessageW(GetDlgItem(hwnd, ID_VAR_GLOBAL_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveVariableSettings();
            ApplyVariablesToMud();
            MessageBoxW(hwnd, L"변수 설정이 저장되고 서버로 전송되었습니다.", L"알림", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        else if (id == IDCANCEL) {
            if (state && state->globalEnabled) *state->globalEnabled = (SendMessageW(GetDlgItem(hwnd, ID_VAR_GLOBAL_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
            SaveVariableSettings();
            DestroyWindow(hwnd); return 0;
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool PromptVariableDialog(HWND owner) {
    const wchar_t kClass[] = L"TTGuiVariablePopupClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = {}; wc.lpfnWndProc = VariablePopupProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }
    VariableDialogState state;
    state.globalEnabled = &g_app->variableGlobalEnabled; state.items = &g_app->variables; state.accepted = false;
    RECT rcOwner = {}; if (owner) GetWindowRect(owner, &rcOwner);
    int dlgW = 760, dlgH = 470;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;
    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"변수 설정",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, x, y, dlgW, dlgH, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!hwnd) return false;
    ApplyPopupTitleBarTheme(hwnd);
    EnableWindow(owner, FALSE);
    MSG msg; while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    EnableWindow(owner, TRUE); SetForegroundWindow(owner);
    return state.accepted;
}

void ApplyVariablesToMud() {
    if (!g_app) return;
    if (!g_app->variableGlobalEnabled) return;

    for (const auto& var : g_app->variables) {
        if (!var.enabled || var.name.empty()) continue;
        std::wstring cmd;
        if (var.type == 0 || var.type == 1) { // 문자열 또는 숫자
            cmd = L"#variable {" + var.name + L"} {" + var.value + L"}";
        }
        else if (var.type == 2) { // 참/거짓 (Boolean)
            std::wstring bVal = (var.value == L"true" || var.value == L"1" || var.value == L"참") ? L"1" : L"0";
            cmd = L"#variable {" + var.name + L"} {" + bVal + L"}";
        }
        else if (var.type == 3) { // 리스트
            cmd = L"#list {" + var.name + L"} {create} {" + var.value + L"}";
        }
        SendRawCommandToMud(cmd);
    }
}
