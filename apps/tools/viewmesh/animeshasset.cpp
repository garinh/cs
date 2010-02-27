/*
    Copyright (C) 2009 by Jelle Hellemans

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

#include "animeshasset.h"

#include "iengine/engine.h"
#include "iutil/objreg.h"
#include "iengine/mesh.h"
#include "csutil/scfstringarray.h"
#include "iengine/scenenode.h"
#include "iengine/movable.h"
#include "imesh/object.h"

#include "imesh/animesh.h"
#include "imesh/skeleton2anim.h"

bool AnimeshAsset::Support(iMeshWrapper* mesh)
{
  csRef<iAnimatedMesh> x = scfQueryInterface<iAnimatedMesh> (mesh->GetMeshObject());
  return x.IsValid();
}

AnimeshAsset::AnimeshAsset(iObjectRegistry* obj_reg, iMeshWrapper* mesh)
  : AssetBase(obj_reg, mesh), reverseAction (false)
{
  animeshstate = scfQueryInterface<iAnimatedMesh> (mesh->GetMeshObject());
  animeshsprite = scfQueryInterface<iAnimatedMeshFactory> (mesh->GetFactory()->GetMeshObjectFactory());

  // create a new animation tree
  iSkeletonAnimPacketFactory2* packetFactory = animeshsprite->GetSkeletonFactory()->GetAnimationPacket();
  csRef<iSkeletonFSMNodeFactory2> fsmNodeFactory = packetFactory->CreateFSMNode ("fsm");
  packetFactory->SetAnimationRoot (fsmNodeFactory);

  // create animation nodes
  for (size_t i = 0; i < packetFactory->GetAnimationCount (); i++)
  {
    csString name = packetFactory->GetAnimation (i)->GetName ();
    csRef<iSkeletonAnimationNodeFactory2> animationNode = packetFactory->CreateAnimationNode (name);
    animationNode->SetAnimation (packetFactory->GetAnimation (i));
    animationNode->SetCyclic (true);
    fsmNodeFactory->AddState (name, animationNode);
  }

  // set new anim tree
  csRef<iSkeletonAnimPacket2> packet = packetFactory->CreateInstance (animeshstate->GetSkeleton ());
  animeshstate->GetSkeleton ()->SetAnimationPacket (packet);
}

AnimeshAsset::~AnimeshAsset()
{
  DetachAll();
  csRef<iEngine> engine = csQueryRegistry<iEngine> (object_reg);
  engine->RemoveObject(spritewrapper);
  engine->RemoveObject(spritewrapper->GetFactory());
  spritewrapper.Invalidate();
  animeshstate.Invalidate();
  animeshsprite.Invalidate();
}

// Animations

bool AnimeshAsset::SupportsAnimations() 
{ 
  return true; 
}

csPtr<iStringArray> AnimeshAsset::GetAnimations()
{
  scfStringArray* arr = new scfStringArray;

  iSkeletonAnimPacketFactory2* packetFactory = animeshsprite->GetSkeletonFactory()->GetAnimationPacket();
  for (size_t i = 0; i < packetFactory->GetAnimationCount (); i++)
  {
    csString name = packetFactory->GetAnimation (i)->GetName ();
    arr->Push (name);
  }

  return csPtr<iStringArray>(arr);
}

bool AnimeshAsset::PlayAnimation(const char* animationName, bool cycle)
{
  csRef<iSkeletonFSMNode2> node = scfQueryInterfaceSafe<iSkeletonFSMNode2>
    (animeshstate->GetSkeleton()->GetAnimationPacket()->GetAnimationRoot()->FindNode ("fsm"));
  csRef<iSkeletonFSMNodeFactory2> factory = scfQueryInterfaceSafe<iSkeletonFSMNodeFactory2>
    (node->GetFactory ());
  
  CS::Animation::StateID id = factory->FindState (animationName);
  if (id == CS::Animation::InvalidKeyframeID)
    return false;

  if (node->IsActive ())
    node->Stop ();
  node->SwitchToState (id);
  csRef<iSkeletonAnimationNodeFactory2> animfactory = scfQueryInterfaceSafe<iSkeletonAnimationNodeFactory2>
    (node->GetStateNode (id)->GetFactory ());
  animfactory->SetCyclic (cycle);
  node->GetStateNode (id)->SetPlaybackSpeed (reverseAction ? -1.00f : 1.00f);
  node->Play ();

  return true;
}

bool AnimeshAsset::StopAnimation(const char* animationName)
{
  csRef<iSkeletonFSMNode2> node = scfQueryInterfaceSafe<iSkeletonFSMNode2>
    (animeshstate->GetSkeleton()->GetAnimationPacket()->GetAnimationRoot()->FindNode ("fsm"));
  if (node->IsActive ())
    node->Stop ();

  return true;
}

bool AnimeshAsset::GetReverseAction()
{
  return reverseAction;
}

void AnimeshAsset::SetReverseAction(bool value)
{
  reverseAction = value;
  csRef<iSkeletonFSMNode2> node = scfQueryInterfaceSafe<iSkeletonFSMNode2>
    (animeshstate->GetSkeleton()->GetAnimationPacket()->GetAnimationRoot()->FindNode ("fsm"));
  node->GetStateNode (node->GetCurrentState ())->SetPlaybackSpeed (reverseAction ? -1.00f : 1.00f);
}


// Sockets
bool AnimeshAsset::SupportsSockets() 
{ 
  return true; 
}

csPtr<iStringArray> AnimeshAsset::GetSockets()
{
  scfStringArray* arr = new scfStringArray;
  for (uint i = 0; i < animeshsprite->GetSocketCount(); i++)
  {
    iAnimatedMeshSocketFactory* v = animeshsprite->GetSocket(i);
    if (!v) continue;

    csString s = v->GetName();
    arr->Push (s);
  }

  return csPtr<iStringArray>(arr);
}

bool AnimeshAsset::AttachMesh(const char* socketName, iMeshWrapper* mesh)
{
  iAnimatedMeshSocket* sock = animeshstate->GetSocket(animeshsprite->FindSocket(socketName));
  if (!sock) return false;

  if (mesh)
  {
    sock->SetSceneNode(mesh->QuerySceneNode());
    mesh->QuerySceneNode ()->SetParent (spritewrapper->QuerySceneNode ());
  }
  else
  {
    csRef<iMeshWrapper> meshWrapOld;
    if (sock->GetSceneNode())
    meshWrapOld = sock->GetSceneNode()->QueryMesh();

    if (!meshWrapOld ) return false;

    meshWrapOld->GetMovable()->SetSector(0);
    csRef<iEngine> engine = csQueryRegistry<iEngine> (object_reg);
    engine->RemoveObject(meshWrapOld);
    engine->RemoveObject(meshWrapOld->GetFactory());
    sock->SetSceneNode(0);
  }

  return true;
}

bool AnimeshAsset::AddSocket(const char* socketName)
{
  return false;
}

bool AnimeshAsset::DeleteSocket(const char* socketName)
{
  return false;
}

SocketDescriptor AnimeshAsset::GetSocketTransform(const char* socketName)
{
  SocketDescriptor desc;

  uint sockf = animeshsprite->FindSocket(socketName);
  if (sockf == (uint)~0) return desc;
  iAnimatedMeshSocket* sock = animeshstate->GetSocket(sockf);
  if (!sock) return desc;

  csString str;

  desc["Name"] = TypeValue("String", sock->GetName());
  desc["Bone"] = TypeValue("String", str.Format("%d", sock->GetBone()).GetData());

  //csVector3 offset = sock->GetFactory()->GetTransform().GetOrigin();
  csVector3 offset = sock->GetTransform().GetOrigin();
  desc["Offset"] = TypeValue("Vector3", str.Format("%.2f, %.2f, %.2f", offset.x, offset.y, offset.z).GetData());

  //csVector3 rot = Decompose(sock->GetFactory()->GetTransform().GetO2T());
  csVector3 rot = Decompose(sock->GetTransform().GetO2T());
  desc["Rotation"] = TypeValue("Vector3", str.Format("%.2f, %.2f, %.2f", rot.x, rot.y, rot.z).GetData());

  return desc;
}

bool AnimeshAsset::SetSocketTransform(const char* socketName, const SocketDescriptor& desc)
{
  uint sockf = animeshsprite->FindSocket(socketName);
  if (sockf == (uint)~0) return false;
  iAnimatedMeshSocket* sock = animeshstate->GetSocket(sockf);
  if (!sock) return false;

  sock->GetFactory()->SetName(desc.find("Name")->second.second.c_str());

  int bone;
  sscanf(desc.find("Bone")->second.second.c_str(), "%d", &bone);
  sock->GetFactory()->SetBone(bone);

  csVector3 offset;
  if (sscanf(desc.find("Offset")->second.second.c_str(), "%f, %f, %f", &offset[0], &offset[1], &offset[2]) != 3)
    return false;
  //csReversibleTransform trans = sock->GetFactory()->GetTransform();
  csReversibleTransform trans = sock->GetTransform();
  trans.SetOrigin(offset);
  //sock->GetFactory()->SetTransform(trans);
  sock->SetTransform(trans);

  csVector3 rot;
  if (sscanf(desc.find("Rotation")->second.second.c_str(), "%f, %f, %f", &rot[0], &rot[1], &rot[2]) != 3)
    return false;
  //csReversibleTransform transr = sock->GetFactory()->GetTransform();
  csReversibleTransform transr = sock->GetTransform();
  csMatrix3 m;
  m *= csXRotMatrix3 (rot.x);
  m *= csYRotMatrix3 (rot.y);
  m *= csZRotMatrix3 (rot.z);
  transr.SetO2T(m);
  //sock->GetFactory()->SetTransform(transr);
  sock->SetTransform(transr);

  return true;
}

bool AnimeshAsset::DetachAll()
{
  for (size_t i = 0; i < animeshsprite->GetSocketCount(); i++)
  {
    iAnimatedMeshSocketFactory* v = animeshsprite->GetSocket(i);
    if (!v) continue;

    AttachMesh(v->GetName(), 0);
  }

  return true;
}


// SubMeshes
bool AnimeshAsset::SupportsSubMeshes() 
{ 
  return true; 
}

csPtr<iStringArray> AnimeshAsset::GetSubMeshes()
{
  scfStringArray* arr = new scfStringArray;
  for (uint i = 0; i < animeshsprite->GetSubMeshCount(); i++)
  {
    iAnimatedMeshFactorySubMesh* v = animeshsprite->GetSubMesh(i);
    if (!v) continue;

    csString s = v->GetName();
    arr->Push (s);
  }

  return csPtr<iStringArray>(arr);
}

bool AnimeshAsset::SetSubMeshRendering(const char* subMeshName, bool value)
{
  size_t idx = animeshsprite->FindSubMesh(selectedSubMesh);
  if (idx != (size_t)-1)
  {
    animeshstate->GetSubMesh(idx)->SetRendering(value);
    return true;
  }
  return false;
}

bool AnimeshAsset::SetSubMeshMaterial(const char* subMeshName, iMaterialWrapper* mat)
{
  size_t idx = animeshsprite->FindSubMesh(subMeshName);
  if (idx != (size_t)-1)
  {
    animeshstate->GetSubMesh(idx)->SetMaterial(mat);
    return true;
  }
  return false;
}


// MorphTargets
bool AnimeshAsset::SupportsMorphTargets() 
{ 
  return true; 
}

csPtr<iStringArray> AnimeshAsset::GetMorphTargets()
{
  scfStringArray* arr = new scfStringArray;
  for (uint i = 0; i < animeshsprite->GetMorphTargetCount(); i++)
  {
    iAnimatedMeshMorphTarget* v = animeshsprite->GetMorphTarget(i);
    if (!v) continue;

    csString s = v->GetName();
    arr->Push (s);
  }

  return csPtr<iStringArray>(arr);
}

float AnimeshAsset::GetMorphTargetWeight(const char* name)
{
  int target = animeshsprite->FindMorphTarget(name);
  if (target == -1) return 0.0f;

  return animeshstate->GetMorphTargetWeight(target);
}

bool AnimeshAsset::SetMorphTargetWeight(const char* name, float value)
{
  int target = animeshsprite->FindMorphTarget(name);
  if (target == -1) return false;

  animeshstate->SetMorphTargetWeight(target, value);
  return true;
}
