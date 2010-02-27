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

#include "particlesldr.h"

#include "csutil/set.h"

#include "iengine/mesh.h"
#include "imap/ldrctxt.h"
#include "imesh/object.h"
#include "imesh/particles.h"
#include "iutil/document.h"
#include "iutil/plugin.h"
#include "iutil/object.h"
#include "iutil/stringarray.h"



template<>
class csHashComputer<iParticleEmitter*> : public csHashComputerIntegral<iParticleEmitter*> {};
template<>
class csHashComputer<iParticleEffector*> : public csHashComputerIntegral<iParticleEffector*> {};

CS_PLUGIN_NAMESPACE_BEGIN(ParticlesLoader)
{
  SCF_IMPLEMENT_FACTORY(ParticlesFactoryLoader);
  SCF_IMPLEMENT_FACTORY(ParticlesObjectLoader);
  SCF_IMPLEMENT_FACTORY(ParticlesFactorySaver);
  SCF_IMPLEMENT_FACTORY(ParticlesObjectSaver);

  ParticlesBaseLoader::ParticlesBaseLoader (iBase* parent)
    : scfImplementationType (this, parent), objectRegistry (0)
  {
    InitTokenTable (xmltokens);
  }

  bool ParticlesBaseLoader::Initialize (iObjectRegistry* objreg)
  {
    objectRegistry = objreg;
    synldr = csQueryRegistry<iSyntaxService> (objectRegistry);
    return true;
  }

  bool ParticlesBaseLoader::ParseBaseNode (iParticleSystemBase* baseObject, 
    iDocumentNode *node, iLoaderContext* ldr_context, iBase* context)
  {
    if (!node || !baseObject)
      return false;

    if (node->GetType () != CS_NODE_ELEMENT)
      return false;

    const char* value = node->GetValue ();
    csStringID id = xmltokens.Request (value);
    switch (id)
    {
    case XMLTOKEN_RENDERORIENTATION:
      {
        const char* orient = node->GetContentsValue ();
        
        csParticleRenderOrientation r = CS_PARTICLE_CAMERAFACE_APPROX;
        if (!strcasecmp (orient, "camface"))
          r = CS_PARTICLE_CAMERAFACE;
        else if (!strcasecmp (orient, "camface approx"))
          r = CS_PARTICLE_CAMERAFACE_APPROX;
        else if (!strcasecmp (orient, "common"))
          r = CS_PARTICLE_ORIENT_COMMON;
        else if (!strcasecmp (orient, "common approx"))
          r = CS_PARTICLE_ORIENT_COMMON_APPROX;
        else if (!strcasecmp (orient, "velocity"))
          r = CS_PARTICLE_ORIENT_VELOCITY;
        else if (!strcasecmp (orient, "self"))
          r = CS_PARTICLE_ORIENT_SELF;
        else if (!strcasecmp (orient, "self forward"))
          r = CS_PARTICLE_ORIENT_SELF_FORWARD;
        else
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Unknown orientation mode (%s)!", orient);
          return false;
        }

        baseObject->SetParticleRenderOrientation (r);
      }
      break;
    case XMLTOKEN_ROTATIONMODE:
      {
        const char* rotation = node->GetContentsValue ();
        csParticleRotationMode m = CS_PARTICLE_ROTATE_NONE;

        if (!strcasecmp (rotation, "none"))
          m = CS_PARTICLE_ROTATE_NONE;
        else if (!strcasecmp (rotation, "texcoord"))
          m = CS_PARTICLE_ROTATE_TEXCOORD;
        else if (!strcasecmp (rotation, "vertex"))
          m = CS_PARTICLE_ROTATE_VERTICES;
        else
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Unknown rotation mode (%s)!", rotation);
          return false;
        }

        baseObject->SetRotationMode (m);
      }
      break;
    case XMLTOKEN_SORTMODE:
      {
        const char* sortmode = node->GetContentsValue ();
        csParticleSortMode m = CS_PARTICLE_SORT_NONE;
        
        if (!strcasecmp (sortmode, "none"))
          m = CS_PARTICLE_SORT_NONE;
        else if (!strcasecmp (sortmode, "distance"))
          m = CS_PARTICLE_SORT_DISTANCE;
        else if (!strcasecmp (sortmode, "dot"))
          m = CS_PARTICLE_SORT_DOT;
        else
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Unknown sorting mode (%s)!", sortmode);
          return false;
        }

        baseObject->SetSortMode (m);
      }
      break;
    case XMLTOKEN_INTEGRATIONMODE:
      {
        const char* integ = node->GetContentsValue ();
        csParticleIntegrationMode m = CS_PARTICLE_INTEGRATE_LINEAR;

        if (!strcasecmp (integ, "none"))
          m = CS_PARTICLE_INTEGRATE_NONE;
        else if (!strcasecmp (integ, "linear"))
          m = CS_PARTICLE_INTEGRATE_LINEAR;
        else if (!strcasecmp (integ, "both"))
          m = CS_PARTICLE_INTEGRATE_BOTH;
        else
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Unknown integration mode (%s)!", integ);
          return false;
        }

        baseObject->SetIntegrationMode (m);
      }
      break;
    case XMLTOKEN_COMMONDIRECTION:
      {
        csVector3 dir;
        if (!synldr->ParseVector (node, dir))
        {
          return false;
        }
        baseObject->SetCommonDirection (dir);
      }
      break;
    case XMLTOKEN_TRANSFORMMODE:
      {
        const char* transm = node->GetContentsValue ();
        csParticleTransformMode m = CS_PARTICLE_LOCAL_MODE;

        if (!strcasecmp (transm, "local"))
          m = CS_PARTICLE_LOCAL_MODE;
        else if (!strcasecmp (transm, "localemitter"))
          m = CS_PARTICLE_LOCAL_EMITTER;
        else if (!strcasecmp (transm, "world"))
          m = CS_PARTICLE_WORLD_MODE;
        else
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Unknown transform mode (%s)!", transm);
          return false;
        }

        baseObject->SetTransformMode (m);
      }
      break;
    case XMLTOKEN_INDIVIDUALSIZE:
      {
        bool ind;
        if (!synldr->ParseBool (node, ind, true))
        {
          return false;
        }
        baseObject->SetUseIndividualSize (ind);
      }
      break;
    case XMLTOKEN_PARTICLESIZE:
      {
        csVector2 size (1.0f);
        if (!synldr->ParseVector (node, size))
        {
          return false;
        }
        baseObject->SetParticleSize (size);
      }
      break;
    case XMLTOKEN_MINBB:
      {
        csBox3 bb;
        if (!synldr->ParseBox (node, bb))
        {
          return false;
        }
        baseObject->SetMinBoundingBox (bb);
      }
      break;
    case XMLTOKEN_EMITTER:
      {
        csRef<iParticleEmitter> emitter = ParseEmitter (node);

        if (!emitter)
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Error loading emitter!");
          return false;
        }
        baseObject->AddEmitter (emitter);
      }
      break;
    case XMLTOKEN_EFFECTOR:
      {
        csRef<iParticleEffector> effector = ParseEffector (node);

        if (!effector)
        {
          synldr->ReportError ("crystalspace.particleloader.parsebase", node,
            "Error loading effector!");
          return false;
        }
        baseObject->AddEffector (effector);
      }
      break;
    default:
      synldr->ReportBadToken (node);
    }

    return true;
  }

  
  csPtr<iParticleEmitter> ParticlesBaseLoader::ParseEmitter (iDocumentNode* node)
  {
    const char* emitterType = node->GetAttributeValue ("type");
    
    if (!emitterType)
    {
      synldr->ReportError ("crystalspace.particleloader.parseemitter", node,
        "No emitter type specified!");
      return 0;
    }

    csRef<iParticleBuiltinEmitterFactory> factory = 
      csLoadPluginCheck<iParticleBuiltinEmitterFactory> (
        objectRegistry, "crystalspace.mesh.object.particles.emitter", false);
    
    if (!factory)
    {
      synldr->ReportError ("crystalspace.particleloader.parseemitter", node,
        "Could not load particle emitter factory!");
      return 0;
    }

    //properties
    float radius = 1.0f, coneAngle = PI/4;
    csVector3 position (0.0f), extent (0.0f), initialVelocity (0.0f), initialAngVelocity (0.0f);
    bool enabled = true;
    float startTime = 0.0f, duration = FLT_MAX, emissionRate = 0.0f, 
      minTTL = FLT_MAX, maxTTL = FLT_MAX, minMass = 1.0f, maxMass = 1.0f;
    csOBB box;
    csParticleBuiltinEmitterPlacement placement = CS_PARTICLE_BUILTIN_CENTER;
    bool unifromVelocity = false;

    csRef<iDocumentNodeIterator> it = node->GetNodes ();
    while (it->HasNext ())
    {
      csRef<iDocumentNode> child = it->Next ();
      
      if (child->GetType () != CS_NODE_ELEMENT) 
        continue;

      const char* value = child->GetValue ();
      csStringID id = xmltokens.Request (value);
      switch(id)
      {
      case XMLTOKEN_ENABLED:
        {
          if(!synldr->ParseBool (child, enabled, true))
          {
            return 0;
          }
        }
        break;
      case XMLTOKEN_STARTTIME:
        startTime = child->GetContentsValueAsFloat ();
        break;
      case XMLTOKEN_DURATION:
        duration = child->GetContentsValueAsFloat ();
        break;
      case XMLTOKEN_EMISSIONRATE:
        emissionRate = child->GetContentsValueAsFloat ();
        break;
      case XMLTOKEN_INITIALTTL:
        {
          minTTL = child->GetAttributeValueAsFloat ("min");
          maxTTL = child->GetAttributeValueAsFloat ("max");
        }
        break;
      case XMLTOKEN_MASS:
        {
          minMass = child->GetAttributeValueAsFloat ("min");
          maxMass = child->GetAttributeValueAsFloat ("max");
        }
        break;
      case XMLTOKEN_POSITION:
        {
          if (!synldr->ParseVector (child, position))
          {
            synldr->ReportError ("crystalspace.particleloader.parseemitter", 
              child, "Error parsing position!");
          }
        }
        break;
      case XMLTOKEN_PLACEMENT:
        {
          const char* p = child->GetContentsValue ();
          if (!strcasecmp (p, "center"))
            placement = CS_PARTICLE_BUILTIN_CENTER;
          else if (!strcasecmp (p, "volume"))
            placement = CS_PARTICLE_BUILTIN_VOLUME;
          else if (!strcasecmp (p, "surface"))
            placement = CS_PARTICLE_BUILTIN_SURFACE;
          else
          {
            synldr->ReportError ("crystalspace.particleloader.parseemitter", 
              child, "Unknown particle placement mode (%s)!", p);
            return 0;
          }
        }
        break;
      case XMLTOKEN_UNIFORMVELOCITY:
        if (!synldr->ParseBool (child, unifromVelocity, true))
        {
          return 0;
        }       
        break;
      case XMLTOKEN_INITIALVELOCITY:
        if (!synldr->ParseVector (child, initialVelocity))
        {
          return 0;
        }
        break;
      case XMLTOKEN_INITIALANGULARVELOCITY:
        if (!synldr->ParseVector (child, initialAngVelocity))
        {
          return 0;
        }
        break;
      case XMLTOKEN_RADIUS:
        radius = child->GetContentsValueAsFloat ();
        break;
      case XMLTOKEN_EXTENT:
        if (!synldr->ParseVector (child, extent))
        {
          return false;
        }
        break;
      case XMLTOKEN_CONEANGLE:
        coneAngle = child->GetContentsValueAsFloat ();
        break;
      case XMLTOKEN_BOX:
        if (!synldr->ParseBox (child, box))
        {
          return 0;
        }
        break;
      default:
        synldr->ReportBadToken (child );
        return 0;
      }
    }

    csRef<iParticleBuiltinEmitterBase> baseEmitter;

    // Create it..
    if (!strcasecmp (emitterType, "sphere")) 
    {
      csRef<iParticleBuiltinEmitterSphere> sphereEmitter = factory->CreateSphere ();
      baseEmitter = sphereEmitter;
      // Set individual properties
      sphereEmitter->SetRadius (radius);
    }
    else if (!strcasecmp (emitterType, "box"))
    {
      csRef<iParticleBuiltinEmitterBox> boxEmitter = factory->CreateBox ();
      baseEmitter = boxEmitter;
      // Set individual properties
      boxEmitter->SetBox (box);
    }
    else if (!strcasecmp (emitterType, "cylinder"))
    {
      csRef<iParticleBuiltinEmitterCylinder> cylinderEmitter = factory->CreateCylinder ();
      baseEmitter = cylinderEmitter;
      // Set individual properties
      cylinderEmitter->SetRadius (radius);
      cylinderEmitter->SetExtent (extent);
    }
    else if (!strcasecmp (emitterType, "cone"))
    {
      csRef<iParticleBuiltinEmitterCone> coneEmitter = factory->CreateCone ();
      baseEmitter = coneEmitter;
      // Set individual properties
      coneEmitter->SetExtent (extent);
      coneEmitter->SetConeAngle (coneAngle);
    }
    else
    {
      synldr->ReportError ("crystalspace.particleloader.parseemitter", node,
              "Unknown emitter type (%s)!", emitterType);
      return 0;
    }

    if (!baseEmitter)
    {
      return 0;
    }

    // Set base properties
    baseEmitter->SetPosition (position);
    baseEmitter->SetInitialVelocity (initialVelocity, initialAngVelocity);
    baseEmitter->SetUniformVelocity (unifromVelocity);
    baseEmitter->SetParticlePlacement (placement);
    
    // Set common properties
    baseEmitter->SetEnabled (enabled);
    baseEmitter->SetStartTime (startTime);
    baseEmitter->SetDuration (duration);
    baseEmitter->SetEmissionRate (emissionRate);
    baseEmitter->SetInitialTTL (minTTL, maxTTL);
    baseEmitter->SetInitialMass (minMass, maxMass);

    return csPtr<iParticleEmitter> (baseEmitter);
  }

  bool ParticlesBaseLoader::ParseLinearEffectorParameters (
      iDocumentNode* child, csParticleParameterSet& param, int& mask)
  {
    param.Clear ();
    csRef<iDocumentNodeIterator> cit = child->GetNodes ();
    while (cit->HasNext ())
    {
      csRef<iDocumentNode> c = cit->Next ();
      if (c->GetType () != CS_NODE_ELEMENT) continue;
      const char* v = c->GetValue ();
      csStringID cid = xmltokens.Request (v);
      switch (cid)
      {
	case XMLTOKEN_COLOR:
	  if (!synldr->ParseColor (c, param.color))
	  {
	    synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
		"Error parsing color!");
	    return false;
	  }
	  mask |= CS_PARTICLE_MASK_COLOR;
	  break;
	case XMLTOKEN_MASS:
	  param.mass = c->GetContentsValueAsFloat ();
	  mask |= CS_PARTICLE_MASK_MASS;
	  break;
	case XMLTOKEN_ANGULARVELOCITY:
	  if (!synldr->ParseVector (c, param.angularVelocity))
	  {
	    synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
		"Error parsing angular velocity!");
	    return false;
	  }
	  mask |= CS_PARTICLE_MASK_ANGULARVELOCITY;
	  break;
	case XMLTOKEN_LINEARVELOCITY:
	  if (!synldr->ParseVector (c, param.linearVelocity))
	  {
	    synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
		"Error parsing linear velocity!");
	    return false;
	  }
	  mask |= CS_PARTICLE_MASK_LINEARVELOCITY;
	  break;
	case XMLTOKEN_PARTICLESIZE:
	  if (!synldr->ParseVector (c, param.particleSize))
	  {
	    synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
		"Error parsing particle size!");
	    return false;
	  }
	  mask |= CS_PARTICLE_MASK_PARTICLESIZE;
	  break;
	default:
	  synldr->ReportBadToken (c);
	  return false;
      }
    }
    return true;
  }


  csPtr<iParticleEffector> ParticlesBaseLoader::ParseEffector (
    iDocumentNode* node)
  {
    const char* effectorType = node->GetAttributeValue ("type");

    if (!effectorType)
    {
      synldr->ReportError ("crystalspace.particleloader.parseeffector", node,
        "No effector type specified!");
      return 0;
    }

    csRef<iParticleBuiltinEffectorFactory> factory = 
      csLoadPluginCheck<iParticleBuiltinEffectorFactory> (
      objectRegistry, "crystalspace.mesh.object.particles.effector", false);

    if (!factory)
    {
      synldr->ReportError ("crystalspace.particleloader.parseeffector", node,
        "Could not load particle effector factory!");
      return 0;
    }

    csRef<iParticleEffector> effector;

    if (!strcasecmp (effectorType, "force"))
    {
      csRef<iParticleBuiltinEffectorForce> forceEffector = factory->CreateForce ();
      effector = forceEffector;

      csRef<iDocumentNodeIterator> it = node->GetNodes ();
      while (it->HasNext ())
      {
        csRef<iDocumentNode> child = it->Next ();

        if (child->GetType () != CS_NODE_ELEMENT) 
          continue;

        const char* value = child->GetValue ();
        csStringID id = xmltokens.Request (value);
        switch(id)
        {
        case XMLTOKEN_ACCELERATION:
          {
            csVector3 acceleration;
            if (!synldr->ParseVector (child, acceleration))
            {
              synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
                "Error parsing acceleration!");
            }

            forceEffector->SetAcceleration (acceleration);
          }
          break;
        case XMLTOKEN_FORCE:
          {
            csVector3 force;
            if (!synldr->ParseVector (child, force))
            {
              synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
                "Error parsing force!");
            }
            forceEffector->SetForce (force);
          }
          break;
        case XMLTOKEN_RANDOMACCELERATION:
          {
            csVector3 randomAcc;
            csRef<iDocumentAttribute> attr = child->GetAttribute ("x");
            if (attr)
            {
              if (!synldr->ParseVector (child, randomAcc))
              {
                synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
                  "Error parsing randomacceleration!");
              }
            }
            else
            {
              float r = child->GetContentsValueAsFloat ();
              randomAcc.Set (r, r, r);
            }

            forceEffector->SetRandomAcceleration (randomAcc);
          }
          break;
        default:
          synldr->ReportBadToken (child); 
          return 0;
        }
      }
    }
    else if (!strcasecmp (effectorType, "linear"))
    {
      csRef<iParticleBuiltinEffectorLinear> linEffector = 
        factory->CreateLinear ();
      effector = linEffector;

      int mask = 0;
      csRef<iDocumentNodeIterator> it = node->GetNodes ();
      while (it->HasNext ())
      {
        csRef<iDocumentNode> child = it->Next ();

        if (child->GetType () != CS_NODE_ELEMENT) 
          continue;

        const char* value = child->GetValue ();
        csStringID id = xmltokens.Request (value);
        switch(id)
        {
        case XMLTOKEN_PARAM:
          {
	    csParticleParameterSet param;
	    if (!ParseLinearEffectorParameters (child, param, mask))
	      return 0;

            float t (0.0f);
            // 'Old' time attribute
            t = child->GetAttributeValueAsFloat ("time");
            // Attribute mnemonic for 'remaining time'
            csRef<iDocumentAttribute> attr = child->GetAttribute ("rtime");
            if (attr) t = attr->GetValueAsFloat ();
            linEffector->AddParameterSet (param, t);
          }
          break;
        default:
          synldr->ReportBadToken (child);
          return 0;
        }
      }
      linEffector->SetMask (mask);

    }
    else if (!strcasecmp (effectorType, "lincolor"))
    {
      csRef<iParticleBuiltinEffectorLinColor> colorEffector = 
        factory->CreateLinColor ();
      effector = colorEffector;

      csRef<iDocumentNodeIterator> it = node->GetNodes ();
      while (it->HasNext ())
      {
        csRef<iDocumentNode> child = it->Next ();

        if (child->GetType () != CS_NODE_ELEMENT) 
          continue;

        const char* value = child->GetValue ();
        csStringID id = xmltokens.Request (value);
        switch(id)
        {
        case XMLTOKEN_COLOR:
          {
            csColor4 c;

            if (!synldr->ParseColor (child, c))
            {
              synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
                "Error parsing color!");
            }

            float t (0.0f);
            // 'Old' time attribute
            t = child->GetAttributeValueAsFloat ("time");
            // Attribute mnemonic for 'remaining time'
            csRef<iDocumentAttribute> attr = child->GetAttribute ("rtime");
            if (attr) t = attr->GetValueAsFloat ();
            colorEffector->AddColor (c, t);
          }
          break;
        default:
          synldr->ReportBadToken (child);
          return 0;
        }
      }

    }
    else if (!strcasecmp (effectorType, "velocityfield"))
    {
      csRef<iParticleBuiltinEffectorVelocityField> vfEffector = 
        factory->CreateVelocityField ();
      effector = vfEffector;
      
      size_t vparamID = 0, fparamID = 0;

      csRef<iDocumentNodeIterator> it = node->GetNodes ();
      while (it->HasNext ())
      {
        csRef<iDocumentNode> child = it->Next ();

        if (child->GetType () != CS_NODE_ELEMENT) 
          continue;

        const char* value = child->GetValue ();
        csStringID id = xmltokens.Request (value);
        switch(id)
        {
        case XMLTOKEN_TYPE:
          {
            const char* type = child->GetContentsValue ();

            if (!strcasecmp (type, "spiral"))
            {
              vfEffector->SetType (CS_PARTICLE_BUILTIN_SPIRAL);
            }
            else if (!strcasecmp (type, "radialpoint"))
            {
              vfEffector->SetType (CS_PARTICLE_BUILTIN_RADIALPOINT);
            }
            else
            {
              synldr->ReportError ("crystalspace.particleloader.parseeffector", node,
                "Unknown force field type (%s)!", type);
            }
          }
          break;
        case XMLTOKEN_VPARAM:
          {
            csVector3 v;

            if (!synldr->ParseVector (child, v))
            {
              synldr->ReportError ("crystalspace.particleloader.parseeffector", child,
                "Error parsing vparam!");
            }
            vfEffector->SetVParameter (vparamID++, v);
          }
          break;
        case XMLTOKEN_FPARAM:
          {
            float f = child->GetContentsValueAsFloat ();

            vfEffector->SetFParameter (fparamID++, f);
          }
          break;
        default:
          synldr->ReportBadToken (child);
          return 0;
        }
      }

      
    }
    else
    {
      synldr->ReportError ("crystalspace.particleloader.parseeffector", node,
        "Unknown effector type (%s)!", effectorType);
      return 0;
    }

    if (!effector)
    {
      return 0;
    }


    return csPtr<iParticleEffector> (effector);
  }



  csPtr<iBase> ParticlesFactoryLoader::Parse (iDocumentNode* node,
    iStreamSource* ssource, iLoaderContext* ldr_context, iBase* context)
  {
    csRef<iMeshObjectType> type = csLoadPluginCheck<iMeshObjectType> (
  	objectRegistry, "crystalspace.mesh.object.particles", false);
    if (!type)
    {
      synldr->ReportError (
		  "crystalspace.particleloader.parsefactory",
		  node, "Could not load the particles mesh object plugin!");
      return 0;
    }

    csRef<iMeshObjectFactory> factoryObj = type->NewFactory ();
    csRef<iParticleSystemFactory> particleFact = 
      scfQueryInterfaceSafe<iParticleSystemFactory> (factoryObj);

    csRef<iDocumentNodeIterator> it = node->GetNodes ();
    while (it->HasNext ())
    {
      csRef<iDocumentNode> child = it->Next ();
      if (child->GetType () != CS_NODE_ELEMENT) continue;
      const char* value = child->GetValue ();
      csStringID id = xmltokens.Request (value);
      switch(id)
      {
      case XMLTOKEN_DEEPCREATION:
        {
          bool deep = false;
          if (!synldr->ParseBool (child, deep, true))
          {
            synldr->ReportError ("crystalspace.particleloader.parsefactory",
              node, "Could not parse factory!");
            return 0;
          }
        }
        break;
      case XMLTOKEN_MATERIAL:
        {
          const char* matname = child->GetContentsValue ();
          iMaterialWrapper* mat = ldr_context->FindMaterial (matname);
          if (!mat)
          {
            synldr->ReportError (
              "crystalspace.genmeshfactoryloader.parse.unknownmaterial",
              child, "Couldn't find material '%s'!", matname);
            return 0;
          }
          factoryObj->SetMaterialWrapper (mat);
        }
        break;
      case XMLTOKEN_MIXMODE:
        {
          uint mm;
          if (!synldr->ParseMixmode (child, mm))
            return 0;
          factoryObj->SetMixMode (mm);
        }
        break;
      default:
        {
          if (!ParseBaseNode (particleFact, child, ldr_context, context))
          {
            synldr->ReportError ("crystalspace.particleloader.parsefactory",
              node, "Could not parse factory!");
            return 0;
          }
        }
      }
    }

    return csPtr<iBase> (factoryObj);
  }

  csPtr<iBase> ParticlesObjectLoader::Parse (iDocumentNode* node,
    iStreamSource* ssource, iLoaderContext* ldr_context, iBase* context)
  {
    
    csRef<iMeshObject> meshObj;
    csRef<iParticleSystem> particleSystem;
    float preAdvanceTime = 0.0f;

    csRef<iDocumentNodeIterator> it = node->GetNodes ();
    while (it->HasNext ())
    {
      csRef<iDocumentNode> child = it->Next ();
      if (child->GetType () != CS_NODE_ELEMENT) continue;
      const char* value = child->GetValue ();
      csStringID id = xmltokens.Request (value);
      switch(id)
      {
      case XMLTOKEN_FACTORY:
        {
          const char* factname = child->GetContentsValue ();
          iMeshFactoryWrapper* fact = ldr_context->FindMeshFactory (factname);

          if(!fact)
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              child, "Could not find factory '%s'!", factname);
            return 0;
          }

          meshObj = fact->GetMeshObjectFactory ()->NewInstance ();
          particleSystem = scfQueryInterface<iParticleSystem> (meshObj);

          if (!particleSystem)
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              child, "Factory '%s' does not seem to be a particle system factory!", 
              factname);

            return 0;
          }
          meshObj->SetMaterialWrapper(fact->GetMeshObjectFactory ()
	  	->GetMaterialWrapper ());
          meshObj->SetMixMode(fact->GetMeshObjectFactory()->GetMixMode());
        }
        break;
      case XMLTOKEN_MATERIAL:
        {
          if (!particleSystem)
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              child, "Specify factory first!");

            return 0;
          }

          const char* matname = child->GetContentsValue ();
          iMaterialWrapper* mat = ldr_context->FindMaterial (matname);
          if (!mat)
          {
            synldr->ReportError (
              "crystalspace.genmeshfactoryloader.parse.unknownmaterial",
              child, "Couldn't find material '%s'!", matname);
            return 0;
          }
          meshObj->SetMaterialWrapper (mat);
        }
        break;
      case XMLTOKEN_MIXMODE:
        {
          if (!particleSystem)
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              child, "Specify factory first!");

            return 0;
          }

          uint mm;
          if (!synldr->ParseMixmode (child, mm))
            return 0;
          meshObj->SetMixMode (mm);
        }
        break;
      case XMLTOKEN_PREADVANCE:
        {
          if (!particleSystem)
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              child, "Specify factory first!");

            return 0;
          }

          preAdvanceTime = child->GetContentsValueAsFloat ();
          break;
        }
      default:
        {
          if (!particleSystem)
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              child, "Specify factory first!");

            return 0;
          }

          if (!ParseBaseNode (particleSystem, child, ldr_context, context))
          {
            synldr->ReportError ("crystalspace.particleloader.parsesystem",
              node, "Could not parse particle system!");
            return 0;
          }
        }
      }
    }

    if (preAdvanceTime > 0)
    {
      csTicks msec = (csTicks)(preAdvanceTime*1000.0f + 0.5f);
      particleSystem->Advance (msec);
    }
    
    return csPtr<iBase> (meshObj);
  }


  ParticlesBaseSaver::ParticlesBaseSaver (iBase* p)
    : scfImplementationType (this, p), objectRegistry (0)
  {
  }

  bool ParticlesBaseSaver::Initialize (iObjectRegistry* objreg)
  {
    objectRegistry = objreg;
    synldr = csQueryRegistry<iSyntaxService> (objectRegistry);
    return true;
  }

  bool ParticlesBaseSaver::WriteOrientation(iDocumentNode *paramsNode, 
    csParticleRenderOrientation orientation)
  {
    csRef<iDocumentNode> orientationNode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    orientationNode->SetValue ("renderorientation");
    csRef<iDocumentNode> valueNode = orientationNode->CreateNodeBefore (CS_NODE_TEXT, 0);

    switch (orientation)
    {
    case CS_PARTICLE_CAMERAFACE:
      valueNode->SetValue ("camface");
      break;
    case CS_PARTICLE_CAMERAFACE_APPROX:
      valueNode->SetValue ("camface approx");
      break;
    case CS_PARTICLE_ORIENT_COMMON:
      valueNode->SetValue ("common");
      break;
    case CS_PARTICLE_ORIENT_COMMON_APPROX:
      valueNode->SetValue ("common approx");
      break;
    case CS_PARTICLE_ORIENT_VELOCITY:
      valueNode->SetValue ("velocity");
      break;
    case CS_PARTICLE_ORIENT_SELF:
      valueNode->SetValue ("self");
      break;
    case CS_PARTICLE_ORIENT_SELF_FORWARD:
      valueNode->SetValue ("self forward");
      break;
    default:
      valueNode->SetValue ("camface");
    }

    return true;
  }

  bool ParticlesBaseSaver::WriteRotation (iDocumentNode* paramsNode,
    csParticleRotationMode rot)
  {
    csRef<iDocumentNode> rotationNode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    rotationNode->SetValue ("rotationmode");
    csRef<iDocumentNode> valueNode = rotationNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    
    switch (rot)
    {
    case CS_PARTICLE_ROTATE_NONE:
      valueNode->SetValue ("none");
      break;
    case CS_PARTICLE_ROTATE_TEXCOORD:
      valueNode->SetValue ("texcoord");
      break;
    case CS_PARTICLE_ROTATE_VERTICES:
      valueNode->SetValue ("vertex");
      break;
    default:
      valueNode->SetValue ("none");
    }

    return true;
  }
  
  bool ParticlesBaseSaver::WriteSort(iDocumentNode *paramsNode, csParticleSortMode sort)
  {
    csRef<iDocumentNode> sortNode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    sortNode->SetValue ("sortmode");
    csRef<iDocumentNode> valueNode = sortNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    
    switch (sort)
    {
    case CS_PARTICLE_SORT_NONE:
      valueNode->SetValue ("none");
      break;
    case CS_PARTICLE_SORT_DISTANCE:
      valueNode->SetValue ("distance");
      break;
    case CS_PARTICLE_SORT_DOT:
      valueNode->SetValue ("dot");
      break;
    default:
      valueNode->SetValue ("none");
    }    

    return true;
  }

  bool ParticlesBaseSaver::WriteIntegration(iDocumentNode *paramsNode, 
    csParticleIntegrationMode integ)
  {
    csRef<iDocumentNode> integNode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    integNode->SetValue ("integrationmode");
    csRef<iDocumentNode> valueNode = integNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    
    switch (integ)
    {
    case CS_PARTICLE_INTEGRATE_NONE:
      valueNode->SetValue ("none");
      break;
    case CS_PARTICLE_INTEGRATE_LINEAR:
      valueNode->SetValue ("linear");
      break;
    case CS_PARTICLE_INTEGRATE_BOTH:
      valueNode->SetValue ("both");
      break;
    default:
      valueNode->SetValue ("linear");
    }

    return true;
  }

  bool ParticlesBaseSaver::WriteTransform(iDocumentNode *paramsNode, 
    csParticleTransformMode mode)
  {
    csRef<iDocumentNode> transMode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    transMode->SetValue ("transformmode");
    csRef<iDocumentNode> valueNode = transMode->CreateNodeBefore (CS_NODE_TEXT, 0);

    switch (mode)
    {
    case CS_PARTICLE_LOCAL_MODE:
      valueNode->SetValue ("local");
      break;
    case CS_PARTICLE_LOCAL_EMITTER:
      valueNode->SetValue ("localemitter");
      break;
    case CS_PARTICLE_WORLD_MODE:
      valueNode->SetValue ("world");
      break;
    default:
      valueNode->SetValue ("local");
    }

    return true;
  }

  bool ParticlesBaseSaver::WriteEmitter(iDocumentNode *paramsNode, 
    iParticleEmitter *emitter)
  {
    //Try to determine emitter type
    csRef<iParticleBuiltinEmitterBase> emitterBase = 
      scfQueryInterfaceSafe<iParticleBuiltinEmitterBase> (emitter);

    if (!emitterBase)
      return false;

    csRef<iDocumentNode> valueNode;

    //Write base properties
    csRef<iDocumentNode> emitterNode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    emitterNode->SetValue ("emitter");

    //Enabled
    synldr->WriteBool (emitterNode, "enabled", emitterBase->GetEnabled ());

    //Start time
    csRef<iDocumentNode> startNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    startNode->SetValue ("starttime");
    valueNode = startNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    valueNode->SetValueAsFloat (emitterBase->GetStartTime ());

    //Duration
    csRef<iDocumentNode> durationNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    durationNode->SetValue ("duration");
    valueNode = durationNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    valueNode->SetValueAsFloat (emitterBase->GetDuration ());

    //Emission rate
    csRef<iDocumentNode> rateNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    rateNode->SetValue ("emissionrate");
    valueNode = rateNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    valueNode->SetValueAsFloat (emitterBase->GetEmissionRate ());

    float tmp0, tmp1;

    //TTL
    emitterBase->GetInitialTTL (tmp0, tmp1);
    csRef<iDocumentNode> ttlNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    ttlNode->SetValue ("initialttl");
    ttlNode->SetAttributeAsFloat ("min", tmp0);
    ttlNode->SetAttributeAsFloat ("max", tmp1);

    //Mass
    emitterBase->GetInitialMass (tmp0, tmp1);
    csRef<iDocumentNode> massNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    massNode->SetValue ("mass");
    massNode->SetAttributeAsFloat ("min", tmp0);
    massNode->SetAttributeAsFloat ("max", tmp1);


    //Position
    csRef<iDocumentNode> posNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    posNode->SetValue ("position");
    synldr->WriteVector (posNode, emitterBase->GetPosition ());

    //Placement
    csParticleBuiltinEmitterPlacement place = emitterBase->GetParticlePlacement ();
    csRef<iDocumentNode> placeNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    placeNode->SetValue ("placement");
    valueNode = placeNode->CreateNodeBefore (CS_NODE_TEXT, 0);
    switch (place)
    {
    case CS_PARTICLE_BUILTIN_CENTER:
      valueNode->SetValue ("center");
      break;
    case CS_PARTICLE_BUILTIN_VOLUME:
      valueNode->SetValue ("volume");
      break;
    case CS_PARTICLE_BUILTIN_SURFACE:
      valueNode->SetValue ("surface");
      break;
    default:
      valueNode->SetValue ("volume");
    }

    //Uniform velocity
    synldr->WriteBool (emitterNode, "uniformvelocity", 
      emitterBase->GetUniformVelocity ());

    //Initial velocity
    csRef<iDocumentNode> velNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    velNode->SetValue ("initialvelocity");
    csVector3 lin, ang;
    emitterBase->GetInitialVelocity (lin, ang);
    synldr->WriteVector (velNode, lin);

    // Initial angular velocity
    csRef<iDocumentNode> angVelNode = emitterNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    angVelNode->SetValue ("initialangularvelocity");
    synldr->WriteVector (angVelNode, ang);

    //Write specific properties
    csRef<iParticleBuiltinEmitterSphere> sphereEmit = 
      scfQueryInterface<iParticleBuiltinEmitterSphere> (emitterBase);

    if (sphereEmit)
    {
      emitterNode->SetAttribute ("type", "sphere");

      csRef<iDocumentNode> radiusNode = emitterNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      radiusNode->SetValue ("radius");
      valueNode = radiusNode->CreateNodeBefore (CS_NODE_TEXT, 0);
      valueNode->SetValueAsFloat (sphereEmit->GetRadius ());

      return true;
    }

    csRef<iParticleBuiltinEmitterCone> coneEmit = 
      scfQueryInterface<iParticleBuiltinEmitterCone> (emitterBase);

    if (coneEmit)
    {
      emitterNode->SetAttribute ("type", "cone");

      csRef<iDocumentNode> coneAngNode = emitterNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      coneAngNode->SetValue ("coneangle");
      valueNode = coneAngNode->CreateNodeBefore (CS_NODE_TEXT, 0);
      valueNode->SetValueAsFloat (coneEmit->GetConeAngle ());

      csRef<iDocumentNode> extentNode = emitterNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      extentNode->SetValue ("extent");
      synldr->WriteVector (extentNode, coneEmit->GetExtent ());

      return true;
    }

    csRef<iParticleBuiltinEmitterBox> boxEmit = 
      scfQueryInterface<iParticleBuiltinEmitterBox> (emitterBase);

    if (boxEmit)
    {
      emitterNode->SetAttribute ("type", "box");

      csRef<iDocumentNode> boxNode = emitterNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      boxNode->SetValue ("box");
      synldr->WriteBox (boxNode, boxEmit->GetBox ());

      return true;
    }

    csRef<iParticleBuiltinEmitterCylinder> cylEmit = 
      scfQueryInterface<iParticleBuiltinEmitterCylinder> (emitterBase);

    if (cylEmit)
    {
      emitterNode->SetAttribute ("type", "sphere");

      csRef<iDocumentNode> radiusNode = emitterNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      radiusNode->SetValue ("radius");
      valueNode = radiusNode->CreateNodeBefore (CS_NODE_TEXT, 0);
      valueNode->SetValueAsFloat (cylEmit->GetRadius ());

      csRef<iDocumentNode> extentNode = emitterNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      extentNode->SetValue ("extent");
      synldr->WriteVector (extentNode, cylEmit->GetExtent ());
  
      return true;
    }

    return true;
  }

  bool ParticlesBaseSaver::WriteEffector (iDocumentNode* paramsNode, 
    iParticleEffector* effector)
  {
    if (!effector)
      return false;

    csRef<iDocumentNode> valueNode;

    //Write base properties
    csRef<iDocumentNode> effectorNode = paramsNode->CreateNodeBefore (
      CS_NODE_ELEMENT, 0);
    effectorNode->SetValue ("effector");

    csRef<iParticleBuiltinEffectorForce> forceEffector = 
      scfQueryInterface<iParticleBuiltinEffectorForce> (effector);

    if (forceEffector)
    {
      effectorNode->SetAttribute ("type", "force");

      csRef<iDocumentNode> accNode = effectorNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      accNode->SetValue ("acceleration");
      synldr->WriteVector (accNode, forceEffector->GetAcceleration ());

      csRef<iDocumentNode> forceNode = effectorNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      forceNode->SetValue ("force");
      synldr->WriteVector (forceNode, forceEffector->GetForce ());

      return true;
    }

    csRef<iParticleBuiltinEffectorLinColor> colorEffector =
      scfQueryInterface<iParticleBuiltinEffectorLinColor> (effector);

    if (colorEffector)
    {
      effectorNode->SetAttribute ("type", "lincolor");

      size_t numColors = colorEffector->GetColorCount ();
      csColor4 c; float t;
      for (size_t i = 0; i < numColors; ++i)
      {
        colorEffector->GetColor (i, c, t);
        csRef<iDocumentNode> colorNode = effectorNode->CreateNodeBefore (
          CS_NODE_ELEMENT, 0);
        colorNode->SetValue ("color");

        synldr->WriteColor (colorNode, c);
        colorNode->SetAttributeAsFloat ("rtime", t);
      }

      return true;
    }

    csRef<iParticleBuiltinEffectorLinear> linEffector =
      scfQueryInterface<iParticleBuiltinEffectorLinear> (effector);

    if (linEffector)
    {
      effectorNode->SetAttribute ("type", "linear");

      int mask = linEffector->GetMask ();
      size_t numParams = linEffector->GetParameterSetCount ();
      csParticleParameterSet c; float t;
      for (size_t i = 0; i < numParams; ++i)
      {
        linEffector->GetParameterSet (i, c, t);
        csRef<iDocumentNode> paramNode = effectorNode->CreateNodeBefore (CS_NODE_ELEMENT, 0);
        paramNode->SetValue ("param");
        paramNode->SetAttributeAsFloat ("rtime", t);

	if (mask & CS_PARTICLE_MASK_COLOR)
	{
          csRef<iDocumentNode> colorNode = paramNode->CreateNodeBefore (CS_NODE_ELEMENT, 0);
          colorNode->SetValue ("color");
          synldr->WriteColor (colorNode, c.color);
	}
	if (mask & CS_PARTICLE_MASK_MASS)
	{
          csRef<iDocumentNode> massNode = paramNode->CreateNodeBefore (CS_NODE_ELEMENT, 0);
          massNode->SetValue ("mass");
	  massNode->CreateNodeBefore (CS_NODE_TEXT)->SetValueAsFloat (c.mass);
	}
	if (mask & CS_PARTICLE_MASK_ANGULARVELOCITY)
	{
          csRef<iDocumentNode> velNode = paramNode->CreateNodeBefore (CS_NODE_ELEMENT, 0);
          velNode->SetValue ("angularvelocity");
          synldr->WriteVector (velNode, c.angularVelocity);
	}
	if (mask & CS_PARTICLE_MASK_LINEARVELOCITY)
	{
          csRef<iDocumentNode> velNode = paramNode->CreateNodeBefore (CS_NODE_ELEMENT, 0);
          velNode->SetValue ("linearvelocity");
          synldr->WriteVector (velNode, c.linearVelocity);
	}
	if (mask & CS_PARTICLE_MASK_PARTICLESIZE)
	{
          csRef<iDocumentNode> sizeNode = paramNode->CreateNodeBefore (CS_NODE_ELEMENT, 0);
          sizeNode->SetValue ("particlesize");
          synldr->WriteVector (sizeNode, c.particleSize);
	}
      }

      return true;
    }

    csRef<iParticleBuiltinEffectorVelocityField> vfEffector =
      scfQueryInterface<iParticleBuiltinEffectorVelocityField> (effector);

    if (vfEffector)
    {
      effectorNode->SetAttribute ("type", "velocityfield");

      csRef<iDocumentNode> typeNode = effectorNode->CreateNodeBefore (
        CS_NODE_ELEMENT);
      typeNode->SetValue ("type");
      switch (vfEffector->GetType ())
      {
      case CS_PARTICLE_BUILTIN_SPIRAL:
        typeNode->CreateNodeBefore (CS_NODE_TEXT)->SetValue ("spiral");
        break;
      case CS_PARTICLE_BUILTIN_RADIALPOINT:
        typeNode->CreateNodeBefore (CS_NODE_TEXT)->SetValue ("radialpoint");
        break;
      default:
        typeNode->CreateNodeBefore (CS_NODE_TEXT)->SetValue ("spiral");
      }

      for (size_t i = 0; i < vfEffector->GetFParameterCount (); ++i)
      {
        float value = vfEffector->GetFParameter (i);
        csRef<iDocumentNode> fvNode = effectorNode->CreateNodeBefore (
          CS_NODE_ELEMENT);

        fvNode->SetValue ("fparam");
        fvNode->CreateNodeBefore (CS_NODE_TEXT)->SetValueAsFloat (value);
      }

      for (size_t i = 0; i < vfEffector->GetVParameterCount (); ++i)
      {
        csVector3 value = vfEffector->GetVParameter (i);
        csRef<iDocumentNode> vvNode = effectorNode->CreateNodeBefore (
          CS_NODE_ELEMENT);

        vvNode->SetValue ("vparam");
        synldr->WriteVector (vvNode, value);
      }


      return true;
    }

    return false;
  }



  bool ParticlesFactorySaver::WriteDown (iBase* obj, iDocumentNode* parent, 
    iStreamSource* ssource)
  {
    if (!parent)
      return false;

    csRef<iDocumentNode> paramsNode = 
      parent->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    paramsNode->SetValue("params");

    if (obj)
    {
      csRef<iParticleSystemFactory> partFact = 
        scfQueryInterface<iParticleSystemFactory> (obj);
      csRef<iMeshObjectFactory> meshFact =
        scfQueryInterface<iMeshObjectFactory> (obj);
      if (!partFact || !meshFact)
        return false;

      // Deep creation mode flag
      synldr->WriteBool (paramsNode, "deepcreation", 
        partFact->GetDeepCreation ());

      //Write orientation
      WriteOrientation (paramsNode, partFact->GetParticleRenderOrientation ());
      
      //Rotation mode
      WriteRotation (paramsNode, partFact->GetRotationMode ());
      
      //Sorting mode
      WriteSort (paramsNode, partFact->GetSortMode ());

      //Integration mode
      WriteIntegration (paramsNode, partFact->GetIntegrationMode ());

      //Transform mode
      WriteTransform (paramsNode, partFact->GetTransformMode ());

      //Common direction
      csRef<iDocumentNode> comdirNode = paramsNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      comdirNode->SetValue ("commondirection");
      synldr->WriteVector (comdirNode, partFact->GetCommonDirection ());

      //Individual size
      synldr->WriteBool (paramsNode, "individualsize", 
        partFact->GetUseIndividualSize ());

      //Particle size
      csRef<iDocumentNode> sizeNode = paramsNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      sizeNode->SetValue ("particlesize");
      synldr->WriteVector (sizeNode, partFact->GetParticleSize ());

      csRef<iDocumentNode> mixmodeNode = paramsNode->CreateNodeBefore (
        CS_NODE_ELEMENT, 0);
      mixmodeNode->SetValue ("mixmode");
      synldr->WriteMixmode (mixmodeNode, meshFact->GetMixMode (),
	  true);

      // Write emitters
      for (size_t i = 0; i < partFact->GetEmitterCount (); i++)
      {
        WriteEmitter (paramsNode, partFact->GetEmitter (i));
      }

      // Write effectors
      for (size_t i = 0; i < partFact->GetEffectorCount (); i++)
      {
        WriteEffector (paramsNode, partFact->GetEffector (i));
      }

    }

    return true;
  }


  bool ParticlesObjectSaver::WriteDown (iBase* obj, iDocumentNode* parent, 
    iStreamSource* ssource)
  {
    if (!parent)
      return false;

    csRef<iDocumentNode> paramsNode = 
      parent->CreateNodeBefore(CS_NODE_ELEMENT, 0);
    paramsNode->SetValue("params");

    if (obj)
    {
      csRef<iParticleSystem> partObj = 
        scfQueryInterface<iParticleSystem> (obj);
      csRef<iMeshObject> meshObj =
        scfQueryInterface<iMeshObject> (obj);
      if (!partObj || !meshObj)
        return false;

      //Factory name
      iMeshFactoryWrapper* factWrap = meshObj->GetFactory ()->GetMeshFactoryWrapper ();
      if (factWrap)
      {
        const char* factName = factWrap->QueryObject ()->GetName ();
        if (factName && *factName)
        {
          csRef<iDocumentNode> factNode = 
            paramsNode->CreateNodeBefore(CS_NODE_ELEMENT, 0);
          factNode->SetValue("factory");
          factNode->CreateNodeBefore(CS_NODE_TEXT, 0)->SetValue(factName);
        }
      }

      csRef<iParticleSystemFactory> factObj = 
        scfQueryInterface<iParticleSystemFactory> (factWrap->GetMeshObjectFactory ());

      
      //Write orientation
      if (factObj->GetParticleRenderOrientation () != 
        partObj->GetParticleRenderOrientation ())
        WriteOrientation (paramsNode, partObj->GetParticleRenderOrientation ());

      //Rotation mode
      if (factObj->GetRotationMode () != 
        partObj->GetRotationMode ())
        WriteRotation (paramsNode, partObj->GetRotationMode ());

      //Sorting mode
      if (factObj->GetSortMode () != partObj->GetSortMode ())
        WriteSort (paramsNode, partObj->GetSortMode ());

      //Integration mode
      if (factObj->GetIntegrationMode () != partObj->GetIntegrationMode ())
        WriteIntegration (paramsNode, partObj->GetIntegrationMode ());

      //Transform mode
      if (factObj->GetTransformMode () != partObj->GetTransformMode ())
        WriteTransform (paramsNode, partObj->GetTransformMode ());

      //Common direction
      csVector3 commonDir = partObj->GetCommonDirection ();

      if (commonDir != factObj->GetCommonDirection ())
      {
        csRef<iDocumentNode> comdirNode = paramsNode->CreateNodeBefore (
          CS_NODE_ELEMENT, 0);
        comdirNode->SetValue ("commondirection");
        synldr->WriteVector (comdirNode, commonDir);
      }

      //Individual size
      if (factObj->GetUseIndividualSize () != partObj->GetUseIndividualSize ())
        synldr->WriteBool (paramsNode, "individualsize", 
          partObj->GetUseIndividualSize ());

      //Particle size
      if (factObj->GetParticleSize () != partObj->GetParticleSize ())
      {
        csRef<iDocumentNode> sizeNode = paramsNode->CreateNodeBefore (
          CS_NODE_ELEMENT, 0);
        sizeNode->SetValue ("particlesize");
        synldr->WriteVector (sizeNode, partObj->GetParticleSize ());
      }

      // Write emitters
      csSet<iParticleEmitter*> factEmitters;
      for (size_t i = 0; i < factObj->GetEmitterCount (); i++)
      {
        factEmitters.Add (factObj->GetEmitter (i)); 
      }

      for (size_t i = 0; i < partObj->GetEmitterCount (); i++)
      {
        iParticleEmitter* emitter = partObj->GetEmitter (i);
        if (!factEmitters.Contains (emitter))
          WriteEmitter (paramsNode, emitter);
      }

      csSet<iParticleEffector*> factEffectors;

      for (size_t i = 0; i < factObj->GetEffectorCount (); i++)
      {
        factEffectors.Add (factObj->GetEffector (i)); 
      }

      for (size_t i = 0; i < partObj->GetEffectorCount (); i++)
      {
        iParticleEffector* effector = partObj->GetEffector (i);
        if (!factEffectors.Contains (effector))
          WriteEffector (paramsNode, effector);
      }
    }

    return true;
  }



}

CS_PLUGIN_NAMESPACE_END(ParticlesLoader)
