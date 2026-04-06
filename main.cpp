// ==============================================
// main.cpp
// ==============================================
#include "constants.h"
#include "types.h"
// 1. 프로젝트 핵심 헤더 (가장 먼저!)
#include "main.h"

// 2. 프로젝트 내부 헤더들 (의존성 순서대로)
#include "utils.h"
#include "terminal_buffer.h"
#include "theme.h"
#include "highlight.h"
#include "variables.h"
#include "abbreviation.h"
#include "settings.h"
#include "functionkey.h"
#include "numpad.h"
#include "chat_capture.h"
#include "shortcut_bar.h"
#include "help_dialog.h"
#include "memo.h"
#include "dialogs.h"
#include "auto_login.h"
#include "input.h"
#include "timer.h"
#include "address_book.h"
// 3. 리소스 헤더
#include "resource.h"

// 4. 시스템 헤더들 (원래 main.cpp에 있던 순서 그대로 유지)
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <richedit.h>
#include <richole.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <regex>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// 5. 라이브러리 링크
#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "UxTheme.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(linker, \
"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

#define INPUT_SEPARATOR_HEIGHT 6

// ==============================================
// 전역 변수
// ==============================================
AppState appInstance;
AppState* g_app = &appInstance;
HMODULE g_hRichEdit = nullptr;

// 누락된 창 이름 변수들
const wchar_t kMainWindowClass[] = L"TTGuiMainWindow";
const wchar_t* kTerminalWindowClass = L"TTGuiTerminalClass";
const wchar_t kInputWindowClass[] = L"TTGuiInputWindow";
const wchar_t* kInputContainerClass = L"TintinInputContainer";
const wchar_t* kShortcutBarClass = L"TintinShortcutBar";
const wchar_t* kStatusBarClass = L"TintinStatusBar";

// 프로세스 종료 함수
void StopProcessAndThread() {
    if (!g_app) return;
    g_app->shuttingDown = true;
    if (g_app->proc.stdinWrite) { CloseHandle(g_app->proc.stdinWrite); g_app->proc.stdinWrite = nullptr; }
    if (g_app->proc.stdoutRead) { CancelIoEx(g_app->proc.stdoutRead, nullptr); CloseHandle(g_app->proc.stdoutRead); g_app->proc.stdoutRead = nullptr; }
    if (g_app->proc.hPC) { ClosePseudoConsoleHandle(g_app->proc.hPC); g_app->proc.hPC = nullptr; }
    if (g_app->readerThread.joinable()) g_app->readerThread.join();
    if (g_app->proc.process) {
        if (WaitForSingleObject(g_app->proc.process, 500) == WAIT_TIMEOUT) {
            TerminateProcess(g_app->proc.process, 0); WaitForSingleObject(g_app->proc.process, 1000);
        }
        CloseHandle(g_app->proc.process); g_app->proc.process = nullptr;
    }
    if (g_app->proc.thread) { CloseHandle(g_app->proc.thread); g_app->proc.thread = nullptr; }
}

// 틴틴 시작 함수
bool StartTinTinProcess() {
    if (!g_app) return false;
    PFN_CreatePseudoConsole createFn = nullptr; PFN_ResizePseudoConsole resizeFn = nullptr; PFN_ClosePseudoConsole closeFn = nullptr;
    if (!GetConPtyApi(&createFn, &resizeFn, &closeFn)) return false;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    HANDLE inputRead = nullptr, inputWrite = nullptr, outputRead = nullptr, outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &inputWrite, &sa, 0)) return false;
    if (!SetHandleInformation(inputWrite, HANDLE_FLAG_INHERIT, 0)) return false;
    if (!CreatePipe(&outputRead, &outputWrite, &sa, 0)) return false;
    if (!SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0)) return false;
    COORD ptySize = GetPseudoConsoleSizeFromLogWindow();
    HPCON hPC = nullptr;
    if (FAILED(createFn(ptySize, inputRead, outputWrite, 0, &hPC))) return false;
    CloseHandle(inputRead); CloseHandle(outputWrite);
    SIZE_T attrListSize = 0; InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);
    STARTUPINFOEXW si = { sizeof(si) };
    si.lpAttributeList = (PPROC_THREAD_ATTRIBUTE_LIST)HeapAlloc(GetProcessHeap(), 0, attrListSize);
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrListSize)) return false;
    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(hPC), nullptr, nullptr)) return false;
    std::wstring exeDir = GetModuleDirectory();
    std::wstring exePath = MakeAbsolutePath(exeDir, L"bin\\tt++.exe");
    std::wstring tinPath = MakeAbsolutePath(exeDir, L"main.tin");
    std::wstring cmdLine = L"\"" + exePath + L"\" \"" + tinPath + L"\"";
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT, nullptr, exeDir.c_str(), &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(si.lpAttributeList); HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
    if (!ok) { CloseHandle(inputWrite); CloseHandle(outputRead); ClosePseudoConsoleHandle(hPC); return false; }
    g_app->proc = { pi.hProcess, pi.hThread, inputWrite, outputRead, hPC };
    return true;
}

// 레이아웃 갱신 함수
void LayoutChildren(HWND hwnd) {
    if (!g_app || !hwnd) return;
    RECT rc{}; GetClientRect(hwnd, &rc);
    int width = rc.right - rc.left, height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;
    int menuHeight = (!g_app->menuHidden) ? g_app->customMenuHeight : 0;
    int chatHeight = 0, chatSeparatorHeight = 0;
    bool chatDockedVisible = (g_app->chatDocked && g_app->chatVisible && g_app->hwndChat && IsWindow(g_app->hwndChat));
    if (chatDockedVisible) {
        int chatFontCy = 16; HDC hdc = GetDC(hwnd);
        HFONT oldFont = (HFONT)SelectObject(hdc, g_app->hFontChat ? g_app->hFontChat : GetStockObject(DEFAULT_GUI_FONT));
        TEXTMETRICW tm = {}; GetTextMetricsW(hdc, &tm); chatFontCy = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(hdc, oldFont); ReleaseDC(hwnd, hdc);
        chatHeight = max(24, (g_app->chatDockedLines * chatFontCy) + 24);
        chatSeparatorHeight = INPUT_SEPARATOR_HEIGHT;
    }
    int inputHeight = max(72, GetInputAreaHeight());
    int shortcutHeight = g_app->shortcutBarVisible ? SHORTCUT_BAR_HEIGHT : 0;
    int statusHeight = GetStatusBarHeight();
    int logHeight = max(80, height - menuHeight - shortcutHeight - inputHeight - statusHeight - chatHeight - chatSeparatorHeight);
    int chatTop = menuHeight, logTop = chatTop + chatHeight + chatSeparatorHeight;
    int shortcutTop = logTop + logHeight, inputTop = shortcutTop + shortcutHeight, statusTop = inputTop + inputHeight;

    HDWP hdwp = BeginDeferWindowPos(5);
    if (chatDockedVisible) { ShowWindow(g_app->hwndChat, SW_SHOW); hdwp = DeferWindowPos(hdwp, g_app->hwndChat, HWND_TOP, 0, chatTop, width, chatHeight, SWP_NOACTIVATE | SWP_FRAMECHANGED); }
    else if (g_app->hwndChat && IsWindow(g_app->hwndChat) && g_app->chatDocked) hdwp = DeferWindowPos(hdwp, g_app->hwndChat, nullptr, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_HIDEWINDOW);
    if (g_app->hwndLog && IsWindow(g_app->hwndLog)) hdwp = DeferWindowPos(hdwp, g_app->hwndLog, nullptr, 0, logTop, width, logHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_app->hwndShortcutBar && IsWindow(g_app->hwndShortcutBar)) hdwp = DeferWindowPos(hdwp, g_app->hwndShortcutBar, nullptr, 0, shortcutTop, width, shortcutHeight > 0 ? shortcutHeight : 0, SWP_NOZORDER | SWP_NOACTIVATE | (g_app->shortcutBarVisible && shortcutHeight > 0 ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    if (g_app->hwndInput && IsWindow(g_app->hwndInput)) hdwp = DeferWindowPos(hdwp, g_app->hwndInput, nullptr, 0, inputTop, width, inputHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (g_app->hwndStatusBar && IsWindow(g_app->hwndStatusBar)) hdwp = DeferWindowPos(hdwp, g_app->hwndStatusBar, nullptr, 0, statusTop, width, statusHeight, SWP_NOZORDER | SWP_NOACTIVATE);
    if (hdwp) EndDeferWindowPos(hdwp);

    if (g_app->termBuffer && (g_app->termBuffer->height != g_app->screenRows || g_app->termBuffer->width != g_app->screenCols)) {
        g_app->termBuffer->Resize(g_app->screenCols, g_app->screenRows);
        if (g_app->proc.hPC) ResizePseudoConsoleToLogWindow();
    }
    if (g_app->hwndShortcutBar) ShowWindow(g_app->hwndShortcutBar, g_app->shortcutBarVisible ? SW_SHOW : SW_HIDE);
    if (g_app->shortcutBarVisible && g_app->hwndShortcutBar) {
        int buttonWidth = max(28, (width - 8 - 4 * (SHORTCUT_BUTTON_COUNT - 1)) / SHORTCUT_BUTTON_COUNT);
        int buttonHeight = max(20, shortcutHeight - 6);
        for (int i = 0, x = 4; i < SHORTCUT_BUTTON_COUNT; ++i, x += buttonWidth + 4) {
            if (g_app->hwndShortcutButtons[i]) { MoveWindow(g_app->hwndShortcutButtons[i], x, 3, buttonWidth, buttonHeight, TRUE); ShowWindow(g_app->hwndShortcutButtons[i], SW_SHOW); }
        }
    }
    else {
        for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i) if (g_app->hwndShortcutButtons[i]) ShowWindow(g_app->hwndShortcutButtons[i], SW_HIDE);
    }
    LayoutInputEdits();
    if (chatDockedVisible) { SendMessageW(g_app->hwndChat, EM_SETBKGNDCOLOR, 0, g_app->chatStyle.backColor); InvalidateRect(g_app->hwndChat, nullptr, TRUE); UpdateWindow(g_app->hwndChat); }
    InvalidateRect(hwnd, nullptr, TRUE);
}

// ==============================================
// wWinMain 진입점
// ==============================================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_LINK_CLASS;
    InitCommonControlsEx(&icc);

    LoadLibraryW(L"Msftedit.dll");

    AppState app;
    g_app = &app;

    // 창 클래스 등록
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = kMainWindowClass;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    RegisterClassW(&wc);

    WNDCLASSW wcTerminal = {};
    wcTerminal.lpfnWndProc   = TerminalWndProc;
    wcTerminal.hInstance     = hInstance;
    wcTerminal.lpszClassName = kTerminalWindowClass;
    wcTerminal.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcTerminal.hbrBackground = nullptr;
    RegisterClassW(&wcTerminal);

    WNDCLASSW wcInput = {};
    wcInput.lpfnWndProc   = DefWindowProcW;
    wcInput.hInstance     = hInstance;
    wcInput.lpszClassName = kInputWindowClass;
    wcInput.hCursor       = LoadCursorW(nullptr, IDC_IBEAM);
    wcInput.hbrBackground = nullptr;
    RegisterClassW(&wcInput);

    WNDCLASSW wcContainer = {};
    wcContainer.lpfnWndProc   = InputContainerProc;
    wcContainer.hInstance     = hInstance;
    wcContainer.lpszClassName = kInputContainerClass;
    wcContainer.hCursor       = LoadCursorW(nullptr, IDC_IBEAM);
    wcContainer.hbrBackground = nullptr;
    RegisterClassW(&wcContainer);

    WNDCLASSW wcShortcut = {};
    wcShortcut.lpfnWndProc   = ShortcutBarProc;
    wcShortcut.hInstance     = hInstance;
    wcShortcut.lpszClassName = kShortcutBarClass;
    wcShortcut.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcShortcut.hbrBackground = nullptr;
    RegisterClassW(&wcShortcut);

    WNDCLASSW wcStatus = {};
    wcStatus.lpfnWndProc   = StatusBarProc;
    wcStatus.hInstance     = hInstance;
    wcStatus.lpszClassName = kStatusBarClass;
    wcStatus.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcStatus.hbrBackground = nullptr;
    RegisterClassW(&wcStatus);

    WNDCLASSW wcChatFloat = {};
    wcChatFloat.lpfnWndProc   = ChatFloatWndProc;
    wcChatFloat.hInstance     = hInstance;
    wcChatFloat.lpszClassName = L"TTGuiChatFloatClass";
    wcChatFloat.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wcChatFloat.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wcChatFloat);

    HWND hwnd = CreateWindowExW(
        0, kMainWindowClass, L"TinTin++ GUI",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        538, 61, 690, 899,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    RegisterEmbeddedFont();
    g_app->useCustomMudFont = true;

    g_app->hFontLog = GetCurrentAppFont(16, FW_NORMAL);

    // 자동 팝업
    if (g_app->autoShowQuickConnect)
        PostMessageW(hwnd, WM_COMMAND, ID_MENU_FILE_QUICK_CONNECT, 0);
    else if (g_app->autoShowAddressBook)
        PostMessageW(hwnd, WM_COMMAND, ID_MENU_FILE_ADDRESSBOOK, 0);

    MSG msg;

    while (GetMessageW(&msg, nullptr, 0, 0))
    {

        // 2. 모달리스 다이얼로그 메시지 처리 (변수명 통일에 주의: hwndSymbol)
        if (g_findState.hwndDialog && IsWindow(g_findState.hwndDialog) && IsDialogMessageW(g_findState.hwndDialog, &msg)) continue;
        if (g_memoFind.hwndDialog && IsWindow(g_memoFind.hwndDialog) && IsDialogMessageW(g_memoFind.hwndDialog, &msg)) continue;

        // ★ 주의: g_app->hwndSymbolDialog라고 쓰셨던 부분을 hwndSymbol로 통일해야 합니다.
        if (g_app && g_app->hwndSymbol && IsWindow(g_app->hwndSymbol) && IsDialogMessageW(g_app->hwndSymbol, &msg)) continue;

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_app = nullptr;
    return static_cast<int>(msg.wParam);
}

static bool s_logDragging = false;
static POINT s_logMouseDownPt = { 0, 0 };
FindState g_findState;

struct LogChunk {
    std::vector<StyledRun> runs;
};
// ==============================================
// MainWndProc
// ==============================================
// 리스트박스 목록 갱신
static void RefreshHiListBox(HWND hList) {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (const auto& r : g_hiState.rules) {
        std::wstring title = r.enabled ? L"[켬] " : L"[끔] ";

        // ★ 핵심: 별명이 있으면 별명을 보여주고, 없으면 패턴을 보여줌
        if (!r.name.empty()) {
            title += r.name;
        }
        else {
            title += r.pattern.empty() ? L"(새 규칙)" : r.pattern;
        }

        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)title.c_str());
    }
}

// 우측 상세 설정 UI에 데이터 채우기 (Name 비활성화 배열 추가)
static void UpdateHiDetailUI(HWND hwnd, int idx) {
    bool hasSel = (idx >= 0 && idx < (int)g_hiState.rules.size());

    // ★ 수정: ID_HI_DET_NAME 도 컨트롤 배열에 추가하여, 선택 해제 시 같이 비활성화되게 함
    int controls[] = { ID_HI_DET_NAME, ID_HI_DET_ENABLE, ID_HI_DET_PATTERN, ID_HI_DET_INVERSE,
                           ID_HI_DET_USECMD, ID_HI_DET_CMD, ID_HI_DET_BEEP,
                           ID_HI_DET_USESOUND, ID_HI_DET_PATH, ID_HI_DET_BROWSE, ID_HI_DET_PLAY_SOUND };
    for (int id : controls) EnableWindow(GetDlgItem(hwnd, id), hasSel);

    if (!hasSel) {
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_NAME), L"");
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATTERN), L"");
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_CMD), L"");
        SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), L"");
        return;
    }

    const auto& r = g_hiState.rules[idx];
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_NAME), r.name.c_str());
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_ENABLE), BM_SETCHECK, r.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATTERN), r.pattern.c_str());
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_INVERSE), BM_SETCHECK, r.useInverse ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USECMD), CB_SETCURSEL, r.actionType, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_CMD), r.command.c_str());
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_BEEP), BM_SETCHECK, r.useBeep ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USESOUND), BM_SETCHECK, r.useSound ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), r.soundPath.c_str());

    InvalidateRect(hwnd, nullptr, TRUE); // 색상 박스 갱신
}

// UI에 입력된 내용을 메모리(vector)에 동기화
static void SyncHiDataFromUI(HWND hwnd, int idx) {
    if (idx < 0 || idx >= (int)g_hiState.rules.size()) return;
    auto& r = g_hiState.rules[idx];

    r.enabled = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_ENABLE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    r.useInverse = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_INVERSE), BM_GETCHECK, 0, 0) == BST_CHECKED);
    r.actionType = (int)SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USECMD), CB_GETCURSEL, 0, 0);
    r.useBeep = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_BEEP), BM_GETCHECK, 0, 0) == BST_CHECKED);
    r.useSound = (SendMessageW(GetDlgItem(hwnd, ID_HI_DET_USESOUND), BM_GETCHECK, 0, 0) == BST_CHECKED);

    wchar_t b[1024];
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_NAME), b, 1024); r.name = b; // 별명 저장
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATTERN), b, 1024); r.pattern = b;
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_CMD), b, 1024); r.command = b;
    GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), b, 1024); r.soundPath = b;
}


static LRESULT CALLBACK HighlightDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static int s_sel = -1;
    switch (msg) {
    case WM_CREATE: {
        // ★ 창 높이를 440으로 줄여 불필요한 하단 여백 제거
        SetWindowPos(hwnd, nullptr, 0, 0, 800, 440, SWP_NOMOVE | SWP_NOZORDER);
        HFONT hF = GetPopupUIFont(hwnd); HINSTANCE hInst = GetModuleHandle(0);

        // 좌측 리스트
        CreateWindowExW(0, L"STATIC", L"규칙 목록", WS_CHILD | WS_VISIBLE, 15, 12, 100, 20, hwnd, 0, hInst, 0);
        HWND hList = CreateWindowExW(
            WS_EX_CLIENTEDGE,
            L"LISTBOX",
            L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_TABSTOP | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS,
            15, 28, 220, 338,
            hwnd,
            (HMENU)ID_HI_LIST,
            hInst,
            0);

        CreateWindowExW(0, L"BUTTON", L"추가", WS_CHILD | WS_VISIBLE, 15, 355, 50, 28, hwnd, (HMENU)ID_HI_BTN_ADD, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"삭제", WS_CHILD | WS_VISIBLE, 70, 355, 50, 28, hwnd, (HMENU)ID_HI_BTN_DEL, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"▲", WS_CHILD | WS_VISIBLE, 125, 355, 30, 28, hwnd, (HMENU)ID_HI_BTN_UP, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"▼", WS_CHILD | WS_VISIBLE, 160, 355, 30, 28, hwnd, (HMENU)ID_HI_BTN_DOWN, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"복제", WS_CHILD | WS_VISIBLE, 195, 355, 40, 28, hwnd, (HMENU)ID_HI_BTN_CLONE, hInst, 0);

        // 우측 상세 설정 (컴팩트 배치)
        CreateWindowExW(0, L"BUTTON", L"규칙 상세 편집", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 250, 15, 520, 335, hwnd, 0, hInst, 0);

        // 1행: 별명 입력란 및 활성화
        CreateWindowExW(0, L"STATIC", L"규칙 이름(별명):", WS_CHILD | WS_VISIBLE, 270, 43, 100, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 375, 40, 260, 24, hwnd, (HMENU)ID_HI_DET_NAME, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"활성화", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 650, 42, 100, 20, hwnd, (HMENU)ID_HI_DET_ENABLE, hInst, 0);

        // 2행: 패턴
        CreateWindowExW(0, L"STATIC", L"인식 패턴:", WS_CHILD | WS_VISIBLE, 270, 75, 100, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 270, 95, 480, 24, hwnd, (HMENU)ID_HI_DET_PATTERN, hInst, 0);

        // 3행: 색상 제어
        CreateWindowExW(0, L"BUTTON", L"색상 반전", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 270, 130, 80, 20, hwnd, (HMENU)ID_HI_DET_INVERSE, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"글자색:", WS_CHILD | WS_VISIBLE, 370, 130, 50, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, 425, 130, 30, 20, hwnd, (HMENU)ID_HI_DET_FG, hInst, 0);
        CreateWindowExW(0, L"STATIC", L"배경색:", WS_CHILD | WS_VISIBLE, 470, 130, 50, 20, hwnd, 0, hInst, 0);
        CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_NOTIFY, 525, 130, 30, 20, hwnd, (HMENU)ID_HI_DET_BG, hInst, 0);

        // 4행: 실행 동작 드롭다운 및 명령 입력
        CreateWindowExW(0, L"STATIC", L"실행 동작:", WS_CHILD | WS_VISIBLE, 270, 165, 70, 20, hwnd, 0, hInst, 0);
        HWND hComboAct = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP, 345, 162, 180, 100, hwnd, (HMENU)ID_HI_DET_USECMD, hInst, 0);
        SendMessageW(hComboAct, CB_ADDSTRING, 0, (LPARAM)L"아무것도 안 함");
        SendMessageW(hComboAct, CB_ADDSTRING, 0, (LPARAM)L"명령 실행 (글월 등)");
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 535, 162, 215, 24, hwnd, (HMENU)ID_HI_DET_CMD, hInst, 0);

        // 5행: 비프 및 사운드 재생
        CreateWindowExW(0, L"BUTTON", L"기본 비프음 사용", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 270, 200, 200, 20, hwnd, (HMENU)ID_HI_DET_BEEP, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"사운드 파일 재생 (.wav, .mp3)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 270, 235, 220, 20, hwnd, (HMENU)ID_HI_DET_USESOUND, hInst, 0);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 270, 260, 310, 24, hwnd, (HMENU)ID_HI_DET_PATH, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"찾기...", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 590, 260, 70, 24, hwnd, (HMENU)ID_HI_DET_BROWSE, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"미리듣기", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 670, 260, 80, 24, hwnd, (HMENU)ID_HI_DET_PLAY_SOUND, hInst, 0);

        // 안내문
        CreateWindowExW(
            0, L"STATIC",
            L"※ 안내: 실행 동작을 [아무것도 안 함]으로 선택하고 활성화할 경우,",
            WS_CHILD | WS_VISIBLE,
            270, 295, 500, 18,
            hwnd, 0, hInst, 0);

        CreateWindowExW(
            0, L"STATIC",
            L"            패턴에 일치하는 텍스트는 설정된 색상으로 하이라이트(반전) 됩니다.",
            WS_CHILD | WS_VISIBLE,
            270, 317, 500, 18,
            hwnd, 0, hInst, 0);

        // 하단 버튼
        CreateWindowExW(0, L"BUTTON", L"적용(A)", WS_CHILD | WS_VISIBLE, 580, 355, 90, 32, hwnd, (HMENU)(INT_PTR)ID_HI_BTN_APPLY, hInst, 0);
        CreateWindowExW(0, L"BUTTON", L"닫기(C)", WS_CHILD | WS_VISIBLE, 680, 355, 90, 32, hwnd, (HMENU)IDCANCEL, hInst, 0);

        EnumChildWindows(hwnd, [](HWND c, LPARAM f) { SendMessageW(c, WM_SETFONT, f, TRUE); return TRUE; }, (LPARAM)hF);

        SendMessageW(hList, LB_SETITEMHEIGHT, 0, 20);

        s_sel = -1;
        RefreshHiListBox(hList);
        UpdateHiDetailUI(hwnd, -1);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam; int id = GetDlgCtrlID((HWND)lParam);
        if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size()) {
            if (id == ID_HI_DET_FG) { SetBkColor(hdc, g_hiState.rules[s_sel].fg); return (INT_PTR)CreateSolidBrush(g_hiState.rules[s_sel].fg); }
            if (id == ID_HI_DET_BG) { SetBkColor(hdc, g_hiState.rules[s_sel].bg); return (INT_PTR)CreateSolidBrush(g_hiState.rules[s_sel].bg); }
        }

        // ★ 여기서 윈도우 기본 팝업 바탕색 브러시를 반환하여 흰색 네모를 없앱니다!
        SetBkMode(hdc, TRANSPARENT);
        return (INT_PTR)GetSysColorBrush(COLOR_BTNFACE);
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_HI_LIST && HIWORD(wParam) == LBN_SELCHANGE) {
            HWND hList = GetDlgItem(hwnd, ID_HI_LIST);
            int newSel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);

            if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size())
                SyncHiDataFromUI(hwnd, s_sel);

            s_sel = newSel;

            if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size())
                UpdateHiDetailUI(hwnd, s_sel);
            else
                UpdateHiDetailUI(hwnd, -1);
        }
        else if (id == ID_HI_BTN_ADD) {
            SyncHiDataFromUI(hwnd, s_sel); g_hiState.rules.push_back(HighlightRule()); RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
            s_sel = (int)g_hiState.rules.size() - 1; SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0); UpdateHiDetailUI(hwnd, s_sel);
        }
        else if (id == ID_HI_BTN_DEL && s_sel >= 0) {
            g_hiState.rules.erase(g_hiState.rules.begin() + s_sel); s_sel = -1; RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST)); UpdateHiDetailUI(hwnd, -1);
        }
        else if (id == ID_HI_BTN_CLONE && s_sel >= 0) {
            SyncHiDataFromUI(hwnd, s_sel);
            g_hiState.rules.push_back(g_hiState.rules[s_sel]);
            RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
            s_sel = (int)g_hiState.rules.size() - 1; SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0); UpdateHiDetailUI(hwnd, s_sel);
        }
        else if (id == ID_HI_DET_FG && s_sel >= 0) {
            if (ChooseColorOnly(hwnd, g_hiState.rules[s_sel].fg)) InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (id == ID_HI_DET_BG && s_sel >= 0) {
            if (ChooseColorOnly(hwnd, g_hiState.rules[s_sel].bg)) InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (id == ID_HI_DET_BROWSE && s_sel >= 0) {
            wchar_t f[MAX_PATH] = { 0 };
            OPENFILENAMEW of = { sizeof(of), hwnd, 0, L"Audio Files (*.wav;*.mp3)\0*.wav;*.mp3\0All Files (*.*)\0*.*\0", 0, 0, 1, f, MAX_PATH };
            if (GetOpenFileNameW(&of)) {
                SetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), f);
            }
        }
        // 미리듣기
        else if (id == ID_HI_DET_PLAY_SOUND && s_sel >= 0) {
            wchar_t pathBuf[1024] = { 0 };
            GetWindowTextW(GetDlgItem(hwnd, ID_HI_DET_PATH), pathBuf, 1024);
            std::wstring path = Trim(pathBuf);
            if (!path.empty()) PlayAudioFile(path);
            else MessageBeep(MB_ICONWARNING);
        }
        else if (id == ID_HI_BTN_APPLY || id == IDOK) {
            SyncHiDataFromUI(hwnd, s_sel); g_hiState.active = !g_hiState.rules.empty(); SaveHighlightSettings();
            RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
            if (g_app->hwndLog) InvalidateRect(g_app->hwndLog, nullptr, TRUE);
            if (id == IDOK) DestroyWindow(hwnd);
        }
        else if (id == ID_HI_BTN_UP) {
            if (s_sel > 0) {
                SyncHiDataFromUI(hwnd, s_sel);
                std::swap(g_hiState.rules[s_sel], g_hiState.rules[s_sel - 1]);
                s_sel--;
                RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
                SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0);
                UpdateHiDetailUI(hwnd, s_sel);
            }
        }
        else if (id == ID_HI_BTN_DOWN) {
            if (s_sel >= 0 && s_sel < (int)g_hiState.rules.size() - 1) {
                SyncHiDataFromUI(hwnd, s_sel);
                std::swap(g_hiState.rules[s_sel], g_hiState.rules[s_sel + 1]);
                s_sel++;
                RefreshHiListBox(GetDlgItem(hwnd, ID_HI_LIST));
                SendMessageW(GetDlgItem(hwnd, ID_HI_LIST), LB_SETCURSEL, s_sel, 0);
                UpdateHiDetailUI(hwnd, s_sel);
            }
        }
        else if (id == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlID == ID_HI_LIST)
        {
            mis->itemHeight = 32;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlID == ID_HI_LIST)
        {
            if (dis->itemID == (UINT)-1)
                return TRUE;

            wchar_t text[256] = {};
            SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)text);

            bool isOn = wcsncmp(text, L"[켬]", 3) == 0;

            COLORREF bg = (dis->itemState & ODS_SELECTED)
                ? GetSysColor(COLOR_HIGHLIGHT)
                : GetSysColor(COLOR_WINDOW);

            COLORREF mainText = (dis->itemState & ODS_SELECTED)
                ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                : GetSysColor(COLOR_WINDOWTEXT);

            COLORREF prefixColor = isOn ? RGB(0, 160, 0) : RGB(200, 40, 40);

            HBRUSH hbr = CreateSolidBrush(bg);
            FillRect(dis->hDC, &dis->rcItem, hbr);
            DeleteObject(hbr);

            SetBkMode(dis->hDC, TRANSPARENT);

            HFONT hFont = GetPopupUIFont(hwnd);
            HFONT hOld = (HFONT)SelectObject(dis->hDC, hFont);

            RECT rcPrefix = dis->rcItem;
            rcPrefix.left += 8;
            rcPrefix.right = rcPrefix.left + 36;

            RECT rcText = dis->rcItem;
            rcText.left += 48;

            SetTextColor(dis->hDC, prefixColor);
            DrawTextW(dis->hDC, text, 3, &rcPrefix, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            SetTextColor(dis->hDC, mainText);
            DrawTextW(dis->hDC, text + 4, -1, &rcText, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

            SelectObject(dis->hDC, hOld);

            if (dis->itemState & ODS_FOCUS)
                DrawFocusRect(dis->hDC, &dis->rcItem);

            return TRUE;
        }
        break;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


static void ShowHighlightDialog(HWND owner) {
    static const wchar_t* kClass = L"TTGuiHighlightClass";
    static bool reg = false;
    if (!reg) {
        WNDCLASSW wc = { 0 }; wc.lpfnWndProc = HighlightDialogProc; wc.hInstance = GetModuleHandle(0);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursor(0, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true;
    }
    HWND hDlg = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, L"트리거 설정", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE, 0, 0, 520, 400, owner, nullptr, GetModuleHandle(0), nullptr);
    RECT rcO, rcD; GetWindowRect(owner, &rcO); GetWindowRect(hDlg, &rcD);
    SetWindowPos(hDlg, 0, rcO.left + (rcO.right - rcO.left - 520) / 2, rcO.top + (rcO.bottom - rcO.top - 400) / 2, 0, 0, SWP_NOSIZE);
    EnableWindow(owner, FALSE);
    MSG msg; while (IsWindow(hDlg) && GetMessageW(&msg, 0, 0, 0)) { if (!IsDialogMessageW(hDlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    EnableWindow(owner, TRUE); SetActiveWindow(owner);
}

static void ShowCustomMenuPopup(HWND hwnd, int menuIndex)
{
    if (!g_app || !g_app->hMainMenu || menuIndex < 0)
        return;

    // ★ 신규 추가: 메뉴가 화면에 그려지기 직전에 무조건 글씨를 최신 상태로 갱신합니다!
    UpdateMenuToggleStates();

    HMENU hPopup = GetSubMenu(g_app->hMainMenu, menuIndex);
    if (!hPopup)
        return;

    int x = 6;
    for (int i = 0; i < menuIndex; ++i)
        x += GetCustomMenuItemWidth(i) + 4;

    int y = g_app->customMenuHeight;

    POINT pt = { x, y };
    ClientToScreen(hwnd, &pt);

    g_app->hotMenuIndex = menuIndex;
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);

    SetForegroundWindow(hwnd);

    TrackPopupMenu(
        hPopup,
        TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON,
        pt.x,
        pt.y,
        0,
        hwnd,
        nullptr);

    PostMessageW(hwnd, WM_NULL, 0, 0);

    g_app->hotMenuIndex = -1;
    InvalidateRect(hwnd, nullptr, FALSE);
    UpdateWindow(hwnd);
}

// ==============================================
// ReaderThreadProc
// ==============================================
static void ReaderThreadProc(HWND hwndMain, HANDLE hRead)
{
    AnsiToRunsParser parser;
    char buffer[4096];

    DWORD lastPostTick = GetTickCount();
    std::vector<StyledRun> pendingRuns;

    auto FlushPendingRuns = [&](bool force)
        {
            if (pendingRuns.empty())
                return;

            DWORD now = GetTickCount();

            // 강제 flush가 아니면 너무 자주 PostMessage 하지 않음
            if (!force)
            {
                if (now - lastPostTick < 30)
                    return;
            }

            LogChunk* chunk = new LogChunk();
            chunk->runs = std::move(pendingRuns);
            pendingRuns.clear();

            PostMessageW(hwndMain, WM_APP_LOG_CHUNK, 0, (LPARAM)chunk);
            lastPostTick = now;
        };

    while (g_app && !g_app->shuttingDown)
    {
        DWORD read = 0;
        BOOL ok = ReadFile(hRead, buffer, sizeof(buffer), &read, nullptr);
        if (!ok || read == 0)
            break;

        // ★ 원본 ANSI/UTF-8 바이트 그대로 저장
        if (g_app && read > 0)
        {
            g_app->rawAnsiCurrentScreen.append(buffer, buffer + read);
            g_app->rawAnsiHistory.append(buffer, buffer + read);

            const size_t kMaxCur = 256 * 1024;
            const size_t kMaxHist = 2 * 1024 * 1024;

            if (g_app->rawAnsiCurrentScreen.size() > kMaxCur)
            {
                g_app->rawAnsiCurrentScreen.erase(
                    0,
                    g_app->rawAnsiCurrentScreen.size() - kMaxCur);
            }

            if (g_app->rawAnsiHistory.size() > kMaxHist)
            {
                g_app->rawAnsiHistory.erase(
                    0,
                    g_app->rawAnsiHistory.size() - kMaxHist);
            }
        }

        std::vector<StyledRun> runs = parser.Feed(buffer, read);
        if (!runs.empty())
        {
            // pendingRuns 뒤에 이어붙임
            pendingRuns.insert(
                pendingRuns.end(),
                std::make_move_iterator(runs.begin()),
                std::make_move_iterator(runs.end()));

            // 너무 커지면 강제로 바로 보냄
            if (pendingRuns.size() >= 512)
            {
                FlushPendingRuns(true);
            }
            else
            {
                FlushPendingRuns(false);
            }
        }
    }

    std::vector<StyledRun> tail = parser.Flush();
    if (!tail.empty())
    {
        pendingRuns.insert(
            pendingRuns.end(),
            std::make_move_iterator(tail.begin()),
            std::make_move_iterator(tail.end()));
    }

    // 남은 것 강제 flush
    FlushPendingRuns(true);

    PostMessageW(hwndMain, WM_APP_PROCESS_EXIT, 0, 0);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_INITMENUPOPUP:
    {
        if (g_app && g_app->termBuffer) {
            bool hasHistory = !g_app->termBuffer->history.empty();
            UINT state = hasHistory ? MF_ENABLED : MF_GRAYED;
            EnableMenuItem((HMENU)wParam, ID_MENU_EDIT_COPY_PAST, MF_BYCOMMAND | state);
            EnableMenuItem((HMENU)wParam, ID_MENU_EDIT_SAVE_PAST, MF_BYCOMMAND | state);
        }
        return 0;
    }

    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
        if (mis && mis->CtlType == ODT_MENU)
        {
            MeasureOwnerDrawMenuItem(hwnd, mis);
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
    {
        DRAWITEMSTRUCT* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
        if (dis && dis->CtlType == ODT_MENU)
        {
            DrawOwnerDrawMenuItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_CREATE:
    {
        // [중요] 다른 폰트 로직이 돌아가기 전에 가장 먼저 등록해야 합니다!
        RegisterEmbeddedFont(); 
        g_app->hwndMain = hwnd;
        g_app->mainBackColor = RGB(45, 45, 48);        

        LoadFontRenderSettings();
        LoadKeepAliveSettings();
        LoadInputHistorySettings();
        LoadCaptureLogSettings();
        LoadChatCaptureSettings();
        LoadScreenSizeSettings();
        LoadAddressBook();
        LoadQuickConnectHistory();
        LoadHighlightSettings();
        LoadAutoLoginSettings();
        LoadVariableSettings();
        LoadNumpadSettings();
        LoadGeneralSettings();
        LoadAbbreviationSettings();
        LoadTimerSettings();

        // ★ 여기서 LoadWindowSettings를 호출하여 파일 상태를 완벽히 가져옴
        LoadWindowSettings(hwnd);


        // 2. 엔진을 돌리기 위한 Win32 타이머 생성 (50ms 주기)
        SetTimer(hwnd, ID_TIMER_USER_ENGINE, 50, nullptr); // <--- 타이머 추가

        // 1. 로그창 스타일 설정 (함수 호출 한 줄로 끝!)
        InitStyleFont(g_app->logStyle.font, hwnd, 12);
        g_app->logStyle.font.lfCharSet = HANGEUL_CHARSET;
        g_app->logStyle.font.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
        g_app->logStyle.font.lfOutPrecision = OUT_TT_ONLY_PRECIS;

        g_app->logStyle.textColor = RGB(220, 220, 220);
        g_app->logStyle.backColor = RGB(0, 0, 0);

        // 2. 입력창 스타일 설정 (함수 호출 한 줄로 끝!)
        InitStyleFont(g_app->inputStyle.font, hwnd, 12);
        g_app->inputStyle.textColor = RGB(230, 230, 230);
        g_app->inputStyle.backColor = RGB(20, 20, 20);

        g_app->chatStyle = g_app->logStyle;

        int initialLogRows = g_app->screenRows;

        g_app->termBuffer = new TerminalBuffer(g_app->screenCols, initialLogRows);

        // ★ 채팅 캡쳐창 본체 생성
        g_app->hwndChat = CreateWindowExW(
            0, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(g_app->hwndChat, EM_SETBKGNDCOLOR, 0, g_app->chatStyle.backColor);
        SetupChatRichEditDefaults(g_app->hwndChat);

        // ★ 추가됨: 방금 만든 우클릭 메뉴용 함수를 채팅 캡쳐창에 연결해줍니다!
        g_app->oldChatProc = (WNDPROC)SetWindowLongPtrW(g_app->hwndChat, GWLP_WNDPROC, (LONG_PTR)ChatEditSubclassProc);

        // ★ 분리했을 때 담아줄 전용 창틀 생성 (여기에 제목을 답니다!)
        g_app->hwndChatFloat = CreateWindowExW(
            WS_EX_TOOLWINDOW, L"TTGuiChatFloatClass", L"채팅 캡쳐창",
            WS_OVERLAPPEDWINDOW,
            g_app->chatFloatRect.left, g_app->chatFloatRect.top,
            g_app->chatFloatRect.right - g_app->chatFloatRect.left,
            g_app->chatFloatRect.bottom - g_app->chatFloatRect.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);

        // 분리 설정 상태면 부모를 창틀로 이동
        if (!g_app->chatDocked) {
            SetParent(g_app->hwndChat, g_app->hwndChatFloat);
            if (g_app->chatVisible) ShowWindow(g_app->hwndChatFloat, SW_SHOW);
        }

        g_app->hwndLog = CreateWindowExW(0, kTerminalWindowClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        g_app->hwndInput = CreateWindowExW(0, kInputContainerClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, 100, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        g_app->hwndStatusBar = CreateWindowExW(0, kStatusBarClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 100, STATUS_BAR_HEIGHT, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
        // WM_CREATE 내부의 hwndShortcutBar 생성 부분
        g_app->hwndShortcutBar = CreateWindowExW(
            0,
            kShortcutBarClass,
            L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 100, SHORTCUT_BAR_HEIGHT,
            hwnd,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        SendMessageW(g_app->hwndShortcutBar, WM_SETFONT, (WPARAM)g_app->hFontInput, TRUE);

        CreateMainMenu(hwnd);

        InitShortcutBindings();
        LoadFunctionKeySettings();

        // 설정 먼저 읽기
        LoadShortcutSettings();

        // 버튼 실제 생성
        InitializeShortcutButtons();

        // 생성된 뒤에 스타일 적용
        for (int i = 0; i < SHORTCUT_BUTTON_COUNT; ++i)
        {
            if (!g_app->hwndShortcutButtons[i])
                continue;

            LONG_PTR style = GetWindowLongPtrW(g_app->hwndShortcutButtons[i], GWL_STYLE);

            if (g_app->shortcutIsToggle[i])
            {
                style &= ~BS_PUSHBUTTON;
                style |= (BS_AUTOCHECKBOX | BS_PUSHLIKE);
            }
            else
            {
                style &= ~(BS_AUTOCHECKBOX | BS_CHECKBOX | BS_PUSHLIKE);
                style |= BS_PUSHBUTTON;
            }

            SetWindowLongPtrW(g_app->hwndShortcutButtons[i], GWL_STYLE, style);
            SendMessageW(g_app->hwndShortcutButtons[i], WM_SETFONT, (WPARAM)GetShortcutButtonUIFont(hwnd), TRUE);
            SetWindowPos(
                g_app->hwndShortcutButtons[i],
                nullptr,
                0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
        }

        if (!g_app->chatVisible) {
            ShowWindow(g_app->hwndChat, SW_HIDE);
        }
        ApplyStyles();

        for (int i = 0; i < INPUT_ROWS; ++i)
        {
            g_app->hwndEdit[i] = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NOHIDESEL, 0, 0, 100, 24, g_app->hwndInput, nullptr, GetModuleHandleW(nullptr), nullptr);
            SendMessageW(g_app->hwndEdit[i], WM_SETFONT, (WPARAM)g_app->hFontInput, TRUE);
            SendMessageW(g_app->hwndEdit[i], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
            SetWindowTheme(g_app->hwndEdit[i], L"", L"");
            g_app->oldEditProc[i] = (WNDPROC)SetWindowLongPtrW(g_app->hwndEdit[i], GWLP_WNDPROC, (LONG_PTR)EditSubclassProc);
        }

        FitWindowToScreenGrid(hwnd, g_app->screenCols, g_app->screenRows, false);
        LayoutChildren(hwnd);

        if (!StartTinTinProcess())
        {
            MessageBoxW(hwnd, L"ConPTY 또는 bin\\tt++.exe 실행 초기화에 실패했습니다.", L"오류", MB_ICONERROR | MB_OK);
            PostQuitMessage(1);
            return -1;
        }

        StartCaptureLog();

        g_app->readerThread = std::thread(ReaderThreadProc, hwnd, g_app->proc.stdoutRead);
        g_app->activeEditIndex = 0;
        SetFocus(g_app->hwndEdit[0]);
        SendMessageW(g_app->hwndEdit[0], EM_SETSEL, -1, -1);
        EnsureVisibleEditCaret(g_app->hwndEdit[0]);
        ApplyShortcutButtons(hwnd);
        SetInputViewLatest();

        if (!g_app->history.empty())
        {
            int n = (int)g_app->history.size();
            for (int i = 0; i < INPUT_ROWS; ++i) g_app->displayedHistoryIndex[i] = -1;
            if (n == 1) g_app->displayedHistoryIndex[1] = n - 1;
            else { g_app->displayedHistoryIndex[0] = n - 2; g_app->displayedHistoryIndex[1] = n - 1; }
            g_app->displayedHistoryIndex[2] = -1;
            ApplyInputView();
            FocusInputRow(2);
        }

        ApplyKeepAliveTimer(hwnd);

        // ★ uptime 변수가 이미 목록에 있다면 00:00:00으로 초기화만 해줍니다.
        if (g_app) {
            for (auto& v : g_app->variables) {
                if (_wcsicmp(v.name.c_str(), L"uptime") == 0) { // _wcsicmp는 대소문자 무시 비교
                    v.value = L"00:00:00";
                    break;
                }
            }
        }

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_MENU_EDIT_COPY_PAST:
            if (g_app && g_app->termBuffer) CopyToClipboard(hwnd, g_app->termBuffer->GetHistoryText());
            return 0;
        case ID_MENU_EDIT_SAVE_PAST:
            if (g_app && g_app->termBuffer) SaveTextToFile(hwnd, g_app->termBuffer->GetHistoryText());
            return 0;
        case ID_MENU_EDIT_COPY_CUR:
            if (g_app && g_app->termBuffer) CopyToClipboard(hwnd, g_app->termBuffer->GetCurrentScreenText());
            return 0;
        case ID_MENU_EDIT_SAVE_CUR:
            if (g_app && g_app->termBuffer) SaveTextToFile(hwnd, g_app->termBuffer->GetCurrentScreenText());
            return 0;
        case ID_MENU_OPTIONS_CHAT_CAPTURE:
        {
            ShowChatCaptureDialog(hwnd);
            return 0;
        }
        case ID_MENU_OPTIONS_CHAT_TOGGLE_VISIBLE:
        {
            if (!g_app || !g_app->hwndChat) break;
            g_app->chatVisible = !g_app->chatVisible;

            if (g_app->chatVisible) {
                if (g_app->chatDocked) {
                    ShowWindow(g_app->hwndChat, SW_SHOW);
                }
                else {
                    if (g_app->hwndChatFloat) ShowWindow(g_app->hwndChatFloat, SW_SHOW);
                }

                SetupChatRichEditDefaults(g_app->hwndChat);
                CHARFORMAT2W cf = {}; cf.cbSize = sizeof(cf);
                cf.dwMask = CFM_COLOR | CFM_BACKCOLOR;
                cf.crTextColor = g_app->chatStyle.textColor;
                cf.crBackColor = g_app->chatStyle.backColor;
                SendMessageW(g_app->hwndChat, EM_SETSEL, 0, -1);
                SendMessageW(g_app->hwndChat, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
                SendMessageW(g_app->hwndChat, EM_SETSEL, -1, -1);
                SendMessageW(g_app->hwndChat, EM_SETBKGNDCOLOR, 0, g_app->chatStyle.backColor);
                InvalidateRect(g_app->hwndChat, nullptr, TRUE);
            }
            else {
                if (g_app->chatDocked) {
                    ShowWindow(g_app->hwndChat, SW_HIDE);
                }
                else {
                    if (g_app->hwndChatFloat) ShowWindow(g_app->hwndChatFloat, SW_HIDE);
                }
            }

            FitWindowToScreenGrid(hwnd, g_app->screenCols, g_app->screenRows, false);
            LayoutChildren(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case ID_MENU_OPTIONS_CHAT_DOCK:
        {
            if (!g_app || !g_app->hwndChat) break;

            // ★ 핵심: 도킹을 하든 분리를 하든, 숨겨져 있다면 무조건 보이게 강제 전환!
            if (!g_app->chatVisible) {
                g_app->chatVisible = true;
                UpdateMenuToggleStates(); // 메뉴 글씨를 '숨기기'로 즉시 갱신
            }

            g_app->chatDocked = !g_app->chatDocked;

            if (g_app->chatDocked) {
                // [도킹되는 경우]
                if (g_app->hwndChatFloat) {
                    GetWindowRect(g_app->hwndChatFloat, &g_app->chatFloatRect);
                    ShowWindow(g_app->hwndChatFloat, SW_HIDE);
                }

                SetParent(g_app->hwndChat, hwnd);

                // 위에서 무조건 보이게(true) 만들었으므로 당당하게 SHOW!
                ShowWindow(g_app->hwndChat, SW_SHOW);
            }
            else {
                // [분리되는 경우]
                SetParent(g_app->hwndChat, g_app->hwndChatFloat);

                if (g_app->hwndChatFloat) {
                    SetWindowPos(g_app->hwndChatFloat, HWND_TOP,
                        g_app->chatFloatRect.left, g_app->chatFloatRect.top,
                        g_app->chatFloatRect.right - g_app->chatFloatRect.left,
                        g_app->chatFloatRect.bottom - g_app->chatFloatRect.top,
                        SWP_NOZORDER);

                    ShowWindow(g_app->hwndChatFloat, SW_SHOW);
                }

                ShowWindow(g_app->hwndChat, SW_SHOW);

                RECT rc; GetClientRect(g_app->hwndChatFloat, &rc);
                MoveWindow(g_app->hwndChat, 0, 0, rc.right, rc.bottom, TRUE);
            }

            FitWindowToScreenGrid(hwnd, g_app->screenCols, g_app->screenRows, false);
            LayoutChildren(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case ID_MENU_SETTINGS:
        {
            ShowSettingsDialog(hwnd);
            ApplyStyles();
            return 0;
        }
        case ID_MENU_STYLE_LOG_FONT:
            if (ChooseFontOnly(hwnd, g_app->logStyle.font)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return 0;
        case ID_MENU_VIEW_SMOOTH_FONT:
        {
            g_app->smoothFontEnabled = !g_app->smoothFontEnabled;
            SaveFontRenderSettings();
            ApplyStyles();
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_STYLE_LOG_COLOR:
            if (ChooseColorOnly(hwnd, g_app->logStyle.textColor)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return 0;
        case ID_MENU_STYLE_INPUT_FONT:
            if (ChooseFontOnly(hwnd, g_app->inputStyle.font)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return 0;
        case ID_MENU_STYLE_INPUT_COLOR:
            if (ChooseColorOnly(hwnd, g_app->inputStyle.textColor)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return 0;
        case ID_MENU_BG_MAIN:
            if (ChooseBackgroundColor(hwnd, g_app->logStyle.backColor)) { ApplyStyles(); QueueSaveWindowSettings(hwnd); }
            return 0;
        case ID_MENU_BG_INPUT:
            if (ChooseBackgroundColor(hwnd, g_app->inputStyle.backColor)) {
                ApplyStyles();
                if (g_app->hwndInput) { InvalidateRect(g_app->hwndInput, nullptr, TRUE); UpdateWindow(g_app->hwndInput); }
                for (int i = 0; i < INPUT_ROWS; ++i) {
                    if (g_app->hwndEdit[i]) { InvalidateRect(g_app->hwndEdit[i], nullptr, TRUE); UpdateWindow(g_app->hwndEdit[i]); }
                }
                QueueSaveWindowSettings(hwnd);
            }
            return 0;
        case ID_MENU_EDIT_MEMO:
            OpenMemoWindow(hwnd);
            return 0;
        case ID_MENU_VIEW_HIDE_MENU:
        {
            if (g_app)
            {
                g_app->menuHidden = !g_app->menuHidden;
                LayoutChildren(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
                UpdateWindow(hwnd);
            }
            return 0;
        }
        case ID_MENU_VIEW_SYMBOLS:
        {
            g_app->hwndTargetEdit = GetFocus();
            ShowSymbolDialog(hwnd);

            if (g_app->hwndSymbol && IsWindow(g_app->hwndSymbol)) {
                SetWindowPos(g_app->hwndSymbol, HWND_TOPMOST, 0, 0, 0, 0,
                    SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
                BringWindowToTop(g_app->hwndSymbol); // 추가
                SetForegroundWindow(g_app->hwndSymbol);
            }
            return 0;
        }
        case ID_MENU_HELP_SHORTCUT:
            ShowShortcutHelp(hwnd);
            return 0;
        case ID_MENU_EDIT_ABBREVIATION:
        {
            PromptAbbreviationDialog(hwnd);
            return 0;
        }
        case ID_MENU_EDIT_VARIABLE:
        {
            PromptVariableDialog(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_EDIT_TIMER:
        {
            PromptTimerDialog(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_EDIT_NUMPAD:
        {
            PromptNumpadDialog(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_EDIT_STATUSBAR: // 추가된 부분
        {
            PromptStatusBarDialog(hwnd);
            return 0;
        }

        case ID_MENU_HELP_ABOUT:
        {
            PromptAboutDialog(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_LOG_SHOW_MENU:
        {
            if (g_app)
            {
                HWND target = g_app->hwndMain ? g_app->hwndMain : hwnd;
                g_app->menuHidden = false;
                LayoutChildren(target);
                InvalidateRect(target, nullptr, TRUE);
                UpdateWindow(target);
            }
            return 0;
        }

        case ID_LOG_COPY:
            if (g_app && g_app->hwndLog) { SendMessageW(g_app->hwndLog, WM_COMMAND, MAKEWPARAM(ID_LOG_COPY, 0), 0); }
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit); ShowCaret(hEdit);
            }
            return 0;
        case ID_MENU_FILE_QUICK_CONNECT:
            ShowQuickConnectDialog(hwnd);
            return 0;
        case ID_MENU_FILE_READ_SCRIPT:
        {
            std::wstring path;
            if (ChooseScriptFile(hwnd, path)) {
                std::wstring cmd = L"#read {" + path + L"}";
                SendRawCommandToMud(cmd);
            }
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_FILE_ZAP:
            // ★ 무작정 #zap을 날리지 않고, 연결된 세션이 있을 때만 정확히 종료합니다.
            if (g_app->hasActiveSession) {
                std::wstring zapCmd = L"#zap {" + g_app->activeSession.name + L"}";
                SendRawCommandToMud(zapCmd);
                g_app->hasActiveSession = false;
            }
            g_app->isConnected = false;

            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        case ID_MENU_FILE_ADDRESSBOOK:
        {
            PromptAddressBook(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_KEEPALIVE:
        {
            bool enabled = g_app->keepAliveEnabled;
            int intervalSec = g_app->keepAliveIntervalSec;
            std::wstring command = g_app->keepAliveCommand;
            if (PromptKeepAliveSettings(hwnd, enabled, intervalSec, command)) {
                g_app->keepAliveEnabled = enabled; g_app->keepAliveIntervalSec = intervalSec; g_app->keepAliveCommand = command;
                SaveKeepAliveSettings(); ApplyKeepAliveTimer(hwnd);
            }
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_SHORTCUTBAR:
        {
            g_app->shortcutBarVisible = !g_app->shortcutBarVisible;
            ApplyShortcutButtons(hwnd); LayoutChildren(hwnd);
            if (g_app->hwndInput) { InvalidateRect(g_app->hwndInput, nullptr, TRUE); UpdateWindow(g_app->hwndInput); }
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_SAVE_INPUT_ON_EXIT:
        {
            g_app->saveInputOnExit = !g_app->saveInputOnExit;
            SaveInputHistorySettings();
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_CAPTURE_LOG:
        {
            g_app->captureLogEnabled = !g_app->captureLogEnabled;
            if (g_app->captureLogEnabled) StartCaptureLog(); else StopCaptureLog();
            SaveCaptureLogSettings(); UpdateMenuToggleStates();
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_KEEPALIVE_TOGGLE:
        {
            if (!g_app) break;
            g_app->keepAliveEnabled = !g_app->keepAliveEnabled; // 상태 반전
            SaveKeepAliveSettings(); // 즉시 파일에 저장
            ApplyKeepAliveTimer(hwnd); // 타이머 끄거나 켜기 반영
            UpdateMenuToggleStates(); // 메뉴 글씨 갱신

            // 포커스를 입력창으로 돌려줌
            if (g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }

        case ID_MENU_OPTIONS_CHAT_TIME_TOGGLE:
        {
            if (!g_app) break;
            g_app->chatTimestampEnabled = !g_app->chatTimestampEnabled; // 상태 반전
            SaveChatCaptureSettings(); // 즉시 파일에 저장
            UpdateMenuToggleStates(); // 메뉴 글씨 갱신

            if (g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_SCREEN_SIZE:
        {
            int cols = g_app->screenCols; int rows = g_app->screenRows;
            if (PromptScreenSizeSettings(hwnd, cols, rows)) {
                g_app->screenCols = cols; g_app->screenRows = rows; SaveScreenSizeSettings();
            }
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_FIT_WINDOW:
        {
            FitWindowToScreenGrid(hwnd, g_app->screenCols, g_app->screenRows, false);
            SaveWindowSettings(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_OPTIONS_SHORTCUT_EDIT:
        {
            PromptShortcutEditor(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        case ID_MENU_THEME_DIALOG:
        {
            int theme = g_app->ansiTheme;
            if (ShowThemeDialog(hwnd, &theme)) {
                g_app->ansiTheme = theme; ApplyThemeVisualsToApp(theme);
            }
            return 0;
        }
        case ID_MENU_FIND_DIALOG:
            ShowFindDialog(hwnd);
            return 0;
        case ID_MENU_EDIT_HIGHLIGHT:
            ShowHighlightDialog(hwnd);
            return 0;

        case ID_MENU_EDIT_FUNCTION_SHORTCUT:
        {
            ShowShortcutDialog(hwnd);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit);
                SendMessageW(hEdit, EM_SETSEL, -1, -1);
                EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }

        // (★ 아래처럼 수정: "끝내기" 메뉴를 누르면 트레이 옵션을 무시하고 완전히 강제 종료시킴)
        case ID_MENU_EXIT:
            // 변수는 건드리지 않고, 창을 즉시 파괴합니다. 
            // 이렇게 하면 WM_CLOSE를 거치지 않고 바로 WM_DESTROY로 가서 안전하게 저장됩니다.
            DestroyWindow(hwnd);
            return 0;

        default:
            if (LOWORD(wParam) >= ID_SHORTCUT_BUTTON_BASE && LOWORD(wParam) < ID_SHORTCUT_BUTTON_BASE + SHORTCUT_BUTTON_COUNT) {
                int idx = LOWORD(wParam) - ID_SHORTCUT_BUTTON_BASE;
                if (idx >= 0 && idx < SHORTCUT_BUTTON_COUNT) { ExecuteShortcutButton(idx); return 0; }
            }
            break;
        }
        break;

       case WM_TIMER:
       {
           if (wParam == 2001 && g_app && g_app->isSessionActive) {
               time_t now = time(NULL);
               int diff = (int)(now - g_app->sessionStartTime);
               if (diff < 0) diff = 0;

               wchar_t buf[32];
               swprintf(buf, 32, L"%02d:%02d:%02d", diff / 3600, (diff % 3600) / 60, diff % 60);

               VariableItem* targetVar = nullptr;
               for (auto& v : g_app->variables) {
                   if (_wcsicmp(v.name.c_str(), L"uptime") == 0) {
                       targetVar = &v;
                       break;
                   }
               }

               if (targetVar) {
                   targetVar->value = buf;
               }
               else {
                   VariableItem vi;
                   vi.name = L"uptime";
                   vi.value = buf;
                   vi.enabled = true;
                   vi.type = 0;
                   g_app->variables.push_back(vi);
               }

               if (g_app->hwndStatusBar) {
                   InvalidateRect(g_app->hwndStatusBar, NULL, FALSE);
               }
               return 0;
           }

           if (wParam == ID_TIMER_DEFER_SAVE) {
               KillTimer(hwnd, ID_TIMER_DEFER_SAVE);
               SaveWindowSettings(hwnd);
               return 0;
           }

           if (wParam == ID_TIMER_LOG_REDRAW) {
               KillTimer(hwnd, ID_TIMER_LOG_REDRAW);

               if (g_app)
                   g_app->logRedrawPending = false;

               if (g_app && g_app->hwndLog) {
                   InvalidateRect(g_app->hwndLog, NULL, FALSE);
               }
               return 0;
           }

           if (wParam == ID_TIMER_KEEPALIVE) {
               SendKeepAliveNow();
               return 0;
           }

           if (wParam == ID_TIMER_AUTORECONNECT) {
               KillTimer(hwnd, ID_TIMER_AUTORECONNECT);

               if (g_app && g_app->hasActiveSession) {
                   ConnectAddressBookEntry(g_app->activeSession);
               }
               return 0;
           }

           if (wParam == ID_TIMER_SWITCH_CONNECT) {
               KillTimer(hwnd, ID_TIMER_SWITCH_CONNECT);

               if (g_app && g_app->hasPendingConnect) {
                   AddressBookEntry entry = g_app->pendingConnectEntry;
                   g_app->hasPendingConnect = false;
                   ConnectAddressBookEntry(entry);
               }
               return 0;
           }

           if (wParam == ID_TIMER_USER_ENGINE) {
               RunTimerEngine();
               return 0;
           }

           break;
       }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // 1. [추가] 전체 배경색 채우기 (깜빡임 방지 및 테마 유지)
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH hbrBack = CreateSolidBrush(g_app ? g_app->mainBackColor : RGB(45, 45, 48));
        FillRect(hdc, &rc, hbrBack);
        DeleteObject(hbrBack); // GDI 객체는 바로 삭제

        // 2. 커스텀 메뉴바 그리기
        DrawCustomMenuBar(hdc, hwnd);

        // 3. 채팅창이 도킹된 경우 구분선(Separator) 그리기
        if (g_app && g_app->chatDocked && g_app->hwndChat && g_app->chatVisible) {

            // 폰트 높이 계산
            int chatFontCy = 16;
            HFONT hChatFont = g_app->hFontChat ? g_app->hFontChat : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            HFONT oldFont = (HFONT)SelectObject(hdc, hChatFont);

            TEXTMETRICW tm = {};
            GetTextMetricsW(hdc, &tm);
            chatFontCy = tm.tmHeight + tm.tmExternalLeading;
            SelectObject(hdc, oldFont);

            // 채팅창 높이 계산
            int chatHeight = (g_app->chatDockedLines * chatFontCy) + 24;
            if (chatHeight < 24) chatHeight = 24;

            int menuHeight = (!g_app->menuHidden) ? g_app->customMenuHeight : 0;

            // 구분선 위치 설정
            RECT sep = {
                0,
                menuHeight + chatHeight,
                rc.right,
                menuHeight + chatHeight + INPUT_SEPARATOR_HEIGHT
            };

            // [수정] 구분선 색상: 다크 테마에 어울리게 배경보다 약간 밝은 회색으로 변경
            // COLOR_BTNFACE 대신 직접 색상을 지정하는 것이 예쁩니다.
            HBRUSH hbrSep = CreateSolidBrush(RGB(75, 75, 80));
            FillRect(hdc, &sep, hbrSep);
            DeleteObject(hbrSep);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(g_app ? g_app->mainBackColor : RGB(45, 45, 48));
        FillRect(hdc, &rc, brush);
        DeleteObject(brush);
        return 1;
    }

    case WM_SIZE:
    {
        LayoutChildren(hwnd);
        QueueSaveWindowSettings(hwnd);
        if (g_app->hwndShortcutBar) RedrawWindow(g_app->hwndShortcutBar, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
        if (g_app->hwndInput) RedrawWindow(g_app->hwndInput, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ERASE | RDW_ALLCHILDREN);
        return 0;
    }

    case WM_MOVE:
        if (!IsIconic(hwnd)) QueueSaveWindowSettings(hwnd);
        return 0;

    case WM_EXITSIZEMOVE:
        if (!IsIconic(hwnd)) SaveWindowSettings(hwnd);
        return 0;
    case WM_APP_LOG_CHUNK:
    {
        std::unique_ptr<LogChunk> chunk(reinterpret_cast<LogChunk*>(lParam));
        if (!chunk || !g_app)
            return 0;

        static std::wstring captureBuffer;
        static DWORD s_lastPromptCheckTick = 0;

        // 1) 청크 텍스트 누적
        for (const auto& run : chunk->runs)
        {
            captureBuffer += run.text;
        }

        // 버퍼 무한 증가 방지
        const size_t MAX_CAPTURE_BUFFER = 65536;
        if (captureBuffer.size() > MAX_CAPTURE_BUFFER)
        {
            captureBuffer.erase(0, captureBuffer.size() - MAX_CAPTURE_BUFFER);
        }

        // 2) 갈무리 저장 (켜졌을 때만)
        if (g_app->captureLogEnabled)
        {
            WriteRunsToCaptureLog(chunk->runs);
        }

        bool needStatusRedraw = false;

        // 3) 완성된 줄만 처리
        size_t pos = 0;
        while ((pos = captureBuffer.find(L'\n')) != std::wstring::npos)
        {
            std::wstring line = captureBuffer.substr(0, pos);
            captureBuffer.erase(0, pos + 1);

            if (!line.empty() && line.back() == L'\r')
                line.pop_back();

            if (line.empty())
                continue;

            // 실제 서버 출력이 들어오기 시작하면 연결 상태 true
            if (!g_app->isConnected)
            {
                g_app->isConnected = true;
                needStatusRedraw = true;
            }

            // 자동 로그인: 활성 시간 안에서만
            if (g_app->autoLoginWindowActive)
            {
                RunAutoLoginEngine(line);
            }

            // 채팅 캡처: 옵션 켜졌을 때만
            if (g_app->chatCaptureEnabled)
            {
                RunChatCaptureEngine(line);
            }

            // 세션 상태 문자열 검사
            if (line.find(L"세션이 종료되었습니다.") != std::wstring::npos)
            {
                if (g_app->isConnected)
                {
                    g_app->isConnected = false;
                    needStatusRedraw = true;
                }

                g_app->autoLoginWindowActive = false;

                if (g_app->hasActiveSession && g_app->activeSession.autoReconnect)
                {
                    SetTimer(hwnd, ID_TIMER_AUTORECONNECT, 5000, nullptr);
                }
            }
            else if (line.find(L"세션이 활성화되었습니다.") != std::wstring::npos)
            {
                KillTimer(hwnd, ID_TIMER_AUTORECONNECT);
            }

            // 하이라이트 룰 검사
            for (const auto& rule : g_hiState.rules)
            {
                if (!rule.enabled || rule.pattern.empty())
                    continue;

                std::vector<std::wstring> caps;
                if (MatchHighlightPattern(rule.pattern, line, caps))
                {
                    ExecuteHighlightRuleAction(rule, caps);
                }
            }
        }

        // 4) 마지막 불완전 줄은 짧게 잘라서 제한적으로만 검사
        if (!captureBuffer.empty() && g_app->autoLoginWindowActive)
        {
            DWORD now = GetTickCount();
            if (now - s_lastPromptCheckTick >= 300)
            {
                s_lastPromptCheckTick = now;

                std::wstring promptLine = captureBuffer;
                if (promptLine.size() > 256)
                {
                    promptLine.erase(0, promptLine.size() - 256);
                }

                RunAutoLoginEngine(promptLine);
            }
        }

        // 5) 로그창 다시그리기 예약은 1회만
        if (g_app->hwndLog)
        {
            if (!g_app->logRedrawPending)
            {
                g_app->logRedrawPending = true;
                SetTimer(hwnd, ID_TIMER_LOG_REDRAW, 30, nullptr);
            }
        }

        if (needStatusRedraw && g_app->hwndStatusBar)
        {
            InvalidateRect(g_app->hwndStatusBar, nullptr, FALSE);
        }

        return 0;
    }
    case WM_APP + 4: // WM_APP_VAR_UPDATE
    {
        std::wstring* pName = (std::wstring*)wParam;
        std::wstring* pVal = (std::wstring*)lParam;

        if (g_app && pName && pVal) {
            // ★ 1. 세션 신호 처리 (session_active)
            // _wcsicmp를 사용하여 대소문자 상관없이 정확히 찾아냅니다.
            if (_wcsicmp(pName->c_str(), L"session_active") == 0) {
                if (*pVal == L"1") {
                    // 이미 작동 중이면 시간을 리셋하지 않음 (01초 멈춤 방지)
                    if (!g_app->isSessionActive) {
                        g_app->isSessionActive = true;
                        g_app->sessionStartTime = time(NULL);
                        SetTimer(hwnd, 2001, 1000, nullptr); // 1초 타이머 시작
                    }
                }
                else {
                    // 세션 종료 시 처리
                    g_app->isSessionActive = false;
                    KillTimer(hwnd, 2001); // 타이머 중지

                    // 종료 시 시간을 00:00:00으로 초기화 (중복 방지 로직 적용)
                    for (auto& v : g_app->variables) {
                        if (_wcsicmp(v.name.c_str(), L"uptime") == 0) {
                            v.value = L"00:00:00";
                            break;
                        }
                    }
                }
            }

            // ★ 2. 기존의 GUI 변수 업데이트 로직 (중복 방지 강화)
            bool found = false;
            for (auto& v : g_app->variables) {
                // pName과 일치하는 기존 변수가 있는지 확인
                if (_wcsicmp(v.name.c_str(), pName->c_str()) == 0) {
                    v.value = *pVal;
                    found = true;
                    break;
                }
            }

            // 목록에 없을 때만 새로 추가
            if (!found) {
                VariableItem vi;
                vi.enabled = true;
                vi.type = 0;
                vi.name = *pName;
                vi.value = *pVal;
                g_app->variables.push_back(vi);
            }

            // 상태바 즉시 갱신
            if (g_app->hwndStatusBar) {
                InvalidateRect(g_app->hwndStatusBar, nullptr, TRUE);
            }
        }

        // 메모리 해제 필수
        if (pName) delete pName;
        if (pVal) delete pVal;
        return 0;
    }

    case WM_APP_PROCESS_EXIT:
    {
        if (g_app) {
            if (!g_app->shuttingDown && g_app->hwndInput)
                EnableWindow(g_app->hwndInput, FALSE);
        }
        return 0;
    }

    // ★ 신규 수정: 트레이 아이콘 상호작용 (왼쪽 클릭 즉시 복구)
    case WM_APP_TRAYICON:
    {
        // lParam이 마우스 메시지 종류를 담고 있습니다.
        if (lParam == WM_LBUTTONUP) { // ★ 왼쪽 버튼에서 손을 뗄 때 (딸깍!)
            HideTrayIcon(hwnd);       // 트레이 아이콘 숨기기
            ShowWindow(hwnd, SW_SHOW); // 메인 창 보이기
            // 최소화되어 있을 수도 있으니 복구 명령 추가
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd); // 창을 맨 앞으로 가져오기
        }
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) { // 오른쪽 클릭 시 메뉴
            POINT pt;
            GetCursorPos(&pt);

            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                AppendMenuW(hMenu, MF_OWNERDRAW | MF_STRING, 1001, L"Ktin:TinTin++ GUI 열기(&O)");
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(hMenu, MF_OWNERDRAW | MF_STRING, ID_MENU_EXIT, L"완전히 종료하기(&X)");

                SetForegroundWindow(hwnd);

                int cmd = TrackPopupMenu(
                    hMenu,
                    TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                    pt.x, pt.y, 0, hwnd, nullptr);

                PostMessageW(hwnd, WM_NULL, 0, 0);
                DestroyMenu(hMenu);

                if (cmd == 1001) {
                    HideTrayIcon(hwnd);
                    ShowWindow(hwnd, SW_SHOW);
                    ShowWindow(hwnd, SW_RESTORE);
                    SetForegroundWindow(hwnd);
                }
                else if (cmd == ID_MENU_EXIT) {
                    SendMessageW(hwnd, WM_COMMAND, ID_MENU_EXIT, 0);
                }
            }
        }
        return 0;
    }

    case WM_SETFOCUS:
        if (g_app) {
            int idx = g_app->activeEditIndex;
            if (idx < 0 || idx >= INPUT_ROWS) idx = 0;
            g_app->activeEditIndex = idx;
            if (g_app->hwndEdit[idx]) { SetFocus(g_app->hwndEdit[idx]); SendMessageW(g_app->hwndEdit[idx], EM_SETSEL, -1, -1); EnsureVisibleEditCaret(g_app->hwndEdit[idx]); }
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        int hit = HitTestCustomMenuBar(x, y);
        if (hit >= 0)
        {
            g_app->pendingMenuIndex = hit;
            g_app->hotMenuIndex = hit;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }

        break;
    }
    case WM_LBUTTONUP:
    {
        if (g_app && g_app->pendingMenuIndex >= 0)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            int hit = HitTestCustomMenuBar(x, y);
            int menuIndex = g_app->pendingMenuIndex;
            g_app->pendingMenuIndex = -1;

            if (hit == menuIndex)
            {
                ShowCustomMenuPopup(hwnd, menuIndex);
            }
            else
            {
                g_app->hotMenuIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }

        break;
    }

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_KEYMENU && (char)lParam == ' ') return 0;
        break;

    case WM_CLOSE:
        // ★ 옵션이 켜져 있을 때만 트레이로 숨깁니다.
        if (g_app && g_app->closeToTray) {
            SaveWindowSettings(hwnd); // 숨기기 전에 현재 설정(체크 상태)을 파일에 먼저 저장!
            ShowTrayIcon(hwnd);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }

        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        // ★ 앱이 완전히 종료될 때 트레이 아이콘 잔상을 확실하게 제거
        HideTrayIcon(hwnd);

        KillTimer(hwnd, 2001);
        UnloadEmbeddedFont();

        KillTimer(hwnd, ID_TIMER_DEFER_SAVE); KillTimer(hwnd, ID_TIMER_KEEPALIVE);
        SaveWindowSettings(hwnd); SaveKeepAliveSettings(); SaveShortcutSettings(); 
        SaveInputHistorySettings(); SaveCaptureLogSettings(); SaveScreenSizeSettings(); 
        SaveChatCaptureSettings(); SaveAddressBook(); SaveFontRenderSettings(); 
        SaveHighlightSettings(); SaveAutoLoginSettings(); StopCaptureLog(); 
        StopProcessAndThread(); SaveVariableSettings(); SaveNumpadSettings();
        SaveGeneralSettings();  SaveTimerSettings();
        if (g_app->termBuffer) { delete g_app->termBuffer; g_app->termBuffer = nullptr; }
        UnregisterHotKey(hwnd, ID_HOTKEY_FIND_DIALOG); UnregisterHotKey(hwnd, ID_HOTKEY_FIND_NEXT); UnregisterHotKey(hwnd, ID_HOTKEY_FIND_PREV);
        if (g_app->hFontLog) { DeleteObject(g_app->hFontLog); g_app->hFontLog = nullptr; }
        if (g_app->hFontInput) { DeleteObject(g_app->hFontInput); g_app->hFontInput = nullptr; }
        if (g_app->hbrInputContainer) { DeleteObject(g_app->hbrInputContainer); g_app->hbrInputContainer = nullptr; }
        if (g_app->hbrInputEdit) { DeleteObject(g_app->hbrInputEdit); g_app->hbrInputEdit = nullptr; }
        if (g_app->hbrInputEditActive) { DeleteObject(g_app->hbrInputEditActive); g_app->hbrInputEditActive = nullptr; }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK TerminalWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ hOldBmp = SelectObject(hdcMem, hbmMem);

        COLORREF bgColor = g_app ? g_app->logStyle.backColor : RGB(0, 0, 0);
        HBRUSH hbrBg = CreateSolidBrush(bgColor);
        FillRect(hdcMem, &rc, hbrBg);
        DeleteObject(hbrBg);

        if (g_app && g_app->termBuffer && g_app->hFontLog)
        {
            std::lock_guard<std::recursive_mutex> lock(g_app->termBuffer->mtx);

            SIZE cell = GetLogCellPixelSize(hwnd);

            int offsetX = 0;
            int offsetY = 0;
            GetTerminalOffset(hwnd, offsetX, offsetY);

            HFONT hOldFont = (HFONT)SelectObject(hdcMem, g_app->hFontLog);
            SetBkMode(hdcMem, TRANSPARENT);
            SetTextAlign(hdcMem, TA_LEFT | TA_TOP | TA_NOUPDATECP);

            for (int y = 0; y < g_app->termBuffer->height; ++y)
            {
                int absY = (int)g_app->termBuffer->history.size() + y - g_app->termBuffer->scrollOffset;

                std::wstring cleanLine;
                std::vector<int> colToCharIdx(g_app->termBuffer->width, -1);

                for (int k = 0; k < g_app->termBuffer->width; ++k)
                {
                    TerminalCell tc = g_app->termBuffer->GetViewCell(k, y);
                    if (!tc.isWideTrailer)
                    {
                        colToCharIdx[k] = (int)cleanLine.length();
                        cleanLine += tc.ch;
                    }
                    else if (k > 0)
                    {
                        colToCharIdx[k] = colToCharIdx[k - 1];
                    }
                }

                // 1차 패스: 배경만 칠함
                for (int x = 0; x < g_app->termBuffer->width; ++x)
                {
                    TerminalCell c = g_app->termBuffer->GetViewCell(x, y);
                    if (c.isWideTrailer)
                        continue;

                    COLORREF drawFg = c.fg;
                    COLORREF drawBg = (c.bg == RGB(0, 0, 0)) ? bgColor : c.bg;

                    if (g_hiState.active && c.ch != L' ')
                    {
                        int charIdx = colToCharIdx[x];
                        if (charIdx != -1)
                        {
                            for (const auto& rule : g_hiState.rules)
                            {
                                if (!rule.enabled || rule.pattern.empty())
                                    continue;

                                std::vector<std::wstring> caps;
                                if (MatchHighlightPattern(rule.pattern, cleanLine, caps))
                                {
                                    size_t pos = cleanLine.find(caps[0]);
                                    while (pos != std::wstring::npos)
                                    {
                                        if ((size_t)charIdx >= pos && (size_t)charIdx < pos + caps[0].length())
                                        {
                                            if (rule.useInverse)
                                                drawBg = drawFg;
                                            else
                                                drawBg = rule.bg;
                                            goto bg_done;
                                        }
                                        pos = cleanLine.find(caps[0], pos + 1);
                                    }
                                }
                            }
                        }
                    }

                bg_done:
                    RECT cellRc{};
                    int cw = GetCharWidthW(c.ch);
                    if (cw < 1) cw = 1;
                    if (cw > 2) cw = 2;

                    cellRc.left = offsetX + x * cell.cx;
                    cellRc.top = offsetY + y * cell.cy;
                    cellRc.right = cellRc.left + cell.cx * cw;
                    cellRc.bottom = cellRc.top + cell.cy;

                    HBRUSH hbrCell = CreateSolidBrush(drawBg);
                    FillRect(hdcMem, &cellRc, hbrCell);
                    DeleteObject(hbrCell);
                }

                // 2차 패스: 글자만 그림
                for (int x = 0; x < g_app->termBuffer->width; ++x)
                {
                    TerminalCell c = g_app->termBuffer->GetViewCell(x, y);
                    if (c.isWideTrailer)
                        continue;

                    COLORREF drawFg = c.fg;
                    COLORREF drawBg = (c.bg == RGB(0, 0, 0)) ? bgColor : c.bg;

                    if (g_hiState.active && c.ch != L' ')
                    {
                        int charIdx = colToCharIdx[x];
                        if (charIdx != -1)
                        {
                            for (const auto& rule : g_hiState.rules)
                            {
                                if (!rule.enabled || rule.pattern.empty())
                                    continue;

                                std::vector<std::wstring> caps;
                                if (MatchHighlightPattern(rule.pattern, cleanLine, caps))
                                {
                                    size_t pos = cleanLine.find(caps[0]);
                                    while (pos != std::wstring::npos)
                                    {
                                        if ((size_t)charIdx >= pos && (size_t)charIdx < pos + caps[0].length())
                                        {
                                            if (rule.useInverse)
                                            {
                                                drawBg = drawFg;
                                                drawFg = bgColor;
                                            }
                                            else
                                            {
                                                drawFg = rule.fg;
                                                drawBg = rule.bg;
                                            }
                                            goto fg_done;
                                        }
                                        pos = cleanLine.find(caps[0], pos + 1);
                                    }
                                }
                            }
                        }
                    }

                fg_done:
                    RECT cellRc{};
                    int cw = GetCharWidthW(c.ch);
                    if (cw < 1) cw = 1;
                    if (cw > 2) cw = 2;

                    cellRc.left = offsetX + x * cell.cx;
                    cellRc.top = offsetY + y * cell.cy;
                    cellRc.right = cellRc.left + cell.cx * cw;
                    cellRc.bottom = cellRc.top + cell.cy;

                    if (c.ch != L' ')
                    {
                        wchar_t out[2] = { c.ch, 0 };
                        HFONT hOldFont = (HFONT)SelectObject(hdcMem, g_app->hFontLog);
                        SetTextColor(hdcMem, drawFg);
                        SetBkMode(hdcMem, TRANSPARENT);

                        SIZE glyphSz = {};
                        GetTextExtentPoint32W(hdcMem, out, 1, &glyphSz);

                        int drawX = cellRc.left;

                        if (cw == 2 && glyphSz.cx > 0)
                        {
                            int expectedWidth = cell.cx * 2;

                            if (NeedsExtraRightShiftForWideGlyph(c.ch))
                            {
                                int overlapOrMissing = expectedWidth - glyphSz.cx;
                                if (overlapOrMissing < 0)
                                    overlapOrMissing = 0;

                                drawX = cellRc.left + overlapOrMissing + 16;
                            }
                            else
                            {
                                drawX = cellRc.left;
                            }
                        }

                        TextOutW(hdcMem, drawX, cellRc.top, out, 1);
                        SelectObject(hdcMem, hOldFont);
                    }
                }
                // 3차 패스: 선택 영역
                for (int x = 0; x < g_app->termBuffer->width; ++x)
                {
                    TerminalCell c = g_app->termBuffer->GetViewCell(x, y);
                    if (c.isWideTrailer)
                        continue;

                    RECT cellRc{};
                    int cw = GetCharWidthW(c.ch);
                    if (cw < 1) cw = 1;
                    if (cw > 2) cw = 2;

                    cellRc.left = offsetX + x * cell.cx;
                    cellRc.top = offsetY + y * cell.cy;
                    cellRc.right = cellRc.left + cell.cx * cw;
                    cellRc.bottom = cellRc.top + cell.cy;

                    if (g_app->termBuffer->IsSelected(x, absY))
                    {
                        RECT selRc = cellRc;
                        InvertRect(hdcMem, &selRc);
                    }
                }
            }

            SelectObject(hdcMem, hOldFont);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOldBmp);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_VSCROLL:
    {
        if (!g_app || !g_app->termBuffer) return 0;
        int action = LOWORD(wParam);
        switch (action) {
        case SB_LINEUP: g_app->termBuffer->DoScroll(1); break;
        case SB_LINEDOWN: g_app->termBuffer->DoScroll(-1); break;
        case SB_PAGEUP: g_app->termBuffer->DoScroll(g_app->termBuffer->height / 2); break;
        case SB_PAGEDOWN: g_app->termBuffer->DoScroll(-(g_app->termBuffer->height / 2)); break;
        case SB_THUMBTRACK: {
            SCROLLINFO si = { sizeof(si), SIF_TRACKPOS }; GetScrollInfo(hwnd, SB_VERT, &si);
            g_app->termBuffer->scrollOffset = (int)g_app->termBuffer->history.size() - si.nTrackPos;
            break;
        }
        }
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) == HTCLIENT && g_app && g_app->termBuffer) {
            POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
            SIZE cell = GetLogCellPixelSize(hwnd);
            int offsetX, offsetY; GetTerminalOffset(hwnd, offsetX, offsetY); // ★ 정렬 방식 적용
            int col = (pt.x - offsetX) / cell.cx, row = (pt.y - offsetY) / cell.cy;
            int absY = (int)g_app->termBuffer->history.size() + row - g_app->termBuffer->scrollOffset;
            if (!g_app->termBuffer->GetWordAt(col, absY).empty()) {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
                return TRUE;
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    case WM_LBUTTONDOWN:
    {
        if (g_app && g_app->termBuffer) {
            SetCapture(hwnd);

            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            SIZE cell = GetLogCellPixelSize(hwnd);
            int offsetX, offsetY;
            GetTerminalOffset(hwnd, offsetX, offsetY);

            int col = (x - offsetX) / cell.cx;
            int row = (y - offsetY) / cell.cy;

            col = ClampInt(col, 0, g_app->termBuffer->width - 1);
            row = ClampInt(row, 0, g_app->termBuffer->height - 1);

            int absY = (int)g_app->termBuffer->history.size() + row - g_app->termBuffer->scrollOffset;
            g_app->termBuffer->SetSelectionStart(col, absY);
            s_logDragging = true;
            s_logMouseDownPt = { col, absY };
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (s_logDragging && g_app && g_app->termBuffer)
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);

            SIZE cell = GetLogCellPixelSize(hwnd);
            int offsetX, offsetY;
            GetTerminalOffset(hwnd, offsetX, offsetY);

            int col = (x - offsetX) / cell.cx;
            int row = (y - offsetY) / cell.cy;

            col = ClampInt(col, 0, g_app->termBuffer->width - 1);
            row = ClampInt(row, 0, g_app->termBuffer->height - 1);

            int absY = (int)g_app->termBuffer->history.size() + row - g_app->termBuffer->scrollOffset;
            g_app->termBuffer->SetSelectionEnd(col, absY);
            InvalidateRect(hwnd, nullptr, TRUE);
        }

        return 0;
    }
    case WM_MOUSELEAVE:
    {
        if (g_app)
        {
            g_app->trackingMenuMouse = false;
            if (g_app->hotMenuIndex != -1)
            {
                g_app->hotMenuIndex = -1;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (s_logDragging && g_app && g_app->termBuffer) {
            ReleaseCapture(); s_logDragging = false;
            int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
            SIZE cell = GetLogCellPixelSize(hwnd);
            int offsetX, offsetY; GetTerminalOffset(hwnd, offsetX, offsetY); // ★ 정렬 방식 적용
            int col = (x - offsetX) / cell.cx, row = (y - offsetY) / cell.cy;
            int absY = (int)g_app->termBuffer->history.size() + row - g_app->termBuffer->scrollOffset;

            if (s_logMouseDownPt.x == col && s_logMouseDownPt.y == absY) {
                g_app->termBuffer->ClearSelection();
                std::wstring word = g_app->termBuffer->GetWordAt(col, absY);
                if (!word.empty()) {
                    SendTextToMud(word);
                    if (g_app->hwndEdit[g_app->activeEditIndex]) SetFocus(g_app->hwndEdit[g_app->activeEditIndex]);
                }
            }
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_RBUTTONUP:
    {
        HMENU hMenu = CreatePopupMenu();
        if (hMenu) {
            auto AddODItem = [](HMENU hTargetMenu, UINT_PTR id, const wchar_t* text)
                {
                    AppendMenuW(hTargetMenu, MF_OWNERDRAW | MF_STRING, id, text);
                };

            if (g_app && g_app->menuHidden) {
                AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
                AddODItem(hMenu, ID_LOG_SHOW_MENU, L"상단 메뉴 보이기");
            }

            AddODItem(hMenu, ID_LOG_COPY, L"복사하기");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

            if (g_app) {
                AddODItem(hMenu, ID_MENU_OPTIONS_CHAT_DOCK,
                    g_app->chatDocked ? L"채팅 캡처창 분리(&D)" : L"채팅 캡처창 도킹(&D)");
                AddODItem(hMenu, ID_MENU_OPTIONS_CHAT_TOGGLE_VISIBLE,
                    g_app->chatVisible ? L"채팅 캡처창 숨기기(&H)" : L"채팅 캡처창 보이기(&H)");
                AddODItem(hMenu, ID_LOG_CLEAR_CHAT, L"채팅 캡처창 내용 지우기(&X)");
            }

            POINT pt;
            GetCursorPos(&pt);

            HWND hOwner = (g_app && g_app->hwndMain) ? g_app->hwndMain : hwnd;
            SetForegroundWindow(hOwner);

            TrackPopupMenu(
                hMenu,
                TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN,
                pt.x, pt.y, 0, hOwner, nullptr);

            PostMessageW(hOwner, WM_NULL, 0, 0);
            DestroyMenu(hMenu);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_LOG_COPY) {
            if (g_app && g_app->termBuffer) {

                std::wstring text = g_app->termBuffer->GetSelectedText();
                if (!text.empty() && OpenClipboard(hwnd)) {

                    EmptyClipboard();
                    size_t size = (text.size() + 1) * sizeof(wchar_t);
                    HGLOBAL hglb = GlobalAlloc(GMEM_MOVEABLE, size);
                    if (hglb) {
                        memcpy(GlobalLock(hglb), text.c_str(), size);
                        GlobalUnlock(hglb);
                        SetClipboardData(CF_UNICODETEXT, hglb);
                    }
                    CloseClipboard();
                }
                g_app->termBuffer->ClearSelection();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
        }
        else if (id == ID_LOG_SHOW_MENU) {
            if (g_app) {
                HWND target = g_app->hwndMain ? g_app->hwndMain : GetParent(hwnd);
                g_app->menuHidden = false;
                LayoutChildren(target);
                InvalidateRect(target, nullptr, TRUE);
                UpdateWindow(target);
            }
            return 0;
        }
        else if (id == ID_LOG_CLEAR_CHAT) {
            // ★ 추가: 채팅 캡처창 내용 깨끗하게 지우기
            if (g_app && g_app->hwndChat) {
                SetWindowTextW(g_app->hwndChat, L"");
            }
            return 0;
        }
        else {
            // ★ 터미널에서 처리하지 않는 명령(도킹, 숨기기 등)은 부모 창(MainWndProc)으로 즉시 패스!
            SendMessageW(GetParent(hwnd), WM_COMMAND, wParam, lParam);
            return 0;
        }
    }


    case WM_MOUSEWHEEL:
    {
        if (g_app && g_app->termBuffer) {
            int zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            g_app->termBuffer->DoScroll(zDelta > 0 ? 3 : -3);
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }

    case WM_ERASEBKGND: return 1;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, TRUE); return 0;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

// ==============================================
// EditSubclassProc
// ==============================================
static std::wstring GetInputRowText(int row)
{
    if (!g_app || row < 0 || row >= INPUT_ROWS || !g_app->hwndEdit[row])
        return L"";

    return GetEditTextW(g_app->hwndEdit[row]);
}

static bool ShiftInputViewOlder()
{
    if (!g_app)
        return false;

    int a = g_app->displayedHistoryIndex[0];
    int b = g_app->displayedHistoryIndex[1];
    int c = g_app->displayedHistoryIndex[2];

    if (a <= 0)
        return false;

    g_app->displayedHistoryIndex[0] = a - 1;
    g_app->displayedHistoryIndex[1] = a;
    g_app->displayedHistoryIndex[2] = (b >= 0) ? b : c;

    ApplyInputView();
    return true;
}

static bool ShiftInputViewNewer()
{
    if (!g_app)
        return false;

    int n = (int)g_app->history.size();
    if (n <= 0)
        return false;

    int a = g_app->displayedHistoryIndex[0];
    int b = g_app->displayedHistoryIndex[1];
    int c = g_app->displayedHistoryIndex[2];

    if (c >= 0)
    {
        if (c < n - 1)
        {
            g_app->displayedHistoryIndex[0] = b;
            g_app->displayedHistoryIndex[1] = c;
            g_app->displayedHistoryIndex[2] = c + 1;
            ApplyInputView();
            return true;
        }

        if (c == n - 1)
        {
            g_app->displayedHistoryIndex[0] = b;
            g_app->displayedHistoryIndex[1] = c;
            g_app->displayedHistoryIndex[2] = -1;
            ApplyInputView();
            return true;
        }
    }

    return false;
}


LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    int index = -1;
    for (int i = 0; i < INPUT_ROWS; ++i)
    {
        if (g_app->hwndEdit[i] == hwnd)
        {
            index = i;
            break;
        }
    }

    if (index < 0)
        return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg)
    {
    case WM_GETDLGCODE:
    {
        return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS | DLGC_WANTMESSAGE;
    }

    case WM_SETFOCUS:
    {
        g_app->activeEditIndex = index;
        g_app->historyBrowseIndex = -1;
        LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);
        EnsureVisibleEditCaret(hwnd);
        InvalidateRect(g_app->hwndInput, nullptr, TRUE);
        return lr;
    }

    case WM_KILLFOCUS:
    {
        LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);
        HideCaret(hwnd);
        InvalidateRect(g_app->hwndInput, nullptr, TRUE);
        return lr;
    }

    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
    {
        LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);
        EnsureVisibleEditCaret(hwnd);
        if (GetFocus() == hwnd)
        {
            ShowCaret(hwnd);
            SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
        }
        return lr;
    }

    case WM_SYSKEYDOWN:
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        if (HandleFunctionKey((int)wParam))
            return 0;

        if (alt && !ctrl && !shift && wParam == VK_SPACE)
        {
            for (int i = 0; i < INPUT_ROWS; ++i) {
                if (g_app->hwndEdit[i]) SetWindowTextW(g_app->hwndEdit[i], L"");
            }

            if (g_app) {
                g_app->history.clear();                    // ← 추가
                g_app->displayedHistoryIndex[0] = -1;
                g_app->displayedHistoryIndex[1] = -1;
                g_app->displayedHistoryIndex[2] = -1;
                SetInputViewLatest();
            }

            FocusInputRow(0);
            return 0;
        }

        // 기존의 Alt + 숫자 단축키 로직 유지
        if (alt && !ctrl && !shift)
        {
            int shortcutIdx = -1;
            if (wParam >= '1' && wParam <= '9') shortcutIdx = (int)(wParam - '1');
            else if (wParam == '0') shortcutIdx = 9;
            else if (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9) shortcutIdx = (int)(wParam - VK_NUMPAD1);
            else if (wParam == VK_NUMPAD0) shortcutIdx = 9;

            if (shortcutIdx >= 0 && shortcutIdx < SHORTCUT_BUTTON_COUNT)
            {
                ExecuteShortcutButton(shortcutIdx);
                return 0;
            }
        }
        break;
    }

    case WM_SYSCHAR:
    {
        // Alt + Space 시 발생하는 시스템 비프음 및 메뉴 활성화 차단
        if (wParam == VK_SPACE) return 0;
        break;
    }

    case WM_KEYDOWN:
    {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;

        // F4 키가 눌렸을 때 (다른 조합 없이 순수하게 F4만 눌린 경우)
        if (wParam == VK_F4 && !ctrl && !alt && !shift)
        {
            // 메인 윈도우에 특수기호 메뉴 클릭 메시지를 강제로 보냅니다.
            SendMessageW(g_app->hwndMain, WM_COMMAND, ID_MENU_VIEW_SYMBOLS, 0);
            return 0; // 여기서 처리를 끝내서 에디트 컨트롤이나 매크로가 가로채지 못하게 함
        }

        if (HandleFunctionKey((int)wParam))
            return 0;

        if (ctrl && wParam == 'A')
        {
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            EnsureVisibleEditCaret(hwnd);
            return 0;
        }

        // --- 로그창 스크롤 및 블럭 지정 단축키 (회원님 요청 로직) ---
        if (!ctrl && !shift && !alt && wParam == VK_PRIOR) {
            if (g_app && g_app->termBuffer) { g_app->termBuffer->DoScroll(g_app->termBuffer->height / 2); InvalidateRect(g_app->hwndLog, nullptr, TRUE); }
            return 0;
        }
        if (!ctrl && !shift && !alt && wParam == VK_NEXT) {
            if (g_app && g_app->termBuffer) { g_app->termBuffer->DoScroll(-(g_app->termBuffer->height / 2)); InvalidateRect(g_app->hwndLog, nullptr, TRUE); }
            return 0;
        }
        if (ctrl && !shift && !alt && wParam == VK_HOME) {
            if (g_app && g_app->termBuffer) { g_app->termBuffer->scrollOffset = (int)g_app->termBuffer->history.size(); InvalidateRect(g_app->hwndLog, nullptr, TRUE); }
            return 0;
        }
        if (ctrl && !shift && !alt && wParam == VK_END) {
            if (g_app && g_app->termBuffer) { g_app->termBuffer->scrollOffset = 0; InvalidateRect(g_app->hwndLog, nullptr, TRUE); }
            return 0;
        }
        if (ctrl && shift && !alt && wParam == VK_HOME) {
            if (g_app && g_app->termBuffer) {
                int currentViewBottomY = (int)g_app->termBuffer->history.size() + g_app->termBuffer->height - 1 - g_app->termBuffer->scrollOffset;
                g_app->termBuffer->SetSelectionStart(0, 0);
                g_app->termBuffer->SetSelectionEnd(g_app->termBuffer->width - 1, currentViewBottomY);
                InvalidateRect(g_app->hwndLog, nullptr, TRUE);
            }
            return 0;
        }
        if (ctrl && shift && !alt && wParam == VK_END) {
            if (g_app && g_app->termBuffer) {
                int currentViewTopY = (int)g_app->termBuffer->history.size() - g_app->termBuffer->scrollOffset;
                int absoluteBottom = (int)g_app->termBuffer->history.size() + g_app->termBuffer->height - 1;
                g_app->termBuffer->SetSelectionStart(0, currentViewTopY);
                g_app->termBuffer->SetSelectionEnd(g_app->termBuffer->width - 1, absoluteBottom);
                InvalidateRect(g_app->hwndLog, nullptr, TRUE);
            }
            return 0;
        }

        // --- 화면 및 입력창 지우기 ---
        if (ctrl && !alt && !shift && wParam == VK_SPACE) {
            ClearLogWindow(false);
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit); SendMessageW(hEdit, EM_SETSEL, -1, -1); EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        if (ctrl && shift && !alt && wParam == VK_SPACE) {
            // 1. 로그창 완전 초기화
            ClearLogWindow(true);

            // 2. 입력창 히스토리 완전 삭제 (이 부분이 빠져 있어서 문제가 발생)
            if (g_app) {
                g_app->history.clear();                    // ← 핵심: 히스토리 벡터 완전 삭제
                g_app->displayedHistoryIndex[0] = -1;
                g_app->displayedHistoryIndex[1] = -1;
                g_app->displayedHistoryIndex[2] = -1;
                SetInputViewLatest();                      // ← 입력창 화면도 최신 상태로 초기화
            }

            // 3. 입력창 포커스 복구
            if (g_app && g_app->hwndEdit[g_app->activeEditIndex]) {
                HWND hEdit = g_app->hwndEdit[g_app->activeEditIndex];
                SetFocus(hEdit);
                SendMessageW(hEdit, EM_SETSEL, -1, -1);
                EnsureVisibleEditCaret(hEdit);
            }
            return 0;
        }
        // ★ 숫자 키패드 매크로 가로채기 (NumLock이 켜져 있을 때만 작동)
        if (!ctrl && !shift && !alt && g_app && g_app->numpadMacroEnabled) {
            int npIdx = -1;
            if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) npIdx = (int)(wParam - VK_NUMPAD0);
            else if (wParam == VK_DIVIDE) npIdx = 10;   // /
            else if (wParam == VK_MULTIPLY) npIdx = 11; // *
            else if (wParam == VK_SUBTRACT) npIdx = 12; // -
            else if (wParam == VK_ADD) npIdx = 13;      // +
            else if (wParam == VK_DECIMAL) npIdx = 14;  // .

            if (npIdx >= 0 && !g_app->numpadMacros[npIdx].empty()) {
                SendRawCommandToMud(g_app->numpadMacros[npIdx]);
                return 0; // 0을 반환해서 입력창에 글씨가 안 써지게 아예 먹어버림!
            }
        }


        // --- 키보드 세부 이동 로직 (원본 100% 복원) ---
        switch (wParam)
        {
        case VK_RETURN:
        {
            std::wstring line = GetInputRowText(index);

            if (Trim(line).empty())
            {
                // 콘솔에서 그냥 Enter 친 것과 동일하게 CR만 전송
                SendCommandToProcess(L"");
            }
            else
            {
                SendTextToMud(line);
                g_app->history.push_back(line);
            }

            SetInputViewLatest();

            if (g_app && g_app->hwndEdit[g_app->activeEditIndex])
            {
                SetFocus(g_app->hwndEdit[g_app->activeEditIndex]);
                SendMessageW(g_app->hwndEdit[g_app->activeEditIndex], EM_SETSEL, -1, -1);
                EnsureVisibleEditCaret(g_app->hwndEdit[g_app->activeEditIndex]);
            }

            return 0;
        }
        case VK_BACK:
        {
            DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
            int start = LOWORD(sel);
            int end = HIWORD(sel);

            if (start == end && start == 0) // 커서가 맨 앞일 때
            {
                if (index > 0) {
                    if (g_app && g_app->limitBackspaceToCurrentRow) return 0; // 제한 옵션 시 무시

                    // 이전 줄로 이동 로직
                    std::wstring prev = GetEditTextW(g_app->hwndEdit[index - 1]);
                    int prevLen = (int)prev.size();
                    g_app->activeEditIndex = index - 1;
                    SetFocus(g_app->hwndEdit[index - 1]);
                    SendMessageW(g_app->hwndEdit[index - 1], EM_SETSEL, prevLen, prevLen);
                    return 0;
                }
                else {
                    // ★ 0번 인덱스(첫 줄) 맨 앞에서 백스페이스 누를 때 소리 방지
                    return 0;
                }
            }
            break;
        }

        case VK_HOME:
        {
            if (ctrl) break;
            int target = 0;
            if (shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                SendMessageW(hwnd, EM_SETSEL, HIWORD(sel), target);
            }
            else {
                SendMessageW(hwnd, EM_SETSEL, target, target);
            }
            EnsureVisibleEditCaret(hwnd);
            return 0;
        }

        case VK_END:
        {
            if (ctrl) break;
            int len = GetWindowTextLengthW(hwnd);
            if (shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                SendMessageW(hwnd, EM_SETSEL, LOWORD(sel), len);
            }
            else {
                SendMessageW(hwnd, EM_SETSEL, len, len);
            }
            EnsureVisibleEditCaret(hwnd);
            return 0;
        }

        case VK_LEFT:
        {
            if (!ctrl && !shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                if (LOWORD(sel) == HIWORD(sel) && LOWORD(sel) == 0 && index > 0) {
                    std::wstring prev = GetEditTextW(g_app->hwndEdit[index - 1]);
                    int prevLen = (int)prev.size();
                    g_app->activeEditIndex = index - 1;
                    SetFocus(g_app->hwndEdit[index - 1]);
                    SendMessageW(g_app->hwndEdit[index - 1], EM_SETSEL, prevLen, prevLen);
                    EnsureVisibleEditCaret(g_app->hwndEdit[index - 1]);
                    return 0;
                }
            }
            break;
        }

        case VK_RIGHT:
        {
            if (!ctrl && !shift) {
                DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
                int len = GetWindowTextLengthW(hwnd);
                if (LOWORD(sel) == HIWORD(sel) && LOWORD(sel) == len && index < INPUT_ROWS - 1) {
                    g_app->activeEditIndex = index + 1;
                    SetFocus(g_app->hwndEdit[index + 1]);
                    SendMessageW(g_app->hwndEdit[index + 1], EM_SETSEL, 0, 0);
                    EnsureVisibleEditCaret(g_app->hwndEdit[index + 1]);
                    return 0;
                }
            }
            break;
        }

        case VK_UP:
            if (!ctrl && !shift) {
                if (index > 0) FocusInputRow(index - 1);
                else if (ShiftInputViewOlder()) FocusInputRow(0);
                return 0;
            }
            break;

        case VK_DOWN:
            if (!ctrl && !shift) {
                if (index < INPUT_ROWS - 1) FocusInputRow(index + 1);
                else if (ShiftInputViewNewer()) FocusInputRow(INPUT_ROWS - 1);
                return 0;
            }
            break;
        }
        break;
    }

    case WM_CHAR:
    case WM_IME_CHAR:
    {
        if (wParam == VK_RETURN || wParam == '\r' || wParam == '\n')
            return 0;

        if (wParam == VK_BACK)
        {
            DWORD sel = (DWORD)SendMessageW(hwnd, EM_GETSEL, 0, 0);
            if (LOWORD(sel) == 0 && HIWORD(sel) == 0)
                return 0;
        }

        if (wParam == 1) { // Ctrl+A
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
        break;
    }
    }

    LRESULT lr = CallWindowProcW(g_app->oldEditProc[index], hwnd, msg, wParam, lParam);

    if (msg == WM_KEYUP || msg == WM_CHAR || msg == WM_PASTE || msg == EM_REPLACESEL) {
        EnsureVisibleEditCaret(hwnd);
        if (GetFocus() == hwnd) ShowCaret(hwnd);
    }

    return lr;
}

LRESULT CALLBACK ChatEditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_RBUTTONUP:
    {
        // [핵심 로직] RichEdit은 원본 WM_RBUTTONUP 내부에서 기본 메뉴를 띄워버리는 경우가 많습니다.
        // 원본으로 메시지가 넘어가지 않도록 차단(return 0)하고, 우리가 직접 WM_CONTEXTMENU를 호출합니다.
        POINT pt;
        GetCursorPos(&pt);
        SendMessageW(hwnd, WM_CONTEXTMENU, (WPARAM)hwnd, MAKELPARAM(pt.x, pt.y));
        return 0;
    }

    case WM_CONTEXTMENU:
    {
        HMENU hMenu = CreatePopupMenu();
        if (hMenu)
        {
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_CUT, L"잘라내기(&T)\tCtrl+X");
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_COPY, L"복사하기(&C)\tCtrl+C");
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_PASTE, L"붙여넣기(&P)\tCtrl+V");
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_DELETE, L"삭제(&D)\tDel");
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_DELETE_LINE, L"행 삭제");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_SELECT_ALL, L"모두 선택(&A)\tCtrl+A");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_CHAT_CLEAR_ALL, L"내용 지우기 (히스토리 포함)");

            int xPos = (short)LOWORD(lParam);
            int yPos = (short)HIWORD(lParam);

            if (xPos == -1 && yPos == -1) // 키보드 메뉴 키 대응
            {
                POINT pt;
                GetCursorPos(&pt);
                xPos = pt.x;
                yPos = pt.y;
            }

            SetForegroundWindow(g_app && g_app->hwndMain ? g_app->hwndMain : hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN, xPos, yPos, 0, hwnd, nullptr);
            DestroyMenu(hMenu);

            switch (cmd)
            {
            case ID_CHAT_CUT:         SendMessageW(hwnd, WM_CUT, 0, 0); break;
            case ID_CHAT_COPY:        SendMessageW(hwnd, WM_COPY, 0, 0); break;
            case ID_CHAT_PASTE:       SendMessageW(hwnd, WM_PASTE, 0, 0); break;
            case ID_CHAT_DELETE:      SendMessageW(hwnd, WM_CLEAR, 0, 0); break;
            case ID_CHAT_DELETE_LINE: SendMessageW(hwnd, EM_REPLACESEL, FALSE, (LPARAM)L""); break;
            case ID_CHAT_SELECT_ALL:  SendMessageW(hwnd, EM_SETSEL, 0, -1); break;
            case ID_CHAT_CLEAR_ALL:
                if (g_app)
                {
                    SetWindowTextW(hwnd, L"");
                    g_app->history.clear();
                    g_app->displayedHistoryIndex[0] = -1;
                    g_app->displayedHistoryIndex[1] = -1;
                    g_app->displayedHistoryIndex[2] = -1;
                    SetInputViewLatest();
                }
                break;
            }
        }
        return 0; // 처리 완료 후 0 반환
    }
    }

    // WM_RBUTTONDOWN 등 나머지 모든 메시지는 원본 프로시저가 정상 처리 (우클릭 커서 이동 지원)
    return CallWindowProcW(g_app->oldChatProc, hwnd, msg, wParam, lParam);
}
// ==============================================
// InputContainerProc
// ==============================================
static LRESULT CALLBACK InputContainerProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);

        COLORREF bg = RGB(0, 0, 0);
        if (g_app)
            bg = g_app->inputStyle.backColor;

        HBRUSH hBrush = CreateSolidBrush(bg);
        FillRect(hdc, &rc, hBrush);
        DeleteObject(hBrush);
        return 1;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC hdc = (HDC)wParam;
        HWND hEdit = (HWND)lParam;

        COLORREF back = g_app ? g_app->inputStyle.backColor : RGB(20, 20, 20);
        COLORREF text = g_app ? g_app->inputStyle.textColor : RGB(230, 230, 230);

        SetTextColor(hdc, text);
        SetBkColor(hdc, back);
        SetBkMode(hdc, OPAQUE);

        if (g_app)
        {
            for (int i = 0; i < INPUT_ROWS; ++i)
            {
                if (g_app->hwndEdit[i] == hEdit)
                    return (INT_PTR)g_app->hbrInputEdit;
            }
        }

        return (INT_PTR)(g_app && g_app->hbrInputEdit ? g_app->hbrInputEdit : (HBRUSH)(COLOR_WINDOW + 1));
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        HBRUSH hbrBack = CreateSolidBrush(g_app ? g_app->inputStyle.backColor : RGB(20, 20, 20));
        FillRect(hdc, &rc, hbrBack);
        DeleteObject(hbrBack);

        // 단축키 바가 숨겨져 있을 때 그리는 구분선
        if (!g_app || !g_app->shortcutBarVisible)
        {
            RECT sep = rc;
            sep.bottom = sep.top + INPUT_SEPARATOR_HEIGHT;

            // ★ 수정됨: 여기서도 메뉴 바와 같은 윈도우 기본 색상으로 변경합니다.
            FillRect(hdc, &sep, GetSysColorBrush(COLOR_BTNFACE));
        }

        EndPaint(hwnd, &ps);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// ShortcutBarProc
// ==============================================
static LRESULT CALLBACK ShortcutBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        HPEN hPenDark = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        HPEN hPenLight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenDark);

        MoveToEx(hdc, 0, 0, nullptr);
        LineTo(hdc, rc.right, 0);

        SelectObject(hdc, hPenLight);
        MoveToEx(hdc, 0, rc.bottom - 1, nullptr);
        LineTo(hdc, rc.right, rc.bottom - 1);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPenDark);
        DeleteObject(hPenLight);
        return 1;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        HPEN hPenDark = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
        HPEN hPenLight = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DHILIGHT));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPenDark);

        MoveToEx(hdc, 0, 0, nullptr);
        LineTo(hdc, rc.right, 0);

        SelectObject(hdc, hPenLight);
        MoveToEx(hdc, 0, rc.bottom - 1, nullptr);
        LineTo(hdc, rc.right, rc.bottom - 1);

        SelectObject(hdc, hOldPen);
        DeleteObject(hPenDark);
        DeleteObject(hPenLight);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC: // ★ 토글 버튼은 Static 메시지로 올 때가 많습니다.
    {
        HDC hdc = (HDC)wParam;
        SetBkMode(hdc, TRANSPARENT);
        // 버튼 텍스트 색상이 흰색 배경에 묻히지 않게 검정색으로 강제 설정
        SetTextColor(hdc, RGB(0, 0, 0));
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_COMMAND:
    {
        HWND hParent = GetParent(hwnd);
        if (hParent)
            return (LRESULT)SendMessageW(hParent, WM_COMMAND, wParam, lParam);
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// StatusBarProc
// ==============================================
static std::wstring ExpandStatusVariables(const std::wstring& format) {
    if (!g_app) return format;
    std::wstring result = format;

    for (const auto& var : g_app->variables) {
        std::wstring target = L"$" + var.name;
        size_t pos = 0;
        while ((pos = result.find(target, pos)) != std::wstring::npos) {

            // ★ 수정된 핵심 부분: var.value를 그냥 넣지 않고 함수를 거쳐서 넣습니다!
            std::wstring formattedValue = FormatNumberWithCommas(var.value);

            result.replace(pos, target.length(), formattedValue);
            pos += formattedValue.length();
        }
    }
    return result;
}

static LRESULT CALLBACK StatusBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        // 1. 배경색 칠하기
        FillRect(hdc, &rc, GetSysColorBrush(COLOR_BTNFACE));

        if (g_app) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0)); // 글자색 검정
            HFONT hFont = GetShortcutButtonUIFont(hwnd);
            HFONT hOld = (HFONT)SelectObject(hdc, hFont);

            int count = g_app->statusPartCount;
            if (count < 1) count = 1;
            int partW = (rc.right - rc.left) / count; // 한 칸의 너비 계산

            for (int i = 0; i < count; ++i) {
                std::wstring text = ExpandStatusVariables(g_app->statusFormats[i]);

                RECT partRc = { rc.left + (i * partW), rc.top, rc.left + ((i + 1) * partW), rc.bottom };
                InflateRect(&partRc, -8, 0);

                // 정렬 옵션 결정
                UINT alignFlag = DT_LEFT; // 기본값
                if (g_app->statusAligns[i] == 1) alignFlag = DT_CENTER;
                else if (g_app->statusAligns[i] == 2) alignFlag = DT_RIGHT;

                // alignFlag를 적용하여 출력
                DrawTextW(hdc, text.c_str(), -1, &partRc, alignFlag | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
                // 칸 구분선 그리기 (마지막 칸이 아닐 때만)
                if (i < count - 1) {
                    HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_3DSHADOW));
                    HGDIOBJ oldP = SelectObject(hdc, hPen);
                    MoveToEx(hdc, rc.left + ((i + 1) * partW), rc.top + 4, NULL);
                    LineTo(hdc, rc.left + ((i + 1) * partW), rc.bottom - 4);
                    SelectObject(hdc, oldP);
                    DeleteObject(hPen);
                }
            }
            SelectObject(hdc, hOld);
        }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// ChatFloatWndProc
// ==============================================
static LRESULT CALLBACK ChatFloatWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_app && g_app->hwndChat && GetParent(g_app->hwndChat) == hwnd) {
            MoveWindow(g_app->hwndChat, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        return 0;
    case WM_CLOSE:
        // ★ 수정됨: X버튼을 누를 때 현재 보이기 상태면 '도킹'으로 자동 전환시킵니다.
        if (g_app && g_app->hwndMain) {
            if (g_app->chatVisible) {
                SendMessageW(g_app->hwndMain, WM_COMMAND, ID_MENU_OPTIONS_CHAT_DOCK, 0);
            }
            else {
                SendMessageW(g_app->hwndMain, WM_COMMAND, ID_MENU_OPTIONS_CHAT_TOGGLE_VISIBLE, 0);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

