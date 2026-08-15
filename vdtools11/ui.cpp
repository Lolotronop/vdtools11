#include <windows.h>
#include <shellapi.h>

#include "ui.h"
#include "log.h"
#include "prop.h"

#define WM_USER_TRAYICON    (WM_USER + 1)
#define WM_USER_TASKBAR_SCROLL (WM_USER + 2)
#define ID_TRAY_APP_ICON    (1002)
#define ID_MENU_ITEM1       (1003)
#define ID_MENU_ITEM2       (1004)
#define ID_MENU_ITEM3       (1005)
#define ID_MENU_ITEM4       (1006)
#define ID_MENU_ABOUT       (1007)
#define ID_MENU_HELP        (1008)
#define ID_MENU_EXIT        (1009)
#define ID_HOTKEY1          (1010)
#define ID_HOTKEY2          (1011)
#define ID_HOTKEY3          (1012)
#define ID_HOTKEY4          (1013)
#define ID_HOTKEY5          (1014)
#define ID_HOTKEY6          (1015)
#define ID_HOTKEY7          (1016)
#define ID_HOTKEY8          (1017)
#define ID_HOTKEY9          (1018)
#define ID_HOTKEY10         (1019)
#define ID_HOTKEY11         (1020)
#define ID_HOTKEY12         (1021)
#define ID_HOTKEY13         (1022)
#define ID_HOTKEY14         (1023)
#define ID_HOTKEY15         (1024)
#define ID_HOTKEY16         (1025)
#define ID_HOTKEY17         (1026)
#define ID_HOTKEY18         (1027)
#define ID_HOTKEY19         (1028)
#define ID_HOTKEY20         (1029)
#define ID_MENU_TASKBAR_SCROLL (1030)

#define SCROLL_TO_PREVIOUS  (1)
#define SCROLL_TO_NEXT      (2)

void (*uiJumpToDesktop)(UINT, BOOL);
UINT (*uiGetCurrentDesktop)(void);
DWORD (*uiStartOnHomeChecked)(void);
DWORD (*uiJumpingChecked)(void);
DWORD (*uiDraggingChecked)(void);
DWORD (*uiNumberChecked)(void);
DWORD (*uiTaskbarScrollChecked)(void);
void (*uiToggleStartOnHome)(void);
void (*uiToggleJumping)(void);
void (*uiToggleDragging)(void);
void (*uiToggleNumber)(void);
void (*uiToggleTaskbarScroll)(void);
static HINSTANCE m_hInstance;
static HWND m_hWnd;
static NOTIFYICONDATA m_nid;
static UINT m_uTaskbarCreatedMsg;
static HWINEVENTHOOK m_hNamechangeEvent;
static HWINEVENTHOOK m_hHideEvent;
static HICON m_hIcon;
static UINT m_numberDisplayed;
static HHOOK m_hTaskbarMouseHook;
static int m_taskbarWheelRemainder;

BOOL IsPointOnTaskbar(POINT pt)
{
    HWND hWnd = WindowFromPoint(pt);

    if (!hWnd)
    {
        return FALSE;
    }

    HWND hRoot = GetAncestor(hWnd, GA_ROOT);
    WCHAR className[64];

    if (!GetClassName(hRoot, className, ARRAYSIZE(className)))
    {
        return FALSE;
    }

    return (0 == lstrcmp(className, TEXT("Shell_TrayWnd"))) ||
           (0 == lstrcmp(className, TEXT("Shell_SecondaryTrayWnd")));
}

LRESULT CALLBACK TaskbarMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if ((nCode == HC_ACTION) && (wParam == WM_MOUSEWHEEL))
    {
        MSLLHOOKSTRUCT *pMouse = (MSLLHOOKSTRUCT *)lParam;

        if (IsPointOnTaskbar(pMouse->pt))
        {
            int delta = (SHORT)HIWORD(pMouse->mouseData);

            // Do not combine partial wheel movements in opposing directions.
            if (((delta > 0) && (m_taskbarWheelRemainder < 0)) ||
                ((delta < 0) && (m_taskbarWheelRemainder > 0)))
            {
                m_taskbarWheelRemainder = 0;
            }

            m_taskbarWheelRemainder += delta;

            while (m_taskbarWheelRemainder >= WHEEL_DELTA)
            {
                PostMessage(m_hWnd, WM_USER_TASKBAR_SCROLL, SCROLL_TO_PREVIOUS, 0);
                m_taskbarWheelRemainder -= WHEEL_DELTA;
            }

            while (m_taskbarWheelRemainder <= -WHEEL_DELTA)
            {
                PostMessage(m_hWnd, WM_USER_TASKBAR_SCROLL, SCROLL_TO_NEXT, 0);
                m_taskbarWheelRemainder += WHEEL_DELTA;
            }

            // Prevent Explorer from handling the same wheel movement.
            return 1;
        }

        m_taskbarWheelRemainder = 0;
    }

    return CallNextHookEx(m_hTaskbarMouseHook, nCode, wParam, lParam);
}

void uiHookTaskbarScroll(void)
{
    if (m_hTaskbarMouseHook)
    {
        return;
    }

    m_taskbarWheelRemainder = 0;
    m_hTaskbarMouseHook = SetWindowsHookEx(WH_MOUSE_LL, TaskbarMouseProc, m_hInstance, 0);

    if (!m_hTaskbarMouseHook)
    {
        logError(TEXT("Could not hook taskbar mouse events."));
    }
}

void uiUnhookTaskbarScroll(void)
{
    if (m_hTaskbarMouseHook)
    {
        (void)UnhookWindowsHookEx(m_hTaskbarMouseHook);
        m_hTaskbarMouseHook = NULL;
    }

    m_taskbarWheelRemainder = 0;
}

void ShowDesktopNumber(void)
{
    UINT numberToDisplay = uiGetCurrentDesktop() + 1;

    if (m_numberDisplayed == numberToDisplay)
    {
        return;
    }

    m_numberDisplayed = numberToDisplay;
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);

    // Create device context.
    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HDC hdcMem2 = CreateCompatibleDC(hdc);

    // Create bitmap.
    HBITMAP hbm = CreateCompatibleBitmap(hdc, cx, cy);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbm);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, NULL);
    HBITMAP hbmOld2 = (HBITMAP)SelectObject(hdcMem2, hbmMask);
    ReleaseDC(NULL, hdc);
    RECT rc = {0, 0, cx, cy};
    FillRect(hdcMem, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
    FillRect(hdcMem2, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));

    // Create font.
    HFONT hFont = CreateFont(
        18,          // cHeight
        0,          // cWidth
        0,          // cEscapement
        0,          // cOrientation
        FW_BOLD,    // cWeight
        0,          // bItalic
        0,          // bUnderline
        0,          // bStrikeOut
        0,          // iCharSet
        0,          // iOutPrecision
        0,          // iClipPrecision
        0,          // iQuality
        0,          // iPitchAndFamily
        0);         // pszFaceName
    HFONT hFontOld = (HFONT)SelectObject(hdcMem, hFont);
    HFONT hFontOld2 = (HFONT)SelectObject(hdcMem2, hFont);

    // Draw text in the bitmap.
    WCHAR buf[32];
    wsprintf(buf, TEXT("%d"), numberToDisplay);
    DrawText(hdcMem, buf, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    DrawText(hdcMem2, buf, -1, &rc, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
    SelectObject(hdcMem, hbmOld);
    SelectObject(hdcMem2, hbmOld2);

    // Create icon.
    ICONINFO ii;
    ii.fIcon = TRUE;
    ii.hbmMask = hbmMask;
    ii.hbmColor = hbm;
    HICON hIcon = CreateIconIndirect(&ii);

    // Update tray icon.
    m_nid.hIcon = hIcon;
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);

    // Release icon
    (void)DestroyIcon(hIcon);

    // Release font.
    SelectObject(hdcMem, hFontOld);
    SelectObject(hdcMem2, hFontOld2);
    DeleteObject(hFont);

    // Release bitmap.
    SelectObject(hdcMem2, hbmOld2);
    DeleteObject(hbmMask);
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbm);

    // Release device context.
    DeleteDC(hdcMem);
    DeleteDC(hdcMem2);
}

VOID CALLBACK Wineventproc(
    HWINEVENTHOOK hWinEventHook,
    DWORD         event,
    HWND          hwnd,
    LONG          idObject,
    LONG          idChild,
    DWORD         idEventThread,
    DWORD         dwmsEventTime
)
{
    (void)hWinEventHook;
    (void)event;
    (void)hwnd;
    (void)idObject;
    (void)idChild;
    (void)idEventThread;
    (void)dwmsEventTime;
    ShowDesktopNumber();
}

void RedrawDesktopNumber(void)
{
    m_numberDisplayed = 0;
    ShowDesktopNumber();
}

void RegisterHotKeyConvenience(int id, UINT fsModifiers, UINT vk)
{
    if (!m_hWnd)
    {
        return;
    }

    if (!RegisterHotKey(m_hWnd, id, fsModifiers, vk))
    {
        WCHAR buf[256];
        wsprintf(buf, TEXT("Could not register hot key with ID %d."), id);
        logError(buf);
    }
}

void uiRegisterJumpKeys(void)
{
    UINT modifiers = (MOD_ALT | MOD_CONTROL | MOD_NOREPEAT | MOD_WIN);
    RegisterHotKeyConvenience(ID_HOTKEY1, modifiers, '1');
    RegisterHotKeyConvenience(ID_HOTKEY2, modifiers, '2');
    RegisterHotKeyConvenience(ID_HOTKEY3, modifiers, '3');
    RegisterHotKeyConvenience(ID_HOTKEY4, modifiers, '4');
    RegisterHotKeyConvenience(ID_HOTKEY5, modifiers, '5');
    RegisterHotKeyConvenience(ID_HOTKEY6, modifiers, '6');
    RegisterHotKeyConvenience(ID_HOTKEY7, modifiers, '7');
    RegisterHotKeyConvenience(ID_HOTKEY8, modifiers, '8');
    RegisterHotKeyConvenience(ID_HOTKEY9, modifiers, '9');

    modifiers = (MOD_ALT | MOD_CONTROL | MOD_NOREPEAT | MOD_SHIFT | MOD_WIN);
    RegisterHotKeyConvenience(ID_HOTKEY10, modifiers, '1');
    RegisterHotKeyConvenience(ID_HOTKEY11, modifiers, '2');
    RegisterHotKeyConvenience(ID_HOTKEY12, modifiers, '3');
    RegisterHotKeyConvenience(ID_HOTKEY13, modifiers, '4');
    RegisterHotKeyConvenience(ID_HOTKEY14, modifiers, '5');
    RegisterHotKeyConvenience(ID_HOTKEY15, modifiers, '6');
    RegisterHotKeyConvenience(ID_HOTKEY16, modifiers, '7');
    RegisterHotKeyConvenience(ID_HOTKEY17, modifiers, '8');
    RegisterHotKeyConvenience(ID_HOTKEY18, modifiers, '9');
}

void uiRegisterDragKeys(void)
{
    UINT modifiers = (MOD_ALT | MOD_CONTROL | MOD_NOREPEAT | MOD_WIN);
    RegisterHotKeyConvenience(ID_HOTKEY19, modifiers, VK_LEFT);
    RegisterHotKeyConvenience(ID_HOTKEY20, modifiers, VK_RIGHT);
}

void uiHookWinEvents(void)
{
    ShowDesktopNumber();

    m_hNamechangeEvent = SetWinEventHook(
        EVENT_OBJECT_NAMECHANGE,                           // eventMin
        EVENT_OBJECT_NAMECHANGE,                           // eventMax
        NULL,                                              // hmodWinEventProc
        Wineventproc,                                      // pfnWinEventProc
        0,                                                 // idProcess (0 = all processes)
        0,                                                 // idThread (0 = all threads)
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS    // dwFlags
    );

    m_hHideEvent = SetWinEventHook(
        EVENT_OBJECT_HIDE,                                 // eventMin
        EVENT_OBJECT_HIDE,                                 // eventMax
        NULL,                                              // hmodWinEventProc
        Wineventproc,                                      // pfnWinEventProc
        0,                                                 // idProcess (0 = all processes)
        0,                                                 // idThread (0 = all threads)
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS    // dwFlags
    );
}

void UnregisterHotKeyConvenience(int id)
{
    if (!m_hWnd)
    {
        return;
    }

    if (!UnregisterHotKey(m_hWnd, id))
    {
        WCHAR buf[256];
        wsprintf(buf, TEXT("Could not unregister hot key with ID %d."), id);
        logError(buf);
    }
}

void UnregisterJumpKeys(void)
{
    UnregisterHotKeyConvenience(ID_HOTKEY1);
    UnregisterHotKeyConvenience(ID_HOTKEY2);
    UnregisterHotKeyConvenience(ID_HOTKEY3);
    UnregisterHotKeyConvenience(ID_HOTKEY4);
    UnregisterHotKeyConvenience(ID_HOTKEY5);
    UnregisterHotKeyConvenience(ID_HOTKEY6);
    UnregisterHotKeyConvenience(ID_HOTKEY7);
    UnregisterHotKeyConvenience(ID_HOTKEY8);
    UnregisterHotKeyConvenience(ID_HOTKEY9);
    UnregisterHotKeyConvenience(ID_HOTKEY10);
    UnregisterHotKeyConvenience(ID_HOTKEY11);
    UnregisterHotKeyConvenience(ID_HOTKEY12);
    UnregisterHotKeyConvenience(ID_HOTKEY13);
    UnregisterHotKeyConvenience(ID_HOTKEY14);
    UnregisterHotKeyConvenience(ID_HOTKEY15);
    UnregisterHotKeyConvenience(ID_HOTKEY16);
    UnregisterHotKeyConvenience(ID_HOTKEY17);
    UnregisterHotKeyConvenience(ID_HOTKEY18);
}

void UnregisterDragKeys(void)
{
    UnregisterHotKeyConvenience(ID_HOTKEY19);
    UnregisterHotKeyConvenience(ID_HOTKEY20);
}

void UnhookWinEvents(void)
{
    if (m_hNamechangeEvent)
    {
        (void)UnhookWinEvent(m_hNamechangeEvent);
    }

    if (m_hHideEvent)
    {
        (void)UnhookWinEvent(m_hHideEvent);
    }

    m_numberDisplayed = 0;

    // Restore default icon.
    m_nid.hIcon = m_hIcon;
    Shell_NotifyIcon(NIM_MODIFY, &m_nid);
}

void ShowMenu(void)
{
    HMENU hMenu = CreatePopupMenu();

    if (!hMenu)
    {
        return;
    }

    AppendMenu(hMenu, MF_STRING | uiStartOnHomeChecked(), ID_MENU_ITEM1, TEXT("Switch To Desktop 1 On Start"));
    AppendMenu(hMenu, MF_STRING | uiJumpingChecked(), ID_MENU_ITEM2, TEXT("Jump To Desktop Using Shortcut"));
    AppendMenu(hMenu, MF_STRING | uiDraggingChecked(), ID_MENU_ITEM3, TEXT("Move Windows To Adjacent Desktop"));
    AppendMenu(hMenu, MF_STRING | uiNumberChecked(), ID_MENU_ITEM4, TEXT("Show Desktop Number In Tray"));
    AppendMenu(hMenu, MF_STRING | uiTaskbarScrollChecked(), ID_MENU_TASKBAR_SCROLL, TEXT("Switch Desktops By Scrolling Taskbar"));

    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_MENU_ABOUT, TEXT("About"));
    AppendMenu(hMenu, MF_STRING, ID_MENU_HELP, TEXT("Help"));
    AppendMenu(hMenu, MF_STRING, ID_MENU_EXIT, TEXT("Exit"));

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(m_hWnd);
    BOOL ret = TrackPopupMenu(hMenu, TPM_LEFTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, m_hWnd, NULL);

    DestroyMenu(hMenu);

    if (ID_MENU_ITEM1 == ret)
    {
        uiToggleStartOnHome();
    }

    if (ID_MENU_ITEM2 == ret)
    {
        uiToggleJumping();
        (uiJumpingChecked()) ? uiRegisterJumpKeys() : UnregisterJumpKeys();
    }

    if (ID_MENU_ITEM3 == ret)
    {
        uiToggleDragging();
        (uiDraggingChecked()) ? uiRegisterDragKeys() : UnregisterDragKeys();
    }

    if (ID_MENU_ITEM4 == ret)
    {
        uiToggleNumber();
        (uiNumberChecked()) ? uiHookWinEvents() : UnhookWinEvents();
    }

    if (ID_MENU_TASKBAR_SCROLL == ret)
    {
        uiToggleTaskbarScroll();
        (uiTaskbarScrollChecked()) ? uiHookTaskbarScroll() : uiUnhookTaskbarScroll();
    }

    if (ID_MENU_ABOUT == ret)
    {
        MessageBox(NULL, TEXT("" APPNAME " v" MAJORVER "." MINORVER "." PATCHVER "."), TEXT("About"), MB_ICONINFORMATION);
    }

    if (ID_MENU_HELP == ret)
    {
        ShellExecute(NULL, TEXT("open"), TEXT("https://github.com/ptoshkov/vdtools11"), NULL, NULL, SW_SHOW);
    }

    if (ID_MENU_EXIT == ret)
    {
        DestroyWindow(m_hWnd);
    }
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_USER_TASKBAR_SCROLL:
    {
        UINT idx = uiGetCurrentDesktop();
        uiJumpToDesktop((SCROLL_TO_PREVIOUS == wParam) ? idx - 1 : idx + 1, FALSE);
        return 0;
    }
    case WM_USER_TRAYICON:
        switch (LOWORD(lParam))
        {
        case NIN_SELECT:
            ShowMenu();
            break;
        case WM_CONTEXTMENU:
            ShowMenu();
            break;
        }
        break;
    case WM_SYSCOLORCHANGE:
        if (uiNumberChecked()) RedrawDesktopNumber();
        break;
    case WM_CLOSE:
        DestroyWindow(hWnd);
        break;
    case WM_QUIT:
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        uiUnhookTaskbarScroll();
        Shell_NotifyIcon(NIM_DELETE, &m_nid);
        PostQuitMessage(0);
        return 0;
    }

    // Re-add the tray icon in case Windows Explorer restarts.
    if (m_uTaskbarCreatedMsg == uMsg)
    {
        uiAddTrayIcon();
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

void uiSetInstance(const HINSTANCE hInstance)
{
    m_hInstance = hInstance;
}

void uiCreateWindow(void)
{
    if (!m_hInstance)
    {
        return;
    }

    m_hWnd = 0;
    m_uTaskbarCreatedMsg = 0;
    m_hNamechangeEvent = 0;
    m_hHideEvent = 0;
    m_hIcon = 0;
    m_numberDisplayed = 0;
    m_hTaskbarMouseHook = 0;
    m_taskbarWheelRemainder = 0;

    // Register the window class.
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = m_hInstance;
    wc.lpszClassName = CLASSNAME;
    RegisterClass(&wc);

    // Create hidden window.
    m_hWnd = CreateWindow(CLASSNAME, WINDOWNAME, 0,
        0, 0, 0, 0, NULL, NULL, m_hInstance, NULL);

    // Subscribe to taskbar created events in case Windows Explorer restarts.
    m_uTaskbarCreatedMsg = RegisterWindowMessage(TEXT("TaskbarCreated"));
}

void uiAddTrayIcon(void)
{
    if (!m_hInstance || !m_hWnd)
    {
        return;
    }

    // Load icon image from resources.
    m_hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(1));

    // Add tray icon.
    m_nid.cbSize = sizeof(NOTIFYICONDATA);
    m_nid.hWnd = m_hWnd;
    m_nid.uID = ID_TRAY_APP_ICON;
    m_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    m_nid.uCallbackMessage = WM_USER_TRAYICON;
    m_nid.hIcon = m_hIcon;
    lstrcpy(m_nid.szTip, APPNAME);
    Shell_NotifyIcon(NIM_ADD, &m_nid);

    // NOTIFYICON_VERSION_4 is prefered
    m_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIcon(NIM_SETVERSION, &m_nid);
}

void uiStartMessageLoop(void)
{
    MSG msg;
    UINT idx;

    while (GetMessage(&msg, NULL, 0, 0))
    {
        switch (msg.message)
        {
        case WM_HOTKEY:
            switch (msg.wParam)
            {
            case ID_HOTKEY1:
                uiJumpToDesktop(0, FALSE);
                break;
            case ID_HOTKEY2:
                uiJumpToDesktop(1, FALSE);
                break;
            case ID_HOTKEY3:
                uiJumpToDesktop(2, FALSE);
                break;
            case ID_HOTKEY4:
                uiJumpToDesktop(3, FALSE);
                break;
            case ID_HOTKEY5:
                uiJumpToDesktop(4, FALSE);
                break;
            case ID_HOTKEY6:
                uiJumpToDesktop(5, FALSE);
                break;
            case ID_HOTKEY7:
                uiJumpToDesktop(6, FALSE);
                break;
            case ID_HOTKEY8:
                uiJumpToDesktop(7, FALSE);
                break;
            case ID_HOTKEY9:
                uiJumpToDesktop(8, FALSE);
                break;
            case ID_HOTKEY10:
                uiJumpToDesktop(0, TRUE);
                break;
            case ID_HOTKEY11:
                uiJumpToDesktop(1, TRUE);
                break;
            case ID_HOTKEY12:
                uiJumpToDesktop(2, TRUE);
                break;
            case ID_HOTKEY13:
                uiJumpToDesktop(3, TRUE);
                break;
            case ID_HOTKEY14:
                uiJumpToDesktop(4, TRUE);
                break;
            case ID_HOTKEY15:
                uiJumpToDesktop(5, TRUE);
                break;
            case ID_HOTKEY16:
                uiJumpToDesktop(6, TRUE);
                break;
            case ID_HOTKEY17:
                uiJumpToDesktop(7, TRUE);
                break;
            case ID_HOTKEY18:
                uiJumpToDesktop(8, TRUE);
                break;
            case ID_HOTKEY19:
                idx = uiGetCurrentDesktop();
                uiJumpToDesktop(idx - 1, TRUE);
                break;
            case ID_HOTKEY20:
                idx = uiGetCurrentDesktop();
                uiJumpToDesktop(idx + 1, TRUE);
                break;
            }
            break;
        default:
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

