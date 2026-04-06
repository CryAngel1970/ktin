#pragma once
#include "constants.h"
#include "types.h"



struct TerminalCell {
    wchar_t ch = L' ';
    COLORREF fg = RGB(220, 220, 220);
    COLORREF bg = RGB(0, 0, 0);
    bool bold = false;
    bool isWideTrailer = false;
};

class TerminalBuffer {
public:
    int width = 80;
    int height = 24;
    int cursorX = 0;
    int cursorY = 0;
    std::vector<TerminalCell> cells;
    std::recursive_mutex mtx;
    COLORREF defaultBg = RGB(0, 0, 0);
    COLORREF defaultFg = RGB(220, 220, 220);
    std::deque<std::vector<TerminalCell>> history;
    int maxHistory = 5000;
    int scrollOffset = 0;
    bool hasSelection = false;
    int selStartX = 0, selStartY = 0;
    int selEndX = 0, selEndY = 0;


    TerminalBuffer(int w, int h);
    void Resize(int w, int h);
    void ClearSelection();
    void SetSelectionStart(int x, int y);
    void SetSelectionEnd(int x, int y);
    bool IsSelected(int x, int absY);
    std::wstring GetSelectedText();
    std::wstring GetWordAt(int x, int absY);
    std::wstring GetCurrentScreenText();
    std::wstring GetHistoryText();
    TerminalCell& GetCell(int x, int y);
    TerminalCell GetViewCell(int x, int y);
    void DoScroll(int lines);
    void ClearSingleCell(int x, int y);
    void ClearCellPairAware(int x, int y);
    void NormalizeCursorForWrite();
    void MoveCursorLeftVisual(int n = 1);
    void MoveCursorRightVisual(int n = 1);
    void ClearLineRangePairAware(int y, int x1, int x2);
    void PutChar(wchar_t ch, COLORREF fg, COLORREF bg, bool bold);
    void ScrollUp();
    void HandleCommand(char cmd, const std::string& params);
    void ResetHistoryBrowse();
};

bool ResizePseudoConsoleToLogWindow();
void ClearLogWindow(bool clearAllBuffer);
int GetDisplayCellWidth(wchar_t ch);
int GetCharWidth(wchar_t ch);


bool IsBoxDrawingChar(wchar_t ch);
int GetTerminalGlyphWidth(wchar_t ch);
int GetTerminalGlyphWidth(wchar_t ch, bool forceAmbiguousWide);
int GetCharWidthW(wchar_t ch);
int GetCharWidthW(wchar_t ch, bool forceAmbiguousWide);
bool NeedsExtraRightShiftForWideGlyph(wchar_t ch);

