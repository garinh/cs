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

#ifndef __CS_FRUSTRUM_H__
#define __CS_FRUSTRUM_H__

/**\file 
 * General frustum.
 */
/**
 * \addtogroup geom_utils
 * @{ */

#include "csextern.h"

#include "cstypes.h"
#include "csutil/refcount.h"
#include "csgeom/vector3.h"

class csPlane3;
template<class T>
class csPtr;
class csTransform;
class csSegment3;

/** \name Polygon-to-Frustum relations
 * Return values for csFrustum::Classify. The routine makes a difference
 * whenever a polygon is fully outside the frustum, fully inside, fully
 * covers the frustum or is partly inside, partly outside.
 * @{ */
enum
{
  /// The polygon is fully outside frustum
  CS_FRUST_OUTSIDE = 0,
  /// The polygon is fully inside frustum
  CS_FRUST_INSIDE = 1,
  /// The polygon fully covers the frustum
  CS_FRUST_COVERED = 2,
  /// The polygon is partially inside frustum
  CS_FRUST_PARTIAL = 3
};
/** @} */

enum csClipType
{
  CS_CLIPINFO_ORIGINAL = 0,
  CS_CLIPINFO_ONEDGE = 1,
  CS_CLIPINFO_INSIDE = 2
};

/**
 * Structure for use with ClipToPlane. This structure
 * is used in addition to a vertex to give additional information
 * about how to interpolate additional information that goes
 * with the vertex.
 */
struct CS_CRYSTALSPACE_EXPORT csClipInfo
{

  csClipType type; // One of CS_CLIPINFO_???
  union
  {
    struct { int idx; } original;
    struct { int i1, i2; float r; } onedge;
    struct { csClipInfo* ci1, * ci2; float r; } inside;
  };

  csClipInfo () : type (CS_CLIPINFO_ORIGINAL) { }
  void Clear ();
  ~csClipInfo () { Clear (); }

  /// Copy the information from another clipinfo instance to this one.
  inline void Copy (csClipInfo& other)
  {
    if (&other == this) return;
    Clear ();
    type = other.type;
    if (type == CS_CLIPINFO_INSIDE)
    {
      inside.r = other.inside.r;
      inside.ci1 = new csClipInfo ();
      inside.ci1->Copy (*other.inside.ci1);
      inside.ci2 = new csClipInfo ();
      inside.ci2->Copy (*other.inside.ci2);
    }
    else if (type == CS_CLIPINFO_ORIGINAL)
      original.idx = other.original.idx;
    else
      onedge = other.onedge;
  }

  /// Move the information from another clipinfo instance to this one.
  inline void Move (csClipInfo& other)
  {
    if (&other == this) return;
    Clear ();
    type = other.type;
    if (type == CS_CLIPINFO_INSIDE)
      inside = other.inside;
    else if (type == CS_CLIPINFO_ORIGINAL)
      original.idx = other.original.idx;
    else
      onedge = other.onedge;
    other.type = CS_CLIPINFO_ORIGINAL;
  }

  inline void Dump (int indent)
  {
    char ind[255];
    int i;
    for (i = 0 ; i < indent ; i++) ind[i] = ' ';
    ind[i] = 0;
    switch (type)
    {
      case CS_CLIPINFO_ORIGINAL:
        printf ("%s ORIGINAL idx=%d\n", ind, original.idx);
	break;
      case CS_CLIPINFO_ONEDGE:
        printf ("%s ONEDGE i1=%d i2=%d r=%g\n", ind, onedge.i1, onedge.i2,
	  onedge.r);
        break;
      case CS_CLIPINFO_INSIDE:
        printf ("%s INSIDE r=%g\n", ind, inside.r);
	inside.ci1->Dump (indent+2);
	inside.ci2->Dump (indent+2);
	break;
    }
    fflush (stdout);
  }
};

/**
 * A general frustum. This consist of a center point (origin),
 * a frustum polygon in 3D space (relative to center point)
 * and a plane. The planes which go through the center and
 * every edge of the polygon form the frustum. The plane
 * is the back plane of the frustum.
 * It is also possible to have an infinite frustum in which
 * case the polygon will be 0 (not specified). The back
 * plane can also be 0.
 */
class CS_CRYSTALSPACE_EXPORT csFrustum : public csRefCount
{
private:
  /// The origin of this frustum
  csVector3 origin;

  /**
   * The polygon vertices for non-wide frustum.
   * If not 0, the frustum is a pyramid with origin in "origin"
   * and with the basis given by this polygon.
   */
  csVector3* vertices;
  /// Number of vertices in frustum polygon
  size_t num_vertices;
  /// Max number of vertices
  size_t max_vertices;

  /// Back clipping plane
  csPlane3* backplane;

  /**
   * If true we have a total wide frustum. A frustum like
   * this does not have a polygon defining the planes but it can have
   * a back plane. The value of this boolean is only used if there
   * is no polygon. In other words, if the polygon is present in this
   * frustum then 'wide' is simply ignored and can have any value.
   */
  bool wide;

  /**
   * If true we have a mirrored frustum where the direction of
   * the polygon is reversed.
   */
  bool mirrored;

  /// Clear the frustum
  void Clear ();

  /// Ensure vertex array is able to hold at least "num" vertices
  void ExtendVertexArray (size_t num);

public:

  /// Create a new empty frustum.
  csFrustum (const csVector3& o) :
    origin (o), vertices (0), num_vertices (0), max_vertices (0),
    backplane (0), wide (false), mirrored (false)
  { }

  /**
   * Create a frustum given a polygon and a backplane.
   * The polygon is given relative to the origin 'o'.
   * If the given polygon is 0 then we create an empty frustum.
   */
  csFrustum (const csVector3& o, csVector3* verts, size_t num_verts,
        csPlane3* backp = 0);

  /**
   * Create a frustum given a number of vertices and a backplane.
   * The vertices are not initialized but space is reserved for them.
   * The polygon is given relative to the origin 'o'.
   */
  csFrustum (const csVector3& o, size_t num_verts,
        csPlane3* backp = 0);

  /// Copy constructor.
  csFrustum (const csFrustum &copy);

  /// Assignment operator
  const csFrustum& operator= (const csFrustum& other);

  ///
  virtual ~csFrustum ();

  /// Set the origin of this frustum.
  inline void SetOrigin (const csVector3& o) { origin = o; }

  /// Get the origin of this frustum.
  inline csVector3& GetOrigin () { return origin; }

  /// Get the origin of this frustum.
  inline const csVector3& GetOrigin () const { return origin; }

  /**
   * Enable/disable mirroring.
   * If mirroring is enabled this means that the frustum polygon
   * is given in anti-clockwise order.
   */
  inline void SetMirrored (bool m) { mirrored = m; }

  /// Is this frustum mirrored?
  inline bool IsMirrored () const { return mirrored; }

  /**
   * Set the back plane of this frustum.
   * The given plane is copied to this structure and can thus
   * be reused/freed later. The plane should be specified relative
   * to the origin point.
   */
  void SetBackPlane (const csPlane3& plane);

  /**
   * Get the back plane.
   */
  inline csPlane3* GetBackPlane () const { return backplane; }

  /**
   * Remove the back plane of this frustum.
   */
  void RemoveBackPlane ();

  /**
   * Add a vertex to the frustum polygon.
   */
  void AddVertex (const csVector3& v);

  /**
   * Get the number of vertices.
   */
  inline size_t GetVertexCount () const { return num_vertices; }

  /**
   * Get a vertex.
   */
  inline csVector3& GetVertex (size_t idx)
  {
    CS_ASSERT (idx < num_vertices);
    return vertices[idx];
  }

  /**
   * Get a vertex.
   */
  inline const csVector3& GetVertex (size_t idx) const
  {
    CS_ASSERT (idx < num_vertices);
    return vertices[idx];
  }

  /**
   * Get the array of vertices.
   */
  inline csVector3* GetVertices () const { return vertices; }

  /**
   * Apply a transformation to this frustum.
   */
  void Transform (csTransform* trans);

  /**
   * Clip this frustum to the positive side of a plane
   * formed by the origin of this frustum, and the two given vertices.
   * 'v1' and 'v2' are given relative to that origin.
   */
  void ClipToPlane (csVector3& v1, csVector3& v2);

  /**
   * Clip a frustum (defined from 0,0,0 origin) to the given plane
   * (defined as 0-v1-v2). This routine will also fill an array
   * of clipinfo so that you can use this information to correctly
   * interpolate information related to the vertex (like texture mapping
   * coordinates). Note that clipinfo needs to be preinitialized correctly
   * with CS_CLIPINFO_ORIGINAL instances and correct indices.
   */
  static void ClipToPlane (csVector3* vertices, size_t& num_vertices,
    csClipInfo* clipinfo, const csVector3& v1, const csVector3& v2);

  CS_DEPRECATED_METHOD_MSG("num_vertices type changed to size_t")
  static void ClipToPlane (csVector3* vertices, int& num_vertices,
    csClipInfo* clipinfo, const csVector3& v1, const csVector3& v2)
  {
    size_t nv;
    ClipToPlane (vertices, nv, clipinfo, v1, v2);
    num_vertices = int (nv);
  }

  /**
   * Clip a frustum (defined from 0,0,0 origin) to the given plane.
   * This routine will also fill an array of clipinfo so that you can
   * use this information to correctly interpolate information related to
   * the vertex (like texture mapping coordinates). Note that clipinfo
   * needs to be preinitialized correctly with CS_CLIPINFO_ORIGINAL
   * instances and correct indices.
   */
  static void ClipToPlane (csVector3* vertices, size_t& num_vertices,
    csClipInfo* clipinfo, const csPlane3& plane);

  CS_DEPRECATED_METHOD_MSG("num_vertices type changed to size_t")
  static void ClipToPlane (csVector3* vertices, int& num_vertices,
    csClipInfo* clipinfo, const csPlane3& plane)
  {
    size_t nv;
    ClipToPlane (vertices, nv, clipinfo, plane);
    num_vertices = int (nv);
  }

  /**
   * Clip the polygon of this frustum to the postive side of an arbitrary plane
   * (which should be specified relative to the origin of the frustum).
   * Note that this clips the polygon which forms the frustum. It does
   * not clip the frustum itself.
   */
  void ClipPolyToPlane (csPlane3* plane);

  /**
   * Intersect with another frustum. The other frustum must have the same
   * origin as this one. Otherwise the result is undefined.
   * \return The new frustum. If there is no intersection then 0 is returned.
   */
  csPtr<csFrustum> Intersect (const csFrustum& other) const;

  /**
   * Intersect a convex polygon with this volume. The convex polygon
   * is given relative to the center point (origin) of this frustum.
   *
   * Returns a new frustum which exactly covers the intersection
   * of the polygon with the frustum (i.e. the smallest frustum
   * which is part of this frustum and which 'sees' exactly
   * the same of the given polygon as this frustum).
   *
   * This function returns 0 if there is no intersection.
   *
   * Note that the frustum polygon of the returned csFrustum is
   * guaranteed to be coplanar with the given polygon.
   */
  csPtr<csFrustum> Intersect (csVector3* poly, size_t num) const;

  /**
   * Intersect a convex polygon with this volume. The convex polygon
   * is given relative to the center point (origin) of this frustum.
   *
   * Returns a new frustum which exactly covers the intersection
   * of the polygon with the frustum (i.e. the smallest frustum
   * which is part of this frustum and which 'sees' exactly
   * the same of the given polygon as this frustum).
   *
   * This function returns 0 if there is no intersection.
   *
   * Note that the frustum polygon of the returned csFrustum is
   * guaranteed to be coplanar with the given polygon.
   */
  static csPtr<csFrustum> Intersect (
    const csVector3& frust_origin, csVector3* frust, size_t num_frust,
    csVector3* poly, size_t num);

  /**
   * Intersect a triangle with this volume. The triangle
   * is given relative to the center point (origin) of this frustum.
   *
   * Returns a new frustum which exactly covers the intersection
   * of the triangle with the frustum (i.e. the smallest frustum
   * which is part of this frustum and which 'sees' exactly
   * the same of the given polygon as this frustum).
   *
   * This function returns 0 if there is no intersection.
   *
   * Note that the frustum polygon of the returned csFrustum is
   * guaranteed to be coplanar with the given triangle.
   */
  static csPtr<csFrustum> Intersect (
    const csVector3& frust_origin, csVector3* frust, size_t num_frust,
    const csVector3& v1, const csVector3& v2, const csVector3& v3);

  /**
   * Intersect a segment with the frustum.
   * \return True if the segment is inside the frustum at any point. The segment
   * will be updated to be the part within the frustum
   */
  bool Intersect (csSegment3& segment);

  /**
   * Check if a polygon intersects with the frustum (i.e.
   * is visible in the frustum). Returns one of #CS_FRUST_OUTSIDE etc. values.
   * Frustum and polygon should be given relative to (0,0,0).
   */
  static int Classify (csVector3* frustum, size_t num_frust,
    csVector3* poly, size_t num_poly);

  /**
   * This is like the above version except that it takes a vector of
   * precalculated frustum plane normals.
   * Use this if you have to classify a batch of polygons against the same
   * frustum.
   */
  static int BatchClassify (csVector3* frustum, csVector3* frustumNormals,
  	size_t num_frust, csVector3* poly, size_t num_poly);

  /**
   * Check if a point (given relative to the origin of the frustum)
   * is inside the frustum.
   */
  bool Contains (const csVector3& point);

  /**
   * Check if a point is inside a frustum. The point and
   * frustum are relative to (0,0,0). Note that this function
   * does not work correctly if the point is in the other direction
   * from the average direction of the frustum.
   */
  static bool Contains (csVector3* frustum, size_t num_frust,
    const csVector3& point);

  /**
   * Check if a point is inside a frustum. The point and
   * frustum are relative to (0,0,0). This function also
   * checks if point is in front of given plane.
   */
  static bool Contains (csVector3* frustum, size_t num_frust,
    const csPlane3& plane, const csVector3& point);

  /// Return true if frustum is empty.
  inline bool IsEmpty () const { return !wide && vertices == 0; }

  /// Return true if frustum is infinite.
  inline bool IsInfinite () const { return wide && vertices == 0 && backplane == 0; }

  /**
   * Return true if frustum is infinitely wide but it can still have a
   * back plane.
   */
  inline bool IsWide () const { return wide && vertices == 0; }

  /**
   * Make the frustum infinite (i.e. clear the polygon and
   * the back plane).
   */
  void MakeInfinite ();

  /**
   * Make the frustum empty.
   */
  void MakeEmpty ();
};

/** @} */

#endif // __CS_FRUSTRUM_H__
