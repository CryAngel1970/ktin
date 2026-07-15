#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "win_util.h"
#include "terminal_buffer.h"
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
        SetWindowPos(hwnd, nullptr, 0, 0, RectWidth(rc), RectHeight(rc), SWP_NOMOVE | SWP_NOZORDER);

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
            0, L"STATIC", L"KTin 2.7 : TinTin++ GUI 도움말",
            WS_CHILD | WS_VISIBLE,
            24, 18, 360, 32,
            hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);

        hSub = CreateWindowExW(
            0, L"STATIC", L"현재 프로그램의 메뉴, 입력창, 메모장과 모든 주요 단축키를 정리했습니다.",
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
            L"기본 / 전체 단축키",
            L"파일 메뉴 / 연결",
            L"편집 메뉴",
            L"보기 메뉴",
            L"옵션 / 환경설정",
            L"입력창 사용법",
            L"로그창 / 선택 / 복사",
            L"단축버튼 / 기능키",
            L"숫자 키패드 / 접근성",
            L"갈무리 / 갈무리 보기",
            L"GMCP 지도 / 정보",
            L"메모장 파일 / 편집",
            L"메모장 찾기 / 서식",
            L"메모장 그리기 / 기호",
            L"파일 위치 / 문제 해결"
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
        ResetGdiObjectRef(hFontTitle);
        ResetGdiObjectRef(hFontSub);
        ResetGdiObjectRef(hFontUi);
        ResetGdiObjectRef(hbrBack);
        ResetGdiObjectRef(hbrPanel);
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
    RECT rcOwner = {};
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
static std::wstring NormalizeHelpPageText(const wchar_t* text)
{
    std::wstring result;
    if (!text)
        return result;

    for (const wchar_t* p = text; *p; ++p)
    {
        if (*p == L'\n')
            result += L"\r\n";
        else if (*p != L'\r')
            result += *p;
    }
    return result;
}

static std::wstring BuildHelpPageText(int page)
{
    switch (page)
    {
    default:
    case 0:
        return NormalizeHelpPageText(LR"HELP([기본 / 전체 단축키]
KTin 2.7의 메인창, 입력창, 로그창에서 바로 사용할 수 있는 고정 단축키입니다.

[연결과 프로그램]
  Alt+Q             빠른 연결
  Alt+A             주소록
  Alt+S             TinTin++ 스크립트 읽기
  Alt+V             KTin 메모장
  Alt+X             프로그램 끝내기
  Ctrl+F9           현재 연결 끊기(#zap)
  F4                특수 기호 창 열기/닫기
  Ctrl+F            로그 찾기

[입력창과 화면]
  Enter              현재 입력줄 전송; 빈 줄이면 빈 Enter 전송
  Ctrl+A             현재 입력줄 전체 선택
  Alt+Space          입력창 3줄과 입력 기록 모두 비우기
  Ctrl+Space         현재 로그 화면 지우기
  Ctrl+Shift+Space   현재·지난 로그와 입력 기록 모두 지우기
  PageUp/PageDown    로그를 반 화면씩 위/아래로 이동
  Ctrl+Home          지난 화면의 맨 처음으로 이동
  Ctrl+End           실시간 화면으로 복귀
  Ctrl+Shift+Home    처음부터 현재 보기 아래까지 블럭 선택
  Ctrl+Shift+End     현재 보기 위부터 최신 화면까지 블럭 선택
  위/아래            3줄 입력창 이동 및 이전/다음 명령 기록 탐색
  왼쪽/오른쪽        줄 경계에서 앞/뒤 입력줄로 이동
  Home/End           현재 입력줄의 처음/끝

[사용자 지정]
  Alt+1~Alt+0        단축버튼 1~10 실행
  F1~F12             일반 기능키 매크로
  Alt+F1~F12         Alt 기능키 매크로
  Shift+F1~F12       Shift 기능키 매크로
  Ctrl+F1~F12        Ctrl 기능키 매크로

F4와 Ctrl+F9는 프로그램 기능에 예약되어 기능키 설정에서 다시 지정할 수 없습니다.
메뉴와 대화상자의 밑줄 문자는 Alt와 함께 사용하는 접근키이며, Enter는 확인,
Esc는 취소/닫기로 동작하는 창이 많습니다.)HELP");

    case 1:
        return NormalizeHelpPageText(LR"HELP([파일 메뉴 / 연결]
서버 접속, 새 창, 스크립트, 메모장과 종료 기능입니다.

[파일 메뉴 전체]
  새 창 띄우기
    별도의 KTin 프로세스를 실행합니다. 각 창은 세션명과 자동 재연결 정보를 독립 보관합니다.
  빠른 연결...                         Alt+Q
    호스트, 포트, 문자셋을 지정하여 바로 접속합니다.
  주소록...                            Alt+A
    서버 이름·주소·포트·문자셋·스크립트·자동 로그인·자동 재연결을 저장합니다.
  연결 끊기(#ZAP)                     Ctrl+F9
    사용자가 직접 현재 세션을 종료합니다. 사용자 종료는 자동 재연결 대상에서 제외됩니다.
  스크립트 읽기...                     Alt+S
    선택한 파일을 TinTin++의 #read 명령으로 읽습니다.
  메모장...                            Alt+V
    KTin 내장 메모장을 엽니다.
  끝내기                               Alt+X
    프로그램을 종료합니다. 트레이 숨기기 옵션이 켜져 있으면 X 버튼은 트레이로 숨깁니다.

[문자셋]
  UTF-8 서버는 UTF-8을 사용합니다.
  EUC-KR/CP949 서버는 주소록 또는 빠른 연결에서 해당 문자셋을 선택합니다.

[자동 로그인과 재연결]
  자동 로그인 패턴 검사는 접속 후 최대 60초 동안만 동작합니다.
  자동 재연결은 그 창에서 마지막으로 접속한 주소록/빠른 연결 정보만 사용합니다.
  새 창이 다른 창의 서버 정보로 다시 접속하지 않도록 내부 세션명이 창마다 분리됩니다.)HELP");

    case 2:
        return NormalizeHelpPageText(LR"HELP([편집 메뉴]
설정 도구와 화면 자료 내보내기를 모아 둔 메뉴입니다.

[설정 도구]
  찾기...                              Ctrl+F
  변수 설정...
  줄임말 설정...
  단축키 설정...
  타이머 설정...
  상태바 설정...
  숫자 키패드 매크로...

[지난 화면을 >]
  클립보드로 복사 / 파일로 저장
  코드로 클립보드 복사 / 코드로 파일 저장
  지우기

[현재 화면을 >]
  클립보드로 복사 / 파일로 저장
  코드로 클립보드 복사 / 코드로 파일 저장
  지우기

[전체 화면을 >]
  클립보드로 복사 / 파일로 저장
  코드로 클립보드 복사 / 코드로 파일 저장
  지우기

일반 복사는 ANSI 제어 코드를 제거한 화면 글자를 내보냅니다.
'코드로' 복사·저장은 셀의 글자색, 배경색, 굵기를 ANSI SGR 코드로 재구성합니다.
일반 파일은 UTF-8 BOM, ANSI 코드 파일은 ESC 바이트 보존을 위해 UTF-8 BOM 없이 저장합니다.)HELP");

    case 3:
        return NormalizeHelpPageText(LR"HELP([보기 메뉴]
화면 표시와 보조창을 여는 메뉴입니다.

[보기 메뉴 전체]
  메뉴 숨기기
    상단 메뉴를 감춥니다. 숨긴 상태에서 로그창 우클릭 → 상단 메뉴 보이기로 복구합니다.
  갈무리 >
    갈무리 켜기/끄기, 갈무리창 모두 닫기, 갈무리 폴더 열기
  갈무리 보기 >
    전체, 잡담, 경매, 대화, 아이템 획득, 경험치, 사용자 1~3, 임시 문자열
  갈무리 필터 설정...
  ANSI 테마 선택...
  화면 여백 없애기
  GMCP 지도 보기
  GMCP 정보 보기
  키패드 보기
  특수 기호...                        F4

[보조창 자석 기능]
  GMCP 지도/정보창과 화면 키패드는 메인창의 왼쪽·오른쪽·위·아래 프레임에 붙일 수 있습니다.
  붙은 창은 메인창 이동과 크기 변경을 따라갑니다.
  갈무리 보기창도 메인창 또는 다른 갈무리창 가까이에 놓으면 자동으로 붙습니다.
  갈무리창 이동 중 Shift를 누르면 자동 붙기를 잠시 무시합니다.)HELP");

    case 4:
        return NormalizeHelpPageText(LR"HELP([옵션 / 환경설정]
옵션 메뉴에는 환경 설정, 단축버튼 표시, 접속 유지 켜기/끄기가 있습니다.

[일반 설정]
  화면 가로칸·세로줄 수, 상하좌우 여백, ClearType, 화면 정렬
  접속 후 60초 자동 로그인 패턴과 아이디/비밀번호

[폰트 및 색상]
  Mud둥근모 우선 사용
  메인 로그 폰트·글자색·배경색
  입력창 폰트·글자색·배경색
  메인 폰트를 변경하면 GMCP 지도·정보창도 같은 폰트를 사용합니다.

[접속 유지]
  사용 여부, 전송 간격, 전송 명령
  명령이 비어 있으면 빈 Enter를 보냅니다.

[기타 설정]
  종료 시 입력 저장, 시작 시 빠른 연결/주소록 열기
  Backspace를 현재 입력줄로 제한, X 버튼 트레이 숨기기
  소리 효과, 모호한 동아시아 문자 폭 넓게 처리
  메인창 항상 위, 갈무리창 자동 붙기
  블럭 설정 후 동작: 일반/ANSI 클립보드, 일반/ANSI 파일저장, 선택메뉴

[단축버튼]
  1~10번의 라벨, ON 명령, OFF 명령, 토글 여부를 설정합니다.

[GMCP]
  정보창 아래 원문 영역에 표시할 수신 모듈을 선택합니다.
  체력/정신력 막대는 Vitals 또는 Cursor에서 선택 여부와 관계없이 갱신됩니다.)HELP");

    case 5:
        return NormalizeHelpPageText(LR"HELP([입력창 사용법]
입력창은 최근 명령을 함께 볼 수 있는 3줄 구조입니다.

[전송]
  Enter              현재 줄을 TinTin++/MUD로 전송
  빈 줄 + Enter      빈 Enter 전송
  위/아래            입력줄 이동; 끝에서는 이전/다음 명령 기록으로 이동
  왼쪽/오른쪽        커서가 줄 경계일 때 앞/뒤 입력줄로 이동
  Backspace          줄 맨 앞에서 이전 입력줄 끝으로 이동
                     '현재 행으로 제한' 옵션이 켜져 있으면 줄을 넘지 않음
  Home/End           현재 줄의 처음/끝
  Shift+Home/End     커서부터 줄 처음/끝까지 선택
  Ctrl+A             현재 줄 모두 선택

[복사와 붙여넣기]
  Ctrl+C/Ctrl+X/Ctrl+V와 Shift+Insert 등 Windows 편집키를 사용할 수 있습니다.
  한 줄 클립보드는 현재 입력줄에 붙습니다.
  줄바꿈이 있는 여러 행은 붙여넣는 즉시 모든 줄을 순서대로 MUD에 전송합니다.
  CRLF, LF, CR을 모두 지원하며 빈 줄과 마지막 줄도 유지합니다.
  게시판 글쓰기에서 본문을 한꺼번에 붙인 뒤 마침표(.)를 별도로 전송할 수 있습니다.

[입력 기록과 지우기]
  Alt+Space          입력창 3줄과 입력 기록 전체 삭제
  Ctrl+Shift+Space   로그 전체와 입력 기록 전체 삭제
  종료 시 입력 저장 옵션을 켜면 재실행 때 입력 내용이 복구됩니다.)HELP");

    case 6:
        return NormalizeHelpPageText(LR"HELP([로그창 / 선택 / 복사]
메인 출력은 ANSI 터미널 셀 버퍼에 현재 화면과 지난 화면으로 나뉘어 저장됩니다.

[키보드 탐색]
  PageUp/PageDown    반 화면씩 이동
  Ctrl+Home/End      지난 화면 맨 위 / 실시간 화면
  Ctrl+Shift+Home    처음부터 현재 보기 아래까지 선택
  Ctrl+Shift+End     현재 보기 위부터 최신 화면까지 선택
  Ctrl+Space         현재 화면 지우기
  Ctrl+Shift+Space   현재·지난 화면 모두 지우기

[마우스]
  휠                3줄씩 위/아래 이동
  한 단어 클릭      클릭한 단어를 명령으로 즉시 전송
  왼쪽 드래그       블럭 선택; 창 위/아래로 끌면 자동 스크롤
  오른쪽 클릭       복사하기 > / 파일저장 > / 지우기 > / 닫기

[선택 완료 동작]
  환경 설정 → 기타 설정 → 블럭 설정 후에서 선택합니다.
  일반 클립보드 / ANSI 코드 클립보드 / 일반 파일 / ANSI 코드 파일 / 선택메뉴
  선택메뉴에는 복사하기, 파일저장, 코드로 복사하기, 코드로 파일저장, 닫기가 있습니다.

[저장 위치]
  기본 폴더는 실행 파일 옆의 '저장'이며 없으면 자동 생성합니다.
  기본 파일명은 YYYYMMDD_HHMMSS.txt입니다.)HELP");

    case 7:
        return NormalizeHelpPageText(LR"HELP([단축버튼 / 기능키]
두 종류의 명령 단축 기능을 제공합니다.

[단축버튼]
  화면의 1~0 버튼 또는 Alt+1~Alt+0으로 실행합니다.
  각 버튼에 표시 라벨과 ON 명령을 지정할 수 있습니다.
  토글을 켜면 첫 실행은 ON 명령, 다음 실행은 OFF 명령을 번갈아 보냅니다.
  옵션 → 단축버튼 표시에서 버튼 줄을 보이거나 숨깁니다.

[기능키 매크로]
  편집 → 단축키 설정에서 다음 48개 조합을 설정합니다.
  F1~F12 / Alt+F1~F12 / Shift+F1~F12 / Ctrl+F1~F12
  각 항목을 사용으로 켜고 전송할 명령을 입력합니다.

[예약 키]
  F4                특수 기호
  Ctrl+F9           연결 끊기
  예약 키는 '할당됨'으로 표시되고 사용자 명령을 넣을 수 없습니다.

기능키 매크로는 메인 입력창에 포커스가 있을 때 실행됩니다.)HELP");

    case 8:
        return NormalizeHelpPageText(LR"HELP([숫자 키패드 / 접근성]
물리 숫자 키패드와 마우스로 누르는 화면 키패드가 같은 매크로 설정을 사용합니다.

[설정]
  편집 → 숫자 키패드 매크로에서 Num0~Num9, /, *, -, +, .에 명령을 지정합니다.
  물리 키패드 매크로는 사용 옵션이 켜져 있고 NumLock 입력이 해당 키로 들어올 때 실행됩니다.

[화면 키패드]
  보기 → 키패드 보기에서 엽니다.
  버튼에는 '4←' 같은 키 이름 대신 실제 전송 명령인 '서'가 표시됩니다.
  버튼을 마우스로 클릭하면 현재 활성 세션에 명령을 즉시 보냅니다.
  물리 키패드 사용 여부와 관계없이 클릭할 수 있어 키보드 입력이 불편한 사용자를 지원합니다.

[도킹]
  화면 키패드를 메인창 왼쪽·오른쪽·위·아래 가까이 옮기면 프레임에 붙습니다.
  위치, 크기, 도킹 방향은 config.ini에 저장됩니다.
  키패드 매크로 설정을 바꾸면 열린 화면 키패드의 글자도 즉시 바뀝니다.)HELP");

    case 9:
        return NormalizeHelpPageText(LR"HELP([갈무리 / 갈무리 보기]
서버 수신 내용을 실행 폴더의 log 디렉터리에 저장하고 별도 창에서 필터별로 봅니다.

[갈무리]
  보기 → 갈무리 → 갈무리 켜기/끄기
  보기 → 갈무리 → 갈무리 폴더 열기
  로그 파일은 UTF-8로 기록되며 원문 ANSI도 보존할 수 있습니다.

[갈무리 보기]
  전체, 잡담, 경매, 대화, 아이템 획득, 경험치, 사용자 1~3, 임시 문자열
  필터창은 정규식 대신 세미콜론으로 구분한 단순 포함 문자열을 사용합니다.
  여러 보기창을 동시에 열고 창마다 탭 조합을 선택할 수 있습니다.
  메뉴 숨기기, 상태바 숨기기, 항상 위, 선택/전체 복사, 모두 선택을 지원합니다.

[안정성]
  여러 창이 같은 로그를 볼 때 공통 tail 읽기 구조를 사용합니다.
  표시 버퍼는 장시간 사용을 위해 제한되지만 상태바의 누적 줄 수는 전체 매칭 수를 나타냅니다.
  내용이 창보다 길 때 휠과 세로 스크롤을 사용할 수 있습니다.)HELP");

    case 10:
        return NormalizeHelpPageText(LR"HELP([GMCP 지도 / 정보]
Sector_D가 보내는 GMCP를 TinTin++가 협상·수신하고 보이지 않는 브리지로 KTin에 전달합니다.

[GMCP 지도]
  보기 → GMCP 지도 보기
  Looming.Map의 지역명과 문자 지도를 메인 로그 폰트로 표시합니다.
  지도 방 기호를 클릭하면 연결선을 분석해 경로를 한 칸씩 전송합니다.
  다음 지도 패킷이 와야 다음 이동을 보내므로 이동 명령을 한꺼번에 밀어 넣지 않습니다.
  전투나 이동 실패로 새 지도가 오지 않으면 멈추며 다시 시도/이동 취소를 사용할 수 있습니다.
  큰 지도는 휠과 세로 스크롤로 탐색합니다.

[GMCP 정보]
  보기 → GMCP 정보 보기
  Cursor 또는 Vitals의 현재/최대 체력과 정신력을 퍼센트 막대로 표시합니다.
  환경설정 GMCP에서 선택한 Char, Combat, Party, Room, System, Info, Chat, Term 등의
  원문은 실제로 그 모듈을 수신한 경우에만 아래 원문 영역에 표시됩니다.
  체크는 서버에 전송을 요구하는 기능이 아니라, 이미 수신한 모듈의 표시 여부입니다.

지도·정보창은 메인창에 자석처럼 붙고 메인 폰트 변경을 함께 따릅니다.)HELP");

    case 11:
        return NormalizeHelpPageText(LR"HELP([메모장 파일 / 편집]
KTin 메모장은 Alt+V로 열며 TinTin++ 스크립트와 일반 문서를 편집할 수 있습니다.

[파일]
  Ctrl+O             열기
  Ctrl+S             저장
  Ctrl+Shift+S       다른 이름으로 저장
  Alt+G              TinTin으로 나가기
  자동 저장 복구     autosave 폴더의 복구 파일 열기
  Esc                 메모장 닫기

[편집]
  Ctrl+Z / Ctrl+Y    실행취소 / 다시실행
  Ctrl+X/C/V         잘라내기 / 복사 / 붙여넣기
  Del                 선택 또는 다음 문자 삭제
  Ctrl+A             모두 선택
  Ctrl+Home/End      문서 처음 / 문서 마지막
  Ctrl+PageUp/Down   현재 화면 처음 / 현재 화면 마지막
  Alt+Y              커서 뒤쪽 지우기
  Ctrl+Backspace     앞 단어 지우기
  Ctrl+Delete        뒷 단어 지우기
  Ctrl+L             현재 한 줄 지우기)HELP");

    case 12:
        return NormalizeHelpPageText(LR"HELP([메모장 찾기 / 서식]

[찾기]
  Ctrl+F             찾기
  F3                 다음 찾기
  Shift+F3           이전 찾기
  Ctrl+H             바꾸기
  Ctrl+G             행 찾아가기

[서식 메뉴]
  글자색/선색, 배경색, 폰트 변경
  UTF-8 또는 CP949 인코딩으로 변경
  자동 줄바꿈 열 너비 설정
  선택영역 왼쪽/가운데/오른쪽 정렬
  구문 테마 선택
  TinTin++, C/C++, C# 구문 강조 선택
  조판 부호 보기

[테마]
  기본 라이트/다크, UltraEdit, VS Code, GitHub Dark,
  Monokai, Dracula, Solarized Light/Dark를 제공합니다.)HELP");

    case 13:
        return NormalizeHelpPageText(LR"HELP([메모장 그리기 / 기호]
ASCII/유니코드 지도와 표를 작성하기 위한 기능입니다.

[특수 메뉴]
  Alt+D              그리기 모드 켜기/끄기
  Ctrl+R             마지막 기호 반복
  자동저장 켜기/끄기
  행 번호 보이기/숨기기
  F4                 특수 기호 창 열기/닫기

[그리기 모드]
  방향키를 누르면 선택한 선 또는 기호를 그리면서 이동합니다.
  선 브러시는 인접 선과 연결되는 기호를 계산하고, 일반 기호는 브러시처럼 찍습니다.
  조판 부호 보기와 행 번호는 문서 구조를 확인할 때 유용합니다.

[특수 기호 창]
  메인창과 메모장에서 공통으로 사용합니다.
  지도 선, 화살표, 원, 상자 기호 등을 마우스로 선택해 현재 편집 위치에 넣습니다.)HELP");

    case 14:
        return NormalizeHelpPageText(LR"HELP([파일 위치 / 문제 해결]

[실행 폴더의 주요 파일]
  ktin.exe            KTin 실행 파일
  bin\tt++.exe       KTin이 실행하는 TinTin++
  config.ini          화면·환경·GMCP·보조창 설정
  sessions.ini        주소록, 기능키 등 세션 관련 설정
  main.tin            선택적으로 읽는 TinTin++ 시작 스크립트
  log\                갈무리 로그
  저장\              화면/선택영역 저장 파일
  autosave\          메모장 자동저장 복구 파일

[화면 문자 폭]
  지도 선과 원이 깨지면 환경설정 → 기타 설정의
  '모호한 동아시아 문자 폭을 넓게 처리'를 바꿔 서버 출력과 맞춥니다.

[GMCP가 안 보일 때]
  GMCP 수정본 tt++.exe가 KTin의 bin 폴더에 있는지 확인합니다.
  서버가 해당 모듈을 실제 전송해야 정보창에 나타납니다.

[빌드]
  Visual Studio 2022 개발자 명령 프롬프트: build_msvc.bat
  WSL MinGW-w64: make clean && make
  MudDunggeunmo-Regular.ttf가 없다는 문구는 오류가 아니라 폰트 미포함 안내입니다.

전체 기능, 빌드법, 설정 파일과 이전 수정 기록은 소스의 통합 README.md에 있습니다.)HELP");
    }

    return L"";
}
