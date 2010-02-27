/*
Copyright (C) 2003 by Keith Fulton

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

#include "cssysdef.h"
#include "csqsqrt.h"

#include "csgeom/math.h"
#include "csgeom/polyclip.h"
#include "csgeom/quaternion.h"
#include "csgeom/sphere.h"
#include "cstool/rviewclipper.h"
#include "csgfx/renderbuffer.h"
#include "csgfx/shadervarcontext.h"
#include "csutil/bitarray.h"
#include "csutil/cfgacc.h"
#include "csutil/csendian.h"
#include "csutil/csmd5.h"
#include "csutil/dirtyaccessarray.h"
#include "csutil/memfile.h"
#include "csutil/randomgen.h"
#include "csutil/hash.h"
#include "csutil/sysfunc.h"
#include "csutil/scfarray.h"
#include "cstool/rbuflock.h"

#include "ivideo/graph3d.h"
#include "iengine/camera.h"
#include "iengine/engine.h"
#include "iengine/light.h"
#include "iengine/mesh.h"
#include "iengine/movable.h"
#include "iengine/rview.h"
#include "iengine/sector.h"
#include "iutil/cache.h"
#include "iutil/databuff.h"
#include "iutil/object.h"
#include "iutil/objreg.h"
#include "iutil/vfs.h"

#include "ivaria/reporter.h"


#include "sprcal3d.h"
#include <cal3d/coretrack.h>
#include <cal3d/corekeyframe.h>

// STL include required by cal3d
#include <string>



CS_PLUGIN_NAMESPACE_BEGIN(SprCal3d)
{

CS_LEAKGUARD_IMPLEMENT (csCal3DMesh);
CS_LEAKGUARD_IMPLEMENT (csSpriteCal3DMeshObject);
CS_LEAKGUARD_IMPLEMENT (csSpriteCal3DMeshObjectFactory);

#ifdef CAL_16BIT_INDICES
#define CS_BUFCOMP_CALINDEX   CS_BUFCOMP_UNSIGNED_SHORT
#else
#define CS_BUFCOMP_CALINDEX   CS_BUFCOMP_UNSIGNED_INT
#endif

static void ReportCalError (iObjectRegistry* objreg, const char* msgId, 
                const char* msg)
{
  csString text;

  if (msg && (*msg != 0))
    text << msg << " [";
  text << "Cal3d: " << CalError::getLastErrorDescription().data();

  if (CalError::getLastErrorText ().size () > 0)
  {
    text << " '" << CalError::getLastErrorText().data() << "'";
  }

  text << " in " << CalError::getLastErrorFile().data() << "(" << 
    CalError::getLastErrorLine ();
  if (msg && (*msg != 0))
    text << "]";

  csReport (objreg, CS_REPORTER_SEVERITY_ERROR, msgId,
    "%s", text.GetData());
}

//--------------------------------------------------------------------------

csSpriteCal3DSocket::csSpriteCal3DSocket() : scfImplementationType (this)
{
  triangle_index = 0;
  submesh_index = 0;
  mesh_index = 0;
  attached_mesh = 0;
}

csSpriteCal3DSocket::~csSpriteCal3DSocket ()
{
}

void csSpriteCal3DSocket::SetName (char const* n)
{
 name = n;
}

void csSpriteCal3DSocket::SetMeshWrapper (iMeshWrapper* mesh)
{
  attached_mesh = mesh;
  csMatrix3 mat;
  mat.Identity();
  attached_mesh_trans = csReversibleTransform(mat, csVector3(0,0,0));
}

size_t csSpriteCal3DSocket::AttachSecondary (iMeshWrapper * mesh, csReversibleTransform trans)
{
  secondary_meshes.Push(csSpriteCal3DSocketMesh(mesh, trans));
  return secondary_meshes.GetSize ()-1;
}

void csSpriteCal3DSocket::DetachSecondary (const char* mesh_name)
{
  size_t a=FindSecondary(mesh_name);
  if (a < secondary_meshes.GetSize ())
    secondary_meshes.DeleteIndex(a);
}

void csSpriteCal3DSocket::DetachSecondary (size_t index)
{
  secondary_meshes.DeleteIndex(index);
}

size_t csSpriteCal3DSocket::FindSecondary (const char* mesh_name)
{
  for (size_t a=0; a<secondary_meshes.GetSize (); ++a)
  {
    if (strcmp (secondary_meshes[a].mesh->QueryObject()->GetName(), 
      mesh_name) == 0)
      return a;
  }
  return secondary_meshes.GetSize ();
}

//--------------------------------------------------------------------------

void csSpriteCal3DMeshObjectFactory::Report (int severity, const char* msg, ...)
{
  va_list arg;
  va_start (arg, msg);
  csReportV (object_reg, severity, "crystalspace.mesh.sprite.cal3d", msg, arg);
  va_end (arg);
}

csSpriteCal3DMeshObjectFactory::csSpriteCal3DMeshObjectFactory (
  csSpriteCal3DMeshObjectType* pParent, iObjectRegistry* object_reg) : 
  scfImplementationType (this, (iBase*)pParent), sprcal3d_type (pParent),
  calCoreModel("no name")
{
  csSpriteCal3DMeshObjectFactory::object_reg = object_reg;
  currentScalingFactor = 1;
  skel_factory.AttachNew (new csCal3dSkeletonFactory ());
}

csSpriteCal3DMeshObjectFactory::~csSpriteCal3DMeshObjectFactory ()
{
  // Now remove ugly CS material hack from model so cal dtor will work
  //    for (int i=0; i<calCoreModel.getCoreMaterialCount(); i++)
  //    {
  //    CalCoreMaterial *mat = calCoreModel.getCoreMaterial(i);
  //    std::vector< CalCoreMaterial::Map > maps = mat->getVectorMap();
  //    maps.clear();
  //    }

//  calCoreModel.destroy();
}

bool csSpriteCal3DMeshObjectFactory::Create(const char* /*name*/)
{  
  // return calCoreModel.create(name);
  return true;
}

void csSpriteCal3DMeshObjectFactory::ReportLastError ()
{
  ReportCalError (object_reg, "crystalspace.mesh.sprite.cal3d", 0);
}

void csSpriteCal3DMeshObjectFactory::SetLoadFlags(int flags)
{
  CalLoader::setLoadingMode(flags);
}

void csSpriteCal3DMeshObjectFactory::SetBasePath(const char *path)
{
  basePath = path;
}

void csSpriteCal3DMeshObjectFactory::RescaleFactory(float factor)
{
  calCoreModel.scale(factor);
  calCoreModel.getCoreSkeleton()->calculateBoundingBoxes(&calCoreModel);
  currentScalingFactor *= factor;
}

void csSpriteCal3DMeshObjectFactory::AbsoluteRescaleFactory(float factor)
{
    RescaleFactory(factor/currentScalingFactor);
}

bool csSpriteCal3DMeshObjectFactory::LoadCoreSkeleton (iVFS *vfs,
    const char *filename)
{
  csString path(basePath);
  path.Append(filename);
  csRef<iDataBuffer> file = vfs->ReadFile (path);
  if (file)
  {
    CalCoreSkeletonPtr skel = CalLoader::loadCoreSkeleton (
        (void *)file->GetData() );
    if (skel)
    {
      calCoreModel.setCoreSkeleton (skel.get());
      skel_factory->SetSkeleton (&calCoreModel);
      return true;
    }
    else
      return false;
  }
  return false;
}

int csSpriteCal3DMeshObjectFactory::LoadCoreAnimation (
    iVFS *vfs,const char *filename,
    const char *name,
    int type,
    float base_vel, float min_vel, float max_vel,
    int min_interval, int max_interval,
    int idle_pct, bool lock)
{
  csString path(basePath);
  path.Append(filename);
  csRef<iDataBuffer> file = vfs->ReadFile (path);
  if (file)
  {
    CalCoreAnimationPtr anim = CalLoader::loadCoreAnimation (
        (void*)file->GetData(), calCoreModel.getCoreSkeleton() );
    if (anim)
    {
      int id = calCoreModel.addCoreAnimation(anim.get());
      if (id != -1)
      {
        csCal3DAnimation *an = new csCal3DAnimation;
        an->name          = name;
        an->type          = type;
        an->base_velocity = base_vel;
        an->min_velocity  = min_vel;
        an->max_velocity  = max_vel;
        an->min_interval  = min_interval;
        an->max_interval  = max_interval;
        an->idle_pct      = idle_pct;
        an->lock          = lock;

        an->index = (int)anims.Push(an);

        std::string str(name);
        calCoreModel.addAnimationName (str,id);
      }
      return id;
    }
    return -1;
  }
  return -1;
}

int csSpriteCal3DMeshObjectFactory::LoadCoreMesh (
    iVFS *vfs,const char *filename,
    const char *name,
    bool attach,
    iMaterialWrapper *defmat)
{
  csString path(basePath);
  path.Append(filename);
  csRef<iDataBuffer> file = vfs->ReadFile (path);
  if (file)
  {
    csCal3DMesh *mesh = new csCal3DMesh;
    CalCoreMeshPtr coremesh = CalLoader::loadCoreMesh((void*)file->GetData() );
    if (coremesh)
    {
      mesh->calCoreMeshID = calCoreModel.addCoreMesh(coremesh.get());
      if (mesh->calCoreMeshID == -1)
      {
        delete mesh;
        return false;
      }
      mesh->name              = name;
      mesh->attach_by_default = attach;
      mesh->default_material  = defmat;

      meshes.Push(mesh);

      return mesh->calCoreMeshID;
    }
    else
      return -1;
  }
  return -1;
}

int csSpriteCal3DMeshObjectFactory::LoadCoreMorphTarget (
    iVFS *vfs,int mesh_index,
    const char *filename,
    const char *name)
{
  if (mesh_index < 0 || meshes.GetSize () <= (size_t)mesh_index)
  {
    return -1;
  }

  csString path(basePath);
  path.Append(filename);
  csRef<iDataBuffer> file = vfs->ReadFile (path);
  if (file)
  {
    CalCoreMeshPtr core_mesh = CalLoader::loadCoreMesh((void *)file->GetData() );
    if(core_mesh.get() == 0)
      return -1;
    
    int morph_index = calCoreModel.getCoreMesh(mesh_index)->
                           addAsMorphTarget(core_mesh.get());
    if(morph_index == -1)
    {
      return -1;
    }
    meshes[mesh_index]->morph_target_name.Push(name);
    return morph_index;
  }
  return -1;
}

void csSpriteCal3DMeshObjectFactory::CalculateAllBoneBoundingBoxes()
{
  // This function is SLOOOW.  Should only be called once after model is
  // fully loaded.
  calCoreModel.getCoreSkeleton()->calculateBoundingBoxes(&calCoreModel);
}

int csSpriteCal3DMeshObjectFactory::AddMorphAnimation(const char *name)
{
  int id = calCoreModel.addCoreMorphAnimation(new CalCoreMorphAnimation());
  morph_animation_names.Push(name);
  return id;
}

bool csSpriteCal3DMeshObjectFactory::AddMorphTarget( int morphanimation_index,
                                                 const char *mesh_name, 
                                                 const char *morphtarget_name)
{
  int mesh_index = FindMeshName(mesh_name);
  if(mesh_index == -1)
  {
    return false;
  }
  csArray<csString>& morph_target = meshes[mesh_index]->morph_target_name;
  size_t i;
  for (i=0; i<morph_target.GetSize (); i++)
  {
    if (morph_target[i] == morphtarget_name)
      break;
  }
  if(i==morph_target.GetSize ())
  {
    return false;
  }
  CalCoreMorphAnimation* morph_animation = calCoreModel.getCoreMorphAnimation (
    morphanimation_index);
  return morph_animation->addMorphTarget (mesh_index, (int)i);
}

int csSpriteCal3DMeshObjectFactory::GetMorphTargetCount(int mesh_id)
{
  if (mesh_id < 0|| meshes.GetSize () <= (size_t)mesh_id)
  {
    return -1;
  }
  return (int)meshes[mesh_id]->morph_target_name.GetSize ();
}

const char *csSpriteCal3DMeshObjectFactory::GetMeshName(int idx)
{
  if ((size_t)idx >= meshes.GetSize ())
    return 0;

  return meshes[idx]->name;
}

bool csSpriteCal3DMeshObjectFactory::IsMeshDefault(int idx)
{
  if ((size_t)idx >= meshes.GetSize ())
    return false;

  return meshes[idx]->attach_by_default;
}

void csSpriteCal3DMeshObjectFactory::DefaultGetBuffer (int meshIdx, 
  csRenderBufferHolder* holder, csRenderBufferName buffer)
{
  if (!holder) return;
  if ((buffer == CS_BUFFER_INDEX)
    || (buffer == CS_BUFFER_TEXCOORD0))
  {
    MeshBuffers* mb = meshBuffers.GetElementPointer (meshIdx);
    if (!mb)
    {
      meshBuffers.Put (meshIdx, MeshBuffers ());
      mb = meshBuffers.GetElementPointer (meshIdx);
    }

    if (!mb->indexBuffer.IsValid() && !mb->texcoordBuffer.IsValid())
    {
      int indexCount = 0;
      int vertexCount = 0;

      CalCoreMesh* mesh = calCoreModel.getCoreMesh (meshIdx);
      int s;
      for (s = 0; s < mesh->getCoreSubmeshCount(); s++)
      {
        CalCoreSubmesh* submesh = mesh->getCoreSubmesh (s);
        indexCount += submesh->getFaceCount() * 3;
        vertexCount += submesh->getVertexCount();
      }

      mb->indexBuffer = csRenderBuffer::CreateIndexRenderBuffer (indexCount,
        CS_BUF_STATIC, CS_BUFCOMP_CALINDEX, 0, vertexCount-1);
      mb->texcoordBuffer = csRenderBuffer::CreateRenderBuffer (vertexCount,
        CS_BUF_STATIC, CS_BUFCOMP_FLOAT, 2);

      csRenderBufferLock<CalIndex> indices (mb->indexBuffer);
      csRenderBufferLock<csVector2> texcoords (mb->texcoordBuffer);

      size_t indexOffs = 0;
      size_t tcOffs = 0;
      for (s = 0; s < mesh->getCoreSubmeshCount(); s++)
      {
        CalCoreSubmesh* submesh = mesh->getCoreSubmesh (s);
        std::vector<CalCoreSubmesh::Face>& faces = submesh->getVectorFace();
        for (size_t f = 0; f < faces.size(); f++)
        {
          indices[indexOffs++] = faces[f].vertexId[0];
          indices[indexOffs++] = faces[f].vertexId[1];
          indices[indexOffs++] = faces[f].vertexId[2];
        }
        std::vector<std::vector<CalCoreSubmesh::TextureCoordinate> >& tcV =
          submesh->getVectorVectorTextureCoordinate();
        if (tcV.size() > 0)
        {
          std::vector<CalCoreSubmesh::TextureCoordinate>& tc = tcV[0];
          for (size_t v = 0; v < tc.size(); v++)
          {
            texcoords[tcOffs++].Set (tc[v].u, tc[v].v);
          }
        }
        else
        {
          size_t vc = submesh->getVertexCount();
          memset (texcoords.Lock() + tcOffs, 0, vc * sizeof (csVector2));
          tcOffs += vc;
        }
      }
    }
    holder->SetRenderBuffer (buffer, 
      (buffer == CS_BUFFER_INDEX) ? mb->indexBuffer : mb->texcoordBuffer);
  }
}

iSpriteCal3DSocket* csSpriteCal3DMeshObjectFactory::AddSocket ()
{
  csSpriteCal3DSocket* socket = new csSpriteCal3DSocket();
  sockets.Push (socket);
  return socket;
}

iSpriteCal3DSocket* csSpriteCal3DMeshObjectFactory::FindSocket (
    const char *n) const
{
  int i;
  for (i = GetSocketCount () - 1; i >= 0; i--)
    if (strcmp (GetSocket (i)->GetName (), n) == 0)
      return GetSocket (i);

  return 0;
}

iSpriteCal3DSocket* csSpriteCal3DMeshObjectFactory::FindSocket (
    iMeshWrapper *mesh) const
{
  int i;
  for (i = GetSocketCount () - 1; i >= 0; i--)
    if (GetSocket (i)->GetMeshWrapper() == mesh)
      return GetSocket (i);

  return 0;
}

int csSpriteCal3DMeshObjectFactory::FindMeshName (const char *meshName)
{
  for (size_t i=0; i<meshes.GetSize (); i++)
  {
    if (meshes[i]->name == meshName)
      return (int)i;
  }
  return -1;
}

const char* csSpriteCal3DMeshObjectFactory::GetDefaultMaterial (
    const char* meshName)
{
  int meshIndex = FindMeshName (meshName);
  if (meshIndex != -1)
  {
    if (meshes[meshIndex]->default_material)
    {
      return meshes[meshIndex]->default_material->QueryObject()->GetName();
    }
  }
    
  return 0;                        
}

const char *csSpriteCal3DMeshObjectFactory::GetMorphAnimationName(int idx)
{
  if ((size_t)idx >= morph_animation_names.GetSize ())
    return 0;

  return morph_animation_names[idx];
}

int csSpriteCal3DMeshObjectFactory::FindMorphAnimationName (
    const char *meshName)
{
  for (size_t i=0; i<morph_animation_names.GetSize (); i++)
  {
    if (morph_animation_names[i] == meshName)
      return (int)i;
  }
  return -1;
}

bool csSpriteCal3DMeshObjectFactory::AddCoreMaterial(iMaterialWrapper *mat)
{
  CalCoreMaterial *newmat = new CalCoreMaterial;
  CalCoreMaterial::Map newmap;
  newmap.userData = mat;

//  newmat->create();
  newmat->reserve(1);
  newmat->setMap(0,newmap);  // sticking iMaterialWrapper into 2 places
  newmat->setUserData(mat);  // jam CS iMaterialWrapper into cal3d material holder

  calCoreModel.addCoreMaterial(newmat);
  return true;
}

void csSpriteCal3DMeshObjectFactory::BindMaterials()
{
  int materialId;

  // make one material thread for each material
  // NOTE: this is not the right way to do it, but this viewer can't do the
  // right mapping without further information on the model etc.
  for (materialId = 0 ; materialId < calCoreModel.getCoreMaterialCount()
    ; materialId++)
  {
    // create the a material thread
    calCoreModel.createCoreMaterialThread (materialId);

    // initialize the material thread
    calCoreModel.setCoreMaterialId (materialId, 0, materialId);
  }
}


csPtr<iMeshObject> csSpriteCal3DMeshObjectFactory::NewInstance ()
{
  csSpriteCal3DMeshObject* spr = new csSpriteCal3DMeshObject (0, 
    object_reg, calCoreModel);
  spr->SetFactory (this);
  spr->updateanim_sqdistance1 = sprcal3d_type->updateanim_sqdistance1;
  spr->updateanim_skip1 = sprcal3d_type->updateanim_skip1;
  spr->updateanim_sqdistance2 = sprcal3d_type->updateanim_sqdistance2;
  spr->updateanim_skip2 = sprcal3d_type->updateanim_skip2;
  spr->updateanim_sqdistance3 = sprcal3d_type->updateanim_sqdistance3;
  spr->updateanim_skip3 = sprcal3d_type->updateanim_skip3;

  csRef<iMeshObject> im (scfQueryInterface<iMeshObject> (spr));
  spr->DecRef ();
  return csPtr<iMeshObject> (im);
}

bool csSpriteCal3DMeshObjectFactory::RegisterAnimCallback(
    const char *anim, CalAnimationCallback *callback,float min_interval)
{
  for (size_t i=0; i<anims.GetSize (); i++)
  {
    if (anims[i]->name == anim)
    {
      CalCoreAnimation *cal_anim = calCoreModel.getCoreAnimation(anims[i]->index);
      cal_anim->registerCallback(callback,min_interval);
      return true;
    }
  }
  return false;
}

bool csSpriteCal3DMeshObjectFactory::RemoveAnimCallback(
    const char *anim, CalAnimationCallback *callback)
{
  for (size_t i=0; i<anims.GetSize (); i++)
  {
    if (anims[i]->name == anim)
    {
      CalCoreAnimation *cal_anim = calCoreModel.getCoreAnimation(anims[i]->index);
      cal_anim->removeCallback(callback);
      return true;
    }
  }
  return false;
}

void csSpriteCal3DMeshObjectFactory::HardTransform (
    const csReversibleTransform& t)
{
  csQuaternion quat;
  quat.SetMatrix (t.GetO2T ());
  CalQuaternion quatrot(quat.v.x,quat.v.y,quat.v.z,quat.w);
  csVector3 trans (t.GetOrigin () );
  CalVector translation (trans.x,trans.y,trans.z);

  // First we transform the skeleton, then we do the same to each animation.

  // get core skeleton
  CalCoreSkeleton *pCoreSkeleton;
  pCoreSkeleton = calCoreModel.getCoreSkeleton();
    
  // get core bone vector
  std::vector<CalCoreBone *>& vectorCoreBone = pCoreSkeleton
    ->getVectorCoreBone();

  // loop through all root core bones
  std::vector<int>& rootCoreBones = 
    pCoreSkeleton->getVectorRootCoreBoneId();
  std::vector<int>::iterator iteratorRootCoreBoneId;
  for (iteratorRootCoreBoneId = rootCoreBones.begin();
       iteratorRootCoreBoneId != rootCoreBones.end();
       ++iteratorRootCoreBoneId)
  {
    CalCoreBone *bone = vectorCoreBone[*iteratorRootCoreBoneId];
    CalQuaternion bonerot = bone->getRotation();
    CalVector bonevec = bone->getTranslation();
    bonerot *= quatrot;
    bonevec *= quatrot;
    bonevec += translation;
    bone->setRotation(bonerot);
    bone->setTranslation(bonevec);
  }

  int i,count = calCoreModel.getCoreAnimationCount();
  for (i = 0; i < count; i++)
  {
    CalCoreAnimation *anim = calCoreModel.getCoreAnimation(i);
    if (!anim) continue;
    // loop through all root core bones
    for (iteratorRootCoreBoneId = rootCoreBones.begin();
         iteratorRootCoreBoneId != rootCoreBones.end();
         ++iteratorRootCoreBoneId)
    {
      CalCoreTrack *track = anim->getCoreTrack(*iteratorRootCoreBoneId);
      if (!track) continue;
      for (int j=0; j<track->getCoreKeyframeCount(); j++)
      {
	CalCoreKeyframe *frame = track->getCoreKeyframe(j);
	CalQuaternion bonerot = frame->getRotation();
	CalVector bonevec = frame->getTranslation();
	bonerot *= quatrot;
	bonevec *= quatrot;
	bonevec += translation;
	frame->setRotation(bonerot);
	frame->setTranslation(bonevec);
      }
    }
  }
//  calCoreModel.getCoreSkeleton()->calculateBoundingBoxes(&calCoreModel);
}

//---------------------------csCal3dSkeletonFactory---------------------------

csCal3dSkeletonFactory::csCal3dSkeletonFactory () :
scfImplementationType(this), core_skeleton(0) 
{
}

void csCal3dSkeletonFactory::SetSkeleton (CalCoreModel *model)
{
  core_model = model;
  core_skeleton = model->getCoreSkeleton ();
  std::vector<CalCoreBone*> bvect = core_skeleton->getVectorCoreBone ();
  for (size_t i = 0; i < bvect.size (); i++)
  {
    csRef<csCal3dSkeletonBoneFactory> newFact;
    newFact.AttachNew (new csCal3dSkeletonBoneFactory (bvect[i], this));
    bones_factories.Push (newFact);
  }

  //now we can setup parents and childres
  for (size_t i = 0; i < bvect.size (); i++)
  {
    bones_factories[i]->Initialize ();
    bones_names.Put (csHashComputer<const char*>::ComputeHash (bones_factories[i]->GetName ()), i);
  }
}
int csCal3dSkeletonFactory::GetCoreBoneId (iSkeletonBoneFactory *core_bone)
{
  return core_skeleton->getCoreBoneId (core_bone->GetName ());
}
size_t csCal3dSkeletonFactory::FindBoneIndex (const char *name)
{
  size_t b_idx = bones_names.Get (
    csHashComputer<const char*>::ComputeHash (name), csArrayItemNotFound);
  return b_idx;
}
iSkeletonBoneFactory *csCal3dSkeletonFactory::FindBone (const char *name)
{
  size_t b_idx = bones_names.Get (
    csHashComputer<const char*>::ComputeHash (name), csArrayItemNotFound);
  if (b_idx != csArrayItemNotFound)
    return bones_factories[b_idx];
  return 0;
}
iSkeletonAnimation *csCal3dSkeletonFactory::CreateAnimation (const char *name)
{
  csCal3dSkeletonAnimation *anim = new csCal3dSkeletonAnimation (this);
  anim->SetName (name);
  animations_names.Put (csHashComputer<const char*>::ComputeHash (name),
    animations.Push (anim));
  core_model->addCoreAnimation (anim->GetCoreAnimation ());
  return anim;
}
iSkeletonAnimation *csCal3dSkeletonFactory::FindAnimation (const char *name) 
{
  size_t idx = animations_names.Get (
    csHashComputer<const char*>::ComputeHash (name), csArrayItemNotFound);
  if (idx != csArrayItemNotFound)
    return animations[idx];
  return 0;
}

iSkeletonBoneFactory* csCal3dSkeletonFactory::GetBone (size_t i)
{ 
  return bones_factories[i];
}

iSkeletonAnimation* csCal3dSkeletonFactory::GetAnimation (size_t idx)
{ 
  return animations[idx];
}

//---------------------------csCal3dSkeletonBoneFactory---------------------------

csCal3dSkeletonBoneFactory::csCal3dSkeletonBoneFactory (CalCoreBone *core_bone,
                                                        csCal3dSkeletonFactory* skelfact) :
scfImplementationType(this), core_bone(core_bone), skeleton_factory(skelfact) 
{
}
bool csCal3dSkeletonBoneFactory::Initialize ()
{
  std::list<int> children_ids = core_bone->getListChildId ();
  for (std::list<int>::iterator it = children_ids.begin (); it != children_ids.end (); it++)
  {
    csRef<iSkeletonBoneFactory> skel_bone = skeleton_factory->GetBone ((*it));
    children.Push (skel_bone);
  }
  int bid = core_bone->getParentId ();
  if (bid != -1)
    parent = skeleton_factory->GetBone (bid);
  
  return false;
}
//---------------------------csCal3dSkeletonAnimation---------------------------
csCal3dSkeletonAnimation::csCal3dSkeletonAnimation (csCal3dSkeletonFactory *skel_fact) : 
scfImplementationType(this), skel_fact(skel_fact) 
{
  animation = new CalCoreAnimation ();
}
csCal3dSkeletonAnimation::~csCal3dSkeletonAnimation ()
{
}
iSkeletonAnimationKeyFrame *csCal3dSkeletonAnimation::CreateFrame (const char* name)
{
  csCal3dAnimationKeyFrame *frame = new csCal3dAnimationKeyFrame (this);
  frames.Push (frame);
  return frame;
}
//---------------------------csCal3dAnimationKeyFrame---------------------------
void csCal3dAnimationKeyFrame::AddTransform (iSkeletonBoneFactory *bone,
    csReversibleTransform &transform, bool relative)
{
  int b_id = animation->GetSkeletonFactory ()->GetCoreBoneId (bone);
  if (b_id != -1)
  {
    CalCoreTrack *track = animation->GetCoreAnimation ()->getCoreTrack (b_id);
    if (!track)
    {
      track = new CalCoreTrack ();
      animation->GetCoreAnimation ()->addCoreTrack (track);
    }
    CalCoreKeyframe *keyframe = new CalCoreKeyframe ();
    csVector3 pos = transform.GetOrigin ();
    keyframe->setTranslation (CalVector (pos.x, pos.y, pos.z));

    csQuaternion csquat;
    csquat.SetMatrix (transform.GetO2T ());
    csquat.Norm ();
    keyframe->setRotation (CalQuaternion (csquat.v.x, csquat.v.y, csquat.v.z, csquat.w));
    if (track->addCoreKeyframe (keyframe))
    {
      bones.Push (bone);
      bones_key_frames.Push (keyframe);
    }
  }
}
void csCal3dAnimationKeyFrame::SetDuration (csTicks time)
{
  for (size_t i = 0; i < bones_key_frames.GetSize (); i++)
  {
    bones_key_frames[i]->setTime (CS_TIME_2_CAL_TIME (time));
  }
}
//=============================================================================

void csSpriteCal3DMeshObject::DefaultAnimTimeUpdateHandler::UpdatePosition(
  float delta, CalModel* model)
{
  model->update(delta);
}

//=============================================================================

csSpriteCal3DMeshObject::csSpriteCal3DMeshObject (iBase *pParent,
						  iObjectRegistry* object_reg,
						  CalCoreModel& calCoreModel) : 
  scfImplementationType (this, pParent), calModel(&calCoreModel)
{
  csSpriteCal3DMeshObject::object_reg = object_reg;

  // create the model instance from the loaded core model
//  if(!calModel.create (&calCoreModel))
//  {
//    ReportCalError (object_reg, "crystalspace.mesh.sprite.cal3d", 
//      "Error creating model instance");
//    return;
//  }
  G3D = csQueryRegistry<iGraphics3D> (object_reg);

  // set the material set of the whole model
  vis_cb = 0;
  is_idling = false;
  idle_override_interval = 0;

  meshVersion = 0;
  bboxVersion = (uint)-1;
  default_idle_anim = -1;
  idle_action = -1;
  last_locked_anim = -1;

  cyclic_blend_factor = 0.0f;

  do_update = -1;
  updateanim_sqdistance1 = 10*10;
  updateanim_skip1 = 5;  // Skip every 5 frames.
  updateanim_sqdistance2 = 20*20;
  updateanim_skip2 = 20;    // Skip every 20 frames.
  updateanim_sqdistance3 = 50*50;
  updateanim_skip3 = 1000;  // Animate very rarely.

  anim_time_handler.AttachNew (new DefaultAnimTimeUpdateHandler());
  calModel.getPhysique()->setAxisFactorX(-1.0f);
}

csSpriteCal3DMeshObject::~csSpriteCal3DMeshObject ()
{
//  calModel.destroy();
}


void csSpriteCal3DMeshObject::SetFactory (csSpriteCal3DMeshObjectFactory* tmpl)
{
  factory = tmpl;

  CalSkeleton *cal_skeleton;
  CalBone *bone;
  cal_skeleton = calModel.getSkeleton();
  std::vector < CalBone *> &bones = cal_skeleton->getVectorBone();

  for (size_t i = 0; i < bones.size(); i++)
  {
    bone = bones[i];
    bone->calculateState ();
  }
  cal_skeleton->calculateState ();

  // attach all default meshes to the model
  int meshId;
  for(meshId = 0; meshId < factory->GetMeshCount(); meshId++)
  {
    if (factory->meshes[meshId]->attach_by_default)
    {
      AttachCoreMesh (factory->meshes[meshId]->calCoreMeshID,
                      factory->meshes[meshId]->default_material);
    }
  }
  // To get an accurate bbox below, you must have the model standing in
  // the default anim first
  calModel.getMixer()->blendCycle(0,1,0);
  calModel.update(0);
  last_update_time = factory->vc->GetCurrentTicks ();

  RecalcBoundingBox(object_bbox);
  calModel.getMixer()->clearCycle(0,0);

//  csPrintf("Object bbox is (%1.2f, %1.2f, %1.2f) to (%1.2f, %1.2f, %1.2f)\n",
//         object_bbox.MinX(),object_bbox.MinY(),object_bbox.MinZ(),object_bbox.MaxX(),object_bbox.MaxY(),object_bbox.MaxZ());

  // Copy the sockets list down to the mesh
  iSpriteCal3DSocket *factory_socket,*new_socket;
  for (int i = 0; i < tmpl->GetSocketCount(); i++)
  {
    factory_socket = tmpl->GetSocket(i);
    new_socket = AddSocket();  // mesh now
    new_socket->SetName (factory_socket->GetName() );
    new_socket->SetTriangleIndex (factory_socket->GetTriangleIndex() );
    new_socket->SetSubmeshIndex (factory_socket->GetSubmeshIndex() );
    new_socket->SetMeshIndex (factory_socket->GetMeshIndex() );
    new_socket->SetMeshWrapper (0);
  }

  skeleton.AttachNew(new csCal3dSkeleton (cal_skeleton, factory->skel_factory, this));
}


void csSpriteCal3DMeshObject::GetRadius (float& rad, csVector3& cent)
{
  cent.Set (object_bbox.GetCenter());

  RecalcBoundingBox (object_bbox);
  const csVector3& maxbox = object_bbox.Max();
  const csVector3& minbox = object_bbox.Min();
  rad = csQsqrt (csSquaredDist::PointPoint (minbox, maxbox));
}

#define CAL3D_EXACT_BOXES true 

void csSpriteCal3DMeshObject::RecalcBoundingBox (csBox3& bbox)
{
  if (bboxVersion == meshVersion)
    return;

  CalBoundingBox &calBoundingBox  = calModel.getBoundingBox(CAL3D_EXACT_BOXES);
  CalVector p[8];
  calBoundingBox.computePoints(p);

  bbox.Set(p[0].x, p[0].y, p[0].z, p[0].x, p[0].y, p[0].z);
  for (int i=1; i<8; i++)
  {
    bbox.AddBoundingVertexSmart(p[i].x,p[i].y,p[i].z);
  }

  bboxVersion = meshVersion;
//  csPrintf("Bbox Width:%1.2f Height:%1.2f Depth:%1.2f\n",bbox.Max().x - bbox.Min().x,bbox.Max().y - bbox.Min().y,bbox.Max().z - bbox.Min().z);
}


const csBox3& csSpriteCal3DMeshObject::GetObjectBoundingBox ()
{
  RecalcBoundingBox (object_bbox);
  return object_bbox;
}

void csSpriteCal3DMeshObject::SetObjectBoundingBox (const csBox3& bbox)
{
  object_bbox = bbox;
  ShapeChanged ();
}

void csSpriteCal3DMeshObjectFactory::GetRadius (float& rad, csVector3& cent)
{
  cent.Set(0,0,0);
  rad = 1.0f;
}

const csBox3& csSpriteCal3DMeshObjectFactory::GetObjectBoundingBox ()
{
  CalCoreSkeleton *skel = calCoreModel.getCoreSkeleton ();
  skel->calculateBoundingBoxes(&calCoreModel);
  std::vector<CalCoreBone*> &vectorCoreBone = skel->getVectorCoreBone();
  CalBoundingBox &calBoundingBox  = vectorCoreBone[0]->getBoundingBox();
  CalVector p[8];
  calBoundingBox.computePoints(p);

  obj_bbox.Set (p[0].x, p[0].y, p[0].z, p[0].x, p[0].y, p[0].z);
  for (int i=1; i<8; i++)
  {
    obj_bbox.AddBoundingVertexSmart(p[i].x, p[i].y, p[i].z);
  }
  return obj_bbox;
}

void csSpriteCal3DMeshObjectFactory::SetObjectBoundingBox (const csBox3&)
{
  // @@@ TODO
}

void csSpriteCal3DMeshObject::GetObjectBoundingBox (csBox3& bbox,
    csVector3* /*verts*/, int /*vertCount*/)
{
  if (object_bbox.Empty())
    RecalcBoundingBox (object_bbox);
  bbox = object_bbox;
}

void csSpriteCal3DMeshObject::SetUserData(void *data)
{
  calModel.setUserData(data);
}

bool csSpriteCal3DMeshObject::HitBeamOutline (const csVector3& start,
    const csVector3& end, csVector3& isect, float* pr)
{
  //Checks all of the cal3d bounding boxes of each bone to see if they hit

  bool hit = false;
  std::vector<CalBone *> vectorBone = calModel.getSkeleton()->getVectorBone();
  csArray<bool> bboxhits;
  bboxhits.SetSize (vectorBone.size());
  int b = 0;
  std::vector<CalBone *>::iterator iteratorBone = vectorBone.begin();
  while (iteratorBone != vectorBone.end())
  {
    CalBoundingBox& cbb = (*iteratorBone)->getBoundingBox();
    int i;
    csPlane3 planes[6];
    for(i=0;i<6;i++)
    {
      planes[i].Set(cbb.plane[i].a,
      cbb.plane[i].b,
      cbb.plane[i].c,
      cbb.plane[i].d);
    }
    csVector3 tempisect;
    float tempdist;
    if (csIntersect3::SegmentPlanes(start,end,planes,6,tempisect,tempdist))
    {
      hit = true;
      bboxhits[b] = true;
    }
    else
    {
      bboxhits[b] = false;
    }
    ++iteratorBone;
    b++;
  }

  if(hit)
  {
    // This routine is slow, but it is intended to be accurate.
    csSegment3 seg (start, end);
    float dist, temp, max;
    temp = dist = max = csSquaredDist::PointPoint (start, end);
    csVector3 tsect;

    size_t m;
    for (m = 0; m < meshes.GetSize (); m++)
    {
      if (!meshes[m].vertex_buffer)
        GetVertexBufferIndex (m, 0);
      csRenderBufferLock<csVector3> vertices (meshes[m].vertex_buffer);
      csBitArray vUpToDate (vertices.GetSize());

      int vertexOffs = 0;
      CalMesh* calMesh = calModel.getMesh (meshes[m].calCoreMeshID);
      for (int s = 0; s < calMesh->getSubmeshCount(); s++)
      {
        CalCoreSubmesh* submesh = 
          calMesh->getSubmesh(s)->getCoreSubmesh();
        std::vector<CalCoreSubmesh::Face>& vectorFace = 
          submesh->getVectorFace();
        std::vector<CalCoreSubmesh::Face>::iterator iteratorFace
          = vectorFace.begin();
    
        while(iteratorFace != vectorFace.end())
        {
          bool bboxhit = false;
          int f;
          for(f=0;!bboxhit&&f<3;f++)
          {
            std::vector<CalCoreSubmesh::Influence> influ  = 
              submesh->getVectorVertex () 
              [(*iteratorFace).vertexId[f]].vectorInfluence;
              
            std::vector<CalCoreSubmesh::Influence>::iterator iteratorInflu =
              influ.begin();
            while(iteratorInflu != influ.end())
            {
              if(bboxhits[(*iteratorInflu).boneId])
              {
                bboxhit = true;
                break;
              }
              ++iteratorInflu;
            }
          }
          if (bboxhit)
          {
            csVector3 tri[3];
            for(f=0;f<3;f++)
            {
              CalIndex vIdx = (*iteratorFace).vertexId[f];
              //Is the vertex calculated?
              if ((meshes[m].vertexVersion != meshVersion)
                && !vUpToDate.IsBitSet (vIdx))
              {
                CalVector calVector = calModel.getPhysique()->calculateVertex(
                  calMesh->getSubmesh(s), vIdx);
                
                vertices[(size_t)(vertexOffs + vIdx)].Set (
                calVector.x, calVector.y, calVector.z);
                vUpToDate.SetBit (vIdx);
              }
              tri[f] = vertices[(size_t)(vertexOffs + vIdx)];
            }

            if (csIntersect3::SegmentTriangle (seg,
              tri[0], tri[1], tri[2], tsect))
            {
              isect = tsect;
              if (pr) *pr = csQsqrt (csSquaredDist::PointPoint (start, isect) /
                csSquaredDist::PointPoint (start, end));
              return true;
            }
          }
          ++iteratorFace;
        }
        vertexOffs += submesh->getVertexCount();
      }
    }
  }
  return false;
}


bool csSpriteCal3DMeshObject::HitBeamObject (const csVector3& start,
    const csVector3& end, csVector3& isect, float* pr, int* polygon_idx,
    iMaterialWrapper** material, csArray<iMaterialWrapper*>* materials)
{
  if (material) *material = 0;
  //Checks all of the cal3d bounding boxes of each bone to see if they hit

  bool hit = false;
  std::vector<CalBone *> vectorBone = calModel.getSkeleton()->getVectorBone();
  csArray<bool> bboxhits;
  bboxhits.SetSize (vectorBone.size());
  int b = 0;
  std::vector<CalBone *>::iterator iteratorBone = vectorBone.begin();
  while (iteratorBone != vectorBone.end())
  {
    CalBoundingBox& cbb = (*iteratorBone)->getBoundingBox();
    int i;
    csPlane3 planes[6];
    for(i=0;i<6;i++)
    {
      planes[i].Set(cbb.plane[i].a,
      cbb.plane[i].b,
      cbb.plane[i].c,
      cbb.plane[i].d);
    }
    csVector3 tempisect;
    float tempdist;
    if(csIntersect3::SegmentPlanes(start,end,planes,6,tempisect,tempdist))
    { 
      hit = true;
      bboxhits[b] = true;
    }
    else
    {
      bboxhits[b] = false;
    }
    ++iteratorBone;
    b++;
  }

  if(hit)
  {
    // This routine is slow, but it is intended to be accurate.
    if (polygon_idx) *polygon_idx = -1;

    csSegment3 seg (start, end);
    float dist, temp, max;
    temp = dist = max = csSquaredDist::PointPoint (start, end);
    csVector3 tsect;

    size_t m;
    for (m = 0; m < meshes.GetSize (); m++)
    {
      if (!meshes[m].vertex_buffer)
        GetVertexBufferIndex (m, 0);
      csRenderBufferLock<csVector3> vertices (meshes[m].vertex_buffer);
      csBitArray vUpToDate (vertices.GetSize());

      int vertexOffs = 0;
      CalMesh* calMesh = calModel.getMesh (meshes[m].calCoreMeshID);
      for (int s = 0; s < calMesh->getSubmeshCount(); s++)
      {
        CalCoreSubmesh* submesh = 
          calMesh->getSubmesh(s)->getCoreSubmesh();
        std::vector<CalCoreSubmesh::Face>& vectorFace = 
          submesh->getVectorFace();
        std::vector<CalCoreSubmesh::Face>::iterator iteratorFace
          = vectorFace.begin();
          
        while(iteratorFace != vectorFace.end())
        {
          bool bboxhit = false;
          int f;
          for(f=0;!bboxhit&&f<3;f++)
          {
            std::vector<CalCoreSubmesh::Influence> influ  = 
              submesh->getVectorVertex () 
              [(*iteratorFace).vertexId[f]].vectorInfluence;
            std::vector<CalCoreSubmesh::Influence>::iterator iteratorInflu =
              influ.begin();
            while(iteratorInflu != influ.end())
            {
              if(bboxhits[(*iteratorInflu).boneId])
              {
                bboxhit = true;
                break;
              }
              
              ++iteratorInflu;
            }
          }
          if (bboxhit)
          {
            csVector3 tri[3];
            for(f=0;f<3;f++)
            {
              CalIndex vIdx = (*iteratorFace).vertexId[f];
              //Is the vertex calculated?
              if ((meshes[m].vertexVersion != meshVersion)
                && !vUpToDate.IsBitSet (vIdx))
              {
                CalVector calVector = calModel.getPhysique()->calculateVertex(
                  calMesh->getSubmesh(s), vIdx);
                  
                vertices[(size_t)(vertexOffs + vIdx)].Set (
                  calVector.x, calVector.y, calVector.z);
                  
                vUpToDate.SetBit (vIdx);
              }
              
              tri[f] = vertices[(size_t)(vertexOffs + vIdx)];
            }

            if (csIntersect3::SegmentTriangle (seg,
              tri[0], tri[1], tri[2], tsect))
            {
              temp = csSquaredDist::PointPoint (start, tsect);
              if (temp < dist)
              {
                dist = temp;
                isect = tsect;
              }
            }
          }
          ++iteratorFace;
        }
        vertexOffs += submesh->getVertexCount();
      }
    }
    if (pr) 
        *pr = csQsqrt (dist / max);
    if (dist >= max) 
        return false;
    return true;
  }
  else
  {
    return false;
  }
}

void csSpriteCal3DMeshObject::PositionChild (iMeshObject* child,
    csTicks current_time)
{
  iSpriteCal3DSocket* socket = 0;
  size_t i;
  for ( i = 0; i < sockets.GetSize (); i++)
  {
    if(sockets[i]->GetMeshWrapper())
    {
      if (sockets[i]->GetMeshWrapper()->GetMeshObject() == child)
      {
        socket = sockets[i];
        break;
      }
    }
    for (size_t a=0; a<sockets[i]->GetSecondaryCount(); ++a)
    {
      if (sockets[i]->GetSecondaryMesh(a)->GetMeshObject() == child)
      {
        socket = sockets[i];
        break;
      }
    }
  }
  
  if (socket)
  {
    Advance(current_time);
    int m = socket->GetMeshIndex();
    int s = socket->GetSubmeshIndex();
    int f = socket->GetTriangleIndex();
    
    // Get the submesh
    CalSubmesh* submesh = calModel.getMesh(m)->getSubmesh(s);
    // Get the core submesh
    CalCoreSubmesh* coresubmesh = calModel.getCoreModel()->
      getCoreMesh(m)->getCoreSubmesh(s);
    // get the triangle
    CalCoreSubmesh::Face face = coresubmesh->getVectorFace()[f];
    // get the vertices
    CalVector vector[3];
    vector[0] = calModel.getPhysique()->calculateVertex(
    submesh,
    face.vertexId[0]);
    vector[1] = calModel.getPhysique()->calculateVertex(
    submesh,
    face.vertexId[1]);
    vector[2] = calModel.getPhysique()->calculateVertex(
    submesh,
    face.vertexId[2]);
    csVector3 vert1(vector[0].x,vector[0].y,vector[0].z);
    csVector3 vert2(vector[1].x,vector[1].y,vector[1].z);
    csVector3 vert3(vector[2].x,vector[2].y,vector[2].z);

    csVector3 center= (vert1+vert2+vert3)/3;

    csVector3 bc = vert3 - vert2;
        
    csVector3 up = vert1-center;
    up.Normalize();
    
    csVector3 normal = bc % up;
    normal.Normalize();

    csReversibleTransform trans; //= movable->GetFullTransform();
    trans.SetOrigin(center);
    trans.LookAt(normal, up);

    if (socket->GetMeshWrapper())
    {
      iMovable* movable = socket->GetMeshWrapper()->GetMovable();
      movable->SetTransform(socket->GetTransform()*trans);
      movable->UpdateMove();
    }
    
    // update secondary meshes
    for (size_t a=0; a<socket->GetSecondaryCount(); ++a)
    {
      iMeshWrapper * sec_mesh = socket->GetSecondaryMesh(a);
      if (sec_mesh)
      {
        iMovable * sec_movable = sec_mesh->GetMovable();
        if (sec_movable)
        {
          sec_movable->SetTransform(socket->GetSecondaryTransform(a)*trans);
          sec_movable->UpdateMove();
        }
      }
    }
  }
}

csRenderMesh** csSpriteCal3DMeshObject::GetRenderMeshes (int &n, 
    iRenderView* rview,
    iMovable* movable, uint32 frustum_mask)
{
  iCamera* camera = rview->GetCamera ();

  // First create the transformation from object to camera space directly:
  //   W = Mow * O - Vow;
  //   C = Mwc * (W - Vwc)
  // ->
  //   C = Mwc * (Mow * O - Vow - Vwc)
  //   C = Mwc * Mow * O - Mwc * (Vow + Vwc)
  csReversibleTransform tr_o2c;
  tr_o2c = camera->GetTransform ();
  if (!movable->IsFullTransformIdentity ())
    tr_o2c /= movable->GetFullTransform ();

  int clip_portal, clip_plane, clip_z_plane;
  CS::RenderViewClipper::CalculateClipSettings (rview->GetRenderContext (),
      frustum_mask, clip_portal, clip_plane, clip_z_plane);
  csVector3 camera_origin = tr_o2c.GetT2OTranslation ();

  // Distance between camera and object. Use this for LOD.
  float sqdist = camera_origin.x * camera_origin.x
    + camera_origin.y * camera_origin.y
    + camera_origin.z * camera_origin.z;
  if (sqdist < updateanim_sqdistance1) 
    do_update = -1;
  else if (sqdist < updateanim_sqdistance2)
  {
    if (do_update == -1 || do_update > updateanim_skip1)
      do_update = updateanim_skip1;
  }
  else if (sqdist < updateanim_sqdistance3)
  {
    if (do_update == -1 || do_update > updateanim_skip2)
      do_update = updateanim_skip2;
  }
  else
  {
    if (do_update == -1 || do_update > updateanim_skip3)
      do_update = updateanim_skip3;
  }

  const uint currentFrame = rview->GetCurrentFrameNumber ();
  bool created;
  csDirtyAccessArray<csRenderMesh*>& rendermeshes = 
    factory->sprcal3d_type->rmArrayHolder.GetUnusedData (
    created, currentFrame);

  const csReversibleTransform o2wt = movable->GetFullTransform ();
  const csVector3& wo = o2wt.GetOrigin ();

  rendermeshes.SetSize (meshes.GetSize ());
  for (size_t m = 0; m < rendermeshes.GetSize (); m++)
  {
    csRenderMesh* rm = factory->sprcal3d_type->rmHolder.GetUnusedMesh (
      created, currentFrame);
    *rm = meshes[m].render_mesh;
    rendermeshes[m] = rm;

    rm->clip_portal = clip_portal;
    rm->clip_plane = clip_plane;
    rm->clip_z_plane = clip_z_plane;
    rm->do_mirror = camera->IsMirrored ();
    rm->worldspace_origin = wo;
    rm->object2world = o2wt;
    rm->bbox = GetObjectBoundingBox(); // @@@ FIXME: could be tighter (submesh bbox)

    // @@@ Hacky.
    ((MeshAccessor*)rm->buffers->GetAccessor())->movable = movable;
  }

  n = (int)rendermeshes.GetSize ();
  return rendermeshes.GetArray();
}
void csSpriteCal3DMeshObject::NextFrame (csTicks current_time, const csVector3& /*new_pos*/,
                                         uint /*currentFrame*/)
{
  if (!skeleton->UpdatedByGraveyard ())
    Advance (current_time);
}
bool csSpriteCal3DMeshObject::Advance (csTicks current_time)
{
  if (do_update != -1)
  {
    if (do_update >= 0)
      do_update--;
    if (do_update >= 0)
      return true;
  }

  // update anim frames, etc. here
  float delta = float (current_time - last_update_time)/1000.0F;
  if (!current_time)
    delta = 0;

  // @@@ Optimization: Only when some animation or so is actually playing?
  if (anim_time_handler.IsValid())
    anim_time_handler->UpdatePosition (delta, &calModel);

  skeleton->UpdateNotify (current_time);

  if (current_time)
    last_update_time = current_time;

  if (is_idling) // check for override and play if time
  {
    idle_override_interval -= delta;
    if ((idle_override_interval <= 0) && (default_idle_anim != -1))
    {
      SetIdleOverrides(&randomGen,default_idle_anim);
      SetAnimAction(factory->anims[idle_action]->name,.25,.25);
    }
  }
  meshVersion++;
  //vertices_dirty = true;
  return true;
}

//--------------------------------------------------------------------------

int csSpriteCal3DMeshObject::GetAnimCount()
{
  return calModel.getCoreModel()->getCoreAnimationCount();
}

const char *csSpriteCal3DMeshObject::GetAnimName(int idx)
{
  if (idx >= GetAnimCount())
    return 0;

  return factory->anims[idx]->name;
}

int csSpriteCal3DMeshObject::GetAnimType(int idx)
{
  if (idx >= GetAnimCount())
    return 0;

  return factory->anims[idx]->type;
}

int csSpriteCal3DMeshObject::FindAnim(const char *name)
{
  int count = GetAnimCount();

  for (int i=0; i<count; i++)
  {
    if (factory->anims[i]->name == name)
      return i;
  }
  return -1;
}

void csSpriteCal3DMeshObject::ClearAllAnims()
{
  while (active_anims.GetSize ())
  {
    ClearAnimCyclePos ((int)(active_anims.GetSize () - 1), cyclic_blend_factor);
  }

  if (last_locked_anim != -1)
  {
    calModel.getMixer()->removeAction(last_locked_anim);
    last_locked_anim = -1;
    is_idling = false;
  }
}

bool csSpriteCal3DMeshObject::SetAnimCycle(const char *name, float weight)
{
  ClearAllAnims();
  return AddAnimCycle(name, weight, cyclic_blend_factor);
}

bool csSpriteCal3DMeshObject::SetAnimCycle(int idx, float weight)
{
  ClearAllAnims();
  return AddAnimCycle(idx, weight, cyclic_blend_factor);
}

bool csSpriteCal3DMeshObject::AddAnimCycle(const char *name, float weight,
    float delay)
{
  int idx = FindAnim(name);
  if (idx == -1)
    return false;

  AddAnimCycle(idx,weight,delay);

  Advance(0);
  return true;
}

bool csSpriteCal3DMeshObject::AddAnimCycle(int idx, float weight, float delay)
{
  calModel.getMixer()->blendCycle(idx,weight,delay);

  ActiveAnim const a = { factory->anims[idx], weight };
  active_anims.Push(a);
  return true;
}

int csSpriteCal3DMeshObject::FindAnimCyclePos(int idx) const
{
  for (size_t i = active_anims.GetSize (); i-- > 0; )
    if (active_anims[i].anim->index == idx)
      return (int)i;
  return -1;
}

int csSpriteCal3DMeshObject::FindAnimCycleNamePos(char const* name) const
{
  for (size_t i = active_anims.GetSize (); i-- > 0; )
    if (active_anims[i].anim->name == name)
      return (int)i;
  return -1;
}

void csSpriteCal3DMeshObject::ClearAnimCyclePos(int pos, float delay)
{
  calModel.getMixer()->clearCycle(active_anims[pos].anim->index,delay);
  // We do not 'delete' active_anims[pos].anim because it is owned by factory.
  active_anims.DeleteIndex(pos);
}

bool csSpriteCal3DMeshObject::ClearAnimCycle (int idx, float delay)
{
  int const pos = FindAnimCyclePos (idx);
  bool const ok = (pos != -1);
  if (ok)
    ClearAnimCyclePos (pos, delay);
  return ok;
}

bool csSpriteCal3DMeshObject::ClearAnimCycle (const char *name, float delay)
{
  int const pos = FindAnimCycleNamePos (name);
  bool const ok = (pos != -1);
  if (ok)
    ClearAnimCyclePos (pos, delay);
  return ok;
}

size_t csSpriteCal3DMeshObject::GetActiveAnimCount()
{
  return active_anims.GetSize ();
}

bool csSpriteCal3DMeshObject::GetActiveAnims (csSpriteCal3DActiveAnim* buffer, 
                          size_t max_length)
{
  if ((buffer == 0) || (max_length == 0))
    return false;

  size_t i, n = csMin (active_anims.GetSize (), max_length);

  for (i=0; i<n; i++)
  {
    ActiveAnim const& a = active_anims[i];
    buffer[i].index = a.anim->index;
    buffer[i].weight = a.weight;
  }
  return i == active_anims.GetSize ();
}

void csSpriteCal3DMeshObject::SetActiveAnims(const csSpriteCal3DActiveAnim* buffer, 
                         size_t anim_count)
{
  ClearAllAnims();

  for (size_t i=0; i<anim_count; i++)
  {
    AddAnimCycle (buffer[i].index, buffer[i].weight, 0);
  }
}

bool csSpriteCal3DMeshObject::SetAnimAction(int idx, float delayIn,
                                            float delayOut)
{
  if (idx < 0 || (size_t)idx >=factory->anims.GetSize () )
    return false;

  calModel.getMixer()->executeAction(idx,delayIn,delayOut,
                                     1,
                                     factory->anims[idx]->lock  );

  if (factory->anims[idx]->lock)
  {
     last_locked_anim = idx;
     is_idling = false;
  }

  return true;
}

bool csSpriteCal3DMeshObject::SetAnimAction(const char *name, float delayIn,
    float delayOut)
{ 
  int idx = FindAnim(name);
  if (idx == -1)
    return false;

  return SetAnimAction(idx,delayIn, delayOut);
}

void csSpriteCal3DMeshObject::SetIdleOverrides(csRandomGen *rng,int which)
{
  csCal3DAnimation *anim = factory->anims[which];

  // Determine interval till next override.
  idle_override_interval = rng->Get(anim->max_interval - anim->min_interval)
    + anim->min_interval;

  // Determine which idle override will be played.
  int odds = rng->Get(100);
  idle_action = 0;
  for (int i=0; i<GetAnimCount(); i++)
  {
    if (factory->anims[i]->idle_pct > odds)
    {
      idle_action = i;
      return;
    }
    else
    {
      odds -= factory->anims[i]->idle_pct;
    }
  }
}

void csSpriteCal3DMeshObject::SetDefaultIdleAnim(const char *name)
{
    default_idle_anim = FindAnim(name);
    if( default_idle_anim != -1 )
      SetIdleOverrides(&randomGen,default_idle_anim);
}

bool csSpriteCal3DMeshObject::SetVelocity(float vel,csRandomGen *rng)
{
  int count = GetAnimCount();
  int i;
  ClearAllAnims();
  if (!vel)
  {
    is_idling = true;
    SetTimeFactor(1);
    if (default_idle_anim != -1)
    {
      AddAnimCycle(default_idle_anim,1,0);
      if (rng)
        SetIdleOverrides (rng,default_idle_anim);
      return true;
    }
    for (i=0; i<count; i++)
    {
      if (factory->anims[i]->type == iSpriteCal3DState::C3D_ANIM_TYPE_IDLE)
      {
        default_idle_anim = i;
        AddAnimCycle(i,1,0);
        if (rng)
          SetIdleOverrides(rng,i);
        return true;
      }
    }
  }

  bool reversed = (vel<0);
  if (reversed)
  {
    vel = -vel;
    SetTimeFactor(-1);
  }
  else
  {
    SetTimeFactor(1);
  }

  is_idling = false;

  // Remove idle-animation that should not be played while moving too fast:
  if( idle_action != -1)
  {
    /* Default value for max_vel is 0 if it is not set in the cal3d file.
       Ideally, we would just test for (max_vel < vel). However, this would
       break expected behaviour for old .cal3d files, and hence require
       additional explanation. */
    if((factory->anims[idle_action]->max_velocity) && (factory->anims[idle_action]->max_velocity < vel))
    {
      calModel.getMixer()->removeAction(idle_action);
      idle_action = -1;
    }
  }

  // first look for animations with a base velocity that exactly matches
  bool found_match = false;
  for (i=0; i<count; i++)
  {
    if (factory->anims[i]->type == iSpriteCal3DState::C3D_ANIM_TYPE_TRAVEL)
    {
      if (vel < factory->anims[i]->min_velocity ||
          vel > factory->anims[i]->max_velocity)
        continue;
      if (vel == factory->anims[i]->base_velocity)
      {
        AddAnimCycle(i,1,0);
        found_match = true;
      }
    }
  }
  if (found_match)
    return true;
   
  /* since no exact matches were found, look for animations with a min to max velocity range that
      this velocity falls in and blend those animations based on how close thier base velocity matches */
  for (i=0; i<count; i++)
  {
    float pct,vel_diff;
    if (factory->anims[i]->type == iSpriteCal3DState::C3D_ANIM_TYPE_TRAVEL)
    {
      if (vel < factory->anims[i]->min_velocity ||
          vel > factory->anims[i]->max_velocity)
        continue;
      if (vel < factory->anims[i]->base_velocity)
      {
        vel_diff = 
            factory->anims[i]->base_velocity - factory->anims[i]->min_velocity;
        pct = (vel - factory->anims[i]->min_velocity) / vel_diff;
      }
      else
      {
        vel_diff = 
            factory->anims[i]->max_velocity - factory->anims[i]->base_velocity;
        pct = (factory->anims[i]->max_velocity - vel) / vel_diff;
      }
      AddAnimCycle(i,pct,0);
//     csPrintf("  Adding %s weight=%1.2f\n",factory->anims[i]->name.GetData(),pct);
    }
  }

  return true;    
}

void csSpriteCal3DMeshObject::SetLOD(float lod)
{
  calModel.setLodLevel(lod);
}

bool csSpriteCal3DMeshObject::AttachCoreMesh(const char *meshname)
{
  int idx = factory->FindMeshName(meshname);
  if (idx == -1)
    return false;

  return AttachCoreMesh (factory->meshes[idx]->calCoreMeshID,
    factory->meshes[idx]->default_material);
}

int csSpriteCal3DMeshObject::CompareMeshIndexKey (const Mesh& m, 
                          int const& id)
{
  return m.calCoreMeshID - id;
}

int csSpriteCal3DMeshObject::CompareMeshMesh (const Mesh& m1, const Mesh& m2)
{
  return m1.calCoreMeshID - m2.calCoreMeshID;
}


csRef<iRenderBuffer> csSpriteCal3DMeshObject::GetVertexBufferIndex (size_t index, 
  CalRenderer *pCalRenderer)
{
  if (meshes[index].vertexVersion != meshVersion)
  {
    GetVertexBufferCal (meshes[index].calCoreMeshID, pCalRenderer, &meshes[index].vertex_buffer);
    meshes[index].vertexVersion = meshVersion;
  }
  return meshes[index].vertex_buffer;
}

csRef<iRenderBuffer> csSpriteCal3DMeshObject::GetVertexBufferCal (int mesh_id, 
  CalRenderer *pCalRenderer)
{
  size_t index = FindMesh (mesh_id);
  
  if (index != csArrayItemNotFound)
  {
    // Return cached vertex buffer
    return GetVertexBufferIndex (index, pCalRenderer);
  }
  else
  {
    // No cached buffer. Return a new one just for the occasion.
    csRef<iRenderBuffer> newBuffer;
    GetVertexBufferCal (mesh_id, pCalRenderer, &newBuffer);
    return newBuffer;
  }
}

void csSpriteCal3DMeshObject::GetVertexBufferCal (int mesh_id, CalRenderer *pCalRenderer,
                          csRef<iRenderBuffer>* vertex_buffer)
{
  CalRenderer* render;

  if (pCalRenderer)
    render = pCalRenderer;
  else
  {
    render = calModel.getRenderer();
    render->beginRendering();
  }

  CalMesh* calMesh = calModel.getMesh (mesh_id);

  int submesh;
  int vertexCount = ComputeVertexCount (mesh_id);;

  if ((!(*vertex_buffer).IsValid ()) 
    || ((*vertex_buffer)->GetElementCount() < (size_t)vertexCount))
  {
    *vertex_buffer = csRenderBuffer::CreateRenderBuffer (vertexCount, 
      CS_BUF_DYNAMIC, CS_BUFCOMP_FLOAT, 3);
  }

  csRenderBufferLock<float> vertexLock (*vertex_buffer);

  int vertOffs = 0;
  
  size_t index = FindMesh (mesh_id);                            
  for (submesh = 0; submesh < calMesh->getSubmeshCount(); submesh++)
  {
    render->selectMeshSubmesh ((int)index, submesh);
    render->getVertices (vertexLock.Lock() + vertOffs * 3);
    vertOffs += render->getVertexCount();
  }

  if (!pCalRenderer)
    render->endRendering();
}

int csSpriteCal3DMeshObject::ComputeVertexCount (int meshIdx)
{
  int vertexCount = 0;
  CalCoreMesh* mesh = calModel.getCoreModel()->getCoreMesh (meshIdx);
  int s;
  for (s = 0; s < mesh->getCoreSubmeshCount(); s++)
  {
    CalCoreSubmesh* submesh = mesh->getCoreSubmesh (s);
    vertexCount += submesh->getVertexCount();
  }
  return vertexCount;
}


size_t csSpriteCal3DMeshObject::FindMesh( int mesh_id )
{
  for (size_t z = 0; z < meshes.GetSize (); z++)
  {
    if (meshes[z].calCoreMeshID == mesh_id)
      return z;
  }
  
  return csArrayItemNotFound;
}


bool csSpriteCal3DMeshObject::AttachCoreMesh(int calCoreMeshID, 
                                             iMaterialWrapper* iMatWrapID)
{

  if (FindMesh( calCoreMeshID ) != csArrayItemNotFound)
    return true;
    
  if (!calModel.attachMesh(calCoreMeshID))
    return false;

  CalMesh* calMesh = calModel.getMesh (calCoreMeshID);
  CS_ASSERT(calMesh);

  
  //if (iMatWrapID == 0)
  //  iMatWrapID = factory->meshes[mesh_id]->default_material;

  Mesh newMesh;
  newMesh.calCoreMeshID = calCoreMeshID;
  newMesh.svc.AttachNew (new csShaderVariableContext ());  
  
  // Sets up the render mesh
  newMesh.render_mesh.geometryInstance = this;
  newMesh.render_mesh.variablecontext = newMesh.svc;
  newMesh.render_mesh.indexstart = 0;
  newMesh.render_mesh.indexend = 0;
  newMesh.render_mesh.meshtype = CS_MESHTYPE_TRIANGLES;
  newMesh.render_mesh.buffers.AttachNew (new csRenderBufferHolder );
  csRef<iRenderBufferAccessor> accessor (
    csPtr<iRenderBufferAccessor> (new MeshAccessor (this, 
      newMesh.calCoreMeshID)));
  newMesh.render_mesh.buffers->SetAccessor (accessor, (uint32)CS_BUFFER_ALL_MASK);
  for (int s = 0; s < calMesh->getSubmeshCount(); s++)
  {
    CalSubmesh* submesh = calMesh->getSubmesh (s);
    newMesh.render_mesh.indexend += submesh->getFaceCount () * 3;
  }  
  newMesh.render_mesh.material = iMatWrapID;
  newMesh.matRef = iMatWrapID;
  
  meshes.Push(newMesh);
  
  return true;
}


bool csSpriteCal3DMeshObject::DetachCoreMesh(const char *meshname)
{
  int idx = factory->FindMeshName(meshname);
  if (idx == -1)
    return false;

  return DetachCoreMesh(factory->meshes[idx]->calCoreMeshID);
}

bool csSpriteCal3DMeshObject::DetachCoreMesh (int calCoreMeshID)
{
  size_t index = FindMesh( calCoreMeshID );
      
  if (!calModel.detachMesh(calCoreMeshID))
    return false;

  meshes.DeleteIndex(index);
  return true;
}

bool csSpriteCal3DMeshObject::BlendMorphTarget(int morph_animation_id,
    float weight, float delay)
{
  if(morph_animation_id < 0||
    factory->morph_animation_names.GetSize () <= (size_t)morph_animation_id)
  {
    return false;
  }
  return calModel.getMorphTargetMixer()->blend(morph_animation_id,weight,delay);
}

bool csSpriteCal3DMeshObject::ClearMorphTarget(int morph_animation_id,
    float delay)
{
  if(morph_animation_id < 0||
    factory->morph_animation_names.GetSize () <= (size_t)morph_animation_id)
  {
    return false;
  }
  return calModel.getMorphTargetMixer()->clear(morph_animation_id,delay);
}

void csSpriteCal3DMeshObject::SetAnimTimeUpdateHandler(
  iAnimTimeUpdateHandler* p)
{
  anim_time_handler = p;
}

iSpriteCal3DSocket* csSpriteCal3DMeshObject::AddSocket ()
{
  csSpriteCal3DSocket* socket = new csSpriteCal3DSocket();
  sockets.Push (socket);
  return socket;
}

iSpriteCal3DSocket* csSpriteCal3DMeshObject::FindSocket (const char *n) const
{
  int i;
  for (i = GetSocketCount () - 1; i >= 0; i--)
    if (strcmp (GetSocket (i)->GetName (), n) == 0)
      return GetSocket (i);

  return 0;
}

iSpriteCal3DSocket* csSpriteCal3DMeshObject::FindSocket (
    iMeshWrapper *mesh) const
{
  int i;
  for (i = GetSocketCount () - 1; i >= 0; i--)
  {
    if (GetSocket (i)->GetMeshWrapper() == mesh)
      return GetSocket (i);
    for (size_t a=0; a<GetSocket (i)->GetSecondaryCount(); ++a)
    {
      if (GetSocket (i)->GetSecondaryMesh(a) == mesh)
        return GetSocket (i);  
    }
  }

  return 0;
}

bool csSpriteCal3DMeshObject::SetMaterial(const char *mesh_name,
                                          iMaterialWrapper *mat)
{
  int idx = factory->FindMeshName(mesh_name);
  if (idx == -1)
    return false;
          
  size_t meshIdx = FindMesh( factory->meshes[idx]->calCoreMeshID );
    
  if (meshIdx == csArrayItemNotFound ) 
    return false;

  meshes[meshIdx].render_mesh.material = mat;
  meshes[meshIdx].matRef = mat;

  return true;
}

void csSpriteCal3DMeshObject::SetTimeFactor(float timeFactor)
{
  calModel.getMixer()->setTimeFactor(timeFactor);
}

float csSpriteCal3DMeshObject::GetTimeFactor()
{
  return calModel.getMixer()->getTimeFactor();
}

void csSpriteCal3DMeshObject::SetCyclicBlendFactor(float factor)
{
  cyclic_blend_factor = factor;
}

iShaderVariableContext* csSpriteCal3DMeshObject::GetCoreMeshShaderVarContext (
  const char* meshName)
{
  int idx = factory->FindMeshName(meshName);
  if (idx == -1)
    return 0;

  //size_t meshIdx = meshes.FindSortedKey (csArrayCmp<Mesh, int> (idx, 
  //  &CompareMeshIndexKey));
    size_t meshIdx = FindMesh( idx );
  
  if (meshIdx == csArrayItemNotFound ) return 0;
          
  return meshes[meshIdx].svc;
}

//----------------------------------------------------------------------

void csSpriteCal3DMeshObject::MeshAccessor::UpdateNormals (csRenderBufferHolder* holder)
{
  if (normal_buffer == 0)
  {
    normal_buffer = csRenderBuffer::CreateRenderBuffer (
      vertexCount, CS_BUF_DYNAMIC,
      CS_BUFCOMP_FLOAT, 3);
    holder->SetRenderBuffer (CS_BUFFER_NORMAL, normal_buffer);
  }

  if (meshobj->meshVersion != normalVersion)
  {
    CalRenderer* render = meshobj->calModel.getRenderer();
    CalMesh* calMesh = meshobj->calModel.getMesh (mesh);

    csRenderBufferLock<float> normalLock (normal_buffer);

    int vertOffs = 0;
    for (int submesh = 0; submesh < calMesh->getSubmeshCount();
      submesh++)
    {
      render->selectMeshSubmesh ((int)MeshIndex(), submesh);

      render->getNormals (normalLock.Lock() + vertOffs * 3);

      vertOffs += render->getVertexCount();
    }

    normalVersion = meshobj->meshVersion;
  }
}

void csSpriteCal3DMeshObject::MeshAccessor::UpdateBinormals (csRenderBufferHolder* holder)
{
  if (binormal_buffer == 0)
  {
    binormal_buffer = csRenderBuffer::CreateRenderBuffer (
      vertexCount, CS_BUF_DYNAMIC, CS_BUFCOMP_FLOAT, 3);
    holder->SetRenderBuffer (CS_BUFFER_BINORMAL, binormal_buffer);
  }

  if (meshobj->meshVersion != binormalVersion)
  {
    UpdateNormals(holder);
    UpdateTangents(holder);

    CalRenderer* render = meshobj->calModel.getRenderer();
    CalMesh* calMesh = meshobj->calModel.getMesh (mesh);

    csRenderBufferLock<csVector3> normalLock (normal_buffer);
    csRenderBufferLock<csVector3> binormalLock (binormal_buffer);
    csRenderBufferLock<csVector4> tangentLock (tangent_buffer);

    for (int submesh = 0; submesh < calMesh->getSubmeshCount(); submesh++)
    {
      render->selectMeshSubmesh ((int)MeshIndex(), submesh);

      for (int vertex = 0; vertex < render->getVertexCount(); vertex++)
      {
        int idx = submesh * render->getVertexCount() + vertex;
        binormalLock.Get(idx).Cross(normalLock.Get(idx),
          csVector3(tangentLock.Get(idx).x, tangentLock.Get(idx).y, tangentLock.Get(idx).z));
        binormalLock.Get(idx) *= tangentLock.Get(idx).w;
      }
    }

    binormalVersion = meshobj->meshVersion;
  }
}

void csSpriteCal3DMeshObject::MeshAccessor::UpdateTangents (csRenderBufferHolder* holder)
{
  if (tangent_buffer == 0)
  {
    tangent_buffer = csRenderBuffer::CreateRenderBuffer (
      vertexCount, CS_BUF_DYNAMIC, CS_BUFCOMP_FLOAT, 4);
    holder->SetRenderBuffer (CS_BUFFER_TANGENT, tangent_buffer);
  }

  if (meshobj->meshVersion != tangentVersion)
  {
    CalRenderer* render = meshobj->calModel.getRenderer();
    CalMesh* calMesh = meshobj->calModel.getMesh (mesh);

    csRenderBufferLock<float> tangentLock (tangent_buffer);

    int vertOffs = 0;
    for (int submesh = 0; submesh < calMesh->getSubmeshCount(); submesh++)
    {
      render->selectMeshSubmesh ((int)MeshIndex(), submesh);

      if(!render->isTangentsEnabled(0))
      {
        calMesh->getSubmesh(submesh)->enableTangents(0, true);
      }

      render->getTangentSpaces (0, tangentLock.Lock() + vertOffs * 4);

      vertOffs += render->getVertexCount();
    }

    tangentVersion = meshobj->meshVersion;
  }
}

void csSpriteCal3DMeshObject::MeshAccessor::PreGetBuffer 
  (csRenderBufferHolder* holder, csRenderBufferName buffer)
{
  if (!holder)
    return;

  switch(buffer)
  {
  case CS_BUFFER_POSITION:
    {
      holder->SetRenderBuffer (CS_BUFFER_POSITION, 
        meshobj->GetVertexBufferCal (mesh, 0));
    }
  case CS_BUFFER_NORMAL:
    {
      UpdateNormals (holder);
      break;
    }
  case CS_BUFFER_TANGENT:
    {
      UpdateTangents (holder);
      break;
    }
  case CS_BUFFER_BINORMAL:
    {
      UpdateBinormals (holder);
      break;
    }
  default:
    {
      meshobj->factory->DefaultGetBuffer (mesh, holder, buffer);
      break;
    }
  }
}

//---------------------------csCal3dSkeleton---------------------------
csCal3dSkeleton::csCal3dSkeleton (CalSkeleton* skeleton,
    csCal3dSkeletonFactory* skel_factory,
    csSpriteCal3DMeshObject *mesh_object) :
      scfImplementationType(this), skeleton(skeleton),
      skeleton_factory (skel_factory) 
{
  csCal3dSkeleton::mesh_object = mesh_object;
  graveyard = csQueryRegistry<iSkeletonGraveyard> (mesh_object->object_reg);
  if (graveyard)
    // @@@ This is a memory leak! There is a circular reference between
    // this Cal3DSkeleton and the list of skeletons in the graveyard.
    graveyard->AddSkeleton (this);

  std::vector<CalBone*> cal_bones = skeleton->getVectorBone ();
  for (size_t i = 0; i < cal_bones.size (); i++)
  {
    csRef<csCal3dSkeletonBone> bone;
    bone.AttachNew(new csCal3dSkeletonBone (cal_bones[i],
         skel_factory->GetBone (i), this));
    bones.Push (bone);
  }
  for (size_t i = 0; i < cal_bones.size (); i++)
  {
    bones[i]->Initialize ();
    bones_names.Put (csHashComputer<const char*>::ComputeHash (
	  bones[i]->GetName ()), i);
  }
}

void csCal3dSkeleton::UpdateNotify (const csTicks &current_ticks)
{
  for (size_t i = 0; i < update_callbacks.GetSize (); i++)
  {
    update_callbacks[i]->Execute (this, current_ticks);
  }
}
size_t csCal3dSkeleton::FindBoneIndex (const char *name)
{
  size_t b_idx = bones_names.Get (
    csHashComputer<const char*>::ComputeHash (name), csArrayItemNotFound);
  return b_idx;
}
iSkeletonBone *csCal3dSkeleton::FindBone (const char *name)
{
  size_t b_idx = bones_names.Get (
    csHashComputer<const char*>::ComputeHash (name), csArrayItemNotFound);
  if (b_idx != csArrayItemNotFound)
    return bones[b_idx];
  return 0;
}
//---------------------------csCal3dSkeletonBone---------------------------

csCal3dSkeletonBone::csCal3dSkeletonBone (CalBone *bone, iSkeletonBoneFactory *factory,
                                          csCal3dSkeleton *skeleton) :
scfImplementationType(this), bone(bone), factory(factory), skeleton(skeleton) 
{
}
bool csCal3dSkeletonBone::Initialize ()
{
  std::list<int> children_ids = bone->getCoreBone ()->getListChildId ();
  for (std::list<int>::iterator it = children_ids.begin (); it != children_ids.end (); it++)
  {
    csRef<iSkeletonBone> skel_bone = skeleton->GetBone ((*it));
    children.Push (skel_bone);
  }
  int bid = bone->getCoreBone ()->getParentId ();
  if (bid != -1)
    parent = skeleton->GetBone (bid);

  name = bone->getCoreBone ()->getName ().c_str ();

  return true;
}
csReversibleTransform &csCal3dSkeletonBone::GetFullTransform ()
{
  CalQuaternion quat = bone->getRotationAbsolute ();
  csQuaternion csquat (quat.x, quat.y, quat.z, quat.w);
  csMatrix3 mat (csquat);

  CalVector calv = bone->getTranslationAbsolute (); 
  csVector3 csv (calv[0], calv[1], calv[2]); 

  global_transform = csReversibleTransform (mat, csv); 
  return global_transform;
}
csReversibleTransform &csCal3dSkeletonBone::GetTransform ()
{
  CalQuaternion quat = bone->getRotation();
  csQuaternion csquat (quat.x, quat.y, quat.z, quat.w);
  csMatrix3 mat (csquat);

  CalVector calv = bone->getTranslation(); 
	csVector3 csv (calv[0], calv[1], calv[2]); 
  
  local_transform = csReversibleTransform (mat, csv);
  return local_transform;
}

void csCal3dSkeletonBone::SetTransform (const csReversibleTransform &transform)
{
  csQuaternion csquat;
  csquat.SetMatrix (transform.GetO2T ());
  csquat = csquat.Unit ();
  CalQuaternion quat (csquat.v.x, csquat.v.y, csquat.v.z, csquat.w);
  bone->setRotation (quat);
  csVector3 vect = transform.GetOrigin();
  bone->setTranslation (CalVector (vect[0], vect[1], vect[2]));
  local_transform = transform;
}

//----------------------------------------------------------------------

SCF_IMPLEMENT_FACTORY (csSpriteCal3DMeshObjectType)

csSpriteCal3DMeshObjectType::csSpriteCal3DMeshObjectType (iBase* pParent) :
  scfImplementationType (this, pParent)
{
  //nullPolyMesh.AttachNew (new NullPolyMesh (pParent));
}

csSpriteCal3DMeshObjectType::~csSpriteCal3DMeshObjectType ()
{
}

bool csSpriteCal3DMeshObjectType::Initialize (iObjectRegistry* object_reg)
{
  csSpriteCal3DMeshObjectType::object_reg = object_reg;
  vc = csQueryRegistry<iVirtualClock> (object_reg);
  engine = csQueryRegistry<iEngine> (object_reg);

  csConfigAccess cfg (object_reg, "/config/sprcal3d.cfg");

  updateanim_sqdistance1 = cfg->GetFloat (
    "Mesh.SpriteCal3D.DistanceThresshold1", 10.0f);
  updateanim_sqdistance1 *= updateanim_sqdistance1;
  updateanim_skip1 = cfg->GetInt ("Mesh.SpriteCal3D.SkipFrames1", 4);

  updateanim_sqdistance2 = cfg->GetFloat (
    "Mesh.SpriteCal3D.DistanceThresshold2", 20.0f);
  updateanim_sqdistance2 *= updateanim_sqdistance2;
  updateanim_skip2 = cfg->GetInt ("Mesh.SpriteCal3D.SkipFrames2", 20);

  updateanim_sqdistance3 = cfg->GetFloat (
    "Mesh.SpriteCal3D.DistanceThresshold3", 50.0f);
  updateanim_sqdistance3 *= updateanim_sqdistance3;
  updateanim_skip3 = cfg->GetInt ("Mesh.SpriteCal3D.SkipFrames3", 1000);

  return true;
}

csPtr<iMeshObjectFactory> csSpriteCal3DMeshObjectType::NewFactory ()
{
  csRef<csSpriteCal3DMeshObjectFactory> cm;
  cm.AttachNew (new csSpriteCal3DMeshObjectFactory (
    this, object_reg));
  cm->vc = vc;
  cm->engine = engine;
  cm->g3d = csQueryRegistry<iGraphics3D> (object_reg);
  return csPtr<iMeshObjectFactory> (cm);
}


}
CS_PLUGIN_NAMESPACE_END(SprCal3d)
