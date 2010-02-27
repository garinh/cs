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
#include "csgeom/transfrm.h"
#include "csgeom/math3d.h"
#include "csutil/blockallocator.h"

namespace {

struct vecar3 { csVector3 ar[3]; };
struct vecar4 { csVector3 ar[4]; };
struct vecar5 { csVector3 ar[5]; };
struct vecar6 { csVector3 ar[6]; };
struct vecar10 { csVector3 ar[10]; };

#include "csutil/custom_new_disable.h"

class csVertexArrayAlloc
{
public:
  csBlockAllocator<vecar3> blk_vecar3;
  csBlockAllocator<vecar4> blk_vecar4;
  csBlockAllocator<vecar5> blk_vecar5;
  csBlockAllocator<vecar6> blk_vecar6;
  csBlockAllocator<vecar10>* blk_vecar10;
  csVertexArrayAlloc () :
  	blk_vecar3 (400),
	  blk_vecar4 (400),
	  blk_vecar5 (100),
	  blk_vecar6 (100),
	  blk_vecar10 (0)
  {
  }

  ~csVertexArrayAlloc ()
  {
    delete blk_vecar10;
  }

  csVector3* GetVertexArray (size_t n)
  {
    if (!n) return 0;
    switch (n)
    {
      case 3: return blk_vecar3.Alloc ()->ar;
      case 4: return blk_vecar4.Alloc ()->ar;
      case 5: return blk_vecar5.Alloc ()->ar;
      case 6: return blk_vecar6.Alloc ()->ar;
      default:
        if (n <= 10)
        {
          if (!blk_vecar10) blk_vecar10 = new csBlockAllocator<vecar10> (100);
          return blk_vecar10->Alloc ()->ar;
        }
        else
        {
          csVector3* p = 
            static_cast<csVector3*> (cs_malloc (sizeof (csVector3) * n));
          for (size_t v = 0; v < n; v++) new (p + v) csVector3;
          return p;
        }
    }
    return 0;
  }

  /// Free an array of n vertices.
  void FreeVertexArray (csVector3* ar, size_t n)
  {
    if (!n) return;
    switch (n)
    {
      case 3: blk_vecar3.Free ((vecar3*)ar); break;
      case 4: blk_vecar4.Free ((vecar4*)ar); break;
      case 5: blk_vecar5.Free ((vecar5*)ar); break;
      case 6: blk_vecar6.Free ((vecar6*)ar); break;
      default:
        if (n <= 10)
          blk_vecar10->Free ((vecar10*)ar);
        else
        {
          for (size_t v = 0; v < n; v++)
            ar[v].~csVector3();
          cs_free (ar);
        }
        break;
    }
  }
};

#include "csutil/custom_new_enable.h"

CS_IMPLEMENT_STATIC_VAR (GetVertexArrayAlloc, csVertexArrayAlloc, ())

} // anonymous namespace

// OpenStep compiler generates corrupt assembly output (with unresolveable
// symbols) when this method is defined inline in the interface, so it is
// implemented here instead.
void csClipInfo::Clear ()
{
  if (type == CS_CLIPINFO_INSIDE)
  {
    delete inside.ci1;
    delete inside.ci2;
    type = CS_CLIPINFO_ORIGINAL;
  }
}

csFrustum::csFrustum (
  const csVector3 &o,
  csVector3 *verts,
  size_t num_verts,
  csPlane3 *backp)
{
  origin = o;
  num_vertices = num_verts;
  max_vertices = num_verts;
  wide = false;
  mirrored = false;
  ref_count = 1;

  if (verts)
  {
    vertices = GetVertexArrayAlloc ()->GetVertexArray (max_vertices);
    memcpy (vertices, verts, sizeof (csVector3) * num_vertices);
  }
  else
    vertices = 0;

  backplane = backp ? new csPlane3 (*backp) : 0;
}

csFrustum::csFrustum (
  const csVector3 &o,
  size_t num_verts,
  csPlane3 *backp)
{
  origin = o;
  num_vertices = num_verts;
  max_vertices = num_verts;
  wide = false;
  mirrored = false;
  ref_count = 1;

  vertices = GetVertexArrayAlloc ()->GetVertexArray (max_vertices);
  backplane = backp ? new csPlane3 (*backp) : 0;
}

csFrustum::csFrustum (const csFrustum &copy)
{
  origin = copy.origin;
  num_vertices = copy.num_vertices;
  max_vertices = copy.max_vertices;
  wide = copy.wide;
  mirrored = copy.mirrored;
  ref_count = 1;

  if (copy.vertices)
  {
    vertices = GetVertexArrayAlloc ()->GetVertexArray (max_vertices);
    memcpy (vertices, copy.vertices, sizeof (csVector3) * num_vertices);
  }
  else
    vertices = 0;

  backplane = copy.backplane ? new csPlane3 (*copy.backplane) : 0;
}

csFrustum::~csFrustum ()
{
  Clear ();
}

const csFrustum& csFrustum::operator= (const csFrustum& copy)
{
  Clear ();
  origin = copy.origin;
  num_vertices = copy.num_vertices;
  max_vertices = copy.max_vertices;
  wide = copy.wide;
  mirrored = copy.mirrored;

  if (copy.vertices)
  {
    vertices = GetVertexArrayAlloc ()->GetVertexArray (max_vertices);
    memcpy (vertices, copy.vertices, sizeof (csVector3) * num_vertices);
  }
  else
    vertices = 0;

  backplane = copy.backplane ? new csPlane3 (*copy.backplane) : 0;

  return *this;
}

void csFrustum::Clear ()
{
  GetVertexArrayAlloc ()->FreeVertexArray (vertices, max_vertices);
  vertices = 0;
  num_vertices = max_vertices = 0;
  delete backplane;
  backplane = 0;
  wide = false;
  mirrored = false;
}

void csFrustum::SetBackPlane (const csPlane3 &plane)
{
  delete backplane;
  backplane = new csPlane3 (plane);
}

void csFrustum::RemoveBackPlane ()
{
  delete backplane;
  backplane = 0;
}

void csFrustum::ExtendVertexArray (size_t num)
{
  csVector3 *new_vertices = GetVertexArrayAlloc ()->GetVertexArray (
  	max_vertices + num);
  if (vertices)
  {
    memcpy (new_vertices, vertices, sizeof (csVector3) * num_vertices);
    GetVertexArrayAlloc ()->FreeVertexArray (vertices, max_vertices);
  }

  vertices = new_vertices;
  max_vertices += num;
}

void csFrustum::AddVertex (const csVector3 &v)
{
  if (num_vertices >= max_vertices) ExtendVertexArray (10);
  vertices[num_vertices] = v;
  num_vertices++;
}

void csFrustum::MakeInfinite ()
{
  Clear ();
  wide = true;
}

void csFrustum::MakeEmpty ()
{
  Clear ();
  wide = false;
}

void csFrustum::Transform (csTransform *trans)
{
  size_t i;
  origin = trans->Other2This (origin);
  for (i = 0; i < num_vertices; i++)
    vertices[i] = trans->Other2ThisRelative (vertices[i]);
  if (backplane) (*backplane) *= (*trans);
}

void csFrustum::ClipPolyToPlane (csPlane3 *plane)
{
  // First classify all vertices of the current polygon with regards to this
  // plane.
  size_t i, i1;

  bool front[100];              // @@@ Hard coded limit.
  size_t count_front = 0;
  for (i = 0; i < num_vertices; i++)
  {
    front[i] = csMath3::Visible (vertices[i], *plane);
    if (front[i]) count_front++;
  }

  if (count_front == 0)
  {
    // None of the vertices of the new polygon are in front of the back
    // plane of this frustum. So intersection is empty.
    MakeEmpty ();
    return;
  }

  if (count_front == num_vertices)
  {
    // All vertices are in front. So nothing happens.
    return;
  }

  // Some of the vertices are in front, others are behind. So we
  // need to do real clipping.
  bool zs, z1s;
  csVector3 clipped_verts[100]; // @@@ Hard coded limit.
  size_t num_clipped_verts = 0;

  float r;
  i1 = num_vertices - 1;
  num_clipped_verts = 0;
  for (i = 0; i < num_vertices; i++)
  {
    zs = !front[i];
    z1s = !front[i1];

    if (z1s && !zs)
    {
      if (
        csIntersect3::SegmentPlane (
            vertices[i1],
            vertices[i],
            *plane,
            clipped_verts[num_clipped_verts],
            r))
        num_clipped_verts++;
      clipped_verts[num_clipped_verts++] = vertices[i];
    }
    else if (!z1s && zs)
    {
      if (
        csIntersect3::SegmentPlane (
            vertices[i1],
            vertices[i],
            *plane,
            clipped_verts[num_clipped_verts],
            r))
        num_clipped_verts++;
    }
    else if (!z1s && !zs)
    {
      clipped_verts[num_clipped_verts] = vertices[i];
      num_clipped_verts++;
    }

    i1 = i;
  }

  // If we have too little vertices, make frustrum empty
  if (num_clipped_verts < 3)
  {
    MakeEmpty ();
    return;
  }

  // Copy the clipped vertices. @@@ Is this efficient? Can't we clip in place?
  if (num_clipped_verts >= max_vertices)
    ExtendVertexArray (num_clipped_verts - max_vertices + 2);
  num_vertices = num_clipped_verts;
  for (i = 0; i < num_vertices; i++) vertices[i] = clipped_verts[i];
}

void csFrustum::ClipToPlane (csVector3 &v1, csVector3 &v2)
{
  size_t cw_offset = (size_t)~0;
  size_t ccw_offset;
  bool first_vertex_side;
  csVector3 isect_cw, isect_ccw;
  csVector3 Plane_Normal;
  size_t i;

  // Make sure that we have space in the array for at least three extra
  // vertices.
  if (num_vertices >= max_vertices - 3) ExtendVertexArray (3);

  // Do the check only once at the beginning instead of twice during the
  // routine.
  if (mirrored)
    Plane_Normal = v2 % v1;
  else
    Plane_Normal = v1 % v2;

  // On which side is the first vertex?
  first_vertex_side = (Plane_Normal * vertices[num_vertices - 1] > 0);

  for (i = 0; i < num_vertices - 1; i++)
  {
    if ((Plane_Normal * vertices[i] > 0) != first_vertex_side)
    {
      cw_offset = i;
      break;
    }
  }

  if (cw_offset == (size_t)~0)
  {
    // Return, if there is no intersection.
    if (first_vertex_side)
      MakeEmpty (); // The whole polygon is behind the plane because first is.
    return ;
  }

  //for (ccw_offset = num_vertices - 2; ccw_offset >= 0; ccw_offset--)
  for (ccw_offset = num_vertices - 1; ccw_offset-- > 0; )
  {
    if ((Plane_Normal * vertices[ccw_offset] > 0) != first_vertex_side)
      break;
  }

  // Calculate the intersection points.
  if (cw_offset == 0)
    i = num_vertices - 1;
  else
    i = cw_offset - 1;

  float dummy;
  csIntersect3::SegmentPlane (
    vertices[cw_offset],
    vertices[i],
    Plane_Normal,
    v1,
    isect_cw,
    dummy);
  csIntersect3::SegmentPlane (
    vertices[ccw_offset],
    vertices[ccw_offset + 1],
    Plane_Normal,
    v1,
    isect_ccw,
    dummy);

  // Remove the obsolete point and insert the intersection points.
  if (first_vertex_side)
  {
    for (i = 0; i < ccw_offset - cw_offset + 1; i++)
      vertices[i] = vertices[i + cw_offset];
    vertices[i] = isect_ccw;
    vertices[i + 1] = isect_cw;
    num_vertices = 3 + ccw_offset - cw_offset;
  }
  else
  {
    if (cw_offset + 1 < ccw_offset)
    {
      for (i = 0; i < num_vertices - ccw_offset - 1; i++)
        vertices[cw_offset + 2 + i] = vertices[ccw_offset + 1 + i];
    }
    else if (cw_offset + 1 > ccw_offset)
    {
      //for (i = num_vertices - 2 - ccw_offset; i-- > 0; )
      for (i = num_vertices - 1 - ccw_offset; i-- > 0; )
        vertices[cw_offset + 2 + i] = vertices[ccw_offset + 1 + i];
    }

    vertices[cw_offset] = isect_cw;
    vertices[cw_offset + 1] = isect_ccw;
    num_vertices = 2 + cw_offset + num_vertices - ccw_offset - 1;
  }
}

void csFrustum::ClipToPlane (
  csVector3 *vertices,
  size_t &num_vertices,
  csClipInfo *clipinfo,
  const csPlane3 &plane)
{
  size_t cw_offset = (size_t)~0;
  size_t ccw_offset;
  bool first_vertex_side;
  csVector3 isect_cw, isect_ccw;
  size_t i;

  // On which side is the first vertex?
  first_vertex_side = (plane.Classify (vertices[num_vertices - 1]) > 0);

  for (i = 0; i < num_vertices - 1; i++)
  {
    if ((plane.Classify (vertices[i]) > 0) != first_vertex_side)
    {
      cw_offset = i;
      break;
    }
  }

  if (cw_offset == (size_t)~0)
  {
    // Return, if there is no intersection.
    if (first_vertex_side)
      num_vertices = 0;  // The whole polygon is behind the plane.

    // because the first is.
    return;
  }

  //for (ccw_offset = num_vertices - 2; ccw_offset >= 0; ccw_offset--)
  for (ccw_offset = num_vertices - 1; ccw_offset-- > 0; )
  {
    if ((plane.Classify (vertices[ccw_offset]) > 0) != first_vertex_side)
      break;
  }

  // Calculate the intersection points.
  if (cw_offset == 0)
    i = num_vertices - 1;
  else
    i = cw_offset - 1;

  float dist_cw, dist_ccw;
  csIntersect3::SegmentPlane (
    vertices[cw_offset],
    vertices[i],
    plane,
    isect_cw,
    dist_cw);

  csClipInfo clip_cw;
  if (clipinfo[cw_offset].type != CS_CLIPINFO_ORIGINAL ||
    clipinfo[i].type != CS_CLIPINFO_ORIGINAL)
  {
    clip_cw.type = CS_CLIPINFO_INSIDE;
    clip_cw.inside.r = dist_cw;
    clip_cw.inside.ci1 = new csClipInfo ();
    clip_cw.inside.ci1->Copy (clipinfo[cw_offset]);
    clip_cw.inside.ci2 = new csClipInfo ();
    clip_cw.inside.ci2->Copy (clipinfo[i]);
  }
  else
  {
    clip_cw.type = CS_CLIPINFO_ONEDGE;
    clip_cw.onedge.r = dist_cw;
    clip_cw.onedge.i1 = clipinfo[cw_offset].original.idx;
    clip_cw.onedge.i2 = clipinfo[i].original.idx;
  }

  csIntersect3::SegmentPlane (
    vertices[ccw_offset],
    vertices[ccw_offset + 1],
    plane,
    isect_ccw,
    dist_ccw);

  csClipInfo clip_ccw;
  if (clipinfo[ccw_offset].type != CS_CLIPINFO_ORIGINAL ||
    clipinfo[ccw_offset + 1].type != CS_CLIPINFO_ORIGINAL)
  {
    clip_ccw.type = CS_CLIPINFO_INSIDE;
    clip_ccw.inside.r = dist_ccw;
    clip_ccw.inside.ci1 = new csClipInfo ();
    clip_ccw.inside.ci1->Copy (clipinfo[ccw_offset]);
    clip_ccw.inside.ci2 = new csClipInfo ();
    clip_ccw.inside.ci2->Copy (clipinfo[ccw_offset + 1]);
  }
  else
  {
    clip_ccw.type = CS_CLIPINFO_ONEDGE;
    clip_ccw.onedge.r = dist_ccw;
    clip_ccw.onedge.i1 = clipinfo[ccw_offset].original.idx;
    clip_ccw.onedge.i2 = clipinfo[ccw_offset + 1].original.idx;
  }

  // Remove the obsolete point and insert the intersection points.
  if (first_vertex_side)
  {
    for (i = 0; i < ccw_offset - cw_offset + 1; i++)
    {
      vertices[i] = vertices[i + cw_offset];
      clipinfo[i].Copy (clipinfo[i + cw_offset]);
    }

    vertices[i] = isect_ccw;
    clipinfo[i].Copy (clip_ccw);
    vertices[i + 1] = isect_cw;
    clipinfo[i + 1].Copy (clip_cw);
    num_vertices = 3 + ccw_offset - cw_offset;
  }
  else
  {
    if (cw_offset + 1 < ccw_offset)
    {
      for (i = 0; i < num_vertices - ccw_offset - 1; i++)
      {
        vertices[cw_offset + 2 + i] = vertices[ccw_offset + 1 + i];
        clipinfo[cw_offset + 2 + i].Copy (clipinfo[ccw_offset + 1 + i]);
      }
    }
    else if (cw_offset + 1 > ccw_offset)
    {
      //for (i = num_vertices - 2 - ccw_offset; i-- > 0; )
      for (i = num_vertices - 1 - ccw_offset; i-- > 0; )
      {
        vertices[cw_offset + 2 + i] = vertices[ccw_offset + 1 + i];
        clipinfo[cw_offset + 2 + i].Copy (clipinfo[ccw_offset + 1 + i]);
      }
    }

    vertices[cw_offset] = isect_cw;
    clipinfo[cw_offset].Copy (clip_cw);
    vertices[cw_offset + 1] = isect_ccw;
    clipinfo[cw_offset + 1].Copy (clip_ccw);
    num_vertices = 2 + cw_offset + num_vertices - ccw_offset - 1;
  }
}

void csFrustum::ClipToPlane (
  csVector3 *vertices,
  size_t &num_vertices,
  csClipInfo *clipinfo,
  const csVector3 &v1,
  const csVector3 &v2)
{
  size_t cw_offset = ~0;
  size_t ccw_offset;
  bool first_vertex_side;
  csVector3 isect_cw, isect_ccw;
  csVector3 Plane_Normal;
  size_t i;

  // Do the check only once at the beginning.
  Plane_Normal = v1 % v2;

  // On which side is the first vertex?
  first_vertex_side = (Plane_Normal * vertices[num_vertices - 1] > 0);

  for (i = 0; i < num_vertices - 1; i++)
  {
    if ((Plane_Normal * vertices[i] > 0) != first_vertex_side)
    {
      cw_offset = i;
      break;
    }
  }

  if (cw_offset == (size_t)~0)
  {
    // Return, if there is no intersection.
    if (first_vertex_side)
      num_vertices = 0;  // The whole polygon is behind the plane.

    // because the first is.
    return ;
  }

  //for (ccw_offset = num_vertices - 2; ccw_offset >= 0; ccw_offset--)
  for (ccw_offset = num_vertices - 1; ccw_offset-- > 0; )
  {
    if ((Plane_Normal * vertices[ccw_offset] > 0) != first_vertex_side)
      break;
  }

  // Calculate the intersection points.
  if (cw_offset == 0)
    i = num_vertices - 1;
  else
    i = cw_offset - 1;

  float dist_cw, dist_ccw;
  csIntersect3::SegmentPlane (
    vertices[cw_offset],
    vertices[i],
    Plane_Normal,
    v1,
    isect_cw,
    dist_cw);

  csClipInfo clip_cw;
  if (
    clipinfo[cw_offset].type != CS_CLIPINFO_ORIGINAL ||
    clipinfo[i].type != CS_CLIPINFO_ORIGINAL)
  {
    clip_cw.type = CS_CLIPINFO_INSIDE;
    clip_cw.inside.r = dist_cw;
    clip_cw.inside.ci1 = new csClipInfo ();
    clip_cw.inside.ci1->Copy (clipinfo[cw_offset]);
    clip_cw.inside.ci2 = new csClipInfo ();
    clip_cw.inside.ci2->Copy (clipinfo[i]);
  }
  else
  {
    clip_cw.type = CS_CLIPINFO_ONEDGE;
    clip_cw.onedge.r = dist_cw;
    clip_cw.onedge.i1 = clipinfo[cw_offset].original.idx;
    clip_cw.onedge.i2 = clipinfo[i].original.idx;
  }

  csIntersect3::SegmentPlane (
    vertices[ccw_offset],
    vertices[ccw_offset + 1],
    Plane_Normal,
    v1,
    isect_ccw,
    dist_ccw);

  csClipInfo clip_ccw;
  if (clipinfo[ccw_offset].type != CS_CLIPINFO_ORIGINAL ||
    clipinfo[ccw_offset + 1].type != CS_CLIPINFO_ORIGINAL)
  {
    clip_ccw.type = CS_CLIPINFO_INSIDE;
    clip_ccw.inside.r = dist_ccw;
    clip_ccw.inside.ci1 = new csClipInfo ();
    clip_ccw.inside.ci1->Copy (clipinfo[ccw_offset]);
    clip_ccw.inside.ci2 = new csClipInfo ();
    clip_ccw.inside.ci2->Copy (clipinfo[ccw_offset + 1]);
  }
  else
  {
    clip_ccw.type = CS_CLIPINFO_ONEDGE;
    clip_ccw.onedge.r = dist_ccw;
    clip_ccw.onedge.i1 = clipinfo[ccw_offset].original.idx;
    clip_ccw.onedge.i2 = clipinfo[ccw_offset + 1].original.idx;
  }

  // Remove the obsolete point and insert the intersection points.
  if (first_vertex_side)
  {
    for (i = 0; i < ccw_offset - cw_offset + 1; i++)
    {
      vertices[i] = vertices[i + cw_offset];
      clipinfo[i].Copy (clipinfo[i + cw_offset]);
    }

    vertices[i] = isect_ccw;
    clipinfo[i].Copy (clip_ccw);
    vertices[i + 1] = isect_cw;
    clipinfo[i + 1].Copy (clip_cw);
    num_vertices = 3 + ccw_offset - cw_offset;
  }
  else
  {
    if (cw_offset + 1 < ccw_offset)
    {
      for (i = 0; i < num_vertices - ccw_offset - 1; i++)
      {
        vertices[cw_offset + 2 + i] = vertices[ccw_offset + 1 + i];
        clipinfo[cw_offset + 2 + i].Copy (clipinfo[ccw_offset + 1 + i]);
      }
    }
    else if (cw_offset + 1 > ccw_offset)
    {
      //for (i = num_vertices - 2 - ccw_offset; i-- > 0; )
      for (i = num_vertices - 1 - ccw_offset; i-- > 0; )
      {
        vertices[cw_offset + 2 + i] = vertices[ccw_offset + 1 + i];
        clipinfo[cw_offset + 2 + i].Copy (clipinfo[ccw_offset + 1 + i]);
      }
    }

    vertices[cw_offset] = isect_cw;
    clipinfo[cw_offset].Copy (clip_cw);
    vertices[cw_offset + 1] = isect_ccw;
    clipinfo[cw_offset + 1].Copy (clip_ccw);
    num_vertices = 2 + cw_offset + num_vertices - ccw_offset - 1;
  }
}

csPtr<csFrustum> csFrustum::Intersect (const csFrustum &other) const
{
  if (other.IsEmpty ()) return 0;
  if (other.IsInfinite ())
  {
    csRef<csFrustum> f;
    f.AttachNew (new csFrustum (*this));
    return csPtr<csFrustum> (f);
  }

  return Intersect (other.vertices, other.num_vertices);
}

csPtr<csFrustum> csFrustum::Intersect (
  const csVector3 &frust_origin,
  csVector3 *frust,
  size_t num_frust,
  const csVector3 &v1,
  const csVector3 &v2,
  const csVector3 &v3)
{
  csRef<csFrustum> new_frustum;

  // General case. Create a new frustum from the given polygon with
  // the origin of this frustum and clip it to every plane from this
  // frustum.
  new_frustum.AttachNew (new csFrustum (frust_origin));
  new_frustum->AddVertex (v1);
  new_frustum->AddVertex (v2);
  new_frustum->AddVertex (v3);

  size_t i, i1;
  i1 = num_frust - 1;
  for (i = 0; i < num_frust; i++)
  {
    new_frustum->ClipToPlane (frust[i1], frust[i]);
    if (new_frustum->IsEmpty ())
    {
      // Intersection has become empty. Return 0.
      return 0;
    }

    i1 = i;
  }

  return csPtr<csFrustum> (new_frustum);
}

csPtr<csFrustum> csFrustum::Intersect (
  const csVector3 &frust_origin,
  csVector3 *frust,
  size_t num_frust,
  csVector3 *poly,
  size_t num)
{
  csRef<csFrustum> new_frustum;

  // General case. Create a new frustum from the given polygon with
  // the origin of this frustum and clip it to every plane from this
  // frustum.
  new_frustum.AttachNew (new csFrustum (frust_origin, poly, num));

  size_t i, i1;
  i1 = num_frust - 1;
  for (i = 0; i < num_frust; i++)
  {
    new_frustum->ClipToPlane (frust[i1], frust[i]);
    if (new_frustum->IsEmpty ())
    {
      // Intersection has become empty. Return 0.
      return 0;
    }

    i1 = i;
  }

  return csPtr<csFrustum> (new_frustum);
}

csPtr<csFrustum> csFrustum::Intersect (csVector3 *poly, size_t num) const
{
  csRef<csFrustum> new_frustum;
  if (IsInfinite ())
  {
    // If this frustum is infinite then the intersection of this
    // frustum with the other is equal to the other.
    new_frustum.AttachNew (new csFrustum (origin, poly, num));
    new_frustum->SetMirrored (IsMirrored ());
  }
  else if (IsEmpty ())
  {
    // If this frustum is empty then the intersection will be empty
    // as well.
    return 0;
  }
  else
  {
    // General case. Create a new frustum from the given polygon with
    // the origin of this frustum and clip it to every plane from this
    // frustum.
    new_frustum.AttachNew (new csFrustum (GetOrigin (), poly, num));
    new_frustum->SetMirrored (IsMirrored ());

    size_t i, i1;
    i1 = num_vertices - 1;
    for (i = 0; i < num_vertices; i++)
    {
      new_frustum->ClipToPlane (vertices[i1], vertices[i]);
      if (new_frustum->IsEmpty ())
      {
        // Intersection has become empty. Return 0.
        return 0;
      }

      i1 = i;
    }

    // If this frustum has a back plane then we also need to clip the polygon
    // in the new frustum to that.
    if (backplane)
    {
      new_frustum->ClipPolyToPlane (backplane);
      if (new_frustum->IsEmpty ())
      {
        // Intersection has become empty. Return 0.
        return 0;
      }
    }
  }

  return csPtr<csFrustum> (new_frustum);
}

bool csFrustum::Intersect (csSegment3& segment)
{
  // Any segment is part of an infinite frustum
  if (IsInfinite ())
    return true;

  csSegment3 newSegment (segment.Start () - origin, segment.End () - origin);

  if (backplane)
  {
    //Handle backplane
    bool startB = (backplane->Classify (newSegment.Start ()) < 0);
    bool endB = (backplane->Classify (newSegment.End ()) < 0);

    if (startB || endB)
    {
      // Either in front, no modification, we do intersect
      csIntersect3::SegmentPlane (*backplane, newSegment);
    }
    else
    {
      // Both behind, no intersection, no modification
      return false; 
    }
  }

  // Now intersect all the others
  // Loop through all planes that makes the frustum
  for (size_t fv = 0, fvp = num_vertices - 1; fv < num_vertices; fvp = fv++)
  {
    const csVector3 &v1 = vertices[fvp];
    const csVector3 &v2 = vertices[fv];
    
    /// Clip segment against plane origo,v1,v2
    const csPlane3 pl (v1, v2);

    bool startPl = (pl.Classify (newSegment.Start ()) < 0);
    bool endPl = (pl.Classify (newSegment.End ()) < 0);

    if (startPl || endPl)
    {
      // Either in front, no modification, we do intersect
      if (!startPl || !endPl)
        csIntersect3::SegmentPlane (pl, newSegment);
    }
    else
    {
      // Both behind, no intersection, no modification
      return false;
    }
  }

  segment = csSegment3 (newSegment.Start ()+origin, newSegment.End ()+origin);

  return true;
}

bool csFrustum::Contains (const csVector3 &point)
{
  if (backplane)
    return Contains (vertices, num_vertices, *backplane, point);
  return Contains (vertices, num_vertices, point);
}

bool csFrustum::Contains (
  csVector3 *frustum,
  size_t num_frust,
  const csVector3 &point)
{
  size_t i, i1;
  i1 = num_frust - 1;
  for (i = 0; i < num_frust; i++)
  {
    if (csMath3::WhichSide3D (point, frustum[i], frustum[i1]) > 0)
      return false;
    i1 = i;
  }

  return true;
}

bool csFrustum::Contains (
  csVector3 *frustum,
  size_t num_frust,
  const csPlane3 &plane,
  const csVector3 &point)
{
  if (!csMath3::Visible (point, plane)) return false;

  size_t i, i1;
  i1 = num_frust - 1;
  for (i = 0; i < num_frust; i++)
  {
    if (csMath3::WhichSide3D (point, frustum[i], frustum[i1]) > 0)
      return false;
    i1 = i;
  }

  return true;
}

int csFrustum::Classify (
  csVector3 *frustum,
  size_t num_frust,
  csVector3 *poly,
  size_t num_poly)
{
  // All poly vertices are inside frustum?
  bool all_inside = true;

  // Loop through all planes that makes the frustum
  size_t fv, fvp, pv, pvp;
  for (fv = 0, fvp = num_frust - 1; fv < num_frust; fvp = fv++)
  {
    // Find the equation of the Nth plane
    // Since the origin of frustum is at (0,0,0) the plane equation
    // has the form A*x + B*y + C*z = 0, where (A,B,C) is the plane normal.
    csVector3 &v1 = frustum[fvp];
    csVector3 &v2 = frustum[fv];
    csVector3 fn = v1 % v2;

    float prev_d = fn * poly[num_poly - 1];
    for (pv = 0, pvp = num_poly - 1; pv < num_poly; pvp = pv++)
    {
      // The distance from plane to polygon vertex
      float d = fn * poly[pv];
      if (all_inside && d > 0) all_inside = false;

      if ((prev_d < 0 && d > 0) || (prev_d > 0 && d < 0))
      {
#if 1
        // This version should be faster.
        if (((poly[pvp] % v1) * poly[pv]) * prev_d >= 0)
          if (((v2 % poly[pvp]) * poly[pv]) * prev_d >= 0)
            return CS_FRUST_PARTIAL;

        //float f1 = (poly [pvp] % v1) * poly [pv];
        //float f2 = (v2 % poly [pvp]) * poly [pv];
        //if (!(f1 > 0 || f2 > 0)
        //|| (f1 == 0) || (f2 == 0))
        //return CS_FRUST_PARTIAL;
#else
        // If the segment intersects with the frustum plane somewhere
        // between two limiting lines, we have the CS_FRUST_PARTIAL case.
        csVector3 p = poly[pvp] - (poly[pv] - poly[pvp]) *
          (prev_d / (d - prev_d));

        // If the vector product between both vectors making the frustum
        // plane and the intersection point between polygon edge and plane
        // have same sign, the intersection point is outside frustum
        //@@@ This is the old code but I think this code
        // should test against the frustum.
        //--- if ((poly [pvp] % p) * (poly [pv] % p) < 0)
        if ((v1 % p) * (v2 % p) <= 0) return CS_FRUST_PARTIAL;
#endif
      }

      prev_d = d;
    }
  }

  if (all_inside) return CS_FRUST_INSIDE;

  // Now we know that all polygon vertices are outside frustum and no polygon
  // edge crosses the frustum. We should choose between CS_FRUST_OUTSIDE and
  // CS_FRUST_COVERED cases. For this we check if all frustum vertices are
  // inside the frustum created by polygon (reverse roles). In fact it is
  // enough to check just the first vertex of frustum is outside polygon
  // frustum: this lets us make the right decision.
  // Note: except if this first vertex happens to coincide with polygon.
  // In that case we need to select another vertex. If all vertices
  // coincide with the polygon then we have COVERED too.
  size_t test_point = 0;
  bool stop = true;
  while (test_point < num_frust)
  {
    for (pv = 0, pvp = num_poly - 1; pv < num_poly; pvp = pv++)
    {
      csVector3 pn = poly[pvp] % poly[pv];
      float c = pn * frustum[test_point];
      if (c >= EPSILON) return CS_FRUST_OUTSIDE;
      if (ABS (c) < EPSILON)
      {
        test_point++;
        stop = false;
        break;
      }
    }

    if (stop) break;
    stop = true;
  }

  return CS_FRUST_COVERED;
}

/**
 * This is like the above version except that it takes a vector of
 * precalculated frustum plane normals. Use this if you have to classify a
 * batch of polygons against the same frustum.
 */
int csFrustum::BatchClassify (
  csVector3 *frustum,
  csVector3 *frustumNormals,
  size_t num_frust,
  csVector3 *poly,
  size_t num_poly)
{
  // All poly vertices are inside frustum?
  bool all_inside = true;

  // Loop through all planes that makes the frustum
  size_t fv, fvp, pv, pvp;
  for (fv = 0, fvp = num_frust - 1; fv < num_frust; fvp = fv++)
  {
    csVector3 &v1 = frustum[fvp];
    csVector3 &v2 = frustum[fv];
    csVector3 &fn = frustumNormals[fvp];

    float prev_d = fn * poly[num_poly - 1];
    for (pv = 0, pvp = num_poly - 1; pv < num_poly; pvp = pv++)
    {
      // The distance from plane to polygon vertex
      float d = fn * poly[pv];
      if (all_inside && d > 0) all_inside = false;

      if ((prev_d < 0 && d > 0) || (prev_d > 0 && d < 0))
      {
#if 1
        // This version should be faster.
        if (((poly[pvp] % v1) * poly[pv]) * prev_d >= 0)
          if (((v2 % poly[pvp]) * poly[pv]) * prev_d >= 0)
            return CS_FRUST_PARTIAL;

#else
        // If the segment intersects with the frustum plane somewhere
        // between two limiting lines, we have the CS_FRUST_PARTIAL case.
        csVector3 p = poly[pvp] - (poly[pv] - poly[pvp]) *
          (prev_d / (d - prev_d));

        // If the vector product between both vectors making the frustum
        // plane and the intersection point between polygon edge and plane
        // have same sign, the intersection point is outside frustum
        //@@@ This is the old code but I think this code
        // should test against the frustum.
        //--- if ((poly [pvp] % p) * (poly [pv] % p) < 0)
        if ((v1 % p) * (v2 % p) <= 0) return CS_FRUST_PARTIAL;
#endif
      }

      prev_d = d;
    }
  }

  if (all_inside) return CS_FRUST_INSIDE;

  // Now we know that all polygon vertices are outside frustum and no polygon
  // edge crosses the frustum. We should choose between CS_FRUST_OUTSIDE and
  // CS_FRUST_COVERED cases. For this we check if all frustum vertices are
  // inside the frustum created by polygon (reverse roles). In fact it is
  // enough to check just the first vertex of frustum is outside polygon
  // frustum: this lets us make the right decision.
  // Note: except if this first vertex happens to coincide with polygon.
  // In that case we need to select another vertex. If all vertices
  // coincide with the polygon then we have COVERED too.
  size_t test_point = 0;
  bool stop = true;
  while (test_point < num_frust)
  {
    for (pv = 0, pvp = num_poly - 1; pv < num_poly; pvp = pv++)
    {
      csVector3 pn = poly[pvp] % poly[pv];
      float c = pn * frustum[test_point];
      if (c >= EPSILON) return CS_FRUST_OUTSIDE;
      if (ABS (c) < EPSILON)
      {
        test_point++;
        stop = false;
        break;
      }
    }

    if (stop) break;
    stop = true;
  }

  return CS_FRUST_COVERED;
}
