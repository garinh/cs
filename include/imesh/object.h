/*
    Copyright (C) 2000-2003 by Jorrit Tyberghein

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

#ifndef __CS_IMESH_OBJECT_H__
#define __CS_IMESH_OBJECT_H__

#include "csutil/scf.h"

/**\file
 * Mesh object and mesh object factory interfaces
 */

/**
 * \addtogroup meshplugins
 * @{ */

struct iDecalBuilder;
struct iLight;
struct iMaterialWrapper;
struct iMeshWrapper;
struct iMeshFactoryWrapper;
struct iMeshObject;
struct iMeshObjectFactory;
struct iMeshObjectType;
struct iMovable;
struct iObjectModel;
struct iPortal;
struct iRenderView;

namespace CS
{
  namespace Graphics
  {
    struct RenderMesh;
  }
}

class csColor;
class csFlags;
class csReversibleTransform;
class csVector3;

/** \name Mesh object flags
 * @{ */
/**
 * If CS_MESH_STATICPOS is set then this mesh will never move.
 * This is a hint for the engine. The mesh object itself can also
 * use this flag to optimize internal data structures.
 */
#define CS_MESH_STATICPOS 1

/**
 * If CS_MESH_STATICSHAPE is set then this mesh will never animate.
 * This is a hint for the engine. The mesh object itself can also
 * use this flag to optimize internal data structures.
 */
#define CS_MESH_STATICSHAPE 2
/** @} */

/** \name Mesh factory flags
 * @{ */
/**
 * If CS_FACTORY_STATICSHAPE is set then this factory will never animate.
 * This is a hint for the engine. The mesh factory itself can also
 * use this flag to optimize internal data structures.
 */
#define CS_FACTORY_STATICSHAPE 2
/** @} */

/**
 * Set a callback which is called just before the object is drawn.
 */
struct iMeshObjectDrawCallback : virtual public iBase
{
  SCF_INTERFACE (iMeshObjectDrawCallback, 0, 0, 1);

  /// Before drawing.
  virtual bool BeforeDrawing (iMeshObject* spr, iRenderView* rview) = 0;
};


/**
 * This is a general mesh object that the engine can interact with. The mesh
 * object only manages its shape, texture etc. but *not* its position, sector
 * or similar information. For this reason, a mesh object can only be used
 * in the engine if a hook object is created for it in the engine that does
 * the required management. The hook object is called mesh wrapper.
 * 
 * Main creators of instances implementing this interface:
 * - All mesh objects implement this.
 * - iMeshObjectFactory::NewInstance()
 *   
 * Main ways to get pointers to this interface:
 * - iMeshWrapper::GetMeshObject()
 *   
 * Main users of this interface:
 * - The 3D engine plugin (crystalspace.engine.3d).
 *   
 */
struct iMeshObject : public virtual iBase
{
  SCF_INTERFACE(iMeshObject, 3,1,0);
  /**
   * Get the reference to the factory that created this mesh object.
   */
  virtual iMeshObjectFactory* GetFactory () const = 0;

  /**
   * Get flags for this object. The following flags are at least supported:
   * \li #CS_MESH_STATICPOS: mesh will never move.
   * \li #CS_MESH_STATICSHAPE: mesh will never animate.
   * 
   * Mesh objects may implement additional flags. These mesh object specific
   * flags must be equal to at least 0x00010000.
   */
  virtual csFlags& GetFlags () = 0;

  /**
   * Creates a copy of this object and returns the clone.
   */
  virtual csPtr<iMeshObject> Clone () = 0;

  /**
   * Returns the set of render meshes.
   * The frustum_mask is given by the culler and contains a mask with
   * all relevant planes for the given object. These planes correspond
   * with the clip planes kept by iRenderView.
   */
  virtual CS::Graphics::RenderMesh** GetRenderMeshes (int& num, iRenderView* rview, 
    iMovable* movable, uint32 frustum_mask) = 0;

  /**
   * Register a callback to the mesh object which will be called
   * from within Draw() if the mesh object thinks that the object is
   * really visible. Depending on the type of mesh object this can be
   * very accurate or not accurate at all. But in all cases it will
   * certainly be called if the object is visible.
   */
  virtual void SetVisibleCallback (iMeshObjectDrawCallback* cb) = 0;

  /**
   * Get the current visible callback.
   */
  virtual iMeshObjectDrawCallback* GetVisibleCallback () const = 0;

  /**
   * Control animation of this object.
   */
  virtual void NextFrame (csTicks current_time,const csVector3& pos,
    uint currentFrame) = 0;

  /**
   * Do a hard transform of this object.
   * This transformation and the original coordinates are not
   * remembered but the object space coordinates are directly
   * computed (world space coordinates are set to the object space
   * coordinates by this routine). Note that some implementations
   * of mesh objects will not change the orientation of the object but
   * only the position.
   */
  virtual void HardTransform (const csReversibleTransform& t) = 0;

  /**
   * Return true if HardTransform is supported for this mesh object type.
   */
  virtual bool SupportsHardTransform () const = 0;

  /**
   * Check if this mesh is hit by this object space vector.
   * This will do a test based on the outline of the object. This means
   * that it is more accurate than HitBeamBBox(). Note that this routine
   * will typically be faster than HitBeamObject(). The hit may be on the front
   * or the back of the object, but will indicate that it iterrupts the beam.
   */
  virtual bool HitBeamOutline (const csVector3& start,
  	const csVector3& end, csVector3& isect, float* pr) = 0;

  /**
   * Check if this mesh is hit by this object space vector.
   * Return the collision point in object space coordinates.
   * This is the most detailed version (and also the slowest). The
   * returned hit will be guaranteed to be the point closest to the
   * 'start' of the beam. If the object supports this then an index
   * of the hit polygon will be returned (or -1 if not supported or no hit).
   * \param start Start of the beam to trace.
   * \param end End of the beam to trace.
   * \param isect Returns the point of the hit.
   * \param pr Returns the position of the hit as a value between 0 and 1,
   *   where 0 means \a start and 1 means \a end.
   * \param polygon_idx Index of the polygon hit on the mesh (or -1 if not
   *   supported).
   * \param material If not 0 then the hit material will be put here. Or 0
   *   in case this mesh object doesn't support material selection.
   */
  virtual bool HitBeamObject (const csVector3& start, const csVector3& end,
  	csVector3& isect, float* pr, int* polygon_idx = 0,
	iMaterialWrapper** material = 0, csArray<iMaterialWrapper*>* materials = 0) = 0;

  /**
   * Set a reference to the mesh wrapper holding the mesh objects.
   * Note that this function should NOT increase the ref-count of the
   * given logical parent because this would cause a circular reference
   * (since the logical parent already holds a reference to this mesh object).
   */
  virtual void SetMeshWrapper (iMeshWrapper* logparent) = 0;

  /**
   * Get the logical parent for this mesh object. See SetMeshWrapper()
   * for more information.
   */
  virtual iMeshWrapper* GetMeshWrapper () const = 0;

  /**
   * Get the generic interface describing the geometry of this mesh.
   * If the factory supports this you should preferably use the object
   * model from the factory instead.
   */
  virtual iObjectModel* GetObjectModel () = 0;

  /**
   * Set the base color of the mesh. This color will be added to whatever
   * color is set for lighting. Not all meshes need to support this.
   * This function will return true if it worked.
   */
  virtual bool SetColor (const csColor& color) = 0;

  /**
   * Get the base color of the mesh. Will return false if not supported.
   */
  virtual bool GetColor (csColor& color) const = 0;

  /**
   * Set the material of the mesh. This only works for single-material
   * meshes. If not supported this function will return false.
   */
  virtual bool SetMaterialWrapper (iMaterialWrapper* material) = 0;

  /**
   * Get the material of the mesh. If not supported this will
   * return 0.
   */
  virtual iMaterialWrapper* GetMaterialWrapper () const = 0;

  /// Set mix mode. Note that not all meshes may support this.
  virtual void SetMixMode (uint mode) = 0;
  /// Get mix mode.
  virtual uint GetMixMode () const = 0;

  /**
   * The engine asks this mesh object to place one of his hierarchical
   * children. It must be placed where it should be at the given time.
   * This object might or might not have been drawn, so you can't use 
   * it's current state.
   */
  virtual void PositionChild (iMeshObject* child,csTicks current_time) = 0;

  /**
   * This mesh is being asked to build a decal for its own geometry.  The
   * mesh is given a position and radius of the decal and must create
   * geometry through the provided iDecalBuilder.
   */
  virtual void BuildDecal(const csVector3* pos, float decalRadius,
	iDecalBuilder* decalBuilder) = 0;
};

/**
 * This object is a factory which can generate
 * mesh objects of a certain type. For example, if you want to have
 * multiple sets of sprites from the same sprite template then
 * you should have an instance of iMeshObjectFactory for every sprite
 * template and an instance of iMeshObject for every sprite.
 * 
 * To use a mesh factory in the engine, you have to create a mesh factory
 * wrapper for it.
 * 
 * Main creators of instances implementing this interface:
 * - All mesh objects implement this.
 * - iMeshObjectType::NewFactory()
 *   
 * Main ways to get pointers to this interface:
 * - iMeshFactoryWrapper::GetMeshObjectFactory()
 * - iMeshObject::GetFactory()
 *   
 * Main users of this interface:
 * - The 3D engine plugin (crystalspace.engine.3d).
 *   
 */
struct iMeshObjectFactory : public virtual iBase
{
  SCF_INTERFACE(iMeshObjectFactory, 2, 0, 0);

  /**
   * Get flags for this factory. The following flags are at least supported:
   * - #CS_FACTORY_STATICSHAPE: factory will never animate.
   * 
   * Mesh factories may implement additional flags. These mesh factory specific
   * flags must be equal to at least 0x00010000.
   */
  virtual csFlags& GetFlags () = 0;

  /// Create an instance of iMeshObject.
  virtual csPtr<iMeshObject> NewInstance () = 0;

  /**
   * Creates a copy of this factory and returns the clone.
   */
  virtual csPtr<iMeshObjectFactory> Clone () = 0;

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
   * Return true if HardTransform is supported for this factory.
   */
  virtual bool SupportsHardTransform () const = 0;

  /**
   * Set a reference to the mesh factory wrapper parent.
   * Note that this function should NOT increase the
   * ref-count of the given logical parent because this would cause a
   * circular reference (since the logical parent already holds a reference
   * to this mesh factory).
   */
  virtual void SetMeshFactoryWrapper (iMeshFactoryWrapper* logparent) = 0;

  /**
   * Get the logical parent for this mesh factory. See SetMeshFactoryWrapper()
   * for more information.
   */
  virtual iMeshFactoryWrapper* GetMeshFactoryWrapper () const = 0;

  /**
   * Get the ObjectType for this mesh factory.
   */
  virtual iMeshObjectType* GetMeshObjectType () const = 0;

  /**
   * Get the generic interface describing the geometry of this mesh factory.
   * It is possible that this will return 0 if the factory itself doesn't
   * support the geometry. In that case you need to get the object model
   * from the individual instance instead. Note that for collision detection
   * and other parts of CS it is prefered to use the factory object model
   * when available as that reduces the amount of redundant memory usage.
   */
  virtual iObjectModel* GetObjectModel () = 0;

  /**
   * Set the material of the mesh. This only works for single-material
   * factories. If not supported this function will return false.
   */
  virtual bool SetMaterialWrapper (iMaterialWrapper* material) = 0;

  /**
   * Get the material of the factory. If not supported this will
   * return 0.
   */
  virtual iMaterialWrapper* GetMaterialWrapper () const = 0;

  /// Set mix mode. Note that not all meshes may support this.
  virtual void SetMixMode (uint mode) = 0;
  /// Get mix mode.
  virtual uint GetMixMode () const = 0;
};


/**
 * This plugin describes a specific type of mesh objects. Through
 * this plugin the user can create instances of mesh object factories
 * which can then be used to create instances of mesh objects.
 * 
 * Main creators of instances implementing this interface:
 * - All mesh object plugins implement this interface.
 *   
 * Main ways to get pointers to this interface:
 * - csQueryPluginClass()
 * - csLoadPlugin()
 *   
 * Main users of this interface:
 * - The 3D engine plugin (crystalspace.engine.3d).
 *   
 */
struct iMeshObjectType : public virtual iBase
{
  SCF_INTERFACE(iMeshObjectType, 2,0,0);
  /// Create an instance of iMeshObjectFactory.
  virtual csPtr<iMeshObjectFactory> NewFactory () = 0;
};

/** @} */

#endif // __CS_IMESH_OBJECT_H__
