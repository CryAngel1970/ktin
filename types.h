#pragma once

#include <windows.h>
#include <string>
#include <vector>

// 1. 테마 및 색상 관련
struct UiStyle {
    LOGFONTW font = {};
    COLORREF textColor = RGB(220, 220, 220);
    COLORREF backColor = RGB(0, 0, 0);
};

struct TextStyle {
    COLORREF fg;
    COLORREF bg;
    bool bold;

    bool operator==(const TextStyle& rhs) const {
        return fg == rhs.fg && bg == rhs.bg && bold == rhs.bold;
    }
    bool operator!=(const TextStyle& rhs) const {
        return !(*this == rhs);
    }
};

struct StyledRun {
    TextStyle style;
    std::wstring text;
};

// 2. 주소록
struct AddressBookEntry {
    std::wstring name;
    std::wstring host;
    int port = 9999;
    std::wstring scriptPath;
    int charset = 1; // 0=UTF-8, 1=EUC-KR

    bool autoLoginEnabled = false;
    std::wstring alIdPattern = L"아이디:";
    std::wstring alId = L"";
    std::wstring alPwPattern = L"비밀번호:";
    std::wstring alPw = L"";

    unsigned long long lastConnected = 0;
    bool autoReconnect = false;

    std::wstring loginSuccessPattern1;
    std::wstring loginSuccessPattern2;
    std::wstring loginSuccessPattern3;

    std::wstring loginFailPattern1;
    std::wstring loginFailPattern2;
    std::wstring loginFailPattern3;
};

// 3. 하이라이트 (트리거)
struct HighlightRule {
    bool enabled = true;
    std::wstring name;
    std::wstring pattern;
    bool useInverse = true;
    COLORREF fg = RGB(255, 255, 255);
    COLORREF bg = RGB(0, 0, 0);

    int actionType = 0;
    std::wstring command;

    bool useBeep = false;
    bool useSound = false;
    std::wstring soundPath;
};

// 4. 줄임말 (매크로)
struct AbbreviationItem {
    bool enabled = true;
    std::wstring shortText;
    std::wstring replaceText;
};

// 5. 변수
struct VariableItem {
    bool enabled = true;
    int type = 0; // 0:문자열, 1:숫자, 2:참/거짓, 3:리스트
    std::wstring name;
    std::wstring value;
};