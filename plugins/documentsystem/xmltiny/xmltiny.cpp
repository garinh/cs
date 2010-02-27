/*
    Copyright (C) 2003 by Jorrit Tyberghein
	      (C) 2003 by Frank Richter

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
#include "iutil/comp.h"
#include "iutil/databuff.h"
#include "iutil/vfs.h"
#include "csutil/scf.h"
#include "csutil/xmltiny.h"

static inline bool HasUTF8Bom (const char* buf)
{
  unsigned char* ub = (unsigned char*)buf; //Need for test...
  return (ub[0] == 0xEF && ub[1] == 0xBB && ub[2] == 0xBF);
}

struct csTinyDocWrapper :
  public scfImplementation1<csTinyDocWrapper, iDocument>
{
private:
  csRef<iDocument> tinydoc;

public:
  csTinyDocWrapper (csRef<iDocument> doc);
  virtual ~csTinyDocWrapper ();

  virtual void Clear ();
  virtual csRef<iDocumentNode> CreateRoot ();
  virtual csRef<iDocumentNode> GetRoot ();
  virtual const char* Parse (iFile* file,      bool collapse = false);
  virtual const char* Parse (iDataBuffer* buf, bool collapse = false);
  virtual const char* Parse (iString* str,     bool collapse = false);
  virtual const char* Parse (const char* buf,  bool collapse = false);
  virtual const char* Write (iFile* file);
  virtual const char* Write (iString* str);
  virtual const char* Write (iVFS* vfs, const char* filename);

  virtual int Changeable ();
};

csTinyDocWrapper::csTinyDocWrapper (csRef<iDocument> doc) :
  scfImplementationType(this)
{
  tinydoc = doc;
}

csTinyDocWrapper::~csTinyDocWrapper ()
{
}

void csTinyDocWrapper::Clear ()
{
  tinydoc->Clear();
}

csRef<iDocumentNode> csTinyDocWrapper::CreateRoot ()
{
  return tinydoc->CreateRoot();
}

csRef<iDocumentNode> csTinyDocWrapper::GetRoot ()
{
  return tinydoc->GetRoot();
}

const char* csTinyDocWrapper::Parse (iFile* file, bool collapse)
{
  csRef<iDataBuffer> buf = csPtr<iDataBuffer>
    (file->GetAllData (true));
  const char *ret = Parse (buf->GetData(), collapse);
  return ret;
}

const char* csTinyDocWrapper::Parse (iDataBuffer* buf, bool collapse)
{
  return Parse ((char*)buf->GetData(), collapse);
}

const char* csTinyDocWrapper::Parse (iString* str, bool collapse)
{
  return Parse ((const char*)str, collapse);
}

const char* csTinyDocWrapper::Parse (const char* buf, bool collapse)
{
  // Skip any UTF8 BOM
  if (HasUTF8Bom (buf))
    buf += 3;

  const char* b = buf;
  while ((*b == ' ') || (*b == '\n') || (*b == '\t') || 
    (*b == '\r')) b++;
  if (*b == '<')
  {
    return tinydoc->Parse (buf, collapse);
  }
  else
  {
    return "Data does not seem to be XML.";
  }
}

const char* csTinyDocWrapper::Write (iFile* file)
{
  return tinydoc->Write (file);
}

const char* csTinyDocWrapper::Write (iString* str)
{
  return tinydoc->Write (str);
}

const char* csTinyDocWrapper::Write (iVFS* vfs, const char* filename)
{
  return tinydoc->Write (vfs, filename);
}

int csTinyDocWrapper::Changeable ()
{
  return tinydoc->Changeable();
}

class csTinyXMLPlugin :
  public scfImplementationExt1<csTinyXMLPlugin, csTinyDocumentSystem,
    iComponent>
{
public:
  csTinyXMLPlugin (iBase* parent) :
    scfImplementationType(this, parent) {}

  virtual bool Initialize (iObjectRegistry* objreg);
};

bool csTinyXMLPlugin::Initialize (iObjectRegistry* /*objreg*/)
{
  return true;
}

SCF_IMPLEMENT_FACTORY (csTinyXMLPlugin)
