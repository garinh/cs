/*
    Copyright (C) 2002 by Norman Kraemer

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
#include "csutil/sysfunc.h"
#include "csutil/csuctransform.h"
#include "csutil/csstring.h"
#include "csutil/databuf.h"
#include "iutil/cfgfile.h"
#include "iutil/objreg.h"
#include "ivaria/reporter.h"
#include "iutil/vfs.h"
#include "iutil/plugin.h"
#include "iutil/eventh.h"
#include "iutil/comp.h"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_MODULE_H
#include FT_SIZES_H
#include FT_OUTLINE_H

#include "freefnt2.h"



CS_PLUGIN_NAMESPACE_BEGIN(FreeFont2)
{

SCF_IMPLEMENT_FACTORY (csFreeType2Server)

csFt2FaceWrapper::csFt2FaceWrapper (csFreeType2Server* owner,
				    iDataBuffer* data,
				    char* faceName) : face(0)
{
  csFt2FaceWrapper::owner = owner;
  csFt2FaceWrapper::data = data;
  csFt2FaceWrapper::faceName = faceName;
}

csFt2FaceWrapper::~csFt2FaceWrapper ()
{
  if (face)
    FT_Done_Face (face);
  owner->RemoveFT2Face (this, faceName);
}

//---------------------------------------------------------------------------

csFreeType2Server::csFreeType2Server (iBase *pParent) : 
  scfImplementationType (this, pParent), library (0), freetype_inited (false),
  emitErrors (true)
{
}

csFreeType2Server::~csFreeType2Server ()
{
  fonts.DeleteAll();
  if (freetype_inited) 
  {
    FT_Done_FreeType (library);
  }
}

void csFreeType2Server::Report (int severity, const char* msg, ...)
{
  va_list arg;
  va_start (arg, msg);
  ReportV (severity, msg, arg);
  va_end (arg);
}

void csFreeType2Server::ReportV (int severity, const char* msg, va_list arg)
{
  csReportV (object_reg, severity, "crystalspace.font.freefont2", msg, arg);
}

const char* csFreeType2Server::GetErrorDescription(int code)
{
  #undef __FTERRORS_H__
  #define FT_ERROR_START_LIST		    switch (code) {
  #define FT_ERRORDEF(Error, Value, String) case Value: return String;
  #define FT_ERROR_END_LIST		    default: return "unknown error"; }
  #include FT_ERRORS_H
}

bool csFreeType2Server::FreetypeError (int errorCode, int reportSeverity, 
				       const char* message, ...)
{
  if (errorCode != 0)
  {
    va_list arg;
    va_start (arg, message);
    csString msg;
    msg.FormatV (message, arg);
    va_end (arg);

    Report (reportSeverity,
      "%s: %s (code %d)", 
      msg.GetData (), GetErrorDescription (errorCode), errorCode);
    return true;
  }
  else
  {
    return false;
  }
}

bool csFreeType2Server::FreetypeError (int errorCode, const char* message, 
				       ...)
{
  if (errorCode != 0)
  {
    va_list arg;
    va_start (arg, message);
    csString msg;
    msg.FormatV (message, arg);
    va_end (arg);

    Report (GetErrorSeverity (),
      "%s: %s (code %d)", 
      msg.GetData (), GetErrorDescription (errorCode), errorCode);
    return true;
  }
  else
  {
    return false;
  }
}

void csFreeType2Server::RemoveFT2Face (csFt2FaceWrapper* face, 
				       char* faceName)
{
  ftfaces.Delete (faceName, face);
  cs_free (faceName);
}

void csFreeType2Server::RemoveFont (iFont* font, 
				    char* fontId)
{
  fonts.Delete (fontId, font);
  cs_free (fontId);
}

bool csFreeType2Server::Initialize (iObjectRegistry *object_reg)
{
  csFreeType2Server::object_reg = object_reg;

  freetype_inited = !FreetypeError (FT_Init_FreeType (&library),
    CS_REPORTER_SEVERITY_ERROR,
    "Could not create a FreeType engine instance");

  if (!freetype_inited)
  {
    return false;
  }

  VFS = csQueryRegistry<iVFS> (object_reg);
  ftconfig.AddConfig(object_reg, "config/freetype.cfg");

  fontset = ftconfig->GetStr ("Freetype2.Settings.FontSet", 0);

  return true;
}

csPtr<iFont> csFreeType2Server::LoadFont (const char *filename, float size)
{
  // First of all look for an alias in config file
  if (ftconfig && fontset)
  {
    csString Keyname;
    Keyname << "Freetype2.Fonts.";
    if (fontset) Keyname << fontset << '.';
    Keyname << filename;
    const char *s = ftconfig->GetStr (Keyname, 0);
    if (s) filename = s;
  }
  csString fontid;
  fontid.Format ("%g:%s", size, filename);
  // see if we already loaded that face/size pair
  csRef<iFont> font = fonts.Get ((const char*)fontid, 0);
  if (font)
  {
    return csPtr<iFont> (font);
  }

  // see if we already loaded that face
  csRef<csFt2FaceWrapper> face = ftfaces.Get (filename, 0);
  if (face == 0)
  {
    // not yet loaded, so do it now
    csRef<iFile> file (VFS->Open (filename, VFS_FILE_READ));
    if (file)
    {
      size_t size = file->GetSize ();
      if (size)
      {
	csRef<iDataBuffer> fontdata = file->GetAllData ();

	// @@@ kludge: don't report error on CSF files(or?)
	if ((size >= 3) && (strncmp (fontdata->GetData (), "CSF", 3) == 0))
	{
	  return 0;
	}
	FT_Face ftFace;
	if (FreetypeError (FT_New_Memory_Face (library, 
	  (FT_Byte*)fontdata->GetData (), (FT_Long)size, 0, &ftFace),
	  "Font file %s could not be loaded", filename))
	{
	  return 0;
	}

	// we do not change the default values of the new instance

	// Attempt to select an Unicode charmap
	if (FreetypeError (FT_Select_Charmap (ftFace, FT_ENCODING_UNICODE),
	  CS_REPORTER_SEVERITY_NOTIFY,
	  "Could not select an Unicode charmap for %s", filename))
	{
	  if (ftFace->num_charmaps == 0)
	  {
	    Report (GetErrorSeverity (),
	      "Fontfile %s doesn't contain charmaps", filename);
	    return 0;
	  }

	  FT_CharMap map = ftFace->charmaps[0];
	  char encName[5];
	  encName[0] = map->encoding >> 24;
	  encName[1] = (map->encoding >> 16) & 0xff;
	  encName[2] = (map->encoding >> 8) & 0xff;
	  encName[3] = map->encoding & 0xff;
	  encName[4] = 0;
	  if (FreetypeError (FT_Set_Charmap (ftFace, map),
	    "Could not select charmap '%s' for %s", encName, filename))
	  {
	    return 0;
	  }
	  else
	  {
	    Report (GetErrorSeverity (),
	      "Using charmap '%s' for %s", encName, filename);
	  }
	}

        char* newFilename = CS::StrDup (filename);
	face.AttachNew (new csFt2FaceWrapper (this, fontdata, newFilename));
	face->face = ftFace;
	ftfaces.Put (newFilename, face);
      }
      else
      {
	  Report (GetErrorSeverity (),
	    "Could not determine filesize for fontfile %s!", filename);
	return 0;
      }
    }
    else
    {
      if (emitErrors)
        Report (CS_REPORTER_SEVERITY_WARNING,
	  "Could not open fontfile %s!", filename);
      return 0;
    }
  }

  char* newFontId = CS::StrDup (fontid);
  font.AttachNew (new csFreeType2Font (this, newFontId, face, size));
  fonts.Put (newFontId, font);

  return csPtr<iFont> (font);
}

//-------------------------------------------// A FreeType font object //----//

csFreeType2Font::csFreeType2Font (csFreeType2Server* server, 
				  char* fontid,
				  csFt2FaceWrapper* face, 
				  float iSize) :
  scfImplementationType (this), DeleteCallbacks (4)
{
  name = strchr (fontid, ':') + 1;
  csFreeType2Font::fontid = fontid;
  csFreeType2Font::face = face;
  csFreeType2Font::server = server;
  fontSize = iSize;

  FT_New_Size (face->face, &size);
  FT_Activate_Size (size);
  int ftError = FT_Set_Char_Size (face->face, 0, 
    (int)(iSize * 64.0f), 96, 96);
  if (ftError != 0)
  {
    int ftError2 = FT_Set_Pixel_Sizes (face->face, 0, 
      (int)iSize);
    if (ftError2 != 0)
    {
      server->FreetypeError (ftError, 
	"Could not set character dimensions for %s",
	name);
      server->FreetypeError (ftError2, 
	"Could not set character pixel dimensions for %s",
	name);
    }
  }
}

csFreeType2Font::~csFreeType2Font ()
{
  size_t i = DeleteCallbacks.GetSize ();
  while (i-- > 0)
  {
    iFontDeleteNotify* delnot = DeleteCallbacks[i];
    delnot->BeforeDelete (this);
  }
  FT_Done_Size (size);
  server->RemoveFont (this, fontid);
  face = 0;
  //delete [] name;
}

float csFreeType2Font::GetSize ()
{
  return fontSize;
}

void csFreeType2Font::GetMaxSize (int &oW, int &oH)
{
  int maxrows = (size->metrics.height + 63) >> 6;
  oW = (size->metrics.max_advance + 63) >> 6;
  oH = maxrows;
}

bool csFreeType2Font::GetGlyphMetrics (utf32_char c, csGlyphMetrics& metrics)
{
  const csGlyphMetrics* cachedMetrics;
  if ((cachedMetrics = glyphMetrics.GetElementPointer (c)) != 0)
  {
    metrics = *cachedMetrics;
    return true;
  }

  FT_UInt ci = (c == CS_FONT_DEFAULT_GLYPH) ? 0 : 
   FT_Get_Char_Index (face->face, (FT_ULong)c);
  if ((c != CS_FONT_DEFAULT_GLYPH) && (ci == 0)) return 0;

  FT_Activate_Size (size);
  if (server->FreetypeError (FT_Load_Glyph (face->face, ci, FT_LOAD_DEFAULT),
    "Could not load glyph %u for %s", ci, name))
  {
    return false;
  }
  
  metrics.advance = face->face->glyph->advance.x >> 6;
  glyphMetrics.Put (c, metrics);

  return true;
}

static void GridFitCbox (FT_BBox& cbox, FT_UInt& glyphW, FT_UInt& glyphH)
{
  /* compute the control box, and grid fit it */
#define FT_PIX_FLOOR( x )     ( (x) & ~63 )
#define FT_PIX_CEIL( x )      FT_PIX_FLOOR( (x) + 63 )
  cbox.xMin = FT_PIX_FLOOR( cbox.xMin );
  cbox.yMin = FT_PIX_FLOOR( cbox.yMin );
  cbox.xMax = FT_PIX_CEIL( cbox.xMax );
  cbox.yMax = FT_PIX_CEIL( cbox.yMax );

  glyphW = (FT_UInt)( ( cbox.xMax - cbox.xMin ) >> 6 );
  glyphH = (FT_UInt)( ( cbox.yMax - cbox.yMin ) >> 6 );
}

csPtr<iDataBuffer> csFreeType2Font::GetGlyphBitmap (utf32_char c,
						    csBitmapMetrics& metrics)
{
  FT_Activate_Size (size);
  FT_UInt ci = (c == CS_FONT_DEFAULT_GLYPH) ? 0 : 
    FT_Get_Char_Index (face->face, (FT_ULong)c);
  if ((c != CS_FONT_DEFAULT_GLYPH) && (ci == 0)) return 0;

  if (server->FreetypeError (FT_Load_Glyph (face->face, ci, 
    FT_LOAD_MONOCHROME | FT_LOAD_TARGET_MONO),
    "Could not load glyph %u for %s", ci, name))
  {
    return 0;
  }

  /* Work around an FT2.2.1 issue where rendering of a glyph fails if the width
   * or height is 0 */
  FT_Outline* outline = &face->face->glyph->outline;
  FT_BBox cbox;
  FT_Outline_Get_CBox( outline, &cbox );
  FT_UInt glyphW, glyphH;
  GridFitCbox (cbox, glyphW, glyphH);

  int maxrows = (size->metrics.height + 63) >> 6;
  int stride, bitmapsize;
  uint8* bitmap;
  if ((glyphW > 0) && (glyphH > 0))
  {
    if (server->FreetypeError (FT_Render_Glyph (face->face->glyph, 
      FT_RENDER_MODE_MONO),
      "Could not render glyph %u for %s", ci, name))
    {
      return 0;
    }
    stride = (face->face->glyph->bitmap.width + 7) / 8;
    bitmapsize = maxrows*stride;
  }
  else
    bitmapsize = 0;

  int descend = (-size->metrics.descender + 63) >> 6;;

  if (bitmapsize > 0)
  {
    int startrow = maxrows - (descend + face->face->glyph->bitmap_top);
    int endrow = startrow + face->face->glyph->bitmap.rows;

    if (startrow < 0) startrow = 0;
    if (endrow > maxrows) endrow = maxrows;

    bitmap = new uint8 [bitmapsize];
    memset (bitmap, 0, bitmapsize);
    int n, i;
    for (n = 0, i = startrow; i < endrow; i++, n++)
      memcpy (bitmap + stride*i, 
              face->face->glyph->bitmap.buffer + 
	      n * face->face->glyph->bitmap.pitch,
              MIN(stride, face->face->glyph->bitmap.pitch));
    metrics.width = face->face->glyph->bitmap.width;
    metrics.left = face->face->glyph->bitmap_left;
  }
  else
  {
    bitmap = 0;
    metrics.width = glyphW >> 6;
    metrics.left = (FT_Int)( cbox.xMin >> 6 );
  }

  metrics.height = maxrows;
  metrics.top = maxrows - descend;

  return (csPtr<iDataBuffer> (new csDataBuffer ((char*)bitmap, bitmapsize, 
    true)));
}

csPtr<iDataBuffer> csFreeType2Font::GetGlyphAlphaBitmap (utf32_char c, 
						 csBitmapMetrics& metrics)
{
  FT_Activate_Size (size);
  FT_UInt ci = (c == CS_FONT_DEFAULT_GLYPH) ? 0 : 
    FT_Get_Char_Index (face->face, (FT_ULong)c);
  if ((c != CS_FONT_DEFAULT_GLYPH) && (ci == 0)) return 0;

  if (server->FreetypeError (FT_Load_Glyph (face->face, ci, 
    FT_LOAD_TARGET_NORMAL),
    "Could not load glyph %u for %s", ci, name))
  {
    return 0;
  }

  /* Work around an FT2.2.1 issue where rendering of a glyph fails if the width
   * or height is 0 */
  FT_Outline* outline = &face->face->glyph->outline;
  FT_BBox cbox;
  FT_Outline_Get_CBox( outline, &cbox );
  FT_UInt glyphW, glyphH;
  GridFitCbox (cbox, glyphW, glyphH);

  int maxrows = (size->metrics.height + 63) >> 6;
  int stride, bitmapsize;
  uint8* bitmap;
  if ((glyphW > 0) && (glyphH > 0))
  {
    if (server->FreetypeError (FT_Render_Glyph (face->face->glyph, 
      FT_RENDER_MODE_NORMAL),
      "Could not render glyph %u for %s", ci, name))
    {
      return 0;
    }
    if (face->face->glyph->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY)
    {
      // That's not what we want.
      return 0;
    }
    stride = face->face->glyph->bitmap.width;
    bitmapsize = maxrows*stride;
    metrics.width = face->face->glyph->bitmap.width;
    metrics.left = face->face->glyph->bitmap_left;
  }
  else
  {
    bitmapsize = 0;
    metrics.width = glyphW >> 6;
    metrics.left = (FT_Int)( cbox.xMin >> 6 );
  }

  int descend = (-size->metrics.descender + 63) >> 6;

  if (bitmapsize > 0)
  {
    int startrow = maxrows - (descend + face->face->glyph->bitmap_top);
    int endrow = startrow + face->face->glyph->bitmap.rows;

    if (startrow < 0) startrow = 0;
    if (endrow > maxrows) endrow = maxrows;

    bitmap = new uint8 [bitmapsize];
    memset (bitmap, 0, bitmapsize);
    int n, i;
    for (n = 0, i = startrow; i < endrow; i++, n++)
      memcpy (bitmap + stride*i, 
              face->face->glyph->bitmap.buffer + 
	      n * face->face->glyph->bitmap.pitch,
              MIN(stride, face->face->glyph->bitmap.pitch));
  }
  else
    bitmap = 0;

  metrics.height = maxrows;
  metrics.top = maxrows - descend;

  return (csPtr<iDataBuffer> (new csDataBuffer ((char*)bitmap, bitmapsize, 
    true)));
}

void csFreeType2Font::GetDimensions (const char *text, int &oW, int &oH, int &desc)
{
  if (!text)
  {
    oW = oH = desc = 0;
    return;
  }

  FT_Activate_Size (size);
  int defW = 0;
  if (!server->FreetypeError (FT_Load_Glyph (face->face, 0, 
    FT_LOAD_DEFAULT),
    "Could not load glyph %u for %s", 0, name))
  {
    defW = (face->face->glyph->advance.x >> 6);
  }

  oW = 0; 
  oH = (size->metrics.height + 63) >> 6; 
  desc = (-size->metrics.descender + 63) >> 6;
  size_t textLen = strlen ((char*)text);
  while (textLen > 0)
  {
    utf32_char glyph;
    int skip = csUnicodeTransform::UTF8Decode ((utf8_char*)text, textLen, glyph, 0);
    if (skip == 0) break;

    text += skip;
    textLen -= skip;

    const csGlyphMetrics* metrics = glyphMetrics.GetElementPointer (glyph);
    if (metrics != 0)
    {
      oW += metrics->advance;
      continue;
    }

    FT_UInt ci = FT_Get_Char_Index (face->face, (FT_ULong)glyph);

    if (!server->FreetypeError (FT_Load_Glyph (face->face, ci, 
      FT_LOAD_DEFAULT),
      "Could not load glyph %u for %s", ci, name))
    {
      csGlyphMetrics metrics;
      metrics.advance = (face->face->glyph->advance.x >> 6);
      glyphMetrics.Put (glyph, metrics);

      oW += metrics.advance;
    }
    else
    {
      oW += defW;
    }
  }
}

void csFreeType2Font::GetDimensions (const char *text, int &oW, int &oH)
{
  int dummy;
  GetDimensions (text, oW, oH, dummy);
}

int csFreeType2Font::GetLength (const char *text, int maxwidth)
{
  // @@@ Improve.
  if (!text)
    return 0;

  FT_Activate_Size (size);
  int defW = 0;
  if (!server->FreetypeError (FT_Load_Glyph (face->face, 0, 
    FT_LOAD_DEFAULT),
    "Could not load glyph %u for %s", 0, name))
  {
    defW = (face->face->glyph->advance.x >> 6);
  }

  int count = 0;
  size_t textLen = strlen ((char*)text);
  while (textLen > 0)
  {
    utf32_char glyph;
    int skip = csUnicodeTransform::UTF8Decode ((utf8_char*)text, textLen, glyph, 0);
    if (skip == 0) break;

    text += skip;
    textLen -= skip;

    int glyphW = defW;
    const csGlyphMetrics* metrics = glyphMetrics.GetElementPointer (glyph);
    if (metrics != 0)
      glyphW = metrics->advance;
    else
    {
      FT_UInt ci = FT_Get_Char_Index (face->face, (FT_ULong)glyph);

      if (!server->FreetypeError (FT_Load_Glyph (face->face, ci, 
	FT_LOAD_DEFAULT),
	"Could not load glyph %u for %s", ci, name))
      {
	csGlyphMetrics metrics;
	metrics.advance = (face->face->glyph->advance.x >> 6);
	glyphMetrics.Put (glyph, metrics);

	glyphW = metrics.advance;
      }
    }
    if (maxwidth < glyphW)
      break;
    count += skip;
    maxwidth -= glyphW;
  }
  return count;
}

void csFreeType2Font::AddDeleteCallback (iFontDeleteNotify* func)
{
  DeleteCallbacks.Push (func);
}

bool csFreeType2Font::RemoveDeleteCallback (iFontDeleteNotify* func)
{
  size_t i = DeleteCallbacks.GetSize ();
  while (i-- > 0)
  {
    iFontDeleteNotify* delnot = DeleteCallbacks[i];
    if (delnot == func)
    {
      DeleteCallbacks.DeleteIndex (i);
      return true;
    }
  }
  return false;
}

int csFreeType2Font::GetAscent ()
{
  return (size->metrics.ascender + 63) >> 6;
}

int csFreeType2Font::GetDescent ()
{
  return ((-size->metrics.descender + 63) >> 6);
}

bool csFreeType2Font::HasGlyph (utf32_char c)
{
  if (c == CS_FONT_DEFAULT_GLYPH) return true;

  return (FT_Get_Char_Index (face->face, (FT_ULong)c) != 0);
}

int csFreeType2Font::GetTextHeight ()
{
  FT_Short height = face->face->height;

  // Need to convert from font units to pixel units.
  return height * size->metrics.y_ppem / face->face->units_per_EM;
}

int csFreeType2Font::GetUnderlinePosition ()
{
  FT_Short underline_position = face->face->underline_position;

  // Need to convert from font units to pixel units.
  float temp = underline_position * size->metrics.y_ppem / 
    (float)face->face->units_per_EM;

  // The value returned from freetype is the opposite of what we want.
  temp *= -1;

  // Round to the nearest integer.
  if(temp > 0)
  {
    temp += 0.5;
  }
  else
  {
    temp -= 0.5;
  }
  return (int)temp;
}

int csFreeType2Font::GetUnderlineThickness ()
{
  FT_Short underline_thickness = face->face->underline_thickness;

  // Need to convert from font units to pixel units.
  underline_thickness = (int)(underline_thickness * size->metrics.y_ppem / 
    (float)face->face->units_per_EM + 0.5);

  // The thicknes should never be 0.
  if(underline_thickness == 0)
  {
    underline_thickness = 1;
  }
  return underline_thickness;
}

}
CS_PLUGIN_NAMESPACE_END(FreeFont2)
