#include <windows.h>
#include <shellapi.h>
#include <shobjidl_core.h>
#include <stdio.h>

#include <algorithm>
#include <iterator>
#include <set>
#include <string>
#include <vector>
#include <stdarg.h>

//#define printf log_stuff
#define printf

using namespace std;

void log_stuff(const char *fmt, ...)
{
  char buffer[512] = { 0 };
  va_list vl;
  va_start(vl, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, vl);
  va_end(vl);

  FILE *f = fopen("c:\\h\\log-brostart.txt", "a+");
  if (f) {
    fputs(buffer, f);
    fclose(f);
  };
}

const CLSID CLSID_ImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39};

IVirtualDesktopManager *pDesktopManager = NULL;

static bool browser_shown = false;

wchar_t *browser_path;

char *window_class = "MozillaWindowClass";
char *window_title = "Mozilla Firefox";

struct find_window {
  find_window()
      : on_current_desktop(false), on_current_monitor(false), pid(0) {}
  string class_name;
  string title_right;
  bool on_current_desktop;
  bool on_current_monitor;
  DWORD pid;
};

find_window findw;

struct enum_windows_param {
  find_window fw;
  bool find_all;
  HWND hwnd;
  vector<HWND> hwnd_all;
};

string desc_window(HWND hwnd)
{
  if (!hwnd)
    return "[HWND null]";

  char buf[512];
  char text[256];

  GetWindowTextA(hwnd, text, sizeof(text));
  sprintf(buf, "[HWND %p] %s", hwnd, text);
  return string(buf);
}

BOOL CALLBACK enum_windows_cb(HWND hwnd, LPARAM lp) {
  enum_windows_param *p = (enum_windows_param *)lp;
  char buf[1024];

  if (!IsWindowVisible(hwnd)) return TRUE;

  GetClassNameA(hwnd, &buf[0], sizeof(buf));
  string cname(buf);

  if (p->fw.class_name.size() && p->fw.class_name != cname) return TRUE;

  GetWindowTextA(hwnd, &buf[0], sizeof(buf));
  string text(buf);
  if (p->fw.title_right.size()) {
    string pat(p->fw.title_right);
    if (text.size() < pat.size()) return TRUE;

    if (text.substr(text.size() - pat.size(), pat.size()) != pat) return TRUE;
  }

  if (p->fw.on_current_desktop) {
    BOOL on_desktop = FALSE;
    pDesktopManager->IsWindowOnCurrentVirtualDesktop(hwnd, &on_desktop);
    if (!on_desktop) return TRUE;
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
    // HMONITOR mon_window = MonitorFromRect(&pl.rcNormalPosition,
    // MONITOR_DEFAULTTONULL); if (!mon_window)
    //HMONITOR mon_window = MonitorFromPoint(lt_window, MONITOR_DEFAULTTONULL);
    HMONITOR mon_window = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL);
    HMONITOR mon_cursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);

    //printf("mon_window=%p mon_cursor=%p mon_window2=%p\n", mon_window, mon_cursor, MonitorFromWindow(hwnd, MONITOR_DEFAULTTONULL));
    if (!mon_window || !mon_cursor || (mon_window != mon_cursor)) return TRUE;
  }

    printf("TEXT is %s\n", text.c_str());
  if (p->fw.pid) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != p->fw.pid) return TRUE;
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

HWND search(struct find_window fw) {
  enum_windows_param param;
  param.hwnd = 0;
  param.fw = fw;
  param.find_all = false;

  EnumWindows(enum_windows_cb, (LPARAM)&param);

  return param.hwnd;
}

vector<HWND> search_all(struct find_window fw) {
  enum_windows_param param;
  param.hwnd = 0;
  param.fw = fw;
  param.find_all = true;

  EnumWindows(enum_windows_cb, (LPARAM)&param);

  return param.hwnd_all;
}

static void try_move_to_monitor(HWND hwnd, HMONITOR mon_cursor)
{
  MONITORINFO mi;
  mi.cbSize = sizeof(mi);

  GetMonitorInfo(mon_cursor, &mi);

  RECT rc = mi.rcWork;
  RECT smallrc = rc;

  int w = rc.right - rc.left;
  int h = rc.bottom - rc.top;

  int shrinkw = w / 8;
  int shrinkh = h / 40;
  smallrc.left += shrinkw;
  smallrc.top += shrinkh;
  smallrc.right -= shrinkw;
  smallrc.bottom -= shrinkh;

  printf("%d %d %d %d\n", rc.left, rc.top, w, h);
  SetWindowPos(hwnd, 0, rc.left, rc.top, rc.right-rc.left,rc.bottom-rc.top,
    0);
  WINDOWPLACEMENT pl;
  memset(&pl, 0, sizeof(pl));
  pl.length = sizeof(pl);
  pl.rcNormalPosition = smallrc;
  pl.ptMinPosition.x = rc.left;
  pl.ptMinPosition.y = rc.top;
  pl.ptMaxPosition.x = rc.left;
  pl.ptMaxPosition.y = rc.top;
  pl.showCmd = SW_SHOWMAXIMIZED;
  SetWindowPlacement(hwnd, &pl);
}

void window_to_front(HWND hwnd) {
  WINDOWPLACEMENT pl;
  pl.length = sizeof(pl);
  GetWindowPlacement(hwnd, &pl);
  if (pl.showCmd == SW_SHOWMINIMIZED) {
    ShowWindow(hwnd, SW_RESTORE);
  }
  SetForegroundWindow(hwnd);
}

static void move_to_current_monitor(HWND hwnd)
{
  POINT cursor;
  GetCursorPos(&cursor);

  HMONITOR mon_cursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);
  if (!mon_cursor)
    return;

  try_move_to_monitor(hwnd, mon_cursor);
}

static void app_start() {
  STARTUPINFOW si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  find_window fw;
  fw = findw;
  fw.on_current_desktop = true;
  fw.on_current_monitor = false;
  fw.pid = 0;

  vector<HWND> windows_before = search_all(fw);

  // Start the child process.
  if (!CreateProcessW(NULL,          // No module name (use command line)
                      browser_path,  // Command line
                      NULL,          // Process handle not inheritable
                      NULL,          // Thread handle not inheritable
                      FALSE,         // Set handle inheritance to FALSE
                      0,             // No creation flags
                      NULL,          // Use parent's environment block
                      NULL,          // Use parent's starting directory
                      &si,           // Pointer to STARTUPINFO structure
                      &pi)           // Pointer to PROCESS_INFORMATION structure
  ) {
    printf("CreateProcess failed (%d).\n", GetLastError());
    return;
  }

  vector<HWND> windows_after;
  int max_iters = 100;
  int i = 0;
  for (;;) {
    windows_after = search_all(fw);
    vector<HWND> diff;
    set_difference(windows_after.begin(), windows_after.end(),
                   windows_before.begin(), windows_before.end(),
                   inserter(diff, diff.end()));

    if (!diff.size()) {
      if (i >= max_iters) break;
      i++;
      Sleep(10);
      continue;
    }
    HWND hnew = *diff.begin();
    move_to_current_monitor(hnew);
    break;
  }
}

//#define CONSOLEAPP

#ifdef CONSOLEAPP
int main()
#else
int WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,
            int nShowCmd)
#endif
{
  wchar_t **_argv;
  int _argc;
  _argv = CommandLineToArgvW(GetCommandLineW(), &_argc);

  if (_argc > 1) {
    browser_path = _argv[1];
  }
  if (!browser_path)
    browser_path = L"C:\\Program Files (x86)\\Mozilla Firefox\\firefox.exe";
  printf("foobar\n");
  printf("%ls\n", browser_path);
  IServiceProvider *pServiceProvider = NULL;
  CoInitializeEx(NULL, 0);
  HRESULT hr = ::CoCreateInstance(
      CLSID_ImmersiveShell, NULL, CLSCTX_LOCAL_SERVER,
      __uuidof(IServiceProvider), (PVOID *)&pServiceProvider);
  if (FAILED(hr)) {
    printf("cocreate failed\n");
    exit(1);
  }

  hr = pServiceProvider->QueryService(__uuidof(IVirtualDesktopManager),
                                      &pDesktopManager);
  if (!pDesktopManager) {
    printf("no desktop manager: hr=%x\n", hr);
    exit(1);
  }

  findw.class_name = window_class;
  findw.title_right = window_title;
  findw.on_current_desktop = true;
  findw.on_current_monitor = true;
  findw.pid = 0;

  vector<HWND> wnds = search_all(findw);
  HWND top = 0;
  if (wnds.size())
    top = wnds[0];

  sort(wnds.begin(), wnds.end());
  for (int i = 0; i < wnds.size(); i++) {
    printf("%d: %s\n", i, desc_window(wnds[i]).c_str());
  }

  // if no windows, start browser
  if (wnds.size() == 0) {
    printf("no windows, start app\n");
    app_start();
    return 0;
  }
  // if browser window already in foreground, cycle to next window
  HWND fore = top;// GetForegroundWindow();
  for (int i = 0; i < wnds.size(); i++) {
    if (wnds[i] == fore) {
      // cycle to next window
      HWND hwnd = wnds[(i + 1) % wnds.size()];
      printf("cycle app from %s to %s\n", desc_window(fore).c_str(), desc_window(hwnd).c_str());
      window_to_front(hwnd);
      return 0;
    }
  }
  // if browser window not in foreground, bring first one to fg
  printf("win to front\n");
  window_to_front(wnds[0]);

  return 0;
}
