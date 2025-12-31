#include <windows.h>
#include <thread>
#include <sstream>
#include <iostream>
#include <psapi.h>
#include <tlhelp32.h>
#include <chrono>
#include <vector>
#include <string>

std::wstring to_wstring(int value) {
    std::wstringstream wss;
    wss << value;
    return wss.str();
}

// 系统托盘
NOTIFYICONDATA nid{};
HWND hHiddenWnd = NULL;

// 创建隐藏窗口
HWND CreateHiddenWindow(HINSTANCE hInstance) {
    WNDCLASS wc{};
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MemoryMonitorHiddenWindow";
    RegisterClass(&wc);

    HWND hWnd = CreateWindow(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    return hWnd;
}

// 初始化托盘图标
void InitTrayIcon(HINSTANCE hInstance) {
    hHiddenWnd = CreateHiddenWindow(hInstance);
    if (!hHiddenWnd) return;

    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hHiddenWnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_USER + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Memory Monitor");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// 托盘气泡提示
void ShowTrayTip(const std::wstring& msg) {
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfo, msg.c_str());
    wcscpy_s(nid.szInfoTitle, L"内存警告");
    nid.dwInfoFlags = NIIF_WARNING;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// 判断进程是否可优化（Ring3 & 非系统核心）
bool IsUserProcess(DWORD pid) {
    if (pid == 0) return false; // System Idle
    if (pid == 4) return false; // System
    WCHAR exeName[MAX_PATH] = { 0 };
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return false;

    if (GetModuleFileNameEx(hProc, NULL, exeName, MAX_PATH) == 0) {
        CloseHandle(hProc);
        return false;
    }

    std::wstring name = exeName;
    // 排除核心系统进程
    std::vector<std::wstring> blacklist = { L"svchost.exe", L"explorer.exe", L"winlogon.exe", L"services.exe", L"lsass.exe" };
    for (auto& blk : blacklist) {
        if (name.find(blk) != std::wstring::npos) {
            CloseHandle(hProc);
            return false;
        }
    }

    CloseHandle(hProc);
    return true;
}

// 优化单个进程
void OptimizeProcess(DWORD pid) {
    HANDLE hProc = OpenProcess(PROCESS_SET_QUOTA | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProc) return;

    // 压缩工作集
    EmptyWorkingSet(hProc);
    // 降低优先级为 BELOW_NORMAL
    SetPriorityClass(hProc, BELOW_NORMAL_PRIORITY_CLASS);

    CloseHandle(hProc);
}

// 内存监控线程
void MonitorMemory() {
    auto lastNotify = std::chrono::steady_clock::now() - std::chrono::minutes(5);

    while (true) {
        MEMORYSTATUSEX mem{};
        mem.dwLength = sizeof(mem);
        if (GlobalMemoryStatusEx(&mem)) {
            DWORD usedPercent = mem.dwMemoryLoad;
            auto now = std::chrono::steady_clock::now();

            if (usedPercent > 85 &&
                std::chrono::duration_cast<std::chrono::minutes>(now - lastNotify).count() >= 5) {

                // 枚举所有进程
                DWORD pids[1024], cbNeeded;
                if (EnumProcesses(pids, sizeof(pids), &cbNeeded)) {
                    DWORD count = cbNeeded / sizeof(DWORD);
                    for (DWORD i = 0; i < count; ++i) {
                        if (IsUserProcess(pids[i])) {
                            OptimizeProcess(pids[i]);
                        }
                    }
                }

                std::wstring msg = L"内存占用: " + to_wstring(usedPercent) + L"%\n已优化后台进程";
                ShowTrayTip(msg);
                lastNotify = now;
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPWSTR lpCmdLine,
    int nCmdShow) {

    InitTrayIcon(hInstance);

    std::thread monitorThread(MonitorMemory);
    monitorThread.detach();

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Shell_NotifyIcon(NIM_DELETE, &nid);
    return 0;
}
