/*

  Image.h - Generic 24 bit image class

*/

#ifndef IMAGE_H_
#define IMAGE_H_

#include "common.h"

CS_PLUGIN_NAMESPACE_BEGIN(DDSImageIO)
{
namespace ImageLib 
{

class Image8;
class Image32;

inline long FtoL(float f)
{
/*
long R;

  __asm {
    fld [f]
    fistp [R]
  }
  return R;
*/
  return (long)f;
}

typedef enum
{
  Type_8Bit,
  Type_32Bit
} ImageType;

typedef enum
{
  AT_None,      // No alpha used - All 0xff
  AT_Binary,      // 0x00 / 0xff
  AT_Constant,    // Single, constant non-0xff value used 
  AT_ConstantBinary,  // 0x00 / 0x??
  AT_DualConstant,  // 0x?? / 0x?? - Two constants
  AT_Modulated    // Multiple values used
} AlphaType;

typedef enum
{
  QM_Lloyd = 1,
  QM_MedianCut = 2
} QuantMethodType;


class Color
{
public:
  Color() {;}
  Color(DWORD c) : Col(c) {;}
  Color(BYTE R, BYTE G, BYTE B, BYTE A)
  { c.a = A; c.r = R; c.g = G; c.b = B; }

  union {
    struct { BYTE a, r, g, b; } c;
    DWORD  Col;
  };
};

// ----------------------------------------------------------------------------
// Base image class
// ----------------------------------------------------------------------------
class Image
{
private:
  long  XSize, YSize;

public:
  virtual ~Image() {;}
  virtual ImageType GetType(void) = 0;

  virtual void SetSize(long x, long y) = 0;
  virtual bool Crop(long x1, long y1, long x2, long y2) = 0;
  virtual bool SizeCanvas(long NewX, long NewY) = 0; // Resize the "canvas" without sizing the image

  long  GetXSize(void) {return XSize;}
  long  GetYSize(void) {return YSize;}

  double  Diff(Image *pComp);
  double  MSE(Image *pComp);

  // Returns how the alpha channel is used
  AlphaType  AlphaUsage(unsigned char *pAlpha1 = 0, unsigned char *pAlpha0 = 0);
  // Force alpha to be 0x00 / 0xff
  void    AlphaToBinary(unsigned char Threshold = 128);

  static  QuantMethodType QuantMethod;
  static  bool QuantDiffusion;

  friend class Image8;
  friend class Image32;
};


// ----------------------------------------------------------------------------
// 8 Bit palettized image class
// ----------------------------------------------------------------------------
class Image8 : public Image
{
private:
  Color  *pPalette;
  long  NumCols;

  BYTE  *pPixels;

public:
  Image8();
  ~Image8();
  ImageType GetType(void) {return Type_8Bit;}

  void ReleaseAll(void);

  void  SetSize(long x, long y);
  BYTE  *GetPixels(void) {return pPixels;}

  void  SetNumColors(long Cols);
  Color  *GetPalette(void) {return pPalette;}
  long  GetNumColors(void) {return NumCols;}
  bool  Crop(long x1, long y1, long x2, long y2);
  bool  SizeCanvas(long NewX, long NewY);

  void  QuantizeFrom(Image32 *pSrcImage, Image32 *pPaletteImage = 0,
  	Color *pForceColor = 0);
  void  QuantizeFrom(Image32 *pSrcImage, long NumCols);
  Image8  &operator=(Image &Src);
  Image8  &operator=(Image8 &Src) {return this->operator=(*(Image *)&Src);}
  Image8  &operator=(Image32 &Src) {return this->operator=(*(Image *)&Src);}

  friend class Image32;
};


// ----------------------------------------------------------------------------
// 32 Bit true color image class
// ----------------------------------------------------------------------------
class Image32 : public Image
{
private:
  Color  *pPixels;

public:
  Image32();
  ~Image32();
  ImageType GetType(void) {return Type_32Bit;}

  void ReleaseAll(void);

  void  SetSize(long x, long y);
  Color  *GetPixels(void) {return pPixels;}

  Image32  &operator=(Image &Src);
  Image32  &operator=(Image8 &Src) {return this->operator=(*(Image *)&Src);}
  Image32  &operator=(Image32 &Src) {return this->operator=(*(Image *)&Src);}

  // Unique color count for the image
  long    UniqueColors(void);

  // Compute the average slope between pixel neighbors
  float  AverageSlope(void);

  // # of bits per gun
  void  DiffuseError(long aBits, long rBits, long gBits, long bBits);
  void  DiffuseQuant(Image8 &DestImg);  // DestImg already contains the palette

  bool  Crop(long x1, long y1, long x2, long y2);
  bool  SizeCanvas(long NewX, long NewY);
  bool  Quarter(Image32 &Dest);      // Generate a quarter size image in Dest
  bool  HalfX(Image32 &Dest);
  bool  HalfY(Image32 &Dest);

  void  ResizeX(Image32 &Dest, long NewX);
  void  ScaleUpX(Image32 &Dest, long NewX);
  void  ScaleDownX(Image32 &Dest, long NewX);

  void  ResizeY(Image32 &Dest, long NewY);
  void  ScaleUpY(Image32 &Dest, long NewY);
  void  ScaleDownY(Image32 &Dest, long NewY);

  friend class Image8;
};

} // end of namespace ImageLib
}
CS_PLUGIN_NAMESPACE_END(DDSImageIO)

#endif
