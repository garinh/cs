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

#ifndef __CS_GLX2D_H__
#define __CS_GLX2D_H__

#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>
#define CS_GLEXTMANAGER_USE_GLX

#include "csutil/scf.h"
#include "csplugincommon/opengl/glcommon2d.h"
#include "csplugincommon/iopengl/openglinterface.h"
#include "ivaria/xwindow.h"

#include "iogldisp.h"

#ifndef XK_MISCELLANY
#define XK_MISCELLANY 1
#endif
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

/// XLIB version.
class csGraphics2DGLX : public scfImplementationExt1<csGraphics2DGLX , 
  csGraphics2DGLCommon, iOpenGLInterface>
{
  csRef<iXWindow> xwin;
  // The display context
  Display* dpy;
  int screen_num;
  Window window;
  XVisualInfo *xvis;
  Colormap cmap;
  GLXContext active_GLContext;
  bool hardwareaccelerated;

  // we are using a specific displaydriver
  csRef<iOpenGLDisp> dispdriver;

  /**
   * Helper function, attempts to choose a visual with the
   * desired attributes
   */
  bool ChooseVisual ();
  /// Obtain (and report) the actual attributes of the context.
  void GetCurrentAttributes ();

public:
  csGraphics2DGLX (iBase *iParent);
  virtual ~csGraphics2DGLX ();

  virtual bool Initialize (iObjectRegistry *object_reg);
  virtual bool Open ();
  virtual void Close ();
  
  const char* GetVersionString (const char* ver);

  void Report (int severity, const char* msg, ...);

  virtual void Print (csRect const* area = 0);

  virtual bool PerformExtensionV (char const* command, va_list);

  virtual void AllowResize (bool iAllow);

  virtual void AlertV (int type, const char* title, const char* okMsg,
  	const char* msg, va_list arg) 
  {
    if (!GetFullScreen ())
    {
      if (!xwin->AlertV (type, title, okMsg, msg, arg))
        csGraphics2DGLCommon::AlertV (type, title, okMsg, msg, arg);
    }
  }

  virtual void SetTitle (const char* title)
  { xwin->SetTitle (title); }
  
  /** Sets the icon of this window with the provided one.
   *
   *  @param image the iImage to set as the icon of this window.
   */
  virtual void SetIcon (iImage *image)
  { xwin->SetIcon (image); }

  virtual void SetFullScreen (bool yesno);

  virtual bool GetFullScreen ()
  { return xwin->GetFullScreen (); }
  /// Set mouse position.
  // should be the window manager
  virtual bool SetMousePosition (int x, int y)
  { return xwin->SetMousePosition (x, y); }

  /// Set mouse cursor shape
  // should be the window manager
  virtual bool SetMouseCursor (csMouseCursorID iShape)
  { return xwin->SetMouseCursor (iShape);}

  virtual bool SetMouseCursor (iImage *image, const csRGBcolor* keycolor, 
    int hotspot_x, int hotspot_y, csRGBcolor fg, csRGBcolor bg)
  { 
    return xwin->SetMouseCursor (image, keycolor, hotspot_x, hotspot_y,
      fg, bg);
  }

  virtual void *GetProcAddress (const char *funcname)
  {
# ifndef CSGL_EXT_STATIC_ASSERTION
    return (void*)glXGetProcAddressARB ((const GLubyte *)funcname);
# else
    (void)funcname;
    return (void*)0;
# endif
  }
};

#endif // __CS_GLX2D_H__
