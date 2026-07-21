#include "trigger_editor.h"

#include "address_book.h"
#include "constants.h"
#include "main.h"
#include "theme.h"
#include "utils.h"
#include "win_util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <utility>
#include <cwchar>
#include <cwctype>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr wchar_t kEditorClass[] = L"KTinTriggerEditorWindow";
    constexpr wchar_t kNameInputClass[] = L"KTinTriggerNameInputWindow";
    // KTin trigger editor: forced dark mode, obsolete detail-apply button removed.

    constexpr int IDC_TR_PATH = 6200;
    constexpr int IDC_TR_TREE = 6201;
    constexpr int IDC_TR_ADD_FOLDER = 6202;
    constexpr int IDC_TR_ADD_CHILD_FOLDER = 6203;
    constexpr int IDC_TR_ADD_TRIGGER = 6204;
    constexpr int IDC_TR_DELETE = 6205;
    constexpr int IDC_TR_UP = 6206;
    constexpr int IDC_TR_DOWN = 6207;
    constexpr int IDC_TR_DETAIL_TITLE = 6208;
    constexpr int IDC_TR_NAME_LABEL = 6209;
    constexpr int IDC_TR_NAME = 6210;
    constexpr int IDC_TR_ENABLED = 6211;
    constexpr int IDC_TR_CLASS_LABEL = 6212;
    constexpr int IDC_TR_CLASS = 6213;
    constexpr int IDC_TR_PATTERN_LABEL = 6214;
    constexpr int IDC_TR_PATTERN = 6215;
    constexpr int IDC_TR_COMMAND_LABEL = 6216;
    constexpr int IDC_TR_COMMAND = 6217;
    constexpr int IDC_TR_PRIORITY_LABEL = 6218;
    constexpr int IDC_TR_PRIORITY = 6219;
    constexpr int IDC_TR_SAVE = 6221;
    constexpr int IDC_TR_RELOAD = 6222;
    constexpr int IDC_TR_HELP = 6223;
    constexpr int IDC_TR_OPEN = 6224;
    constexpr int IDC_TR_SAVE_AS = 6225;
    constexpr int IDC_TR_ASSIGN_ADDRESS = 6226;
    constexpr int IDC_TR_ADDRESS_STATUS = 6227;

    constexpr int IDC_NAME_PROMPT = 6250;

    constexpr wchar_t kBlockBegin[] = L"#NOP {KTIN_TRIGGER_EDITOR_BEGIN|1}";
    constexpr wchar_t kBlockEnd[] = L"#NOP {KTIN_TRIGGER_EDITOR_END}";
    constexpr wchar_t kRuntimeBegin[] = L"#NOP {KTIN_TRIGGER_RUNTIME_BEGIN}";
    constexpr wchar_t kRuntimeEnd[] = L"#NOP {KTIN_TRIGGER_RUNTIME_END}";

    struct TriggerFolder
    {
        std::wstring id;
        std::wstring parentId;
        std::wstring name;
        bool enabled = true;
    };

    struct TriggerRule
    {
        std::wstring id;
        std::wstring folderId;
        std::wstring name;
        std::wstring pattern;
        std::wstring command;
        bool enabled = true;
        double priority = 5.0;
    };

    struct TriggerModel
    {
        std::wstring storedPath;
        std::wstring absolutePath;
        std::wstring prefix;
        std::wstring suffix;
        std::vector<TriggerFolder> folders;
        std::vector<TriggerRule> triggers;
        // 이전 저장본에서 사용한 Class ID도 보존합니다. 폴더를 삭제한 뒤
        // 같은 세션에서 다시 읽을 때 삭제된 Class를 먼저 kill하지 않으면
        // 그 폴더의 옛 트리거가 접속 종료 전까지 계속 남기 때문입니다.
        std::set<std::wstring> knownClassIds;
        bool dirty = false;
    };

    struct TriggerEditorState
    {
        TriggerModel model;
        std::wstring selectedId;
        bool selectedIsFolder = true;
        bool rebuildingTree = false;
        bool updatingControls = false;
        int addressIndex = -1;
        COLORREF panelBack = RGB(32, 32, 32);
        COLORREF panelText = RGB(240, 240, 240);
        COLORREF editBack = RGB(20, 20, 20);
        COLORREF editText = RGB(240, 240, 240);
        HBRUSH panelBrush = nullptr;
        HBRUSH editBrush = nullptr;
    };

    struct TreeNodeData
    {
        bool isFolder = true;
        std::wstring id;
    };

    struct NamePromptState
    {
        std::wstring title;
        std::wstring label;
        std::wstring value;
        bool accepted = false;
        COLORREF panelBack = RGB(32, 32, 32);
        COLORREF panelText = RGB(240, 240, 240);
        COLORREF editBack = RGB(20, 20, 20);
        COLORREF editText = RGB(240, 240, 240);
        HBRUSH panelBrush = nullptr;
        HBRUSH editBrush = nullptr;
    };

    static std::uint64_t g_idCounter = 0;

    static TriggerEditorState* GetState(HWND hwnd)
    {
        return reinterpret_cast<TriggerEditorState*>(GetPropW(hwnd, L"KTinTriggerEditorState"));
    }

    static int ColorChannel(int value)
    {
        return std::max(0, std::min(255, value));
    }

    static COLORREF ShiftColor(COLORREF color, int amount)
    {
        return RGB(
            ColorChannel(static_cast<int>(GetRValue(color)) + amount),
            ColorChannel(static_cast<int>(GetGValue(color)) + amount),
            ColorChannel(static_cast<int>(GetBValue(color)) + amount));
    }

    static bool IsDarkColor(COLORREF color)
    {
        const int luminance = static_cast<int>(GetRValue(color)) * 299 +
            static_cast<int>(GetGValue(color)) * 587 +
            static_cast<int>(GetBValue(color)) * 114;
        return luminance < 128000;
    }

    static void ApplyForcedDarkTitleBar(HWND hwnd)
    {
        if (!hwnd)
            return;

        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        HMODULE dwmApi = LoadLibraryW(L"dwmapi.dll");
        if (!dwmApi)
            return;

        auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
            GetProcAddress(dwmApi, "DwmSetWindowAttribute"));
        if (setWindowAttribute)
        {
            const BOOL enabled = TRUE;
            // Windows 10 20H1 이후는 20, 이전 빌드는 19를 사용합니다.
            if (FAILED(setWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))))
                setWindowAttribute(hwnd, 19, &enabled, sizeof(enabled));
        }
        FreeLibrary(dwmApi);
    }

    static void DestroyEditorBrushes(TriggerEditorState* state)
    {
        if (!state)
            return;
        if (state->panelBrush)
        {
            DeleteObject(state->panelBrush);
            state->panelBrush = nullptr;
        }
        if (state->editBrush)
        {
            DeleteObject(state->editBrush);
            state->editBrush = nullptr;
        }
    }

    static void InitializeEditorTheme(HWND hwnd, TriggerEditorState* state)
    {
        if (!state)
            return;

        DestroyEditorBrushes(state);
        // 트리거 편집기는 메인 ANSI 테마와 관계없이 항상 다크 모드로 표시합니다.
        state->panelBack = RGB(32, 32, 32);
        state->panelText = RGB(240, 240, 240);
        state->editBack = RGB(20, 20, 20);
        state->editText = RGB(240, 240, 240);
        state->panelBrush = CreateSolidBrush(state->panelBack);
        state->editBrush = CreateSolidBrush(state->editBack);

        HWND tree = GetDlgItem(hwnd, IDC_TR_TREE);
        if (tree)
        {
            TreeView_SetBkColor(tree, state->editBack);
            TreeView_SetTextColor(tree, state->editText);
            TreeView_SetLineColor(tree, ShiftColor(state->editText, -80));
        }

        SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
        EnumChildWindows(hwnd, [](HWND child, LPARAM) -> BOOL {
            wchar_t className[32] = {};
            GetClassNameW(child, className, static_cast<int>(std::size(className)));

            if (_wcsicmp(className, L"Button") == 0)
            {
                const LONG_PTR style = GetWindowLongPtrW(child, GWL_STYLE);
                const LONG_PTR type = style & BS_TYPEMASK;
                if (type == BS_PUSHBUTTON || type == BS_DEFPUSHBUTTON)
                {
                    SetWindowLongPtrW(child, GWL_STYLE, (style & ~BS_TYPEMASK) | BS_OWNERDRAW);
                    SetWindowPos(child, nullptr, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
                }
            }

            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
            return TRUE;
        }, 0);

        ApplyForcedDarkTitleBar(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    static bool DrawEditorButton(const DRAWITEMSTRUCT* draw, const TriggerEditorState* state)
    {
        if (!draw || !state || draw->CtlType != ODT_BUTTON)
            return false;

        COLORREF background = ShiftColor(state->panelBack, IsDarkColor(state->panelBack) ? 18 : -12);
        COLORREF border = ShiftColor(state->panelBack, IsDarkColor(state->panelBack) ? 55 : -55);
        COLORREF text = state->panelText;
        if ((draw->itemState & ODS_SELECTED) != 0)
            background = ShiftColor(background, IsDarkColor(background) ? 18 : -18);
        if ((draw->itemState & ODS_DISABLED) != 0)
            text = ShiftColor(state->panelText, IsDarkColor(state->panelText) ? -90 : 90);

        HBRUSH backgroundBrush = CreateSolidBrush(background);
        HBRUSH borderBrush = CreateSolidBrush(border);
        if (backgroundBrush)
        {
            FillRect(draw->hDC, &draw->rcItem, backgroundBrush);
            DeleteObject(backgroundBrush);
        }
        if (borderBrush)
        {
            FrameRect(draw->hDC, &draw->rcItem, borderBrush);
            DeleteObject(borderBrush);
        }

        wchar_t label[256] = {};
        GetWindowTextW(draw->hwndItem, label, static_cast<int>(std::size(label)));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, text);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(draw->hDC, font) : nullptr;
        RECT textRect = draw->rcItem;
        DrawTextW(draw->hDC, label, -1, &textRect,
            DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (oldFont)
            SelectObject(draw->hDC, oldFont);
        if ((draw->itemState & ODS_FOCUS) != 0)
        {
            RECT focus = draw->rcItem;
            InflateRect(&focus, -3, -3);
            DrawFocusRect(draw->hDC, &focus);
        }
        return true;
    }

    static std::wstring NormalizeNewlines(const std::wstring& text)
    {
        std::wstring out;
        out.reserve(text.size() + 16);
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == L'\r')
            {
                out += L"\r\n";
                if (i + 1 < text.size() && text[i + 1] == L'\n')
                    ++i;
            }
            else if (text[i] == L'\n')
            {
                out += L"\r\n";
            }
            else
            {
                out.push_back(text[i]);
            }
        }
        return out;
    }

    static size_t FindLineMarker(const std::wstring& text, const std::wstring& marker, size_t start = 0)
    {
        size_t pos = start;
        while ((pos = text.find(marker, pos)) != std::wstring::npos)
        {
            const bool atLineStart = pos == 0 ||
                (pos >= 2 && text[pos - 2] == L'\r' && text[pos - 1] == L'\n');
            const size_t after = pos + marker.size();
            const bool atLineEnd = after == text.size() ||
                (after + 1 < text.size() && text[after] == L'\r' && text[after + 1] == L'\n');
            if (atLineStart && atLineEnd)
                return pos;
            ++pos;
        }
        return std::wstring::npos;
    }

    static std::vector<std::wstring> Split(const std::wstring& text, wchar_t delimiter)
    {
        std::vector<std::wstring> result;
        size_t start = 0;
        while (start <= text.size())
        {
            size_t pos = text.find(delimiter, start);
            if (pos == std::wstring::npos)
            {
                result.push_back(text.substr(start));
                break;
            }
            result.push_back(text.substr(start, pos - start));
            start = pos + 1;
        }
        return result;
    }

    static std::wstring HexEncode(const std::wstring& text)
    {
        static const wchar_t* digits = L"0123456789ABCDEF";
        const std::string utf8 = WideToUtf8(text);
        std::wstring out;
        out.reserve(utf8.size() * 2);
        for (unsigned char ch : utf8)
        {
            out.push_back(digits[(ch >> 4) & 0x0F]);
            out.push_back(digits[ch & 0x0F]);
        }
        return out;
    }

    static int HexDigit(wchar_t ch)
    {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    }

    static bool HexDecode(const std::wstring& encoded, std::wstring& out)
    {
        if ((encoded.size() % 2) != 0)
            return false;

        std::string bytes;
        bytes.reserve(encoded.size() / 2);
        for (size_t i = 0; i < encoded.size(); i += 2)
        {
            const int hi = HexDigit(encoded[i]);
            const int lo = HexDigit(encoded[i + 1]);
            if (hi < 0 || lo < 0)
                return false;
            bytes.push_back(static_cast<char>((hi << 4) | lo));
        }
        out = Utf8ToWide(bytes);
        return true;
    }

    static bool IsSafeId(const std::wstring& id)
    {
        if (id.size() != 16)
            return false;
        for (wchar_t ch : id)
        {
            const bool hex = (ch >= L'0' && ch <= L'9') ||
                (ch >= L'A' && ch <= L'F') ||
                (ch >= L'a' && ch <= L'f');
            if (!hex)
                return false;
        }
        return true;
    }

    static std::wstring NewId()
    {
        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        const std::uint64_t tick = (static_cast<std::uint64_t>(ft.dwHighDateTime) << 32) |
            static_cast<std::uint64_t>(ft.dwLowDateTime);
        const std::uint64_t value = tick ^ (++g_idCounter * 0x9E3779B97F4A7C15ULL);

        std::wostringstream ss;
        ss << std::hex << std::uppercase << std::setw(16) << std::setfill(L'0') << value;
        return ss.str();
    }

    static bool HasBalancedBraces(const std::wstring& text)
    {
        int depth = 0;
        size_t backslashRun = 0;
        for (wchar_t ch : text)
        {
            if (ch == L'\\')
            {
                ++backslashRun;
                continue;
            }

            const bool escaped = (backslashRun % 2) != 0;
            backslashRun = 0;
            if (escaped)
                continue;

            if (ch == L'{')
                ++depth;
            else if (ch == L'}')
            {
                if (--depth < 0)
                    return false;
            }
        }
        return depth == 0;
    }

    static bool IsAbsoluteWindowsPath(const std::wstring& path)
    {
        return path.size() >= 2 &&
            ((iswalpha(path[0]) && path[1] == L':') ||
             (path[0] == L'\\' && path[1] == L'\\'));
    }

    static std::wstring ResolveAbsoluteScriptPath(const std::wstring& storedPath)
    {
        if (IsAbsoluteWindowsPath(storedPath))
        {
            wchar_t full[MAX_PATH] = {};
            const DWORD length = GetFullPathNameW(storedPath.c_str(), MAX_PATH, full, nullptr);
            if (length > 0 && length < MAX_PATH)
                return full;
            return storedPath;
        }
        return MakeAbsolutePath(GetModuleDirectory(), storedPath);
    }

    static std::wstring NormalizePathForCompare(std::wstring path)
    {
        std::replace(path.begin(), path.end(), L'/', L'\\');
        while (path.size() > 3 && path.back() == L'\\')
            path.pop_back();
        return path;
    }

    static std::wstring MakeStoredScriptPath(const std::wstring& selectedPath)
    {
        const std::wstring absolute = NormalizePathForCompare(ResolveAbsoluteScriptPath(selectedPath));
        std::wstring module = NormalizePathForCompare(GetModuleDirectory());
        if (!module.empty() && module.back() != L'\\')
            module.push_back(L'\\');

        if (absolute.size() >= module.size() &&
            _wcsnicmp(absolute.c_str(), module.c_str(), module.size()) == 0)
        {
            return absolute.substr(module.size());
        }
        return absolute;
    }

    static bool PromptTriggerFile(HWND owner, bool saveDialog,
        const std::wstring& currentPath, std::wstring& storedPath)
    {
        std::vector<wchar_t> fileBuffer(32768, L'\0');
        const std::wstring currentAbsolute = ResolveAbsoluteScriptPath(currentPath);
        if (currentAbsolute.size() + 1 < fileBuffer.size())
            wcsncpy_s(fileBuffer.data(), fileBuffer.size(), currentAbsolute.c_str(), _TRUNCATE);

        wchar_t initialDirectory[MAX_PATH] = {};
        const std::wstring moduleDirectory = GetModuleDirectory();
        if (moduleDirectory.size() + 1 < std::size(initialDirectory))
            wcsncpy_s(initialDirectory, moduleDirectory.c_str(), _TRUNCATE);

        const wchar_t filter[] =
            L"TinTin++ 스크립트 (*.tin)\0*.tin\0"
            L"텍스트 파일 (*.txt)\0*.txt\0"
            L"모든 파일 (*.*)\0*.*\0\0";

        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = filter;
        ofn.nFilterIndex = 1;
        ofn.lpstrFile = fileBuffer.data();
        ofn.nMaxFile = static_cast<DWORD>(fileBuffer.size());
        ofn.lpstrInitialDir = initialDirectory;
        ofn.lpstrDefExt = L"tin";
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        if (saveDialog)
            ofn.Flags |= OFN_OVERWRITEPROMPT;
        else
            ofn.Flags |= OFN_FILEMUSTEXIST;

        const BOOL accepted = saveDialog ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
        if (!accepted)
            return false;

        storedPath = MakeStoredScriptPath(fileBuffer.data());
        return !Trim(storedPath).empty();
    }

    static bool EnsureParentDirectory(const std::wstring& absolutePath)
    {
        const size_t slash = absolutePath.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return true;

        const std::wstring dir = absolutePath.substr(0, slash);
        if (dir.empty() || (dir.size() == 2 && dir[1] == L':'))
            return true;

        const int result = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
        return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
    }

    static bool ReadAllBytes(const std::wstring& path, std::string& bytes)
    {
        bytes.clear();
        UniqueHandle file(CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file.IsValid())
            return false;

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.Get(), &size) || size.QuadPart < 0 || size.QuadPart > 64LL * 1024LL * 1024LL)
            return false;

        bytes.resize(static_cast<size_t>(size.QuadPart));
        size_t offset = 0;
        while (offset < bytes.size())
        {
            DWORD read = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - offset, 1024 * 1024));
            if (!ReadFile(file.Get(), &bytes[offset], chunk, &read, nullptr))
                return false;
            if (read == 0)
                break;
            offset += read;
        }
        bytes.resize(offset);
        return true;
    }

    static std::wstring DecodeScriptBytes(const std::string& bytes)
    {
        if (bytes.size() >= 3 &&
            static_cast<unsigned char>(bytes[0]) == 0xEF &&
            static_cast<unsigned char>(bytes[1]) == 0xBB &&
            static_cast<unsigned char>(bytes[2]) == 0xBF)
        {
            return MultiByteToWide(bytes.substr(3), CP_UTF8);
        }

        if (!bytes.empty())
        {
            const int test = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
            if (test > 0)
                return MultiByteToWide(bytes, CP_UTF8);
        }

        return MultiByteToWide(bytes, 949);
    }

    static TriggerFolder* FindFolder(TriggerModel& model, const std::wstring& id)
    {
        for (auto& folder : model.folders)
            if (folder.id == id)
                return &folder;
        return nullptr;
    }

    static const TriggerFolder* FindFolder(const TriggerModel& model, const std::wstring& id)
    {
        for (const auto& folder : model.folders)
            if (folder.id == id)
                return &folder;
        return nullptr;
    }

    static TriggerRule* FindTrigger(TriggerModel& model, const std::wstring& id)
    {
        for (auto& trigger : model.triggers)
            if (trigger.id == id)
                return &trigger;
        return nullptr;
    }

    static const TriggerRule* FindTrigger(const TriggerModel& model, const std::wstring& id)
    {
        for (const auto& trigger : model.triggers)
            if (trigger.id == id)
                return &trigger;
        return nullptr;
    }

    static std::wstring ClassNameForFolder(const std::wstring& id)
    {
        return L"ktin_c_" + id;
    }

    static bool IsFolderEffectivelyEnabled(const TriggerModel& model, const TriggerFolder& folder)
    {
        if (!folder.enabled)
            return false;

        std::set<std::wstring> seen;
        const TriggerFolder* current = &folder;
        while (current && !current->parentId.empty())
        {
            if (!seen.insert(current->id).second)
                return false;
            current = FindFolder(model, current->parentId);
            if (!current || !current->enabled)
                return false;
        }
        return true;
    }

    static void EnsureModelIntegrity(TriggerModel& model)
    {
        std::set<std::wstring> ids;
        for (auto& folder : model.folders)
        {
            if (!IsSafeId(folder.id) || !ids.insert(folder.id).second)
                folder.id = NewId();
            if (Trim(folder.name).empty())
                folder.name = L"새 폴더";
        }

        if (model.folders.empty())
        {
            TriggerFolder folder;
            folder.id = NewId();
            folder.name = L"기본";
            model.folders.push_back(folder);
        }

        const std::wstring fallbackFolder = model.folders.front().id;
        for (auto& folder : model.folders)
        {
            if (!folder.parentId.empty() && !FindFolder(model, folder.parentId))
                folder.parentId.clear();

            std::set<std::wstring> chain;
            const TriggerFolder* current = &folder;
            while (current && !current->parentId.empty())
            {
                if (!chain.insert(current->id).second)
                {
                    folder.parentId.clear();
                    break;
                }
                current = FindFolder(model, current->parentId);
            }
        }

        ids.clear();
        for (auto& trigger : model.triggers)
        {
            if (!IsSafeId(trigger.id) || !ids.insert(trigger.id).second)
                trigger.id = NewId();
            if (!FindFolder(model, trigger.folderId))
                trigger.folderId = fallbackFolder;
            if (Trim(trigger.name).empty())
                trigger.name = L"새 트리거";
            if (!std::isfinite(trigger.priority) || trigger.priority < 1.0 || trigger.priority > 9.0)
                trigger.priority = 5.0;
        }
    }

    static bool ParseMetadataLine(const std::wstring& line, TriggerModel& model)
    {
        const std::wstring folderPrefix = L"#NOP {KTIN_FOLDER|";
        const std::wstring triggerPrefix = L"#NOP {KTIN_TRIGGER|";
        const std::wstring knownClassPrefix = L"#NOP {KTIN_KNOWN_CLASS|";

        if (line.rfind(knownClassPrefix, 0) == 0 && !line.empty() && line.back() == L'}')
        {
            const std::wstring id = line.substr(knownClassPrefix.size(),
                line.size() - knownClassPrefix.size() - 1);
            if (IsSafeId(id))
                model.knownClassIds.insert(id);
            return IsSafeId(id);
        }

        if (line.rfind(folderPrefix, 0) == 0 && !line.empty() && line.back() == L'}')
        {
            const std::wstring payload = line.substr(folderPrefix.size(), line.size() - folderPrefix.size() - 1);
            const auto fields = Split(payload, L'|');
            if (fields.size() != 4)
                return false;

            TriggerFolder folder;
            folder.id = fields[0];
            folder.parentId = fields[1];
            folder.enabled = fields[2] != L"0";
            if (!HexDecode(fields[3], folder.name))
                return false;
            if (IsSafeId(folder.id))
                model.knownClassIds.insert(folder.id);
            model.folders.push_back(folder);
            return true;
        }

        if (line.rfind(triggerPrefix, 0) == 0 && !line.empty() && line.back() == L'}')
        {
            const std::wstring payload = line.substr(triggerPrefix.size(), line.size() - triggerPrefix.size() - 1);
            const auto fields = Split(payload, L'|');
            if (fields.size() != 7)
                return false;

            TriggerRule trigger;
            trigger.id = fields[0];
            trigger.folderId = fields[1];
            trigger.enabled = fields[2] != L"0";
            trigger.priority = _wtof(fields[3].c_str());
            if (!HexDecode(fields[4], trigger.name) ||
                !HexDecode(fields[5], trigger.pattern) ||
                !HexDecode(fields[6], trigger.command))
            {
                return false;
            }
            model.triggers.push_back(trigger);
            return true;
        }

        return false;
    }

    static bool LoadTriggerModel(const std::wstring& storedPath, TriggerModel& model)
    {
        model = TriggerModel{};
        model.storedPath = storedPath;
        model.absolutePath = ResolveAbsoluteScriptPath(storedPath);

        std::string bytes;
        std::wstring text;
        if (ReadAllBytes(model.absolutePath, bytes))
            text = NormalizeNewlines(DecodeScriptBytes(bytes));

        const size_t beginPos = FindLineMarker(text, kBlockBegin);
        const size_t firstEndPos = FindLineMarker(text, kBlockEnd);
        const size_t endPos = (beginPos == std::wstring::npos)
            ? std::wstring::npos
            : FindLineMarker(text, kBlockEnd, beginPos + wcslen(kBlockBegin));

        // 관리 구역 표식이 한쪽만 남은 파일에 새 관리 구역을 덧붙이면,
        // 다음 로드에서 옛 시작 표식과 새 끝 표식이 잘못 한 쌍으로 잡힐 수 있습니다.
        // 이런 경우에는 원본을 건드리지 않고 편집기 열기를 중단합니다.
        if ((beginPos == std::wstring::npos) != (firstEndPos == std::wstring::npos) ||
            (beginPos != std::wstring::npos && endPos == std::wstring::npos))
        {
            return false;
        }

        if (beginPos == std::wstring::npos)
        {
            model.prefix = text;
            if (!model.prefix.empty() &&
                (model.prefix.size() < 2 || model.prefix.substr(model.prefix.size() - 2) != L"\r\n"))
            {
                model.prefix += L"\r\n";
            }
            model.suffix.clear();
        }
        else
        {
            model.prefix = text.substr(0, beginPos);
            size_t endLine = text.find(L"\r\n", endPos);
            if (endLine == std::wstring::npos)
                model.suffix.clear();
            else
                model.suffix = text.substr(endLine + 2);

            size_t cursor = beginPos;
            while (cursor < endPos)
            {
                size_t lineEnd = text.find(L"\r\n", cursor);
                if (lineEnd == std::wstring::npos || lineEnd > endPos)
                    lineEnd = endPos;
                ParseMetadataLine(text.substr(cursor, lineEnd - cursor), model);
                cursor = lineEnd + 2;
            }
        }

        EnsureModelIntegrity(model);
        for (const auto& folder : model.folders)
            model.knownClassIds.insert(folder.id);
        return true;
    }

    static void AppendFolderRuntime(std::wostringstream& out,
        const TriggerModel& model,
        const TriggerFolder& folder,
        std::set<std::wstring>& visiting)
    {
        if (!IsFolderEffectivelyEnabled(model, folder))
            return;
        if (!visiting.insert(folder.id).second)
            return;

        const std::wstring className = ClassNameForFolder(folder.id);
        out << L"#class {" << className << L"} {open}\r\n";

        for (const auto& trigger : model.triggers)
        {
            if (trigger.folderId != folder.id || !trigger.enabled)
                continue;

            std::wostringstream priority;
            priority << std::fixed << std::setprecision(2) << trigger.priority;
            std::wstring p = priority.str();
            while (!p.empty() && p.back() == L'0') p.pop_back();
            if (!p.empty() && p.back() == L'.') p.pop_back();
            if (p.empty()) p = L"5";

            out << L"#action {" << trigger.pattern << L"} {" << trigger.command << L"} {" << p << L"}\r\n";
        }

        for (const auto& child : model.folders)
        {
            if (child.parentId == folder.id)
                AppendFolderRuntime(out, model, child, visiting);
        }

        out << L"#class {" << className << L"} {close}\r\n";
        visiting.erase(folder.id);
    }

    static std::wstring BuildManagedBlock(const TriggerModel& model)
    {
        std::wostringstream out;
        out << kBlockBegin << L"\r\n";
        out << L"#NOP {이 구역은 KTin 트리거 편집기가 관리합니다. 직접 수정하면 다음 저장 때 다시 작성됩니다.}\r\n";

        std::set<std::wstring> cleanupClassIds = model.knownClassIds;
        for (const auto& folder : model.folders)
            cleanupClassIds.insert(folder.id);

        for (const auto& classId : cleanupClassIds)
            out << L"#NOP {KTIN_KNOWN_CLASS|" << classId << L"}\r\n";

        for (const auto& folder : model.folders)
        {
            out << L"#NOP {KTIN_FOLDER|" << folder.id << L"|" << folder.parentId << L"|"
                << (folder.enabled ? L"1" : L"0") << L"|" << HexEncode(folder.name) << L"}\r\n";
        }

        for (const auto& trigger : model.triggers)
        {
            std::wostringstream priority;
            priority << std::fixed << std::setprecision(2) << trigger.priority;
            out << L"#NOP {KTIN_TRIGGER|" << trigger.id << L"|" << trigger.folderId << L"|"
                << (trigger.enabled ? L"1" : L"0") << L"|" << priority.str() << L"|"
                << HexEncode(trigger.name) << L"|" << HexEncode(trigger.pattern) << L"|"
                << HexEncode(trigger.command) << L"}\r\n";
        }

        out << kRuntimeBegin << L"\r\n";
        for (const auto& classId : cleanupClassIds)
            out << L"#class {" << ClassNameForFolder(classId) << L"} {kill}\r\n";

        std::set<std::wstring> visiting;
        for (const auto& folder : model.folders)
        {
            if (folder.parentId.empty())
                AppendFolderRuntime(out, model, folder, visiting);
        }
        out << kRuntimeEnd << L"\r\n";
        out << kBlockEnd << L"\r\n";
        return out.str();
    }

    static bool ValidateModel(HWND hwnd, const TriggerModel& model)
    {
        for (const auto& folder : model.folders)
        {
            if (Trim(folder.name).empty())
            {
                ShowCenteredMessageBox(hwnd, L"이름이 비어 있는 폴더가 있습니다.", L"트리거 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
        }

        for (const auto& trigger : model.triggers)
        {
            if (Trim(trigger.name).empty())
            {
                ShowCenteredMessageBox(hwnd, L"이름이 비어 있는 트리거가 있습니다.", L"트리거 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (Trim(trigger.pattern).empty())
            {
                std::wstring msg = L"'" + trigger.name + L"' 트리거의 인식 패턴이 비어 있습니다.";
                ShowCenteredMessageBox(hwnd, msg.c_str(), L"트리거 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (Trim(trigger.command).empty())
            {
                std::wstring msg = L"'" + trigger.name + L"' 트리거의 실행 명령이 비어 있습니다.";
                ShowCenteredMessageBox(hwnd, msg.c_str(), L"트리거 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (!HasBalancedBraces(trigger.pattern))
            {
                std::wstring msg = L"'" + trigger.name + L"' 트리거의 인식 패턴에 짝이 맞지 않는 { }가 있습니다.";
                ShowCenteredMessageBox(hwnd, msg.c_str(), L"트리거 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (!HasBalancedBraces(trigger.command))
            {
                std::wstring msg = L"'" + trigger.name + L"' 트리거의 실행 명령에 짝이 맞지 않는 { }가 있습니다.";
                ShowCenteredMessageBox(hwnd, msg.c_str(), L"트리거 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
        }
        return true;
    }

    static bool SaveTriggerModel(HWND hwnd, TriggerModel& model)
    {
        EnsureModelIntegrity(model);
        if (!ValidateModel(hwnd, model))
            return false;

        if (!EnsureParentDirectory(model.absolutePath))
        {
            ShowCenteredMessageBox(hwnd, L"트리거 파일 폴더를 만들지 못했습니다.", L"트리거 편집", MB_OK | MB_ICONERROR);
            return false;
        }

        const std::wstring text = model.prefix + BuildManagedBlock(model) + model.suffix;
        const std::wstring tempPath = model.absolutePath + L".tmp";
        const std::wstring backupPath = model.absolutePath + L".bak";

        if (!WriteUtf8NoBomTextFile(tempPath, WideToUtf8(text)))
        {
            DeleteFileW(tempPath.c_str());
            ShowCenteredMessageBox(hwnd, L"임시 트리거 파일을 저장하지 못했습니다.", L"트리거 편집", MB_OK | MB_ICONERROR);
            return false;
        }

        if (GetFileAttributesW(model.absolutePath.c_str()) != INVALID_FILE_ATTRIBUTES)
            CopyFileW(model.absolutePath.c_str(), backupPath.c_str(), FALSE);

        if (!MoveFileExW(tempPath.c_str(), model.absolutePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tempPath.c_str());
            ShowCenteredMessageBox(hwnd, L"트리거 파일을 교체하지 못했습니다. 다른 프로그램에서 파일을 사용 중인지 확인하세요.",
                L"트리거 편집", MB_OK | MB_ICONERROR);
            return false;
        }

        for (const auto& folder : model.folders)
            model.knownClassIds.insert(folder.id);
        model.dirty = false;
        return true;
    }

    static bool SameAddress(const AddressBookEntry& a, const AddressBookEntry& b)
    {
        return _wcsicmp(Trim(a.name).c_str(), Trim(b.name).c_str()) == 0 &&
            _wcsicmp(Trim(a.host).c_str(), Trim(b.host).c_str()) == 0 &&
            a.port == b.port;
    }

    static int FindCurrentAddressIndex()
    {
        if (!g_app)
            return -1;

        const AddressBookEntry* current = nullptr;
        if (g_app->hasPendingConnect)
            current = &g_app->pendingConnectEntry;
        else if (g_app->hasActiveSession)
            current = &g_app->activeSession;

        if (!current)
            return -1;

        for (size_t i = 0; i < g_app->addressBook.size(); ++i)
        {
            if (SameAddress(g_app->addressBook[i], *current))
                return static_cast<int>(i);
        }
        return -1;
    }

    static void SyncAddressScriptPath(int index, const std::wstring& storedPath)
    {
        if (!g_app || index < 0 || index >= static_cast<int>(g_app->addressBook.size()))
            return;

        AddressBookEntry& entry = g_app->addressBook[index];
        entry.scriptPath = Trim(storedPath);
        SaveAddressBook();

        if (g_app->hasActiveSession && SameAddress(g_app->activeSession, entry))
            g_app->activeSession.scriptPath = entry.scriptPath;
        if (g_app->hasPendingConnect && SameAddress(g_app->pendingConnectEntry, entry))
            g_app->pendingConnectEntry.scriptPath = entry.scriptPath;
    }

    static std::wstring ResolveEditorTargetPath()
    {
        constexpr wchar_t kDefaultTriggerFile[] = L"ktin_triggers.tin";
        if (!g_app)
            return kDefaultTriggerFile;

        const int index = FindCurrentAddressIndex();
        if (index < 0 || index >= static_cast<int>(g_app->addressBook.size()))
            return kDefaultTriggerFile;

        AddressBookEntry& entry = g_app->addressBook[index];
        if (Trim(entry.scriptPath).empty())
        {
            // 주소록에 별도 파일이 없으면 임의의 ktin_c_* 파일을 만들지 않습니다.
            // 공통 기본 파일을 주소록에도 기록하여 다음 주소록 접속부터
            // TinTin++ #session의 스크립트 인수로 같은 파일이 자동 로드됩니다.
            SyncAddressScriptPath(index, kDefaultTriggerFile);
        }
        return Trim(g_app->addressBook[index].scriptPath);
    }

    static void UpdateEditorPathControls(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return;

        SetWindowTextW(GetDlgItem(hwnd, IDC_TR_PATH), state->model.storedPath.c_str());

        std::wstring status;
        if (g_app && state->addressIndex >= 0 &&
            state->addressIndex < static_cast<int>(g_app->addressBook.size()))
        {
            const AddressBookEntry& entry = g_app->addressBook[state->addressIndex];
            status = L"현재 주소록: " + entry.name + L"  /  접속 시 읽을 파일: ";
            const std::wstring assigned = Trim(entry.scriptPath);
            status += assigned.empty() ? L"(지정 안 됨)" : assigned;
            EnableWindow(GetDlgItem(hwnd, IDC_TR_ASSIGN_ADDRESS), TRUE);
        }
        else
        {
            status = L"주소록으로 접속한 상태가 아닙니다. 현재 파일은 직접 편집할 수 있습니다.";
            EnableWindow(GetDlgItem(hwnd, IDC_TR_ASSIGN_ADDRESS), FALSE);
        }
        SetWindowTextW(GetDlgItem(hwnd, IDC_TR_ADDRESS_STATUS), status.c_str());
    }

    static bool IsTriggerEffectivelyEnabled(const TriggerModel& model, const TriggerRule& trigger)
    {
        if (!trigger.enabled)
            return false;

        const TriggerFolder* folder = FindFolder(model, trigger.folderId);
        return folder && IsFolderEffectivelyEnabled(model, *folder);
    }

    static std::wstring TreeLabel(const TriggerModel& model, const TriggerFolder& folder)
    {
        return std::wstring(IsFolderEffectivelyEnabled(model, folder)
            ? L"[켬] [폴더] " : L"[끔] [폴더] ") + folder.name;
    }

    static std::wstring TreeLabel(const TriggerModel& model, const TriggerRule& trigger)
    {
        return std::wstring(IsTriggerEffectivelyEnabled(model, trigger)
            ? L"[켬] " : L"[끔] ") + trigger.name;
    }

    static HTREEITEM InsertTreeNode(HWND tree, HTREEITEM parent, const std::wstring& text, bool isFolder, const std::wstring& id)
    {
        auto* data = new (std::nothrow) TreeNodeData();
        if (!data)
            return nullptr;
        data->isFolder = isFolder;
        data->id = id;

        TVINSERTSTRUCTW insert{};
        insert.hParent = parent;
        insert.hInsertAfter = TVI_LAST;
        insert.item.mask = TVIF_TEXT | TVIF_PARAM;
        insert.item.pszText = const_cast<wchar_t*>(text.c_str());
        insert.item.lParam = reinterpret_cast<LPARAM>(data);
        HTREEITEM item = TreeView_InsertItem(tree, &insert);
        if (!item)
            delete data;
        return item;
    }

    static HTREEITEM FindTreeItemById(HWND tree, HTREEITEM item, const std::wstring& id, bool isFolder)
    {
        while (item)
        {
            TVITEMW tv{};
            tv.mask = TVIF_PARAM;
            tv.hItem = item;
            if (TreeView_GetItem(tree, &tv))
            {
                auto* data = reinterpret_cast<TreeNodeData*>(tv.lParam);
                if (data && data->isFolder == isFolder && data->id == id)
                    return item;
            }

            HTREEITEM child = TreeView_GetChild(tree, item);
            if (child)
            {
                HTREEITEM found = FindTreeItemById(tree, child, id, isFolder);
                if (found)
                    return found;
            }
            item = TreeView_GetNextSibling(tree, item);
        }
        return nullptr;
    }

    static void InsertFolderTree(HWND tree, TriggerEditorState& state, const TriggerFolder& folder, HTREEITEM parent)
    {
        HTREEITEM item = InsertTreeNode(tree, parent, TreeLabel(state.model, folder), true, folder.id);
        if (!item)
            return;

        for (const auto& child : state.model.folders)
        {
            if (child.parentId == folder.id)
                InsertFolderTree(tree, state, child, item);
        }

        for (const auto& trigger : state.model.triggers)
        {
            if (trigger.folderId == folder.id)
                InsertTreeNode(tree, item, TreeLabel(state.model, trigger), false, trigger.id);
        }

        TreeView_Expand(tree, item, TVE_EXPAND);
    }

    static void RebuildTree(HWND hwnd, bool preserveSelection)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return;

        HWND tree = GetDlgItem(hwnd, IDC_TR_TREE);
        if (!tree)
            return;

        const std::wstring selectedId = preserveSelection ? state->selectedId : L"";
        const bool selectedFolder = state->selectedIsFolder;

        state->rebuildingTree = true;
        TreeView_DeleteAllItems(tree);

        for (const auto& folder : state->model.folders)
        {
            if (folder.parentId.empty())
                InsertFolderTree(tree, *state, folder, TVI_ROOT);
        }

        HTREEITEM selectItem = nullptr;
        if (!selectedId.empty())
            selectItem = FindTreeItemById(tree, TreeView_GetRoot(tree), selectedId, selectedFolder);
        if (!selectItem)
            selectItem = TreeView_GetRoot(tree);
        if (selectItem)
            TreeView_SelectItem(tree, selectItem);

        state->rebuildingTree = false;
    }

    static void SetControlVisible(HWND hwnd, int id, bool visible)
    {
        HWND control = GetDlgItem(hwnd, id);
        if (control)
            ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }

    static void UpdateDetails(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return;

        state->updatingControls = true;
        const bool folderSelected = state->selectedIsFolder && FindFolder(state->model, state->selectedId);
        const bool triggerSelected = !state->selectedIsFolder && FindTrigger(state->model, state->selectedId);
        const bool hasSelection = folderSelected || triggerSelected;

        EnableWindow(GetDlgItem(hwnd, IDC_TR_NAME), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_TR_ENABLED), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_TR_DELETE), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_TR_UP), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_TR_DOWN), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_TR_ADD_CHILD_FOLDER), folderSelected);
        EnableWindow(GetDlgItem(hwnd, IDC_TR_ADD_TRIGGER), hasSelection);

        SetControlVisible(hwnd, IDC_TR_PATTERN_LABEL, triggerSelected);
        SetControlVisible(hwnd, IDC_TR_PATTERN, triggerSelected);
        SetControlVisible(hwnd, IDC_TR_COMMAND_LABEL, triggerSelected);
        SetControlVisible(hwnd, IDC_TR_COMMAND, triggerSelected);
        SetControlVisible(hwnd, IDC_TR_PRIORITY_LABEL, triggerSelected);
        SetControlVisible(hwnd, IDC_TR_PRIORITY, triggerSelected);

        if (!hasSelection)
        {
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_DETAIL_TITLE), L"항목을 선택하세요");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_NAME), L"");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_CLASS), L"");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_PATTERN), L"");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_COMMAND), L"");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_PRIORITY), L"5");
            state->updatingControls = false;
            return;
        }

        if (folderSelected)
        {
            TriggerFolder* folder = FindFolder(state->model, state->selectedId);
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_DETAIL_TITLE), L"폴더 상세 편집");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_NAME), folder->name.c_str());
            SendMessageW(GetDlgItem(hwnd, IDC_TR_ENABLED), BM_SETCHECK, folder->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_CLASS), ClassNameForFolder(folder->id).c_str());
        }
        else
        {
            TriggerRule* trigger = FindTrigger(state->model, state->selectedId);
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_DETAIL_TITLE), L"트리거 상세 편집");
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_NAME), trigger->name.c_str());
            SendMessageW(GetDlgItem(hwnd, IDC_TR_ENABLED), BM_SETCHECK, trigger->enabled ? BST_CHECKED : BST_UNCHECKED, 0);

            const TriggerFolder* folder = FindFolder(state->model, trigger->folderId);
            std::wstring classDisplay = folder
                ? folder->name + L"  (" + ClassNameForFolder(folder->id) + L")"
                : L"";
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_CLASS), classDisplay.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_PATTERN), trigger->pattern.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_COMMAND), trigger->command.c_str());
            std::wostringstream priority;
            priority << std::fixed << std::setprecision(2) << trigger->priority;
            std::wstring p = priority.str();
            while (!p.empty() && p.back() == L'0') p.pop_back();
            if (!p.empty() && p.back() == L'.') p.pop_back();
            SetWindowTextW(GetDlgItem(hwnd, IDC_TR_PRIORITY), p.c_str());
        }

        state->updatingControls = false;
    }

    static bool SyncDetails(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state || state->updatingControls || state->selectedId.empty())
            return true;

        bool changed = false;
        const std::wstring name = Trim(GetWindowTextString(GetDlgItem(hwnd, IDC_TR_NAME)));
        const bool enabled = SendMessageW(GetDlgItem(hwnd, IDC_TR_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;

        if (state->selectedIsFolder)
        {
            TriggerFolder* folder = FindFolder(state->model, state->selectedId);
            if (!folder)
                return true;
            if (!name.empty() && folder->name != name) { folder->name = name; changed = true; }
            if (folder->enabled != enabled) { folder->enabled = enabled; changed = true; }
        }
        else
        {
            TriggerRule* trigger = FindTrigger(state->model, state->selectedId);
            if (!trigger)
                return true;
            const std::wstring pattern = GetWindowTextString(GetDlgItem(hwnd, IDC_TR_PATTERN));
            const std::wstring command = GetWindowTextString(GetDlgItem(hwnd, IDC_TR_COMMAND));
            double priority = _wtof(GetWindowTextString(GetDlgItem(hwnd, IDC_TR_PRIORITY)).c_str());
            if (!std::isfinite(priority)) priority = trigger->priority;
            if (priority < 1.0) priority = 1.0;
            if (priority > 9.0) priority = 9.0;

            if (!name.empty() && trigger->name != name) { trigger->name = name; changed = true; }
            if (trigger->enabled != enabled) { trigger->enabled = enabled; changed = true; }
            if (trigger->pattern != pattern) { trigger->pattern = pattern; changed = true; }
            if (trigger->command != command) { trigger->command = command; changed = true; }
            if (trigger->priority != priority) { trigger->priority = priority; changed = true; }
        }

        if (changed)
            state->model.dirty = true;
        return true;
    }

    static bool PromptName(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& value);

    static std::wstring SelectedFolderId(const TriggerEditorState& state)
    {
        if (state.selectedIsFolder)
        {
            if (FindFolder(state.model, state.selectedId))
                return state.selectedId;
        }
        else
        {
            const TriggerRule* trigger = FindTrigger(state.model, state.selectedId);
            if (trigger)
                return trigger->folderId;
        }

        return state.model.folders.empty() ? L"" : state.model.folders.front().id;
    }

    static void AddFolder(HWND hwnd, bool child)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);

        std::wstring name = L"새 폴더";
        if (!PromptName(hwnd, child ? L"하위 폴더 추가" : L"폴더 추가", L"폴더 이름:", name))
            return;

        TriggerFolder folder;
        folder.id = NewId();
        folder.name = Trim(name);
        if (folder.name.empty())
            return;

        if (child)
        {
            folder.parentId = SelectedFolderId(*state);
        }
        else if (state->selectedIsFolder)
        {
            const TriggerFolder* selected = FindFolder(state->model, state->selectedId);
            if (selected)
                folder.parentId = selected->parentId;
        }
        else
        {
            const TriggerRule* selected = FindTrigger(state->model, state->selectedId);
            const TriggerFolder* parent = selected ? FindFolder(state->model, selected->folderId) : nullptr;
            if (parent)
                folder.parentId = parent->parentId;
        }

        state->model.folders.push_back(folder);
        state->selectedId = folder.id;
        state->selectedIsFolder = true;
        state->model.dirty = true;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
    }

    static void AddTrigger(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);

        const std::wstring folderId = SelectedFolderId(*state);
        if (folderId.empty())
            return;

        std::wstring name = L"새 트리거";
        if (!PromptName(hwnd, L"트리거 추가", L"트리거 이름:", name))
            return;

        TriggerRule trigger;
        trigger.id = NewId();
        trigger.folderId = folderId;
        trigger.name = Trim(name);
        trigger.pattern = L"수신할 문자열";
        trigger.command = L"실행할 명령";
        state->model.triggers.push_back(trigger);
        state->selectedId = trigger.id;
        state->selectedIsFolder = false;
        state->model.dirty = true;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_TR_PATTERN));
        SendMessageW(GetDlgItem(hwnd, IDC_TR_PATTERN), EM_SETSEL, 0, -1);
    }


    static void DeleteSelected(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state || state->selectedId.empty())
            return;
        SyncDetails(hwnd);

        if (state->selectedIsFolder)
        {
            const TriggerFolder* folder = FindFolder(state->model, state->selectedId);
            if (!folder)
                return;

            std::wstring message = L"'" + folder->name + L"' 폴더와 그 안의 모든 하위 폴더/트리거를 삭제하시겠습니까?";
            if (ShowCenteredMessageBox(hwnd, message.c_str(), L"트리거 편집", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;

            const std::wstring deletedId = folder->id;
            std::set<std::wstring> deletedFolderIds;
            deletedFolderIds.insert(deletedId);
            bool added = true;
            while (added)
            {
                added = false;
                for (const auto& candidate : state->model.folders)
                {
                    if (!candidate.parentId.empty() &&
                        deletedFolderIds.count(candidate.parentId) != 0 &&
                        deletedFolderIds.insert(candidate.id).second)
                    {
                        added = true;
                    }
                }
            }

            state->model.triggers.erase(
                std::remove_if(state->model.triggers.begin(), state->model.triggers.end(),
                    [&](const TriggerRule& t)
                    {
                        return deletedFolderIds.count(t.folderId) != 0;
                    }),
                state->model.triggers.end());

            state->model.folders.erase(
                std::remove_if(state->model.folders.begin(), state->model.folders.end(),
                    [&](const TriggerFolder& f)
                    {
                        return deletedFolderIds.count(f.id) != 0;
                    }),
                state->model.folders.end());

            EnsureModelIntegrity(state->model);
            state->selectedId = state->model.folders.front().id;
            state->selectedIsFolder = true;
        }
        else
        {
            const TriggerRule* trigger = FindTrigger(state->model, state->selectedId);
            if (!trigger)
                return;
            std::wstring message = L"'" + trigger->name + L"' 트리거를 삭제하시겠습니까?";
            if (ShowCenteredMessageBox(hwnd, message.c_str(), L"트리거 편집", MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;

            const std::wstring folderId = trigger->folderId;
            state->model.triggers.erase(
                std::remove_if(state->model.triggers.begin(), state->model.triggers.end(),
                    [&](const TriggerRule& t) { return t.id == state->selectedId; }),
                state->model.triggers.end());
            state->selectedId = folderId;
            state->selectedIsFolder = true;
        }

        state->model.dirty = true;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
    }

    template <typename T, typename ParentFn>
    static bool MoveWithinSiblings(std::vector<T>& items, size_t index, int direction, ParentFn parentOf)
    {
        if (index >= items.size())
            return false;
        const std::wstring parent = parentOf(items[index]);
        if (direction < 0)
        {
            for (size_t i = index; i > 0; --i)
            {
                if (parentOf(items[i - 1]) == parent)
                {
                    std::swap(items[index], items[i - 1]);
                    return true;
                }
            }
        }
        else
        {
            for (size_t i = index + 1; i < items.size(); ++i)
            {
                if (parentOf(items[i]) == parent)
                {
                    std::swap(items[index], items[i]);
                    return true;
                }
            }
        }
        return false;
    }

    static void MoveSelected(HWND hwnd, int direction)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state || state->selectedId.empty())
            return;
        SyncDetails(hwnd);

        bool moved = false;
        if (state->selectedIsFolder)
        {
            for (size_t i = 0; i < state->model.folders.size(); ++i)
            {
                if (state->model.folders[i].id == state->selectedId)
                {
                    moved = MoveWithinSiblings(state->model.folders, i, direction,
                        [](const TriggerFolder& f) { return f.parentId; });
                    break;
                }
            }
        }
        else
        {
            for (size_t i = 0; i < state->model.triggers.size(); ++i)
            {
                if (state->model.triggers[i].id == state->selectedId)
                {
                    moved = MoveWithinSiblings(state->model.triggers, i, direction,
                        [](const TriggerRule& t) { return t.folderId; });
                    break;
                }
            }
        }

        if (moved)
        {
            state->model.dirty = true;
            RebuildTree(hwnd, true);
        }
        else
        {
            MessageBeep(MB_ICONINFORMATION);
        }
    }

    static bool SaveChangesBeforeFileSwitch(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return false;

        SyncDetails(hwnd);
        if (!state->model.dirty)
            return true;

        const int result = ShowCenteredMessageBox(hwnd,
            L"현재 파일에 저장하지 않은 변경 내용이 있습니다. 먼저 저장하시겠습니까?",
            L"트리거 파일 변경", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (result == IDCANCEL)
            return false;
        if (result == IDYES)
            return SaveTriggerModel(hwnd, state->model);
        return true;
    }

    static bool ReplaceEditorModel(HWND hwnd, const std::wstring& storedPath, bool createIfMissing)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return false;

        TriggerModel loaded;
        if (!LoadTriggerModel(storedPath, loaded))
        {
            ShowCenteredMessageBox(hwnd,
                L"선택한 파일의 KTin 관리 구역 시작/끝 표식이 맞지 않습니다.\n"
                L"원본 파일은 수정하지 않았습니다.",
                L"트리거 파일 불러오기", MB_OK | MB_ICONERROR);
            return false;
        }

        if (GetFileAttributesW(loaded.absolutePath.c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            if (!createIfMissing)
            {
                ShowCenteredMessageBox(hwnd, L"선택한 트리거 파일을 찾을 수 없습니다.",
                    L"트리거 파일 불러오기", MB_OK | MB_ICONERROR);
                return false;
            }
            loaded.dirty = true;
            if (!SaveTriggerModel(hwnd, loaded))
                return false;
        }

        HWND tree = GetDlgItem(hwnd, IDC_TR_TREE);
        if (tree)
            TreeView_DeleteAllItems(tree);

        state->model = std::move(loaded);
        state->selectedId = state->model.folders.front().id;
        state->selectedIsFolder = true;
        UpdateEditorPathControls(hwnd);
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
        SetFocus(GetDlgItem(hwnd, IDC_TR_TREE));
        return true;
    }

    static void OpenTriggerFile(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state || !SaveChangesBeforeFileSwitch(hwnd))
            return;

        std::wstring selected;
        if (!PromptTriggerFile(hwnd, false, state->model.storedPath, selected))
            return;

        ReplaceEditorModel(hwnd, selected, false);
    }

    static void SaveTriggerFileAs(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);

        std::wstring selected;
        if (!PromptTriggerFile(hwnd, true, state->model.storedPath, selected))
            return;

        const std::wstring oldStoredPath = state->model.storedPath;
        const std::wstring oldAbsolutePath = state->model.absolutePath;
        state->model.storedPath = selected;
        state->model.absolutePath = ResolveAbsoluteScriptPath(selected);
        state->model.dirty = true;
        if (!SaveTriggerModel(hwnd, state->model))
        {
            state->model.storedPath = oldStoredPath;
            state->model.absolutePath = oldAbsolutePath;
            state->model.dirty = true;
            UpdateEditorPathControls(hwnd);
            return;
        }

        UpdateEditorPathControls(hwnd);
        ShowCenteredMessageBox(hwnd,
            L"새 파일로 저장했습니다. 현재 주소록에서 이 파일을 자동으로 읽게 하려면\n"
            L"'주소록에 현재 파일 지정'을 누르세요.",
            L"트리거 다른 이름으로 저장", MB_OK | MB_ICONINFORMATION);
    }

    static void AssignCurrentFileToAddress(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state || !g_app || state->addressIndex < 0 ||
            state->addressIndex >= static_cast<int>(g_app->addressBook.size()))
        {
            ShowCenteredMessageBox(hwnd,
                L"현재 주소록으로 접속한 상태가 아니어서 지정할 주소록 항목이 없습니다.",
                L"주소록 스크립트 지정", MB_OK | MB_ICONINFORMATION);
            return;
        }

        SyncDetails(hwnd);
        if (state->model.dirty && !SaveTriggerModel(hwnd, state->model))
            return;

        SyncAddressScriptPath(state->addressIndex, state->model.storedPath);
        UpdateEditorPathControls(hwnd);
        ShowCenteredMessageBox(hwnd,
            L"현재 트리거 파일을 주소록의 스크립트로 지정했습니다.\n"
            L"다음 주소록 접속부터 TinTin++가 연결 성공 후 이 파일을 읽습니다.",
            L"주소록 스크립트 지정", MB_OK | MB_ICONINFORMATION);
    }

    static bool SaveAndMaybeReload(HWND hwnd, bool reload)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return false;
        SyncDetails(hwnd);
        if (!SaveTriggerModel(hwnd, state->model))
            return false;

        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);

        bool reloaded = false;
        if (reload && g_app && g_app->proc.stdinWrite)
        {
            SendRawCommandToMud(L"#read {" + state->model.storedPath + L"}");
            reloaded = true;
        }

        const wchar_t* message = L"트리거 파일을 저장했습니다.";
        if (reload)
        {
            message = reloaded
                ? L"트리거 파일을 저장하고 TinTin++에서 다시 읽었습니다."
                : L"트리거 파일은 저장했지만 TinTin++ 백엔드가 실행 중이 아니어서 다시 읽지는 못했습니다.";
        }
        ShowCenteredMessageBox(hwnd, message, L"트리거 편집", MB_OK | MB_ICONINFORMATION);
        return true;
    }

    static bool ConfirmClose(HWND hwnd)
    {
        TriggerEditorState* state = GetState(hwnd);
        if (!state)
            return true;
        SyncDetails(hwnd);
        if (!state->model.dirty)
            return true;

        const int result = ShowCenteredMessageBox(hwnd,
            L"저장하지 않은 변경 내용이 있습니다. 저장하고 닫으시겠습니까?",
            L"트리거 편집", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (result == IDCANCEL)
            return false;
        if (result == IDYES)
            return SaveAndMaybeReload(hwnd, true);
        return true;
    }

    static LRESULT CALLBACK NameInputProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        NamePromptState* state = reinterpret_cast<NamePromptState*>(GetPropW(hwnd, L"KTinNamePromptState"));
        switch (msg)
        {
        case WM_NCCREATE:
        {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            if (create && create->lpCreateParams)
            {
                state = reinterpret_cast<NamePromptState*>(create->lpCreateParams);
                SetPropW(hwnd, L"KTinNamePromptState", state);
            }
            return TRUE;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK)
            {
                if (state)
                {
                    state->value = Trim(GetWindowTextString(GetDlgItem(hwnd, IDC_NAME_PROMPT)));
                    if (state->value.empty())
                    {
                        MessageBeep(MB_ICONWARNING);
                        SetFocus(GetDlgItem(hwnd, IDC_NAME_PROMPT));
                        return 0;
                    }
                    state->accepted = true;
                }
                DestroyWindow(hwnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL)
            {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_ERASEBKGND:
            if (state && state->panelBrush)
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->panelBrush);
                return 1;
            }
            break;
        case WM_DESTROY:
            if (state)
            {
                if (state->panelBrush)
                {
                    DeleteObject(state->panelBrush);
                    state->panelBrush = nullptr;
                }
                if (state->editBrush)
                {
                    DeleteObject(state->editBrush);
                    state->editBrush = nullptr;
                }
            }
            RemovePropW(hwnd, L"KTinNamePromptState");
            return 0;
        case WM_CTLCOLOREDIT:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->editText);
                SetBkColor(dc, state->editBack);
                return reinterpret_cast<INT_PTR>(state->editBrush);
            }
            break;
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->panelText);
                return reinterpret_cast<INT_PTR>(state->panelBrush);
            }
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static bool PromptName(HWND owner, const wchar_t* title, const wchar_t* label, std::wstring& value)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = NameInputProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kNameInputClass;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            RegisterClassW(&wc);
            registered = true;
        }

        NamePromptState state;
        state.title = title;
        state.label = label;
        state.value = value;
        state.panelBack = RGB(32, 32, 32);
        state.panelText = RGB(240, 240, 240);
        state.editBack = RGB(20, 20, 20);
        state.editText = RGB(240, 240, 240);
        state.panelBrush = CreateSolidBrush(state.panelBack);
        state.editBrush = CreateSolidBrush(state.editBack);

        HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kNameInputClass, title,
            WS_POPUP | WS_CAPTION | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 430, 155,
            owner, nullptr, GetModuleHandleW(nullptr), &state);
        if (!dialog)
        {
            if (state.panelBrush) DeleteObject(state.panelBrush);
            if (state.editBrush) DeleteObject(state.editBrush);
            return false;
        }

        CreateWindowExW(0, L"STATIC", label, WS_CHILD | WS_VISIBLE,
            16, 18, 380, 20, dialog, nullptr, nullptr, nullptr);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", value.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            16, 42, 388, 25, dialog, reinterpret_cast<HMENU>(IDC_NAME_PROMPT), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            226, 82, 82, 30, dialog, reinterpret_cast<HMENU>(IDOK), nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            318, 82, 82, 30, dialog, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

        HFONT font = GetPopupUIFont(dialog);
        EnumChildWindows(dialog, [](HWND child, LPARAM f) -> BOOL {
            SendMessageW(child, WM_SETFONT, f, TRUE);
            return TRUE;
        }, reinterpret_cast<LPARAM>(font));
        SetWindowTheme(dialog, L"DarkMode_Explorer", nullptr);
        EnumChildWindows(dialog, [](HWND child, LPARAM) -> BOOL {
            SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
            return TRUE;
        }, 0);
        ApplyForcedDarkTitleBar(dialog);
        InvalidateRect(dialog, nullptr, TRUE);

        RECT ownerRect{}, dialogRect{};
        GetWindowRect(owner, &ownerRect);
        GetWindowRect(dialog, &dialogRect);
        const int width = dialogRect.right - dialogRect.left;
        const int height = dialogRect.bottom - dialogRect.top;
        const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
        const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
        SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);

        EnableWindow(owner, FALSE);
        SetFocus(edit);
        SendMessageW(edit, EM_SETSEL, 0, -1);

        MSG msg{};
        while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0))
        {
            if (!IsDialogMessageW(dialog, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        SetForegroundWindow(owner);

        if (state.accepted)
            value = state.value;
        return state.accepted;
    }

    static LRESULT CALLBACK TriggerEditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        TriggerEditorState* state = GetState(hwnd);
        switch (msg)
        {
        case WM_NCCREATE:
        {
            CREATESTRUCTW* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            if (create && create->lpCreateParams)
            {
                state = reinterpret_cast<TriggerEditorState*>(create->lpCreateParams);
                SetPropW(hwnd, L"KTinTriggerEditorState", state);
            }
            return TRUE;
        }

        case WM_CREATE:
        {
            CreateWindowExW(0, L"STATIC", L"편집 파일:", WS_CHILD | WS_VISIBLE,
                16, 15, 75, 20, hwnd, nullptr, nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                92, 11, 565, 25, hwnd, reinterpret_cast<HMENU>(IDC_TR_PATH), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"파일 불러오기...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                668, 10, 112, 28, hwnd, reinterpret_cast<HMENU>(IDC_TR_OPEN), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"다른 이름 저장...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                788, 10, 120, 28, hwnd, reinterpret_cast<HMENU>(IDC_TR_SAVE_AS), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"주소록에 현재 파일 지정", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                916, 10, 146, 28, hwnd, reinterpret_cast<HMENU>(IDC_TR_ASSIGN_ADDRESS), nullptr, nullptr);
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP,
                92, 43, 970, 20, hwnd, reinterpret_cast<HMENU>(IDC_TR_ADDRESS_STATUS), nullptr, nullptr);

            CreateWindowExW(0, L"STATIC", L"폴더와 트리거", WS_CHILD | WS_VISIBLE,
                16, 70, 180, 20, hwnd, nullptr, nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES |
                TVS_LINESATROOT | TVS_SHOWSELALWAYS,
                16, 92, 350, 450, hwnd, reinterpret_cast<HMENU>(IDC_TR_TREE), nullptr, nullptr);

            CreateWindowExW(0, L"BUTTON", L"폴더 추가", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                16, 552, 82, 29, hwnd, reinterpret_cast<HMENU>(IDC_TR_ADD_FOLDER), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"하위 폴더", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                104, 552, 82, 29, hwnd, reinterpret_cast<HMENU>(IDC_TR_ADD_CHILD_FOLDER), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"트리거 추가", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                192, 552, 86, 29, hwnd, reinterpret_cast<HMENU>(IDC_TR_ADD_TRIGGER), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"삭제", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                284, 552, 82, 29, hwnd, reinterpret_cast<HMENU>(IDC_TR_DELETE), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"위로", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                192, 587, 82, 29, hwnd, reinterpret_cast<HMENU>(IDC_TR_UP), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"아래로", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                284, 587, 82, 29, hwnd, reinterpret_cast<HMENU>(IDC_TR_DOWN), nullptr, nullptr);

            // BS_GROUPBOX는 일부 Windows 테마에서 내부를 흰색으로 칠하므로
            // 제목은 일반 STATIC으로 두고 테두리는 부모 창 WM_PAINT에서 그립니다.
            CreateWindowExW(0, L"STATIC", L"항목 상세 편집", WS_CHILD | WS_VISIBLE,
                406, 70, 180, 20, hwnd, nullptr, nullptr, nullptr);
            CreateWindowExW(0, L"STATIC", L"항목을 선택하세요", WS_CHILD | WS_VISIBLE,
                406, 94, 350, 22, hwnd, reinterpret_cast<HMENU>(IDC_TR_DETAIL_TITLE), nullptr, nullptr);
            CreateWindowExW(0, L"STATIC", L"이름:", WS_CHILD | WS_VISIBLE,
                406, 130, 70, 20, hwnd, reinterpret_cast<HMENU>(IDC_TR_NAME_LABEL), nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                482, 126, 545, 25, hwnd, reinterpret_cast<HMENU>(IDC_TR_NAME), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"사용", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                482, 159, 90, 24, hwnd, reinterpret_cast<HMENU>(IDC_TR_ENABLED), nullptr, nullptr);

            CreateWindowExW(0, L"STATIC", L"내부 Class(파일 아님):", WS_CHILD | WS_VISIBLE,
                406, 195, 168, 20, hwnd, reinterpret_cast<HMENU>(IDC_TR_CLASS_LABEL), nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                578, 191, 449, 25, hwnd, reinterpret_cast<HMENU>(IDC_TR_CLASS), nullptr, nullptr);

            CreateWindowExW(0, L"STATIC", L"인식 패턴:", WS_CHILD | WS_VISIBLE,
                406, 236, 85, 20, hwnd, reinterpret_cast<HMENU>(IDC_TR_PATTERN_LABEL), nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                406, 260, 621, 25, hwnd, reinterpret_cast<HMENU>(IDC_TR_PATTERN), nullptr, nullptr);

            CreateWindowExW(0, L"STATIC", L"실행 명령:", WS_CHILD | WS_VISIBLE,
                406, 300, 85, 20, hwnd, reinterpret_cast<HMENU>(IDC_TR_COMMAND_LABEL), nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN,
                406, 324, 621, 105, hwnd, reinterpret_cast<HMENU>(IDC_TR_COMMAND), nullptr, nullptr);

            CreateWindowExW(0, L"STATIC", L"우선순위(1~9):", WS_CHILD | WS_VISIBLE,
                406, 442, 115, 20, hwnd, reinterpret_cast<HMENU>(IDC_TR_PRIORITY_LABEL), nullptr, nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"5",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                526, 438, 80, 25, hwnd, reinterpret_cast<HMENU>(IDC_TR_PRIORITY), nullptr, nullptr);

            CreateWindowExW(0, L"STATIC",
                L"내부 Class는 파일명이 아니라 폴더용 TinTin++ 식별자입니다.\n"
                L"폴더를 끄면 하위 항목도 즉시 [끔]으로 표시되고 실행되지 않습니다.\n"
                L"KTin 관리 구역 밖의 직접 작성 스크립트는 그대로 보존됩니다.",
                WS_CHILD | WS_VISIBLE,
                406, 470, 621, 66, hwnd, reinterpret_cast<HMENU>(IDC_TR_HELP), nullptr, nullptr);

            CreateWindowExW(0, L"BUTTON", L"저장", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                752, 587, 90, 34, hwnd, reinterpret_cast<HMENU>(IDC_TR_SAVE), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"저장 후 다시 읽기", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                850, 587, 134, 34, hwnd, reinterpret_cast<HMENU>(IDC_TR_RELOAD), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"닫기", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                992, 587, 70, 34, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);

            HFONT font = GetPopupUIFont(hwnd);
            EnumChildWindows(hwnd, [](HWND child, LPARAM f) -> BOOL {
                SendMessageW(child, WM_SETFONT, f, TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(font));

            InitializeEditorTheme(hwnd, state);
            UpdateEditorPathControls(hwnd);
            return 0;
        }

        case WM_NOTIFY:
        {
            LPNMHDR header = reinterpret_cast<LPNMHDR>(lParam);
            if (!header || header->idFrom != IDC_TR_TREE || !state)
                break;

            if (header->code == TVN_DELETEITEMW)
            {
                NMTREEVIEWW* tv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                delete reinterpret_cast<TreeNodeData*>(tv->itemOld.lParam);
                return 0;
            }

            if (state->rebuildingTree)
                return 0;

            if (header->code == TVN_SELCHANGINGW)
            {
                SyncDetails(hwnd);
                return 0;
            }

            if (header->code == TVN_SELCHANGEDW)
            {
                NMTREEVIEWW* tv = reinterpret_cast<NMTREEVIEWW*>(lParam);
                auto* data = reinterpret_cast<TreeNodeData*>(tv->itemNew.lParam);
                if (data)
                {
                    state->selectedId = data->id;
                    state->selectedIsFolder = data->isFolder;
                }
                UpdateDetails(hwnd);
                return 0;
            }
            break;
        }

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
            case IDC_TR_OPEN:
                OpenTriggerFile(hwnd);
                return 0;
            case IDC_TR_SAVE_AS:
                SaveTriggerFileAs(hwnd);
                return 0;
            case IDC_TR_ASSIGN_ADDRESS:
                AssignCurrentFileToAddress(hwnd);
                return 0;
            case IDC_TR_ADD_FOLDER:
                AddFolder(hwnd, false);
                return 0;
            case IDC_TR_ADD_CHILD_FOLDER:
                AddFolder(hwnd, true);
                return 0;
            case IDC_TR_ADD_TRIGGER:
                AddTrigger(hwnd);
                return 0;
            case IDC_TR_DELETE:
                DeleteSelected(hwnd);
                return 0;
            case IDC_TR_UP:
                MoveSelected(hwnd, -1);
                return 0;
            case IDC_TR_DOWN:
                MoveSelected(hwnd, 1);
                return 0;
            case IDC_TR_ENABLED:
                if (HIWORD(wParam) == BN_CLICKED && state && !state->updatingControls)
                {
                    // 체크 직후 모델과 트리 표시를 바로 맞춥니다. 폴더를 끄면
                    // 하위 폴더와 트리거도 실제 실행 상태 기준으로 [끔] 표시됩니다.
                    SyncDetails(hwnd);
                    RebuildTree(hwnd, true);
                    UpdateDetails(hwnd);
                }
                return 0;
            case IDC_TR_SAVE:
                SaveAndMaybeReload(hwnd, false);
                return 0;
            case IDC_TR_RELOAD:
                SaveAndMaybeReload(hwnd, true);
                return 0;
            case IDCANCEL:
                if (ConfirmClose(hwnd))
                    DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_CLOSE:
            if (ConfirmClose(hwnd))
                DestroyWindow(hwnd);
            return 0;

        case WM_PAINT:
            if (state)
            {
                PAINTSTRUCT paint{};
                HDC dc = BeginPaint(hwnd, &paint);
                if (state->panelBrush)
                    FillRect(dc, &paint.rcPaint, state->panelBrush);

                const COLORREF borderColor = ShiftColor(state->panelBack,
                    IsDarkColor(state->panelBack) ? 55 : -55);
                HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
                if (pen)
                {
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    Rectangle(dc, 388, 80, 1062, 542);
                    SelectObject(dc, oldBrush);
                    SelectObject(dc, oldPen);
                    DeleteObject(pen);
                }
                EndPaint(hwnd, &paint);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            if (state && state->panelBrush)
            {
                RECT client{};
                GetClientRect(hwnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->panelBrush);
                return 1;
            }
            break;

        case WM_DRAWITEM:
            if (DrawEditorButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), state))
                return TRUE;
            break;

        case WM_DESTROY:
        {
            HWND tree = GetDlgItem(hwnd, IDC_TR_TREE);
            if (tree)
                TreeView_DeleteAllItems(tree);
            DestroyEditorBrushes(state);
            RemovePropW(hwnd, L"KTinTriggerEditorState");
            return 0;
        }

        case WM_CTLCOLOREDIT:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->editText);
                SetBkColor(dc, state->editBack);
                return reinterpret_cast<INT_PTR>(state->editBrush);
            }
            break;

        case WM_CTLCOLORBTN:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->panelText);
                SetBkColor(dc, state->panelBack);
                return reinterpret_cast<INT_PTR>(state->panelBrush);
            }
            break;

        case WM_CTLCOLORSTATIC:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                HWND control = reinterpret_cast<HWND>(lParam);
                wchar_t className[16] = {};
                if (control)
                    GetClassNameW(control, className, static_cast<int>(std::size(className)));
                if (_wcsicmp(className, L"Edit") == 0)
                {
                    SetTextColor(dc, state->editText);
                    SetBkColor(dc, state->editBack);
                    return reinterpret_cast<INT_PTR>(state->editBrush);
                }

                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->panelText);
                return reinterpret_cast<INT_PTR>(state->panelBrush);
            }
            break;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void PromptTriggerEditor(HWND owner)
{
    if (!owner || !IsWindow(owner))
        return;

    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = TriggerEditorProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kEditorClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        registered = true;
    }

    TriggerEditorState state;
    state.addressIndex = FindCurrentAddressIndex();
    const std::wstring storedPath = ResolveEditorTargetPath();
    if (!LoadTriggerModel(storedPath, state.model))
    {
        ShowCenteredMessageBox(owner,
            L"트리거 파일의 KTin 관리 구역 시작/끝 표식이 맞지 않습니다.\n"
            L"원본 파일은 수정하지 않았습니다. 파일의 KTIN_TRIGGER_EDITOR_BEGIN과\n"
            L"KTIN_TRIGGER_EDITOR_END 표식을 확인한 뒤 다시 여세요.",
            L"트리거 편집", MB_OK | MB_ICONERROR);
        return;
    }
    if (GetFileAttributesW(state.model.absolutePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        state.model.dirty = true;
        if (!SaveTriggerModel(owner, state.model))
            return;
    }
    state.selectedId = state.model.folders.front().id;
    state.selectedIsFolder = true;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kEditorClass, L"TinTin++ 트리거 편집",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1090, 680,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog)
        return;

    UpdateEditorPathControls(dialog);
    RebuildTree(dialog, true);
    UpdateDetails(dialog);

    ApplyForcedDarkTitleBar(dialog);

    RECT ownerRect{}, dialogRect{};
    GetWindowRect(owner, &ownerRect);
    GetWindowRect(dialog, &dialogRect);
    const int width = dialogRect.right - dialogRect.left;
    const int height = dialogRect.bottom - dialogRect.top;
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{ sizeof(mi) };
    if (GetMonitorInfoW(monitor, &mi))
    {
        const int workLeft = static_cast<int>(mi.rcWork.left);
        const int workTop = static_cast<int>(mi.rcWork.top);
        const int workRight = static_cast<int>(mi.rcWork.right);
        const int workBottom = static_cast<int>(mi.rcWork.bottom);
        x = std::max(workLeft, std::min(x, workRight - width));
        y = std::max(workTop, std::min(y, workBottom - height));
    }

    SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(owner, FALSE);
    SetFocus(GetDlgItem(dialog, IDC_TR_TREE));

    MSG msg{};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0))
    {
        if (!IsDialogMessageW(dialog, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetForegroundWindow(owner);
}


void LoadDefaultTriggerScriptIfPresent()
{
    if (!g_app || !g_app->proc.stdinWrite)
        return;

    const std::wstring storedPath = L"ktin_triggers.tin";
    const std::wstring absolutePath = ResolveAbsoluteScriptPath(storedPath);
    const DWORD attributes = GetFileAttributesW(absolutePath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return;

    // main.tin은 프로세스 명령행 인수로 먼저 처리됩니다. 그 뒤 stdin에
    // 들어온 이 명령이 실행되므로 기본 KTin 트리거는 main.tin 다음에 로드됩니다.
    SendRawCommandToMud(L"#read {" + storedPath + L"}");
}
