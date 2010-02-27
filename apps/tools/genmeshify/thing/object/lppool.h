/*
    Copyright (C) 1998 by Jorrit Tyberghein

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

#ifndef __CS_LPPOOL_H__
#define __CS_LPPOOL_H__

#include "csutil/blockallocator.h"

struct iLight;

class csLightPatchPool;
class csPolygon3D;

/**
 * A light patch. This is a 3D polygon which fits on a world level 3D
 * polygon and defines where the light hits the polygon.
 * There is a list of light patches in every polygon (all dynamic lights
 * hitting a polygon will give rise to a separate light patch) and there
 * is a list of light patches in every dynamic light (representing all
 * polygons that are hit by that particular light).
 */
class csLightPatch
{
  friend class csLightPatchPool;

private:
  csLightPatch* next;
  csLightPatch* prev;

  /// Vertices.
  csVector3* vertices;
  /// Current number of vertices.
  int num_vertices;
  /// Maximum number of vertices.
  int max_vertices;

  /// Polygon that this light patch is for.
  csPolygon3D* polygon;

  /// Light that this light patch originates from.
  iLight* light;

  /// frustum of where the visible light hits (for use with curves)
  csRef<csFrustum> light_frustum;

public:
  /**
   * Create an empty light patch (infinite frustum).
   */
  csLightPatch ();

  /**
   * Unlink this light patch from the polygon and the light
   * and then destroy.
   */
  ~csLightPatch ();

  /**
   * Make room for the specified number of vertices and
   * initialize to start a new light patch.
   */
  void Initialize (int n);

  /**
   * Remove this light patch (unlink from all lists).
   */
  void RemovePatch ();

  /**
   * Get the polygon that this light patch belongs too.
   */
  csPolygon3D* GetPolygon () { return polygon; }

  /**
   * Get the light that this light patch belongs too.
   */
  iLight* GetLight () { return light; }

  /// Get the number of vertices in this light patch.
  int GetVertexCount () { return num_vertices; }
  /// Get all the vertices.
  csVector3* GetVertices () { return vertices; }

  /// Get a vertex.
  csVector3& GetVertex (int i)
  {
    CS_ASSERT (vertices != 0);
    CS_ASSERT (i >= 0 && i < num_vertices);
    return vertices[i];
  }

  /**
   * Get next light patch.
   */
  csLightPatch* GetNext () { return next; }

  /// Set polygon.
  void SetPolyCurve (csPolygon3D* pol) { polygon = pol; }
  /// Set light.
  void SetLight (iLight* l) { light = l; }
  /// Add to list.
  void AddList (csLightPatch*& first)
  {
    next = first;
    prev = 0;
    if (first)
      first->prev = this;
    first = this;
  }
  /// Remove from list.
  void RemoveList (csLightPatch*& first)
  {
    if (next) next->prev= prev;
    if (prev) prev->next= next;
    else first = next;
    prev= next= 0;
    polygon = 0;
  }

  /// Set the light frustum.
  void SetLightFrustum (csFrustum* lf) { light_frustum = lf; }
  /// Set the light frustum.
  void SetNewLightFrustum (csPtr<csFrustum> lf) { light_frustum = lf; }
  /// Get the light frustum.
  csFrustum* GetLightFrustum () { return light_frustum; }
};

/**
 * This is an object pool which holds objects of type
 * csLightPatch. You can ask new instances from this pool.
 * If needed it will allocate one for you but ideally it can
 * give you one which was allocated earlier.
 */
class csLightPatchPool : public csBlockAllocator<csLightPatch>
{
public:
  void Free (csLightPatch* o)
  {
    o->RemovePatch ();
    csBlockAllocator<csLightPatch>::Free (o);
  }
};

#endif // __CS_LPPOOL_H__
