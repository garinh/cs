/*
    Copyright (C) 2001 by Jorrit Tyberghein

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

#ifndef __CS_TERRFUNC_H__
#define __CS_TERRFUNC_H__

#include "cstool/objmodel.h"
#include "csgeom/transfrm.h"
#include "csgeom/vector3.h"
#include "igeom/trimesh.h"
#include "cstool/rendermeshholder.h"
#include "csutil/bitarray.h"
#include "csutil/cscolor.h"
#include "csutil/flags.h"
#include "csutil/leakguard.h"
#include "csutil/list.h"
#include "csutil/refarr.h"
#include "csutil/sysfunc.h"
#include "csutil/scf_implementation.h"
#include "csutil/scfarray.h"
#include "csutil/weakref.h"
#include "iengine/lightmgr.h"
#include "iengine/mesh.h"
#include "imesh/objmodel.h"
#include "imesh/object.h"
#include "imesh/terrain.h"
#include "iutil/comp.h"
#include "iutil/eventh.h"
#include "ivaria/terraform.h"
#include "ivideo/rndbuf.h"

struct iEngine;
struct iMaterialWrapper;
struct iObjectRegistry;
class csSegment3;

CS_PLUGIN_NAMESPACE_BEGIN(BruteBlock)
{

class csTerrainQuad;
class csTerrainObject;
class csTerrainFactory;

/**
 * This is one block in the terrain.
 */
class csTerrBlock : public csRefCount
{
public:
  CS_LEAKGUARD_DECLARE (csTerrBlock);

  csRef<iRenderBuffer> mesh_vertices;
  csVector3 *vertex_data;
  csRef<iRenderBuffer> mesh_normals;
  csVector3 *normal_data;
  csRef<iRenderBuffer> mesh_texcoords;
  csVector2 *texcoord_data;
  csRef<iRenderBuffer> mesh_colors;
  csColor *color_data;
  csRef<csRenderBufferHolder> bufferHolder;

  csArray<csRenderMesh> meshes;
  csRef<iMaterialWrapper> material;
  csVector3 center;
  float size;
  int res;
  uint last_colorVersion;

  bool built;

  csRef<iTerraSampler> terrasampler;

  csTerrainObject *terr;

  //          0
  //      ---------
  //      | 0 | 1 |
  //    2 |-------| 1
  //      | 2 | 3 |
  //      ---------
  //          3

  csTerrBlock* parent;
  csRef<csTerrBlock> children[4];
  csTerrBlock* neighbours[4];
  int child;

  static int tris;

  csBox3 bbox;

  csBitArray materialsChecked;
  csBitArray materialsUsed;

  void UpdateBlockColors ();

public:
  csTerrBlock (csTerrainObject *terr);
  ~csTerrBlock ();
  
  /// Load data from Former
  void LoadData ();

  /// Generate mesh
  void SetupMesh ();

  /// Detach the node from the tree
  void Detach ();

  /// Set all child neighbours that equal a to b
  void ReplaceChildNeighbours(csTerrBlock *a, csTerrBlock *b);

  /// Split block in 4 children
  bool Split ();

  /// Merge block
  bool Merge ();

  /// Checks if something needs to be merged or split
  void CalcLOD ();

  /// Returns true if this node is a leaf
  bool IsLeaf ()  const
  { return children[0] == 0; }

  void UpdateStaticLighting ();
  void DrawTest (iGraphics3D* g3d, iRenderView *rview, uint32 frustum_mask,
                 csReversibleTransform &transform, iMovable *movable);

  bool detach;

  bool IsMaterialUsed (int index);
};


/**
 * An array giving shadow information for a pseudo-dynamic light.
 */
class csShadowArray
{
public:
  iLight* light;
  // For every vertex of the mesh a value.
  float* shadowmap;

  csShadowArray () : shadowmap (0) { }
  ~csShadowArray ()
  {
    delete[] shadowmap;
  }
};

#include "csutil/deprecated_warn_off.h"

class csTerrainObject : 
  public scfImplementationExt2<csTerrainObject,
                               csObjectModel,
                               iMeshObject,
                               iTerrainObjectState>
{
private:
  friend class csTerrBlock;

  csBox2 region;
  csRef<csTerrBlock> rootblock;
  bool block_dim_invalid;
  csBox3 global_bbox;

  csFlags flags;

  csRefArray<iObjectModelListener> listeners;

  csRef<iRenderBuffer> mesh_indices[16];
  int numindices[16];

  csRef<iMaterialWrapper> matwrap;
  csArray<iMaterialWrapper*> palette;
  // Used to make sure we hold refs to materials (so the ref count is correct when we look to unload).
  csRefArray<iMaterialWrapper> refPalette;
  csRefArray<iImage> alphas;
  csRef<iShaderVariableContext> baseContext;
  csRefArray<iShaderVariableContext> paletteContexts;
  bool materialAlphaMaps;

  float error_tolerance;
  float lod_distance;

  iObjectRegistry* object_reg;
  bool verbose;
  csWeakRef<iGraphics3D> g3d;
  iMeshWrapper* logparent;
  csTerrainFactory* pFactory;
  csRef<iMeshObjectDrawCallback> vis_cb;

  float lod_lcoeff;
  float lod_qcoeff;
  float block_maxsize;
  float block_minsize;
  int block_res;

  csRef<iTerraFormer> terraformer;

  //bool use_singlemap;
  //csArray<csArray<char> > materialMaps;
  csArray<csBitArray> globalMaterialsUsed;
  int materialMapW, materialMapH;
  float wm, hm;	// Scales to map between material map and object space.
  
  csString materialMapFile;
  bool materialMapRaw;

  csDirtyAccessArray<csRenderMesh*>* returnMeshes;
  csRenderMeshHolder rmHolder;
  csFrameDataHolder<csDirtyAccessArray<csRenderMesh*> > returnMeshesHolder;
  csReversibleTransform tr_o2c;

  // Use for clipping during rendering.
  csPlane3 planes[10];

  csStringID vertices_name, normals_name, texcoords_name, colors_name;

  bool initialized;

  bool staticlighting;
  bool castshadows;

  // Data for the colldet polygon mesh.
  bool polymesh_valid;
  csVector3* polymesh_vertices;
  int polymesh_vertex_count;
  void SetupPolyMeshData ();
  void CleanPolyMeshData ();
  csTriangle* polymesh_triangles;
  int polymesh_tri_count;
  bool ReadCDLODFromCache ();
  void WriteCDLODToCache ();
  int cd_resolution;
  float cd_lod_cost;

  /**
  * Do the setup of the entire terrain. This will compute the base
  * mesh, the LOD meshes, normals, ...
  */
  void SetupObject ();
  csTerrainObject *neighbor[4];

  //=============
  // Lighting.
  csDirtyAccessArray<csColor> staticLights;
  csDirtyAccessArray<csColor> staticColors;
  int lmres;
  uint colorVersion;
  uint last_colorVersion;

  /**
   * Global sector wide dynamic ambient version.
   */
  uint dynamic_ambient_version;

  // If we are using the iLightingInfo lighting system then this
  // is an array of lights that affect us right now.
  //csSet<csPtrKey<iLight> > affecting_lights;
  csHash<csShadowArray*, csPtrKey<iLight> > pseudoDynInfo;
  void UpdateColors (iMovable* movable);
  //=============

  bool LODCalc;

  csStringID stringVertices;

public:
  CS_LEAKGUARD_DECLARE (csTerrainObject);

  /// Constructor.
  csTerrainObject (iObjectRegistry* object_reg, csTerrainFactory* factory);
  virtual ~csTerrainObject ();

  /// Set the neighbor above (1 on Z axis) (For use in combining multiple brute meshes)
  void SetTopNeighbor(iTerrainObjectState *top);
  /// Set the neighbor to the right (1 on X axis) (For use in combining multiple brute meshes)
  void SetRightNeighbor(iTerrainObjectState *right);
  /// Set the neighbor to the left (-1 on X axis) (For use in combining multiple brute meshes)
  void SetLeftNeighbor(iTerrainObjectState *left);
  /// Set the neighbor below (-1 on Z axis) (For use in combining multiple brute meshes)
  void SetBottomNeighbor(iTerrainObjectState *bottom);

  const csDirtyAccessArray<csColor>& GetStaticColors () const
  {
    return staticColors;
  }
  int GetLightMapResolution () const { return lmres; }

  bool SetColor (const csColor& /*color*/) { return false; }
  bool GetColor (csColor& /*color*/) const { return false; }
  bool SetMaterialWrapper (iMaterialWrapper *material)
  {
    matwrap = material;
    return true;
  }
  iMaterialWrapper *GetMaterialWrapper () const { return matwrap; }
  virtual void SetMixMode (uint) { }
  virtual uint GetMixMode () const { return CS_FX_COPY; }

  void SetBlockMaxSize (float size)
  {
    block_maxsize = size;
    initialized = false;
  }

  float GetBlockMaxSize () { return block_maxsize; }

  void SetBlockMinSize (float size)
  {
    block_minsize = size;
  }

  float GetBlockMinSize () { return block_minsize; }

  void SetBlockResolution (int res)
  {
    block_res = res;
    initialized = false;
  }

  int GetBlockResolution ()
  {
    return block_res;
  }

  void SetLODCoeffs (float linear, float quadratic)
  {
    lod_lcoeff = linear;
    lod_qcoeff = quadratic;
  }

  /**
  * Test visibility from a given position.
  * This will call MarkVisible() for all quad nodes that are visible.
  */
  //void TestVisibility (iRenderView* rview);

  int CollisionDetect (iMovable *m, csTransform *p);

  const csBox3& GetObjectBoundingBox ();
  void SetObjectBoundingBox (const csBox3& bbox);
  void GetRadius (float& rad, csVector3& cent);

  bool SetCurrentMaterialAlphaMaps (const csArray<csArray<char> >& data, 
                                    int w, int h);
  bool SetCurrentMaterialMap (const csArray<char>& data, int x, int y);

  ///--------------------- iMeshObject implementation ---------------------

  virtual csFlags& GetFlags () { return flags; }
  virtual csPtr<iMeshObject> Clone () { return 0; }

  virtual iMeshObjectFactory* GetFactory () const;

  virtual bool DrawTest (iRenderView* rview, iMovable* movable,
    uint32 frustum_mask);

  virtual csRenderMesh** GetRenderMeshes (int &n, iRenderView* rview,
    iMovable* movable, uint32 frustum_mask);

  virtual void UpdateLighting (iLight** lights, int num_lights,
    iMovable* movable);

  virtual void SetVisibleCallback (iMeshObjectDrawCallback* cb)
  {
    vis_cb = cb;
  }
  virtual iMeshObjectDrawCallback* GetVisibleCallback () const
  {
    return vis_cb;
  }

  virtual void NextFrame (csTicks, const csVector3& /*pos*/,
    uint /*currentFrame*/) { }

  virtual void HardTransform (const csReversibleTransform&) { }
  virtual bool SupportsHardTransform () const { return false; }
  virtual void SetMeshWrapper (iMeshWrapper* lp) { logparent = lp; }
  virtual iMeshWrapper* GetMeshWrapper () const { return logparent; }

  bool HitBeam (csTerrBlock* block,
	const csSegment3& seg,
	csVector3& isect, float* pr);
  bool HitBeamVertical (csTerrBlock* block,
	const csSegment3& seg,
	csVector3& isect, float* pr);
  virtual bool HitBeamOutline (const csVector3& start, const csVector3& end,
    csVector3& isect, float* pr);
  virtual bool HitBeamObject (const csVector3& start, const csVector3& end,
    csVector3& isect, float* pr, int* polygon_idx = 0,
    iMaterialWrapper** material = 0, csArray<iMaterialWrapper*>* materials = 0);

  virtual void BuildDecal(const csVector3* pos, float decalRadius,
          iDecalBuilder* decalBuilder);

  virtual void PositionChild (iMeshObject* /*child*/,
  	csTicks /*current_time*/) { }

  void FireListeners ();
  void AddListener (iObjectModelListener* listener);
  void RemoveListener (iObjectModelListener* listener);

  char* GenerateCacheName ();
  void SetStaticLighting (bool enable);

  //------------------ iTriangleMesh interface implementation ----------------//
  struct TriMesh : public scfImplementation1<TriMesh, iTriangleMesh>
  {
  private:
    csTerrainObject* terrain;
    csFlags flags;
  public:
    void SetTerrain (csTerrainObject* t)
    {
      terrain = t;
    }
    void Cleanup () { }

    virtual size_t GetVertexCount ();
    virtual csVector3* GetVertices ();
    virtual size_t GetTriangleCount ();
    virtual csTriangle* GetTriangles ();
    virtual void Lock () { }
    virtual void Unlock () { }

    virtual csFlags& GetFlags () { return flags;  }
    virtual uint32 GetChangeNumber() const { return 0; }

    TriMesh () : scfImplementationType (this)
    { }
    virtual ~TriMesh ()
    { }
  };
  friend struct TriMesh;

  /**\name iObjectModel implementation
   * @{ */
  virtual iTerraFormer* GetTerraFormerColldet ()
  { return terraformer; }
  virtual iTerrainSystem* GetTerrainColldet () { return 0; }
  /** @} */

  virtual iObjectModel* GetObjectModel () { return this; }

  /**\name iTerrainObjectState implementation
   * @{ */
  bool SetMaterialPalette (const csArray<iMaterialWrapper*>& pal);
  csArray<iMaterialWrapper*> GetMaterialPalette ();
  const csArray<iMaterialWrapper*> &GetMaterialPalette () const { return palette; }
  bool SetMaterialAlphaMaps (const csArray<csArray<char> >& data, int w, int h);
  bool SetMaterialAlphaMaps (const csArray<iImage*>& maps);
  bool SetMaterialMap (const csArray<char>& data, int x, int y);
  bool SetMaterialMap (iImage* map);
  bool SetLODValue (const char* parameter, float value);
  float GetLODValue (const char* parameter) const;
  void SetMaterialMapFile (const char* file, int width, int height, bool raw);
  const char* GetMaterialMapFile (int& width, int& height, bool& raw);
  bool SaveState (const char* /*filename*/) { return true; }
  bool RestoreState (const char* /*filename*/) { return true; }
  bool GetStaticLighting () { return staticlighting; }
  void SetCastShadows (bool enable) { castshadows = enable; }
  bool GetCastShadows () { return castshadows; }
  /** @} */
};

/**
* Factory for terrain.
*/
class csTerrainFactory : 
  public scfImplementationExt2<csTerrainFactory,
                               csObjectModel,
                               iMeshObjectFactory,
                               iTerrainFactoryState>
{
private:
  iMeshFactoryWrapper* logparent;

  iMeshObjectType* brute_type;

  csFlags flags;
  csBox3 obj_bbox;

public:
  CS_LEAKGUARD_DECLARE (csTerrainFactory);

  csRef<iTerraFormer> terraformer;
  csWeakRef<iEngine> engine;
  csRef<iLightManager> light_mgr;

  csBox2 samplerRegion;
  //int resolution;
  int hm_x, hm_y;

  iObjectRegistry *object_reg;

  csVector3 scale;

  /// Constructor.
  csTerrainFactory (iObjectRegistry* object_reg, iMeshObjectType* parent);

  /// Destructor.
  virtual ~csTerrainFactory ();

  virtual csPtr<iMeshObject> NewInstance ();
  virtual csPtr<iMeshObjectFactory> Clone () { return 0; }
  virtual void HardTransform (const csReversibleTransform&) { }
  virtual bool SupportsHardTransform () const { return false; }
  virtual void SetMeshFactoryWrapper (iMeshFactoryWrapper* lp)
  { logparent = lp; }
  virtual iMeshFactoryWrapper* GetMeshFactoryWrapper () const
  { return logparent; }
  virtual iMeshObjectType* GetMeshObjectType () const { return brute_type; }

  /**\name iTerrainFactoryState implementation
   * @{ */
  void SetTerraFormer (iTerraFormer* form);
  iTerraFormer* GetTerraFormer ();
  void SetSamplerRegion (const csBox2& region);
  const csBox2& GetSamplerRegion ();
  bool SaveState (const char* /*filename*/) { return true; }
  bool RestoreState (const char* /*filename*/) { return true; }
  /** @} */

  virtual csFlags& GetFlags () { return flags; }

  /**\name iObjectModel implementation
   * @{ */
  iTerraFormer* GetTerraFormerColldet () { return terraformer; }
  const csBox3& GetObjectBoundingBox () { return obj_bbox; }
  void SetObjectBoundingBox (const csBox3& /*bbox*/) { }
  void GetRadius (float& /*rad*/, csVector3& /*cent*/) { }
  /** @} */

  virtual iObjectModel* GetObjectModel () { return this; }
  virtual bool SetMaterialWrapper (iMaterialWrapper*) { return false; }
  virtual iMaterialWrapper* GetMaterialWrapper () const { return 0; }
  virtual void SetMixMode (uint) { }
  virtual uint GetMixMode () const { return 0; }
};

#include "csutil/deprecated_warn_on.h"

/**
* TerrFunc type. This is the plugin you have to use to create instances
* of csTerrainFactory.
*/
class csTerrainObjectType :
  public scfImplementation2<csTerrainObjectType,
    iMeshObjectType, iComponent>
{
private:
  iObjectRegistry *object_reg;

public:
  /// Constructor.
  csTerrainObjectType (iBase*);
  /// Destructor.
  virtual ~csTerrainObjectType ();

  /// create a new factory.
  virtual csPtr<iMeshObjectFactory> NewFactory ();

  virtual bool Initialize (iObjectRegistry* p)
  { this->object_reg = p; return true; }
};

}
CS_PLUGIN_NAMESPACE_END(BruteBlock)

#endif // __CS_TERRFUNC_H__
