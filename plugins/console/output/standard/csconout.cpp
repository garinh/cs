/*
    Copyright (C) 2000 by Michael Dale Long

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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "csutil/sysfunc.h"
#include "csutil/cfgacc.h"
#include "csconout.h"
#include "conbuff.h"
#include "ivaria/conout.h"
#include "ivaria/reporter.h"
#include "ivideo/graph2d.h"
#include "ivideo/graph3d.h"
#include "iutil/plugin.h"
#include "ivideo/txtmgr.h"
#include "ivideo/fontserv.h"
#include "csutil/csevent.h"
#include "csutil/event.h"
#include "csgeom/csrect.h"
#include "csutil/csstring.h"
#include "iutil/eventq.h"
#include "iutil/objreg.h"
#include "iutil/eventh.h"
#include "iutil/comp.h"
#include "iutil/cfgmgr.h"



CS_PLUGIN_NAMESPACE_BEGIN(ConOut)
{

SCF_IMPLEMENT_FACTORY (csConsoleOutput)


csConsoleOutput::csConsoleOutput (iBase *base) : 
  scfImplementationType (this, base), buffer (0)
{
  fg_rgb.Set (255, 255, 255);	// Foreground defaults to white
  bg_rgb.Set (0, 0, 0, 192);	// Background defaults to black, slightly transparent
  shadow_rgb.Set (32, 32, 32);	// Shadow defaults to darkgray
  transparent = false;		// Default to no transparency
  do_snap = true;		// Default to snapping
  // Initialize the cursor state variables
  cursor = csConNoCursor;
  cx = cy = 0;
  flash_interval = 500;
  cursor_visible = true;
  clear_input = false;
  auto_update = true;
  system_ready = false;
  visible = true;
  has_shadow = false;
  Client = 0;
  // clear font for closedown
  object_reg = 0;
  fg=0;
  bg=0;
  shadow=0;
}

csConsoleOutput::~csConsoleOutput ()
{
  if (eventHandler)
  {
    csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (object_reg));
    if (q != 0)
      q->RemoveListener (eventHandler);
  }
  delete buffer;
}

bool csConsoleOutput::Initialize (iObjectRegistry *object_reg)
{
  csConsoleOutput::object_reg = object_reg;
  G3D = csQueryRegistry<iGraphics3D> (object_reg);
  if (!G3D) return false;
  G2D = G3D->GetDriver2D ();

  csConfigAccess Config (object_reg, "/config/standardcon.cfg");
  const char* fontname = Config->GetStr ("StandardConsole.ConFont", "auto");
  int fontsize = Config->GetInt ("StandardConsole.ConFontSize", 10);
  transparent = Config->GetBool ("StandardConsole.TranspBG", false);

  // Initialize the display rectangle to the entire display
  size.Set (0, 0, G2D->GetWidth () - 1, G2D->GetHeight () - 1);
  invalid.Set (size); // Invalidate the entire console
  int fw, fh;
  csRef<iFontServer> fserv = G2D->GetFontServer();
  if (fserv)
  {
    if (!strcasecmp (fontname, "auto"))
    {
      // choose a font that allows at least 80 columns of text
      if (G2D->GetWidth () <= 560)
        fontname = CSFONT_SMALL;
      else if (G2D->GetWidth () <= 640)
        fontname = CSFONT_COURIER;
      else
        fontname = CSFONT_LARGE;
      fontsize = 10;
    }
    font = fserv->LoadFont (fontname, fontsize);
    font->GetMaxSize (fw, fh);
  }
  else
  {
    fw = fh = 20;
    csReport (object_reg, CS_REPORTER_SEVERITY_WARNING,
      "crystalspace.console.output.standard",
      "csConsoleOutput: Unable to locate iFontServer");
  }
  // Create the backbuffer (4096 lines max)
  buffer = new csConsoleBuffer (4096, (size.Height() / (fh + 2)));
  // Initialize flash_time for flashing cursors
  flash_time = csGetTicks ();

  // We want to see broadcast events
  CS_INITIALIZE_SYSTEM_EVENT_SHORTCUTS (object_reg);
  if (!eventHandler)
    eventHandler.AttachNew (new EventHandler (this));
  csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (object_reg));
  if (q.IsValid())
  {
    csEventID events[3] = { SystemOpen, SystemClose, CS_EVENTLIST_END };
    q->RegisterListener (eventHandler, events);
  }
  return true;
}

void csConsoleOutput::Clear (bool wipe)
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  if (wipe)
    // Clear the buffer
    buffer->Clear ();
  else
    // Put the top of the buffer on the current line
    buffer->SetTopLine (buffer->GetCurLine ());

  // Redraw the (now blank) console
  invalid.Set (size);
  // Reset the cursor position
  cx = cy = 0;
  clear_input = false;
}

void csConsoleOutput::SetBufferSize (int lines)
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);

  buffer->SetLength (lines);
}

void csConsoleOutput::PutTextV (const char *text2, va_list args)
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);

  size_t i;
  csString *curline = 0;

  csString text;
  text.FormatV (text2, args);

  // Scan the string for escape characters
  for (i = 0; i < text.Length(); i++)
  {
    if (clear_input)
    {
      curline = buffer->WriteLine ();
      curline->Clear ();
      clear_input = false;
      cx = 0;
    }

    switch (text [i])
    {
      case '\r':
        clear_input = true;
        break;
      case '\n':
        buffer->NewLine (do_snap);
        // Update the cursor Y position
        cx = 0;
        if (do_snap)
          cy = buffer->GetCurLine () - buffer->GetTopLine ();
        else
          ++cy < buffer->GetPageSize () ? cy : cy--;
        // Make sure we don't change the X position below
        curline = 0;
        break;
      case '\b':
        if (cx > 0)
        {
          if (cx == 1)
          {
            cx = 0;
            buffer->DeleteLine (cy);
            curline = 0;
          }
          else
          {
            // Delete the character before the cursor, and move the cursor back
            curline = buffer->WriteLine ();
            curline->DeleteAt (--cx);
          }
        }
        else if (cy > 0)
        {
          // Backup a line
          cy--;
          buffer->SetCurLine (cy);
          curline = buffer->WriteLine ();
        }
        break;
      case '\t':
        // Print 4-space tabs
        curline = buffer->WriteLine ();
        curline->Append ("    ");
        cx += 4;
        break;
      default:
        curline = buffer->WriteLine ();
        if (cx == (int)curline->Length ())
          curline->Append (text [i]);
        else
          curline->Insert (cx, text [i]);
        cx++;
        break;
      }
  }

  if (auto_update && system_ready && G2D->BeginDraw ())
  {
    csRect rect;
    G2D->Clear (bg);
    Draw2D (&rect);
    G2D->FinishDraw ();
    G2D->Print (&rect);
  }
}

const char *csConsoleOutput::GetLine (int line) const
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  return buffer->GetLine ((line == -1) ?
    (buffer->GetCurLine () - buffer->GetTopLine ()) : line)->GetData ();
}

void csConsoleOutput::DeleteText (int start, int end)
{
  csString *text = buffer->WriteLine ();
  int length = (int)text->Length ();

  // Avoid invalid start points
  if (start > length)
    return;

  // Make sure we don't go past the end of the string
  if ((end == -1) || (end >= length))
  {
    text->DeleteAt (start, length - start);
    cx = (int)text->Length ();
  }
  else
  {
    text->DeleteAt (start, end - start);
    cx -= end - start;
  }
}

void csConsoleOutput::Draw2D (csRect *area)
{
  if (!visible || !font) return;

  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  int i, height, fh;
  csRect line, oldrgn;
  const csString *text;
  bool dirty;

  invalid.Union (size);

  // Save old clipping region
  G2D->GetClipRect (oldrgn.xmin, oldrgn.ymin, oldrgn.xmax, oldrgn.ymax);
  G2D->SetClipRect (invalid.xmin, invalid.ymin, invalid.xmax, invalid.ymax);

  // Calculate the height of the text
  font->GetMaxSize (i, height);
  height += 2;

  // Draw the background
  if (!transparent)
    G2D->DrawBox (size.xmin, size.ymin, size.xmax, size.ymax, bg);

  // Make sure we redraw everything we need to
  if (area)
    area->Union (invalid);

  // Print all lines on the current page
  for (i = 0; i < buffer->GetPageSize (); i++)
  {
    // Retrieve the line from the buffer and it's dirty flag
    text = buffer->GetLine (i, &dirty);

    // A 0 line indicates it's the last printed line on the page
    if (text == 0)
      break;

    // Calculate the rectangle of this line
    line.Set (size.xmin, (i * height) + size.ymin,
      size.xmax, (i * height) + size.ymin + height);

    // See if the line changed or if the line intersects with the invalid area
    if (area && (dirty || line.Intersects (invalid)))
      area->Union (line);

    // draw the shadow if needed, behind the text
    if (has_shadow)
      G2D->Write (font, 1 + size.xmin + 1, (i * height) + size.ymin + 1, 
	  shadow, -1, text->GetData ());

    // Write the line
    G2D->Write (font, 1 + size.xmin, (i * height) + size.ymin, fg, -1,
      text->GetData ());
  }

  // Test for a change in the flash state
  if (flash_interval > 0)
  {
    csTicks cur_time = csGetTicks ();
    if (cur_time > flash_time + flash_interval || cur_time < flash_time)
    {
      cursor_visible = !cursor_visible;
      flash_time = cur_time;
    }
  }
  else
    cursor_visible = true;

  // See if we draw a cursor
  if ((cursor != csConNoCursor) && cursor_visible && (cy != -1))
  {
    int cx_pix, cy_pix;

    // Get the line of text that the cursor is on
    text = buffer->GetLine (cy);

    if (text == 0)
    {
#ifdef CS_DEBUG
      if (cx != 0)
	csPrintf (
	  "csConsoleOutput:  Current line is empty but cursor x != 0!\n");
#endif // CS_DEBUG
      cx_pix = 1;
    }
    else
    {
      // Make a copy of the text
      csString curText (*text);
      curText.Truncate(cx);
      font->GetDimensions (curText.GetData (), cx_pix, fh);
    }
    cy_pix = (cy * height) + size.ymin;
    cx_pix += size.xmin;

    int cursor_w;
    font->GetDimensions ("_", cursor_w, fh);
    line.Set (cx_pix, cy_pix, cx_pix + cursor_w, cy_pix + height);

    // Draw the appropriate cursor
    switch (cursor)
    {
      case csConInsertCursor:
        G2D->DrawLine (cx_pix + 1, cy_pix + (height-3), line.xmax,
	  cy_pix + (height-3), fg);
        break;
      case csConNormalCursor:
        G2D->DrawBox (cx_pix + 1, cy_pix + 1, line.xmax - 1 - (cx_pix + 1),
	  (height - 1) - 1, fg);
        break;
#ifdef CS_DEBUG
      default:
        csPrintf ("csConsoleOutput:  Invalid cursor setting!\n");
#endif // CS_DEBUG
    }
  }

  // Restore the original clipping region
  G2D->SetClipRect (oldrgn.xmin, oldrgn.ymin, oldrgn.xmax, oldrgn.ymax);

  // No more invalid area
  invalid.MakeEmpty ();
}

void csConsoleOutput::CacheColors ()
{
  fg = G2D->FindRGB (fg_rgb.red, fg_rgb.green, fg_rgb.blue, fg_rgb.alpha);
  bg = G2D->FindRGB (bg_rgb.red, bg_rgb.green, bg_rgb.blue, bg_rgb.alpha);
  shadow = G2D->FindRGB (shadow_rgb.red, shadow_rgb.green, shadow_rgb.blue, 
    shadow_rgb.alpha);
}

void
csConsoleOutput::GetPosition(int &x, int &y, int &width, int &height) const
{
  x = size.xmin;
  y = size.ymin;
  width = size.Width();
  height = size.Height();
}

void csConsoleOutput::SetPosition(int x, int y, int width, int height)
{
  if (!font) return;

  if (x >= 0)
    size.xmin = x;
  if (y >= 0)
    size.ymin = y;
  if (width >= 0)
    size.xmax = size.xmin + width;
  if (height >= 0)
    size.ymax = size.ymin + height;

  // Make sure we don't go off the current screen
  if (size.xmax >= G2D->GetWidth ())
    size.xmax = G2D->GetWidth () - 1;
  if (size.ymax >= G2D->GetHeight ())
    size.ymax = G2D->GetHeight () - 1;

  // Calculate the number of lines on the console
  int fw, fh;
  font->GetMaxSize (fw, fh);
  buffer->SetPageSize (size.Height () / (fh + 2));

  // Invalidate the entire new area of the console
  invalid.Set (size);

  // Update cursor coordinates
  cy = MIN (cy, buffer->GetPageSize ());
  // now check how many chars do fit in the current width
  const csString *text = buffer->GetLine (cy);
  if (!text)
    cx = 0;
  else
  {
    csString curText (*text);
    curText.Truncate (cx);
    while (cx)
    {
      int fw, fh;
      font->GetDimensions (curText.GetData (), fw, fh);
      if (fw <= size.Width ())
        break;
      curText.Truncate (--cx);
    }
  }
}

void csConsoleOutput::Invalidate (csRect &area)
{
  // Make sure we only update within our rectangle, otherwise 2D
  // driver may crash!
  csRect console (size);
  console.Intersect (area);
  if (!console.IsEmpty ())
    invalid.Union (console);
}

void csConsoleOutput::SetFont (iFont *Font)
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  if (font != Font)
  {
    font = Font;
    if (font)
    {
      // Calculate the number of lines on the console with the new font
      int fw, fh;
      font->GetMaxSize (fw, fh);
      buffer->SetPageSize (size.Height () / (fh + 2));
    }
  }
}

int csConsoleOutput::GetTopLine () const
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  return buffer->GetTopLine ();
}

void csConsoleOutput::ScrollTo(int top, bool snap)
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  switch (top)
  {
    case csConPageUp:
      buffer->SetTopLine(MAX(0, buffer->GetTopLine() - buffer->GetPageSize()));
      break;
    case csConPageDown:
      buffer->SetTopLine(buffer->GetTopLine () + buffer->GetPageSize ());
      break;
    case csConVeryTop:
      buffer->SetTopLine(0);
      break;
    case csConVeryBottom:
      buffer->SetTopLine(buffer->GetCurLine () - buffer->GetPageSize () + 1);
      break;
    default:
      buffer->SetTopLine(top);
      break;
  }

  if ((buffer->GetCurLine () >= buffer->GetTopLine()) &&
      (buffer->GetCurLine () <= buffer->GetTopLine() + buffer->GetPageSize()))
    cy = MAX (buffer->GetCurLine () - buffer->GetTopLine (), 0);
  else
    cy = -1;
  do_snap = snap;
}

void csConsoleOutput::GetCursorPos(int &x, int &y) const
{
  x = cx;
  y = cy;
}

void csConsoleOutput::SetCursorPos(int x, int y)
{
  int max_x, max_y = buffer->GetPageSize ();
  const csString *curline = buffer->GetLine (cy);

  if (curline)
    max_x = (int)curline->Length ();
  else
    max_x = 0;

  // Keep the cursor from going past the end of the line
  if (x > max_x)
    cx = max_x - 1;
  else
    cx = x;

  // Keep it from going off the bottom of the display
  if (y > max_y)
    cy = max_y - 1;
  else
    cy = y;
}

void csConsoleOutput::SetCursorPos (int iCharNo)
{
  CS::Threading::RecursiveMutexScopedLock lock (mutex);
  if (cy>-1)
  {
    int max_x;
    const csString *curline = buffer->GetLine (cy);

    max_x = curline ? (int)curline->Length () : 0;
    cx = (iCharNo > max_x) ? max_x : (iCharNo <= 0) ? 0 : iCharNo;
  }
}

void csConsoleOutput::SetVisible (bool iShow)
{
  visible = iShow;
  if (Client)
    Client->ConsoleVisibilityChanged(this, iShow);
  invalid.Set (size);
}

bool csConsoleOutput::PerformExtension (const char *iCommand, ...)
{
  va_list args;
  va_start (args, iCommand);
  bool rc = PerformExtensionV(iCommand, args);
  va_end (args);
  return rc;
}

bool csConsoleOutput::PerformExtensionV (const char *iCommand, va_list args)
{
  bool rc = true;
  if (!strcmp (iCommand, "FlashTime"))
    flash_interval = va_arg (args, csTicks);
  else if (!strcmp (iCommand, "GetPos"))
  {
    int *x = va_arg (args, int *);
    int *y = va_arg (args, int *);
    int *w = va_arg (args, int *);
    int *h = va_arg (args, int *);
    GetPosition (*x, *y, *w, *h);
  }
  else if (!strcmp (iCommand, "SetPos"))
  {
    int x = va_arg (args, int);
    int y = va_arg (args, int);
    int w = va_arg (args, int);
    int h = va_arg (args, int);
    SetPosition (x, y, w, h);
  }
  else if (!strcmp (iCommand, "GetBackgroundColor"))
  {
    int *bgcolor = va_arg (args, int *);
    *bgcolor = bg;
  }
  else if (!strcmp (iCommand, "GetForegroundColor"))
  {
    int *fgcolor = va_arg (args, int *);
    *fgcolor = fg;
  }
  else if (!strcmp (iCommand, "GetShadowColor"))
  {
    int *shcolor = va_arg (args, int *);
    *shcolor = shadow;
  }
  else if (!strcmp (iCommand, "SetBackgroundColor"))
  {
    bg = va_arg (args, int);
  }
  else if (!strcmp (iCommand, "SetForegroundColor"))
  {
    fg = va_arg (args, int);
  }
  else if (!strcmp (iCommand, "SetShadowColor"))
  {
    shadow = va_arg (args, int);
  }
  else if (!strcmp (iCommand, "EnableShadow"))
  {
    has_shadow = true;
  }
  else if (!strcmp (iCommand, "DisableShadow"))
  {
    has_shadow = false;
  }
  else
    rc = false;
  return rc;
}

bool csConsoleOutput::HandleEvent (iEvent &Event)
{
  if (Event.Name == SystemOpen)
  {
          system_ready = true;
          CacheColors ();
          return true;
  }
  else if (Event.Name == SystemClose)
  {
          system_ready = false;
          return true;
  }
  return false;
}

}
CS_PLUGIN_NAMESPACE_END(ConOut)
