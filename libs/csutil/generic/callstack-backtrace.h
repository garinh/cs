/*
  Copyright (C) 2005 by Jorrit Tyberghein
            (C) 2005 by Frank Richter

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

#ifndef __CS_CSUTIL_GENERIC_CALLSTACK_BACKTRACE_H__
#define __CS_CSUTIL_GENERIC_CALLSTACK_BACKTRACE_H__

#include "../callstack.h"

namespace CS
{
namespace Debug
{

/// Call stack creator utilizing backtrace()
class CallStackCreatorBacktrace :
  public CallstackAllocatedDerived<iCallStackCreator>
{
  virtual bool CreateCallStack (CallStackEntriesArray& entries, 
    CallStackParamsArray& params, bool fast);
};

/// Call stack name resolver utilizing backtrace_symbols()
class CallStackNameResolverBacktrace :
  public CallstackAllocatedDerived<iCallStackNameResolver>
{
  virtual bool GetAddressSymbol (void* addr, char*& sym);
  virtual void* OpenParamSymbols (void* addr);
  virtual bool GetParamName (void* handle, size_t paramNum, char*& sym);
  virtual void FreeParamSymbols (void* handle);
  virtual bool GetLineNumber (void* addr, char*& lineAndFile);
};

} // namespace Debug
} // namespace CS

#endif // __CS_CSUTIL_GENERIC_CALLSTACK_BACKTRACE_H__
