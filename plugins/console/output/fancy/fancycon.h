/*
    Copyright (C) 2000 by Norman Kramer

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

#ifndef __CS_FANCYCON_H__
#define __CS_FANCYCON_H__

#include "ivaria/conout.h"
#include "iutil/eventh.h"
#include "iutil/comp.h"
#include "csgeom/csrect.h"
#include "csutil/scf_implementation.h"

struct iGraphics2D;
struct iGraphics3D;
struct iVFS;
struct iImageIO;
struct iTextureHandle;

struct ConDecoBorder
{
  csRef<iTextureHandle> txt;
  bool do_keycolor;
  uint8 kr, kg, kb;
  bool do_stretch;
  int offx, offy;
  bool do_alpha;
  float alpha;
};

struct ConsoleDecoration
{
  ConDecoBorder border[8];
  ConDecoBorder bgnd;
  int lx, rx, ty, by;
  int p2lx, p2rx, p2ty, p2by;
};

class csFancyConsole :
  public scfImplementation3<csFancyConsole,
    iConsoleOutput, iComponent, iEventHandler>
{
private:
  iObjectRegistry *object_reg;
  csRef<iVFS> VFS;
  csRef<iConsoleOutput> base;
  csRef<iGraphics2D> G2D;
  csRef<iGraphics3D> G3D;
  csRef<iImageIO> ImageLoader;
  csRef<iEventHandler> weakEventHandler;
  ConsoleDecoration deco;
  csRect outersize, bordersize, p2size;
  bool border_computed;
  bool pix_loaded;
  bool system_ready;
  bool auto_update;
  bool visible;

  CS_DECLARE_SYSTEM_EVENT_SHORTCUTS;

  void LoadPix();
  void PrepPix(iConfigFile *ini, const char *sect, ConDecoBorder &border,
    bool bgnd );
  void DrawBorder(int x, int y, int width, int height, ConDecoBorder &border,
    int align );
  void GetPosition(int &x, int &y, int &width, int &height) const;
  void SetPosition(int x, int y, int width = -1, int height = -1);

public:
  csFancyConsole (iBase *);
  virtual ~csFancyConsole ();

  void Report (int severity, const char* msg, ...);

  virtual bool Initialize (iObjectRegistry *);
  virtual bool HandleEvent (iEvent &Event);
  virtual void PutText (const char *iText, ...)
  {
    va_list arg;
    va_start (arg, iText);
    PutTextV (iText, arg);
    va_end (arg);
  }
  virtual void PutTextV (const char *iText, va_list args);
  virtual const char *GetLine (int iLine = -1) const
    { return base->GetLine(iLine); }
  virtual void Draw2D (csRect *oRect = 0) { base->Draw2D(oRect); }
  virtual void Draw3D (csRect *rect);
  virtual void Clear (bool iWipe = false) { base->Clear(iWipe); }
  virtual void SetBufferSize (int n) { base->SetBufferSize(n); }
  virtual bool GetTransparency () const { return base->GetTransparency(); }
  virtual void SetTransparency (bool) { base->SetTransparency (true); }
  virtual iFont *GetFont () const { return base->GetFont(); }
  virtual void SetFont (iFont *Font) { base->SetFont(Font); }
  virtual int GetTopLine () const { return base->GetTopLine(); }
  virtual void ScrollTo (int iTopLine, bool iSnap = true)
  { base->ScrollTo(iTopLine, iSnap); }
  virtual int GetCursorStyle () const { return base->GetCursorStyle(); }
  virtual void SetCursorStyle (int iStyle) { base->SetCursorStyle(iStyle); }
  virtual void SetVisible (bool b) { visible = b; base->SetVisible(b); }
  virtual bool GetVisible () { return visible; }
  virtual void AutoUpdate (bool b) { auto_update = b; base->AutoUpdate(b); }
  virtual void SetCursorPos (int p) { base->SetCursorPos(p); }
  virtual int GetMaxLineWidth () { return base->GetMaxLineWidth(); }
  virtual void RegisterWatcher (iConsoleWatcher *p)
  { base->RegisterWatcher(p); }
  virtual bool PerformExtension (const char *command, ...);
  virtual bool PerformExtensionV (const char *iCommand, va_list);

  CS_EVENTHANDLER_NAMES("crystalspace.console")
  virtual const csHandlerID * GenericPrec(
    csRef<iEventHandlerRegistry> &r1, csRef<iEventNameRegistry> &r2,
    csEventID e) const
  {
    if (e == csevSystemOpen (r2))
    {
      /* TODO : not thread-safe */
        static csHandlerID precs[2] =
        { CS_HANDLERLIST_END, CS_HANDLERLIST_END };
	static bool precs_init;
	if (!precs_init)
	{
	  precs_init = true;
	  precs[0] = r1->GetGenericID("crystalspace.graphics3d");
	}
      return precs;
    } else {
      return 0;
    }
  }
  virtual const csHandlerID * GenericSucc(
    csRef<iEventHandlerRegistry> &, csRef<iEventNameRegistry> &,
    csEventID) const
  { return 0; }
    
  CS_EVENTHANDLER_DEFAULT_INSTANCE_CONSTRAINTS
};

#endif // __CS_FANCYCON_H__
