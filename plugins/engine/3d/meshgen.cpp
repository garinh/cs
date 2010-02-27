/*
Copyright (C) 2005-2005 by Jorrit Tyberghein

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
#include "csgeom/math3d.h"
#include "csgeom/matrix3.h"
#include "csutil/randomgen.h"
#include "plugins/engine/3d/engine.h"
#include "plugins/engine/3d/meshgen.h"
#include "iengine/material.h"
#include "igraphic/image.h"

static int AreaCompare(csVector4 const& r, csVector4 const& k)
{
  float a = (r.z - r.x) * (r.w - r.y);
  float b = (k.z - k.x) * (k.w - k.y);
  return (a < b) ? 1 : (a - b) > 0.01 ? -1 : 0;
}

PositionMap::PositionMap(const csBox2& box)
{
  posGen.Initialize();
  freeAreas.Push(csVector4(box.MinX(), box.MinY(), box.MaxX(), box.MaxY()));
}

bool PositionMap::GetRandomPosition(float& xpos, float& zpos, float& radius, float& minRadius)
{
  // Do a quick search.
  for(size_t i=0; i<freeAreas.GetSize(); ++i)
  {
    csVector4 freeArea = freeAreas[i];

    if(freeArea.z - freeArea.x < radius*2 ||
       freeArea.w - freeArea.y < radius*2)
    {
      continue;
    }

    float xAvail = freeArea.x + radius * 0.5f;
    float yAvail = freeArea.y + radius * 0.5f;
    float zAvail = freeArea.z - radius * 0.5f;
    float wAvail = freeArea.w - radius * 0.5f;

    xpos = freeArea.x + radius + (zAvail - xAvail) * posGen.Get();
    zpos = freeArea.y + radius + (wAvail - yAvail) * posGen.Get();

    freeAreas.DeleteIndex(i);

    if(minRadius*2 <= (zpos-radius) - freeArea.y)
      freeAreas.InsertSorted(csVector4(freeArea.x, freeArea.y, xpos+radius, zpos-radius), AreaCompare);

    if(minRadius*2 <= freeArea.z - (xpos+radius))
      freeAreas.InsertSorted(csVector4(xpos+radius, freeArea.y, freeArea.z, zpos+radius), AreaCompare);

    if(minRadius*2 <= freeArea.w - (zpos+radius))
      freeAreas.InsertSorted(csVector4(xpos-radius, zpos+radius, freeArea.z, freeArea.w), AreaCompare);

    if(minRadius*2 <= (xpos-radius) - freeArea.x)
      freeAreas.InsertSorted(csVector4(freeArea.x, zpos-radius, xpos-radius, freeArea.w), AreaCompare);

    return true;
  }

  // No space found.. we could do a more thorough search if this case happens often.
  return false;
}

csMeshGeneratorGeometry::csMeshGeneratorGeometry (
  csMeshGenerator* generator) : scfImplementationType (this),
  generator (generator)
{
  colldetID = generator->GetStringSet()->Request ("colldet");
  radius = 0.0f;
  density = 1.0f;
  total_max_dist = 0.0f;
  default_material_factor = 0.0f;
  celldim = 0;
  positions = 0;
  wind_direction = csVector3(0, 0, 0);
  wind_bias = 1.0f;
  wind_speed = 1.0f;
}

csMeshGeneratorGeometry::~csMeshGeneratorGeometry ()
{
}

void csMeshGeneratorGeometry::AddSVToMesh (iMeshWrapper* mesh,  
                                           csShaderVariable* sv) 
{ 
  if (!mesh) return; 
  mesh->GetSVContext()->AddVariable (sv); 
  iSceneNode* sn = mesh->QuerySceneNode (); 
  csRef<iSceneNodeArray> children = sn->GetChildrenArray (); 
  size_t i; 
  for (i = 0 ; i < children->GetSize () ; i++) 
    AddSVToMesh (children->Get (i)->QueryMesh (), sv); 
} 

void csMeshGeneratorGeometry::SetMeshBBox (iMeshWrapper* mesh,  
                                           const csBox3& bbox) 
{ 
  if (!mesh) return; 
  mesh->GetMeshObject()->GetObjectModel()->SetObjectBoundingBox (bbox); 
  // FIXME UGLY UGLY UGLY
  // So the player doesn't collide with the huge mesh bbox. 
 	mesh->GetMeshObject()->GetObjectModel()->SetTriangleData (colldetID, 0); 
  iSceneNode* sn = mesh->QuerySceneNode (); 
  csRef<iSceneNodeArray> children = sn->GetChildrenArray (); 
  size_t i; 
  for (i = 0 ; i < children->GetSize () ; i++) 
    SetMeshBBox (children->Get (i)->QueryMesh (), bbox); 
} 

void csMeshGeneratorGeometry::GetDensityMapFactor (float x, float z,
                                                   float &data)
{
  if (density_map)
  {
    if (density_map.IsValid ())
      density_map->SampleFloat (density_map_type, x, z, data); 
    data *= density_map_factor;  
  }
  else data = 1.0f;
}
void csMeshGeneratorGeometry::SetDensityMap (iTerraFormer* map, float factor, 
                                             const csStringID &type)
{
  density_map = map;
  density_map_factor = factor;
  density_map_type = type;
}

void csMeshGeneratorGeometry::AddPositionsFromMap (iTerraFormer* map,
	const csBox2 &region, uint resx, uint resy, float value,
	const csStringID & type)
{
  csRef<iTerraSampler> sampler = map->GetSampler (region, resx, resy);
  float stepx = (region.MaxX () - region.MinX ())/resx;
  float stepy = (region.MaxY () - region.MinY ())/resy;

  const float* map_values = sampler->SampleFloat (type);
  float curx = region.MinX (), cury = region.MinY ();
  for (uint i = 0; i < resx; i++)
  {
    for (uint j = 0; j < resy; j++)
    {
      if (map_values[i*resx + j] == value)
      {
        AddPosition (csVector2 (curx, cury));
      }
      curx += stepx;
    }
    curx = region.MinX ();
    cury += stepy;
  }
}

void csMeshGeneratorGeometry::ResetManualPositions (int new_celldim)
{
  if (celldim == new_celldim)
    return;

  csArray<csVector2> tmp_pos;
  for (int i = 0; i < celldim; i++)
  {
    for (size_t j = 0; j < positions[i].GetSize (); j++)
    {
      tmp_pos.Push (positions[i][j]);
    }
  }

  delete[] positions;

  celldim = new_celldim;
  positions = new csArray<csVector2> [celldim*celldim];

  for (size_t i = 0; i < tmp_pos.GetSize (); i++)
  {
    AddPosition (tmp_pos[i]);
  }
}

void csMeshGeneratorGeometry::AddPosition (const csVector2 &pos)
{
  ResetManualPositions (generator->GetCellCount ());
  positions[generator->GetCellId (pos)].Push (pos);
}

void csMeshGeneratorGeometry::AddDensityMaterialFactor (
  iMaterialWrapper* material, float factor)
{
  csMGDensityMaterialFactor mf;
  mf.material = material;
  mf.factor = factor;
  material_factors.Push (mf);
}

static int CompareGeom (csMGGeom const& m1, csMGGeom const& m2)
{
  if (m1.maxdistance < m2.maxdistance) return -1;
  else if (m1.maxdistance > m2.maxdistance) return 1;
  else return 0;
}

void csMeshGeneratorGeometry::AddFactory (iMeshFactoryWrapper* factory,
                                          float maxdist)
{
  csMGGeom g;
  g.factory = factory;
  g.maxdistance = maxdist;
  g.sqmaxdistance = maxdist * maxdist;
  g.factory->GetMeshObjectFactory ()->SetMixMode (CS_FX_COPY);
  g.mesh = generator->engine->CreateMeshWrapper (g.factory, 0);
  g.mesh->SetZBufModeRecursive (CS_ZBUF_USE); 
  g.vertexInfoArray.transformVar.AttachNew (new csShaderVariable (generator->varTransform)); 
  g.vertexInfoArray.transformVar->SetType (csShaderVariable::ARRAY); 
  g.vertexInfoArray.transformVar->SetArraySize (0); 
  g.vertexInfoArray.fadeFactorVar.AttachNew (new csShaderVariable (generator->varFadeFactor)); 
  g.vertexInfoArray.fadeFactorVar->SetType (csShaderVariable::ARRAY);
  g.vertexInfoArray.fadeFactorVar->SetArraySize (0); 
  g.vertexInfoArray.windVar.AttachNew (new csShaderVariable (generator->varWind)); 
  g.vertexInfoArray.windVar->SetType (csShaderVariable::ARRAY);
  g.vertexInfoArray.windVar->SetArraySize (0);
  g.vertexInfoArray.windSpeedVar.AttachNew (new csShaderVariable (generator->varWindSpeed)); 
  g.vertexInfoArray.windSpeedVar->SetType (csShaderVariable::ARRAY);
  g.vertexInfoArray.windSpeedVar->SetArraySize (0); 
  AddSVToMesh (g.mesh, g.vertexInfoArray.transformVar); 
  AddSVToMesh (g.mesh, g.vertexInfoArray.fadeFactorVar); 
  AddSVToMesh (g.mesh, g.vertexInfoArray.windVar); 
  AddSVToMesh (g.mesh, g.vertexInfoArray.windSpeedVar); 
  csBox3 bbox;
  bbox.SetSize (csVector3 (maxdist, maxdist, maxdist));
  SetMeshBBox (g.mesh, bbox);

  factories.InsertSorted (g, CompareGeom);

  if (maxdist > total_max_dist) total_max_dist = maxdist;
}

void csMeshGeneratorGeometry::RemoveFactory (size_t idx)
{
  factories.DeleteIndex (idx);
}

void csMeshGeneratorGeometry::SetRadius (float radius)
{
  csMeshGeneratorGeometry::radius = radius;
}

void csMeshGeneratorGeometry::SetDensity (float density)
{
  csMeshGeneratorGeometry::density = density;
}

iMeshWrapper* csMeshGeneratorGeometry::AllocMesh (
  int cidx, const csMGCell& cell, float sqdist,
  size_t& lod, csMGInstVertexInfo& vertexInfo) 
{
  lod = GetLODLevel (sqdist);
  if (lod == csArrayItemNotFound) return 0;

  csMGGeom& geom = factories[lod];
  if (geom.vertexinfo_setaside.GetSize () > 0)
  {
    vertexInfo = geom.vertexinfo_setaside.Pop();
    return geom.mesh; 
  }
  else
  {
    if (geom.vertexInfoArray.transformVar->GetArraySize() == 0) 
    { 
      geom.mesh->GetMovable ()->SetSector (generator->GetSector ()); 
      geom.mesh->GetMovable ()->UpdateMove ();
    }
    vertexInfo.transformVar.AttachNew (new csShaderVariable);
 	vertexInfo.fadeFactorVar.AttachNew (new csShaderVariable);
    vertexInfo.windVar.AttachNew (new csShaderVariable);
    vertexInfo.windSpeedVar.AttachNew (new csShaderVariable);
    csRandomGen rng (csGetTicks ());
    vertexInfo.windRandVar = rng.Get();
 	geom.vertexInfoArray.transformVar->AddVariableToArray (vertexInfo.transformVar);
 	geom.vertexInfoArray.fadeFactorVar->AddVariableToArray (vertexInfo.fadeFactorVar);
    geom.vertexInfoArray.windVar->AddVariableToArray (vertexInfo.windVar);
    geom.vertexInfoArray.windSpeedVar->AddVariableToArray (vertexInfo.windSpeedVar);
    vertexInfo.fadeFactorVar->SetValue(1.0f);

    return geom.mesh;
  }
}

void csMeshGeneratorGeometry::MoveMesh (int cidx, iMeshWrapper* mesh,
                                        size_t lod, csMGInstVertexInfo& vertexInfo, 
                                        const csVector3& position,
                                        const csMatrix3& matrix)
{
  csVector3 meshpos = mesh->GetMovable ()->GetFullPosition ();
  csVector3 pos = position - meshpos;
  csReversibleTransform tr (matrix, pos);
  vertexInfo.transformVar->SetValue (tr);
}

void csMeshGeneratorGeometry::SetWindDirection (float x, float z)
{
  wind_direction.x = x;
  wind_direction.z = z;
}

void csMeshGeneratorGeometry::SetWindBias (float bias)
{
  if(bias >= 1.0f)
    wind_bias = bias;
}

void csMeshGeneratorGeometry::SetWindSpeed (float speed)
{
  if(speed >= 0.0f)
    wind_speed = speed;
}

void csMeshGeneratorGeometry::SetAsideMesh (int cidx, iMeshWrapper* mesh,
                                            size_t lod, csMGInstVertexInfo& vertexInfo)
{
  csMGGeom& geom = factories[lod];
  geom.vertexinfo_setaside.Push (vertexInfo);
}

void csMeshGeneratorGeometry::FreeSetAsideMeshes ()
{
  size_t lod;
  for (lod = 0 ; lod < factories.GetSize () ; lod++)
  {
    csMGGeom& geom = factories[lod];
    bool varRemoved = false; 
    while (geom.vertexinfo_setaside.GetSize() > 0) 
    { 
      csMGInstVertexInfo vertexInfo = geom.vertexinfo_setaside.Pop (); 
      size_t idx = geom.vertexInfoArray.transformVar->FindArrayElement (vertexInfo.transformVar); 
      if (idx != csArrayItemNotFound) 
      { 
        geom.vertexInfoArray.transformVar->RemoveFromArray (idx); 
        varRemoved = true; 
      } 

      idx = geom.vertexInfoArray.fadeFactorVar->FindArrayElement (vertexInfo.fadeFactorVar); 
      if (idx != csArrayItemNotFound) 
      { 
        geom.vertexInfoArray.fadeFactorVar->RemoveFromArray (idx); 
        varRemoved = true; 
      } 

      idx = geom.vertexInfoArray.windVar->FindArrayElement (vertexInfo.windVar); 
      if (idx != csArrayItemNotFound) 
      { 
        geom.vertexInfoArray.windVar->RemoveFromArray (idx); 
        varRemoved = true; 
      } 

      idx = geom.vertexInfoArray.windSpeedVar->FindArrayElement (vertexInfo.windSpeedVar); 
      if (idx != csArrayItemNotFound) 
      { 
        geom.vertexInfoArray.windSpeedVar->RemoveFromArray (idx); 
        varRemoved = true; 
      } 
    } 
    if (varRemoved && (geom.vertexInfoArray.transformVar->GetArraySize() == 0)) 
      geom.mesh->GetMovable()->ClearSectors (); 
  }
}

size_t csMeshGeneratorGeometry::GetLODLevel (float sqdist)
{
  size_t i;
  for (i = 0 ; i < factories.GetSize () ; i++)
  {
    csMGGeom& geom = factories[i];
    if (sqdist <= geom.sqmaxdistance)
    {
      return i;
    }
  }
  return csArrayItemNotFound;
}

bool csMeshGeneratorGeometry::IsRightLOD (float sqdist, size_t current_lod)
{
  // With only one lod level we are always right.
  if (factories.GetSize () <= 1) return true;
  if (current_lod == 0)
    return (sqdist <= factories[0].sqmaxdistance);
  else
    return (sqdist <= factories[current_lod].sqmaxdistance) &&
    (sqdist > factories[current_lod-1].sqmaxdistance);
}

void csMeshGeneratorGeometry::UpdatePosition (const csVector3& pos) 
{ 
  for (size_t f = 0; f < factories.GetSize(); f++) 
  { 
    factories[f].mesh->GetMovable()->SetPosition (pos); 
    factories[f].mesh->GetMovable()->UpdateMove (); 
  } 
} 

//--------------------------------------------------------------------------

csMeshGenerator::csMeshGenerator (csEngine* engine) : 
  scfImplementationType (this), total_max_dist (-1.0f), minRadius(-1.0f),
  use_density_scaling (false), use_alpha_scaling (false),
  last_pos (0, 0, 0), setup_cells (false),
  cell_dim (50), inuse_blocks (0), inuse_blocks_last (0),
  max_blocks (100), engine (engine)
{
  cells = new csMGCell [cell_dim * cell_dim];

  for (size_t i = 0 ; i < size_t (max_blocks) ; i++)
    cache_blocks.Push (new csMGPositionBlock ());

  prev_cells.MakeEmpty ();

  for (size_t i = 0 ; i < CS_GEOM_MAX_ROTATIONS ; i++)
  {
    rotation_matrices[i] = csYRotMatrix3 (2.0f*PI * float (i)
      / float (CS_GEOM_MAX_ROTATIONS));
  }

  alpha_priority = engine->GetAlphaRenderPriority ();
  object_priority = engine->GetObjectRenderPriority ();
  
  strings = csQueryRegistryTagInterface<iStringSet> (
    engine->objectRegistry, "crystalspace.shared.stringset");
  SVstrings = csQueryRegistryTagInterface<iShaderVarStringSet> (
    engine->objectRegistry, "crystalspace.shader.variablenameset");
  varTransform = SVstrings->Request ("instancing transforms");
  varFadeFactor = SVstrings->Request ("alpha factor");
  varWind = SVstrings->Request ("wind data");
  varWindSpeed = SVstrings->Request ("wind speed");
}

csMeshGenerator::~csMeshGenerator ()
{
  delete[] cells;
  while (inuse_blocks)
  {
    csMGPositionBlock* n = inuse_blocks->next;
    delete inuse_blocks;
    inuse_blocks = n;
  }
}

void csMeshGenerator::SelfDestruct ()
{
  if (GetSector ())
  {
    size_t c = GetSector ()->GetMeshGeneratorCount ();
    while (c > 0)
    {
      c--;
      if (GetSector ()->GetMeshGenerator (c) == (iMeshGenerator*)this)
      {
        GetSector ()->RemoveMeshGenerator (c);
        return;
      }
    }
    CS_ASSERT (false);
  }
}

float csMeshGenerator::GetTotalMaxDist ()
{
  if (total_max_dist < 0.0f)
  {
    total_max_dist = 0.0f;
    size_t i;
    for (i = 0 ; i < geometries.GetSize () ; i++)
    {
      float md = geometries[i]->GetTotalMaxDist ();
      if (md > total_max_dist) total_max_dist = md;
    }
    sq_total_max_dist = total_max_dist * total_max_dist;
  }
  return total_max_dist;
}

void csMeshGenerator::SetDensityScale (float mindist, float maxdist,
                                       float maxdensityfactor)
{
  use_density_scaling = true;
  density_mindist = mindist;
  sq_density_mindist = density_mindist * density_mindist;
  density_maxdist = maxdist;
  density_maxfactor = maxdensityfactor;

  density_scale = (1.0f-density_maxfactor) / (density_maxdist - density_mindist);
}

void csMeshGenerator::SetAlphaScale (float mindist, float maxdist)
{
  use_alpha_scaling = true;
  alpha_mindist = mindist;
  sq_alpha_mindist = alpha_mindist * alpha_mindist;
  alpha_maxdist = maxdist;

  alpha_scale = 1.0f / (alpha_maxdist - alpha_mindist);
}

void csMeshGenerator::SetCellCount (int number)
{
  if (setup_cells)
  {
    // We already setup so we need to deallocate all blocks first.
    int x, z;
    for (z = 0 ; z < cell_dim ; z++)
      for (x = 0 ; x < cell_dim ; x++)
      {
        int cidx = z*cell_dim + x;
        csMGCell& cell = cells[cidx];
        FreeMeshesInBlock (cidx, cell);
        if (cell.block)
        {
          cell.block->parent_cell = csArrayItemNotFound;
          cache_blocks.Push (cell.block);
          cell.block->next = cell.block->prev = 0;
          cell.block = 0;
        }
      }
      inuse_blocks = 0;
      inuse_blocks_last = 0;
      setup_cells = false;
  }
  cell_dim = number;
  delete[] cells;
  cells = new csMGCell [cell_dim * cell_dim];
}

void csMeshGenerator::SetBlockCount (int number)
{
  if (setup_cells)
  {
    // We already setup so we need to deallocate all blocks first.
    int x, z;
    for (z = 0 ; z < cell_dim ; z++)
      for (x = 0 ; x < cell_dim ; x++)
      {
        int cidx = z*cell_dim + x;
        csMGCell& cell = cells[cidx];
        FreeMeshesInBlock (cidx, cell);
        if (cell.block)
        {
          cell.block->parent_cell = csArrayItemNotFound;
          cache_blocks.Push (cell.block);
          cell.block->next = cell.block->prev = 0;
          cell.block = 0;
        }
      }
      inuse_blocks = 0;
      inuse_blocks_last = 0;

      setup_cells = false;
  }
  size_t i;
  if (number > max_blocks)
  {
    for (i = max_blocks ; i < size_t (number) ; i++)
      cache_blocks.Push (new csMGPositionBlock ());
  }
  else
  {
    for (i = number ; i < size_t (max_blocks) ; i++)
    {
      csMGPositionBlock* block = cache_blocks.Pop ();
      delete block;
    }
  }
  max_blocks = number;
}

void csMeshGenerator::SetSampleBox (const csBox3& box)
{
  samplebox = box;
  setup_cells = false;
}

void csMeshGenerator::SetupSampleBox ()
{
  if (setup_cells) return;
  setup_cells = true;
  samplefact_x = float (cell_dim) / (samplebox.MaxX () - samplebox.MinX ());
  samplefact_z = float (cell_dim) / (samplebox.MaxZ () - samplebox.MinZ ());
  samplecellwidth_x = 1.0f / samplefact_x;
  samplecellheight_z = 1.0f / samplefact_z;
  int x, z;
  int idx = 0;
  for (z = 0 ; z < cell_dim ; z++)
  {
    float wz = GetWorldZ (z);
    for (x = 0 ; x < cell_dim ; x++)
    {
      float wx = GetWorldX (x);
      cells[idx].box.Set (wx, wz, wx + samplecellwidth_x, wz + samplecellheight_z);
      delete cells[idx].positionMap;
      cells[idx].positionMap = new PositionMap(cells[idx].box);
      // Here we need to calculate the meshes relevant for this cell (i.e.
      // meshes that intersect this cell as seen in 2D space).
      // @@@ For now we just copy the list of meshes from csMeshGenerator.
      cells[idx].meshes = meshes;
      idx++;
    }
  }
  for (size_t g = 0 ; g < geometries.GetSize () ; g++)
    geometries[g]->ResetManualPositions (cell_dim);
}

int csMeshGenerator::GetCellId (const csVector2& pos)
{
  SetupSampleBox ();
  int cellx = GetCellX (pos.x);
  int cellz = GetCellZ (pos.y);
  return cellz*cell_dim + cellx;
}

size_t csMeshGenerator::CountPositions (int cidx, csMGCell& cell)
{
  random.Initialize ((unsigned int)cidx); // @@@ Consider using a better seed?
  size_t counter = 0;

  const csBox2& box = cell.box;
  float box_area = box.Area ();

  size_t i, j, g;
  for (g = 0 ; g < geometries.GetSize () ; g++)
  {
    float density = geometries[g]->GetDensity ();
    size_t count = size_t (density * box_area);
    for (j = 0 ; j < count ; j++)
    {
      float x = random.Get (box.MinX (), box.MaxX ());
      float z = random.Get (box.MinY (), box.MaxY ());
      csVector3 start (x, samplebox.MaxY (), z);
      csVector3 end = start;
      end.y = samplebox.MinY ();
      bool hit = false;
      for (i = 0 ; i < cell.meshes.GetSize () ; i++)
      {
        csHitBeamResult rc = cell.meshes[i]->HitBeam (start, end);
        if (rc.hit)
        {
          end.y = rc.isect.y + 0.0001;
          hit = true;
          break;
        }
      }
      
      if (hit)
      {
        counter++;
      }
    }
  }
  return counter;
}

size_t csMeshGenerator::CountAllPositions ()
{
  size_t counter = 0;
  int x, z;
  for (z = 0 ; z < cell_dim ; z++)
    for (x = 0 ; x < cell_dim ; x++)
    {
      int cidx = z*cell_dim + x;
      csMGCell& cell = cells[cidx];
      counter += CountPositions (cidx, cell);
    }
    return counter;
}

void csMeshGenerator::GeneratePositions (int cidx, csMGCell& cell,
                                         csMGPositionBlock* block)
{
  random.Initialize ((unsigned int)cidx); // @@@ Consider using a better seed?

  block->positions.Empty ();

  const csBox2& box = cell.box;
  float box_area = box.Area ();

  if(minRadius < 0.0f)
  {
    for (size_t i = 0 ; i < geometries.GetSize () ; i++)
    {
      if(geometries[i]->GetRadius() < minRadius || minRadius < 0.0f)
      {
        minRadius = geometries[i]->GetRadius();
      }
    }
  }

  size_t i, j;
  for (size_t g = 0 ; g < geometries.GetSize () ; g++)
  {
    csMGPosition pos;
    pos.geom_type = g;
    size_t mpos_count = geometries[g]->GetManualPositionCount (cidx);

    float density = geometries[g]->GetDensity ();

    size_t count = (mpos_count > 0)? mpos_count : size_t (density * box_area);

    const csArray<csMGDensityMaterialFactor>& mftable
      = geometries[g]->GetDensityMaterialFactors ();
    float default_material_factor
      = geometries[g]->GetDefaultDensityMaterialFactor ();
    bool do_material = mftable.GetSize () > 0;
    for (j = 0 ; j < count ; j++)
    {
      float pos_factor;
      float x;
      float z;
      if (mpos_count == 0)
      {
        float r = geometries[g]->GetRadius();
        if(!cell.positionMap->GetRandomPosition(x, z, r, minRadius))
        {
          // Ran out of room in this cell.
          return;
        }
        geometries[g]->GetDensityMapFactor (x, z, pos_factor);
      }
      else
      {
        csVector2 mpos = geometries[g]->GetManualPosition (cidx,j);
        x = mpos.x; 
        z = mpos.y;
        pos_factor = 1.0f;
      }
      
      if (!((pos_factor < 0.0001) ||
        (pos_factor < 0.9999 && random.Get () > pos_factor)))
      {
        csVector3 start (x, samplebox.MaxY (), z);
        csVector3 end = start;
        end.y = samplebox.MinY ();
        bool hit = false;
        iMaterialWrapper* hit_material = 0;
        csArray<iMaterialWrapper*> hit_materials;
        for (i = 0 ; i < cell.meshes.GetSize () ; i++)
        {
          csHitBeamResult rc = cell.meshes[i]->HitBeam (start, end,
            do_material);
          if (rc.hit)
          {
            pos.position = rc.isect;
            end.y = rc.isect.y + 0.0001;
            hit_material = rc.material;
            if(hit_material == 0)
            {
                hit_materials = rc.materials;
            }
            hit = true;
          }
        }
        if (hit)
        {
          if (do_material)
          {
            // We use material density tables.
            float factor = default_material_factor;
            if(hit_material != 0)
            {
              for (size_t mi = 0 ; mi < mftable.GetSize () ; mi++)
              {
                if (mftable[mi].material == hit_material)
                {
                  factor = mftable[mi].factor;
                  break;
                }
              }
            }
            else
            {
              // Get the highest material factor.
              float factorh = 0;
              for(size_t m=0; m<hit_materials.GetSize(); ++m)
              {
                for (size_t mi=0 ; mi<mftable.GetSize (); ++mi)
                {
                  if (mftable[mi].material == hit_materials.Get(m))
                  {
                    if(mftable[mi].factor > factorh)
                    {
                      factorh = mftable[mi].factor;
                    }
                    break;
                  }
                }
              }

              if(factorh > 0)
              {
                factor = factorh;
              }
            }

            if (factor < 0.0001 || (factor < 0.9999 && random.Get () > factor))
            {
              hit = false;
            }
          }
          if (hit)
          {
            int rot = int (random.Get (CS_GEOM_MAX_ROTATIONS));
            if (rot < 0) rot = 0;
            else if (rot >= CS_GEOM_MAX_ROTATIONS) rot = CS_GEOM_MAX_ROTATIONS-1;
            pos.rotation = rot;
            pos.random = random.Get ();
            block->positions.Push (pos);
          }
        }
      }
    }
  }
}

void csMeshGenerator::AllocateBlock (int cidx, csMGCell& cell)
{
  if (cell.block)
  {
    // Our block is already there. We just push it back to the
    // front of 'inuse_blocks' if it is not already there.
    csMGPositionBlock* block = cell.block;
    if (block->prev)
    {
      // Unlink first.
      block->prev->next = block->next;
      if (block->next) block->next->prev = block->prev;
      else inuse_blocks_last = block->prev;

      // Link to front.
      block->next = inuse_blocks;
      block->prev = 0;
      inuse_blocks->prev = block;
      inuse_blocks = block;
    }
  }
  else if (cache_blocks.GetSize () > 0)
  {
    // We need a new block and one is available in the cache.
    csMGPositionBlock* block = cache_blocks.Pop ();
    CS_ASSERT (block->parent_cell == csArrayItemNotFound);
    CS_ASSERT (block->next == 0 && block->prev == 0);
    block->parent_cell = cidx;
    cell.block = block;
    // Link block to the front.
    block->next = inuse_blocks;
    block->prev = 0;
    if (inuse_blocks) inuse_blocks->prev = block;
    else inuse_blocks_last = block;
    inuse_blocks = block;

    delete cell.positionMap;
    cell.positionMap = new PositionMap(cell.box);
    GeneratePositions (cidx, cell, block);
  }
  else
  {
    // We need a new block and the cache is empty.
    // Now we take the last used block from 'inuse_blocks'.
    csMGPositionBlock* block = inuse_blocks_last;
    CS_ASSERT (block->parent_cell != csArrayItemNotFound);
    CS_ASSERT (block == cells[block->parent_cell].block);
    cells[block->parent_cell].block = 0;
    block->parent_cell = cidx;
    cell.block = block;

    // Unlink first.
    block->prev->next = 0;
    inuse_blocks_last = block->prev;

    // Link to front.
    block->next = inuse_blocks;
    block->prev = 0;
    inuse_blocks->prev = block;
    inuse_blocks = block;

    delete cell.positionMap;
    cell.positionMap = new PositionMap(cell.box);
    GeneratePositions (cidx, cell, block);
  }
}

void csMeshGenerator::SetFade (iMeshWrapper* mesh, uint mode)
{
  if (!mesh) return;
  mesh->GetMeshObject ()->SetMixMode (mode);
  iSceneNode* sn = mesh->QuerySceneNode ();
  const csRef<iSceneNodeArray> children = sn->GetChildrenArray ();
  size_t i;
  for (i = 0 ; i < children->GetSize(); i++)
    SetFade (children->Get (i)->QueryMesh (), mode);
}

void csMeshGenerator::SetFade (csMGPosition& p, float factor)
{
  p.vertexInfo.fadeFactorVar->SetValue (factor);
}

void csMeshGenerator::SetWindData (csMGPosition& p)
{
  csMeshGeneratorGeometry* geom = geometries[p.geom_type];
  if(!geom->GetWindDirection().IsZero())
  {
    csReversibleTransform transform;
    p.vertexInfo.transformVar->GetValue(transform);
    csVector3 windDirection = transform.Other2ThisRelative(geom->GetWindDirection());
    p.vertexInfo.windVar->SetValue (csVector4(windDirection.x, windDirection.z, p.vertexInfo.windRandVar, geom->GetWindBias()));
    p.vertexInfo.windSpeedVar->SetValue (geom->GetWindSpeed());
  }
}

void csMeshGenerator::AllocateMeshes (int cidx, csMGCell& cell,
                                      const csVector3& pos,
                                      const csVector3& delta)
{
  CS_ASSERT (cell.block != 0);
  CS_ASSERT (sector != 0);
  csArray<csMGPosition>& positions = cell.block->positions;
  GetTotalMaxDist ();
  size_t i;
  for (i = 0 ; i < positions.GetSize () ; i++)
  {
    csMGPosition& p = positions[i];

    float sqdist = csSquaredDist::PointPoint (pos, p.position);
    if (sqdist < sq_total_max_dist)
    {
      if (!p.mesh)
      {
        // We didn't have a mesh here so we allocate one.
        // But first we test if we have density scaling.
        bool show = true;
        if (use_density_scaling && sqdist > sq_density_mindist)
        {
          float dist = sqrt (sqdist);
          float factor = (density_maxdist - dist) * density_scale
            + density_maxfactor;
          if (factor < 0) factor = 0;
          else if (factor > 1) factor = 1;
          if (p.random > factor)
              show = false;
          else
              p.addedDist = dist;
        }

        if (show)
        {
          iMeshWrapper* mesh = geometries[p.geom_type]->AllocMesh (
            cidx, cell, sqdist, p.lod, p.vertexInfo);
          if (mesh)
          {
            p.mesh = mesh;
            p.last_mixmode = ~0;
            geometries[p.geom_type]->MoveMesh (cidx, mesh, p.lod,
              p.vertexInfo, p.position, rotation_matrices[p.rotation]);
          }
        }
      }
      else
      {
        // We already have a mesh but we check here if we should switch LOD level.
        if (!geometries[p.geom_type]->IsRightLOD (sqdist, p.lod))
        {
          // We need a different mesh here.
          geometries[p.geom_type]->SetAsideMesh (cidx, p.mesh, p.lod,
            p.vertexInfo);
          iMeshWrapper* mesh = geometries[p.geom_type]->AllocMesh (
            cidx, cell, sqdist, p.lod, p.vertexInfo);
          p.mesh = mesh;
          if (mesh)
          {
            p.last_mixmode = ~0;
            geometries[p.geom_type]->MoveMesh (cidx, mesh, p.lod, p.vertexInfo,
              p.position, rotation_matrices[p.rotation]);
          }
        }
        else if(!delta.IsZero())
        { 
          // LOD level is fine, adjust for new pos 
          geometries[p.geom_type]->MoveMesh (cidx, p.mesh, p.lod, p.vertexInfo, 
            p.position, rotation_matrices[p.rotation]); 
        } 
      }
      if (p.mesh)
      {
        SetWindData(p);
      }
      if (!delta.IsZero() && p.mesh && use_alpha_scaling)
      {
        // These are used when we have both density and alpha scaling.
        // The alpha limits are adjusted for the density added mesh.
        float correct_alpha_maxdist = alpha_maxdist;
        float correct_sq_alpha_mindist = sq_alpha_mindist;
        float correct_scale = alpha_scale;

        if (use_density_scaling && p.addedDist > 0)
        {
          correct_alpha_maxdist = p.addedDist;
          float correct_alpha_mindist = p.addedDist*(alpha_mindist/alpha_maxdist);
          correct_sq_alpha_mindist = correct_alpha_mindist*correct_alpha_mindist;
          correct_scale = 1.0f/(correct_alpha_maxdist-correct_alpha_mindist);
        }

        float factor = 1.0;
        if (sqdist > correct_sq_alpha_mindist)
        {
          float dist = sqrt (sqdist);
          factor = (correct_alpha_maxdist - dist) * correct_scale;
          if(factor < 0) factor = 0.0f;
        }

        if(use_density_scaling && p.addedDist > 0 && (1.0f - factor) < 0.01)
        {
            p.addedDist = 0;
        }
 
        SetFade (p, factor);
      }
    }
    else
    {
      if (p.mesh)
      {
        geometries[p.geom_type]->SetAsideMesh (cidx, p.mesh, p.lod,
          p.vertexInfo);
        p.mesh = 0;
        p.addedDist = 0;
      }
    }
  }
}

void csMeshGenerator::AllocateBlocks (const csVector3& pos)
{
  csVector3 delta = pos - last_pos;

  last_pos = pos;
  SetupSampleBox ();

  for (size_t i = 0 ; i < geometries.GetSize () ; i++) 
  { 
    geometries[i]->UpdatePosition (pos); 
  } 

  int cellx = GetCellX (pos.x);
  int cellz = GetCellZ (pos.z);

  // Total maximum distance for all geometries.
  float md = GetTotalMaxDist ();
  float sqmd = md * md;
  // Same in cell counts:
  int cell_x_md = 1+int (md * samplefact_x);
  int cell_z_md = 1+int (md * samplefact_z);

  // @@@ This can be done more efficiently.
  csVector2 pos2d (pos.x, pos.z);
  int x, z;

  int minz = cellz - cell_z_md;
  if (minz < 0) minz = 0;
  int maxz = cellz + cell_z_md+1;
  if (maxz > cell_dim) maxz = cell_dim;

  int minx = cellx - cell_x_md;
  if (minx < 0) minx = 0;
  int maxx = cellx + cell_x_md+1;
  if (maxx > cell_dim) maxx = cell_dim;

  // Calculate the intersection of minx, ... with prev_minx, ...
  // This intersection represent all cells that are not used this frame
  // but may potentially have gotten meshes the previous frame. We need
  // to free those meshes.
  csRect cur_cells (minx, minz, maxx, maxz);
  if (!prev_cells.IsEmpty ())
  {
    for (z = prev_cells.ymin ; z < prev_cells.ymax ; z++)
      for (x = prev_cells.xmin ; x < prev_cells.xmax ; x++)
        if (!cur_cells.Contains (x, z))
        {
          int cidx = z*cell_dim + x;
          csMGCell& cell = cells[cidx];
          FreeMeshesInBlock (cidx, cell);
        }
  }
  prev_cells = cur_cells;

  int total_needed = 0;

  // Now allocate the cells that are close enough.
  for (z = minz ; z < maxz ; z++)
  {
    int cidx = z*cell_dim + minx;
    for (x = minx ; x < maxx ; x++)
    {
      csMGCell& cell = cells[cidx];
      float sqdist = cell.box.SquaredPosDist (pos2d);

      if (sqdist < sqmd)
      {
        total_needed++;
        if (total_needed >= max_blocks)
        {
          engine->Error (
            "The mesh generator needs more blocks than %d!", max_blocks);
          return;	// @@@ What to do here???
        }
        AllocateBlock (cidx, cell);
        AllocateMeshes (cidx, cell, pos, delta);
      }
      else
      {
        // Block is out of range. We keep the block in the cache
        // but free all meshes that are in it.
        FreeMeshesInBlock (cidx, cell);
      }
      cidx++;
    }
  }

  // Now really free the meshes we didn't reuse.
  size_t i;
  for (i = 0 ; i < geometries.GetSize () ; i++)
  {
    geometries[i]->FreeSetAsideMeshes ();
  }
}

void csMeshGenerator::FreeMeshesInBlock (int cidx, csMGCell& cell)
{
  if (cell.block)
  {
    csArray<csMGPosition>& positions = cell.block->positions;
    size_t i;
    for (i = 0 ; i < positions.GetSize () ; i++)
    {
      if (positions[i].mesh)
      {
        CS_ASSERT (positions[i].geom_type >= 0);
        CS_ASSERT (positions[i].geom_type < geometries.GetSize ());
        geometries[positions[i].geom_type]->SetAsideMesh (cidx,
          positions[i].mesh, positions[i].lod, positions[i].vertexInfo);
        positions[i].mesh = 0;
      }
    }
  }
}

iMeshGeneratorGeometry* csMeshGenerator::CreateGeometry ()
{
  csMeshGeneratorGeometry* geom = new csMeshGeneratorGeometry (this);
  geometries.Push (geom);
  minRadius = -1.0f; // Requires a recalculate.
  total_max_dist = -1.0f;	// Requires a recalculate.
  geom->DecRef ();
  return geom;
}

void csMeshGenerator::RemoveGeometry (size_t idx)
{
  geometries.DeleteIndex (idx);
}

void csMeshGenerator::RemoveMesh (size_t idx)
{
  meshes.DeleteIndex (idx);
}

