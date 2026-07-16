#include "process_manager.h"

#include "main.h"
#include "terminal_buffer.h"
#include "theme.h"
#include "utils.h"
#include "chat_capture.h"
#include "gmcp.h"
#include "win_util.h"

#include <algorithm>
#include <thread>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif

namespace
{
    void ClosePseudoConsoleIfOpen(HPCON& hpc)
    {
        if (hpc)
        {
            ClosePseudoConsoleHandle(hpc);
            hpc = nullptr;
        }
    }

    void PostLogDirty(HWND hwndMain)
    {
        if (!hwndMain || !g_app)
            return;

        // ReaderThreadProc는 블로킹 ReadFile()을 사용합니다. 예전 50ms 틱 제한은
        // 마지막 출력이 제한 시간 안에 도착한 뒤 추가 데이터가 없으면 화면 갱신
        // 메시지를 영구히 보류했습니다. UI가 아직 처리하지 않은 알림이 없을 때만
        // 즉시 한 번 게시하고, 실제 다시 그리기는 기존 UI 30ms 타이머가 묶습니다.
        bool expected = false;
        if (!g_app->logChunkMessagePending.compare_exchange_strong(expected, true))
            return;

        if (!PostMessageW(hwndMain, WM_APP_LOG_CHUNK, 0, 0))
            g_app->logChunkMessagePending.store(false);
    }

    void AppendBounded(std::string& dst, const char* data, size_t len, size_t limit)
    {
        if (!data || len == 0 || limit == 0)
            return;

        if (len >= limit)
        {
            dst.assign(data + len - limit, data + len);
            return;
        }

        const size_t needed = dst.size() + len;
        if (dst.capacity() < needed)
        {
            size_t nextCap = dst.capacity() ? dst.capacity() * 2 : 4096;
            if (nextCap < needed)
                nextCap = needed;
            if (nextCap > limit)
                nextCap = limit;
            dst.reserve(nextCap);
        }

        dst.append(data, len);
        if (dst.size() > limit)
        {
            const size_t headroom = std::min<size_t>(limit / 8, 128 * 1024);
            const size_t target = (limit > headroom) ? (limit - headroom) : limit;
            dst.erase(0, dst.size() - target);
        }
    }

}


void ReaderThreadProc(HWND hwndMain, HANDLE hRead)
{
    AnsiToRunsParser parser;
    char buffer[4096];

    while (g_app && !g_app->shuttingDown)
    {
        DWORD read = 0;
        BOOL ok = ReadFile(hRead, buffer, sizeof(buffer), &read, nullptr);
        if (!ok || read == 0)
            break;

        if (g_app && read > 0 && (g_app->captureLogEnabled || g_app->chatCaptureEnabled))
        {
            if (g_app->captureLogEnabled && g_app->captureLogOpen && g_app->captureLogAnsi)
                WriteRawAnsiBytesToCaptureLog(buffer, read);

            const size_t kMaxCur = 256 * 1024;
            const size_t kMaxHist = 2 * 1024 * 1024;
            AppendBounded(g_app->rawAnsiCurrentScreen, buffer, read, kMaxCur);
            AppendBounded(g_app->rawAnsiHistory, buffer, read, kMaxHist);
        }

        if (parser.Feed(buffer, read))
            PostLogDirty(hwndMain);
    }

    if (parser.Flush())
        PostLogDirty(hwndMain);

    PostMessageW(hwndMain, WM_APP_PROCESS_EXIT, 0, 0);
}

void StopProcessAndThread()
{
    if (!g_app)
        return;

    g_app->shuttingDown = true;

    if (g_app->proc.stdoutRead)
        CancelIoEx(g_app->proc.stdoutRead, nullptr);

    ResetWinHandleRef(g_app->proc.stdinWrite);
    ResetWinHandleRef(g_app->proc.stdoutRead);
    ClosePseudoConsoleIfOpen(g_app->proc.hPC);

    if (g_app->readerThread.joinable())
        g_app->readerThread.join();

    g_app->logChunkMessagePending.store(false);

    if (g_app->proc.process)
    {
        if (WaitForSingleObject(g_app->proc.process, 500) == WAIT_TIMEOUT)
        {
            TerminateProcess(g_app->proc.process, 0);
            WaitForSingleObject(g_app->proc.process, 1000);
        }
        ResetWinHandleRef(g_app->proc.process);
    }

    ResetWinHandleRef(g_app->proc.thread);
}

bool StartTinTinProcess()
{
    if (!g_app)
        return false;

    PFN_CreatePseudoConsole createFn = nullptr;
    PFN_ResizePseudoConsole resizeFn = nullptr;
    PFN_ClosePseudoConsole closeFn = nullptr;
    if (!GetConPtyApi(&createFn, &resizeFn, &closeFn))
        return false;
    (void)resizeFn;
    (void)closeFn;

    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
    UniqueHandle inputRead;
    UniqueHandle inputWrite;
    UniqueHandle outputRead;
    UniqueHandle outputWrite;
    HPCON hPC = nullptr;
    UniqueProcThreadAttributeList attrList;

    auto cleanup = [&]()
    {
        ClosePseudoConsoleIfOpen(hPC);
    };

    HANDLE rawInputRead = nullptr;
    HANDLE rawInputWrite = nullptr;
    if (!CreatePipe(&rawInputRead, &rawInputWrite, &sa, 0))
        return false;
    inputRead.Reset(rawInputRead);
    inputWrite.Reset(rawInputWrite);

    if (!SetHandleInformation(inputWrite.Get(), HANDLE_FLAG_INHERIT, 0))
    {
        cleanup();
        return false;
    }

    HANDLE rawOutputRead = nullptr;
    HANDLE rawOutputWrite = nullptr;
    if (!CreatePipe(&rawOutputRead, &rawOutputWrite, &sa, 0))
    {
        cleanup();
        return false;
    }
    outputRead.Reset(rawOutputRead);
    outputWrite.Reset(rawOutputWrite);

    if (!SetHandleInformation(outputRead.Get(), HANDLE_FLAG_INHERIT, 0))
    {
        cleanup();
        return false;
    }

    COORD ptySize = GetPseudoConsoleSizeFromLogWindow();
    if (FAILED(createFn(ptySize, inputRead.Get(), outputWrite.Get(), 0, &hPC)))
    {
        cleanup();
        return false;
    }

    inputRead.Reset();
    outputWrite.Reset();

    if (!attrList.Allocate(1))
    {
        cleanup();
        return false;
    }

    if (!UpdateProcThreadAttribute(attrList.Get(), 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
        hPC, sizeof(hPC), nullptr, nullptr))
    {
        cleanup();
        return false;
    }

    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrList.Get();

    std::wstring exeDir = GetModuleDirectory();
    std::wstring exePath = MakeAbsolutePath(exeDir, L"bin\\tt++.exe");
    std::wstring tinPath = MakeAbsolutePath(exeDir, L"main.tin");
    std::wstring cmdLine = L"\"" + exePath + L"\" \"" + tinPath + L"\"";

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(nullptr, &cmdLine[0], nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT, nullptr, exeDir.c_str(), &si.StartupInfo, &pi);

    if (!ok)
    {
        cleanup();
        return false;
    }

    ResetWinHandleRef(pi.hThread);

    g_app->proc = { pi.hProcess, nullptr, inputWrite.Release(), outputRead.Release(), hPC };
    hPC = nullptr;
    return true;
}
