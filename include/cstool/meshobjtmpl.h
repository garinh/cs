/*
    Copyright (C) 2003 by Martin Geisse <mgeisse@gmx.net>

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

#ifndef __CS_MESHOBJTMPL_H__
#define __CS_MESHOBJTMPL_H__

/**\file
 * Base implementation of iMeshObject.
 */

#include "csextern.h"

#include "csgeom/box.h"
#include "cstool/objmodel.h"
#include "csutil/flags.h"
#include "csutil/refarr.h"
#include "csutil/scf_implementation.h"

#include "ivideo/graph3d.h"
#include "imesh/object.h"
#include "iutil/comp.h"

struct iEngine;
struct iMaterialWrapper;

//Deprecate these when possible!
/// Declare a simple mesh factory class
#define CS_DECLARE_SIMPLE_MESH_FACTORY(name,meshclass)                      \
  class name : public csMeshFactory {                                       \
  public:                                                                   \
    name (iEngine *e, iObjectRegistry* reg, iMeshObjectType* type)          \
    : csMeshFactory (e, reg, type) {}                                       \
    virtual csPtr<iMeshObject> NewInstance ()                               \
    { return new meshclass (Engine, this); }                                \
    virtual csPtr<iMeshObjectFactory> Clone () { return 0; }                \
  };

/// Declare a simple mesh type plugin
#define CS_DECLARE_SIMPLE_MESH_PLUGIN(name,factclass)                       \
  class name : public csMeshType {                                          \
  public:                                                                   \
    name (iBase *p) : csMeshType (p) {}                                     \
    virtual csPtr<iMeshObjectFactory> NewFactory ()                         \
    { return new factclass (Engine, object_reg, this); }                    \
  };

/**
 * This is an abstract implementation of iMeshObject. It can be used to
 * write custom mesh object implementations more easily. Currently it
 * supports the following common functions of mesh objects:
 * - Implementation of iMeshObject
 * - Implementation of iObjectModel
 * - Storing a "visible callback"
 * - Storing a logical parent
 * - Storing object model properties
 * - Default implementation of most methods
 */
class CS_CRYSTALSPACE_EXPORT csMeshObject : 
  public scfImplementationExt1<csMeshObject, csObjectModel, iMeshObject>
{
protected:
  /// the drawing callback
  csRef<iMeshObjectDrawCallback> VisCallback;

  /// logical parent (usually the wrapper object from the engine)
  iMeshWrapper *LogParent;

  /// pointer to the engine if available.
  iEngine *Engine;

  /// Tell the engine that this object wants to be deleted
  void WantToDie ();

  /// Flags.
  csFlags flags;

  /// The bounding box.
  csBox3 boundingbox;

public:

  /// Constructor
  csMeshObject (iEngine *engine);

  /// Destructor
  virtual ~csMeshObject ();

  /**
   * See imesh/object.h for specification. There is no default
   * implementation for this method.
   */
  virtual iMeshObjectFactory* GetFactory () const = 0;

  /**
   * See imesh/object.h for specification. The default implementation
   * does nothing and returns 0.
   */
  virtual csPtr<iMeshObject> Clone () { return 0; }
  
  /**
   * See imesh/object.h for specification.
   */
  virtual csFlags& GetFlags () { return flags; }

  /**
   * See imesh/object.h for specification. The default implementation
   * does nothing and always returns 0.
   */
  virtual CS::Graphics::RenderMesh** GetRenderMeshes (int& num, iRenderView*, iMovable*,
  	uint32)
  {
    num = 0;
    return 0;
  }

  /**
   * See imesh/object.h for specification. This function is handled
   * completely in csMeshObject. The actual implementation just has
   * to use the VisCallback variable to perform the callback.
   */
  virtual void SetVisibleCallback (iMeshObjectDrawCallback* cb);

  /**
   * See imesh/object.h for specification. This function is handled
   * completely in csMeshObject.
   */
  virtual iMeshObjectDrawCallback* GetVisibleCallback () const;

  /**
   * See imesh/object.h for specification. The default implementation
   * does nothing.
   */
  virtual void NextFrame (csTicks current_time,const csVector3& pos,
    uint currentFrame);

  /**
   * See imesh/object.h for specification. The default implementation
   * does nothing.
   */
  virtual void HardTransform (const csReversibleTransform& t);

  /**
   * See imesh/object.h for specification. The default implementation
   * returns false.
   */
  virtual bool SupportsHardTransform () const;

  /**
   * See imesh/object.h for specification. The default implementation
   * will always return a miss.
   */
  virtual bool HitBeamOutline (const csVector3& start,
  	const csVector3& end, csVector3& isect, float* pr);

  /**
   * See imesh/object.h for specification. The default implementation
   * will always return a miss.
   */
  virtual bool HitBeamObject (const csVector3& start, const csVector3& end,
  	csVector3& isect, float* pr, int* polygon_idx = 0,
	iMaterialWrapper** = 0);

  /**
   * See imesh/object.h for specification. This function is handled
   * completely in csMeshObject.
   */
  virtual void SetMeshWrapper (iMeshWrapper* logparent);

  /**
   * See imesh/object.h for specification. This function is handled
   * completely in csMeshObject.
   */
  virtual iMeshWrapper* GetMeshWrapper () const;

  /**
   * See imesh/object.h for specification.
   */
  virtual iObjectModel* GetObjectModel () { return this; }

  /**
   * See imesh/object.h for specification. The default implementation
   * does not support a base color.
   */
  virtual bool SetColor (const csColor& color);

  /**
   * See imesh/object.h for specification. The default implementation
   * does not support a base color.
   */
  virtual bool GetColor (csColor& color) const;

  /**
   * See imesh/object.h for specification. The default implementation
   * does not support a material.
   */
  virtual bool SetMaterialWrapper (iMaterialWrapper* material);

  /**
   * See imesh/object.h for specification. The default implementation
   * does not support a material.
   */
  virtual iMaterialWrapper* GetMaterialWrapper () const;

  /// Set mix mode. Default implementation doesn't do anything.
  virtual void SetMixMode (uint) { }
  /// Get mix mode.
  virtual uint GetMixMode () const { return CS_FX_COPY; }

  /**
   * see imesh/object.h for specification. The default implementation
   * does nothing.
   */
  virtual void PositionChild (iMeshObject* /*child*/,csTicks /*current_time*/) { }

  /**
   * see imesh/object.h for specification.  The default implementation
   * does nothing.
   */
  virtual void BuildDecal(const csVector3* pos, float decalRadius,
	iDecalBuilder* decalBuilder)
  {
  }

  /**
   * See imesh/objmodel.h for specification. The default implementation
   * returns an infinite bounding box.
   */
  virtual const csBox3& GetObjectBoundingBox ();

  /**
   * See imesh/objmodel.h for specification. Overrides the
   * default bounding box.
   */
  virtual void SetObjectBoundingBox (const csBox3& bbox);

  /**
   * See imesh/objmodel.h for specification. The default implementation
   * returns an infinite radius.
   */
  virtual void GetRadius (float& radius, csVector3& center);

  /**
   * See imesh/objmodel.h for specification. The default implementation
   * returns 0.
   */
  virtual iTerraFormer* GetTerraFormerColldet () { return 0; }

  virtual iTerrainSystem* GetTerrainColldet () { return 0; }
};

/**
 * This is the abstract implementation of iMeshObjectFactory. Like
 * csMeshObject, it stores a pointer to the "logical parent".
 */
class CS_CRYSTALSPACE_EXPORT csMeshFactory : 
  public scfImplementation1<csMeshFactory, iMeshObjectFactory>
{
protected:
  /// Logical parent (usually the wrapper object from the engine)
  iMeshFactoryWrapper *LogParent;

  /// Pointer to the MeshObjectType
  iMeshObjectType* mesh_type;

  /// Pointer to the engine if available.
  iEngine *Engine;

  /// Object registry.
  iObjectRegistry* object_reg;

  /// Flags.
  csFlags flags;

public:

  /// Constructor
  csMeshFactory (iEngine *engine, iObjectRegistry* object_reg,
    iMeshObjectType* parent);

  /// Get the object registry.
  iObjectRegistry* GetObjectRegistry () { return object_reg; }

  /// destructor
  virtual ~csMeshFactory ();

  /**
   * See imesh/object.h for specification.
   */
  virtual csFlags& GetFlags () { return flags; }

  /**
   * See imesh/object.h for specification. There is no default
   * implementation for this method.
   */
  virtual csPtr<iMeshObject> NewInstance () = 0;

  /**
   * See imesh/object.h for specification. The default implementation
   * does nothing.
   */
  virtual void HardTransform (const csReversibleTransform& t);

  /**
   * See imesh/object.h for specification. The default implementation
   * returns false.
   */
  virtual bool SupportsHardTransform () const;

  /**
   * See imesh/object.h for specification. This function is handled
   * completely in csMeshObject.
   */
  virtual void SetMeshFactoryWrapper (iMeshFactoryWrapper* logparent);

  /**
   * See imesh/object.h for specification. This function is handled
   * completely in csMeshObject.
   */
  virtual iMeshFactoryWrapper* GetMeshFactoryWrapper () const;

  /**
   * Get the ObjectType for this mesh factory.
   */
  virtual iMeshObjectType* GetMeshObjectType () const;

  /**
   * See imesh/object.h for specification.
   */
  virtual iObjectModel* GetObjectModel () { return 0; }

  virtual bool SetMaterialWrapper (iMaterialWrapper*) { return false; }
  virtual iMaterialWrapper* GetMaterialWrapper () const { return 0; }
  virtual void SetMixMode (uint) { }
  virtual uint GetMixMode () const { return 0; }
};

/**
 * This is the abstract implementation of iMeshObjectType.
 */
class CS_CRYSTALSPACE_EXPORT csMeshType : 
  public scfImplementation2<csMeshType, iMeshObjectType, iComponent>
{
protected:
  /// pointer to the engine if available.
  iEngine *Engine;

  /// Object registry.
  iObjectRegistry* object_reg;

public:

  /// constructor
  csMeshType (iBase *p);

  /// destructor
  virtual ~csMeshType ();

  /**
   * Initialize this plugin.
   */
  bool Initialize (iObjectRegistry* reg);

  /**
   * See imesh/object.h for specification. There is no default
   * implementation for this method.
   */
  virtual csPtr<iMeshObjectFactory> NewFactory () = 0;

};

#endif // __CS_MESHOBJTMPL_H__
