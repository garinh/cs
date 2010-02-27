/*
    Copyright (C) 1998-2003 by Jorrit Tyberghein
    Written by Alex Pfaffe.

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
#ifndef __CS_COLLIDER_H__
#define __CS_COLLIDER_H__

/**\file
 * Collision helper.
 */

#include "csextern.h"

#include "csgeom/box.h"
#include "csutil/csobject.h"
#include "csutil/leakguard.h"
#include "csutil/array.h"
#include "csutil/scf_implementation.h"
#include "csutil/set.h"

#include "ivaria/collider.h"

struct iCamera;
struct iCollection;
struct iCollider;
struct iCollideSystem;
struct iEngine;
struct iMeshWrapper;
struct iMovable;
struct iObject;
struct iTriangleMesh;
struct iSector;
struct iTerrainSystem;

struct csCollisionPair;
class csReversibleTransform;

struct csIntersectingTriangle;

/**
 * This is a convenience object that you can use in your own
 * games to attach an iCollider object (from the CD plugin system)
 * to any other csObject (including CS entities). Use of this object
 * is optional (if you can assign your iCollider's to entities in
 * another manner then this is ok) and the engine will not use
 * this object itself.
 * <p>
 * Important! After creating an instance of this object you should
 * actually DecRef() the pointer because csColliderWrapper will automatically
 * attach itself to the given object. You can use
 * csColliderWrapper::GetCollider() later to get the collider again.
 */
class CS_CRYSTALSPACE_EXPORT csColliderWrapper :
  public scfImplementationExt1<csColliderWrapper,
                               csObject,
                               scfFakeInterface<csColliderWrapper> >
{
private:
  csRef<iCollideSystem> collide_system;
  csRef<iCollider> collider;

public:
  SCF_INTERFACE(csColliderWrapper, 2,2,0);

  CS_LEAKGUARD_DECLARE (csColliderWrapper);

  /**
   * Create a collider based on a mesh.
   */
  csColliderWrapper (csObject& parent, iCollideSystem* collide_system,
  	iTriangleMesh* mesh);

  /**
   * Create a collider based on a mesh.
   */
  csColliderWrapper (iObject* parent, iCollideSystem* collide_system,
  	iTriangleMesh* mesh);

  /// Create a collider based on a terrain.
  csColliderWrapper (iObject* parent, iCollideSystem* collide_system,
  	iTerraFormer* terrain);

  /// Create a collider based on a terrain.
  csColliderWrapper (iObject* parent, iCollideSystem* collide_system,
  	iTerrainSystem* terrain);

  /**
   * Create a collider based on a collider. Note that it is legal to pass
   * in a 0 collider. In that case it indicates that this object has no
   * collider.
   */
  csColliderWrapper (iObject* parent, iCollideSystem* collide_system,
  	iCollider* collider);

  /// Destroy the plugin collider object
  virtual ~csColliderWrapper ();

  /// Get the collider interface for this object.
  iCollider* GetCollider () { return collider; }

  /// Get the collide system.
  iCollideSystem* GetCollideSystem () { return collide_system; }

  /**
   * Check if this collider collides with pOtherCollider.
   * Returns true if collision detected and adds the pair to the collisions
   * hists vector.
   * This collider and pOtherCollider must be of comparable subclasses, if
   * not false is returned.
   */
  bool Collide (csColliderWrapper& pOtherCollider,
                csReversibleTransform* pThisTransform = 0,
                csReversibleTransform* pOtherTransform = 0);
  /**
   * Similar to Collide for csColliderWrapper. Calls GetColliderWrapper for
   * otherCollider.
   */
  bool Collide (csObject& otherObject,
                csReversibleTransform* pThisTransform = 0,
                csReversibleTransform* pOtherTransform = 0);
  /**
   * Similar to Collide for csColliderWrapper. Calls GetColliderWrapper for
   * otherCollider.
   */
  bool Collide (iObject* otherObject,
                csReversibleTransform* pThisTransform = 0,
                csReversibleTransform* pOtherTransform = 0);

  /**
   * If object has a child of type csColliderWrapper it is returned.
   * Otherwise 0 is returned.
   */
  static csColliderWrapper* GetColliderWrapper (csObject& object);

  /**
   * If object has a child of type csColliderWrapper it is returned.
   * Otherwise 0 is returned.
   */
  static csColliderWrapper* GetColliderWrapper (iObject* object);

  /**
   * Update collider from a triangle mesh.
   */
  void UpdateCollider (iTriangleMesh* mesh);

  /// Update collider from a terraformer.
  void UpdateCollider (iTerraFormer* terrain);

};

/**
 * Return structure for the csColliderHelper::TraceBeam() method.
 */
struct CS_CRYSTALSPACE_EXPORT csTraceBeamResult
{
  /// Closest triangle from the model.
  /**
   * closest_tri will be set to the closest triangle that is hit. The
   * triangle will be specified in world space.
   */
  csIntersectingTriangle closest_tri;
  /**
   * closest_isect will be set to the closest intersection point (in
   * world space).
   */
  csVector3 closest_isect;
  /**
   * closest_mesh will be set to the closest mesh that is hit.
   */
  iMeshWrapper* closest_mesh;
  /**
   * The squared distance between 'start' and the closest hit
   * or else a negative number if there was no hit.
   */
  float sqdistance;
  /**
   * Sector in which the collision occured.
   */
  iSector* end_sector;
};

/**
 * This is a class containing a few static member functions to help
 * work with csColliderWrapper and collision detection in general.
 */
class CS_CRYSTALSPACE_EXPORT csColliderHelper
{
public:
  /**
   * Initialize collision detection for a single object. This function will
   * first check if the parent factory has a collider. If so it will use
   * that for the object too. Otherwise it will create a new collider
   * and attaches it to the object. The new collider will also be attached
   * to the parent factory if it supports iObjectModel.
   * <p>
   * This function will also initialize colliders for the children of the
   * mesh.
   * \return the created collider wrapper.
   */
  static csColliderWrapper* InitializeCollisionWrapper (iCollideSystem* colsys,
  	iMeshWrapper* mesh);

  /**
   * Initialize collision detection (i.e. create csColliderWrapper) for
   * all objects in the engine. If the optional region is given only
   * the objects from that region will be initialized.
   */
  static void InitializeCollisionWrappers (iCollideSystem* colsys,
      iEngine* engine, iCollection* collection = 0);

  /**
   * Test collision between one collider and an array of colliders.
   * This function is mainly used by CollidePath() below.
   * \param colsys is the collider system.
   * \param collider is the collider of the object that we are going to move
   *        along the path.
   * \param trans is the transform of that object (see Collide()).
   * \param num_colliders is the number of colliders that we are going to use
   *        to collide with.
   * \param colliders is an array of colliders. Typically you can obtain such a
   *        list by doing iEngine::GetNearbyMeshes() and then getting the
   *        colliders from all meshes you get (possibly using
   *        csColliderWrapper). Note that it is safe to have 'collider' sitting
   *        in this list. This function will ignore that collider.
   * \param transforms is an array of transforms that belong with the array of
   *        colliders.
   */
  static bool CollideArray (
	iCollideSystem* colsys,
	iCollider* collider,
	const csReversibleTransform* trans,
  	int num_colliders,
	iCollider** colliders,
	csReversibleTransform **transforms);

  /**
   * Test if an object can move to a new position. The new position
   * vector will be modified to reflect the maximum new position that the
   * object could move to without colliding with something. This function
   * will return:
   * - -1 if the object could not move at all (i.e. stuck at start position).
   * - 0 if the object could not move fully to the desired position.
   * - 1 if the object can move unhindered to the end position.
   *
   * <p>
   * This function will reset the collision pair array. If there was a
   * collision along the way the array will contain the information for
   * the first collision preventing movement.
   * 
   * The given transform should be the transform of the object corresponding
   * with the old position. 'colliders' and 'transforms' should be arrays
   * with 'num_colliders' elements for all the objects that we should
   * test against.
   * \param colsys is the collider system.
   * \param collider is the collider of the object that we are going
   * to move along the path.
   * \param trans is the transform of that object (see Collide()).
   * \param nbrsteps is the number of steps we want to check along the path.
   * Typically the stepsize resulting from this number of steps is set
   * to a step size smaller then the size of the collider bbox.
   * \param newpos is the new position of that object.
   * \param num_colliders is the number of colliders that we are going
   * to use to collide with.
   * \param colliders is an array of colliders. Typically you can obtain
   * such a list by doing iEngine::GetNearbyMeshes() and then getting
   * the colliders from all meshes you get (possibly using csColliderWrapper).
   * Note that it is safe to have 'collider' sitting in this list. This
   * function will ignore that collider.
   * \param transforms is an array of transforms that belong with the
   * array of colliders.
   */
  static int CollidePath (
  	iCollideSystem* colsys,
  	iCollider* collider, const csReversibleTransform* trans,
	float nbrsteps,
	csVector3& newpos,
	int num_colliders,
	iCollider** colliders,
	csReversibleTransform** transforms);

  /**
   * Trace a beam from 'start' to 'end' and return the first hit.
   * This function will use CollideRay() from the collision detection system
   * and is pretty fast. Note that only OPCODE plugin currently supports
   * this! A special note about portals! Portal traversal will NOT be used
   * on portals that have a collider! The idea there is that the portal itself
   * is a surface that cannot be penetrated.
   * \param cdsys The collider system.
   * \param sector The starting sector for the beam.
   * \param start The start of the beam.
   * \param end The end of the beam.
   * \param traverse_portals Set it to true in case you want the beam to
   *   traverse portals.
   * \param closest_tri Will be set to the closest triangle that is hit. The
   *   triangle will be specified in world space.
   * \param closest_isect Will be set to the closest intersection point (in
   *   world space).
   * \param closest_mesh Will be set to the closest mesh that is hit.
   * \param end_sector [optional] Will be set to the sector containing the 
   *   closest_mesh.
   * \return The squared distance between 'start' and the closest hit
   *   or else a negative number if there was no hit.
   */
  static float TraceBeam (iCollideSystem* cdsys, iSector* sector,
	const csVector3& start, const csVector3& end,
	bool traverse_portals,
	csIntersectingTriangle& closest_tri,
	csVector3& closest_isect,
	iMeshWrapper** closest_mesh = 0,
	iSector** end_sector = 0);

  /**
   * Trace a beam from 'start' to 'end' and return the first hit.
   * This function will use CollideRay() from the collision detection system
   * and is pretty fast. Note that only OPCODE plugin currently supports
   * this! A special note about portals! Portal traversal will NOT be used
   * on portals that have a collider! The idea there is that the portal itself
   * is a surface that cannot be penetrated.
   * \param cdsys is the collider system.
   * \param sector is the starting sector for the beam.
   * \param start is the start of the beam.
   * \param end is the end of the beam.
   * \param traverse_portals set to true in case you want the beam to
   * traverse portals.
   * \return a result instance of csTraceBeamResult.
   * \sa csTraceBeamResult
   */
  static csTraceBeamResult TraceBeam (iCollideSystem* cdsys, iSector* sector,
	const csVector3& start, const csVector3& end,
	bool traverse_portals);
};

/**
 * With csColliderActor you can more easily manage collision detection of
 * a player or character model with gravity handling.
 */
class CS_CRYSTALSPACE_EXPORT csColliderActor
{
private:
  bool revertMove;
  bool onground;
  bool cd;
  csArray<csCollisionPair> our_cd_contact;
  float gravity;
  iMeshWrapper* mesh;
  iCamera* camera;
  iMovable* movable;
  iCollideSystem* cdsys;
  iEngine* engine;
  csVector3 velWorld;
  /// Set of meshes we hit with last call to Move.
  csSet<csPtrKey<iMeshWrapper> > hit_meshes;
  bool do_hit_meshes;

  /// For rotation - Euler angles in radians
  csVector3 rotation;

  csRef<iCollider> topCollider;
  csRef<iCollider> bottomCollider;
  csBox3 boundingBox;
  csVector3 shift;
  csVector3 topSize;
  csVector3 bottomSize;
  csVector3 intervalSize;

  int revertCount;

  /**
   * Performs the collision detection for the provided csColliderWrapper vs
   * all nearby objects.
   * <p>
   * This function gets all nearby objects, crossing sector bounds. It compares
   * for collisions. If a collision is found, it follows a line segment from
   * the "old" position of the Mesh (described by cdstart) to the position of
   * one end of a line segment describing the collision. If this results in
   * crossing into the same sector that the mesh we collided with is in, then
   * the collision is valid.
   * <p>
   * This catches the case where a piece of world geometry extends into
   * coordinates of another sector, but does not actually exist in that sector.
   */
  int CollisionDetect (
	iCollider *collider,
	iSector* sector,
	csReversibleTransform* transform,
	csReversibleTransform* old_transform);

  /**
   * Performs the collision detection for the provided csColliderWrapper vs
   * all nearby objects and gives the furthest point that will not collide.
   * <p>
   * This function calls CollisionDetect each time splitting the range
   * between the 'new' position and the original position of the object and
   * testing the point in the middle. This finds the point of first contact to
   * an accuracy of EPSILON and then sets maxmove to a point before it.
   */
  int CollisionDetectIterative (
	iCollider *collider,
	iSector* sector,
	csReversibleTransform* transform,
	csReversibleTransform* old_transform, csVector3& maxmove);
  bool MoveV (float delta, const csVector3& velBody);
  bool RotateV (float delta, const csVector3& angularVelocity);
  void InitializeColliders (const csVector3& legs,
  	const csVector3& body, const csVector3& shift);

public:
  /// Construct.
  csColliderActor ();

  /// Set the collision detection system.
  void SetCollideSystem (iCollideSystem* cdsys)
  {
    csColliderActor::cdsys = cdsys;
  }

  /// Set the engine.
  void SetEngine (iEngine* engine)
  {
    csColliderActor::engine = engine;
  }

  /**
   * Initialize the colliders.
   * \param mesh is the mesh.
   * \param legs is the size of the leg collider.
   * \param body is the size of the body collider.
   * \param shift is a shift added to the colliders. Normally the
   * origin is assumed to be at the bottom of the model. With this
   * shift you can adjust that.
   */
  void InitializeColliders (iMeshWrapper* mesh, const csVector3& legs,
  	const csVector3& body, const csVector3& shift);

  /**
   * Initialize the colliders. This version is used if you have a first
   * person view and want collision detection to move the camera instead
   * of a mesh.
   * \param camera is the camera.
   * \param legs is the size of the leg collider.
   * \param body is the size of the body collider.
   * \param shift is a shift added to the colliders. Normally the
   * origin is assumed to be at the bottom of the model. With this
   * shift you can adjust that.
   */
  void InitializeColliders (iCamera* camera, const csVector3& legs,
  	const csVector3& body, const csVector3& shift);

  /**
   * Change the current camera.
   * \param camera New current camera
   * \param adjustRotation Whether to retrieve the current rotation from
   *  the camera.
   */
  void SetCamera (iCamera* camera, bool adjustRotation = true);

  /**
   * Set gravity. Terran default is 9.806.
   */
  void SetGravity (float g)
  {
    gravity = g;
    velWorld.y = 0;
  }

  /**
   * Get gravity.
   */
  float GetGravity () const { return gravity; }

  /**
   * Check if we are on the ground.
   */
  bool IsOnGround () const { return onground; }

  /**
   * Set the onground status.
   */
  void SetOnGround (bool og) { onground = og; }

  /**
   * Return true if collision detection is enabled.
   */
  bool HasCD () const { return cd; }

  /**
   * Enable/disable collision detection (default enabled).
   */
  void SetCD (bool c) { cd = c; }

  /**
   * Check if we should revert a move (revert rotation).
   */
  bool CheckRevertMove () const { return revertMove; }

  /**
   * Enable remembering of the meshes we hit.
   * By default this is disabled. If this is enabled you can call
   * GetHitMeshes() after calling Move() to get a set of all meshes
   * that were hit.
   */
  void EnableHitMeshes (bool hm) { do_hit_meshes = hm; }

  /// Return true if we remember the meshes we hit.
  bool CheckHitMeshes () const { return do_hit_meshes; }

  /**
   * Return the meshes that we hit in the last call to Move().
   * Calling Move() again will clear this set and calculate it again.
   * This works only if EnableHitMeshes(true) is called.
   */
  const csSet<csPtrKey<iMeshWrapper> >& GetHitMeshes ()
  { return hit_meshes; }

  /**
   * Move the model. If EnableHitMeshes(true) is set then you can
   * use GetHitMeshes() after this to detect the meshes that were hit.
   * \param delta is the number of seconds (floating point) elapsed
   * time. Typically this is the elapsed time from the virtual clock
   * divided by 1000.0f.
   * \param speed is the desired movement speed. This can be 1.0f for
   * default speed.
   * \param velBody is the relative movement vector in object space
   * of the model (i.e. 0,0,1 will move the model forward).
   * \param angularVelocity is the velocity of rotation.
   */
  bool Move (float delta, float speed, const csVector3& velBody,
  	const csVector3& angularVelocity);

  /**
   * Get current rotation in angles around every axis.
   * This is only used if a camera is used.
   */
  const csVector3& GetRotation () { return rotation; }

  /**
   * Set current rotation.
   * This is only used if a camera is used.
   */
  void SetRotation (const csVector3& rot);

  /**
   * This is used by Move() but you can also call it manually.
   * It will adjust the new position to match with collision
   * detection.
   */
  bool AdjustForCollisions (const csVector3& oldpos,
	csVector3& newpos,
	const csVector3& vel,
	float delta);
};

#endif // __CS_COLLIDER_H__
