/*
    Copyright (C) 2000-2001 by Jorrit Tyberghein

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

#ifndef __CS_IENGINE_MESH_H__
#define __CS_IENGINE_MESH_H__

/**\file
 * Mesh interfaces
 */
/**
 * \addtogroup engine3d_meshes
 * @{ */

#include "csutil/scf.h"

#include "csgeom/box.h"
#include "csgeom/vector3.h"

#include "ivideo/graph3d.h"
#include "ivideo/rendermesh.h"

struct iCamera;
struct iLODControl;
struct iMeshFactoryList;
struct iMeshFactoryWrapper;
struct iMeshList;
struct iMeshObject;
struct iMeshObjectFactory;
struct iMeshWrapper;
struct iMovable;
struct iObject;
struct iPortalContainer;
struct iRenderView;
struct iShaderVariableContext;
struct iSharedVariable;
struct iSceneNode;
struct iMaterialWrapper;

class csEllipsoid;
class csFlags;
class csReversibleTransform;

/** \name Meshwrapper flags
 * @{ */
/**
 * If CS_ENTITY_DETAIL is set then this entity is a detail
 * object. A detail object is treated as a single object by
 * the engine. The engine can do several optimizations on this.
 * In general you should use this flag for small and detailed
 * objects.
 * This flag is currently not used.
 */
#define CS_ENTITY_DETAIL 2

/**
 * If CS_ENTITY_CAMERA is set then this entity will be always
 * be centerer around the same spot relative to the camera. This
 * is useful for skyboxes or skydomes. Important note! When you
 * use an object with this flag you should also add this object to
 * a render priority that also has the camera flag set (see
 * iEngine::SetRenderPriorityCamera()).
 */
#define CS_ENTITY_CAMERA 4

/**
 * If CS_ENTITY_INVISIBLEMESH is set then this thing will not be
 * rendered. It will still cast shadows and be present otherwise.
 * Use the CS_ENTITY_NOSHADOWS flag to disable shadows. Using this
 * flag does NOT automatically imply that HitBeam() will ignore this
 * mesh. For that you need to set CS_ENTITY_NOHITBEAM.
 */
#define CS_ENTITY_INVISIBLEMESH 8

/**
 * If CS_ENTITY_INVISIBLE is set then this thing will not be rendered.
 * It will still cast shadows and be present otherwise. Use the
 * CS_ENTITY_NOSHADOWS flag to disable shadows. Making a mesh invisible
 * will also imply that HitBeam() will ignore it.
 */
#define CS_ENTITY_INVISIBLE (CS_ENTITY_INVISIBLEMESH+CS_ENTITY_NOHITBEAM)

/**
 * If CS_ENTITY_NOSHADOWCAST is set then this mesh will not cast
 * shadows. Lighting will still be calculated for it though. Use the
 * CS_ENTITY_NOLIGHTING flag to disable that.
 */
#define CS_ENTITY_NOSHADOWCAST 16
#define CS_ENTITY_NOSHADOWS   CS_ENTITY_NOSHADOWCAST

/**
 * If CS_ENTITY_NOLIGHTING is set then this thing will not be lit.
 * It may still cast shadows though. Use the CS_ENTITY_NOSHADOWS flag
 * to disable that.
 */
#define CS_ENTITY_NOLIGHTING 32

/**
 * If CS_ENTITY_NOHITBEAM is set then this thing will not react to
 * HitBeam calls.
 */
#define CS_ENTITY_NOHITBEAM 64

/**
 * If CS_ENTITY_NOCLIP is set then this entity will be drawn fully
 * (unclipped to portal frustum) and only once for every frame/camera
 * combination. This is useful in a scenario where you have an indoor
 * sector with lots of portals to an outdoor sector. In the outdoor sector
 * there is a complex terrain mesh object and you really only want to render
 * that once with full screen and not many times clipped to individual
 * portals.
 */
#define CS_ENTITY_NOCLIP 128

/**
 * If CS_ENTITY_NODECAL is set then this entity will not accept decals.
 */
#define CS_ENTITY_NODECAL 256

/**
 * Indicates that static lighting was computed for this mesh.
 */
#define CS_ENTITY_STATICLIT 512

/**
 * If CS_ENTITY_NOSHADOWRECEIVE is set then this mesh will not receive
 * shadows. 
 */
#define CS_ENTITY_NOSHADOWRECEIVE 1024

/**
 * Mark a mesh as a shadow caster if a render managers allows limiting shadow
 * casting limited to select meshes.
 */
#define CS_ENTITY_LIMITEDSHADOWCAST 2048

/** @} */

/**
 * Set a callback which is called just before the object is drawn.
 * This is useful to do some expensive computations which only need
 * to be done on a visible object. Note that this function will be
 * called even if the object is not visible. In general it is called
 * if there is a likely probability that the object is visible (i.e.
 * it is in the same sector as the camera for example).
 *
 * This callback is used by:
 * - iMeshWrapper
 */
struct iMeshDrawCallback : public virtual iBase
{
  SCF_INTERFACE (iMeshDrawCallback, 0, 0, 1);

  /**
   * Before drawing. It is safe to delete this callback
   * in this function.
   */
  virtual bool BeforeDrawing (iMeshWrapper* spr, iRenderView* rview) = 0;
};

/**
 * Return structure for the iMeshWrapper::HitBeam() routines.
 * \sa csSectorHitBeamResult csBulletHitBeamResult
 */
struct csHitBeamResult
{
  /// Intersection point in object space.
  csVector3 isect;
  /**
   * Value between 0 and 1 indicating where on the segment the intersection
   * occured.
   */
  float r;
  /// Only for HitBeamObject: the polygon/triangle index that was hit.
  int polygon_idx;
  /**
   * Only for HitBeamObject and HitBeam: the material that was hit. Can be 0
   * in the case that the meshobject doesn't support getting the material.
   */
  iMaterialWrapper* material;
  /**
   * Only for HitBeamObject and HitBeam: the materials that were hit. Can be
   * empty in the case that the meshobject doesn't support getting the material.
   */
  csArray<iMaterialWrapper*> materials;
  /**
   * Only for HitBeamBBox: Face number that was hit.
   * \sa csIntersect3::BoxSegment
   */
  int facehit;
  /**
   * For all except HitBeamBBox: true if hit, false otherwise.
   */
  bool hit;

  csHitBeamResult() : material(0), hit(false)
  {
  }
};

/**
 * Return structure for iMeshWrapper::GetScreenBoundingBox().
 */
struct csScreenBoxResult
{
  /// 2D box in screen space.
  csBox2 sbox;
  /// 3D box in camera space.
  csBox3 cbox;
  /**
   * -1 if object behind the camera or else the distance between
   * the camera and the furthest point of the 3D box.
   */
  float distance;
};

/**
 * A mesh wrapper is an engine-level object that wraps around an actual
 * mesh object (iMeshObject). Every mesh object in the engine is represented
 * by a mesh wrapper, which keeps the pointer to the mesh object, its position,
 * its name, etc.
 *
 * Think of the mesh wrapper as the hook that holds the mesh object in the
 * engine. An effect of this is that the i???State interfaces (e.g.
 * iSprite3DState) must be queried from the mesh *objects*, not the wrappers!
 *
 * Note that a mesh object should never be contained in more than one wrapper.
 *
 * Main creators of instances implementing this interface:
 * - iEngine::CreateMeshWrapper()
 * - iEngine::LoadMeshWrapper()
 * - iEngine::CreatePortalContainer()
 * - iEngine::CreatePortal()
 * - iLoader::LoadMeshObject()
 * - CS::Geometry::GeneralMeshBuilder::CreateMesh()
 *
 * Main ways to get pointers to this interface:
 * - iEngine::FindMeshObject()
 * - iMeshList::Get()
 * - iMeshList::FindByName()
 * - iMeshWrapperIterator::Next()
 * - iLoaderContext::FindMeshObject()
 *
 * Main users of this interface:
 * - iEngine
 */
struct iMeshWrapper : public virtual iBase
{
  SCF_INTERFACE(iMeshWrapper, 3, 0, 0);

  /**
   * Get the iObject for this mesh object. This can be used to get the
   * name of the mesh wrapper and also to attach other user objects
   * to this mesh (like for collision detection or game data).
   */
  virtual iObject *QueryObject () = 0;

  /// Get the iMeshObject.
  virtual iMeshObject* GetMeshObject () const = 0;
  /// Set the iMeshObject.
  virtual void SetMeshObject (iMeshObject*) = 0;
  /**
   * If this mesh is a portal container you can use GetPortalContainer() to
   * get the portal container interface.
   */
  virtual iPortalContainer* GetPortalContainer () const = 0;

  /// Get the parent factory.
  virtual iMeshFactoryWrapper *GetFactory () const = 0;
  /// Set the parent factory (this only sets a pointer).
  virtual void SetFactory (iMeshFactoryWrapper* factory) = 0;

  /**
   * Get the movable instance for this object.
   * It is very important to call GetMovable()::UpdateMove()
   * after doing any kind of modification to this movable
   * to make sure that internal data structures are
   * correctly updated.
   */
  virtual iMovable* GetMovable () const = 0;

  /**
   * Get the scene node that this object represents.
   */
  virtual iSceneNode* QuerySceneNode () = 0;

  /**
   * Find a child mesh by name. If there is a colon in the name
   * then this function is able to search for children too.
   * i.e. like mesh:childmesh:childmesh.
   */
  virtual iMeshWrapper* FindChildByName (const char* name) = 0;

  /**
   * This routine will find out in which sectors a mesh object
   * is positioned. To use it the mesh has to be placed in one starting
   * sector. This routine will then start from that sector, find all
   * portals that touch the sprite and add all additional sectors from
   * those portals. Note that this routine using a bounding sphere for
   * this test so it is possible that the mesh will be added to sectors
   * where it really isn't located (but the sphere is).
   * <p>
   * If the mesh is already in several sectors those additional sectors
   * will be ignored and only the first one will be used for this routine.
   * <p>
   * Placing a mesh in different sectors is important when the mesh crosses
   * a portal boundary. If you don't do this then it is possible that the
   * mesh will be clipped wrong. For small mesh objects you can get away
   * by not doing this in most cases.
   */
  virtual void PlaceMesh () = 0;

  /**
   * Check if this mesh is hit by this object space vector.
   * This will do a rough but fast test based on bounding box only.
   * So this means that it might return a hit even though the object
   * isn't really hit at all. Depends on how much the bounding box
   * overestimates the object. This also returns the face number
   * as defined in csBox3 on which face the hit occured. Useful for
   * grid structures.
   * \sa csHitBeamResult
   */
  virtual csHitBeamResult HitBeamBBox (const csVector3& start,
  	const csVector3& end) = 0;

  /**
   * Check if this object is hit by this object space vector.
   * Outline check.
   * \sa csHitBeamResult
   */
  virtual csHitBeamResult HitBeamOutline (const csVector3& start,
  	const csVector3& end) = 0;

  /**
   * Check if this object is hit by this object space vector.
   * Return the collision point in object space coordinates. This version
   * is more accurate than HitBeamOutline.
   * This version can also return the material that was hit (this will
   * only happen if 'do_material' is true). This is not
   * supported by all meshes so this can return 0 even if there was a hit.
   * \sa csHitBeamResult
   */
  virtual csHitBeamResult HitBeamObject (const csVector3& start,
  	const csVector3& end, bool do_material = false) = 0;

  /**
   * Check if this object is hit by this world space vector.
   * Return the collision point in world space coordinates.
   * This version can also return the material that was hit (this will
   * only happen if 'do_material' is true). This is not
   * supported by all meshes so this can return 0 even if there was a hit.
   * \sa csHitBeamResult iSector::HitBeam() iSector::HitBeamPortals()
   * iBulletDynamicSystem::HitBeam()
   */
  virtual csHitBeamResult HitBeam (const csVector3& start,
  	const csVector3& end, bool do_material = false) = 0;

  /**
   * Set a callback which is called just before the object is drawn.
   * This is useful to do some expensive computations which only need
   * to be done on a visible object. Note that this function will be
   * called even if the object is not visible. In general it is called
   * if there is a likely probability that the object is visible (i.e.
   * it is in the same sector as the camera for example).
   */
  virtual void SetDrawCallback (iMeshDrawCallback* cb) = 0;

  /**
   * Remove a draw callback.
   */
  virtual void RemoveDrawCallback (iMeshDrawCallback* cb) = 0;

  /// Get the number of draw callbacks.
  virtual int GetDrawCallbackCount () const = 0;

  /// Get the specified draw callback.
  virtual iMeshDrawCallback* GetDrawCallback (int idx) const = 0;

  /**
   * The renderer will render all objects in a sector based on this
   * number. Low numbers get rendered first. High numbers get rendered
   * later. There are a few often used slots:
   * - 1. Sky objects are rendered before
   *   everything else. Usually they are rendered using ZFILL (or ZNONE).
   * - 2. Walls are rendered after that. They
   *   usually use ZFILL.
   * - 3. After that normal objects are
   *   rendered using the Z-buffer (ZUSE).
   * - 4. Alpha transparent objects or objects
   *   using some other transparency system are rendered after that. They
   *   are usually rendered using ZTEST.
   */
  virtual void SetRenderPriority (CS::Graphics::RenderPriority rp) = 0;
  /**
   * Get the render priority.
   */
  virtual CS::Graphics::RenderPriority GetRenderPriority () const = 0;

  /**
   * Same as SetRenderPriority() but this version will recursively set
   * render priority for the children too.
   */
  virtual void SetRenderPriorityRecursive (CS::Graphics::RenderPriority rp) = 0;

  /**
   * Get flags for this meshwrapper. The following flags are supported:
   * - #CS_ENTITY_DETAIL: this is a detail object. Again this is a hint
   *   for the engine to render this object differently. Currently not used.
   * - #CS_ENTITY_CAMERA: entity will always be centered around the camera.
   * - #CS_ENTITY_INVISIBLEMESH: entity is not rendered. 
   * - #CS_ENTITY_NOHITBEAM: this entity will not be considered by HitBeam() 
   *   calls.
   * - #CS_ENTITY_INVISIBLE: means that either CS_ENTITY_INVISIBLEMESH and 
   *   CS_ENTITY_NOHITBEAM are set.
   * - #CS_ENTITY_NOSHADOWS: cast no shadows.
   * - #CS_ENTITY_NOLIGHTING: do not light this object.
   * - #CS_ENTITY_NOCLIP: do not clip this object.
   *
   * \remarks Despite the name, this method does not only provide read access
   *   to the mesh flags, as the returned reference to a csFlags object also 
   *   provides write access.
   */
  virtual csFlags& GetFlags () = 0;

  /**
   * Set some flags with the given mask for this mesh and all children.
   * \param mask The bits to modify; only those bits are affected.
   * \param flags The values the bits specified in \a mask are set to.
   * <p>
   * Enabling flags:
   * \code
   * csRef<iMeshWrapper> someWrapper = ...;
   * someWrapper->SetFlagsRecursive (CS_ENTITY_INVISIBLE | CS_ENTITY_NOCLIP);
   * \endcode
   * <p>
   * Disabling flags:
   * \code
   * csRef<iMeshWrapper> someWrapper = ...;
   * someWrapper->SetFlagsRecursive (CS_ENTITY_INVISIBLE | CS_ENTITY_NOCLIP, 0);
   * \endcode
   * \remarks To set flags non-recursive, use GetFlags().Set().
   */
  virtual void SetFlagsRecursive (uint32 mask, uint32 flags = ~0) = 0;

  /**
   * Set the Z-buf drawing mode to use for this object.
   * Possible values are:
   * - #CS_ZBUF_NONE: do not read nor write the Z-buffer.
   * - #CS_ZBUF_FILL: only write the Z-buffer but do not read.
   * - #CS_ZBUF_USE: write and read the Z-buffer.
   * - #CS_ZBUF_TEST: only read the Z-buffer but do not write.
   */
  virtual void SetZBufMode (csZBufMode mode) = 0;
  /**
   * Get the Z-buf drawing mode.
   */
  virtual csZBufMode GetZBufMode () const = 0;
  /**
   * Same as SetZBufMode() but this will also set the z-buf
   * mode for the children too.
   */
  virtual void SetZBufModeRecursive (csZBufMode mode) = 0;

  /**
   * Do a hard transform of this object.
   * This transformation and the original coordinates are not
   * remembered but the object space coordinates are directly
   * computed (world space coordinates are set to the object space
   * coordinates by this routine). Note that some implementations
   * of mesh objects will not change the orientation of the object but
   * only the position.
   * <p>
   * Note also that some mesh objects don't support HardTransform. You
   * can find out by calling iMeshObject::SupportsHardTransform().
   * In that case you can sometimes still call HardTransform() on the
   * factory.
   */
  virtual void HardTransform (const csReversibleTransform& t) = 0;

  /**
   * Get the bounding box of this object in world space.
   * This routine will cache the bounding box and only recalculate it
   * if the movable changes.
   */
  virtual const csBox3& GetWorldBoundingBox () = 0;

  /**
   * Get the bounding box of this object after applying a transformation to it.
   * This is really a very inaccurate function as it will take the bounding
   * box of the object in object space and then transform this bounding box.
   */
  virtual csBox3 GetTransformedBoundingBox (
  	const csReversibleTransform& trans) = 0;

  /**
   * Get a very inaccurate bounding box of the object in screen space.
   * Returns -1 if object behind the camera or else the distance between
   * the camera and the furthest point of the 3D box.
   */
  virtual csScreenBoxResult GetScreenBoundingBox (iCamera* camera) = 0;

  /// Get the radius of this mesh and all its children.
  virtual csSphere GetRadius () const = 0;

  /**
   * Reset minimum/maximum render range to defaults (i.e. unlimited).
   */
  virtual void ResetMinMaxRenderDistance () = 0;

  /**
   * Set the minimum distance at which this mesh will be rendered.
   * By default this is 0.
   */
  virtual void SetMinimumRenderDistance (float min) = 0;

  /**
   * Get the minimum distance at which this mesh will be rendered.
   */
  virtual float GetMinimumRenderDistance () const = 0;

  /**
   * Set the maximum distance at which this mesh will be rendered.
   * By default this is 0.
   */
  virtual void SetMaximumRenderDistance (float min) = 0;

  /**
   * Get the maximum distance at which this mesh will be rendered.
   */
  virtual float GetMaximumRenderDistance () const = 0;

  /**
   * Set the minimum distance at which this mesh will be rendered.
   * This version uses a variable.
   * By default this is -1000000000.0.
   */
  virtual void SetMinimumRenderDistanceVar (iSharedVariable* min) = 0;

  /**
   * Get the minimum distance at which this mesh will be rendered.
   * If lod was not set using variables then it will return 0.
   */
  virtual iSharedVariable* GetMinimumRenderDistanceVar () const = 0;

  /**
   * Set the maximum distance at which this mesh will be rendered.
   * This version uses a variable.
   * By default this is 1000000000.0.
   */
  virtual void SetMaximumRenderDistanceVar (iSharedVariable* min) = 0;

  /**
   * Get the maximum distance at which this mesh will be rendered.
   * If lod was not set using variables then it will return 0.
   */
  virtual iSharedVariable* GetMaximumRenderDistanceVar () const = 0;

  /**
   * Create a LOD control for this mesh wrapper. This is relevant
   * only if the mesh is a hierarchical mesh. The LOD control will be
   * used to select which children are visible and which are not.
   * Use this to create static lod.
   */
  virtual iLODControl* CreateStaticLOD () = 0;

  /**
   * Destroy the LOD control for this mesh. After this call the hierarchical
   * mesh will act as usual.
   */
  virtual void DestroyStaticLOD () = 0;

  /**
   * Get the LOD control for this mesh. This will return 0 if this is a normal
   * (hierarchical) mesh. Otherwise it will return an object with which you
   * can control the static LOD of this object.
   */
  virtual iLODControl* GetStaticLOD () = 0;

  /**
   * Set a given child mesh at a specific lod level. Note that a mesh
   * can be at several lod levels at once.
   */
  virtual void AddMeshToStaticLOD (int lod, iMeshWrapper* mesh) = 0;

  /**
   * Remove a child mesh from all lod levels. The mesh is not removed
   * from the list of child meshes however.
   */
  virtual void RemoveMeshFromStaticLOD (iMeshWrapper* mesh) = 0;

  /**
   * Get the shader variable context of the mesh object.
   */
  virtual iShaderVariableContext* GetSVContext() = 0;

  /**
   * Get the render mesh list for this mesh wrapper and given view
   */
  virtual csRenderMesh** GetRenderMeshes (int& num, iRenderView* rview,
    uint32 frustum_mask) = 0;

  /**
   * Adds a render mesh to the list of extra render meshes.
   * This list is used for special cases (like decals) where additional
   * things need to be renderered for the mesh in an abstract way.
   */
  virtual size_t AddExtraRenderMesh(CS::Graphics::RenderMesh* renderMesh, 
    csZBufMode zBufMode) = 0;
  /// \deprecated Deprecated in 1.3. Pass render priority in render mesh
  CS_DEPRECATED_METHOD_MSG("Pass render priority in render mesh")
  virtual void AddExtraRenderMesh(CS::Graphics::RenderMesh* renderMesh, 
    CS::Graphics::RenderPriority priority, csZBufMode zBufMode) = 0;
          
  /// Get a specific extra render mesh.
  virtual CS::Graphics::RenderMesh* GetExtraRenderMesh (size_t idx) const = 0;

  /// Get number of extra render meshes.
  virtual size_t GetExtraRenderMeshCount () const = 0;

  /** 
   * Gets the priority of a specific extra rendermesh.
   * \deprecated Deprecated in 1.3. Obtain render priority from render mesh
   */
  CS_DEPRECATED_METHOD_MSG("Obtain render priority from render mesh")
  virtual CS::Graphics::RenderPriority GetExtraRenderMeshPriority(size_t idx) const = 0;

  /**
   * Gets the z-buffer mode of a specific extra rendermesh
   */
  virtual csZBufMode GetExtraRenderMeshZBufMode(size_t idx) const = 0;

  //@{
  /**
   * Deletes a specific extra rendermesh
   */
  virtual void RemoveExtraRenderMesh(CS::Graphics::RenderMesh* renderMesh) = 0;
  virtual void RemoveExtraRenderMesh(size_t idx) = 0;
  //@}

  /**
   * Adds a (pseudo-)instance at the given position.
   * Returns the instance transform shadervar.
   */
  virtual csShaderVariable* AddInstance(csVector3& position, csMatrix3& rotation) = 0;

  /**
   * Removes a (pseudo-)instance of the mesh.
   */
  virtual void RemoveInstance(csShaderVariable* instance) = 0;
};

/**
 * A mesh factory wrapper is an engine-level object that wraps around a
 * mesh object factory (iMeshObjectFactory). Every mesh object factory in
 * the engine is represented by a mesh factory wrapper, which keeps the
 * pointer to the mesh factory, its name, etc.
 *
 * Think of the mesh factory wrapper as the hook that holds the mesh
 * factory in the engine. An effect of this is that the i???FactoryState
 * interfaces (e.g. iSprite3DFactoryState) must be queried from the mesh
 * *factories*, not the wrappers!
 *
 * Main creators of instances implementing this interface:
 * - iEngine::CreateMeshFactory()
 * - iEngine::LoadMeshFactory()
 * - iLoader::LoadMeshObjectFactory()
 *
 * Main ways to get pointers to this interface:
 * - iEngine::FindMeshFactory()
 * - iMeshFactoryList::Get()
 * - iMeshFactoryList::FindByName()
 * - iLoaderContext::FindMeshFactory()
 *
 * Main users of this interface:
 * - iEngine
 */
struct iMeshFactoryWrapper : public virtual iBase
{
  SCF_INTERFACE(iMeshFactoryWrapper, 2, 1, 0);
  /// Get the iObject for this mesh factory.
  virtual iObject *QueryObject () = 0;
  /// Get the iMeshObjectFactory.
  virtual iMeshObjectFactory* GetMeshObjectFactory () const = 0;
  /// Set the mesh object factory.
  virtual void SetMeshObjectFactory (iMeshObjectFactory* fact) = 0;
  /**
   * Do a hard transform of this factory.
   * This transformation and the original coordinates are not
   * remembered but the object space coordinates are directly
   * computed (world space coordinates are set to the object space
   * coordinates by this routine). Note that some implementations
   * of mesh objects will not change the orientation of the object but
   * only the position.
   */
  virtual void HardTransform (const csReversibleTransform& t) = 0;
  /**
   * Create mesh objects from this factory. If the factory has a hierarchy
   * then a hierarchical mesh object will be created.
   */
  virtual csPtr<iMeshWrapper> CreateMeshWrapper () = 0;

  /**
   * Get default flags for all meshes created from this factory.
   * The following flags are supported:
   * - #CS_ENTITY_DETAIL: this is a detail object. Again this is a hint
   *   for the engine to render this object differently. Currently not used.
   * - #CS_ENTITY_CAMERA: entity will always be centered around the camera.
   * - #CS_ENTITY_INVISIBLEMESH: entity is not rendered. 
   * - #CS_ENTITY_NOHITBEAM: this entity will not be considered by HitBeam() 
   *   calls.
   * - #CS_ENTITY_INVISIBLE: means that either CS_ENTITY_INVISIBLEMESH and 
   *   CS_ENTITY_NOHITBEAM are set.
   * - #CS_ENTITY_NOSHADOWS: cast no shadows.
   * - #CS_ENTITY_NOLIGHTING: do not light this object.
   * - #CS_ENTITY_NOCLIP: do not clip this object.
   *
   * \remarks Despite the name, this method does not only provide read access
   *   to the mesh flags, as the returned reference to a csFlags object also 
   *   provides write access.
   */
  virtual csFlags& GetFlags () = 0;

  /**
   * Get the parent of this factory. This will be 0 if this factory
   * has no parent.
   */
  virtual iMeshFactoryWrapper* GetParentContainer () const = 0;
  /**
   * Set the parent of this factory. This will only change the 'parent'
   * pointer but not add the factory as a child! Internal use only.
   */
  virtual void SetParentContainer (iMeshFactoryWrapper *p) = 0;

  /**
   * Get all the children of this mesh factory.
   */
  virtual iMeshFactoryList* GetChildren () = 0;

  /**
   * Get optional relative transform (relative to parent).
   */
  virtual csReversibleTransform& GetTransform () = 0;

  /**
   * Set optional relative transform (relative to parent).
   */
  virtual void SetTransform (const csReversibleTransform& tr) = 0;

  /**
   * Create a LOD control template for this factory. This is relevant
   * only if the factory is hierarchical. The LOD control will be
   * used to select which children are visible and which are not.
   * Use this to create static lod.
   */
  virtual iLODControl* CreateStaticLOD () = 0;

  /**
   * Destroy the LOD control for this factory.
   */
  virtual void DestroyStaticLOD () = 0;

  /**
   * Get the LOD control for this factory. This will return 0 if this is a
   * normal (hierarchical) factory. Otherwise it will return an object with
   * which you can control the static LOD of this factory.
   */
  virtual iLODControl* GetStaticLOD () = 0;

  /**
   * Set the LOD function parameters for this factory. These control the
   * function:
   * <pre>
   *    float lod = m * distance + a;
   * </pre>
   */
  virtual void SetStaticLOD (float m, float a) = 0;

  /**
   * Get the LOD function parameters for this factory.
   */
  virtual void GetStaticLOD (float& m, float& a) const = 0;

  /**
   * Set a given child factory at a specific lod level. Note that a factory
   * can be at several lod levels at once.
   */
  virtual void AddFactoryToStaticLOD (int lod, iMeshFactoryWrapper* fact) = 0;

  /**
   * Remove a child factory from all lod levels. The factory is not removed
   * from the list of factories however.
   */
  virtual void RemoveFactoryFromStaticLOD (iMeshFactoryWrapper* fact) = 0;

  /**
   * Set the Z-buf drawing mode to use for this factory. All objects created
   * from this factory will have this mode as default.
   * Possible values are:
   * - #CS_ZBUF_NONE: do not read nor write the Z-buffer.
   * - #CS_ZBUF_FILL: only write the Z-buffer but do not read.
   * - #CS_ZBUF_USE: write and read the Z-buffer.
   * - #CS_ZBUF_TEST: only read the Z-buffer but do not write.
   */
  virtual void SetZBufMode (csZBufMode mode) = 0;
  /**
   * Get the Z-buf drawing mode.
   */
  virtual csZBufMode GetZBufMode () const = 0;
  /**
   * Same as SetZBufMode() but this will also set the z-buf
   * mode for the children too.
   */
  virtual void SetZBufModeRecursive (csZBufMode mode) = 0;

  /**
   * The renderer will render all objects in a sector based on this
   * number. Low numbers get rendered first. High numbers get rendered
   * later. The value for the factory is used as a default for objects
   * created from that factory. There are a few often used slots:
   * - 1. Sky objects are rendered before
   *   everything else. Usually they are rendered using ZFILL (or ZNONE).
   * - 2. Walls are rendered after that. They
   *   usually use ZFILL.
   * - 3. After that normal objects are
   *   rendered using the Z-buffer (ZUSE).
   * - 4. Alpha transparent objects or objects
   *   using some other transparency system are rendered after that. They
   *   are usually rendered using ZTEST.
   */
  virtual void SetRenderPriority (long rp) = 0;
  /**
   * Get the render priority.
   */
  virtual long GetRenderPriority () const = 0;

  /**
   * Same as SetRenderPriority() but this version will recursively set
   * render priority for the children too.
   */
  virtual void SetRenderPriorityRecursive (long rp) = 0;

  /**
   * Get the shader variable context of the mesh factory.
   */
  virtual iShaderVariableContext* GetSVContext() = 0;

  /**
   * Sets the instance factory.
   */
  virtual void SetInstanceFactory(iMeshFactoryWrapper* meshfact) = 0;

  /**
   * Returns the instance factory.
   */
  virtual iMeshFactoryWrapper* GetInstanceFactory() const = 0;

  /**
   * Adds a (pseudo-)instance of the instance factory at the given position.
   */
  virtual void AddInstance(csVector3& position, csMatrix3& rotation) = 0;

  /**
   * Returns the instancing transforms array shadervar.
   */
  virtual csShaderVariable* GetInstances() const = 0;
};

/**
 * A list of meshes.
 *
 * Main ways to get pointers to this interface:
 *   - iEngine::GetMeshes()
 *   - iSector::GetMeshes()
 *
 * Main users of this interface:
 *   - iEngine
 */
struct iMeshList : public virtual iBase
{
  SCF_INTERFACE(iMeshList, 2,0,0);
  /// Return the number of meshes in this list
  virtual int GetCount () const = 0;

  /// Return a mesh by index
  virtual iMeshWrapper *Get (int n) const = 0;

  /// Add a mesh
  virtual int Add (iMeshWrapper *obj) = 0;

  /// Remove a mesh
  virtual bool Remove (iMeshWrapper *obj) = 0;

  /// Remove the nth mesh
  virtual bool Remove (int n) = 0;

  /// Remove all meshes
  virtual void RemoveAll () = 0;

  /// Find a mesh and return its index
  virtual int Find (iMeshWrapper *obj) const = 0;

  /**
   * Find a mesh by name. If there is a colon in the name
   * then this function is able to search for children too.
   * i.e. like mesh:childmesh:childmesh.
   */
  virtual iMeshWrapper *FindByName (const char *Name) const = 0;
};

/**
 * A list of mesh factories.
 *
 * Main ways to get pointers to this interface:
 *   - iEngine::GetMeshFactories()
 *   - iMeshFactoryWrapper::GetChildren()
 *
 * Main users of this interface:
 *   - iEngine
 */
struct iMeshFactoryList : public virtual iBase
{
  SCF_INTERFACE(iMeshFactoryList,2,0,0);
  /// Return the number of mesh factory wrappers in this list.
  virtual int GetCount () const = 0;

  /// Return a mesh factory wrapper by index.
  virtual iMeshFactoryWrapper *Get (int n) const = 0;

  /// Add a mesh factory wrapper.
  virtual int Add (iMeshFactoryWrapper *obj) = 0;

  /// Remove a mesh factory wrapper.
  virtual bool Remove (iMeshFactoryWrapper *obj) = 0;

  /// Remove the nth mesh factory wrapper.
  virtual bool Remove (int n) = 0;

  /// Remove all mesh factory wrappers.
  virtual void RemoveAll () = 0;

  /// Find a mesh factory wrapper and return its index.
  virtual int Find (iMeshFactoryWrapper *obj) const = 0;

  /// Find a mesh factory wrapper by name.
  virtual iMeshFactoryWrapper *FindByName (const char *Name) const = 0;
};

/**
 * This is an iterator mesh factory wrappers.
 *
 * Main ways to get pointers to this interface:
 *   - iEngine::GetNearbyMeshes()
 *   - iEngine::GetVisibleMeshes()
 */
struct iMeshFactoryWrapperIterator : public virtual iBase
{
  SCF_INTERFACE(iMeshFactoryWrapperIterator,1,0,0);
  /// Move forward.
  virtual iMeshFactoryWrapper* Next () = 0;

  /// Reset the iterator to the beginning.
  virtual void Reset () = 0;

  /// Check if we have any more meshes.
  virtual bool HasNext () const = 0;
};

/**
 * This is an iterator of mesh wrappers.
 *
 * Main ways to get pointers to this interface:
 *   - iEngine::GetNearbyMeshes()
 *   - iEngine::GetVisibleMeshes()
 */
struct iMeshWrapperIterator : public virtual iBase
{
  SCF_INTERFACE(iMeshWrapperIterator,2,0,0);
  /// Move forward.
  virtual iMeshWrapper* Next () = 0;

  /// Reset the iterator to the beginning.
  virtual void Reset () = 0;

  /// Check if we have any more meshes.
  virtual bool HasNext () const = 0;
};


/** @} */

#endif // __CS_IENGINE_MESH_H__

