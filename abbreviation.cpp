#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "abbreviation.h"
#include "settings.h"

#include <vector>
#include <string>
#include <algorithm>
#include <commctrl.h>

struct AbbreviationItemEditState
{
    std::wstring* shortText = nullptr;
    std::wstring* replaceText = nullptr;
    bool accepted = false;
};

struct AbbreviationDialogState
{
    std::vector<AbbreviationItem> workingItems;
    bool workingGlobalEnabled = true;

    bool committed = false;
    int sortColumn = 0;
    bool sortAscending = true;
};

static LRESULT CALLBACK AbbreviationPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK AbbrItemEditPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 유틸리티 함수
static int GetSelectedAbbreviationIndex(HWND hList)
{
    if (!hList)
        return -1;
    return ListView_GetNextItem(hList, -1, LVNI_SELECTED);
}

static void UpdateAbbreviationButtons(HWND hwnd)
{
    HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
    int sel = GetSelectedAbbreviationIndex(hList);
    int count = hList ? ListView_GetItemCount(hList) : 0;
    bool hasSel = (sel >= 0);

    EnableWindow(GetDlgItem(hwnd, ID_ABBR_DELETE), hasSel);
    EnableWindow(GetDlgItem(hwnd, ID_ABBR_UP), hasSel && sel > 0);
    EnableWindow(GetDlgItem(hwnd, ID_ABBR_DOWN), hasSel && sel >= 0 && sel < count - 1);
}

static void RefreshAbbreviationList(HWND hList, const std::vector<AbbreviationItem>& items)
{
    if (!hList)
        return;

    ListView_DeleteAllItems(hList);

    for (int i = 0; i < (int)items.size(); ++i)
    {
        LVITEMW lvi = {};
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = const_cast<LPWSTR>(items[i].shortText.c_str());
        ListView_InsertItem(hList, &lvi);

        ListView_SetItemText(hList, i, 1, const_cast<LPWSTR>(items[i].replaceText.c_str()));
        ListView_SetCheckState(hList, i, items[i].enabled ? TRUE : FALSE);
    }
}

// ★ 최적화: UI의 체크 상태를 메모리(workingItems)로 동기화
static void SyncWorkingItemsFromList(HWND hwnd, AbbreviationDialogState* state)
{
    if (!hwnd || !state)
        return;

    HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
    if (!hList)
        return;

    int count = ListView_GetItemCount(hList);
    int limit = min(count, (int)state->workingItems.size());

    for (int i = 0; i < limit; ++i)
    {
        state->workingItems[i].enabled = ListView_GetCheckState(hList, i) ? true : false;
    }
}

static void SortAbbreviations(std::vector<AbbreviationItem>& items, int column, bool ascending)
{
    std::stable_sort(items.begin(), items.end(),
        [column, ascending](const AbbreviationItem& a, const AbbreviationItem& b)
        {
            const std::wstring& lhs = (column == 0) ? a.shortText : a.replaceText;
            const std::wstring& rhs = (column == 0) ? b.shortText : b.replaceText;

            int cmp = _wcsicmp(lhs.c_str(), rhs.c_str());
            return ascending ? (cmp < 0) : (cmp > 0);
        });
}

// ★ 로직 수정: 실제 전역 변수에 반영하고 파일로 저장하는 핵심 함수
static void CommitAbbreviationDialog(HWND hwnd, AbbreviationDialogState* state)
{
    if (!g_app || !state)
        return;

    // 저장하기 직전에 화면의 체크 상태를 확실하게 읽어옴
    SyncWorkingItemsFromList(hwnd, state);

    HWND hGlobal = GetDlgItem(hwnd, ID_ABBR_GLOBAL_ENABLE);
    if (hGlobal)
    {
        state->workingGlobalEnabled = (SendMessageW(hGlobal, BM_GETCHECK, 0, 0) == BST_CHECKED);
    }

    g_app->abbreviationGlobalEnabled = state->workingGlobalEnabled;
    g_app->abbreviations = state->workingItems;

    SaveAbbreviationSettings();
    state->committed = true;
}

// 설정 로드/저장
void LoadAbbreviationSettings()
{
    if (!g_app)
        return;

    std::wstring ini = GetSessionsPath();

    g_app->abbreviationGlobalEnabled =
        GetPrivateProfileIntW(L"Abbreviation", L"GlobalEnabled", 1, ini.c_str()) != 0;

    int count = GetPrivateProfileIntW(L"Abbreviation", L"Count", 0, ini.c_str());
    if (count < 0) count = 0;
    if (count > 1000) count = 1000;

    g_app->abbreviations.clear();
    g_app->abbreviations.reserve(count);

    for (int i = 0; i < count; ++i)
    {
        wchar_t key[64] = {};
        wchar_t shortBuf[1024] = {};
        wchar_t replaceBuf[2048] = {};

        AbbreviationItem item;

        swprintf_s(key, L"Enabled%d", i);
        item.enabled = GetPrivateProfileIntW(L"Abbreviation", key, 1, ini.c_str()) != 0;

        swprintf_s(key, L"Short%d", i);
        GetPrivateProfileStringW(L"Abbreviation", key, L"", shortBuf, (DWORD)_countof(shortBuf), ini.c_str());

        swprintf_s(key, L"Replace%d", i);
        GetPrivateProfileStringW(L"Abbreviation", key, L"", replaceBuf, (DWORD)_countof(replaceBuf), ini.c_str());

        item.shortText = shortBuf;
        item.replaceText = replaceBuf;

        if (!Trim(item.shortText).empty())
            g_app->abbreviations.push_back(item);
    }
}

void SaveAbbreviationSettings()
{
    std::wstring ini = GetSessionsPath();

    WritePrivateProfileStringW(
        L"Abbreviation", L"GlobalEnabled",
        (g_app && g_app->abbreviationGlobalEnabled) ? L"1" : L"0", ini.c_str());

    int count = g_app ? (int)g_app->abbreviations.size() : 0;
    if (count < 0) count = 0;
    if (count > 1000) count = 1000;

    wchar_t numBuf[32] = {};
    swprintf_s(numBuf, L"%d", count);
    WritePrivateProfileStringW(L"Abbreviation", L"Count", numBuf, ini.c_str());

    for (int i = 0; i < count; ++i)
    {
        wchar_t key[64] = {};

        swprintf_s(key, L"Enabled%d", i);
        WritePrivateProfileStringW(L"Abbreviation", key,
            g_app->abbreviations[i].enabled ? L"1" : L"0", ini.c_str());

        swprintf_s(key, L"Short%d", i);
        WritePrivateProfileStringW(L"Abbreviation", key,
            g_app->abbreviations[i].shortText.c_str(), ini.c_str());

        swprintf_s(key, L"Replace%d", i);
        WritePrivateProfileStringW(L"Abbreviation", key,
            g_app->abbreviations[i].replaceText.c_str(), ini.c_str());
    }

    // 찌꺼기 정리
    for (int i = count; i < 1000; ++i)
    {
        wchar_t key[64] = {};
        swprintf_s(key, L"Enabled%d", i);
        WritePrivateProfileStringW(L"Abbreviation", key, nullptr, ini.c_str());
        swprintf_s(key, L"Short%d", i);
        WritePrivateProfileStringW(L"Abbreviation", key, nullptr, ini.c_str());
        swprintf_s(key, L"Replace%d", i);
        WritePrivateProfileStringW(L"Abbreviation", key, nullptr, ini.c_str());
    }
}

// 대화상자 관련
bool PromptAbbreviationDialog(HWND owner)
{
    const wchar_t kClassName[] = L"TTGuiAbbreviationPopupClass";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = AbbreviationPopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    AbbreviationDialogState state;
    if (g_app)
    {
        state.workingGlobalEnabled = g_app->abbreviationGlobalEnabled;
        state.workingItems = g_app->abbreviations;
    }

    RECT rcOwner = {};
    if (owner) GetWindowRect(owner, &rcOwner);

    const int dlgW = 760;
    const int dlgH = 470;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - dlgW) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - dlgH) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClassName, L"줄임말 설정",
        // 기존: WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, // ★ WS_OVERLAPPED로 수정
        x, y, dlgW, dlgH, owner, nullptr, GetModuleHandleW(nullptr), &state);

    if (!hwnd) return false;

    ApplyPopupTitleBarTheme(hwnd);

    if (owner) EnableWindow(owner, FALSE);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (owner)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    return state.committed;
}

bool PromptAbbreviationItemEdit(HWND owner, std::wstring& shortText, std::wstring& replaceText, bool isEdit)
{
    const wchar_t kClassName[] = L"TTGuiAbbrItemEditPopupClass";
    static bool s_registered = false;

    if (!s_registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = AbbrItemEditPopupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        s_registered = true;
    }

    AbbreviationItemEditState state;
    state.shortText = &shortText;
    state.replaceText = &replaceText;
    state.accepted = false;

    RECT rcOwner = {};
    if (owner) GetWindowRect(owner, &rcOwner);

    int w = 450;
    int h = 180;
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;

    const wchar_t* titleText = isEdit ? L"줄임말 수정" : L"줄임말 추가";

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClassName, titleText,
        // 기존: WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, // ★ WS_OVERLAPPED로 수정
        x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), &state);

    if (!hwnd) return false;

    ApplyPopupTitleBarTheme(hwnd);

    CreateWindowExW(0, L"STATIC", L"줄임말:", WS_CHILD | WS_VISIBLE, 15, 20, 60, 20, hwnd, nullptr, nullptr, nullptr);
    HWND hShort = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", shortText.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        80, 16, 120, 24, hwnd, (HMENU)(UINT_PTR)ID_ABBR_EDIT_SHORT, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"전송할 명령:", WS_CHILD | WS_VISIBLE, 15, 55, 80, 20, hwnd, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", replaceText.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        100, 51, 315, 24, hwnd, (HMENU)(UINT_PTR)ID_ABBR_EDIT_REPLACE, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        245, 95, 80, 28, hwnd, (HMENU)(UINT_PTR)IDOK, nullptr, nullptr);
    CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        335, 95, 80, 28, hwnd, (HMENU)(UINT_PTR)IDCANCEL, nullptr, nullptr);

    HFONT hFont = GetPopupUIFont(hwnd);
    EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);

    if (owner) EnableWindow(owner, FALSE);

    SetFocus(hShort);
    SendMessageW(hShort, EM_SETSEL, 0, -1);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    if (owner)
    {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    return state.accepted;
}

bool TryExpandAbbreviation(const std::wstring& input, std::wstring& output)
{
    output = input;

    if (!g_app || !g_app->abbreviationGlobalEnabled)
        return false;

    std::wstring trimmed = Trim(input);
    if (trimmed.empty())
        return false;

    for (const auto& item : g_app->abbreviations)
    {
        if (!item.enabled)
            continue;

        std::wstring key = Trim(item.shortText);
        if (key.empty())
            continue;

        if (_wcsicmp(trimmed.c_str(), key.c_str()) == 0)
        {
            output = item.replaceText;
            return true;
        }
    }
    return false;
}

// 메인 다이얼로그 윈도우 프로시저
static LRESULT CALLBACK AbbreviationPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AbbreviationDialogState* state = (AbbreviationDialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_CREATE:
    {
        state = (AbbreviationDialogState*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        HFONT hFont = GetPopupUIFont(hwnd);
        HINSTANCE hInst = GetModuleHandleW(nullptr);

        CreateWindowExW(0, L"BUTTON", L"전체 사용", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            15, 15, 120, 24, hwnd, (HMENU)(UINT_PTR)ID_ABBR_GLOBAL_ENABLE, hInst, nullptr);

        HWND hList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
            15, 50, 710, 320, hwnd, (HMENU)(UINT_PTR)ID_ABBR_LIST, hInst, nullptr);

        ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);

        LVCOLUMNW col = {};
        col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        col.pszText = const_cast<LPWSTR>(L"    줄임말");
        col.cx = 180;
        col.iSubItem = 0;
        ListView_InsertColumn(hList, 0, &col);

        col.pszText = const_cast<LPWSTR>(L"치환하기 전 원래 문장 또는 전송할 명령");
        col.cx = 526;
        col.iSubItem = 1;
        ListView_InsertColumn(hList, 1, &col);

        CreateWindowExW(0, L"BUTTON", L"추가", WS_CHILD | WS_VISIBLE, 15, 382, 70, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_ADD, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"삭제", WS_CHILD | WS_VISIBLE, 90, 382, 70, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_DELETE, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"▲", WS_CHILD | WS_VISIBLE, 165, 382, 35, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_UP, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"▼", WS_CHILD | WS_VISIBLE, 205, 382, 35, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_DOWN, hInst, nullptr);

        CreateWindowExW(0, L"BUTTON", L"전체 체크", WS_CHILD | WS_VISIBLE, 370, 382, 85, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_CHECK_ALL, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"전체 해제", WS_CHILD | WS_VISIBLE, 460, 382, 85, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_UNCHECK_ALL, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"적용", WS_CHILD | WS_VISIBLE, 550, 382, 85, 28, hwnd, (HMENU)(UINT_PTR)ID_ABBR_APPLY, hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"닫기", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 640, 382, 85, 28, hwnd, (HMENU)(UINT_PTR)IDCANCEL, hInst, nullptr);

        EnumChildWindows(hwnd, [](HWND c, LPARAM lp) -> BOOL { SendMessageW(c, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);

        SendMessageW(GetDlgItem(hwnd, ID_ABBR_GLOBAL_ENABLE), BM_SETCHECK, state && state->workingGlobalEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        if (state) RefreshAbbreviationList(hList, state->workingItems);

        UpdateAbbreviationButtons(hwnd);
        return 0;
    }

    case WM_NOTIFY:
    {
        NMHDR* hdr = (NMHDR*)lParam;
        if (!hdr || hdr->idFrom != ID_ABBR_LIST)
            return 0;

        if (hdr->code == LVN_ITEMCHANGED)
        {
            // 여기서는 단순히 버튼 활성화 상태만 갱신합니다. (오동작의 주범이었던 자동 저장 로직 제거)
            UpdateAbbreviationButtons(hwnd);
            return 0;
        }

        if (hdr->code == LVN_COLUMNCLICK)
        {
            NMLISTVIEW* nmlv = (NMLISTVIEW*)lParam;
            HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);

            if (state && nmlv && hList)
            {
                SyncWorkingItemsFromList(hwnd, state); // 화면 상태 백업

                if (state->sortColumn == nmlv->iSubItem)
                    state->sortAscending = !state->sortAscending;
                else
                {
                    state->sortColumn = nmlv->iSubItem;
                    state->sortAscending = true;
                }

                SortAbbreviations(state->workingItems, state->sortColumn, state->sortAscending);
                RefreshAbbreviationList(hList, state->workingItems);
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;
        }

        if (hdr->code == NM_DBLCLK)
        {
            HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
            int sel = GetSelectedAbbreviationIndex(hList);

            if (state && hList && sel >= 0 && sel < (int)state->workingItems.size())
            {
                SyncWorkingItemsFromList(hwnd, state);
                std::wstring sText = state->workingItems[sel].shortText;
                std::wstring rText = state->workingItems[sel].replaceText;

                if (PromptAbbreviationItemEdit(hwnd, sText, rText, true))
                {
                    state->workingItems[sel].shortText = sText;
                    state->workingItems[sel].replaceText = rText;
                    RefreshAbbreviationList(hList, state->workingItems);
                    ListView_SetItemState(hList, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            else if (state && hList && sel < 0)
            {
                std::wstring sText, rText;
                if (PromptAbbreviationItemEdit(hwnd, sText, rText, false))
                {
                    SyncWorkingItemsFromList(hwnd, state);
                    AbbreviationItem item;
                    item.enabled = true;
                    item.shortText = sText;
                    item.replaceText = rText;
                    state->workingItems.push_back(item);

                    RefreshAbbreviationList(hList, state->workingItems);
                    int idx = (int)state->workingItems.size() - 1;
                    ListView_SetItemState(hList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;
        }

        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_ABBR_ADD:
            if (state)
            {
                SyncWorkingItemsFromList(hwnd, state);
                std::wstring sText, rText;

                if (PromptAbbreviationItemEdit(hwnd, sText, rText, false))
                {
                    AbbreviationItem item;
                    item.enabled = true;
                    item.shortText = sText;
                    item.replaceText = rText;
                    state->workingItems.push_back(item);

                    HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
                    RefreshAbbreviationList(hList, state->workingItems);
                    int idx = (int)state->workingItems.size() - 1;
                    ListView_SetItemState(hList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case ID_ABBR_DELETE:
            if (state)
            {
                SyncWorkingItemsFromList(hwnd, state);
                HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
                int sel = GetSelectedAbbreviationIndex(hList);

                if (sel >= 0 && sel < (int)state->workingItems.size())
                {
                    state->workingItems.erase(state->workingItems.begin() + sel);
                    RefreshAbbreviationList(hList, state->workingItems);

                    if (sel >= (int)state->workingItems.size())
                        sel = (int)state->workingItems.size() - 1;
                    if (sel >= 0)
                        ListView_SetItemState(hList, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case ID_ABBR_UP:
            if (state)
            {
                SyncWorkingItemsFromList(hwnd, state);
                HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
                int sel = GetSelectedAbbreviationIndex(hList);

                if (sel > 0 && sel < (int)state->workingItems.size())
                {
                    std::swap(state->workingItems[sel], state->workingItems[sel - 1]);
                    RefreshAbbreviationList(hList, state->workingItems);
                    ListView_SetItemState(hList, sel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case ID_ABBR_DOWN:
            if (state)
            {
                SyncWorkingItemsFromList(hwnd, state);
                HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
                int sel = GetSelectedAbbreviationIndex(hList);

                if (sel >= 0 && sel < (int)state->workingItems.size() - 1)
                {
                    std::swap(state->workingItems[sel], state->workingItems[sel + 1]);
                    RefreshAbbreviationList(hList, state->workingItems);
                    ListView_SetItemState(hList, sel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case ID_ABBR_CHECK_ALL:
            if (state)
            {
                SyncWorkingItemsFromList(hwnd, state); // 전체 체크 전 상태 동기화
                for (size_t i = 0; i < state->workingItems.size(); ++i)
                    state->workingItems[i].enabled = true;

                HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
                RefreshAbbreviationList(hList, state->workingItems);
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case ID_ABBR_UNCHECK_ALL:
            if (state)
            {
                SyncWorkingItemsFromList(hwnd, state); // 전체 해제 전 상태 동기화
                for (size_t i = 0; i < state->workingItems.size(); ++i)
                    state->workingItems[i].enabled = false;

                HWND hList = GetDlgItem(hwnd, ID_ABBR_LIST);
                RefreshAbbreviationList(hList, state->workingItems);
            }
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case ID_ABBR_APPLY:
            CommitAbbreviationDialog(hwnd, state); // 실제 적용
            UpdateAbbreviationButtons(hwnd);
            return 0;

        case IDOK:
            CommitAbbreviationDialog(hwnd, state); // 실제 적용 후 닫기
            DestroyWindow(hwnd);
            return 0;

        case IDCANCEL:
            // ★ 중요 수정: [취소]나 [닫기]를 누르면 저장(Commit)하지 않고 그냥 창만 닫습니다!
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CLOSE:
        // 우측 상단 X 버튼을 누를 때도 저장하지 않고 닫기
        SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// 개별 항목 추가/편집 다이얼로그 프로시저
static LRESULT CALLBACK AbbrItemEditPopupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    AbbreviationItemEditState* state = (AbbreviationItemEditState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            if (state && state->shortText && state->replaceText)
            {
                wchar_t shortBuf[1024] = {};
                wchar_t replBuf[2048] = {};

                GetWindowTextW(GetDlgItem(hwnd, ID_ABBR_EDIT_SHORT), shortBuf, (int)_countof(shortBuf));
                GetWindowTextW(GetDlgItem(hwnd, ID_ABBR_EDIT_REPLACE), replBuf, (int)_countof(replBuf));

                *state->shortText = shortBuf;
                *state->replaceText = replBuf;
                state->accepted = true;
            }
            DestroyWindow(hwnd);
            return 0;
        }
        else if (LOWORD(wParam) == IDCANCEL)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}