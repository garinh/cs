/*
    Copyright (C) 2000-2001 by Jorrit Tyberghein
    Copyright (C) 1999 by Andrew Zabolotny <bit@eltech.ru>

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

#ifndef __CS_IENGINE_LIGHT_H__
#define __CS_IENGINE_LIGHT_H__

/**\file
 * Light interfaces
 */
/**
 * \addtogroup engine3d_light
 * @{ */
 
#include "csutil/scf.h"

class csColor;
class csFlags;
class csVector3;
class csVector4;
class csBox3;

struct iLight;
struct iMovable;
struct iObject;
struct iSector;
struct iSceneNode;
struct iShaderVariableContext;

struct iBaseHalo;
struct iCrossHalo;
struct iNovaHalo;
struct iFlareHalo;

/** \name Light flags
 * @{ */
/// Indicates that a light should not cast shadows
#define CS_LIGHT_NOSHADOWS	0x00000001

/** 
 * If this flag is set, the halo for this light is active and is in the
 * engine's queue of active halos. When halo become inactive, this flag
 * is reset.
 */
#define CS_LIGHT_ACTIVEHALO	0x80000000
/** @} */

/** \name Light Dynamic Types
 * @{ */

enum csLightDynamicType
{
  /**
   * A fully static light. Unless you are using shaders/renderloop that treat
   * all lights as dynamic this light cannot move and cannot change color.
   * Shadows are accurate and precalculated (if you use lightmaps).
   */
  CS_LIGHT_DYNAMICTYPE_STATIC = 1, 
 
  /**
   * A pseudo-dynamic light. Unless you are using shaders/renderloop that treat
   * all lights as dynamic this light cannot move but it can change color.
   * Shadows are accurate and precalculated (if you use lightmaps).
   */
  CS_LIGHT_DYNAMICTYPE_PSEUDO = 2,

  /**
   * A fully dynamic light.
   * No shadows are calculated unless you use a shader/renderloop
   * that does that in hardware.
   */
  CS_LIGHT_DYNAMICTYPE_DYNAMIC = 3
};
/** @} */

/// Light level that is used when there is no light on the texture.
#define CS_DEFAULT_LIGHT_LEVEL 20
/// Light level that corresponds to a normally lit texture.
#define CS_NORMAL_LIGHT_LEVEL 128

/** \name Attenuation modes
 * Attenuation controls how the brightness of a light fades with distance.
 * Apart from the attenuation mode there are one to tree constants,
 * which change meaning depending on attenuation mode.
 * There are five attenuation formulas: (distance is distance between
 * point for which lighting is computed and the light)
 * - no attenuation = light * 1
 * - linear attenuation = light * (1 - distance / constant1)
 * - inverse attenuation = light / distance
 * - realistic attenuation = light / distance^2
 * - CLQ, Constant Linear Quadratic 
 *   = light / (constant1 + constant2*distance + constant3*distance^2)
 * @{ */
enum csLightAttenuationMode
{
  /// no attenuation: light * 1
  CS_ATTN_NONE = 0,
  /// linear attenuation: light * (1 - distance / constant1)
  CS_ATTN_LINEAR = 1,
  /// inverse attenuation: light / distance
  CS_ATTN_INVERSE = 2,
  /// realistic attenuation: light / distance^2
  CS_ATTN_REALISTIC = 3,
  /** 
   * CLQ, Constant Linear Quadratic: 
   * light / (constant1 + constant2*distance + constant3*distance^2)
   */
  CS_ATTN_CLQ = 4
};
/** @} */

/**
 * Type of lightsource. 
 * There are currently three types of lightsources:
 * - Point lights - have a position. Shines in all directions.
 * - Directional lights - have a direction and radius. Shines along it's
 *                        major axis. The direction is 0,0,1 in light space.
 * - Spot lights - have both position and direction. Shines with full
 *                      strength along major axis and out to the hotspot angle.
 *                      Between hotspot and outer angle it will falloff, outside
 *                      outer angle there shines no light. The direction is
 *                      0,0,1 in light space.
 */
enum csLightType
{
  /// Point light
  CS_LIGHT_POINTLIGHT,
  /// Directional light
  CS_LIGHT_DIRECTIONAL,
  /// Spot light
  CS_LIGHT_SPOTLIGHT
};

/**
 * Set a callback which is called when this light color is changed.
 * The given context will be either an instance of iRenderView
 * or else 0.
 *
 * This callback is used by:
 * - iLight
 */
struct iLightCallback : public virtual iBase
{
  SCF_INTERFACE(iLightCallback,2,0,0);
  /**
   * Light color will be changed. It is safe to delete this callback
   * in this function.
   */
  virtual void OnColorChange (iLight* light, const csColor& newcolor) = 0;

  /**
   * Light position will be changed. It is safe to delete this callback
   * in this function.
   */
  virtual void OnPositionChange (iLight* light, const csVector3& newpos) = 0;

  /**
   * Sector will be changed. It is safe to delete this callback
   * in this function.
   */
  virtual void OnSectorChange (iLight* light, iSector* newsector) = 0;

  /**
   * Radius will be changed.
   * It is safe to delete this callback in this function.
   */
  virtual void OnRadiusChange (iLight* light, float newradius) = 0;

  /**
   * Light will be destroyed.
   * It is safe to delete this callback in this function.
   */
  virtual void OnDestroy (iLight* light) = 0;

  /**
   * Attenuation will be changed.
   * It is safe to delete this callback in this function.
   */
  virtual void OnAttenuationChange (iLight* light, int newatt) = 0;
};


/**
 * The iLight interface is the SCF interface for the csLight class.
 * <p>
 * First some terminology about all the several types of lights
 * that Crystal Space supports:
 * - Static light. This is a normal static light that cannot move
 *   and cannot change intensity/color. All lighting information from
 *   all static lights is collected in one static lightmap.
 * - Pseudo-dynamic light. This is a static light that still cannot
 *   move but the intensity/color can change. The shadow information
 *   from every pseudo-dynamic light is kept in a separate shadow-map.
 *   Shadowing is very accurate with pseudo-dynamic lights since they
 *   use the same algorithm as static lights.
 * - Dynamic light. This is a light that can move and change
 *   intensity/color. These lights are the most flexible. All lighting
 *   information from all dynamic lights is collected in one dynamic
 *   lightmap (separate from the pseudo-dynamic shadow-maps).
 *   Shadows for dynamic lights will be less accurate because things
 *   will not cast accurate shadows (due to computation speed limitations).
 *
 * Main creators of instances implementing this interface:
 * - iEngine::CreateLight()
 *
 * Main ways to get pointers to this interface:
 * - iEngine::FindLight()
 * - iEngine::FindLightID()
 * - iEngine::GetLightIterator()
 * - iEngine::GetNearbyLights()
 * - iLightList::Get()
 * - iLightList::FindByName()
 * - iLoaderContext::FindLight()
 *
 * Main users of this interface:
 * - iEngine
 */
struct iLight : public virtual iBase
{
  SCF_INTERFACE(iLight,3,0,1);
  /// Get the id of this light. This is a 16-byte MD5.
  virtual const char* GetLightID () = 0;

  /// Get the iObject for this light.
  virtual iObject *QueryObject() = 0;

  /**
   * Get the dynamic type of this light.
   * Supported types:
   * - #CS_LIGHT_DYNAMICTYPE_STATIC
   * - #CS_LIGHT_DYNAMICTYPE_PSEUDO
   * - #CS_LIGHT_DYNAMICTYPE_DYNAMIC
   */
  virtual csLightDynamicType GetDynamicType () const = 0;

  /**
   * Get the position of this light (local transformation relative
   * to whatever parent it has).
   * \deprecate Deprecated in RM. Use GetMovable() and the iMovable interface
   */
  CS_DEPRECATED_METHOD_MSG("Deprecated. Use GetMovable() and the iMovable interface.")
  virtual const csVector3& GetCenter () const = 0;
  /**
   * Get the position of this light. This function correctly takes
   * care of the optional parents of this light.
   * \deprecate Deprecated in RM. Use GetMovable() and the iMovable interface
   */
  CS_DEPRECATED_METHOD_MSG("Deprecated. Use GetMovable() and the iMovable interface.")
  virtual const csVector3 GetFullCenter () const = 0;
  /**
   * Set the position of this light.
   * \deprecate Deprecated in RM. Use GetMovable() and the iMovable interface
   */ 
  CS_DEPRECATED_METHOD_MSG("Deprecated. Use GetMovable() and the iMovable interface.")
  virtual void SetCenter (const csVector3& pos) = 0;

  /**
   * Get the sector for this light.
   * \deprecate Deprecated in RM. Use GetMovable() and the iMovable interface
   */
  CS_DEPRECATED_METHOD_MSG("Deprecated. Use GetMovable() and the iMovable interface.")
  virtual iSector *GetSector () = 0;

  /**
   * Get the movable for this light ('this' space = light space,
   * 'other' space = world space).
   * The rotation of the movable determines the direction for directional and 
   * spot lights. Lights shine along +Z in light space; thus the direction in
   * world space can be computed by translating the direction (0,0,1) from\
   * the movable's 'this' to 'other' space.
   */
  virtual iMovable *GetMovable () = 0;

  /**
   * Get the scene node that this object represents.
   */
  virtual iSceneNode* QuerySceneNode () = 0;

  /// Get the diffuse color of this light.
  virtual const csColor& GetColor () const = 0;
  /// Set the diffuse color of this light.
  virtual void SetColor (const csColor& col) = 0;

  /// Get the specular color of this light.
  virtual const csColor& GetSpecularColor () const = 0;
  /// Set the specular color of this light.
  virtual void SetSpecularColor (const csColor& col) = 0;
  
  /// Get the light type of this light.
  virtual csLightType GetType () const = 0;
  /// Set the light type of this light.
  virtual void SetType (csLightType type) = 0;

  /**
   * Return current attenuation mode.
   * \sa csLightAttenuationMode
   */
  virtual csLightAttenuationMode GetAttenuationMode () const = 0;
  /**
   * Set attenuation mode.
   * \sa csLightAttenuationMode
   */
  virtual void SetAttenuationMode (csLightAttenuationMode a) = 0;

  /**
   * Set attenuation constants
   * \sa csLightAttenuationMode
   */
  virtual void SetAttenuationConstants (const csVector4& constants) = 0;
  /**
   * Get attenuation constants
   * \sa csLightAttenuationMode
   */
  virtual const csVector4 &GetAttenuationConstants () const = 0;

  /**
   * Get the the maximum distance at which the light is guaranteed to shine. 
   * Can be seen as the distance at which we turn the light off.
   * Used for culling and selection of meshes to light, but not
   * for the lighting itself.
   */
  virtual float GetCutoffDistance () const = 0;

  /**
   * Set the the maximum distance at which the light is guaranteed to shine. 
   * Can be seen as the distance at which we turn the light off.
   * Used for culling and selection of meshes to light, but not
   * for the lighting itself.
   */
  virtual void SetCutoffDistance (float distance) = 0;

  /**
   * Get radial cutoff distance for directional lights.
   * The directional light can be viewed as a cylinder with radius
   * equal to DirectionalCutoffRadius and length CutoffDistance
   */
  virtual float GetDirectionalCutoffRadius () const = 0;

  /**
   * Set radial cutoff distance for directional lights.
   * The directional light can be viewed as a cylinder with radius
   * equal to DirectionalCutoffRadius and length CutoffDistance
   */
  virtual void SetDirectionalCutoffRadius (float radius) = 0;

  /**
   * Set spot light falloff angles. Set in cosine of the angle. 
   */
  virtual void SetSpotLightFalloff (float inner, float outer) = 0;

  /**
   * Get spot light falloff angles. Get in cosine of the angle.
   */
  virtual void GetSpotLightFalloff (float& inner, float& outer) const = 0;

  /// Create a cross halo for this light.
  virtual iCrossHalo* CreateCrossHalo (float intensity, float cross) = 0;
  /// Create a nova halo for this light.
  virtual iNovaHalo* CreateNovaHalo (int seed, int num_spokes,
  	float roundness) = 0;
  /// Create a flare halo for this light.
  virtual iFlareHalo* CreateFlareHalo () = 0;

  /// Return the associated halo
  virtual iBaseHalo* GetHalo () const = 0;

  /// Get the brightness of a light at a given distance.
  virtual float GetBrightnessAtDistance (float d) const = 0;

  /**
   * Get flags for this light.
   * Supported flags:
   * - #CS_LIGHT_ACTIVEHALO
   */
  virtual csFlags& GetFlags () = 0;

  /**
   * Set the light callback. This will call IncRef() on the callback
   * So make sure you call DecRef() to release your own reference.
   */
  virtual void SetLightCallback (iLightCallback* cb) = 0;

  /**
   * Remove a light callback.
   */
  virtual void RemoveLightCallback (iLightCallback* cb) = 0;

  /// Get the number of light callbacks.
  virtual int GetLightCallbackCount () const = 0;
  
  /// Get the specified light callback.
  virtual iLightCallback* GetLightCallback (int idx) const = 0;

  /**
   * Return a number that changes when the light changes (color,
   * or position).
   */
  virtual uint32 GetLightNumber () const = 0;

  /**
   * Get the shader variable context of the light.
   */
  virtual iShaderVariableContext* GetSVContext() = 0;

  //@{
  /**
   * Get the bounding box of the light (the bounds define the influence area).
   */
  virtual const csBox3& GetLocalBBox () const = 0;
  virtual const csBox3& GetWorldBBox () const = 0;
  //@}
};

/**
 * This structure represents a list of lights.
 *
 * Main ways to get pointers to this interface:
 * - iSector::GetLights()
 *
 * Main users of this interface:
 * - iEngine
 */
struct iLightList : public virtual iBase
{
  SCF_INTERFACE(iLightList,2,0,0);
  /// Return the number of lights in this list.
  virtual int GetCount () const = 0;

  /// Return a light by index.
  virtual iLight *Get (int n) const = 0;

  /// Add a light.
  virtual int Add (iLight *obj) = 0;

  /// Remove a light.
  virtual bool Remove (iLight *obj) = 0;

  /// Remove the nth light.
  virtual bool Remove (int n) = 0;

  /// Remove all lights.
  virtual void RemoveAll () = 0;

  /// Find a light and return its index.
  virtual int Find (iLight *obj) const = 0;

  /// Find a light by name.
  virtual iLight *FindByName (const char *Name) const = 0;

  /// Find a light by its ID value (16-byte MD5).
  virtual iLight *FindByID (const char* id) const = 0;
};

/**
 * Iterator to iterate over all static lights in the engine.
 * This iterator assumes there are no fundamental changes
 * in the engine while it is being used.
 * If changes to the engine happen the results are unpredictable.
 *
 * Main creators of instances implementing this interface:
 * - iEngine::GetLightIterator()
 *
 * Main users of this interface:
 * - Application.
 */
struct iLightIterator : public virtual iBase
{
  SCF_INTERFACE (iLightIterator, 0, 1, 0);

  /// Return true if there are more elements.
  virtual bool HasNext () = 0;

  /// Get light from iterator. Return 0 at end.
  virtual iLight* Next () = 0;

  /// Get the sector for the last fetched light.
  virtual iSector* GetLastSector () = 0;

  /// Restart iterator.
  virtual void Reset () = 0;

};

/** @} */

#endif // __CS_IENGINE_LIGHT_H__

