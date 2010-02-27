/*
  Copyright (C) 2007 by Frank Richter

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

#include "common.h"

#include "material.h"

namespace lighter
{
  void RadMaterial::ComputeFilterImage (iImage* _img)
  {
    CS::ImageAutoConvert img (_img, CS_IMGFMT_TRUECOLOR);
    filterImage.AttachNew (new MaterialImage<csColor> (
      img->GetWidth(), img->GetHeight ()));
      
    ScopedSwapLock<MaterialImage<csColor> > imgLock (*filterImage);
      
    csRGBpixel* srcPtr = (csRGBpixel*)img->GetImageData ();
    csColor* dstPtr = filterImage->GetData();
    size_t numPixels = filterImage->GetWidth() * filterImage->GetHeight();
    while (numPixels-- > 0)
    {
      const float ub2f = 1.0f/255.0f;
      csColor c;
      c.Set (srcPtr->red * ub2f, srcPtr->green * ub2f, srcPtr->blue * ub2f);
      float a = srcPtr->alpha * ub2f;
      /* The filter image contains the color value the light passing through
         the texture is modulated with.
         When alpha goes toward 0, modulation should go toward white (pass light
         through).
         When alpha goes toward 1, modulation should go toward black (shadow).
         In between, the color also influences the light modulation.
       */
      c *= (1-a);
      c = csLerp (csColor (1, 1, 1), c, a);
      *dstPtr++ = c;
      srcPtr++;
    }
  }
} // namespace lighter
