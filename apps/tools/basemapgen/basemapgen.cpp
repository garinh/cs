/*
    Copyright (C) 2007 by Jelle Hellemans aka sueastside

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

#include "crystalspace.h"
#include "basemapgen.h"

CS_IMPLEMENT_APPLICATION

// The global pointer to basemapgen
BaseMapGen *basemapgen;

BaseMapGen::BaseMapGen (iObjectRegistry* object_reg)
 : terraformerRE (0), meshRE (0)
{
  BaseMapGen::object_reg = object_reg;
}

BaseMapGen::~BaseMapGen ()
{
  delete terraformerRE;
  delete meshRE;
}

bool BaseMapGen::Initialize ()
{
  if (!csInitializer::RequestPlugins (object_reg,
  	CS_REQUEST_VFS,
	CS_REQUEST_FONTSERVER,
	CS_REQUEST_IMAGELOADER,
	CS_REQUEST_REPORTER,
	CS_REQUEST_REPORTERLISTENER,
	CS_REQUEST_END))
  {
    Report("Can't initialize plugins!");
    return false;
  }

  // Check for commandline help.
  if (csCommandLineHelper::CheckHelp (object_reg))
  {
    OnCommandLineHelp();
    return false;
  }

  cmdline = csQueryRegistry<iCommandLineParser> (object_reg);
  if (!cmdline)
  {
    Report("No iCommandLineParser plugin!");
    return false;
  }

  // Open the main system. This will open all the previously loaded plug-ins.
  if (!csInitializer::OpenApplication (object_reg))
  {
    Report("Error opening system!");
    return false;
  }
  
  const char* optTerrain = cmdline->GetOption ("terrainname", 0);
  if (optTerrain != 0)
    meshRE = new csRegExpMatcher (optTerrain);
  const char* optTerraformer = cmdline->GetOption ("terraformername", 0);
  if (optTerraformer != 0)
    terraformerRE = new csRegExpMatcher (optTerraformer);

  return true;
}

void BaseMapGen::Report(const char* msg, ...)
{
  va_list arg;
  va_start (arg, msg);
  csReportV(object_reg, CS_REPORTER_SEVERITY_ERROR, "crystalspace.application.basemapgen", msg, arg);
  va_end (arg);
}

void BaseMapGen::OnCommandLineHelp()
{
  csPrintf ("\n");
  csPrintf ("Usage: basemapgen <path> <world> [OPTIONS]\n");
  csPrintf (" Example: basemapgen /this/data/terrain world\n");
  csPrintf ("\n");
  csPrintf ("<path>                    The path to the worldfile           (default /this)\n");
  csPrintf ("<world>                   Name of the world file              (default 'world')\n");
  csPrintf ("-terrainname=<regexp>     Regexp for the terrain mesh objects (default '.*')\n");
  csPrintf ("-terraformername=<regexp> Regexp for the terraformer          (default '.*')\n");
  csPrintf ("-resolution=<pixels>      The resolution for the basemap      (default basemap resolution)\n");
}

static csPtr<iImage> GenerateErrorTexture (int width, int height)
{
  static const csRGBpixel colorTable[] = 
    {csRGBpixel (0,0,0,255), csRGBpixel (255,0,0,255),
     csRGBpixel (0,255,0,255), csRGBpixel (0,0,255,255)};

  size_t colorIndex = 0;

  csRef<csImageMemory> image; 
  image.AttachNew (new csImageMemory (width, height));
  csRGBpixel *pixel = (csRGBpixel*)image->GetImagePtr();
  for (int y = 0; y < height; y+=4)
  {
    for (int y2 = 0; y2 < 4; ++y2)
    {
      for (int x = 0; x < width; x+=4)
      {
        for (int x2 = 0; x2 < 4; ++x2)
        {
          *pixel++ = colorTable[colorIndex];
        }

        colorIndex ^= 0x1;// Flip lowest bit 
      }
    }
    colorIndex ^= 0x2; // Flip higher bit
  }

  return csPtr<iImage> (image);
}

csRef<iImage> BaseMapGen::LoadImage (const csString& filename, int format)
{
  csPrintf ("Trying to load '%s'... \t", filename.GetData());
  fflush (stdout);
  csRef<iImage> image;
  csRef<iImageIO> imageio = csQueryRegistry<iImageIO> (object_reg);
  csRef<iVFS> VFS = csQueryRegistry<iVFS> (object_reg);

  csRef<iDataBuffer> buf = VFS->ReadFile (filename.GetData(), false);
  if (!buf)
  {
    Report ("Failed to load image file '%s'!", filename.GetData());
    return GenerateErrorTexture (32, 32);
  }
  image = imageio->Load (buf, format);
  image.IsValid() ? csPrintf ("success.\n") : csPrintf ("failed.\n");

  return image;
}
  
void BaseMapGen::ScanPluginNodes ()
{
  csRef<iDocumentNode> plugins = rootnode->GetNode ("plugins");
  if (!plugins) return;

  csRef<iDocumentNodeIterator> itPlugin = plugins->GetNodes("plugin");
  while (itPlugin->HasNext())
  {
    csRef<iDocumentNode> plugin = itPlugin->Next();
    const char* pluginShort = plugin->GetAttributeValue ("name");
    const char* pluginID = plugin->GetContentsValue();
    pluginMap.Put (pluginShort, pluginID);
  }
}

const char* BaseMapGen::GetPluginSCFID (const char* pluginStr)
{
  if (!pluginStr) return 0;
  
  const char* id = pluginMap.Get (pluginStr, (const char*)0);
  if (id != 0) return id;
  return pluginStr;
  
}
  
void BaseMapGen::ScanTextures ()
{
  csRef<iDocumentNode> textures = rootnode->GetNode("textures");
  if (textures.IsValid())
  {
    csRef<iDocumentNodeIterator> it = textures->GetNodes("texture");
    while (it->HasNext())
    {
      csRef<iDocumentNode> current = it->Next();
      csString name = current->GetAttributeValue("name");
      csRef<iDocumentNode> file = current->GetNode("file");
      csString filename = file->GetContentsValue();
      textureFiles.Put (name, filename);
    }
  }
}

void BaseMapGen::ScanMaterials ()
{
  csRef<iDocumentNode> mats = rootnode->GetNode ("materials");
  if (!mats) return;

  csRef<iDocumentNodeIterator> it_mats = mats->GetNodes("material");
  while (it_mats->HasNext())
  {
    csRef<iDocumentNode> mat = it_mats->Next();
    csString matname = mat->GetAttributeValue ("name");
    
    // Get texture file name.
    csRef<iDocumentNode> tex = mat->GetNode ("texture");
    if (!tex) continue;
    const char* texname = tex->GetContentsValue();
    const char* texture_file = textureFiles.Get (texname, (const char*)0);
    if (!texture_file) continue;
    
    // Set the texture scale.
    csRef<iDocumentNodeIterator> it = mat->GetNodes("shadervar");
    csVector2 texscale(32,32);
    while (it->HasNext())
    {
      csRef<iDocumentNode> current = it->Next();
      csString name = current->GetAttributeValue("name");
      if ( name.Compare("texture scale"))
      {
	csString scalestr = current->GetContentsValue();
	size_t pos = scalestr.FindFirst(",",0);
	csString firststr = scalestr.Slice(0,pos);
	csString secondstr = scalestr.Slice(pos+1, scalestr.Length());
	int first = atoi(firststr.GetData());
	int second = atoi(secondstr.GetData());
	texscale = csVector2(first, second);
	break;
      }
    } // while

    csRef<MaterialLayer> material;
    material.AttachNew (new MaterialLayer);
    material->name = matname;
    material->texture_name = texname;
    material->texture_file = texture_file;
    material->texture_scale = texscale;
    materials.Put (matname, material);
  }
}

bool BaseMapGen::LoadMap ()
{
  csRef<iVFS> vfs = csQueryRegistry<iVFS> (object_reg);
  csRef<iConfigManager> cfgmgr = csQueryRegistry<iConfigManager> (object_reg);

  csRef<iConfigFile> cfgfile = cfgmgr->GetDynamicDomain ();
  csPrintf ("Loading custom VFS mounts from:\n%s\n", cfgfile->GetFileName ());
  vfs->LoadMountsFromFile(cfgfile);
  
  csString path = cmdline->GetName(0);
  csString world = cmdline->GetName(1);

  if (path.IsEmpty()) path = "/this";
  if (world.IsEmpty()) world = "world";

  csStringArray paths;
  paths.Push ("/lev/");
  if (!vfs->ChDirAuto(path.GetData(), &paths, 0, "world"))
  {
    Report("Error setting directory '%s'!", path.GetData ());
    return false;
  }

  csRef<iFile> buf = vfs->Open(world.GetData(), VFS_FILE_READ);
  if (!buf)
  {
    Report("Failed to open file '%s'!", world.GetData());
    return false;
  }

  csRef<iDocument> doc;
  csRef<iDocumentSystem> docsys;

  if (!docsys) docsys = csPtr<iDocumentSystem> (new csTinyDocumentSystem ());
  doc = docsys->CreateDocument ();
  const char* error = doc->Parse (buf, true);
  if (error != 0)
  {
    Report("Failed to parse file '%s': %s", world.GetData(),
      error);
    return false;
  }

  if (doc)
  {
    rootnode = doc->GetRoot()->GetNode("world");
    if (!rootnode)
      return false;
  }
  
  ScanPluginNodes ();
  
  ScanTextures();
  ScanMaterials();
  
  return true;
}
  
/* Get mipmap for an image, using precomputed mipmaps as far as
   possible. */
static csRef<iImage> GetImageMip (iImage* img, uint mip)
{
  if (mip == 0) return img;
  csRef<iImage> imgToMip (img);
  uint hasMips = img->HasMipmaps();
  if (mip <= hasMips) return img->GetMipmap (mip);
  imgToMip = img->GetMipmap (hasMips);
  mip -= hasMips;
  if (mip == 0) return imgToMip;
  return csImageManipulate::Mipmap (imgToMip, mip);
}

static csColor GetPixelWrap (iImage* img, int img_w, int img_h, 
                             int x, int y)
{
  // Wrap around the texture coordinates.
  x = x % img_w;
  y = y % img_h;

  // Get the pixel.
  csRGBpixel* px = (csRGBpixel*)img->GetImageData() + x + (y * img_h);
  return csColor (px->red, px->green, px->blue);
}

class LayerSampler
{
  int img_w, img_h;
  csRef<iImage> img;
  csVector2 textureScale;
public:
  LayerSampler (int basemap_w, int basemap_h, MaterialLayer* layer)
   : textureScale (layer->texture_scale)
  {
    float layer_needed_x = float (basemap_w) / textureScale.x;
    float layer_needed_y = float (basemap_h) / textureScale.y;
    iImage* layerImage = layer->GetImage();
    int mip_x = csFindNearestPowerOf2 (
      int (ceil (layerImage->GetWidth() / layer_needed_x)));
    int mip_y = csFindNearestPowerOf2 (
      int (ceil (layerImage->GetHeight() / layer_needed_y)));
    int mip = csMax (
      csClamp (csLog2 (mip_x), csLog2 (layerImage->GetWidth()), 0),
      csClamp (csLog2 (mip_y), csLog2 (layerImage->GetHeight()), 0));
    img = GetImageMip (layerImage, mip);
    img_w = img->GetWidth();
    img_h = img->GetHeight();
  }
  
  csColor GetPixel (float coord_x, float coord_y)
  {
    // Scale the texture coordinates.
    coord_x *= textureScale.x;
    coord_y *= textureScale.y;
  
    // Calculate the material coordinates.
    float matcoord_x_f = (coord_x * img_w);
    float matcoord_y_f = (coord_y * img_h);
    int matcoord_x = int (matcoord_x_f);
    int matcoord_y = int (matcoord_y_f);
  
    // Bilinearly filter from material.
    csColor p00 (GetPixelWrap (img, img_w, img_h,
      matcoord_x, matcoord_y));
    csColor p01 (GetPixelWrap (img, img_w, img_h,
      matcoord_x, matcoord_y+1));
    csColor p11 (GetPixelWrap (img, img_w, img_h,
      matcoord_x+1, matcoord_y+1));
    csColor p10 (GetPixelWrap (img, img_w, img_h,
      matcoord_x+1, matcoord_y));
  
    float f1 = matcoord_x_f - matcoord_x;
    float f2 = matcoord_y_f - matcoord_y;
  
    return csLerp (csLerp (p00, p10, f1), 
      csLerp (p01, p11, f1), f2);
  }
};

csPtr<iImage> BaseMapGen::CreateBasemap (int basemap_w, int basemap_h, 
                                         const AlphaLayers& alphaLayers,
                                         MaterialLayers& txt_layers)
{
  csPrintf ("Creating base texturemap... \n\n"); fflush (stdout);
  // Block: to measure time
  {
    CS::MeasureTime lTimeMeasurer ("Time taken");

    csArray<LayerSampler> samplers;
    /* Mipmap the materials to the highest mipmap needed below the required 
     * resolution. This mipmap is then upsampled. */
    for (unsigned int i = 0 ; i < txt_layers.GetSize() ; i++)
    {
      samplers.Push (LayerSampler (basemap_w, basemap_h, txt_layers[i]));
    }

    csRef<iImage> basemapImage;
    basemapImage.AttachNew (new csImageMemory (basemap_w, basemap_h));
    // Get image data.
    csRGBpixel* bm_dst = (csRGBpixel*)(basemapImage->GetImageData());

    float coord_x, coord_y;

    float inv_basemap_w = 1.0f / basemap_w;
    float inv_basemap_h = 1.0f / basemap_h;

    for (int y = 0 ; y < basemap_h ; y++)
    {
      // Draw progress.
      uint percent = uint ((y*100) * inv_basemap_h);
      DrawProgress(percent);

      for (int x = 0 ; x < basemap_w ; x++)
      {
        // Calculate the destination coordinates.
        coord_x    = x * inv_basemap_w;
        coord_y    = y * inv_basemap_h;

        csColor col (0, 0, 0);
        for (size_t l = 0; l < samplers.GetSize(); l++)
        {
          if (l >= alphaLayers.GetSize()) break;
        
          float a = alphaLayers.GetAlpha (l, coord_x, coord_y);
          // Blend material colors.
          col += samplers[l].GetPixel (coord_x, coord_y) * a;
        }

        csRGBpixel mat_dst (int (col.red), int (col.green), int (col.blue));

        // Set the basemap pixel.
        bm_dst->Set (mat_dst.red, mat_dst.green, mat_dst.blue);

        // Increase the pointer.
        bm_dst++;

      } // for y
    } // for x

    csPrintf ("\n");
  
    return csPtr<iImage> (basemapImage);
  } // Block to measure time

}

void BaseMapGen::SaveImage (iImage* image, const char* texname)
{
  csPrintf ("Saving %zu KB of data.\n", 
    csImageTools::ComputeDataSize (image)/1024);
	
  csRef<iImageIO> imageio = csQueryRegistry<iImageIO> (object_reg);
  csRef<iVFS> VFS = csQueryRegistry<iVFS> (object_reg);

  csRef<iDataBuffer> db = imageio->Save (image, "image/png", "progressive");
  if (db)
  {
    if (!VFS->WriteFile (texname, (const char*)db->GetData (), db->GetSize ()))
    {
      Report("Failed to write file '%s'!", texname);
      return;
    }
  }
  else
  {
    Report("Failed to save png image for basemap!");
    return;
  }
}

void BaseMapGen::DrawProgress (int percent)
{
  const uint progTotal = 65;
  uint numDone = (progTotal*percent) / 100;

  csPrintf (CS_ANSI_CURSOR_BWD(72) CS_ANSI_CLEAR_LINE "[");

  uint x;
  for (x = 0 ; x < numDone ; x++)
    csPrintf ("=");

  for (; x < progTotal; x++)
    csPrintf (" ");

  csPrintf("] %d%%", percent);

  fflush (stdout);

}

void BaseMapGen::Start ()
{
  // Load the world.
  if (!LoadMap ())
  {
    Report("Error reading world file!");
    return;
  }

  ScanOldMaterialMaps();
  ScanTerrain1Factories();
  ScanTerrain1Meshes();
  
  ScanTerrain2Factories();
  ScanTerrain2Meshes();
}

/*---------------------------------------------------------------------*
 * Main function
 *---------------------------------------------------------------------*/
int main (int argc, char* argv[])
{
  iObjectRegistry* object_reg = csInitializer::CreateEnvironment (argc, argv);
  if (!object_reg)
  {
    csReport (0, CS_REPORTER_SEVERITY_ERROR,
      "crystalspace.summoning.failed",
      "Not enough chickens sacrificed for cthulhu!");
    return -1;
  }

  basemapgen = new BaseMapGen (object_reg);

  if (basemapgen->Initialize ())
    basemapgen->Start ();

  delete basemapgen;

  csInitializer::DestroyApplication (object_reg);
  return 0;
}
