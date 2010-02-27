/*
  Copyright (C) 2006 by Marten Svanfeldt

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

#include "csgeom/math.h"
#include "csgeom/transfrm.h"
#include "csgeom/tri.h"
#include "csgfx/renderbuffer.h"
#include "cstool/rbuflock.h"
#include "cstool/rviewclipper.h"
#include "csutil/algorithms.h"
#include "csutil/sysfunc.h"
#include "csutil/radixsort.h"
#include "csutil/floatrand.h"

#include "imesh/particles.h"
#include "iengine/material.h"
#include "iengine/camera.h"
#include "iengine/rview.h"
#include "iengine/mesh.h"
#include "iengine/movable.h"
#include "ivideo/graph3d.h"
#include "ivideo/rendermesh.h"

#include "particles.h"
#include "vertexsetup.h"



CS_PLUGIN_NAMESPACE_BEGIN(Particles)
{

  CS_LEAKGUARD_IMPLEMENT (ParticlesMeshObject::RenderBufferAccessor);

  SCF_IMPLEMENT_FACTORY(ParticlesMeshObjectType)

  //-- Object type
  ParticlesMeshObjectType::ParticlesMeshObjectType (iBase* parent)
    : scfImplementationType (this, parent)
  {
  }

  ParticlesMeshObjectType::~ParticlesMeshObjectType ()
  {
  }

  bool ParticlesMeshObjectType::Initialize (iObjectRegistry* object_reg)
  {
    return true;
  }

  csPtr<iMeshObjectFactory> ParticlesMeshObjectType::NewFactory ()
  {
    return new ParticlesMeshFactory (this);
  }

  //-- Object factory
  ParticlesMeshFactory::ParticlesMeshFactory (ParticlesMeshObjectType* objectType)
    : scfImplementationType (this), objectType (objectType), factoryWrapper (0),
    materialWrapper (0), mixMode (0),
    deepCreation (false), particleOrientation (CS_PARTICLE_CAMERAFACE_APPROX), 
    rotationMode (CS_PARTICLE_ROTATE_NONE), sortMode (CS_PARTICLE_SORT_NONE),
    integrationMode (CS_PARTICLE_INTEGRATE_LINEAR), 
    transformMode (CS_PARTICLE_LOCAL_MODE),
    commonDirection (1.0f,0,0), individualSize (false), particleSize (1.0f)
  {
  }

  ParticlesMeshFactory::~ParticlesMeshFactory()
  {
  }

  csPtr<iMeshObject> ParticlesMeshFactory::NewInstance ()
  {
    ParticlesMeshObject* mesh = new ParticlesMeshObject (this);

    // Copy default properties
    mesh->materialWrapper = materialWrapper;
    mesh->mixMode = mixMode;

    mesh->particleOrientation = particleOrientation;
    mesh->rotationMode = rotationMode;
    mesh->sortMode = sortMode;
    mesh->integrationMode = integrationMode;
    mesh->transformMode = transformMode;
    mesh->commonDirection = commonDirection;
    mesh->individualSize = individualSize;
    mesh->particleSize = particleSize;
    mesh->minBB = minBB;

    if (deepCreation)
    {
      for (size_t i = 0; i < emitters.GetSize (); ++i)
      {
        csRef<iParticleEmitter> copy = emitters[i]->Clone ();
        if (copy)
          mesh->emitters.Push (copy);
      }

      for (size_t i = 0; i < effectors.GetSize (); ++i)
      {
        csRef<iParticleEffector> copy = effectors[i]->Clone ();
        if (copy)
          mesh->effectors.Push (copy);
      }
    }
    else
    {
      mesh->emitters = emitters;
      mesh->effectors = effectors;
    }

    return mesh;
  }

  csPtr<iMeshObjectFactory> ParticlesMeshFactory::Clone ()
  {
    ParticlesMeshFactory* newFact = new ParticlesMeshFactory (objectType);

    newFact->materialWrapper = materialWrapper;
    newFact->mixMode = mixMode;
    newFact->flags = flags;
    newFact->deepCreation = deepCreation;
    newFact->particleOrientation = particleOrientation;
    newFact->rotationMode = rotationMode;
    newFact->sortMode = sortMode;
    newFact->integrationMode = integrationMode;
    newFact->commonDirection = commonDirection;
    newFact->transformMode = transformMode;
    newFact->individualSize = individualSize;
    newFact->particleSize = particleSize;
    newFact->minBB = minBB;

    if (deepCreation)
    {
      for (size_t i = 0; i < emitters.GetSize (); ++i)
      {
        csRef<iParticleEmitter> copy = emitters[i]->Clone ();
        if (copy)
          newFact->emitters.Push (copy);
      }

      for (size_t i = 0; i < effectors.GetSize (); ++i)
      {
        csRef<iParticleEffector> copy = effectors[i]->Clone ();
        if (copy)
          newFact->effectors.Push (copy);
      }
    }
    else
    {
      newFact->emitters = emitters;
      newFact->effectors = effectors;
    }

    return newFact;
  }

  //-- Object
  ParticlesMeshObject::ParticlesMeshObject (ParticlesMeshFactory* factory)
    : scfImplementationType (this), 
    factory (factory), vertexSetup (0), delayedAdvance(0),
    meshWrapper (0), mixMode (CS_FX_COPY), lastUpdateTime (0),
    lastFrameNumber (0), totalParticleTime (0.0f),
    radius (1.0f), minRadius (1.0f), rawBuffer (0), particleAllocatedSize (0),
    externalControl (false),
    particleOrientation (CS_PARTICLE_CAMERAFACE_APPROX), rotationMode (CS_PARTICLE_ROTATE_NONE), 
    integrationMode (CS_PARTICLE_INTEGRATE_LINEAR), 
    sortMode (CS_PARTICLE_SORT_NONE), transformMode (CS_PARTICLE_LOCAL_MODE), 
    commonDirection (1.0f,0,0), individualSize (false), particleSize (1.0f)
  {
    particleBuffer.particleCount = 0;

    renderBufferAccessor.AttachNew (new RenderBufferAccessor (this));
  }

  ParticlesMeshObject::~ParticlesMeshObject ()
  {
    // Delete all particles
    delete [] rawBuffer;
    delete vertexSetup;
  }

  void ParticlesMeshObject::ReserveNewParticles (size_t numNew)
  {
    if (particleBuffer.particleCount + numNew > particleAllocatedSize)
    {
      //Allocate some more
      size_t newSize = particleBuffer.particleCount + numNew;
      size_t byteSize = newSize * (sizeof (csParticle) + sizeof (csParticleAux));

      uint8* newBuf = new uint8[byteSize];

      csParticleBuffer setupBuffer;
      setupBuffer.particleCount = particleBuffer.particleCount;
      setupBuffer.particleData = reinterpret_cast<csParticle*> (newBuf);
      setupBuffer.particleAuxData = reinterpret_cast<csParticleAux*> (
        newBuf + newSize*sizeof (csParticle));

      memcpy(setupBuffer.particleData, particleBuffer.particleData, 
        particleBuffer.particleCount * sizeof (csParticle));
      memcpy(setupBuffer.particleAuxData, particleBuffer.particleAuxData, 
        particleBuffer.particleCount * sizeof (csParticleAux));

      delete [] rawBuffer;

      particleBuffer = setupBuffer;
      rawBuffer = newBuf;

      particleAllocatedSize = newSize;
    }
  }

  void ParticlesMeshObject::SetupIndexBuffer (csRenderBufferHolder* bufferHolder, 
    const csReversibleTransform& o2c)
  {
    if (particleBuffer.particleCount == 0)
      return;

    if (sortMode == CS_PARTICLE_SORT_NONE)
    {
      //Make sure buffer is big enough
      if (!unsortedIndexBuffer || 
          particleBuffer.particleCount*6 > unsortedIndexBuffer->GetElementCount ())
      {
        unsortedIndexBuffer = csRenderBuffer::CreateIndexRenderBuffer (
          particleBuffer.particleCount*6, CS_BUF_STATIC, CS_BUFCOMP_UNSIGNED_INT, 
          0, particleBuffer.particleCount*4);

        csRenderBufferLock<csTriangle> bufferLock (unsortedIndexBuffer);
        csTriangle* trigs = bufferLock.Lock ();

        for (unsigned int i = 0; i < particleBuffer.particleCount; ++i)
        {
          const unsigned int trigId = i*2;
          const unsigned int index = i*4;
          trigs[trigId+0].a = index+0;
          trigs[trigId+0].b = index+1;
          trigs[trigId+0].c = index+2;

          trigs[trigId+1].a = index+2;
          trigs[trigId+1].b = index+3;
          trigs[trigId+1].c = index+0;
        }
      }

      bufferHolder->SetRenderBuffer (CS_BUFFER_INDEX, unsortedIndexBuffer);
    }
    else
    {
      csRef<iRenderBuffer> indexBuffer = bufferHolder->
        GetRenderBufferNoAccessor (CS_BUFFER_INDEX);

      if (!indexBuffer || 
        particleBuffer.particleCount*6 > indexBuffer->GetElementCount ())
      {
        indexBuffer = csRenderBuffer::CreateIndexRenderBuffer (
          particleBuffer.particleCount*6, CS_BUF_STREAM, CS_BUFCOMP_UNSIGNED_INT,
          0, particleBuffer.particleCount*4);
      }

      CS_ALLOC_STACK_ARRAY(float, sortValues, particleBuffer.particleCount);
      
      if (sortMode == CS_PARTICLE_SORT_DISTANCE)
      {
        const csVector3& camPos = o2c.GetOrigin ();

        for (unsigned int i = 0; i < particleBuffer.particleCount; ++i)
        {
          const csParticle& particle = particleBuffer.particleData[i];
          sortValues[i] = -(particle.position - camPos).SquaredNorm ();
        }
      }
      else if (sortMode == CS_PARTICLE_SORT_DOT)
      {
        const csVector3& camFwd = o2c.GetFront ();

        for (unsigned int i = 0; i < particleBuffer.particleCount; ++i)
        {
          const csParticle& particle = particleBuffer.particleData[i];
          sortValues[i] = -(particle.position * camFwd);
        }
      }

      indexSorter.Sort (sortValues, particleBuffer.particleCount);
      unsigned int* ranks = (unsigned int*)indexSorter.GetRanks (); 

      csRenderBufferLock<csTriangle> bufferLock (indexBuffer);
      csTriangle* trigs = bufferLock.Lock ();

      for (unsigned int i = 0; i < particleBuffer.particleCount; ++i)
      {
        const unsigned int trigId = i*2;
        const unsigned int index = ranks[i]*4;
        trigs[trigId+0].a = index+0;
        trigs[trigId+0].b = index+1;
        trigs[trigId+0].c = index+2;

        trigs[trigId+1].a = index+2;
        trigs[trigId+1].b = index+3;
        trigs[trigId+1].c = index+0;
      }

      bufferHolder->SetRenderBuffer (CS_BUFFER_INDEX, indexBuffer);
    }
  }

  void ParticlesMeshObject::SetupVertexBuffer (csRenderBufferHolder* bufferHolder, 
    const csReversibleTransform& o2c)
  {
    if (particleBuffer.particleCount == 0)
      return;

    csRef<iRenderBuffer> buffer = bufferHolder->GetRenderBuffer (CS_BUFFER_POSITION);
    if (!buffer ||
      particleBuffer.particleCount*4 > buffer->GetElementCount ())
    {
      //Create a new one
      buffer = csRenderBuffer::CreateRenderBuffer (particleBuffer.particleCount*4,
        CS_BUF_STREAM, CS_BUFCOMP_FLOAT, 3);

      bufferHolder->SetRenderBuffer (CS_BUFFER_POSITION, buffer);
    }

    csRenderBufferLock<csVector3> bufferLock (buffer);
    csVector3* vertices = bufferLock.Lock ();

    if (!vertexSetup)
    {
      vertexSetup = GetVertexSetupFunc (rotationMode, particleOrientation,
        individualSize);
    }

    vertexSetup->Init (o2c, commonDirection, particleSize);

    vertexSetup->SetupVertices (particleBuffer, vertices);
  }

  void ParticlesMeshObject::UpdateTexCoordBuffer ()
  {
    if (rotationMode == CS_PARTICLE_ROTATE_TEXCOORD)
    {
      if (!tcBuffer ||
        particleBuffer.particleCount*4 > tcBuffer->GetElementCount ())
      {
        tcBuffer = csRenderBuffer::CreateRenderBuffer (
          particleBuffer.particleCount*4,
          CS_BUF_DYNAMIC, CS_BUFCOMP_FLOAT, 2);
      }

      csRenderBufferLock<csVector2> bufferLock (tcBuffer);
      csVector2 *tcs = bufferLock.Lock ();

      for (unsigned int idx = 0; idx < particleBuffer.particleCount; ++idx)
      {
        const unsigned int tcIdx = idx*4;

        const csParticle& particle = particleBuffer.particleData[idx];

        csVector3 tmpV;
        float rot;
        particle.orientation.GetAxisAngle (tmpV, rot);
        const float r = rot + PI/4;
        const float s = sinf(r);
        const float c = cosf(r);

        float pX = 0.5f*(c-s);
        float pY = 0.5f*(s+c);

        tcs[tcIdx+0].x = -pX; tcs[tcIdx+0].y = +pY;
        tcs[tcIdx+1].x =  pY; tcs[tcIdx+1].y =  pX;
        tcs[tcIdx+2].x =  pX; tcs[tcIdx+2].y = -pY;          
        tcs[tcIdx+3].x = -pY; tcs[tcIdx+3].y = -pX;

      }        
    
    }
    else
    {
      // Just setup 00 10 11 01
      if (!tcBuffer ||
          particleBuffer.particleCount*4 > tcBuffer->GetElementCount ())
      {
        tcBuffer = csRenderBuffer::CreateRenderBuffer (
          particleBuffer.particleCount*4,
          CS_BUF_DYNAMIC, CS_BUFCOMP_FLOAT, 2);

        csRenderBufferLock<csVector2> bufferLock (tcBuffer);
        csVector2 *tcs = bufferLock.Lock ();

        for (unsigned int idx = 0; idx < particleBuffer.particleCount; ++idx)
        {
          const unsigned int tcIdx = idx*4;

          tcs[tcIdx+0].x = -0.0f; tcs[tcIdx+0].y = -0.0f;
          tcs[tcIdx+1].x =  1.0f; tcs[tcIdx+1].y = -0.0f;
          tcs[tcIdx+2].x =  1.0f; tcs[tcIdx+2].y =  1.0f;
          tcs[tcIdx+3].x = -0.0f; tcs[tcIdx+3].y =  1.0f;
        }        
      }
    }
    
  }

  void ParticlesMeshObject::UpdateColorBuffer ()
  {
    if (!colorBuffer ||
        particleBuffer.particleCount*4 > colorBuffer->GetElementCount ())
    {
      colorBuffer = csRenderBuffer::CreateRenderBuffer (particleBuffer.particleCount*4,
        CS_BUF_STREAM, CS_BUFCOMP_FLOAT, 4);
    }

    csRenderBufferLock<csColor4> bufferLock (colorBuffer);
    csColor4 *color = bufferLock.Lock ();

    for (unsigned int idx = 0; idx < particleBuffer.particleCount; ++idx)
    {
      const unsigned int cIdx = idx*4;

      const csParticleAux& aux = particleBuffer.particleAuxData[idx];

      color[cIdx+0] = aux.color;
      color[cIdx+1] = aux.color;
      color[cIdx+2] = aux.color;
      color[cIdx+3] = aux.color;
    }          
  }

  void ParticlesMeshObject::InvalidateVertexSetup ()
  {
    delete vertexSetup;
    vertexSetup = 0;
  }

  csPtr<iMeshObject> ParticlesMeshObject::Clone ()
  {
    ParticlesMeshObject* newMesh = new ParticlesMeshObject (factory);

    newMesh->flags = flags;
    newMesh->mixMode = mixMode;

    newMesh->minRadius = minRadius;

    newMesh->particleOrientation = particleOrientation;
    newMesh->rotationMode = rotationMode;
    newMesh->sortMode = sortMode;
    newMesh->integrationMode = integrationMode;
    newMesh->commonDirection = commonDirection;
    newMesh->localMode = localMode;
    newMesh->individualSize = individualSize;
    newMesh->particleSize = particleSize;
    newMesh->minBB = minBB;

    newMesh->emitters = emitters;
    newMesh->effectors = effectors;

    return newMesh;
  }

  csRenderMesh** ParticlesMeshObject::GetRenderMeshes (int& num, iRenderView* rview, 
    iMovable* movable, uint32 frustum_mask)
  {
    num = 0;

    if (particleBuffer.particleCount == 0)
      return 0;

    iMaterialWrapper* mater = materialWrapper;
    if (!mater)
    {
      csPrintf ("INTERNAL ERROR: mesh used without material!\n");
      return 0;
    }

    if (mater->IsVisitRequired ()) 
      mater->Visit ();
    
    iCamera* camera = rview->GetCamera ();
    csReversibleTransform obj2world;

    if (transformMode == CS_PARTICLE_LOCAL_MODE)
      obj2world = movable->GetFullTransform ();

    csReversibleTransform obj2cam = camera->GetTransform () / obj2world;

    bool rmCreated;
    csRenderMesh*& mesh = rmHolder.GetUnusedMesh (rmCreated, 
      rview->GetCurrentFrameNumber ());

    int clip_portal, clip_plane, clip_z_plane;
    CS::RenderViewClipper::CalculateClipSettings (rview->GetRenderContext (),
	frustum_mask, clip_portal, clip_plane, clip_z_plane);

    if (rmCreated)
    {
      mesh->buffers.AttachNew (new csRenderBufferHolder);
      mesh->meshtype = CS_MESHTYPE_TRIANGLES;
      mesh->buffers->SetAccessor (renderBufferAccessor, 
        CS_BUFFER_COLOR_MASK | CS_BUFFER_TEXCOORD0_MASK);
    }

    mesh->mixmode = mixMode;
    mesh->clip_plane = clip_plane;
    mesh->clip_portal = clip_portal;
    mesh->clip_z_plane = clip_z_plane;
    mesh->do_mirror = camera->IsMirrored ();
    mesh->indexstart = 0;
    mesh->indexend = (unsigned int)(particleBuffer.particleCount * 6);
    mesh->material = materialWrapper;
    mesh->worldspace_origin = obj2world.GetOrigin (); //@@TODO: use real center
    mesh->geometryInstance = (void*)this;
    mesh->object2world = obj2world;
    mesh->bbox = GetObjectBoundingBox();

    SetupIndexBuffer (mesh->buffers, obj2cam);
    SetupVertexBuffer (mesh->buffers, obj2cam);

    num = 1;
    return &mesh;
  }

  void IntegrateLinear (csParticle& particle, float& radiusSq, float dt)
  {
    particle.position += particle.linearVelocity * dt;

    float currDistSq = particle.position.SquaredNorm ();
    if (currDistSq > radiusSq)
      radiusSq = currDistSq;
  }

  void IntegrateLinearAngular (csParticle& particle, float& radiusSq, float dt)
  {
    particle.position += particle.linearVelocity * dt;

    // Use closed-form quaternion integrator
    float w = particle.angularVelocity.SquaredNorm ();
    if (w != 0)
    {
      w = sqrtf (w);
      float v = dt * 0.5f * w;
      float q = cosf (v);
      float s = sinf (v) / w;

      csVector3 pqr = particle.angularVelocity * s;
      csQuaternion qVel (pqr, 0);
      csQuaternion res = qVel * particle.orientation;
      particle.orientation = res + particle.orientation * q;
    }


    float currDistSq = particle.position.SquaredNorm ();
    if (currDistSq > radiusSq)
      radiusSq = currDistSq;
  }

  CS_IMPLEMENT_STATIC_VAR(GetFGen, csRandomFloatGen, ());
  
  void ParticlesMeshObject::Advance (float dt, float& newRadiusSq)
  {
    totalParticleTime += dt;

    if (externalControl)
      return;

    // Retire old particles
    size_t currentParticleIdx = 0;
    while (currentParticleIdx < particleBuffer.particleCount)
    {
      csParticle &currentParticle = particleBuffer.particleData[currentParticleIdx];

      currentParticle.timeToLive -= dt;
      if (currentParticle.timeToLive < 0)
      {
        //retire particle
        particleBuffer.particleAuxData[currentParticleIdx] = 
          particleBuffer.particleAuxData[--particleBuffer.particleCount];

        currentParticle = 
          particleBuffer.particleData[particleBuffer.particleCount];
        continue;
      }

      currentParticleIdx++;
    }

    // Apply emitters
    size_t totalEmitted = 0;
    csReversibleTransform t = meshWrapper->GetMovable ()->GetFullTransform ();
    csReversibleTransform* tptr = transformMode == CS_PARTICLE_LOCAL_EMITTER ? 
      &t : 0;
    for (size_t idx = 0; idx < emitters.GetSize (); ++idx)
    {
      iParticleEmitter* emitter = emitters[idx];
      size_t numParticles = emitter->ParticlesToEmit (this, dt, totalParticleTime);
      if (numParticles == 0)
        continue;

      ReserveNewParticles (numParticles);
      totalEmitted += numParticles;

      csParticleBuffer tmpBuf;
      tmpBuf.particleCount = numParticles;
      tmpBuf.particleData = particleBuffer.particleData + particleBuffer.particleCount;
      tmpBuf.particleAuxData = particleBuffer.particleAuxData + particleBuffer.particleCount;
      
      emitter->EmitParticles (this, tmpBuf, dt, totalParticleTime, tptr);

      particleBuffer.particleCount += numParticles;
    }

    // Apply effectors
    for (size_t idx = 0; idx < effectors.GetSize (); ++idx)
    {
      iParticleEffector* effector = effectors[idx];
      
      effector->EffectParticles (this, particleBuffer, dt, totalParticleTime);
    }
    
    // Integrate positions
    if (integrationMode == CS_PARTICLE_INTEGRATE_LINEAR)
    {
      for (currentParticleIdx = 0; 
        currentParticleIdx < particleBuffer.particleCount - totalEmitted; 
        ++currentParticleIdx)
      {
        csParticle &currentParticle = 
          particleBuffer.particleData[currentParticleIdx];
        IntegrateLinear (currentParticle, newRadiusSq, dt);
      }

      for (;
        currentParticleIdx < particleBuffer.particleCount; 
        ++currentParticleIdx)
      {
        csParticle &currentParticle = 
          particleBuffer.particleData[currentParticleIdx];
        IntegrateLinear (currentParticle, newRadiusSq, dt * GetFGen ()->Get ());
      }
    }
    else if (integrationMode == CS_PARTICLE_INTEGRATE_BOTH)
    {
      for (currentParticleIdx = 0; 
        currentParticleIdx < particleBuffer.particleCount - totalEmitted; 
        ++currentParticleIdx)
      {
        csParticle &currentParticle = 
          particleBuffer.particleData[currentParticleIdx];
        IntegrateLinearAngular (currentParticle, newRadiusSq, dt);
      }

      for (;
        currentParticleIdx < particleBuffer.particleCount; 
        ++currentParticleIdx)
      {
        csParticle &currentParticle = 
          particleBuffer.particleData[currentParticleIdx];
        IntegrateLinearAngular (currentParticle, newRadiusSq, 
          dt * GetFGen ()->Get ());
      }
    }
  }

  void ParticlesMeshObject::NextFrame (csTicks current_time, const csVector3& pos,
    uint currentFrame)
  {
    // Update the particle buffers etc
    if (lastFrameNumber == currentFrame)
      return;

    if (lastFrameNumber == 0 ||
        lastUpdateTime == current_time)
    {
      lastFrameNumber = currentFrame;
      lastUpdateTime = current_time;
      return; //first update, or been invisible for a while
    }

    lastFrameNumber = currentFrame;
    csTicks currentDt = current_time - lastUpdateTime;
    lastUpdateTime = current_time;

    // Some artificial limiting of dt
    if (currentDt > 500) currentDt = 500;

    // Advance particle system in slices of that duration
    const csTicks advanceSlice = 50;
  
    float newRadius = minRadius;
    while (currentDt > 0)
    {
      csTicks sliceDt = csMin (currentDt, advanceSlice);
      float dt = sliceDt/1000.0f;
      float newRadiusSq = 0;
      Advance (dt, newRadiusSq);
      newRadius = csMax(sqrtf(newRadiusSq), newRadius);
      currentDt -= sliceDt;
    }

    if (newRadius > radius)
    {
      radius = newRadius;
      ShapeChanged ();
    }
  }

  const csBox3&  ParticlesMeshObject::GetObjectBoundingBox ()
  {
    objectBB.SetCenter (csVector3 (0.0f));
    objectBB.SetSize (csVector3 (radius*2));
    objectBB.AddBoundingBox (minBB);
    return objectBB;
  }

  void ParticlesMeshObject::SetObjectBoundingBox (const csBox3& bbox)
  {
    minRadius = bbox.GetSize ().Norm () * 0.5f;
  }

  void ParticlesMeshObject::GetRadius (float& radius, csVector3& center)
  {
    radius = this->radius;
    center.Set (0.0f);
  }

  void ParticlesMeshObject::PreGetBuffer (csRenderBufferHolder* holder, 
    csRenderBufferName buffer)
  {
    switch (buffer)
    {
    case CS_BUFFER_COLOR:
      {
        UpdateColorBuffer ();
        holder->SetRenderBuffer (CS_BUFFER_COLOR, colorBuffer);
      }
      break;
    case CS_BUFFER_TEXCOORD0:
      {
        UpdateTexCoordBuffer ();
        holder->SetRenderBuffer (CS_BUFFER_TEXCOORD0, tcBuffer);
      }
      break;
    default:
      break;
    }
  }

  csParticleBuffer* ParticlesMeshObject::LockForExternalControl (
    size_t maxParticles)
  {
    particleBuffer.particleCount = 0;
    particleBuffer.particleData = 0;
    particleBuffer.particleAuxData = 0;

    particleAllocatedSize = 0;

    delete[] rawBuffer;

    ReserveNewParticles (maxParticles);

    externalControl = true; 

    return &particleBuffer;
  }
  
  void ParticlesMeshObject::Advance (csTicks time)
  {
    // Check that we have a meshwrapper.
    if(!meshWrapper)
    {
      // Delay the advance until we do.
      delayedAdvance += time;
      return;
    }

    // Advance particle system in slices of that duration
    const csTicks advanceSlice = 50;
  
    float newRadius = minRadius;
    while (time > 0)
    {
      csTicks currentDt = csMin (time, advanceSlice);
      float dt = currentDt/1000.0f;
      float newRadiusSq = 0;
      Advance (dt, newRadiusSq);
      newRadius = csMax(sqrtf(newRadiusSq), newRadius);
      time -= currentDt;
    }

    if (newRadius > radius)
    {
      radius = newRadius;
      ShapeChanged ();
    }
  }
}
CS_PLUGIN_NAMESPACE_END(Particles)


