#include <windows.h>
#include <shobjidl_core.h>
#include <stdio.h>
#include <string>


using namespace std;

const CLSID CLSID_ImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 };
 
IVirtualDesktopManager *pDesktopManager = NULL;

BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp)
{
  char buf[1024];

  GetClassNameA(hwnd, &buf[0], sizeof(buf));

  string cname(buf);
  if (cname == "MozillaWindowClass") {
    GetWindowTextA(hwnd, &buf[0], sizeof(buf));
    string text(buf);
    string pat("Mozilla Firefox");
    if (text.size() >= pat.size()) {
      if (text.substr(text.size() - pat.size(), pat.size()) == pat) {
        BOOL on_desktop = FALSE;
        pDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &on_desktop);

        if (!IsWindowVisible(hwnd) || !on_desktop)
          return TRUE;

        POINT cursor;
        GetCursorPos(&cursor);

        WINDOWPLACEMENT pl;
        GetWindowPlacement(hwnd, &pl);
        pl.length = sizeof(pl);

        POINT lt_window;
        lt_window.x = pl.rcNormalPosition.left;
        lt_window.y = pl.rcNormalPosition.top;
        HMONITOR mon_window = MonitorFromRect(&pl.rcNormalPosition, MONITOR_DEFAULTTONULL);
        HMONITOR mon_cursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);

        if (!mon_window || !mon_cursor || (mon_window != mon_cursor))
          return TRUE;

        RECT rc = pl.rcNormalPosition;
        printf("%s\n", text.c_str());
        printf("%d %d %d %d\n", rc.left, rc.top, rc.right, rc.bottom);
        if (pl.showCmd == SW_SHOWMAXIMIZED) {
          ShowWindow(hwnd, SW_SHOWMAXIMIZED);
        } else if (pl.showCmd == SW_SHOWMINIMIZED) {
          ShowWindow(hwnd, SW_RESTORE);
        } else {
          ShowWindow(hwnd, SW_SHOWNORMAL);
        }
        
        printf("cursor @ %d,%d\n", cursor.x, cursor.y);
        //BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        return FALSE;
      }
    }
  }
  return TRUE;
}

int main() {
  IServiceProvider* pServiceProvider = NULL;
  CoInitializeEx(NULL, 0);
  HRESULT hr = ::CoCreateInstance(
      CLSID_ImmersiveShell, NULL, CLSCTX_LOCAL_SERVER,
      __uuidof(IServiceProvider), (PVOID*)&pServiceProvider);
  if (FAILED(hr)) {
    printf("cocreate failed\n");
    exit(1);
  }

  hr = pServiceProvider->QueryService(__uuidof(IVirtualDesktopManager), &pDesktopManager);
  if (!pDesktopManager) {
    printf("no desktop manager: hr=%x\n", hr);
    exit(1);
  }


  EnumWindows(enum_windows_cb, NULL);
}
