/*
    Copyright (C) 1998-2006 by Jorrit Tyberghein
              (C) 2004-2008 by Marten Svanfeldt

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

#ifndef __CS_SECTOR_H__
#define __CS_SECTOR_H__

#include "csgeom/aabbtree.h"
#include "csgeom/math3d.h"
#include "cstool/rendermeshlist.h"
#include "csutil/scf_implementation.h"
#include "csutil/array.h"
#include "csutil/array.h"
#include "csutil/cscolor.h"
#include "csutil/csobject.h"
#include "csutil/hash.h"
#include "csutil/nobjvec.h"
#include "csutil/refarr.h"
#include "csutil/scf_implementation.h"
#include "csutil/threadmanager.h"
#include "iutil/selfdestruct.h"
#include "iengine/portalcontainer.h"
#include "iengine/sector.h"
#include "iengine/rview.h"
#include "iengine/viscull.h"
#include "ivideo/graph3d.h"
#include "ivideo/rndbuf.h"
#include "ivideo/shader/shader.h"

#include "plugins/engine/3d/light.h"
#include "plugins/engine/3d/meshobj.h"
#include "plugins/engine/3d/meshgen.h"

class csEngine;
class csProgressPulse;
class csSector;
class csMeshGenerator;
struct iVisibilityCuller;
struct iMeshWrapper;

CS_PLUGIN_NAMESPACE_BEGIN(Engine)
{
  class csMeshWrapper;
}
CS_PLUGIN_NAMESPACE_END(Engine)

/// A list of meshes for a sector.
class csSectorMeshList : public CS_PLUGIN_NAMESPACE_NAME(Engine)::csMeshList
{
public:
  /// constructor
  csSectorMeshList ();
  /// destructor
  virtual ~csSectorMeshList () { RemoveAll (); }
  /// Set the sector.
  void SetSector (csSector* sec) { sector = sec; }

  /// Override PrepareMesh
  virtual void PrepareMesh (iMeshWrapper* item);
  /// Override FreeMesh
  virtual void FreeMesh (iMeshWrapper* item);

private:
  csSector* sector;
};

CS_PLUGIN_NAMESPACE_BEGIN(Engine)
{
  struct LightExtraAABBNodeData
  {
    /// Light types flags
    enum
    {
      ltStatic = 1,
      ltDynamic = 2
    };
    uint32 lightTypes;
    
    static uint GetLightType (csLightDynamicType dynType)
    {
      if (dynType == CS_LIGHT_DYNAMICTYPE_DYNAMIC)
        return ltDynamic;
      else
        return ltStatic;
    }
    static uint GetLightType (csLight* light)
    {
      return GetLightType (light->csLight::GetDynamicType());
    }
    
    LightExtraAABBNodeData() : lightTypes (0) {}
  
    void LeafAddObject (csLight* light)
    {
      lightTypes |= GetLightType (light);
    }
    
    void LeafUpdateObjects (csLight** lights, uint numLights)
    {
      lightTypes = 0;
      for (uint i = 0; i < numLights; i++)
      {
        lightTypes |= GetLightType (lights[i]);
      }
    }
    
    void NodeUpdate (const LightExtraAABBNodeData& child1,
      const LightExtraAABBNodeData& child2)
    {
      lightTypes = child1.lightTypes | child2.lightTypes;
    }
  };
}
CS_PLUGIN_NAMESPACE_END(Engine)

/// A list of lights for a sector.
class csSectorLightList : public csLightList
{
public:
  typedef CS::Geometry::AABBTree<
    CS_PLUGIN_NAMESPACE_NAME(Engine)::csLight, 2,
    CS_PLUGIN_NAMESPACE_NAME(Engine)::LightExtraAABBNodeData>  LightAABBTree;
  /// constructor
  csSectorLightList (csSector* s);
  /// destructor
  virtual ~csSectorLightList ();

  /// Override PrepareLight
  virtual void PrepareLight (iLight* light);
  /// Override FreeLight
  virtual void FreeLight (iLight* item);

  /// Get the AABB-tree  for this light list.
  const LightAABBTree& GetLightAABBTree () const 
  { return lightTree; }

  void UpdateLightBounds (CS_PLUGIN_NAMESPACE_NAME(Engine)::csLight* light,
    const csBox3& oldBox);

private:
  csSector* sector;
  /**
   * AABB-tree with all lights in sector
   */
  LightAABBTree lightTree;
};

#include "csutil/deprecated_warn_off.h"

/**
 * A sector is a container for objects. It is one of
 * the base classes for the portal engine.
 */
class csSector : public scfImplementationExt3<csSector, 
                                              csObject,
                                              iSector,
					                                    iSelfDestruct,
                                              scfFakeInterface<iShaderVariableContext> >,
                 public CS::ShaderVariableContextImpl,
                 public ThreadedCallable<csSector>
{
  // Friends
  friend class csEngine;
  friend class CS_PLUGIN_NAMESPACE_NAME(Engine)::csMeshWrapper;
  friend class csSectorMeshList;

public:
  /**
   * Construct a sector. This sector will be completely empty.
   */
  csSector (csEngine*);

  /**
   * Set single mesh. This is used to render only a single mesh out
   * of the sector. Set to 0 to go back to default behaviour of drawing
   * all visible meshes.
   */
  void SetSingleMesh (iMeshWrapper* mesh) { single_mesh = mesh; }

  /**\name iSector 
   * @{ */
  virtual iObject *QueryObject ()
  { return this; }
  /** @} */

  /**\name Mesh handling
   * @{ */
  virtual iMeshList* GetMeshes ()
  { return &meshes; }

  virtual csRenderMeshList* GetVisibleMeshes (iRenderView *);

  virtual csSectorVisibleRenderMeshes* GetVisibleRenderMeshes (int& num,
    iMeshWrapper* mesh, iRenderView *rview, uint32 frustum_mask);

  virtual const csSet<csPtrKey<iMeshWrapper> >& GetPortalMeshes () const
  { return portalMeshes; }

  void RegisterPortalMesh (iMeshWrapper* mesh);
  void UnregisterPortalMesh (iMeshWrapper* mesh);

  virtual void UnlinkObjects ();

  virtual void AddSectorMeshCallback (iSectorMeshCallback* cb);
  virtual void RemoveSectorMeshCallback (iSectorMeshCallback* cb);
  /** @} */

  /**\name Drawing related
   * @{ */
  virtual void Draw (iRenderView* rview);

  virtual void PrepareDraw (iRenderView* rview);

  virtual int GetRecLevel () const
  { return drawBusy; }
  virtual void IncRecLevel ()
  { drawBusy++; }
  virtual void DecRecLevel ()
  { drawBusy--; }

  THREADED_CALLABLE_DECL1(csSector, SetRenderLoop, csThreadReturn,
    iRenderLoop*, rl, MED, false, false);

  virtual iRenderLoop* GetRenderLoop ()
  { return renderloop; }
  /** @} */

  /**\name Fog handling
   * @{ */
  virtual bool HasFog () const
  { return fog.mode != CS_FOG_MODE_NONE; }
  
  virtual const csFog& GetFog () const
  { return fog; }
  
  virtual void SetFog (float density, const csColor& color)
  {
    fog.mode = CS_FOG_MODE_CRYSTALSPACE;
    fog.density = density;
    fog.color.Set(color);
    UpdateFogSVs ();
  }
  virtual void SetFog (const csFog& fog)
  { 
    this->fog = fog; 
    UpdateFogSVs ();
  }

  virtual void DisableFog ()
  { 
    fog.mode = CS_FOG_MODE_NONE; 
    UpdateFogSVs ();
  }
  /** @} */

  /**\name Light handling
   * @{ */
  virtual iLightList* GetLights ()
  { return &lights; }

  THREADED_CALLABLE_DECL1(csSector, AddLight, csThreadReturn,
    csRef<iLight>, light, MED, false, false);

  virtual void SetDynamicAmbientLight (const csColor& color);

  virtual csColor GetDynamicAmbientLight () const
  { return dynamicAmbientLightColor;}

  virtual uint GetDynamicAmbientVersion () const
  { return dynamicAmbientLightVersion; }

  const csSectorLightList::LightAABBTree& GetLightAABBTree () const
  { return lights.GetLightAABBTree (); }
  /** @} */

  /**\name Visculling
   * @{ */
  virtual void CalculateSectorBBox (csBox3& bbox,
    bool do_meshes) const;

  virtual bool SetVisibilityCullerPlugin (const char* name,
  	iDocumentNode* culler_params = 0);

  virtual iVisibilityCuller* GetVisibilityCuller ();

  virtual csSectorHitBeamResult HitBeamPortals (const csVector3& start,
  	const csVector3& end);

  virtual csSectorHitBeamResult HitBeam (const csVector3& start,
  	const csVector3& end, bool accurate = false);

  virtual iSector* FollowSegment (csReversibleTransform& t,
    csVector3& new_position, bool& mirror, bool only_portals = false,
    iPortal** transversed_portals = 0, iMeshWrapper** portal_meshes = 0,
    int firstIndex = 0, int* lastIndex = 0);
  /** @} */

  /**\name Callbacks
   * @{ */
  THREADED_CALLABLE_DECL1(csSector, SetSectorCallback, csThreadReturn,
    csRef<iSectorCallback>, cb, MED, false, false)

  THREADED_CALLABLE_DECL1(csSector, RemoveSectorCallback, csThreadReturn,
    csRef<iSectorCallback>, cb, MED, false, false)

  virtual int GetSectorCallbackCount () const 
  { return (int) sectorCallbackList.GetSize (); }

  virtual iSectorCallback* GetSectorCallback (int idx) const
  { return sectorCallbackList.Get (idx); }

  virtual void CallSectorCallbacks (iRenderView* rview)
  {
    int numSectorCB = (int)sectorCallbackList.GetSize ();
    while (numSectorCB-- > 0)
    {
      iSectorCallback* cb = sectorCallbackList.Get (numSectorCB);
      cb->Traverse (this, rview);
    }
  }
  /** @} */

  /**\name Lighting
   * @{ */
  virtual void SetLightCulling (bool enable) {}
  virtual bool IsLightCullingEnabled () const { return false; }
  virtual void AddLightVisibleCallback (iLightVisibleCallback* cb) {}
  virtual void RemoveLightVisibleCallback (iLightVisibleCallback* cb) {}
  void FireLightVisibleCallbacks (iLight* light) {}

  void UpdateLightBounds (CS_PLUGIN_NAMESPACE_NAME(Engine)::csLight* light,
    const csBox3& oldBox);

  /** @} */

  /**\name Mesh generators
   * @{ */
  iMeshGenerator* CreateMeshGenerator (const char* name);
  size_t GetMeshGeneratorCount () const
  {
    return meshGenerators.GetSize ();
  }
  iMeshGenerator* GetMeshGenerator (size_t idx)
  {
    return (iMeshGenerator*)(csMeshGenerator*)meshGenerators[idx];
  }
  iMeshGenerator* GetMeshGeneratorByName (const char* name);
  void RemoveMeshGenerator (size_t idx);
  void RemoveMeshGenerator (const char* name);
  void RemoveMeshGenerators ();
  /** @} */

  /**\name iSelfDestruct implementation
   * @{ */
  virtual void SelfDestruct ();
  /** @} */

  virtual iShaderVariableContext* GetSVContext()
  { return static_cast<iShaderVariableContext*> (this); }

  virtual void PrecacheDraw ();

protected:
  virtual void InternalRemove() { SelfDestruct(); }

private:
  // -- PRIVATE METHODS

  /**
   * Destroy this sector. All things in this sector are also destroyed.
   * Meshes are unlinked from the sector but not removed because they
   * could be in other sectors.
   */
  virtual ~csSector ();

  /**
   * Register a mesh and all children to the visibility culler.
   */
  void RegisterEntireMeshToCuller (iMeshWrapper* mesh);

  /**
   * Register a mesh (without children) to the visibility culler.
   */
  void RegisterMeshToCuller (iMeshWrapper* mesh);

  /**
   * Unregister a mesh (without children) from the visibility culler.
   */
  void UnregisterMeshToCuller (iMeshWrapper* mesh);

  /**
   * Prepare a mesh for rendering. This function is called for all meshes that
   * are added to the sector.
   */
  void PrepareMesh (iMeshWrapper* mesh);

  /**
   * Unprepare a mesh. This function is called for all meshes that
   * are removed from the sector.
   */
  void UnprepareMesh (iMeshWrapper* mesh);

  /**
   * Relink a mesh from this sector. This is mainly useful if
   * characterics of the mesh changed (like render priority) so
   * that the sector needs to know this.
   */
  void RelinkMesh (iMeshWrapper* mesh);

  /**
   * Intersect world-space segment with polygons of this sector. Return
   * polygon it intersects with (or 0) and the intersection point
   * in world coordinates.<p>
   *
   * If 'pr' != 0 it will also return a value between 0 and 1
   * indicating where on the 'start'-'end' vector the intersection
   * happened.<p>
   *
   * If 'only_portals' == true only portals are checked.<p>
   *
   * If 'mesh' != 0 the mesh will be filled in.
   */
  int IntersectSegment (const csVector3& start,
	const csVector3& end, csVector3& isect,
	float* pr = 0, bool only_portals = false,
	iMeshWrapper** p_mesh = 0);

  void FireNewMesh (iMeshWrapper* mesh);
  void FireRemoveMesh (iMeshWrapper* mesh);

  /// Update shader vars with fog information
  void UpdateFogSVs ();

  void SetupSVNames();
private:
  // PRIVATE MEMBERS

  /**
   * List of meshes in this sector. Note that meshes also
   * need to be in the engine list. This vector contains objects
   * of type iMeshWrapper*.
   */
  csSectorMeshList meshes;

  /**
   * List of camera meshes (meshes with CS_ENTITY_CAMERA flag set).
   */
  csArray<iMeshWrapper*> cameraMeshes;

  /**
   * List of meshes that have portals that leave from this sector.
   */
  csSet<csPtrKey<iMeshWrapper> > portalMeshes;

  /**
   * Mesh generators.
   */
  csRefArrayObject<csMeshGenerator> meshGenerators;

  /**
   * List of sector callbacks.
   */
  csRefArray<iSectorCallback> sectorCallbackList;

  /**
   * List of sector mesh callbacks.
   */
  csRefArray<iSectorMeshCallback> sectorMeshCallbackList;

  /**
   * List of light visible callbacks.
   */
  csRefArray<iLightVisibleCallback> lightVisibleCallbackList;

  /**
   * All lights in this sector.
   * This vector contains objects of type iLight*.
   */
  csSectorLightList lights;

  /**
   * This color stores the most recently set dynamic
   * ambient color.
   */
  csColor dynamicAmbientLightColor;
  uint dynamicAmbientLightVersion;

  /// Engine handle.
  csEngine* engine;

  /// Required by ThreadedCallable
  iObjectRegistry* GetObjectRegistry() const;

  /// Optional renderloop.
  iRenderLoop* renderloop;

  /// Fog information.
  csFog fog;

  /**
   * The visibility culler for this sector or 0 if none.
   * In future we should support more than one visibility culler probably.
   */
  csRef<iVisibilityCuller> culler;

  /// Caching of visible meshes
  struct visibleMeshCacheHolder
  {
    csRenderMeshList *meshList;

    // We consider visibility result to be the same if
    // the frame number and context id are the same.
    // The context_id is stored in csRenderContext and
    // is modified whenever a new csRenderContext is created.
    uint32 cachedFrameNumber;
    uint32 cached_context_id;

    visibleMeshCacheHolder() : meshList(0) {}
    ~visibleMeshCacheHolder()
    {
      //delete meshList;
    }
  };

  csArray<visibleMeshCacheHolder> visibleMeshCache;
  csPDelArray<csRenderMeshList> usedMeshLists;

  // These are used by GetVisibleRenderMeshes
  csSectorVisibleRenderMeshes oneVisibleMesh[2];
  csDirtyAccessArray<csSectorVisibleRenderMeshes> renderMeshesScratch;
  void MarkMeshAndChildrenVisible (iMeshWrapper* mesh, 
    iRenderView* rview, uint32 frustum_mask,
    bool doFade = false, float fade = 1.0f);
  void ObjectVisible (CS_PLUGIN_NAMESPACE_NAME(Engine)::csMeshWrapper* cmesh,
    iRenderView* rview, uint32 frustum_mask, bool doFade, float fade);

  /**
   * Visibilty number for last VisTest call
   */
  uint32 currentVisibilityNumber;

  /**
   * How many times are we busy drawing this sector (recursive).
   * This is an important variable as it indicates to
   * 'new_transformation' which set of camera vertices it should
   * use.
   */
  int drawBusy;

  /**
   * If this is not 0 then we're drawing only a specific mesh and
   * not all meshes.
   */
  iMeshWrapper* single_mesh;

  /// Shader variable names
  struct SVNamesHolder
  {
    CS::ShaderVarName dynamicAmbient;
    CS::ShaderVarName lightAmbient;
    CS::ShaderVarName fogColor;
    CS::ShaderVarName fogMode;
    CS::ShaderVarName fogFadeStart;
    CS::ShaderVarName fogFadeEnd;
    CS::ShaderVarName fogLimit;
    CS::ShaderVarName fogDensity;
  };
  CS_DECLARE_STATIC_CLASSVAR_REF(svNames, SVNames, SVNamesHolder);
  csRef<csShaderVariable> svDynamicAmbient;
  csRef<csShaderVariable> svLightAmbient;
  csRef<csShaderVariable> svFogColor;
  csRef<csShaderVariable> svFogMode;
  csRef<csShaderVariable> svFogFadeStart;
  csRef<csShaderVariable> svFogFadeEnd;
  csRef<csShaderVariable> svFogLimit;
  csRef<csShaderVariable> svFogDensity;
  
  class LightAmbientAccessor :
    public scfImplementation1<LightAmbientAccessor,
                              iShaderVariableAccessor>
  {
    csSector* sector;
  public:
    LightAmbientAccessor (csSector* sector) : scfImplementationType (this),
      sector (sector) {}
      
    void PreGetValue (csShaderVariable* sv);
  };
};

#include "csutil/deprecated_warn_on.h"

/// List of 3D engine sectors.
class csSectorList : public scfImplementation1<csSectorList, iSectorList>
{
private:
  csRefArrayObject<iSector> list;
  csHash<iSector*, csString> sectors_hash;
  mutable CS::Threading::ReadWriteMutex sectorLock;

  class NameChangeListener : public scfImplementation1<NameChangeListener,
  	iObjectNameChangeListener>
  {
  private:
    csWeakRef<csSectorList> list;

  public:
    NameChangeListener (csSectorList* list) : scfImplementationType (this),
  	  list (list)
    {
    }
    virtual ~NameChangeListener () { }

    virtual void NameChanged (iObject* obj, const char* oldname,
  	  const char* newname)
    {
      if (list)
        list->NameChanged (obj, oldname, newname);
    }
  };
  csRef<NameChangeListener> listener;

  csEngine* engine;
public:
  void NameChanged (iObject* object, const char* oldname,
  	const char* newname);

  /// constructor
  csSectorList (csEngine* engine);
  /// destructor
  virtual ~csSectorList ();

  /// Override FreeSector.
  virtual void FreeSector (iSector* item);

  virtual int GetCount () const;
  virtual iSector *Get (int n) const;
  virtual int Add (iSector *obj);
  void AddBatch (csRef<iSectorLoaderIterator> itr);
  virtual bool Remove (iSector *obj);
  virtual bool Remove (int n);
  virtual void RemoveAll ();
  virtual int Find (iSector *obj) const;
  virtual iSector *FindByName (const char *Name) const;
};

#endif // __CS_SECTOR_H__
