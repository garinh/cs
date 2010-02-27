/*
    Copyright (C) 2000-2001 by Jorrit Tyberghein
    Copyright (C) 2000 by Samuel Humphreys

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
#include <math.h>

#include "csgfx/imagememory.h"
#include "cstool/proctex.h"
#include "csutil/hash.h"
#include "csutil/event.h"
#include "csutil/eventnames.h"
#include "csutil/eventhandlers.h"
#include "csutil/scf_implementation.h"
#include "iengine/engine.h"
#include "iengine/material.h"
#include "iengine/texture.h"
#include "igraphic/image.h"
#include "imap/loader.h"
#include "itexture/itexfact.h"
#include "iutil/comp.h"
#include "iutil/event.h"
#include "iutil/eventh.h"
#include "iutil/eventq.h"
#include "iutil/objreg.h"
#include "iutil/plugin.h"
#include "iutil/virtclk.h"
#include "ivideo/graph2d.h"
#include "ivideo/graph3d.h"
#include "ivideo/material.h"
#include "ivideo/txtmgr.h"
//---------------------------------------------------------------------------
/**
 * Event handler that takes care of updating all procedural
 * textures that were visible last frame.
 */
class csProcTexEventHandler : 
  public scfImplementation1<csProcTexEventHandler, iEventHandler>
{
private:
  iObjectRegistry* object_reg;
  // Set of textures that needs updating next frame.
  csSet<csPtrKey<csProcTexture> > textures;

public:
  csProcTexEventHandler (iObjectRegistry* r)
    : scfImplementationType (this)
  {
    object_reg = r;
  }
  virtual ~csProcTexEventHandler ()
  {
  }

  virtual bool HandleEvent (iEvent& event);

  CS_EVENTHANDLER_PHASE_LOGIC("crystalspace.proctex")

public:
  virtual void PushTexture (csProcTexture* txt)
  {
    textures.Add (txt);
  }
  virtual void PopTexture (csProcTexture* txt)
  {
    textures.Delete (txt);
  }
};


bool csProcTexEventHandler::HandleEvent (iEvent& event)
{
  csRef<iVirtualClock> vc (csQueryRegistry<iVirtualClock> (object_reg));
  csTicks elapsed_time, current_time;
  elapsed_time = vc->GetElapsedTicks ();
  current_time = vc->GetCurrentTicks ();
  csSet<csPtrKey<csProcTexture> > keep_tex;
  (void) event; // unused except for this assert so silence the warning
  CS_ASSERT(event.Name == csevFrame(object_reg));
  {
    {
      csSet<csPtrKey<csProcTexture> >::GlobalIterator it = textures.GetIterator();
      while (it.HasNext ())
      {
        csProcTexture* pt = it.Next ();
	if (!pt->anim_prepared)
	  pt->PrepareAnim();
	if (pt->anim_prepared)
          pt->Animate (current_time);
	pt->visible = false;
	if (pt->always_animate) keep_tex.Add (pt);
        pt->last_cur_time = current_time;
      }
    }
    textures.DeleteAll ();
    // enqueue 'always animate' textures for next cycle
    csSet<csPtrKey<csProcTexture> >::GlobalIterator it = keep_tex.GetIterator();
    while (it.HasNext ())
    {
      csProcTexture* pt = it.Next ();
      textures.Add (pt);
    }
    return true;
  }
}

//---------------------------------------------------------------------------


csProcTexture::csProcTexture (iTextureFactory* p, iImage* image)
  : scfImplementationType (this)
{
  ptReady = false;
  tex = 0;
  texFlags = 0;
  key_color = false;
  object_reg = 0;
  use_cb = true;
  last_cur_time = 0;
  anim_prepared = false;
  always_animate = false;
  visible = false;
  parent = p;
  proc_image = image;
}

csProcTexture::~csProcTexture ()
{
  if (proceh != 0)
    ((csProcTexEventHandler*)(iEventHandler*)(proceh))->PopTexture (this);

}

THREADED_CALLABLE_IMPL1(csProcTexture, SetupProcEventHandler,
	iObjectRegistry* object_reg)
{
  csRef<iEventHandler> proceh = csQueryRegistryTagInterface<iEventHandler>
  	(object_reg, "crystalspace.proctex.eventhandler");
  if (!proceh)
  {
    proceh = csPtr<iEventHandler> (new csProcTexEventHandler (object_reg));
    csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (object_reg));
    if (q != 0)
    {
      q->RegisterListener (proceh, csevFrame(object_reg));
      object_reg->Register (proceh, "crystalspace.proctex.eventhandler");
    }
  }

  ret->SetResult(csRef<iBase>(proceh));
  return true;
}

struct csProcTexCallback : 
  public scfImplementation2<csProcTexCallback, iTextureCallback, iProcTexCallback>
{
  csWeakRef<csProcTexture> pt;
  csProcTexCallback () : scfImplementationType (this) { }
  virtual ~csProcTexCallback () { }
  virtual void UseTexture (iTextureWrapper*);
  virtual iProcTexture* GetProcTexture() const;
};

void csProcTexCallback::UseTexture (iTextureWrapper*)
{
  if (pt) pt->UseTexture ();
}
iProcTexture* csProcTexCallback::GetProcTexture() const
{
  return pt;
}

iTextureWrapper* csProcTexture::CreateTexture (iObjectRegistry* object_reg)
{
  csRef<iTextureWrapper> tex;

  csRef<iEngine> engine (csQueryRegistry<iEngine> (object_reg));
  csRef<iThreadedLoader> tldr = csQueryRegistry<iThreadedLoader> (object_reg);
  csRef<iTextureManager> texman = csQueryRegistry<iTextureManager> (object_reg);
  if (proc_image)
  {
    tex = engine->GetTextureList()->CreateTexture (proc_image);
    tldr->AddTextureToList(tex);
    tex->SetFlags (CS_TEXTURE_3D | texFlags);
    proc_image = 0;
  }
  else
  {
    csRef<iTextureHandle> texHandle = g3d->GetTextureManager()->CreateTexture (mat_w, mat_h,
    csimg2D, "rgb8", CS_TEXTURE_3D | texFlags);
    tex = engine->GetTextureList()->CreateTexture (texHandle);
    tldr->AddTextureToList(tex);
  }

  return tex;
}

bool csProcTexture::Initialize (iObjectRegistry* object_reg)
{
  csProcTexture::object_reg = object_reg;
  csRef<iThreadReturn> itr = SetupProcEventHandler (object_reg);
  itr->Wait(false);
  proceh = scfQueryInterface<iEventHandler>(itr->GetResultRefPtr());

  g3d = csQueryRegistry<iGraphics3D> (object_reg);
  g2d = csQueryRegistry<iGraphics2D> (object_reg);

  tex = CreateTexture (object_reg);
  if (!tex)
    return false;

  if (key_color)
    tex->SetKeyColor (key_red, key_green, key_blue);

  tex->QueryObject ()->SetName (GetName ());
  if (use_cb)
  {
    csRef<csProcTexCallback> cb;
    cb.AttachNew (new csProcTexCallback ());
    cb->pt = this;
    tex->SetUseCallback (cb);
  }
  ptReady = true;
  return true;
}

bool csProcTexture::PrepareAnim ()
{
  if (anim_prepared) return true;
  iTextureHandle* txt_handle = tex->GetTextureHandle ();
  if (!txt_handle) return false;
  anim_prepared = true;
  return true;
}

iMaterialWrapper* csProcTexture::Initialize (iObjectRegistry * object_reg,
    	iEngine* engine, iTextureManager* txtmgr, const char* name)
{
  SetName (name);
  Initialize (object_reg);
  if (txtmgr)
  {
    tex->Register (txtmgr);
  }
  //PrepareAnim ();
  csRef<iMaterial> material (engine->CreateBaseMaterial (tex));
  iMaterialWrapper* mat = engine->GetMaterialList ()->NewMaterial (material,
  	name);
  return mat;
}

bool csProcTexture::GetAlwaysAnimate () const
{
  return always_animate;
}

void csProcTexture::SetAlwaysAnimate (bool enable)
{
  always_animate = enable;
  if (always_animate)
  {
    ((csProcTexEventHandler*)(iEventHandler*)proceh)->PushTexture (this);
  }
}

void csProcTexture::UseTexture ()
{
  if (!PrepareAnim ()) return;
  visible = true;
  ((csProcTexEventHandler*)(iEventHandler*)(proceh))->PushTexture (this);
}

//-----------------------------------------------------------------------------

iObject* csProcTexture::QueryObject()
{
  return tex->QueryObject();
}

iTextureWrapper* csProcTexture::Clone () const
{
  return tex->Clone();
}

void csProcTexture::SetImageFile (iImage *Image)
{
  tex->SetImageFile (Image);
}

iImage* csProcTexture::GetImageFile ()
{
  return tex->GetImageFile();
}

void csProcTexture::SetTextureHandle (iTextureHandle *t)
{
  tex->SetTextureHandle (t);
}

iTextureHandle* csProcTexture::GetTextureHandle ()
{
  return tex->GetTextureHandle();
}


void csProcTexture::GetKeyColor (int &red, int &green, int &blue) const
{
  tex->GetKeyColor (red, green, blue);
}

void csProcTexture::SetFlags (int flags)
{
  tex->SetFlags (flags);
}

int csProcTexture::GetFlags () const
{
  return tex->GetFlags();
}

void csProcTexture::Register (iTextureManager *txtmng)
{
  tex->Register (txtmng);
}

void csProcTexture::SetUseCallback (iTextureCallback* callback)
{
  tex->SetUseCallback (callback);
}

iTextureCallback* csProcTexture::GetUseCallback () const
{
  return tex->GetUseCallback();
}

void csProcTexture::Visit ()
{
  tex->Visit();
}

bool csProcTexture::IsVisitRequired () const
{
  return tex->IsVisitRequired ();
}

void csProcTexture::SetKeepImage (bool k)
{
  tex->SetKeepImage (k);
}

bool csProcTexture::KeepImage () const
{
  return tex->KeepImage();
}

void csProcTexture::SetTextureClass (const char* className)
{
  tex->SetTextureClass (className);
}

const char* csProcTexture::GetTextureClass ()
{
  return tex->GetTextureClass();
}

//-----------------------------------------------------------------------------

iTextureFactory* csProcTexture::GetFactory()
{
  return parent;
}

