#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

// --- CONFIGURATION ---
const char* JSON_URL_PRIMARY = "https://raw.githubusercontent.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/gamelist/primarygamelist.json"; // UPDATE THIS
const char* JSON_URL_FALLBACK = "https://raw.githubusercontent.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/gamelist/fallbackgamelist.json"; // UPDATE THIS

// Global UI Brushes for Dark Theme
HBRUSH hBgBrush;
HBRUSH hBtnBrush;
COLORREF clrText = RGB(240, 240, 240);
COLORREF clrBg = RGB(30, 30, 30);
COLORREF clrEditBg = RGB(45, 45, 45);
HBRUSH hEditBrush;
HFONT hFont;

// Global dummy state
int totalTime = 0;
int timeLeft = 0;
char dgeFolderPath[MAX_PATH] = { 0 };
char origExePath[MAX_PATH] = { 0 };

// Controls Main
HWND hGameName, hCustomExe, hTime, hBtnLaunch;
// Controls Dummy
HWND hTimerLabel, hProgressLabel, hBtnCancel;

// --- UTILITY: JSON Fetcher ---
char* FetchJSON(const char* url) {
    HINTERNET hInternet = InternetOpenA("DGE_App/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return NULL;

    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) {
        InternetCloseHandle(hInternet);
        return NULL;
    }

    DWORD bytesRead = 0;
    DWORD totalBytes = 0;
    DWORD bufSize = 65536; // 64KB max for this lightweight app
    char* buffer = (char*)calloc(bufSize, 1);

    while (InternetReadFile(hUrl, buffer + totalBytes, bufSize - totalBytes - 1, &bytesRead) && bytesRead > 0) {
        totalBytes += bytesRead;
    }

    InternetCloseHandle(hUrl);
    InternetCloseHandle(hInternet);
    return buffer;
}

// --- UTILITY: Fuzzy String Matcher ---
BOOL FuzzyCompare(const char* s1, const char* s2) {
    while (*s1 || *s2) {
        // Skip anything that isn't a letter or number in both strings
        while (*s1 && !((*s1 >= 'A' && *s1 <= 'Z') || (*s1 >= 'a' && *s1 <= 'z') || (*s1 >= '0' && *s1 <= '9'))) s1++;
        while (*s2 && !((*s2 >= 'A' && *s2 <= 'Z') || (*s2 >= 'a' && *s2 <= 'z') || (*s2 >= '0' && *s2 <= '9'))) s2++;

        // Convert to lowercase for comparison
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;

        if (c1 != c2) return FALSE;

        if (*s1) s1++;
        if (*s2) s2++;
    }
    return TRUE;
}

// --- UTILITY: String Parser ---
BOOL ParseGame(const char* json, const char* target, char* outPrimary, char* outExe) {
    const char* p = json;
    while ((p = strstr(p, "\"names\"")) != NULL) {
        p = strchr(p, '[');
        if (!p) break;

        char primary[256] = { 0 };
        BOOL hasPrimary = FALSE;
        BOOL matchFound = FALSE;
        const char* endArr = strchr(p, ']');
        const char* strStart = p;

        // Parse array of aliases
        while ((strStart = strchr(strStart, '"')) != NULL && strStart < endArr) {
            strStart++;
            const char* strEnd = strchr(strStart, '"');
            if (!strEnd) break;

            char alias[256] = { 0 };
            strncpy(alias, strStart, strEnd - strStart);

            if (!hasPrimary) {
                strcpy(primary, alias);
                hasPrimary = TRUE;
            }

            if (FuzzyCompare(alias, target)) {
                matchFound = TRUE;
            }
            strStart = strEnd + 1;
        }

        if (matchFound) {
            const char* exeProp = strstr(endArr, "\"exe\"");
            if (exeProp) {
                const char* exeStart = strchr(exeProp + 5, '"');
                if (exeStart) {
                    exeStart++;
                    const char* exeEnd = strchr(exeStart, '"');
                    if (exeEnd) {
                        strncpy(outExe, exeStart, exeEnd - exeStart);
                        strcpy(outPrimary, primary);
                        
                        // Unescape JSON '\\' to '\'
                        char* read = outExe;
                        char* write = outExe;
                        while (*read) {
                            if (*read == '\\' && *(read + 1) == '\\') {
                                *write++ = '\\';
                                read += 2;
                            }
                            else {
                                *write++ = *read++;
                            }
                        }
                        *write = '\0';
                        return TRUE;
                    }
                }
            }
        }
        p = endArr;
    }
    return FALSE;
}

// --- UTILITY: Self-Destruct Trigger ---
void TriggerSelfDestruct() {
    char cmd[MAX_PATH * 2];
    // Uses ping as a 2 second delay before attempting deletion, so this exe can close safely
    sprintf(cmd, "/c ping 127.0.0.1 -n 2 > nul & rmdir /s /q \"%s\" & start \"\" \"%s\"", dgeFolderPath, origExePath);
    ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
}

BOOL CALLBACK SetFontProc(HWND hwndChild, LPARAM lParam) {
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// --- DUMMY APP WINDOW PROC ---
LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        hTimerLabel = CreateWindowA("STATIC", "Time Remaining: --", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 20, 240, 20, hwnd, NULL, NULL, NULL);
        hProgressLabel = CreateWindowA("STATIC", "Progress: 0%", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 50, 240, 20, hwnd, NULL, NULL, NULL);
        hBtnCancel = CreateWindowA("BUTTON", "Terminate", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 80, 90, 120, 30, hwnd, (HMENU)1, NULL, NULL);
        SetTimer(hwnd, 1, 1000, NULL);
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);
        break;
    }
    case WM_TIMER: {
        timeLeft--;
        if (timeLeft <= 0) {
            PostMessage(hwnd, WM_CLOSE, 0, 0);
            return 0;
        }
        char buf[64];
        int minutes = timeLeft / 60;
        int seconds = timeLeft % 60;
        
        // %02d ensures seconds always show as two digits (e.g., 13m 05s)
        sprintf(buf, "Time Remaining: %dm %02ds", minutes, seconds); 
        SetWindowTextA(hTimerLabel, buf);

        int percent = (int)(((float)(totalTime - timeLeft) / (float)totalTime) * 100.0f);
        sprintf(buf, "Progress: %d%%", percent);
        SetWindowTextA(hProgressLabel, buf);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) PostMessage(hwnd, WM_CLOSE, 0, 0);
        break;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        FillRect(pdis->hDC, &pdis->rcItem, hBtnBrush);
        SetBkMode(pdis->hDC, TRANSPARENT);
        SetTextColor(pdis->hDC, clrText);
        DrawTextA(pdis->hDC, "Terminate", -1, &pdis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText);
        SetBkColor((HDC)wParam, clrBg);
        return (LRESULT)hBgBrush;
    case WM_CLOSE:
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        TriggerSelfDestruct(); // Initiate self-deletion sequence
        PostQuitMessage(0);
        break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- UTILITY: Math Equation Evaluator ---
int EvalExpr(const char** p);

int ParseFactor(const char** p) {
    while (**p == ' ') (*p)++;
    int val = 0;
    if (**p == '(') {
        (*p)++;
        val = EvalExpr(p);
        if (**p == ')') (*p)++;
    } else {
        while (**p >= '0' && **p <= '9') {
            val = val * 10 + (**p - '0');
            (*p)++;
        }
    }
    while (**p == ' ') (*p)++;
    return val;
}

int ParseTerm(const char** p) {
    int val = ParseFactor(p);
    while (**p == '*' || **p == '/') {
        char op = **p;
        (*p)++;
        int next = ParseFactor(p);
        if (op == '*') val *= next;
        else if (next != 0) val /= next; // prevent divide by zero
    }
    return val;
}

int EvalExpr(const char** p) {
    int val = ParseTerm(p);
    while (**p == '+' || **p == '-') {
        char op = **p;
        (*p)++;
        int next = ParseTerm(p);
        if (op == '+') val += next;
        else val -= next;
    }
    return val;
}

// --- MAIN APP WINDOW PROC ---
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowA("STATIC", "Game Name (or alias):", WS_CHILD | WS_VISIBLE, 20, 20, 200, 20, hwnd, NULL, NULL, NULL);
        hGameName = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 20, 40, 240, 22, hwnd, NULL, NULL, NULL);

        CreateWindowA("STATIC", "Custom EXE Path (Optional):", WS_CHILD | WS_VISIBLE, 20, 70, 200, 20, hwnd, NULL, NULL, NULL);
        hCustomExe = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 20, 90, 240, 22, hwnd, NULL, NULL, NULL);

        CreateWindowA("STATIC", "Time (Seconds):", WS_CHILD | WS_VISIBLE, 20, 120, 200, 20, hwnd, NULL, NULL, NULL);
        hTime = CreateWindowA("EDIT", "910", WS_CHILD | WS_VISIBLE | WS_BORDER, 20, 140, 100, 22, hwnd, NULL, NULL, NULL);

        hBtnLaunch = CreateWindowA("BUTTON", "Launch Game", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 80, 180, 120, 30, hwnd, (HMENU)2, NULL, NULL);
        
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);
        break;
    }
    case WM_COMMAND: {
        if (LOWORD(wParam) == 2) { // Launch Button
            char gameInput[256], customExe[256], timeInput[32];
            GetWindowTextA(hGameName, gameInput, 256);
            GetWindowTextA(hCustomExe, customExe, 256);
            GetWindowTextA(hTime, timeInput, 32);

            const char* mathPtr = timeInput;
            int t = EvalExpr(&mathPtr);
            if (t <= 0 || (strlen(gameInput) == 0 && strlen(customExe) == 0)) {
                MessageBoxA(hwnd, "Please enter either a Game Name or Custom EXE, and a valid Time.", "Error", MB_ICONERROR | MB_OK);
                break;
            }

            char primaryName[256] = { 0 };
            char exePath[256] = { 0 };

            // Fetch & Parse Primary JSON
            BOOL foundInJson = FALSE;
            char* jsonPrimary = FetchJSON(JSON_URL_PRIMARY);
            if (jsonPrimary) {
                foundInJson = ParseGame(jsonPrimary, gameInput, primaryName, exePath);
                free(jsonPrimary);
            }

            // If not found in Primary, try Fallback JSON
            if (!foundInJson) {
                char* jsonFallback = FetchJSON(JSON_URL_FALLBACK);
                if (jsonFallback) {
                    foundInJson = ParseGame(jsonFallback, gameInput, primaryName, exePath);
                    free(jsonFallback);
                }
            }

            // Fallback / Priorities
            if (strlen(customExe) > 0) strcpy(exePath, customExe);
            if (strlen(primaryName) == 0) {
                if (strlen(gameInput) > 0) strcpy(primaryName, gameInput);
                else strcpy(primaryName, "CustomGame"); // Fallback if no game name was typed
            }

            if (strlen(exePath) == 0) {
                MessageBoxA(hwnd, "Game not found in JSON. Please provide a Custom EXE path.", "Not Found", MB_ICONWARNING | MB_OK);
                break;
            }

            // --- NEW: Strip special characters so folder creation doesn't fail ---
            char* r = primaryName;
            char* w = primaryName;
            while (*r) {
                // Strips all invalid Windows folder characters
                if (*r != ':' && *r != ';' && *r != '<' && *r != '>' && *r != '"' && 
                    *r != '/' && *r != '\\' && *r != '|' && *r != '?' && *r != '*') {
                    *w++ = *r;
                }
                r++;
            }
            *w = '\0';

            // Build Temp Paths
            char tempDir[MAX_PATH];
            GetTempPathA(MAX_PATH, tempDir);
            
            char baseDgeFolder[MAX_PATH];
            sprintf(baseDgeFolder, "%sDGE_%s", tempDir, primaryName);

            char fullExePath[MAX_PATH];
            sprintf(fullExePath, "%s\\%s", baseDgeFolder, exePath);

            // Extract directory from fullExePath to create it
            char dirToCreate[MAX_PATH];
            strcpy(dirToCreate, fullExePath);
            char* lastSlash = strrchr(dirToCreate, '\\');
            if (lastSlash) *lastSlash = '\0';

            // Create recursive directories
            SHCreateDirectoryExA(NULL, dirToCreate, NULL);

            // Self-Copy
            char currentExe[MAX_PATH];
            GetModuleFileNameA(NULL, currentExe, MAX_PATH);
            CopyFileA(currentExe, fullExePath, FALSE);

            // Execute the copy with hidden arguments
            char cmdLine[MAX_PATH * 3];
            sprintf(cmdLine, "\"%s\" -dummy %d \"%s\" \"%s\"", fullExePath, t, baseDgeFolder, currentExe);

            STARTUPINFOA si = { sizeof(si) };
            PROCESS_INFORMATION pi;
            if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
                PostQuitMessage(0); // Close Main UI
            } else {
                MessageBoxA(hwnd, "Failed to launch dummy process.", "Error", MB_ICONERROR | MB_OK);
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText);
        SetBkColor((HDC)wParam, clrBg);
        return (LRESULT)hBgBrush;
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wParam, clrText);
        SetBkColor((HDC)wParam, clrEditBg);
        return (LRESULT)hEditBrush;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT pdis = (LPDRAWITEMSTRUCT)lParam;
        FillRect(pdis->hDC, &pdis->rcItem, hBtnBrush);
        SetBkMode(pdis->hDC, TRANSPARENT);
        SetTextColor(pdis->hDC, clrText);
        DrawTextA(pdis->hDC, "Launch Game", -1, &pdis->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return TRUE;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int showCmd) {
    // Setup Dark Theme Brushes
    hBgBrush = CreateSolidBrush(clrBg);
    hBtnBrush = CreateSolidBrush(RGB(70, 70, 70));
    hEditBrush = CreateSolidBrush(clrEditBg);

    // Detect if running as Dummy or Main
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    BOOL isDummy = FALSE;

    if (argc >= 5) {
        char arg1[32];
        WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, arg1, 32, NULL, NULL);
        if (strcmp(arg1, "-dummy") == 0) {
            isDummy = TRUE;
            totalTime = _wtoi(argv[2]);
            timeLeft = totalTime;
            WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, dgeFolderPath, MAX_PATH, NULL, NULL);
        }
        WideCharToMultiByte(CP_UTF8, 0, argv[4], -1, origExePath, MAX_PATH, NULL, NULL);
    }
    LocalFree(argv);

hFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = isDummy ? DummyWndProc : MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(101));
    wc.hbrBackground = hBgBrush;
    wc.lpszClassName = isDummy ? "DgeDummyClass" : "DgeMainClass";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, isDummy ? "Game Session" : "Discord Game Emulator",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, isDummy ? 180 : 270,
        NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, showCmd);
    int useImmersiveDarkMode = 1;
    DwmSetWindowAttribute(hwnd, 20, &useImmersiveDarkMode, sizeof(useImmersiveDarkMode));
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeleteObject(hBgBrush);
    DeleteObject(hBtnBrush);
    DeleteObject(hEditBrush);
    return 0;
}
