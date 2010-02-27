/*
    Copyright (C) 1998-2001 by Jorrit Tyberghein

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

#ifndef __CS_THING_H__
#define __CS_THING_H__

#include "csgeom/csrect.h"
#include "cstool/objmodel.h"
#include "csgeom/subrec.h"
#include "csgeom/transfrm.h"
#include "igeom/trimesh.h"
#include "csgfx/imagememory.h"
#include "csgfx/shadervar.h"
#include "csutil/array.h"
#include "csutil/blockallocator.h"
#include "csutil/cscolor.h"
#include "csutil/csobject.h"
#include "csutil/flags.h"
#include "csutil/dirtyaccessarray.h"
#include "csutil/nobjvec.h"
#include "csutil/parray.h"
#include "csutil/refarr.h"
#include "csutil/util.h"
#include "csutil/weakref.h"
#include "csutil/leakguard.h"
#include "cstool/rendermeshholder.h"
#include "iengine/mesh.h"
#include "iengine/rview.h"
#include "imesh/object.h"
#include "../thing.h"
#include "iutil/comp.h"
#include "iutil/dbghelp.h"
#include "iutil/eventh.h"
#include "iutil/pluginconfig.h"
#include "ivideo/rendermesh.h"
#include "ivideo/shader/shader.h"
#include "ivideo/txtmgr.h"

#include "lghtmap.h"
#include "parrays.h"
#include "polygon.h"
#include "polyrender.h"

struct csVisObjInfo;
struct iGraphics3D;
struct iRenderView;
struct iMovable;
struct iMaterialWrapper;
struct iPolygonBuffer;

class csThing;
class csThingStatic;
class csThingObjectType;
class csLightPatchPool;
class csPolyTexLightMap;

/**
 * A structure used to replace materials.
 */
struct RepMaterial
{
  iMaterialWrapper* old_mat;
  iMaterialWrapper* new_mat;
  RepMaterial (iMaterialWrapper* o, iMaterialWrapper* n) :
  	old_mat (o), new_mat (n) { }
};

#include "csutil/deprecated_warn_off.h"

/**
 * A helper class for iTriangleMesh implementations used by csThing.
 */
class TriMeshHelper : 
  public scfImplementation1<TriMeshHelper, 
			    iTriangleMesh>
{
public:
  /**
   * Make a triangle mesh helper.
   */
  TriMeshHelper (uint32 flag) : scfImplementationType (this), 
    vertices (0), triangles (0), poly_flag (flag),
    locked (0)
  {
  }
  virtual ~TriMeshHelper ()
  {
    Cleanup ();
  }

  void Setup ();
  void SetThing (csThingStatic* thing);

  virtual size_t GetVertexCount ()
  {
    Setup ();
    return num_verts;
  }
  virtual csVector3* GetVertices ()
  {
    Setup ();
    return vertices;
  }
  virtual size_t GetTriangleCount ()
  {
    Setup ();
    return num_tri;
  }
  virtual csTriangle* GetTriangles ()
  {
    Setup ();
    return triangles;
  }
  virtual void Lock () { locked++; }
  virtual void Unlock ();

  virtual csFlags& GetFlags () { return flags;  }
  virtual uint32 GetChangeNumber() const { return 0; }

  void Cleanup ();
  void ForceCleanup ();

private:
  csThingStatic* thing;
  uint32 static_data_nr;	// To see if the static thing has changed.
  csVector3* vertices;		// Array of vertices (points to obj_verts).
  size_t num_verts;		// Total number of vertices.
  csFlags flags;
  csTriangle* triangles;
  size_t num_tri;
  uint32 poly_flag;
  int locked;
};

/**
 * The static data for a thing.
 */
class csThingStatic : 
  public scfImplementationExt2<csThingStatic, 
			       csObjectModel,
			       iThingFactoryState, 
			       iMeshObjectFactory>
{
public:
  CS_LEAKGUARD_DECLARE (csThingStatic);
  
  csRef<csThingObjectType> thing_type;
  /// Pointer to logical parent.
  iMeshFactoryWrapper* logparent;
  iMeshObjectType* thingmesh_type;
  /// Set of flags
  csFlags flags;
  csFlags internalFlags;

  /// If true then this thing has been prepared (Prepare() function).
  bool IsPrepared() { return internalFlags.Check (1); }
  void SetPrepared (bool b) { internalFlags.SetBool (1, b); }
  bool IsLmPrepared() { return internalFlags.Check (2); }
  void SetLmPrepared (bool b) { internalFlags.SetBool (2, b); }
  /// Smooth flag
  bool IsSmoothed() { return internalFlags.Check (4); }
  void SetSmoothed (bool b) { internalFlags.SetBool (4, b); }
  /// If true then the bounding box in object space is valid.
  bool IsObjBboxValid() { return internalFlags.Check (8); }
  void SetObjBboxValid (bool b) { internalFlags.SetBool (8, b); }

  // Mixmode for rendering.
  uint mixmode;

  /// Number of vertices
  int num_vertices;
  /// Maximal number of vertices
  int max_vertices;
  /// Vertices in object space.
  csVector3* obj_verts;
  /// Normals in object space
  csVector3* obj_normals;

  /// Last used range.
  csPolygonRange last_range;
  /**
   * Function to calculate real start and end polygon indices based
   * on last_range and the given input range.
   */
  void GetRealRange (const csPolygonRange& requested_range, int& start,
  	int& end);
  /**
   * Function to calculate real individual polygon index.
   */
  int GetRealIndex (int requested_index) const;

  /// Bounding box in object space.
  csBox3 obj_bbox;

  /// Radius of object in object space.
  float obj_radius;

  /// Static polys which share the same material
  struct csStaticPolyGroup
  {
    iMaterialWrapper* material;
    csArray<int> polys;

    int numLitPolys;
    int totalLumels;
  };

  struct StaticSuperLM;

  /**
   * Static polys which share the same material and fit on the same SLM
   * template.
   */
  struct csStaticLitPolyGroup : public csStaticPolyGroup
  {
    csArray<csRect> lmRects;
    StaticSuperLM* staticSLM;
  };

  /// SLM template
  struct StaticSuperLM
  {
    int width, height;
    CS::SubRectangles* rects;
    int freeLumels;

    StaticSuperLM (int w, int h) : width(w), height(h)
    {
      rects = 0;
      freeLumels = width * height;
    }
    ~StaticSuperLM()
    {
      delete rects;
    }

    CS::SubRectangles* GetRects ()
    {
      if (rects == 0)
      {
	rects = new CS::SubRectangles (csRect (0, 0, width, height));
      }
      return rects;
    }

    void Grow (int newWidth, int newHeight)
    {
      int usedLumels = (width * height) - freeLumels;

      width = newWidth; height = newHeight;
      if (rects != 0)
	rects->Grow (width, height);

      freeLumels = (width * height) - usedLumels;
    }
  };

  /// The array of static polygon data (csPolygon3DStatic).
  csPolygonStaticArray static_polygons;
  csPDelArray<csStaticLitPolyGroup> litPolys;
  csPDelArray<csStaticPolyGroup> unlitPolys;
  csArray<StaticSuperLM*> superLMs;

  /** 
   * Used to verify whether poly-specific renderbuffers have the right
   * properties.
   */
  csUserRenderBufferManager polyBufferTemplates;

  /**
   * This field describes how the light hitting polygons of this thing is
   * affected by the angle by which the beam hits the polygon. If this value is
   * equal to -1 (default) then the global csPolyTexture::cfg_cosinus_factor
   * will be used.
   */
  float cosinus_factor;

  csWeakRef<iGraphics3D> r3d;

  csRefArray<csPolygonRenderer> polyRenderers;

  static CS::ShaderVarStringID texLightmapName;

  class LightmapTexAccessor : 
    public scfImplementation1<LightmapTexAccessor,
			      iShaderVariableAccessor>
  {
    csThing* instance;
    csRef<iTextureHandle> texh;
  public:
    LightmapTexAccessor (csThing* instance, size_t polyIndex);
    void PreGetValue (csShaderVariable *variable);
  };

public:
  csThingStatic (iBase* parent, csThingObjectType* thing_type);
  virtual ~csThingStatic ();

  /**
   * Prepare the thing for use. This function MUST be called
   * AFTER the texture manager has been prepared. This function
   * is normally called by csEngine::Prepare() so you only need
   * to worry about this function when you add sectors or things
   * later.
   */
  void Prepare (iBase* thing_logparent);
  /**
   * Sets up a layout of lightmaps on a super lightmap that all instances
   * of this factory can reuse.
   */
  void PrepareLMLayout ();
  /// Do the actual distribution.
  void DistributePolyLMs (const csStaticPolyGroup& inputPolys,
    csPDelArray<csStaticLitPolyGroup>& outputPolys,
    csStaticPolyGroup* rejectedPolys);
  /// Delete LM distro information. Needed when adding/removing polys.
  void UnprepareLMLayout ();

  /// Calculates the interpolated normals of all vertices
  void CalculateNormals ();

  /**
   * Get the static data number.
   */
  uint32 GetStaticDataNumber () const
  {
    return GetShapeNumber ();
  }

  /**
   * Invalidate this shape (called after doing a modification).
   */
  void InvalidateShape ();

  /// Get the specified polygon from this set.
  csPolygon3DStatic *GetPolygon3DStatic (int idx)
  { return static_polygons.Get (idx); }

  /// Clone this static data in a separate instance.
  csPtr<csThingStatic> CloneStatic ();

  /**
   * Get the bounding box in object space for this polygon set.
   * This is calculated based on the oriented bounding box.
   */
  const csBox3& GetBoundingBox ();

  /**
   * Set the bounding box in object space for this polygon set.
   */
  void SetBoundingBox (const csBox3& box);

  /**
   * Get the radius in object space for this polygon set.
   */
  void GetRadius (float& rad, csVector3& cent);

  //----------------------------------------------------------------------
  // Vertex handling functions
  //----------------------------------------------------------------------

  /// Just add a new vertex to the thing.
  int AddVertex (const csVector3& v) { return AddVertex (v.x, v.y, v.z); }

  /// Just add a new vertex to the thing.
  int AddVertex (float x, float y, float z);

  virtual int CreateVertex (const csVector3 &iVertex)
  { return AddVertex (iVertex.x, iVertex.y, iVertex.z); }

  /**
   * Compress the vertex table so that all nearly identical vertices
   * are compressed. The polygons in the set are automatically adapted.
   * This function can be called at any time in the creation of the object
   * and it can be called multiple time but it normally only makes sense
   * to call this function after you have finished adding all polygons
   * and all vertices.<p>
   * Note that calling this function will make the camera vertex array
   * invalid.
   */
  virtual void CompressVertices ();

  /**
   * Optimize the vertex table so that all unused vertices are deleted.
   * Note that calling this function will make the camera vertex array
   * invalid.
   */
  void RemoveUnusedVertices ();

  /// Change a vertex.
  virtual void SetVertex (int idx, const csVector3& vt);

  /// Delete a vertex.
  virtual void DeleteVertex (int idx);

  /// Delete a range of vertices.
  virtual void DeleteVertices (int from, int to);

  /// Return the object space vector for the vertex.
  const csVector3& Vobj (int idx) const { return obj_verts[idx]; }

  /// Return the number of vertices.
  virtual int GetVertexCount () const { return num_vertices; }

  virtual const csVector3 &GetVertex (int i) const
  { return obj_verts[i]; }
  virtual const csVector3* GetVertices () const
  { return obj_verts; }

  /// Add a polygon to this thing and return index.
  int AddPolygon (csPolygon3DStatic* spoly);

  /**
   * Intersect object-space segment with polygons of this set. Return
   * polygon index it intersects with (or -1) and the intersection point
   * in object coordinates.<p>
   *
   * If 'pr' != 0 it will also return a value between 0 and 1
   * indicating where on the 'start'-'end' vector the intersection
   * happened.
   */
  int IntersectSegmentIndex (
    const csVector3 &start, const csVector3 &end,
    csVector3 &isect,
    float *pr);

  virtual int GetPolygonCount () { return (int)static_polygons.GetSize (); }
  virtual void RemovePolygon (int idx);
  virtual void RemovePolygons ();

  virtual void SetSmoothingFlag (bool smoothing) { SetSmoothed (smoothing); }
  virtual bool GetSmoothingFlag () { return IsSmoothed(); }
  virtual csVector3* GetNormals () { return obj_normals; }

  virtual float GetCosinusFactor () const { return cosinus_factor; }
  virtual void SetCosinusFactor (float c) { cosinus_factor = c; }

  virtual int FindPolygonByName (const char* name);
  virtual int AddEmptyPolygon ();
  virtual int AddTriangle (const csVector3& v1, const csVector3& v2,
  	const csVector3& v3);
  virtual int AddQuad (const csVector3& v1, const csVector3& v2,
  	const csVector3& v3, const csVector3& v4);
  virtual int AddPolygon (csVector3* vertices, int num);
  virtual int AddPolygon (int num, ...);
  virtual int AddOutsideBox (const csVector3& bmin, const csVector3& bmax);
  virtual int AddInsideBox (const csVector3& bmin, const csVector3& bmax);
  virtual void SetPolygonName (const csPolygonRange& range,
  	const char* name);
  virtual const char* GetPolygonName (int polygon_idx);
  virtual csPtr<iPolygonHandle> CreatePolygonHandle (int polygon_idx);
  virtual void SetPolygonMaterial (const csPolygonRange& range,
  	iMaterialWrapper* material);
  virtual iMaterialWrapper* GetPolygonMaterial (int polygon_idx);
  virtual void AddPolygonVertex (const csPolygonRange& range,
  	const csVector3& vt);
  virtual void AddPolygonVertex (const csPolygonRange& range, int vt);
  virtual void SetPolygonVertexIndices (const csPolygonRange& range,
  	int num, int* indices);
  virtual int GetPolygonVertexCount (int polygon_idx);
  virtual const csVector3& GetPolygonVertex (int polygon_idx, int vertex_idx);
  virtual int* GetPolygonVertexIndices (int polygon_idx);
  virtual bool SetPolygonTextureMapping (const csPolygonRange& range,
  	const csMatrix3& m, const csVector3& v);
  virtual bool SetPolygonTextureMapping (const csPolygonRange& range,
  	const csVector2& uv1, const csVector2& uv2, const csVector2& uv3);
  virtual bool SetPolygonTextureMapping (const csPolygonRange& range,
  	const csVector3& p1, const csVector2& uv1,
  	const csVector3& p2, const csVector2& uv2,
  	const csVector3& p3, const csVector2& uv3);
  virtual bool SetPolygonTextureMapping (const csPolygonRange& range,
  	const csVector3& v_orig, const csVector3& v1, float len1);
  virtual bool SetPolygonTextureMapping (const csPolygonRange& range,
  	const csVector3& v_orig,
	const csVector3& v1, float len1,
	const csVector3& v2, float len2);
  virtual bool SetPolygonTextureMapping (const csPolygonRange& range,
  	float len1);
  virtual void GetPolygonTextureMapping (int polygon_idx,
  	csMatrix3& m, csVector3& v);
  virtual void SetPolygonTextureMappingEnabled (const csPolygonRange& range,
  	bool enabled);
  virtual bool IsPolygonTextureMappingEnabled (int polygon_idx) const;
  virtual bool PointOnPolygon (int polygon_idx, const csVector3& v);
  virtual void SetPolygonFlags (const csPolygonRange& range, uint32 flags);
  virtual void SetPolygonFlags (const csPolygonRange& range, uint32 mask,
  	uint32 flags);
  virtual void ResetPolygonFlags (const csPolygonRange& range, uint32 flags);
  virtual csFlags& GetPolygonFlags (int polygon_idx);
  virtual const csPlane3& GetPolygonObjectPlane (int polygon_idx);
  virtual bool IsPolygonTransparent (int polygon_idx);

  virtual bool AddPolygonRenderBuffer (int polygon_idx, const char* name,
    iRenderBuffer* buffer);
  virtual bool GetLightmapLayout (int polygon_idx, size_t& slm, 
    csRect& slmSubRect, float* slmCoord);

  //-------------------- iMeshObjectFactory interface implementation ----------

  virtual csFlags& GetFlags () { return flags; }
  virtual csPtr<iMeshObject> NewInstance ();
  virtual csPtr<iMeshObjectFactory> Clone ();
  virtual void HardTransform (const csReversibleTransform& t);
  virtual bool SupportsHardTransform () const { return true; }
  virtual void SetMeshFactoryWrapper (iMeshFactoryWrapper* lp)
  { logparent = lp; }
  virtual iMeshFactoryWrapper* GetMeshFactoryWrapper () const
  { return logparent; }
  virtual iMeshObjectType* GetMeshObjectType () const { return thingmesh_type; }
  virtual bool SetMaterialWrapper (iMaterialWrapper*) { return false; }
  virtual iMaterialWrapper* GetMaterialWrapper () const { return 0; }
  virtual void SetMixMode (uint mode)
  { mixmode = mode; }
  virtual uint GetMixMode () const
  { return mixmode; }

  //-------------------- iObjectModel implementation --------------------------
  virtual const csBox3& GetObjectBoundingBox ()
  {
    return GetBoundingBox ();
  }
  virtual void SetObjectBoundingBox (const csBox3& bbox)
  {
    SetBoundingBox (bbox);
  }

  void FillRenderMeshes (csThing* instance,
    csDirtyAccessArray<csRenderMesh*>& rmeshes,
    const csArray<RepMaterial>& repMaterials, uint mixmode);

  virtual iObjectModel* GetObjectModel () { return (iObjectModel*)this; }
  virtual iTerraFormer* GetTerraFormerColldet () { return 0; }
  virtual iTerrainSystem* GetTerrainColldet () { return 0; }
};

#include "csutil/deprecated_warn_on.h"

/**
 * A Thing is a set of polygons. A thing can be used for the
 * outside of a sector or else to augment the sector with
 * features that are difficult to describe with convex sectors alone.<p>
 *
 * Every polygon in the set has a visible and an invisible face;
 * if the vertices of the polygon are ordered clockwise then the
 * polygon is visible. Using this feature it is possible to define
 * two kinds of things: in one kind the polygons are oriented
 * such that they are visible from within the hull. In other words,
 * the polygons form a sort of container or room where the camera
 * can be located. This kind of thing can be used for the outside
 * walls of a sector. In another kind the polygons are
 * oriented such that they are visible from the outside.
 */
class csThing : 
  public scfImplementation2<csThing,
			    iMeshObject, 
			    iThingState>
{
  friend class csPolygon3D;
  friend class csPolygonRenderer::BufferAccessor;

private:
  /// Static data for this thing.
  csRef<csThingStatic> static_data;

  /// ID for this thing (will be >0).
  unsigned int thing_id;
  /// Last used ID.
  static int last_thing_id;
  /// Current visibility number.
  uint32 current_visnr;

  /**
   * Vertices in world space.
   * It is possible that this array is equal to obj_verts. In that
   * case this is a thing that never moves.
   */
  csVector3* wor_verts;

  /**
   * This number indicates the last value of the movable number.
   * This thing can use this to check if the world space coordinates
   * need to be updated.
   */
  long movablenr;
  /**
   * The last movable used to move this object.
   */
  iMovable* cached_movable;
  /**
   * How is moving of this thing controlled? This is one of the
   * CS_THING_MOVE_... flags above.
   */
  int cfg_moving;

  /// The array of dynamic polygon data (csPolygon3D).
  csPolygonArray polygons;
  /// World space planes (if movable is not identity).
  csPlane3* polygon_world_planes;
  size_t polygon_world_planes_num;

  /// Optional array of materials to replace.
  csArray<RepMaterial> replace_materials;

  /**
   * An array of materials that must be visited before use.
   */
  csArray<iMaterialWrapper*> materials_to_visit;

  /**
   * Bounding box in world space.
   * This is a cache for GetBoundingBox(iMovable,csBox3) which
   * will recalculate this if the movable changes (by using movablenr).
   */
  csBox3 wor_bbox;
  /// Last movable number that was used for the bounding box in world space.
  long wor_bbox_movablenr;

  /**
   * Global sector wide dynamic ambient version.
   */
  uint dynamic_ambient_version;

  /**
   * Version number for dynamic/pseudo-dynamic light changes
   * and also for ambient.
   */
  uint32 light_version;

  /// Pointer to logical parent.
  iMeshWrapper* logparent;

  /**
   * This number is compared with the static_data_nr in the static data to
   * see if static data has changed and this thing needs to updated local
   * data
   */
  int32 static_data_nr;

  float current_lod;

  csFlags flags;
  csFlags internalFlags;

  /// If true then this thing has been prepared (Prepare() function).
  bool IsPrepared() { return internalFlags.Check (1); }
  void SetPrepared (bool b) { internalFlags.SetBool (1, b); }
#ifdef __USE_MATERIALS_REPLACEMENT__
  /// If true then a material has been added/removed from
  /// the replace_materials array, and the polygon
  /// buffer of the thing needs to be recalculated.
  bool IsReplaceMaterialChanged() { return internalFlags.Check (2); }
  void SetReplaceMaterialChanged (bool b) { internalFlags.SetBool (2, b); }
#endif
  bool IsLmPrepared() { return internalFlags.Check (4); }
  void SetLmPrepared (bool b) { internalFlags.SetBool (4, b); }
  bool IsLmDirty() { return internalFlags.Check (8); }
  void SetLmDirty (bool b) { internalFlags.SetBool (8, b); }

  csFrameDataHolder<csDirtyAccessArray<csRenderMesh*> > meshesHolder;

  void PrepareRenderMeshes (csDirtyAccessArray<csRenderMesh*>& renderMeshes);

  // Mixmode for rendering.
  uint mixmode;

  /// A group of polys that share the same material.
  struct csPolyGroup
  {
    iMaterialWrapper* material;
    csArray<size_t> polys;
  };

  /// Polys with the same material and the same SLM
  struct csLitPolyGroup : public csPolyGroup
  {
  };

  csPDelArray<csLitPolyGroup> litPolys;
  csPDelArray<csPolyGroup> unlitPolys;

  void PreparePolygons ();
  void PrepareLMs ();
  void ClearLMs ();
private:
  /**
   * Prepare the polygon buffer for use by DrawPolygonMesh.
   * If the polygon buffer is already made then this function will do
   * nothing.
   */
  void PreparePolygonBuffer ();

  /**
   * Invalidate a thing. This has to be called when new polygons are
   * added or removed.
   */
  void InvalidateThing ();

  /**
   * Draw the given array of polygons in the current thing.
   * This version uses iGraphics3D->DrawPolygonMesh()
   * for more efficient rendering. WARNING! This version only works for
   * lightmapped polygons right now and is far from complete.
   * 't' is the movable transform.
   */
  void DrawPolygonArrayDPM (iRenderView* rview, iMovable* movable,
  	csZBufMode zMode);

  /// Generate a cachename based on geometry.
  csString GenerateCacheName ();

public:
  CS_LEAKGUARD_DECLARE (csThing);

  /// Option variable: quality for lightmap calculation.
  static int lightmap_quality;

  /// Option variable: enable/disable lightmapping.
  static bool lightmap_enabled;

  /**
   * Create an empty thing.
   */
  csThing (iBase* parent, csThingStatic* static_data);

  /// Destructor.
  virtual ~csThing ();

  /// Get the pointer to the static data.
  csThingStatic* GetStaticData () { return static_data; }

  /// Get the cached movable.
  iMovable* GetCachedMovable () const { return cached_movable; }

  //----------------------------------------------------------------------
  // Vertex handling functions
  //----------------------------------------------------------------------

  /// Make sure the world vertices are up-to-date.
  void WorUpdate ();

  /**
   * Return the world space vector for the vertex.
   * Make sure you recently called WorUpdate(). Otherwise it is
   * possible that this coordinate will not be valid.
   */
  const csVector3& Vwor (int idx) const { return wor_verts[idx]; }

  /**
   * Get the world plane for a polygon. This function does NOT
   * check if the world plane is valid. Call WorUpdate() to make sure
   * it is valid.
   */
  const csPlane3& GetPolygonWorldPlaneNoCheck (int polygon_idx) const;

  //----------------------------------------------------------------------
  // Polygon handling functions
  //----------------------------------------------------------------------

  /// Get the number of polygons in this thing.
  int GetPolygonCount ()
  { return (int)polygons.GetSize (); }

  /// Get the specified polygon from this set.
  csPolygon3DStatic *GetPolygon3DStatic (int idx)
  { return static_data->GetPolygon3DStatic (idx); }

  /// Get the specified polygon from this set.
  csPolygon3D *GetPolygon3D (int idx)
  { return &polygons.Get (idx); }

  /// Get the named polygon from this set.
  csPolygon3D *GetPolygon3D (const char* name);

  /// Get the entire array of polygons.
  csPolygonArray& GetPolygonArray () { return polygons; }

  /// Remove a single polygon.
  void RemovePolygon (int idx);

  /// Remove all polygons.
  void RemovePolygons ();

  //----------------------------------------------------------------------
  // Setup
  //----------------------------------------------------------------------

  /**
   * Prepare all polygons for use. This function MUST be called
   * AFTER the texture manager has been prepared. This function
   * is normally called by csEngine::Prepare() so you only need
   * to worry about this function when you add sectors or things
   * later.
   */
  void PrepareSomethingOrOther ();

  /** Reset the prepare flag so that this Thing can be re-prepared.
   * Among other things this will allow cached lightmaps to be
   * recalculated.
   */
  void Unprepare ();

  /// Find the real material to use if it was replaced (or 0 if not).
  iMaterialWrapper* FindRealMaterial (iMaterialWrapper* old_mat);

  void ReplaceMaterial (iMaterialWrapper* oldmat, iMaterialWrapper* newmat);
  void ClearReplacedMaterials ();

  void PrepareForUse ();

  //----------------------------------------------------------------------
  // Bounding information
  //----------------------------------------------------------------------

  /**
   * Get the bounding box for this object given some transformation (movable).
   */
  void GetBoundingBox (iMovable* movable, csBox3& box);

  //----------------------------------------------------------------------
  // Lighting
  //----------------------------------------------------------------------

  /**
   * Init the lightmaps for all polygons in this thing.
   */
  //virtual void InitializeDefault (bool clear);

  /**
   * Read the lightmaps from the cache.
   */
  virtual bool ReadFromCache (iCacheManager* cache_mgr);

  /**
   * Cache the lightmaps for all polygons in this thing.
   */
  virtual bool WriteToCache (iCacheManager* cache_mgr);

  /**
   * Prepare the lightmaps for all polys so that they are suitable
   * for the 3D rasterizer.
   */
  //virtual void PrepareLighting ();

  /// Marks the whole object as it is affected by any light.
  void MarkLightmapsDirty ();

  /**
   * Get LM texture for a polygon.
   * Note: \a index is into litPolys.
   */
  iTextureHandle* GetPolygonTexture (size_t index)
  {
    return 0;
  }
  /// Ensure lightmap textures are up-to-date
  void UpdateDirtyLMs ();

  //----------------------------------------------------------------------
  // Utility functions
  //----------------------------------------------------------------------

  /**
   * Test a beam with this thing.
   */
  virtual bool HitBeamOutline (const csVector3& start, const csVector3& end,
  	csVector3& isect, float* pr);

  /**
   * Test a beam with this thing.
   */
  virtual bool HitBeamObject (const csVector3& start, const csVector3& end,
  	csVector3& isect, float* pr, int* polygon_idx = 0,
	iMaterialWrapper** material = 0, csArray<iMaterialWrapper*>* materials = 0);

  //----------------------------------------------------------------------
  // Various
  //----------------------------------------------------------------------

  /**
   * Do a hardtransform. This will make a clone of the factory
   * to avoid other meshes using this factory to be hard transformed too.
   */
  virtual void HardTransform (const csReversibleTransform& t);
  virtual bool SupportsHardTransform () const { return true; }

  /**
   * Control how this thing will be moved.
   */
  void SetMovingOption (int opt);

  /**
   * Get the moving option.
   */
  int GetMovingOption () const { return cfg_moving; }

  /// Get light version.
  uint32 GetLightVersion() const
  { return light_version; }

  //virtual void LightChanged (iLight* light);
  //virtual void LightDisconnect (iLight* light);
  //virtual void DisconnectAllLights ();

  void SetMixMode (uint mode)
  {
    mixmode = mode;
  }
  uint GetMixMode () const
  {
    return mixmode;
  }

  csPtr<iPolygonHandle> CreatePolygonHandle (int polygon_idx);
  const csPlane3& GetPolygonWorldPlane (int polygon_idx);

  /**\name iThingState interface
   * @{ */
  virtual const csVector3 &GetVertexW (int i) const
  { return wor_verts[i]; }
  virtual const csVector3* GetVerticesW () const
  { return wor_verts; }

  /// Prepare.
  virtual void Prepare ()
  { PrepareForUse (); }
  virtual csPtr<iImage> GetPolygonLightmap (int polygon_idx);
  virtual bool GetPolygonPDLight (int polygon_idx, size_t pdlight_index, 
    csRef<iImage>& map, iLight*& light);
  iMaterialWrapper* GetReplacedMaterial (iMaterialWrapper* oldMat);
  /** @} */

  //-------------------- iMeshObject interface implementation ----------
  virtual csRenderMesh **GetRenderMeshes (int &num, iRenderView* rview, 
    iMovable* movable, uint32 frustum_mask);

  virtual iMeshObjectFactory* GetFactory () const;
  virtual csFlags& GetFlags () { return flags; }
  virtual csPtr<iMeshObject> Clone () { return 0; }
  virtual void SetVisibleCallback (iMeshObjectDrawCallback* /*cb*/) { }
  virtual iMeshObjectDrawCallback* GetVisibleCallback () const
  { return 0; }
  virtual void NextFrame (csTicks /*current_time*/,const csVector3& /*pos*/,
    uint /*currentFrame*/)
  { }
  virtual void SetMeshWrapper (iMeshWrapper* lp) { logparent = lp; }
  virtual iMeshWrapper* GetMeshWrapper () const { return logparent; }
  virtual iObjectModel* GetObjectModel ()
  {
    return static_data->GetObjectModel ();
  }
  virtual bool SetColor (const csColor&) { return false; }
  virtual bool GetColor (csColor&) const { return false; }
  virtual bool SetMaterialWrapper (iMaterialWrapper*) { return false; }
  virtual iMaterialWrapper* GetMaterialWrapper () const { return 0; }
  virtual void PositionChild (iMeshObject* /*child*/, csTicks /*current_time*/) { }
  virtual void BuildDecal(const csVector3* pos, float decalRadius,
	iDecalBuilder* decalBuilder);
};

struct intar2 { int ar[2]; };
struct intar3 { int ar[3]; };
struct intar4 { int ar[4]; };
struct intar5 { int ar[5]; };
struct intar6 { int ar[6]; };
struct intar20 { int ar[20]; };
struct intar60 { int ar[60]; };

/**
 * Thing type. This is the plugin you have to use to create instances
 * of csThing.
 */
class csThingObjectType : 
  public scfImplementation5<csThingObjectType,
			    iMeshObjectType,
			    iThingEnvironment,
			    iComponent,
			    iPluginConfig,
			    iDebugHelper>
{
public:
  iObjectRegistry* object_reg;
  static bool do_verbose;	// Verbose error reporting.
  iEngine* engine;
  /**
   * csThingObjectType must keep a reference to G3D because when polygons
   * are destructed they actually refer to G3D to clear the cache.
   */
  csWeakRef<iGraphics3D> G3D;
  /// An object pool for lightpatches.
  csLightPatchPool* lightpatch_pool;
  csRef<iShaderVarStringSet> stringsetSvName;
  csRef<iShaderManager> shadermgr;

  /**
   * Block allocators for various types of objects in thing.
   */
  csBlockAllocator<csPolygon3DStatic> blk_polygon3dstatic;
  csBlockAllocator<csPolyTextureMapping> blk_texturemapping;
  csBlockAllocator<csLightMap> blk_lightmap;
  csBlockAllocator<intar3> blk_polidx3;
  csBlockAllocator<intar4> blk_polidx4;
  csBlockAllocator<intar5>* blk_polidx5;
  csBlockAllocator<intar6>* blk_polidx6;
  csBlockAllocator<intar20>* blk_polidx20;
  csBlockAllocator<intar60>* blk_polidx60;
  csBlockAllocator<csRenderMesh> blk_rendermesh;

  csLightingScratchBuffer lightingScratch;

  int maxLightmapW, maxLightmapH;
  float maxSLMSpaceWaste;

  csStringID base_id;
  csStringID colldet_id;
  csStringID viscull_id;

public:
  /// Constructor.
  csThingObjectType (iBase*);

  /// Destructor.
  virtual ~csThingObjectType ();

  /// Register plugin with the system driver
  virtual bool Initialize (iObjectRegistry *object_reg);
  void Clear ();

  void Warn (const char *description, ...);
  void Bug (const char *description, ...);
  void Notify (const char *description, ...);
  void Error (const char *description, ...);

  /// New Factory.
  virtual csPtr<iMeshObjectFactory> NewFactory ();

  /**\name iThingEnvironment implementation.
   * @{ */
  virtual int GetLightmapCellSize () const
  {
    return csLightMap::lightcell_size;
  }
  virtual void SetLightmapCellSize (int size)
  {
    csLightMap::SetLightCellSize (size);
  }
  virtual int GetDefaultLightmapCellSize () const
  {
    return csLightMap::default_lightmap_cell_size;
  }
  /** @} */

  /**\name iConfig implementation.
   * @{ */
  virtual bool GetOptionDescription (int idx, csOptionDescription *option);
  virtual bool SetOption (int id, csVariant* value);
  virtual bool GetOption (int id, csVariant* value);
  /** @} */

  /**\name iDebugHelper implementation
   * @{ */
  virtual int GetSupportedTests () const
  { return 0; }
  virtual csPtr<iString> UnitTest ()
  { return 0; }
  virtual csPtr<iString> StateTest ()
  { return 0; }
  virtual csTicks Benchmark (int /*num_iterations*/)
  { return 0; }
  virtual csPtr<iString> Dump ()
  { return 0; }
  virtual void Dump (iGraphics3D* /*g3d*/)
  { }
  virtual bool DebugCommand (const char* cmd);
  /** @} */
};

#endif // __CS_THING_H__
