/*
    Copyright (C) 1998-2001 by Jorrit Tyberghein

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

#include "csgeom/frustum.h"
#include "csgeom/plane3.h"
#include "csgeom/poly2d.h"
#include "csgeom/polyclip.h"
#include "csplugincommon/rendermanager/renderview.h"
#include "iengine/camera.h"
#include "iengine/light.h"
#include "iengine/movable.h"
#include "iengine/rview.h"
#include "iengine/texture.h"
#include "ivideo/texture.h"
#include "plugins/engine/3d/portal.h"
#include "plugins/engine/3d/portalcontainer.h"

CS_PLUGIN_NAMESPACE_BEGIN(Engine)
{

typedef csDirtyAccessArray<csVector3> portal_VectorArray;
CS_IMPLEMENT_STATIC_VAR (GetStaticVectorArray, portal_VectorArray,())

static portal_VectorArray *VectorArray = 0;


csPortal::csPortal (csPortalContainer* parent)
  : scfImplementationType (this), parent (parent), name (0),
  max_sector_visit (5), filter_r (0), filter_g (1), filter_b (1)
{
  flags.Set (CS_PORTAL_VISCULL);

  VectorArray = GetStaticVectorArray ();
}

csPortal::~csPortal ()
{
  delete[] name;
}

void csPortal::SetSector (iSector *s)
{
  sector = s;
}

const csVector3* csPortal::GetVertices () const
{
  return parent->GetVertices ()->GetArray ();
}

const csVector3* csPortal::GetWorldVertices ()
{
  parent->CheckMovable ();
  return parent->GetWorldVertices ()->GetArray ();
}

int* csPortal::GetVertexIndices () const
{
  return (int*)vertex_indices.GetArray ();
}

int csPortal::GetVertexIndicesCount () const
{
  return (int)vertex_indices.GetSize ();
}

size_t csPortal::GetVerticesCount () const
{ 
  return parent->GetVertices ()->GetSize (); 
}

bool csPortal::CompleteSector (iBase *context)
{
  if (sector)
  {
    bool rc = true;
    int i;
    // Callback are traversed in reverse order so that they can safely
    // delete themselves.
    i = (int)portal_cb_vector.GetSize ()-1;
    while (i >= 0)
    {
      iPortalCallback* cb = portal_cb_vector[i];
      rc = cb->Traverse (this, context);
      if (!rc) break;
      i--;
    }
    return rc;
  }
  else
  {
    bool rc = false;
    // Callback are traversed in reverse order so that they can safely
    // delete themselves.
    int i = (int)sector_cb_vector.GetSize ()-1;
    while (i >= 0)
    {
      iPortalCallback* cb = sector_cb_vector[i];
      rc = cb->Traverse (this, context);
      if (rc == true) break;
      i--;
    }
    return rc;
  }

  return false;
}

void csPortal::ObjectToWorld (const csReversibleTransform &t,
  csReversibleTransform& warp_wor) const
{
  if (flags.Check (CS_PORTAL_STATICDEST))
    warp_wor = warp_obj * t;
  else
    warp_wor = t.GetInverse () * warp_obj * t;
}

void csPortal::HardTransform (const csReversibleTransform &t)
{
  if (flags.Check (CS_PORTAL_WARP))
    ObjectToWorld (t, warp_obj);
}

void csPortal::SetWarp (const csTransform &t)
{
  flags.Set (CS_PORTAL_WARP);
  warp_obj = t;

  csMatrix3 m = warp_obj.GetO2T ();
  flags.SetBool (
      CS_PORTAL_MIRROR,
      (((m.Col1 () % m.Col2 ()) * m.Col3 ()) < 0));
}

void csPortal::SetWarp (
  const csMatrix3 &m_w,
  const csVector3 &v_w_before,
  const csVector3 &v_w_after)
{
  flags.Set (CS_PORTAL_WARP);

  warp_obj = csTransform (m_w.GetInverse (), v_w_after - m_w * v_w_before);

  // If the three colunms of the transformation matrix are taken
  // as vectors, V1, V2, and V3, then V1 x V2 = ( + or - ) V3.
  // The result is positive for non-mirroring transforms, and
  // negative for mirroring transforms.  Thus, (V1 x V2) * V3
  // will equal +1 or -1, depending on whether the transform is
  // mirroring.
  csMatrix3 m = warp_obj.GetO2T ();
  flags.SetBool (
      CS_PORTAL_MIRROR,
      (((m.Col1 () % m.Col2 ()) * m.Col3 ()) < 0));
}

const csReversibleTransform &csPortal::GetWarp () const
{
  return warp_obj;
}

void csPortal::SetFilter (iTextureHandle *ft)
{
  filter_texture = ft;
}

iTextureHandle *csPortal::GetTextureFilter () const
{
  return filter_texture;
}

void csPortal::SetFilter (float r, float g, float b)
{
  filter_r = r;
  filter_g = g;
  filter_b = b;
  filter_texture = 0;
}

void csPortal::GetColorFilter (float &r, float &g, float &b) const
{
  r = filter_r;
  g = filter_g;
  b = filter_b;
}

const csPlane3& csPortal::GetWorldPlane ()
{
  parent->CheckMovable ();
  return world_plane;
}

const csSphere& csPortal::GetWorldSphere ()
{
  parent->CheckMovable ();
  return world_sphere;
}

void csPortal::ComputeCameraPlane (const csReversibleTransform& t,
	csPlane3& camplane)
{
  csDirtyAccessArray<csVector3>* vt = parent->GetWorldVertices ();
  csVector3 cam_vert = t.Other2This ((*vt)[0]);
  t.Other2This (world_plane, cam_vert, camplane);
}

bool csPortal::PointOnPolygon (const csVector3& v)
{
  parent->CheckMovable ();
  csDirtyAccessArray<csVector3>* vt = parent->GetWorldVertices ();
  // First check if point is on the plane.
  csPlane3 &pl = world_plane;
  float dot = pl.D () + pl.A () * v.x + pl.B () * v.y + pl.C () * v.z;
  if (ABS (dot) >= EPSILON) return false;

  // Check if 'v' is on the same side of all edges.
  size_t i, i1;
  bool neg = false, pos = false;
  i1 = vertex_indices.GetSize () - 1;
  for (i = 0; i < vertex_indices.GetSize (); i++)
  {
    float ar = csMath3::Direction3 (v, (*vt)[vertex_indices[i1]],
    	(*vt)[vertex_indices[i]]);
    if (ar < 0)
      neg = true;
    else if (ar > 0)
      pos = true;
    if (neg && pos) return false;
    i1 = i;
  }

  return true;
}

bool csPortal::IntersectRay (const csVector3 &start,
    const csVector3 &end) const
{
  // First we do backface culling on the polygon with respect to
  // the starting point of the beam.
  const csPlane3 &pl = object_plane;
  float dot1 = pl.D () +
    pl.A () * start.x + pl.B () * start.y + pl.C () * start.z;
  if (dot1 > 0) return false;

  // If this vector is perpendicular to the plane of the polygon we
  // need to catch this case here.
  float dot2 = pl.D () + pl.A () * end.x + pl.B () * end.y + pl.C () * end.z;
  if (ABS (dot1 - dot2) < SMALL_EPSILON) return false;

  // Now we generate a plane between the starting point of the ray and
  // every edge of the polygon. With the plane normal of that plane we
  // can then check if the end of the ray is on the same side for all
  // these planes.
  csVector3 normal;
  csVector3 relend = end;
  relend -= start;

  csDirtyAccessArray<csVector3>* vt = parent->GetVertices ();
  size_t i, i1;
  i1 = vertex_indices.GetSize () - 1;
  for (i = 0; i < vertex_indices.GetSize (); i++)
  {
    csMath3::CalcNormal (normal, start, (*vt)[vertex_indices[i1]],
    	(*vt)[vertex_indices[i]]);
    if ((relend * normal) > 0) return false;
    i1 = i;
  }

  return true;
}

bool csPortal::IntersectSegmentPlane (
  const csVector3 &start, const csVector3 &end,
  csVector3 &isect, float *pr) const
{
  float x1 = start.x;
  float y1 = start.y;
  float z1 = start.z;
  float x2 = end.x;
  float y2 = end.y;
  float z2 = end.z;
  float r, num, denom;

  // So now we have the plane equation of the polygon:
  // A*x + B*y + C*z + D = 0
  //
  // We also have the parameter line equations of the ray
  // going through 'start' and 'end':
  // x = r*(x2-x1)+x1
  // y = r*(y2-y1)+y1
  // z = r*(z2-z1)+z1
  //
  // =>   A*(r*(x2-x1)+x1) + B*(r*(y2-y1)+y1) + C*(r*(z2-z1)+z1) + D = 0
  // Set *pr to -1 to indicate error if we return false now.
  if (pr) *pr = -1;

  denom = object_plane.A () * (x2 - x1) +
	  object_plane.B () * (y2 - y1) +
	  object_plane.C () * (z2 - z1);
  if (ABS (denom) < SMALL_EPSILON) return false;  // Lines are parallel
  num = -(object_plane.A () * x1 +
	  object_plane.B () * y1 +
	  object_plane.C () * z1 +
	  object_plane.D ());
  r = num / denom;

  // Calculate 'r' and 'isect' even if the intersection point is
  // not on the segment. That way we can use this function for testing
  // with rays as well.
  if (pr) *pr = r;

  isect.x = r * (x2 - x1) + x1;
  isect.y = r * (y2 - y1) + y1;
  isect.z = r * (z2 - z1) + z1;

  // If r is not in [0,1] the intersection point is not on the segment.
  if (r < 0 /*-SMALL_EPSILON*/ || r > 1) return false;

  return true;
}

bool csPortal::IntersectSegment (
  const csVector3 &start, const csVector3 &end,
  csVector3 &isect, float *pr) const
{
  if (!IntersectRay (start, end)) return false;
  return IntersectSegmentPlane (start, end, isect, pr);
}

csVector3 csPortal::Warp (const csReversibleTransform& t,
    const csVector3 &pos) const
{
  if (flags.Check (CS_PORTAL_WARP))
  {
    csReversibleTransform warp_wor;
    // @@@ Perhaps can be calculated more efficiently without having
    // to calculate a new transform?
    ObjectToWorld (t, warp_wor);
    return warp_wor.Other2This (pos);
  }
  else
  {
    return pos;
  }
}

void csPortal::WarpSpace (const csReversibleTransform& warp_wor,
    csReversibleTransform &t, bool &mirror) const
{
  // warp_wor is a world -> warp space transformation.
  // t is a world -> camera space transformation.
  // Set t to equal a warp -> camera space transformation by
  // reversing warp and then applying the old t.
  t /= warp_wor;
  if (flags.Check (CS_PORTAL_MIRROR)) mirror = !mirror;
}

bool csPortal::Draw (
  const csPoly2D& new_clipper,
  const csReversibleTransform& t,
  iRenderView *rview,
  const csPlane3& camera_plane)
{
  if (!CompleteSector (rview)) return false;
  if (sector->GetRecLevel () >= max_sector_visit)
    return false;

  if (!new_clipper.GetVertexCount ()) return false;

  CS::RenderManager::RenderView* csrview =
    (CS::RenderManager::RenderView*)rview;
  csRenderContext *old_ctxt = csrview->GetCsRenderContext ();
  iCamera *icam = old_ctxt->icamera;
  csPolygonClipper new_view ((csPoly2D*)&new_clipper,
  	icam->IsMirrored (), true);

  csrview->CreateRenderContext ();
  csrview->SetRenderRecursionLevel (csrview->GetRenderRecursionLevel () + 1);
  csrview->SetClipper (&new_view);
  csrview->ResetFogInfo ();
  csrview->SetLastPortal ((iPortal*)this);
  csrview->SetPreviousSector (rview->GetThisSector ());
  csrview->SetClipPlane (camera_plane);
  csrview->GetClipPlane ().Invert ();
  if (flags.Check (CS_PORTAL_CLIPDEST))
  {
    csrview->UseClipPlane (true);
    csrview->UseClipFrustum (true);
  }
  // When going through a portal we first remember the old clipper
  // and clip plane (if any). Then we set a new one. Later we restore.
  iGraphics3D *G3D = rview->GetGraphics3D ();
  csRef<iClipper2D> old_clipper = G3D->GetClipper ();

  int old_cliptype = G3D->GetClipType ();
  G3D->SetClipper (
      rview->GetClipper (),
      csrview->IsClipperRequired ()
      	? CS_CLIPPER_REQUIRED : CS_CLIPPER_OPTIONAL);

  csPlane3 old_near_plane = G3D->GetNearPlane ();
  bool old_do_near_plane = G3D->HasNearPlane ();
  csPlane3 cp;
  if (csrview->GetClipPlane (cp))
    G3D->SetNearPlane (cp);
  else
    G3D->ResetNearPlane ();

  if (flags.Check (CS_PORTAL_WARP))
  {
    iCamera *inewcam = csrview->CreateNewCamera ();

    bool mirror = inewcam->IsMirrored ();
    csReversibleTransform warp_wor;
    ObjectToWorld (t, warp_wor);
    WarpSpace (warp_wor, inewcam->GetTransform (), mirror);
    inewcam->SetMirrored (mirror);

    sector->Draw (rview);
  }
  else
    sector->Draw (rview);

  csrview->RestoreRenderContext ();

  // Now restore our G3D clipper and plane.
  G3D->SetClipper (old_clipper, old_cliptype);
  if (old_do_near_plane)
    G3D->SetNearPlane (old_near_plane);
  else
    G3D->ResetNearPlane ();

  return true;
}

iMeshWrapper* csPortal::HitBeamPortals (
  const csReversibleTransform& t,
  const csVector3 &start,
  const csVector3 &end,
  csVector3 &isect,
  int* polygon_idx,
  iSector** final_sector)
{
  if (!CompleteSector (0)) return 0;
  if (sector->GetRecLevel () >= max_sector_visit)
    return 0;
  if (flags.Check (CS_PORTAL_WARP))
  {
    csReversibleTransform warp_wor;
    // @@@ Perhaps can be calculated more efficiently without having
    // to calculate a new transform?
    ObjectToWorld (t, warp_wor);

    csVector3 new_start = warp_wor.Other2This (start);
    csVector3 new_end = warp_wor.Other2This (end);
    csSectorHitBeamResult hbresult = sector->HitBeamPortals (new_start, new_end);
    if (final_sector) *final_sector = hbresult.final_sector;
    if (hbresult.mesh)
    {
      if (polygon_idx) *polygon_idx = hbresult.polygon_idx;
      isect = warp_wor.This2Other (hbresult.isect);
    }
    return hbresult.mesh;
  }
  else
  {    
    csSectorHitBeamResult hbresult = sector->HitBeamPortals (start, end);
    if (final_sector) *final_sector = hbresult.final_sector;
    if (hbresult.mesh)
    {
      if (polygon_idx) *polygon_idx = hbresult.polygon_idx;
      isect = hbresult.isect;
    }
    return hbresult.mesh;
  }
}

void csPortal::SetMirror (const csPlane3& plane)
{
  SetWarp (csTransform::GetReflect (plane));
}

void csPortal::SetPortalCallback (iPortalCallback *cb)
{
  portal_cb_vector.Push (cb);
}

iPortalCallback *csPortal::GetPortalCallback (int idx) const
{
  return portal_cb_vector[idx];
}

void csPortal::SetMissingSectorCallback (iPortalCallback *cb)
{
  sector_cb_vector.Push (cb);
}

iPortalCallback *csPortal::GetMissingSectorCallback (int idx) const
{
  return sector_cb_vector[idx];
}

}
CS_PLUGIN_NAMESPACE_END(Engine)
