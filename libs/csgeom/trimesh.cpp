/*
    Copyright (C) 2002 by Jorrit Tyberghein

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
#include "csgeom/trimesh.h"

//---------------------------------------------------------------------------

csTriangleMesh::csTriangleMesh (const csTriangleMesh& mesh) :
  scfImplementationType (this), change_nr (0)
{
  triangles.SetSize (mesh.GetTriangleCount ());
  memcpy (triangles.GetArray (), mesh.GetTriangles (),
  	sizeof (csTriangle)*mesh.GetTriangleCount ());
  vertices.SetSize (mesh.GetVertexCount ());
  memcpy (vertices.GetArray (), mesh.GetVertices (),
  	sizeof (csVector3)*mesh.GetVertexCount ());
}

csTriangleMesh::~csTriangleMesh ()
{
}

void csTriangleMesh::Clear ()
{
  triangles.SetSize (0);
  vertices.SetSize (0);
}

void csTriangleMesh::SetSize (int count)
{
  triangles.SetSize (count);
}

void csTriangleMesh::AddVertex (const csVector3& v)
{
  vertices.Push (v);
}

void csTriangleMesh::SetTriangles (csTriangle const* trigs, int count)
{
  triangles.SetSize (count);
  memcpy (triangles.GetArray (), trigs, sizeof(csTriangle) * count);
}

void csTriangleMesh::AddTriangle (int a, int b, int c)
{
  csTriangle t (a, b, c);
  triangles.Push (t);
}

void csTriangleMesh::AddTriangleMesh(const csTriangleMesh& tm)
{
	// retrieve the old size - size before any insertion
	size_t oldSize = GetVertexCount();

	// get vertices first
	size_t vertexCount = tm.GetVertexCount();
	const csVector3* verts = tm.GetVertices();
	for (size_t i = 0; i < vertexCount; i++)
	{
		AddVertex(verts[i]);
	}

	size_t triCount = tm.GetTriangleCount();
	const csTriangle* trisToAdd = tm.GetTriangles();

	// for each of the indexes in each triangle, we
	// will need to add oldSize to it
	for (size_t i = 0; i < triCount; i++)
	{
		csTriangle oneToAdd = trisToAdd[i];
		AddTriangle((int)(oneToAdd[0] + oldSize), (int)(oneToAdd[1] + oldSize), (int)(oneToAdd[2] + oldSize));
	}
}

csTriangleMesh& csTriangleMesh::operator+=(const csTriangleMesh& tm)
{
	AddTriangleMesh(tm);
	return *this;
}

//---------------------------------------------------------------------------

void csTriangleVertex::AddTriangle (size_t idx)
{
  con_triangles.PushSmart (idx);
}

void csTriangleVertex::AddVertex (int idx)
{
  CS_ASSERT (idx != csTriangleVertex::idx);
  con_vertices.PushSmart (idx);
}

csTriangleVertices::csTriangleVertices (csTriangleMesh* mesh,
	csVector3* verts, int num_verts)
{
  vertices = new csTriangleVertex [num_verts];
  num_vertices = num_verts;

  // Build connectivity information for all vertices in this mesh.
  csTriangle* triangles = mesh->GetTriangles ();
  size_t j;

  // First add the triangles that are used by every vertex.
  size_t tricount = mesh->GetTriangleCount ();
  for (j = 0 ; j < tricount ; j++)
  {
    vertices[triangles[j].a].AddTriangle (j);
    vertices[triangles[j].b].AddTriangle (j);
    vertices[triangles[j].c].AddTriangle (j);
  }
  // Now setup the vertices and add the connected vertices.
  int i;
  for (i = 0 ; i < num_vertices ; i++)
  {
    vertices[i].pos = verts[i];
    vertices[i].idx = i;
    for (j = 0 ; j < vertices[i].con_triangles.GetSize () ; j++)
    {
      size_t triidx = vertices[i].con_triangles[j];
      if (triangles[triidx].a != i) vertices[i].AddVertex (triangles[triidx].a);
      if (triangles[triidx].b != i) vertices[i].AddVertex (triangles[triidx].b);
      if (triangles[triidx].c != i) vertices[i].AddVertex (triangles[triidx].c);
    }
  }

}

csTriangleVertices::~csTriangleVertices ()
{
  delete [] vertices;
}

void csTriangleVertices::UpdateVertices (csVector3* verts)
{
  int i;
  for (i = 0 ; i < num_vertices ; i++)
    vertices[i].pos = verts[i];
}

