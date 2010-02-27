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

#ifndef __GLSHADER_FIXEDFP_H__
#define __GLSHADER_FIXEDFP_H__

#include "ivideo/shader/shader.h"
#include "csutil/weakref.h"
#include "csutil/leakguard.h"
#include "csgfx/shadervar.h"
#include "iengine/sector.h" // for csFog
#include "imap/services.h"
#include "csplugincommon/shader/shaderplugin.h"
#include "csplugincommon/shader/shaderprogram.h"

class csGLShader_FIXED;

class csGLShaderFFP : public csShaderProgram
{
private:
  csStringHash tokens;
#define CS_TOKEN_ITEM_FILE \
  "plugins/video/render3d/shader/shaderplugins/glshader_fixed/glshader_ffp.tok"
#include "cstool/tokenlist.h"
#undef CS_TOKEN_ITEM_FILE

  csWeakRef<iGraphics3D> g3d;
  csGLExtensionManager* ext;
  csGLShader_FIXED* shaderPlug;

  csGLStateCache* statecache;

  bool validProgram;

  struct FogInfo
  {
    int mode;

    ProgramParam density;
    ProgramParam start;
    ProgramParam end;
    ProgramParam color;

    FogInfo () : mode (CS_FOG_MODE_NONE) {}
  };
  FogInfo fog;
  bool colorSum;

  // Layers of multitexturing
  struct mtexlayer
  {
    struct TexFunc
    {
      // Argument sources
      int source[3];
      // Modifier
      int mod[3];
      // Operation
      int op;
      // Scale
      float scale;

      TexFunc()
      {
	source[0] = GL_PREVIOUS_ARB;
	source[1] = GL_TEXTURE;
	source[2] = -1;
	op = GL_MODULATE;
	scale = 1.0f;
      }
    };

    TexFunc color;
    TexFunc alpha;

    mtexlayer()
    {
      color.mod[0] = GL_SRC_COLOR;
      color.mod[1] = GL_SRC_COLOR;
      color.mod[2] = GL_SRC_COLOR;
      alpha.mod[0] = GL_SRC_ALPHA;
      alpha.mod[1] = GL_SRC_ALPHA;
      alpha.mod[2] = GL_SRC_ALPHA;
    }
  };

  //array of mtexlayers
  csArray<mtexlayer> texlayers;

  //maximum number of layers
  int maxlayers;
  csHash<int, csString> layerNames;

  void Report (int severity, const char* msg, ...);
  void Report (int severity, iDocumentNode* node, const char* msg, ...);

  int GetCrossbarSource (const char* str);
  bool LoadLayer(mtexlayer* layer, iDocumentNode* node);
  bool LoadEnvironment(mtexlayer* layer, iDocumentNode* node);
  bool ParseFog (iDocumentNode* node, FogInfo& fog);

  void DumpTexFunc (const mtexlayer::TexFunc& tf);
  void CompactLayers();
  /**
   * Try if texture function tf1 and its following function, tf2, can be merged
   * into one.
   */
  bool TryMergeTexFuncs (mtexlayer::TexFunc& newTF, 
    const mtexlayer::TexFunc& tf1, const mtexlayer::TexFunc& tf2);

  void ActivateTexFunc (const mtexlayer::TexFunc& tf, GLenum sourceP,
    GLenum operandP, GLenum combineP, GLenum scaleP);

  void BuildTokenHash();
public:
  CS_LEAKGUARD_DECLARE (csGLShaderFFP);

  csGLShaderFFP(csGLShader_FIXED* shaderPlug);
  virtual ~csGLShaderFFP ();

  ////////////////////////////////////////////////////////////////////
  //                      iShaderProgram
  ////////////////////////////////////////////////////////////////////

  /// Sets this program to be the one used when rendering
  virtual void Activate ();

  /// Deactivate program so that it's not used in next rendering
  virtual void Deactivate ();

  /// Setup states needed for proper operation of the shader
  virtual void SetupState (const CS::Graphics::RenderMesh* mesh,
    CS::Graphics::RenderMeshModes& modes,
    const csShaderVariableStack& stack);

  /// Reset states to original
  virtual void ResetState ();

  /// Loads from a document-node
  virtual bool Load(iShaderDestinationResolver*, iDocumentNode* node);

  /// Loads from raw text
  virtual bool Load (iShaderDestinationResolver*, const char*, 
    csArray<csShaderVarMapping> &)
  { return false; }

  /// Compile a program
  virtual bool Compile (iHierarchicalCache*, csRef<iString>*);

  virtual int ResolveTU (const char* binding)
  { return layerNames.Get (binding, -1); }

  virtual void GetUsedShaderVars (csBitArray& bits) const;
};


#endif //__GLSHADER_FIXEDFP_H__

