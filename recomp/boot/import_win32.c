/* Setting the game up: when there's no game beside the program (boot.c),
 * a small window asks for the player's copy of it, a disc image (ISO, RVZ,
 * WBFS and the rest), and extracts it into an R8AE01 folder beside the
 * program, as the game is read from there: its files in R8AE01\disc and
 * main.dol in R8AE01\sys. The extracting is done by decomp-toolkit
 * (tools\dtk.exe beside the program, "dtk disc extract"), which reads all
 * those formats; the disc must be PokePark Wii's (USA), R8AE01. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")
/* Today's buttons, not Windows 95's (common controls 6). */
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum { ID_CHOOSE = 1, ID_QUIT };

static HWND sText, sChoose, sQuit;
static HFONT sFont;
static char sDir[MAX_PATH]; /* the program's folder */
static int sDone;           /* 1 set up, -1 quit */

static void set_text(const char* s) { SetWindowTextA(sText, s); }

/* Deletes a folder and everything in it. */
static void remove_tree(const char* path) {
    char from[MAX_PATH + 2];
    SHFILEOPSTRUCTA op;
    memset(from, 0, sizeof(from));
    snprintf(from, MAX_PATH, "%s", path);
    memset(&op, 0, sizeof(op));
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_NO_UI;
    SHFileOperationA(&op);
}

/* Runs dtk to extract image into out, the window kept answering meanwhile;
 * returns its exit code (-1 if it couldn't start). */
static int extract(const char* image, const char* out) {
    char cmd[3 * MAX_PATH], dtk[MAX_PATH];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD code = (DWORD)-1;
    snprintf(dtk, sizeof(dtk), "%s\\tools\\dtk.exe", sDir);
    snprintf(cmd, sizeof(cmd), "\"%s\" disc extract -q \"%s\" \"%s\"", dtk, image, out);
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    if (!CreateProcessA(dtk, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) return -1;
    while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

/* Whether the extracted disc at dir is the game: its ID, R8AE01, opens
 * sys\boot.bin. */
static int is_game(const char* dir) {
    char path[MAX_PATH], id[6];
    FILE* f;
    int ok;
    snprintf(path, sizeof(path), "%s\\sys\\boot.bin", dir);
    if (!(f = fopen(path, "rb"))) return 0;
    ok = fread(id, 1, 6, f) == 6 && !memcmp(id, "R8AE01", 6);
    fclose(f);
    return ok;
}

static void import(HWND w) {
    char image[MAX_PATH] = "", game[MAX_PATH], disc[MAX_PATH], sys[MAX_PATH], from[MAX_PATH], to[MAX_PATH];
    OPENFILENAMEA ofn;
    int code;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = w;
    ofn.lpstrFilter = "Disc images (*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz;*.nfs)\0*.iso;*.rvz;*.wia;*.wbfs;*.ciso;*.gcz;*.nfs\0"
                      "All files (*.*)\0*.*\0";
    ofn.lpstrFile = image;
    ofn.nMaxFile = sizeof(image);
    ofn.lpstrTitle = "Choose your copy of PokePark Wii";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&ofn)) return;
    snprintf(game, sizeof(game), "%s\\R8AE01", sDir);
    snprintf(disc, sizeof(disc), "%s\\disc", game);
    snprintf(sys, sizeof(sys), "%s\\sys", game);
    remove_tree(disc); /* what an earlier try left */
    CreateDirectoryA(game, NULL);
    EnableWindow(sChoose, FALSE);
    set_text("Setting the game up from your disc image...\r\nThis takes a minute or two.");
    code = extract(image, disc);
    if (code != 0 || !is_game(disc)) {
        remove_tree(disc);
        EnableWindow(sChoose, TRUE);
        set_text(code == -1 ? "Couldn't start tools\\dtk.exe, which reads disc images: it should be in the "
                              "tools folder beside PokePark.exe."
                 : code != 0 ? "That file couldn't be read as a disc image. Choose a PokePark Wii disc image "
                               "(ISO, RVZ, WBFS...)."
                             : "That disc isn't PokePark Wii: Pikachu's Adventure (USA, R8AE01). Choose that "
                               "game's disc image.");
        return;
    }
    /* main.dol where the game is looked for, as in the source tree. */
    CreateDirectoryA(sys, NULL);
    snprintf(from, sizeof(from), "%s\\sys\\main.dol", disc);
    snprintf(to, sizeof(to), "%s\\main.dol", sys);
    if (!CopyFileA(from, to, FALSE)) {
        EnableWindow(sChoose, TRUE);
        set_text("The disc image was read, but main.dol couldn't be copied. Is the folder read-only?");
        return;
    }
    sDone = 1;
}

static LRESULT CALLBACK proc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == ID_CHOOSE) import(w);
        if (LOWORD(wp) == ID_QUIT) sDone = -1;
        return 0;
    case WM_CLOSE: sDone = -1; return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    return DefWindowProcA(w, msg, wp, lp);
}

int import_game(char* mainDol, size_t n) {
    WNDCLASSA wc;
    HWND w;
    RECT r = {0, 0, 480, 190};
    NONCLIENTMETRICSA ncm;
    char* slash;
    MSG m;
    int dpi;
    if (!GetModuleFileNameA(NULL, sDir, sizeof(sDir))) return 0;
    if ((slash = strrchr(sDir, '\\')) != NULL) *slash = '\0';
    SetProcessDPIAware();
    dpi = GetDeviceCaps(GetDC(NULL), LOGPIXELSY);
    r.right = MulDiv(r.right, dpi, 96);
    r.bottom = MulDiv(r.bottom, dpi, 96);
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = "PokeParkImport";
    RegisterClassA(&wc);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    w = CreateWindowA(wc.lpszClassName, "PokePark", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT,
                      CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, wc.hInstance, NULL);
    if (!w) return 0;
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    sFont = CreateFontIndirectA(&ncm.lfMessageFont);
#define S(v) MulDiv(v, dpi, 96)
    sText = CreateWindowA("STATIC",
                          "PokePark Wii isn't set up yet. Choose your copy of the game, a disc image of "
                          "PokePark Wii: Pikachu's Adventure (USA): an ISO, RVZ, WBFS or the like.",
                          WS_CHILD | WS_VISIBLE, S(20), S(20), S(440), S(80), w, NULL, wc.hInstance, NULL);
    sChoose = CreateWindowA("BUTTON", "Choose game file...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                            S(20), S(130), S(200), S(36), w, (HMENU)ID_CHOOSE, wc.hInstance, NULL);
    sQuit = CreateWindowA("BUTTON", "Quit", WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(360), S(130), S(100), S(36), w,
                          (HMENU)ID_QUIT, wc.hInstance, NULL);
#undef S
    SendMessageA(sText, WM_SETFONT, (WPARAM)sFont, TRUE);
    SendMessageA(sChoose, WM_SETFONT, (WPARAM)sFont, TRUE);
    SendMessageA(sQuit, WM_SETFONT, (WPARAM)sFont, TRUE);
    ShowWindow(w, SW_SHOW);
    SetFocus(sChoose);
    while (!sDone && GetMessageA(&m, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    DestroyWindow(w);
    DeleteObject(sFont);
    if (sDone != 1) return 0;
    snprintf(mainDol, n, "%s\\R8AE01\\sys\\main.dol", sDir);
    return 1;
}
