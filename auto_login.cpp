#include "constants.h"
#include "types.h"
#include "main.h"
#include "utils.h"
#include "terminal_buffer.h"
#include "auto_login.h"
#include "settings.h"

static bool ContainsTextI(const std::wstring& haystack, const std::wstring& needle)
{
    if (needle.empty())
        return false;

    std::wstring h = haystack;
    std::wstring n = needle;

    std::transform(h.begin(), h.end(), h.begin(),
        [](wchar_t ch) { return (wchar_t)towlower(ch); });

    std::transform(n.begin(), n.end(), n.begin(),
        [](wchar_t ch) { return (wchar_t)towlower(ch); });

    return h.find(n) != std::wstring::npos;
}

static bool IsAutoLoginSuccessLine(const std::wstring& line)
{
    if (!g_app || !g_app->hasActiveSession)
        return false;

    const AddressBookEntry& e = g_app->activeSession;

    if (ContainsTextI(line, e.loginSuccessPattern1)) return true;
    if (ContainsTextI(line, e.loginSuccessPattern2)) return true;
    if (ContainsTextI(line, e.loginSuccessPattern3)) return true;

    return false;
}

static bool IsAutoLoginFailLine(const std::wstring& line)
{
    if (!g_app || !g_app->hasActiveSession)
        return false;

    const AddressBookEntry& e = g_app->activeSession;

    if (ContainsTextI(line, e.loginFailPattern1)) return true;
    if (ContainsTextI(line, e.loginFailPattern2)) return true;
    if (ContainsTextI(line, e.loginFailPattern3)) return true;

    return false;
}

void RunAutoLoginEngine(const std::wstring& line)
{
    if (!g_app)
        return;

    if (!g_app->autoLoginWindowActive)
        return;

    if (!g_app->activeAutoLoginEnabled)
        return;

    if (g_app->autoLoginTriggered)
        return;

    DWORD now = GetTickCount();
    if (now - g_app->autoLoginStartTick > 120000)
    {
        g_app->autoLoginWindowActive = false;
        g_app->autoLoginState = -1;
        g_app->autoLoginTriggered = true;
        return;
    }

    std::wstring text = Trim(line);
    if (text.empty())
        return;

    // 이미 성공/실패 상태면 종료
    if (g_app->autoLoginState == 3 || g_app->autoLoginState == -1)
    {
        g_app->autoLoginWindowActive = false;
        return;
    }

    // 0 = 대기
    // 1 = 아이디 전송 완료
    // 2 = 비밀번호 전송 완료
    // 3 = 로그인 성공
    // -1 = 로그인 실패

    // 1) 아이디 프롬프트 검사
    if (g_app->autoLoginState == 0)
    {
        if (!g_app->activeAutoLoginIdPattern.empty() &&
            !g_app->activeAutoLoginId.empty() &&
            ContainsTextI(text, g_app->activeAutoLoginIdPattern))
        {
            SendRawCommandToMud(g_app->activeAutoLoginId);
            g_app->autoLoginState = 1;
            return;
        }
    }

    // 2) 비밀번호 프롬프트 검사
    if (g_app->autoLoginState <= 1)
    {
        if (!g_app->activeAutoLoginPwPattern.empty() &&
            !g_app->activeAutoLoginPw.empty() &&
            ContainsTextI(text, g_app->activeAutoLoginPwPattern))
        {
            SendRawCommandToMud(g_app->activeAutoLoginPw);
            g_app->autoLoginState = 2;
            return;
        }
    }

    // 3) 비밀번호 전송 후 성공/실패 메시지 검사
    if (g_app->autoLoginState == 2)
    {
        if (IsAutoLoginSuccessLine(text))
        {
            g_app->autoLoginState = 3;
            g_app->autoLoginWindowActive = false;
            g_app->autoLoginTriggered = true;
            return;
        }

        if (IsAutoLoginFailLine(text))
        {
            g_app->autoLoginState = -1;
            g_app->autoLoginWindowActive = false;
            g_app->autoLoginTriggered = true;
            return;
        }
    }
}