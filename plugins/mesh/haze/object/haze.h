/*
    Copyright (C) 2001 by W.C.A. Wijngaards

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

#ifndef __CS_HAZE_H__
#define __CS_HAZE_H__

#include "cstool/objmodel.h"
#include "csgeom/transfrm.h"
#include "csgeom/vector3.h"
#include "cstool/rendermeshholder.h"
#include "csutil/cscolor.h"
#include "csutil/flags.h"
#include "csutil/parray.h"
#include "csutil/refarr.h"
#include "imesh/haze.h"
#include "imesh/object.h"
#include "imesh/particle.h"
#include "iutil/comp.h"
#include "iutil/eventh.h"
#include "ivideo/graph3d.h"

struct iMaterialWrapper;
struct iCamera;
class csHazeMeshObjectFactory;
class csHazeMeshObjectType;

/**
 * haze layer
 */
class csHazeLayer
{
public:
  /// hull
  iHazeHull *hull;
  /// scale
  float scale;
  /// construct with values
  csHazeLayer(iHazeHull *h, float s) {hull=h; scale=s;}
  /// destroy (decref hull)
  ~csHazeLayer() {if(hull) hull->DecRef();}
};

/// a haze hull
class csHazeHull :
  public scfImplementation1<csHazeHull, iHazeHull>
{
protected:
  /// total counts
  int total_poly, total_vert, total_edge;
  /// array of vertices, [vert_idx]
  csVector3* verts;
  /// pt_1 and pt_2 of edges (the vertice_indices), [edge_idx]
  int *edgept1, *edgept2;
  /// number of vertices == number of edges per polygon
  int *pol_num;
  /** per polygon: the vertice indices [pol_idx][num]
   * use new[] and delete[] per pol_verts[i] as well as for pol_vert itself.
   */
  int **pol_verts;
  /// per polygon the edge indices (for edges starting at that v, to v+1)
  int **pol_edges;

public:
  /// create empty, new[] content by subclass.
  csHazeHull();
  /// destructs content, using delete[].
  virtual ~csHazeHull();

  /**
   *  If already verts, total_vert, total_poly, pol_num and pol_verts are
   *  given. This routine computes the edges, and fills
   *  edgept1, edgept2, pol_edges and total_edges.
   */
  void ComputeEdges();

  /**
   * given camera position will compute the clockwise convex outline
   * polygon for the hull.
   * if num_pts = 0, no polygon resulted.
   * array of vertice idx is new[]ed.
   * made static here, so not every custom iHazeHull has to implement it.
   */
  static void ComputeOutline(iHazeHull *hull, const csVector3& campos,
    int& numv, int*& pts);

  /// --------- iHazeHull implementation --------------------------
  virtual int GetPolygonCount() const {return total_poly;}
  virtual int GetVerticeCount() const {return total_vert;}
  virtual int GetEdgeCount() const {return total_edge;}

  virtual void GetVertex(csVector3& res, int vertex_idx) const
  {
    CS_ASSERT( vertex_idx >= 0 && vertex_idx < total_vert);
    res = verts[vertex_idx];}
  virtual void GetEdge(int edge_num, int& vertex_idx_1, int& vertex_idx_2) const
  {
    CS_ASSERT( edge_num >= 0 && edge_num < total_edge);
    vertex_idx_1 = edgept1[edge_num];
    vertex_idx_2 = edgept2[edge_num];
  }

  virtual int GetPolVerticeCount(int polygon_num) const
  {
    CS_ASSERT( polygon_num >= 0 && polygon_num < total_poly);
    return pol_num[polygon_num];
  }
  virtual int GetPolVertex(int polygon_num, int vertex_num) const
  {
    CS_ASSERT( polygon_num >= 0 && polygon_num < total_poly);
    CS_ASSERT( vertex_num >= 0 && vertex_num < pol_num[polygon_num]);
    return pol_verts[polygon_num][vertex_num];
  }
  virtual int GetPolEdge(int polygon_num, int vertex_num, int& start_idx,
    int& end_idx) const
  {
    CS_ASSERT( polygon_num >= 0 && polygon_num < total_poly);
    CS_ASSERT( vertex_num >= 0 && vertex_num < pol_num[polygon_num]);
    start_idx = pol_verts[polygon_num][vertex_num];
    int inc = (vertex_num+1)%pol_num[polygon_num];
    end_idx = pol_verts[polygon_num][inc];
    return pol_edges[polygon_num][vertex_num];
  }
};


/** box haze hull */
class csHazeHullBox :
  public scfImplementationExt1<csHazeHullBox, csHazeHull, iHazeHullBox>
{
  csVector3 min, max;
public:
  /// create new.
  csHazeHullBox(const csVector3& a, const csVector3& b);
  ///
  virtual ~csHazeHullBox();
  //------------------------- iHazeHullBox implementation ----------------
  /// get settings
  virtual void GetSettings(csVector3& a, csVector3& b)
  {
    a = this->min;
    b = this->max;
  }
};

/** cone haze hull */
class csHazeHullCone :
  public scfImplementationExt1<csHazeHullCone, csHazeHull, iHazeHullCone>
{
  int nr_sides;
  csVector3 start, end;
  float start_radius, end_radius;
public:
  /// create new.
  csHazeHullCone(int nr, const csVector3& a, const csVector3& b, float ra,
    float rb);
  ///
  virtual ~csHazeHullCone();
  //------------------------- iHazeHullCone implementation ----------------
  /// get settings
  virtual void GetSettings(int &nr, csVector3& a, csVector3& b, float &ra,
    float &rb)
  {
    nr = this->nr_sides;
    a = this->start;
    b = this->end;
    ra = this->start_radius;
    rb = this->end_radius;
  }
};

/**
 * Haze mesh object.
 */
class csHazeMeshObject :
  public scfImplementationExt3<csHazeMeshObject, csObjectModel,
    iMeshObject, iHazeState, scfFakeInterface<iHazeFactoryState> >
{
private:
  csRef<iMeshObjectFactory> ifactory;
  iMeshWrapper* logparent;
  csHazeMeshObjectFactory* factory;

  csRef<iMaterialWrapper> material;
  uint MixMode;
  bool initialized;
  csRef<iMeshObjectDrawCallback> vis_cb;
  float radius;
  float current_lod;
  uint32 current_features;
  csFlags flags;

  /// bbox in object space
  csBox3 bbox;
  /**
   * Camera space bounding box is cached here.
   * GetCameraBoundingBox() will check the current camera number from the
   * camera to see if it needs to recalculate this.
   */
  csBox3 camera_bbox;
  csBox3 world_bbox;

  /// Current camera number.
  long cur_cameranr;
  /// Current movable number.
  long cur_movablenr;

  /// haze data
  csVector3 origin, directional;
  /// vector of csHazeLayer
  csPDelArray<csHazeLayer> layers;

  csRenderMeshHolder rmHolder;

  struct HazeRenderBuffer
  {
    size_t count;
    csRef<iRenderBuffer> buffer;

    HazeRenderBuffer() : count(0) { }
  };
  csFrameDataHolder<HazeRenderBuffer> renderBuffers;
  csFrameDataHolder<HazeRenderBuffer> indexBuffers;
  static CS::ShaderVarStringID vertex_name, texel_name, index_name;

  /**
   * Setup this object. This function will check if setup is needed.
   */
  void SetupObject ();

  /// Get the bounding box in transformed space.
  void GetTransformedBoundingBox (long cameranr, long movablenr,
      const csReversibleTransform& trans, csBox3& cbox);
  /**
   * Get the coordinates of the bbox in screen coordinates.
   * Fills in the boundingBox with the X and Y locations of the haze.
   * Returns the max Z location of the haze, or -1 if not
   * on-screen. If the haze is not on-screen, the X and Y values are not
   * valid.
   */
  float GetScreenBoundingBox (long cameranr, long movablenr, float fov,
        float sx, float sy,
        const csReversibleTransform& trans, csBox2& sbox, csBox3& cbox);

public:
  /// Constructor.
  csHazeMeshObject (csHazeMeshObjectFactory* factory);

  /// Destructor.
  virtual ~csHazeMeshObject ();

  /**
   *  Compute the outline of a particular hull.
   *  Pass a hull and its layerscale.
   *  Also pass the camera position in object space, and projection info.
   *  Returned is: layer_num - number of vertices in layer outline
   *  layer_poly - vertice indices of layer polygon, clockwise. new[]ed.
   *  layer_pts - vertice csvector3 position of layer polygon. new[]ed.
   *    these positions are in screen space.
   *  layer_uvs - u,v information per vertice.
   */
  void ComputeHullOutline(iHazeHull *hull, float layer_scale,
    const csVector3& campos, csReversibleTransform& tr_o2c, float fov,
    float shx, float shy, int &layer_num, int *& layer_poly,
    csVector3 *& layer_pts, csVector3** cam_pts, csVector2 *&layer_uvs);
  /** project a vertice in object space to screenspace */
  void ProjectO2S(csReversibleTransform& tr_o2c, float fov, float shiftx,
    float shifty, const csVector3& objpos, csVector3& scrpos, 
    csVector3* campos);

  //------------------------- iObjectModel implementation ----------------
  const csBox3& GetObjectBoundingBox ();
  void SetObjectBoundingBox (const csBox3& bbox);
  void GetRadius (float& rad, csVector3& cent)
  { rad = radius; cent = bbox.GetCenter (); }
  virtual iTerraFormer* GetTerraFormerColldet () { return 0; }
  virtual iTerrainSystem* GetTerrainColldet () { return 0; }

  ///--------------------- iMeshObject implementation ------------------------
  virtual iMeshObjectFactory* GetFactory () const { return ifactory; }
  virtual csFlags& GetFlags () { return flags; }
  virtual csPtr<iMeshObject> Clone () { return 0; }
  /** 
   * Recursively generate geometry. 
   * \a type 0=center 1,2 is rim, with alternative interpolation.
   * \a quality is the minimal cos of the angle.
   *  .9999 is maximum quality (infinite polygons).
   *  .90 is nice quality.
   *  0.70 is lower quality.
   *  0.50 is low quality.
   *  -1 is lowest quality (no adaptation)
   * Recursion will only go until >= maxdepth. Pass depth=0 at start.
   */
  void GenGeometryAdapt (iRenderView *rview, iGraphics3D *g3d, 
    int num_sides, csVector3* scrpts, csVector3* campts, csVector2* uvs,
    float layer_scale, float quality, int depth, int maxdepth);
  virtual csRenderMesh **GetRenderMeshes (int &n, iRenderView*, 
    iMovable*, uint32);
  virtual void SetVisibleCallback (iMeshObjectDrawCallback* cb)
  {
    vis_cb = cb;
  }
  virtual iMeshObjectDrawCallback* GetVisibleCallback () const
  {
    return vis_cb;
  }
  virtual void NextFrame (csTicks current_time, const csVector3& /*pos*/,
    uint /*currentFrame*/);
  virtual void HardTransform (const csReversibleTransform& t);
  virtual bool SupportsHardTransform () const { return false; }
  virtual bool HitBeamOutline (const csVector3&, const csVector3&,
        csVector3&, float*)
  { return false; }
  virtual bool HitBeamObject (const csVector3&, const csVector3&,
  	csVector3&, float*, int* = 0, iMaterialWrapper** = 0,
    csArray<iMaterialWrapper*>* materials = 0) { return false; }
  virtual void SetMeshWrapper (iMeshWrapper* lp) { logparent = lp; }
  virtual iMeshWrapper* GetMeshWrapper () const { return logparent; }

  virtual iObjectModel* GetObjectModel () { return this; }
  virtual bool SetColor (const csColor&) { return false; }
  virtual bool GetColor (csColor&) const { return false; }
  virtual bool SetMaterialWrapper (iMaterialWrapper* mat)
  {
    material = mat;
    return true;
  }
  virtual iMaterialWrapper* GetMaterialWrapper () const { return material; }
  virtual void SetMixMode (uint mode) { MixMode = mode; }
  virtual uint GetMixMode () const { return MixMode; }

  /**
   * see imesh/object.h for specification. The default implementation
   * does nothing.
   */
  virtual void PositionChild (iMeshObject* /*child*/, csTicks /*current_time*/) { }
  virtual void BuildDecal(const csVector3* pos, float decalRadius,
          iDecalBuilder* decalBuilder)
  {
  }

  //------------------------- iHazeState implementation ----------------
  virtual void SetOrigin(const csVector3& pos) { this->origin = pos; }
  virtual const csVector3& GetOrigin() const {return this->origin;}
  virtual void SetDirectional(const csVector3& pos)
  { this->directional=pos;}
  virtual const csVector3& GetDirectional() const
  {return this->directional;}
  virtual size_t GetLayerCount() const {return this->layers.GetSize ();}
  virtual void AddLayer(iHazeHull *hull, float scale)
  {
    csHazeLayer *lay = new csHazeLayer(hull, scale);
    this->layers.Push(lay);
  }
  virtual void SetLayerHull(int layer, iHazeHull* hull)
  {
    if (hull) hull->IncRef();
    if (this->layers[layer]->hull)
      this->layers[layer]->hull->DecRef();
    this->layers[layer]->hull = hull;
  }
  virtual iHazeHull* GetLayerHull(int layer) const
  { return this->layers[layer]->hull; }
  virtual void SetLayerScale(int layer, float scale)
  { this->layers[layer]->scale = scale; }
  virtual float GetLayerScale(int layer) const
  { return this->layers[layer]->scale; }
};

/**
 * Factory for 2D sprites. This factory also implements iHazeFactoryState.
 */
class csHazeMeshObjectFactory :
  public scfImplementation3<csHazeMeshObjectFactory,
    iMeshObjectFactory, iHazeFactoryState, iHazeHullCreation>
{
private:
  csRef<iMaterialWrapper> material;
  uint MixMode;
  /// haze state info
  csVector3 origin, directional;
  /// vector of csHazeLayer
  csPDelArray<csHazeLayer> layers;
  iMeshFactoryWrapper* logparent;
  iMeshObjectType* haze_type;
  csFlags flags;
public:
  iObjectRegistry* object_reg;

  /// Constructor.
  csHazeMeshObjectFactory (csHazeMeshObjectType* pParent);

  /// Destructor.
  virtual ~csHazeMeshObjectFactory ();

  /// Get the layers vector
  csPDelArray<csHazeLayer>* GetLayers() {return &layers;}

  //------------------------ iMeshObjectFactory implementation --------------
  virtual csFlags& GetFlags () { return flags; }
  virtual csPtr<iMeshObject> NewInstance ();
  virtual csPtr<iMeshObjectFactory> Clone () { return 0; }
  virtual void HardTransform (const csReversibleTransform&) { }
  virtual bool SupportsHardTransform () const { return false; }
  virtual void SetMeshFactoryWrapper (iMeshFactoryWrapper* lp)
  { logparent = lp; }
  virtual iMeshFactoryWrapper* GetMeshFactoryWrapper () const
  { return logparent; }
  virtual iMeshObjectType* GetMeshObjectType () const { return haze_type; }
  virtual iObjectModel* GetObjectModel () { return 0; }
  virtual bool SetMaterialWrapper (iMaterialWrapper* material)
  {
    csHazeMeshObjectFactory::material = material;
    return true;
  }
  virtual iMaterialWrapper* GetMaterialWrapper () const { return material; }
  virtual void SetMixMode (uint mode) { MixMode = mode; }
  virtual uint GetMixMode () const { return MixMode; }

  //------------------------- iHazeFactoryState implementation ----------------
  virtual void SetOrigin(const csVector3& pos) { this->origin = pos; }
  virtual const csVector3& GetOrigin() const {return this->origin;}
  virtual void SetDirectional(const csVector3& pos)
  { this->directional=pos;}
  virtual const csVector3& GetDirectional() const
  {return this->directional;}
  virtual size_t GetLayerCount() const {return this->layers.GetSize ();}
  virtual void AddLayer(iHazeHull *hull, float scale)
  {
    csHazeLayer *lay = new csHazeLayer(hull, scale);
    this->layers.Push(lay);
  }
  virtual void SetLayerHull(int layer, iHazeHull* hull)
  {
    if(hull) hull->IncRef();
    if(this->layers[layer]->hull)
      this->layers[layer]->hull->DecRef();
    this->layers[layer]->hull = hull;
  }
  virtual iHazeHull* GetLayerHull(int layer) const
  { return this->layers[layer]->hull; }
  virtual void SetLayerScale(int layer, float scale)
  { this->layers[layer]->scale = scale; }
  virtual float GetLayerScale(int layer) const
  { return this->layers[layer]->scale; }

  //------------------------- iHazeHullCreation implementation ----------------
  virtual csRef<iHazeHullBox> CreateBox(
    const csVector3& a, const csVector3& b) const
  {
    csHazeHullBox* c = new csHazeHullBox(a, b);
    csRef<iHazeHullBox> x;
    x.AttachNew(c);
    return x;
  }
  virtual csRef<iHazeHullCone> CreateCone(int nr_sides,
    const csVector3& start, const csVector3& end, float srad,
    float erad) const
  {
    csHazeHullCone* c = new csHazeHullCone(nr_sides, start, end, srad, erad);
    csRef<iHazeHullCone> x;
    x.AttachNew(c);
    return x;
  }
};

/**
 * Sprite 2D type. This is the plugin you have to use to create instances
 * of csHazeMeshObjectFactory.
 */
class csHazeMeshObjectType :
  public scfImplementation2<csHazeMeshObjectType,
    iMeshObjectType, iComponent>
{
public:
  iObjectRegistry* object_reg;

  /// Constructor.
  csHazeMeshObjectType (iBase*);
  /// Destructor.
  virtual ~csHazeMeshObjectType ();
  /// New Factory.
  virtual csPtr<iMeshObjectFactory> NewFactory ();

  virtual bool Initialize (iObjectRegistry* object_reg) 
  { this->object_reg = object_reg; return true; }
};

#endif // __CS_HAZE_H__
