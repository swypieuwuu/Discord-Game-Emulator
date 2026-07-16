#define _WIN32_WINNT 0x0600
#include <windows.h>
#include <wininet.h>
#include <shlobj.h>
#include <shellapi.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "dwmapi.lib")

// --- CONFIGURATION ---
const char* JSON_URL_PRIMARY = "https://raw.githubusercontent.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/gamelist/primarygamelist.json";
const char* JSON_URL_FALLBACK = "https://raw.githubusercontent.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/gamelist/fallbackgamelist.json";
const float APP_VERSION = 1.0f; // Increase this in the source code every time you release a new update
const char* VERSION_URL = "https://raw.githubusercontent.com/swypieuwuu/Discord-Game-Emulator/refs/heads/main/version.txt";
char updateUrl[512] = { 0 };

// --- GLOBALS ---
HBRUSH hBgBrush, hBtnBrush, hEditBrush;
COLORREF clrText = RGB(240, 240, 240);
COLORREF clrBg = RGB(30, 30, 30);
COLORREF clrEditBg = RGB(45, 45, 45);
HFONT hFont;

// Queue Data
typedef struct {
    char gameName[256];
    char customExe[256];
    int timeSec;
} QueueItem;

QueueItem queue[100];
int queueCount = 0;
BOOL isQueueMode = FALSE;

// Main App Controls
HWND hGameName, hCustomExe, hTime;
HWND hBtnLaunch, hBtnToggleQueue, hBtnUpdate;
HWND hBtnAddQueue, hBtnStartQueue, hBtnRemoveQueue, hListBox, hQueueLabel;

// Dummy State
int totalTime = 0, timeLeft = 0;
int qCurrent = 1, qTotal = 1;
char dgeFolderPath[MAX_PATH] = { 0 };
char origExePath[MAX_PATH] = { 0 };
char queueFilePath[MAX_PATH] = { 0 };
char currentGameName[256] = { 0 };
HWND hTimerLabel, hProgressLabel, hQueueStatusLabel, hGameLabel, hBtnCancel;

// --- UTILITY: Math Equation Evaluator ---
int EvalExpr(const char** p);
int ParseFactor(const char** p) {
    while (**p == ' ') (*p)++;
    int val = 0;
    if (**p == '(') {
        (*p)++; val = EvalExpr(p);
        if (**p == ')') (*p)++;
    } else {
        while (**p >= '0' && **p <= '9') { val = val * 10 + (**p - '0'); (*p)++; }
    }
    while (**p == ' ') (*p)++;
    return val;
}
int ParseTerm(const char** p) {
    int val = ParseFactor(p);
    while (**p == '*' || **p == '/') {
        char op = **p; (*p)++;
        int next = ParseFactor(p);
        if (op == '*') val *= next;
        else if (next != 0) val /= next;
    }
    return val;
}
int EvalExpr(const char** p) {
    int val = ParseTerm(p);
    while (**p == '+' || **p == '-') {
        char op = **p; (*p)++;
        int next = ParseTerm(p);
        if (op == '+') val += next;
        else val -= next;
    }
    return val;
}

// --- UTILITY: Fuzzy String Matcher ---
BOOL FuzzyCompare(const char* s1, const char* s2) {
    while (*s1 || *s2) {
        while (*s1 && !((*s1 >= 'A' && *s1 <= 'Z') || (*s1 >= 'a' && *s1 <= 'z') || (*s1 >= '0' && *s1 <= '9'))) s1++;
        while (*s2 && !((*s2 >= 'A' && *s2 <= 'Z') || (*s2 >= 'a' && *s2 <= 'z') || (*s2 >= '0' && *s2 <= '9'))) s2++;
        char c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        char c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return FALSE;
        if (*s1) s1++;
        if (*s2) s2++;
    }
    return TRUE;
}

// --- UTILITY: JSON Fetcher (Dynamic Size) ---
char* FetchJSON(const char* url) {
    HINTERNET hInternet = InternetOpenA("DGE_App/1.0", INTERNET_OPEN_TYPE_DIRECT, NULL, NULL, 0);
    if (!hInternet) return NULL;
    HINTERNET hUrl = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hUrl) { InternetCloseHandle(hInternet); return NULL; }

    DWORD bytesRead = 0, totalBytes = 0, bufSize = 65536;
    char* buffer = (char*)malloc(bufSize);
    if (!buffer) { InternetCloseHandle(hUrl); InternetCloseHandle(hInternet); return NULL; }

    while (InternetReadFile(hUrl, buffer + totalBytes, bufSize - totalBytes - 1, &bytesRead) && bytesRead > 0) {
        totalBytes += bytesRead;
        buffer[totalBytes] = '\0';
        if (bufSize - totalBytes < 4096) {
            bufSize *= 2;
            char* newBuffer = (char*)realloc(buffer, bufSize);
            if (!newBuffer) { free(buffer); InternetCloseHandle(hUrl); InternetCloseHandle(hInternet); return NULL; }
            buffer = newBuffer;
        }
    }
    InternetCloseHandle(hUrl); InternetCloseHandle(hInternet);
    return buffer;
}

// --- UTILITY: String Parser ---
BOOL ParseGame(const char* json, const char* target, char* outPrimary, char* outExe) {
    const char* p = json;
    while (p && (p = strstr(p, "\"names\"")) != NULL) {
        p = strchr(p, '[');
        if (!p) break;
        const char* endArr = strchr(p, ']');
        if (!endArr) break;

        char primary[256] = { 0 };
        BOOL hasPrimary = FALSE, matchFound = FALSE;
        const char* strStart = p;

        while ((strStart = strchr(strStart, '"')) != NULL && strStart < endArr) {
            strStart++;
            const char* strEnd = strchr(strStart, '"');
            if (!strEnd || strEnd > endArr) break;

            size_t len = strEnd - strStart;
            if (len > 255) len = 255;
            char alias[256] = { 0 };
            strncpy(alias, strStart, len);

            if (!hasPrimary) { strcpy(primary, alias); hasPrimary = TRUE; }
            if (FuzzyCompare(alias, target)) matchFound = TRUE;
            strStart = strEnd + 1;
        }

        if (matchFound) {
            const char* nextObj = strstr(endArr, "\"names\"");
            const char* exeProp = strstr(endArr, "\"exe\"");
            if (exeProp && (!nextObj || exeProp < nextObj)) {
                const char* exeStart = strchr(exeProp + 5, '"');
                if (exeStart) {
                    exeStart++;
                    const char* exeEnd = strchr(exeStart, '"');
                    if (exeEnd) {
                        size_t exeLen = exeEnd - exeStart;
                        if (exeLen > 255) exeLen = 255;
                        memset(outExe, 0, 256);
                        strncpy(outExe, exeStart, exeLen);
                        strcpy(outPrimary, primary);
                        
                        char *read = outExe, *write = outExe;
                        while (*read) {
                            if (*read == '\\' && *(read + 1) == '\\') { *write++ = '\\'; read += 2; }
                            else { *write++ = *read++; }
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

// --- UTILITY: Trigger Next Queue or Self-Destruct ---
void ProcessQueueBaton() {
    char cmd[MAX_PATH * 3];
    if (qCurrent < qTotal) {
        // Find next game in queue file, update file, and launch
        FILE* f = fopen(queueFilePath, "r");
        if (f) {
            char fileData[8192] = { 0 };
            fread(fileData, 1, 8192, f);
            fclose(f);

            // Update CURRENT= in file string
            char searchStr[32]; sprintf(searchStr, "CURRENT=%d", qCurrent);
            char replaceStr[32]; sprintf(replaceStr, "CURRENT=%d", qCurrent + 1);
            char* pos = strstr(fileData, searchStr);
            if (pos) strncpy(pos, replaceStr, strlen(replaceStr));

            f = fopen(queueFilePath, "w");
            if (f) { fwrite(fileData, 1, strlen(fileData), f); fclose(f); }

            // Parse next game details
            char nextTarget[32]; sprintf(nextTarget, "\n%d|", qCurrent + 1);
            char* line = strstr(fileData, nextTarget);
            if (line) {
                line++; // skip newline
                char tIdx[16], pName[256], ePath[256], tSec[32];
                sscanf(line, "%[^|]|%[^|]|%[^|]|%s", tIdx, pName, ePath, tSec);

                char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
                char nextBaseDir[MAX_PATH]; sprintf(nextBaseDir, "%sDGE_%s", tempDir, pName);
                char nextExePath[MAX_PATH]; sprintf(nextExePath, "%s\\%s", nextBaseDir, ePath);

                char dirToCreate[MAX_PATH]; strcpy(dirToCreate, nextExePath);
                char* lastSlash = strrchr(dirToCreate, '\\');
                if (lastSlash) *lastSlash = '\0';
                SHCreateDirectoryExA(NULL, dirToCreate, NULL);

                char currentExe[MAX_PATH]; GetModuleFileNameA(NULL, currentExe, MAX_PATH);
                CopyFileA(currentExe, nextExePath, FALSE);

                char nextCmdLine[MAX_PATH * 3];
                sprintf(nextCmdLine, "\"%s\" -queue \"%s\"", nextExePath, queueFilePath);
                
                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                CreateProcessA(NULL, nextCmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
            }
        }
        // Self-Destruct JUST current folder
        sprintf(cmd, "/c ping 127.0.0.1 -n 2 > nul & rmdir /s /q \"%s\"", dgeFolderPath);
        ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
    } else {
        // Last Game: Self-Destruct folder, delete queue file, relaunch main
        sprintf(cmd, "/c ping 127.0.0.1 -n 2 > nul & rmdir /s /q \"%s\" & del /q \"%s\" & start \"\" \"%s\"", dgeFolderPath, queueFilePath, origExePath);
        ShellExecuteA(NULL, "open", "cmd.exe", cmd, NULL, SW_HIDE);
    }
}

// --- FONT HELPER ---
BOOL CALLBACK SetFontProc(HWND hwndChild, LPARAM lParam) {
    SendMessage(hwndChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// --- DUMMY APP WINDOW PROC ---
LRESULT CALLBACK DummyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        char qStr[64]; sprintf(qStr, "Game %d of %d", qCurrent, qTotal);
        
        hQueueStatusLabel = CreateWindowA("STATIC", qStr, WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 10, 240, 20, hwnd, NULL, NULL, NULL);
        
        hGameLabel = CreateWindowA("STATIC", currentGameName, WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 35, 240, 20, hwnd, NULL, NULL, NULL);
        
        hTimerLabel = CreateWindowA("STATIC", "Time Remaining: --", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 60, 240, 20, hwnd, NULL, NULL, NULL);
        hProgressLabel = CreateWindowA("STATIC", "Progress: 0%", WS_CHILD | WS_VISIBLE | SS_CENTER, 20, 85, 240, 20, hwnd, NULL, NULL, NULL);
        hBtnCancel = CreateWindowA("BUTTON", "Terminate", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 80, 120, 120, 30, hwnd, (HMENU)1, NULL, NULL);
        
        SetTimer(hwnd, 1, 1000, NULL);
        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);
        break;
    }
    case WM_TIMER: {
        timeLeft--;
        if (timeLeft <= 0) { PostMessage(hwnd, WM_CLOSE, 0, 0); return 0; }
        
        char buf[64];
        sprintf(buf, "Time Remaining: %dm %02ds", timeLeft / 60, timeLeft % 60);
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
        LPDRAWITEMSTRUCT p = (LPDRAWITEMSTRUCT)lParam;
        FillRect(p->hDC, &p->rcItem, hBtnBrush);
        SetBkMode(p->hDC, TRANSPARENT); SetTextColor(p->hDC, clrText);
        
        char btnText[32]; GetWindowTextA(p->hwndItem, btnText, 32);
        DrawTextA(p->hDC, btnText, -1, &p->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        return TRUE;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrBg); return (LRESULT)hBgBrush;
    case WM_CLOSE:
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        ProcessQueueBaton();
        PostQuitMessage(0);
        break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// --- QUEUE EXECUTION LOGIC (Called by Start Queue or Launch Game) ---
void ExecuteQueue(HWND hwnd, BOOL isSingleShot) {
    if (isSingleShot) {
        char gInput[256], cExe[256], tInput[32];
        GetWindowTextA(hGameName, gInput, 256); GetWindowTextA(hCustomExe, cExe, 256); GetWindowTextA(hTime, tInput, 32);
        const char* mathPtr = tInput; int t = EvalExpr(&mathPtr);
        if (t <= 0 || (strlen(gInput) == 0 && strlen(cExe) == 0)) {
            MessageBoxA(hwnd, "Please enter a Game Name or Custom EXE, and a valid Time.", "Error", MB_ICONERROR | MB_OK); return;
        }
        strcpy(queue[0].gameName, gInput); strcpy(queue[0].customExe, cExe); queue[0].timeSec = t;
        queueCount = 1;
    } else {
        if (queueCount == 0) { MessageBoxA(hwnd, "Queue is empty!", "Error", MB_ICONERROR | MB_OK); return; }
    }

    char* jPri = FetchJSON(JSON_URL_PRIMARY);
    char* jFall = FetchJSON(JSON_URL_FALLBACK);

    char currentExe[MAX_PATH]; GetModuleFileNameA(NULL, currentExe, MAX_PATH);
    char fileBuf[8192] = { 0 };
    sprintf(fileBuf, "TOTAL=%d\nCURRENT=1\nORIG_EXE=%s\n", queueCount, currentExe);

    for (int i = 0; i < queueCount; i++) {
        char primaryName[256] = { 0 }, exePath[256] = { 0 };
        BOOL found = FALSE;

        if (jPri) found = ParseGame(jPri, queue[i].gameName, primaryName, exePath);
        if (!found && jFall) found = ParseGame(jFall, queue[i].gameName, primaryName, exePath);

        if (strlen(queue[i].customExe) > 0) strcpy(exePath, queue[i].customExe);
        if (strlen(primaryName) == 0) strcpy(primaryName, strlen(queue[i].gameName) > 0 ? queue[i].gameName : "CustomGame");

        if (strlen(exePath) == 0) {
            char err[512]; sprintf(err, "Could not find path for game: '%s'. Check for spelling errors, or enter a custom EXE path.", queue[i].gameName);
            MessageBoxA(hwnd, err, "Error", MB_ICONERROR | MB_OK);
            if(jPri) free(jPri); if(jFall) free(jFall);
            if(isSingleShot) queueCount = 0; // reset
            return;
        }

        // Sanitize folder name
        char* r = primaryName; char* w = primaryName;
        while (*r) {
            if (*r != ':' && *r != ';' && *r != '<' && *r != '>' && *r != '"' && *r != '/' && *r != '\\' && *r != '|' && *r != '?' && *r != '*') {
                *w++ = *r;
            }
            r++;
        }
        *w = '\0';
        sprintf(fileBuf + strlen(fileBuf), "%d|%s|%s|%d\n", i + 1, primaryName, exePath, queue[i].timeSec);
    }
    if(jPri) free(jPri); if(jFall) free(jFall);

    // Save dge_queue.txt
    char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
    sprintf(queueFilePath, "%sdge_queue.txt", tempDir);
    FILE* f = fopen(queueFilePath, "w");
    if (f) { fwrite(fileBuf, 1, strlen(fileBuf), f); fclose(f); }

    // Read details for Game 1 to launch the chain
    char pName[256], ePath[256];
    char searchStr[16]; sprintf(searchStr, "\n1|");
    char* line1 = strstr(fileBuf, searchStr);
    sscanf(line1 + 3, "%[^|]|%[^|]", pName, ePath);

    char baseDgeFolder[MAX_PATH]; sprintf(baseDgeFolder, "%sDGE_%s", tempDir, pName);
    char fullExePath[MAX_PATH]; sprintf(fullExePath, "%s\\%s", baseDgeFolder, ePath);

    char dirToCreate[MAX_PATH]; strcpy(dirToCreate, fullExePath);
    char* lastSlash = strrchr(dirToCreate, '\\');
    if (lastSlash) *lastSlash = '\0';
    SHCreateDirectoryExA(NULL, dirToCreate, NULL);

    CopyFileA(currentExe, fullExePath, FALSE);

    char cmdLine[MAX_PATH * 3];
    sprintf(cmdLine, "\"%s\" -queue \"%s\"", fullExePath, queueFilePath);

    STARTUPINFOA si = { sizeof(si) }; PROCESS_INFORMATION pi;
    if (CreateProcessA(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
        PostQuitMessage(0); // Exit main
    } else {
        MessageBoxA(hwnd, "Failed to launch game 1.", "Error", MB_ICONERROR | MB_OK);
    }
}

// --- MAIN APP WINDOW PROC ---
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowA("STATIC", "Game Name (or alias):", WS_CHILD | WS_VISIBLE, 20, 20, 200, 20, hwnd, NULL, NULL, NULL);
        hGameName = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 20, 40, 240, 22, hwnd, NULL, NULL, NULL);

        CreateWindowA("STATIC", "Custom EXE Path (Optional):", WS_CHILD | WS_VISIBLE, 20, 70, 200, 20, hwnd, NULL, NULL, NULL);
        hCustomExe = CreateWindowA("EDIT", "", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 20, 90, 240, 22, hwnd, NULL, NULL, NULL);

        CreateWindowA("STATIC", "Time (Seconds or Math):", WS_CHILD | WS_VISIBLE, 20, 120, 200, 20, hwnd, NULL, NULL, NULL);
        hTime = CreateWindowA("EDIT", "910", WS_CHILD | WS_VISIBLE | WS_BORDER, 20, 140, 100, 22, hwnd, NULL, NULL, NULL);

        hBtnLaunch = CreateWindowA("BUTTON", "Emulate", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 80, 180, 120, 30, hwnd, (HMENU)2, NULL, NULL);
        hBtnToggleQueue = CreateWindowA("BUTTON", "", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 255, 10, 25, 25, hwnd, (HMENU)3, NULL, NULL);
        char* verData = FetchJSON(VERSION_URL); 
        BOOL updateFound = FALSE;
        if (verData) {
            float remoteVer;
            if (sscanf(verData, "%f\n%s", &remoteVer, updateUrl) == 2) {
                if (remoteVer > APP_VERSION) updateFound = TRUE;
            }
            free(verData);
        }
        hBtnUpdate = CreateWindowA("BUTTON", "", WS_CHILD | (updateFound ? WS_VISIBLE : 0) | BS_OWNERDRAW, 225, 10, 25, 25, hwnd, (HMENU)7, NULL, NULL);
        hQueueLabel = CreateWindowA("STATIC", "Session Queue:", WS_CHILD, 310, 20, 200, 20, hwnd, NULL, NULL, NULL);
        hListBox = CreateWindowA("LISTBOX", NULL, WS_CHILD | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 310, 40, 250, 122, hwnd, NULL, NULL, NULL);
        hBtnAddQueue = CreateWindowA("BUTTON", "Add to Queue", WS_CHILD | BS_OWNERDRAW, 80, 180, 120, 30, hwnd, (HMENU)4, NULL, NULL);
        hBtnRemoveQueue = CreateWindowA("BUTTON", "Remove", WS_CHILD | BS_OWNERDRAW, 310, 180, 90, 30, hwnd, (HMENU)5, NULL, NULL);
        hBtnStartQueue = CreateWindowA("BUTTON", "Start Queue", WS_CHILD | BS_OWNERDRAW, 460, 180, 100, 30, hwnd, (HMENU)6, NULL, NULL);

        EnumChildWindows(hwnd, SetFontProc, (LPARAM)hFont);
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == 2) { ExecuteQueue(hwnd, TRUE); } // Single Launch
        else if (id == 6) { ExecuteQueue(hwnd, FALSE); } // Start Queue
        else if (id == 3) { // Toggle Mode
            isQueueMode = !isQueueMode;
            SetWindowPos(hwnd, NULL, 0, 0, isQueueMode ? 600 : 300, 280, SWP_NOMOVE | SWP_NOZORDER);
            ShowWindow(hBtnLaunch, isQueueMode ? SW_HIDE : SW_SHOW);
            ShowWindow(hBtnAddQueue, isQueueMode ? SW_SHOW : SW_HIDE);
            ShowWindow(hListBox, isQueueMode ? SW_SHOW : SW_HIDE);
            ShowWindow(hQueueLabel, isQueueMode ? SW_SHOW : SW_HIDE);
            ShowWindow(hBtnRemoveQueue, isQueueMode ? SW_SHOW : SW_HIDE);
            ShowWindow(hBtnStartQueue, isQueueMode ? SW_SHOW : SW_HIDE);
            InvalidateRect(hwnd, NULL, TRUE);
        }
        else if (id == 4) { // Add to Queue
            if (queueCount >= 100) return 0;
            char gInput[256], cExe[256], tInput[32];
            GetWindowTextA(hGameName, gInput, 256); GetWindowTextA(hCustomExe, cExe, 256); GetWindowTextA(hTime, tInput, 32);
            
            const char* mathPtr = tInput; int t = EvalExpr(&mathPtr);
            if (t <= 0 || (strlen(gInput) == 0 && strlen(cExe) == 0)) {
                MessageBoxA(hwnd, "Invalid Entry.", "Error", MB_ICONERROR | MB_OK); break;
            }

            strcpy(queue[queueCount].gameName, gInput);
            strcpy(queue[queueCount].customExe, cExe);
            queue[queueCount].timeSec = t;
            queueCount++;

            char listStr[300]; 
            char* dispName = strlen(gInput) > 0 ? gInput : cExe;
            sprintf(listStr, "[%dm %02ds] %s", t / 60, t % 60, dispName);
            SendMessageA(hListBox, LB_ADDSTRING, 0, (LPARAM)listStr);

            SetWindowTextA(hGameName, ""); SetWindowTextA(hCustomExe, "");
        }
        else if (id == 5) { // Remove from Queue
            int sel = SendMessageA(hListBox, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                SendMessageA(hListBox, LB_DELETESTRING, sel, 0);
                for (int i = sel; i < queueCount - 1; i++) queue[i] = queue[i + 1];
                queueCount--;
            }
        }
        else if (id == 7) { // Update Button Clicked
            if (MessageBoxA(hwnd, "A new update is available! Do you want to download and restart the app?", "Update Available", MB_ICONQUESTION | MB_YESNO) == IDYES) {
                
                // Get current exe name
                char currentExe[MAX_PATH]; GetModuleFileNameA(NULL, currentExe, MAX_PATH);
                
                // Set temporary update download name
                char updateExe[MAX_PATH]; strcpy(updateExe, currentExe);
                char* lastSlash = strrchr(updateExe, '\\');
                if (lastSlash) strcpy(lastSlash + 1, "DGE_UpdateTemp.exe");

                // Generate the hidden CMD script
                char cmdStr[2048];
                sprintf(cmdStr, "/c curl -s -L -o \"%s\" \"%s\" & ping 127.0.0.1 -n 2 > nul & move /y \"%s\" \"%s\" & start \"\" \"%s\"",
                    updateExe, updateUrl, updateExe, currentExe, currentExe);

                // Launch terminal and kill the current app immediately!
                ShellExecuteA(NULL, "open", "cmd.exe", cmdStr, NULL, SW_HIDE);
                PostQuitMessage(0); 
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrBg); return (LRESULT)hBgBrush;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        SetTextColor((HDC)wParam, clrText); SetBkColor((HDC)wParam, clrEditBg); return (LRESULT)hEditBrush;
    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT p = (LPDRAWITEMSTRUCT)lParam;
        FillRect(p->hDC, &p->rcItem, hBtnBrush); // Draw button background

        if (p->CtlID == 7) {
            // Draw Green Download Arrow for Update Button
            HPEN hPen = CreatePen(PS_SOLID, 2, RGB(50, 220, 50)); // Bright Green
            HPEN hOldPen = SelectObject(p->hDC, hPen);
            
            MoveToEx(p->hDC, 12, 6, NULL); LineTo(p->hDC, 12, 16); // Arrow Stem
            MoveToEx(p->hDC, 7, 11, NULL); LineTo(p->hDC, 13, 17); // Left Flap
            MoveToEx(p->hDC, 17, 11, NULL); LineTo(p->hDC, 11, 17); // Right Flap
            MoveToEx(p->hDC, 6, 19, NULL); LineTo(p->hDC, 19, 19); // Base Line
            
            SelectObject(p->hDC, hOldPen);
            DeleteObject(hPen);
        }
        else if (p->CtlID == 3) {
            // Draw Hamburger Menu for Queue Button
            HBRUSH hIconBrush = CreateSolidBrush(clrText);
            RECT line1 = { 6, 7, 19, 9 };
            RECT line2 = { 6, 12, 19, 14 };
            RECT line3 = { 6, 17, 19, 19 };
            FillRect(p->hDC, &line1, hIconBrush); FillRect(p->hDC, &line2, hIconBrush); FillRect(p->hDC, &line3, hIconBrush);
            DeleteObject(hIconBrush);
        } else {
            // Draw Standard Text for all other buttons
            SetBkMode(p->hDC, TRANSPARENT); 
            SetTextColor(p->hDC, clrText);
            char btnText[32]; GetWindowTextA(p->hwndItem, btnText, 32);
            DrawTextA(p->hDC, btnText, -1, &p->rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        }
        return TRUE;
    }
    case WM_DESTROY: PostQuitMessage(0); break;
    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int showCmd) {
    hBgBrush = CreateSolidBrush(clrBg);
    hBtnBrush = CreateSolidBrush(RGB(70, 70, 70));
    hEditBrush = CreateSolidBrush(clrEditBg);
    hFont = CreateFontA(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    int argc; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    BOOL isDummy = FALSE;

    if (argc >= 3) {
        char arg1[32]; WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, arg1, 32, NULL, NULL);
        if (strcmp(arg1, "-queue") == 0) {
            isDummy = TRUE;
            WideCharToMultiByte(CP_UTF8, 0, argv[2], -1, queueFilePath, MAX_PATH, NULL, NULL);
            
            // Read Dummy Data from Queue File
            FILE* f = fopen(queueFilePath, "r");
            if (f) {
                char fileData[8192] = { 0 }; fread(fileData, 1, 8192, f); fclose(f);
                char* pTotal = strstr(fileData, "TOTAL="); if (pTotal) qTotal = atoi(pTotal + 6);
                char* pCur = strstr(fileData, "CURRENT="); if (pCur) qCurrent = atoi(pCur + 8);
                char* pOrig = strstr(fileData, "ORIG_EXE="); if (pOrig) sscanf(pOrig + 9, "%[^\n]", origExePath);

                char targetLine[32]; sprintf(targetLine, "\n%d|", qCurrent);
                char* line = strstr(fileData, targetLine);
                if (line) {
                    char tIdx[16], pName[256], ePath[256], tSec[32];
                    sscanf(line + 1, "%[^|]|%[^|]|%[^|]|%s", tIdx, pName, ePath, tSec);
                    
                    strcpy(currentGameName, pName);

                    totalTime = atoi(tSec); timeLeft = totalTime;
                    
                    char tempDir[MAX_PATH]; GetTempPathA(MAX_PATH, tempDir);
                    sprintf(dgeFolderPath, "%sDGE_%s", tempDir, pName);
                }
            }
        }
    }
    LocalFree(argv);

    WNDCLASSA wc = { 0 };
    wc.lpfnWndProc = isDummy ? DummyWndProc : MainWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconA(hInst, MAKEINTRESOURCEA(101));
    wc.hbrBackground = hBgBrush;
    wc.lpszClassName = isDummy ? "DgeDummyClass" : "DgeMainClass";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(wc.lpszClassName, isDummy ? "Game Session" : "Discord Game Emulator",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, isDummy ? 200 : 280, NULL, NULL, hInst, NULL);

    int useImmersiveDarkMode = 1;
    DwmSetWindowAttribute(hwnd, 20, &useImmersiveDarkMode, sizeof(useImmersiveDarkMode));

    ShowWindow(hwnd, showCmd); UpdateWindow(hwnd);

    MSG msg; 
    while (GetMessage(&msg, NULL, 0, 0)) { 
        // Intercept 'Enter' key in the Main Window
        if (!isDummy && msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageA(hwnd, WM_COMMAND, isQueueMode ? 4 : 2, 0);
        }
        TranslateMessage(&msg); 
        DispatchMessage(&msg); 
    }

    DeleteObject(hBgBrush); DeleteObject(hBtnBrush); DeleteObject(hEditBrush); DeleteObject(hFont);
    return 0;
}
