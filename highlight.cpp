#include "highlight.h"

#include "address_book.h"
#include "main.h"
#include "utils.h"
#include "win_util.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <new>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr wchar_t kEditorClass[] = L"KTinHighlightEditorWindow";
    constexpr wchar_t kNameInputClass[] = L"KTinHighlightNameInputWindow";
    constexpr wchar_t kStateProperty[] = L"KTinHighlightEditorState";

    constexpr wchar_t kBlockBegin[] = L"#NOP {KTIN_HIGHLIGHT_EDITOR_BEGIN|1}";
    constexpr wchar_t kBlockEnd[] = L"#NOP {KTIN_HIGHLIGHT_EDITOR_END}";
    constexpr wchar_t kRuntimeBegin[] = L"#NOP {KTIN_HIGHLIGHT_RUNTIME_BEGIN}";
    constexpr wchar_t kRuntimeEnd[] = L"#NOP {KTIN_HIGHLIGHT_RUNTIME_END}";

    constexpr int IDC_HI_PATH = 6500;
    constexpr int IDC_HI_TREE = 6501;
    constexpr int IDC_HI_ADD_FOLDER = 6502;
    constexpr int IDC_HI_ADD_CHILD_FOLDER = 6503;
    constexpr int IDC_HI_ADD_RULE = 6504;
    constexpr int IDC_HI_DELETE = 6505;
    constexpr int IDC_HI_UP = 6506;
    constexpr int IDC_HI_DOWN = 6507;
    constexpr int IDC_HI_DETAIL_TITLE = 6508;
    constexpr int IDC_HI_NAME_LABEL = 6509;
    constexpr int IDC_HI_NAME = 6510;
    constexpr int IDC_HI_ENABLED = 6511;
    constexpr int IDC_HI_CLASS_LABEL = 6512;
    constexpr int IDC_HI_CLASS = 6513;
    constexpr int IDC_HI_PATTERN_LABEL = 6514;
    constexpr int IDC_HI_PATTERN = 6515;
    constexpr int IDC_HI_INVERSE = 6516;
    constexpr int IDC_HI_FG_LABEL = 6517;
    constexpr int IDC_HI_BG_LABEL = 6518;
    constexpr int IDC_HI_FG_NONE = 6519;
    constexpr int IDC_HI_BG_NONE = 6520;
    constexpr int IDC_HI_FG_CUSTOM = 6521;
    constexpr int IDC_HI_BG_CUSTOM = 6522;
    constexpr int IDC_HI_COLOR_HELP = 6523;
    constexpr int IDC_HI_SAVE = 6524;
    constexpr int IDC_HI_RELOAD = 6525;
    constexpr int IDC_HI_HELP = 6526;
    constexpr int IDC_HI_OPEN = 6527;
    constexpr int IDC_HI_SAVE_AS = 6528;
    constexpr int IDC_HI_ASSIGN_ADDRESS = 6529;
    constexpr int IDC_HI_ADDRESS_STATUS = 6530;
    constexpr int IDC_HI_FG_BASE = 6540;
    constexpr int IDC_HI_BG_BASE = 6560;
    constexpr int IDC_NAME_PROMPT = 6590;

    constexpr int kCustomColorMode = 16;

    struct HighlightFolder
    {
        std::wstring id;
        std::wstring parentId;
        std::wstring name;
        bool enabled = true;
    };

    struct HighlightEntry
    {
        std::wstring id;
        std::wstring folderId;
        std::wstring name;
        std::wstring pattern;
        bool enabled = true;
        bool inverse = true;
        int foregroundMode = -1; // -1=없음, 0~15=기본색, 16=직접 선택
        int backgroundMode = -1;
        COLORREF customForeground = RGB(255, 255, 255);
        COLORREF customBackground = RGB(0, 0, 0);
        double priority = 5.0;
    };

    struct HighlightModel
    {
        std::wstring storedPath;
        std::wstring absolutePath;
        std::wstring prefix;
        std::wstring suffix;
        std::vector<HighlightFolder> folders;
        std::vector<HighlightEntry> entries;
        std::set<std::wstring> knownClassIds;
        bool dirty = false;
    };

    struct TreeNodeData
    {
        bool isFolder = true;
        std::wstring id;
    };

    struct HighlightEditorState
    {
        HighlightModel model;
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

    constexpr std::array<COLORREF, 16> kPalette = {
        RGB(0, 0, 0),       RGB(128, 0, 0),     RGB(0, 128, 0),     RGB(128, 128, 0),
        RGB(0, 0, 128),     RGB(128, 0, 128),   RGB(0, 128, 128),   RGB(192, 192, 192),
        RGB(128, 128, 128), RGB(255, 0, 0),     RGB(0, 255, 0),     RGB(255, 255, 0),
        RGB(0, 0, 255),     RGB(255, 0, 255),   RGB(0, 255, 255),   RGB(255, 255, 255)
    };

    constexpr std::array<const wchar_t*, 16> kPaletteNames = {
        L"ebony", L"red", L"green", L"yellow", L"blue", L"magenta", L"cyan", L"silver",
        L"Ebony", L"Red", L"Green", L"Yellow", L"Blue", L"Magenta", L"Cyan", L"White"
    };

    static HighlightEditorState* GetState(HWND hwnd)
    {
        return reinterpret_cast<HighlightEditorState*>(GetPropW(hwnd, kStateProperty));
    }

    static int ClampChannel(int value)
    {
        return std::max(0, std::min(255, value));
    }

    static COLORREF ShiftColor(COLORREF color, int amount)
    {
        return RGB(
            ClampChannel(static_cast<int>(GetRValue(color)) + amount),
            ClampChannel(static_cast<int>(GetGValue(color)) + amount),
            ClampChannel(static_cast<int>(GetBValue(color)) + amount));
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
            if (FAILED(setWindowAttribute(hwnd, 20, &enabled, sizeof(enabled))))
                setWindowAttribute(hwnd, 19, &enabled, sizeof(enabled));
        }
        FreeLibrary(dwmApi);
    }

    static void DestroyBrushes(HighlightEditorState* state)
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

    static void InitializeTheme(HWND hwnd, HighlightEditorState* state)
    {
        if (!state)
            return;

        DestroyBrushes(state);
        state->panelBrush = CreateSolidBrush(state->panelBack);
        state->editBrush = CreateSolidBrush(state->editBack);

        HWND tree = GetDlgItem(hwnd, IDC_HI_TREE);
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
                (ch >= L'A' && ch <= L'F') || (ch >= L'a' && ch <= L'f');
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
        std::wostringstream out;
        out << std::hex << std::uppercase << std::setw(16) << std::setfill(L'0') << value;
        return out.str();
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
            else if (ch == L'}' && --depth < 0)
                return false;
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
            return absolute.substr(module.size());
        return absolute;
    }

    static bool PromptScriptFile(HWND owner, bool saveDialog,
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
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            return false;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 64 * 1024 * 1024)
        {
            CloseHandle(file);
            return false;
        }
        bytes.resize(static_cast<size_t>(size.QuadPart));
        DWORD total = 0;
        while (total < bytes.size())
        {
            DWORD read = 0;
            const DWORD chunk = static_cast<DWORD>(std::min<size_t>(bytes.size() - total, 1u << 20));
            if (!ReadFile(file, bytes.data() + total, chunk, &read, nullptr) || read == 0)
                break;
            total += read;
        }
        CloseHandle(file);
        bytes.resize(total);
        return total == static_cast<DWORD>(size.QuadPart);
    }

    static std::wstring DecodeScriptBytes(std::string bytes)
    {
        if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
            static_cast<unsigned char>(bytes[1]) == 0xBB && static_cast<unsigned char>(bytes[2]) == 0xBF)
            bytes.erase(0, 3);
        if (bytes.empty())
            return L"";
        const int utf8Length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
        if (utf8Length > 0)
            return Utf8ToWide(bytes);
        return MultiByteToWide(bytes, 949);
    }

    static HighlightFolder* FindFolder(HighlightModel& model, const std::wstring& id)
    {
        for (auto& folder : model.folders)
            if (folder.id == id)
                return &folder;
        return nullptr;
    }

    static const HighlightFolder* FindFolder(const HighlightModel& model, const std::wstring& id)
    {
        for (const auto& folder : model.folders)
            if (folder.id == id)
                return &folder;
        return nullptr;
    }

    static HighlightEntry* FindEntry(HighlightModel& model, const std::wstring& id)
    {
        for (auto& entry : model.entries)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    static const HighlightEntry* FindEntry(const HighlightModel& model, const std::wstring& id)
    {
        for (const auto& entry : model.entries)
            if (entry.id == id)
                return &entry;
        return nullptr;
    }

    static std::wstring ClassNameForFolder(const std::wstring& id)
    {
        return L"ktin_h_" + id;
    }

    static bool FolderEffectivelyEnabled(const HighlightModel& model, const HighlightFolder& folder)
    {
        if (!folder.enabled)
            return false;
        std::set<std::wstring> seen;
        const HighlightFolder* current = &folder;
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

    static bool EntryEffectivelyEnabled(const HighlightModel& model, const HighlightEntry& entry)
    {
        if (!entry.enabled)
            return false;
        const HighlightFolder* folder = FindFolder(model, entry.folderId);
        return folder && FolderEffectivelyEnabled(model, *folder);
    }

    static void EnsureModelIntegrity(HighlightModel& model)
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
            HighlightFolder folder;
            folder.id = NewId();
            folder.name = L"기본";
            model.folders.push_back(folder);
        }

        for (auto& folder : model.folders)
        {
            if (!folder.parentId.empty() && !FindFolder(model, folder.parentId))
                folder.parentId.clear();
            std::set<std::wstring> chain;
            const HighlightFolder* current = &folder;
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
        const std::wstring fallbackFolder = model.folders.front().id;
        for (auto& entry : model.entries)
        {
            if (!IsSafeId(entry.id) || !ids.insert(entry.id).second)
                entry.id = NewId();
            if (!FindFolder(model, entry.folderId))
                entry.folderId = fallbackFolder;
            if (Trim(entry.name).empty())
                entry.name = L"새 하이라이트";
            if (entry.foregroundMode < -1 || entry.foregroundMode > kCustomColorMode)
                entry.foregroundMode = -1;
            if (entry.backgroundMode < -1 || entry.backgroundMode > kCustomColorMode)
                entry.backgroundMode = -1;
            if (!std::isfinite(entry.priority) || entry.priority < 1.0 || entry.priority > 9.0)
                entry.priority = 5.0;
        }
    }

    static bool ParseMetadataLine(const std::wstring& line, HighlightModel& model)
    {
        const std::wstring knownPrefix = L"#NOP {KTIN_HIGHLIGHT_KNOWN_CLASS|";
        const std::wstring folderPrefix = L"#NOP {KTIN_HIGHLIGHT_FOLDER|";
        const std::wstring entryPrefix = L"#NOP {KTIN_HIGHLIGHT|";

        if (line.rfind(knownPrefix, 0) == 0 && !line.empty() && line.back() == L'}')
        {
            const std::wstring id = line.substr(knownPrefix.size(), line.size() - knownPrefix.size() - 1);
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
            HighlightFolder folder;
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

        if (line.rfind(entryPrefix, 0) == 0 && !line.empty() && line.back() == L'}')
        {
            const std::wstring payload = line.substr(entryPrefix.size(), line.size() - entryPrefix.size() - 1);
            const auto fields = Split(payload, L'|');
            if (fields.size() != 11)
                return false;
            HighlightEntry entry;
            entry.id = fields[0];
            entry.folderId = fields[1];
            entry.enabled = fields[2] != L"0";
            entry.inverse = fields[3] != L"0";
            entry.foregroundMode = _wtoi(fields[4].c_str());
            entry.customForeground = static_cast<COLORREF>(wcstoull(fields[5].c_str(), nullptr, 10));
            entry.backgroundMode = _wtoi(fields[6].c_str());
            entry.customBackground = static_cast<COLORREF>(wcstoull(fields[7].c_str(), nullptr, 10));
            entry.priority = _wtof(fields[8].c_str());
            if (!HexDecode(fields[9], entry.name) || !HexDecode(fields[10], entry.pattern))
                return false;
            model.entries.push_back(entry);
            return true;
        }
        return false;
    }

    static bool LoadModel(const std::wstring& storedPath, HighlightModel& model)
    {
        model = HighlightModel{};
        model.storedPath = storedPath;
        model.absolutePath = ResolveAbsoluteScriptPath(storedPath);

        std::string bytes;
        std::wstring text;
        if (ReadAllBytes(model.absolutePath, bytes))
            text = NormalizeNewlines(DecodeScriptBytes(bytes));

        const size_t beginPos = FindLineMarker(text, kBlockBegin);
        const size_t firstEndPos = FindLineMarker(text, kBlockEnd);
        const size_t endPos = beginPos == std::wstring::npos ? std::wstring::npos :
            FindLineMarker(text, kBlockEnd, beginPos + wcslen(kBlockBegin));

        if ((beginPos == std::wstring::npos) != (firstEndPos == std::wstring::npos) ||
            (beginPos != std::wstring::npos && endPos == std::wstring::npos))
            return false;

        if (beginPos == std::wstring::npos)
        {
            model.prefix = text;
            if (!model.prefix.empty() &&
                (model.prefix.size() < 2 || model.prefix.substr(model.prefix.size() - 2) != L"\r\n"))
                model.prefix += L"\r\n";
        }
        else
        {
            model.prefix = text.substr(0, beginPos);
            const size_t endLine = text.find(L"\r\n", endPos);
            model.suffix = endLine == std::wstring::npos ? L"" : text.substr(endLine + 2);
            size_t cursor = beginPos;
            while (cursor < endPos)
            {
                size_t lineEnd = text.find(L"\r\n", cursor);
                if (lineEnd == std::wstring::npos || lineEnd > endPos)
                    lineEnd = endPos;
                const std::wstring line = text.substr(cursor, lineEnd - cursor);
                const bool isMetadata =
                    line.rfind(L"#NOP {KTIN_HIGHLIGHT_KNOWN_CLASS|", 0) == 0 ||
                    line.rfind(L"#NOP {KTIN_HIGHLIGHT_FOLDER|", 0) == 0 ||
                    line.rfind(L"#NOP {KTIN_HIGHLIGHT|", 0) == 0;
                if (isMetadata && !ParseMetadataLine(line, model))
                    return false;
                cursor = lineEnd + 2;
            }
        }

        EnsureModelIntegrity(model);
        for (const auto& folder : model.folders)
            model.knownClassIds.insert(folder.id);
        return true;
    }

    static wchar_t HexNibble(int value)
    {
        static const wchar_t* digits = L"0123456789abcdef";
        return digits[std::max(0, std::min(15, value))];
    }

    static std::wstring CustomColorCode(COLORREF color)
    {
        std::wstring code = L"<000>";
        code[1] = HexNibble((static_cast<int>(GetRValue(color)) * 15 + 127) / 255);
        code[2] = HexNibble((static_cast<int>(GetGValue(color)) * 15 + 127) / 255);
        code[3] = HexNibble((static_cast<int>(GetBValue(color)) * 15 + 127) / 255);
        return code;
    }

    static std::wstring ColorToken(int mode, COLORREF custom)
    {
        if (mode >= 0 && mode < static_cast<int>(kPaletteNames.size()))
            return kPaletteNames[static_cast<size_t>(mode)];
        if (mode == kCustomColorMode)
            return CustomColorCode(custom);
        return L"";
    }

    static std::wstring BuildColorSpec(const HighlightEntry& entry)
    {
        std::vector<std::wstring> parts;
        const std::wstring foreground = ColorToken(entry.foregroundMode, entry.customForeground);
        if (!foreground.empty())
            parts.push_back(foreground);
        const std::wstring background = ColorToken(entry.backgroundMode, entry.customBackground);
        if (!background.empty())
        {
            parts.push_back(L"b");
            parts.push_back(background);
        }
        if (entry.inverse)
            parts.push_back(L"reverse");

        std::wstring result;
        for (const auto& part : parts)
        {
            if (!result.empty())
                result += L' ';
            result += part;
        }
        return result;
    }

    static void AppendFolderRuntime(std::wostringstream& out, const HighlightModel& model,
        const HighlightFolder& folder, std::set<std::wstring>& visiting)
    {
        if (!FolderEffectivelyEnabled(model, folder) || !visiting.insert(folder.id).second)
            return;

        out << L"#class {" << ClassNameForFolder(folder.id) << L"} {open}\r\n";
        for (const auto& entry : model.entries)
        {
            if (entry.folderId != folder.id || !entry.enabled)
                continue;
            std::wostringstream priority;
            priority << std::fixed << std::setprecision(2) << entry.priority;
            std::wstring p = priority.str();
            while (!p.empty() && p.back() == L'0') p.pop_back();
            if (!p.empty() && p.back() == L'.') p.pop_back();
            if (p.empty()) p = L"5";
            out << L"#highlight {" << entry.pattern << L"} {" << BuildColorSpec(entry) << L"} {" << p << L"}\r\n";
        }
        for (const auto& child : model.folders)
        {
            if (child.parentId == folder.id)
                AppendFolderRuntime(out, model, child, visiting);
        }
        out << L"#class {" << ClassNameForFolder(folder.id) << L"} {close}\r\n";
        visiting.erase(folder.id);
    }

    static std::wstring BuildManagedBlock(const HighlightModel& model)
    {
        std::wostringstream out;
        out << kBlockBegin << L"\r\n";
        out << L"#NOP {이 구역은 KTin 하이라이트 편집기가 관리합니다. 직접 수정하면 다음 저장 때 다시 작성됩니다.}\r\n";

        std::set<std::wstring> cleanupClassIds = model.knownClassIds;
        for (const auto& folder : model.folders)
            cleanupClassIds.insert(folder.id);
        for (const auto& id : cleanupClassIds)
            out << L"#NOP {KTIN_HIGHLIGHT_KNOWN_CLASS|" << id << L"}\r\n";
        for (const auto& folder : model.folders)
        {
            out << L"#NOP {KTIN_HIGHLIGHT_FOLDER|" << folder.id << L"|" << folder.parentId << L"|"
                << (folder.enabled ? L"1" : L"0") << L"|" << HexEncode(folder.name) << L"}\r\n";
        }
        for (const auto& entry : model.entries)
        {
            std::wostringstream priority;
            priority << std::fixed << std::setprecision(2) << entry.priority;
            out << L"#NOP {KTIN_HIGHLIGHT|" << entry.id << L"|" << entry.folderId << L"|"
                << (entry.enabled ? L"1" : L"0") << L"|" << (entry.inverse ? L"1" : L"0") << L"|"
                << entry.foregroundMode << L"|" << static_cast<unsigned long>(entry.customForeground) << L"|"
                << entry.backgroundMode << L"|" << static_cast<unsigned long>(entry.customBackground) << L"|"
                << priority.str() << L"|" << HexEncode(entry.name) << L"|" << HexEncode(entry.pattern) << L"}\r\n";
        }

        out << kRuntimeBegin << L"\r\n";
        for (const auto& id : cleanupClassIds)
            out << L"#class {" << ClassNameForFolder(id) << L"} {kill}\r\n";
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

    static bool ValidateModel(HWND hwnd, const HighlightModel& model)
    {
        for (const auto& folder : model.folders)
        {
            if (Trim(folder.name).empty())
            {
                ShowCenteredMessageBox(hwnd, L"이름이 비어 있는 폴더가 있습니다.",
                    L"하이라이트 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
        }
        for (const auto& entry : model.entries)
        {
            if (Trim(entry.name).empty())
            {
                ShowCenteredMessageBox(hwnd, L"이름이 비어 있는 하이라이트가 있습니다.",
                    L"하이라이트 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (Trim(entry.pattern).empty())
            {
                const std::wstring message = L"'" + entry.name + L"' 하이라이트의 인식 패턴이 비어 있습니다.";
                ShowCenteredMessageBox(hwnd, message.c_str(), L"하이라이트 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (!HasBalancedBraces(entry.pattern))
            {
                const std::wstring message = L"'" + entry.name + L"' 하이라이트의 인식 패턴에 짝이 맞지 않는 { }가 있습니다.";
                ShowCenteredMessageBox(hwnd, message.c_str(), L"하이라이트 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
            if (!entry.inverse && entry.foregroundMode < 0 && entry.backgroundMode < 0)
            {
                const std::wstring message = L"'" + entry.name + L"' 하이라이트에 글자색·배경색·반전 중 하나를 선택하십시오.";
                ShowCenteredMessageBox(hwnd, message.c_str(), L"하이라이트 편집", MB_OK | MB_ICONWARNING);
                return false;
            }
        }
        return true;
    }

    static bool SaveModel(HWND hwnd, HighlightModel& model)
    {
        EnsureModelIntegrity(model);
        if (!ValidateModel(hwnd, model))
            return false;
        if (!EnsureParentDirectory(model.absolutePath))
        {
            ShowCenteredMessageBox(hwnd, L"하이라이트 파일 폴더를 만들지 못했습니다.",
                L"하이라이트 편집", MB_OK | MB_ICONERROR);
            return false;
        }

        const std::wstring text = model.prefix + BuildManagedBlock(model) + model.suffix;
        const std::wstring tempPath = model.absolutePath + L".tmp";
        const std::wstring backupPath = model.absolutePath + L".bak";
        if (!WriteUtf8NoBomTextFile(tempPath, WideToUtf8(text)))
        {
            DeleteFileW(tempPath.c_str());
            ShowCenteredMessageBox(hwnd, L"임시 하이라이트 파일을 저장하지 못했습니다.",
                L"하이라이트 편집", MB_OK | MB_ICONERROR);
            return false;
        }
        if (GetFileAttributesW(model.absolutePath.c_str()) != INVALID_FILE_ATTRIBUTES)
            CopyFileW(model.absolutePath.c_str(), backupPath.c_str(), FALSE);
        if (!MoveFileExW(tempPath.c_str(), model.absolutePath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(tempPath.c_str());
            ShowCenteredMessageBox(hwnd,
                L"하이라이트 파일을 교체하지 못했습니다. 다른 프로그램에서 파일을 사용 중인지 확인하세요.",
                L"하이라이트 편집", MB_OK | MB_ICONERROR);
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
            _wcsicmp(Trim(a.host).c_str(), Trim(b.host).c_str()) == 0 && a.port == b.port;
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
        AddressBookEntry& entry = g_app->addressBook[static_cast<size_t>(index)];
        entry.scriptPath = Trim(storedPath);
        SaveAddressBook();
        if (g_app->hasActiveSession && SameAddress(g_app->activeSession, entry))
            g_app->activeSession.scriptPath = entry.scriptPath;
        if (g_app->hasPendingConnect && SameAddress(g_app->pendingConnectEntry, entry))
            g_app->pendingConnectEntry.scriptPath = entry.scriptPath;
    }

    static std::wstring ResolveEditorTargetPath()
    {
        constexpr wchar_t kDefaultFile[] = L"ktin_triggers.tin";
        if (!g_app)
            return kDefaultFile;
        const int index = FindCurrentAddressIndex();
        if (index < 0 || index >= static_cast<int>(g_app->addressBook.size()))
            return kDefaultFile;
        AddressBookEntry& entry = g_app->addressBook[static_cast<size_t>(index)];
        if (Trim(entry.scriptPath).empty())
            SyncAddressScriptPath(index, kDefaultFile);
        return Trim(entry.scriptPath).empty() ? kDefaultFile : entry.scriptPath;
    }

    static void UpdatePathControls(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SetWindowTextW(GetDlgItem(hwnd, IDC_HI_PATH), state->model.storedPath.c_str());
        std::wstring status;
        if (state->addressIndex >= 0 && g_app &&
            state->addressIndex < static_cast<int>(g_app->addressBook.size()))
        {
            const AddressBookEntry& entry = g_app->addressBook[static_cast<size_t>(state->addressIndex)];
            status = L"현재 주소록: " + entry.name + L" / 스크립트: " +
                (Trim(entry.scriptPath).empty() ? L"지정 안 됨" : entry.scriptPath);
            EnableWindow(GetDlgItem(hwnd, IDC_HI_ASSIGN_ADDRESS), TRUE);
        }
        else
        {
            status = L"주소록으로 접속한 상태가 아닙니다. 현재 파일은 직접 편집할 수 있습니다.";
            EnableWindow(GetDlgItem(hwnd, IDC_HI_ASSIGN_ADDRESS), FALSE);
        }
        SetWindowTextW(GetDlgItem(hwnd, IDC_HI_ADDRESS_STATUS), status.c_str());
    }

    static std::wstring TreeLabel(const HighlightModel& model, const HighlightFolder& folder)
    {
        return std::wstring(FolderEffectivelyEnabled(model, folder) ? L"[켬] [폴더] " : L"[끔] [폴더] ") + folder.name;
    }

    static std::wstring TreeLabel(const HighlightModel& model, const HighlightEntry& entry)
    {
        return std::wstring(EntryEffectivelyEnabled(model, entry) ? L"[켬] " : L"[끔] ") + entry.name;
    }

    static HTREEITEM InsertTreeNode(HWND tree, HTREEITEM parent, const std::wstring& text,
        bool isFolder, const std::wstring& id)
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

    static void InsertFolderTree(HWND tree, HighlightEditorState& state,
        const HighlightFolder& folder, HTREEITEM parent)
    {
        HTREEITEM item = InsertTreeNode(tree, parent, TreeLabel(state.model, folder), true, folder.id);
        if (!item)
            return;
        for (const auto& child : state.model.folders)
        {
            if (child.parentId == folder.id)
                InsertFolderTree(tree, state, child, item);
        }
        for (const auto& entry : state.model.entries)
        {
            if (entry.folderId == folder.id)
                InsertTreeNode(tree, item, TreeLabel(state.model, entry), false, entry.id);
        }
        TreeView_Expand(tree, item, TVE_EXPAND);
    }

    static void RebuildTree(HWND hwnd, bool preserveSelection)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        HWND tree = GetDlgItem(hwnd, IDC_HI_TREE);
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
        HTREEITEM selected = nullptr;
        if (!selectedId.empty())
            selected = FindTreeItemById(tree, TreeView_GetRoot(tree), selectedId, selectedFolder);
        if (!selected)
            selected = TreeView_GetRoot(tree);
        if (selected)
            TreeView_SelectItem(tree, selected);
        state->rebuildingTree = false;
    }

    static void SetControlVisible(HWND hwnd, int id, bool visible)
    {
        HWND control = GetDlgItem(hwnd, id);
        if (control)
            ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }

    static void SetColorControlsVisible(HWND hwnd, bool visible)
    {
        SetControlVisible(hwnd, IDC_HI_PATTERN_LABEL, visible);
        SetControlVisible(hwnd, IDC_HI_PATTERN, visible);
        SetControlVisible(hwnd, IDC_HI_INVERSE, visible);
        SetControlVisible(hwnd, IDC_HI_FG_LABEL, visible);
        SetControlVisible(hwnd, IDC_HI_BG_LABEL, visible);
        SetControlVisible(hwnd, IDC_HI_FG_NONE, visible);
        SetControlVisible(hwnd, IDC_HI_BG_NONE, visible);
        SetControlVisible(hwnd, IDC_HI_FG_CUSTOM, visible);
        SetControlVisible(hwnd, IDC_HI_BG_CUSTOM, visible);
        SetControlVisible(hwnd, IDC_HI_COLOR_HELP, visible);
        for (int i = 0; i < 16; ++i)
        {
            SetControlVisible(hwnd, IDC_HI_FG_BASE + i, visible);
            SetControlVisible(hwnd, IDC_HI_BG_BASE + i, visible);
        }
    }

    static void InvalidateColorControls(HWND hwnd)
    {
        const int fixedIds[] = {
            IDC_HI_FG_NONE, IDC_HI_BG_NONE,
            IDC_HI_FG_CUSTOM, IDC_HI_BG_CUSTOM
        };
        for (int id : fixedIds)
        {
            HWND control = GetDlgItem(hwnd, id);
            if (control)
                InvalidateRect(control, nullptr, TRUE);
        }
        for (int i = 0; i < 16; ++i)
        {
            HWND foreground = GetDlgItem(hwnd, IDC_HI_FG_BASE + i);
            HWND background = GetDlgItem(hwnd, IDC_HI_BG_BASE + i);
            if (foreground) InvalidateRect(foreground, nullptr, TRUE);
            if (background) InvalidateRect(background, nullptr, TRUE);
        }
    }

    static void UpdateDetails(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        state->updatingControls = true;
        const bool folderSelected = state->selectedIsFolder && FindFolder(state->model, state->selectedId);
        const bool entrySelected = !state->selectedIsFolder && FindEntry(state->model, state->selectedId);
        const bool hasSelection = folderSelected || entrySelected;

        EnableWindow(GetDlgItem(hwnd, IDC_HI_NAME), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_HI_ENABLED), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_HI_DELETE), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_HI_UP), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_HI_DOWN), hasSelection);
        EnableWindow(GetDlgItem(hwnd, IDC_HI_ADD_CHILD_FOLDER), folderSelected);
        EnableWindow(GetDlgItem(hwnd, IDC_HI_ADD_RULE), hasSelection);
        SetColorControlsVisible(hwnd, entrySelected);

        if (!hasSelection)
        {
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_DETAIL_TITLE), L"항목을 선택하세요");
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_NAME), L"");
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_CLASS), L"");
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_PATTERN), L"");
            state->updatingControls = false;
            InvalidateColorControls(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return;
        }

        if (folderSelected)
        {
            HighlightFolder* folder = FindFolder(state->model, state->selectedId);
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_DETAIL_TITLE), L"폴더 상세 편집");
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_NAME), folder->name.c_str());
            SendMessageW(GetDlgItem(hwnd, IDC_HI_ENABLED), BM_SETCHECK,
                folder->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_CLASS), ClassNameForFolder(folder->id).c_str());
        }
        else
        {
            HighlightEntry* entry = FindEntry(state->model, state->selectedId);
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_DETAIL_TITLE), L"하이라이트 상세 편집");
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_NAME), entry->name.c_str());
            SendMessageW(GetDlgItem(hwnd, IDC_HI_ENABLED), BM_SETCHECK,
                entry->enabled ? BST_CHECKED : BST_UNCHECKED, 0);
            const HighlightFolder* folder = FindFolder(state->model, entry->folderId);
            const std::wstring classDisplay = folder ?
                folder->name + L"  (" + ClassNameForFolder(folder->id) + L")" : L"";
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_CLASS), classDisplay.c_str());
            SetWindowTextW(GetDlgItem(hwnd, IDC_HI_PATTERN), entry->pattern.c_str());
            SendMessageW(GetDlgItem(hwnd, IDC_HI_INVERSE), BM_SETCHECK,
                entry->inverse ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        state->updatingControls = false;
        InvalidateColorControls(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    static bool SyncDetails(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state || state->updatingControls || state->selectedId.empty())
            return false;

        wchar_t buffer[4096] = {};
        GetWindowTextW(GetDlgItem(hwnd, IDC_HI_NAME), buffer, static_cast<int>(std::size(buffer)));
        const std::wstring name = Trim(buffer);
        const bool enabled = SendMessageW(GetDlgItem(hwnd, IDC_HI_ENABLED), BM_GETCHECK, 0, 0) == BST_CHECKED;
        bool changed = false;

        if (state->selectedIsFolder)
        {
            HighlightFolder* folder = FindFolder(state->model, state->selectedId);
            if (!folder)
                return false;
            if (!name.empty() && folder->name != name) { folder->name = name; changed = true; }
            if (folder->enabled != enabled) { folder->enabled = enabled; changed = true; }
        }
        else
        {
            HighlightEntry* entry = FindEntry(state->model, state->selectedId);
            if (!entry)
                return false;
            GetWindowTextW(GetDlgItem(hwnd, IDC_HI_PATTERN), buffer, static_cast<int>(std::size(buffer)));
            const std::wstring pattern = buffer;
            const bool inverse = SendMessageW(GetDlgItem(hwnd, IDC_HI_INVERSE), BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (!name.empty() && entry->name != name) { entry->name = name; changed = true; }
            if (entry->enabled != enabled) { entry->enabled = enabled; changed = true; }
            if (entry->pattern != pattern) { entry->pattern = pattern; changed = true; }
            if (entry->inverse != inverse) { entry->inverse = inverse; changed = true; }
        }
        if (changed)
            state->model.dirty = true;
        return changed;
    }

    static std::wstring SelectedFolderId(const HighlightEditorState& state)
    {
        if (state.selectedIsFolder)
            return state.selectedId;
        const HighlightEntry* entry = FindEntry(state.model, state.selectedId);
        return entry ? entry->folderId : L"";
    }

    static LRESULT CALLBACK NamePromptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<NamePromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg)
        {
        case WM_CREATE:
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<NamePromptState*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            if (!state)
                return -1;
            SetWindowTextW(hwnd, state->title.c_str());
            state->panelBrush = CreateSolidBrush(state->panelBack);
            state->editBrush = CreateSolidBrush(state->editBack);
            HFONT font = GetPopupUIFont(hwnd);
            HWND label = CreateWindowExW(0, L"STATIC", state->label.c_str(), WS_CHILD | WS_VISIBLE,
                16, 18, 350, 20, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->value.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                16, 43, 350, 25, hwnd, reinterpret_cast<HMENU>(IDC_NAME_PROMPT), GetModuleHandleW(nullptr), nullptr);
            HWND ok = CreateWindowExW(0, L"BUTTON", L"확인", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                205, 82, 76, 28, hwnd, reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"취소", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                290, 82, 76, 28, hwnd, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
            if (font)
            {
                SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(ok, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
            SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
            ApplyForcedDarkTitleBar(hwnd);
            SetFocus(edit);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == IDOK && state)
            {
                state->value = Trim(GetWindowTextString(GetDlgItem(hwnd, IDC_NAME_PROMPT)));
                if (state->value.empty())
                {
                    ShowCenteredMessageBox(hwnd, L"이름을 입력하십시오.", state->title.c_str(), MB_OK | MB_ICONWARNING);
                    return 0;
                }
                state->accepted = true;
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
        case WM_CTLCOLOREDIT:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->editText);
                SetBkColor(dc, state->editBack);
                return reinterpret_cast<LRESULT>(state->editBrush);
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->panelText);
                return reinterpret_cast<LRESULT>(state->panelBrush);
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
        case WM_DESTROY:
            if (state)
            {
                if (state->panelBrush) { DeleteObject(state->panelBrush); state->panelBrush = nullptr; }
                if (state->editBrush) { DeleteObject(state->editBrush); state->editBrush = nullptr; }
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    static bool PromptName(HWND owner, const std::wstring& title,
        const std::wstring& label, std::wstring& value)
    {
        static bool registered = false;
        if (!registered)
        {
            WNDCLASSW wc{};
            wc.lpfnWndProc = NamePromptProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = kNameInputClass;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                return false;
            registered = true;
        }

        NamePromptState state;
        state.title = title;
        state.label = label;
        state.value = value;
        HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kNameInputClass, title.c_str(),
            WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT,
            400, 155, owner, nullptr, GetModuleHandleW(nullptr), &state);
        if (!dialog)
            return false;
        RECT ownerRect{}, dialogRect{};
        GetWindowRect(owner, &ownerRect);
        GetWindowRect(dialog, &dialogRect);
        const int width = dialogRect.right - dialogRect.left;
        const int height = dialogRect.bottom - dialogRect.top;
        SetWindowPos(dialog, HWND_TOP,
            ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2,
            ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2,
            0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
        EnableWindow(owner, FALSE);
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
        if (state.accepted)
            value = state.value;
        return state.accepted;
    }

    static void AddFolder(HWND hwnd, bool child)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);
        std::wstring name = L"새 폴더";
        if (!PromptName(hwnd, child ? L"하위 폴더 추가" : L"폴더 추가", L"폴더 이름:", name))
            return;
        HighlightFolder folder;
        folder.id = NewId();
        folder.name = Trim(name);
        if (child && state->selectedIsFolder && FindFolder(state->model, state->selectedId))
            folder.parentId = state->selectedId;
        state->model.folders.push_back(folder);
        state->model.dirty = true;
        state->selectedId = folder.id;
        state->selectedIsFolder = true;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
    }

    static void AddEntry(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);
        std::wstring folderId = SelectedFolderId(*state);
        if (!FindFolder(state->model, folderId))
            folderId = state->model.folders.front().id;
        std::wstring name = L"새 하이라이트";
        if (!PromptName(hwnd, L"하이라이트 추가", L"하이라이트 이름:", name))
            return;
        HighlightEntry entry;
        entry.id = NewId();
        entry.folderId = folderId;
        entry.name = Trim(name);
        entry.pattern = L"인식할 문자열";
        state->model.entries.push_back(entry);
        state->model.dirty = true;
        state->selectedId = entry.id;
        state->selectedIsFolder = false;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
    }

    static void CollectFolderIds(const HighlightModel& model, const std::wstring& root,
        std::set<std::wstring>& ids)
    {
        if (!ids.insert(root).second)
            return;
        for (const auto& folder : model.folders)
        {
            if (folder.parentId == root)
                CollectFolderIds(model, folder.id, ids);
        }
    }

    static void DeleteSelected(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state || state->selectedId.empty())
            return;
        SyncDetails(hwnd);
        if (state->selectedIsFolder)
        {
            const HighlightFolder* folder = FindFolder(state->model, state->selectedId);
            if (!folder)
                return;
            const std::wstring message = L"'" + folder->name + L"' 폴더와 그 안의 모든 하위 폴더/하이라이트를 삭제하시겠습니까?";
            if (ShowCenteredMessageBox(hwnd, message.c_str(), L"하이라이트 편집",
                MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            std::set<std::wstring> ids;
            CollectFolderIds(state->model, folder->id, ids);
            state->model.entries.erase(std::remove_if(state->model.entries.begin(), state->model.entries.end(),
                [&](const HighlightEntry& entry) { return ids.count(entry.folderId) != 0; }), state->model.entries.end());
            state->model.folders.erase(std::remove_if(state->model.folders.begin(), state->model.folders.end(),
                [&](const HighlightFolder& item) { return ids.count(item.id) != 0; }), state->model.folders.end());
            EnsureModelIntegrity(state->model);
            state->selectedId = state->model.folders.front().id;
            state->selectedIsFolder = true;
        }
        else
        {
            const HighlightEntry* entry = FindEntry(state->model, state->selectedId);
            if (!entry)
                return;
            const std::wstring message = L"'" + entry->name + L"' 하이라이트를 삭제하시겠습니까?";
            if (ShowCenteredMessageBox(hwnd, message.c_str(), L"하이라이트 편집",
                MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
            const std::wstring folderId = entry->folderId;
            state->model.entries.erase(std::remove_if(state->model.entries.begin(), state->model.entries.end(),
                [&](const HighlightEntry& item) { return item.id == state->selectedId; }), state->model.entries.end());
            state->selectedId = folderId;
            state->selectedIsFolder = true;
        }
        state->model.dirty = true;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
    }

    template <typename T, typename Predicate>
    static bool MoveWithinSiblings(std::vector<T>& items, size_t index, int direction, Predicate sameParent)
    {
        if (index >= items.size())
            return false;
        if (direction < 0)
        {
            for (size_t i = index; i-- > 0;)
            {
                if (sameParent(items[index], items[i]))
                {
                    std::swap(items[index], items[i]);
                    return true;
                }
            }
        }
        else
        {
            for (size_t i = index + 1; i < items.size(); ++i)
            {
                if (sameParent(items[index], items[i]))
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
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
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
                        [](const HighlightFolder& a, const HighlightFolder& b) { return a.parentId == b.parentId; });
                    break;
                }
            }
        }
        else
        {
            for (size_t i = 0; i < state->model.entries.size(); ++i)
            {
                if (state->model.entries[i].id == state->selectedId)
                {
                    moved = MoveWithinSiblings(state->model.entries, i, direction,
                        [](const HighlightEntry& a, const HighlightEntry& b) { return a.folderId == b.folderId; });
                    break;
                }
            }
        }
        if (moved)
        {
            state->model.dirty = true;
            RebuildTree(hwnd, true);
            UpdateDetails(hwnd);
        }
    }

    static bool ConfirmDiscardOrSave(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state || !state->model.dirty)
            return true;
        const int answer = ShowCenteredMessageBox(hwnd,
            L"현재 하이라이트 파일에 저장하지 않은 변경이 있습니다.\n저장하시겠습니까?",
            L"하이라이트 파일 변경", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL)
            return false;
        if (answer == IDYES)
        {
            SyncDetails(hwnd);
            return SaveModel(hwnd, state->model);
        }
        return true;
    }

    static bool LoadIntoEditor(HWND hwnd, const std::wstring& storedPath)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return false;
        HighlightModel loaded;
        if (!LoadModel(storedPath, loaded))
        {
            ShowCenteredMessageBox(hwnd,
                L"하이라이트 관리 구역의 시작/끝 표식이 맞지 않습니다.\n원본 파일은 수정하지 않았습니다.",
                L"하이라이트 파일 불러오기", MB_OK | MB_ICONERROR);
            return false;
        }
        state->model = std::move(loaded);
        state->selectedId = state->model.folders.front().id;
        state->selectedIsFolder = true;
        UpdatePathControls(hwnd);
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);
        return true;
    }

    static void OpenScript(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state || !ConfirmDiscardOrSave(hwnd))
            return;
        std::wstring selected = state->model.storedPath;
        if (PromptScriptFile(hwnd, false, selected, selected))
            LoadIntoEditor(hwnd, selected);
    }

    static void SaveAs(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);
        std::wstring selected = state->model.storedPath;
        if (!PromptScriptFile(hwnd, true, selected, selected))
            return;
        HighlightModel copy = state->model;
        copy.storedPath = selected;
        copy.absolutePath = ResolveAbsoluteScriptPath(selected);
        if (!SaveModel(hwnd, copy))
            return;
        state->model = std::move(copy);
        UpdatePathControls(hwnd);
        ShowCenteredMessageBox(hwnd, L"하이라이트 파일을 다른 이름으로 저장했습니다.",
            L"하이라이트 다른 이름으로 저장", MB_OK | MB_ICONINFORMATION);
    }

    static void AssignAddress(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state || state->addressIndex < 0)
            return;
        SyncAddressScriptPath(state->addressIndex, state->model.storedPath);
        UpdatePathControls(hwnd);
        ShowCenteredMessageBox(hwnd,
            L"현재 하이라이트·트리거 파일을 주소록의 스크립트로 지정했습니다.\n다음 주소록 접속부터 자동으로 읽습니다.",
            L"주소록 스크립트 지정", MB_OK | MB_ICONINFORMATION);
    }

    static void SaveAndMaybeReload(HWND hwnd, bool reload)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return;
        SyncDetails(hwnd);
        if (!SaveModel(hwnd, state->model))
            return;
        RebuildTree(hwnd, true);
        UpdateDetails(hwnd);

        std::wstring message = L"하이라이트 파일을 저장했습니다.";
        if (reload)
        {
            if (g_app && g_app->proc.stdinWrite)
            {
                SendRawCommandToMud(L"#read {" + state->model.storedPath + L"}");
                message = L"하이라이트 파일을 저장하고 TinTin++에서 다시 읽었습니다.";
            }
            else
            {
                message = L"하이라이트 파일은 저장했지만 TinTin++ 백엔드가 실행 중이 아니어서 다시 읽지는 못했습니다.";
            }
        }
        ShowCenteredMessageBox(hwnd, message.c_str(), L"하이라이트 편집", MB_OK | MB_ICONINFORMATION);
    }

    static bool ConfirmClose(HWND hwnd)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state)
            return true;
        SyncDetails(hwnd);
        if (!state->model.dirty)
            return true;
        const int answer = ShowCenteredMessageBox(hwnd,
            L"저장하지 않은 하이라이트 변경이 있습니다.\n저장하고 닫으시겠습니까?",
            L"하이라이트 편집", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (answer == IDCANCEL)
            return false;
        if (answer == IDYES)
            return SaveModel(hwnd, state->model);
        return true;
    }

    static void SetEntryColor(HWND hwnd, bool foreground, int mode)
    {
        HighlightEditorState* state = GetState(hwnd);
        if (!state || state->selectedIsFolder)
            return;
        SyncDetails(hwnd);
        HighlightEntry* entry = FindEntry(state->model, state->selectedId);
        if (!entry)
            return;
        int& targetMode = foreground ? entry->foregroundMode : entry->backgroundMode;
        COLORREF& targetColor = foreground ? entry->customForeground : entry->customBackground;
        if (mode == kCustomColorMode)
        {
            COLORREF selected = targetColor;
            if (!ChooseColorOnly(hwnd, selected))
                return;
            targetColor = selected;
        }
        if (targetMode != mode || mode == kCustomColorMode)
        {
            targetMode = mode;
            state->model.dirty = true;
        }
        InvalidateColorControls(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }

    static bool DrawColorButton(const DRAWITEMSTRUCT* draw, const HighlightEditorState* state)
    {
        if (!draw || !state || draw->CtlType != ODT_BUTTON)
            return false;
        const int id = static_cast<int>(draw->CtlID);
        bool foreground = false;
        int mode = -999;
        if (id >= IDC_HI_FG_BASE && id < IDC_HI_FG_BASE + 16)
        {
            foreground = true;
            mode = id - IDC_HI_FG_BASE;
        }
        else if (id >= IDC_HI_BG_BASE && id < IDC_HI_BG_BASE + 16)
        {
            foreground = false;
            mode = id - IDC_HI_BG_BASE;
        }
        else
        {
            return false;
        }

        COLORREF color = kPalette[static_cast<size_t>(mode)];
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(draw->hDC, &draw->rcItem, brush);
        DeleteObject(brush);
        FrameRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_WINDOWFRAME));

        bool selected = false;
        if (!state->selectedIsFolder)
        {
            const HighlightEntry* entry = FindEntry(state->model, state->selectedId);
            if (entry)
                selected = (foreground ? entry->foregroundMode : entry->backgroundMode) == mode;
        }
        if (selected)
        {
            RECT inner = draw->rcItem;
            InflateRect(&inner, -2, -2);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 210, 70));
            HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
            HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(draw->hDC, inner.left, inner.top, inner.right, inner.bottom);
            SelectObject(draw->hDC, oldBrush);
            SelectObject(draw->hDC, oldPen);
            DeleteObject(pen);
        }
        if ((draw->itemState & ODS_FOCUS) != 0)
        {
            RECT focus = draw->rcItem;
            InflateRect(&focus, -4, -4);
            DrawFocusRect(draw->hDC, &focus);
        }
        return true;
    }

    static bool DrawNormalButton(const DRAWITEMSTRUCT* draw, const HighlightEditorState* state)
    {
        if (!draw || !state || draw->CtlType != ODT_BUTTON)
            return false;
        const int id = static_cast<int>(draw->CtlID);
        if ((id >= IDC_HI_FG_BASE && id < IDC_HI_FG_BASE + 16) ||
            (id >= IDC_HI_BG_BASE && id < IDC_HI_BG_BASE + 16))
            return false;

        COLORREF background = ShiftColor(state->panelBack,
            (draw->itemState & ODS_SELECTED) ? 35 : 18);
        if ((draw->itemState & ODS_DISABLED) != 0)
            background = ShiftColor(state->panelBack, 8);
        HBRUSH brush = CreateSolidBrush(background);
        FillRect(draw->hDC, &draw->rcItem, brush);
        DeleteObject(brush);
        FrameRect(draw->hDC, &draw->rcItem, GetSysColorBrush(COLOR_WINDOWFRAME));

        bool optionSelected = false;
        bool customButton = false;
        COLORREF customColor = RGB(0, 0, 0);
        if (!state->selectedIsFolder)
        {
            const HighlightEntry* entry = FindEntry(state->model, state->selectedId);
            if (entry)
            {
                if (id == IDC_HI_FG_NONE) optionSelected = entry->foregroundMode < 0;
                else if (id == IDC_HI_BG_NONE) optionSelected = entry->backgroundMode < 0;
                else if (id == IDC_HI_FG_CUSTOM)
                {
                    optionSelected = entry->foregroundMode == kCustomColorMode;
                    customButton = true;
                    customColor = entry->customForeground;
                }
                else if (id == IDC_HI_BG_CUSTOM)
                {
                    optionSelected = entry->backgroundMode == kCustomColorMode;
                    customButton = true;
                    customColor = entry->customBackground;
                }
            }
        }

        RECT textRect = draw->rcItem;
        if (customButton)
        {
            RECT swatch = draw->rcItem;
            swatch.left += 5;
            swatch.right = std::min(swatch.right - 4, swatch.left + 18);
            swatch.top += 5;
            swatch.bottom -= 5;
            HBRUSH swatchBrush = CreateSolidBrush(customColor);
            FillRect(draw->hDC, &swatch, swatchBrush);
            DeleteObject(swatchBrush);
            FrameRect(draw->hDC, &swatch, GetSysColorBrush(COLOR_WINDOWFRAME));
            textRect.left += 21;
        }
        if (optionSelected)
        {
            RECT inner = draw->rcItem;
            InflateRect(&inner, -2, -2);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 210, 70));
            HGDIOBJ oldPen = SelectObject(draw->hDC, pen);
            HGDIOBJ oldBrush = SelectObject(draw->hDC, GetStockObject(NULL_BRUSH));
            Rectangle(draw->hDC, inner.left, inner.top, inner.right, inner.bottom);
            SelectObject(draw->hDC, oldBrush);
            SelectObject(draw->hDC, oldPen);
            DeleteObject(pen);
        }

        wchar_t text[256] = {};
        GetWindowTextW(draw->hwndItem, text, static_cast<int>(std::size(text)));
        SetBkMode(draw->hDC, TRANSPARENT);
        SetTextColor(draw->hDC, (draw->itemState & ODS_DISABLED) ? RGB(120, 120, 120) : state->panelText);
        HFONT font = reinterpret_cast<HFONT>(SendMessageW(draw->hwndItem, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = font ? SelectObject(draw->hDC, font) : nullptr;
        DrawTextW(draw->hDC, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

    static LRESULT CALLBACK HighlightEditorProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        HighlightEditorState* state = GetState(hwnd);
        switch (msg)
        {
        case WM_CREATE:
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<HighlightEditorState*>(create->lpCreateParams);
            if (!state)
                return -1;
            SetPropW(hwnd, kStateProperty, state);
            HFONT font = GetPopupUIFont(hwnd);

            CreateWindowExW(0, L"STATIC", L"트리거와 함께 저장할 TinTin++ 스크립트 파일:",
                WS_CHILD | WS_VISIBLE, 16, 16, 340, 20, hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                16, 39, 730, 25, hwnd, reinterpret_cast<HMENU>(IDC_HI_PATH), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"열기...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                756, 38, 76, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_OPEN), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"다른 이름...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                840, 38, 96, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_SAVE_AS), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"주소록 지정", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                944, 38, 96, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_ASSIGN_ADDRESS), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                16, 68, 1024, 20, hwnd, reinterpret_cast<HMENU>(IDC_HI_ADDRESS_STATUS), GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER |
                TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_SHOWSELALWAYS,
                16, 96, 350, 500, hwnd, reinterpret_cast<HMENU>(IDC_HI_TREE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"폴더 추가", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                16, 606, 82, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_ADD_FOLDER), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"하위 폴더", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                104, 606, 82, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_ADD_CHILD_FOLDER), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"하이라이트", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                192, 606, 86, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_ADD_RULE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"삭제", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                284, 606, 82, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_DELETE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"위로", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                16, 640, 82, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_UP), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"아래로", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                104, 640, 82, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_DOWN), GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, L"STATIC", L"하이라이트 상세 편집", WS_CHILD | WS_VISIBLE,
                398, 100, 300, 24, hwnd, reinterpret_cast<HMENU>(IDC_HI_DETAIL_TITLE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"이름", WS_CHILD | WS_VISIBLE,
                414, 139, 70, 20, hwnd, reinterpret_cast<HMENU>(IDC_HI_NAME_LABEL), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                490, 135, 420, 26, hwnd, reinterpret_cast<HMENU>(IDC_HI_NAME), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"사용", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                928, 138, 90, 22, hwnd, reinterpret_cast<HMENU>(IDC_HI_ENABLED), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"CLASS", WS_CHILD | WS_VISIBLE,
                414, 178, 70, 20, hwnd, reinterpret_cast<HMENU>(IDC_HI_CLASS_LABEL), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                490, 174, 528, 26, hwnd, reinterpret_cast<HMENU>(IDC_HI_CLASS), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"STATIC", L"인식 패턴", WS_CHILD | WS_VISIBLE,
                414, 217, 90, 20, hwnd, reinterpret_cast<HMENU>(IDC_HI_PATTERN_LABEL), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                414, 241, 604, 28, hwnd, reinterpret_cast<HMENU>(IDC_HI_PATTERN), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"반전", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                414, 284, 90, 23, hwnd, reinterpret_cast<HMENU>(IDC_HI_INVERSE), GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, L"STATIC", L"글자색", WS_CHILD | WS_VISIBLE,
                414, 329, 70, 20, hwnd, reinterpret_cast<HMENU>(IDC_HI_FG_LABEL), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"없음", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                490, 323, 58, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_FG_NONE), GetModuleHandleW(nullptr), nullptr);
            for (int i = 0; i < 16; ++i)
            {
                CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    556 + i * 27, 323, 24, 27, hwnd,
                    reinterpret_cast<HMENU>(IDC_HI_FG_BASE + i), GetModuleHandleW(nullptr), nullptr);
            }
            CreateWindowExW(0, L"BUTTON", L"직접 선택...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                990, 323, 92, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_FG_CUSTOM), GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, L"STATIC", L"배경색", WS_CHILD | WS_VISIBLE,
                414, 371, 70, 20, hwnd, reinterpret_cast<HMENU>(IDC_HI_BG_LABEL), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"없음", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                490, 365, 58, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_BG_NONE), GetModuleHandleW(nullptr), nullptr);
            for (int i = 0; i < 16; ++i)
            {
                CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                    556 + i * 27, 365, 24, 27, hwnd,
                    reinterpret_cast<HMENU>(IDC_HI_BG_BASE + i), GetModuleHandleW(nullptr), nullptr);
            }
            CreateWindowExW(0, L"BUTTON", L"직접 선택...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                990, 365, 92, 27, hwnd, reinterpret_cast<HMENU>(IDC_HI_BG_CUSTOM), GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, L"STATIC",
                L"색상을 선택하지 않고 반전만 체크할 수 있습니다. 기본색은 16색 버튼, 그 밖의 색은 직접 선택을 사용합니다.",
                WS_CHILD | WS_VISIBLE, 414, 414, 660, 40, hwnd,
                reinterpret_cast<HMENU>(IDC_HI_COLOR_HELP), GetModuleHandleW(nullptr), nullptr);

            CreateWindowExW(0, L"BUTTON", L"도움말", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                398, 624, 82, 32, hwnd, reinterpret_cast<HMENU>(IDC_HI_HELP), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"저장", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                730, 624, 92, 32, hwnd, reinterpret_cast<HMENU>(IDC_HI_SAVE), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"저장 후 적용", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                830, 624, 112, 32, hwnd, reinterpret_cast<HMENU>(IDC_HI_RELOAD), GetModuleHandleW(nullptr), nullptr);
            CreateWindowExW(0, L"BUTTON", L"닫기", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                950, 624, 92, 32, hwnd, reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);

            if (font)
                EnumChildWindows(hwnd, [](HWND child, LPARAM fontValue) -> BOOL {
                    SendMessageW(child, WM_SETFONT, fontValue, TRUE);
                    return TRUE;
                }, reinterpret_cast<LPARAM>(font));
            InitializeTheme(hwnd, state);
            return 0;
        }

        case WM_NOTIFY:
            if (state && reinterpret_cast<NMHDR*>(lParam)->idFrom == IDC_HI_TREE)
            {
                NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
                if (header->code == TVN_SELCHANGINGW && !state->rebuildingTree)
                    SyncDetails(hwnd);
                else if (header->code == TVN_SELCHANGEDW && !state->rebuildingTree)
                {
                    auto* changed = reinterpret_cast<NMTREEVIEWW*>(lParam);
                    auto* data = reinterpret_cast<TreeNodeData*>(changed->itemNew.lParam);
                    if (data)
                    {
                        state->selectedId = data->id;
                        state->selectedIsFolder = data->isFolder;
                        UpdateDetails(hwnd);
                    }
                }
                else if (header->code == TVN_DELETEITEMW)
                {
                    auto* changed = reinterpret_cast<NMTREEVIEWW*>(lParam);
                    delete reinterpret_cast<TreeNodeData*>(changed->itemOld.lParam);
                }
            }
            break;

        case WM_COMMAND:
            if (!state)
                break;
            switch (LOWORD(wParam))
            {
            case IDC_HI_OPEN: OpenScript(hwnd); return 0;
            case IDC_HI_SAVE_AS: SaveAs(hwnd); return 0;
            case IDC_HI_ASSIGN_ADDRESS: AssignAddress(hwnd); return 0;
            case IDC_HI_ADD_FOLDER: AddFolder(hwnd, false); return 0;
            case IDC_HI_ADD_CHILD_FOLDER: AddFolder(hwnd, true); return 0;
            case IDC_HI_ADD_RULE: AddEntry(hwnd); return 0;
            case IDC_HI_DELETE: DeleteSelected(hwnd); return 0;
            case IDC_HI_UP: MoveSelected(hwnd, -1); return 0;
            case IDC_HI_DOWN: MoveSelected(hwnd, 1); return 0;
            case IDC_HI_ENABLED:
                if (HIWORD(wParam) == BN_CLICKED && !state->updatingControls)
                {
                    SyncDetails(hwnd);
                    RebuildTree(hwnd, true);
                    UpdateDetails(hwnd);
                }
                return 0;
            case IDC_HI_INVERSE:
                if (HIWORD(wParam) == BN_CLICKED && !state->updatingControls)
                {
                    SyncDetails(hwnd);
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
                return 0;
            case IDC_HI_FG_NONE: SetEntryColor(hwnd, true, -1); return 0;
            case IDC_HI_BG_NONE: SetEntryColor(hwnd, false, -1); return 0;
            case IDC_HI_FG_CUSTOM: SetEntryColor(hwnd, true, kCustomColorMode); return 0;
            case IDC_HI_BG_CUSTOM: SetEntryColor(hwnd, false, kCustomColorMode); return 0;
            case IDC_HI_SAVE: SaveAndMaybeReload(hwnd, false); return 0;
            case IDC_HI_RELOAD: SaveAndMaybeReload(hwnd, true); return 0;
            case IDC_HI_HELP:
                ShowCenteredMessageBox(hwnd,
                    L"하이라이트는 현재 트리거 스크립트 파일에 함께 저장됩니다.\n\n"
                    L"1. 폴더 또는 하이라이트를 추가합니다.\n"
                    L"2. 인식 패턴과 글자색·배경색 또는 반전을 선택합니다.\n"
                    L"3. '저장 후 적용'을 누르면 TinTin++가 같은 파일을 다시 읽습니다.\n\n"
                    L"생성 구문: #highlight {인식패턴} {색상} {우선순위}\n"
                    L"폴더 사용을 끄면 그 CLASS 안의 하이라이트도 실행되지 않습니다.",
                    L"하이라이트 편집 도움말", MB_OK | MB_ICONINFORMATION);
                return 0;
            case IDCANCEL:
                if (ConfirmClose(hwnd))
                    DestroyWindow(hwnd);
                return 0;
            default:
                if (LOWORD(wParam) >= IDC_HI_FG_BASE && LOWORD(wParam) < IDC_HI_FG_BASE + 16)
                {
                    SetEntryColor(hwnd, true, LOWORD(wParam) - IDC_HI_FG_BASE);
                    return 0;
                }
                if (LOWORD(wParam) >= IDC_HI_BG_BASE && LOWORD(wParam) < IDC_HI_BG_BASE + 16)
                {
                    SetEntryColor(hwnd, false, LOWORD(wParam) - IDC_HI_BG_BASE);
                    return 0;
                }
                break;
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
                const COLORREF border = ShiftColor(state->panelBack, 55);
                HPEN pen = CreatePen(PS_SOLID, 1, border);
                if (pen)
                {
                    HGDIOBJ oldPen = SelectObject(dc, pen);
                    HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                    Rectangle(dc, 388, 90, 1094, 606);
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
            if (DrawColorButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), state) ||
                DrawNormalButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam), state))
                return TRUE;
            break;

        case WM_CTLCOLOREDIT:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->editText);
                SetBkColor(dc, state->editBack);
                return reinterpret_cast<LRESULT>(state->editBrush);
            }
            break;

        case WM_CTLCOLORBTN:
            if (state)
            {
                HDC dc = reinterpret_cast<HDC>(wParam);
                SetTextColor(dc, state->panelText);
                SetBkColor(dc, state->panelBack);
                return reinterpret_cast<LRESULT>(state->panelBrush);
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
                    return reinterpret_cast<LRESULT>(state->editBrush);
                }
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, state->panelText);
                return reinterpret_cast<LRESULT>(state->panelBrush);
            }
            break;

        case WM_DESTROY:
            if (state)
            {
                HWND tree = GetDlgItem(hwnd, IDC_HI_TREE);
                if (tree)
                    TreeView_DeleteAllItems(tree);
                DestroyBrushes(state);
                RemovePropW(hwnd, kStateProperty);
            }
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

void LoadHighlightSettings()
{
    // KTin 2.7.20부터 하이라이트는 config.ini가 아니라 TinTin++ 스크립트에서 관리합니다.
}

void SaveHighlightSettings()
{
    // 호환용 빈 함수. 실제 저장은 하이라이트 편집기의 저장 버튼에서 처리합니다.
}

void ShowHighlightDialog(HWND owner)
{
    if (!owner || !IsWindow(owner))
        return;

    static bool registered = false;
    if (!registered)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = HighlightEditorProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kEditorClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            return;
        registered = true;
    }

    HighlightEditorState state;
    state.addressIndex = FindCurrentAddressIndex();
    const std::wstring storedPath = ResolveEditorTargetPath();
    if (!LoadModel(storedPath, state.model))
    {
        ShowCenteredMessageBox(owner,
            L"하이라이트 파일의 KTin 관리 구역 시작/끝 표식이 맞지 않습니다.\n"
            L"원본 파일은 수정하지 않았습니다. KTIN_HIGHLIGHT_EDITOR_BEGIN과\n"
            L"KTIN_HIGHLIGHT_EDITOR_END 표식을 확인한 뒤 다시 여세요.",
            L"하이라이트 편집", MB_OK | MB_ICONERROR);
        return;
    }
    if (GetFileAttributesW(state.model.absolutePath.c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        state.model.dirty = true;
        if (!SaveModel(owner, state.model))
            return;
    }
    state.selectedId = state.model.folders.front().id;
    state.selectedIsFolder = true;

    HWND dialog = CreateWindowExW(WS_EX_DLGMODALFRAME, kEditorClass,
        L"TinTin++ 하이라이트 편집",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1120, 720,
        owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!dialog)
        return;

    UpdatePathControls(dialog);
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
    MONITORINFO info{ sizeof(info) };
    if (GetMonitorInfoW(monitor, &info))
    {
        x = std::max(static_cast<int>(info.rcWork.left),
            std::min(x, static_cast<int>(info.rcWork.right) - width));
        y = std::max(static_cast<int>(info.rcWork.top),
            std::min(y, static_cast<int>(info.rcWork.bottom) - height));
    }

    SetWindowPos(dialog, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    EnableWindow(owner, FALSE);
    SetFocus(GetDlgItem(dialog, IDC_HI_TREE));

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
