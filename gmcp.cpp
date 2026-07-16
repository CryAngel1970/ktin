#include "gmcp.h"

#include "main.h"
#include "settings.h"
#include "terminal_buffer.h"
#include "utils.h"
#include "win_util.h"

#include <algorithm>
#include <mutex>
#include <cctype>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <cmath>
#include <map>
#include <memory>
#include <new>
#include <queue>
#include <richedit.h>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t kMapWindowClass[] = L"KTinGmcpMapWindow";
    constexpr wchar_t kInfoWindowClass[] = L"KTinGmcpInfoWindow";
    constexpr int ID_GMCP_RETRY = 7301;
    constexpr int ID_GMCP_CANCEL = 7302;
    constexpr int ID_GMCP_INFO_TEXT = 7305;
    constexpr UINT_PTR ID_GMCP_STEP_TIMER = 7303;
    constexpr UINT_PTR ID_GMCP_WAIT_TIMER = 7304;

    constexpr int kPanelGap = 0;
    constexpr int kDockGap = 0;
    constexpr int kDockSnapDistance = 48;
    constexpr int kDefaultPanelWidth = 380;
    constexpr int kMinimumPanelWidth = 300;
    constexpr int kMaximumPanelWidth = 760;
    constexpr int kDwmExtendedFrameBounds = 9;
    constexpr int kInfoTextTop = 82;

    struct GmcpPacket
    {
        std::string module;
        std::string json;
    };

    struct MapHit
    {
        RECT rc{};
        int x = 0;
        int y = 0;
    };

    struct MapGrid
    {
        int width = 0;
        int height = 0;
        std::vector<std::vector<wchar_t>> cells;
        std::vector<std::pair<int, int>> rooms;
        std::pair<int, int> current{ -1, -1 };
    };

    struct GmcpModel
    {
        std::map<std::string, std::string> modules;
        std::wstring zone;
        std::vector<std::wstring> mapLines;

        double hp = 0.0;
        double maxHp = 0.0;
        double mp = 0.0;
        double maxMp = 0.0;
        bool haveHp = false;
        bool haveMaxHp = false;
        bool haveMp = false;
        bool haveMaxMp = false;

        std::vector<std::wstring> route;
        size_t routeIndex = 0;
        bool routeWaiting = false;
        std::wstring routeStatus;
    };

    GmcpModel g_model;
    HWND g_hwndMap = nullptr;
    HWND g_hwndInfo = nullptr;
    HWND g_hwndInfoText = nullptr;
    HBRUSH g_infoEditBrush = nullptr;
    std::vector<MapHit> g_mapHits;
    bool g_classesRegistered = false;
    bool g_syncingPanelWidth = false;
    bool g_syncingDockLayout = false;
    int g_panelOuterWidth = kDefaultPanelWidth;
    int g_mapOuterHeight = 500;
    int g_infoOuterHeight = 360;
    int g_infoExpandedOuterHeight = 360;
    int g_dockSide = 1; // -1=메인창 왼쪽, 0=해제, 1=오른쪽
    int g_mapScrollRow = 0;
    bool g_infoRawVisible = false;
    bool g_adjustingInfoLayout = false;

    std::mutex g_terminalVitalsMutex;
    std::wstring g_terminalVitalsTail;

    bool ExtractJsonString(const std::string& json, const std::string& key, std::string& value);
    bool ExtractJsonNumber(const std::string& json, const std::vector<std::string>& keys, double& value);
    void UpdateMapScrollBar(HWND hwnd);
    MapGrid BuildMapGrid();
    void ApplyInfoTextColor();


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

    int VisibleWidthForOuter(HWND hwnd, int outerWidth)
    {
        WindowFrameInfo frame = QueryWindowFrame(hwnd);
        int leftInset = static_cast<int>(frame.visible.left - frame.outer.left);
        int rightInset = static_cast<int>(frame.outer.right - frame.visible.right);
        return std::max(1, outerWidth - leftInset - rightInset);
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

    COLORREF BackColor()
    {
        return g_app ? g_app->logStyle.backColor : RGB(0, 0, 0);
    }

    COLORREF TextColor()
    {
        return g_app ? g_app->logStyle.textColor : RGB(220, 220, 220);
    }

    COLORREF AccentColor()
    {
        return RGB(255, 210, 70);
    }

    HFONT MainLogFont()
    {
        if (g_app && g_app->hFontLog)
            return g_app->hFontLog;
        return static_cast<HFONT>(GetStockObject(SYSTEM_FIXED_FONT));
    }

    HFONT UiFont()
    {
        if (g_app && g_app->hFontInput)
            return g_app->hFontInput;
        return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    }

    SIZE MainCellSize(HWND fallback)
    {
        HWND target = (g_app && g_app->hwndLog) ? g_app->hwndLog : fallback;
        SIZE cell = GetLogCellPixelSize(target);
        if (cell.cx <= 0) cell.cx = 8;
        if (cell.cy <= 0) cell.cy = 18;
        return cell;
    }

    const char* ModuleNameForOption(int index)
    {
        static const char* names[GMCP_DISPLAY_COUNT] =
        {
            "Looming.Map",
            "Looming.Vitals",
            "Looming.Cursor",
            "Looming.Char",
            "Looming.Combat",
            "Looming.Party",
            "Looming.Room",
            "Looming.System",
            "Looming.Info",
            "Looming.Chat",
            "Looming.Term",
            nullptr
        };
        if (index < 0 || index >= GMCP_DISPLAY_COUNT)
            return nullptr;
        return names[index];
    }

    std::wstring DisplayModuleName(const std::string& module)
    {
        constexpr const char prefix[] = "Looming.";
        if (module.compare(0, sizeof(prefix) - 1, prefix) == 0)
            return Utf8ToWide(module.substr(sizeof(prefix) - 1));
        return Utf8ToWide(module);
    }

    int OptionForModule(const std::string& module)
    {
        for (int i = 0; i < GMCP_DISPLAY_OTHER; ++i)
        {
            const char* name = ModuleNameForOption(i);
            if (name && module == name)
                return i;
        }
        return GMCP_DISPLAY_OTHER;
    }

    bool ShouldDisplayModule(const std::string& module)
    {
        if (!g_app)
            return false;
        int index = OptionForModule(module);
        if (index == GMCP_DISPLAY_MAP)
            return false;
        return index >= 0 && index < GMCP_DISPLAY_COUNT &&
            g_app->gmcpDisplayModules[index];
    }

    std::wstring StripMudColorTokens(std::wstring text)
    {
        for (;;)
        {
            size_t begin = text.find(L"%^");
            if (begin == std::wstring::npos)
                break;
            size_t end = text.find(L"%^", begin + 2);
            if (end == std::wstring::npos)
            {
                text.erase(begin);
                break;
            }
            text.erase(begin, end + 2 - begin);
        }

        // 혹시 실제 ANSI SGR이 들어와도 체력 숫자 분석에는 영향을 주지 않게 제거합니다.
        for (;;)
        {
            size_t esc = text.find(L'\x1B');
            if (esc == std::wstring::npos)
                break;
            size_t end = esc + 1;
            if (end < text.size() && text[end] == L'[')
            {
                ++end;
                while (end < text.size() &&
                    !((text[end] >= L'@' && text[end] <= L'~')))
                    ++end;
                if (end < text.size()) ++end;
            }
            text.erase(esc, std::max<size_t>(1, end - esc));
        }
        return text;
    }

    bool ReadStrictPromptNumber(const std::wstring& text, size_t& pos, double& value)
    {
        while (pos < text.size() && std::iswspace(text[pos]))
            ++pos;
        if (pos >= text.size() ||
            !(text[pos] == L'-' || (text[pos] >= L'0' && text[pos] <= L'9')))
        {
            return false;
        }

        wchar_t* end = nullptr;
        value = std::wcstod(text.c_str() + pos, &end);
        if (!end || end == text.c_str() + pos)
            return false;
        pos = static_cast<size_t>(end - text.c_str());
        return true;
    }

    bool ParsePlainPromptAt(const std::wstring& text, size_t start, size_t& end,
        double& hp, double& maxHp, double& mp, double& maxMp)
    {
        if (start >= text.size() || text[start] != L'[')
            return false;

        size_t pos = start + 1;
        if (!ReadStrictPromptNumber(text, pos, hp))
            return false;

        while (pos < text.size() && std::iswspace(text[pos]))
            ++pos;
        if (pos >= text.size() || text[pos] != L'/')
            return false;
        ++pos;

        if (!ReadStrictPromptNumber(text, pos, maxHp))
            return false;
        while (pos < text.size() && std::iswspace(text[pos]))
            ++pos;
        if (pos >= text.size() || text[pos] != L',')
            return false;
        ++pos;

        if (!ReadStrictPromptNumber(text, pos, mp))
            return false;
        while (pos < text.size() && std::iswspace(text[pos]))
            ++pos;
        if (pos >= text.size() || text[pos] != L'/')
            return false;
        ++pos;

        if (!ReadStrictPromptNumber(text, pos, maxMp))
            return false;
        while (pos < text.size() && std::iswspace(text[pos]))
            ++pos;

        // Sector_D 기본 프롬프트는 명령/피로 수치를 <...> 안에 넣습니다.
        // 이 표식을 요구하여 일반 문장 속 숫자 배열을 프롬프트로 오인하지 않습니다.
        if (pos >= text.size() || text[pos] != L'<')
            return false;

        const size_t greater = text.find(L'>', pos + 1);
        if (greater == std::wstring::npos || greater - start > 128)
            return false;
        const size_t close = text.find(L']', greater + 1);
        if (close == std::wstring::npos || close - start > 256)
            return false;

        end = close + 1;
        return maxHp > 0.0 && maxMp > 0.0;
    }

    std::string NumberForJson(double value)
    {
        std::ostringstream out;
        out.precision(15);
        out << value;
        return out.str();
    }

    void PostTerminalVitalsPacket(double hp, double maxHp, double mp, double maxMp)
    {
        if (!g_app || !g_app->hwndMain)
            return;

        std::unique_ptr<GmcpPacket> packet(new (std::nothrow) GmcpPacket());
        if (!packet)
            return;

        // 실제 GMCP 모듈과 구분되는 내부 이벤트입니다. ApplyPacket()은 이를
        // 원문 모듈 목록에 보관하지 않고 상태 막대 갱신에만 사용합니다.
        packet->module = "KTin.TerminalVitals";
        packet->json = "{\"hp\":" + NumberForJson(hp) +
            ",\"max_hp\":" + NumberForJson(maxHp) +
            ",\"mp\":" + NumberForJson(mp) +
            ",\"max_mp\":" + NumberForJson(maxMp) + "}";

        GmcpPacket* raw = packet.release();
        if (!PostMessageW(g_app->hwndMain, WM_APP_GMCP_UPDATE, 0,
            reinterpret_cast<LPARAM>(raw)))
        {
            delete raw;
        }
    }

    void UpdateVitalsFromPacket(const GmcpPacket& packet)
    {
        double hp = 0.0, maxHp = 0.0, mp = 0.0, maxMp = 0.0;
        bool parsed = false;

        if (packet.module == "Looming.Vitals" ||
            packet.module == "KTin.TerminalVitals")
        {
            bool haveHp = ExtractJsonNumber(packet.json,
                { "hp", "health", "체력" }, hp);
            bool haveMaxHp = ExtractJsonNumber(packet.json,
                { "max_hp", "maxhp", "health_max", "최대체력" }, maxHp);
            bool haveMp = ExtractJsonNumber(packet.json,
                { "mp", "mana", "정신력" }, mp);
            bool haveMaxMp = ExtractJsonNumber(packet.json,
                { "max_mp", "maxmp", "mana_max", "최대정신력" }, maxMp);
            parsed = haveHp && haveMaxHp && haveMp && haveMaxMp &&
                maxHp > 0.0 && maxMp > 0.0;
        }

        if (parsed)
        {
            g_model.hp = hp;
            g_model.maxHp = maxHp;
            g_model.mp = mp;
            g_model.maxMp = maxMp;
            g_model.haveHp = true;
            g_model.haveMaxHp = true;
            g_model.haveMp = true;
            g_model.haveMaxMp = true;
        }
    }

    RECT WorkAreaForWindow(HWND hwnd)
    {
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        if (monitor && GetMonitorInfoW(monitor, &mi))
            return mi.rcWork;
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        return work;
    }

    int ClampPanelWidth(int width)
    {
        return std::max(kMinimumPanelWidth, std::min(kMaximumPanelWidth, width));
    }

    int OuterHeight(HWND hwnd, int fallback)
    {
        RECT rc{};
        if (hwnd && IsWindow(hwnd) && GetWindowRect(hwnd, &rc))
            return std::max(100, static_cast<int>(rc.bottom - rc.top));
        return fallback;
    }

    void SetWindowOuterSize(HWND hwnd, int width, int height)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;
        RECT rc{};
        GetWindowRect(hwnd, &rc);
        SetWindowPos(hwnd, nullptr, rc.left, rc.top, width, height,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void SyncPanelWidths(HWND source, int requestedWidth)
    {
        if (g_syncingPanelWidth)
            return;
        g_syncingPanelWidth = true;
        g_panelOuterWidth = ClampPanelWidth(requestedWidth);

        HWND windows[2] = { g_hwndMap, g_hwndInfo };
        for (HWND wnd : windows)
        {
            if (!wnd || !IsWindow(wnd))
                continue;
            RECT rc{};
            GetWindowRect(wnd, &rc);
            int width = rc.right - rc.left;
            if (width == g_panelOuterWidth)
                continue;
            SetWindowPos(wnd, nullptr, 0, 0, g_panelOuterWidth,
                rc.bottom - rc.top,
                SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        g_syncingPanelWidth = false;
    }

    void DockGmcpWindowsToMainInternal()
    {
        if (g_syncingDockLayout || g_dockSide == 0 || !g_app ||
            !g_app->hwndMain || !IsWindow(g_app->hwndMain))
            return;

        g_syncingDockLayout = true;
        WindowFrameInfo mainFrame = QueryWindowFrame(g_app->hwndMain);
        RECT mainVisible = mainFrame.visible;
        RECT work = WorkAreaForWindow(g_app->hwndMain);

        bool mapVisible = g_hwndMap && IsWindow(g_hwndMap) && IsWindowVisible(g_hwndMap);
        bool infoVisible = g_hwndInfo && IsWindow(g_hwndInfo) && IsWindowVisible(g_hwndInfo);
        HWND widthReference = mapVisible ? g_hwndMap : g_hwndInfo;
        int panelVisibleWidth = VisibleWidthForOuter(widthReference, g_panelOuterWidth);
        int visibleLeft = g_dockSide < 0
            ? static_cast<int>(mainVisible.left) - panelVisibleWidth - kDockGap
            : static_cast<int>(mainVisible.right) + kDockGap;
        visibleLeft = std::max(static_cast<int>(work.left),
            std::min(visibleLeft, static_cast<int>(work.right) - panelVisibleWidth));
        int visibleTop = std::max(static_cast<int>(work.top),
            static_cast<int>(mainVisible.top));

        int totalAvailable = std::max(220,
            static_cast<int>(work.bottom) - visibleTop);
        int mapHeight = mapVisible
            ? OuterHeight(g_hwndMap, g_mapOuterHeight) : 0;
        int infoHeight = infoVisible
            ? OuterHeight(g_hwndInfo, g_infoOuterHeight) : 0;

        if (mapVisible && infoVisible && mapHeight + infoHeight > totalAvailable)
        {
            int minimumInfo = g_infoRawVisible ? 180 : 110;
            int maximumMap = std::max(220, totalAvailable - minimumInfo);
            mapHeight = std::min(mapHeight, maximumMap);
            infoHeight = std::max(minimumInfo, totalAvailable - mapHeight);
        }
        else if (mapVisible)
        {
            mapHeight = std::min(mapHeight, totalAvailable);
        }
        else if (infoVisible)
        {
            infoHeight = std::min(infoHeight, totalAvailable);
        }

        if (mapVisible)
        {
            PlaceWindowByVisibleTopLeft(g_hwndMap, visibleLeft, visibleTop,
                g_panelOuterWidth, mapHeight);
            UpdateMapScrollBar(g_hwndMap);
            WindowFrameInfo mapFrame = QueryWindowFrame(g_hwndMap);
            visibleTop = static_cast<int>(mapFrame.visible.bottom) + kPanelGap;
        }
        if (infoVisible)
        {
            PlaceWindowByVisibleTopLeft(g_hwndInfo, visibleLeft, visibleTop,
                g_panelOuterWidth, infoHeight);
        }
        g_syncingDockLayout = false;
    }

    void UpdateDockSideFromMovedWindow(HWND hwnd)
    {
        if (g_syncingDockLayout || !g_app || !g_app->hwndMain || !IsWindow(hwnd))
            return;
        WindowFrameInfo mainFrame = QueryWindowFrame(g_app->hwndMain);
        WindowFrameInfo movedFrame = QueryWindowFrame(hwnd);

        int leftDistance = std::abs(static_cast<int>(
            movedFrame.visible.right - mainFrame.visible.left));
        int rightDistance = std::abs(static_cast<int>(
            movedFrame.visible.left - mainFrame.visible.right));
        if (leftDistance <= kDockSnapDistance)
            g_dockSide = -1;
        else if (rightDistance <= kDockSnapDistance)
            g_dockSide = 1;
        else
        {
            g_dockSide = 0;
            return;
        }
        DockGmcpWindowsToMainInternal();
    }

    int MapHeaderHeight(HWND hwnd)
    {
        SIZE cell = MainCellSize(hwnd);
        return 8 + cell.cy + 4;
    }

    int MapFooterHeight()
    {
        return 38;
    }

    int MapVisibleRows(HWND hwnd)
    {
        RECT client{};
        GetClientRect(hwnd, &client);
        SIZE cell = MainCellSize(hwnd);
        int pixels = (client.bottom - client.top) - MapHeaderHeight(hwnd) - MapFooterHeight();
        return std::max(1, pixels / std::max(1, static_cast<int>(cell.cy)));
    }

    void UpdateMapScrollBar(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;

        int totalRows = std::max(1, static_cast<int>(g_model.mapLines.size()));
        int pageRows = MapVisibleRows(hwnd);
        int maxTop = std::max(0, totalRows - pageRows);
        g_mapScrollRow = std::max(0, std::min(g_mapScrollRow, maxTop));

        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = totalRows - 1;
        si.nPage = static_cast<UINT>(pageRows);
        si.nPos = g_mapScrollRow;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        ShowScrollBar(hwnd, SB_VERT, totalRows > pageRows);
    }

    void SetMapScrollRow(HWND hwnd, int row)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;
        int totalRows = std::max(1, static_cast<int>(g_model.mapLines.size()));
        int maxTop = std::max(0, totalRows - MapVisibleRows(hwnd));
        int next = std::max(0, std::min(row, maxTop));
        if (next == g_mapScrollRow)
            return;
        g_mapScrollRow = next;
        UpdateMapScrollBar(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }

    void CenterMapOnCurrentRoom(HWND hwnd)
    {
        if (!hwnd || !IsWindow(hwnd))
            return;
        MapGrid grid = BuildMapGrid();
        if (grid.current.second < 0)
        {
            UpdateMapScrollBar(hwnd);
            return;
        }
        int pageRows = MapVisibleRows(hwnd);
        SetMapScrollRow(hwnd, grid.current.second - pageRows / 2);
    }

    int Base64Value(unsigned char ch)
    {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    }

    bool DecodeBase64(std::string_view input, std::string& output)
    {
        output.clear();
        output.reserve((input.size() / 4) * 3);
        unsigned int value = 0;
        int bits = -8;

        for (unsigned char ch : input)
        {
            if (ch == '=')
                break;
            if (std::isspace(ch))
                continue;

            int n = Base64Value(ch);
            if (n < 0)
                return false;

            value = (value << 6) | static_cast<unsigned int>(n);
            bits += 6;
            if (bits >= 0)
            {
                output.push_back(static_cast<char>((value >> bits) & 0xFF));
                bits -= 8;
            }
        }
        return true;
    }

    void SkipSpaces(const std::string& text, size_t& pos)
    {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])))
            ++pos;
    }

    void AppendUtf8Codepoint(std::string& out, unsigned int cp)
    {
        if (cp <= 0x7F)
            out.push_back(static_cast<char>(cp));
        else if (cp <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseJsonString(const std::string& text, size_t& pos, std::string& out)
    {
        SkipSpaces(text, pos);
        if (pos >= text.size() || text[pos] != '"')
            return false;

        ++pos;
        out.clear();
        while (pos < text.size())
        {
            unsigned char ch = static_cast<unsigned char>(text[pos++]);
            if (ch == '"')
                return true;
            if (ch != '\\')
            {
                out.push_back(static_cast<char>(ch));
                continue;
            }

            if (pos >= text.size())
                return false;
            char esc = text[pos++];
            switch (esc)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
            {
                if (pos + 4 > text.size()) return false;
                unsigned int cp = 0;
                for (int i = 0; i < 4; ++i)
                {
                    char h = text[pos++];
                    cp <<= 4;
                    if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                    else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(h - 'a' + 10);
                    else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(h - 'A' + 10);
                    else return false;
                }
                AppendUtf8Codepoint(out, cp);
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool FindJsonKeyValueStart(const std::string& json, const std::string& key, size_t& valuePos)
    {
        size_t pos = 0;
        while (pos < json.size())
        {
            if (json[pos] != '"')
            {
                ++pos;
                continue;
            }

            size_t keyPos = pos;
            std::string found;
            if (!ParseJsonString(json, keyPos, found))
            {
                ++pos;
                continue;
            }

            size_t colon = keyPos;
            SkipSpaces(json, colon);
            if (colon < json.size() && json[colon] == ':' && found == key)
            {
                valuePos = colon + 1;
                SkipSpaces(json, valuePos);
                return true;
            }
            pos = keyPos;
        }
        return false;
    }

    bool ExtractJsonString(const std::string& json, const std::string& key, std::string& value)
    {
        size_t pos = 0;
        if (!FindJsonKeyValueStart(json, key, pos))
            return false;
        return ParseJsonString(json, pos, value);
    }

    bool ExtractJsonStringArray(const std::string& json, const std::string& key, std::vector<std::string>& values)
    {
        size_t pos = 0;
        if (!FindJsonKeyValueStart(json, key, pos) || pos >= json.size() || json[pos] != '[')
            return false;

        ++pos;
        values.clear();
        for (;;)
        {
            SkipSpaces(json, pos);
            if (pos >= json.size()) return false;
            if (json[pos] == ']') return true;

            std::string value;
            if (!ParseJsonString(json, pos, value))
                return false;
            values.push_back(std::move(value));

            SkipSpaces(json, pos);
            if (pos >= json.size()) return false;
            if (json[pos] == ',')
            {
                ++pos;
                continue;
            }
            if (json[pos] == ']')
                return true;
            return false;
        }
    }

    bool ExtractJsonNumber(const std::string& json, const std::vector<std::string>& keys, double& value)
    {
        for (const std::string& key : keys)
        {
            size_t pos = 0;
            if (!FindJsonKeyValueStart(json, key, pos))
                continue;

            char* end = nullptr;
            value = std::strtod(json.c_str() + pos, &end);
            if (end && end != json.c_str() + pos)
                return true;
        }
        return false;
    }

    std::wstring PrettyJson(const std::string& json)
    {
        std::string out;
        out.reserve(json.size() + 64);
        bool inString = false;
        bool escaped = false;
        int indent = 0;

        auto newline = [&]()
        {
            out.push_back('\n');
            out.append(static_cast<size_t>(std::max(0, indent)) * 2, ' ');
        };

        for (char ch : json)
        {
            if (inString)
            {
                out.push_back(ch);
                if (escaped) escaped = false;
                else if (ch == '\\') escaped = true;
                else if (ch == '"') inString = false;
                continue;
            }

            if (ch == '"')
            {
                inString = true;
                out.push_back(ch);
            }
            else if (ch == '{' || ch == '[')
            {
                out.push_back(ch);
                ++indent;
                newline();
            }
            else if (ch == '}' || ch == ']')
            {
                --indent;
                newline();
                out.push_back(ch);
            }
            else if (ch == ',')
            {
                out.push_back(ch);
                newline();
            }
            else if (ch == ':')
            {
                out += ": ";
            }
            else if (!std::isspace(static_cast<unsigned char>(ch)))
            {
                out.push_back(ch);
            }
        }
        return Utf8ToWide(out);
    }

    bool IsRoomGlyph(wchar_t ch)
    {
        switch (ch)
        {
        case L'○': case L'⊙': case L'◎': case L'▣':
        case L'∧': case L'∨': case L'↕':
            return true;
        default:
            return false;
        }
    }

    MapGrid BuildMapGrid()
    {
        MapGrid grid;
        grid.height = static_cast<int>(g_model.mapLines.size());

        for (const std::wstring& line : g_model.mapLines)
        {
            int w = 0;
            for (wchar_t ch : line)
                w += std::max(1, KTinCharWidth(ch));
            grid.width = std::max(grid.width, w);
        }

        if (grid.width <= 0 || grid.height <= 0)
            return grid;

        grid.cells.assign(static_cast<size_t>(grid.height),
            std::vector<wchar_t>(static_cast<size_t>(grid.width), L' '));

        for (int y = 0; y < grid.height; ++y)
        {
            int x = 0;
            for (wchar_t ch : g_model.mapLines[static_cast<size_t>(y)])
            {
                int cw = std::max(1, KTinCharWidth(ch));
                if (x >= grid.width) break;
                grid.cells[static_cast<size_t>(y)][static_cast<size_t>(x)] = ch;
                for (int k = 1; k < cw && x + k < grid.width; ++k)
                    grid.cells[static_cast<size_t>(y)][static_cast<size_t>(x + k)] = 0;

                if (IsRoomGlyph(ch))
                {
                    grid.rooms.emplace_back(x, y);
                    if (ch == L'▣') grid.current = { x, y };
                }
                x += cw;
            }
        }
        return grid;
    }

    wchar_t CellAt(const MapGrid& grid, int x, int y)
    {
        if (x < 0 || y < 0 || x >= grid.width || y >= grid.height)
            return L' ';
        return grid.cells[static_cast<size_t>(y)][static_cast<size_t>(x)];
    }

    bool HasRoomAt(const MapGrid& grid, int x, int y)
    {
        return IsRoomGlyph(CellAt(grid, x, y));
    }

    struct DirectionRule
    {
        int dx;
        int dy;
        int mx;
        int my;
        const wchar_t* connectors;
        const wchar_t* command;
    };

    const DirectionRule kDirections[] =
    {
        {  4,  0,  2,  0, L"─→", L"동" },
        { -4,  0, -2,  0, L"─←", L"서" },
        {  0,  2,  0,  1, L"│↓", L"남" },
        {  0, -2,  0, -1, L"│↑", L"북" },
        {  4, -2,  2, -1, L"／↗", L"북동" },
        { -4, -2, -2, -1, L"＼↖", L"북서" },
        {  4,  2,  2,  1, L"＼↘", L"남동" },
        { -4,  2, -2,  1, L"／↙", L"남서" }
    };

    std::vector<std::wstring> FindRouteTo(int targetX, int targetY)
    {
        MapGrid grid = BuildMapGrid();
        if (grid.current.first < 0 || !HasRoomAt(grid, targetX, targetY))
            return {};

        using Point = std::pair<int, int>;
        std::queue<Point> work;
        std::set<Point> seen;
        std::map<Point, std::pair<Point, std::wstring>> previous;

        work.push(grid.current);
        seen.insert(grid.current);

        while (!work.empty())
        {
            Point p = work.front();
            work.pop();
            if (p.first == targetX && p.second == targetY)
                break;

            for (const DirectionRule& rule : kDirections)
            {
                Point next{ p.first + rule.dx, p.second + rule.dy };
                wchar_t connector = CellAt(grid, p.first + rule.mx, p.second + rule.my);
                if (!HasRoomAt(grid, next.first, next.second) ||
                    std::wcschr(rule.connectors, connector) == nullptr ||
                    seen.find(next) != seen.end())
                    continue;

                seen.insert(next);
                previous[next] = { p, rule.command };
                work.push(next);
            }
        }

        Point target{ targetX, targetY };
        if (target != grid.current && previous.find(target) == previous.end())
            return {};

        std::vector<std::wstring> route;
        while (target != grid.current)
        {
            auto it = previous.find(target);
            if (it == previous.end()) return {};
            route.push_back(it->second.second);
            target = it->second.first;
        }
        std::reverse(route.begin(), route.end());
        return route;
    }

    void RefreshWindows()
    {
        if (g_hwndMap && IsWindow(g_hwndMap)) InvalidateRect(g_hwndMap, nullptr, FALSE);
        if (g_hwndInfo && IsWindow(g_hwndInfo)) InvalidateRect(g_hwndInfo, nullptr, FALSE);
    }

    void StopRoute(const wchar_t* status)
    {
        g_model.route.clear();
        g_model.routeIndex = 0;
        g_model.routeWaiting = false;
        g_model.routeStatus = status ? status : L"";
        if (g_hwndMap && IsWindow(g_hwndMap))
        {
            KillTimer(g_hwndMap, ID_GMCP_STEP_TIMER);
            KillTimer(g_hwndMap, ID_GMCP_WAIT_TIMER);
            InvalidateRect(g_hwndMap, nullptr, FALSE);
        }
    }

    void SendCurrentRouteStep()
    {
        if (g_model.routeIndex >= g_model.route.size())
        {
            StopRoute(L"이동 완료");
            return;
        }

        const std::wstring& command = g_model.route[g_model.routeIndex];
        SendTextToMud(command);
        g_model.routeWaiting = true;
        g_model.routeStatus = L"이동 중: " + command + L"  (방 정보 대기)";

        if (g_hwndMap && IsWindow(g_hwndMap))
        {
            KillTimer(g_hwndMap, ID_GMCP_WAIT_TIMER);
            SetTimer(g_hwndMap, ID_GMCP_WAIT_TIMER, 4000, nullptr);
            InvalidateRect(g_hwndMap, nullptr, FALSE);
        }
    }

    void OnMapPacketReceived()
    {
        if (!g_model.routeWaiting)
            return;

        g_model.routeWaiting = false;
        ++g_model.routeIndex;
        if (g_model.routeIndex >= g_model.route.size())
        {
            StopRoute(L"이동 완료");
            return;
        }

        if (g_hwndMap && IsWindow(g_hwndMap))
        {
            KillTimer(g_hwndMap, ID_GMCP_WAIT_TIMER);
            SetTimer(g_hwndMap, ID_GMCP_STEP_TIMER, 120, nullptr);
        }
    }

    void UpdateMap(const std::string& json)
    {
        std::string zone;
        std::vector<std::string> lines;
        if (ExtractJsonString(json, "zone", zone))
            g_model.zone = Utf8ToWide(zone);
        if (ExtractJsonStringArray(json, "lines", lines))
        {
            g_model.mapLines.clear();
            g_model.mapLines.reserve(lines.size());
            for (const std::string& line : lines)
                g_model.mapLines.push_back(Utf8ToWide(line));
        }
    }

    void ApplyFontsToGmcpWindows()
    {
        HFONT logFont = MainLogFont();
        HFONT uiFont = UiFont();

        if (g_hwndMap && IsWindow(g_hwndMap))
        {
            SendMessageW(g_hwndMap, WM_SETFONT, reinterpret_cast<WPARAM>(logFont), TRUE);
            HWND retry = GetDlgItem(g_hwndMap, ID_GMCP_RETRY);
            HWND cancel = GetDlgItem(g_hwndMap, ID_GMCP_CANCEL);
            if (retry) SendMessageW(retry, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
            InvalidateRect(g_hwndMap, nullptr, FALSE);
        }

        if (g_hwndInfo && IsWindow(g_hwndInfo))
            InvalidateRect(g_hwndInfo, nullptr, FALSE);
        if (g_hwndInfoText && IsWindow(g_hwndInfoText))
        {
            SendMessageW(g_hwndInfoText, WM_SETFONT,
                reinterpret_cast<WPARAM>(logFont), TRUE);
            SendMessageW(g_hwndInfoText, EM_SETBKGNDCOLOR, 0, BackColor());
            ApplyInfoTextColor();
            InvalidateRect(g_hwndInfoText, nullptr, TRUE);
        }
    }

    void DrawProgress(HDC hdc, const RECT& rc, const wchar_t* label,
        bool haveValue, double value, double maximum, COLORREF fill)
    {
        wchar_t valueText[128] = {};
        double ratio = 0.0;
        if (haveValue && maximum > 0.0)
        {
            ratio = std::max(0.0, std::min(1.0, value / maximum));
            int percent = static_cast<int>(std::lround(ratio * 100.0));
            swprintf_s(valueText, L"%.0f/%.0f %d%%", value, maximum, percent);
        }
        else
        {
            wcscpy_s(valueText, L"수신 대기");
        }

        SIZE cell = MainCellSize(g_hwndInfo);
        const int gap = std::max(1, static_cast<int>(cell.cx));
        SIZE labelSize{};
        SIZE valueSize{};
        GetTextExtentPoint32W(hdc, label, static_cast<int>(wcslen(label)), &labelSize);
        GetTextExtentPoint32W(hdc, valueText, static_cast<int>(wcslen(valueText)), &valueSize);

        RECT labelRc = rc;
        labelRc.right = labelRc.left + labelSize.cx;
        SetTextColor(hdc, TextColor());
        DrawTextW(hdc, label, -1, &labelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT valueRc = rc;
        valueRc.left = std::max(rc.left, rc.right - valueSize.cx);
        DrawTextW(hdc, valueText, -1, &valueRc,
            DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT bar = rc;
        bar.left = labelRc.right + gap;
        bar.right = valueRc.left - gap;
        if (bar.right < bar.left + 40)
            bar.right = bar.left + 40;

        HBRUSH emptyBrush = CreateSolidBrush(RGB(32, 32, 32));
        FillRect(hdc, &bar, emptyBrush);
        DeleteObject(emptyBrush);

        HBRUSH border = CreateSolidBrush(RGB(100, 100, 100));
        FrameRect(hdc, &bar, border);
        DeleteObject(border);

        if (haveValue && maximum > 0.0)
        {
            RECT filled = bar;
            filled.left += 1;
            filled.top += 1;
            filled.bottom -= 1;
            filled.right = filled.left +
                static_cast<int>((bar.right - bar.left - 2) * ratio);
            HBRUSH brush = CreateSolidBrush(fill);
            FillRect(hdc, &filled, brush);
            DeleteObject(brush);
        }
    }

    bool HasReceivedSelectedModule()
    {
        if (!g_app)
            return false;

        for (int i = GMCP_DISPLAY_VITALS; i < GMCP_DISPLAY_OTHER; ++i)
        {
            const char* name = ModuleNameForOption(i);
            if (!name || !g_app->gmcpDisplayModules[i])
                continue;
            if (i == GMCP_DISPLAY_VITALS &&
                g_model.haveHp && g_model.haveMaxHp &&
                g_model.haveMp && g_model.haveMaxMp)
            {
                return true;
            }
            if (g_model.modules.find(name) != g_model.modules.end())
                return true;
        }

        if (g_app->gmcpDisplayModules[GMCP_DISPLAY_OTHER])
        {
            for (const auto& item : g_model.modules)
            {
                if (OptionForModule(item.first) == GMCP_DISPLAY_OTHER)
                    return true;
            }
        }
        return false;
    }

    std::wstring BuildWaitingModuleText()
    {
        if (!g_app)
            return L"";

        std::vector<std::wstring> waiting;
        bool selectedAny = false;
        for (int i = GMCP_DISPLAY_VITALS; i < GMCP_DISPLAY_OTHER; ++i)
        {
            if (!g_app->gmcpDisplayModules[i])
                continue;
            selectedAny = true;
            const char* name = ModuleNameForOption(i);
            if (i == GMCP_DISPLAY_VITALS &&
                g_model.haveHp && g_model.haveMaxHp &&
                g_model.haveMp && g_model.haveMaxMp)
            {
                continue;
            }
            if (name && g_model.modules.find(name) == g_model.modules.end())
                waiting.push_back(DisplayModuleName(name));
        }

        if (g_app->gmcpDisplayModules[GMCP_DISPLAY_OTHER])
        {
            selectedAny = true;
            bool haveOther = false;
            for (const auto& item : g_model.modules)
            {
                if (OptionForModule(item.first) == GMCP_DISPLAY_OTHER)
                {
                    haveOther = true;
                    break;
                }
            }
            if (!haveOther)
                waiting.push_back(L"기타 모듈");
        }

        if (!selectedAny)
            return L"원본 모듈 표시 안 함";
        if (waiting.empty())
            return L"";

        std::wstringstream out;
        out << L"수신 대기: ";
        for (size_t i = 0; i < waiting.size(); ++i)
        {
            if (i != 0) out << L", ";
            out << waiting[i];
        }
        return out.str();
    }

    std::wstring BuildInfoText()
    {
        std::wstringstream out;
        bool emittedAny = false;

        auto appendModule = [&](const std::string& name, const std::string& json)
        {
            if (emittedAny)
                out << L"\r\n";
            emittedAny = true;
            out << L"[" << DisplayModuleName(name) << L"]\r\n";
            std::wstring pretty = PrettyJson(json);
            for (wchar_t ch : pretty)
            {
                if (ch == L'\n') out << L"\r\n";
                else out << ch;
            }
            out << L"\r\n";
        };

        // Map은 전용 지도창에서 사용하므로 정보창 원문 표시 대상에서 제외합니다.
        for (int i = GMCP_DISPLAY_VITALS; i < GMCP_DISPLAY_OTHER; ++i)
        {
            const char* name = ModuleNameForOption(i);
            if (!name || !g_app || !g_app->gmcpDisplayModules[i])
                continue;
            auto it = g_model.modules.find(name);
            if (it != g_model.modules.end())
                appendModule(it->first, it->second);
        }

        if (g_app && g_app->gmcpDisplayModules[GMCP_DISPLAY_OTHER])
        {
            for (const auto& item : g_model.modules)
            {
                if (OptionForModule(item.first) == GMCP_DISPLAY_OTHER)
                    appendModule(item.first, item.second);
            }
        }

        std::wstring waiting = BuildWaitingModuleText();
        if (emittedAny && !waiting.empty() && waiting != L"원본 모듈 표시 안 함")
            out << L"\r\n[수신 대기]\r\n" << waiting.substr(7) << L"\r\n";
        return out.str();
    }

    int CompactInfoOuterHeight(HWND hwnd)
    {
        SIZE cell = MainCellSize(hwnd);
        int clientHeight = std::max(108, 82 + static_cast<int>(cell.cy) + 12);
        RECT outer{ 0, 0, g_panelOuterWidth, clientHeight };
        AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            FALSE, WS_EX_TOOLWINDOW);
        return std::max(120, static_cast<int>(outer.bottom - outer.top));
    }

    void ApplyInfoTextColor()
    {
        if (!g_hwndInfoText || !IsWindow(g_hwndInfoText))
            return;
        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR;
        format.dwEffects = 0;
        format.crTextColor = TextColor();
        SendMessageW(g_hwndInfoText, EM_SETCHARFORMAT, SCF_DEFAULT,
            reinterpret_cast<LPARAM>(&format));
        SendMessageW(g_hwndInfoText, EM_SETCHARFORMAT, SCF_ALL,
            reinterpret_cast<LPARAM>(&format));
    }

    void UpdateInfoRawVisibility(bool showRaw)
    {
        if (!g_hwndInfo || !IsWindow(g_hwndInfo))
            return;

        if (g_hwndInfoText && IsWindow(g_hwndInfoText))
            ShowWindow(g_hwndInfoText, showRaw ? SW_SHOW : SW_HIDE);

        if (g_infoRawVisible == showRaw && showRaw)
            return;

        g_adjustingInfoLayout = true;
        if (!showRaw)
        {
            RECT rc{};
            if (GetWindowRect(g_hwndInfo, &rc))
                g_infoExpandedOuterHeight = std::max(180,
                    static_cast<int>(rc.bottom - rc.top));
        }

        g_infoRawVisible = showRaw;
        int targetHeight = showRaw
            ? std::max(220, g_infoExpandedOuterHeight)
            : CompactInfoOuterHeight(g_hwndInfo);
        g_infoOuterHeight = targetHeight;
        SetWindowOuterSize(g_hwndInfo, g_panelOuterWidth, targetHeight);
        g_adjustingInfoLayout = false;
        DockGmcpWindowsToMainInternal();
    }

    void UpdateInfoTextControl(bool preserveScroll = true)
    {
        if (!g_hwndInfoText || !IsWindow(g_hwndInfoText))
            return;

        bool showRaw = HasReceivedSelectedModule();
        UpdateInfoRawVisibility(showRaw);
        if (!showRaw)
        {
            SetWindowTextW(g_hwndInfoText, L"");
            if (g_hwndInfo && IsWindow(g_hwndInfo))
                InvalidateRect(g_hwndInfo, nullptr, TRUE);
            return;
        }

        LRESULT firstVisible = preserveScroll
            ? SendMessageW(g_hwndInfoText, EM_GETFIRSTVISIBLELINE, 0, 0)
            : 0;
        std::wstring text = BuildInfoText();
        SetWindowTextW(g_hwndInfoText, text.c_str());
        ApplyInfoTextColor();

        RECT client{};
        GetClientRect(g_hwndInfoText, &client);
        SIZE cell = MainCellSize(g_hwndInfoText);
        const int clientHeight = static_cast<int>(client.bottom - client.top);
        const int cellHeight = std::max(1, static_cast<int>(cell.cy));
        int visibleLines = std::max(1, clientHeight / cellHeight);
        int lineCount = static_cast<int>(SendMessageW(
            g_hwndInfoText, EM_GETLINECOUNT, 0, 0));
        int maxTop = std::max(0, lineCount - visibleLines);
        int target = preserveScroll
            ? std::max(0, std::min(static_cast<int>(firstVisible), maxTop))
            : 0;
        if (target > 0)
            SendMessageW(g_hwndInfoText, EM_LINESCROLL, 0, target);
        else
        {
            SendMessageW(g_hwndInfoText, EM_SETSEL, 0, 0);
            SendMessageW(g_hwndInfoText, EM_SCROLLCARET, 0, 0);
        }
        InvalidateRect(g_hwndInfoText, nullptr, TRUE);
    }

    void AutoFitMapWindow()
    {
        MapGrid grid = BuildMapGrid();
        SIZE cell = MainCellSize(g_hwndMap);
        int cellW = std::max(1, static_cast<int>(cell.cx));
        int cellH = std::max(1, static_cast<int>(cell.cy));

        int desiredClientWidth = std::max(280, grid.width * cellW + 24);
        int desiredClientHeight = 12 + cellH + 4 +
            std::max(1, grid.height) * cellH + 42;

        RECT outer{ 0, 0, desiredClientWidth, desiredClientHeight };
        AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
            FALSE, WS_EX_TOOLWINDOW);
        g_panelOuterWidth = ClampPanelWidth(outer.right - outer.left);
        g_mapOuterHeight = std::max(220,
            static_cast<int>(outer.bottom - outer.top));

        if (g_hwndMap && IsWindow(g_hwndMap))
        {
            SetWindowOuterSize(g_hwndMap, g_panelOuterWidth, g_mapOuterHeight);
            SyncPanelWidths(g_hwndMap, g_panelOuterWidth);
            UpdateMapScrollBar(g_hwndMap);
            DockGmcpWindowsToMainInternal();
        }
    }

    void ApplyPacket(const GmcpPacket& packet)
    {
        if (packet.module != "KTin.TerminalVitals")
            g_model.modules[packet.module] = packet.json;
        UpdateVitalsFromPacket(packet);

        if (packet.module == "Looming.Map")
        {
            UpdateMap(packet.json);
            OnMapPacketReceived();
            AutoFitMapWindow();
            if (g_hwndMap && IsWindow(g_hwndMap))
                CenterMapOnCurrentRoom(g_hwndMap);
        }

        UpdateInfoTextControl();
        RefreshWindows();
    }

    LRESULT CALLBACK MapWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            HWND retry = CreateWindowW(L"BUTTON", L"다시 시도",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 86, 26, hwnd,
                reinterpret_cast<HMENU>(ID_GMCP_RETRY),
                GetModuleHandleW(nullptr), nullptr);
            HWND cancel = CreateWindowW(L"BUTTON", L"이동 취소",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 86, 26, hwnd,
                reinterpret_cast<HMENU>(ID_GMCP_CANCEL),
                GetModuleHandleW(nullptr), nullptr);
            if (retry) SendMessageW(retry, WM_SETFONT,
                reinterpret_cast<WPARAM>(UiFont()), TRUE);
            if (cancel) SendMessageW(cancel, WM_SETFONT,
                reinterpret_cast<WPARAM>(UiFont()), TRUE);
            UpdateMapScrollBar(hwnd);
            return 0;
        }

        case WM_SIZE:
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            HWND retry = GetDlgItem(hwnd, ID_GMCP_RETRY);
            HWND cancel = GetDlgItem(hwnd, ID_GMCP_CANCEL);
            int y = std::max(4, height - 32);
            const int buttonWidth = 86;
            const int buttonGap = 8;
            const int groupWidth = buttonWidth * 2 + buttonGap;
            const int startX = std::max(8, (width - groupWidth) / 2);
            if (retry) MoveWindow(retry, startX, y, buttonWidth, 26, TRUE);
            if (cancel) MoveWindow(cancel, startX + buttonWidth + buttonGap, y, buttonWidth, 26, TRUE);

            UpdateMapScrollBar(hwnd);
            if (!g_syncingPanelWidth && !g_syncingDockLayout)
            {
                RECT rc{};
                GetWindowRect(hwnd, &rc);
                g_mapOuterHeight = rc.bottom - rc.top;
                SyncPanelWidths(hwnd, rc.right - rc.left);
            }
            return 0;
        }

        case WM_VSCROLL:
        {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int row = g_mapScrollRow;
            switch (LOWORD(wParam))
            {
            case SB_LINEUP: row -= 1; break;
            case SB_LINEDOWN: row += 1; break;
            case SB_PAGEUP: row -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: row += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: row = si.nTrackPos; break;
            case SB_TOP: row = 0; break;
            case SB_BOTTOM: row = si.nMax; break;
            default: return 0;
            }
            SetMapScrollRow(hwnd, row);
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            if (lines == 0)
                return 0;
            int rowCount = lines == WHEEL_PAGESCROLL
                ? MapVisibleRows(hwnd)
                : static_cast<int>(lines);
            int notches = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
            SetMapScrollRow(hwnd, g_mapScrollRow - notches * rowCount);
            return 0;
        }

        case WM_EXITSIZEMOVE:
            UpdateDockSideFromMovedWindow(hwnd);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) == ID_GMCP_RETRY)
            {
                if (g_model.routeIndex < g_model.route.size())
                    SendCurrentRouteStep();
                return 0;
            }
            if (LOWORD(wParam) == ID_GMCP_CANCEL)
            {
                StopRoute(L"이동 취소");
                return 0;
            }
            break;

        case WM_TIMER:
            if (wParam == ID_GMCP_STEP_TIMER)
            {
                KillTimer(hwnd, ID_GMCP_STEP_TIMER);
                SendCurrentRouteStep();
                return 0;
            }
            if (wParam == ID_GMCP_WAIT_TIMER)
            {
                KillTimer(hwnd, ID_GMCP_WAIT_TIMER);
                if (g_model.routeWaiting)
                {
                    g_model.routeStatus =
                        L"이동 정지: 전투나 이동 실패를 확인한 뒤 다시 시도하십시오.";
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;

        case WM_LBUTTONUP:
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            for (const MapHit& hit : g_mapHits)
            {
                if (!PtInRect(&hit.rc, pt)) continue;
                std::vector<std::wstring> route = FindRouteTo(hit.x, hit.y);
                if (route.empty())
                {
                    g_model.routeStatus =
                        L"현재 방이거나 연결 경로를 계산할 수 없는 방입니다.";
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
                g_model.route = std::move(route);
                g_model.routeIndex = 0;
                g_model.routeWaiting = false;
                g_model.routeStatus = L"클릭 이동 시작";
                SendCurrentRouteStep();
                return 0;
            }
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH back = CreateSolidBrush(BackColor());
            FillRect(hdc, &client, back);
            DeleteObject(back);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, TextColor());
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, MainLogFont()));
            SIZE cell = MainCellSize(hwnd);
            int cellW = std::max(1, static_cast<int>(cell.cx));
            int cellH = std::max(1, static_cast<int>(cell.cy));

            const int headerY = 8;
            std::wstring zone = L"지역명: ";
            zone += g_model.zone.empty() ? L"수신 대기" : g_model.zone;
            TextOutW(hdc, 10, headerY, zone.c_str(), static_cast<int>(zone.size()));

            MapGrid grid = BuildMapGrid();
            const int mapPixelWidth = grid.width * cellW;
            const int originX = std::max(8,
                (static_cast<int>(client.right) - mapPixelWidth) / 2);
            const int originY = headerY + cellH + 4;
            const int scrollPixels = g_mapScrollRow * cellH;
            g_mapHits.clear();

            for (int y = 0; y < grid.height; ++y)
            {
                for (int x = 0; x < grid.width; ++x)
                {
                    wchar_t ch = CellAt(grid, x, y);
                    if (ch == 0 || ch == L' ') continue;
                    int cw = std::max(1, KTinCharWidth(ch));
                    if (cw > 2) cw = 2;
                    int cellLeft = originX + x * cellW;
                    int py = originY + y * cellH - scrollPixels;
                    if (py + cellH <= originY ||
                        py >= static_cast<int>(client.bottom) - MapFooterHeight())
                        continue;

                    int drawX = cellLeft;
                    if (cw == 2)
                    {
                        SIZE glyph{};
                        if (GetTextExtentPoint32W(hdc, &ch, 1, &glyph) &&
                            glyph.cx > 0 && glyph.cx < cw * cellW)
                        {
                            drawX += (cw * cellW - glyph.cx) / 2;
                        }
                    }

                    SetTextColor(hdc, ch == L'▣' ? AccentColor() : TextColor());
                    TextOutW(hdc, drawX, py, &ch, 1);
                    if (IsRoomGlyph(ch))
                    {
                        MapHit hit;
                        hit.rc = { cellLeft - 2, py - 1,
                            cellLeft + cw * cellW + 2, py + cellH };
                        hit.x = x;
                        hit.y = y;
                        g_mapHits.push_back(hit);
                    }
                }
            }

            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            DockGmcpWindowsToMainInternal();
            return 0;

        case WM_DESTROY:
            KillTimer(hwnd, ID_GMCP_STEP_TIMER);
            KillTimer(hwnd, ID_GMCP_WAIT_TIMER);
            if (g_hwndMap == hwnd) g_hwndMap = nullptr;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    LRESULT CALLBACK InfoWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            g_hwndInfoText = CreateWindowExW(WS_EX_CLIENTEDGE,
                MSFTEDIT_CLASS, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_NOHIDESEL,
                10, kInfoTextTop, 300, 200, hwnd,
                reinterpret_cast<HMENU>(ID_GMCP_INFO_TEXT),
                GetModuleHandleW(nullptr), nullptr);
            if (g_hwndInfoText)
            {
                SendMessageW(g_hwndInfoText, WM_SETFONT,
                    reinterpret_cast<WPARAM>(MainLogFont()), TRUE);
                SendMessageW(g_hwndInfoText, EM_SETBKGNDCOLOR, 0, BackColor());
                SendMessageW(g_hwndInfoText, EM_SETREADONLY, TRUE, 0);
                ApplyInfoTextColor();
                UpdateInfoTextControl(false);
            }
            return 0;
        }

        case WM_SIZE:
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            if (g_hwndInfoText && g_infoRawVisible)
                MoveWindow(g_hwndInfoText, 10, kInfoTextTop,
                    std::max(40, width - 20),
                    std::max(40, height - kInfoTextTop - 10), TRUE);

            if (!g_syncingPanelWidth && !g_syncingDockLayout &&
                !g_adjustingInfoLayout)
            {
                RECT rc{};
                GetWindowRect(hwnd, &rc);
                g_infoOuterHeight = rc.bottom - rc.top;
                if (g_infoRawVisible)
                    g_infoExpandedOuterHeight = g_infoOuterHeight;
                SyncPanelWidths(hwnd, rc.right - rc.left);
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
            if (g_hwndInfoText && IsWindow(g_hwndInfoText))
            {
                SendMessageW(g_hwndInfoText, WM_MOUSEWHEEL, wParam, lParam);
                return 0;
            }
            break;

        case WM_EXITSIZEMOVE:
            UpdateDockSideFromMovedWindow(hwnd);
            return 0;

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            HWND edit = reinterpret_cast<HWND>(lParam);
            if (edit == g_hwndInfoText)
            {
                SetTextColor(hdc, TextColor());
                SetBkColor(hdc, BackColor());
                if (!g_infoEditBrush)
                    g_infoEditBrush = CreateSolidBrush(BackColor());
                return reinterpret_cast<LRESULT>(g_infoEditBrush);
            }
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);
            HBRUSH back = CreateSolidBrush(BackColor());
            FillRect(hdc, &client, back);
            DeleteObject(back);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, TextColor());
            HFONT old = static_cast<HFONT>(SelectObject(hdc, MainLogFont()));

            RECT hpRc{ 10, 10, client.right - 10, 36 };
            RECT mpRc{ 10, 44, client.right - 10, 70 };
            DrawProgress(hdc, hpRc, L"체  력",
                g_model.haveHp && g_model.haveMaxHp,
                g_model.hp, g_model.maxHp, RGB(190, 45, 45));
            DrawProgress(hdc, mpRc, L"정신력",
                g_model.haveMp && g_model.haveMaxMp,
                g_model.mp, g_model.maxMp, RGB(45, 100, 210));

            if (!g_infoRawVisible)
            {
                std::wstring waiting = BuildWaitingModuleText();
                if (!waiting.empty())
                {
                    RECT waitRc{ 10, 78, client.right - 10, client.bottom - 6 };
                    SetTextColor(hdc, RGB(170, 170, 170));
                    DrawTextW(hdc, waiting.c_str(), -1, &waitRc,
                        DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
                }
            }

            SelectObject(hdc, old);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            DockGmcpWindowsToMainInternal();
            return 0;

        case WM_DESTROY:
            if (g_hwndInfo == hwnd) g_hwndInfo = nullptr;
            g_hwndInfoText = nullptr;
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    bool RegisterClasses()
    {
        if (g_classesRegistered)
            return true;

        HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW mapClass{};
        mapClass.lpfnWndProc = MapWindowProc;
        mapClass.hInstance = instance;
        mapClass.hCursor = LoadCursorW(nullptr, IDC_HAND);
        mapClass.hbrBackground = nullptr;
        mapClass.lpszClassName = kMapWindowClass;

        WNDCLASSW infoClass = mapClass;
        infoClass.lpfnWndProc = InfoWindowProc;
        infoClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        infoClass.lpszClassName = kInfoWindowClass;

        if (!RegisterClassW(&mapClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;
        if (!RegisterClassW(&infoClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return false;

        g_classesRegistered = true;
        return true;
    }
}

void ObserveTerminalTextForVitals(std::wstring_view text)
{
    if (text.empty())
        return;

    bool found = false;
    double hp = 0.0, maxHp = 0.0, mp = 0.0, maxMp = 0.0;

    {
        std::lock_guard<std::mutex> lock(g_terminalVitalsMutex);
        g_terminalVitalsTail.append(text.data(), text.size());

        // 긴 방 설명 전체를 계속 들고 있지 않고, 분할 수신될 수 있는 프롬프트를
        // 찾기에 충분한 마지막 부분만 유지합니다.
        constexpr size_t kTailLimit = 4096;
        if (g_terminalVitalsTail.size() > kTailLimit)
            g_terminalVitalsTail.erase(0, g_terminalVitalsTail.size() - kTailLimit);

        size_t scan = 0;
        size_t consumed = 0;
        while (scan < g_terminalVitalsTail.size())
        {
            const size_t open = g_terminalVitalsTail.find(L'[', scan);
            if (open == std::wstring::npos)
                break;

            size_t end = 0;
            double nextHp = 0.0, nextMaxHp = 0.0;
            double nextMp = 0.0, nextMaxMp = 0.0;
            if (ParsePlainPromptAt(g_terminalVitalsTail, open, end,
                nextHp, nextMaxHp, nextMp, nextMaxMp))
            {
                hp = nextHp;
                maxHp = nextMaxHp;
                mp = nextMp;
                maxMp = nextMaxMp;
                found = true;
                consumed = end;
                scan = end;
            }
            else
            {
                scan = open + 1;
            }
        }

        if (consumed > 0)
            g_terminalVitalsTail.erase(0, consumed);
    }

    if (found)
        PostTerminalVitalsPacket(hp, maxHp, mp, maxMp);
}

void ClearTerminalTextVitalsState()
{
    std::lock_guard<std::mutex> lock(g_terminalVitalsMutex);
    g_terminalVitalsTail.clear();
}

bool PostGmcpPacketFromOsc(std::string_view encoded)
{
    if (!g_app || !g_app->hwndMain || encoded.empty())
        return false;

    std::string decoded;
    if (!DecodeBase64(encoded, decoded))
        return false;

    size_t split = decoded.find('\n');
    if (split == std::string::npos || split == 0)
        return false;

    std::unique_ptr<GmcpPacket> packet(new (std::nothrow) GmcpPacket());
    if (!packet)
        return false;

    packet->module.assign(decoded.data(), split);
    packet->json.assign(decoded.data() + split + 1, decoded.size() - split - 1);


    GmcpPacket* raw = packet.release();
    if (!PostMessageW(g_app->hwndMain, WM_APP_GMCP_UPDATE, 0, reinterpret_cast<LPARAM>(raw)))
    {
        delete raw;
        return false;
    }
    return true;
}

bool HandleMainGmcpUpdate(HWND hwnd, LPARAM lParam)
{
    (void)hwnd;
    std::unique_ptr<GmcpPacket> packet(reinterpret_cast<GmcpPacket*>(lParam));
    if (!packet)
        return true;

    ApplyPacket(*packet);
    return true;
}

void ShowGmcpMapWindow(HWND owner)
{
    if (!RegisterClasses())
    {
        MessageBoxW(owner, L"GMCP 지도 창을 만들 수 없습니다.",
            L"KTin", MB_OK | MB_ICONERROR);
        return;
    }

    if (!g_hwndMap || !IsWindow(g_hwndMap))
    {
        g_hwndMap = CreateWindowExW(WS_EX_TOOLWINDOW,
            kMapWindowClass, L"KTin GMCP 지도",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VSCROLL,
            CW_USEDEFAULT, CW_USEDEFAULT,
            g_panelOuterWidth, g_mapOuterHeight,
            owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    if (g_hwndMap)
    {
        AutoFitMapWindow();
        ShowWindow(g_hwndMap, SW_SHOWNORMAL);
        ApplyFontsToGmcpWindows();
        DockGmcpWindowsToMainInternal();
        SetForegroundWindow(g_hwndMap);
        InvalidateRect(g_hwndMap, nullptr, FALSE);
    }
}

void ShowGmcpInfoWindow(HWND owner)
{
    if (!RegisterClasses())
    {
        MessageBoxW(owner, L"GMCP 정보 창을 만들 수 없습니다.",
            L"KTin", MB_OK | MB_ICONERROR);
        return;
    }

    if (!g_hwndInfo || !IsWindow(g_hwndInfo))
    {
        g_hwndInfo = CreateWindowExW(WS_EX_TOOLWINDOW,
            kInfoWindowClass, L"KTin GMCP 정보",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT,
            g_panelOuterWidth, g_infoOuterHeight,
            owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    if (g_hwndInfo)
    {
        ShowWindow(g_hwndInfo, SW_SHOWNORMAL);
        ApplyFontsToGmcpWindows();
        UpdateInfoTextControl(false);
        DockGmcpWindowsToMainInternal();
        SetForegroundWindow(g_hwndInfo);
        InvalidateRect(g_hwndInfo, nullptr, FALSE);
    }
}

void RefreshGmcpWindowStyles()
{
    if (g_infoEditBrush)
    {
        DeleteObject(g_infoEditBrush);
        g_infoEditBrush = nullptr;
    }
    ApplyFontsToGmcpWindows();
    if (g_hwndMap && IsWindow(g_hwndMap))
        AutoFitMapWindow();
    UpdateInfoTextControl();
    RefreshWindows();
}

void RefreshGmcpInfoWindowContent()
{
    UpdateInfoTextControl(false);
    if (g_hwndInfo && IsWindow(g_hwndInfo))
        InvalidateRect(g_hwndInfo, nullptr, FALSE);
}

void SyncGmcpWindowsToMain()
{
    DockGmcpWindowsToMainInternal();
}

void LoadGmcpDisplaySettings()
{
    if (!g_app)
        return;

    std::wstring path = GetSettingsPath();
    static const wchar_t* keys[GMCP_DISPLAY_COUNT] =
    {
        L"map", L"vitals", L"cursor", L"char", L"combat", L"party",
        L"room", L"system", L"info", L"chat", L"term", L"other"
    };
    for (int i = 0; i < GMCP_DISPLAY_COUNT; ++i)
    {
        g_app->gmcpDisplayModules[i] =
            GetPrivateProfileIntW(L"gmcp_display", keys[i], 0,
                path.c_str()) != 0;
    }
    // Map 원문은 전용 GMCP 지도창에서 사용하므로 정보창 선택에서 제외합니다.
    g_app->gmcpDisplayModules[GMCP_DISPLAY_MAP] = false;
}

void SaveGmcpDisplaySettings()
{
    if (!g_app)
        return;

    std::wstring path = GetSettingsPath();
    static const wchar_t* keys[GMCP_DISPLAY_COUNT] =
    {
        L"map", L"vitals", L"cursor", L"char", L"combat", L"party",
        L"room", L"system", L"info", L"chat", L"term", L"other"
    };
    g_app->gmcpDisplayModules[GMCP_DISPLAY_MAP] = false;
    for (int i = 0; i < GMCP_DISPLAY_COUNT; ++i)
    {
        WritePrivateProfileStringW(L"gmcp_display", keys[i],
            g_app->gmcpDisplayModules[i] ? L"1" : L"0", path.c_str());
    }
}

void CloseGmcpWindows()
{
    if (g_hwndMap && IsWindow(g_hwndMap)) DestroyWindow(g_hwndMap);
    if (g_hwndInfo && IsWindow(g_hwndInfo)) DestroyWindow(g_hwndInfo);
    g_hwndMap = nullptr;
    g_hwndInfo = nullptr;
    g_hwndInfoText = nullptr;

    if (g_infoEditBrush)
    {
        DeleteObject(g_infoEditBrush);
        g_infoEditBrush = nullptr;
    }

    g_model = GmcpModel{};
    g_mapHits.clear();
    g_dockSide = 1;
    g_panelOuterWidth = kDefaultPanelWidth;
    g_mapOuterHeight = 500;
    g_infoOuterHeight = 360;
    g_infoExpandedOuterHeight = 360;
    g_mapScrollRow = 0;
    g_infoRawVisible = false;
    g_adjustingInfoLayout = false;
}
