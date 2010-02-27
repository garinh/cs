/*
  Copyright (C) 2005 by Christopher Nelson

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
#include "cstool/pen.h"
#include "ivideo/fontserv.h"
#include "ivideo/graph2d.h"

#define MESH_TYPE(a) ((flags & CS_PEN_FILL) ? a : pen_width>1 ? CS_MESHTYPE_QUADS : CS_MESHTYPE_LINESTRIP)


csPen::csPen (iGraphics2D *_g2d, iGraphics3D *_g3d) : g3d (_g3d), g2d(_g2d), pen_width(1.0), flags(0), gen_tex_coords(false)
{
  mesh.object2world.Identity();
  mesh.mixmode = CS_FX_ALPHA;
  tt.Set(0,0,0);
}

csPen::~csPen ()
{

}

void csPen::Start ()
{
  poly.MakeEmpty();
  poly_idx.MakeEmpty();
  colors.SetSize (0);  
  texcoords.SetSize (0);
  line_points.SetSize (0);  
  gen_tex_coords=false;
}

void csPen::AddVertex (float x, float y, bool force_add)
{
  if (force_add || (flags & CS_PEN_FILL) || !(pen_width>1))
  {
  	poly_idx.AddVertex((int)poly.AddVertex(x,y,0));
  	colors.Push(color);
  	
  	// Generate texture coordinates.
  	if (gen_tex_coords && (flags & CS_PEN_TEXTURE_ONLY))
  	{
		AddTexCoord(x / sh_w, y / sh_h); 	
  	}
  }
  else
  {
	point p = {x,y};			
	  
	if (line_points.GetSize ())
	{
		AddThickPoints(line_points.Top().x, line_points.Top().y, x, y);
	}	  
	
	line_points.Push(p);	
  }
}

void csPen::AddTexCoord(float x, float y)
{
	texcoords.Push(csVector2(x,y));
}

void csPen::SetupMesh ()
{
  mesh.vertices = poly.GetVertices ();
  mesh.vertexCount = (uint)poly.GetVertexCount ();

  mesh.indices = (uint *)poly_idx.GetVertexIndices ();
  mesh.indexCount = (uint)poly_idx.GetVertexCount ();

  mesh.colors = colors.GetArray ();
  mesh.texcoords = texcoords.GetArray();
  if (flags & CS_PEN_TEXTURE_ONLY)
  {
	mesh.texture=tex;
  }
  else
  {
	mesh.texture=0;	  
  }
  
  //mesh.alphaType = alphaSmooth;
  //mesh.mixmode = CS_FX_COPY | CS_FX_ALPHA; // CS_FX_FLAT  
}

void csPen::DrawMesh (csRenderMeshType mesh_type)
{
  mesh.meshtype = mesh_type;
  g3d->DrawSimpleMesh (mesh, csSimpleMeshScreenspace);
}

void csPen::SetMixMode(uint mode)
{
  mesh.mixmode = mode;	
}

void csPen::SetAutoTexture(float w, float h)
{
	sh_w = w;
	sh_h = h;
	gen_tex_coords=true;	
}

void csPen::AddThickPoints(float fx1, float fy1, float fx2, float fy2)
{		
	float angle = atan2(fy2-fy1, fx2-fx1);
	
	float a1 = angle - (PI/2.0);	
	float ca1 = cos(a1)*pen_width, sa1 = sin(a1)*pen_width;
	bool first = line_points.GetSize ()<2;
	
// 	AddVertex(fx1+ca1, fy1+sa1, true);		
// 	AddVertex(fx2+ca1, fy2+sa1, true);
// 		
// 	AddVertex(fx2-ca1, fy2-sa1, true);	
// 	AddVertex(fx1-ca1, fy1-sa1, true);
// 	
	
	if (first) AddVertex(fx1+ca1, fy1+sa1, true);
	else	   AddVertex(last[0].x, last[0].y, true);
		
	AddVertex(fx2+ca1, fy2+sa1, true);		
	AddVertex(fx2-ca1, fy2-sa1, true);	
	
	if (first) AddVertex(fx1-ca1, fy1-sa1, true);
	else	   AddVertex(last[1].x, last[1].y, true);	
	
	
	last[0].x=fx2+ca1; last[0].y=fy2+sa1;		
	last[1].x=fx2-ca1; last[1].y=fy2-sa1;
}

void csPen::SetFlag(uint flag)
{
	flags |= flag;	  
}	
  

void csPen::ClearFlag(uint flag)
{
	flags &= (~flag);	  
}

void csPen::SetColor (float r, float g, float b, float a)
{
  color.x=r;
  color.y=g;
  color.z=b;
  color.w=a;
}

void csPen::SetColor(const csColor4 &c)
{
  color.x=c.red;
  color.y=c.green;
  color.z=c.blue;
  color.w=c.alpha;
}

void csPen::SetTexture(csRef<iTextureHandle> _tex)
{
	tex=_tex;
}

void csPen::SwapColors()
{
  csVector4 tmp;

  tmp=color;
  color=alt_color;
  alt_color=tmp;
}

void csPen::SetPenWidth(float width)
{
	pen_width=width;	
}

void csPen::ClearTransform()
{
  mesh.object2world.Identity();
  tt = csVector3(0,0,0);
}

void csPen::PushTransform()
{
  transforms.Push(mesh.object2world);
  translations.Push(tt);
}

void csPen::PopTransform()
{
  ClearTransform();

  mesh.object2world*=transforms.Top();
  transforms.Pop();
  
  tt=translations.Top();
  translations.Pop();
}

void csPen::SetOrigin(const csVector3 &o)
{
  mesh.object2world.SetOrigin(o);
}

void 
csPen::Translate(const csVector3 &t)
{
  csTransform tr;

  tr.Translate(t);
  mesh.object2world*=tr;

  tt+=t;
}

void 
csPen::Rotate(const float &a)
{
  csZRotMatrix3 rm(a);
  csTransform tr(rm, csVector3(0));
  mesh.object2world*=tr;
}

void csPen::DrawThickLine(uint x1, uint y1, uint x2, uint y2)
{	
		
	Start();
	
	AddThickPoints(x1,y1,x2,y2);
	
	SetupMesh();
	DrawMesh(CS_MESHTYPE_QUADS);
}

/** Draws a single line. */
void csPen::DrawLine (uint x1, uint y1, uint x2, uint y2)
{	
  if (pen_width>1) { DrawThickLine(x1,y1,x2,y2); return; }	
	
  Start ();
  AddVertex (x1,y1);
  
  if (flags & CS_PEN_SWAPCOLORS) SwapColors();  
  
  AddVertex (x2,y2);

  SetupMesh ();
  DrawMesh (CS_MESHTYPE_LINES);
}

/** Draws a single point. */
void csPen::DrawPoint (uint x1, uint y1)
{
	
  Start ();
  AddVertex (x1,y1);
  
  SetupMesh (); 
  DrawMesh (CS_MESHTYPE_POINTS);
}

/** Draws a rectangle. */
void csPen::DrawRect (uint x1, uint y1, uint x2, uint y2)
{  	
  Start ();
  SetAutoTexture(x2-x1, y2-y1);
  
  AddVertex (x1, y1); 
  AddVertex (x2, y1); 

  if (flags & CS_PEN_SWAPCOLORS) SwapColors();

  AddVertex (x2, y2); 
  AddVertex (x1, y2); 

  if (flags & CS_PEN_SWAPCOLORS) SwapColors();

  if (!(flags & CS_PEN_FILL)) AddVertex (x1, y1);  

  SetupMesh ();
  DrawMesh (MESH_TYPE(CS_MESHTYPE_QUADS));
}

/** Draws a mitered rectangle. The miter value should be between 0.0 and 1.0, and determines how
* much of the corner is mitered off and beveled. */    
void csPen::DrawMiteredRect (uint x1, uint y1, uint x2, uint y2, 
                             uint miter)
{  	
  if (miter == 0) 
  { 
    DrawRect (x1,y1,x2,y2); 
    return; 
  }
   
			
  uint width = x2-x1;
  uint height = y2-y1;

  uint center_x = x1+(width>>1);
  uint center_y = y1+(height>>1);

  uint y_miter = miter;
  uint x_miter = miter;
  
  float ym_1 = y1 + y_miter;
  float ym_2 = y2 - y_miter;
  float xm_1 = x1 + x_miter;
  float xm_2 = x2 - x_miter;
  		
  Start ();
  SetAutoTexture(x2-x1, y2-y1);
    
  if (flags & CS_PEN_SWAPCOLORS) SwapColors(); 
  if (flags & CS_PEN_FILL)AddVertex(center_x, center_y);
   
  AddVertex (x1, ym_2); 

  if (flags & CS_PEN_SWAPCOLORS) SwapColors(); 
  
  AddVertex (x1, ym_1); 
  AddVertex (xm_1, y1);
  AddVertex (xm_2, y1);  
  AddVertex (x2, ym_1);

  if (flags & CS_PEN_SWAPCOLORS) SwapColors();

  AddVertex (x2, ym_2);
  AddVertex (xm_2, y2);
  AddVertex (xm_1, y2);
  AddVertex (x1, ym_2);
  
  SetupMesh ();
  DrawMesh (MESH_TYPE(CS_MESHTYPE_TRIANGLEFAN));  
}

/** Draws a rounded rectangle. The roundness value should be between 0.0 and 1.0, and determines how
  * much of the corner is rounded off. */
void csPen::DrawRoundedRect (uint x1, uint y1, uint x2, uint y2, 
                             uint roundness)
{		
  if (roundness == 0) 
  { 
    DrawRect (x1,y1,x2,y2); 
    return; 
  }
			
  float width = x2-x1;
  float height = y2-y1;
  
  float center_x = x1+(width/2);
  float center_y = y1+(height/2);


  float y_round = roundness;
  float x_round = roundness;  
  float delta = 0.0384f; 

  Start();
  SetAutoTexture(width, height);

  float angle;

  if ((flags & CS_PEN_FILL))AddVertex(center_x, center_y);
  			
  for(angle=(HALF_PI)*3.0f; angle>PI; angle-=delta)
  {
    AddVertex (x1+x_round+(cosf (angle)*x_round), y2-y_round-(sinf (angle)*y_round));
  }
	  
  AddVertex (x1, y2-y_round);
  AddVertex (x1, y1+y_round);
  
  for(angle=PI; angle>HALF_PI; angle-=delta)
  {
	  AddVertex (x1+x_round+(cosf (angle)*x_round), y1+y_round-(sinf (angle)*y_round));
  }
  
  AddVertex (x1+x_round, y1);
  AddVertex (x2-x_round, y1);

  if (flags & CS_PEN_SWAPCOLORS) SwapColors();
  
  for(angle=HALF_PI; angle>0; angle-=delta)
  {
    AddVertex (x2-x_round+(cosf (angle)*x_round), y1+y_round-(sinf (angle)*y_round));
  }
  
  AddVertex (x2, y1+y_round);
  AddVertex (x2, y2-y_round);
  
  for(angle=TWO_PI; angle>HALF_PI*3.0; angle-=delta)
  {
    AddVertex (x2-x_round+(cosf (angle)*x_round), y2-y_round-(sinf (angle)*y_round));
  }
  
  AddVertex (x2-x_round, y2);
  AddVertex (x1+x_round, y2);		

  if (flags & CS_PEN_SWAPCOLORS) SwapColors();

  SetupMesh ();
  DrawMesh (MESH_TYPE(CS_MESHTYPE_TRIANGLEFAN));
}

/** 
   * Draws an elliptical arc from start angle to end angle.  Angle must be specified in radians.
   * The arc will be made to fit in the given box.  If you want a circular arc, make sure the box is
   * a square.  If you want a full circle or ellipse, specify 0 as the start angle and 2*PI as the end
   * angle.
   */
void csPen::DrawArc(uint x1, uint y1, uint x2, uint y2, float start_angle, float end_angle)
{		
  // Check to make sure that the arc is not in a negative box.
  if (x2<x1) { x2^=x1; x1^=x2; x2^=x1; }
  if (y2<y1) { y2^=y1; y1^=y2; y2^=y1; }
  
  // If start angle and end_angle are too close, abort.
  if (fabs(end_angle-start_angle) < 0.0001) return;
	
  float width = x2-x1;
  float height = y2-y1;

  if (width==0 || height==0) return;

  float x_radius = width/2;
  float y_radius = height/2;
  
  float center_x = x1+(x_radius);
  float center_y = y1+(y_radius);
  
  // Set the delta to be two degrees.  
  float delta = 0.0384f; 
  float angle;
    
  Start();
  SetAutoTexture(width, height);
  
  if ((flags & CS_PEN_FILL)) AddVertex(center_x, center_y);

  for(angle=start_angle; angle<=end_angle; angle+=delta)
  {
    AddVertex(center_x+(cos(angle)*x_radius), center_y+(sin(angle)*y_radius));
  }

  SetupMesh ();
  DrawMesh (MESH_TYPE(CS_MESHTYPE_TRIANGLEFAN));
}

void csPen::DrawTriangle(uint x1, uint y1, uint x2, uint y2, uint x3, uint y3)
{	
  Start();

  AddVertex(x1, y1); AddTexCoord(0,0);
  AddVertex(x2, y2); AddTexCoord(0,1);
  AddVertex(x3, y3); AddTexCoord(1,1);
  
  if (!(flags & CS_PEN_FILL)) AddVertex(x1, y1);

  SetupMesh ();
  DrawMesh (MESH_TYPE(CS_MESHTYPE_TRIANGLES));
}

void 
csPen::Write(iFont *font, uint x1, uint y1, char *text)
{
  if (font==0) return;

  int the_color = g2d->FindRGB(static_cast<int>(color.x*255), 
		 	       static_cast<int>(color.y*255), 
			       static_cast<int>(color.z*255),
			       static_cast<int>(color.w*255));

  csVector3 pos(x1,y1,0);

  pos += tt;

  g2d->Write(font, (int)pos.x, (int)pos.y, the_color, -1, text);  
}

void 
csPen::WriteBoxed(iFont *font, uint x1, uint y1, uint x2, uint y2, uint h_align, uint v_align, char *text)
{
  if (font==0) return;

  uint x, y;
  int w, h;

  // Get the maximum dimensions of the text.
  font->GetDimensions(text, w, h);

  // Figure out the correct starting point in the box for horizontal alignment.
  switch(h_align)
  {
    case CS_PEN_TA_LEFT:
    default:
      x=x1;
    break;

    case CS_PEN_TA_RIGHT:
      x=x2-w;
    break;
  
    case CS_PEN_TA_CENTER:
      x=x1+((x2-x1)>>1) - (w>>1);
    break;
  }

  // Figure out the correct starting point in the box for vertical alignment.
  switch(v_align)
  {
    case CS_PEN_TA_TOP:
    default:
      y=y1;
    break;

    case CS_PEN_TA_BOT:
      y=y2-h;
    break;
  
    case CS_PEN_TA_CENTER:
      y=y1+((y2-y1)>>1) - (h>>1);
    break;
  }

  Write(font, x, y, text);
}
