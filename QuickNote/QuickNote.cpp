#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <string>
#include <strsafe.h>
#include <vector>
#include <ctime>

#pragma comment(lib, "shell32.lib")

#define HOTKEY_ID 1
#define TRAY_UID  1001
#define WM_TRAYICON (WM_USER + 1)


HHOOK g_hook = nullptr;
bool g_recording = false;
NOTIFYICONDATA nid = {};
HWND g_hWnd = nullptr;
HANDLE g_hMutex = nullptr;
std::wstring g_noteBuffer;


std::wstring GetTimeHeader() {
    time_t now = time(nullptr);
    struct tm localTime;
    localtime_s(&localTime, &now);
    wchar_t buffer[80];
    wcsftime(buffer, 80, L"[%Y-%m-%d %H:%M:%S]", &localTime);
    return std::wstring(buffer);
}

void SaveNoteToDisk() {
    // Don't save empty notes
    if (g_noteBuffer.empty()) return;

    std::wofstream logFile;
    logFile.open("QuickNote.txt", std::ios::app);

    if (logFile.is_open()) {
        logFile << GetTimeHeader() << L"\n";
        logFile << g_noteBuffer;
        logFile << L"\n------------------\n";
        logFile.close();
    }
}

void UpdateTray(const wchar_t* msg) {
    nid.uFlags = NIF_INFO;
    StringCchCopyW(nid.szInfo, ARRAYSIZE(nid.szInfo), msg);
    StringCchCopyW(nid.szInfoTitle, ARRAYSIZE(nid.szInfoTitle), L"QuickNote");
    nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;

        if (g_recording) {
            if (kb->vkCode == VK_BACK) {
                // If the buffer isn't empty, remove the last character
                if (!g_noteBuffer.empty()) {
                    g_noteBuffer.pop_back();
                }
            }
            else if (kb->vkCode == VK_RETURN) {
                g_noteBuffer += L'\n';
            }
            else if (kb->vkCode == VK_SPACE) {
                g_noteBuffer += L' ';
            }
            else if (kb->vkCode == VK_TAB) {
                g_noteBuffer += L'\t';
            }
            else {
                // Determine State
                BYTE keyState[256] = { 0 };
                GetKeyboardState(keyState);
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000)   keyState[VK_SHIFT] = 0x80;
                if (GetAsyncKeyState(VK_CAPITAL) & 0x0001) keyState[VK_CAPITAL] = 0x01;
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) keyState[VK_CONTROL] = 0x80;
                if (GetAsyncKeyState(VK_MENU) & 0x8000)    keyState[VK_MENU] = 0x80;

                HKL hkl = GetKeyboardLayout(0);
                wchar_t buffer[5] = { 0 };

                int result = ToUnicodeEx(kb->vkCode, kb->scanCode, keyState, buffer, 4, 0, hkl);

                if (result > 0) {
                    if (buffer[0] > 31) { // Printable chars only
                        g_noteBuffer += buffer; 
                    }
                }
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        break;
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) {
            g_recording = !g_recording;

            if (g_recording) {
                g_noteBuffer.clear(); // Wipe the buffer clean for a new note
                UpdateTray(L"Recording ON");
            }
            else {
                SaveNoteToDisk(); // Flush memory to file
                g_noteBuffer.clear(); // Clear memory
                UpdateTray(L"Recording OFF - Saved");
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(_In_ HINSTANCE hInst, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {

    g_hMutex = CreateMutex(nullptr, TRUE, L"Local\\QuickNoteInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBox(nullptr, L"QuickNote is already running!", L"QuickNote", MB_ICONINFORMATION | MB_OK);
        return 0;
    }

    WNDCLASS wc = {};
    wc.lpfnWndProc = HiddenWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"QuickNoteHiddenClass";
    RegisterClass(&wc);

    g_hWnd = CreateWindowEx(0, wc.lpszClassName, L"QuickNoteHidden",
        0, 0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);

    if (!g_hWnd) return 1;

    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hWnd;
    nid.uID = TRAY_UID;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_INFO;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    StringCchCopyW(nid.szTip, ARRAYSIZE(nid.szTip), L"QuickNote");
    Shell_NotifyIcon(NIM_ADD, &nid);

    RegisterHotKey(g_hWnd, HOTKEY_ID, MOD_CONTROL, '3');
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, hInst, 0);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    Shell_NotifyIcon(NIM_DELETE, &nid);

    if (g_hMutex) {
        ReleaseMutex(g_hMutex);
        CloseHandle(g_hMutex);
    }

    return (int)msg.wParam;
}