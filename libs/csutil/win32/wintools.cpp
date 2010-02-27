/*
    Copyright (C) 2003 by Frank Richter

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
#include "csutil/csunicode.h"
#include "csutil/csstring.h"
#include "csutil/util.h"

#include "csutil/win32/wintools.h"

struct _WinVersion 
{
  bool IsWinNT;
  cswinWindowsVersion version;

  _WinVersion()
  { 
    OSVERSIONINFO vi;
    vi.dwOSVersionInfoSize = sizeof (vi);
    GetVersionEx (&vi);
    IsWinNT = (vi.dwPlatformId == VER_PLATFORM_WIN32_NT);
    if (!IsWinNT)
    {
      version = cswinWin9x;
    }
    else
    {
      if (vi.dwMajorVersion >= 6)
      {
        version = cswinWinVista;
      }
      else if (vi.dwMajorVersion >= 5)
      {
	if (vi.dwMinorVersion >= 2)
	{
	  version = cswinWin2003;
        }
	else if (vi.dwMinorVersion >= 1)
	{
	  version = cswinWinXP;
	}
	else
	{
	  version = cswinWin2K;
	}
      }
      else
      {
	version = cswinWinNT;
      }
    }
  };
};

CS_IMPLEMENT_STATIC_VAR (getWinVersion, _WinVersion, ())

static _WinVersion* GetWinVer ()
{
  _WinVersion *winver = getWinVersion ();
  return winver;
}

bool cswinIsWinNT (cswinWindowsVersion* version)
{
  if (version)
    *version = GetWinVer ()->version;
  return GetWinVer ()->IsWinNT;
}

static wchar_t* CallFormatMessage (HRESULT code, DWORD flags)
{
  DWORD dwResult;
  LPVOID lpMsgBuf;
  wchar_t* ret = 0;

  if (cswinIsWinNT ())
  {
    dwResult = FormatMessageW (
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | flags,
      0, code, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR) &lpMsgBuf, 
      0, 0);
    if (dwResult != 0)
    {
      ret = csStrNewW ((wchar_t*)lpMsgBuf);
      LocalFree (lpMsgBuf);
    }
  }
  else
  {
    dwResult = FormatMessageA (
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | flags,
      0, code, MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR) &lpMsgBuf, 
      0, 0);
    if (dwResult != 0)
    {
      ret = cswinAnsiToWide ((char*)lpMsgBuf);
      LocalFree (lpMsgBuf);
    }
  }

  return ret;
}

wchar_t* cswinGetErrorMessageW (HRESULT code)
{
  wchar_t* ret = CallFormatMessage (code, FORMAT_MESSAGE_IGNORE_INSERTS);

  if (ret == 0)
  {
    HRESULT fmError = GetLastError ();
    csString msg;
    msg.Format ("{FormatMessage() error %.8x}", (uint)fmError);
    ret = csStrNewW (msg);
  }

  // Slash trailing CRLF
  wchar_t* retEnd = ret + wcslen (ret);
  while (retEnd > ret)
  {
    retEnd--;
    if ((*retEnd != '\n') && (*retEnd != '\r'))
      break;
    *retEnd = 0;
  }

  return ret;
}

char* cswinGetErrorMessage (HRESULT code)
{
  wchar_t* retW = cswinGetErrorMessageW (code);
  char* ret = csStrNew (retW);
  delete[] retW;
  return ret;
}

