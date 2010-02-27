/*
    Copyright (C) 2004 by Peter Amstutz

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

#ifndef _CS_IVIDEO_WXWIN_H_
#define _CS_IVIDEO_WXWIN_H_

/**\file
 * wxWidgets specific interfaces
 */

#include "csutil/scf.h"

// wxWidgets boilerplate.
#ifndef _WX_WXH__
#include <wx/wxprec.h>
#ifdef __BORLANDC__
#pragma hdrstop
#endif
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#endif

/**
 * Interface to communicate with with a wxWidgets-based canvas.
 */
struct iWxWindow : public virtual iBase
{
  SCF_INTERFACE(iWxWindow, 0, 0, 1);

  /// Set wxWidgets parent window of the canvas.
  virtual void SetParent(wxWindow* parent) = 0;
  /// Get wxWidgets window object of the canvas.
  virtual wxWindow* GetWindow() = 0;
};

#endif // _CS_IVIDEO_WXWIN_H_
