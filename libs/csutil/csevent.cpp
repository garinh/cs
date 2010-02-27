/*
  Crystal Space Event Queue
  Copyright (C) 1998-2004 by Jorrit Tyberghein
  Written by Andrew Zabolotny <bit@eltech.ru>, Eric Sunshine, Jonathan Tarbox,
    Frank Richter

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
#include "csutil/array.h"
#include "csutil/csevent.h"
#include "csutil/eventnames.h"
#include "csutil/cseventq.h"
#include "csutil/memfile.h"
#include "csutil/util.h"
#include "csutil/sysfunc.h"

//---------------------------------------------------------------------------

CS_IMPLEMENT_STATIC_VAR(GetEventStrSet, csStringSet, ())

char const* csEvent::GetTypeName (csEventAttributeType t)
{
  switch (t)
  {
    case csEventAttrInt:	    return "int";
    case csEventAttrUInt:	    return "uint";
    case csEventAttrFloat:	    return "double";
    case csEventAttrDatabuffer:	    return "databuffer";
    case csEventAttrEvent:	    return "event";
    case csEventAttriBase:	    return "iBase";
    default:
      break;
  }
  return "unknown";
}

csStringID csEvent::GetKeyID (const char* key)
{
  return GetEventStrSet()->Request (key);
}

const char* csEvent::GetKeyName (csStringID id)
{
  return GetEventStrSet()->Request (id);
}

const csEventID csEvent::GetName()
{
  return Name;
}

csEvent::csEvent () :
  scfImplementationType (this), attributes (53), count(0)
{
  Time = ~0;
  Name = csInvalidStringID;
  Broadcast = false;
}

csEvent::csEvent (csTicks iTime, csEventID iName, bool iBroadcast) :
  scfImplementationType (this), attributes (53), count (0)
{
  Time = iTime;
  Name = iName;
  Broadcast = iBroadcast;
}

// Copy constructor
csEvent::csEvent (csEvent const& e) : iBase(), scfImplementationType (this),
	attributes (53)
{
  count = 0;

  Name = e.Name;
  Broadcast = e.Broadcast;
  Time = e.Time;
  csHash<attribute*, csStringID>::ConstGlobalIterator it = e.attributes.GetIterator();
  csStringID key;
  while (it.HasNext())
  {
    attribute *a = it.Next(key);
    attributes.Put(key, new attribute(*a));
    count++;
  }
}

csEvent::~csEvent ()
{
  RemoveAll();
}

bool csEvent::Add (const char *name, float v)
{
  if (attributes.In (GetKeyID (name))) return false;
  attribute* object = new attribute (csEventAttrFloat);
  object->doubleVal = v;
  attributes.Put (GetKeyID (name), object);
  count++;
  return true;
}

bool csEvent::Add (const char *name, double v)
{
  if (attributes.In (GetKeyID (name))) return false;
  attribute* object = new attribute (csEventAttrFloat);
  object->doubleVal = v;
  attributes.Put (GetKeyID (name), object);
  count++;
  return true;
}

bool csEvent::Add (const char *name, bool v)
{
  if (attributes.In (GetKeyID (name))) return false;
  attribute* object = new attribute (csEventAttrInt);
  object->intVal = v ? 1 : 0;
  attributes.Put (GetKeyID (name), object);
  count++;
  return true;
}

bool csEvent::Add (const char *name, const char *v)
{
  if (attributes.In (GetKeyID (name))) return false;
  attribute* object = new attribute (csEventAttrDatabuffer);
  object->dataSize = strlen(v);
  object->bufferVal = csStrNew(v);
  attributes.Put (GetKeyID (name), object);
  count++;
  return true;
}

bool csEvent::Add (const char *name, const void *v, size_t size)
{
  if (attributes.In (GetKeyID (name))) return false;
  attribute* object = new attribute (csEventAttrDatabuffer);
  object->bufferVal = new char[size + 1];
  memcpy (object->bufferVal, v, size);
  object->bufferVal[size] = 0;
  object->dataSize = size;
  attributes.Put (GetKeyID (name), object);
  count++;
  return true;
}

bool csEvent::CheckForLoops (iEvent* current, iEvent* e)
{
  csRef<iEventAttributeIterator> iter (current->GetAttributeIterator());

  while (iter->HasNext())
  {
    const char* attr = iter->Next();

    if (current->GetAttributeType (attr) == csEventAttrEvent)
    {
      csRef<iEvent> ev;
      if (current->Retrieve (attr, ev) != csEventErrNone) continue;
      if (ev == e)
	return false;
      return CheckForLoops(ev, e);
    }
  }
  return true;
}

bool csEvent::Add (const char *name, iEvent *v)
{
  if (attributes.In (GetKeyID (name))) return false;
  if (this == v)
    return false;
  if (v && CheckForLoops(v, this))
  {
    attribute* object = new attribute (csEventAttrEvent);
    (object->ibaseVal = v)->IncRef();
    attributes.Put (GetKeyID (name), object);
    count++;
    return true;
  }
  return false;
}

bool csEvent::Add (const char *name, iBase* v)
{
  if (attributes.In (GetKeyID (name))) return false;
  if (v)
  {
    attribute* object = new attribute (csEventAttriBase);
    (object->ibaseVal = v)->IncRef();
    attributes.Put (GetKeyID (name), object);
    count++;
    return true;
  }
  return false;
}

csEventError csEvent::Retrieve (const char *name, float &v) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttrFloat)
  {
    v = object->doubleVal;
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

csEventError csEvent::Retrieve (const char *name, double &v) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttrFloat)
  {
    v = object->doubleVal;
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

csEventError csEvent::Retrieve (const char *name, const char *&v) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttrDatabuffer)
  {
    v = object->bufferVal;
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

csEventError csEvent::Retrieve (const char *name, void const *&v,
  size_t &size) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttrDatabuffer)
  {
    v = object->bufferVal;
    size = object->dataSize;
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

csEventError csEvent::Retrieve (const char *name, bool &v) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttrInt)
  {
    v = object->intVal != 0;
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

csEventError csEvent::Retrieve (const char *name, csRef<iEvent> &v) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttrEvent)
  {
    iBase *b = object->ibaseVal;
    v = scfQueryInterface<iEvent> (b);
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

csEventError csEvent::Retrieve (const char *name, csRef<iBase> &v) const
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (!object) return csEventErrNotFound;
  if (object->type == csEventAttriBase)
  {
    v = object->ibaseVal;
    return csEventErrNone;
  }
  else
  {
    return InternalReportMismatch (object);
  }
}

bool csEvent::AttributeExists (const char* name)
{
  return attributes.In (GetKeyID (name));
}

csEventAttributeType csEvent::GetAttributeType (const char* name)
{
  attribute* object = attributes.Get (GetKeyID (name), 0);
  if (object)
  {
    return object->type;
  }
  return csEventAttrUnknown;
}

bool csEvent::Remove(const char *name)
{
  csStringID id = GetKeyID (name);
  if (!attributes.In (id)) return false;
  attribute* object = attributes.Get (id, 0);
  bool result = attributes.Delete (id, object);
  delete object;
  return result;
}

bool csEvent::RemoveAll()
{
  csHash<attribute*, csStringID>::GlobalIterator iter (
    attributes.GetIterator ());

  while (iter.HasNext())
  {
    csStringID name;
    attribute* object = iter.Next (name);
    delete object;
  }

  attributes.DeleteAll();
  count = 0;
  return true;
}

csRef<iEventAttributeIterator> csEvent::GetAttributeIterator()
{
  csHash<csEvent::attribute*, csStringID>::GlobalIterator attrIter (
    attributes.GetIterator());
  return csPtr<iEventAttributeIterator> (new csEventAttributeIterator (
    attrIter));
}

static void IndentLevel(int level)
{
  for (int i = 0; i < level; i++)
    csPrintf("\t");
}

bool csEvent::Print (int level)
{
  csHash<attribute*, csStringID>::GlobalIterator iter (
    attributes.GetIterator ());

  while (iter.HasNext())
  {
    csStringID name;
    attribute* object = iter.Next (name);

    IndentLevel(level); csPrintf ("------\n");
    IndentLevel(level); csPrintf ("Name: %s\n", GetKeyName (name));
    IndentLevel(level); csPrintf (" Datatype: %s\n",
	  GetTypeName(object->type));
    if (object->type == csEventAttrEvent)
    {
      IndentLevel(level); csPrintf(" Sub-Event Contents:\n");
      csRef<csEvent> csev = scfQueryInterface<csEvent> (object->ibaseVal);
      if (csev)
	csev->Print(level+1);
      else
      {
	IndentLevel(level+1); csPrintf(" (Not an event!):\n");
      }

    }
    if (object->type == csEventAttrInt)
    {

      IndentLevel(level);
      csPrintf(" Value: %" CS_PRId64 "\n", object->intVal);
    }
    else if (object->type == csEventAttrUInt)
    {
      IndentLevel(level);
      csPrintf(" Value: %" CS_PRIu64 "\n", object->intVal);
    }
    else if (object->type == csEventAttrFloat)
    {
      IndentLevel(level);
      csPrintf (" Value: %f\n", object->doubleVal);
    }
    else if (object->type == csEventAttrDatabuffer)
    {
      IndentLevel(level); csPrintf(" Value: 0x%p\n", object->bufferVal);
      IndentLevel(level); csPrintf(" Length: %zu\n", object->dataSize);
    }
  }

  return true;
}

csRef<iEvent> csEvent::CreateEvent()
{
  return csPtr<iEvent>(new csEvent());
}

//---------------------------------------------------------------------------

const char* csEventAttributeIterator::Next()
{
  csStringID key;
  iterator.Next (key);
  return csEvent::GetKeyName (key);
}

//*****************************************************************************
// csPoolEvent
//*****************************************************************************
csPoolEvent::csPoolEvent(csEventQueue *q)
{
  pool = q;
  next = 0;
}

void csPoolEvent::DecRef()
{
  if (scfRefCount == 1)
  {
    if (!pool.IsValid())
      return;

    next = pool->EventPool;
    pool->EventPool = this;
    RemoveAll();
    Name = csInvalidStringID;
    Time = ~0;
    Broadcast = false;
  }
  else
  {
    scfRefCount--;
  }
}

csRef<iEvent> csPoolEvent::CreateEvent()
{
  if (pool.IsValid())
    return pool->CreateEvent();
  return superclass::CreateEvent();
}
