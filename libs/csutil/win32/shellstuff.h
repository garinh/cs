/*
    Copyright (C) 2002,2003 by Frank Richter

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

#ifndef __CS_SYS_WIN32_SHELLSTUFF_H__
#define __CS_SYS_WIN32_SHELLSTUFF_H__

#define WIN32_LEAN_AND_MEAN
#include <shlobj.h>
#include "csutil/csstring.h"
#include "csutil/win32/cachedll.h"

// This file contains some newer SHELL32 stuff, for example not found
// in MinGW Win32 headers.

typedef HRESULT (STDAPICALLTYPE* SHGETFOLDERPATHAPROC)(HWND hwndOwner, 
					      int nFolder, 
					      HANDLE hToken, 
					      DWORD dwFlags, 
					      LPCSTR pszPath);

#ifndef CSIDL_APPDATA 
#define CSIDL_APPDATA			0x001a
#endif
#ifndef CSIDL_LOCAL_APPDATA 
#define CSIDL_LOCAL_APPDATA		0x001c
#endif
#ifndef CSIDL_PROGRAM_FILES
#define CSIDL_PROGRAM_FILES             0x0026
#endif
#ifndef SHGFP_TYPE_CURRENT
#define SHGFP_TYPE_CURRENT		0
#endif

static inline bool
GetShellFolderPath (int CSIDL, char* path)
{
  bool result = false;

  // ShFolder.dll can 'emulate' special folders on older Windowses.
  static CS::Platform::Win32::CacheDLL shFolder ("shfolder.dll");
	
  if (shFolder)
  {
    SHGETFOLDERPATHAPROC SHGetFolderPathA;

    SHGetFolderPathA = 
      (SHGETFOLDERPATHAPROC) GetProcAddress (shFolder, "SHGetFolderPathA");

    if (SHGetFolderPathA)
    {
      result = (SHGetFolderPathA (0, CSIDL, 0, 
	SHGFP_TYPE_CURRENT, path) == S_OK);
    }
  }
  else
  {
    // no shfolder? Try normal shell32 instead
    LPMALLOC MAlloc;
    LPITEMIDLIST pidl;

    if (SUCCEEDED(SHGetMalloc (&MAlloc)))
    {
      if (SUCCEEDED(SHGetSpecialFolderLocation (0, CSIDL, &pidl)))
      {
	result = (SHGetPathFromIDList (pidl, path) == TRUE);
	MAlloc->Free (pidl);
      }
      MAlloc->Release ();
    }
  }
  return result;
}

static inline bool GetShellFolderPath (int CSIDL, csString& path)
{
  bool result = false;
  char buf[MAX_PATH];

  // ShFolder.dll can 'emulate' special folders on older Windowses.
  static CS::Platform::Win32::CacheDLL shell32 ("shell32.dll");
  static CS::Platform::Win32::CacheDLL shFolder ("shfolder.dll");
	
  SHGETFOLDERPATHAPROC SHGetFolderPathA;
  SHGetFolderPathA = 
    (SHGETFOLDERPATHAPROC) GetProcAddress (shell32, "SHGetFolderPathA");

  if (!SHGetFolderPathA && shFolder)
  {
    SHGetFolderPathA = 
      (SHGETFOLDERPATHAPROC) GetProcAddress (shFolder, "SHGetFolderPathA");
  }

  if (SHGetFolderPathA)
  {
    result = (SHGetFolderPathA (0, CSIDL, 0, 
      SHGFP_TYPE_CURRENT, buf) == S_OK);
  }
  else
  {
    // no shfolder? Try normal shell32 instead
    LPMALLOC MAlloc;
    LPITEMIDLIST pidl;

    if (SUCCEEDED(SHGetMalloc (&MAlloc)))
    {
      if (SUCCEEDED(SHGetSpecialFolderLocation (0, CSIDL, &pidl)))
      {
	result = (SHGetPathFromIDListA (pidl, buf) == TRUE);
	MAlloc->Free (pidl);
      }
      MAlloc->Release ();
    }
  }
  if (result) path.Replace (buf);
  return result;
}

#endif // __CS_SYS_WIN32_SHELLSTUFF_H__
