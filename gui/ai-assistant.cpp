// NeuralXP - Offline AI for Windows XP
// Built by Panmox (panmox.org)
// Pure Win32 API - looks native on XP Luna theme

#define _WIN32_WINNT 0x0501
#define WINVER 0x0501
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#pragma comment(lib, "comctl32.lib")

// Menu IDs
#define IDM_FILE_NEWCHAT    1001
#define IDM_FILE_SAVECHAT   1002
#define IDM_FILE_EXIT       1003
#define IDM_EDIT_COPY       1004
#define IDM_EDIT_SELECTALL  1005
#define IDM_HELP_ABOUT      1006
#define IDM_HELP_TIPS       1007
#define IDM_FILE_OPENCHAT   1008

// Icon resource IDs
#define IDI_NEURALXP     1

// Control IDs
#define ID_CHAT_HISTORY  101
#define ID_INPUT_BOX     102
#define ID_SEND_BUTTON   103
#define ID_STATUS_BAR    104
#define ID_CLEAR_BUTTON  105
#define ID_TOOLBAR       106
#define ID_HEADER_STATIC 107

// Toolbar button IDs
#define ID_TB_NEWCHAT    201
#define ID_TB_SAVE       202
#define ID_TB_ABOUT      203

// Chat list dialog
#define IDD_CHATLIST     300
#define IDC_CHATLISTBOX  301
#define IDC_CHATDELETE   302

// Window dimensions
#define WIN_WIDTH   540
#define WIN_HEIGHT  500

// Max chats and name length
#define MAX_CHAT_NAME 64
#define MAX_CHATS 100

// XP Luna colors
#define XP_BLUE_DARK      RGB(0, 56, 168)
#define XP_BLUE_LIGHT     RGB(0, 84, 227)
#define XP_BLUE_HEADER    RGB(51, 94, 168)
#define XP_BG_GRAY        RGB(236, 233, 216)
#define XP_BG_LIGHT       RGB(246, 244, 236)
#define XP_BORDER_DARK     RGB(172, 168, 153)
#define XP_WHITE           RGB(255, 255, 255)
#define XP_TEXT_DARK       RGB(0, 0, 0)
#define XP_TEXT_GRAY       RGB(112, 112, 112)
#define XP_TEXT_BLUE       RGB(0, 51, 153)
#define XP_CHAT_USER_BG    RGB(232, 242, 254)
#define XP_CHAT_AI_BG      RGB(255, 255, 255)
#define XP_GREEN_STATUS    RGB(0, 128, 0)
#define XP_HEADER_TOP      RGB(0, 88, 208)
#define XP_HEADER_BOT      RGB(4, 48, 136)

// Global handles
static HWND g_hWnd = NULL;
static HWND g_hChatHistory = NULL;
static HWND g_hInputBox = NULL;
static HWND g_hSendButton = NULL;
static HWND g_hClearButton = NULL;
static HWND g_hStatusBar = NULL;
static HWND g_hHeaderPanel = NULL;
static HWND g_hLabelPanel = NULL;
static HFONT g_hFontUI = NULL;
static HFONT g_hFontChat = NULL;
static HFONT g_hFontBold = NULL;
static HFONT g_hFontHeader = NULL;
static HFONT g_hFontHeaderSub = NULL;
static HFONT g_hFontInput = NULL;
static HBRUSH g_hBrushBg = NULL;
static HBRUSH g_hBrushWhite = NULL;
static HBRUSH g_hBrushHeader = NULL;
static HMENU g_hMenu = NULL;

// State
static BOOL g_bGenerating = FALSE;
static HANDLE g_hThread = NULL;
static char g_szModelPath[MAX_PATH] = {0};
static char g_szExeDir[MAX_PATH] = {0};
static char g_szChatsDir[MAX_PATH] = {0};
static int g_nMessageCount = 0;
static char g_szCurrentChat[MAX_CHAT_NAME] = {0};  // current chat name (empty = unsaved)
static char g_szChatContext[300] = {0};  // loaded context from previous session - small to fit in 256-token window
static char g_szChatFullHistory[8192] = {0};  // full saved chat for display only

// ---- Chat file helpers ----

// Make sure chats/ folder exists
static void EnsureChatsDir(void) {
    snprintf(g_szChatsDir, MAX_PATH, "%s\\chats", g_szExeDir);
    CreateDirectoryA(g_szChatsDir, NULL);
}

// Build path to a chat file
static void GetChatFilePath(char *out, int outLen, const char *chatName) {
    snprintf(out, outLen, "%s\\%s.txt", g_szChatsDir, chatName);
}

// Sanitize chat name (remove bad chars for filename)
static void SanitizeName(char *name) {
    for (int i = 0; name[i]; i++) {
        char c = name[i];
        if (c == '\\' || c == '/' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            name[i] = '_';
        }
    }
    // Trim trailing spaces
    int len = strlen(name);
    while (len > 0 && name[len-1] == ' ') name[--len] = '\0';
}

// Check if a line is "filler" (not worth saving)
static BOOL IsFillerLine(const char *line) {
    // Skip whitespace
    while (*line == ' ' || *line == '\t') line++;
    int len = strlen(line);
    if (len < 4) return TRUE;  // too short = filler

    // Common filler phrases (case insensitive check)
    const char *fillers[] = {
        "hi", "hello", "hey", "ok", "okay", "thanks", "thank you",
        "bye", "goodbye", "yes", "no", "sure", "cool", "nice",
        "how are you", "how can i help", "how can i assist",
        "welcome to neuralxp", "type your question",
        "____", "========", NULL
    };

    char lower[256] = {0};
    for (int i = 0; i < 255 && line[i]; i++) {
        lower[i] = (line[i] >= 'A' && line[i] <= 'Z') ? line[i] + 32 : line[i];
    }

    for (int i = 0; fillers[i]; i++) {
        if (strcmp(lower, fillers[i]) == 0) return TRUE;
        // Also check if the line starts with the filler and is short
        if (strstr(lower, fillers[i]) == lower && len < (int)strlen(fillers[i]) + 5) return TRUE;
    }
    return FALSE;
}

// Save important lines from current chat to file
static void AutoSaveChat(void) {
    if (g_szCurrentChat[0] == '\0') return;  // no chat name, don't save

    int textLen = GetWindowTextLengthA(g_hChatHistory);
    if (textLen <= 0) return;

    char *allText = (char *)malloc(textLen + 1);
    if (!allText) return;
    GetWindowTextA(g_hChatHistory, allText, textLen + 1);

    char filepath[MAX_PATH];
    GetChatFilePath(filepath, MAX_PATH, g_szCurrentChat);

    FILE *f = fopen(filepath, "w");
    if (!f) { free(allText); return; }

    // Write chat name header
    fprintf(f, "[NeuralXP Chat: %s]\r\n", g_szCurrentChat);

    // Parse line by line, save important ones
    char *line = strtok(allText, "\r\n");
    while (line) {
        // Keep "You:" and "NeuralXP:" lines that aren't filler
        if (strncmp(line, "You:  ", 6) == 0) {
            const char *content = line + 6;
            if (!IsFillerLine(content)) {
                fprintf(f, "%s\r\n", line);
            }
        } else if (strncmp(line, "NeuralXP:  ", 11) == 0) {
            const char *content = line + 11;
            if (!IsFillerLine(content)) {
                fprintf(f, "%s\r\n", line);
            }
        }
        line = strtok(NULL, "\r\n");
    }

    fclose(f);
    free(allText);
}

// Load chat context from file
// - g_szChatFullHistory gets full text (for display)
// - g_szChatContext gets a tiny summary of LAST few user messages only (for AI prompt)
static void LoadChatContext(const char *chatName) {
    char filepath[MAX_PATH];
    GetChatFilePath(filepath, MAX_PATH, chatName);

    g_szChatContext[0] = '\0';
    g_szChatFullHistory[0] = '\0';

    FILE *f = fopen(filepath, "r");
    if (!f) return;

    // Buffer all lines first so we can pick last few
    char lines[50][256];  // max 50 lines, 256 chars each
    int lineCount = 0;

    char line[512];
    fgets(line, sizeof(line), f);  // skip header

    int fullLen = 0;
    while (fgets(line, sizeof(line), f) && lineCount < 50) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\r' || line[len-1] == '\n')) line[--len] = '\0';
        if (len == 0) continue;

        // Save to full history for display
        if (fullLen + len + 3 < (int)sizeof(g_szChatFullHistory)) {
            strcpy(g_szChatFullHistory + fullLen, line);
            fullLen += len;
            g_szChatFullHistory[fullLen++] = '\r';
            g_szChatFullHistory[fullLen++] = '\n';
            g_szChatFullHistory[fullLen] = '\0';
        }

        // Save to lines array for context picking
        strncpy(lines[lineCount], line, 255);
        lines[lineCount][255] = '\0';
        lineCount++;
    }
    fclose(f);

    // Build tiny context: only last 2 user messages, compact
    int contextLen = 0;
    int userCount = 0;
    for (int i = lineCount - 1; i >= 0 && userCount < 2; i--) {
        if (strncmp(lines[i], "You:  ", 6) == 0) {
            const char *content = lines[i] + 6;
            int clen = strlen(content);
            if (clen > 80) clen = 80;  // truncate long messages
            if (contextLen + clen + 4 < (int)sizeof(g_szChatContext)) {
                if (contextLen > 0) {
                    g_szChatContext[contextLen++] = ';';
                    g_szChatContext[contextLen++] = ' ';
                }
                strncpy(g_szChatContext + contextLen, content, clen);
                contextLen += clen;
                g_szChatContext[contextLen] = '\0';
                userCount++;
            }
        }
    }
}

// Update window title with chat name
static void UpdateTitle(void) {
    char title[256];
    if (g_szCurrentChat[0]) {
        snprintf(title, sizeof(title), "NeuralXP - %s", g_szCurrentChat);
    } else {
        snprintf(title, sizeof(title), "NeuralXP - Offline AI for Windows XP");
    }
    SetWindowTextA(g_hWnd, title);
}

// ---- Chat list dialog ----

// List all .txt files in chats/ folder
static int ListChats(char chatNames[][MAX_CHAT_NAME], int maxChats) {
    char searchPath[MAX_PATH];
    snprintf(searchPath, MAX_PATH, "%s\\*.txt", g_szChatsDir);

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(searchPath, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    int count = 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            // Remove .txt extension for display
            char name[MAX_CHAT_NAME];
            strncpy(name, fd.cFileName, MAX_CHAT_NAME - 1);
            name[MAX_CHAT_NAME - 1] = '\0';
            char *dot = strrchr(name, '.');
            if (dot) *dot = '\0';
            strncpy(chatNames[count], name, MAX_CHAT_NAME);
            count++;
            if (count >= maxChats) break;
        }
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
    return count;
}

// Dialog proc for Open Chat dialog
static char g_szSelectedChat[MAX_CHAT_NAME] = {0};

static BOOL CALLBACK OpenChatDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG: {
        // Center dialog on parent
        RECT rcParent, rcDlg;
        GetWindowRect(g_hWnd, &rcParent);
        GetWindowRect(hDlg, &rcDlg);
        int x = rcParent.left + ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcParent.top + ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // Populate listbox
        HWND hList = GetDlgItem(hDlg, IDC_CHATLISTBOX);
        SendMessage(hList, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        char chatNames[MAX_CHATS][MAX_CHAT_NAME];
        int count = ListChats(chatNames, MAX_CHATS);

        if (count == 0) {
            SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"(no saved chats)");
            EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
            EnableWindow(GetDlgItem(hDlg, IDC_CHATDELETE), FALSE);
        } else {
            for (int i = 0; i < count; i++) {
                SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)chatNames[i]);
            }
            SendMessage(hList, LB_SETCURSEL, 0, 0);
        }
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            HWND hList = GetDlgItem(hDlg, IDC_CHATLISTBOX);
            int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                SendMessageA(hList, LB_GETTEXT, sel, (LPARAM)g_szSelectedChat);
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDC_CHATDELETE: {
            HWND hList = GetDlgItem(hDlg, IDC_CHATLISTBOX);
            int sel = SendMessage(hList, LB_GETCURSEL, 0, 0);
            if (sel != LB_ERR) {
                char name[MAX_CHAT_NAME];
                SendMessageA(hList, LB_GETTEXT, sel, (LPARAM)name);

                char msg2[256];
                snprintf(msg2, sizeof(msg2), "Delete chat \"%s\"?", name);
                if (MessageBoxA(hDlg, msg2, "Delete Chat", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    char filepath[MAX_PATH];
                    GetChatFilePath(filepath, MAX_PATH, name);
                    DeleteFileA(filepath);
                    SendMessage(hList, LB_DELETESTRING, sel, 0);
                    if (SendMessage(hList, LB_GETCOUNT, 0, 0) == 0) {
                        SendMessageA(hList, LB_ADDSTRING, 0, (LPARAM)"(no saved chats)");
                        EnableWindow(GetDlgItem(hDlg, IDOK), FALSE);
                        EnableWindow(GetDlgItem(hDlg, IDC_CHATDELETE), FALSE);
                    } else {
                        SendMessage(hList, LB_SETCURSEL, 0, 0);
                    }
                }
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// Build dialog template in memory (no .rc file needed - XP compatible)
static INT_PTR ShowOpenChatDialog(void) {
    // We build a DLGTEMPLATE in memory with controls
    // This avoids needing a .rc resource file
    // DWORD type ensures 4-byte alignment required by DialogBoxIndirect
    DWORD dlgBuf[256];
    memset(dlgBuf, 0, sizeof(dlgBuf));

    LPDLGTEMPLATE pDlg = (LPDLGTEMPLATE)dlgBuf;
    pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    pDlg->cdit = 4;  // 4 controls: listbox, Open, Delete, Cancel
    pDlg->cx = 160;
    pDlg->cy = 140;

    // Dialog title
    WORD *p = (WORD *)(pDlg + 1);
    *p++ = 0; // menu
    *p++ = 0; // class

    // Title: "Open Chat"
    const char *title = "Open Chat";
    for (int i = 0; title[i]; i++) *p++ = (WORD)title[i];
    *p++ = 0;

    // Align to DWORD
    if (((ULONG_PTR)p) % 4) p++;

    // Control 1: Listbox
    DLGITEMTEMPLATE *pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | WS_TABSTOP;
    pItem->x = 8; pItem->y = 8; pItem->cx = 144; pItem->cy = 90;
    pItem->id = IDC_CHATLISTBOX;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0083;  // Listbox class
    *p++ = 0;  // no text
    *p++ = 0;  // no extra data
    if (((ULONG_PTR)p) % 4) p++;

    // Control 2: Open button
    pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP;
    pItem->x = 8; pItem->y = 104; pItem->cx = 42; pItem->cy = 14;
    pItem->id = IDOK;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;  // Button class
    const char *openTxt = "Open";
    for (int i = 0; openTxt[i]; i++) *p++ = (WORD)openTxt[i];
    *p++ = 0;
    *p++ = 0;
    if (((ULONG_PTR)p) % 4) p++;

    // Control 3: Delete button
    pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
    pItem->x = 56; pItem->y = 104; pItem->cx = 42; pItem->cy = 14;
    pItem->id = IDC_CHATDELETE;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    const char *delTxt = "Delete";
    for (int i = 0; delTxt[i]; i++) *p++ = (WORD)delTxt[i];
    *p++ = 0;
    *p++ = 0;
    if (((ULONG_PTR)p) % 4) p++;

    // Control 4: Cancel button
    pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
    pItem->x = 110; pItem->y = 104; pItem->cx = 42; pItem->cy = 14;
    pItem->id = IDCANCEL;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    const char *canTxt = "Cancel";
    for (int i = 0; canTxt[i]; i++) *p++ = (WORD)canTxt[i];
    *p++ = 0;
    *p++ = 0;

    return DialogBoxIndirectA(GetModuleHandle(NULL), pDlg, g_hWnd, OpenChatDlgProc);
}

// ---- Name input dialog ----

static char g_szNewChatName[MAX_CHAT_NAME] = {0};

static BOOL CALLBACK NameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    (void)lParam;
    switch (msg) {
    case WM_INITDIALOG: {
        RECT rcParent, rcDlg;
        GetWindowRect(g_hWnd, &rcParent);
        GetWindowRect(hDlg, &rcDlg);
        int x = rcParent.left + ((rcParent.right - rcParent.left) - (rcDlg.right - rcDlg.left)) / 2;
        int y = rcParent.top + ((rcParent.bottom - rcParent.top) - (rcDlg.bottom - rcDlg.top)) / 2;
        SetWindowPos(hDlg, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        HWND hEdit = GetDlgItem(hDlg, 400);
        SendMessage(hEdit, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        SendMessage(hEdit, EM_SETLIMITTEXT, MAX_CHAT_NAME - 1, 0);
        SetFocus(hEdit);
        return FALSE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            GetDlgItemTextA(hDlg, 400, g_szNewChatName, MAX_CHAT_NAME);
            SanitizeName(g_szNewChatName);
            if (g_szNewChatName[0] == '\0') {
                MessageBoxA(hDlg, "Please enter a chat name.", "NeuralXP", MB_OK);
                return TRUE;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static INT_PTR ShowNameDialog(void) {
    DWORD dlgBuf[256];
    memset(dlgBuf, 0, sizeof(dlgBuf));

    LPDLGTEMPLATE pDlg = (LPDLGTEMPLATE)dlgBuf;
    pDlg->style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_CENTER;
    pDlg->cdit = 4;  // label, edit, OK, Cancel
    pDlg->cx = 180;
    pDlg->cy = 60;

    WORD *p = (WORD *)(pDlg + 1);
    *p++ = 0; *p++ = 0;
    const char *title = "New Chat";
    for (int i = 0; title[i]; i++) *p++ = (WORD)title[i];
    *p++ = 0;
    if (((ULONG_PTR)p) % 4) p++;

    // Label
    DLGITEMTEMPLATE *pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | SS_LEFT;
    pItem->x = 8; pItem->y = 8; pItem->cx = 164; pItem->cy = 10;
    pItem->id = 0xFFFF;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0082;  // Static class
    const char *lbl = "Chat name:";
    for (int i = 0; lbl[i]; i++) *p++ = (WORD)lbl[i];
    *p++ = 0; *p++ = 0;
    if (((ULONG_PTR)p) % 4) p++;

    // Edit box
    pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL;
    pItem->x = 8; pItem->y = 20; pItem->cx = 164; pItem->cy = 14;
    pItem->id = 400;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0081;  // Edit class
    *p++ = 0; *p++ = 0;
    if (((ULONG_PTR)p) % 4) p++;

    // OK
    pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP;
    pItem->x = 76; pItem->y = 40; pItem->cx = 42; pItem->cy = 14;
    pItem->id = IDOK;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    const char *okTxt = "OK";
    for (int i = 0; okTxt[i]; i++) *p++ = (WORD)okTxt[i];
    *p++ = 0; *p++ = 0;
    if (((ULONG_PTR)p) % 4) p++;

    // Cancel
    pItem = (DLGITEMTEMPLATE *)p;
    pItem->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
    pItem->x = 126; pItem->y = 40; pItem->cx = 42; pItem->cy = 14;
    pItem->id = IDCANCEL;
    p = (WORD *)(pItem + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    const char *canTxt = "Cancel";
    for (int i = 0; canTxt[i]; i++) *p++ = (WORD)canTxt[i];
    *p++ = 0; *p++ = 0;

    return DialogBoxIndirectA(GetModuleHandle(NULL), pDlg, g_hWnd, NameDlgProc);
}

// Find model
static BOOL FindModel(void) {
    char path[MAX_PATH];
    snprintf(path, MAX_PATH, "%s\\models\\qwen2.5-0.5b-instruct-q2_k.gguf", g_szExeDir);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        strncpy(g_szModelPath, path, MAX_PATH);
        return TRUE;
    }
    snprintf(path, MAX_PATH, "%s\\models\\SmolLM-135M-Instruct.Q4_K_M.gguf", g_szExeDir);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        strncpy(g_szModelPath, path, MAX_PATH);
        return TRUE;
    }
    return FALSE;
}

// Append text to chat
static void AppendChat(const char *text) {
    int len = GetWindowTextLengthA(g_hChatHistory);
    SendMessageA(g_hChatHistory, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    SendMessageA(g_hChatHistory, EM_REPLACESEL, FALSE, (LPARAM)text);
    SendMessageA(g_hChatHistory, EM_SCROLLCARET, 0, 0);
}

static void AppendChatLine(const char *text) {
    int len = GetWindowTextLengthA(g_hChatHistory);
    if (len > 0) {
        AppendChat("\r\n");
    }
    AppendChat(text);
}

static void SetStatus(const char *text) {
    SendMessageA(g_hStatusBar, SB_SETTEXTA, 0, (LPARAM)text);
}

static void UpdateMessageCount(void) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Messages: %d", g_nMessageCount);
    SendMessageA(g_hStatusBar, SB_SETTEXTA, 1, (LPARAM)buf);
}

// Thread for inference
struct InferenceArgs {
    char prompt[2048];
};

static DWORD WINAPI InferenceThread(LPVOID lpParam) {
    struct InferenceArgs *args = (struct InferenceArgs *)lpParam;

    char cmdline[4096];
    // Escape quotes in prompt
    char escaped[2048] = {0};
    int j = 0;
    for (int i = 0; args->prompt[i] && j < 2040; i++) {
        if (args->prompt[i] == '"') {
            escaped[j++] = '\\';
            escaped[j++] = '"';
        } else {
            escaped[j++] = args->prompt[i];
        }
    }
    escaped[j] = '\0';

    // If we have chat context, prepend it briefly (kept very short to fit in 256-token window)
    if (g_szChatContext[0]) {
        char fullPrompt[1024];
        // Re-escape context too (could contain quotes)
        char ctxEscaped[512] = {0};
        int k = 0;
        for (int i = 0; g_szChatContext[i] && k < 500; i++) {
            if (g_szChatContext[i] == '"') {
                ctxEscaped[k++] = '\\';
                ctxEscaped[k++] = '"';
            } else {
                ctxEscaped[k++] = g_szChatContext[i];
            }
        }
        ctxEscaped[k] = '\0';
        snprintf(fullPrompt, sizeof(fullPrompt),
            "(Earlier: %s) %s", ctxEscaped, escaped);
        snprintf(cmdline, sizeof(cmdline),
            "\"%s\\llama-simple-chat-headless.exe\" -m \"%s\" -c 256 -n 80 -p \"%s\"",
            g_szExeDir, g_szModelPath, fullPrompt);
    } else {
        snprintf(cmdline, sizeof(cmdline),
            "\"%s\\llama-simple-chat-headless.exe\" -m \"%s\" -c 256 -n 80 -p \"%s\"",
            g_szExeDir, g_szModelPath, escaped);
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    HANDLE hReadPipe, hWritePipe;
    CreatePipe(&hReadPipe, &hWritePipe, &sa, 0);
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, g_szExeDir, &si, &pi)) {
        AppendChatLine("");
        AppendChat("NeuralXP: Sorry, I couldn't start the inference engine.");
        MessageBeep(MB_ICONHAND);
        SetStatus("Error - inference engine not found");
        g_bGenerating = FALSE;
        EnableWindow(g_hSendButton, TRUE);
        EnableWindow(g_hInputBox, TRUE);
        SetFocus(g_hInputBox);
        free(args);
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return 1;
    }

    CloseHandle(hWritePipe);

    // Read output
    char buf[2];
    DWORD bytesRead;
    BOOL firstChar = TRUE;

    AppendChatLine("");
    AppendChat("NeuralXP:  ");

    while (ReadFile(hReadPipe, buf, 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[1] = '\0';
        if (firstChar && (buf[0] == ' ' || buf[0] == '\n' || buf[0] == '\r')) continue;
        firstChar = FALSE;

        int len = GetWindowTextLengthA(g_hChatHistory);
        SendMessageA(g_hChatHistory, EM_SETSEL, (WPARAM)len, (LPARAM)len);
        SendMessageA(g_hChatHistory, EM_REPLACESEL, FALSE, (LPARAM)buf);
        SendMessageA(g_hChatHistory, EM_SCROLLCARET, 0, 0);
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    g_nMessageCount++;
    UpdateMessageCount();

    SetStatus("Ready");
    g_bGenerating = FALSE;
    EnableWindow(g_hSendButton, TRUE);
    EnableWindow(g_hInputBox, TRUE);
    SetFocus(g_hInputBox);

    free(args);
    return 0;
}

// Send message
static void DoSend(void) {
    if (g_bGenerating) return;

    char input[2048] = {0};
    GetWindowTextA(g_hInputBox, input, sizeof(input));

    char *start = input;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (*start == '\0') return;

    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) *end-- = '\0';

    // If no chat name yet, ask for one on first real message
    if (g_szCurrentChat[0] == '\0') {
        g_szNewChatName[0] = '\0';
        if (ShowNameDialog() == IDOK && g_szNewChatName[0]) {
            strncpy(g_szCurrentChat, g_szNewChatName, MAX_CHAT_NAME);
            UpdateTitle();
            char status[128];
            snprintf(status, sizeof(status), "Chat: %s", g_szCurrentChat);
            SetStatus(status);
        }
        // Even if they cancel the name dialog, still send the message
    }

    AppendChatLine("");
    AppendChat("You:  ");
    AppendChat(start);

    SetWindowTextA(g_hInputBox, "");
    g_nMessageCount++;
    UpdateMessageCount();

    g_bGenerating = TRUE;
    EnableWindow(g_hSendButton, FALSE);

    SetStatus("NeuralXP is thinking...");
    struct InferenceArgs *args = (struct InferenceArgs *)malloc(sizeof(struct InferenceArgs));
    strncpy(args->prompt, start, sizeof(args->prompt) - 1);

    DWORD threadId;
    g_hThread = CreateThread(NULL, 0, InferenceThread, args, 0, &threadId);
}

// New chat
static void DoNewChat(void) {
    if (g_bGenerating) {
        MessageBeep(MB_ICONEXCLAMATION);
        return;
    }

    // Auto-save current chat before starting new one
    AutoSaveChat();

    // Reset state
    g_szCurrentChat[0] = '\0';
    g_szChatContext[0] = '\0';
    g_szChatFullHistory[0] = '\0';

    SetWindowTextA(g_hChatHistory, "");
    g_nMessageCount = 0;
    UpdateMessageCount();

    AppendChat("Welcome to NeuralXP!\r\n");
    AppendChat("Offline AI for Windows XP  -  Built by Panmox (panmox.org)\r\n");
    AppendChat("Type your question below and press Send.\r\n");
    AppendChat("____________________________________________________________");

    UpdateTitle();
    SetStatus("Ready - New conversation started");
    SetFocus(g_hInputBox);
}

// Open saved chat
static void DoOpenChat(void) {
    if (g_bGenerating) {
        MessageBeep(MB_ICONEXCLAMATION);
        return;
    }

    // Auto-save current chat first
    AutoSaveChat();

    g_szSelectedChat[0] = '\0';
    if (ShowOpenChatDialog() != IDOK || g_szSelectedChat[0] == '\0') return;

    // Load the selected chat
    strncpy(g_szCurrentChat, g_szSelectedChat, MAX_CHAT_NAME);
    LoadChatContext(g_szCurrentChat);

    // Reset display
    SetWindowTextA(g_hChatHistory, "");
    g_nMessageCount = 0;

    AppendChat("NeuralXP - Chat: ");
    AppendChat(g_szCurrentChat);
    AppendChat("\r\n____________________________________________________________\r\n");

    // Show the saved chat history
    if (g_szChatFullHistory[0]) {
        AppendChat(g_szChatFullHistory);
        AppendChat("____________________________________________________________");
    } else {
        AppendChat("(empty chat - start typing!)");
    }

    UpdateTitle();
    char status[128];
    snprintf(status, sizeof(status), "Chat: %s - Context loaded", g_szCurrentChat);
    SetStatus(status);
    SetFocus(g_hInputBox);
}

// Save chat (manual save with file picker)
static void DoSaveChat(void) {
    char filename[MAX_PATH] = "NeuralXP_Chat.txt";

    OPENFILENAMEA ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "txt";

    if (GetSaveFileNameA(&ofn)) {
        int len = GetWindowTextLengthA(g_hChatHistory);
        char *buf = (char *)malloc(len + 1);
        GetWindowTextA(g_hChatHistory, buf, len + 1);

        FILE *f = fopen(filename, "w");
        if (f) {
            fprintf(f, "NeuralXP Chat Log\r\n");
            fprintf(f, "Saved: %s\r\n", __DATE__);
            fprintf(f, "========================================\r\n\r\n");
            fprintf(f, "%s", buf);
            fclose(f);
            SetStatus("Chat saved successfully");
        }
        free(buf);
    }
}

// About dialog
static void DoAbout(void) {
    MessageBoxA(g_hWnd,
        "NeuralXP v1.0\r\n"
        "Offline AI Assistant for Windows XP\r\n"
        "\r\n"
        "Built by Panmox\r\n"
        "https://panmox.org\r\n"
        "\r\n"
        "Powered by Qwen2.5-0.5B via llama.cpp\r\n"
        "Runs 100% offline - no internet needed\r\n"
        "\r\n"
        "System Requirements:\r\n"
        "  Windows XP SP2 or later\r\n"
        "  512 MB RAM minimum\r\n"
        "  500 MB disk space",
        "About NeuralXP",
        MB_OK | MB_ICONINFORMATION);
}

// Tips dialog
static void DoTips(void) {
    MessageBoxA(g_hWnd,
        "Tips for best results:\r\n"
        "\r\n"
        "- Keep questions short and specific\r\n"
        "- Ask for brief answers\r\n"
        "- For code: \"write a short [language] function that...\"\r\n"
        "- For facts: just ask directly\r\n"
        "- Press Enter to send, Shift+Enter for new line\r\n"
        "\r\n"
        "Chat System:\r\n"
        "- First message asks for chat name\r\n"
        "- Chats auto-save when you switch or exit\r\n"
        "- File > Open Chat to load a saved chat\r\n"
        "\r\n"
        "NeuralXP runs on your CPU so responses\r\n"
        "take 15-30 seconds. Be patient!",
        "NeuralXP Tips",
        MB_OK | MB_ICONINFORMATION);
}

// Draw XP-style gradient header
static void DrawHeader(HDC hdc, RECT *rc) {
    int h = rc->bottom - rc->top;
    for (int i = 0; i < h; i++) {
        float t = (float)i / (float)h;
        int r = (int)(0 + t * 4);
        int g = (int)(88 - t * 40);
        int b = (int)(208 - t * 72);
        if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0;
        HBRUSH hBr = CreateSolidBrush(RGB(r, g, b));
        RECT line = {rc->left, rc->top + i, rc->right, rc->top + i + 1};
        FillRect(hdc, &line, hBr);
        DeleteObject(hBr);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    SelectObject(hdc, g_hFontHeader);
    RECT rcTitle = {16, rc->top + 8, rc->right - 16, rc->top + 30};
    DrawTextA(hdc, "NeuralXP", -1, &rcTitle, DT_LEFT | DT_SINGLELINE);

    SelectObject(hdc, g_hFontHeaderSub);
    SetTextColor(hdc, RGB(200, 220, 255));
    RECT rcSub = {16, rc->top + 30, rc->right - 16, rc->top + 48};
    DrawTextA(hdc, "Offline AI  \xB7  panmox.org", -1, &rcSub, DT_LEFT | DT_SINGLELINE);

    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 40, 120));
    SelectObject(hdc, hPen);
    MoveToEx(hdc, rc->left, rc->bottom - 1, NULL);
    LineTo(hdc, rc->right, rc->bottom - 1);
    DeleteObject(hPen);
}

// Draw 3D sunken border (XP style)
static void Draw3DBorder(HDC hdc, RECT *rc) {
    HPEN hPenDark = CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HPEN hPenLight = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));

    SelectObject(hdc, hPenDark);
    MoveToEx(hdc, rc->left, rc->bottom - 1, NULL);
    LineTo(hdc, rc->left, rc->top);
    LineTo(hdc, rc->right - 1, rc->top);

    SelectObject(hdc, hPenLight);
    LineTo(hdc, rc->right - 1, rc->bottom - 1);
    LineTo(hdc, rc->left, rc->bottom - 1);

    DeleteObject(hPenDark);
    DeleteObject(hPenLight);
}

// Create menu bar
static HMENU CreateAppMenu(void) {
    HMENU hMenuBar = CreateMenu();

    HMENU hFile = CreatePopupMenu();
    AppendMenuA(hFile, MF_STRING, IDM_FILE_NEWCHAT, "&New Chat\tCtrl+N");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_OPENCHAT, "&Open Chat...\tCtrl+O");
    AppendMenuA(hFile, MF_STRING, IDM_FILE_SAVECHAT, "&Save Chat...\tCtrl+S");
    AppendMenuA(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hFile, MF_STRING, IDM_FILE_EXIT, "E&xit\tAlt+F4");

    HMENU hEdit = CreatePopupMenu();
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_COPY, "&Copy\tCtrl+C");
    AppendMenuA(hEdit, MF_STRING, IDM_EDIT_SELECTALL, "Select &All\tCtrl+A");

    HMENU hHelp = CreatePopupMenu();
    AppendMenuA(hHelp, MF_STRING, IDM_HELP_TIPS, "&Tips for Best Results");
    AppendMenuA(hHelp, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hHelp, MF_STRING, IDM_HELP_ABOUT, "&About NeuralXP...");

    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hFile, "&File");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hEdit, "&Edit");
    AppendMenuA(hMenuBar, MF_POPUP, (UINT_PTR)hHelp, "&Help");

    return hMenuBar;
}

// Window procedure
static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g_hFontUI = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");

        g_hFontChat = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_MODERN, "Lucida Console");

        g_hFontBold = CreateFontA(-11, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");

        g_hFontHeader = CreateFontA(-18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "Trebuchet MS");

        g_hFontHeaderSub = CreateFontA(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");

        g_hFontInput = CreateFontA(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Tahoma");

        g_hBrushBg = CreateSolidBrush(XP_BG_GRAY);
        g_hBrushWhite = CreateSolidBrush(XP_WHITE);

        g_hChatHistory = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT", "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL |
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_LEFT,
            12, 58, WIN_WIDTH - 38, 290,
            hWnd, (HMENU)ID_CHAT_HISTORY, GetModuleHandle(NULL), NULL);

        g_hLabelPanel = CreateWindowExA(
            0, "STATIC", "  Type your message:",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 358, 200, 16,
            hWnd, NULL, GetModuleHandle(NULL), NULL);

        g_hInputBox = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT", "",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_AUTOVSCROLL | WS_TABSTOP,
            12, 376, WIN_WIDTH - 118, 50,
            hWnd, (HMENU)ID_INPUT_BOX, GetModuleHandle(NULL), NULL);

        g_hSendButton = CreateWindowExA(
            0,
            "BUTTON", "Send",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_TEXT | WS_TABSTOP,
            WIN_WIDTH - 96, 376, 78, 30,
            hWnd, (HMENU)ID_SEND_BUTTON, GetModuleHandle(NULL), NULL);

        g_hClearButton = CreateWindowExA(
            0,
            "BUTTON", "New Chat",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            WIN_WIDTH - 96, 410, 78, 22,
            hWnd, (HMENU)ID_CLEAR_BUTTON, GetModuleHandle(NULL), NULL);

        g_hStatusBar = CreateWindowExA(
            0, STATUSCLASSNAMEA, NULL,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            hWnd, (HMENU)ID_STATUS_BAR, GetModuleHandle(NULL), NULL);

        int parts[2] = {320, -1};
        SendMessage(g_hStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

        SendMessage(g_hChatHistory, WM_SETFONT, (WPARAM)g_hFontChat, TRUE);
        SendMessage(g_hInputBox, WM_SETFONT, (WPARAM)g_hFontInput, TRUE);
        SendMessage(g_hSendButton, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
        SendMessage(g_hClearButton, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        SendMessage(g_hLabelPanel, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);
        SendMessage(g_hStatusBar, WM_SETFONT, (WPARAM)g_hFontUI, TRUE);

        int tabstop = 16;
        SendMessage(g_hChatHistory, EM_SETTABSTOPS, 1, (LPARAM)&tabstop);

        AppendChat("Welcome to NeuralXP!\r\n");
        AppendChat("Offline AI for Windows XP  -  Built by Panmox (panmox.org)\r\n");
        AppendChat("Type your question below and press Send.\r\n");
        AppendChat("____________________________________________________________");

        SetStatus("Ready");
        UpdateMessageCount();

        SetFocus(g_hInputBox);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rcClient;
        GetClientRect(hWnd, &rcClient);

        RECT rcHeader = {0, 0, rcClient.right, 54};
        DrawHeader(hdc, &rcHeader);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: {
        HDC hdcStatic = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hLabelPanel) {
            SetBkColor(hdcStatic, GetSysColor(COLOR_BTNFACE));
            SetTextColor(hdcStatic, XP_TEXT_DARK);
            return (LRESULT)g_hBrushBg;
        }
        if (hCtrl == g_hChatHistory) {
            SetBkColor(hdcStatic, XP_WHITE);
            SetTextColor(hdcStatic, XP_TEXT_DARK);
            return (LRESULT)g_hBrushWhite;
        }
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdcEdit = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        if (hCtrl == g_hChatHistory) {
            SetBkColor(hdcEdit, XP_WHITE);
            SetTextColor(hdcEdit, XP_TEXT_DARK);
            return (LRESULT)g_hBrushWhite;
        }
        if (hCtrl == g_hInputBox) {
            SetBkColor(hdcEdit, XP_WHITE);
            SetTextColor(hdcEdit, RGB(0, 0, 0));
            return (LRESULT)g_hBrushWhite;
        }
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        rc.top = 54;
        FillRect(hdc, &rc, g_hBrushBg);
        return 1;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_SEND_BUTTON:
            DoSend();
            return 0;
        case ID_CLEAR_BUTTON:
        case IDM_FILE_NEWCHAT:
            DoNewChat();
            return 0;
        case IDM_FILE_OPENCHAT:
            DoOpenChat();
            return 0;
        case IDM_FILE_SAVECHAT:
            DoSaveChat();
            return 0;
        case IDM_FILE_EXIT:
            PostMessage(hWnd, WM_CLOSE, 0, 0);
            return 0;
        case IDM_EDIT_COPY:
            SendMessage(g_hChatHistory, WM_COPY, 0, 0);
            return 0;
        case IDM_EDIT_SELECTALL:
            SendMessage(g_hChatHistory, EM_SETSEL, 0, -1);
            return 0;
        case IDM_HELP_ABOUT:
            DoAbout();
            return 0;
        case IDM_HELP_TIPS:
            DoTips();
            return 0;
        }
        break;

    case WM_SIZE: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        int w = rc.right;
        int h = rc.bottom;

        RECT rcStatus;
        GetWindowRect(g_hStatusBar, &rcStatus);
        int statusH = rcStatus.bottom - rcStatus.top;

        int chatBottom = h - statusH - 100;
        int inputTop = chatBottom + 22;

        MoveWindow(g_hChatHistory, 12, 58, w - 24, chatBottom - 58, TRUE);
        MoveWindow(g_hLabelPanel, 12, chatBottom + 4, 200, 16, TRUE);
        MoveWindow(g_hInputBox, 12, inputTop, w - 106, 50, TRUE);
        MoveWindow(g_hSendButton, w - 84, inputTop, 72, 30, TRUE);
        MoveWindow(g_hClearButton, w - 84, inputTop + 32, 72, 22, TRUE);
        SendMessage(g_hStatusBar, WM_SIZE, 0, 0);

        int parts[2] = {w - 150, -1};
        SendMessage(g_hStatusBar, SB_SETPARTS, 2, (LPARAM)parts);

        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lParam;
        mmi->ptMinTrackSize.x = 420;
        mmi->ptMinTrackSize.y = 380;
        return 0;
    }

    case WM_CLOSE:
        if (g_bGenerating) {
            MessageBeep(MB_ICONEXCLAMATION);
            if (MessageBoxA(hWnd, "NeuralXP is still thinking.\nClose anyway?",
                          "NeuralXP", MB_YESNO | MB_ICONQUESTION) != IDYES) {
                return 0;
            }
        }
        // Auto-save on exit
        AutoSaveChat();
        DestroyWindow(hWnd);
        return 0;

    case WM_DESTROY:
        if (g_hFontUI) DeleteObject(g_hFontUI);
        if (g_hFontChat) DeleteObject(g_hFontChat);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        if (g_hFontHeader) DeleteObject(g_hFontHeader);
        if (g_hFontHeaderSub) DeleteObject(g_hFontHeaderSub);
        if (g_hFontInput) DeleteObject(g_hFontInput);
        if (g_hBrushBg) DeleteObject(g_hBrushBg);
        if (g_hBrushWhite) DeleteObject(g_hBrushWhite);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

// Subclass input for Enter key
static WNDPROC g_OldInputProc = NULL;
static LRESULT CALLBACK InputSubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
        if (!(GetKeyState(VK_SHIFT) & 0x8000)) {
            DoSend();
            return 0;
        }
    }
    return CallWindowProcA(g_OldInputProc, hWnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    // Get exe directory
    GetModuleFileNameA(NULL, g_szExeDir, MAX_PATH);
    char *lastSlash = strrchr(g_szExeDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    // Create chats directory
    EnsureChatsDir();

    // Find model
    if (!FindModel()) {
        MessageBoxA(NULL,
            "NeuralXP could not find the AI model.\n\n"
            "Please make sure the file\n"
            "'models\\qwen2.5-0.5b-instruct-q2_k.gguf'\n"
            "exists in the same folder as NeuralXP.exe",
            "NeuralXP - Model Not Found",
            MB_OK | MB_ICONERROR);
        return 1;
    }

    // Init common controls
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    // Register window class
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = "NeuralXPClass";
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_NEURALXP));
    wc.hIconSm = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_NEURALXP), IMAGE_ICON,
                                   0, 0, LR_DEFAULTSIZE);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassExA(&wc)) return 1;

    g_hMenu = CreateAppMenu();

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    g_hWnd = CreateWindowExA(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE,
        "NeuralXPClass",
        "NeuralXP - Offline AI for Windows XP",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        (screenW - WIN_WIDTH) / 2,
        (screenH - WIN_HEIGHT) / 2,
        WIN_WIDTH, WIN_HEIGHT,
        NULL, g_hMenu, hInstance, NULL);

    if (!g_hWnd) return 1;

    g_OldInputProc = (WNDPROC)SetWindowLongA(g_hInputBox, GWL_WNDPROC, (LONG)InputSubclassProc);

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && (GetKeyState(VK_CONTROL) & 0x8000)) {
            if (msg.wParam == 'N') { DoNewChat(); continue; }
            if (msg.wParam == 'S') { DoSaveChat(); continue; }
            if (msg.wParam == 'O') { DoOpenChat(); continue; }
        }

        if (!IsDialogMessage(g_hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}
