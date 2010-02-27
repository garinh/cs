/*
Copyright (C) 2002 by John Harger

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

#ifndef __GLSHADER_PS1_COMMON_H__
#define __GLSHADER_PS1_COMMON_H__

#include "csgfx/shadervarcontext.h"
#include "csutil/strhash.h"
#include "iutil/databuff.h"
#include "iutil/strset.h"
#include "ivideo/shader/shader.h"

#include "csplugincommon/shader/shaderplugin.h"
#include "csplugincommon/shader/shaderprogram.h"

#include "glshader_ps1.h"

CS_PLUGIN_NAMESPACE_BEGIN(GLShaderPS1)
{

class csPixelShaderParser;

class csShaderGLPS1_Common : public csShaderProgram
{
protected:
  csGLShader_PS1* shaderPlug;

  bool validProgram;
  csRef<iDataBuffer> programBuffer;

  enum
  {
    /// Number of available constant registers
    MAX_CONST_REGS = 8
  };
  ProgramParam constantRegs[MAX_CONST_REGS];

  void Report (int severity, const char* msg, ...);

  virtual bool LoadProgramStringToGL (const csPixelShaderParser& parser) = 0;
public:
  csShaderGLPS1_Common (csGLShader_PS1* shaderplug) : 
    csShaderProgram (shaderplug->object_reg)
  {
    validProgram = true;
    shaderPlug = shaderplug;
  }
  virtual ~csShaderGLPS1_Common ()
  {
  }

  void SetValid(bool val) { validProgram = val; }

  ////////////////////////////////////////////////////////////////////
  //                      iShaderProgram
  ////////////////////////////////////////////////////////////////////

  /// Check if valid
  virtual bool IsValid() { return validProgram;} 

  /// Loads from a document-node
  virtual bool Load (iShaderDestinationResolver*, iDocumentNode* node);

  /// Loads from raw text
  virtual bool Load (iShaderDestinationResolver*, const char*, 
    csArray<csShaderVarMapping> &);

  /// Compile a program
  virtual bool Compile (iHierarchicalCache*, csRef<iString>*);

  virtual void GetUsedShaderVars (csBitArray& bits) const;
};

}
CS_PLUGIN_NAMESPACE_END(GLShaderPS1)

#endif //__GLSHADER_PS1_COMMON_H__
