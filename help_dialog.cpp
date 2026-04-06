#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "help_dialog.h"
#include "theme.h"
#include "resource.h"
#include "settings.h"
#include <richedit.h>
#include <commctrl.h>

static std::wstring BuildHelpPageText(int page);

// ==============================================
// RichEdit 서식 도우미 함수
// ==============================================
static void HelpSetCharFormatRange(HWND hEdit, LONG cpMin, LONG cpMax, LONG yHeight, COLORREF color, bool bold)
{
    CHARRANGE cr{};
    cr.cpMin = cpMin;
    cr.cpMax = cpMax;
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    CHARFORMAT2W cf{};
    cf.cbSize = sizeof(cf);
    cf.dwMask = CFM_SIZE | CFM_COLOR | CFM_BOLD;
    cf.yHeight = yHeight;
    cf.crTextColor = color;
    cf.dwEffects = bold ? CFE_BOLD : 0;

    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}

static LONG HelpFindLineEnd(const std::wstring& text, LONG start)
{
    size_t pos = text.find(L"\r\n", (size_t)start);
    if (pos == std::wstring::npos)
        return (LONG)text.size();
    return (LONG)pos;
}

static void SetHelpRichEditText(HWND hEdit, const std::wstring& text)
{
    HFONT hUiFont = GetPopupUIFont(hEdit);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hUiFont, TRUE);

    SetWindowTextW(hEdit, text.c_str());

    // 전체 기본 서식 초기화
    CHARRANGE all{};
    all.cpMin = 0;
    all.cpMax = -1;
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&all);

    CHARFORMAT2W base{};
    base.cbSize = sizeof(base);
    base.dwMask = CFM_SIZE | CFM_COLOR | CFM_BOLD;
    base.yHeight = 220; // 본문
    base.crTextColor = RGB(235, 235, 235);
    base.dwEffects = 0;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&base);

    // 내부 여백만 적용
    RECT rc{};
    GetClientRect(hEdit, &rc);
    rc.left += 14;
    rc.top += 10;
    rc.right -= 14;
    rc.bottom -= 10;
    SendMessageW(hEdit, EM_SETRECTNP, 0, (LPARAM)&rc);

    // 첫 줄 끝 찾기
    LONG line1Start = 0;
    LONG line1End = HelpFindLineEnd(text, line1Start);

    // 둘째 줄 찾기
    LONG line2Start = line1End;
    if (line2Start < (LONG)text.size() && text.compare((size_t)line2Start, 2, L"\r\n") == 0)
        line2Start += 2;
    LONG line2End = HelpFindLineEnd(text, line2Start);

    // 첫 줄: 큰 제목
    HelpSetCharFormatRange(
        hEdit,
        line1Start,
        line1End,
        320,
        RGB(255, 255, 255),
        true);

    // 둘째 줄이 비어있지 않을 때만 부제 처리
    if (line2Start < line2End)
    {
        HelpSetCharFormatRange(
            hEdit,
            line2Start,
            line2End,
            190,
            RGB(180, 185, 190),
            false);
    }

    // 나머지 본문
    LONG bodyStart = line2End;
    if (bodyStart < (LONG)text.size() && text.compare((size_t)bodyStart, 2, L"\r\n") == 0)
        bodyStart += 2;

    if (bodyStart < (LONG)text.size())
    {
        HelpSetCharFormatRange(
            hEdit,
            bodyStart,
            -1,
            220,
            RGB(235, 235, 235),
            false);
    }

    SendMessageW(hEdit, EM_SETSEL, 0, 0);
    SendMessageW(hEdit, EM_SCROLLCARET, 0, 0);
    InvalidateRect(hEdit, nullptr, TRUE);
}


// ==============================================
// 도움말 창 프로시저
// ==============================================
static LRESULT CALLBACK ShortcutHelpProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HWND hTitle = nullptr;
    static HWND hSub = nullptr;
    static HWND hList = nullptr;
    static HWND hView = nullptr;
    static HWND hClose = nullptr;
    static HFONT hFontTitle = nullptr;
    static HFONT hFontSub = nullptr;
    static HFONT hFontUi = nullptr;
    static HBRUSH hbrBack = nullptr;
    static HBRUSH hbrPanel = nullptr;

    switch (msg)
    {
    case WM_CREATE:
    {
        RECT rc = { 0, 0, 920, 640 };
        AdjustWindowRectEx(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
        SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

        ApplyPopupTitleBarTheme(hwnd);
        hbrBack = CreateSolidBrush(RGB(32, 34, 37));
        hbrPanel = CreateSolidBrush(RGB(43, 45, 49));

        LOGFONTW lf = {};
        lf.lfHeight = -22;
        lf.lfWeight = FW_BOLD;
        lstrcpyW(lf.lfFaceName, L"맑은 고딕");
        hFontTitle = CreateFontIndirectW(&lf);

        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -15;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"맑은 고딕");
        hFontSub = CreateFontIndirectW(&lf);

        ZeroMemory(&lf, sizeof(lf));
        lf.lfHeight = -16;
        lf.lfWeight = FW_NORMAL;
        lstrcpyW(lf.lfFaceName, L"맑은 고딕");
        hFontUi = CreateFontIndirectW(&lf);

        hTitle = CreateWindowExW(
            0, L"STATIC", L"Ktin : TinTin++ GUI 도움말",
            WS_CHILD | WS_VISIBLE,
            24, 18, 360, 32,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        hSub = CreateWindowExW(
            0, L"STATIC", L"현재 소스 기준 기능과 단축키를 정리한 도움말입니다.",
            WS_CHILD | WS_VISIBLE,
            24, 52, 520, 22,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        hList = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
            24, 92, 220, 470,
            hwnd, (HMENU)10001, GetModuleHandleW(nullptr), nullptr);

        hView = CreateWindowExW(
            WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
            260, 92, 630, 470,
            hwnd, (HMENU)10002, GetModuleHandleW(nullptr), nullptr);

        // ★ 닫기 버튼에 &C 추가 (ALT+C 단축키)
        hClose = CreateWindowExW(
            0, L"BUTTON", L"닫기(&C)",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            790, 578, 100, 32,
            hwnd, (HMENU)IDCANCEL, GetModuleHandleW(nullptr), nullptr);

        SendMessageW(hTitle, WM_SETFONT, (WPARAM)hFontTitle, TRUE);
        SendMessageW(hSub, WM_SETFONT, (WPARAM)hFontSub, TRUE);
        SendMessageW(hList, WM_SETFONT, (WPARAM)hFontUi, TRUE);
        SendMessageW(hView, WM_SETFONT, (WPARAM)hFontUi, TRUE);
        SendMessageW(hClose, WM_SETFONT, (WPARAM)hFontUi, TRUE);

        SendMessageW(hView, EM_SETBKGNDCOLOR, 0, RGB(24, 26, 27));

        RECT rcView{};
        GetClientRect(hView, &rcView);
        rcView.left += 12;
        rcView.top += 12;
        rcView.right -= 12;
        rcView.bottom -= 12;
        SendMessageW(hView, EM_SETRECTNP, 0, (LPARAM)&rcView);

        const wchar_t* cats[] = {
            L"기본 / 단축키",
            L"연결 / 주소록",
            L"입력 / 로그창",
            L"단축버튼 / 줄임말",
            L"트리거 / 변수",
            L"채팅 캡쳐",
            L"환경설정 / 화면",
            L"메모장 / 특수기호",
            L"트리거 변수 / 상태바"
        };
        for (int i = 0; i < (int)(sizeof(cats) / sizeof(cats[0])); ++i)
            SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)cats[i]);

        SendMessageW(hList, LB_SETCURSEL, 0, 0);
        SetHelpRichEditText(hView, BuildHelpPageText(0));

        return 0;
    }

    // ★★★ ALT + C 단축키 처리 추가 ★★★
    case WM_SYSCHAR:
    {
        wchar_t ch = towlower((wchar_t)wParam);
        if (ch == 'c')
        {
            SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
            return 0;
        }
        break;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);
        const int margin = 24;
        const int topY1 = 18;
        const int topY2 = 52;
        const int contentTop = 92;
        const int leftW = 220;
        const int gap = 16;
        const int btnW = 100;
        const int btnH = 32;
        const int bottomMargin = 18;

        int btnX = w - margin - btnW;
        int btnY = h - bottomMargin - btnH;
        int contentBottom = btnY - 14;
        int contentH = contentBottom - contentTop;
        if (contentH < 100) contentH = 100;

        int rightX = margin + leftW + gap;
        int rightW = w - rightX - margin;

        MoveWindow(hTitle, margin, topY1, w - margin * 2, 32, TRUE);
        MoveWindow(hSub, margin, topY2, w - margin * 2, 22, TRUE);
        MoveWindow(hList, margin, contentTop, leftW, contentH + 3, TRUE);
        MoveWindow(hView, rightX, contentTop, rightW, contentH, TRUE);
        MoveWindow(hClose, btnX, btnY, btnW, btnH, TRUE);

        if (hView)
        {
            RECT rcView{};
            GetClientRect(hView, &rcView);
            rcView.left += 14;
            rcView.top += 10;
            rcView.right -= 14;
            rcView.bottom -= 10;
            SendMessageW(hView, EM_SETRECTNP, 0, (LPARAM)&rcView);
        }
        return 0;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 10001 && HIWORD(wParam) == LBN_SELCHANGE)
        {
            int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);
            if (sel >= 0)
                SetHelpRichEditText(hView, BuildHelpPageText(sel));
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOK)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }

    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = (HDC)wParam;
        HWND hCtl = (HWND)lParam;
        SetBkMode(hdc, TRANSPARENT);
        if (hCtl == hTitle)
        {
            SetTextColor(hdc, RGB(255, 255, 255));
            return (INT_PTR)hbrBack;
        }
        if (hCtl == hSub)
        {
            SetTextColor(hdc, RGB(180, 185, 190));
            return (INT_PTR)hbrBack;
        }
        SetTextColor(hdc, RGB(220, 220, 220));
        return (INT_PTR)hbrBack;
    }

    case WM_ERASEBKGND:
    {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, hbrBack);

        RECT rcPanelLeft = { 20, 88, 248, 566 };
        RECT rcPanelRight = { 256, 88, 894, 566 };
        FillRect(hdc, &rcPanelLeft, hbrPanel);
        FillRect(hdc, &rcPanelRight, hbrPanel);
        return 1;
    }

    case WM_DESTROY:
    {
        if (hFontTitle) { DeleteObject(hFontTitle); hFontTitle = nullptr; }
        if (hFontSub) { DeleteObject(hFontSub); hFontSub = nullptr; }
        if (hFontUi) { DeleteObject(hFontUi); hFontUi = nullptr; }
        if (hbrBack) { DeleteObject(hbrBack); hbrBack = nullptr; }
        if (hbrPanel) { DeleteObject(hbrPanel); hbrPanel = nullptr; }
        return 0;
    }
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==============================================
// 도움말 창 표시 함수
// ==============================================
void ShowShortcutHelp(HWND owner)
{
    static const wchar_t* kClass = L"TTGuiShortcutHelpClass";
    static bool registered = false;

    if (!registered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = ShortcutHelpProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassW(&wc);
        registered = true;
    }

    int w = 920;
    int h = 640;

    // 부모 창(메인 프로그램)의 위치와 크기 가져오기
    RECT rcOwner = { 0 };
    if (owner && IsWindow(owner)) {
        GetWindowRect(owner, &rcOwner);
    }
    else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcOwner, 0);
    }

    // 정중앙 좌표 계산
    int x = rcOwner.left + ((rcOwner.right - rcOwner.left) - w) / 2;
    int y = rcOwner.top + ((rcOwner.bottom - rcOwner.top) - h) / 2;

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        kClass,
        L"단축키 및 도움말",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, w, h,
        owner,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr);

    if (!hwnd)
        return;

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
    SetActiveWindow(owner);
}

// ==============================================
// 도움말 페이지 텍스트 생성
// ==============================================
static std::wstring BuildHelpPageText(int page)
{
    switch (page)
    {
    default:
    case 0:
        return
            L"[기본 / 단축키]\r\n"
            L"\r\n"
            L"Ktin: TinTin++ GUI Client는 TinTin++를 GUI 창에서 다루기 위한 프로그램입니다.\r\n"
            L"기본 화면은 보통 다음 순서로 구성됩니다.\r\n"
            L"\r\n"
            L"  1. 채팅 캡쳐창(도킹 상태일 때 상단 표시)\r\n"
            L"  2. 로그창\r\n"
            L"  3. 단축버튼 바\r\n"
            L"  4. 3줄 입력창\r\n"
            L"  5. 상태바\r\n"
            L"\r\n"
            L"[주요 단축키]\r\n"
            L"\r\n"
            L"  Alt+Q   : 빠른 연결\r\n"
            L"  Alt+A   : 주소록\r\n"
            L"  Alt+V   : 메모장\r\n"
            L"  Alt+X   : 프로그램 종료\r\n"
            L"  Ctrl+F  : 찾기 창 열기\r\n"
            L"            - 메인 창: 로그 찾기\r\n"
            L"            - 메모장: 메모장 찾기\r\n"
            L"  Shift+F : 트리거(글월 찾기/하이라이트) 설정\r\n"
            L"  F4      : 특수기호 창 열기/닫기\r\n"
            L"\r\n"
            L"[로그 / 입력 관련 단축키]\r\n"
            L"\r\n"
            L"  Alt+Space        : 입력창 3줄 전체 비우기\r\n"
            L"  Ctrl+Space       : 현재 로그 화면만 비우기\r\n"
            L"  Ctrl+Shift+Space : 지난 화면(history)까지 모두 비우기\r\n"
            L"  Ctrl+F9          : 연결 끊기(#zap)\r\n"
            L"\r\n"
            L"[예시]\r\n"
            L"\r\n"
            L"  1) 게임 도중 입력창이 지저분해졌을 때\r\n"
            L"     → Alt+Space 로 3줄 입력칸을 모두 비웁니다.\r\n"
            L"\r\n"
            L"  2) 현재 보이는 로그만 깨끗하게 지우고 싶을 때\r\n"
            L"     → Ctrl+Space\r\n"
            L"     → 현재 화면은 지워지지만, 이전 내용은 지난 화면으로 남습니다.\r\n"
            L"\r\n"
            L"  3) 지난 화면까지 전부 날리고 새로 시작하고 싶을 때\r\n"
            L"     → Ctrl+Shift+Space\r\n";

    case 1:
        return
            L"[연결 / 주소록]\r\n"
            L"\r\n"
            L"[빠른 연결]\r\n"
            L"\r\n"
            L"빠른 연결 창은 메뉴 또는 Alt+Q로 열 수 있습니다.\r\n"
            L"최근 연결한 주소와 문자셋 기록을 따로 저장합니다.\r\n"
            L"마지막에 직접 입력한 연결 명령도 기억합니다.\r\n"
            L"\r\n"
            L"[스크립트 읽기]\r\n"
            L"\r\n"
            L"파일 메뉴의 스크립트 읽기는 선택한 파일을 TinTin++에\r\n"
            L"#read {경로} 형식으로 전송합니다.\r\n"
            L"\r\n"
            L"[주소록]\r\n"
            L"\r\n"
            L"주소록은 Alt+A 또는 메뉴로 열 수 있습니다.\r\n"
            L"목록은 이름 / 서버주소:포트 / 아이디 3개 열로 표시됩니다.\r\n"
            L"\r\n"
            L"지원 기능\r\n"
            L"  - 더블클릭 접속\r\n"
            L"  - 새로 만들기\r\n"
            L"  - 수정\r\n"
            L"  - 삭제\r\n"
            L"  - 연결\r\n"
            L"  - 정렬(최근 접속순 / 이름순 / 서버주소순 / 아이디순)\r\n"
            L"  - 열 머리글 클릭 정렬\r\n"
            L"\r\n"
            L"[주소록에 저장되는 정보]\r\n"
            L"\r\n"
            L"  - 서버 이름\r\n"
            L"  - 서버 주소\r\n"
            L"  - 서버 포트\r\n"
            L"  - 서버 문자셋 (UTF-8 / EUC-KR CP949)\r\n"
            L"  - TinTin 스크립트 경로\r\n"
            L"  - 개별 자동 로그인 사용 여부\r\n"
            L"  - 아이디/비밀번호 패턴과 실제 값\r\n"
            L"  - 연결 끊김 시 자동 재접속 여부\r\n"
            L"  - 최근 접속 시간\r\n"
            L"\r\n"
            L"[자동 로그인]\r\n"
            L"\r\n"
            L"전역 자동 로그인 설정이 있으며,\r\n"
            L"주소록 항목별 자동 로그인이 켜져 있으면 전역값보다\r\n"
            L"주소록 개별값이 우선 적용됩니다.\r\n"
            L"\r\n"
            L"[자동 재접속]\r\n"
            L"\r\n"
            L"현재 활성 세션이 주소록 항목이며 해당 항목의 자동 재접속이\r\n"
            L"켜져 있으면, 세션 종료 메시지를 감지했을 때 5초 후\r\n"
            L"재접속 타이머가 동작합니다.\r\n"
            L"\r\n"
            L"[예시 1 : 주소록 등록]\r\n"
            L"\r\n"
            L"  이름     : 루미나리\r\n"
            L"  주소     : mud.example.com\r\n"
            L"  포트     : 4100\r\n"
            L"  문자셋   : EUC-KR (CP949)\r\n"
            L"  스크립트 : D:\\TinTin\\main.tin\r\n"
            L"\r\n"
            L"[예시 2 : 자동 로그인 패턴]\r\n"
            L"\r\n"
            L"  아이디 패턴   : 아이디:\r\n"
            L"  아이디        : myid\r\n"
            L"  비밀번호 패턴 : 비밀번호:\r\n"
            L"  비밀번호      : mypw\r\n";

    case 2:
        return
            L"[입력 / 로그창]\r\n"
            L"\r\n"
            L"[입력창]\r\n"
            L"\r\n"
            L"입력창은 3줄 구조입니다.\r\n"
            L"포커스된 줄이 현재 활성 입력줄이 되며,\r\n"
            L"입력 기록 보기 상태와 현재 입력 상태를 나눠 관리합니다.\r\n"
            L"\r\n"
            L"지원 기능\r\n"
            L"  - 3줄 입력 구조\r\n"
            L"  - Alt+Space : 3줄 전체 비우기\r\n"
            L"  - 입력 기록 저장 / 불러오기\r\n"
            L"  - 최근 기록을 3줄 보기 형태로 배치\r\n"
            L"  - 옵션에서 Backspace를 현재 행으로 제한 가능\r\n"
            L"\r\n"
            L"[로그창]\r\n"
            L"\r\n"
            L"로그창은 자체 터미널 버퍼를 사용하며,\r\n"
            L"현재 화면과 지난 화면(history)을 분리해서 관리합니다.\r\n"
            L"ANSI 색상과 굵게 표시를 해석해 내부 셀 버퍼에 그립니다.\r\n"
            L"\r\n"
            L"지원 기능\r\n"
            L"  - 현재 화면 / 지난 화면 분리 저장\r\n"
            L"  - 마우스 드래그 선택 후 복사\r\n"
            L"  - 단어 클릭 시 즉시 전송\r\n"
            L"  - 마우스 휠 스크롤\r\n"
            L"  - PageUp / PageDown 탐색\r\n"
            L"  - Ctrl+F 로그 검색\r\n"
            L"\r\n"
            L"[로그 복사 / 저장]\r\n"
            L"\r\n"
            L"메뉴에서 다음 기능을 사용할 수 있습니다.\r\n"
            L"  - 지난 화면 복사\r\n"
            L"  - 지난 화면 저장\r\n"
            L"  - 현재 화면 복사\r\n"
            L"  - 현재 화면 저장\r\n"
            L"\r\n"
            L"[로그 비우기]\r\n"
            L"\r\n"
            L"  Ctrl+Space\r\n"
            L"    → 현재 화면만 비웁니다.\r\n"
            L"    → 비워진 내용은 지난 화면(history)로 밀어 넣습니다.\r\n"
            L"\r\n"
            L"  Ctrl+Shift+Space\r\n"
            L"    → 지난 화면(history)까지 포함해 전부 삭제합니다.\r\n";

    case 3:
        return
            L"[단축버튼 / 줄임말]\r\n"
            L"\r\n"
            L"[단축버튼]\r\n"
            L"\r\n"
            L"단축버튼은 최대 10개이며 화면에 가로로 배치됩니다.\r\n"
            L"숨기기 / 표시가 가능하고, 각 버튼은 다음 정보를 가집니다.\r\n"
            L"\r\n"
            L"  - 라벨\r\n"
            L"  - 켜기(ON) 명령\r\n"
            L"  - 끄기(OFF) 명령\r\n"
            L"  - 토글 여부\r\n"
            L"\r\n"
            L"[토글 동작 예시]\r\n"
            L"\r\n"
            L"  버튼 이름 : 힐\r\n"
            L"  켜기 명령 : heal on\r\n"
            L"  끄기 명령 : heal off\r\n"
            L"  토글      : 체크\r\n"
            L"\r\n"
            L"  → 첫 클릭   : heal on\r\n"
            L"  → 두 번째   : heal off\r\n"
            L"  → 세 번째   : heal on\r\n"
            L"\r\n"
            L"[실행 단축키]\r\n"
            L"\r\n"
            L"  Alt+1 ~ Alt+0 : 단축버튼 1~10 실행\r\n"
            L"  숫자패드 숫자도 같이 처리됩니다.\r\n"
            L"\r\n"
            L"[줄임말]\r\n"
            L"\r\n"
            L"줄임말 기능은 전역 사용 여부와 여러 항목 목록을 가집니다.\r\n"
            L"각 항목은 사용 여부 / 줄임말 / 치환할 실제 명령으로 저장됩니다.\r\n"
            L"\r\n"
            L"동작 방식\r\n"
            L"  - 입력한 한 줄을 Trim한 뒤 비교합니다.\r\n"
            L"  - 등록된 줄임말과 완전히 일치할 때만 치환됩니다.\r\n"
            L"  - 부분 문자열 치환이 아닙니다.\r\n"
            L"  - 대소문자는 구분하지 않습니다.\r\n"
            L"\r\n"
            L"[예시]\r\n"
            L"\r\n"
            L"  줄임말   : ㅊ\r\n"
            L"  전송명령 : 쳐다본다\r\n"
            L"\r\n"
            L"  줄임말   : buff\r\n"
            L"  전송명령 : cast bless\r\n";

    case 4:
        return
            L"[트리거 / 변수]\r\n"
            L"\r\n"
            L"[트리거(글월 찾기 / 하이라이트)]\r\n"
            L"\r\n"
            L"Shift+F로 여는 트리거 설정 창입니다.\r\n"
            L"규칙은 여러 개를 저장할 수 있으며, 각 규칙은 다음 속성을 가집니다.\r\n"
            L"\r\n"
            L"  - 사용 여부\r\n"
            L"  - 별명(name)\r\n"
            L"  - 패턴\r\n"
            L"  - 반전 표시 여부 또는 지정 색상\r\n"
            L"  - 액션 타입\r\n"
            L"  - 명령문\r\n"
            L"  - 삑(beep)\r\n"
            L"  - 사운드 재생 및 파일 경로\r\n"
            L"\r\n"
            L"패턴이 맞으면 규칙 액션이 실행됩니다.\r\n"
            L"\r\n"
            L"[패턴 예시]\r\n"
            L"\r\n"
            L"  %1가 %2 라고 말합니다.\r\n"
            L"\r\n"
            L"  예를 들어 로그에\r\n"
            L"    철수 가 안녕 이라고 말합니다.\r\n"
            L"  가 들어오면\r\n"
            L"    %1 = 철수\r\n"
            L"    %2 = 안녕\r\n"
            L"  으로 쓸 수 있습니다.\r\n"
            L"\r\n"
            L"[변수]\r\n"
            L"\r\n"
            L"변수 기능은 전역 사용 여부와 여러 항목 목록을 가집니다.\r\n"
            L"각 항목은 사용 여부 / 자료형 / 변수명 / 변수값으로 저장됩니다.\r\n"
            L"자료형은 문자열, 숫자, 참/거짓, 리스트 4종입니다.\r\n"
            L"\r\n"
            L"[서버 전송 형식]\r\n"
            L"\r\n"
            L"  문자열 / 숫자 : #variable {name} {value}\r\n"
            L"  참/거짓       : #variable {name} {1 또는 0}\r\n"
            L"  리스트        : #list {name} {create} {value}\r\n"
            L"\r\n"
            L"[예시]\r\n"
            L"\r\n"
            L"  자료형 : 문자열\r\n"
            L"  변수명 : target\r\n"
            L"  변수값 : orc\r\n"
            L"  → #variable {target} {orc}\r\n";

    case 5:
        return
            L"[채팅 캡쳐]\r\n"
            L"\r\n"
            L"채팅 캡쳐는 별도 RichEdit 창으로 구현되어 있으며,\r\n"
            L"도킹 / 분리 / 보이기 / 숨기기를 지원합니다.\r\n"
            L"\r\n"
            L"도킹 상태\r\n"
            L"  - 메인 창 상단에 붙습니다.\r\n"
            L"\r\n"
            L"분리 상태\r\n"
            L"  - 별도의 보조 창으로 뜹니다.\r\n"
            L"\r\n"
            L"[채팅 캡쳐 패턴]\r\n"
            L"\r\n"
            L"최대 10개 규칙을 저장합니다.\r\n"
            L"각 규칙은 사용 여부, 인식 패턴, 출력 형식을 가집니다.\r\n"
            L"\r\n"
            L"[예시]\r\n"
            L"\r\n"
            L"  패턴     : %1가 %2 라고 말합니다.\r\n"
            L"  출력형식 : [대화] %1: %2\r\n"
            L"\r\n"
            L"  로그 원문 : 철수 가 안녕 이라고 말합니다.\r\n"
            L"  결과     : [대화] 철수: 안녕\r\n"
            L"\r\n"
            L"[시간 표시]\r\n"
            L"\r\n"
            L"채팅 캡쳐에 시간 표시 옵션이 있으며,\r\n"
            L"메뉴 토글과 환경설정 둘 다 지원합니다.\r\n";

    case 6:
        return
            L"[환경설정 / 화면]\r\n"
            L"\r\n"
            L"환경설정 창은 왼쪽 리스트로 패널을 바꾸는 구조이며,\r\n"
            L"다음 5개 카테고리를 가집니다.\r\n"
            L"\r\n"
            L"  - 일반 설정\r\n"
            L"  - 폰트 및 색상\r\n"
            L"  - 접속 유지\r\n"
            L"  - 기타 설정\r\n"
            L"  - 단축버튼\r\n"
            L"\r\n"
            L"[일반 설정]\r\n"
            L"  - 화면 가로 칸 수\r\n"
            L"  - 화면 세로 줄 수\r\n"
            L"  - 폰트 부드럽게 표시(ClearType)\r\n"
            L"  - 화면 정렬: 왼쪽 / 중앙 / 오른쪽\r\n"
            L"  - 도킹 시 채팅 캡쳐창 줄 수\r\n"
            L"  - 채팅 캡쳐 시간 출력\r\n"
            L"  - 전역 자동 로그인 사용\r\n"
            L"\r\n"
            L"[폰트 및 색상]\r\n"
            L"메인창 / 입력창 / 채팅 캡쳐창 각각의\r\n"
            L"폰트 / 글자색 / 배경색을 따로 바꿀 수 있습니다.\r\n"
            L"\r\n"
            L"[접속 유지(KeepAlive)]\r\n"
            L"사용 여부, 전송 간격, 전송 명령을 설정합니다.\r\n"
            L"\r\n"
            L"[기타 설정]\r\n"
            L"  - 종료 시 입력창 내용 저장\r\n"
            L"  - 갈무리 켜기/끄기\r\n"
            L"  - 프로그램 시작 시 빠른 연결 띄우기\r\n"
            L"  - 프로그램 시작 시 주소록 띄우기\r\n"
            L"  - Backspace를 현재 행으로 제한\r\n"
            L"  - X 버튼 클릭 시 트레이로 숨기기\r\n"
            L"\r\n"
            L"[테마]\r\n"
            L"Windows, xterm 16색, Campbell, PowerShell, AlmaLinux,\r\n"
            L"Dark+, Ubuntu 20.04/22.04, Solarized, Tango 등이 있습니다.\r\n";

    case 7:
        return
            L"[메모장 / 특수기호]\r\n"
            L"\r\n"
            L"[메모장]\r\n"
            L"\r\n"
            L"메모장은 Alt+V로 열 수 있는 별도 창입니다.\r\n"
            L"파일 열기/저장/다른 이름 저장, 최근 파일, 자동저장,\r\n"
            L"상태바, 줄 번호, 찾기/바꾸기/이동, 인코딩 선택,\r\n"
            L"구문 강조, 특수기호/선 그리기 기능이 있습니다.\r\n"
            L"\r\n"
            L"[인코딩 처리]\r\n"
            L"\r\n"
            L"메모장은 파일을 열 때\r\n"
            L"  1. UTF-8 BOM 검사\r\n"
            L"  2. BOM 없는 UTF-8 엄격 검사\r\n"
            L"  3. 실패 시 CP949\r\n"
            L"순서로 판단합니다.\r\n"
            L"\r\n"
            L"[찾기 단축키]\r\n"
            L"\r\n"
            L"  Ctrl+F   : 찾기\r\n"
            L"  F3       : 다음 찾기\r\n"
            L"  Shift+F3 : 이전 찾기\r\n"
            L"  Esc      : 찾기 창 닫기\r\n"
            L"\r\n"
            L"[특수기호 / 그리기]\r\n"
            L"\r\n"
            L"특수기호 창은 F4로 열고 닫습니다.\r\n"
            L"Alt+D 는 그리기 모드, Alt+C 는 열 모드를 토글합니다.\r\n";

    case 8:
        return
            L"[트리거 변수 / 상태바]\r\n"
            L"\r\n"
            L"하이라이트(트리거)에서는 %1 ~ %9 캡처값을 사용할 수 있습니다.\r\n"
            L"실행 동작에서 @SET, @ADD 등의 내부 명령으로 GUI 변수를 제어하고,\r\n"
            L"상태바에는 $변수이름 형식으로 표시할 수 있습니다.\r\n"
            L"\r\n"
            L"[기본 개념]\r\n"
            L"\r\n"
            L"  %1 : 패턴에서 첫 번째 캡처값\r\n"
            L"  %2 : 패턴에서 두 번째 캡처값\r\n"
            L"  변수 저장 : @SET 변수이름 값\r\n"
            L"  상태바 표시 : $변수이름\r\n"
            L"\r\n"
            L"[예시 1 - 소지금 표시]\r\n"
            L"\r\n"
            L"패턴:\r\n"
            L"<가지고 있는 돈>  %1개의 동전.\r\n"
            L"\r\n"
            L"명령:\r\n"
            L"@SET gold %1\r\n"
            L"\r\n"
            L"상태바:\r\n"
            L"금화 $gold\r\n"
            L"\r\n"
            L"[예시 2 - 경험치 누적]\r\n"
            L"\r\n"
            L"패턴:\r\n"
            L"경험치를 %1 얻었습니다.\r\n"
            L"\r\n"
            L"명령:\r\n"
            L"@ADD exp %1\r\n"
            L"\r\n"
            L"상태바:\r\n"
            L"경험치 $exp\r\n"
            L"\r\n"
            L"[예시 3 - 몬스터 처치 카운트]\r\n"
            L"\r\n"
            L"패턴:\r\n"
            L"%1를 죽였습니다.\r\n"
            L"\r\n"
            L"명령:\r\n"
            L"@INC killcount\r\n"
            L"\r\n"
            L"상태바:\r\n"
            L"처치 $killcount\r\n"
            L"\r\n"
            L"[예시 4 - 전투 상태]\r\n"
            L"\r\n"
            L"패턴:\r\n"
            L"전투가 시작되었습니다.\r\n"
            L"\r\n"
            L"명령:\r\n"
            L"@SET combat 1\r\n"
            L"\r\n"
            L"패턴:\r\n"
            L"전투가 끝났습니다.\r\n"
            L"\r\n"
            L"명령:\r\n"
            L"@SET combat 0\r\n"
            L"\r\n"
            L"상태바:\r\n"
            L"전투 $combat\r\n"
            L"\r\n"
            L"[사용 가능한 내부 명령]\r\n"
            L"\r\n"
            L"  @SET 변수 값   : 값 설정\r\n"
            L"  @ADD 변수 값   : 값 더하기\r\n"
            L"  @SUB 변수 값   : 값 빼기\r\n"
            L"  @MUL 변수 값   : 값 곱하기\r\n"
            L"  @DIV 변수 값   : 값 나누기\r\n"
            L"  @INC 변수      : 1 증가\r\n"
            L"  @DEC 변수      : 1 감소\r\n"
            L"  @DEL 변수      : 변수 삭제\r\n"
            L"  @TOGGLE 변수   : 0/1 토글\r\n"
            L"\r\n"
            L"[상태바에서의 사용법]\r\n"
            L"\r\n"
            L"상태바 형식 문자열에 $gold, $exp, $killcount, $combat 처럼 넣으면\r\n"
            L"트리거에서 갱신한 값을 그대로 표시할 수 있습니다.\r\n"
            L"\r\n"
            L"[주의]\r\n"
            L"\r\n"
            L"  - 명령어는 대소문자 구분 없이 사용 가능합니다.\r\n"
            L"  - 숫자 연산 시 값이 숫자가 아니면 0으로 처리됩니다.\r\n"
            L"  - %1 캡처는 패턴이 정확히 일치해야 동작합니다.\r\n";
    case 9:  // ← 기존 case 8 다음에 추가
        return
            L"[타이머]\r\n"
            L"\r\n"
            L"타이머는 시간 기반으로 자동 명령을 실행하는 기능입니다.\r\n"
            L"최대 무제한 개수 생성 가능하며, 그룹 관리도 지원합니다.\r\n"
            L"\r\n"
            L"주요 기능\r\n"
            L" - 반복/일회성 실행\r\n"
            L" - 프로그램 시작 시 자동 실행\r\n"
            L" - #TIMER {이름} {START|STOP|PAUSE|RESUME|RESET} 명령 지원\r\n"
            L"\r\n"
            L"[메모장]\r\n"
            L"\r\n"
            L"Alt+V로 여는 고급 메모장입니다.\r\n"
            L"자동저장, 구문 강조, 특수기호 입력, 최근 파일 목록, 찾기/바꾸기 등을 지원합니다.\r\n"
            L"\r\n"
            L"특징\r\n"
            L" - UTF-8 / CP949 자동 감지\r\n"
            L" - F4 특수기호 창 연동\r\n"
            L" - 줄 번호 표시 / 자동 들여쓰기\r\n"
            L"\r\n"
            L"[상태바]\r\n"
            L"\r\n"
            L"하단 상태바에 $변수이름 형식으로 트리거에서 갱신된 변수를 실시간 표시합니다.\r\n"
            L"최대 5개 칸까지 분할 가능하며, 좌/중/우 정렬을 개별 설정할 수 있습니다.\r\n";

    case 10: // 필요 시 추가 페이지
        return
            L"[추가 기능 안내]\r\n"
            L"\r\n"
            L"• OSC를 통한 GUI 변수 연동 (#send {GUI_VAR:name=value})\r\n"
            L"• 단축버튼 토글 기능 (ON/OFF 명령 별도 지정)\r\n"
            L"• F1~F12 + Ctrl/Shift/Alt 조합 단축키\r\n"
            L"• 숫자 키패드 매크로 (NumLock ON 상태)\r\n";
    }

    return L"";
}
