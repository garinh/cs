/*

	ImageDXTC.cpp

*/

#include "ImageDXTC.h"
#include "CodeBook.h"
#include "fCodeBook.h"
#include "DXTCGen.h"

CS_PLUGIN_NAMESPACE_BEGIN(DDSImageIO)
{
namespace ImageLib
{


const unsigned long Mask1565 = 0xf8fcf880;
const unsigned long Mask0565 = 0xf8fcf800;
const unsigned long MaskAlpha = 0x000000ff;

ImageDXTC::ImageDXTC()
{
	XSize = YSize = 0;
	pBlocks = 0;
	Method = DC_None;
}

ImageDXTC::~ImageDXTC()
{
	ReleaseAll();
}

void ImageDXTC::ReleaseAll(void)
{
	if(pBlocks) delete [] pBlocks;
	pBlocks = 0;
	XSize = YSize = 0;
	Method = DC_None;
}


// ----------------------------------------------------------------------------
// Used internally by CompressDXT1(), and CompressDXT3()
// Sets the internal compression method field
// ----------------------------------------------------------------------------
void ImageDXTC::SetMethod(DXTCMethod NewMethod)
{
	Method = NewMethod;
}

// ----------------------------------------------------------------------------
// Used internally by CompressDXT1(), and CompressDXT3()
// Allocates the memory required for the block data
// ----------------------------------------------------------------------------
void ImageDXTC::SetSize(long x, long y)
{
	if(pBlocks) delete [] pBlocks;
	pBlocks = 0;
	XSize = x;
	YSize = y;

	size_t pixNum = ((XSize + 3) / 4) * ((YSize + 3) / 4) * 16;

	switch(Method)
	{
	case DC_DXT1:
		pBlocks = new WORD[pixNum / 2];
		break;

	case DC_DXT3:
	case DC_DXT5:
		pBlocks = new WORD[pixNum];
		break;

	default:
		XSize = YSize = 0;
		break;
	}
}

// ----------------------------------------------------------------------------
// Convert an image
// ----------------------------------------------------------------------------
void ImageDXTC::FromImage32(Image32 *pSrc, DXTCMethod Mth)
{
AlphaType Alpha;

	switch(Mth)
	{
	case DC_None:
		// Classify and convert to DXT1, or DXT3, DXT5 accordingly
		Alpha = pSrc->AlphaUsage(&AlphaValue);

		switch(Alpha)
		{
		case AT_None:
		case AT_Binary:
		case AT_Constant:
		case AT_ConstantBinary:
			CompressDXT1(pSrc);
			break;

		case AT_DualConstant:
		case AT_Modulated:
			CompressDXT3(pSrc);
			break;
		}
		break;

	case DC_DXT1:
		CompressDXT1(pSrc);
		break;

	case DC_DXT3:
		CompressDXT3(pSrc);
		break;

	case DC_DXT5:
		CompressDXT5(pSrc);
		break;
	}
}

static Color Col565To32(WORD Col)
{
Color c;

	c.c.a = 0xff;
	c.c.r = (BYTE)( (long)(Col >> 11) * 255 / 31 );
	c.c.g = (BYTE)( (long)((Col >> 5) & 0x3f) * 255 / 63 );
	c.c.b = (BYTE)( (long)(Col & 0x1f) * 255 / 31 );
	return c;
}


static void PlotDXT1(WORD *pSrc, Color *pDest, long Pitch)
{
Color	Col[4];
long r, g, b, xx, yy;

	if(pSrc[0] > pSrc[1])
	{
		Col[0] = Col565To32(pSrc[0]);
		Col[1] = Col565To32(pSrc[1]);
		pSrc += 2;

		r = (Col[0].c.r * 2 + Col[1].c.r) / 3;
		g = (Col[0].c.g * 2 + Col[1].c.g) / 3;
		b = (Col[0].c.b * 2 + Col[1].c.b) / 3;
		Col[2].c.a = 0xff;
		Col[2].c.r = (BYTE)r;
		Col[2].c.g = (BYTE)g;
		Col[2].c.b = (BYTE)b;

		r = (Col[1].c.r * 2 + Col[0].c.r) / 3;
		g = (Col[1].c.g * 2 + Col[0].c.g) / 3;
		b = (Col[1].c.b * 2 + Col[0].c.b) / 3;
		Col[3].c.a = 0xff;
		Col[3].c.r = (BYTE)r;
		Col[3].c.g = (BYTE)g;
		Col[3].c.b = (BYTE)b;

		int Shift = 0;
		for(yy=0; yy<4; yy++)
		{
			for(xx=0; xx<4; xx++, Shift+=2)
				pDest[xx] = Col[ (pSrc[0] >> Shift) & 3 ];

			pSrc += (yy&1);
			Shift &= 0x0f;
			pDest += Pitch;
		}
	}
	else
	{
		Col[0] = Col565To32(pSrc[0]);
		Col[1] = Col565To32(pSrc[1]);
		pSrc += 2;

		r = (Col[0].c.r + Col[1].c.r) / 2;
		g = (Col[0].c.g + Col[1].c.g) / 2;
		b = (Col[0].c.b + Col[1].c.b) / 2;
		Col[2].c.a = 0xff;
		Col[2].c.r = (BYTE)r;
		Col[2].c.g = (BYTE)g;
		Col[2].c.b = (BYTE)b;
		Col[3].Col = 0;

		int Shift = 0;
		for(yy=0; yy<4; yy++)
		{
			for(xx=0; xx<4; xx++, Shift+=2)
				pDest[xx] = Col[ (pSrc[0] >> Shift) & 3 ];

			pSrc += (yy&1);
			Shift &= 0x0f;
			pDest += Pitch;
		}
	}
}

static void PlotDXT3Alpha(WORD *pSrc, Color *pDest, long Pitch)
{
long xx, yy;

	for(yy=0; yy<4; yy++)
	{
		for(xx=0; xx<4; xx++)
			pDest[xx].c.a = ((pSrc[yy] >> (xx * 4)) & 0x0f) << 4;
		pDest += Pitch;
	}
}

void ImageDXTC::DXT1to32(Image32 *pDest)
{
Color	*pPix;
WORD	*pSrc;
long	x, y;

	pDest->SetSize(XSize, YSize);

	pSrc = pBlocks;
	pPix = pDest->GetPixels();
	for(y=0; y<YSize; y+=4)
	{
		for(x=0; x<XSize; x+=4)
		{
			PlotDXT1(pSrc, pPix+x, XSize);
			pSrc += 4;
		}

		pPix += XSize*4;
	}
}

void ImageDXTC::DXT3to32(Image32 *pDest)
{
Color	*pPix;
WORD	*pSrc;
long	x, y;

	pDest->SetSize(XSize, YSize);

	pSrc = pBlocks;
	pPix = pDest->GetPixels();
	for(y=0; y<YSize; y+=4)
	{
		for(x=0; x<XSize; x+=4)
		{
			PlotDXT1(pSrc+4, pPix+x, XSize);
			PlotDXT3Alpha(pSrc, pPix+x, XSize);
			pSrc += 8;
		}

		pPix += XSize*4;
	}
}

void ImageDXTC::ToImage32(Image32 *pDest)
{
	switch(Method)
	{
	case DC_None:
		CS_ASSERT(false);
		break;
	case DC_DXT1:
		DXT1to32(pDest);
		break;

	case DC_DXT3:
		DXT3to32(pDest);
		break;
	/* DXT5 is not supported for decoding. */
	default:
		break;
	}
}


inline WORD Make565(Color Col)
{
	return ((WORD)(Col.c.r >> 3) << 11) | ((WORD)(Col.c.g >> 2) << 5) | ((WORD)(Col.c.b >> 3));
}

// Lookups for which vectors in the DXTn block format map to which vectors in
// the codebook generated by DXTCGen.  I could really just make DXTCGen output
// The codes in the right order, but...
const WORD ColorBits4[4] = {0, 2, 3, 1};
const WORD ColorBits3[3] = {0, 2, 1};


// ----------------------------------------------------------------------------
// Build a DXT1 image from an Image32
// ----------------------------------------------------------------------------
void ImageDXTC::CompressDXT1(Image32 *pSrcImg)
{
long		x, y;
WORD		*pDest;
Color		*pSrc;
CodeBook	cb, cb2, dcb, dcb2;
Color		*pSrcPix, C, C2;
long		xx, yy, AlphaCount;
fCodebook	fcbSrc, fcb;
DXTCGen		GQuant;
Color*		allocSource = 0;

	SetMethod(DC_DXT1);
	SetSize(pSrcImg->GetXSize(), pSrcImg->GetYSize());

	cb.SetCount(16);
	cb2.SetSize(16);
	dcb.SetCount(4);
	dcb2.SetCount(4);

	pSrc = pSrcImg->GetPixels();
	if ((XSize < 4) || (YSize < 4))
	{
	  allocSource = new Color[4 * 4];
	  memset (allocSource, 0, 4 * 4 * sizeof(Color));
	  for(y=0; y<YSize; y++)
	  {
	    memcpy (allocSource + (y * 4), pSrc + (y * XSize), 
	      sizeof(Color) * XSize);
	  }
	}
	pDest = pBlocks;
	for(y=0; y<YSize; y+=4)
	{
		for(x=0; x<XSize; x+=4)
		{
			// Compute a unique color list for the block
			cb2.SetCount(0);
			AlphaCount = 0;
			pSrcPix = pSrc;
			const long maxYY = MIN(YSize-y, 4);
			for(yy=0; yy<maxYY; yy++)
			{
				const long maxXX = MIN(XSize-x, 4);
				for(xx=0; xx<maxXX; xx++)
				{
					C.Col = pSrcPix[xx].Col & Mask1565;
					if(C.c.a == 0x00)
						AlphaCount++;
					else
					{
						C.c.a = 0x00;
						cb[yy*4 + xx] = *(cbVector *)&C;
						cb2.AddVector( *(cbVector *)&C );
					}
				}
				pSrcPix += XSize;
			}

			if(AlphaCount)
			{
				switch(cb2.NumCodes())
				{
				case 0:
					EmitTransparentBlock(pDest);
					break;

				case 1:
					C.Col = *(long *)&cb2[0];
					Emit1ColorBlockTrans(pDest, C, pSrc);
					break;

				case 2:
					C = *((Color *)&cb2[0]);
					C2 = *((Color *)&cb2[1]);
					Emit2ColorBlockTrans(pDest, C, C2, pSrc);
					break;

				default:
					GQuant.Execute3(cb2, cb, dcb);
					EmitMultiColorBlockTrans(pDest, dcb, pSrc);
					break;
				}
			}
			else
			{
				switch(cb2.NumCodes())
				{
				case 1:
					C.Col = *(long *)&cb2[0];
					Emit1ColorBlock(pDest, C);
					break;

				case 2:
					C = *((Color *)&cb2[0]);
					C2 = *((Color *)&cb2[1]);
					Emit2ColorBlock(pDest, C, C2, pSrc);
					break;

				default:
					{
						long e3 = GQuant.Execute3(cb2, cb, dcb);
						long e4 = GQuant.Execute4(cb2, cb, dcb2);

						if(e3 < e4)
							EmitMultiColorBlockTrans(pDest, dcb, pSrc);
						else
							EmitMultiColorBlock4(pDest, dcb2, pSrc);
					}
					break;
				}
			}

			pDest += 4;
			pSrc += 4;
		}
		pSrc += XSize*3;
	}
	delete[] allocSource;
}

// ----------------------------------------------------------------------------
// Build a DXT3 image from an Image32
// ----------------------------------------------------------------------------
void ImageDXTC::CompressDXT3(Image32 *pSrcImg)
{
long		x, y;
WORD		*pDest;
Color		*pSrc;
CodeBook	cb, cb2, dcb, dcb2;
Color		*pSrcPix, C, C2;
long		xx, yy;
fCodebook	fcbSrc, fcb;
DXTCGen		GQuant;
Color*		allocSource = 0;

	SetMethod(DC_DXT3);
	SetSize(pSrcImg->GetXSize(), pSrcImg->GetYSize());

	cb.SetCount(16);
	cb2.SetSize(16);
	dcb.SetCount(4);
	dcb2.SetCount(4);

	pSrc = pSrcImg->GetPixels();
	if ((XSize < 4) || (YSize < 4))
	{
	  allocSource = new Color[4 * 4];
	  memset (allocSource, 0, 4 * 4 * sizeof(Color));
	  for(y=0; y<YSize; y++)
	  {
	    memcpy (allocSource + (y * 4), pSrc + (y * XSize), 
	      sizeof(Color) * XSize);
	  }
	}
	pDest = pBlocks;
	for(y=0; y<YSize; y+=4)
	{
		for(x=0; x<XSize; x+=4)
		{
			// Compute a unique color list for the block
			cb2.SetCount(0);
			pSrcPix = pSrc;
			const long maxYY = MIN(YSize-y, 4);
			for(yy=0; yy<maxYY; yy++)
			{
				const long maxXX = MIN(XSize-x, 4);
				for(xx=0; xx<maxXX; xx++)
				{
					C.Col = pSrcPix[xx].Col & Mask0565;
					cb[yy*4 + xx] = *(cbVector *)&C;
					cb2.AddVector( *(cbVector *)&C );
				}
				pSrcPix += XSize;
			}

			Emit4BitAlphaBlock(pDest, pSrc);
			pDest += 4;

			switch(cb2.NumCodes())
			{
			case 1:
				C.Col = *(long *)&cb2[0];
				Emit1ColorBlock(pDest, C);
				break;

			case 2:
				C = *((Color *)&cb2[0]);
				C2 = *((Color *)&cb2[1]);
				Emit2ColorBlock(pDest, C, C2, pSrc);
				break;

			default:
				{
					long e3 = GQuant.Execute3(cb2, cb, dcb);
					long e4 = GQuant.Execute4(cb2, cb, dcb2);

					if(e3 < e4)
						EmitMultiColorBlock3(pDest, dcb, pSrc);
					else
						EmitMultiColorBlock4(pDest, dcb2, pSrc);
				}
				break;
			}

			pDest += 4;
			pSrc += 4;
		}
		pSrc += XSize*3;
	}
	delete[] allocSource;
}


// ----------------------------------------------------------------------------
// Build a DXT5 image from an Image32
// ----------------------------------------------------------------------------
void ImageDXTC::CompressDXT5(Image32 *pSrcImg)
{
long		x, y;
WORD		*pDest;
Color		*pSrc;
CodeBook	cb, cb2, dcb, dcb2, cbA, cbA2, dcbA, dcbA2;
Color		*pSrcPix, C, C2;
long		xx, yy;
fCodebook	fcbSrc, fcb;
DXTCGen		GQuant, GQuantA;
Color*		allocSource = 0;

	SetMethod(DC_DXT5);
	SetSize(pSrcImg->GetXSize(), pSrcImg->GetYSize());

	cb.SetCount(16);
	cb2.SetSize(16);
	dcb.SetCount(4);
	dcb2.SetCount(4);
	cbA.SetCount(16);
	cbA2.SetSize(16);
	dcbA.SetCount(8);
	dcbA2.SetCount(8);

	pSrc = pSrcImg->GetPixels();
	if ((XSize < 4) || (YSize < 4))
	{
	  allocSource = new Color[4 * 4];
	  memset (allocSource, 0, 4 * 4 * sizeof(Color));
	  for(y=0; y<YSize; y++)
	  {
	    memcpy (allocSource + (y * 4), pSrc + (y * XSize), 
	      sizeof(Color) * XSize);
	  }
	}
	pDest = pBlocks;
	for(y=0; y<YSize; y+=4)
	{
		for(x=0; x<XSize; x+=4)
		{
			// Compute a unique color list for the block
			cb2.SetCount(0);
			cbA2.SetCount(0);
			pSrcPix = pSrc;
			const long maxYY = MIN(YSize-y, 4);
			for(yy=0; yy<maxYY; yy++)
			{
				const long maxXX = MIN(XSize-x, 4);
				for(xx=0; xx<maxXX; xx++)
				{
					C.Col = pSrcPix[xx].Col & Mask0565;
					cb[yy*4 + xx] = *(cbVector *)&C;
					cb2.AddVector( *(cbVector *)&C );

					C.Col = pSrcPix[xx].Col & MaskAlpha;
					cbA[yy*4 + xx] = *(cbVector *)&C;
					cbA2.AddVector( *(cbVector *)&C );
				}
				pSrcPix += XSize;
			}

			switch (cbA2.NumCodes())
			{
			  case 1:
			    C.Col = *(long *)&cbA2[0];
			    Emit1AlphaBlock (pDest, C);
			    break;
			  case 2:
			    C = *((Color *)&cbA2[0]);
			    C2 = *((Color *)&cbA2[1]);
			    Emit2AlphaBlock (pDest, C, C2, pSrc);
			    break;
			  default:
			    {
			      long e8 = GQuant.Execute8(cbA2, cbA, dcbA);
			      long e6 = GQuant.Execute6(cbA2, cbA, dcbA2);
			      if (e8 < e6)
				EmitMultiAlphaBlock8(pDest, dcbA, pSrc);
			      else
				EmitMultiAlphaBlock6(pDest, dcbA2, pSrc);
			    }
			    break;
			}
			pDest += 4;

			switch(cb2.NumCodes())
			{
			case 1:
				C.Col = *(long *)&cb2[0];
				Emit1ColorBlock(pDest, C);
				break;

			case 2:
				C = *((Color *)&cb2[0]);
				C2 = *((Color *)&cb2[1]);
				Emit2ColorBlock(pDest, C, C2, pSrc);
				break;

			default:
				{
					long e3 = GQuant.Execute3(cb2, cb, dcb);
					long e4 = GQuant.Execute4(cb2, cb, dcb2);

					if(e3 < e4)
						EmitMultiColorBlock3(pDest, dcb, pSrc);
					else
						EmitMultiColorBlock4(pDest, dcb2, pSrc);
				}
				break;
			}

			pDest += 4;
			pSrc += 4;
		}
		pSrc += XSize*3;
	}
	delete[] allocSource;
}

// ----------------------------------------------------------------------------
// Block emission routines follow - These routines take a block of input colors,
// map them to the desired output colors, and emit the right bit patterns
// ----------------------------------------------------------------------------
void ImageDXTC::EmitTransparentBlock(WORD *pDest)
{
	*(DWORD *)(pDest+0) = 0;
	*(DWORD *)(pDest+2) = 0xffffffff;
}

void ImageDXTC::Emit1ColorBlock(WORD *pDest, Color c)
{
	pDest[0] = Make565(c);
	pDest[1] = 0;
	pDest[2] = 0;
	pDest[3] = 0;
}

void ImageDXTC::Emit1ColorBlockTrans(WORD *pDest, Color c, Color *pSrc)
{
long x, y, Shift;
WORD Index;

	pDest[0] = 0;
	pDest[1] = Make565(c);
	pDest[2] = 0;
	pDest[3] = 0;
	pDest += 2;

	for(y=0; y<4; y++)
	{
		Shift = (y&1) * 8;
		for(x=0; x<4; x++)
		{
			if((pSrc[x].c.a & 0x80) == 0)
				Index = 3;
			else
				Index = 1;

			pDest[0] |= Index << Shift;
			Shift += 2;
		}
		pSrc += XSize;
		pDest += (y&1);
	}
}

void ImageDXTC::Emit2ColorBlock(WORD *pDest, Color c1, Color c2, Color *pSrc)
{
long x, y, Shift;
WORD Index;

	pDest[0] = Make565(c1);
	pDest[1] = Make565(c2);
	pDest[2] = 0;
	pDest[3] = 0;
	pDest += 2;

	for(y=0; y<4; y++)
	{
		Shift = (y&1) * 8;
		for(x=0; x<4; x++)
		{
			if((pSrc[x].Col & Mask0565) == c1.Col)
				Index = 0;
			else
				Index = 1;

			pDest[0] |= Index << Shift;
			Shift += 2;
		}
		pSrc += XSize;
		pDest += (y&1);
	}
}

void ImageDXTC::Emit2ColorBlockTrans(WORD *pDest, Color c1, Color c2, Color *pSrc)
{
long x, y, Shift;
WORD Index, Col1, Col2;

	Col1 = Make565(c1);
	Col2 = Make565(c2);
	if(Col1 > Col2)
	{
		pDest[0] = Col2;
		pDest[1] = Col1;
		c1 = c2;			// Force the color test below to "flip"
	}
	else
	{
		pDest[0] = Col1;
		pDest[1] = Col2;
	}
	pDest[2] = 0;
	pDest[3] = 0;
	pDest += 2;

	for(y=0; y<4; y++)
	{
		Shift = (y&1) * 8;
		for(x=0; x<4; x++)
		{
			if((pSrc[x].c.a & 0x80) == 0)
				Index = 3;
			else
			{
				if((pSrc[x].Col & Mask0565) == c1.Col)
					Index = 0;
				else
					Index = 1;
			}

			pDest[0] |= Index << Shift;
			Shift += 2;
		}
		pSrc += XSize;
		pDest += (y&1);
	}
}

void ImageDXTC::EmitMultiColorBlock4(WORD *pDest, CodeBook &cb, Color *pSrc)
{
long x, y, Shift;
WORD Index, Col1, Col2;
Color C, C2;

	C = *((Color *)&cb[0]);
	C2 = *((Color *)&cb[3]);

	Col1 = Make565(C);
	Col2 = Make565(C2);

	if(Col1 > Col2)
	{
		pDest[0] = Col1;
		pDest[1] = Col2;
	}
	else if(Col1 < Col2)
	{
		*((Color *)&cb[0]) = C2;
		*((Color *)&cb[3]) = C;
		C = *((Color *)&cb[1]);		// Shuffle the color table order around
		C2 = *((Color *)&cb[2]);
		*((Color *)&cb[1]) = C2;
		*((Color *)&cb[2]) = C;

		pDest[0] = Col2;
		pDest[1] = Col1;
	}
	else
	{
		// Both colors are equal - Emit the block and return
		pDest[0] = Col1;
		pDest[1] = 0;
		pDest[2] = pDest[3] = 0;
		return;
	}

	pDest[2] = 0;
	pDest[3] = 0;
	pDest += 2;

	for(y=0; y<4; y++)
	{
		Shift = (y&1) * 8;
		for(x=0; x<4; x++)
		{
			Index = ColorBits4[ cb.FindVectorSlow( *((cbVector *)(pSrc+x)) ) ];

			pDest[0] |= Index << Shift;
			Shift += 2;
		}
		pSrc += XSize;
		pDest += (y&1);
	}
}

void ImageDXTC::EmitMultiColorBlock3(WORD *pDest, CodeBook &cb, Color *pSrc)
{
long x, y, Shift;
WORD Index, Col1, Col2;
Color C, C2;

	C = *((Color *)&cb[0]);
	C2 = *((Color *)&cb[2]);

	Col1 = Make565(C);
	Col2 = Make565(C2);

	if(Col1 > Col2)
	{
		*((Color *)&cb[0]) = C2;
		*((Color *)&cb[2]) = C;		// Shuffle the color table order around
		pDest[0] = Col2;
		pDest[1] = Col1;
	}
	else
	{
		pDest[0] = Col1;
		pDest[1] = Col2;
	}

	pDest[2] = 0;
	pDest[3] = 0;
	pDest += 2;

	for(y=0; y<4; y++)
	{
		Shift = (y&1) * 8;
		for(x=0; x<4; x++)
		{
			C = pSrc[x];
			C.c.a = 0;
			Index = ColorBits3[ cb.FindVectorSlow( *((cbVector *)(&C)) ) ];

			pDest[0] |= Index << Shift;
			Shift += 2;
		}
		pSrc += XSize;
		pDest += (y&1);
	}
}


void ImageDXTC::EmitMultiColorBlockTrans(WORD *pDest, CodeBook &cb, Color *pSrc)
{
long x, y, Shift;
WORD Index, Col1, Col2;
Color C, C2;

	C = *((Color *)&cb[0]);
	C2 = *((Color *)&cb[2]);

	Col1 = Make565(C);
	Col2 = Make565(C2);

	if(Col1 > Col2)
	{
		*((Color *)&cb[0]) = C2;
		*((Color *)&cb[2]) = C;		// Shuffle the color table order around
		pDest[0] = Col2;
		pDest[1] = Col1;
	}
	else
	{
		pDest[0] = Col1;
		pDest[1] = Col2;
	}

	pDest[2] = 0;
	pDest[3] = 0;
	pDest += 2;

	for(y=0; y<4; y++)
	{
		Shift = (y&1) * 8;
		for(x=0; x<4; x++)
		{
			if(pSrc[x].c.a == 0)
				Index = 3;
			else
				Index = ColorBits3[ cb.FindVectorSlow( *((cbVector *)(pSrc+x)) ) ];

			pDest[0] |= Index << Shift;
			Shift += 2;
		}
		pSrc += XSize;
		pDest += (y&1);
	}
}

void ImageDXTC::Emit4BitAlphaBlock(WORD *pDest, Color *pSrc)
{
long x, y, Shift;
WORD Alpha;

	for(y=0; y<4; y++)
	{
		Alpha = 0;
		for(x=0; x<4; x++)
		{
			Shift = x*4;
			Alpha |= (pSrc[x].c.a >> 4) << Shift;
		}
		pDest[y] = Alpha;
		pSrc += XSize;
	}
}

void ImageDXTC::Emit1AlphaBlock (WORD *pDest, Color c)
{
  pDest[0] = c.c.a * 0x101;
  pDest[1] = 0;
  pDest[2] = 0;
  pDest[3] = 0;
}

void ImageDXTC::Emit2AlphaBlock (WORD *pDest, Color c, Color c2, Color *pSrc)
{
  uint v1, v2;
  if (c.c.a > c2.c.a)
  {
    pDest[0] = c.c.a | (c2.c.a << 8);
    v1 = 0x0; v2 = 0x1;
  }
  else
  {
    pDest[0] = c2.c.a | (c.c.a << 8);
    v1 = 0x1; v2 = 0x0;
  }
  uint alpha[2];
  alpha[0] = alpha[1] = 0;
  for (int i = 0; i < 2; i++)
  {
    int shift = 0;
    for (int y = 0; y < 2; y++)
    {
      for (int x = 0; x < 4; x++)
      {
	if (pSrc[x].c.a == c.c.a)
	  alpha[i] |= v1 << shift;
	else
	  alpha[i] |= v2 << shift;
	shift += 3;
      }
      pSrc += XSize;
    }
  }
  pDest[1] = alpha[0] & 0xffff;
  pDest[2] = (alpha[0] >> 16) | ((alpha[1] & 0xff) << 8);
  pDest[3] = alpha[1] >> 8;
}

void ImageDXTC::EmitMultiAlphaBlock8 (WORD *pDest, CodeBook &cb, Color *pSrc)
{
  Color C, C2;

  C = *((Color *)&cb[0]);
  C2 = *((Color *)&cb[1]);

  if(C.c.a > C2.c.a)
  {
    pDest[0] = C.c.a | (C2.c.a << 8);
  }
  else if (C.c.a < C2.c.a)
  {
    *((Color *)&cb[0]) = C2;
    *((Color *)&cb[1]) = C;
    C = *((Color *)&cb[2]);		// Shuffle the color table order around
    C2 = *((Color *)&cb[7]);
    *((Color *)&cb[2]) = C2;
    *((Color *)&cb[7]) = C;
    C = *((Color *)&cb[3]);		// Shuffle the color table order around
    C2 = *((Color *)&cb[6]);
    *((Color *)&cb[3]) = C2;
    *((Color *)&cb[6]) = C;
    C = *((Color *)&cb[4]);		// Shuffle the color table order around
    C2 = *((Color *)&cb[5]);
    *((Color *)&cb[4]) = C2;
    *((Color *)&cb[5]) = C;

    C = *((Color *)&cb[0]);
    C2 = *((Color *)&cb[1]);

    pDest[0] = C.c.a | (C2.c.a << 8);
  }
  else
  {
    // Both colors are equal - Emit the block and return
    pDest[0] = C.c.a * 0x101;
    pDest[1] = pDest[2] = pDest[3] = 0;
    return;
  }

  pDest[1] = 0;
  pDest[2] = 0;
  pDest[3] = 0;

  uint alpha[2];
  alpha[0] = alpha[1] = 0;
  for (int i = 0; i < 2; i++)
  {
    int shift = 0;
    for (int y = 0; y < 2; y++)
    {
      for (int x = 0; x < 4; x++)
      {
	long Index = cb.FindVectorSlow( *((cbVector *)(pSrc+x)) );
	alpha[i] |= Index << shift;
	shift += 3;
      }
      pSrc += XSize;
    }
  }
  pDest[1] = alpha[0] & 0xffff;
  pDest[2] = (alpha[0] >> 16) | ((alpha[1] & 0xff) << 8);
  pDest[3] = alpha[1] >> 8;
}

void ImageDXTC::EmitMultiAlphaBlock6 (WORD *pDest, CodeBook &cb, Color *pSrc)
{
  Color C, C2;

  C = *((Color *)&cb[0]);
  C2 = *((Color *)&cb[1]);

  if(C.c.a < C2.c.a)
  {
    pDest[0] = C.c.a | (C2.c.a << 8);
  }
  else if (C.c.a > C2.c.a)
  {
    *((Color *)&cb[0]) = C2;
    *((Color *)&cb[1]) = C;
    C = *((Color *)&cb[2]);		// Shuffle the color table order around
    C2 = *((Color *)&cb[5]);
    *((Color *)&cb[2]) = C2;
    *((Color *)&cb[5]) = C;
    C = *((Color *)&cb[3]);		// Shuffle the color table order around
    C2 = *((Color *)&cb[4]);
    *((Color *)&cb[3]) = C2;
    *((Color *)&cb[4]) = C;

    C = *((Color *)&cb[0]);
    C2 = *((Color *)&cb[1]);

    pDest[0] = C.c.a | (C2.c.a << 8);
  }
  else
  {
    // Both colors are equal - Emit the block and return
    pDest[0] = C.c.a * 0x101;
    pDest[1] = pDest[2] = pDest[3] = 0;
    return;
  }

  pDest[1] = 0;
  pDest[2] = 0;
  pDest[3] = 0;

  uint alpha[2];
  alpha[0] = alpha[1] = 0;
  for (int i = 0; i < 2; i++)
  {
    int shift = 0;
    for (int y = 0; y < 2; y++)
    {
      for (int x = 0; x < 4; x++)
      {
	long Index = cb.FindVectorSlow( *((cbVector *)(pSrc+x)) );
	alpha[i] |= Index << shift;
	shift += 3;
      }
      pSrc += XSize;
    }
  }
  pDest[1] = alpha[0] & 0xffff;
  pDest[2] = (alpha[0] >> 16) | ((alpha[1] & 0xff) << 8);
  pDest[3] = alpha[1] >> 8;
}

} // end of namespace ImageLib
}
CS_PLUGIN_NAMESPACE_END(DDSImageIO)
