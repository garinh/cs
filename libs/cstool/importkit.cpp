/*
    Copyright (C) 2005 by Jorrit Tyberghein
	      (C) 2005 by Frank Richter

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

#include "cstool/importkit.h"
#include "importkit_glue.h"

using namespace CS::Utility::Implementation;

namespace CS
{
namespace Utility
{

  ImportKit::Container::Model::~Model()
  {
    delete[] name;
  }

  ImportKit::Container::Model::Model (
    const ImportKit::Container::Model& other)
  {
    name = csStrNewW (other.name);
    glueModel = other.glueModel;
    meshes = other.meshes;
    type = other.type;
  }

  //-------------------------------------------------------------------------

  ImportKit::Container::Material::~Material()
  {
    delete[] name;
    delete[] texture;
  }

  ImportKit::Container::Material::Material (const Material& other)
  {
    name = csStrNewW (other.name);
    texture = csStrNew (other.texture);
  }

  //-------------------------------------------------------------------------

  ImportKit::ImportKit (iObjectRegistry* objectReg)
  {
    glue = new Glue (objectReg);
  }

  ImportKit::~ImportKit ()
  {
    delete glue;
  }

  ImportKit::Container* ImportKit::OpenContainer (const char* filename, 
						  const char* archive)
  {
    Container* cnt = new Container();
    if (!glue->PopulateContainer (filename, archive, *cnt))
    {
      delete cnt;
      return 0;
    }

    return cnt;
  }

} // Namespace Utility
} // namespace CS
