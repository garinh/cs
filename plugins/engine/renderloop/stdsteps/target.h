/*
    Copyright (C) 2003 by Jorrit Tyberghein
	      (C) 2003 by Frank Richter
              (C) 2003 by Anders Stenberg

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

#ifndef __CS_TARGET_H__
#define __CS_TARGET_H__

#include "csutil/scf_implementation.h"
#include "csutil/csstring.h"
#include "csutil/weakref.h"
#include "iengine/renderloop.h"
#include "iengine/rendersteps/icontainer.h"
#include "iengine/rendersteps/irenderstep.h"
#include "ivideo/shader/shader.h"

#include "csplugincommon/renderstep/basesteptype.h"
#include "csplugincommon/renderstep/basesteploader.h"
#include "csplugincommon/renderstep/parserenderstep.h"

class csTargetRSType :
  public scfImplementationExt0<csTargetRSType, csBaseRenderStepType>
{
public:
  csTargetRSType (iBase* p);

  virtual csPtr<iRenderStepFactory> NewFactory();
};

class csTargetRSLoader :
  public scfImplementationExt0<csTargetRSLoader, csBaseRenderStepLoader>
{
  csRenderStepParser rsp;

  csStringHash tokens;
#define CS_TOKEN_ITEM_FILE "plugins/engine/renderloop/stdsteps/target.tok"
#include "cstool/tokenlist.h"

public:
  csTargetRSLoader (iBase* p);

  virtual bool Initialize (iObjectRegistry* object_reg);

  virtual csPtr<iBase> Parse (iDocumentNode* node, 
    iStreamSource*, iLoaderContext* ldr_context, 	
    iBase* context);

  virtual bool IsThreadSafe() { return true; }
};

class csTargetRenderStepFactory :
  public scfImplementation1<csTargetRenderStepFactory, iRenderStepFactory>
{
private:
  iObjectRegistry* object_reg;

public:
  csTargetRenderStepFactory (iObjectRegistry* object_reg);
  virtual ~csTargetRenderStepFactory ();

  virtual csPtr<iRenderStep> Create ();
};

class csTargetRenderStep :
  public scfImplementation2<csTargetRenderStep,
    iRenderStep, iRenderStepContainer>
{
private:
  csRefArray<iRenderStep> steps;
  csWeakRef<iEngine> engine;
  csString target;
  bool doCreate;
  int newW, newH;
  bool persistent;

public:
  csTargetRenderStep (iObjectRegistry* object_reg);
  virtual ~csTargetRenderStep ();

  virtual void Perform (iRenderView* rview, iSector* sector,
    csShaderVariableStack& stack);

  virtual size_t AddStep (iRenderStep* step);
  virtual bool DeleteStep (iRenderStep* step);
  virtual iRenderStep* GetStep (size_t n) const;
  virtual size_t Find (iRenderStep* step) const;
  virtual size_t GetStepCount () const;

  void SetTarget (const char* t)
  { target = t; }
  void SetCreate (int w, int h)
  { doCreate = true; newW = w; newH = h; }
  void SetPersistent (bool p)
  { persistent = p; }
};

#endif // __CS_TARGET_H__
