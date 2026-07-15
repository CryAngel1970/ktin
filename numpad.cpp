#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "numpad.h"
#include "resource.h"
#include "settings.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <cwctype>

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

namespace
{
    constexpr wchar_t kNumpadViewClass[] = L"KTinNumpadViewWindow";
    constexpr int kDwmExtendedFrameBounds = 9;
    constexpr int kDockSnapDistance = 48;
    constexpr int kDefaultViewWidth = 300;
    constexpr int kDefaultViewHeight = 390;
    constexpr int kMinimumViewWidth = 250;
    constexpr int kMinimumViewHeight = 320;

    enum NumpadDockSide
    {
        NumpadDockNone = 0,
        NumpadDockLeft = 1,
        NumpadDockRight = 2,
        NumpadDockTop = 3,
        NumpadDockBottom = 4
    };

    HWND g_hwndNumpadView = nullptr;
    HWND g_hwndNumpadButtons[15] = {};
    HWND g_hwndNumpadNum = nullptr;
    HWND g_hwndNumpadEnter = nullptr;
    bool g_numpadViewClassRegistered = false;
    bool g_syncingNumpadDock = false;
    int g_numpadDockSide = NumpadDockRight;
    int g_numpadDockOffset = 0;
    RECT g_numpadFloatRect = { 180, 180, 180 + kDefaultViewWidth, 180 + kDefaultViewHeight };

    struct WindowFrameInfo
    {
        RECT outer{};
        RECT visible{};
    };

    WindowFrameInfo QueryWindowFrame(HWND hwnd)
    {
        WindowFrameInfo frame{};
        if (!hwnd || !IsWindow(hwnd) || !GetWindowRect(hwnd, &frame.outer))
            return frame;

        frame.visible = frame.outer;
        using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
        static DwmGetWindowAttributeFn getDwmAttribute = []() -> DwmGetWindowAttributeFn
        {
            HMODULE module = LoadLibraryW(L"dwmapi.dll");
            if (!module)
                return nullptr;
            return reinterpret_cast<DwmGetWindowAttributeFn>(
                GetProcAddress(module, "DwmGetWindowAttribute"));
        }();

        RECT visible{};
        if (getDwmAttribute && SUCCEEDED(getDwmAttribute(
            hwnd, kDwmExtendedFrameBounds, &visible, sizeof(visible))))
        {
            frame.visible = visible;
        }
        return frame;
    }

    RECT WorkAreaForWindow(HWND hwnd)
    {
        RECT work{};
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (monitor && GetMonitorInfoW(monitor, &mi))
            return mi.rcWork;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        return work;
    }

    int VisibleWidthForOuter(HWND hwnd, int outerWidth)
    {
        WindowFrameInfo frame = QueryWindowFrame(hwnd);
        int leftInset = static_cast<int>(frame.visible.left - frame.outer.left);
        int rightInset = static_cast<int>(frame.outer.right - frame.visible.right);
        return std::max(1, outerWidth - leftInset - rightInset);
    }

    int VisibleHeightForOuter(HWND hwnd, int outerHeight)
    {
        WindowFrameInfo frame = QueryWindowFrame(hwnd);
        int topInset = static_cast<int>(frame.visible.top - frame.outer.top);
        int bottomInset = static_cast<int>(frame.outer.bottom - frame.visible.bottom);
        return std::max(1, outerHeight - topInset - bottomInset);
    }

    void PlaceWindowByVisibleTopLeft(HWND hwnd, int visibleLeft, int visibleTop,
        int outerWidth, int outerHeight)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;
        WindowFrameInfo frame = QueryWindowFrame(hwnd);
        int leftInset = static_cast<int>(frame.visible.left - frame.outer.left);
        int topInset = static_cast<int>(frame.visible.top - frame.outer.top);
        SetWindowPos(hwnd, nullptr,
            visibleLeft - leftInset, visibleTop - topInset,
            outerWidth, outerHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int ClampInt(int value, int low, int high)
    {
        if (high < low)
            return low;
        return std::max(low, std::min(high, value));
    }

    bool RangesOverlap(int a1, int a2, int b1, int b2)
    {
        return a2 >= b1 && b2 >= a1;
    }

    std::wstring NumpadButtonText(int idx)
    {
        if (!g_app || idx < 0 || idx >= 15)
            return L"";

        std::wstring text = Trim(g_app->numpadMacros[idx]);
        if (text.empty())
            return L"(없음)";

        // 버튼에는 실제 전송 명령을 보여줍니다. 여러 행 매크로라면 버튼 안에서도
        // 행을 나눠 표시하되 CRLF는 하나의 줄바꿈으로 정규화합니다.
        std::wstring normalized;
        normalized.reserve(text.size());
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r')
            {
                if (i + 1 < text.size() && text[i + 1] == L'\n')
                    ++i;
                normalized.push_back(L'\n');
            }
            else
            {
                normalized.push_back(text[i]);
            }
        }
        return normalized;
    }

    void SaveNumpadViewPlacement()
    {
        std::wstring path = GetSettingsPath();
        WritePrivateProfileStringW(L"NumpadView", L"DockSide",
            std::to_wstring(g_numpadDockSide).c_str(), path.c_str());
        WritePrivateProfileStringW(L"NumpadView", L"DockOffset",
            std::to_wstring(g_numpadDockOffset).c_str(), path.c_str());

        RECT rc = g_numpadFloatRect;
        if (g_hwndNumpadView && IsWindow(g_hwndNumpadView))
        {
            RECT current{};
            if (GetWindowRect(g_hwndNumpadView, &current))
            {
                if (g_numpadDockSide == NumpadDockNone)
                    rc = current;
                else
                {
                    // 도킹 상태에서도 사용자가 조절한 창 크기는 보존합니다.
                    rc.right = rc.left + (current.right - current.left);
                    rc.bottom = rc.top + (current.bottom - current.top);
                }
            }
        }
        g_numpadFloatRect = rc;

        WritePrivateProfileStringW(L"NumpadView", L"Left",
            std::to_wstring(rc.left).c_str(), path.c_str());
        WritePrivateProfileStringW(L"NumpadView", L"Top",
            std::to_wstring(rc.top).c_str(), path.c_str());
        WritePrivateProfileStringW(L"NumpadView", L"Width",
            std::to_wstring(std::max(kMinimumViewWidth, static_cast<int>(rc.right - rc.left))).c_str(),
            path.c_str());
        WritePrivateProfileStringW(L"NumpadView", L"Height",
            std::to_wstring(std::max(kMinimumViewHeight, static_cast<int>(rc.bottom - rc.top))).c_str(),
            path.c_str());
    }

    void LoadNumpadViewPlacement()
    {
        std::wstring path = GetSettingsPath();
        g_numpadDockSide = GetPrivateProfileIntW(
            L"NumpadView", L"DockSide", NumpadDockRight, path.c_str());
        if (g_numpadDockSide < NumpadDockNone || g_numpadDockSide > NumpadDockBottom)
            g_numpadDockSide = NumpadDockRight;

        g_numpadDockOffset = GetPrivateProfileIntW(
            L"NumpadView", L"DockOffset", 0, path.c_str());
        int left = GetPrivateProfileIntW(L"NumpadView", L"Left", 180, path.c_str());
        int top = GetPrivateProfileIntW(L"NumpadView", L"Top", 180, path.c_str());
        int width = std::max(kMinimumViewWidth,
            static_cast<int>(GetPrivateProfileIntW(
                L"NumpadView", L"Width", kDefaultViewWidth, path.c_str())));
        int height = std::max(kMinimumViewHeight,
            static_cast<int>(GetPrivateProfileIntW(
                L"NumpadView", L"Height", kDefaultViewHeight, path.c_str())));
        g_numpadFloatRect = { left, top, left + width, top + height };
    }

    void LayoutNumpadViewControls(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;

        RECT client{};
        GetClientRect(hwnd, &client);
        const int clientWidth = static_cast<int>(client.right - client.left);
        const int clientHeight = static_cast<int>(client.bottom - client.top);
        const int margin = 8;
        const int gap = 5;
        const int columnWidth = std::max(44, (clientWidth - margin * 2 - gap * 3) / 4);
        const int rowHeight = std::max(44, (clientHeight - margin * 2 - gap * 4) / 5);

        auto MoveButton = [&](HWND button, int col, int row, int colSpan = 1, int rowSpan = 1)
        {
            if (!button || !IsWindow(button))
                return;
            const int x = margin + col * (columnWidth + gap);
            const int y = margin + row * (rowHeight + gap);
            const int width = columnWidth * colSpan + gap * (colSpan - 1);
            const int height = rowHeight * rowSpan + gap * (rowSpan - 1);
            SetWindowPos(button, nullptr, x, y, width, height,
                SWP_NOZORDER | SWP_NOACTIVATE);
        };

        MoveButton(g_hwndNumpadNum, 0, 0);
        MoveButton(g_hwndNumpadButtons[10], 1, 0);
        MoveButton(g_hwndNumpadButtons[11], 2, 0);
        MoveButton(g_hwndNumpadButtons[12], 3, 0);

        MoveButton(g_hwndNumpadButtons[7], 0, 1);
        MoveButton(g_hwndNumpadButtons[8], 1, 1);
        MoveButton(g_hwndNumpadButtons[9], 2, 1);
        MoveButton(g_hwndNumpadButtons[13], 3, 1, 1, 2);

        MoveButton(g_hwndNumpadButtons[4], 0, 2);
        MoveButton(g_hwndNumpadButtons[5], 1, 2);
        MoveButton(g_hwndNumpadButtons[6], 2, 2);

        MoveButton(g_hwndNumpadButtons[1], 0, 3);
        MoveButton(g_hwndNumpadButtons[2], 1, 3);
        MoveButton(g_hwndNumpadButtons[3], 2, 3);
        MoveButton(g_hwndNumpadEnter, 3, 3, 1, 2);

        MoveButton(g_hwndNumpadButtons[0], 0, 4, 2, 1);
        MoveButton(g_hwndNumpadButtons[14], 2, 4);
    }

    void ApplyNumpadViewFont()
    {
        if (!g_hwndNumpadView || !IsWindow(g_hwndNumpadView))
            return;
        HFONT font = GetPopupUIFont(g_hwndNumpadView);
        if (g_hwndNumpadNum)
            SendMessageW(g_hwndNumpadNum, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (g_hwndNumpadEnter)
            SendMessageW(g_hwndNumpadEnter, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (HWND button : g_hwndNumpadButtons)
        {
            if (button)
                SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    void DockNumpadViewToMainInternal()
    {
        if (g_syncingNumpadDock || g_numpadDockSide == NumpadDockNone ||
            !g_hwndNumpadView || !IsWindow(g_hwndNumpadView) ||
            !g_app || !g_app->hwndMain || !IsWindow(g_app->hwndMain) ||
            IsIconic(g_app->hwndMain))
        {
            return;
        }

        g_syncingNumpadDock = true;
        WindowFrameInfo mainFrame = QueryWindowFrame(g_app->hwndMain);
        RECT work = WorkAreaForWindow(g_app->hwndMain);
        RECT keypadOuter{};
        GetWindowRect(g_hwndNumpadView, &keypadOuter);
        int outerWidth = std::max(kMinimumViewWidth,
            static_cast<int>(keypadOuter.right - keypadOuter.left));
        int outerHeight = std::max(kMinimumViewHeight,
            static_cast<int>(keypadOuter.bottom - keypadOuter.top));
        int visibleWidth = VisibleWidthForOuter(g_hwndNumpadView, outerWidth);
        int visibleHeight = VisibleHeightForOuter(g_hwndNumpadView, outerHeight);

        int visibleLeft = static_cast<int>(mainFrame.visible.left);
        int visibleTop = static_cast<int>(mainFrame.visible.top);

        if (g_numpadDockSide == NumpadDockLeft || g_numpadDockSide == NumpadDockRight)
        {
            visibleLeft = g_numpadDockSide == NumpadDockLeft
                ? static_cast<int>(mainFrame.visible.left) - visibleWidth
                : static_cast<int>(mainFrame.visible.right);
            visibleTop = static_cast<int>(mainFrame.visible.top) + g_numpadDockOffset;
            visibleTop = ClampInt(visibleTop, static_cast<int>(work.top),
                static_cast<int>(work.bottom) - visibleHeight);
        }
        else
        {
            visibleTop = g_numpadDockSide == NumpadDockTop
                ? static_cast<int>(mainFrame.visible.top) - visibleHeight
                : static_cast<int>(mainFrame.visible.bottom);
            visibleLeft = static_cast<int>(mainFrame.visible.left) + g_numpadDockOffset;
            visibleLeft = ClampInt(visibleLeft, static_cast<int>(work.left),
                static_cast<int>(work.right) - visibleWidth);
        }

        PlaceWindowByVisibleTopLeft(g_hwndNumpadView, visibleLeft, visibleTop,
            outerWidth, outerHeight);
        g_syncingNumpadDock = false;
    }

    void UpdateNumpadDockFromMovedWindow()
    {
        if (g_syncingNumpadDock || !g_hwndNumpadView || !IsWindow(g_hwndNumpadView) ||
            !g_app || !g_app->hwndMain || !IsWindow(g_app->hwndMain))
        {
            return;
        }

        WindowFrameInfo mainFrame = QueryWindowFrame(g_app->hwndMain);
        WindowFrameInfo keypadFrame = QueryWindowFrame(g_hwndNumpadView);
        bool verticalOverlap = RangesOverlap(
            static_cast<int>(keypadFrame.visible.top), static_cast<int>(keypadFrame.visible.bottom),
            static_cast<int>(mainFrame.visible.top), static_cast<int>(mainFrame.visible.bottom));
        bool horizontalOverlap = RangesOverlap(
            static_cast<int>(keypadFrame.visible.left), static_cast<int>(keypadFrame.visible.right),
            static_cast<int>(mainFrame.visible.left), static_cast<int>(mainFrame.visible.right));

        int bestDistance = INT_MAX;
        int bestSide = NumpadDockNone;
        auto Consider = [&](int side, int distance, bool allowed)
        {
            if (allowed && distance < bestDistance)
            {
                bestDistance = distance;
                bestSide = side;
            }
        };

        Consider(NumpadDockLeft,
            std::abs(static_cast<int>(keypadFrame.visible.right - mainFrame.visible.left)),
            verticalOverlap);
        Consider(NumpadDockRight,
            std::abs(static_cast<int>(keypadFrame.visible.left - mainFrame.visible.right)),
            verticalOverlap);
        Consider(NumpadDockTop,
            std::abs(static_cast<int>(keypadFrame.visible.bottom - mainFrame.visible.top)),
            horizontalOverlap);
        Consider(NumpadDockBottom,
            std::abs(static_cast<int>(keypadFrame.visible.top - mainFrame.visible.bottom)),
            horizontalOverlap);

        if (bestDistance > kDockSnapDistance)
        {
            g_numpadDockSide = NumpadDockNone;
            GetWindowRect(g_hwndNumpadView, &g_numpadFloatRect);
            SaveNumpadViewPlacement();
            return;
        }

        g_numpadDockSide = bestSide;
        if (bestSide == NumpadDockLeft || bestSide == NumpadDockRight)
        {
            g_numpadDockOffset = static_cast<int>(
                keypadFrame.visible.top - mainFrame.visible.top);
        }
        else
        {
            g_numpadDockOffset = static_cast<int>(
                keypadFrame.visible.left - mainFrame.visible.left);
        }
        DockNumpadViewToMainInternal();
        SaveNumpadViewPlacement();
    }

    HWND CreateNumpadViewButton(HWND parent, int id, const wchar_t* text, bool enabled)
    {
        DWORD style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_MULTILINE;
        if (enabled)
            style |= WS_TABSTOP;
        else
            style |= WS_DISABLED;
        return CreateWindowExW(0, L"BUTTON", text, style,
            0, 0, 40, 40, parent,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
            GetModuleHandleW(nullptr), nullptr);
    }

    LRESULT CALLBACK NumpadViewProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_hwndNumpadView = hwnd;
            g_hwndNumpadNum = CreateNumpadViewButton(
                hwnd, ID_NP_VIEW_NUMLOCK, L"Num", false);
            g_hwndNumpadEnter = CreateNumpadViewButton(
                hwnd, ID_NP_VIEW_ENTER, L"Enter", false);
            for (int i = 0; i < 15; ++i)
            {
                g_hwndNumpadButtons[i] = CreateNumpadViewButton(
                    hwnd, ID_NP_VIEW_BTN_BASE + i, L"", true);
            }
            ApplyPopupTitleBarTheme(hwnd);
            RefreshNumpadViewWindow();
            LayoutNumpadViewControls(hwnd);
            return 0;
        }

        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            if (id >= ID_NP_VIEW_BTN_BASE && id < ID_NP_VIEW_BTN_BASE + 15)
            {
                int idx = id - ID_NP_VIEW_BTN_BASE;
                if (g_app && idx >= 0 && idx < 15)
                {
                    std::wstring command = Trim(g_app->numpadMacros[idx]);
                    if (!command.empty())
                        SendRawCommandToMud(command);
                    else
                        MessageBeep(MB_ICONWARNING);
                }
                return 0;
            }
            break;
        }

        case WM_SIZE:
            LayoutNumpadViewControls(hwnd);
            return 0;

        case WM_GETMINMAXINFO:
        {
            MINMAXINFO* info = reinterpret_cast<MINMAXINFO*>(lParam);
            if (info)
            {
                info->ptMinTrackSize.x = kMinimumViewWidth;
                info->ptMinTrackSize.y = kMinimumViewHeight;
            }
            return 0;
        }

        case WM_EXITSIZEMOVE:
            UpdateNumpadDockFromMovedWindow();
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            SaveNumpadViewPlacement();
            g_hwndNumpadView = nullptr;
            g_hwndNumpadNum = nullptr;
            g_hwndNumpadEnter = nullptr;
            for (HWND& button : g_hwndNumpadButtons)
                button = nullptr;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

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
    SaveNumpadViewPlacement();
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
    LoadNumpadViewPlacement();
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
            RefreshNumpadViewWindow();
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
            RefreshNumpadViewWindow();
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

void ShowNumpadViewWindow(HWND owner)
{
    if (g_hwndNumpadView && IsWindow(g_hwndNumpadView))
    {
        ShowWindow(g_hwndNumpadView, SW_SHOWNORMAL);
        SetForegroundWindow(g_hwndNumpadView);
        return;
    }

    if (!g_numpadViewClassRegistered)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = NumpadViewProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kNumpadViewClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
        wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_ICON1));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        g_numpadViewClassRegistered = true;
    }

    int width = std::max(kMinimumViewWidth,
        static_cast<int>(g_numpadFloatRect.right - g_numpadFloatRect.left));
    int height = std::max(kMinimumViewHeight,
        static_cast<int>(g_numpadFloatRect.bottom - g_numpadFloatRect.top));
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kNumpadViewClass,
        L"KTin 키패드",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        g_numpadFloatRect.left, g_numpadFloatRect.top,
        width, height,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd)
        return;

    ApplyPopupTitleBarTheme(hwnd);
    if (g_numpadDockSide != NumpadDockNone)
        DockNumpadViewToMainInternal();
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
}

void CloseNumpadViewWindow()
{
    if (g_hwndNumpadView && IsWindow(g_hwndNumpadView))
        DestroyWindow(g_hwndNumpadView);
    g_hwndNumpadView = nullptr;
}

void RefreshNumpadViewWindow()
{
    if (!g_hwndNumpadView || !IsWindow(g_hwndNumpadView))
        return;

    ApplyNumpadViewFont();
    for (int i = 0; i < 15; ++i)
    {
        if (!g_hwndNumpadButtons[i])
            continue;
        std::wstring text = NumpadButtonText(i);
        SetWindowTextW(g_hwndNumpadButtons[i], text.c_str());
        EnableWindow(g_hwndNumpadButtons[i],
            g_app && !Trim(g_app->numpadMacros[i]).empty());
    }
    LayoutNumpadViewControls(g_hwndNumpadView);
    InvalidateRect(g_hwndNumpadView, nullptr, TRUE);
}

void SyncNumpadViewToMain()
{
    DockNumpadViewToMainInternal();
}
