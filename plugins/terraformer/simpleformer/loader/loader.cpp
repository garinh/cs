/*
    Copyright (C) 2004 Anders Stenberg, Daniel Duhprey

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

#include "csutil/csendian.h"
#include "csutil/csstring.h"
#include "csutil/dirtyaccessarray.h"
#include "csutil/scf.h"

#include "iengine/engine.h"
#include "iengine/mesh.h"
#include "iengine/movable.h"
#include "imap/ldrctxt.h"
#include "imap/loader.h"
#include "imap/loader.h"
#include "imap/services.h"
#include "iutil/databuff.h"
#include "iutil/document.h"
#include "iutil/object.h"
#include "iutil/objreg.h"
#include "iutil/plugin.h"
#include "iutil/vfs.h"
#include "ivaria/reporter.h"
#include "ivaria/simpleformer.h"
#include "ivaria/terraform.h"

#include "loader.h"



using namespace CS::Plugins::SimpleFormerLoader;

SCF_IMPLEMENT_FACTORY (csSimpleFormerLoader)

csSimpleFormerLoader::csSimpleFormerLoader (iBase* parent) :
  scfImplementationType(this, parent)
{
}

csSimpleFormerLoader::~csSimpleFormerLoader ()
{
}

bool csSimpleFormerLoader::Initialize (iObjectRegistry* object_reg)
{
  objreg = object_reg;
  synldr = csQueryRegistry<iSyntaxService> (objreg);
  pluginmgr = csQueryRegistry<iPluginManager> (objreg);

  InitTokenTable (xmltokens);
  return true;
}

csPtr<iBase> csSimpleFormerLoader::Parse (iDocumentNode* node,
		iStreamSource*, iLoaderContext* /*ldr_context*/,
		iBase* /*context*/)
{
  csRef<iTerraFormer> former = csLoadPlugin<iTerraFormer> (pluginmgr, 
  	"crystalspace.terraformer.simple");
  if (!former) 
  {
    synldr->ReportError ("crystalspace.terraformer.simple.loader",
      node, "Could not loader crystalspace.terraformer.simple plugin");
    return 0;
  }
  csRef<iSimpleFormerState> state =  
        scfQueryInterface<iSimpleFormerState> (former);

  uint alphamapcount = 0;
  bool material_map_set = false;
  bool one_material_map_used = false;

  csRef<iDocumentNodeIterator> it = node->GetNodes ();
  while (it->HasNext ())
  {
    csRef<iDocumentNode> child = it->Next ();
    if (child->GetType () != CS_NODE_ELEMENT) continue;
    const char *value = child->GetValue ();
    csStringID id = xmltokens.Request (value);
    switch (id)
    {
      case XMLTOKEN_NAME:
      {
        const char *name = child->GetContentsValue ();
        objreg->Register (former, name);
        former->QueryObject ()->SetName (name);
        break;
      }
      case XMLTOKEN_HEIGHTMAP: 
      {
	const char *format = child->GetAttributeValue ("format");
	if ((format == 0) || (strcmp (format, "image") == 0))
	{
	  const char *image = child->GetContentsValue ();
	  csRef<iLoader> loader = csQueryRegistry<iLoader> (objreg);
	  csRef<iImage> map = loader->LoadImage (image, CS_IMGFMT_ANY);
	  if (map == 0) 
	  {
	    synldr->ReportError ("crystalspace.terraformer.simple.loader",
	      child, "Error reading in image file '%s' for heightmap", image);
	    return 0;
	  }
	  state->SetHeightmap (map);
	}
	else if (strcmp (format, "heightmap32") == 0)
	{
	  if (!LoadHeightmap32 (child, state))
	    return 0;
	}
	else if (strcmp (format, "raw16le") == 0)
	{
	  if (!LoadHeightmapRaw16LE (child, state))
	    return 0;
	}
	else if (strcmp (format, "raw16be") == 0)
	{
	  if (!LoadHeightmapRaw16BE (child, state))
	    return 0;
	}
	else if (strcmp (format, "raw32le") == 0)
	{
	  if (!LoadHeightmapRaw32LE (child, state))
	    return 0;
	}
	else if (strcmp (format, "raw32be") == 0)
	{
	  if (!LoadHeightmapRaw32BE (child, state))
	    return 0;
	}
	else if (strcmp (format, "rawfloatle") == 0)
	{
	  if (!LoadHeightmapRawFloatLE (child, state))
	    return 0;
	}
	else if (strcmp (format, "rawfloatbe") == 0)
	{
	  if (!LoadHeightmapRawFloatBE (child, state))
	    return 0;
	}
	else
	{
	  synldr->ReportError ("crystalspace.terraformer.simple.loader",
	    child, "Unknown heightmap format '%s'", format);
	  return 0;
	}
        break;
      }
      case XMLTOKEN_HEIGHTMAP32: 
      {
	if (!LoadHeightmap32 (child, state))
	  return 0;
        break;
      }
      case XMLTOKEN_INTMAP: 
      {
        const char *image = child->GetContentsValue ();
	csRef<iLoader> loader = csQueryRegistry<iLoader> (objreg);
        csRef<iImage> map = loader->LoadImage (image);
        if (map == 0) 
        {
          synldr->ReportError ("crystalspace.terraformer.simple.loader",
            child, "Error reading in image file for intmap '%s'", image);
          return 0;
        }
	int scale = child->GetAttributeValueAsInt ("scale");
	int offset = child->GetAttributeValueAsInt ("offset");
	const char* typestring = child->GetAttributeValue ("type");
        csRef<iStringSet> strings = csQueryRegistryTagInterface<iStringSet> (
	  objreg, "crystalspace.shared.stringset");
        csStringID type = strings->Request (typestring);
        state->SetIntegerMap (type, map, scale, offset);
        break;
      }
      case XMLTOKEN_FLOATMAP: 
      {
        const char *image = child->GetContentsValue ();
	csRef<iLoader> loader = csQueryRegistry<iLoader> (objreg);
        csRef<iImage> map = loader->LoadImage (image);
        if (map == 0) 
        {
          synldr->ReportError ("crystalspace.terraformer.simple.loader",
            child, "Error reading in image file for floatmap '%s'", image);
          return 0;
        }
	float scale = child->GetAttributeValueAsFloat ("scale");
	float offset = child->GetAttributeValueAsFloat ("offset");
	const char* typestring = child->GetAttributeValue ("type");
        csRef<iStringSet> strings = csQueryRegistryTagInterface<iStringSet> (
	  objreg, "crystalspace.shared.stringset");
        csStringID type = strings->Request (typestring);
        state->SetFloatMap (type, map, scale, offset);
        break;
      }
      case XMLTOKEN_SCALE:
      {
        csVector3 v;
        if (!synldr->ParseVector (child, v))
        {
          synldr->ReportError ("crystalspace.terraformer.simple.loader",
            child, "Error parsing scale vector");
          return 0;
        }
        state->SetScale (v);
        break;
      }
      case XMLTOKEN_OFFSET:
      {
        csVector3 v;
        if (!synldr->ParseVector (child, v))
        {
          synldr->ReportError ("crystalspace.terraformer.simple.loader",
            child, "Error parsing scale vector");
          return 0;
        }
        state->SetOffset (v);
        break;
      }
      case XMLTOKEN_MATERIALMAP:
      {
//@@@
/*
        if (!palette_set)
        {
          synldr->ReportError ("crystalspace.terrain.factory.loader",
              child, "First set a material palette before <materialmap>!");
          return 0;
        }
*/
        material_map_set = true;
        one_material_map_used = true;
        const char* imagefile = child->GetAttributeValue ("image");
        const char *arrayfile = child->GetAttributeValue ("raw");
        int width = child->GetAttributeValueAsInt ("width");
        int height = child->GetAttributeValueAsInt ("height");
        if (imagefile != 0)
        {
          csRef<iLoader> loader = csQueryRegistry<iLoader> (objreg);
          csRef<iImage> map = loader->LoadImage(imagefile,CS_IMGFMT_PALETTED8);
          if (map == 0)
          {
            synldr->ReportError ("crystalspace.terrain.factory.loader",
              child, "Error reading in image file for heightmap '%s'",
              imagefile);
            return 0;
          }

          csRef<iStringSet> strings = csQueryRegistryTagInterface<iStringSet> (
            objreg, "crystalspace.shared.stringset");

          state->SetIntegerMap(strings->Request("materialmap"), map);
//          state->SetMaterialMapFile (imagefile, map->GetWidth (),
//          map->GetHeight ());
        }
        else if (arrayfile != 0 && width != 0 && height != 0)
        {
//@@@ fixme or remove
        synldr->ReportError ("crystalspace.terrain.factory.loader",
              child, "Using raw files is broken! Complain to Fossi.");
        return 0;
          csRef<iVFS> vfs = csQueryRegistry<iVFS> (objreg);
          csRef<iFile> file = vfs->Open (arrayfile, VFS_FILE_READ);
          if (file == 0)
          {

            synldr->ReportError ("crystalspace.terrain.factory.loader",
              child, "Error reading in raw file for heightmap '%s'",
              arrayfile);
            return 0;
          }
          csArray<char> array;
          int index = 0;
          while (!file->AtEOF())
          {
            file->Read (&array.GetExtend (index++), sizeof (char));
          }
//          state->SetIntegerMap (array, width, height);
//          state->SetMaterialMapFile (arrayfile, width,
//          height, true);
        }
        else
        {
          synldr->ReportError ("crystalpace.terrain.object.loader",
            child, "No image or raw file specified for material map");
          return 0;
        }
        break;
      }
      case XMLTOKEN_MATERIALALPHAMAP:
      {
        const char* imagefile = child->GetAttributeValue ("image");
        if (imagefile != 0)
        {
          csRef<iLoader> loader = csQueryRegistry<iLoader> (objreg);
          csRef<iImage> map = loader->LoadImage(imagefile,CS_IMGFMT_PALETTED8);
          if (map == 0)
          {
            synldr->ReportError ("crystalspace.terraformer.simple.loader",
              child, "Error reading in image file for heightmap '%s'",
              imagefile);
            return 0;
          }
          csString id = csString("alphamap ");
          id += alphamapcount++;

          csRef<iStringSet> strings = csQueryRegistryTagInterface<iStringSet> (
            objreg, "crystalspace.shared.stringset");

          state->SetIntegerMap(strings->Request(id), map);
        }
        else
        {
          synldr->ReportError ("crystalspace.terraformer.simple.loader",
            child, "No image file specified for materialraw  p");
          return 0;
        }
        break;
      }
      default:
        synldr->ReportError ("crystalspace.terraformer.simple.loader",
          child, "Unknown token!");
    }
  }

  return csPtr<iBase> (former);
}

template<typename Endianness>
struct GetterFloat
{
  static inline void Get (char*& buf, float&f)
  {
    f = csIEEEfloat::ToNative (Endianness::Convert (csGetFromAddress::UInt32 (buf))); 
    buf += sizeof(uint32);
  }
  static inline size_t ItemSize()
  { return sizeof(uint32); }
};

template<typename Endianness>
struct GetterUint16
{
  static inline void Get (char*& buf, float&f)
  {
    uint16 v = Endianness::Convert (csGetFromAddress::UInt16 (buf));
    buf += sizeof (uint16);
    f = float(v) / 65535.0f;
  }
  static inline size_t ItemSize()
  { return sizeof(uint16); }
};

template<typename Endianness>
struct GetterUint32
{
  static inline void Get (char*& buf, float&f)
  {
    uint32 v = Endianness::Convert (csGetFromAddress::UInt32 (buf));
    buf += sizeof (uint32);
    f = float(v) / 4294967295.0f;
  }
  static inline size_t ItemSize()
  { return sizeof(uint32); }
};

namespace CS
{
namespace Plugins
{
namespace SimpleFormerLoader
{
template<typename Tgetter>
class RawHeightmapReader
{
  csSimpleFormerLoader* loader;
  iSimpleFormerState* state;
public:
  RawHeightmapReader (csSimpleFormerLoader* loader, 
    iSimpleFormerState* state) : loader(loader), state(state) {}

  bool ReadData (char* buf, uint w, uint h)
  {
    const size_t num = w * h;
    csDirtyAccessArray<float> fdata;
    fdata.SetSize (num);
    for (size_t i = 0; i < num; i++)
    {
      Tgetter::Get (buf, fdata[i]);
    }
    state->SetHeightmap (fdata.GetArray(), w, h);
    return true;
  }

  bool ReadRawMap (iDocumentNode* child)
  {
    csRef<iDataBuffer> buf;
    if (!(buf = loader->GetDataBuffer (child)))
      return false;

    int w = child->GetAttributeValueAsInt ("width");
    int h = child->GetAttributeValueAsInt ("height");
    if (w <= 0)
    {
      loader->synldr->ReportError (
	"crystalspace.terraformer.simple.loader",
	child, "Bogus raw map width %d", w);
      return false;
    }
    if (h <= 0)
    {
      loader->synldr->ReportError (
	"crystalspace.terraformer.simple.loader",
	child, "Bogus raw map height %d", h);
      return false;
    }

    if (buf->GetSize() < (w * h * Tgetter::ItemSize()))
    {
      const char *filename = child->GetContentsValue ();
      loader->synldr->ReportError (
	"crystalspace.terraformer.simple.loader",
	child, "File '%s' is not a valid raw heightmap file: size mismatch",
	    filename);
      return false;
    }

    if (!ReadData (buf->GetData(), w, h))
      return false;

    return true;
  }
};
}
}
}

csRef<iDataBuffer> csSimpleFormerLoader::GetDataBuffer (iDocumentNode* child)
{
  const char *filename = child->GetContentsValue ();
  csRef<iVFS> vfs = csQueryRegistry<iVFS> (objreg);
  csRef<iDataBuffer> buf = vfs->ReadFile (filename, false);
  if (buf == 0) 
  {
    synldr->ReportError ("crystalspace.terraformer.simple.loader",
      child, "Error reading in file '%s' for heightmap", filename);
  }

  return buf;
}

bool csSimpleFormerLoader::LoadHeightmap32 (iDocumentNode* child, 
					    iSimpleFormerState* state)
{
  csRef<iDataBuffer> buf;
  if (!(buf = GetDataBuffer (child)))
    return false;

  char* data = buf->GetData ();
  char c1 = *data++;
  char c2 = *data++;
  char c3 = *data++;
  char c4 = *data++;
  if (c1 != 'H' || c2 != 'M' || c3 != '3' || c4 != '2')
  {
    const char *filename = child->GetContentsValue ();
    synldr->ReportError ("crystalspace.terraformer.simple.loader",
      child, "File '%s' is not a heightmap32 file", filename);
    return false;
  }
  uint32 width = csLittleEndian::Convert (csGetFromAddress::UInt32 (data)); 
  data += 4;
  uint32 height = csLittleEndian::Convert (csGetFromAddress::UInt32 (data)); 
  data += 4;
  if (buf->GetSize () != (4+4+4+ width*height*4))
  {
    const char *filename = child->GetContentsValue ();
    synldr->ReportError ("crystalspace.terraformer.simple.loader",
      child, "File '%s' is not a valid heightmap32 file: size mismatch",
	  filename);
    return false;
  }
  RawHeightmapReader<GetterUint32<csLittleEndian> > reader (this, state);
  if (!reader.ReadData (data, width, height))
    return false;

  return true;
}

bool csSimpleFormerLoader::LoadHeightmapRaw16LE (iDocumentNode* child, 
						 iSimpleFormerState* state)
{
  RawHeightmapReader<GetterUint16<csLittleEndian> > reader (this, state);
  return reader.ReadRawMap (child);
}

bool csSimpleFormerLoader::LoadHeightmapRaw16BE (iDocumentNode* child, 
						 iSimpleFormerState* state)
{
  RawHeightmapReader<GetterUint16<csBigEndian> > reader (this, state);
  return reader.ReadRawMap (child);
}

bool csSimpleFormerLoader::LoadHeightmapRaw32LE (iDocumentNode* child, 
						 iSimpleFormerState* state)
{
  RawHeightmapReader<GetterUint32<csLittleEndian> > reader (this, state);
  return reader.ReadRawMap (child);
}

bool csSimpleFormerLoader::LoadHeightmapRaw32BE (iDocumentNode* child, 
						 iSimpleFormerState* state)
{
  RawHeightmapReader<GetterUint32<csBigEndian> > reader (this, state);
  return reader.ReadRawMap (child);
}

bool csSimpleFormerLoader::LoadHeightmapRawFloatLE (iDocumentNode* child, 
						    iSimpleFormerState* state)
{
  RawHeightmapReader<GetterFloat<csLittleEndian> > reader (this, state);
  return reader.ReadRawMap (child);
}

bool csSimpleFormerLoader::LoadHeightmapRawFloatBE (iDocumentNode* child, 
						    iSimpleFormerState* state)
{
  RawHeightmapReader<GetterFloat<csBigEndian> > reader (this, state);
  return reader.ReadRawMap (child);
}

