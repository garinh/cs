/*
    Copyright (C) 1998-2005 by Jorrit Tyberghein
    Largely rewritten by Ivan Avramovic <ivan@avramovic.com>

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
#include <math.h>
#include <float.h>
#include "csqsqrt.h"

#include "csgeom/box.h"
#include "csgeom/math3d.h"
#include "csgeom/math3d_d.h"
#include "csgeom/matrix3.h"
#include "csgeom/plane2.h"
#include "csgeom/plane3.h"
#include "csgeom/poly3d.h"
#include "csgeom/segment.h"

#include "csutil/scfstr.h"
#include "csutil/sysfunc.h"

//---------------------------------------------------------------------------
void csMath3::Between (
  const csVector3 &v1,
  const csVector3 &v2,
  csVector3 &v,
  float pct,
  float wid)
{
  if (pct != -1)
    pct /= 100.;
  else
  {
    float sqdist = (v1 - v2) * (v1 - v2);
    if (sqdist < SMALL_EPSILON)
    {
      v = v1;
      return ;
    }

    float invdist = csQisqrt (sqdist);
    pct = wid * invdist;
  }

  v = v1 + pct * (v2 - v1);
}

bool csMath3::Visible (
  const csVector3 &p,
  const csVector3 &t1,
  const csVector3 &t2,
  const csVector3 &t3)
{
  float x1 = t1.x - p.x;
  float y1 = t1.y - p.y;
  float z1 = t1.z - p.z;
  float x2 = t2.x - p.x;
  float y2 = t2.y - p.y;
  float z2 = t2.z - p.z;
  float x3 = t3.x - p.x;
  float y3 = t3.y - p.y;
  float z3 = t3.z - p.z;
  float c = x3 * ((z1 * y2) - (y1 * z2)) + y3 * ((x1 * z2) - (z1 * x2)) + z3 *
    ((y1 * x2) - (x1 * y2));
  return c > 0;
}

bool csMath3::PlanesClose (const csPlane3 &p1, const csPlane3 &p2)
{
  if (PlanesEqual (p1, p2)) return true;

  csPlane3 p1n = p1;
  p1n.Normalize ();

  csPlane3 p2n = p2;
  p2n.Normalize ();
  return PlanesEqual (p1n, p2n);
}

int csMath3::OuterPlanes (
  const csBox3 &box1,
  const csBox3 &box2,
  csPlane3 *planes)
{
  int i, j, k;

  // @@@ This is a HIGHLY INEFFICIENT routine.
  // However, I haven't yet found something better.
  int num_planes = 0;
  for (i = 0; i < 8; i++)
  {
    csVector3 v1 = box1.GetCorner (i);

    // Try 24 edges instead of 12. The 12 other edges
    // are just edges with inverted direction.
    for (j = 0; j < 24; j++)
    {
      csSegment3 seg = box2.GetEdge (j);
      csVector3 v2a, v2b;
      v2a = seg.Start ();
      v2b = seg.End ();

      csPlane3 pl (v1, v2a, v2b);
      pl.Normalize ();

      // Check if we already have this plane.
      bool equal = false;
      for (k = 0; k < num_planes; k++)
        if (csMath3::PlanesEqual (planes[k], pl))
        {
          equal = true;
          break;
        }

      if (equal) continue;

      // Count how many vertices of the two boxes are inside or outside
      // the plane. We need planes with all vertices either
      // on the plane or inside.
      int cnt_out = 0;
      for (k = 0; k < 8; k++)
      {
        float cl = pl.Classify (box1.GetCorner (k));
        if (cl < -EPSILON)
        {
          cnt_out++;
          break;
        }

        cl = pl.Classify (box2.GetCorner (k));
        if (cl < -EPSILON)
        {
          cnt_out++;
          break;
        }
      }

      // If no vertices are outside then we have a good plane.
      if (cnt_out == 0)
      {
        CS_ASSERT (num_planes < 8);
        planes[num_planes++] = pl;
      }
    }
  }

  return num_planes;
}

int csMath3::FindObserverSides (
  const csBox3 &box1,
  const csBox3 &box2,
  int *sides)
{
  int num_sides = 0;
  csPlane3 pl;
  pl.Set (1, 0, 0, -box1.MinX ());
  if (pl.Classify (box2.GetCorner (CS_BOX_CORNER_xyz)) < 0)
    sides[num_sides++] = 0;
  pl.Set (-1, 0, 0, box1.MaxX ());
  if (pl.Classify (box2.GetCorner (CS_BOX_CORNER_Xyz)) < 0)
    sides[num_sides++] = 1;
  pl.Set (0, 1, 0, -box1.MinY ());
  if (pl.Classify (box2.GetCorner (CS_BOX_CORNER_xyz)) < 0)
    sides[num_sides++] = 2;
  pl.Set (0, -1, 0, box1.MaxY ());
  if (pl.Classify (box2.GetCorner (CS_BOX_CORNER_xYz)) < 0)
    sides[num_sides++] = 3;
  pl.Set (0, 0, 1, -box1.MinZ ());
  if (pl.Classify (box2.GetCorner (CS_BOX_CORNER_xyz)) < 0)
    sides[num_sides++] = 4;
  pl.Set (0, 0, -1, box1.MaxZ ());
  if (pl.Classify (box2.GetCorner (CS_BOX_CORNER_xyZ)) < 0)
    sides[num_sides++] = 5;
  return num_sides;
}

void csMath3::SpherePosition (
  float angle_xz,
  float angle_vert,
  csVector3 &pos)
{
  float cosxz = cos (angle_xz);
  float sinxz = sin (angle_xz);
  float cosve = cos (angle_vert);
  float sinve = sin (angle_vert);
  pos.Set (cosxz * cosve, sinve, sinxz * cosve);
}

//---------------------------------------------------------------------------
float csSquaredDist::PointLine (
  const csVector3 &p,
  const csVector3 &l1,
  const csVector3 &l2)
{
  csVector3 W = l1 - p;
  csVector3 L = l2 - l1;
  csVector3 p2l = W - L * (W * L) / (L * L);
  return p2l * p2l;
}

float csSquaredDist::PointPoly (
  const csVector3 &p,
  csVector3 *V,
  int n,
  const csPlane3 &plane,
  float sqdist)
{
  csVector3 W, L;
  bool lflag = true, lflag0 = true;
  int i;
  for (i = 0; i < n - 1; i++)
  {
    W = V[i] - p;
    if (i == 0)
    {
      if (!(W * (V[n - 1] - V[0]) > 0))
        lflag0 = false;
      else if (W * (V[1] - V[0]) > 0)
        return W * W;
      else
        lflag = false;
    }
    else if (!(W * (L = V[i - 1] - V[i]) > 0))
    {
      if (!lflag && W * (plane.norm % L) > 0)
      {
        L = W - L * (W * L) / (L * L);
        return L * L;
      }

      lflag = (W * (V[i + 1] - V[i]) > 0);
    }
    else if (W * (V[i + 1] - V[i]) > 0)
      return W * W;
    else
      lflag = false;
  }

  W = V[n - 1] - p;
  if (!lflag)
  {
    lflag = W * (L = V[n - 2] - V[n - 1]) <= 0;
    if (lflag && (W * (plane.norm % L) > 0))
    {
      L = W - L * (W * L) / (L * L);
      return L * L;
    }
  }

  if (!lflag0)
  {
    lflag0 = W * (L = V[0] - V[n - 1]) <= 0;
    if (lflag0 && (W * (plane.norm % L) < 0))
    {
      L = W - L * (W * L) / (L * L);
      return L * L;
    }
  }

  if (!lflag && !lflag0) return W * W;
  if (sqdist >= 0) return sqdist;
  return csSquaredDist::PointPlane (p, plane);
}

//---------------------------------------------------------------------------
int csIntersect3::SegmentFrustum (
  csPlane3 *planes,
  int num_planes,
  csSegment3 &seg)
{
  csVector3 &v1 = seg.Start ();
  csVector3 &v2 = seg.End ();
  csVector3 isect;
  float dist;
  bool mod = false;
  bool out1 = false, out2 = false;  // If v1 or v2 is out the frustum.
  int i;
  for (i = 0; i < num_planes; i++)
  {
    csPlane3 &pl = planes[i];
    float c1 = pl.Classify (v1);
    float c2 = pl.Classify (v2);
    if (c1 < 0) out1 = true;
    if (c2 < 0) out2 = true;
    if (c1 < 0 && c2 > 0)
    {
      if (SegmentPlane (v1, v2, pl, isect, dist))
      {
        mod = true;
        v1 = isect;
        if ((v2 - v1) < (float).0001) return -1;
      }
    }
    else if (c1 > 0 && c2 < 0)
    {
      if (SegmentPlane (v1, v2, pl, isect, dist))
      {
        mod = true;
        v2 = isect;
        if ((v2 - v1) < (float).0001) return -1;
      }
    }
  }

  if (out1 && out2 && !mod) return -1;
  return mod ? 1 : 0;
}

bool csIntersect3::PlanePolygon (
  const csPlane3 &plane,
  csPoly3D *poly,
  csSegment3 &seg)
{
  csVector3 &v1 = seg.Start ();
  csVector3 &v2 = seg.End ();
  size_t i, i1;
  float c, c1;
  csVector3 isect;
  float dist;
  i1 = poly->GetVertexCount () - 1;
  c1 = plane.Classify ((*poly)[i1]);

  bool found_v1 = false;
  bool found_v2 = false;
  for (i = 0; i < poly->GetVertexCount (); i++)
  {
    c = plane.Classify ((*poly)[i]);
    if ((c < 0 && c1 > 0) || (c1 < 0 && c > 0))
    {
      SegmentPlane ((*poly)[i1], (*poly)[i], plane, isect, dist);
      if (!found_v1)
      {
        v1 = isect;
        found_v1 = true;
      }
      else
      {
        v2 = isect;
        found_v2 = true;
        break;
      }
    }

    i1 = i;
    c1 = c;
  }

  if (!found_v1) return false;
  if (!found_v2) v2 = v1;
  return true;
}

bool csIntersect3::SegmentTriangle (
  const csSegment3 &seg,
  const csVector3 &tr1,
  const csVector3 &tr2,
  const csVector3 &tr3,
  csVector3 &isect)
{
  csPlane3 plane (tr1, tr2, tr3);
  float dist;
  if (!SegmentPlane (seg.Start (), seg.End (), plane, isect, dist))
    return false;

  // 'isect' is the intersection of the segment and the
  // plane. Now we have to see if this intersection is
  // in the triangle.
  int test1, test2, test3;

  // Check if intersected plane is sufficiently away from origin.
  // Note: don't use too small of an epsilon, or else
  // we might try using WhichSide3D on a plane that is too
  // close to the origin to yield accurate results.
  if (plane.D () > EPSILON)
  {
    test1 = csMath3::WhichSide3D (isect, tr3, tr1);
    test2 = csMath3::WhichSide3D (isect, tr1, tr2);
    if (test1 != test2 && test1 != 0 && test2 != 0) return false;
    test3 = csMath3::WhichSide3D (isect, tr2, tr3);
  }
  else  // shift plane because it is too close to origin
  {
    csVector3 norm = plane.Normal ();
    csVector3 nsect = isect + norm;
    csVector3 ntr1 = tr1 + norm;
    csVector3 ntr2 = tr2 + norm;
    csVector3 ntr3 = tr3 + norm;
    test1 = csMath3::WhichSide3D (nsect, ntr3, ntr1);
    test2 = csMath3::WhichSide3D (nsect, ntr1, ntr2);
    if (test1 != test2 && test1 != 0 && test2 != 0) return false;
    test3 = csMath3::WhichSide3D (nsect, ntr2, ntr3);
  }

  // Check if the point is on the same side of each plane.
  // This works for both backface and frontface triangles.
  // Return success if all WhichSide3D tests have all the same
  // sign (ignoring the ones that return 0).
  // We already compared 'test1' and 'test2' above.
  if (test3 != 0 && (test3 == -test1 || test3 == -test2)) return false;
  return true;
}

bool csIntersect3::SegmentPolygon (
	const csSegment3& seg,
	const csPoly3D& poly, const csPlane3& poly_plane,
	csVector3& isect)
{
  float dist;
  const csVector3& start = seg.Start ();
  const csVector3& end = seg.End ();
  if (!SegmentPlane (start, end, poly_plane, isect, dist))
    return false;

  // If this vector is perpendicular to the plane of the polygon we
  // need to catch this case here.
  float dot1 = poly_plane.D () +
    poly_plane.A () * start.x +
    poly_plane.B () * start.y +
    poly_plane.C () * start.z;
  float dot2 = poly_plane.D () + poly_plane.A () * end.x
  	+ poly_plane.B () * end.y + poly_plane.C () * end.z;
  if (ABS (dot1 - dot2) < SMALL_EPSILON) return false;

  // If dot1 > 0 the polygon would have been backface culled.
  // In this case we just use the result of this test to reverse
  // the test below.
  // Now we generate a plane between the starting point of the ray and
  // every edge of the polygon. With the plane normal of that plane we
  // can then check if the end of the ray is on the same side for all
  // these planes.
  csVector3 normal;
  csVector3 relend = end;
  relend -= start;

  size_t i, i1;
  i1 = poly.GetVertexCount () - 1;
  for (i = 0; i < poly.GetVertexCount () ; i++)
  {
    csMath3::CalcNormal (normal, start, poly[i1], poly[i]);
    if (dot1 > 0)
    {
      if ((relend * normal) < 0) return false;
    }
    else
    {
      if ((relend * normal) > 0) return false;
    }

    i1 = i;
  }

  return true;
}

bool csIntersect3::SegmentPlanes (
   const csVector3& u, 
   const csVector3& v,
   const csPlane3* planes,
   int length,
   csVector3& isect,
   float& dist)
{
  dist = -1;
  int i;
  for(i = 0;i < length;i++)
  {
    csVector3 tempisect;
    float tempdist;
    if (SegmentPlane(u,v,planes[i],tempisect,tempdist))
    {
      if(dist == -1||tempdist<dist)
      {
        bool inside = true;
        int j;
        for(j = 0;j < length;j++)
        {
          if(planes[j].Classify(tempisect)<0)
 	  {
	    inside = false;
	    break;
 	  }
        }
        if(inside)
        {
          isect = tempisect;
	  dist = tempdist;
        }
      }
    }
  }
  if(dist == -1)
  {
    return false;
  }
  return true;
}

bool csIntersect3::SegmentPlane (
  const csVector3 &u,
  const csVector3 &v,
  const csVector3 &normal,
  const csVector3 &a,
  csVector3 &isect,
  float &dist)
{
  float divider = normal * (v - u);
  if (divider == 0)
  {
    isect = v;
    return false;
  }

  float counter = normal * (u - a);
  dist = -counter / divider;
  isect = u + dist * (v - u);
  return true;
}

bool csIntersect3::SegmentPlane (
  const csVector3 &u,
  const csVector3 &v,
  const csPlane3 &p,
  csVector3 &isect,
  float &dist)
{
  float denom;
  csVector3 uv = u - v;

  denom = p.norm * uv;
  if (denom == 0)
  {
    // they are parallel
    dist = 0; //'dist' is an output arg, so it must be initialized.
    isect = v;
    return false;
  }
  dist = (p.norm * u + p.DD) / denom;
  if (dist < -SMALL_EPSILON || dist > 1 + SMALL_EPSILON) 
  {
    isect = 0;//'isect' is an output arg, so it must be initialized.
    return false;
  }

  isect = u + dist * -uv;
  return true;
}

bool csIntersect3::SegmentPlane (
  const csPlane3& plane,
  csSegment3& segment)
{
  const csVector3& start = segment.Start ();
  const csVector3& end = segment.End ();

  csVector3 isec;
  float dist;

  if (SegmentPlane (start, end, plane, isec, dist))
  {
    // Have an intersection, update segment
    const csVector3 d = end - start;
    const float dd = d * plane.norm;

    if (dd > 0)
    {
      // Plane pointing opposite to segment, update end
      segment.SetEnd (isec);
    }
    else
    {
      segment.SetStart (isec);
    }

    return true;
  }


  return false;
}

bool csIntersect3::ThreePlanes (
  const csPlane3 &p1,
  const csPlane3 &p2,
  const csPlane3 &p3,
  csVector3 &isect)
{
  //To find the one point that is on all three planes, we need to solve
  //the following equation system (we need to find the x, y and z which
  //are true for all equations):
  // A1*x+B1*y+C1*z+D1=0 //plane1
  // A2*x+B2*y+C2*z+D2=0 //plane2
  // A3*x+B3*y+C3*z+D3=0 //plane3
  //This can be solved according to Cramers rule by looking at the
  //determinants of the equation system.
  csMatrix3 mdet (
              p1.A (),
              p1.B (),
              p1.C (),
              p2.A (),
              p2.B (),
              p2.C (),
              p3.A (),
              p3.B (),
              p3.C ());
  float det = mdet.Determinant ();
  if (det == 0) return false; //some planes are parallel.
  csMatrix3 mx (
              -p1.D (),
              p1.B (),
              p1.C (),
              -p2.D (),
              p2.B (),
              p2.C (),
              -p3.D (),
              p3.B (),
              p3.C ());
  float xdet = mx.Determinant ();

  csMatrix3 my (
              p1.A (),
              -p1.D (),
              p1.C (),
              p2.A (),
              -p2.D (),
              p2.C (),
              p3.A (),
              -p3.D (),
              p3.C ());
  float ydet = my.Determinant ();

  csMatrix3 mz (
              p1.A (),
              p1.B (),
              -p1.D (),
              p2.A (),
              p2.B (),
              -p2.D (),
              p3.A (),
              p3.B (),
              -p3.D ());
  float zdet = mz.Determinant ();

  isect.x = xdet / det;
  isect.y = ydet / det;
  isect.z = zdet / det;
  return true;
}

bool csIntersect3::PlaneXPlane (const csPlane3 &p1, float x2, csPlane2 &isect)
{
  // p1: A*x+B*y+C*z+D=0
  if (ABS (p1.B ()) < SMALL_EPSILON && ABS (p1.C ()) < SMALL_EPSILON)
    return false;
  isect.Set (p1.B (), p1.C (), p1.D () + x2 * p1.A ());
  return true;
}

bool csIntersect3::PlaneYPlane (const csPlane3 &p1, float y2, csPlane2 &isect)
{
  // p1: A*x+B*y+C*z+D=0
  if (ABS (p1.A ()) < SMALL_EPSILON && ABS (p1.C ()) < SMALL_EPSILON)
    return false;
  isect.Set (p1.A (), p1.C (), p1.D () + y2 * p1.B ());
  return true;
}

bool csIntersect3::PlaneZPlane (const csPlane3 &p1, float z2, csPlane2 &isect)
{
  // p1: A*x+B*y+C*z+D=0
  if (ABS (p1.A ()) < SMALL_EPSILON && ABS (p1.B ()) < SMALL_EPSILON)
    return false;
  isect.Set (p1.A (), p1.B (), p1.D () + z2 * p1.C ());
  return true;
}

float csIntersect3::SegmentZ0Plane (
  const csVector3 &u,
  const csVector3 &v,
  csVector3 &isect)
{
  float r = u.z / (u.z - v.z);
  isect.x = r * (v.x - u.x) + u.x;
  isect.y = r * (v.y - u.y) + u.y;
  isect.z = 0;
  return r;
}

float csIntersect3::SegmentXPlane (
  const csVector3 &u,
  const csVector3 &v,
  float xval,
  csVector3 &isect)
{
  float r = (xval - u.x) / (v.x - u.x);
  isect.x = xval;
  isect.y = r * (v.y - u.y) + u.y;
  isect.z = r * (v.z - u.z) + u.z;
  return r;
}

float csIntersect3::SegmentYPlane (
  const csVector3 &u,
  const csVector3 &v,
  float yval,
  csVector3 &isect)
{
  float r = (yval - u.y) / (v.y - u.y);
  isect.x = r * (v.x - u.x) + u.x;
  isect.y = yval;
  isect.z = r * (v.z - u.z) + u.z;
  return r;
}

float csIntersect3::SegmentZPlane (
  const csVector3 &u,
  const csVector3 &v,
  float zval,
  csVector3 &isect)
{
  float r = (zval - u.z) / (v.z - u.z);
  isect.x = r * (v.x - u.x) + u.x;
  isect.y = r * (v.y - u.y) + u.y;
  isect.z = zval;
  return r;
}

float csIntersect3::SegmentXFrustum (
  const csVector3 &u,
  const csVector3 &v,
  float A,
  csVector3 &isect)
{
  float r = (A * u.x + u.z) / (A * (u.x - v.x) + u.z - v.z);
  isect.x = r * (v.x - u.x) + u.x;
  isect.y = r * (v.y - u.y) + u.y;
  isect.z = r * (v.z - u.z) + u.z;
  return r;
}

float csIntersect3::SegmentYFrustum (
  const csVector3 &u,
  const csVector3 &v,
  float B,
  csVector3 &isect)
{
  float r = (B * u.y + u.z) / (B * (u.y - v.y) + u.z - v.z);
  isect.x = r * (v.x - u.x) + u.x;
  isect.y = r * (v.y - u.y) + u.y;
  isect.z = r * (v.z - u.z) + u.z;
  return r;
}

int csIntersect3::BoxSegment (
  const csBox3 &box,
  const csSegment3 &seg,
  csVector3 &isect,
  float *pr,
  bool use_ray)
{
  const csVector3 &u = seg.Start ();
  const csVector3 &v = seg.End ();
  float r, plane_pos = 0;
  int sides[3];
  int num_sides = box.GetVisibleSides (u, sides);
  int i;

  // If there are no sides then we're in the box so we can return true.
  if (num_sides == 0)
  {
    isect = u;
    if (pr) *pr = 0;
    return CS_BOX_INSIDE;
  }

  for (i = 0; i < num_sides; i++)
  {
    switch (sides[i])
    {
      case CS_BOX_SIDE_x:
        plane_pos = box.MinX ();

      // Fall THRU...
      case CS_BOX_SIDE_X:
        if (sides[i] == CS_BOX_SIDE_X) plane_pos = box.MaxX ();
        if (ABS (v.x - u.x) > SMALL_EPSILON)
        {
          r = (plane_pos - u.x) / (v.x - u.x);
          if (r < 0 || ((!use_ray) && r > 1)) break;
          isect.x = plane_pos;
          isect.y = r * (v.y - u.y) + u.y;
          isect.z = r * (v.z - u.z) + u.z;
          if (
            isect.y + EPSILON >= box.MinY () &&
            isect.y - EPSILON <= box.MaxY () &&
            isect.z + EPSILON >= box.MinZ () &&
            isect.z - EPSILON <= box.MaxZ ())
          {
            if (pr) *pr = r;
            return sides[i];
          }
        }
        break;
      case CS_BOX_SIDE_y:
        plane_pos = box.MinY ();

      // Fall THRU...
      case CS_BOX_SIDE_Y:
        if (sides[i] == CS_BOX_SIDE_Y) plane_pos = box.MaxY ();
        if (ABS (v.y - u.y) > SMALL_EPSILON)
        {
          r = (plane_pos - u.y) / (v.y - u.y);
          if (r < 0 || ((!use_ray) && r > 1)) break;
          isect.x = r * (v.x - u.x) + u.x;
          isect.y = plane_pos;
          isect.z = r * (v.z - u.z) + u.z;
          if (
            isect.x >= box.MinX () &&
            isect.x <= box.MaxX () &&
            isect.z >= box.MinZ () &&
            isect.z <= box.MaxZ ())
          {
            if (pr) *pr = r;
            return sides[i];
          }
        }
        break;
      case CS_BOX_SIDE_z:
        plane_pos = box.MinZ ();

      // Fall THRU...
      case CS_BOX_SIDE_Z:
        if (sides[i] == CS_BOX_SIDE_Z) plane_pos = box.MaxZ ();
        if (ABS (v.z - u.z) > SMALL_EPSILON)
        {
          r = (plane_pos - u.z) / (v.z - u.z);
          if (r < 0 || ((!use_ray) && r > 1)) break;
          isect.x = r * (v.x - u.x) + u.x;
          isect.y = r * (v.y - u.y) + u.y;
          isect.z = plane_pos;
          if (
            isect.x >= box.MinX () &&
            isect.x <= box.MaxX () &&
            isect.y >= box.MinY () &&
            isect.y <= box.MaxY ())
          {
            if (pr) *pr = r;
            return sides[i];
          }
        }
        break;
    }
  }

  return -1;
}

bool csIntersect3::ClipSegmentBox (csSegment3& segment, const csBox3& box,
    bool use_ray)
{
  const csVector3& minBox = box.Min ();
  const csVector3& maxBox = box.Max ();

  float minLength = 0.0f;
  float maxLength;
  if (use_ray) maxLength = FLT_MAX * 0.9f;
  else maxLength = sqrtf (csSquaredDist::PointPoint (segment.Start (),
	segment.End ()));

  const csVector3& origin = segment.Start ();
  csVector3 direction = (segment.End () - segment.Start ()).Unit ();

  float mint = minLength;
  float maxt = maxLength;

  // Check if ray have any chance of going through box
  int i = 0;
  for (i = 0; i < 3; i++)
  {
    if (direction[i] < 0)
    {
      if (origin[i] < minBox[i]) return false;
    }
    else if (direction[i] > 0)
    {
      if (origin[i] > maxBox[i]) return false;
    }
  }

  // Clip one dimension at a time
  for (i = 0; i < 3; i++)
  {
    float pos = origin[i] + direction[i] * maxt;

    // Ray going "left"
    if (direction[i] < 0)
    {
      // Clip end
      if (pos < minBox[i])
      {
        maxt = mint + (maxt - mint) * ((origin[i] - minBox[i])
	    / (origin[i] - pos));
      }
      // Clip start
      if (origin[i] > maxBox[i])
      {
        mint += (maxt - mint) * ((origin[i] - maxBox[i])
	    / (maxt * direction[i]));
      }
    }
    else if (direction[i] > 0) // Ray going "right"
    {
      // Clip end
      if (pos > maxBox[i])
      {
        maxt = mint + (maxt - mint) * ((maxBox[i] - origin[i])
	    / (pos - origin[i]));
      }
      // Clip start
      if (origin[i] < minBox[i])
      {
        mint += (maxt - mint) * ((minBox[i] - origin[i])
	    / (maxt * direction[i]));
      }
    }
    if (mint > maxt) return false;
  }

  segment.SetStart (origin + mint * direction);
  segment.SetEnd (origin + maxt * direction);

  return true;
}


bool csIntersect3::BoxFrustum (const csBox3& box, const csFrustum* frustum)
{
  if (frustum->IsInfinite ()) return true;

  csVector3 m = box.GetCenter ();
  csVector3 d = box.Max ()-m;		// Half-diagonal.

  const csVector3& origin = frustum->GetOrigin ();
  size_t vtcount = frustum->GetVertexCount ();
  csVector3* vts = frustum->GetVertices ();
  size_t i1 = vtcount-1;
  size_t i;
  for (i = 0 ; i < vtcount ; i++)
  {
    csPlane3 p (origin, vts[i], vts[i1]);
    float NP = (float)(d.x*fabs(p.A ())+d.y*fabs(p.B ())+d.z*fabs(p.C ()));
    float MP = p.Classify (m);
    if ((MP+NP) < 0.0f) return false;       // behind clip plane
    i1 = i;
  }
  csPlane3* bp = frustum->GetBackPlane ();
  if (bp)
  {
    float NP = (float)(d.x*fabs(bp->A ())+d.y*fabs(bp->B ())+d.z*fabs(bp->C ()));
    float MP = bp->Classify (m);
    if ((MP+NP) < 0.0f) return false;       // behind clip plane
  }
  return true;
}

bool csIntersect3::BoxFrustum (const csBox3& box, const csPlane3* f,
	uint32 inClipMask, uint32& outClipMask)
{
  csVector3 m = box.GetCenter ();
  csVector3 d = box.Max ()-m;		// Half-diagonal.
  uint32 mk = 1;
  outClipMask = 0;

  // Loop for all active planes.
  while (mk <= inClipMask)
  {
    if (inClipMask & mk)
    {
      // This is an active clip plane.
      float NP = (float)(d.x*fabs(f->A ())+d.y*fabs(f->B ())+d.z*fabs(f->C ()));
      float MP = f->Classify (m);
      if ((MP+NP) < 0.0f) return false;       // behind clip plane
      if ((MP-NP) < 0.0f) outClipMask |= mk;
    }
    mk += mk;
    f++;
  }
  return true;	// There is an intersection.
}

bool csIntersect3::BoxSphere (const csBox3& box, const csVector3& center,
		  float sqradius)
{
  csBox3 b (box.Min ()-center, box.Max ()-center);
  return (b.SquaredOriginDist () <= sqradius);
}

//-------------------------------------------------------------------------

#define AXISTEST_X01(a, b, fa, fb)			   \
	p0 = a*v0.y - b*v0.z;			       	   \
	p2 = a*v2.y - b*v2.z;			       	   \
        if(p0<p2) {min=p0; max=p2;} else {min=p2; max=p0;} \
	rad = fa * boxhalfsize.y + fb * boxhalfsize.z;     \
	if(min>rad || max<-rad) return false;
#define AXISTEST_X2(a, b, fa, fb)			   \
	p0 = a*v0.y - b*v0.z;			           \
	p1 = a*v1.y - b*v1.z;			       	   \
        if(p0<p1) {min=p0; max=p1;} else {min=p1; max=p0;} \
	rad = fa * boxhalfsize.y + fb * boxhalfsize.z;     \
	if(min>rad || max<-rad) return false;
#define AXISTEST_Y02(a, b, fa, fb)			   \
	p0 = -a*v0.x + b*v0.z;			      	   \
	p2 = -a*v2.x + b*v2.z;		       	       	   \
        if(p0<p2) {min=p0; max=p2;} else {min=p2; max=p0;} \
	rad = fa * boxhalfsize.x + fb * boxhalfsize.z;     \
	if(min>rad || max<-rad) return false;
#define AXISTEST_Y1(a, b, fa, fb)			   \
	p0 = -a*v0.x + b*v0.z;		      	  	   \
	p1 = -a*v1.x + b*v1.z;	     	       	  	   \
        if(p0<p1) {min=p0; max=p1;} else {min=p1; max=p0;} \
	rad = fa * boxhalfsize.x + fb * boxhalfsize.z;     \
	if(min>rad || max<-rad) return false;
#define AXISTEST_Z12(a, b, fa, fb)			   \
	p1 = a*v1.x - b*v1.y;			           \
	p2 = a*v2.x - b*v2.y;			       	   \
        if(p2<p1) {min=p2; max=p1;} else {min=p1; max=p2;} \
	rad = fa * boxhalfsize.x + fb * boxhalfsize.y;     \
	if(min>rad || max<-rad) return false;
#define AXISTEST_Z0(a, b, fa, fb)			   \
	p0 = a*v0.x - b*v0.y;				   \
	p1 = a*v1.x - b*v1.y;			           \
        if(p0<p1) {min=p0; max=p1;} else {min=p1; max=p0;} \
	rad = fa * boxhalfsize.x + fb * boxhalfsize.y;     \
	if(min>rad || max<-rad) return false;
#define FINDMINMAX(x0,x1,x2,min,max) \
  min = max = x0;   \
  if(x1<min) min=x1;\
  if(x1>max) max=x1;\
  if(x2<min) min=x2;\
  if(x2>max) max=x2;


bool csIntersect3::BoxTriangle (const csBox3& box,
	const csVector3& tri0, const csVector3& tri1, const csVector3& tri2)
{
  csVector3 boxcenter = box.GetCenter ();
  csVector3 boxhalfsize = box.Max () - boxcenter;

  /*
   * Use separating axis theorem to test overlap between triangle and box
   * need to test for overlap in these directions:
   *    1) the {x,y,z}-directions (actually, since we use the AABB of the
   *       triangle we do not even need to test these)
   *    2) normal of the triangle
   *    3) crossproduct(edge from tri, {x,y,z}-directin)
   *       this gives 3x3=9 more tests
   */

  float min, max, p0, p1, p2, rad, fex, fey, fez;

  /* Move everything so that the boxcenter is in (0,0,0) */
  csVector3 v0 = tri0 - boxcenter;
  csVector3 v1 = tri1 - boxcenter;
  csVector3 v2 = tri2 - boxcenter;

  /* Compute triangle edges */
  csVector3 e0 = v1 - v0;	// Edge 0
  csVector3 e1 = v2 - v1;	// Edge 1
  csVector3 e2 = v0 - v2;	// Edge 2

  /* Bullet 3:  */
  /*  Test the 9 tests first (this was faster) */
  fex = fabsf (e0.x);
  fey = fabsf (e0.y);
  fez = fabsf (e0.z);
  AXISTEST_X01 (e0.z, e0.y, fez, fey);
  AXISTEST_Y02 (e0.z, e0.x, fez, fex);
  AXISTEST_Z12 (e0.y, e0.x, fey, fex);

  fex = fabsf (e1.x);
  fey = fabsf (e1.y);
  fez = fabsf (e1.z);
  AXISTEST_X01 (e1.z, e1.y, fez, fey);
  AXISTEST_Y02 (e1.z, e1.x, fez, fex);
  AXISTEST_Z0 (e1.y, e1.x, fey, fex);

  fex = fabsf (e2.x);
  fey = fabsf (e2.y);
  fez = fabsf (e2.z);
  AXISTEST_X2 (e2.z, e2.y, fez, fey);
  AXISTEST_Y1 (e2.z, e2.x, fez, fex);
  AXISTEST_Z12 (e2.y, e2.x, fey, fex);

  /* Bullet 1: */
  /*  first test overlap in the {x,y,z}-directions */
  /*  find min, max of the triangle each direction, and test for overlap in */
  /*  that direction -- this is equivalent to testing a minimal AABB around */
  /*  the triangle against the AABB */

  /* Test in X-direction */
  FINDMINMAX (v0.x,v1.x,v2.x,min,max);
  if (min > boxhalfsize.x || max < -boxhalfsize.x) return false;

  /* Test in Y-direction */
  FINDMINMAX (v0.y,v1.y,v2.y,min,max);
  if (min > boxhalfsize.y || max < -boxhalfsize.y) return false;

  /* Test in Z-direction */
  FINDMINMAX (v0.z,v1.z,v2.z,min,max);
  if (min > boxhalfsize.z || max < -boxhalfsize.z) return false;

  /* Bullet 2: */
  /*  Test if the box intersects the plane of the triangle */
  /*  compute plane equation of triangle: normal*x+d=0 */
  csVector3 normal = e0 % e1;

  if (!BoxPlaneInternal (normal, v0, boxhalfsize)) return false;

  return true;   /* box and triangle overlaps */
}

bool csIntersect3::BoxPlane (const csBox3& box,
	const csVector3& normal, const csVector3& vert)
{
  csVector3 boxcenter = box.GetCenter ();
  csVector3 boxhalfsize = box.Max () - boxcenter;
  return BoxPlaneInternal (normal, vert-boxcenter, boxhalfsize);
}

bool csIntersect3::BoxPlane (const csBox3& box, const csPlane3& plane)
{
  csVector3 boxcenter = box.GetCenter ();
  csVector3 boxhalfsize = box.Max () - boxcenter;
  return BoxPlaneInternal (plane.GetNormal (), plane.FindPoint ()-boxcenter,
  	boxhalfsize);
}

bool csIntersect3::BoxPlaneInternal (const csVector3& normal,
	const csVector3& vert, const csVector3& boxhalfsize)
{
  csVector3 vmin, vmax;

  if (normal.x > 0.0f)
  {
    vmin.x = -boxhalfsize.x;
    vmax.x =  boxhalfsize.x;
  }
  else
  {
    vmin.x =  boxhalfsize.x;
    vmax.x = -boxhalfsize.x;
  }
  if (normal.y > 0.0f)
  {
    vmin.y = -boxhalfsize.y;
    vmax.y =  boxhalfsize.y;
  }
  else
  {
    vmin.y =  boxhalfsize.y;
    vmax.y = -boxhalfsize.y;
  }
  if (normal.z > 0.0f)
  {
    vmin.z = -boxhalfsize.z;
    vmax.z =  boxhalfsize.z;
  }
  else
  {
    vmin.z =  boxhalfsize.z;
    vmax.z = -boxhalfsize.z;
  }

  vmin -= vert;
  vmax -= vert;

  if ((normal * vmin) > 0.0f) return false;
  if ((normal * vmax) >= 0.0f) return true;
  return false;
}
