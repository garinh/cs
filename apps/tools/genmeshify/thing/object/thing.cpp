/*
    Copyright (C) 1998-2002 by Jorrit Tyberghein

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
#include <limits.h>
#include "csgeom/frustum.h"
#include "csgeom/math3d.h"
#include "csgeom/poly3d.h"
#include "csgeom/polypool.h"
#include "csgeom/sphere.h"
#include "csgeom/subrec.h"
#include "csgeom/trimesh.h"
#include "cstool/rviewclipper.h"
#include "csgfx/shadervarcontext.h"
#include "csqint.h"
#include "csqsqrt.h"
#include "csutil/array.h"
#include "csutil/cfgacc.h"
#include "csutil/csendian.h"
#include "csutil/csmd5.h"
#include "csutil/csstring.h"
#include "csutil/dirtyaccessarray.h"
#include "csutil/hash.h"
#include "csutil/memfile.h"
#include "csutil/timer.h"
#include "csutil/weakref.h"
#include "iengine/camera.h"
#include "iengine/engine.h"
#include "iengine/light.h"
#include "iengine/material.h"
#include "iengine/mesh.h"
#include "iengine/movable.h"
#include "iengine/rview.h"
#include "iengine/sector.h"
#include "iengine/texture.h"
#include "igraphic/imageio.h"
#include "iutil/cache.h"
#include "iutil/comp.h"
#include "iutil/eventh.h"
#include "iutil/strset.h"
#include "iutil/verbositymanager.h"
#include "iutil/vfs.h"
#include "ivaria/reporter.h"
#include "ivideo/graph3d.h"
#include "ivideo/rendermesh.h"
#include "ivideo/texture.h"
#include "ivideo/txtmgr.h"
#include "lppool.h"
#include "polygon.h"
#include "polyrender.h"
#include "polytext.h"
#include "thing.h"
#include "ivaria/collider.h"
#include "ivaria/decal.h"

#ifdef CS_DEBUG
  //#define LIGHTMAP_DEBUG
#endif

CS_LEAKGUARD_IMPLEMENT (csThingStatic);
CS_LEAKGUARD_IMPLEMENT (csThing);

int csThing::lightmap_quality = 3;
bool csThing::lightmap_enabled = true;
bool csThingObjectType::do_verbose = false;

//---------------------------------------------------------------------------

class csPolygonHandle : 
  public scfImplementation1<csPolygonHandle, 
			    iPolygonHandle>
{
private:
  csWeakRef<iThingFactoryState> factstate;
  csWeakRef<iMeshObjectFactory> factory;
  csWeakRef<iThingState> objstate;
  csWeakRef<iMeshObject> obj;
  int index;

public:
  csPolygonHandle (
        iThingFactoryState* factstate, iMeshObjectFactory* factory,
        iThingState* objstate, iMeshObject* obj,
	int index) : scfImplementationType (this)
  {
    csPolygonHandle::factstate = factstate;
    csPolygonHandle::factory = factory;
    csPolygonHandle::objstate = objstate;
    csPolygonHandle::obj = obj;
    csPolygonHandle::index = index;
  }
  virtual ~csPolygonHandle ()
  {
  }

  virtual iThingFactoryState* GetThingFactoryState () const
  {
    return factstate;
  }
  virtual iMeshObjectFactory* GetMeshObjectFactory () const
  {
    return factory;
  }
  virtual iThingState* GetThingState () const
  {
    return objstate;
  }
  virtual iMeshObject* GetMeshObject () const
  {
    return obj;
  }
  virtual int GetIndex () const
  {
    return index;
  }
};

//---------------------------------------------------------------------------

int csThing:: last_thing_id = 0;

CS::ShaderVarStringID csThingStatic::texLightmapName = CS::InvalidShaderVarStringID;

csThingStatic::csThingStatic (iBase* parent, csThingObjectType* thing_type) :
  scfImplementationType (this, parent),
  last_range (0, -1),
  static_polygons (32)
{
  csThingStatic::thing_type = thing_type;
  static_polygons.SetThingType (thing_type);

  csRef<TriMeshHelper> trimesh;
  trimesh.AttachNew (new TriMeshHelper (0));
  trimesh->SetThing (this);
  SetTriangleData (thing_type->base_id, trimesh);
  trimesh.AttachNew (new TriMeshHelper (CS_POLY_COLLDET));
  trimesh->SetThing (this);
  SetTriangleData (thing_type->colldet_id, trimesh);
  trimesh.AttachNew (new TriMeshHelper (CS_POLY_VISCULL));
  trimesh->SetThing (this);
  SetTriangleData (thing_type->viscull_id, trimesh);

  max_vertices = num_vertices = 0;
  obj_verts = 0;
  obj_normals = 0;

  cosinus_factor = -1;
  logparent = 0;
  thingmesh_type = thing_type;

  mixmode = (uint)~0;   // Just a marker meaning not set.

  r3d = csQueryRegistry<iGraphics3D> (thing_type->object_reg);

  if ((texLightmapName == CS::InvalidShaderVarStringID))
  {
    texLightmapName = thing_type->stringsetSvName->Request ("tex lightmap");
  }
}

csThingStatic::~csThingStatic ()
{
  delete[] obj_verts;
  delete[] obj_normals;

  UnprepareLMLayout ();
}

void csThingStatic::InvalidateShape ()
{
  ShapeChanged ();
  polyRenderers.Empty ();
}

void csThingStatic::Prepare (iBase* thing_logparent)
{
  if (!IsPrepared())
  {
    SetPrepared (true);

    if (!flags.Check (CS_THING_NOCOMPRESS))
    {
      CompressVertices ();
      RemoveUnusedVertices ();
    }

    if (IsSmoothed())
      CalculateNormals();

    size_t i;
    csPolygon3DStatic* sp;
    for (i = 0; i < static_polygons.GetSize (); i++)
    {
      sp = static_polygons.Get (i);
      // If a Finish() call returns false this means the textures are not
      // completely ready yet. In that case we set 'prepared' to false
      // again so that we force a new prepare later.
      if (!sp->Finish (thing_logparent))
        SetPrepared (false);
    }
    static_polygons.ShrinkBestFit ();
  }

  if (IsPrepared())
  {
    PrepareLMLayout ();
  }
}

static int CompareStaticPolyGroups (
  csThingStatic::csStaticPolyGroup* const& pg1,
  csThingStatic::csStaticPolyGroup* const& pg2)
{
  float r1 = (float)pg1->totalLumels / (float)pg1->numLitPolys;
  float r2 = (float)pg2->totalLumels / (float)pg2->numLitPolys;

  float rd = r2 - r1;
  if (rd > EPSILON)
  {
    return 1;
  }
  else if (rd < -EPSILON)
  {
    return -1;
  }
  else
  {
    return ((uint8*)pg2 - (uint8*)pg1);
  }
}

void csThingStatic::PrepareLMLayout ()
{
  if (IsLmPrepared()) return;

  csHash<csStaticPolyGroup*, csPtrKey<iMaterialWrapper> > polysSorted;

  int i;
  for (i = 0; i < (int)static_polygons.GetSize (); i++)
  {
    int polyIdx = i;
    csPolygon3DStatic* sp = static_polygons.Get (polyIdx);

    iMaterialWrapper* mat = sp->GetMaterialWrapper ();

    csStaticPolyGroup* lp = polysSorted.Get (mat, 0);
    if (lp == 0)
    {
      lp = new csStaticPolyGroup;
      lp->material = mat;
      lp->numLitPolys = 0;
      lp->totalLumels = 0;
      polysSorted.Put (mat, lp);
    }

    csPolyTextureMapping* lmi = sp->GetTextureMapping ();
    if ((lmi != 0) &&
        (csThing::lightmap_enabled && sp->flags.Check (CS_POLY_LIGHTING)))
    {
      lp->numLitPolys++;

      int lmw = (csLightMap::CalcLightMapWidth (lmi->GetLitWidth ()));
      int lmh = (csLightMap::CalcLightMapHeight (lmi->GetLitHeight ()));
      lp->totalLumels += lmw * lmh;
    }

    lp->polys.Push (polyIdx);
  }

  /*
   * Presort polys.
   */
  csArray<csStaticPolyGroup*> polys;
  {
    csHash<csStaticPolyGroup*, csPtrKey<iMaterialWrapper> >::GlobalIterator
      polyIt = polysSorted.GetIterator ();

    while (polyIt.HasNext ())
    {
      csStaticPolyGroup* lp = polyIt.Next ();
      polys.InsertSorted (lp, CompareStaticPolyGroups);
    }
  }

  csStaticPolyGroup* rejectedPolys = new csStaticPolyGroup;
  for (i = 0; i < (int)polys.GetSize (); i++)
  {
    csStaticPolyGroup* lp = polys[i];
    lp->polys.ShrinkBestFit ();

    if (lp->numLitPolys == 0)
    {
      unlitPolys.Push (lp);
    }
    else
    {
      DistributePolyLMs (*lp, litPolys, rejectedPolys);
      if (rejectedPolys->polys.GetSize () > 0)
      {
        unlitPolys.Push (rejectedPolys);
        rejectedPolys = new csStaticPolyGroup;
      }

      delete lp;
    }
  }
  delete rejectedPolys;

  litPolys.ShrinkBestFit ();
  unlitPolys.ShrinkBestFit ();

  for (i = 0 ; i < (int)litPolys.GetSize () ; i++)
  {
    StaticSuperLM* slm = litPolys[i]->staticSLM;
    delete slm->rects;
    slm->rects = 0;
  }

  SetLmPrepared (true);
}

#ifdef LIGHTMAP_DEBUG
#define LM_BORDER 1
#else
#define LM_BORDER 0
#endif

static int CompareStaticSuperLM (csThingStatic::StaticSuperLM* const& slm1,
                                 csThingStatic::StaticSuperLM* const& slm2)
{
  int d = slm2->freeLumels - slm1->freeLumels;
  if (d != 0) return d;
  return ((uint8*)slm2 - (uint8*)slm1);
}

// @@@ urg
static csPolygonStaticArray* static_poly_array = 0;

static int CompareStaticPolys (int const& i1, int const& i2)
{
  csPolygon3DStatic* const poly1 = (*static_poly_array)[i1];
  csPolygon3DStatic* const poly2 = (*static_poly_array)[i2];
  csPolyTextureMapping* lm1 = poly1->GetTextureMapping ();
  csPolyTextureMapping* lm2 = poly2->GetTextureMapping ();

  int maxdim1, mindim1, maxdim2, mindim2;

  maxdim1 = MAX (
    csLightMap::CalcLightMapWidth (lm1->GetLitWidth ()),
    csLightMap::CalcLightMapHeight (lm1->GetLitHeight ()));
  mindim1 = MIN (
    csLightMap::CalcLightMapWidth (lm1->GetLitWidth ()),
    csLightMap::CalcLightMapHeight (lm1->GetLitHeight ()));
  maxdim2 = MAX (
    csLightMap::CalcLightMapWidth (lm2->GetLitWidth ()),
    csLightMap::CalcLightMapHeight (lm2->GetLitHeight ()));
  mindim2 = MIN (
    csLightMap::CalcLightMapWidth (lm2->GetLitWidth ()),
    csLightMap::CalcLightMapHeight (lm2->GetLitHeight ()));

  if (maxdim1 == maxdim2)
  {
    return (mindim1 - mindim2);
  }

  return (maxdim1 - maxdim2);
}

void csThingStatic::DistributePolyLMs (
        const csStaticPolyGroup& inputPolys,
        csPDelArray<csStaticLitPolyGroup>& outputPolys,
        csStaticPolyGroup* rejectedPolys)
{
  struct InternalPolyGroup : public csStaticPolyGroup
  {
    int totalLumels;
    int maxlmw, maxlmh;
    int minLMArea;
  };

  // Polys that couldn't be fit onto a SLM are processed again.
  InternalPolyGroup inputQueues[2];
  int curQueue = 0;

  size_t i;

  static_poly_array = &static_polygons;

  rejectedPolys->material = inputPolys.material;
  inputQueues[0].material = inputPolys.material;
  inputQueues[0].totalLumels = 0;
  inputQueues[0].maxlmw = 0;
  inputQueues[0].maxlmh = 0;
  inputQueues[0].minLMArea = INT_MAX;
  inputQueues[1].material = inputPolys.material;
  // Sort polys and filter out oversized polys on the way
  for (i = 0; i < inputPolys.polys.GetSize (); i++)
  {
    int polyIdx  = inputPolys.polys[i];
    csPolygon3DStatic* sp = static_polygons[polyIdx];

    csPolyTextureMapping* lm = sp->GetTextureMapping ();
    if ((lm == 0) || (!csThing::lightmap_enabled) ||
        !sp->flags.Check (CS_POLY_LIGHTING))
    {
      sp->polygon_data.useLightmap = false;
      rejectedPolys->polys.Push (polyIdx);
      continue;
    }

    int lmw = (csLightMap::CalcLightMapWidth (lm->GetLitWidth ())
        + LM_BORDER);
    int lmh = (csLightMap::CalcLightMapHeight (lm->GetLitHeight ())
        + LM_BORDER);

    if ((lmw > thing_type->maxLightmapW) ||
      (lmh > thing_type->maxLightmapH))
    {
      sp->polygon_data.useLightmap = false;
      rejectedPolys->polys.Push (polyIdx);
    }
    else
    {
      inputQueues[0].totalLumels += (lmw * lmh);
      inputQueues[0].maxlmw = MAX (inputQueues[0].maxlmw, lmw);
      inputQueues[0].maxlmh = MAX (inputQueues[0].maxlmh, lmh);
      inputQueues[0].minLMArea = MIN(inputQueues[0].minLMArea, lmw * lmh);
      inputQueues[0].polys.InsertSorted (polyIdx, CompareStaticPolys);
    }
  }

  csStaticLitPolyGroup* curOutputPolys = new csStaticLitPolyGroup;
  while (inputQueues[curQueue].polys.GetSize () > 0)
  {
    // Try to fit as much polys as possible into the SLMs.
    size_t s = 0;
    while ((s<superLMs.GetSize ()) && (inputQueues[curQueue].polys.GetSize ()>0))
    {
      StaticSuperLM* slm = superLMs[s];

      /*
       * If the number of free lumels is less than the number of lumels in
       * the smallest LM, we can break testing SLMs. SLMs are sorted by free
       * lumels, so subsequent SLMs won't have any more free space.
       */
      if (slm->freeLumels < inputQueues[curQueue].minLMArea)
      {
        break;
      }

      curOutputPolys->staticSLM = slm;
      curOutputPolys->material = inputQueues[curQueue].material;

      inputQueues[curQueue ^ 1].totalLumels = 0;
      inputQueues[curQueue ^ 1].maxlmw = 0;
      inputQueues[curQueue ^ 1].maxlmh = 0;
      inputQueues[curQueue ^ 1].minLMArea = INT_MAX;

      while (inputQueues[curQueue].polys.GetSize () > 0)
      {
        bool stuffed = false;
        CS::SubRectangles::SubRect* slmSR;
        int polyIdx = inputQueues[curQueue].polys.Pop ();
        csPolygon3DStatic* sp = static_polygons[polyIdx];

        csPolyTextureMapping* lm = sp->GetTextureMapping ();

        int lmw = (csLightMap::CalcLightMapWidth (lm->GetLitWidth ())
                + LM_BORDER);
        int lmh = (csLightMap::CalcLightMapHeight (lm->GetLitHeight ())
                + LM_BORDER);

        csRect r;
        if ((lmw * lmh) <= slm->freeLumels)
        {
          if ((slmSR = slm->GetRects ()->Alloc (lmw, lmh, r)) != 0)
          {
            r.xmax -= LM_BORDER;
            r.ymax -= LM_BORDER;
            stuffed = true;
            slm->freeLumels -= (lmw * lmh);
          }
        }

        if (stuffed)
        {
          curOutputPolys->polys.Push (polyIdx);
          curOutputPolys->lmRects.Push (r);
        }
        else
        {
          inputQueues[curQueue ^ 1].polys.InsertSorted (
            polyIdx, CompareStaticPolys);
          inputQueues[curQueue ^ 1].totalLumels += (lmw * lmh);
          inputQueues[curQueue ^ 1].maxlmw =
            MAX (inputQueues[curQueue ^ 1].maxlmw, lmw);
          inputQueues[curQueue ^ 1].maxlmh =
            MAX (inputQueues[curQueue ^ 1].maxlmh, lmh);
          inputQueues[curQueue ^ 1].minLMArea =
            MIN(inputQueues[curQueue ^ 1].minLMArea, lmw * lmh);
        }
      }
      superLMs.DeleteIndex (s);
      size_t nidx = superLMs.InsertSorted (slm, CompareStaticSuperLM);
      if (nidx <= s + 1)
      {
        s++;
      }

      if (curOutputPolys->polys.GetSize () > 0)
      {
        curOutputPolys->lmRects.ShrinkBestFit ();
        curOutputPolys->polys.ShrinkBestFit ();
        outputPolys.Push (curOutputPolys);
        curOutputPolys = new csStaticLitPolyGroup;
      }

      curQueue ^= 1;
    }

    // Not all polys could be stuffed away, so we possibly need more space.
    if (inputQueues[curQueue].polys.GetSize () > 0)
    {
      // Try if enlarging an existing SLM suffices.
      bool foundNew = false;
      s = superLMs.GetSize ();
      while (s > 0)
      {
        s--;

        StaticSuperLM* slm = superLMs[s];
        int usedLumels = (slm->width * slm->height) - slm->freeLumels;

        int neww = (slm->width > slm->height) ? slm->width : slm->width*2;
        int newh = (slm->width > slm->height) ? slm->height*2 : slm->height;

        if ((((neww*newh) - usedLumels) >= inputQueues[curQueue].totalLumels) &&
          (((float)(usedLumels + inputQueues[curQueue].totalLumels) /
          (float)(neww * newh)) > (1.0f - thing_type->maxSLMSpaceWaste)) &&
          (neww <= thing_type->maxLightmapW) &&
          (newh <= thing_type->maxLightmapH))
        {
          superLMs.DeleteIndex (s);
          slm->Grow (neww, newh);
          superLMs.InsertSorted (slm, CompareStaticSuperLM);
          foundNew = true;
          break;
        }
      }

      // Otherwise, add a new empty SLM.
      if (!foundNew)
      {
        int lmW = csFindNearestPowerOf2 (inputQueues[curQueue].maxlmw);
        int lmH = csFindNearestPowerOf2 (inputQueues[curQueue].maxlmh);

        while (inputQueues[curQueue].totalLumels > (lmW * lmH))
        {
          if (lmH < lmW)
            lmH *= 2;
          else
            lmW *= 2;
        }
        StaticSuperLM* newslm = new StaticSuperLM (lmW, lmH);
        superLMs.InsertSorted (newslm, CompareStaticSuperLM);
      }
    }
  }
  delete curOutputPolys;

  //superLMs.ShrinkBestFit ();

  for (i = 0; i < litPolys.GetSize (); i++)
  {
    StaticSuperLM* slm = litPolys[i]->staticSLM;
    for (size_t j = 0; j < litPolys[i]->polys.GetSize (); j++)
    {
      csPolygon3DStatic* sp = static_polygons[litPolys[i]->polys[j]];
      const csRect& r = litPolys[i]->lmRects[j];

      sp->polygon_data.useLightmap = true;
      float lmu1, lmv1, lmu2, lmv2;
      float islmW = 1.0f / (float)slm->width;
      float islmH = 1.0f / (float)slm->height;
      // Those offsets seem to result in a look similar to the software
      // renderer... but not perfect yet.
      lmu1 = ((float)r.xmin + 0.5f) * islmW;
      lmv1 = ((float)r.ymin + 0.5f) * islmH;
      lmu2 = ((float)r.xmax - 1.0f) * islmW;
      lmv2 = ((float)r.ymax - 1.0f) * islmH;
      sp->polygon_data.tmapping->SetCoordsOnSuperLM (
        lmu1, lmv1, lmu2, lmv2);
    }
  }
}

void csThingStatic::UnprepareLMLayout ()
{
  if (!IsLmPrepared()) return;
  litPolys.DeleteAll ();
  unlitPolys.DeleteAll ();

  size_t i;
  for (i = 0; i < superLMs.GetSize (); i++)
  {
    StaticSuperLM* sslm = superLMs[i];
    delete sslm;
  }
  superLMs.DeleteAll ();
  SetLmPrepared (false);
}

int csThingStatic::AddVertex (float x, float y, float z)
{
  if (!obj_verts)
  {
    max_vertices = 10;
    obj_verts = new csVector3[max_vertices];
  }

  while (num_vertices >= max_vertices)
  {
    if (max_vertices < 10000)
      max_vertices *= 2;
    else
      max_vertices += 10000;

    csVector3 *new_obj_verts = new csVector3[max_vertices];
    memcpy (new_obj_verts, obj_verts, sizeof (csVector3) * num_vertices);
    delete[] obj_verts;
    obj_verts = new_obj_verts;
  }

  obj_verts[num_vertices].Set (x, y, z);
  num_vertices++;
  InvalidateShape ();
  return num_vertices - 1;
}

void csThingStatic::SetVertex (int idx, const csVector3 &vt)
{
  CS_ASSERT (idx >= 0 && idx < num_vertices);
  obj_verts[idx] = vt;
  InvalidateShape ();
}

void csThingStatic::DeleteVertex (int idx)
{
  CS_ASSERT (idx >= 0 && idx < num_vertices);

  int copysize = sizeof (csVector3) * (num_vertices - idx - 1);
  memmove (obj_verts + idx, obj_verts + idx + 1, copysize);
  InvalidateShape ();
}

void csThingStatic::DeleteVertices (int from, int to)
{
  if (from <= 0 && to >= num_vertices - 1)
  {
    // Delete everything.
    delete[] obj_verts;
    max_vertices = num_vertices = 0;
    obj_verts = 0;
  }
  else
  {
    if (from < 0) from = 0;
    if (to >= num_vertices) to = num_vertices - 1;

    int rangelen = to - from + 1;
    int copysize = sizeof (csVector3) * (num_vertices - from - rangelen);
    memmove (obj_verts + from,
        obj_verts + from + rangelen, copysize);
    num_vertices -= rangelen;
  }

  InvalidateShape ();
}

void csThingStatic::CompressVertices ()
{
  csVector3* new_obj;
  size_t count_unique;
  csCompressVertex* vt = csVector3Array::CompressVertices (
        obj_verts, num_vertices, new_obj, count_unique);
  if (vt == 0) return;

  // Replace the old vertex tables.
  delete[] obj_verts;
  obj_verts = new_obj;
  num_vertices = max_vertices = (int)count_unique;

  // Now we can remap the vertices in all polygons.
  size_t i;
  int j;
  for (i = 0; i < static_polygons.GetSize (); i++)
  {
    csPolygon3DStatic *p = static_polygons.Get (i);
    int *idx = p->GetVertexIndices ();
    for (j = 0; j < p->GetVertexCount (); j++) idx[j] = (int)vt[idx[j]].new_idx;
  }

  delete[] vt;
  InvalidateShape ();
}

void csThingStatic::RemoveUnusedVertices ()
{
  if (num_vertices <= 0) return ;

  // Copy all the vertices that are actually used by polygons.
  bool *used = new bool[num_vertices];
  int i, j;
  size_t k;
  for (i = 0; i < num_vertices; i++) used[i] = false;

  // Mark all vertices that are used as used.
  for (k = 0; k < static_polygons.GetSize (); k++)
  {
    csPolygon3DStatic *p = static_polygons.Get (k);
    int *idx = p->GetVertexIndices ();
    for (j = 0; j < p->GetVertexCount (); j++) used[idx[j]] = true;
  }

  // Count relevant values.
  int count_relevant = 0;
  for (i = 0; i < num_vertices; i++)
  {
    if (used[i]) count_relevant++;
  }

  // If all vertices are relevant then there is nothing to do.
  if (count_relevant == num_vertices)
  {
    delete[] used;
    return ;
  }

  // Now allocate and fill new vertex tables.
  // Also fill the 'relocate' table.
  csVector3 *new_obj = new csVector3[count_relevant];
  int *relocate = new int[num_vertices];
  j = 0;
  for (i = 0; i < num_vertices; i++)
  {
    if (used[i])
    {
      new_obj[j] = obj_verts[i];
      relocate[i] = j;
      j++;
    }
    else
      relocate[i] = -1;
  }

  // Replace the old vertex tables.
  delete[] obj_verts;
  obj_verts = new_obj;
  num_vertices = max_vertices = count_relevant;

  // Now we can remap the vertices in all polygons.
  for (k = 0; k < static_polygons.GetSize (); k++)
  {
    csPolygon3DStatic *p = static_polygons.Get (k);
    int *idx = p->GetVertexIndices ();
    for (j = 0; j < p->GetVertexCount (); j++) idx[j] = relocate[idx[j]];
  }

  delete[] relocate;
  delete[] used;

  SetObjBboxValid (false);
  InvalidateShape ();
}

struct PolygonsForVertex
{
  csArray<int> poly_indices;
};

void csThingStatic::CalculateNormals ()
{
  int polyCount = (int)static_polygons.GetSize ();
  int i, k;

  delete[] obj_normals;
  obj_normals = new csVector3[num_vertices];
  memset (obj_normals, 0, sizeof (csVector3)*num_vertices);

  for (i = 0 ; i < polyCount ; i++)
  {
    csPolygon3DStatic* p = static_polygons.Get (i);
    const csVector3& normal = p->GetObjectPlane ().Normal();
    int* vtidx = p->GetVertexIndices ();
    for (k = 0 ; k < p->GetVertexCount () ; k++)
    {
      CS_ASSERT (vtidx[k] >= 0 && vtidx[k] < num_vertices);
      obj_normals[vtidx[k]] += normal;
    }
  }

  // Now calculate normals.
  for (i = 0 ; i < num_vertices ; i++)
  {
    obj_normals[i].Normalize ();
  }
}

int csThingStatic::AddPolygon (csPolygon3DStatic* spoly)
{
  spoly->SetParent (this);
  spoly->EnableTextureMapping (true);
  int idx = (int)static_polygons.Push (spoly);
  InvalidateShape ();
  UnprepareLMLayout ();
  return idx;
}

void csThingStatic::RemovePolygon (int idx)
{
  static_polygons.FreeItem (static_polygons.Get (idx));
  static_polygons.DeleteIndex (idx);
  InvalidateShape ();
  UnprepareLMLayout ();
}

void csThingStatic::RemovePolygons ()
{
  static_polygons.FreeAll ();
  InvalidateShape ();
  UnprepareLMLayout ();
}

int csThingStatic::IntersectSegmentIndex (
  const csVector3 &start, const csVector3 &end,
  csVector3 &isect,
  float *pr)
{
  return 0;
}

csPtr<csThingStatic> csThingStatic::CloneStatic ()
{
  csThingStatic* clone = new csThingStatic (GetSCFParent (), thing_type);
  clone->flags.SetAll (GetFlags ().Get ());
  clone->SetSmoothed (IsSmoothed());
  clone->obj_bbox = obj_bbox;
  clone->SetObjBboxValid (IsObjBboxValid ());
  clone->obj_radius = obj_radius;
  clone->SetPrepared (IsPrepared());
  clone->SetShapeNumber (GetShapeNumber ());
  clone->cosinus_factor = cosinus_factor;

  clone->num_vertices = num_vertices;
  clone->max_vertices = max_vertices;
  if (obj_verts)
  {
    clone->obj_verts = new csVector3[max_vertices];
    memcpy (clone->obj_verts, obj_verts, sizeof (csVector3)*num_vertices);
  }
  else
  {
    clone->obj_verts = 0;
  }
  if (obj_normals)
  {
    clone->obj_normals = new csVector3[max_vertices];
    memcpy (clone->obj_normals, obj_normals, sizeof (csVector3)*num_vertices);
  }
  else
  {
    clone->obj_normals = 0;
  }

  size_t i;
  for (i = 0 ; i < static_polygons.GetSize () ; i++)
  {
    csPolygon3DStatic* p = static_polygons.Get (i)->Clone (clone);
    clone->static_polygons.Push (p);
  }

  return csPtr<csThingStatic> (clone);
}

csPtr<iMeshObjectFactory> csThingStatic::Clone ()
{
  csRef<csThingStatic> clone=CloneStatic ();
  return csPtr<iMeshObjectFactory> (clone);
}

void csThingStatic::HardTransform (const csReversibleTransform &t)
{
  int i;

  for (i = 0; i < num_vertices; i++)
  {
    obj_verts[i] = t.This2Other (obj_verts[i]);
  }

  //-------
  // Now transform the polygons.
  //-------
  for (int j = 0; j < (int)static_polygons.GetSize (); j++)
  {
    csPolygon3DStatic *p = GetPolygon3DStatic (j);
    p->HardTransform (t);
  }

  InvalidateShape ();
  SetObjBboxValid (false);
}

csPtr<iMeshObject> csThingStatic::NewInstance ()
{
  csThing *thing = new csThing ((iBase*)(iThingFactoryState*)this, this);
  if (mixmode != (uint)~0)
    thing->SetMixMode (mixmode);
  return csPtr<iMeshObject> ((iMeshObject*)thing);
}

void csThingStatic::SetBoundingBox (const csBox3 &box)
{
  SetObjBboxValid (true);
  obj_bbox = box;
  ShapeChanged ();
}

const csBox3& csThingStatic::GetBoundingBox ()
{
  int i;

  if (IsObjBboxValid())
    return obj_bbox;

  SetObjBboxValid (true);

  if (!obj_verts || num_vertices <= 0)
  {
    obj_bbox.Set (0, 0, 0, 0, 0, 0);
    obj_radius = 0.0f;
    return obj_bbox;
  }

  obj_bbox.StartBoundingBox (obj_verts[0]);
  for (i = 1; i < num_vertices; i++)
    obj_bbox.AddBoundingVertexSmart (obj_verts[i]);

  obj_radius = csQsqrt (csSquaredDist::PointPoint (
        obj_bbox.Max (), obj_bbox.Min ())) * 0.5f;
  return obj_bbox;
}

void csThingStatic::GetRadius (float &rad, csVector3 &cent)
{
  const csBox3& b = GetBoundingBox ();
  rad = obj_radius;
  cent = b.GetCenter ();
}

void csThingStatic::FillRenderMeshes (csThing*,
        csDirtyAccessArray<csRenderMesh*>&,
        const csArray<RepMaterial>&,
        uint)
{
}

int csThingStatic::FindPolygonByName (const char* name)
{
  return (int)static_polygons.FindKey (static_polygons.KeyCmp(name));
}

int csThingStatic::GetRealIndex (int requested_index) const
{
  if (requested_index == -1)
  {
    CS_ASSERT (last_range.end != -1);
    return last_range.end;
  }
  return requested_index;
}

void csThingStatic::GetRealRange (const csPolygonRange& requested_range,
        int& start, int& end)
{
  if (requested_range.start == -1)
  {
    start = last_range.start;
    end = last_range.end;
    CS_ASSERT (end != -1);
    return;
  }
  start = requested_range.start;
  end = requested_range.end;
  if (start < 0) start = 0;
  if ((size_t)end >= static_polygons.GetSize ())
    end = (int)static_polygons.GetSize ()-1;
}

int csThingStatic::AddEmptyPolygon ()
{
  csPolygon3DStatic* sp = thing_type->blk_polygon3dstatic.Alloc ();
  int idx = AddPolygon (sp);
  last_range.Set (idx);
  return idx;
}

int csThingStatic::AddTriangle (const csVector3& v1, const csVector3& v2,
        const csVector3& v3)
{
  int idx = AddEmptyPolygon ();
  csPolygon3DStatic* sp = static_polygons[idx];
  sp->SetNumVertices (3);
  sp->SetVertex (0, v1);
  sp->SetVertex (1, v2);
  sp->SetVertex (2, v3);
  last_range.Set (idx);
  sp->SetTextureSpace (v1, v2, 1);
  SetObjBboxValid(false);
  return idx;
}

int csThingStatic::AddQuad (const csVector3& v1, const csVector3& v2,
        const csVector3& v3, const csVector3& v4)
{
  int idx = AddEmptyPolygon ();
  csPolygon3DStatic* sp = static_polygons[idx];
  sp->SetNumVertices (4);
  sp->SetVertex (0, v1);
  sp->SetVertex (1, v2);
  sp->SetVertex (2, v3);
  sp->SetVertex (3, v4);
  last_range.Set (idx);
  sp->SetTextureSpace (v1, v2, 1);
  SetObjBboxValid(false);
  return idx;
}

int csThingStatic::AddPolygon (csVector3* vertices, int num)
{
  int idx = AddEmptyPolygon ();
  csPolygon3DStatic* sp = static_polygons[idx];
  sp->SetNumVertices (num);
  int i;
  for (i = 0 ; i < num ; i++)
  {
    sp->SetVertex (i, vertices[i]);
  }
  last_range.Set (idx);
  sp->SetTextureSpace (vertices[0], vertices[1], 1);
  SetObjBboxValid(false);
  return idx;
}

int csThingStatic::AddPolygon (int num, ...)
{
  int idx = AddEmptyPolygon ();
  csPolygon3DStatic* sp = static_polygons[idx];
  sp->SetNumVertices (num);
  va_list arg;
  va_start (arg, num);
  int i;
  for (i = 0 ; i < num ; i++)
  {
    int v = va_arg (arg, int);
    sp->SetVertex (i, v);
  }
  va_end (arg);
  last_range.Set (idx);
  sp->SetTextureSpace (sp->Vobj (0), sp->Vobj (1), 1);
  return idx;
}

int csThingStatic::AddOutsideBox (const csVector3& bmin, const csVector3& bmax)
{
  csBox3 box (bmin, bmax);
  int firstidx = AddQuad (
        box.GetCorner (CS_BOX_CORNER_xYz),
        box.GetCorner (CS_BOX_CORNER_XYz),
        box.GetCorner (CS_BOX_CORNER_Xyz),
        box.GetCorner (CS_BOX_CORNER_xyz));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_XYz),
        box.GetCorner (CS_BOX_CORNER_XYZ),
        box.GetCorner (CS_BOX_CORNER_XyZ),
        box.GetCorner (CS_BOX_CORNER_Xyz));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_XYZ),
        box.GetCorner (CS_BOX_CORNER_xYZ),
        box.GetCorner (CS_BOX_CORNER_xyZ),
        box.GetCorner (CS_BOX_CORNER_XyZ));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_xYZ),
        box.GetCorner (CS_BOX_CORNER_xYz),
        box.GetCorner (CS_BOX_CORNER_xyz),
        box.GetCorner (CS_BOX_CORNER_xyZ));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_xyz),
        box.GetCorner (CS_BOX_CORNER_Xyz),
        box.GetCorner (CS_BOX_CORNER_XyZ),
        box.GetCorner (CS_BOX_CORNER_xyZ));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_xYZ),
        box.GetCorner (CS_BOX_CORNER_XYZ),
        box.GetCorner (CS_BOX_CORNER_XYz),
        box.GetCorner (CS_BOX_CORNER_xYz));

  last_range.Set (firstidx, firstidx+5);
  SetObjBboxValid(false);
  return firstidx;
}

int csThingStatic::AddInsideBox (const csVector3& bmin, const csVector3& bmax)
{
  csBox3 box (bmin, bmax);
  int firstidx = AddQuad (
        box.GetCorner (CS_BOX_CORNER_xyz),
        box.GetCorner (CS_BOX_CORNER_Xyz),
        box.GetCorner (CS_BOX_CORNER_XYz),
        box.GetCorner (CS_BOX_CORNER_xYz));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_Xyz),
        box.GetCorner (CS_BOX_CORNER_XyZ),
        box.GetCorner (CS_BOX_CORNER_XYZ),
        box.GetCorner (CS_BOX_CORNER_XYz));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_XyZ),
        box.GetCorner (CS_BOX_CORNER_xyZ),
        box.GetCorner (CS_BOX_CORNER_xYZ),
        box.GetCorner (CS_BOX_CORNER_XYZ));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_xyZ),
        box.GetCorner (CS_BOX_CORNER_xyz),
        box.GetCorner (CS_BOX_CORNER_xYz),
        box.GetCorner (CS_BOX_CORNER_xYZ));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_xyZ),
        box.GetCorner (CS_BOX_CORNER_XyZ),
        box.GetCorner (CS_BOX_CORNER_Xyz),
        box.GetCorner (CS_BOX_CORNER_xyz));
  AddQuad (
        box.GetCorner (CS_BOX_CORNER_xYz),
        box.GetCorner (CS_BOX_CORNER_XYz),
        box.GetCorner (CS_BOX_CORNER_XYZ),
        box.GetCorner (CS_BOX_CORNER_xYZ));

  last_range.Set (firstidx, firstidx+5);
  SetObjBboxValid(false);
  return firstidx;
}

void csThingStatic::SetPolygonName (const csPolygonRange& range,
        const char* name)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
    static_polygons[i]->SetName (name);
}

const char* csThingStatic::GetPolygonName (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->GetName ();
}

csPtr<iPolygonHandle> csThingStatic::CreatePolygonHandle (int polygon_idx)
{
  return csPtr<iPolygonHandle> (new csPolygonHandle (
        (iThingFactoryState*)this,
        (iMeshObjectFactory*)this,
        0, 0,
        GetRealIndex (polygon_idx)));
}

void csThingStatic::SetPolygonMaterial (const csPolygonRange& range,
        iMaterialWrapper* material)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
    static_polygons[i]->SetMaterial (material);
}

iMaterialWrapper* csThingStatic::GetPolygonMaterial (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->GetMaterialWrapper ();
}

void csThingStatic::AddPolygonVertex (const csPolygonRange& range,
        const csVector3& vt)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
    static_polygons[i]->AddVertex (vt);
}

void csThingStatic::AddPolygonVertex (const csPolygonRange& range, int vt)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
    static_polygons[i]->AddVertex (vt);
}

void csThingStatic::SetPolygonVertexIndices (const csPolygonRange& range,
        int num, int* indices)
{
  int i, start, end;
  int j;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->SetNumVertices (num);
    for (j = 0 ; j < num ; j++)
      sp->SetVertex (j, indices[j]);
  }
}

int csThingStatic::GetPolygonVertexCount (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->GetVertexCount ();
}

const csVector3& csThingStatic::GetPolygonVertex (int polygon_idx,
        int vertex_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->Vobj (vertex_idx);
}

int* csThingStatic::GetPolygonVertexIndices (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->GetVertexIndices ();
}

bool csThingStatic::SetPolygonTextureMapping (const csPolygonRange& range,
        const csMatrix3& m, const csVector3& v)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
    static_polygons[i]->SetTextureSpace (m, v);
  return true;
}

bool csThingStatic::SetPolygonTextureMapping (const csPolygonRange& range,
        const csVector2& uv1, const csVector2& uv2, const csVector2& uv3)
{
  int i, start, end;
  GetRealRange (range, start, end);
  bool error = false;
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    if (!sp->SetTextureSpace (
        sp->Vobj (0), uv1,
        sp->Vobj (1), uv2,
        sp->Vobj (2), uv3))
      error = true;
  }
  return !error;
}

bool csThingStatic::SetPolygonTextureMapping (const csPolygonRange& range,
        const csVector3& p1, const csVector2& uv1,
        const csVector3& p2, const csVector2& uv2,
        const csVector3& p3, const csVector2& uv3)
{
  int i, start, end;
  GetRealRange (range, start, end);
  bool error = false;
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    if (!sp->SetTextureSpace (p1, uv1, p2, uv2, p3, uv3))
      error = true;
  }
  return !error;
}

bool csThingStatic::SetPolygonTextureMapping (const csPolygonRange& range,
        const csVector3& v_orig, const csVector3& v1, float len1)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->SetTextureSpace (v_orig, v1, len1);
  }
  return true;
}

bool csThingStatic::SetPolygonTextureMapping (const csPolygonRange& range,
        float len1)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->SetTextureSpace (sp->Vobj (0), sp->Vobj (1), len1);
  }
  return true;
}

bool csThingStatic::SetPolygonTextureMapping (const csPolygonRange& range,
        const csVector3& v_orig,
        const csVector3& v1, float len1,
        const csVector3& v2, float len2)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->SetTextureSpace (v_orig, v1, len1, v2, len2);
  }
  return true;
}

void csThingStatic::GetPolygonTextureMapping (int polygon_idx,
        csMatrix3& m, csVector3& v)
{
  static_polygons[GetRealIndex (polygon_idx)]->GetTextureSpace (m, v);
}

void csThingStatic::SetPolygonTextureMappingEnabled (
        const csPolygonRange& range, bool enabled)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->EnableTextureMapping (enabled);
  }
}

bool csThingStatic::IsPolygonTextureMappingEnabled (int polygon_idx) const
{
  return static_polygons[GetRealIndex (polygon_idx)]->IsTextureMappingEnabled ();
}

void csThingStatic::SetPolygonFlags (const csPolygonRange& range, uint32 flags)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->flags.Set (flags);
  }
}

void csThingStatic::SetPolygonFlags (const csPolygonRange& range, uint32 mask, uint32 flags)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->flags.Set (mask, flags);
  }
}

void csThingStatic::ResetPolygonFlags (const csPolygonRange& range, uint32 flags)
{
  int i, start, end;
  GetRealRange (range, start, end);
  for (i = start ; i <= end ; i++)
  {
    csPolygon3DStatic* sp = static_polygons[i];
    sp->flags.Reset (flags);
  }
}

csFlags& csThingStatic::GetPolygonFlags (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->flags;
}

const csPlane3& csThingStatic::GetPolygonObjectPlane (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->GetObjectPlane ();
}

bool csThingStatic::IsPolygonTransparent (int polygon_idx)
{
  return static_polygons[GetRealIndex (polygon_idx)]->IsTransparent ();
}

bool csThingStatic::AddPolygonRenderBuffer (int polygon_idx, const char* name,
                                            iRenderBuffer* buffer)
{
  CS::ShaderVarStringID nameID = thing_type->stringsetSvName->Request (name);
  iRenderBuffer* Template;
  if ((Template = polyBufferTemplates.GetRenderBuffer (nameID)) != 0)
  {
    if ((Template->GetComponentType() != buffer->GetComponentType())
      || (Template->GetComponentCount() != buffer->GetComponentCount()))
      return false;
  }
  else
    polyBufferTemplates.AddRenderBuffer (nameID, buffer);
  csPolygon3DStatic* sp = static_polygons[GetRealIndex (polygon_idx)];
  return sp->polyBuffers.AddRenderBuffer (nameID, buffer);
}

bool csThingStatic::PointOnPolygon (int polygon_idx, const csVector3& v)
{
  return static_polygons[GetRealIndex (polygon_idx)]->PointOnPolygon (v);
}

bool csThingStatic::GetLightmapLayout (int polygon_idx, size_t& slm, 
                                       csRect& slmSubRect, float* slmCoord)
{
  Prepare (0);
  /* The layouted polys are in a structure primarily intended for rendering,
   * not to quickly access a specific polygon. Linear search time. */
  for (size_t litPoly = 0; litPoly < litPolys.GetSize(); litPoly++)
  {
    const csStaticLitPolyGroup& polyGroup = *(litPolys[litPoly]);
    for (size_t p = 0; p < polyGroup.polys.GetSize(); p++)
    {
      if (polyGroup.polys[p] == polygon_idx)
      {
        slm = superLMs.Find (polyGroup.staticSLM);
        slmSubRect = polyGroup.lmRects[p];

        const csPolygonRenderData* static_data = 
          &static_polygons[polygon_idx]->polygon_data;
        // Compute TCs (lifted from the poly renderer)
        csMatrix3 t_m;
        csVector3 t_v;
        t_m = static_data->tmapping->GetO2T ();
        t_v = static_data->tmapping->GetO2TTranslation ();
        csTransform object2texture (t_m, t_v);

        csTransform tex2lm;
        struct csPolyLMCoords
        {
          float u1, v1, u2, v2;
        };

        csPolyLMCoords lmc;
        static_data->tmapping->GetCoordsOnSuperLM (lmc.u1, lmc.v1,
          lmc.u2, lmc.v2);

        float lm_low_u = 0.0f, lm_low_v = 0.0f;
        float lm_high_u = 1.0f, lm_high_v = 1.0f;
        static_data->tmapping->GetTextureBox (
          lm_low_u, lm_low_v, lm_high_u, lm_high_v);

        float lm_scale_u = ((lmc.u2 - lmc.u1) / (lm_high_u - lm_low_u));
        float lm_scale_v = ((lmc.v2 - lmc.v1) / (lm_high_v - lm_low_v));

        tex2lm.SetO2T (
          csMatrix3 (lm_scale_u, 0, 0,
          0, lm_scale_v, 0,
          0, 0, 1));
        tex2lm.SetO2TTranslation (
          csVector3 (
          (lm_scale_u != 0.0f) ? (lm_low_u - lmc.u1 / lm_scale_u) : 0,
          (lm_scale_v != 0.0f) ? (lm_low_v - lmc.v1 / lm_scale_v) : 0,
          0));

        csVector3* obj_verts = *(static_data->p_obj_verts);
        int j, vc = static_data->num_vertices;
        for (j = 0; j < vc; j++)
        {
          int vidx = static_data->vertices[j];
          const csVector3& vertex = obj_verts[vidx];
          csVector3 t = object2texture.Other2This (vertex);
          csVector3 l = tex2lm.Other2This (t);
          *slmCoord++ = l.x;
          *slmCoord++ = l.y;
        }
        
        return true;
      }
    }
  }
  return false;
}

//----------------------------------------------------------------------------

csThingStatic::LightmapTexAccessor::LightmapTexAccessor (csThing* instance, 
							 size_t polyIndex) :
  scfImplementationType (this), instance (instance)
{
  texh = instance->GetPolygonTexture (polyIndex);
}

void csThingStatic::LightmapTexAccessor::PreGetValue (csShaderVariable *variable)
{
  instance->UpdateDirtyLMs ();
  variable->SetValue (texh);
}

//----------------------------------------------------------------------------

csThing::csThing (iBase *parent, csThingStatic* static_data) :
  scfImplementationType (this, parent),
  polygons(32)
{
  csThing::static_data = static_data;
  polygons.SetThingType (static_data->thing_type);
  polygon_world_planes = 0;
  polygon_world_planes_num = (size_t)-1;        // -1 means not checked yet, 0 means no planes.

  last_thing_id++;
  thing_id = last_thing_id;
  logparent = 0;

  wor_verts = 0;

  dynamic_ambient_version = 0;
  light_version = 1;

  mixmode = CS_FX_COPY;

  movablenr = -1;
  wor_bbox_movablenr = -1;
  cached_movable = 0;

  cfg_moving = CS_THING_MOVE_NEVER;

  static_data_nr = 0xfffffffd;  // (static_nr of csThingStatic is init to -1)

  current_visnr = 1;

  SetLmDirty (true);
}

csThing::~csThing ()
{
  ClearLMs ();

  bool meshesCreated;
  csDirtyAccessArray<csRenderMesh*>& renderMeshes =
    meshesHolder.GetUnusedData (meshesCreated, 0);
  size_t i;
  for (i = 0; i < renderMeshes.GetSize () ; i++)
  {
    // @@@ Is this needed?
    //if (renderMeshes[i]->variablecontext != 0)
      //renderMeshes[i]->variablecontext->DecRef ();
    static_data->thing_type->blk_rendermesh.Free (renderMeshes[i]);
  }
  renderMeshes.DeleteAll ();

  if (wor_verts != static_data->obj_verts)
  {
    delete[] wor_verts;
  }

  polygons.DeleteAll ();
  delete[] polygon_world_planes;
}

csString csThing::GenerateCacheName ()
{
  csMemFile mf;
  int32 l;
  l = csLittleEndian::Convert ((int32)static_data->num_vertices);
  mf.Write ((char*)&l, 4);
  l = csLittleEndian::Convert ((int32)polygons.GetSize ());
  mf.Write ((char*)&l, 4);

  if (logparent)
  {
    iObject* o = logparent->QueryObject ();
    if (o->GetName ())
      mf.Write (o->GetName (), strlen (o->GetName ()));
    iSector* sect = logparent->GetMovable ()->GetSectors ()->Get (0);
    if (sect && sect->QueryObject ()->GetName ())
      mf.Write (sect->QueryObject ()->GetName (),
                strlen (sect->QueryObject ()->GetName ()));
  }

  csMD5::Digest digest = csMD5::Encode (mf.GetData (), mf.GetSize ());
  return digest.HexString();
}

void csThing::MarkLightmapsDirty ()
{
  SetLmDirty (true);
  light_version++;
}

void csThing::SetMovingOption (int opt)
{
  cfg_moving = opt;
  switch (cfg_moving)
  {
    case CS_THING_MOVE_NEVER:
      if (wor_verts != static_data->obj_verts) delete[] wor_verts;
      wor_verts = static_data->obj_verts;
      break;

    case CS_THING_MOVE_OCCASIONAL:
      if ((wor_verts == 0 || wor_verts == static_data->obj_verts)
        && static_data->max_vertices)
      {
        wor_verts = new csVector3[static_data->max_vertices];
        memcpy (wor_verts, static_data->obj_verts,
                static_data->max_vertices * sizeof (csVector3));
      }

      //cached_movable = 0;
      movablenr--;
      break;
  }

  movablenr = -1;                 // @@@ Is this good?
}

void csThing::WorUpdate ()
{
  size_t i;
  int j;
  switch (cfg_moving)
  {
    case CS_THING_MOVE_NEVER:
      if (cached_movable && cached_movable->GetUpdateNumber () != movablenr)
      {
        if (!cached_movable->IsFullTransformIdentity ())
        {
          // If the movable is no longer the identity transform we
          // have to change modes to moveable.
          SetMovingOption (CS_THING_MOVE_OCCASIONAL);
          WorUpdate ();
          break;
        }
        movablenr = cached_movable->GetUpdateNumber ();
        delete[] polygon_world_planes;
        polygon_world_planes = 0;
        polygon_world_planes_num = 0;
      }
      return ;

    case CS_THING_MOVE_OCCASIONAL:
      if (cached_movable && cached_movable->GetUpdateNumber () != movablenr)
      {
        movablenr = cached_movable->GetUpdateNumber ();

        if (cached_movable->IsFullTransformIdentity ())
        {
          memcpy (wor_verts, static_data->obj_verts,
                static_data->num_vertices * (sizeof (csVector3)));
          delete[] polygon_world_planes;
          polygon_world_planes = 0;
          polygon_world_planes_num = 0;
        }
        else
        {
          csReversibleTransform movtrans = cached_movable->GetFullTransform ();
          for (j = 0; j < static_data->num_vertices; j++)
            wor_verts[j] = movtrans.This2Other (static_data->obj_verts[j]);
          if (!polygon_world_planes || polygon_world_planes_num < polygons.GetSize () ||
            polygon_world_planes_num == (size_t)-1)
          {
            delete[] polygon_world_planes;
            polygon_world_planes_num = polygons.GetSize ();
            polygon_world_planes = new csPlane3[polygon_world_planes_num];
          }
          for (i = 0; i < polygons.GetSize (); i++)
          {
            csPolygon3DStatic* sp = static_data->GetPolygon3DStatic ((int)i);
            movtrans.This2Other (sp->polygon_data.plane_obj,
                Vwor (sp->GetVertexIndices ()[0]),
                polygon_world_planes[i]);
            polygon_world_planes[i].Normalize ();
          }
        }
      }
      break;
  }
}

void csThing::HardTransform (const csReversibleTransform& t)
{
  csRef<csThingStatic> new_static_data = static_data->CloneStatic ();
  static_data = new_static_data;
  static_data->HardTransform (t);
}

void csThing::Unprepare ()
{
  SetPrepared (false);
}

void csThing::PreparePolygons ()
{
  csPolygon3DStatic *ps;
  csPolygon3D *p;
  polygons.DeleteAll ();
  delete[] polygon_world_planes;
  polygon_world_planes = 0;
  polygon_world_planes_num = (size_t)-1;        // Not checked!

  size_t i;
  polygons.SetSize (static_data->static_polygons.GetSize ());
  for (i = 0; i < static_data->static_polygons.GetSize (); i++)
  {
    p = &polygons.Get (i);
    ps = static_data->static_polygons.Get (i);
    p->SetParent (this);
    p->Finish (ps);
  }
  polygons.ShrinkBestFit ();
}

void csThing::PrepareSomethingOrOther ()
{
  static_data->Prepare (logparent);

  if (IsPrepared())
  {
    if (static_data_nr != static_data->GetShapeNumber ())
    {
      static_data_nr = static_data->GetShapeNumber ();

      if (cfg_moving == CS_THING_MOVE_OCCASIONAL)
      {
        if (wor_verts != static_data->obj_verts)
          delete[] wor_verts;
        wor_verts = new csVector3[static_data->max_vertices];
      }
      else
      {
        wor_verts = static_data->obj_verts;
      }
      if (cached_movable) movablenr = cached_movable->GetUpdateNumber ()-1;
      else movablenr--;

      meshesHolder.Clear();

      materials_to_visit.DeleteAll ();

      ClearLMs ();
      PreparePolygons ();

      MarkLightmapsDirty ();
      ClearLMs ();
      PrepareLMs ();
    }
    return;
  }

  SetPrepared (true);

  static_data_nr = static_data->GetShapeNumber ();

  if (cfg_moving == CS_THING_MOVE_OCCASIONAL)
  {
    if (wor_verts != static_data->obj_verts)
      delete[] wor_verts;
    wor_verts = new csVector3[static_data->max_vertices];
  }
  else
  {
    wor_verts = static_data->obj_verts;
  }
  if (cached_movable) movablenr = cached_movable->GetUpdateNumber ()-1;
  else movablenr--;

  meshesHolder.Clear();

  materials_to_visit.DeleteAll ();

  PreparePolygons ();

  // don't prepare lightmaps yet - the LMs may still be unlit,
  // as this function is called from within 'ReadFromCache()'.
}

iMaterialWrapper* csThing::FindRealMaterial (iMaterialWrapper* old_mat)
{
  size_t i;
  for (i = 0 ; i < replace_materials.GetSize () ; i++)
  {
    if (replace_materials[i].old_mat == old_mat)
      return replace_materials[i].new_mat;
  }
  return 0;
}

void csThing::ReplaceMaterial (iMaterialWrapper* oldmat,
        iMaterialWrapper* newmat)
{
  replace_materials.Push (RepMaterial (oldmat, newmat));
  SetPrepared (false);
}


void csThing::ClearReplacedMaterials ()
{
  replace_materials.DeleteAll ();
  SetPrepared (false);
}

csPolygon3D *csThing::GetPolygon3D (const char *name)
{
  int idx = static_data->FindPolygonByName (name);
  return idx >= 0 ? &polygons.Get (idx) : 0;
}

csPtr<iPolygonHandle> csThing::CreatePolygonHandle (int polygon_idx)
{
  CS_ASSERT (polygon_idx >= 0);
  return csPtr<iPolygonHandle> (new csPolygonHandle (
        (iThingFactoryState*)(csThingStatic*)static_data,
        (iMeshObjectFactory*)(csThingStatic*)static_data,
        this,
        (iMeshObject*)this,
        polygon_idx));
}

const csPlane3& csThing::GetPolygonWorldPlane (int polygon_idx)
{
  CS_ASSERT (polygon_idx >= 0);
  if (polygon_world_planes_num == (size_t)-1)
  {
    WorUpdate ();
  }
  return GetPolygonWorldPlaneNoCheck (polygon_idx);
}

const csPlane3& csThing::GetPolygonWorldPlaneNoCheck (int polygon_idx) const
{
  if (polygon_world_planes)
    return polygon_world_planes[polygon_idx];
  else
    return static_data->static_polygons[polygon_idx]->GetObjectPlane ();
}

void csThing::InvalidateThing ()
{
  materials_to_visit.DeleteAll ();

  SetPrepared (false);
  static_data->SetObjBboxValid (false);

  delete [] static_data->obj_normals; static_data->obj_normals = 0;
  static_data->InvalidateShape ();
}

void csThing::BuildDecal(const csVector3*, float,
    iDecalBuilder*)
{
}

bool csThing::HitBeamOutline (const csVector3&,
  const csVector3&, csVector3&, float*)
{
  return false;
}

bool csThing::HitBeamObject (const csVector3&,
  const csVector3&, csVector3&, float*, int*,
  iMaterialWrapper**, csArray<iMaterialWrapper*>*)
{
  return false;
}

struct MatPol
{
  iMaterialWrapper *mat;
  int mat_index;
  csPolygon3DStatic *spoly;
  csPolygon3D *poly;
};

void csThing::PreparePolygonBuffer ()
{
}

void csThing::DrawPolygonArrayDPM (
  iRenderView* /*rview*/,
  iMovable* /*movable*/,
  csZBufMode /*zMode*/)
{

}

void csThing::GetBoundingBox (iMovable *movable, csBox3 &box)
{
  if (wor_bbox_movablenr != movable->GetUpdateNumber ())
  {
    // First make sure obj_bbox is valid.
    box = static_data->GetBoundingBox ();
    wor_bbox_movablenr = movable->GetUpdateNumber ();
    csBox3& obj_bbox = static_data->obj_bbox;

    // @@@ Maybe it would be better to really calculate the bounding box
    // here instead of just transforming the object space bounding box?
    if (movable->IsFullTransformIdentity ())
    {
      wor_bbox = obj_bbox;
    }
    else
    {
      csReversibleTransform mt = movable->GetFullTransform ();
      wor_bbox.StartBoundingBox (mt.This2Other (obj_bbox.GetCorner (0)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (1)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (2)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (3)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (4)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (5)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (6)));
      wor_bbox.AddBoundingVertexSmart (mt.This2Other (obj_bbox.GetCorner (7)));
    }
  }

  box = wor_bbox;
}

//-------------------------------------------------------------------------

struct TriMeshTimerEvent : 
  public scfImplementation1<TriMeshTimerEvent, 
			    iTimerEvent>
{
  csWeakRef<TriMeshHelper> pmh;
  TriMeshTimerEvent (TriMeshHelper* pmh) : scfImplementationType (this)
  {
    TriMeshTimerEvent::pmh = pmh;
  }
  virtual bool Perform (iTimerEvent*)
  {
    if (pmh) pmh->Cleanup ();
    return false;
  }
};

//-------------------------------------------------------------------------

void TriMeshHelper::SetThing (csThingStatic* thing)
{
  TriMeshHelper::thing = thing;
  static_data_nr = thing->GetStaticDataNumber ()-1;
  num_tri = ~0;
}

void TriMeshHelper::Setup ()
{
  thing->Prepare (0);
  if (static_data_nr != thing->GetStaticDataNumber ())
  {
    static_data_nr = thing->GetStaticDataNumber ();
    ForceCleanup ();
  }

  if (triangles || num_tri == 0)
  {
    // Already set up. First we check if the object vertex array
    // is still valid.
    if (vertices == thing->obj_verts) return ;
  }

  vertices = 0;

  // Count the number of needed triangles and vertices.
  num_verts = thing->GetVertexCount ();
  num_tri = 0;

  size_t i;
  const csPolygonStaticArray &pol = thing->static_polygons;
  for (i = 0 ; i < pol.GetSize () ; i++)
  {
    csPolygon3DStatic *p = pol.Get (i);
    if (p->flags.CheckAll (poly_flag))
      num_tri += p->GetVertexCount ()-2;
  }

  // Allocate the arrays and the copy the data.
  if (num_verts)
  {
    vertices = thing->obj_verts;
  }

  if (num_tri)
  {
    triangles = new csTriangle[num_tri];
    num_tri = 0;
    for (i = 0 ; i < pol.GetSize () ; i++)
    {
      csPolygon3DStatic *p = pol.Get (i);
      if (p->flags.CheckAll (poly_flag))
      {
        int* vi = p->GetVertexIndices ();
        size_t j;
        for (j = 1 ; j < (size_t)p->GetVertexCount ()-1 ; j++)
        {
	  triangles[num_tri].a = vi[0];
	  triangles[num_tri].b = vi[j];
	  triangles[num_tri].c = vi[j+1];
          num_tri++;
        }
      }
    }
  }

  csRef<iEventTimer> timer = csEventTimer::GetStandardTimer (
        thing->thing_type->object_reg);
  TriMeshTimerEvent* te = new TriMeshTimerEvent (this);
  timer->AddTimerEvent (te, 9000+(rand ()%2000));
  te->DecRef ();
}

void TriMeshHelper::Unlock ()
{
  locked--;
  CS_ASSERT (locked >= 0);
  if (locked <= 0)
  {
    csRef<iEventTimer> timer = csEventTimer::GetStandardTimer (
        thing->thing_type->object_reg);
    TriMeshTimerEvent* te = new TriMeshTimerEvent (this);
    timer->AddTimerEvent (te, 9000+(rand ()%2000));
    te->DecRef ();
  }
}

void TriMeshHelper::Cleanup ()
{
  if (locked) return;
  ForceCleanup ();
}

void TriMeshHelper::ForceCleanup ()
{
  vertices = 0;
  delete[] triangles;
  triangles = 0;
  num_tri = ~0;
}

//----------------------------------------------------------------------

bool csThing::ReadFromCache (iCacheManager* cache_mgr)
{
  PrepareSomethingOrOther ();
  {
    csString cachename = GenerateCacheName ();
    cache_mgr->SetCurrentScope (cachename);
  }

  // For error reporting.
  const char* thing_name = 0;
  if (csThingObjectType::do_verbose && logparent)
  {
    thing_name = logparent->QueryObject ()->GetName ();
  }

  bool rc = true;
  csRef<iDataBuffer> db = cache_mgr->ReadCache ("thing_lm", 0, (uint32) ~0);
  if (db)
  {
    csMemFile mf (db, true);
    size_t i;
    for (i = 0; i < polygons.GetSize (); i++)
    {
      csPolygon3D& p = polygons.Get (i);
      csPolygon3DStatic* sp = static_data->GetPolygon3DStatic ((int)i);
      const char* error = p.ReadFromCache (&mf, sp);
      if (error != 0)
      {
        rc = false;
        if (csThingObjectType::do_verbose)
        {
          csPrintf ("  Thing '%s' Poly '%s': %s\n",
            thing_name, sp->GetName (), error);
          fflush (stdout);
        }
      }
    }
  }
  else
  {
    if (csThingObjectType::do_verbose)
    {
      csPrintf ("  Thing '%s': Could not find cached lightmap file for thing!\n",
        thing_name);
      fflush (stdout);
    }
    rc = false;
  }

  cache_mgr->SetCurrentScope (0);
  return rc;
}

bool csThing::WriteToCache (iCacheManager* cache_mgr)
{
  csString cachename = GenerateCacheName ();
  cache_mgr->SetCurrentScope (cachename);

  size_t i;
  bool rc = false;
  csMemFile mf;
  for (i = 0; i < polygons.GetSize (); i++)
  {
    csPolygon3D& p = polygons.Get (i);
    csPolygon3DStatic* sp = static_data->GetPolygon3DStatic ((int)i);
    if (!p.WriteToCache (&mf, sp)) goto stop;
  }
  if (!cache_mgr->CacheData ((void*)(mf.GetData ()), mf.GetSize (),
        "thing_lm", 0, (uint32) ~0))
    goto stop;

  rc = true;

stop:
  cache_mgr->SetCurrentScope (0);
  return rc;
}

void csThing::PrepareRenderMeshes (
  csDirtyAccessArray<csRenderMesh*>& renderMeshes)
{
  size_t i;

  for (i = 0; i < renderMeshes.GetSize () ; i++)
  {
    // @@@ Is this needed?
    //if (renderMeshes[i]->variablecontext != 0)
      //renderMeshes[i]->variablecontext->DecRef ();
    static_data->thing_type->blk_rendermesh.Free (renderMeshes[i]);
  }
  renderMeshes.DeleteAll ();
  static_data->FillRenderMeshes (this, renderMeshes, replace_materials, 
    mixmode);
  renderMeshes.ShrinkBestFit ();
  materials_to_visit.DeleteAll ();
  for (i = 0 ; i < renderMeshes.GetSize () ; i++)
  {
    if (renderMeshes[i]->material->IsVisitRequired ())
      materials_to_visit.Push (renderMeshes[i]->material);
  }
  materials_to_visit.ShrinkBestFit ();

  /*for (i = 0; i < renderMeshes.GetSize (); i++)
  {
    csRenderMesh* rm = renderMeshes[i];
    rm->variablecontext->GetVariable (static_data->texLightmapName)->
      SetValue (i < litPolys.GetSize () ? litPolys[i]->SLM->GetTexture() : 0);
  }*/

}

void csThing::PrepareForUse ()
{
  PrepareSomethingOrOther ();
  PreparePolygonBuffer ();
  PrepareLMs ();

  WorUpdate ();
  //UpdateDirtyLMs ();

  bool meshesCreated;
  csDirtyAccessArray<csRenderMesh*>& renderMeshes =
    meshesHolder.GetUnusedData (meshesCreated, 0);
  if (renderMeshes.GetSize () == 0)
  {
    PrepareRenderMeshes (renderMeshes);
  }
}

csRenderMesh **csThing::GetRenderMeshes (int&, iRenderView*, iMovable*, uint32)
{
  return 0;
}

void csThing::PrepareLMs ()
{
  if (IsLmPrepared()) return;

  size_t i;
  for (i = 0; i < static_data->litPolys.GetSize (); i++)
  {
    const csThingStatic::csStaticLitPolyGroup& slpg =
      *(static_data->litPolys[i]);

    csLitPolyGroup* lpg = new csLitPolyGroup;
    lpg->material = FindRealMaterial (slpg.material);
    if (lpg->material == 0) lpg->material = slpg.material;

    size_t j;
    lpg->polys.SetSize (slpg.polys.GetSize ());
    for (j = 0; j < slpg.polys.GetSize (); j++)
    {
      lpg->polys.Put (j, slpg.polys[j]);
    }

    litPolys.Push (lpg);
  }

  for (i = 0; i < static_data->unlitPolys.GetSize (); i++)
  {
    const csThingStatic::csStaticPolyGroup& spg =
      *(static_data->unlitPolys[i]);
    csPolyGroup* pg = new csPolyGroup;
    pg->material = FindRealMaterial (spg.material);
    if (pg->material == 0) pg->material = spg.material;

    size_t j;
    pg->polys.SetSize (spg.polys.GetSize ());
    for (j = 0; j < spg.polys.GetSize (); j++)
    {
      pg->polys.Put (j, spg.polys[j]);
    }
    //pg->polys.ShrinkBestFit();

    unlitPolys.Push (pg);
  }

  litPolys.ShrinkBestFit ();
  unlitPolys.ShrinkBestFit ();

  SetLmDirty (true);
  SetLmPrepared (true);
}

void csThing::ClearLMs ()
{
  if (!IsLmPrepared()) return;

  litPolys.DeleteAll ();
  unlitPolys.DeleteAll ();

  SetLmDirty (true);
  SetLmPrepared (false);
}

void csThing::UpdateDirtyLMs ()
{
  csColor amb (0, 0, 0);

  if (!IsLmDirty()) return;

  SetLmDirty (false);
}

csPtr<iImage> csThing::GetPolygonLightmap (int polygon_idx)
{
  if ((polygon_idx < 0) 
    || ((size_t)polygon_idx >= polygons.GetSize())) return 0;
  csPolyTexture* polytex = polygons[polygon_idx].GetPolyTexture();
  if (!polytex) return 0;
  csLightMap* lm = polytex->GetLightMap();
  if (!lm) return 0;

  csRGBcolor* rgbPtr = lm->GetStaticMap ();
  if (!rgbPtr) return 0;
  size_t numPixels = lm->GetWidth() * lm->GetHeight();
  csRGBpixel* rgbaData = new csRGBpixel[numPixels];
  {
    csRGBpixel* rgbaPtr = rgbaData;
    while (numPixels-- > 0)
    {
      *rgbaPtr++ = *rgbPtr++;
    }
  }
  return csPtr<iImage> (new csImageMemory (lm->GetWidth(), lm->GetHeight(), 
    rgbaData, true));
}

bool csThing::GetPolygonPDLight (int polygon_idx, size_t pdlight_index, 
                                 csRef<iImage>& map, iLight*& light)
{
  if ((polygon_idx < 0) 
    || ((size_t)polygon_idx >= polygons.GetSize())) return false;
  csPolyTexture* polytex = polygons[polygon_idx].GetPolyTexture();
  if (!polytex) return false;
  csLightMap* lm = polytex->GetLightMap();
  if (!lm) return false;

  csShadowMap* smap = lm->GetShadowMap (pdlight_index);
  if (!smap) return false;

  // Create grayscale image from PD shadow map.
  light = smap->Light;
  size_t smapSize = smap->map->GetSize();
  uint8* pdData = new uint8[smapSize];
  memcpy (pdData, smap->map->GetData(), smapSize);
  csRGBpixel* pal = new csRGBpixel[256];
  for (int i = 0; i < 256; i++) pal[i].Set (i, i, i);
  map.AttachNew (new csImageMemory (lm->GetWidth(), lm->GetHeight(), 
    pdData, true, CS_IMGFMT_PALETTED8, pal));

  return true;
}

iMaterialWrapper* csThing::GetReplacedMaterial (iMaterialWrapper* oldMat)
{
  for (size_t i = 0; i < replace_materials.GetSize(); i++)
  {
    if (replace_materials[i].old_mat == oldMat) 
      return replace_materials[i].new_mat;
  }
  return 0;
}

//---------------------------------------------------------------------------

iMeshObjectFactory *csThing::GetFactory () const
{
  return (iMeshObjectFactory*)static_data;
}

//---------------------------------------------------------------------------

csThingObjectType::csThingObjectType (iBase *pParent) :
  scfImplementationType (this, pParent),
  blk_polygon3dstatic (2000),
  blk_texturemapping (2000),
  blk_lightmap (2000),
  blk_polidx3 (1000),
  blk_polidx4 (2000)
{
  lightpatch_pool = 0;
  blk_polidx5 = 0;
  blk_polidx6 = 0;
  blk_polidx20 = 0;
  blk_polidx60 = 0;
}

csThingObjectType::~csThingObjectType ()
{
  delete lightpatch_pool;
  delete blk_polidx5;
  delete blk_polidx6;
  delete blk_polidx20;
  delete blk_polidx60;
}

bool csThingObjectType::Initialize (iObjectRegistry *object_reg)
{
  csThingObjectType::object_reg = object_reg;
  csRef<iStringSet> strset = csQueryRegistryTagInterface<iStringSet> (
      object_reg, "crystalspace.shared.stringset");
  base_id = strset->Request ("base");
  viscull_id = strset->Request ("viscull");
  colldet_id = strset->Request ("colldet");
  csRef<iEngine> e = csQueryRegistry<iEngine> (object_reg);
  engine = e;   // We don't want a real ref here to avoid circular refs.
  csRef<iGraphics3D> g = csQueryRegistry<iGraphics3D> (object_reg);
  G3D = g;
  if (!g) return false;

  lightpatch_pool = new csLightPatchPool ();

  csRef<iVerbosityManager> verbosemgr (
    csQueryRegistry<iVerbosityManager> (object_reg));
  if (verbosemgr)
    csThingObjectType::do_verbose = verbosemgr->Enabled ("thing");

  csRef<iTextureManager> txtmgr = g->GetTextureManager ();

  int maxTW, maxTH, maxTA;
  txtmgr->GetMaxTextureSize (maxTW, maxTH, maxTA);

  csConfigAccess cfg (object_reg, "/config/thing.cfg");

  int maxLightmapSize = cfg->GetInt ("Mesh.Thing.MaxSuperlightmapSize",
    /*256*/MIN (maxTW, maxTH));
  maxLightmapW =
    cfg->GetInt ("Mesh.Thing.MaxSuperlightmapWidth", maxLightmapSize);
  maxLightmapW = MIN (maxLightmapW, maxTW);
  maxLightmapH =
    cfg->GetInt ("Mesh.Thing.MaxSuperlightmapHeight", maxLightmapSize);
  maxLightmapH = MIN (maxLightmapH, maxTH);
  maxSLMSpaceWaste =
    cfg->GetFloat ("Mesh.Thing.MaxSuperlightmapWaste", 0.6f);
  csThing::lightmap_quality = cfg->GetInt (
      "Mesh.Thing.LightmapQuality", 3);
  csThing::lightmap_enabled = cfg->GetBool (
      "Mesh.Thing.EnableLightmaps", true);
  if (csThingObjectType::do_verbose)
  {
    Notify ("Lightmap quality=%d", csThing::lightmap_quality);
    Notify ("Lightmapping enabled=%d", (int)csThing::lightmap_enabled);
  }

  stringsetSvName = csQueryRegistryTagInterface<iShaderVarStringSet> (
    object_reg, "crystalspace.shader.variablenameset");

  shadermgr = csQueryRegistry<iShaderManager> (object_reg);

  return true;
}

void csThingObjectType::Clear ()
{
  delete lightpatch_pool;
  lightpatch_pool = new csLightPatchPool ();
}

csPtr<iMeshObjectFactory> csThingObjectType::NewFactory ()
{
  csThingStatic *cm = new csThingStatic (this, this);
  csRef<iMeshObjectFactory> ifact (
    scfQueryInterface<iMeshObjectFactory> (cm));
  cm->DecRef ();
  return csPtr<iMeshObjectFactory> (ifact);
}

bool csThingObjectType::DebugCommand (const char* /*cmd*/)
{
  return false;
}

void csThingObjectType::Warn (const char *description, ...)
{
  va_list arg;
  va_start (arg, description);

  csReportV (object_reg,
    CS_REPORTER_SEVERITY_WARNING,
    "crystalspace.mesh.object.thing",
    description,
    arg);

  va_end (arg);
}

void csThingObjectType::Bug (const char *description, ...)
{
  va_list arg;
  va_start (arg, description);

  csReportV (object_reg,
    CS_REPORTER_SEVERITY_BUG,
    "crystalspace.mesh.object.thing",
    description,
    arg);

  va_end (arg);
}

void csThingObjectType::Error (const char *description, ...)
{
  va_list arg;
  va_start (arg, description);

  csReportV (object_reg,
    CS_REPORTER_SEVERITY_ERROR,
    "crystalspace.mesh.object.thing",
    description,
    arg);

  va_end (arg);
}

void csThingObjectType::Notify (const char *description, ...)
{
  va_list arg;
  va_start (arg, description);

  csReportV (object_reg,
    CS_REPORTER_SEVERITY_NOTIFY,
    "crystalspace.mesh.object.thing",
    description,
    arg);

  va_end (arg);
}

//---------------------------------------------------------------------------

static const csOptionDescription
  config_options[] =
{
  { 0, "cosfact", "Cosinus factor for lighting", CSVAR_FLOAT },
  { 1, "lightqual", "Lighting quality", CSVAR_LONG },
  { 2, "fullbright", "Enable/disable fullbright", CSVAR_BOOL }
};
const int NUM_OPTIONS =
  (
    sizeof (config_options) /
    sizeof (config_options[0])
  );

bool csThingObjectType::SetOption (int id, csVariant *value)
{
  switch (id)
  {
    case 0:
      csPolyTexture::cfg_cosinus_factor = value->GetFloat ();
      break;
    case 1:
      csThing::lightmap_quality = value->GetLong ();
      if (csThingObjectType::do_verbose)
        Notify ("Lightmap quality=%d", csThing::lightmap_quality);
      break;
    case 2:
      csThing::lightmap_enabled = !value->GetBool ();
      if (csThingObjectType::do_verbose)
        Notify ("Fullbright enabled=%d",
                (int)!csThing::lightmap_enabled);
      break;
    default:
      return false;
  }

  return true;
}

bool csThingObjectType::GetOption (int id, csVariant *value)
{
  switch (id)
  {
    case 0:   value->SetFloat (csPolyTexture::cfg_cosinus_factor); break;
    case 1:   value->SetLong (csThing::lightmap_quality); break;
    case 2:   value->SetBool (!csThing::lightmap_enabled); break;
    default:  return false;
  }

  return true;
}

bool csThingObjectType::GetOptionDescription (
  int idx,
  csOptionDescription *option)
{
  if (idx < 0 || idx >= NUM_OPTIONS) return false;
  *option = config_options[idx];
  return true;
}

