/*
  Copyright (C) 2002 by Marten Svanfeldt
                        Anders Stenberg

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free
  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "cssysdef.h"

#include <ctype.h>

#include "csqint.h"

#include "igeom/clip2d.h"
#include "igraphic/imageio.h"
#include "iutil/eventq.h"
#include "iutil/plugin.h"
#include "iutil/vfs.h"
#include "ivaria/bugplug.h"
#include "ivaria/profile.h"
#include "ivideo/graph3d.h"
#include "ivideo/material.h"
#include "ivideo/rendermesh.h"

#include "csgeom/box.h"
#include "csgeom/projections.h"
#include "csgfx/imagememory.h"
#include "csgfx/renderbuffer.h"
#include "csgfx/vertexlistwalker.h"
#include "csplugincommon/opengl/assumedstate.h"
#include "csplugincommon/opengl/glhelper.h"
#include "csplugincommon/opengl/glstates.h"
#include "csplugincommon/render3d/normalizationcube.h"
#include "cstool/fogmath.h"
#include "cstool/rbuflock.h"
#include "csutil/bitarray.h"
#include "csutil/eventnames.h"
#include "csutil/scfarray.h"
#include "csutil/measuretime.h"

#include "gl_r2t_ext_fb_o.h"
#include "gl_r2t_framebuf.h"
#include "gl_render3d.h"
#include "gl_txtmgr_basictex.h"

const int CS_CLIPPER_EMPTY = 0xf008412;

// uses CS_CLIPPER_EMPTY
#include "gl_stringlists.h"
#include <csplugincommon/opengl/glenum_identstrs.h>



CS_PLUGIN_NAMESPACE_BEGIN(gl3d)
{

void csGLGraphics3D::BufferShadowDataHelper::RenderBufferDestroyed (
  iRenderBuffer* buffer)
{
  shadowedBuffers.DeleteAll (buffer);
}
    
iRenderBuffer* csGLGraphics3D::BufferShadowDataHelper::GetSupportedRenderBuffer (
  iRenderBuffer* originalBuffer)
{
  CS_ASSERT(!originalBuffer->IsIndexBuffer());

  ShadowedBuffer& shadowData = shadowedBuffers.GetOrCreate (originalBuffer);
  if (!shadowData.shadowBuffer
      || (shadowData.originalBufferVersion != originalBuffer->GetVersion()))
  {
    if (shadowData.IsNew())
      originalBuffer->SetCallback (this);
  
    if (!shadowData.shadowBuffer
        || (shadowData.shadowBuffer->GetComponentCount()
	  != originalBuffer->GetComponentCount())
	|| (shadowData.shadowBuffer->GetElementCount()
	  != originalBuffer->GetElementCount()))
    {
      shadowData.shadowBuffer = csRenderBuffer::CreateRenderBuffer (
        originalBuffer->GetElementCount(),
        originalBuffer->GetBufferType(),
        CS_BUFCOMP_FLOAT,
        originalBuffer->GetComponentCount());
    }
    shadowData.originalBufferVersion = originalBuffer->GetVersion();
    
    // The vertex list walker actually already does all the conversion we need
    csVertexListWalker<float> src (originalBuffer);
    csRenderBufferLock<float> dst (shadowData.shadowBuffer);
    size_t copySize = sizeof(float) * shadowData.shadowBuffer->GetComponentCount();
    for (size_t i = 0; i < dst.GetSize(); i++)
    {
      memcpy ((float*)(dst++), (const float*)src, copySize);
      ++src;
    }
  }
  
  return shadowData.shadowBuffer;
}

//---------------------------------------------------------------------------

CS_DECLARE_PROFILER
CS_DECLARE_PROFILER_ZONE(csGLGraphics3D_DrawMesh);
CS_DECLARE_PROFILER_ZONE(csGLGraphics3D_DrawMesh_DrawElements);

#define BYTE_TO_FLOAT(x) ((x) * (1.0 / 255.0))

csGLStateCache* csGLGraphics3D::statecache = 0;
csGLExtensionManager* csGLGraphics3D::ext = 0;

CS_IMPLEMENT_STATIC_CLASSVAR(MakeAString, scratch, GetScratch, csString, ())
CS_IMPLEMENT_STATIC_CLASSVAR_ARRAY(MakeAString, formatter, GetFormatter,
                                   char, [sizeof(MakeAString::Formatter)])
CS_IMPLEMENT_STATIC_CLASSVAR_ARRAY(MakeAString, reader, GetReader,
                                   char, [sizeof(MakeAString::Reader)])

SCF_IMPLEMENT_FACTORY (csGLGraphics3D)

csGLGraphics3D::csGLGraphics3D (iBase *parent) : 
  scfImplementationType (this, parent), isOpen (false), frameNum (0), 
  explicitProjection (false), needMatrixUpdate (true), imageUnits (0),
  activeVertexAttribs (0), wantToSwap (false), delayClearFlags (0),
  currentAttachments (0)
{
  verbose = false;
  frustum_valid = false;

  do_near_plane = false;
  viewwidth = 100;
  viewheight = 100;
  needViewportUpdate = true;

  stencilclipnum = 0;
  clip_planes_enabled = false;
  hasOld2dClip = false;

  current_drawflags = 0;
  current_shadow_state = 0;
  current_zmode = CS_ZBUF_NONE;
  zmesh = false;
  forceWireframe = false;

  use_hw_render_buffers = false;
  stencil_threshold = 500;
  broken_stencil = false;

  unsigned int i;
  for (i = 0; i < CS_VATTRIB_SPECIFIC_LAST+1; i++)
  {
    defaultBufferMapping[i] = CS_BUFFER_NONE;
  }
  defaultBufferMapping[CS_VATTRIB_POSITION] = CS_BUFFER_POSITION;
  defaultBufferMapping[CS_VATTRIB_TEXCOORD0] = CS_BUFFER_TEXCOORD0;
  defaultBufferMapping[CS_VATTRIB_COLOR] = CS_BUFFER_COLOR;
//  lastUsedShaderpass = 0;

  scrapIndicesSize = 0;
  scrapVerticesSize = 0;
  scrapBufferHolder.AttachNew (new csRenderBufferHolder);

  shadow_stencil_enabled = false;
  clipping_stencil_enabled = false;
  clipportal_dirty = true;
  clipportal_floating = 0;
  cliptype = CS_CLIPPER_NONE;

  r2tbackend = 0;
  bufferShadowDataHelper.AttachNew (new BufferShadowDataHelper);
}

csGLGraphics3D::~csGLGraphics3D()
{
  csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (object_reg));
  if (q)
  {
    q->RemoveListener (eventHandler1);
    q->RemoveListener (eventHandler2);
  }
}

void csGLGraphics3D::OutputMarkerString (const char* function, 
					 const wchar_t* file,
					 int line, const char* message)
{
  csStringFast<256> marker;
  marker.Format ("[%ls %s():%d] %s", file, function, line, message);
  ext->glStringMarkerGREMEDY ((GLsizei)marker.Length (), marker);
}

void csGLGraphics3D::OutputMarkerString (const char* function, 
					 const wchar_t* file,
					 int line, MakeAString& message)
{
  csStringFast<256> marker;
  marker.Format ("[%ls %s():%d] %s", file, function, line, 
    message.GetStr());
  ext->glStringMarkerGREMEDY ((GLsizei)marker.Length (), marker);
}

////////////////////////////////////////////////////////////////////
// Private helpers
////////////////////////////////////////////////////////////////////


void csGLGraphics3D::Report (int severity, const char* msg, ...)
{
  va_list arg;
  va_start (arg, msg);
  csReportV (object_reg, severity, "crystalspace.graphics3d.opengl", msg, arg);
  va_end (arg);
}

void csGLGraphics3D::CheckGLError (const wchar_t* sourceFile, int sourceLine,
				   const char* call)
{
  GLenum glerror = glGetError();
  if (glerror != GL_NO_ERROR)
  {
    Report (CS_REPORTER_SEVERITY_WARNING,
	    "GL error %s in \"%s\" [%ls:%d]",
	    CS::PluginCommon::OpenGLErrors.StringForIdent (glerror),
	    call, sourceFile, sourceLine);
  }
}

void csGLGraphics3D::SetCorrectStencilState ()
{
  if (shadow_stencil_enabled || clipping_stencil_enabled ||
  	clipportal_floating)
  {
    statecache->Enable_GL_STENCIL_TEST ();
  }
  else
  {
    statecache->Disable_GL_STENCIL_TEST ();
  }
}

void csGLGraphics3D::EnableStencilShadow ()
{
  shadow_stencil_enabled = true;
  statecache->Enable_GL_STENCIL_TEST ();
}

void csGLGraphics3D::DisableStencilShadow ()
{
  shadow_stencil_enabled = false;
  SetCorrectStencilState ();
}

void csGLGraphics3D::EnableStencilClipping ()
{
  clipping_stencil_enabled = true;
  statecache->Enable_GL_STENCIL_TEST ();
}

void csGLGraphics3D::DisableStencilClipping ()
{
  clipping_stencil_enabled = false;
  SetCorrectStencilState ();
}

void csGLGraphics3D::SetGlOrtho (bool inverted)
{
  if (inverted)
    glOrtho (0., (GLdouble) viewwidth, (GLdouble) viewheight, 0., -1.0, 10.0);
  else
    glOrtho (0., (GLdouble) viewwidth, 0., (GLdouble) viewheight, -1.0, 10.0);
}
  
void csGLGraphics3D::ComputeProjectionMatrix()
{
  if (!needMatrixUpdate) return;
  
  projectionMatrix = CS::Math::Projections::CSPerspective (
    viewwidth, viewheight, asp_center_x, asp_center_y, inv_aspect);
  
  needMatrixUpdate = false;
}

csZBufMode csGLGraphics3D::GetZModePass2 (csZBufMode mode)
{
  switch (mode)
  {
    case CS_ZBUF_NONE:
    case CS_ZBUF_TEST:
    case CS_ZBUF_EQUAL:
      return mode;
    case CS_ZBUF_FILL:
    case CS_ZBUF_USE:
      return CS_ZBUF_EQUAL;
    default:
      return CS_ZBUF_NONE;
  }
}

void csGLGraphics3D::SetZModeInternal (csZBufMode mode)
{
  switch (mode)
  {
    default:
    case CS_ZBUF_NONE:
      statecache->Disable_GL_DEPTH_TEST ();
      break;
    case CS_ZBUF_FILL:
      statecache->Enable_GL_DEPTH_TEST ();
      statecache->SetDepthFunc (GL_ALWAYS);
      statecache->SetDepthMask (GL_TRUE);
      break;
    case CS_ZBUF_EQUAL:
      statecache->Enable_GL_DEPTH_TEST ();
      statecache->SetDepthFunc (GL_EQUAL);
      statecache->SetDepthMask (GL_FALSE);
      break;
    case CS_ZBUF_INVERT:
      statecache->Enable_GL_DEPTH_TEST ();
      statecache->SetDepthFunc (GL_GREATER);
      statecache->SetDepthMask (GL_FALSE);
      break;
    case CS_ZBUF_TEST:
    case CS_ZBUF_USE:
      statecache->Enable_GL_DEPTH_TEST ();
      statecache->SetDepthFunc (GL_LEQUAL);
      statecache->SetDepthMask ((mode == CS_ZBUF_USE) ? GL_TRUE : GL_FALSE);
      break;
  }
}

static GLenum CSblendOpToGLblendOp (uint csop)
{
  switch (csop)
  {
    default:
    case CS_MIXMODE_FACT_ZERO:		return GL_ZERO;
    case CS_MIXMODE_FACT_ONE:		return GL_ONE;
    case CS_MIXMODE_FACT_SRCCOLOR:      return GL_SRC_COLOR;
    case CS_MIXMODE_FACT_SRCCOLOR_INV:  return GL_ONE_MINUS_SRC_COLOR;
    case CS_MIXMODE_FACT_DSTCOLOR:      return GL_DST_COLOR;
    case CS_MIXMODE_FACT_DSTCOLOR_INV:  return GL_ONE_MINUS_DST_COLOR;
    case CS_MIXMODE_FACT_SRCALPHA:      return GL_SRC_ALPHA;
    case CS_MIXMODE_FACT_SRCALPHA_INV:  return GL_ONE_MINUS_SRC_ALPHA;
    case CS_MIXMODE_FACT_DSTALPHA:      return GL_DST_ALPHA;
    case CS_MIXMODE_FACT_DSTALPHA_INV:  return GL_ONE_MINUS_DST_ALPHA;
  }
}

void csGLGraphics3D::SetMixMode (uint mode, csAlphaMode::AlphaType alphaType,
				 const CS::Graphics::AlphaTestOptions& alphaTest)
{
  bool doAlphaTest;
  switch (mode & CS_MIXMODE_ALPHATEST_MASK)
  {
    case CS_MIXMODE_ALPHATEST_ENABLE:
      doAlphaTest = true;
      break;
    case CS_MIXMODE_ALPHATEST_DISABLE:
      doAlphaTest = false;
      break;
    default:
    case CS_MIXMODE_ALPHATEST_AUTO:
      doAlphaTest = (alphaType == csAlphaMode::alphaBinary);
      break;
  }

  switch (mode & CS_MIXMODE_TYPE_MASK)
  {
    case CS_MIXMODE_TYPE_BLENDOP:
      statecache->Enable_GL_BLEND ();
      if ((mode & CS_MIXMODE_FLAG_BLENDOP_ALPHA) 
	&& (ext->CS_GL_EXT_blend_func_separate))
	statecache->SetBlendFuncSeparate (
	  CSblendOpToGLblendOp (CS_MIXMODE_BLENDOP_SRC(mode)),
	  CSblendOpToGLblendOp (CS_MIXMODE_BLENDOP_DST(mode)),
	  CSblendOpToGLblendOp (CS_MIXMODE_BLENDOP_ALPHA_SRC(mode)),
	  CSblendOpToGLblendOp (CS_MIXMODE_BLENDOP_ALPHA_DST(mode)));
      else
	statecache->SetBlendFunc (
	  CSblendOpToGLblendOp (CS_MIXMODE_BLENDOP_SRC(mode)),
	  CSblendOpToGLblendOp (CS_MIXMODE_BLENDOP_DST(mode)));
      break;
    case CS_MIXMODE_TYPE_AUTO:
    default:
      switch (alphaType)
      {
	case csAlphaMode::alphaNone:
	case csAlphaMode::alphaBinary:
	  statecache->Disable_GL_BLEND ();
	  break;
	default:
	case csAlphaMode::alphaSmooth:
	  statecache->Enable_GL_BLEND ();
	  statecache->SetBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	  break;
      }
      break;
  }

  if (doAlphaTest)
  {
    statecache->Enable_GL_ALPHA_TEST ();
    GLenum alphaFunc = GL_GEQUAL;
    switch (alphaTest.func)
    {
      case CS::Graphics::atfGreaterEqual:	/* already set */ break;
      case CS::Graphics::atfGreater:		alphaFunc = GL_GREATER; break;
      case CS::Graphics::atfLowerEqual:	alphaFunc = GL_LEQUAL; break;
      case CS::Graphics::atfLower:		alphaFunc = GL_LESS; break;
    }
    statecache->SetAlphaFunc (alphaFunc, alphaTest.threshold);
  }
  else
    statecache->Disable_GL_ALPHA_TEST ();
}

void csGLGraphics3D::CalculateFrustum ()
{
  if (frustum_valid) return;
  frustum_valid = true;
  if (clipper)
  {
    frustum.MakeEmpty ();
    size_t nv = clipper->GetVertexCount ();
    csVector3 v3;
    v3.z = 1;
    csVector2* v = clipper->GetClipPoly ();
    size_t i;
    for (i = 0 ; i < nv ; i++)
    {
      v3.x = (v[i].x - asp_center_x) * (1.0/aspect);
      v3.y = (v[i].y - asp_center_y) * (1.0/aspect);
      frustum.AddVertex (v3);
    }
  }
}

void csGLGraphics3D::SetupStencil ()
{
  if (stencil_initialized)
    return;

  stencil_initialized = true;

  if (clipper)
  {
    csBitArray clipPlanes (maxClipPlanes);
    int p;
    for (p = 0; p < maxClipPlanes; p++)
    {
      if (glIsEnabled (GL_CLIP_PLANE0+p))
      {
	clipPlanes.Set (p);
	glDisable (GL_CLIP_PLANE0+p);
      }
    }

    statecache->SetMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();
    statecache->SetMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glLoadIdentity ();
    // First set up the stencil area.
    EnableStencilClipping ();

    //stencilclipnum++;
    //if (stencilclipnum>255)
    {
      /*glStencilMask (128);
      glClearStencil (128);
      glClear (GL_STENCIL_BUFFER_BIT);*/
      stencilclipnum = 1;
    }
    size_t nv = clipper->GetVertexCount ();
    csVector2* v = clipper->GetClipPoly ();

    statecache->SetShadeModel (GL_FLAT);

    bool oldz = statecache->IsEnabled_GL_DEPTH_TEST ();
    statecache->Disable_GL_DEPTH_TEST ();
    bool tex2d = statecache->IsEnabled_GL_TEXTURE_2D ();
    statecache->Disable_GL_TEXTURE_2D ();

    GLboolean wmRed, wmGreen, wmBlue, wmAlpha;
    statecache->GetColorMask (wmRed, wmGreen, wmBlue, wmAlpha);
    statecache->SetColorMask (false, false, false, false);

    statecache->SetStencilMask (stencil_clip_mask);
    statecache->SetStencilFunc (GL_ALWAYS, stencil_clip_value, stencil_clip_mask);
    statecache->SetStencilOp (GL_REPLACE, GL_REPLACE, GL_REPLACE);
    glBegin (GL_TRIANGLE_FAN);
      glVertex2f ( 1, -1);
      glVertex2f (-1, -1);
      glVertex2f (-1,  1);
      glVertex2f ( 1,  1);
    glEnd ();

    statecache->SetStencilFunc (GL_ALWAYS, 0, stencil_clip_mask);

    glBegin (GL_TRIANGLE_FAN);
    size_t i;
    const float clipVertScaleX = 2.0f / (float)viewwidth;
    const float clipVertScaleY = 2.0f / (float)viewheight;
    for (i = 0 ; i < nv ; i++)
      glVertex2f (v[i].x*clipVertScaleX - 1.0f,
                  v[i].y*clipVertScaleY - 1.0f);
    glEnd ();

    statecache->SetColorMask (wmRed, wmGreen, wmBlue, wmAlpha);

    glPopMatrix ();
    statecache->SetMatrixMode (GL_PROJECTION);
    glPopMatrix ();
    if (oldz) statecache->Enable_GL_DEPTH_TEST ();
    if (tex2d) statecache->Enable_GL_TEXTURE_2D ();
    for (p = 0; p < maxClipPlanes; p++)
    {
      if (clipPlanes.IsBitSet (p))
        glEnable (GL_CLIP_PLANE0+p);
    }
  }
}

int csGLGraphics3D::SetupClipPlanes (bool add_clipper,
				     bool add_near_clip,
				     bool add_z_clip)
{
  GLRENDER3D_OUTPUT_STRING_MARKER(("(%d, %d, %d)", (int)add_clipper, 
    (int)add_near_clip, (int)add_z_clip));

  if (!(add_clipper || add_near_clip || add_z_clip)) return 0;

  statecache->SetMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();

  int planes = 0;
  GLdouble plane_eq[4];

  // This routine assumes the hardware planes can handle the
  // required number of planes from the clipper.
  if (clipper && add_clipper)
  {
    CalculateFrustum ();
    csPlane3 pl;
    int i1;
    i1 = (int)frustum.GetVertexCount ()-1;

    int maxfrustplanes = 6;
    if (add_near_clip) maxfrustplanes--;
    if (add_z_clip) maxfrustplanes--;
    int numfrustplanes = (int)frustum.GetVertexCount ();
    // Correct for broken stencil implementation.
    if (numfrustplanes > maxfrustplanes)
      numfrustplanes = maxfrustplanes;

    int i;
    for (i = 0 ; i < numfrustplanes ; i++)
    {
      pl.Set (csVector3 (0), frustum[i], frustum[i1]);
      plane_eq[0] = pl.A ();
      plane_eq[1] = pl.B ();
      plane_eq[2] = pl.C ();
      plane_eq[3] = pl.D ();
      glClipPlane ((GLenum)(GL_CLIP_PLANE0+planes), plane_eq);
      planes++;
      i1 = i;
    }
  }

  if (add_near_clip)
  {
    plane_eq[0] = -near_plane.A ();
    plane_eq[1] = -near_plane.B ();
    plane_eq[2] = -near_plane.C ();
    plane_eq[3] = -near_plane.D ();
    glClipPlane ((GLenum)(GL_CLIP_PLANE0+planes), plane_eq);
    planes++;
  }
  if (add_z_clip)
  {
    plane_eq[0] = 0;
    plane_eq[1] = 0;
    plane_eq[2] = 1;
    plane_eq[3] = -.001;
    glClipPlane ((GLenum)(GL_CLIP_PLANE0+planes), plane_eq);
    planes++;
  }

  glPopMatrix ();
  return planes;
}

void csGLGraphics3D::SetupClipper (int clip_portal,
				   int clip_plane,
				   int clip_z_plane,
				   int tri_count)
{
  GLRENDER3D_OUTPUT_STRING_MARKER(("(%d, %d, %d, %d)", 
    clip_portal, clip_plane, clip_z_plane, tri_count));
  
  // @@@@ RETHINK!!! THIS IS A HUGE PERFORMANCE BOOST. BUT???
  clip_z_plane = CS_CLIP_NOT;

  // There are two cases to consider.
  // 1. We have not encountered any floating portals yet. In that case
  //    we use scissor, stencil,  or plane clipping as directed by the
  //    clipper, clipping needs, and object (number of triangles).
  // 2. We have encountered a floating portal. In that case we always
  //    consider every subsequent portal as being floating.
  if (clipportal_floating)
  {
    if (clipportal_dirty)
    {
      clipportal_dirty = false;
      SetupClipPortals ();
    }
  }

  // If we have a box clipper then we can simply use glScissor (which
  // is already set up) to do the clipping. In case we have a floating
  // portal we also can use the following part.
  if ((clipper && clipper->GetClipperType() == iClipper2D::clipperBox) ||
  	clipportal_floating)
  {
    SetCorrectStencilState ();
    // If we still need plane clipping then we must set that up too.
    if (!clip_plane && !clip_z_plane)
      return;
    // We force clip_portal to CS_CLIP_NOT here so that we only do
    // the other clipping.
    clip_portal = CS_CLIP_NOT;
  }

  // Normal clipping.
  if (cache_clip_portal == clip_portal &&
      cache_clip_plane == clip_plane &&
      cache_clip_z_plane == clip_z_plane)
  {
    SetCorrectStencilState ();
    return;
  }
  cache_clip_portal = clip_portal;
  cache_clip_plane = clip_plane;
  cache_clip_z_plane = clip_z_plane;

  clip_planes_enabled = false;

  //===========
  // First we are going to find out what kind of clipping (if any)
  // we need. This depends on various factors including what the engine
  // says about the mesh (the clip_portal and clip_plane flags in the
  // mesh), what the current clipper is (the current cliptype),
  // and what the prefered clipper (stencil or glClipPlane).
  //===========

  // If the following flag becomes true in this routine then this means
  // that for portal clipping we will use stencil.
  bool clip_with_stencil = false;
  // If the following flag becomes true in this routine then this means
  // that for portal clipping we will use glClipPlane. This flag does
  // not say anything about z-plane and near plane clipping.
  bool clip_with_planes = false;
  // If one of the following flags is true then this means
  // that we will have to do plane clipping using glClipPlane for the near
  // or z=0 plane.
  bool do_plane_clipping = (do_near_plane && (clip_plane != CS_CLIP_NOT));
  bool do_z_plane_clipping = (clip_z_plane != CS_CLIP_NOT);

  bool m_prefer_stencil = (stencil_threshold >= 0) && 
    (tri_count > stencil_threshold);

  // First we see how many additional planes we might need because of
  // z-plane clipping and/or near-plane clipping. These additional planes
  // will not be usable for portal clipping (if we're using OpenGL plane
  // clipping).
  int reserved_planes = int (do_plane_clipping) + int (do_z_plane_clipping);

  if (clip_portal != CS_CLIP_NOT)//@@@??? && cliptype != CS_CLIPPER_OPTIONAL)
  {
    // Some clipping may be required.
    if (m_prefer_stencil)
      clip_with_stencil = true;
    else if (clipper && 
      (clipper->GetVertexCount () > (size_t)(maxClipPlanes - reserved_planes)))
    {
      if (broken_stencil || !stencil_clipping_available)
      {
        // If the stencil is broken we will clip with planes
	// even if we don't have enough planes. We will just
	// ignore the other planes then.
        clip_with_stencil = false;
        clip_with_planes = true;
      }
      else
      {
        clip_with_stencil = true;
      }
    }
    else
      clip_with_planes = true;
  }

  //===========
  // First setup the clipper that we need.
  //===========
  if (clip_with_stencil)
  {
    SetupStencil ();
    // Use the stencil area.
    EnableStencilClipping ();
  }
  else
  {
    DisableStencilClipping ();
  }

  int planes = SetupClipPlanes (clip_with_planes, do_plane_clipping,
  	do_z_plane_clipping);
  if (planes > 0)
  {
    clip_planes_enabled = true;
    for (int i = 0 ; i < planes ; i++)
      glEnable ((GLenum)(GL_CLIP_PLANE0+i));
  }
  for (int i = planes ; i < maxClipPlanes; i++)
    glDisable ((GLenum)(GL_CLIP_PLANE0+i));
}

/*void csGLGraphics3D::ApplyObjectToCamera ()
{
  GLfloat matrixholder[16];
  const csMatrix3 &orientation = object2camera.GetO2T();
  const csVector3 &translation = object2camera.GetO2TTranslation();

  matrixholder[0] = orientation.m11;
  matrixholder[1] = orientation.m21;
  matrixholder[2] = orientation.m31;
  matrixholder[3] = 0.0f;

  matrixholder[4] = orientation.m12;
  matrixholder[5] = orientation.m22;
  matrixholder[6] = orientation.m32;
  matrixholder[7] = 0.0f;

  matrixholder[8] = orientation.m13;
  matrixholder[9] = orientation.m23;
  matrixholder[10] = orientation.m33;
  matrixholder[11] = 0.0f;

  matrixholder[12] = 0.0f;
  matrixholder[13] = 0.0f;
  matrixholder[14] = 0.0f;
  matrixholder[15] = 1.0f;

  statecache->SetMatrixMode (GL_MODELVIEW);
  glLoadMatrixf (matrixholder);
  glTranslatef (-translation.x, -translation.y, -translation.z);
}
*/

void csGLGraphics3D::UpdateProjectionSVs ()
{
  if (!shadermgr) return;

  CS::Math::Matrix4 actualProjection = csGLGraphics3D::GetProjectionMatrix();

  shadermgr->GetVariableAdd (string_projection)->SetValue (actualProjection);
  shadermgr->GetVariableAdd (string_projection_inv)->SetValue (
    actualProjection.GetInverse());
}

void csGLGraphics3D::SetupProjection ()
{
  if (!needProjectionUpdate) return;

  GLRENDER3D_OUTPUT_LOCATION_MARKER;
  
  CS::Math::Matrix4 actualProjection;
  if (currentAttachments != 0)
    actualProjection = r2tbackend->FixupProjection (
      csGLGraphics3D::GetProjectionMatrix());
  else
    actualProjection = csGLGraphics3D::GetProjectionMatrix();

  statecache->SetMatrixMode (GL_PROJECTION);
  GLfloat matrixholder[16];
  CS::PluginCommon::MakeGLMatrix4x4 (actualProjection, matrixholder);
  glLoadMatrixf (matrixholder);
    
  statecache->SetMatrixMode (GL_MODELVIEW);
  needProjectionUpdate = false;
}

void csGLGraphics3D::ParseByteSize (const char* sizeStr, size_t& size)
{
  const char* end = sizeStr + strspn (sizeStr, "0123456789"); 	 
  size_t sizeFactor = 1; 	 
  if ((*end == 'k') || (*end == 'K')) 	 
    sizeFactor = 1024; 	 
  else if ((*end == 'm') || (*end == 'M')) 	 
    sizeFactor = 1024*1024; 	 
  else if (*end != 0)
  { 	 
    Report (CS_REPORTER_SEVERITY_WARNING, 	 
      "Unknown suffix '%s' in maximum buffer size '%s'.", end, sizeStr); 	 
    sizeFactor = 0; 	 
  } 	 
  if (sizeFactor != 0) 	 
  { 	 
    unsigned long tmp;
    if (sscanf (sizeStr, "%lu", &tmp) != 0)
    {
      size = tmp;
      size *= sizeFactor; 	 
    }
    else 	 
      Report (CS_REPORTER_SEVERITY_WARNING, 	 
      "Invalid buffer size '%s'.", sizeStr); 	 
  }
}

////////////////////////////////////////////////////////////////////
// iGraphics3D
////////////////////////////////////////////////////////////////////

bool csGLGraphics3D::Open ()
{
  if (isOpen) return true;
  isOpen = true;
  csRef<iPluginManager> plugin_mgr = 
  	csQueryRegistry<iPluginManager> (object_reg);

  csRef<iVerbosityManager> verbosemgr (
    csQueryRegistry<iVerbosityManager> (object_reg));
  if (verbosemgr) verbose = verbosemgr->Enabled ("renderer");
  if (!verbose) bugplug = 0;

  textureLodBias = config->GetFloat ("Video.OpenGL.TextureLODBias", 0.0f);
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY,
      "Texture LOD bias %g", textureLodBias);
 
  if (!G2D->Open ())
  {
    Report (CS_REPORTER_SEVERITY_ERROR, "Error opening Graphics2D context.");
    return false;
  }

  viewwidth = G2D->GetWidth();
  viewheight = G2D->GetHeight();
  SetPerspectiveAspect (viewheight);
  SetPerspectiveCenter (viewwidth/2, viewheight/2);
  
  object_reg->Register( G2D, "iGraphics2D");

  G2D->PerformExtension ("getstatecache", &statecache);
  G2D->PerformExtension	("getextmanager", &ext);

  G2D->GetFramebufferDimensions (scrwidth, scrheight);

  // The extension manager requires to initialize all used extensions with
  // a call to Init<ext> first.
  ext->InitGL_version_1_2 ();
  if (ext->CS_GL_version_1_2)
    glDrawRangeElements = ext->glDrawRangeElements;
  else
    glDrawRangeElements = myDrawRangeElements;
  ext->InitGL_ARB_multitexture ();
  ext->InitGL_ARB_texture_cube_map();
  ext->InitGL_EXT_texture3D ();
  ext->InitGL_ARB_vertex_buffer_object ();
  ext->InitGL_SGIS_generate_mipmap ();
  ext->InitGL_EXT_texture_filter_anisotropic ();
  ext->InitGL_EXT_texture_lod_bias ();
  //ext->InitGL_EXT_stencil_wrap ();
  //ext->InitGL_EXT_stencil_two_side ();
  ext->InitGL_ARB_point_parameters ();
  ext->InitGL_ARB_point_sprite ();
  ext->InitGL_EXT_framebuffer_object ();
  ext->InitGL_ARB_texture_non_power_of_two ();
  if (!ext->CS_GL_ARB_texture_non_power_of_two)
  {
    ext->InitGL_ARB_texture_rectangle ();
    if (!ext->CS_GL_ARB_texture_rectangle)
    {
      ext->InitGL_EXT_texture_rectangle();
      if (!ext->CS_GL_EXT_texture_rectangle)
        ext->InitGL_NV_texture_rectangle();
    }
  }
  ext->InitGL_ARB_vertex_program (); // needed for vertex attrib code
  // || ARB_vertex_shader, || GL_version_2_0
  ext->InitGL_ARB_fragment_program (); // needed for AFP DrawPixmap() workaround
  //ext->InitGL_ATI_separate_stencil ();
  ext->InitGL_EXT_secondary_color ();
  ext->InitGL_EXT_blend_func_separate ();
  ext->InitGL_GREMEDY_string_marker ();
  
  // Some 'assumed state' is for extensions, so set again
  CS::PluginCommon::GL::SetAssumedState (statecache, ext);
  
  rendercaps.minTexHeight = 2;
  rendercaps.minTexWidth = 2;
  GLint mts = config->GetInt ("Video.OpenGL.Caps.MaxTextureSize", -1);
  if (mts == -1)
  {
    glGetIntegerv (GL_MAX_TEXTURE_SIZE, &mts);
    if (mts <= 0)
    {
      // There appears to be a bug in some OpenGL drivers where
      // getting the maximum texture size simply doesn't work. In that
      // case we will issue a warning about this and assume 256x256.
      mts = 256;
      Report (CS_REPORTER_SEVERITY_WARNING, 
	"Detecting maximum texture size fails! 256x256 is assumed.\n"
	"Edit Video.OpenGL.Caps.MaxTextureSize if you want to specify a "
	"value.");
    }
  }
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY,
      "Maximum texture size is %dx%d", mts, mts);
  rendercaps.maxTexHeight = mts;
  rendercaps.maxTexWidth = mts;
  maxNpotsTexSize = mts;
  if (ext->CS_GL_ARB_texture_rectangle
    || ext->CS_GL_EXT_texture_rectangle
    || ext->CS_GL_NV_texture_rectangle)
  {
    glGetIntegerv (GL_MAX_RECTANGLE_TEXTURE_SIZE_ARB, &maxNpotsTexSize);
  }

  rendercaps.SupportsPointSprites = ext->CS_GL_ARB_point_parameters &&
    ext->CS_GL_ARB_point_sprite;

  {
    GLint abits;
    glGetIntegerv (GL_ALPHA_BITS, &abits);
    rendercaps.DestinationAlpha = abits > 0;
  }

  glGetIntegerv (GL_MAX_CLIP_PLANES, &maxClipPlanes);

  // check for support of VBO
  use_hw_render_buffers = ext->CS_GL_ARB_vertex_buffer_object;
  if (use_hw_render_buffers) 
  {
    size_t vboSize;


    ParseByteSize (config->GetStr ("Video.OpenGL.VBO.MaxSize", "64M"), vboSize);

    Report (CS_REPORTER_SEVERITY_NOTIFY, "Using VBO with %d MB of VBO memory",
      vboSize / (1024*1024));
    vboManager.AttachNew (new csGLVBOBufferManager (ext, statecache, vboSize));
  }

  GLint dbits;
  glGetIntegerv (GL_DEPTH_BITS, &dbits);
  if (dbits<25) 
    depth_epsilon = 1.0f/float ((1 << dbits)-1);
  else 
    depth_epsilon = 1.0f/float ((1 << 24)-1);
    
  stencil_shadow_mask = 127;
  {
    GLint sbits;
    glGetIntegerv (GL_STENCIL_BITS, &sbits);

    stencil_clipping_available = sbits > 0;
    if (stencil_clipping_available)
      stencil_clip_value = stencil_clip_mask = 1 << (sbits - 1);
    else
      stencil_clip_value = stencil_clip_mask = 0;
    if ((rendercaps.StencilShadows = (sbits > 1)))
    {
      stencil_shadow_mask = (1 << (sbits - 1)) - 1;
    }
  }

  stencil_threshold = config->GetInt ("Video.OpenGL.StencilThreshold", 500);
  broken_stencil = false;
  if (config->GetBool ("Video.OpenGL.BrokenStencil", false))
  {
    broken_stencil = true;
    stencil_threshold = -1;
  }
  if (verbose)
  {
    if (broken_stencil)
      Report (CS_REPORTER_SEVERITY_NOTIFY, "Stencil clipping is broken!");
    else if (!stencil_clipping_available)
      Report (CS_REPORTER_SEVERITY_NOTIFY, "Stencil clipping is not available");
    else
    {
      if (stencil_threshold >= 0)
      {
	      Report (CS_REPORTER_SEVERITY_NOTIFY, 
	        "Stencil clipping is used for objects >= %d triangles.", 
	      stencil_threshold);
      }
      else
      {
	      Report (CS_REPORTER_SEVERITY_NOTIFY, 
	        "Plane clipping is preferred.");
      }
    }
  }

  stencilClearWithZ = config->GetBool ("Video.OpenGL.StencilClearWithZ", true);
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY, 
      "Clearing Z buffer when stencil clear is needed %s", 
      stencilClearWithZ ? "enabled" : "disabled");

  shadermgr = csQueryRegistryOrLoad<iShaderManager> (object_reg,
    "crystalspace.graphics3d.shadermanager", false);
  if (!shadermgr && verbose)
  {
    Report (CS_REPORTER_SEVERITY_WARNING, 
      "Could not load shader manager. Any attempt at 3D rendering will fail.");
  }

  txtmgr.AttachNew (new csGLTextureManager (
    object_reg, GetDriver2D (), config, this));

  statecache->Enable_GL_CULL_FACE ();
  statecache->SetCullFace (GL_BACK);

  statecache->SetStencilMask (stencil_shadow_mask);

  numImageUnits = statecache->GetNumImageUnits();
  numTCUnits = statecache->GetNumTexCoords();
  imageUnits = new ImageUnit[numImageUnits];
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY, 
      "Available texture image units: %d texture coordinate units: %d",
      numImageUnits, numTCUnits);

  // Set up texture LOD bias.
  if (ext->CS_GL_EXT_texture_lod_bias)
  {
    if (ext->CS_GL_ARB_multitexture)
    {
      for (int u = numImageUnits - 1; u >= 0; u--)
      {
        statecache->SetCurrentImageUnit (u);
        statecache->ActivateImageUnit ();
        glTexEnvf (GL_TEXTURE_FILTER_CONTROL_EXT, 
	        GL_TEXTURE_LOD_BIAS_EXT, textureLodBias); 
      }
    }
    else
    {
      glTexEnvf (GL_TEXTURE_FILTER_CONTROL_EXT, 
	      GL_TEXTURE_LOD_BIAS_EXT, textureLodBias); 
    }
  }

  string_vertices = strings->Request ("vertices");
  string_texture_coordinates = strings->Request ("texture coordinates");
  string_normals = strings->Request ("normals");
  string_colors = strings->Request ("colors");
  string_indices = strings->Request ("indices");
  string_point_radius = strings->Request ("point radius");
  string_point_scale = strings->Request ("point scale");
  string_texture_diffuse = strings->Request (CS_MATERIAL_TEXTURE_DIFFUSE);
  string_world2camera = strings->Request ("world2camera transform");
  string_world2camera_inv = strings->Request ("world2camera transform inverse");
  string_projection = strings->Request ("projection transform");
  string_projection_inv = strings->Request ("projection transform inverse");

  cache_clip_portal = -1;
  cache_clip_plane = -1;
  cache_clip_z_plane = -1;

  const char* r2tBackendStr;
  if (ext->CS_GL_EXT_framebuffer_object)
  {
    r2tBackendStr = "EXT_framebuffer_object";
    r2tbackend = new csGLRender2TextureEXTfbo (this);
  }
  
  if ((r2tbackend == 0) || !r2tbackend->Status())
  {
    r2tBackendStr = "framebuffer";
    delete r2tbackend;
    r2tbackend = new csGLRender2TextureFramebuf (this);
  }
  
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY, "Render-to-texture backend: %s",
      r2tBackendStr);

  enableDelaySwap = config->GetBool ("Video.OpenGL.DelaySwap", false);
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY, "Delayed buffer swapping: %s",
      enableDelaySwap ? "enabled" : "disabled");

  drawPixmapAFP = config->GetBool ("Video.OpenGL.AFPDrawPixmap", false)
    && ext->CS_GL_ARB_fragment_program;
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY, "AFP DrawPixmap() workaround: %s",
      drawPixmapAFP ? "enabled" : "disabled");

  if (drawPixmapAFP)
  {
    static const char drawPixmapProgramStr[] = 
      "!!ARBfp1.0\n"
      "TEMP texel;\n"
      "TEX texel, fragment.texcoord[0], texture[0], 2D;\n"
      "MUL result.color, texel, fragment.color.primary;\n"
      "END\n";

    ext->glGenProgramsARB (1, &drawPixmapProgram);
    ext->glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, drawPixmapProgram);
    ext->glProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, 
      GL_PROGRAM_FORMAT_ASCII_ARB, 
      (GLsizei)(sizeof (drawPixmapProgramStr) - 1), 
      (void*)drawPixmapProgramStr);

    const GLubyte * programErrorString = glGetString (GL_PROGRAM_ERROR_STRING_ARB);

    GLint errorpos;
    glGetIntegerv (GL_PROGRAM_ERROR_POSITION_ARB, &errorpos);
    if(errorpos != -1)
    {
      if (verbose)
      {
        Report (CS_REPORTER_SEVERITY_WARNING, 
          "Couldn't load fragment program for text drawing");
        Report (CS_REPORTER_SEVERITY_WARNING, "Program error at position %d", errorpos);
        Report (CS_REPORTER_SEVERITY_WARNING, "Error string: '%s'", 
          programErrorString);
        ext->glDeleteProgramsARB (1, &drawPixmapProgram);
        drawPixmapAFP = false;
      }
    }
    else
    {
      if (verbose && (programErrorString != 0) && (*programErrorString != 0))
      {
        Report (CS_REPORTER_SEVERITY_WARNING, 
	  "Warning for DrawPixmap() fragment program: '%s'", 
	  programErrorString);
      }
    }
  }

#define LQUOT   "\xE2\x80\x9c"
#define RQUOT   "\xE2\x80\x9d"
  fixedFunctionForcefulEnable = 
    config->GetBool ("Video.OpenGL.FixedFunctionForcefulEnable", false);
  if (verbose)
    Report (CS_REPORTER_SEVERITY_NOTIFY, 
      LQUOT "Forceful" RQUOT " fixed function enable: %s",
      fixedFunctionForcefulEnable ? "yes" : "no");
      
  return true;
}

void csGLGraphics3D::SetupShaderVariables()
{
  // Allow start up w/o shader manager
  if (!shadermgr) return;

  /* The shadermanager clears all SVs in Open(), but renderer Open() is called
     before the shadermanager's, thus the renderer needs to catch the open
     event twice, the second time setting up SVs */
  shadermgr->GetVariableAdd (strings->Request ("world2camera transform"));
  shadermgr->GetVariableAdd (strings->Request ("world2camera transform inverse"));
  shadermgr->GetVariableAdd (strings->Request ("projection transform"));
  shadermgr->GetVariableAdd (strings->Request ("projection transform inverse"));
  
  /* @@@ All those default textures, better put them into the engine? */

  // @@@ These shouldn't be here, I guess.
  #ifdef CS_FOGTABLE_SIZE
  #undef CS_FOGTABLE_SIZE
  #endif
  #define CS_FOGTABLE_SIZE 256
  // Each texel in the fog table holds the fog alpha value at a certain
  // (distance*density).  The median distance parameter determines the
  // (distance*density) value represented by the texel at the center of
  // the fog table.  The fog calculation is:
  // alpha = 1.0 - exp( -(density*distance) / CS_FOGTABLE_MEDIANDISTANCE)
  #define CS_FOGTABLE_MEDIANDISTANCE 10.0f
  #define CS_FOGTABLE_MAXDISTANCE (CS_FOGTABLE_MEDIANDISTANCE * 2.0f)
  #define CS_FOGTABLE_DISTANCESCALE (1.0f / CS_FOGTABLE_MAXDISTANCE)

  csRGBpixel *transientfogdata = 
    new csRGBpixel[CS_FOGTABLE_SIZE * CS_FOGTABLE_SIZE];
  memset(transientfogdata, 255, CS_FOGTABLE_SIZE * CS_FOGTABLE_SIZE * 4);
  for (unsigned int fogindex1 = 0; fogindex1 < CS_FOGTABLE_SIZE; fogindex1++)
  {
    for (unsigned int fogindex2 = 0; fogindex2 < CS_FOGTABLE_SIZE; fogindex2++)
    {
      unsigned char fogalpha1 = 
        (unsigned char)(255.0f * csFogMath::Ramp (
          (float)fogindex1 / CS_FOGTABLE_SIZE));
      if (fogindex1 == (CS_FOGTABLE_SIZE - 1))
        fogalpha1 = 255;
      unsigned char fogalpha2 = 
        (unsigned char)(255.0f * csFogMath::Ramp (
          (float)fogindex2 / CS_FOGTABLE_SIZE));
      if (fogindex2 == (CS_FOGTABLE_SIZE - 1))
        fogalpha2 = 255;
      transientfogdata[(fogindex1+fogindex2*CS_FOGTABLE_SIZE)].alpha = 
        MIN(fogalpha1, fogalpha2);
    }
  }

  csRef<iImage> img = csPtr<iImage> (new csImageMemory (
    CS_FOGTABLE_SIZE, CS_FOGTABLE_SIZE, transientfogdata, true, 
    CS_IMGFMT_TRUECOLOR | CS_IMGFMT_ALPHA));
  csRef<iTextureHandle> fogtex = txtmgr->RegisterTexture (
    img, CS_TEXTURE_3D | CS_TEXTURE_CLAMP | CS_TEXTURE_NOMIPMAPS);
  fogtex->SetTextureClass ("lookup");

  csRef<csShaderVariable> fogvar = csPtr<csShaderVariable> (
  	new csShaderVariable (strings->Request ("standardtex fog")));
  fogvar->SetValue (fogtex);
  shadermgr->AddVariable(fogvar);

  {
    const int normalizeCubeSize = config->GetInt (
      "Video.OpenGL.NormalizeCubeSize", 256);

    csRef<csShaderVariable> normvar = 
      csPtr<csShaderVariable> (new csShaderVariable (
      strings->Request ("standardtex normalization map")));
    csRef<iShaderVariableAccessor> normCube;
    normCube.AttachNew (new csNormalizationCubeAccessor (txtmgr, 
      normalizeCubeSize));
    normvar->SetAccessor (normCube);
    shadermgr->AddVariable(normvar);
  }

  {
    csRGBpixel* white = new csRGBpixel[1];
    white->Set (255, 255, 255);
    img = csPtr<iImage> (new csImageMemory (1, 1, white, true, 
      CS_IMGFMT_TRUECOLOR));

    csRef<iTextureHandle> whitetex = txtmgr->RegisterTexture (
      img, CS_TEXTURE_3D | CS_TEXTURE_NOMIPMAPS);

    csRef<csShaderVariable> whitevar = csPtr<csShaderVariable> (
      new csShaderVariable (
      strings->Request ("standardtex white")));
    whitevar->SetValue (whitetex);
    shadermgr->AddVariable (whitevar);
  }

}

void csGLGraphics3D::Close ()
{
  if (!isOpen) return;

  glFinish ();

  if (drawPixmapAFP)
    ext->glDeleteProgramsARB (1, &drawPixmapProgram);

  txtmgr = 0;
  shadermgr = 0;
  delete[] imageUnits;
  delete r2tbackend; r2tbackend = 0;
  for (size_t h = 0; h < halos.GetSize (); h++)
  {
    if (halos[h]) halos[h]->DeleteTexture();
  }
  vboManager.Invalidate();

  if (G2D)
    G2D->Close ();
  statecache = 0;
  
  isOpen = false;
}

bool csGLGraphics3D::SetRenderTarget (iTextureHandle* handle, bool persistent,
                                      int subtexture,
                                      csRenderTargetAttachment attachment)
{
  uint newAttachments = currentAttachments;
  if (handle != 0)
    newAttachments |= (1 << attachment);
  else
    newAttachments &= ~(1 << attachment);
  
  if (newAttachments == 0)
  {
    r2tbackend->UnsetRenderTargets();
  }
  else
  {
    if ((handle != 0)
        && !r2tbackend->SetRenderTarget (handle, persistent, subtexture,
      attachment)) return false; 
  }
  
  if ((newAttachments != 0) != (currentAttachments != 0))
  {
    int hasRenderTarget = (newAttachments != 0) ? 1 : 0;
    G2D->PerformExtension ("userendertarget", hasRenderTarget);
    viewwidth = G2D->GetWidth();
    viewheight = G2D->GetHeight();
    needViewportUpdate = true;
  }
  currentAttachments = newAttachments;
  return true;
}

bool csGLGraphics3D::ValidateRenderTargets ()
{
  return r2tbackend->ValidateRenderTargets ();
}

bool csGLGraphics3D::CanSetRenderTarget (const char* format,
                                         csRenderTargetAttachment attachment)
{
  return r2tbackend->CanSetRenderTarget (format, attachment);
}

iTextureHandle* csGLGraphics3D::GetRenderTarget (csRenderTargetAttachment attachment,
                                                 int* subtexture) const
{
  return r2tbackend->GetRenderTarget (attachment, subtexture);
}

void csGLGraphics3D::UnsetRenderTargets()
{
  r2tbackend->UnsetRenderTargets();
  
  G2D->PerformExtension ("userendertarget", 0);
  viewwidth = G2D->GetWidth();
  viewheight = G2D->GetHeight();
  needViewportUpdate = true;
}

void csGLGraphics3D::CopyFromRenderTargets (size_t num,
  csRenderTargetAttachment* attachments,
  iTextureHandle** textures,
  int* subtextures)
{
  for (size_t i = 0; i < num; i++)
  {
    /* CopyTex(Sub)Image 'chooses' the attachment accorings of the format
       of the texture copied to; thus, ignore the specified attachment
       for now ... */
    iTextureHandle* tex = textures[i];
    int subtexture = subtextures ? subtextures[i] : 0;
  
    csGLBasicTextureHandle* tex_mm = static_cast<csGLBasicTextureHandle*> (tex);
    tex_mm->Precache ();
    // Texture is in tha cache, update texture directly.
    ActivateTexture (tex_mm);
  
    GLenum internalFormat = 0;
  
    GLenum textarget = tex_mm->GetGLTextureTarget();
    if ((textarget != GL_TEXTURE_2D)
	&& (textarget != GL_TEXTURE_3D)  
	&& (textarget != GL_TEXTURE_RECTANGLE_ARB) 
	&& (textarget != GL_TEXTURE_CUBE_MAP))
      return;
      
    int txt_w, txt_h;
    tex_mm->GetRendererDimensions (txt_w, txt_h);

    bool handle_subtexture = (textarget == GL_TEXTURE_CUBE_MAP);
    bool handle_3d = (textarget == GL_TEXTURE_3D);
    /* Reportedly, some drivers crash if using CopyTexImage on a texture
      * size larger than the framebuffer. Use CopyTexSubImage then. */
    bool needSubImage = (txt_w > viewwidth) 
      || (txt_h > viewheight);
    // Texture was not used as a render target before.
    // Make some necessary adjustments.
    if (needSubImage)
    {
      int orgX = 0;
      int orgY = scrheight - (csMin (txt_h, viewheight));
    
      if (handle_subtexture)
	glCopyTexSubImage2D (
	  GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + subtexture,
	  0, 0, 0, orgX, orgY, 
	  csMin (txt_w, viewwidth), 
	  csMin (txt_h, viewheight));
      else if (handle_3d)
	ext->glCopyTexSubImage3D (textarget, 0, 0, 0, orgX, orgY,
	  subtexture,
	  csMin (txt_w, viewwidth),
	  csMin (txt_h, viewheight));
      else
	glCopyTexSubImage2D (textarget, 0, 0, 0, orgX, orgY, 
	  csMin (txt_w, viewwidth),
	  csMin (txt_h, viewheight));
    }
    else
    {
      int orgX = 0;
      int orgY = scrheight - txt_h;
    
      glGetTexLevelParameteriv ((textarget == GL_TEXTURE_CUBE_MAP) 
	  ? GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB : textarget, 
	0, GL_TEXTURE_INTERNAL_FORMAT, (GLint*)&internalFormat);
      
      if (handle_subtexture)
	glCopyTexSubImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X_ARB + subtexture, 
	  0, 0, 0, orgX, orgY, txt_w, txt_h);
      else if (handle_3d)
	ext->glCopyTexSubImage3D (textarget, 0, 0, 0, orgX, orgY,
	  subtexture, txt_w, txt_h);
      else
	glCopyTexSubImage2D (textarget, 0,
	  0, 0, orgX, orgY, txt_w, txt_h);
    }
    tex_mm->RegenerateMipmaps();
  }
}

bool csGLGraphics3D::BeginDraw (int drawflags)
{
  (void)drawflagNames; // Pacify compiler when CS_DEBUG not defined.
                       // Avoids `symbols defined but not used' warning.
  GLRENDER3D_OUTPUT_STRING_MARKER(("drawflags = %s", 
    csBitmaskToString::GetStr (drawflags, drawflagNames)));

  SetWriteMask (true, true, true, true);
  if (ext->CS_GL_ARB_point_sprite)
    statecache->Disable_GL_POINT_SPRITE_ARB();

  clipportal_dirty = true;
  clipportal_floating = 0;
  CS_ASSERT (clipportal_stack.GetSize () == 0);

  debug_inhibit_draw = false;

  int i = 0;
  for (i = numImageUnits; i-- > 0;)
    DeactivateTexture (i);

  // if 2D graphics is not locked, lock it
  if ((drawflags & (CSDRAW_2DGRAPHICS | CSDRAW_3DGRAPHICS))
   != (current_drawflags & (CSDRAW_2DGRAPHICS | CSDRAW_3DGRAPHICS))
   || needViewportUpdate)
  {
    if (!G2D->BeginDraw ())
      return false;
    if (current_drawflags & CSDRAW_2DGRAPHICS)
      G2D->PerformExtension ("glflushtext");
    GLRENDER3D_OUTPUT_STRING_MARKER(("after G2D->BeginDraw()"));
  }
  viewwidth = G2D->GetWidth();
  viewheight = G2D->GetHeight();
  needViewportUpdate = false;
  const int old_drawflags = current_drawflags;
  current_drawflags = drawflags;

  int clearMask = 0;
  const bool doStencilClear = 
    (drawflags & CSDRAW_3DGRAPHICS) && stencil_clipping_available;
  const bool doZbufferClear = (drawflags & CSDRAW_CLEARZBUFFER)
    || (doStencilClear && stencilClearWithZ);
  if (doZbufferClear)
  {
    const GLbitfield stencilFlag = 
      stencil_clipping_available ? GL_STENCIL_BUFFER_BIT : 0;
    statecache->SetDepthMask (GL_TRUE);
    if (drawflags & CSDRAW_CLEARSCREEN)
      clearMask = GL_DEPTH_BUFFER_BIT | stencilFlag
      	| GL_COLOR_BUFFER_BIT;
    else
      clearMask = GL_DEPTH_BUFFER_BIT | stencilFlag;
  }
  else if (drawflags & CSDRAW_CLEARSCREEN)
    clearMask = GL_COLOR_BUFFER_BIT;
  else if (doStencilClear)
    clearMask = GL_STENCIL_BUFFER_BIT;
  
  bool scissorWasEnabled = false;
  if (drawflags & CSDRAW_NOCLIPCLEAR)
  {
    scissorWasEnabled = statecache->IsEnabled_GL_SCISSOR_TEST();
    statecache->Disable_GL_SCISSOR_TEST();
  }
    
  if (!enableDelaySwap)
    glClear (clearMask);
  else
    delayClearFlags = clearMask;
    
  if ((drawflags & CSDRAW_NOCLIPCLEAR) && scissorWasEnabled)
    statecache->Enable_GL_SCISSOR_TEST();

  statecache->SetCullFace (GL_FRONT);

  /* Note: this function relies on the canvas and/or the R2T backend to setup
   * matrices etc. So be careful when changing stuff. */

  if (currentAttachments != 0)
    r2tbackend->BeginDraw (drawflags); 

  if (drawflags & CSDRAW_3DGRAPHICS)
  {
    // @@@ Jorrit: to avoid flickering I had to increase the
    // values below and multiply them with 3.
    // glPolygonOffset (-0.05f, -2.0f); 
    glPolygonOffset (-0.15f, -6.0f); 
    needProjectionUpdate = true;
    glColor4f (1.0f, 1.0f, 1.0f, 1.0f);

  //    object2camera.Identity ();
    //@@@ TODO FIX
    return true;
  }
  else if (drawflags & CSDRAW_2DGRAPHICS)
  {
    SwapIfNeeded();
    // Don't set up the 2D stuff if we already are in 2D mode
    if (!(old_drawflags & CSDRAW_2DGRAPHICS))
    {
      /*
	Turn off some stuff that isn't needed for 2d (or even can
	cause visual glitches.)
      */
      DeactivateBuffers (0, 0);
      statecache->Disable_GL_ALPHA_TEST ();
      if (ext->CS_GL_ARB_multitexture)
      {
        statecache->SetCurrentImageUnit (0);
        statecache->ActivateImageUnit ();
        statecache->SetCurrentTCUnit (0);
        statecache->ActivateTCUnit (csGLStateCache::activateTexCoord);
      }
      statecache->Disable_GL_POLYGON_OFFSET_FILL ();

      if (fixedFunctionForcefulEnable)
      {
        const GLenum state = GL_FOG;
        GLboolean s = glIsEnabled (state);
        if (s) glDisable (state); else glEnable (state);
        glBegin (GL_TRIANGLES);  glEnd ();
        if (s) glEnable (state); else glDisable (state);
      }

      needProjectionUpdate = false; 
      /* Explicitly avoid update. The 3D mode projection will not work in
       * 2D mode. */

//      object2camera.Identity ();
      //@@@ TODO FIX

      SetZMode (CS_ZBUF_NONE);
      
      SetMixMode (CS_FX_ALPHA, csAlphaMode::alphaSmooth,
	CS::Graphics::AlphaTestOptions ()); 
      // So alpha blending works w/ 2D drawing
      glColor4f (1.0f, 1.0f, 1.0f, 1.0f);
    }
    return true;
  }

  current_drawflags = 0;
  return false;
}

void csGLGraphics3D::FinishDraw ()
{
  if (current_drawflags & (CSDRAW_2DGRAPHICS | CSDRAW_3DGRAPHICS))
    G2D->FinishDraw ();

  DeactivateBuffers (0, 0);
  
  if (currentAttachments != 0)
  {
    r2tbackend->FinishDraw ((current_drawflags & CSDRAW_READBACK) != 0);
    UnsetRenderTargets();
    currentAttachments = 0;
  }
  
  current_drawflags = 0;
}

void csGLGraphics3D::Print (csRect const* area)
{
  //glFinish ();
  if (bugplug)
    bugplug->ResetCounter ("Triangle Count");

  if (vboManager.IsValid ())
  {
//@@TODO:    vboManager->ResetFrameStats ();
  }

  if (enableDelaySwap)
  {
    if (area == 0)
    {
      wantToSwap = true;
      return;
    }
    SwapIfNeeded();
  }
  G2D->Print (area);
  
  //csPrintf ("frame\n");
  frameNum++;
  r2tbackend->NextFrame (frameNum);
  txtmgr->NextFrame (frameNum);
}

void csGLGraphics3D::DrawLine (const csVector3 & v1, const csVector3 & v2,
	float fov, int color)
{
  SwapIfNeeded();

  if (v1.z < SMALL_Z && v2.z < SMALL_Z)
    return;

  float x1 = v1.x, y1 = v1.y, z1 = v1.z;
  float x2 = v2.x, y2 = v2.y, z2 = v2.z;

  if (z1 < SMALL_Z)
  {
    // x = t*(x2-x1)+x1;
    // y = t*(y2-y1)+y1;
    // z = t*(z2-z1)+z1;
    float t = (SMALL_Z - z1) / (z2 - z1);
    x1 = t * (x2 - x1) + x1;
    y1 = t * (y2 - y1) + y1;
    z1 = SMALL_Z;
  }
  else if (z2 < SMALL_Z)
  {
    // x = t*(x2-x1)+x1;
    // y = t*(y2-y1)+y1;
    // z = t*(z2-z1)+z1;
    float t = (SMALL_Z - z1) / (z2 - z1);
    x2 = t * (x2 - x1) + x1;
    y2 = t * (y2 - y1) + y1;
    z2 = SMALL_Z;
  }
  float iz1 = fov / z1;
  int px1 = csQint (x1 * iz1 + (viewwidth / 2));
  int py1 = viewheight - 1 - csQint (y1 * iz1 + (viewheight / 2));
  float iz2 = fov / z2;
  int px2 = csQint (x2 * iz2 + (viewwidth / 2));
  int py2 = viewheight - 1 - csQint (y2 * iz2 + (viewheight / 2));

  G2D->DrawLine (px1, py1, px2, py2, color);
}


bool csGLGraphics3D::ActivateBuffers (csRenderBufferHolder *holder, 
                                      csRenderBufferName mapping[CS_VATTRIB_SPECIFIC_LAST+1])
{
  if (!holder) return false;

  BufferChange queueEntry;

  queueEntry.buffer = holder->GetRenderBuffer (mapping[CS_VATTRIB_POSITION]);
  queueEntry.attrib = CS_VATTRIB_POSITION;
  changeQueue.Push (queueEntry);
  
  queueEntry.buffer = holder->GetRenderBuffer (mapping[CS_VATTRIB_NORMAL]);
  queueEntry.attrib = CS_VATTRIB_NORMAL;
  changeQueue.Push (queueEntry);
  
  queueEntry.buffer = holder->GetRenderBuffer (mapping[CS_VATTRIB_COLOR]);
  queueEntry.attrib = CS_VATTRIB_COLOR;
  changeQueue.Push (queueEntry);
  
  queueEntry.buffer = holder->GetRenderBuffer (mapping[CS_VATTRIB_SECONDARY_COLOR]);
  queueEntry.attrib = CS_VATTRIB_SECONDARY_COLOR;
  changeQueue.Push (queueEntry);
  
  const int n = ( numTCUnits < 8 ) ? numTCUnits : 8 ;
  for (int i = 0; i < n; i++)
  {
    queueEntry.buffer = holder->GetRenderBuffer (mapping[CS_VATTRIB_TEXCOORD0+i]);
    queueEntry.attrib = (csVertexAttrib)(CS_VATTRIB_TEXCOORD0+i);
    changeQueue.Push (queueEntry);
  }
  return true;
}

bool csGLGraphics3D::ActivateBuffers (csVertexAttrib *attribs, 
                                      iRenderBuffer** buffers, unsigned int count)
{
  for (unsigned int i = 0; i < count; i++)
  {
    csVertexAttrib att = attribs[i];
    iRenderBuffer *buffer = buffers[i];
    if (!buffer) continue;

    BufferChange queueEntry;
    queueEntry.buffer = buffer;
    queueEntry.attrib = att;
    changeQueue.Push (queueEntry);
  }
  return true;
}

void csGLGraphics3D::DeactivateBuffers (csVertexAttrib *attribs, unsigned int count)
{
  GLRENDER3D_OUTPUT_STRING_MARKER(("%p, %u", attribs, count));

  if (vboManager) vboManager->DeactivateVBO ();
  unsigned int i;
  if (!attribs)
  {
    //disable all
    statecache->Disable_GL_VERTEX_ARRAY ();
    statecache->Disable_GL_NORMAL_ARRAY ();
    statecache->Disable_GL_COLOR_ARRAY ();
    if (ext->CS_GL_EXT_secondary_color)
      statecache->Disable_GL_SECONDARY_COLOR_ARRAY_EXT ();
    for (i = numTCUnits; i-- > 0;)
    {
      statecache->SetCurrentTCUnit (i);
      statecache->Disable_GL_TEXTURE_COORD_ARRAY ();
    }
    if (ext->glDisableVertexAttribArrayARB)
    {
      for (i = 0; i < CS_VATTRIB_GENERIC_LAST-CS_VATTRIB_GENERIC_FIRST+1; i++)
      {
	if (activeVertexAttribs & (1 << i))
	{
	  ext->glDisableVertexAttribArrayARB (i);
	  activeVertexAttribs &= ~(1 << i);
	}
      }
    }

    for (i = 0; i < CS_VATTRIB_SPECIFIC_LAST-CS_VATTRIB_SPECIFIC_FIRST+1; i++)
    {
      iRenderBuffer *b = spec_renderBuffers[i];
      if (b) RenderRelease (b);// b->RenderRelease ();
    }
    for (i = 0; i < CS_VATTRIB_GENERIC_LAST-CS_VATTRIB_GENERIC_FIRST+1; i++)
    {
      iRenderBuffer *b = gen_renderBuffers[i];
      if (b) RenderRelease (b);// b->RenderRelease ();
      gen_renderBuffers[i] = 0;
    }
    changeQueue.Empty();
  }
  else
  {
    for (i = 0; i < count; i++)
    {
      csVertexAttrib att = attribs[i];
      BufferChange queueEntry;
      queueEntry.buffer = 0;
      queueEntry.attrib = att;
      changeQueue.Push (queueEntry);
    }
  }
}

bool csGLGraphics3D::ActivateTexture (iTextureHandle *txthandle, int unit)
{
  if (ext->CS_GL_ARB_multitexture)
  {
    statecache->SetCurrentImageUnit (unit);
    statecache->ActivateImageUnit ();
  }
  else if (unit != 0) return false;

  csGLBasicTextureHandle* gltxthandle = 
    static_cast<csGLBasicTextureHandle*> (txthandle);
  GLuint texHandle = gltxthandle->GetHandle ();

  switch (gltxthandle->texType)
  {
    case iTextureHandle::texType1D:
      statecache->Enable_GL_TEXTURE_1D ();
      statecache->SetTexture (GL_TEXTURE_1D, texHandle);
      break;
    case iTextureHandle::texType2D:
      statecache->Enable_GL_TEXTURE_2D ();
      statecache->SetTexture (GL_TEXTURE_2D, texHandle);
      break;
    case iTextureHandle::texType3D:
      statecache->Enable_GL_TEXTURE_3D ();
      statecache->SetTexture (GL_TEXTURE_3D, texHandle);
      break;
    case iTextureHandle::texTypeCube:
      statecache->Enable_GL_TEXTURE_CUBE_MAP ();
      statecache->SetTexture (GL_TEXTURE_CUBE_MAP, texHandle);
      break;
    case iTextureHandle::texTypeRect:
      statecache->Enable_GL_TEXTURE_RECTANGLE_ARB ();
      statecache->SetTexture (GL_TEXTURE_RECTANGLE_ARB, texHandle);
      break;
    default:
      DeactivateTexture (unit);
      return false;
  }
  imageUnits[unit].texture = gltxthandle;
  
  return true;
}

void csGLGraphics3D::DeactivateTexture (int unit)
{
  if (ext->CS_GL_ARB_multitexture)
  {
    statecache->SetCurrentImageUnit (unit);
  }
  else if (unit != 0) return;

  if (imageUnits[unit].texture == 0) return;

  switch (imageUnits[unit].texture->texType)
  {
    case iTextureHandle::texType1D:
      statecache->Disable_GL_TEXTURE_1D ();
      statecache->SetTexture (GL_TEXTURE_1D, 0);
      break;
    case iTextureHandle::texType2D:
      statecache->Disable_GL_TEXTURE_2D ();
      statecache->SetTexture (GL_TEXTURE_2D, 0);
      break;
    case iTextureHandle::texType3D:
      statecache->Disable_GL_TEXTURE_3D ();
      statecache->SetTexture (GL_TEXTURE_3D, 0);
      break;
    case iTextureHandle::texTypeCube:
      statecache->Disable_GL_TEXTURE_CUBE_MAP ();
      statecache->SetTexture (GL_TEXTURE_CUBE_MAP, 0);
      break;
    case iTextureHandle::texTypeRect:
      statecache->Disable_GL_TEXTURE_RECTANGLE_ARB ();
      statecache->SetTexture (GL_TEXTURE_RECTANGLE_ARB, 0);
      break;
    default:
      break;
  }

  imageUnits[unit].texture = 0;
}

void csGLGraphics3D::SetTextureState (int* units, iTextureHandle** textures,
	int count)
{
  if (!textures)
  {
    for (int i = 0 ; i < count ; i++)
    {
      int unit = units[i];
      DeactivateTexture (unit);
    }
  }
  else
  {
    for (int i = 0 ; i < count ; i++)
    {
      int unit = units[i];
      iTextureHandle* txt = textures[i];
      if (txt)
	ActivateTexture (txt, unit);
      else
	DeactivateTexture (unit);
    }
  }
}
  
void csGLGraphics3D::SetTextureComparisonModes (int* units,
  CS::Graphics::TextureComparisonMode* modes, int count)
{
  if (modes == 0)
  {
    CS::Graphics::TextureComparisonMode modeDisabled;
    for (int i = 0 ; i < count ; i++)
    {
      int unit = units[i];
      
      if (imageUnits[unit].texture == 0) continue;
      
      if (ext->CS_GL_ARB_multitexture)
      {
	statecache->SetCurrentImageUnit (unit);
	statecache->ActivateImageUnit ();
      }
      else if (unit != 0) continue;
      
      imageUnits[unit].texture->ChangeTextureCompareMode (modeDisabled);
    }
  }
  else
  {
    for (int i = 0 ; i < count ; i++)
    {
      int unit = units[i];
      if (imageUnits[unit].texture == 0) continue;
      
      if (ext->CS_GL_ARB_multitexture)
      {
	statecache->SetCurrentImageUnit (unit);
	statecache->ActivateImageUnit ();
      }
      else if (unit != 0) continue;
      
      imageUnits[unit].texture->ChangeTextureCompareMode (modes[i]);
    }
  }
}

GLvoid csGLGraphics3D::myDrawRangeElements (GLenum mode, GLuint /*start*/, 
    GLuint /*end*/, GLsizei count, GLenum type, const GLvoid* indices)
{
  glDrawElements (mode, count, type, indices);
}

void csGLGraphics3D::SetWorldToCamera (const csReversibleTransform& w2c)
{
  GLRENDER3D_OUTPUT_LOCATION_MARKER;

  world2camera = w2c;
  float m[16];

  shadermgr->GetVariableAdd (string_world2camera)->SetValue (w2c);
  shadermgr->GetVariableAdd (string_world2camera_inv)->SetValue (w2c.GetInverse());

  makeGLMatrix (world2camera, m);
  statecache->SetMatrixMode (GL_MODELVIEW);
  glLoadMatrixf (m);
}

void csGLGraphics3D::SetupInstance (size_t instParamNum, 
                                    const csVertexAttrib targets[], 
                                    csShaderVariable* const params[])
{
  csVector4 v;
  float matrix[16];
  bool uploadMatrix;
  for (size_t n = 0; n < instParamNum; n++)
  {
    csShaderVariable* param = params[n];
    csVertexAttrib target = targets[n];
    switch (param->GetType())
    {
    case csShaderVariable::MATRIX:
      {
        csMatrix3 m;
        params[n]->GetValue (m);
        makeGLMatrix (m, matrix, target != CS_IATTRIB_OBJECT2WORLD);
        uploadMatrix = true;
      }
      break;
    case csShaderVariable::TRANSFORM:
      {
        csReversibleTransform tf;
        params[n]->GetValue (tf);
        makeGLMatrix (tf, matrix, target != CS_IATTRIB_OBJECT2WORLD);
        uploadMatrix = true;
      }
      break;
    default:
      uploadMatrix = false;
    }
    if (uploadMatrix)
    {
      switch (target)
      {
      case CS_IATTRIB_OBJECT2WORLD:
        {
          statecache->SetMatrixMode (GL_MODELVIEW);
          glPushMatrix ();
          glMultMatrixf (matrix);
        }
        break;
      default:
        if (ext->CS_GL_ARB_multitexture)
        {
          if ((target >= CS_VATTRIB_TEXCOORD0) 
            && (target <= CS_VATTRIB_TEXCOORD7))
          {
            // numTCUnits is type GLint, while target is an enumerated
            // type. These are not necessarily the same.
            size_t maxN = csMin (3,csVertexAttrib(numTCUnits) - (target - CS_VATTRIB_TEXCOORD0));
            GLenum tu = GL_TEXTURE0 + (target - CS_VATTRIB_TEXCOORD0);
            for (size_t n = 0; n < maxN; n++)
            {
              ext->glMultiTexCoord4fvARB (tu + n, &matrix[n*4]);
            }
          }
        }
        if (ext->glVertexAttrib4fvARB)
        {
          if (CS_VATTRIB_IS_GENERIC (target))
          {
            size_t maxN = csMin (3, CS_VATTRIB_GENERIC_LAST - target + 1);
            GLenum attr = (target - CS_VATTRIB_GENERIC_FIRST);
            for (size_t n = 0; n < maxN; n++)
            {
              ext->glVertexAttrib4fvARB (attr + n, &matrix[n*4]);
            }
          }
        }
      }
    }
    else
    {
      params[n]->GetValue (v);
      switch (target)
      {
      case CS_VATTRIB_WEIGHT:
        if (ext->CS_GL_EXT_vertex_weighting)
          ext->glVertexWeightfvEXT (v.m);
        break;
      case CS_VATTRIB_NORMAL:
        glNormal3fv (v.m);
        break;
      case CS_VATTRIB_PRIMARY_COLOR:
        glColor4fv (v.m);
        break;
      case CS_VATTRIB_SECONDARY_COLOR:
        if (ext->CS_GL_EXT_secondary_color)
          ext->glSecondaryColor3fvEXT (v.m);
        break;
      case CS_VATTRIB_FOGCOORD:
        if (ext->CS_GL_EXT_fog_coord)
          ext->glFogCoordfvEXT (v.m);
        break;
      case CS_IATTRIB_OBJECT2WORLD:
        {
          statecache->SetMatrixMode (GL_MODELVIEW);
          glPushMatrix ();
        }
        break;
      default:
        if (ext->CS_GL_ARB_multitexture)
        {
          if ((target >= CS_VATTRIB_TEXCOORD0) 
            && (target <= CS_VATTRIB_TEXCOORD7))
            ext->glMultiTexCoord4fvARB (
            GL_TEXTURE0 + (target - CS_VATTRIB_TEXCOORD0), 
            v.m);
        }
        else if (target == CS_VATTRIB_TEXCOORD)
        {
          glTexCoord4fv (v.m);
        }
        if (ext->glVertexAttrib4fvARB)
        {
          if (CS_VATTRIB_IS_GENERIC (target))
            ext->glVertexAttrib4fvARB (target - CS_VATTRIB_0,
            v.m);
        }
      }
    }
  }
}

void csGLGraphics3D::TeardownInstance (size_t instParamNum, 
                                       const csVertexAttrib targets[])
{
  for (size_t n = 0; n < instParamNum; n++)
  {
    csVertexAttrib target = targets[n];
    switch (target)
    {
    case CS_IATTRIB_OBJECT2WORLD:
      {
        statecache->SetMatrixMode (GL_MODELVIEW);
        glPopMatrix ();
      }
      break;
    default:
      /* Nothing to do */
      break;
    }
  }
}

void csGLGraphics3D::DrawMesh (const csCoreRenderMesh* mymesh,
    const csRenderMeshModes& modes,
    const csShaderVariableStack& stacks)
{
  if (cliptype == CS_CLIPPER_EMPTY) 
    return;

  CS_PROFILER_ZONE(csGLGraphics3D_DrawMesh);

  GLRENDER3D_OUTPUT_STRING_MARKER(("%p ('%s')", mymesh, mymesh->db_mesh_name));
  SwapIfNeeded();

  SetupProjection ();

  SetupClipper (mymesh->clip_portal, 
                mymesh->clip_plane, 
                mymesh->clip_z_plane,
		(mymesh->indexend-mymesh->indexstart)/3);
  if (debug_inhibit_draw) 
    return;

  const csReversibleTransform& o2w = mymesh->object2world;

  bool needMatrix = !o2w.IsIdentity();
  if (needMatrix)
  {
    float matrix[16];
    makeGLMatrix (o2w, matrix);
    statecache->SetMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glMultMatrixf (matrix);
  }

  ApplyBufferChanges();

  iRenderBuffer* iIndexbuf = (modes.buffers
  	? modes.buffers->GetRenderBuffer(CS_BUFFER_INDEX)
	: 0);

  if (!iIndexbuf)
  {
    csShaderVariable* indexBufSV = csGetShaderVariableFromStack (stacks, string_indices);
    CS_ASSERT (indexBufSV);
    indexBufSV->GetValue (iIndexbuf);
    CS_ASSERT(iIndexbuf);
  }
  
  const size_t indexCompsBytes = 
    csRenderBufferComponentSizes[iIndexbuf->GetComponentType()];
  CS_ASSERT_MSG("Expecting index buffers to have only 1 component",
    (iIndexbuf->GetComponentCount() == 1));
  if (!(mymesh->multiRanges && mymesh->rangesNum))
  {
    CS_ASSERT((indexCompsBytes * mymesh->indexstart) <= iIndexbuf->GetSize());
    CS_ASSERT((indexCompsBytes * mymesh->indexend) <= iIndexbuf->GetSize());
  }

  GLenum primitivetype = GL_TRIANGLES;
  int primNum_divider = 1, primNum_sub = 0;
  switch (mymesh->meshtype)
  {
    case CS_MESHTYPE_QUADS:
      primNum_divider = 2;
      primitivetype = GL_QUADS;
      break;
    case CS_MESHTYPE_TRIANGLESTRIP:
      primNum_sub = 2;
      primitivetype = GL_TRIANGLE_STRIP;
      break;
    case CS_MESHTYPE_TRIANGLEFAN:
      primNum_sub = 2;
      primitivetype = GL_TRIANGLE_FAN;
      break;
    case CS_MESHTYPE_POINTS:
      primitivetype = GL_POINTS;
      break;
    case CS_MESHTYPE_POINT_SPRITES:
    {
      if(!(ext->CS_GL_ARB_point_sprite && ext->CS_GL_ARB_point_parameters))
      {
        break;
      }
      primitivetype = GL_POINTS;
      break;
    }
    case CS_MESHTYPE_LINES:
      primNum_divider = 2;
      primitivetype = GL_LINES;
      break;
    case CS_MESHTYPE_LINESTRIP:
      primNum_sub = 1;
      primitivetype = GL_LINE_STRIP;
      break;
    case CS_MESHTYPE_TRIANGLES:
    default:
      primNum_divider = 3;
      primitivetype = GL_TRIANGLES;
      break;
  }
  if (ext->CS_GL_ARB_point_sprite)
  {
    if (primitivetype == GL_POINTS)
      statecache->Enable_GL_POINT_SPRITE_ARB();
    else
      statecache->Disable_GL_POINT_SPRITE_ARB();
  }

  // Based on the kind of clipping we need we set or clip mask.
  int clip_mask, clip_value;
  if (clipportal_floating)
  {
    clip_mask = stencil_clip_mask;
    clip_value = stencil_clip_value;
  }
  else if (clipping_stencil_enabled)
  {
    clip_mask = stencil_clip_mask;
    clip_value = 0;
  }
  else
  {
    clip_mask = 0;
    clip_value = 0;
  }

  switch (current_shadow_state)
  {
    case CS_SHADOW_VOLUME_PASS1:
      statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_INCR);
      statecache->SetStencilFunc (GL_ALWAYS, clip_value, clip_mask);
      break;
    case CS_SHADOW_VOLUME_FAIL1:
      statecache->SetStencilOp (GL_KEEP, GL_INCR, GL_KEEP);
      statecache->SetStencilFunc (GL_ALWAYS, clip_value, clip_mask);
      break;
    case CS_SHADOW_VOLUME_PASS2:
      statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_DECR);
      statecache->SetStencilFunc (GL_ALWAYS, clip_value, clip_mask);
      break;
    case CS_SHADOW_VOLUME_FAIL2:
      statecache->SetStencilOp (GL_KEEP, GL_DECR, GL_KEEP);
      statecache->SetStencilFunc (GL_ALWAYS, clip_value, clip_mask);
      break;
    case CS_SHADOW_VOLUME_USE:
      statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
      statecache->SetStencilFunc (GL_EQUAL, clip_value, stencil_shadow_mask
      	| clip_mask);
      break;
    default:
      if (clip_mask)
      {
        statecache->SetStencilFunc (GL_EQUAL, clip_value, clip_mask);
        statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
      }
  }

  bool mirrorflag;
  if (current_shadow_state == CS_SHADOW_VOLUME_PASS2 ||
      current_shadow_state == CS_SHADOW_VOLUME_FAIL1)
    mirrorflag = !mymesh->do_mirror;
  else
    mirrorflag = mymesh->do_mirror;
    
  GLenum cullFace;
  statecache->GetCullFace (cullFace);
    
  CS::Graphics::MeshCullMode cullMode = modes.cullMode;
  // Flip face culling if we do mirroring
  if (mirrorflag)
    cullMode = CS::Graphics::GetFlippedCullMode (cullMode);
  
  if (cullMode == CS::Graphics::cullDisabled)
  {
    statecache->Disable_GL_CULL_FACE ();
  }
  else
  {
    statecache->Enable_GL_CULL_FACE ();
    
    // Flip culling if shader wants it
    if (cullMode == CS::Graphics::cullFlipped)
      statecache->SetCullFace ((cullFace == GL_FRONT) ? GL_BACK : GL_FRONT);
  }

  const uint mixmode = modes.mixmode;
  statecache->SetShadeModel ((mixmode & CS_FX_FLAT) ? GL_FLAT : GL_SMOOTH);

  if (modes.zoffset)
    statecache->Enable_GL_POLYGON_OFFSET_FILL ();
  else
    statecache->Disable_GL_POLYGON_OFFSET_FILL ();

  GLenum compType = compGLtypes[iIndexbuf->GetComponentType()];
  void* bufData =
    RenderLock (iIndexbuf, CS_GLBUF_RENDERLOCK_ELEMENTS);
  statecache->ApplyBufferBinding (csGLStateCacheContext::boIndexArray);
  if (bufData != (void*)-1)
  {
    SetMixMode (mixmode, modes.alphaType, modes.alphaTest);

    if ((current_zmode == CS_ZBUF_MESH) || (current_zmode == CS_ZBUF_MESH2))
    {
      CS_ASSERT_MSG ("Meshes can't have zmesh zmode. You deserve some spanking", 
        (modes.z_buf_mode != CS_ZBUF_MESH) && 
        (modes.z_buf_mode != CS_ZBUF_MESH2));
      SetZModeInternal ((current_zmode == CS_ZBUF_MESH2) ? 
        GetZModePass2 (modes.z_buf_mode) : modes.z_buf_mode);
      /*if (current_zmode == CS_ZBUF_MESH2)
      {
      glPolygonOffset (0.15f, 6.0f); 
      statecache->Enable_GL_POLYGON_OFFSET_FILL ();
      }*/
    }

    {
      CS_PROFILER_ZONE(csGLGraphics3D_DrawMesh_DrawElements);
      if (mymesh->multiRanges && mymesh->rangesNum)
      {
        size_t num_tri = 0;
        for (size_t r = 0; r < mymesh->rangesNum; r++)
        {
          CS::Graphics::RenderMeshIndexRange range = mymesh->multiRanges[r];
          if (bugplug) num_tri += (range.end-range.start)/primNum_divider - primNum_sub;
          glDrawRangeElements (primitivetype, (GLuint)iIndexbuf->GetRangeStart(), 
            (GLuint)iIndexbuf->GetRangeEnd(), range.end - range.start,
            compType, 
            ((uint8*)bufData) + (indexCompsBytes * range.start));
        }
        if (bugplug)
        {
          bugplug->AddCounter ("Triangle Count", (int)num_tri);
          bugplug->AddCounter ("Mesh Count", 1);
        }
      }
      else
      {
        if (bugplug)
        {
          size_t num_tri = (mymesh->indexend-mymesh->indexstart)/primNum_divider - primNum_sub;
          bugplug->AddCounter ("Triangle Count", (int)num_tri);
          bugplug->AddCounter ("Mesh Count", 1);
        }

        if (modes.doInstancing)
        {
          const size_t instParamNum = modes.instParamNum;
          const csVertexAttrib* const instParamsTargets = modes.instParamsTargets;
          for (size_t n = 0; n < modes.instanceNum; n++)
          {
            SetupInstance (instParamNum, instParamsTargets, modes.instParams[n]);
            glDrawRangeElements (primitivetype, (GLuint)iIndexbuf->GetRangeStart(), 
              (GLuint)iIndexbuf->GetRangeEnd(), mymesh->indexend - mymesh->indexstart,
              compType, 
              ((uint8*)bufData) + (indexCompsBytes * mymesh->indexstart));
            TeardownInstance (instParamNum, instParamsTargets);
          }
        }
        else
        {
          // @@@ Temporary comment. If runnung Ubuntu 8.04 on a machine with Intel
          // hardware and you get an error that traces back to the function below.
          // Please see: http://trac.crystalspace3d.org/trac/CS/ticket/551 in the
          // first instance.
          glDrawRangeElements (primitivetype, (GLuint)iIndexbuf->GetRangeStart(), 
            (GLuint)iIndexbuf->GetRangeEnd(), mymesh->indexend - mymesh->indexstart,
            compType, 
            ((uint8*)bufData) + (indexCompsBytes * mymesh->indexstart));
        }
      }
    }
  }

  if (needMatrix)
    glPopMatrix ();
  //indexbuf->RenderRelease ();
  RenderRelease (iIndexbuf);
  // Restore cull mode
  if (cullMode == CS::Graphics::cullFlipped) statecache->SetCullFace (cullFace);
  //statecache->Disable_GL_POLYGON_OFFSET_FILL ();
}

void csGLGraphics3D::DrawPixmap (iTextureHandle *hTex,
  int sx, int sy, int sw, int sh, 
  int tx, int ty, int tw, int th, uint8 Alpha)
{
  SwapIfNeeded();

  /*
    @@@ DrawPixmap is called in 2D mode quite often.
    To reduce state changes, the text drawing states are reset as late
    as possible. The 2D canvas methods call a routine to flush all text to
    the screen, do the same here.
   */
  G2D->PerformExtension ("glflushtext");

  if (current_drawflags & CSDRAW_3DGRAPHICS)
    DeactivateBuffers (0, 0);

  if (drawPixmapAFP)
  {
    ext->glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, drawPixmapProgram);
    glEnable (GL_FRAGMENT_PROGRAM_ARB);
  }

  // If original dimensions are different from current dimensions (because
  // image has been scaled to conform to OpenGL texture size restrictions)
  // we correct the input coordinates here.
  int bitmapwidth = 0, bitmapheight = 0;
  hTex->GetRendererDimensions (bitmapwidth, bitmapheight);
  csGLBasicTextureHandle *txt_mm = static_cast<csGLBasicTextureHandle*> (
    (iTextureHandle*)hTex);
  int owidth = txt_mm->orig_width;
  int oheight = txt_mm->orig_height;
  if (owidth != bitmapwidth || oheight != bitmapheight)
  {
    tx = (int)(tx * (float)bitmapwidth  / (float)owidth );
    ty = (int)(ty * (float)bitmapheight / (float)oheight);
    tw = (int)(tw * (float)bitmapwidth  / (float)owidth );
    th = (int)(th * (float)bitmapheight / (float)oheight);
  }

  // cache the texture if we haven't already.
  hTex->Precache ();

  // as we are drawing in 2D, we disable some of the commonly used features
  // for fancy 3D drawing
  statecache->SetShadeModel (GL_FLAT);
  SetZModeInternal (CS_ZBUF_NONE);
  //@@@???statecache->SetDepthMask (GL_FALSE);

  // if the texture has transparent bits, we have to tweak the
  // OpenGL blend mode so that it handles the transparent pixels correctly
  if ((hTex->GetKeyColor () || Alpha) ||
    (current_drawflags & CSDRAW_2DGRAPHICS)) // In 2D mode we always want to blend
    SetMixMode (CS_FX_ALPHA, hTex->GetAlphaType(),
      CS::Graphics::AlphaTestOptions());
  else
    SetMixMode (CS_FX_COPY, hTex->GetAlphaType(),
      CS::Graphics::AlphaTestOptions());

  glColor4f (1.0, 1.0, 1.0, Alpha ? (1.0 - BYTE_TO_FLOAT (Alpha)) : 1.0);
  ActivateTexture (hTex);

  // convert texture coords given above to normalized (0-1.0) texture
  // coordinates
  float ntx1,nty1,ntx2,nty2;
  ntx1 = ((float)tx            );
  ntx2 = ((float)tx + (float)tw);
  nty1 = ((float)ty            );
  nty2 = ((float)ty + (float)th);
  if (txt_mm->texType != iTextureHandle::texTypeRect)
  {
    ntx1 /= bitmapwidth;
    ntx2 /= bitmapwidth;
    nty1 /= bitmapheight;
    nty2 /= bitmapheight;
  }

  // draw the bitmap
  glBegin (GL_QUADS);
  //    glTexCoord2f (ntx1, nty1);
  //    glVertex2i (sx, height - sy - 1);
  //    glTexCoord2f (ntx2, nty1);
  //    glVertex2i (sx + sw, height - sy - 1);
  //    glTexCoord2f (ntx2, nty2);
  //    glVertex2i (sx + sw, height - sy - sh - 1);
  //    glTexCoord2f (ntx1, nty2);
  //    glVertex2i (sx, height - sy - sh - 1);

  // smgh: This works in software opengl.
  // wouter: removed height-sy-1 to be height-sy.
  //    this is because on opengl y=0.0 is off screen, as is y=height.
  //    using height-sy gives output on screen which is identical to 
  //    using the software canvas.
  glTexCoord2f (ntx1, nty1);
  glVertex2i (sx, viewheight - sy);
  glTexCoord2f (ntx2, nty1);
  glVertex2i (sx + sw, viewheight - sy);
  glTexCoord2f (ntx2, nty2);
  glVertex2i (sx + sw, viewheight - (sy + sh));
  glTexCoord2f (ntx1, nty2);
  glVertex2i (sx, viewheight - (sy + sh));
  glEnd ();

  // Restore.
  SetZModeInternal (current_zmode);
  DeactivateTexture ();
  if (drawPixmapAFP)
    glDisable (GL_FRAGMENT_PROGRAM_ARB);
}

void csGLGraphics3D::SetShadowState (int state)
{
  switch (state)
  {
    case CS_SHADOW_VOLUME_BEGIN:
      current_shadow_state = CS_SHADOW_VOLUME_BEGIN;
      stencil_initialized = false;
      glClearStencil (0);
      glClear (GL_STENCIL_BUFFER_BIT);
      EnableStencilShadow ();
      //statecache->SetStencilFunc (GL_ALWAYS, 0, 127);
      //statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
      // @@@ Jorrit: to avoid flickering I had to increase the
      // values below and multiply them with 3.
      //glPolygonOffset (-0.1f, -4.0f); 
      glPolygonOffset (0.3f, 12.0f); 
      statecache->Enable_GL_POLYGON_OFFSET_FILL ();
      break;
    case CS_SHADOW_VOLUME_PASS1:
      current_shadow_state = CS_SHADOW_VOLUME_PASS1;
      break;
    case CS_SHADOW_VOLUME_FAIL1:
      current_shadow_state = CS_SHADOW_VOLUME_FAIL1;
      break;
    case CS_SHADOW_VOLUME_PASS2:
      current_shadow_state = CS_SHADOW_VOLUME_PASS2;
      break;
    case CS_SHADOW_VOLUME_FAIL2:
      current_shadow_state = CS_SHADOW_VOLUME_FAIL2;
      break;
    case CS_SHADOW_VOLUME_USE:
      current_shadow_state = CS_SHADOW_VOLUME_USE;
      statecache->Disable_GL_POLYGON_OFFSET_FILL ();
      break;
    case CS_SHADOW_VOLUME_FINISH:
      current_shadow_state = 0;
      DisableStencilShadow ();
      break;
  }
}

void csGLGraphics3D::DebugVisualizeStencil (uint32 mask)
{
  statecache->Enable_GL_STENCIL_TEST ();

  statecache->SetStencilMask (mask);
  statecache->SetStencilFunc (GL_EQUAL, 0xff, mask);
  statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
  glScissor (0, 0, 640, 480);
  statecache->Disable_GL_TEXTURE_2D ();
  statecache->SetShadeModel (GL_FLAT);

  SetZModeInternal (CS_ZBUF_FILL);
  glColor4f (1, 1, 1, 0);

  statecache->SetMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  statecache->SetMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();

  glBegin (GL_QUADS);
  glVertex3f (-1.0f, 1.0f, 1.0f);
  glVertex3f (1.0f, 1.0f, 1.0f);
  glVertex3f (1.0f, -1.0f, 1.0f);
  glVertex3f (-1.0f, -1.0f, 1.0f);
  glEnd ();

  glPopMatrix ();
  statecache->SetMatrixMode (GL_PROJECTION);
  glPopMatrix ();

  SetZModeInternal (current_zmode);
  SetCorrectStencilState ();
}

void csGLGraphics3D::OpenPortal (size_t numVertices, 
				 const csVector2* vertices,
				 const csPlane3& normal,
				 csFlags flags)
{
  csClipPortal* cp = new csClipPortal ();
  GLRENDER3D_OUTPUT_STRING_MARKER(("portal = %p, flags = %s", cp,
    csBitmaskToString::GetStr (flags.Get(), openPortalFlags)));
  cp->poly = new csVector2[numVertices];
  memcpy (cp->poly, vertices, numVertices * sizeof (csVector2));
  cp->num_poly = (int)numVertices;
  cp->normal = normal;
  cp->flags = flags;
  cp->status = 0;
  clipportal_stack.Push (cp);
  clipportal_dirty = true;

  // If we already have a floating portal then we increase the
  // number & mark new portal as floating too. Otherwise we start at one.
  if (clipportal_floating)
  {
    clipportal_floating++;
    cp->flags.Set(CS_OPENPORTAL_FLOAT);
  }
  else if (flags.Check(CS_OPENPORTAL_FLOAT))
    clipportal_floating = 1;
    
  //if (clipportal_stack.GetSize () > 1) debug_inhibit_draw = true;
}

void csGLGraphics3D::ClosePortal ()
{
  if (clipportal_stack.GetSize () <= 0) return;
  bool mirror = IsPortalMirrored (clipportal_stack.GetSize ()-1);
  csClipPortal* cp = clipportal_stack.Pop ();
  GLRENDER3D_OUTPUT_STRING_MARKER(("portal %p", cp));

  if (cp->status.Check(CS_PORTALSTATUS_SFILLED) || cp->flags.Check(CS_OPENPORTAL_ZFILL))
  {
    // Store glstate and setup matrices for 2D drawing
    statecache->SetMatrixMode (GL_PROJECTION);
    glPushMatrix ();
    glLoadIdentity ();
    statecache->SetMatrixMode (GL_MODELVIEW);
    glPushMatrix ();
    glLoadIdentity ();
    
    GLboolean wmRed, wmGreen, wmBlue, wmAlpha;
    statecache->GetColorMask (wmRed, wmGreen, wmBlue, wmAlpha);
    statecache->SetColorMask (false, false, false, false);
    
    GLenum oldcullface;
    statecache->GetCullFace (oldcullface);
    if (currentAttachments != 0)
    {
      r2tbackend->SetupClipPortalDrawing ();
      statecache->SetCullFace (mirror?GL_FRONT:GL_BACK);
    }
    else    
      statecache->SetCullFace (mirror?GL_BACK:GL_FRONT);
    
    bool tex2d = statecache->IsEnabled_GL_TEXTURE_2D ();
    statecache->Disable_GL_TEXTURE_2D ();
    statecache->SetShadeModel (GL_FLAT);
        
    //Fill Z here, if required (assumed stencil is correct)
    if (cp->flags.Check(CS_OPENPORTAL_ZFILL))
    {
      SetZModeInternal (CS_ZBUF_USE);
      Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
    }
    
    if (cp->status.Check(CS_PORTALSTATUS_SFILLED))
    {
      //clear stencil for floating portal
      statecache->SetStencilFunc (GL_ALWAYS, 0, stencil_clip_mask);
      statecache->SetStencilOp (GL_ZERO, GL_ZERO, GL_ZERO);
      SetZModeInternal (CS_ZBUF_NONE);
   
      //Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
      DrawScreenPolygon (cp->poly, cp->num_poly);
    }
    
    //restore glstate and matrices
    statecache->SetMatrixMode (GL_MODELVIEW);
    glPopMatrix ();
    statecache->SetMatrixMode (GL_PROJECTION);
    glPopMatrix ();
    
    statecache->SetCullFace (oldcullface);
    statecache->SetColorMask (wmRed, wmGreen, wmBlue, wmAlpha);
    if (tex2d) statecache->Enable_GL_TEXTURE_2D ();
    SetZModeInternal (current_zmode);
  }
  
  delete cp;
  clipportal_dirty = true;
  if (clipportal_floating > 0)
    clipportal_floating--;
    
  //if (clipportal_stack.GetSize () < 2) debug_inhibit_draw = false;
}

void* csGLGraphics3D::RenderLock (iRenderBuffer* buffer, 
				  csGLRenderBufferLockType type)
{
  if (vboManager.IsValid())
    return vboManager->RenderLock (buffer, type);
  else
  {
    void* data;
    iRenderBuffer* master;
    if ((master = buffer->GetMasterBuffer()) != 0)
      data = master->Lock (CS_BUF_LOCK_READ);
    else
      data = buffer->Lock (CS_BUF_LOCK_READ);
    if (data == (void*)-1) return (void*)-1;
    return ((uint8*)data + buffer->GetOffset());
  }
}

void csGLGraphics3D::RenderRelease (iRenderBuffer* buffer)
{
  if (buffer == 0) return;

  if (vboManager.IsValid())
    vboManager->RenderRelease (buffer);
  else
  {
    iRenderBuffer* master;
    if ((master = buffer->GetMasterBuffer()) != 0)
      master->Release();
    else
      buffer->Release();
  }
}

void csGLGraphics3D::ApplyBufferChanges()
{
  GLRENDER3D_OUTPUT_LOCATION_MARKER;
  
  for (size_t i = 0; i < changeQueue.GetSize (); i++)
  {
    const BufferChange& changeEntry = changeQueue[i];
    csVertexAttrib att = changeEntry.attrib;

    if (changeEntry.buffer.IsValid())
    {
      iRenderBuffer *buffer = changeEntry.buffer;

      if (CS_VATTRIB_IS_GENERIC (att)) 
        AssignGenericBuffer (att-CS_VATTRIB_GENERIC_FIRST, buffer);
      else               
        AssignSpecBuffer (att-CS_VATTRIB_SPECIFIC_FIRST, buffer);      

      csRenderBufferComponentType compType = buffer->GetComponentType();
      csRenderBufferComponentType compTypeBase = csRenderBufferComponentType(compType & ~CS_BUFCOMP_NORMALIZED);
      bool isFloat = (compType == CS_BUFCOMP_FLOAT) 
	|| (compType == CS_BUFCOMP_DOUBLE);
      bool normalized = !isFloat && (compType & CS_BUFCOMP_NORMALIZED);

      /* Normalization/data type fixup:
         Specialized attribs treat vertex data as generally either normalized
         or not normalized. If the actual data doesn't fit that conversion
         is needed.
         The specialized vertex array functions also don't support all data
         types. */
      const uint wants_short_int =
        (1 << CS_BUFCOMP_SHORT) | (1 << CS_BUFCOMP_INT);
      const uint wants_byte_short_int = wants_short_int |
        (1 << CS_BUFCOMP_BYTE);
      switch (att)
      {
      case CS_VATTRIB_POSITION:
	if (!isFloat && (normalized
	    || (((1 << compTypeBase) & wants_short_int) == 0)))
        {
          // Set up shadow buffer
          buffer = bufferShadowDataHelper->GetSupportedRenderBuffer (buffer);
          compType = buffer->GetComponentType();
        }
        break;
      case CS_VATTRIB_NORMAL:
	if (!isFloat && (!normalized
	    || (((1 << compTypeBase) & wants_byte_short_int) == 0)))
        {
          // Set up shadow buffer
          buffer = bufferShadowDataHelper->GetSupportedRenderBuffer (buffer);
          compType = buffer->GetComponentType();
        }
        break;
      case CS_VATTRIB_COLOR:
        if (!isFloat && !normalized)
        {
          // Set up shadow buffer
          buffer = bufferShadowDataHelper->GetSupportedRenderBuffer (buffer);
          compType = buffer->GetComponentType();
        }
        break;
      case CS_VATTRIB_SECONDARY_COLOR:
        if (ext->CS_GL_EXT_secondary_color)
        {
	  if (!isFloat && !normalized)
	  {
	    // Set up shadow buffer
	    buffer = bufferShadowDataHelper->GetSupportedRenderBuffer (buffer);
	    compType = buffer->GetComponentType();
	  }
        }
        break;
      default:
        if (att >= CS_VATTRIB_TEXCOORD0 && att <= CS_VATTRIB_TEXCOORD7)
        {
	if (!isFloat && (normalized
	    || (((1 << compTypeBase) & wants_short_int) == 0)))
	  {
	    // Set up shadow buffer
	    buffer = bufferShadowDataHelper->GetSupportedRenderBuffer (buffer);
	    compType = buffer->GetComponentType();
	  }
        }
      }
	
      GLenum compGLType = compGLtypes[compType];
      void *data = RenderLock (buffer, CS_GLBUF_RENDERLOCK_ARRAY);

      if (data == (void*)-1) continue;

      switch (att)
      {
      case CS_VATTRIB_POSITION:
        statecache->Enable_GL_VERTEX_ARRAY ();
        statecache->SetVertexPointer (buffer->GetComponentCount (),
          compGLType, (GLsizei)buffer->GetStride (), data);
        break;
      case CS_VATTRIB_NORMAL:
	statecache->Enable_GL_NORMAL_ARRAY ();
        statecache->SetNormalPointer (compGLType, (GLsizei)buffer->GetStride (), 
          data);
        break;
      case CS_VATTRIB_COLOR:
	statecache->Enable_GL_COLOR_ARRAY ();
        statecache->SetColorPointer (buffer->GetComponentCount (),
          compGLType, (GLsizei)buffer->GetStride (), data);
        break;
      case CS_VATTRIB_SECONDARY_COLOR:
        if (ext->CS_GL_EXT_secondary_color)
        {
	  statecache->Enable_GL_SECONDARY_COLOR_ARRAY_EXT ();
	  statecache->SetSecondaryColorPointerExt (buffer->GetComponentCount (),
	    compGLType, (GLsizei)buffer->GetStride (), data);
        }
        break;
      default:
        if (att >= CS_VATTRIB_TEXCOORD0 && att <= CS_VATTRIB_TEXCOORD7)
        {
          //texcoord
          unsigned int unit = att- CS_VATTRIB_TEXCOORD0;
          if (ext->CS_GL_ARB_multitexture)
          {
            statecache->SetCurrentTCUnit (unit);
          } 
	  statecache->Enable_GL_TEXTURE_COORD_ARRAY ();
          statecache->SetTexCoordPointer (buffer->GetComponentCount (),
            compGLType, (GLsizei)buffer->GetStride (), data);
        }
        else if (CS_VATTRIB_IS_GENERIC(att))
        {
	  if (ext->glEnableVertexAttribArrayARB)
	  {
	    GLuint index = att - CS_VATTRIB_GENERIC_FIRST;
	    statecache->ApplyBufferBinding (csGLStateCacheContext::boElementArray);
	    if (!(activeVertexAttribs & (1 << index)))
	    {
	      ext->glEnableVertexAttribArrayARB (index);
	      activeVertexAttribs |= (1 << index);
	    }
	    ext->glVertexAttribPointerARB(index, buffer->GetComponentCount (),
              compGLType, normalized, (GLsizei)buffer->GetStride (), data);
	  }
        }
        else
        {
          //none, assert...
          CS_ASSERT_MSG("Unknown vertex attribute", 0);
        }
      }
    }
    else
    {
      switch (att)
      {
      case CS_VATTRIB_POSITION:
        statecache->Disable_GL_VERTEX_ARRAY ();
        break;
      case CS_VATTRIB_NORMAL:
        statecache->Disable_GL_NORMAL_ARRAY ();
        break;
      case CS_VATTRIB_COLOR:
        statecache->Disable_GL_COLOR_ARRAY ();
        break;
      case CS_VATTRIB_SECONDARY_COLOR:
        if (ext->CS_GL_EXT_secondary_color)
	  statecache->Disable_GL_SECONDARY_COLOR_ARRAY_EXT ();
        break;
      default:
        if (att >= CS_VATTRIB_TEXCOORD0 && att <= CS_VATTRIB_TEXCOORD7)
        {
          //texcoord
          unsigned int unit = att- CS_VATTRIB_TEXCOORD0;
          if (ext->CS_GL_ARB_multitexture)
          {
            statecache->SetCurrentTCUnit (unit);
          }
          statecache->Disable_GL_TEXTURE_COORD_ARRAY ();
        }
        else if (CS_VATTRIB_IS_GENERIC(att))
        {
	  if (ext->glDisableVertexAttribArrayARB)
	  {
	    GLuint index = att - CS_VATTRIB_GENERIC_FIRST;
	    if (activeVertexAttribs & (1 << index))
	    {
	      ext->glDisableVertexAttribArrayARB (index);
	      activeVertexAttribs &= ~(1 << index);
	    }
	  }
        }
        else
        {
          //none, assert...
          CS_ASSERT_MSG("Unknown vertex attribute", 0);
        }
      }
      if (CS_VATTRIB_IS_GENERIC (att))
      {
	uint index = att - CS_VATTRIB_GENERIC_FIRST;
        if (gen_renderBuffers[index]) 
        {
          RenderRelease (gen_renderBuffers[index]);
          gen_renderBuffers[index] = 0;
        }
      }
      else
      {
	uint index = att - CS_VATTRIB_SPECIFIC_FIRST;
        if (spec_renderBuffers[index]) 
        {
          RenderRelease (spec_renderBuffers[index]);
          spec_renderBuffers[index] = 0;
        }
      }
    }
  }
  changeQueue.Empty();
}

void csGLGraphics3D::Draw2DPolygon (csVector2* poly, int num_poly,
	const csPlane3& normal)
{
  SwapIfNeeded();

  // Get the plane normal of the polygon. Using this we can calculate
  // '1/z' at every screen space point.
  float M, N, O;
  float Dc = normal.D ();
  if (ABS (Dc) < 0.01f)
  {
    M = N = 0;
    O = 1;
  }
  else
  {
    float inv_Dc = 1.0f / Dc;
    M = -normal.A () * inv_Dc * inv_aspect;
    N = -normal.B () * inv_Dc * inv_aspect;
    O = -normal.C () * inv_Dc;
  }

  // Basically a GL ortho matrix
  const float P0 = 2.0f / (float)viewwidth;
  const float P5 = 2.0f / (float)viewheight;
  const float P10 = -2.0f / 11.0f;
  const float P11 = -9.0f / 11.0f;

  int v;
  glBegin (GL_TRIANGLE_FAN);
  csVector2* vt = poly;
  for (v = 0 ; v < num_poly ; v++)
  {
    float sx = vt->x - asp_center_x;
    float sy = vt->y - asp_center_y;
    float one_over_sz = M * sx + N * sy + O;
    float sz = 1.0f / one_over_sz;
    // This is what we would do if we'd use glOrtho():
    //glVertex4f (vt->x * sz, vt->y * sz, -1.0f, sz);

    // The vector that results from a GL ortho transform
    csVector4 bar ((vt->x * sz) * P0 - sz,
      (vt->y * sz) * P5 - sz,
      -P10 + sz * P11,
      sz);
    /* Now it can happen that a vertex of a polygon gets clipped when it's
     * very close to the near plane. In practice that causes sonme of the 
     * portal magic (stencil area setup, Z fill) to go wrong when the camera
     * is close to the portal. We "fix" this by checking whether the vertex
     * would get clipped and ... */
    const float bar_w = bar.w, minus_bar_w = -bar_w;
    if (/*(bar.x < minus_bar_w) || (bar.x > bar_w) 
      || (bar.y < minus_bar_w) || (bar.y > bar_w) 
      ||*/ (bar.z < minus_bar_w) || (bar.z > bar_w))
    {
      /* If yes, "fix" the vertex sent to GL by replacing the Z value with one
       * that won't cause clipping. */
      const float hackedZ = 1.0f - depth_epsilon;
      glVertex3f (bar.x/bar_w, bar.y/bar_w, hackedZ);
    }
    else
    {
      // If not, proceed as usual.
      glVertex4f (bar.x, bar.y, bar.z, bar.w);
    }
    vt++;
  }
  glEnd ();
}

void csGLGraphics3D::DrawScreenPolygon (csVector2* poly, int num_poly)
{
  SwapIfNeeded();
  float z = 1.0f - depth_epsilon;
  glBegin (GL_TRIANGLE_FAN);
  csVector2* vt = poly;
  for (int v = 0 ; v < num_poly ; v++)
  {
    glVertex3f (vt->x*2.0f/(float)viewwidth-1.0f, 
                vt->y*2.0f/(float)viewheight-1.0f, 
                z);
    vt++;
  }
  glEnd ();
}

void csGLGraphics3D::SwapIfNeeded()
{
  if (!enableDelaySwap) return;

  if (wantToSwap)
  {
    GLRENDER3D_OUTPUT_STRING_MARKER(("<< delayed swap >>"));
    G2D->Print (0);
    wantToSwap = false;
    if (delayClearFlags != 0)
    {
      GLRENDER3D_OUTPUT_STRING_MARKER(("<< delayed clear >>"));
      glClear (delayClearFlags);
      delayClearFlags = 0;
    }
  }
}

void csGLGraphics3D::SetupClipPortals ()
{
  if (broken_stencil || !stencil_clipping_available)
    return;
  size_t i;
  //index of first floating portal in stack
  size_t ffp;
  //index of current floating portal in stack (top)
  size_t cfp;
  //first floating portal which has no z-cleared status
  size_t ffpnz;
  //first floating portal which has s-filled status
  size_t ffps;
  //clipping portal to operate on
  csClipPortal* cp;
  
  //init portal indexes
  ffpnz = csArrayItemNotFound;
  ffps = csArrayItemNotFound; 
  cfp = clipportal_stack.GetSize ()-1; 
  for (ffp = 0; ffp <= cfp; ffp++) 
    if (clipportal_stack[ffp]->flags.Check (CS_OPENPORTAL_FLOAT)) break;
    
  for (i = ffp; i <= cfp; i++)
    if (!(clipportal_stack[i]->status.Check (CS_PORTALSTATUS_ZCLEARED)))
    {
      ffpnz = i;
      break;
    }
    
  for (i = ffp; i <= cfp; i++)
    if (clipportal_stack[i]->status.Check (CS_PORTALSTATUS_SFILLED))
    {
      ffps = i;
      break;
    }

  // Store glstate and setup matrices for 2D drawing
  statecache->SetMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  statecache->SetMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();
  //We should call render_target->SetupClipPortalDrawing just one time
  //because next call will flip modelview matrix again
  if (currentAttachments != 0)
    r2tbackend->SetupClipPortalDrawing ();
  
  GLboolean wmRed, wmGreen, wmBlue, wmAlpha;
  statecache->GetColorMask (wmRed, wmGreen, wmBlue, wmAlpha);
  statecache->SetColorMask (false, false, false, false);
  
  GLenum oldcullface;
  statecache->GetCullFace (oldcullface);
    
  GLboolean sciss = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_SCISSOR_TEST);
  
  bool tex2d = statecache->IsEnabled_GL_TEXTURE_2D ();
  statecache->Disable_GL_TEXTURE_2D ();
  statecache->SetShadeModel (GL_FLAT);
  
  // First set up the stencil area.
  statecache->Enable_GL_STENCIL_TEST ();
  statecache->SetStencilMask (stencil_clip_mask);
  
  // General idea is that before drawing first mesh we must:
  // 1) clear depth buffer under all opened portals. But just one time
  // for each. That is why we track z-cleared status of portals.
  // 2) setup clipping stencil for current portal.
  // We track s-filled status of portals to escape using glClear()
  // for stencil clearing
  // some of conditions below may never met but we keep them for
  // reliability

  if (ffpnz != csArrayItemNotFound)
  {
    //needed z-clear, but first we need s-fill for the same portal
    //glClear(GL_STENCIL_BUFFER_BIT);//TEMP!!!
    if ((ffps != csArrayItemNotFound) && (ffps != ffpnz))
    {
      if (ffps < ffpnz)
      {
        //clear stencil of ffps
        if (currentAttachments != 0)
        	statecache->SetCullFace (IsPortalMirrored(ffps)?GL_FRONT:GL_BACK);
        else 
          statecache->SetCullFace (IsPortalMirrored(ffps)?GL_BACK:GL_FRONT);
            
        statecache->SetStencilFunc (GL_ALWAYS, 0, stencil_clip_mask);
        statecache->SetStencilOp (GL_ZERO, GL_ZERO, GL_ZERO);
        SetZModeInternal (CS_ZBUF_NONE);
        
        cp = clipportal_stack[ffps];
        //Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
        DrawScreenPolygon (cp->poly, cp->num_poly);
      }
      clipportal_stack[ffps]->status.Reset(CS_PORTALSTATUS_SFILLED);
      ffps = csArrayItemNotFound;
    }
    if (ffps == csArrayItemNotFound)
    {
      //make s-fill of ffpnz
      if (currentAttachments != 0) 
        statecache->SetCullFace (IsPortalMirrored(ffpnz)?GL_FRONT:GL_BACK);
      else 
        statecache->SetCullFace (IsPortalMirrored(ffpnz)?GL_BACK:GL_FRONT);
          
      statecache->SetStencilFunc (GL_ALWAYS, stencil_clip_value, stencil_clip_mask);
      statecache->SetStencilOp (GL_ZERO, GL_ZERO, GL_REPLACE);
      SetZModeInternal (CS_ZBUF_TEST);
      
      cp = clipportal_stack[ffpnz];
      Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
        
      clipportal_stack[ffpnz]->status.Set(CS_PORTALSTATUS_SFILLED);
      ffps = ffpnz;
    }
    //perform z-clear finally
    if (currentAttachments != 0) 
      statecache->SetCullFace (IsPortalMirrored(ffpnz)?GL_FRONT:GL_BACK);
    else 
      statecache->SetCullFace (IsPortalMirrored(ffpnz)?GL_BACK:GL_FRONT);
        
    statecache->SetStencilFunc (GL_EQUAL, stencil_clip_value, stencil_clip_mask);
    statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
    SetZModeInternal (CS_ZBUF_FILL);

  	// for debug: clearing portals with colors
  	/*GLfloat debug_colors[][3] = {
  		{0.0,0.0,1.0}, {0.0,1.0,0.0}, {1.0,0.0,0.0}, 
  		{1.0,1.0,0.0}, {0.0,1.0,1.0}, {1.0,0.0,1.0},
  		{0.0,0.0,0.5}, {0.0,0.5,0.0}, {0.5,0.0,0.0},
  		{0.5,0.5,0.0}, {0.0,0.5,0.5}, {0.5,0.0,0.5},
  		{1.0,1.0,1.0}};
  	//int j = clipportal_stack.GetSize ()-1;
  	int j = cfp;
  	if (j>12) j=12;
  	glColorMask (true, true, true, true);
  	for (i=15;i>=0;i--) DeactivateTexture(i);
  	glColor3f(debug_colors[j][0], debug_colors[j][1], debug_colors[j][2]);*/
	  //
	  
    cp = clipportal_stack[ffpnz];
    //map all Z-values to 0.0
    glDepthRange(1.0, 1.0);
    //Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
    DrawScreenPolygon (cp->poly, cp->num_poly);
    //restore default z-mapping
    glDepthRange(1.0, 0.0);
    
    // finish of debug coloring
    //glColorMask (false, false, false, false);
    
    //set z-clear status from ffpnz to cfp
    for (i = ffpnz; i <= cfp; i++) 
      clipportal_stack[i]->status.Set (CS_PORTALSTATUS_ZCLEARED);
    //just in case
    ffpnz = csArrayItemNotFound;
  }
  
  if (ffps != cfp)
  {
    //need s-fill for current portal
    //glClear(GL_STENCIL_BUFFER_BIT);//TEMP!!!
    if (ffps != csArrayItemNotFound)
    {
      //clear previous s-fill
      if (currentAttachments != 0) 
        statecache->SetCullFace (IsPortalMirrored(ffps)?GL_FRONT:GL_BACK);
      else 
        statecache->SetCullFace (IsPortalMirrored(ffps)?GL_BACK:GL_FRONT);
          
      statecache->SetStencilFunc (GL_ALWAYS, 0, stencil_clip_mask);
      statecache->SetStencilOp (GL_ZERO, GL_ZERO, GL_ZERO);
      SetZModeInternal (CS_ZBUF_NONE);
      
      cp = clipportal_stack[ffps];
      //Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
      DrawScreenPolygon (cp->poly, cp->num_poly);
      
      clipportal_stack[ffps]->status.Reset(CS_PORTALSTATUS_SFILLED);
      ffps = csArrayItemNotFound;
    }
    //perform s-fill finally
    if (currentAttachments != 0)
      statecache->SetCullFace (IsPortalMirrored(cfp)?GL_FRONT:GL_BACK);
    else 
      statecache->SetCullFace (IsPortalMirrored(cfp)?GL_BACK:GL_FRONT);
        
    statecache->SetStencilFunc (GL_ALWAYS, stencil_clip_value, stencil_clip_mask);
    statecache->SetStencilOp (GL_ZERO, GL_ZERO, GL_REPLACE);
    SetZModeInternal (CS_ZBUF_TEST);
    
    cp = clipportal_stack[cfp];
    Draw2DPolygon (cp->poly, cp->num_poly, cp->normal);
      
    clipportal_stack[cfp]->status.Set(CS_PORTALSTATUS_SFILLED);
    ffps = cfp;
  }
  
  //use stencil for clipping
  statecache->SetStencilFunc (GL_EQUAL, stencil_clip_value, stencil_clip_mask);
  statecache->SetStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
    
  //restore glstate and matrices
  statecache->SetMatrixMode (GL_MODELVIEW);
  glPopMatrix ();
  statecache->SetMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  
  statecache->SetCullFace (oldcullface);
  statecache->SetColorMask (wmRed, wmGreen, wmBlue, wmAlpha);
  if (tex2d) statecache->Enable_GL_TEXTURE_2D ();
  if (sciss) glEnable(GL_SCISSOR_TEST);
  SetZModeInternal (current_zmode);
  
  //DebugVisualizeStencil (128);
  //debug_inhibit_draw = true;
  
}

void csGLGraphics3D::SetClipper (iClipper2D* clipper, int cliptype)
{
  GLRENDER3D_OUTPUT_STRING_MARKER(("%p, %s", clipper, 
    ClipperTypes.StringForIdent (cliptype)));

  //clipper = new csBoxClipper (10, 10, 200, 200);
  csGLGraphics3D::clipper = clipper;
  if (!clipper) cliptype = CS_CLIPPER_NONE;
  csGLGraphics3D::cliptype = cliptype;
  stencil_initialized = false;
  frustum_valid = false;
  size_t i;
  for (i = 0; i<6; i++)
    glDisable ((GLenum)(GL_CLIP_PLANE0+i));
  DisableStencilClipping ();
  cache_clip_portal = -1;
  cache_clip_plane = -1;
  cache_clip_z_plane = -1;

  if (cliptype != CS_CLIPPER_NONE)
  {
    if (!hasOld2dClip)
      G2D->GetClipRect (old2dClip.xmin, old2dClip.ymin, 
	old2dClip.xmax, old2dClip.ymax);
    hasOld2dClip = true;

    csVector2* clippoly = clipper->GetClipPoly ();
    csBox2 scissorbox;
    scissorbox.AddBoundingVertex (clippoly[0]);
    for (i=1; i<clipper->GetVertexCount (); i++)
      scissorbox.AddBoundingVertexSmart (csVector2 (clippoly[i].x, 
        clippoly[i].y));
    csBox2 scissorClip;
    scissorClip.Set (old2dClip.xmin, viewheight - old2dClip.ymax,
      old2dClip.xmax, viewheight - old2dClip.ymin);
    scissorbox *= csBox2 (scissorClip);
    if (scissorbox.Empty())
    {
      csGLGraphics3D::cliptype = CS_CLIPPER_EMPTY;
      return;
    }

    const csRect scissorRect ((int)floorf (scissorbox.MinX ()), 
      (int)floorf (scissorbox.MinY ()), 
      (int)ceilf (scissorbox.MaxX ()), 
      (int)ceilf (scissorbox.MaxY ()));
    if (currentAttachments != 0)
      r2tbackend->SetClipRect (scissorRect);
    else
    {
      GLint vp[4];
      glGetIntegerv (GL_VIEWPORT, vp);
      glScissor (vp[0] + scissorRect.xmin, vp[1] + scissorRect.ymin, scissorRect.Width(),
	scissorRect.Height());
    }
  }
  else if (hasOld2dClip)
  {
    G2D->SetClipRect (old2dClip.xmin, old2dClip.ymin, 
      old2dClip.xmax, old2dClip.ymax);
    hasOld2dClip = false;
  }
}

// @@@ doesn't serve any purpose for now, but might in the future.
// left in for now.
bool csGLGraphics3D::SetRenderState (G3D_RENDERSTATEOPTION op, long val)
{
  switch (op)
  {
    case G3DRENDERSTATE_EDGES:
      forceWireframe = (val != 0);
      if (forceWireframe)
        glPolygonMode (GL_BACK, GL_LINE);
      else
        glPolygonMode (GL_BACK, GL_FILL);
      return true;
    default:
      return false;
  }
}

long csGLGraphics3D::GetRenderState (G3D_RENDERSTATEOPTION op) const
{
  switch (op)
  {
    case G3DRENDERSTATE_EDGES:
      return (forceWireframe ? 1 : 0);
    default:
      return 0;
  }
}

bool csGLGraphics3D::SetOption (const char* name, const char* value)
{
  if (!strcmp (name, "StencilThreshold"))
  {
    sscanf (value, "%d", &stencil_threshold);
    return true;
  }
  return false;
}

void csGLGraphics3D::DrawSimpleMesh (const csSimpleRenderMesh& mesh, 
				     uint flags)
{
  csGLGraphics3D::DrawSimpleMeshes (&mesh, 1, flags);
}

void csGLGraphics3D::DrawSimpleMeshes (const csSimpleRenderMesh* meshes,
				       size_t numMeshes, uint flags)
{  
  
  if (current_drawflags & CSDRAW_2DGRAPHICS)
  {
    // Try to be compatible with 2D drawing mode
    G2D->PerformExtension ("glflushtext");
  }

  bool restoreProjection = false;
  bool wasProjectionExplicit = false;
  CS::Math::Matrix4 oldProjection;
  csReversibleTransform oldWorld2Camera;

  if (flags & csSimpleMeshScreenspace)
  {
    if (current_drawflags & CSDRAW_2DGRAPHICS)
    {
      csReversibleTransform camtrans;
      camtrans.SetO2T (
        csMatrix3 (1.0f, 0.0f, 0.0f,
      		   0.0f, -1.0f, 0.0f,
		   0.0f, 0.0f, 1.0f));
      camtrans.SetO2TTranslation (csVector3 (0, viewheight, 0));
      SetWorldToCamera (camtrans.GetInverse ());
    } 
    else 
    {
      const float vwf = (float)(viewwidth);
      const float vhf = (float)(viewheight);

      wasProjectionExplicit = explicitProjection;
      explicitProjection = true;
      oldProjection = projectionMatrix;
    
      projectionMatrix = CS::Math::Projections::Ortho (0, vwf, vhf, 0, -1.0, 10.0);

      oldWorld2Camera = world2camera;
      SetWorldToCamera (csReversibleTransform ());
    
      restoreProjection = true;
      needProjectionUpdate = true;
    }
  }

  csZBufMode old_zbufmode = current_zmode;
  bool needDisableTexture = false;
  bool needDisableBuffers = false;
  
  for (size_t m = 0; m < numMeshes; m++)
  {
    const csSimpleRenderMesh& mesh = meshes[m];

    bool useShader = (mesh.shader != 0);
    uint indexCount = mesh.indices ? mesh.indexCount : mesh.vertexCount;
    if (!mesh.renderBuffers.IsValid())
    {
      if (scrapIndicesSize < indexCount)
      {
	scrapIndices = csRenderBuffer::CreateIndexRenderBuffer (indexCount,
	  CS_BUF_STREAM, CS_BUFCOMP_UNSIGNED_INT, 0, mesh.vertexCount - 1);
	scrapIndicesSize = indexCount;
      }
      if (scrapVerticesSize < mesh.vertexCount)
      {
	scrapVertices = csRenderBuffer::CreateRenderBuffer (
	  mesh.vertexCount, CS_BUF_STREAM, CS_BUFCOMP_FLOAT, 3);
	scrapTexcoords = csRenderBuffer::CreateRenderBuffer (
	  mesh.vertexCount, CS_BUF_STREAM, CS_BUFCOMP_FLOAT, 2);
	scrapColors = csRenderBuffer::CreateRenderBuffer (
	  mesh.vertexCount, CS_BUF_STREAM, CS_BUFCOMP_FLOAT, 4);
    
	scrapVerticesSize = mesh.vertexCount;
      }
    }

    csShaderVariable* sv;
    if (!mesh.renderBuffers.IsValid())
    {
      sv = scrapContext.GetVariableAdd (string_indices);
      if (mesh.indices)
      {
	scrapIndices->CopyInto (mesh.indices, indexCount);
      }
      else
      {
	csRenderBufferLock<uint> indexLock (scrapIndices);
	for (uint i = 0; i < mesh.vertexCount; i++)
	  indexLock[(size_t)i] = i;
      }
      sv->SetValue (scrapIndices);
      scrapBufferHolder->SetRenderBuffer (CS_BUFFER_INDEX, scrapIndices);
    
      sv = scrapContext.GetVariableAdd (string_vertices);
      if (mesh.vertices)
      {
	scrapVertices->CopyInto (mesh.vertices, mesh.vertexCount);
	scrapBufferHolder->SetRenderBuffer (CS_BUFFER_POSITION, scrapVertices);
	if (useShader)
	  sv->SetValue (scrapVertices);
      }
      else
      {
	scrapBufferHolder->SetRenderBuffer (CS_BUFFER_POSITION, 0);
	if (useShader)
	  sv->SetValue (0);
      }
      sv = scrapContext.GetVariableAdd (string_texture_coordinates);
      if (mesh.texcoords)
      {
	scrapTexcoords->CopyInto (mesh.texcoords, mesh.vertexCount);
	scrapBufferHolder->SetRenderBuffer (CS_BUFFER_TEXCOORD0, scrapTexcoords);
	if (useShader)
	  sv->SetValue (scrapTexcoords);
      }
      else
      {
	scrapBufferHolder->SetRenderBuffer (CS_BUFFER_TEXCOORD0, 0);
	if (useShader)
	  sv->SetValue (0);
      }
      sv = scrapContext.GetVariableAdd (string_colors);
      if (mesh.colors)
      {
	scrapColors->CopyInto (mesh.colors, mesh.vertexCount);
	scrapBufferHolder->SetRenderBuffer (CS_BUFFER_COLOR, scrapColors);
	if (useShader)
	  sv->SetValue (scrapColors);
      }
      else
      {
	scrapBufferHolder->SetRenderBuffer (CS_BUFFER_COLOR, 0);
	if (useShader)
	  sv->SetValue (0);
      }
    }
    if (useShader)
    {
      sv = scrapContext.GetVariableAdd (string_texture_diffuse);
      sv->SetValue (mesh.texture);
      needDisableTexture = false;
    }
    else
    {
      if (fixedFunctionForcefulEnable)
      {
	const GLenum state = GL_LIGHTING;
	GLboolean s = glIsEnabled (state);
	if (s) glDisable (state); else glEnable (state);
	glBegin (GL_TRIANGLES);  glEnd ();
	if (s) glEnable (state); else glDisable (state);
      }
      if (ext->CS_GL_ARB_multitexture)
      {
	statecache->SetCurrentImageUnit (0);
	statecache->ActivateImageUnit ();
	statecache->SetCurrentTCUnit (0);
	statecache->ActivateTCUnit (csGLStateCache::activateTexCoord);
      }
      if (mesh.texture)
      {
	ActivateTexture (mesh.texture);
	imageUnits[0].texture->ChangeTextureCompareMode (
	  CS::Graphics::TextureComparisonMode ());
      }
      else
      {
	DeactivateTexture ();
	needDisableTexture = false;
      }
    }

    csRenderMesh rmesh;
#ifdef CS_DEBUG
    csString meshName;
    meshName.Format ("SimpleMesh %zu/%zu", m+1, numMeshes);
    rmesh.db_mesh_name = meshName;
#endif
    //rmesh.z_buf_mode = mesh.z_buf_mode;
    rmesh.mixmode = mesh.mixmode;
    rmesh.clip_portal = 0;
    rmesh.clip_plane = 0;
    rmesh.clip_z_plane = 0;
    rmesh.do_mirror = false;
    rmesh.meshtype = mesh.meshtype;
    if (mesh.indexStart < mesh.indexEnd)
    {
      rmesh.indexstart = mesh.indexStart;
      rmesh.indexend = mesh.indexEnd;
    }
    else
    {
      rmesh.indexstart = 0;
      rmesh.indexend = indexCount;
    }
    rmesh.variablecontext = &scrapContext;
    rmesh.buffers =
      mesh.renderBuffers.IsValid() ? mesh.renderBuffers : scrapBufferHolder;

    rmesh.object2world = mesh.object2world;

    csShaderVariableStack stack;
    stack.Setup (strings->GetSize ());
    if (mesh.shader != 0) mesh.shader->PushVariables (stack);
    shadermgr->PushVariables (stack);
    scrapContext.PushVariables (stack);
    if (mesh.dynDomain != 0) mesh.dynDomain->PushVariables (stack);

    if (mesh.alphaType.autoAlphaMode)
    {
      csAlphaMode::AlphaType autoMode = csAlphaMode::alphaNone;

      iTextureHandle* tex = 0;
      csShaderVariable *texVar = csGetShaderVariableFromStack (stack, 
	mesh.alphaType.autoModeTexture);
      if (texVar)
	texVar->GetValue (tex);

      if (tex == 0)
	tex = mesh.texture;
      if (tex != 0)
	autoMode = tex->GetAlphaType ();

      rmesh.alphaType = autoMode;
    }
    else
    {
      rmesh.alphaType = mesh.alphaType.alphaType;
    }
    
    SetZMode (mesh.z_buf_mode);
    csRenderMeshModes modes (rmesh);

    size_t shaderTicket = 0;
    size_t passCount = 1;
    if (mesh.shader != 0)
    {
      shaderTicket = mesh.shader->GetTicket (modes, stack);
      passCount = mesh.shader->GetNumberOfPasses (shaderTicket);
    }

    for (size_t p = 0; p < passCount; p++)
    {
      if (mesh.shader != 0)
      {
	mesh.shader->ActivatePass (shaderTicket, p);
	mesh.shader->SetupPass (shaderTicket, &rmesh, modes, stack);
      }
      else if (mesh.renderBuffers)
      {
	ActivateBuffers (mesh.renderBuffers, defaultBufferMapping);
      }
      else
      {
	ActivateBuffers (scrapBufferHolder, defaultBufferMapping);
      }
      DrawMesh (&rmesh, modes, stack);
      if (mesh.shader != 0)
      {
	mesh.shader->TeardownPass (shaderTicket);
	mesh.shader->DeactivatePass (shaderTicket);
	needDisableBuffers = false;
      }
      else
      {
	needDisableBuffers = true;
      }
    }

    if (!useShader)
    {
      if (mesh.texture)
	needDisableTexture = true;
    }
  }

  if (flags & csSimpleMeshScreenspace)
  {
    if (current_drawflags & CSDRAW_2DGRAPHICS)
    {
      // Bring it back, that old new york rap! 
      // Or well, at least that old identity transform
      SetWorldToCamera (csReversibleTransform ());
    }
  }

  if (needDisableTexture)
    DeactivateTexture ();
  if (needDisableBuffers)
    DeactivateBuffers (0,0);

  SetZMode (old_zbufmode);
  
  if (restoreProjection)
  {
    explicitProjection = wasProjectionExplicit;
    projectionMatrix = oldProjection;
    world2camera = oldWorld2Camera;
    needProjectionUpdate = true;
  }
}

bool csGLGraphics3D::PerformExtensionV (char const* command, va_list /*args*/)
{
  if (!strcasecmp (command, "applybufferchanges"))
  {
    ApplyBufferChanges ();
    return true;
  }
  return false;
}

bool csGLGraphics3D::PerformExtension (char const* command, ...)
{
  va_list args;
  va_start (args, command);
  bool rc = PerformExtensionV(command, args);
  va_end (args);
  return rc;
}

csOpenGLHalo::csOpenGLHalo (float iR, float iG, float iB,
  unsigned char *iAlpha, int iWidth, int iHeight, csGLGraphics3D* iG3D) :
  scfImplementationType(this), R(iR), G(iG), B(iB)
{
  // Initialization  
  R = iR; G = iG; B = iB;
  // OpenGL can only use 2^n sized textures
  Width = csFindNearestPowerOf2 (iWidth);
  Height = csFindNearestPowerOf2 (iHeight);

  uint8* rgba = new uint8 [Width * Height * 4];
  memset (rgba, 0, Width * Height * 4);
  uint8* rgbaPtr = rgba;
  for (int y = 0; y < iHeight; y++)
  {
    for (int x = 0; x < iWidth; x++)
    {
      *rgbaPtr++ = 0xff;
      *rgbaPtr++ = 0xff;
      *rgbaPtr++ = 0xff;
      *rgbaPtr++ = *iAlpha++;
    }
    rgbaPtr += (Width - iWidth) * 4;
  }

  // Create handle
  glGenTextures (1, &halohandle);
  // Activate handle
  csGLGraphics3D::statecache->SetCurrentImageUnit (0);
  csGLGraphics3D::statecache->ActivateImageUnit ();
  csGLGraphics3D::statecache->SetTexture (GL_TEXTURE_2D, halohandle);

  // Jaddajaddajadda
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, Width, Height, 0, GL_RGBA,
    GL_UNSIGNED_BYTE, rgba);

  delete[] rgba;
  (G3D = iG3D)->IncRef ();

  Wfact = float (iWidth) / Width;
  Hfact = float (iHeight) / Height;

  Width = iWidth;
  Height = iHeight;

  if (R > 1.0 || G > 1.0 || B > 1.0)
  {
    dstblend = CS_FX_SRCALPHAADD;
    R /= 2; G /= 2; B /= 2;
  }
  else
    dstblend = CS_FX_ALPHA;
}

csOpenGLHalo::~csOpenGLHalo ()
{
  DeleteTexture();
  G3D->DecRef ();
}

void csOpenGLHalo::DeleteTexture ()
{
  // Kill, crush and destroy
  // Delete generated OpenGL handle
  if (halohandle != 0)
  {
    glDeleteTextures (1, &halohandle);
    halohandle = 0;
  }
}

// Draw the halo. Wasn't that a suprise
void csOpenGLHalo::Draw (float x, float y, float w, float h, float iIntensity,
  csVector2 *iVertices, size_t iVertCount)
{
  //G3D->SwapIfNeeded();
  int swidth = G3D->GetWidth ();
  int sheight = G3D->GetHeight ();
  size_t i;

  if (w < 0) w = Width;
  if (h < 0) h = Height;

  csVector2 HaloPoly [4];
  if (!iVertices)
  {
    iVertCount = 4;
    iVertices = HaloPoly;

    float x1 = x, y1 = y, x2 = x + w, y2 = y + h;
    if (x1 < 0) x1 = 0; if (x2 > swidth ) x2 = swidth ;
    if (y1 < 0) y1 = 0; if (y2 > sheight) y2 = sheight;
    if ((x1 >= x2) || (y1 >= y2))
      return;

    HaloPoly [0].Set (x1, y1);
    HaloPoly [1].Set (x1, y2);
    HaloPoly [2].Set (x2, y2);
    HaloPoly [3].Set (x2, y1);
  };

  /// The inverse width and height of the halo
  float inv_W = Wfact / w, inv_H = Hfact / h;
  // the screen setup does not seem to be like in DrawPixmap,
  // so vx, vy (nice DrawPixmap coords) need to be transformed.
  // magic constant .86 that make halos align properly, with this formula.
  float aspectw = .86* (float)G3D->GetWidth() / G3D->GetAspect();
  float aspecth = .86* (float)G3D->GetHeight() / G3D->GetAspect(); 
  float hw = (float)G3D->GetWidth() * 0.5f;
  float hh = (float)G3D->GetHeight() * 0.5f;

  int oldIU = G3D->statecache->GetCurrentImageUnit ();
  int oldTCU = G3D->statecache->GetCurrentTCUnit ();
  G3D->statecache->SetCurrentImageUnit (0);
  G3D->statecache->ActivateImageUnit ();
  G3D->statecache->SetCurrentTCUnit (0);
  G3D->statecache->ActivateTCUnit (csGLStateCache::activateTexCoord);

  
  //csGLGraphics3D::SetGLZBufferFlags (CS_ZBUF_NONE);
  // @@@ Is this correct to override current_zmode?
  G3D->SetZMode (CS_ZBUF_NONE);
  bool texEnabled = 
    csGLGraphics3D::statecache->IsEnabled_GL_TEXTURE_2D ();
  csGLGraphics3D::statecache->Enable_GL_TEXTURE_2D ();

  csGLGraphics3D::statecache->SetShadeModel (GL_FLAT);
  csGLGraphics3D::statecache->SetTexture (GL_TEXTURE_2D, halohandle);

  //???@@@statecache->SetMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity();
  G3D->SetGlOrtho (false);
  //glTranslatef (0, 0, 0);

  G3D->SetMixMode (dstblend, csAlphaMode::alphaSmooth,
    CS::Graphics::AlphaTestOptions());

  glColor4f (R, G, B, iIntensity);

  glBegin (GL_POLYGON);
  for (i = iVertCount; i-- > 0;)
  {
    float vx = iVertices [i].x, vy = iVertices [i].y;
    glTexCoord2f ((vx - x) * inv_W, (vy - y) * inv_H);
    glVertex3f ((vx - hw) * aspectw + hw, 
      (sheight - vy - hh)*aspecth + hh, -14.0f);
  }
  glEnd ();

  glPopMatrix ();

  /*
    @@@ Urgh. B/C halos are drawn outside the normal 
    shader/texture/buffer activation/mesh drawing realms, the states
    changed have to be backed up and restored when done.
   */
  csGLGraphics3D::statecache->SetTexture (GL_TEXTURE_2D, 0);
  if (!texEnabled)
    csGLGraphics3D::statecache->Disable_GL_TEXTURE_2D ();
  G3D->statecache->SetCurrentImageUnit (oldIU);
  G3D->statecache->ActivateImageUnit ();
  G3D->statecache->SetCurrentTCUnit (oldTCU);
  G3D->statecache->ActivateTCUnit (csGLStateCache::activateTexCoord);
}

iHalo *csGLGraphics3D::CreateHalo (float iR, float iG, float iB,
  unsigned char *iAlpha, int iWidth, int iHeight)
{
  csOpenGLHalo* halo = new csOpenGLHalo (iR, iG, iB, iAlpha, iWidth, iHeight, 
    this);
  halos.Push (halo);
  return halo;
}

void csGLGraphics3D::RemoveHalo (csOpenGLHalo* halo)
{
  halos.Delete (halo);
}

float csGLGraphics3D::GetZBuffValue (int x, int y)
{
  GLfloat zvalue;
  glReadPixels (x, viewheight - y - 1, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, 
    &zvalue);
  if (zvalue < .000001) return 1000000000.;
  // 0.090909=1/11, that is 1 divided by total depth delta set by
  // glOrtho. Where 0.090834 comes from, I don't know
  //return (0.090834 / (zvalue - (0.090909)));
  // @@@ Jorrit: I have absolutely no idea what they are trying to do
  // but changing the above formula to the one below at least appears
  // to give more accurate results.
  return (0.090728 / (zvalue - (0.090909)));
}

////////////////////////////////////////////////////////////////////
// iComponent
////////////////////////////////////////////////////////////////////

bool csGLGraphics3D::Initialize (iObjectRegistry* p)
{
  bool ok = true;
  object_reg = p;

  if (!eventHandler1)
    eventHandler1.AttachNew (new EventHandler<false> (this));
  if (!eventHandler2)
    eventHandler2.AttachNew (new EventHandler<true> (this));

  SystemOpen = csevSystemOpen(object_reg);
  SystemClose = csevSystemClose(object_reg);

  csRef<iEventQueue> q = csQueryRegistry<iEventQueue> (object_reg);
  if (q)
  {
    csEventID events1[] = { SystemOpen, SystemClose,
			    CS_EVENTLIST_END };
    q->RegisterListener (eventHandler1, events1);
    csEventID events2[] = { SystemOpen, CS_EVENTLIST_END };
    q->RegisterListener (eventHandler2, events2);
  }
  // We subscribe to csevCanvasResize after G2D has been created
  
  bugplug = csQueryRegistry<iBugPlug> (object_reg);

  strings = csQueryRegistryTagInterface<iShaderVarStringSet> (
    object_reg, "crystalspace.shader.variablenameset");

  csRef<iPluginManager> plugin_mgr = 
  	csQueryRegistry<iPluginManager> (object_reg);
  csRef<iCommandLineParser> cmdline = 
  	csQueryRegistry<iCommandLineParser> (object_reg);

  /* Note: r3dopengl.cfg is also added by the canvases. This is done because
   * either the canvas or the renderer may be loaded before the other, but
   * both need settings from that file. */
  config.AddConfig(object_reg, "/config/r3dopengl.cfg");

  /* Obtain the iGraphics2D, with the following precedence:
   * - Canvas supplied with the -canvas option.
   * - The one from the object registry.
   * - The one set with the Video.OpenGL.Canvas config setting.
   * - Default canvas.
   */
  const char *driver = cmdline->GetOption ("canvas");
  if (driver == 0)
    G2D = csQueryRegistry<iGraphics2D> (object_reg);
  if (!G2D.IsValid())
  {
    if (!driver)
      driver = config->GetStr ("Video.OpenGL.Canvas", CS_OPENGL_2D_DRIVER);

    G2D = csLoadPlugin<iGraphics2D> (plugin_mgr, driver);
    if (G2D.IsValid())
      object_reg->Register(G2D, "iGraphics2D");
    else
    {
      Report (CS_REPORTER_SEVERITY_ERROR, "Error loading Graphics2D plugin.");
      ok = false;
    }
  }

  if (ok)
  {
    CanvasResize = csevCanvasResize(object_reg, G2D);
    q->RegisterListener (eventHandler1, CanvasResize);
  }

  return ok;
}




////////////////////////////////////////////////////////////////////
// iEventHandler
////////////////////////////////////////////////////////////////////

bool csGLGraphics3D::HandleEvent (iEvent& Event, bool postShaderManager)
{
  if (postShaderManager)
  {
    if (Event.Name == SystemOpen)
    {
      SetupShaderVariables ();
      return true;
    }
    return false;
  }
  
  if (Event.Name == SystemOpen)
  {
    Open ();
    return true;
  }
  else if (Event.Name == SystemClose)
  {
    Close ();
    return true;
  }
  else if (Event.Name == CanvasResize)
  {
    viewwidth = G2D->GetWidth();
    viewheight = G2D->GetHeight();
    G2D->GetFramebufferDimensions (scrwidth, scrheight);
    asp_center_x = (int)(viewwidth/2.0f);
    asp_center_y = (int)(viewheight/2.0f);
    return true;
  }
  return false;
}


////////////////////////////////////////////////////////////////////
//                          iDebugHelper
////////////////////////////////////////////////////////////////////

bool csGLGraphics3D::DebugCommand (const char* cmdstr)
{
  CS_ALLOC_STACK_ARRAY(char, cmd, strlen (cmdstr) + 1);
  strcpy (cmd, cmdstr);
  char* param = 0;
  char* space = strchr (cmd, ' ');
  if (space)
  {
    param = space + 1;
    *space = 0;
  }

  if (strcasecmp (cmd, "dump_zbuf") == 0)
  {
    const char* dir = 
      ((param != 0) && (*param != 0)) ? param : "/tmp/zbufdump/";
    DumpZBuffer (dir);

    return true;
  }
  else if (strcasecmp (cmd, "dump_textures") == 0)
  {
    csRef<iImageIO> imgsaver = csQueryRegistry<iImageIO> (object_reg);
    if (!imgsaver)
    {
      Report (CS_REPORTER_SEVERITY_WARNING,
	      "Could not get image saver.");
      return false;
    }

    csRef<iVFS> vfs = csQueryRegistry<iVFS> (object_reg);
    if (!vfs)
    {
      Report (CS_REPORTER_SEVERITY_WARNING, 
	      "Could not get VFS.");
      return false;
    }

    if (txtmgr)
    {
      const char* dir = 
	  ((param != 0) && (*param != 0)) ? param : "/tmp/textures/";
      txtmgr->DumpTextures (vfs, imgsaver, dir);
    }

    return true;
  }
  else if (strcasecmp (cmd, "dump_vbostat") == 0)
  {
    if (vboManager) vboManager->DumpStats ();
    return true;
  }
  return false;
}

void csGLGraphics3D::DumpZBuffer (const char* path)
{
  csRef<iImageIO> imgsaver = csQueryRegistry<iImageIO> (object_reg);
  if (!imgsaver)
  {
    Report (CS_REPORTER_SEVERITY_WARNING,
      "Could not get image saver.");
    return;
  }

  csRef<iVFS> vfs = csQueryRegistry<iVFS> (object_reg);
  if (!vfs)
  {
    Report (CS_REPORTER_SEVERITY_WARNING, 
      "Could not get VFS.");
    return;
  }

  static int zBufDumpNr = 0;
  csString filenameZ;
  csString filenameScr;
  do
  {
    int nr = zBufDumpNr++;
    filenameZ.Format ("%s%d_z.png", path, nr);
    filenameScr.Format ("%s%d_scr.png", path, nr);
  }
  while (vfs->Exists (filenameZ) && vfs->Exists (filenameScr));

  {
    csRef<iImage> screenshot = G2D->ScreenShot ();
    csRef<iDataBuffer> buf = imgsaver->Save (screenshot, "image/png");
    if (!buf)
    {
      Report (CS_REPORTER_SEVERITY_WARNING,
	"Could not save screen.");
    }
    else
    {
      if (!vfs->WriteFile (filenameScr, (char*)buf->GetInt8 (), 
	buf->GetSize ()))
      {
	Report (CS_REPORTER_SEVERITY_WARNING,
	  "Could not write to %s.", filenameScr.GetData ());
      }
      else
      {
	Report (CS_REPORTER_SEVERITY_NOTIFY,
	  "Dumped screen to %s", filenameScr.GetData ());
      }
    }
  }
  {
    csRef<csImageMemory> zImage;
    zImage.AttachNew (new csImageMemory (viewwidth, viewheight));

    static const uint8 zBufColors[][3] = {
      {  0,   0,   0},
      {  0, 255,   0},
      {255, 255,   0},
      {255,   0,   0},
      {255,   0, 255},
      {  0,   0, 255},
      {255, 255, 255},
    };
    const int colorMax = (sizeof (zBufColors) / sizeof (zBufColors[0])) - 1;

    int num = viewwidth * viewheight;
    GLfloat* zvalues = new GLfloat[num];
    glReadPixels (0, 0, viewwidth, viewheight, GL_DEPTH_COMPONENT, GL_FLOAT, 
      zvalues);
    GLfloat minValue = 1.0f;
    GLfloat maxValue = 0.0f;
    for (int i = 0; i < num; i++)
    {
      if (zvalues[i] == 0.0f) continue;	 // possibly leftovers from a Z buffer clean
      if (zvalues[i] < minValue)
	minValue = zvalues[i];
      else if (zvalues[i] > maxValue)
	maxValue = zvalues[i];
    }
    float zMul = 1.0f;
    if (maxValue - minValue > 0)
      zMul /= (maxValue - minValue);
    csRGBpixel* imgPtr = (csRGBpixel*)zImage->GetImageData ();
    for (int y = 0; y < viewheight; y++)
    {
      GLfloat* zPtr = zvalues + (viewheight - y - 1) * viewwidth;
      for (int x = 0; x < viewwidth; x++)
      {
	GLfloat zv = *zPtr++; 
	zv -= minValue;
	zv *= zMul;
	float cif = zv * (float)colorMax;
	int ci = csQint (cif);
	ci = MAX (0, MIN (ci, colorMax));
	if (ci == colorMax)
	{
	  (imgPtr++)->Set (zBufColors[ci][0], zBufColors[ci][1],
	    zBufColors[ci][2]);
	}
	else
	{
	  float ratio = cif - (float)ci;
	  float invRatio = 1.0f - ratio;
	  (imgPtr++)->Set (
	    csQint (zBufColors[ci+1][0] * ratio + zBufColors[ci][0] * invRatio), 
	    csQint (zBufColors[ci+1][1] * ratio + zBufColors[ci][1] * invRatio), 
	    csQint (zBufColors[ci+1][2] * ratio + zBufColors[ci][2] * invRatio));

	}
      }
    }
    delete[] zvalues;

    csRef<iDataBuffer> buf = imgsaver->Save (zImage, "image/png");
    if (!buf)
    {
      Report (CS_REPORTER_SEVERITY_WARNING,
	"Could not save Z buffer.");
    }
    else
    {
      if (!vfs->WriteFile (filenameZ, (char*)buf->GetInt8 (), 
	buf->GetSize ()))
      {
	Report (CS_REPORTER_SEVERITY_WARNING,
	  "Could not write to %s.", filenameZ.GetData ());
      }
      else
      {
	Report (CS_REPORTER_SEVERITY_NOTIFY,
	  "Dumped Z buffer to %s", filenameZ.GetData ());
      }
    }
  }
}

int csGLGraphics3D::GetCurrentDrawFlags () const
{
  return current_drawflags;
}

}
CS_PLUGIN_NAMESPACE_END(gl3d)
