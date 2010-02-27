/*
    Copyright (C) 1998 by Jorrit Tyberghein

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

#ifndef __CS_SNDSYS_LOADER_H__
#define __CS_SNDSYS_LOADER_H__

/**\file
 * Sound system: loader
 */

#include "csutil/scf.h"
#include "csutil/ref.h"

/**\addtogroup sndsys
 * @{ */

struct iDataBuffer;
struct iSndSysData;

/**
 * The sound loader is used to load sound files given a raw input data stream.
 */
struct iSndSysLoader : public virtual iBase
{
  /// SCF2006 - See http://www.crystalspace3d.org/cseps/csep-0010.html
  SCF_INTERFACE(iSndSysLoader,0,3,0);

  /// Create a sound object from raw input data.
  //
  //  Optional pDescription may point to a brief description that will follow this data
  //   through any streams or sources created from it, and may be useful for display or
  //   diagnostic purposes.
  virtual csPtr<iSndSysData> LoadSound (iDataBuffer* buffer, const char *pDescription=0) = 0;
};

/** @} */

#endif // __CS_SNDSYS_LOADER_H__
