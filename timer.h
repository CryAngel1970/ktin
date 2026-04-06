#pragma once
#include <windows.h>
#include <string>
#include <vector>

// 런타임 실행 상태
enum TimerRunState
{
    TIMER_STOPPED = 0,
    TIMER_RUNNING,
    TIMER_PAUSED
};

// 설정 및 런타임 데이터 구조체
struct TimerItem
{
    // --- 설정 데이터 (파일에 저장됨) ---
    std::wstring name;
    std::wstring groupPath;

    bool enabled = false;   // 설정상 사용 여부
    bool repeat = false;    // 반복 여부
    bool autoStart = false; // 시작 시 자동 실행 여부

    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;

    std::wstring command;

    // --- 런타임 데이터 (파일에 저장되지 않음) ---
    TimerRunState state = TIMER_STOPPED;
    ULONGLONG intervalMs = 0;     // 발동 주기 (설정값을 ms로 변환하여 캐싱)
    ULONGLONG remainingMs = 0;    // Pause 시 남은 시간 보관용
    ULONGLONG nextFireTick = 0;   // 다음 발동 예상 시스템 틱
    ULONGLONG lastFireTick = 0;   // 마지막 발동 시스템 틱
};

// --- 함수 선언 ---

// 설정 로드/저장
void LoadTimerSettings();
void SaveTimerSettings();

// 런타임 제어 및 엔진
void RecalculateTimerInterval(TimerItem& t);
void StartTimerItem(TimerItem& t);
void StopTimerItem(TimerItem& t);
void PauseTimerItem(TimerItem& t);
void ResumeTimerItem(TimerItem& t);
void ResetTimerItem(TimerItem& t);

void RunTimerEngine(); // WM_TIMER에서 주기적으로 호출됨
bool InterceptTimerCommand(const std::wstring& inputCmd); // #TIMER 명령어 가로채기

// UI 호출
void PromptTimerDialog(HWND owner);