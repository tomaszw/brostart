#include <windows.h>
#include <shobjidl_core.h>
#include <stdio.h>
#include <string>


using namespace std;

const CLSID CLSID_ImmersiveShell = {
    0xC2F03A33, 0x21F5, 0x47FA, 0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39 };
 
IVirtualDesktopManager *pDesktopManager = NULL;

static bool browser_shown = false;

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
  HWND hwnd;
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
    HMONITOR mon_window = MonitorFromRect(&pl.rcNormalPosition, MONITOR_DEFAULTTONULL);
    if (!mon_window)
      mon_window = MonitorFromPoint(lt_window, MONITOR_DEFAULTTONULL);
    HMONITOR mon_cursor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONULL);

    if (!mon_window || !mon_cursor || (mon_window != mon_cursor))
      return TRUE;
  }

    //printf("%d\n", (int)pid);
  if (p->fw.pid) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != p->fw.pid)
      return TRUE;
  }
  /* found window */
  p->hwnd = hwnd;
  return FALSE;
}

HWND search(struct find_window fw)
{
  enum_windows_param param;
  param.hwnd = 0;
  param.fw = fw;

  EnumWindows(enum_windows_cb, (LPARAM) &param);

  return param.hwnd;
}

static void browser_start()
{
  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory( &si, sizeof(si) );
  si.cb = sizeof(si);
  ZeroMemory( &pi, sizeof(pi) );

  // Start the child process. 
  if( !CreateProcessA( NULL,   // No module name (use command line)
      "C:\\Program Files (x86)\\Mozilla Firefox\\firefox.exe",        // Command line
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

  find_window fw;
  fw.pid = pi.dwProcessId;

  //for (;;) {
    HWND hwnd = search(fw);
    printf("hwnd=%p\n", hwnd);
    //if (hwnd)
      //break;
    //Sleep(200);
  //}
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

  find_window fw;
  fw.class_name = "MozillaWindowClass";
  fw.title_right = "Mozilla Firefox";
  fw.on_current_desktop = true;
  fw.on_current_monitor = true;
  fw.pid = 0;

  HWND hwnd = search(fw);
  if (hwnd) {
    WINDOWPLACEMENT pl;
    pl.length = sizeof(pl);
    GetWindowPlacement(hwnd, &pl);
    if (pl.showCmd == SW_SHOWMINIMIZED) {
      ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
  } else {
    browser_start();
  }
}
