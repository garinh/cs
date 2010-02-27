/*
    Copyright (C) 2004 by Frank Richter
	      (C) 2004 by Jorrit Tyberghein

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
#include "csutil/memdebug.h"

CS_CRYSTALSPACE_EXPORT 
CS_IMPLEMENT_STATIC_VARIABLE_REGISTRATION (csStaticVarCleanup_csutil)

#ifdef CS_BUILD_SHARED_LIBS
CS_DEFINE_STATIC_VARIABLE_REGISTRATION (csStaticVarCleanup_csutil);
CS_DEFINE_MEMTRACKER_MODULE

#if defined(CS_MEMORY_TRACKER)
struct MemTrackerModuleIniter
{
  MemTrackerModuleIniter (const char* modName)
  {
    CS::Debug::MemTracker::RegisterModule (modName);
  }
};

namespace
{
  static MemTrackerModuleIniter mtmi ("libcrystalspace");
}
#endif

#endif
