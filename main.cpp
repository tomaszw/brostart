#include <windows.h>
#include <shobjidl_core.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <set>
#include <algorithm>
#include <iterator>
#include <vector>

using namespace std;

const CLSID CLSID_ImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 };
 
IVirtualDesktopManager *pDesktopManager = NULL;

static bool browser_shown = false;

wchar_t *browser_path;

char *window_class = "MozillaWindowClass";
char *window_title = "Mozilla Firefox";

struct find_window {
  find_window() : on_current_desktop(false), on_current_monitor(false), pid(0) { }
  string class_name;
  string title_right;
  bool on_current_desktop;
  bool on_current_monitor;
  DWORD pid;
};

struct enum_windows_param {
  find_window fw;
  bool find_all;
  HWND hwnd;
  vector<HWND> hwnd_all;

};

struct MonitorRect
{
    HMONITOR hmonitor;
    RECT rc;
    bool found;

    static BOOL CALLBACK MonitorEnum(HMONITOR hMon,HDC hdc,LPRECT lprcMonitor,LPARAM pData)
    {
        MonitorRect* pThis = reinterpret_cast<MonitorRect*>(pData);
        if (pThis->hmonitor == hMon) {
          pThis->rc = *lprcMonitor;
          pThis->found = true;
          return FALSE;
        }

        return TRUE;
    }

    MonitorRect(HMONITOR hmonitor) : hmonitor(hmonitor)
    {
        SetRectEmpty(&rc);
        EnumDisplayMonitors(0, 0, MonitorEnum, (LPARAM)this);
    }
};

BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp)
{
  enum_windows_param *p = (enum_windows_param*)lp;
  char buf[1024];

  if (!IsWindowVisible(hwnd))
    return TRUE;

  GetClassNameA(hwnd, &buf[0], sizeof(buf));
  string cname(buf);

  if (p->fw.class_name.size() && p->fw.class_name != cname)
    return TRUE;

  if (p->fw.title_right.size()) {
    GetWindowTextA(hwnd, &buf[0], sizeof(buf));
    string text(buf);
    string pat(p->fw.title_right);
    if (text.size() < pat.size())
      return TRUE;
    
    if (text.substr(text.size() - pat.size(), pat.size()) != pat)
      return TRUE;
  }

  if (p->fw.on_current_desktop) {
    BOOL on_desktop = FALSE;
    pDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &on_desktop);
    if (!on_desktop)
      return TRUE;
  }

  if (p->fw.on_current_monitor) {
    POINT cursor;
    GetCursorPos(&cursor);

    WINDOWPLACEMENT pl;
    GetWindowPlacement(hwnd, &pl);
    pl.length = sizeof(pl);

    POINT lt_window;
    lt_window.x = pl.rcNormalPosition.left;
    lt_window.y = pl.rcNormalPosition.top;
    //HMONITOR mon_window = MonitorFromRect(&pl.rcNormalPosition, MONITOR_DEFAULTTONULL);
    //if (!mon_window)
    HMONITOR mon_window = MonitorFromPoint(lt_window, MONITOR_DEFAULTTONULL);
    HMONITOR mon_cursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);

    if (!mon_window || !mon_cursor || (mon_window != mon_cursor))
      return TRUE;
  }

  if (p->fw.pid) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != p->fw.pid)
      return TRUE;
  }
  /* found window */
  if (p->find_all) {
    p->hwnd_all.push_back(hwnd);
    return TRUE;
  } else {
    p->hwnd = hwnd;
    return FALSE;
  }
}

HWND search(struct find_window fw)
{
  enum_windows_param param;
  param.hwnd = 0;
  param.fw = fw;
  param.find_all = false;

  EnumWindows(enum_windows_cb, (LPARAM) &param);

  return param.hwnd;
}

vector<HWND> search_all(struct find_window fw)
{
  enum_windows_param param;
  param.hwnd = 0;
  param.fw = fw;
  param.find_all = true;

  EnumWindows(enum_windows_cb, (LPARAM) &param);

  return param.hwnd_all;
}

static void move_to_current_monitor(HWND hwnd)
{
    POINT cursor;
    GetCursorPos(&cursor);

    HMONITOR mon_cursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
    if (!mon_cursor)
      return;
    
    MonitorRect mrc(mon_cursor);
    if (mrc.found) {
      RECT rc = mrc.rc;
      int w = rc.right - rc.left;
      int h = rc.bottom - rc.top;

      int shrinkw = w/40;
      int shrinkh = h/40;
      rc.left += shrinkw;
      rc.top += shrinkw;
      rc.right -= shrinkh;
      rc.bottom -= shrinkh;

      WINDOWPLACEMENT pl;

      pl.length = sizeof(pl);
      GetWindowPlacement(hwnd, &pl);
      pl.rcNormalPosition = rc;
      pl.ptMaxPosition.x = rc.left;
      pl.ptMaxPosition.y = rc.top;
      pl.showCmd = SW_SHOWNORMAL;
      SetWindowPlacement(hwnd, &pl);
      MoveWindow(hwnd, rc.left, rc.top, rc.right-rc.left,rc.bottom-rc.top, TRUE);
    }
}

void window_to_front(HWND hwnd)
{
  WINDOWPLACEMENT pl;
  pl.length = sizeof(pl);
  GetWindowPlacement(hwnd, &pl);
  if (pl.showCmd == SW_SHOWMINIMIZED)
  {
    ShowWindow(hwnd, SW_RESTORE);
  }
  SetForegroundWindow(hwnd);
}

static void browser_start()
{
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;

  ZeroMemory( &si, sizeof(si) );
  si.cb = sizeof(si);
  ZeroMemory( &pi, sizeof(pi) );

  find_window fw;
  fw.class_name = window_class;
  fw.title_right = window_title;
  fw.on_current_desktop = true;
  fw.on_current_monitor = false;
  fw.pid = 0;

  vector<HWND> windows_before = search_all(fw);

  // Start the child process. 
  if( !CreateProcessW( NULL,   // No module name (use command line)
      browser_path,        // Command line
      NULL,           // Process handle not inheritable
      NULL,           // Thread handle not inheritable
      FALSE,          // Set handle inheritance to FALSE
      0,              // No creation flags
      NULL,           // Use parent's environment block
      NULL,           // Use parent's starting directory 
      &si,            // Pointer to STARTUPINFO structure
      &pi )           // Pointer to PROCESS_INFORMATION structure
  ) 
  {
      printf( "CreateProcess failed (%d).\n", GetLastError() );
      return;
  }

  vector<HWND> windows_after;
  int max_iters = 8;
  int i = 0;
  for (;;) {
    windows_after = search_all(fw);
    if (windows_after == windows_before) {
      if (i >= max_iters)
        break;
      i++;
      Sleep(200);
      continue;
    }

    vector<HWND> diff;
    set_difference(windows_after.begin(), windows_after.end(),
      windows_before.begin(), windows_before.end(),
      inserter(diff, diff.end()));
    
    if (diff.begin() != diff.end()) {
      HWND hnew = *diff.begin();
      //printf("found new window %p\n", hnew);
      move_to_current_monitor(hnew);
    }
    break;
  }
}

#ifdef CONSOLEAPP
int main()
#else
int WinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPSTR     lpCmdLine,
  int       nShowCmd
)
#endif
{ 
  wchar_t **_argv;
  int _argc;
  _argv = CommandLineToArgvW(GetCommandLineW(), &_argc);

  if (_argc > 1) {
    browser_path = _argv[1];
  if (!browser_path)
    browser_path = L"C:\\Program Files (x86)\\Mozilla Firefox\\firefox.exe";
  }
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

  find_window fw;
  fw.class_name = window_class;
  fw.title_right = window_title;
  fw.on_current_desktop = true;
  fw.on_current_monitor = true;
  fw.pid = 0;

  vector<HWND> wnds = search_all(fw);
  sort(wnds.begin(), wnds.end());
  // if no windows, start browser
  if (wnds.size() == 0) {
    browser_start();
    return 0;
  }
  // if browser window already in foreground, cycle to next window
  HWND fore = GetForegroundWindow();
  for (int i = 0; i < wnds.size(); i++) {
    if (wnds[i] == fore) {
      // cycle to next window
      HWND hwnd = wnds[(i+1) % wnds.size()];
      window_to_front(hwnd);
      return 0;
    }
  }
  // if browser window not in foreground, bring first one to fg
  window_to_front(wnds[0]);

  return 0;
}
