/*
    Copyright (C) 1998-2001 by Jorrit Tyberghein

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "cssysdef.h"
#include <ctype.h>
#include <stdarg.h>
#include "csutil/csuctransform.h"
#include "csutil/event.h"
#include "csutil/eventnames.h"
#include "csutil/eventhandlers.h"
#include "csutil/scf_implementation.h"
#include "csutil/ref.h"
#include "csutil/refarr.h"
#include "csutil/sysfunc.h"
#include "csutil/syspath.h"
#include "csutil/win32/win32.h"
#include "iutil/cfgmgr.h"
#include "iutil/event.h"
#include "iutil/eventh.h"
#include "iutil/eventq.h"
#include "iutil/cmdline.h"
#include "iutil/objreg.h"
#include "ivideo/natwin.h"
#include "ivideo/cursor.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <imm.h>

#include "csutil/win32/wintools.h"
#include "win32kbd.h"
#include "csutil/win32/cachedll.h"

#include <stdio.h>
#include <time.h>

#if defined(CS_COMPILER_MSVC)
#include <sys/timeb.h>
#endif

#if defined(CS_COMPILER_BCC)
#include <dos.h> // For _argc & _argv
#endif

#if defined(__CYGWIN__)
// Cygwin doesn't understand _argc or _argv, so we define them here.
// These are borrowed from Mingw32 includes (see stdlib.h)
// Cygwin Purists, forgive the corruption, Cygwin means Cygnus for Win32.
extern int CS_WIN32_ARGC;
extern char** CS_WIN32_ARGV;
#endif

void SystemFatalError (const char *s)
{
  ChangeDisplaySettings (0, 0);  // doesn't hurt
  csPrintfErr ("FATAL: %s\n", s);
  MessageBoxW (0, csCtoW (s), L"Fatal Error", MB_OK | MB_ICONSTOP);
}

#define MAX_SCANCODE 0x100

class Win32Assistant : public scfImplementation3<Win32Assistant,
                                                 iWin32Assistant,
                                                 iEventPlug,
                                                 iEventHandler>
{
private:
  bool ApplicationActive;
  HINSTANCE ModuleHandle;
  int ApplicationShow;

  csRef<iObjectRegistry> registry;
  /// is a console window to be displayed?
  bool console_window;
  /// is the binary linked as GUI or console app?
  bool is_console_app;
  /// is command line help requested?
  bool cmdline_help_wanted;
  /// use our own message loop
  bool use_own_message_loop;

  HCURSOR m_hCursor;
  csRef<iEventOutlet> EventOutlet;
  csRef<csWin32KeyboardDriver> kbdDriver;

  int mouseButtons;

  /// The console codepage that was set on program startup. Will be restored on exit.
  //UINT oldCP;

  CS_DECLARE_SYSTEM_EVENT_SHORTCUTS;
  CS_DECLARE_FRAME_EVENT_SHORTCUTS;
  csEventID Quit;
  csEventID CommandLineHelp;
  csEventID FocusGained;
  csEventID FocusLost;

  /// The window class name for this assistant
  uint8* windowClass;

  static LRESULT CALLBACK WindowProc (HWND hWnd, UINT message,
    WPARAM wParam, LPARAM lParam);
  static LRESULT CALLBACK CBTProc (int nCode, WPARAM wParam, LPARAM lParam);
  static BOOL WINAPI ConsoleHandlerRoutine (DWORD dwCtrlType);
public:
  Win32Assistant (iObjectRegistry*);
  virtual ~Win32Assistant ();
  virtual void Shutdown();
  virtual HINSTANCE GetInstance () const;
  virtual bool GetIsActive () const;
  virtual int GetCmdShow () const;
  virtual bool SetCursor (int cursor);
  virtual bool SetHCursor (HCURSOR);
  virtual bool HandleEvent (iEvent&);
  virtual unsigned GetPotentiallyConflictingEvents ();
  virtual unsigned QueryEventPriority (unsigned);
  virtual void DisableConsole ();
  void AlertV (HWND window, int type, const char* title, 
    const char* okMsg, const char* msg, va_list args);

  virtual void UseOwnMessageLoop(bool ownmsgloop);
  virtual bool HasOwnMessageLoop();
  virtual HWND CreateCSWindow (iGraphics2D* canvas,
    DWORD exStyle, DWORD style, int x,
    int y, int w, int h);

  iEventOutlet* GetEventOutlet();

  /**
   * Handle a keyboard-related Windows message.
   */
  bool HandleKeyMessage (HWND hWnd, UINT message,
    WPARAM wParam, LPARAM lParam);

  CS_EVENTHANDLER_PHASE_LOGIC ("crystalspace.win32")
};

//static Win32Assistant* GLOBAL_ASSISTANT = 0;
static csRefArray<Win32Assistant,CS::Memory::AllocatorMallocPlatform> assistants;

static void ToLower (csString& s)
{
  for (size_t i = 0; i < s.Length(); i++)
    s[i] = tolower (s[i]);
}

static inline bool AddToPathEnv (csString dir, csString& pathEnv)
{
  // check if installdir is in the path.
  bool gotpath = false;

  size_t dlen = dir.Length();
  // csGetInstallDir() might return "" (current dir)
  if (dlen != 0)
  {
    ToLower (dir);
  
    if (!pathEnv.IsEmpty())
    {
      csString mypath (pathEnv);
      ToLower (mypath);

      const char* ppos = strstr (mypath.GetData(), dir);
      while (!gotpath && ppos)
      {
        const char* npos = strchr (ppos, ';');
        size_t len = npos ? npos - ppos : strlen (ppos);

        if ((len == dlen) || (len == dlen+1))
        {
          if (ppos[len] == '\\') len--;
	  if (!strncmp (ppos, dir, len))
	  {
	    // found it
	    gotpath = true;
	  }
        }
        ppos = npos ? strstr (npos+1, dir) : 0;
      }
    }

    if (!gotpath)
    {
      // put CRYSTAL path into PATH environment.
      csString newpath;
      newpath.Append (dir);
      newpath.Append (";");
      newpath.Append (pathEnv);
      pathEnv = newpath;
      return true;
    }
  }
  return false;
}

typedef void (WINAPI * LPFNSETDLLDIRECTORYA)(LPCSTR lpPathName);
typedef BOOL (WINAPI * LPFNSETPROCESSDPIAWARE)();

#include "csutil/custom_new_disable.h"
bool csPlatformStartup(iObjectRegistry* r)
{
  /* Work around QueryPerformanceCounter() issues on multiprocessor systems.
   * @@@ FIXME: until Marten(or someone else) finds a better solution ... 
   */
  SetThreadAffinityMask (GetCurrentThread(), 1);

  /* Mark program as DPI aware on Vista to prevent automatic scaling
     by the system on high resolution screens */
  {
    CS::Platform::Win32::CacheDLL hUser32 ("user32.dll");
    CS_ASSERT (hUser32 != 0);
    LPFNSETPROCESSDPIAWARE SetProcessDPIAware =
      (LPFNSETPROCESSDPIAWARE)GetProcAddress (hUser32, "SetProcessDPIAware");
    if (SetProcessDPIAware) SetProcessDPIAware();
  }
  
  csRef<iCommandLineParser> cmdline (csQueryRegistry<iCommandLineParser> (r));

  csPathsList* pluginpaths = csGetPluginPaths (cmdline->GetAppPath());

  /*
    When it isn't already in the PATH environment,
    the CS directory will be put there in front of all
    other paths.
    The idea is that DLLs required by plugins (e.g. zlib)
    which reside in the CS directory can be found by the
    OS even if the application is somewhere else.
   */
  bool needPATHpatch = true;

#if 0
  // @@@ doesn't seem to work in some cases?
  /*
    WinXP SP 1 has a nice function that does exactly that: setting
    a number of search paths for DLLs. However, it's WinXP SP 1+,
    so we have to check if it's available, and if not, patch the PATH
    env var.
   */
  cswinCacheDLL hKernel32 ("kernel32.dll");
  if (hKernel32 != 0)
  {
    LPFNSETDLLDIRECTORYA SetDllDirectoryA =
      (LPFNSETDLLDIRECTORYA)GetProcAddress (hKernel32, "SetDllDirectoryA");

    if (SetDllDirectoryA)
    {
      csString path;

      for (int i = 0; i < pluginpaths->Length(); i++)
      {
	if (((*pluginpaths)[i].path != 0) && (*((*pluginpaths)[i].path) != 0))
	{
	  path << (*pluginpaths)[i].path;
	  path << ';';
	}
      }

      if (path.Length () > 0) SetDllDirectoryA (path.GetData ());
      needPATHpatch = false;
    }
  }
#endif

  if (needPATHpatch)
  {
    csString pathEnv (getenv("PATH"));
    bool pathChanged = false;

    for (size_t i = 0; i < pluginpaths->Length(); i++)
    {
      // @@@ deal with path recursion here?
      if (AddToPathEnv ((*pluginpaths)[i].path, pathEnv)) pathChanged = true;
    }
    if (pathChanged) SetEnvironmentVariable ("PATH", pathEnv);
  }

  delete pluginpaths;


  csRef<Win32Assistant> a = csPtr<Win32Assistant> (new Win32Assistant(r));
  bool ok = r->Register (static_cast<iWin32Assistant*>(a), "iWin32Assistant");
  if (ok)
  {
    assistants.Push (a);
  }
  else
  {
    SystemFatalError ("Failed to register iWin32Assistant!");
  }

  return ok;
}
#include "csutil/custom_new_enable.h"

bool csPlatformShutdown(iObjectRegistry* r)
{
  csRef<iWin32Assistant> assi = csQueryRegistryTagInterface<iWin32Assistant> 
    (r, "iWin32Assistant");
  if (assi.IsValid())
  {
    r->Unregister(assi, "iWin32Assistant");
    Win32Assistant* a = (Win32Assistant*)((iWin32Assistant*)assi);
    a->Shutdown();
    assistants.Delete (a);
    return true;
  }
  return false;
}

BOOL WINAPI Win32Assistant::ConsoleHandlerRoutine (DWORD dwCtrlType)
{
  BOOL result = FALSE;
  switch (dwCtrlType)
  {
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
      {
	for (size_t i = 0; i < assistants.GetSize (); i++)
	{
	  assistants[i]->GetEventOutlet()->ImmediateBroadcast (
	    csevQuit (assistants[i]->registry), 0);
	  result = TRUE;
	}
      }
  }
  return result;
}

/// Determine whether a standard handle was redirected to a file
static bool IsStdHandleRedirected (DWORD nHandle)
{
  HANDLE file = GetStdHandle (nHandle);
  return (file != INVALID_HANDLE_VALUE) && (file != NULL) 
    && (GetFileType (file) != FILE_TYPE_CHAR);
}

Win32Assistant::Win32Assistant (iObjectRegistry* r) 
  : scfImplementationType (this),
  ApplicationActive (true),
  ModuleHandle (0),
  console_window (false),  
  is_console_app(false),
  cmdline_help_wanted(false),
  EventOutlet (0),
  mouseButtons(0)
{
  ModuleHandle = GetModuleHandle(0);
  STARTUPINFO startupInfo;
  GetStartupInfo (&startupInfo);
  if (startupInfo.dwFlags & STARTF_USESHOWWINDOW)
    ApplicationShow = startupInfo.wShowWindow;
  else
    ApplicationShow = SW_SHOWNORMAL;

// Cygwin has problems with freopen()
#if defined(CS_DEBUG) || defined(__CYGWIN__)
  console_window = true;
#else
  console_window = false;
#endif

  use_own_message_loop = true;

  csRef<iCommandLineParser> cmdline (csQueryRegistry<iCommandLineParser> (r));
  console_window = cmdline->GetBoolOption ("console", console_window);

  cmdline_help_wanted = (cmdline->GetOption ("help") != 0);

  /*
     to determine if we are actually a console app we look up
     the subsystem field in the PE header.
   */
  PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)ModuleHandle;
  PIMAGE_NT_HEADERS NTheader = (PIMAGE_NT_HEADERS)((uint8*)dosHeader + dosHeader->e_lfanew);
  if (NTheader->Signature == 0x00004550) // check for PE sig
  {
    is_console_app = 
      (NTheader->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI);
  }

  /*
    - console apps won't do anything about their console... yet. 
    - GUI apps will open a console window if desired.
   */
  if (!is_console_app)
  {
    if (console_window || cmdline_help_wanted)
    {
      AllocConsole ();
      /* "Redirect" C runtime standard files to console, if not redirected 
       * by the user. */
      if (!IsStdHandleRedirected (STD_ERROR_HANDLE)) 
        freopen("CONOUT$", "a", stderr);
      if (!IsStdHandleRedirected (STD_OUTPUT_HANDLE)) 
        freopen("CONOUT$", "a", stdout);
      if (!IsStdHandleRedirected (STD_INPUT_HANDLE)) 
        freopen("CONIN$", "r", stdin);
    }
  }

  /*
    In case we're a console app, set up a control handler so
    console window closing, user logoff and system shutdown
    cause CS to properly shut down.
   */
  if (is_console_app || console_window || cmdline_help_wanted)
  {
    SetConsoleCtrlHandler (&ConsoleHandlerRoutine, TRUE);
  }

  // experimental UC stuff.
  // Retrieve old codepage.
  //oldCP = GetConsoleOutputCP ();
  // Try to set console codepage to UTF-8.
  // @@@ The drawback of UTF8 is that it requires a TrueType console
  // font to work properly. However, default is "raster font" :/
  // In this case, text output w/ non-ASCII chars will appear broken, tho
  // this is really a Windows issue. (see MS KB 99795)
  // @@@ Maybe a command line param to set a different con cp could be useful.
  // * Don't change the codepage, for now.
  //SetConsoleOutputCP (CP_UTF8);
  //

  registry = r;

  HICON appIcon;

  // try the app icon...
  appIcon = LoadIcon (ModuleHandle, MAKEINTRESOURCE (1));
  // not? maybe executable.ico?
  if (!appIcon) 
  {
    char apppath[MAX_PATH];
    GetModuleFileName (0, apppath, sizeof(apppath));

    char *dot = strrchr (apppath, '.');
    if (dot) 
    {
      strcpy (dot, ".ico");
    }
    else
    {
      strcat (apppath, ".ico");
    }
    appIcon = (HICON)LoadImageA (ModuleHandle, apppath, IMAGE_ICON,
      0, 0, LR_DEFAULTSIZE | LR_LOADFROMFILE);
  }
  // finally the default one
  if (!appIcon) appIcon = LoadIcon (0, IDI_APPLICATION);
  
  csString wndClass;
  wndClass.Format ("CrystalSpaceWin32_%p", (void*)this);

  bool bResult = false;
  HBRUSH backBrush = (HBRUSH)::GetStockObject (BLACK_BRUSH);
  if (cswinIsWinNT ())
  {
    WNDCLASSEXW wc;
    
    windowClass = new uint8[(wndClass.Length()+1) * sizeof (WCHAR)];
    csUnicodeTransform::UTF8toWC ((wchar_t*)windowClass,
      wndClass.Length()+1, (utf8_char*)(wndClass.GetData()), (size_t)-1);

    wc.cbSize	      = sizeof (wc);
    wc.hCursor        = 0;
    wc.hIcon          = appIcon;
    wc.lpszMenuName   = 0;
    wc.lpszClassName  = (WCHAR*)windowClass;
    wc.hbrBackground  = backBrush;
    wc.hInstance      = ModuleHandle;
    wc.style          = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc    = WindowProc;
    wc.cbClsExtra     = 0;
    wc.cbWndExtra     = 2*sizeof (LONG_PTR);
    wc.hIconSm	      = 0;
    bResult = RegisterClassExW (&wc) != 0;
  }
  else
  {
    WNDCLASSEXA wc;
    
    windowClass = new uint8[wndClass.Length()+1];
    strcpy ((char*)windowClass, wndClass);

    wc.cbSize	      = sizeof (wc);
    wc.hCursor        = 0;
    wc.hIcon          = appIcon;
    wc.lpszMenuName   = 0;
    wc.lpszClassName  = (char*)windowClass;
    wc.hbrBackground  = backBrush;
    wc.hInstance      = ModuleHandle;
    wc.style          = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc    = WindowProc;
    wc.cbClsExtra     = 0;
    wc.cbWndExtra     = 2*sizeof (LONG_PTR);
    wc.hIconSm	      = 0;
    bResult = RegisterClassExA (&wc) != 0;
  }

  if (!bResult)
  {
    SystemFatalError ("Failed to register window class!");
  }

  m_hCursor = LoadCursor (0, IDC_ARROW);

  csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (registry));
  CS_ASSERT (q != 0);
  csEventID events[] = {
    csevFrame (registry),
    csevSystemOpen (registry),
    csevSystemClose (registry),
    csevCommandLineHelp (registry),
    CS_EVENTLIST_END
  };
  q->RegisterListener (this, events);
  CS_INITIALIZE_SYSTEM_EVENT_SHORTCUTS (registry);
  CS_INITIALIZE_FRAME_EVENT_SHORTCUTS (registry);
  Quit = csevQuit (registry);
  CommandLineHelp = csevCommandLineHelp (registry);
  FocusGained = csevFocusGained (registry);
  FocusLost = csevFocusLost (registry);
  //CanvasExposed = csevCanvasExposed (registry, "graph2d");
  //CanvasHidden = csevCanvasHidden (registry, "graph2d");

  // Put our own keyboard driver in place.
#include "csutil/custom_new_disable.h"
  kbdDriver.AttachNew (new csWin32KeyboardDriver (r));
  if (kbdDriver == 0)
  {
    SystemFatalError ("Failed to create keyboard driver!");
  }

  csRef<iBase> currentKbd = 
    csQueryRegistryTag (r, "iKeyboardDriver");
  if (currentKbd != 0)
  {
    // Bit hacky: remove old keyboard driver
    csRef<iEventHandler> eh =  
      scfQueryInterface<iEventHandler> (currentKbd);
    q->RemoveListener (eh);
    r->Unregister (currentKbd, "iKeyboardDriver");
  }
  r->Register (kbdDriver, "iKeyboardDriver");
}
#include "csutil/custom_new_enable.h"

Win32Assistant::~Win32Assistant ()
{
  //SetConsoleOutputCP (oldCP);
  if (!is_console_app && (console_window || cmdline_help_wanted))
    FreeConsole();
  if (cswinIsWinNT ())
    UnregisterClassW ((WCHAR*)windowClass, ModuleHandle);
  else
    UnregisterClassA ((char*)windowClass, ModuleHandle); 
  delete[] windowClass;
}

void Win32Assistant::Shutdown()
{
  csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (registry));
  if (q != 0)
    q->RemoveListener(this);
  if (!is_console_app && (cmdline_help_wanted || console_window))
  {
    HANDLE hConsole = CreateFile ("CONIN$", GENERIC_READ, FILE_SHARE_READ, 0, 
      OPEN_EXISTING, 0, 0);
    if (hConsole != 0)
    {
      INPUT_RECORD ir;
      DWORD events_read;
      /* Empty console events, to remove earlier key presses */
      while (PeekConsoleInput (hConsole, &ir, 1, &events_read)
	&& (events_read != 0))
      {
	ReadConsoleInput (hConsole, &ir, 1, &events_read);
      }
      
      csPrintf ("\nPress a key to close this window...");
      fflush (stdout);
      /* Wait for keyboard event */
      do 
      {
	ReadConsoleInput (hConsole, &ir, 1, &events_read);
      } while ((events_read == 0) || (ir.EventType != KEY_EVENT));
      CloseHandle (hConsole);
    }
  }
}

bool Win32Assistant::SetHCursor (HCURSOR cur)
{
  m_hCursor = cur;
  ::SetCursor (cur);
  return true;
}

unsigned Win32Assistant::GetPotentiallyConflictingEvents ()
  { return CSEVTYPE_Keyboard | CSEVTYPE_Mouse; }
unsigned Win32Assistant::QueryEventPriority (unsigned /*iType*/)
  { return 100; }

iEventOutlet* Win32Assistant::GetEventOutlet()
{
  if (!EventOutlet.IsValid())
  {
    csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (registry));
    if (q != 0)
      EventOutlet = q->CreateEventOutlet(this);
  }
  return EventOutlet;
}

bool Win32Assistant::HandleEvent (iEvent& e)
{
  if (e.Name == Frame)
  {
    if(use_own_message_loop)
    {
      MSG msg;
      /*
        [res] *W versions of the message queue functions exist,
        but they don't seem to make a difference.
      */
      while (PeekMessage (&msg, 0, 0, 0, PM_NOREMOVE))
      {
        if (!GetMessage (&msg, 0, 0, 0))
        {
          iEventOutlet* outlet = GetEventOutlet();
          outlet->Broadcast (Quit);
          return true;
        }
        TranslateMessage (&msg);
        DispatchMessage (&msg);
      }
    }
    return true;
  }
  else if (e.Name == SystemOpen)
  {
    return true;
  }
  else if (e.Name == SystemClose)
  {
  } 
  else if (e.Name == CommandLineHelp)
  {

   #ifdef CS_DEBUG 
    const char *defcon = "yes";
   #else
    const char *defcon = "no";
   #endif

    csPrintf ("Win32-specific options:\n");
    csPrintf ("  -[no]console       Create a debug console (default = %s)\n",     
      defcon);
  }
  return false;
}

HINSTANCE Win32Assistant::GetInstance () const
{
  return ModuleHandle;
}

bool Win32Assistant::GetIsActive () const
{
  return ApplicationActive;
}

int Win32Assistant::GetCmdShow () const
{
  return ApplicationShow;
}

//----------------------------------------// Windows input translator //------//

#ifndef WM_MOUSEWHEEL
#define	WM_MOUSEWHEEL	0x020a
#endif

#ifndef WM_XBUTTONDOWN
#define	WM_XBUTTONDOWN	0x020b
#endif

#ifndef WM_XBUTTONUP
#define	WM_XBUTTONUP	0x020c
#endif

struct CreateInfo
{
  Win32Assistant* assistant;
  iGraphics2D* canvas;
};

LRESULT CALLBACK Win32Assistant::WindowProc (HWND hWnd, UINT message,
  WPARAM wParam, LPARAM lParam)
{
  Win32Assistant* assistant;
  if (IsWindowUnicode (hWnd))
    assistant = (Win32Assistant*)GetWindowLongPtrW (hWnd, 0);
  else
    assistant = (Win32Assistant*)GetWindowLongPtrA (hWnd, 0);
  switch (message)
  {
    case WM_ACTIVATEAPP:
      if ((assistant != 0))
      {
        if (wParam) 
	{ 
	  assistant->ApplicationActive = true; 
	} 
	else 
	{ 
	  assistant->ApplicationActive = false; 
	}
      }
      break;
    case WM_ACTIVATE:
      if ((assistant != 0))
      {
	iEventOutlet* outlet = assistant->GetEventOutlet();
        if (LOWORD(wParam) != WA_INACTIVE)
          outlet->Broadcast (assistant->FocusGained, 1);
        else
          outlet->Broadcast (assistant->FocusLost, 0);
      }
      break;
    case WM_CREATE:
      {
	CreateInfo* ci;
	if (IsWindowUnicode (hWnd))
	{
	  ci = (CreateInfo*)((LPCREATESTRUCTW)lParam)->lpCreateParams;
	  SetWindowLongPtrW (hWnd, 0, (LONG_PTR)ci->assistant);
	  SetWindowLongPtrW (hWnd, sizeof (LONG_PTR), (LONG_PTR)ci->canvas);
	}
	else
	{
	  ci = (CreateInfo*)((LPCREATESTRUCTA)lParam)->lpCreateParams;
	  SetWindowLongPtrA (hWnd, 0, (LONG_PTR)ci->assistant);
	  SetWindowLongPtrA (hWnd, sizeof (LONG_PTR), (LONG_PTR)ci->canvas);
	}
	// a window is created. Hide the console window, if requested.
	if (ci->assistant->is_console_app && 
	  !ci->assistant->console_window)
	{
	  ci->assistant->DisableConsole ();
	}
      }
      break;
    case WM_SYSCOMMAND:
      if (wParam == SC_CLOSE)
      {
	PostQuitMessage (0);
	return TRUE;
      }
      break;
    case WM_SYSCHAR:
    case WM_CHAR:
    case WM_UNICHAR:
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
    case WM_IME_COMPOSITION:
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
      if (assistant != 0)
      {	  
	if (assistant->HandleKeyMessage (hWnd, message, wParam, lParam))
	{
	  return 0;
	}
      }
      break;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    {
      if (assistant != 0)
      {
	const int buttonNum = (message == WM_LBUTTONDOWN) ? csmbLeft :
          (message == WM_RBUTTONDOWN) ? csmbRight : csmbMiddle;
        if (assistant->mouseButtons == 0) SetCapture (hWnd);
	assistant->mouseButtons |= 1 << (buttonNum - csmbLeft);

        iEventOutlet* outlet = assistant->GetEventOutlet();
        outlet->Mouse (buttonNum, true,
          short (LOWORD (lParam)), short (HIWORD (lParam)));
      }
      return TRUE;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    {
      if (assistant != 0)
      {
	const int buttonNum = (message == WM_LBUTTONUP) ? csmbLeft :
          (message == WM_RBUTTONUP) ? csmbRight : csmbMiddle;
	assistant->mouseButtons &= ~(1 << (buttonNum - csmbLeft));
        if (assistant->mouseButtons == 0) ReleaseCapture ();

        iEventOutlet* outlet = assistant->GetEventOutlet();
        outlet->Mouse (buttonNum, false,
          short (LOWORD (lParam)), short (HIWORD (lParam)));
      }
      return TRUE;
    }
    case WM_MOUSEWHEEL:
    {
      if (assistant != 0)
      {
        iEventOutlet* outlet = assistant->GetEventOutlet();
	int wheelDelta = (short)HIWORD (wParam);
	// @@@ Only emit events when WHEEL_DELTA wheel ticks accumulated?
	POINT coords;
	coords.x = short (LOWORD (lParam));
	coords.y = short (HIWORD (lParam));
	ScreenToClient(hWnd, &coords);
	outlet->Mouse (wheelDelta > 0 ? csmbWheelUp : csmbWheelDown, true,
	  coords.x, coords.y);
	//outlet->Mouse (wheelDelta > 0 ? csmbWheelUp : csmbWheelDown, false,
	  //coords.x, coords.y); 
      }
      return 0;
    }
    case WM_XBUTTONUP:
    case WM_XBUTTONDOWN:
    {
      if (assistant != 0)
      {
	bool down = (message == WM_XBUTTONDOWN);
	const int maxXButtons = 16; 
	  // XButton flags are stored in high word of lparam
	const int mbFlagsOffs = csmbMiddle; 
	  // Offset of bit num of mouseButtons

	int XButtons = HIWORD(wParam);

	if (down && (assistant->mouseButtons == 0)) SetCapture (hWnd);

        iEventOutlet* outlet = assistant->GetEventOutlet();
	for (int x = 0; x < maxXButtons; x++)
	{
	  if (XButtons & (1 << x))
	  {
	    int mbFlag = 1 << (x + mbFlagsOffs);
	    if (down && !(assistant->mouseButtons & mbFlag))
	    {
	      assistant->mouseButtons |= mbFlag;
	      outlet->Mouse (csmbExtra1 + x, true,
		short (LOWORD (lParam)), short (HIWORD (lParam)));
	    }
	    else if (!down && (assistant->mouseButtons & mbFlag))
	    {
	      assistant->mouseButtons &= ~mbFlag;
	      outlet->Mouse (csmbExtra1 + x, false,
		short (LOWORD (lParam)), short (HIWORD (lParam)));
	    }
	  }
	}
        if (!down && (assistant->mouseButtons == 0)) ReleaseCapture ();
      }
      return TRUE;
    }
    case WM_MOUSEMOVE:
    {
      if (assistant != 0)
      {
        iEventOutlet* outlet = assistant->GetEventOutlet();
        outlet->Mouse (csmbNone, false, short(LOWORD(lParam)), 
	  short(HIWORD(lParam)));
      }
      return TRUE;
    }
    case WM_SETCURSOR:
    {
      if ((assistant != 0) && (LOWORD (lParam) == HTCLIENT))
      {
        ::SetCursor (assistant->m_hCursor);
        return TRUE;
      }
      break;
    }
    case WM_SIZE:
    {
      if (assistant != 0)
      {
	iGraphics2D* canvas;
	if (IsWindowUnicode (hWnd))
	  canvas = (iGraphics2D*)GetWindowLongPtrW (hWnd, sizeof (LONG_PTR));
	else
	  canvas = (iGraphics2D*)GetWindowLongPtrA (hWnd, sizeof (LONG_PTR));

	if ( (wParam == SIZE_MAXIMIZED) || (wParam == SIZE_RESTORED) )
	{
          iEventOutlet* outlet = assistant->GetEventOutlet();
	  outlet->Broadcast (csevCanvasExposed (assistant->registry, canvas));
	} 
	else if (wParam == SIZE_MINIMIZED) 
	{
          iEventOutlet* outlet = assistant->GetEventOutlet();
	  outlet->Broadcast (csevCanvasHidden (assistant->registry, canvas));
	}
      }
      return TRUE;
    }
    case WM_SHOWWINDOW:
    {
      if (assistant != 0)
      {
	iGraphics2D* canvas;
	if (IsWindowUnicode (hWnd))
	  canvas = (iGraphics2D*)GetWindowLongPtrW (hWnd, sizeof (LONG_PTR));
	else
	  canvas = (iGraphics2D*)GetWindowLongPtrA (hWnd, sizeof (LONG_PTR));

	if (wParam)
	{
          iEventOutlet* outlet = assistant->GetEventOutlet();
	  outlet->Broadcast (csevCanvasExposed (assistant->registry, canvas));
	} 
	else
	{
          iEventOutlet* outlet = assistant->GetEventOutlet();
	  outlet->Broadcast (csevCanvasHidden (assistant->registry, canvas));
	}
      }
      break;
    }
  }
  if (IsWindowUnicode (hWnd))
  {
    return DefWindowProcW (hWnd, message, wParam, lParam);
  }
  else
  {
    return DefWindowProcA (hWnd, message, wParam, lParam);
  }
}

bool Win32Assistant::SetCursor (int cursor)
{
  char *CursorID;
  switch (cursor)
  {
    case csmcNone:     CursorID = (char *)-1;   break;
    case csmcArrow:    CursorID = IDC_ARROW;    break;
    case csmcCross:    CursorID = IDC_CROSS;	break;
    //case csmcPen:      CursorID = IDC_PEN;	break;
    case csmcPen:      CursorID = MAKEINTRESOURCE(32631);	break;
    case csmcMove:     CursorID = IDC_SIZEALL;  break;
    case csmcSizeNWSE: CursorID = IDC_SIZENWSE; break;
    case csmcSizeNESW: CursorID = IDC_SIZENESW; break;
    case csmcSizeNS:   CursorID = IDC_SIZENS;   break;
    case csmcSizeEW:   CursorID = IDC_SIZEWE;   break;
    case csmcStop:     CursorID = IDC_NO;       break;
    case csmcWait:     CursorID = IDC_WAIT;     break;
    default:           CursorID = 0;            break;
  }

  bool success;
  HCURSOR cur;
  if (CursorID)
  {
    cur = ((CursorID != (char *)-1) ? LoadCursor (0, CursorID) : 0);
    success = true;
  }
  else
  {
    cur = 0;
    success = false;
  }
  SetHCursor (cur);
  return success;
}

void Win32Assistant::DisableConsole ()
{
  if (!console_window) return;
  console_window = false;

  DWORD lasterr;
  csString outName;
  {
    char apppath[MAX_PATH];
    GetModuleFileName (0, apppath, sizeof(apppath));
    lasterr = GetLastError ();
    if (lasterr != ERROR_INSUFFICIENT_BUFFER)
      outName = apppath;
  }
  if (lasterr == ERROR_INSUFFICIENT_BUFFER)
  {
    DWORD bufSize = 2*MAX_PATH;
    char* buf = 0;
    while (lasterr == ERROR_INSUFFICIENT_BUFFER)
    {
      buf = (char*)cs_realloc (buf, bufSize);
      GetModuleFileName (0, buf, bufSize);
      bufSize += MAX_PATH;
      lasterr = GetLastError ();
    }
    outName = buf;
    cs_free (buf);
  }

  {
    size_t basePos = outName.FindLast ('\\');
    if (basePos != (size_t)-1) outName.DeleteAt (0, basePos+1);
  }
  {
    size_t dot = outName.FindLast ('.');
    if (dot != (size_t)-1)
      outName.Overwrite (dot, ".txt");
    else
      outName.Append (".txt");
  }
  {
    char tmp[MAX_PATH];
    if (GetTempPath (sizeof (tmp), tmp) != 0)
      outName.Insert (0, tmp);
  }

  /* Redirect only those handles that were not initially redirected by
   * the user */
  if (!IsStdHandleRedirected (STD_ERROR_HANDLE)) 
    freopen (outName, "w", stderr);
  if (!IsStdHandleRedirected (STD_OUTPUT_HANDLE)) 
    freopen (outName, "w", stdout);
  FreeConsole();

  struct tm *now;
  time_t aclock;
  time( &aclock );
  now = localtime( &aclock );
  csPrintf("====== %s", asctime(now));
}

// @@@ The following aren't thread-safe. Prolly use TLS.
/// The "Ok" button text passsed to Alert(),
static const char* msgOkMsg;
/// Hook that will change the OK button text.
static HHOOK msgBoxOkChanger;

LRESULT Win32Assistant::CBTProc (int nCode, WPARAM wParam, LPARAM lParam)
{
  switch (nCode)
  {
    // when the MB is first activated we change the button text
  case HCBT_ACTIVATE:
    {
      // The MBs we request always have just one button (OK)
      HWND Button = FindWindowEx ((HWND)wParam, 0, "Button", 0);
      if (Button)
      {
	if (cswinIsWinNT ())
	{
          SetWindowTextW (Button, csCtoW (msgOkMsg));
	}
	else
	{
          SetWindowTextA (Button, cswinCtoA (msgOkMsg));
	}
      }
      LRESULT ret = CallNextHookEx (msgBoxOkChanger,
	nCode, wParam, lParam);
      // The work is done, remove the hook.
      UnhookWindowsHookEx (msgBoxOkChanger);
      msgBoxOkChanger = 0;
      return ret;
    }
    break;
  }
  return CallNextHookEx (msgBoxOkChanger,
    nCode, wParam, lParam);
}

void Win32Assistant::AlertV (HWND window, int type, const char* title, 
    const char* okMsg, const char* msg, va_list args)
{
  UINT style = MB_OK;

  if (type == CS_ALERT_ERROR)
    style |= MB_ICONERROR;
  else if (type == CS_ALERT_WARNING)
    style |= MB_ICONWARNING;
  else if (type == CS_ALERT_NOTE)
    style |= MB_ICONINFORMATION;

  msgBoxOkChanger = 0;
  /*
    To change the text of the "OK" button, we somehow have to get
    a handle to it, preferably before the user sees anything of
    the message. So a hook is set up.
   */
  if (okMsg != 0)
  {
    msgOkMsg = okMsg;
    msgBoxOkChanger = SetWindowsHookEx (WH_CBT,
      (HOOKPROC)&CBTProc, ModuleHandle, GetCurrentThreadId());
  }

  csString buf;
  buf.FormatV (msg, args);

  // No need to juggle with string conversions here, 
  // MessageBoxW() also exists on Win9x
  MessageBoxW (window, csCtoW (buf), csCtoW (title), style);

  /*
    Clean up in case it isn't already.
   */
  if (msgBoxOkChanger != 0)
  {
    UnhookWindowsHookEx (msgBoxOkChanger);
  }
}

void Win32Assistant::UseOwnMessageLoop(bool ownmsgloop)
{
  use_own_message_loop = ownmsgloop;
}

bool Win32Assistant::HasOwnMessageLoop()
{
  return use_own_message_loop;
}

HWND Win32Assistant::CreateCSWindow (iGraphics2D* canvas,
				     DWORD exStyle, DWORD style, 
				     int x, int y, int w, int h)
{
  HWND wnd;
  CreateInfo ci;
  ci.assistant = this;
  ci.canvas = canvas;
  if (cswinIsWinNT())
  {
    wnd = CreateWindowExW (exStyle, (WCHAR*)windowClass, 0, style,
      x, y, w, h, 0, 0, ModuleHandle, &ci);
  }
  else
  {
    wnd = CreateWindowExA (exStyle, (char*)windowClass, 0, style,
      x, y, w, h, 0, 0, ModuleHandle, &ci);
  }
  return wnd;
}

bool Win32Assistant::HandleKeyMessage (HWND hWnd, UINT message, 
				       WPARAM wParam, LPARAM lParam)
{
  return kbdDriver->HandleKeyMessage (hWnd, message, wParam, lParam);
}
