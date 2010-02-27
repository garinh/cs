/*
Copyright (C) 2007 by Jorrit Tyberghein

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

#ifndef __CS_BULLET_DYNAMICS_H__
#define __CS_BULLET_DYNAMICS_H__

#include "ivaria/dynamics.h"
#include "iutil/comp.h"
#include "csutil/scf.h"
#include "csutil/scf_implementation.h"
#include "csutil/csobject.h"
#include "csutil/nobjvec.h"
#include "csutil/weakrefarr.h"
#include "ivaria/bullet.h"

CS_PLUGIN_NAMESPACE_BEGIN(Bullet)
{

class csBulletMotionState;
class csBulletDebugDraw;
class csBulletRigidBody;
class csBulletCollider;

/**
* This is the implementation for a default dynamics move callback.
* It can update mesh, light or camera's positions.
*/
class csBulletDefaultMoveCallback : public scfImplementation1<
  csBulletDefaultMoveCallback, iDynamicsMoveCallback>
{
public:
  csBulletDefaultMoveCallback ();
  virtual ~csBulletDefaultMoveCallback ();

  void Execute (iMovable* movable, csOrthoTransform& t);
  virtual void Execute (iMeshWrapper* mesh, csOrthoTransform& t);
  virtual void Execute (iLight* light, csOrthoTransform& t);
  virtual void Execute (iCamera* camera, csOrthoTransform& t);
  virtual void Execute (csOrthoTransform& t);
};

/**
* This is the implementation for a default dynamics move callback.
* It can update mesh, light or camera's positions.
*/
class csBulletDefaultKinematicCallback : public scfImplementation1<
  csBulletDefaultKinematicCallback, iBulletKinematicCallback>
{
public:
  csBulletDefaultKinematicCallback ();
  virtual ~csBulletDefaultKinematicCallback ();

  virtual void GetBodyTransform (iRigidBody* body, csOrthoTransform& transform) const;
};

/**
* This is the implementation for the actual plugin.
* It is responsible for creating iDynamicSystem.
*/
class csBulletDynamics : public scfImplementation2<csBulletDynamics,
  iDynamics, iComponent>
{
private:
  iObjectRegistry* object_reg;
  csRefArrayObject<iDynamicSystem> systems;
  csRefArray<iDynamicsStepCallback> stepCallbacks;

public:
  csBulletDynamics (iBase *iParent);
  virtual ~csBulletDynamics ();

  virtual csPtr<iDynamicSystem> CreateSystem ();
  virtual void RemoveSystem (iDynamicSystem* system);
  virtual void RemoveSystems ();
  virtual iDynamicSystem* FindSystem (const char *name);
  virtual void Step (float stepsize);
  virtual void AddStepCallback (iDynamicsStepCallback *callback)
  { stepCallbacks.Push (callback); }
  virtual void RemoveStepCallback (iDynamicsStepCallback *callback)
  { stepCallbacks.Delete (callback); }

  //  iComponent
  virtual bool Initialize (iObjectRegistry* object_reg);  
};

class csBulletDynamicsSystem : public scfImplementationExt2<
  csBulletDynamicsSystem, csObject, iDynamicSystem,
  iBulletDynamicSystem>
{
  friend class csBulletRigidBody;
  friend class csBulletCollider;
  friend class csBulletJoint;

private:
  btDynamicsWorld* bulletWorld;
  btCollisionDispatcher* dispatcher;
  btDefaultCollisionConfiguration* configuration;
  btSequentialImpulseConstraintSolver* solver;
  btBroadphaseInterface* broadphase;

  csRefArrayObject<iRigidBody> dynamicBodies;
  csRefArrayObject<iRigidBody> colliderBodies;
  csRefArray<iJoint> joints;
  csRef<csBulletDefaultMoveCallback> moveCb;
  bool gimpactRegistered;

  // For getting collision mesh data.
  csStringID baseId;
  csStringID colldetId;

  csBulletDebugDraw* debugDraw;
  void CheckCollisions();
  void CheckCollision(csBulletRigidBody& cs_obA, btCollisionObject *obB,
		      btPersistentManifold &contactManifold);

public:
  csBulletDynamicsSystem (iObjectRegistry* object_reg);
  virtual ~csBulletDynamicsSystem ();

  //-- iDynamicsSystem
  virtual iObject *QueryObject () { return (iObject*) this; }
  virtual void SetGravity (const csVector3& v);
  virtual const csVector3 GetGravity () const;
  virtual void SetLinearDampener (float d);
  virtual float GetLinearDampener () const;
  virtual void SetRollingDampener (float d);
  virtual float GetRollingDampener () const;
  virtual void EnableAutoDisable (bool enable);
  virtual bool AutoDisableEnabled ();
  virtual void SetAutoDisableParams (float linear, float angular, int steps,
    float time);
  virtual void Step (float stepsize);

  virtual csPtr<iRigidBody> CreateBody ();
  virtual void RemoveBody (iRigidBody* body);
  virtual iRigidBody *FindBody (const char *name);
  virtual iRigidBody *GetBody (unsigned int index);
  virtual int GetBodysCount ();

  virtual csPtr<iBodyGroup> CreateGroup ();
  virtual void RemoveGroup (iBodyGroup* group);

  virtual csPtr<iJoint> CreateJoint ();
  virtual void RemoveJoint (iJoint* joint);
  virtual iDynamicsMoveCallback* GetDefaultMoveCallback ();

  virtual bool AttachColliderConvexMesh (iMeshWrapper* mesh,
    const csOrthoTransform& trans, float friction,
    float elasticity, float softness = 0.01f);
  virtual bool AttachColliderMesh (iMeshWrapper* mesh,
    const csOrthoTransform& trans, float friction,
    float elasticity, float softness = 0.01f);
  virtual bool AttachColliderCylinder (float length, float radius,
    const csOrthoTransform& trans, float friction,
    float elasticity, float softness = 0.01f);
  virtual bool AttachColliderCapsule (float length, float radius,
    const csOrthoTransform& trans, float friction,
    float elasticity, float softness = 0.01f);
  virtual bool AttachColliderBox (const csVector3 &size,
    const csOrthoTransform& trans, float friction,
    float elasticity, float softness = 0.01f);
  virtual bool AttachColliderSphere (float radius, const csVector3 &offset,
    float friction, float elasticity, float softness = 0.01f);
  virtual bool AttachColliderPlane (const csPlane3 &plane, float friction,
    float elasticity, float softness = 0.01f);
  virtual void DestroyColliders ();
  virtual void DestroyCollider (iDynamicsSystemCollider* collider);
  virtual void AttachCollider (iDynamicsSystemCollider* collider);
  virtual csRef<iDynamicsSystemCollider> CreateCollider ();
  virtual csRef<iDynamicsSystemCollider> GetCollider (unsigned int index);
  virtual int GetColliderCount ();

  void RegisterGimpact ();

  //-- iBulletDynamicSystem
  virtual void DebugDraw (iView* view);
  virtual csBulletHitBeamResult HitBeam (const csVector3 &start, const csVector3 &end);
};

class csBulletRigidBody : public scfImplementationExt2<csBulletRigidBody,
  csObject, iRigidBody, iBulletRigidBody>
{
  friend class csBulletMotionState;
  friend class csBulletKinematicMotionState;
  friend class csBulletDynamicsSystem;
  friend class csBulletCollider;
  friend class csBulletJoint;

  csBulletDynamicsSystem* dynSys;
  btRigidBody* body;
  csRef<iDynamicsMoveCallback> moveCb;
  csRef<iDynamicsCollisionCallback> collCb;
  csRef<iBulletKinematicCallback> kinematicCb;
  csBulletMotionState* motionState;
  csRefArray<csBulletCollider> colliders;
  btCompoundShape* compoundShape;
  csBulletState dynamicState;
  bool customMass;
  float mass;
  bool compoundChanged;
  csRef<iMeshWrapper> mesh;
  csRef<iLight> light;
  csRef<iCamera> camera;

  csArray<btCollisionObject*> contactObjects;
  csArray<btCollisionObject*> lastContactObjects;

  void RebuildBody ();

public: 
  csBulletRigidBody (csBulletDynamicsSystem* dynSys, bool isStatic = false);
  virtual ~csBulletRigidBody ();

  //-- iRigidBody
  virtual iObject* QueryObject (void) { return (iObject*) this; }

  virtual bool MakeStatic (void);
  virtual bool MakeDynamic (void);
  virtual bool IsStatic (void);
  virtual bool Disable (void);
  virtual bool Enable (void);
  virtual bool IsEnabled (void);  

  virtual csRef<iBodyGroup> GetGroup (void);

  virtual bool AttachColliderConvexMesh (iMeshWrapper* mesh,
    const csOrthoTransform& trans, float friction, float density,
    float elasticity, float softness = 0.01f);

  virtual bool AttachColliderMesh (iMeshWrapper* mesh,
    const csOrthoTransform& trans, float friction, float density,
    float elasticity, float softness = 0.01f);

  virtual bool AttachColliderCylinder (float length, float radius,
    const csOrthoTransform& trans, float friction, float density,
    float elasticity, float softness = 0.01f);

  virtual bool AttachColliderCapsule (float length, float radius,
    const csOrthoTransform& trans, float friction, float density,
    float elasticity, float softness = 0.01f);

  virtual bool AttachColliderBox (const csVector3 &size,
    const csOrthoTransform& trans, float friction, float density,
    float elasticity, float softness = 0.01f);

  virtual bool AttachColliderSphere (float radius, const csVector3 &offset,
    float friction, float density, float elasticity,
    float softness = 0.01f);

  virtual bool AttachColliderPlane (const csPlane3 &plane, float friction,
    float density, float elasticity, float softness = 0.01f);

  virtual void AttachCollider (iDynamicsSystemCollider* collider);

  virtual void DestroyColliders ();
  virtual void DestroyCollider (iDynamicsSystemCollider* collider);

  virtual void SetPosition (const csVector3& trans);
  virtual const csVector3 GetPosition () const;
  virtual void SetOrientation (const csMatrix3& trans);
  virtual const csMatrix3 GetOrientation () const;
  virtual void SetTransform (const csOrthoTransform& trans);
  virtual const csOrthoTransform GetTransform () const;
  virtual void SetLinearVelocity (const csVector3& vel);
  virtual const csVector3 GetLinearVelocity () const;
  virtual void SetAngularVelocity (const csVector3& vel);
  virtual const csVector3 GetAngularVelocity () const;

  virtual void SetProperties (float mass, const csVector3& center,
    const csMatrix3& inertia);
  virtual void GetProperties (float* mass, csVector3* center,
    csMatrix3* inertia);

  virtual float GetMass ();
  virtual csVector3 GetCenter ();
  virtual csMatrix3 GetInertia ();

  virtual void AdjustTotalMass (float targetmass);

  virtual void AddForce (const csVector3& force);
  virtual void AddTorque (const csVector3& force);
  virtual void AddRelForce (const csVector3& force);
  virtual void AddRelTorque (const csVector3& force) ;
  virtual void AddForceAtPos (const csVector3& force, const csVector3& pos);
  virtual void AddForceAtRelPos (const csVector3& force,
    const csVector3& pos);
  virtual void AddRelForceAtPos (const csVector3& force,
    const csVector3& pos);
  virtual void AddRelForceAtRelPos (const csVector3& force,
    const csVector3& pos);

  virtual const csVector3 GetForce () const;
  virtual const csVector3 GetTorque () const;

  virtual void AttachMesh (iMeshWrapper* mesh);
  virtual iMeshWrapper* GetAttachedMesh ();
  virtual void AttachLight (iLight* light);
  virtual iLight* GetAttachedLight ();
  virtual void AttachCamera (iCamera* camera);
  virtual iCamera* GetAttachedCamera ();

  virtual void Update ();

  virtual void SetMoveCallback (iDynamicsMoveCallback* cb);
  virtual void SetCollisionCallback (iDynamicsCollisionCallback* cb);
  virtual void Collision (iRigidBody *other, const csVector3& pos,
      const csVector3& normal, float depth);
  virtual csRef<iDynamicsSystemCollider> GetCollider (unsigned int index);
  virtual int GetColliderCount ();

  //-- iBulletRigidBody
  virtual void MakeKinematic ();
  virtual csBulletState GetDynamicState () const;
  virtual void SetDynamicState (csBulletState state);
  virtual void SetKinematicCallback (iBulletKinematicCallback* callback);
};

class csBulletCollider : public scfImplementation1<csBulletCollider,
  iDynamicsSystemCollider>
{
  friend class csBulletDynamicsSystem;
  friend class csBulletRigidBody;

  csOrthoTransform localTransform;
  csBulletDynamicsSystem* dynSys;
  csBulletRigidBody* body;
  csRef<iDynamicsColliderCollisionCallback> collCb;
  bool isStaticBody;
  csColliderGeometryType geomType;
  btCollisionShape* shape;
  float density;
  float friction;
  float softness;
  float elasticity;

  // Data we need to keep for the body so we can clean it up
  // later.
  size_t triangleCount, vertexCount;
  btVector3* vertices;
  int* indices;

public:
  csBulletCollider (csBulletDynamicsSystem* dynSys,
		    csBulletRigidBody* body = NULL, bool isStaticBody = false);
  virtual ~csBulletCollider ();

  virtual bool CreateSphereGeometry (const csSphere& sphere);
  virtual bool CreatePlaneGeometry (const csPlane3& plane);
  virtual bool CreateConvexMeshGeometry (iMeshWrapper *mesh);
  virtual bool CreateMeshGeometry (iMeshWrapper *mesh);
  void RebuildMeshGeometry ();
  virtual bool CreateBoxGeometry (const csVector3& box_size);
  virtual bool CreateCapsuleGeometry (float length, float radius);
  virtual bool CreateCylinderGeometry (float length, float radius);

  virtual void SetCollisionCallback (
    iDynamicsColliderCollisionCallback* cb);

  virtual void SetFriction (float friction);
  virtual void SetSoftness (float softness);
  virtual void SetDensity (float density);
  virtual void SetElasticity (float elasticity);
  virtual float GetFriction ();
  virtual float GetSoftness ();
  virtual float GetDensity ();
  virtual float GetElasticity ();

  virtual void FillWithColliderGeometry (
      csRef<iGeneralFactoryState> genmesh_fact);
  virtual csColliderGeometryType GetGeometryType () 
  { return geomType; }
  virtual csOrthoTransform GetTransform ();
  virtual csOrthoTransform GetLocalTransform ();
  virtual void SetTransform (const csOrthoTransform& trans);
  virtual bool GetBoxGeometry (csVector3& size); 
  virtual bool GetSphereGeometry (csSphere& sphere);
  virtual bool GetPlaneGeometry (csPlane3& plane); 
  virtual bool GetCylinderGeometry (float& length, float& radius);
  virtual bool GetCapsuleGeometry (float& length, float& radius);
  virtual bool GetMeshGeometry (csVector3*& vertices, size_t& vertexCount,
				int*& indices, size_t& triangleCount);
  virtual bool GetConvexMeshGeometry (csVector3*& vertices, size_t& vertexCount,
				      int*& indices, size_t& triangleCount);
  virtual void MakeStatic ();
  virtual void MakeDynamic ();
  virtual bool IsStatic ();

  float GetVolume ();
};

#define BULLET_JOINT_NONE 0
#define BULLET_JOINT_HINGE 1
#define BULLET_JOINT_POINT2POINT 2
#define BULLET_JOINT_6DOF 3

class csBulletJoint : public scfImplementation1<csBulletJoint, iJoint>
{
private:
  csBulletDynamicsSystem* dynSys;

  int jointType; // One of BULLET_JOINT_xxx

  // Pointer to either btGeneric6DofContraint (in case of BULLET_JOINT_6DOF),
  // btHingeConstraint (BULLET_JOINT_HINGE), or btPoint2PointConstraint
  // (BULLET_JOINT_POINT2POINT).
  btTypedConstraint* constraint;

  csRef<csBulletRigidBody> bodies[2];

  bool trans_constraint_x;
  bool trans_constraint_y;
  bool trans_constraint_z;
  csVector3 min_dist;
  csVector3 max_dist;

  bool rot_constraint_x;
  bool rot_constraint_y;
  bool rot_constraint_z;
  csVector3 min_angle;
  csVector3 max_angle;

  csVector3 bounce;
  csVector3 desired_velocity;
  csVector3 maxforce;

  csOrthoTransform transform;
  csVector3 angular_constraints_axis[2];

  /**
   * Compute the bullet joint type that best matches the current
   * configuration.
   */
  int ComputeBestBulletJointType ();

public:
  csBulletJoint (csBulletDynamicsSystem* dynSys);
  virtual ~csBulletJoint ();
 
  virtual bool RebuildJoint ();

  virtual void Attach (iRigidBody* body1, iRigidBody* body2,
		       bool force_update = true);
  virtual csRef<iRigidBody> GetAttachedBody (int body)
  {
    CS_ASSERT (body >= 0 && body <= 1);
    return bodies[body];
  }

  virtual void SetTransform (const csOrthoTransform& trans,
			     bool force_update = true);
  virtual csOrthoTransform GetTransform () { return transform; }

  virtual void SetTransConstraints (bool x, bool y, bool z,
				    bool force_update = true);
  virtual bool IsXTransConstrained () { return trans_constraint_x; }
  virtual bool IsYTransConstrained () { return trans_constraint_y; }
  virtual bool IsZTransConstrained () { return trans_constraint_z; }
  virtual void SetMinimumDistance (const csVector3& min,
				   bool force_update = true);
  virtual csVector3 GetMinimumDistance () { return min_dist; }
  virtual void SetMaximumDistance (const csVector3& max,
				   bool force_update = true);
  virtual csVector3 GetMaximumDistance () { return max_dist; }

  virtual void SetRotConstraints (bool x, bool y, bool z,
				  bool force_update = true);
  virtual bool IsXRotConstrained () { return rot_constraint_x; }
  virtual bool IsYRotConstrained () { return rot_constraint_y; }
  virtual bool IsZRotConstrained () { return rot_constraint_z; }

  virtual void SetMinimumAngle (const csVector3& min, bool force_update = true);
  virtual csVector3 GetMinimumAngle () { return min_angle; }
  virtual void SetMaximumAngle (const csVector3& max, bool force_update = true);
  virtual csVector3 GetMaximumAngle () { return max_angle; }

  virtual void SetBounce (const csVector3& bounce, bool force_update = true);
  virtual csVector3 GetBounce () { return bounce; }

  virtual void SetDesiredVelocity (const csVector3& velocity,
				   bool force_update = true);
  virtual csVector3 GetDesiredVelocity () { return desired_velocity; }

  virtual void SetMaxForce (const csVector3& maxForce, bool force_update = true);
  virtual csVector3 GetMaxForce () { return maxforce; }

  virtual void SetAngularConstraintAxis (const csVector3& axis, int body,
					 bool force_update = true);
  virtual csVector3 GetAngularConstraintAxis (int body);
};

}
CS_PLUGIN_NAMESPACE_END(Bullet)

#endif //__CS_BULLET_DYNAMICS_H__
