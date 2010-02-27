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

#include "csgeom/vector3.h"
#include "csplugincommon/opengl/glextmanager.h"
#include "csutil/objreg.h"
#include "csutil/ref.h"
#include "csutil/scf.h"
#include "iutil/comp.h"
#include "iutil/plugin.h"
#include "ivideo/graph2d.h"
#include "ivideo/graph3d.h"
#include "ivideo/shader/shader.h"
#include "iutil/databuff.h"

#include "glshader_avp.h"
#include "glshader_afp.h"
#include "glshader_arb.h"



CS_LEAKGUARD_IMPLEMENT (csGLShader_ARB);

SCF_IMPLEMENT_FACTORY (csGLShader_ARB)

csGLShader_ARB::csGLShader_ARB(iBase* parent) : 
  scfImplementationType (this, parent), ext (0)
{
  enable = false;
  isOpen = false;
}

csGLShader_ARB::~csGLShader_ARB()
{
}

////////////////////////////////////////////////////////////////////
//                      iShaderProgramPlugin
////////////////////////////////////////////////////////////////////
bool csGLShader_ARB::SupportType(const char* type)
{
  if (!enable)
    return false;
  else if (strcasecmp (type, "vp") == 0)
    return true;
  else if (strcasecmp (type, "fp") == 0)
    return true;
  return false;
}

csPtr<iShaderProgram> csGLShader_ARB::CreateProgram (const char* type)
{
  /*if (!enable)
    return 0;*/
  if (strcasecmp (type, "vp") == 0)
    return csPtr<iShaderProgram> (new csShaderGLAVP (this));
  if (strcasecmp (type, "fp") == 0)
    return csPtr<iShaderProgram> (new csShaderGLAFP (this));
  else
    return 0;
}

void csGLShader_ARB::Open()
{
  if (isOpen) return;
  if(!object_reg)
    return;

  if (ext)
  {
    ext->InitGL_ARB_vertex_program ();
    ext->InitGL_ARB_fragment_program ();
  } else return;

  isOpen = true;
}

////////////////////////////////////////////////////////////////////
//                          iComponent
////////////////////////////////////////////////////////////////////
bool csGLShader_ARB::Initialize(iObjectRegistry* reg)
{
  object_reg = reg;

  csRef<iGraphics3D> r = csQueryRegistry<iGraphics3D> (object_reg);

  csRef<iFactory> f = scfQueryInterfaceSafe<iFactory> (r);
  if (f != 0 && strcmp ("crystalspace.graphics3d.opengl", 
      f->QueryClassID ()) == 0)
    enable = true;
  else
    return false;

  r->GetDriver2D()->PerformExtension ("getextmanager", &ext);
  return true;
}

