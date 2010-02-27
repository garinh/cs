/*
    Copyright (C) 2000-2001 by Jorrit Tyberghein
    Copyright (C) 1998-2000 by Ivan Avramovic <ivan@avramovic.com>

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
#include "csqint.h"

#include "csgfx/imagememory.h"
#include "cstool/vfsdirchange.h"
#include "igraphic/animimg.h"
#include "imap/services.h"
#include "itexture/iproctex.h"
#include "iutil/stringarray.h"
#include "iutil/vfs.h"
#include "ivaria/reporter.h"
#include "ivideo/material.h"

#include "csthreadedloader.h"
#include "loadtex.h"

CS_PLUGIN_NAMESPACE_BEGIN(csparser)
{
  THREADED_CALLABLE_IMPL4(csThreadedLoader, ParseTexture, csRef<iLoaderContext> ldr_context,
    csRef<iDocumentNode> node, csSafeCopyArray<ProxyTexture>* proxyTextures, const char* path)
  {
    csVfsDirectoryChanger dirChange(vfs);
    dirChange.ChangeTo(path);

    const char* txtname = node->GetAttributeValue ("name");

    iTextureWrapper* t = ldr_context->FindTexture (txtname, false);
    if (t)
    {
      ldr_context->AddToCollection(t->QueryObject ());
      ret->SetResult(scfQueryInterfaceSafe<iBase>(t));
      return true;
    }

    if(!AddLoadingTexture(txtname))
    {
      // Fixme.
      while(!t)
      {
        t = ldr_context->FindTexture (txtname, false);
      }

      ldr_context->AddToCollection(t->QueryObject ());
      ret->SetResult(scfQueryInterfaceSafe<iBase>(t));
      return true;
    }

    csRef<iTextureWrapper> tex;
    csRef<iLoaderPlugin> plugin;

    csString filename = node->GetAttributeValue ("file");
    enum { None, Filename, Color } imageSourceType = None;
    if (!filename.IsEmpty()) imageSourceType = Filename;
    csColor4 singleColor (0, 0, 0);
    csColor transp (0, 0, 0);
    bool do_transp = false;
    bool keep_image = false;
    bool always_animate = false;
    TextureLoaderContext context (txtname);
    csRef<iDocumentNode> ParamsNode;
    csString type;
    csAlphaMode::AlphaType alphaType = csAlphaMode::alphaNone;
    bool overrideAlphaType = false;

    csRefArray<iDocumentNode> key_nodes;

    csRef<iDocumentNodeIterator> it = node->GetNodes ();
    while (it->HasNext ())
    {
      csRef<iDocumentNode> child = it->Next ();
      if (child->GetType () != CS_NODE_ELEMENT) continue;
      const char* value = child->GetValue ();
      csStringID id = xmltokens.Request (value);
      switch (id)
      {
      case XMLTOKEN_KEY:
        key_nodes.Push (child);
        break;
      case XMLTOKEN_FOR2D:
        {
          bool for2d;
          if (!SyntaxService->ParseBool (child, for2d, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (for2d)
            context.SetFlags (context.GetFlags() | CS_TEXTURE_2D);
          else
            context.SetFlags (context.GetFlags() & ~CS_TEXTURE_2D);
        }
        break;
      case XMLTOKEN_FOR3D:
        {
          bool for3d;
          if (!SyntaxService->ParseBool (child, for3d, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (for3d)
            context.SetFlags (context.GetFlags() | CS_TEXTURE_3D);
          else
            context.SetFlags (context.GetFlags() & ~CS_TEXTURE_3D);
        }
        break;
      case XMLTOKEN_TRANSPARENT:
        do_transp = true;
        if (!SyntaxService->ParseColor (child, transp))
          return false;
        break;
      case XMLTOKEN_FILE:
        {
          const char* fname = child->GetContentsValue ();
          if (!fname)
          {
            SyntaxService->ReportError (
              "crystalspace.maploader.parse.texture",
              child, "Expected VFS filename for 'file'!");
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (imageSourceType == Color)
          {
            SyntaxService->ReportError (
              "crystalspace.maploader.parse.texture",
              child, "<file> is specified, but <color> was specified earlier");
          }
          imageSourceType = Filename;
          filename = fname;
        }
        break;
      case XMLTOKEN_COLOR:
        {
          if (!SyntaxService->ParseColor (child, singleColor))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (imageSourceType == Filename)
          {
            SyntaxService->ReportError (
              "crystalspace.maploader.parse.texture",
              child, "<color> is specified, but <file> was specified earlier");
          }
          imageSourceType = Color;
        }
        break;
      case XMLTOKEN_MIPMAP:
        {
          bool mm;
          if (!SyntaxService->ParseBool (child, mm, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (!mm)
            context.SetFlags (context.GetFlags() | CS_TEXTURE_NOMIPMAPS);
          else
            context.SetFlags (context.GetFlags() & ~CS_TEXTURE_NOMIPMAPS);
        }
        break;
      case XMLTOKEN_NPOTS:
        {
          bool npots;
          if (!SyntaxService->ParseBool (child, npots, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (npots)
            context.SetFlags (context.GetFlags() | CS_TEXTURE_NPOTS);
          else
            context.SetFlags (context.GetFlags() & ~CS_TEXTURE_NPOTS);
        }
        break;
      case XMLTOKEN_KEEPIMAGE:
        {
          if (!SyntaxService->ParseBool (child, keep_image, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
        }
        break;
      case XMLTOKEN_PARAMS:
        ParamsNode = child;
        break;
      case XMLTOKEN_TYPE:
        type = child->GetContentsValue ();
        if (type.IsEmpty ())
        {
          SyntaxService->ReportError (
            "crystalspace.maploader.parse.texture",
            child, "Expected plugin ID for <type>!");
          RemoveLoadingTexture(txtname);
          return false;
        }
        break;
      case XMLTOKEN_SIZE:
        {
          csRef<iDocumentAttribute> attr_w, attr_h;
          if ((attr_w = child->GetAttribute ("width")) &&
            (attr_h = child->GetAttribute ("height")))
          {
            context.SetSize (attr_w->GetValueAsInt(),
              attr_h->GetValueAsInt());
          }
        }
        break;
      case XMLTOKEN_ALWAYSANIMATE:
        if (!SyntaxService->ParseBool (child, always_animate, true))
          return false;
        break;
      case XMLTOKEN_CLAMP:
        {
          bool c;
          if (!SyntaxService->ParseBool (child, c, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (c)
            context.SetFlags (context.GetFlags() | CS_TEXTURE_CLAMP);
          else
            context.SetFlags (context.GetFlags() & ~CS_TEXTURE_CLAMP);
        }
        break;
      case XMLTOKEN_FILTER:
        {
          bool c;
          if (!SyntaxService->ParseBool (child, c, true))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          if (c)
            context.SetFlags (context.GetFlags() & ~CS_TEXTURE_NOFILTER);
          else
            context.SetFlags (context.GetFlags() | CS_TEXTURE_NOFILTER);
        }
        break;
      case XMLTOKEN_CLASS:
        {
          context.SetClass (child->GetContentsValue ());
        }
        break;
      case XMLTOKEN_ALPHA:
        {
          csAlphaMode am;
          if (!SyntaxService->ParseAlphaMode (child, 0, am, false))
          {
            RemoveLoadingTexture(txtname);
            return false;
          }
          overrideAlphaType = true;
          alphaType = am.alphaType;
        }
        break;
      default:
        {
          SyntaxService->ReportBadToken (child);
          RemoveLoadingTexture(txtname);
          return false;
        }
      }
    }

    csString texClass = context.GetClass();

    if (imageSourceType != Color)
    {
      // Proxy texture loading if the loader isn't specified
      // and we don't need to load them immediately.
      if(txtname && type.IsEmpty() && ldr_context->GetKeepFlags() == KEEP_USED &&
	ldr_context->GetCollection())
      {
	if (filename.IsEmpty())
	{
	  filename = txtname;
	}
  
	// Get absolute path (on VFS) of the file.
	csRef<iDataBuffer> absolutePath = vfs->ExpandPath(filename);
	filename = absolutePath->GetData();
  
	ProxyTexture proxTex;
	proxTex.img.AttachNew (new ProxyImage (this, filename, object_reg));
	proxTex.always_animate = always_animate;
  
	tex = Engine->GetTextureList()->CreateTexture (proxTex.img);
	tex->SetTextureClass(context.GetClass());
	tex->SetFlags(context.GetFlags());
	tex->QueryObject()->SetName(txtname);
	AddTextureToList(tex);
	RemoveLoadingTexture(txtname);
  
	proxTex.alphaType = csAlphaMode::alphaNone;
	if(overrideAlphaType)
	{
	  proxTex.alphaType = alphaType;
	}
  
	if(keep_image)
	  tex->SetKeepImage(true);
  
	proxTex.keyColour.do_transp = do_transp;
	if(do_transp)
	{
	  proxTex.keyColour.colours = transp;
	}
  
	proxTex.textureWrapper = tex;
	ldr_context->AddToCollection(proxTex.textureWrapper->QueryObject());
	proxyTextures->Push(proxTex);
	ret->SetResult(scfQueryInterfaceSafe<iBase>(proxTex.textureWrapper));
  
	return true;
      }

      // @@@ some more comments
      if (type.IsEmpty () && filename.IsEmpty ())
      {
	filename = txtname;
      }
    }

    iTextureManager* texman;
    texman = g3d ? g3d->GetTextureManager() : 0;
    int Format;
    Format = texman ? texman->GetTextureFormat () : CS_IMGFMT_TRUECOLOR;
    csRef<iLoaderPlugin> BuiltinImageTexLoader;
    if (imageSourceType == Color)
    {
      csRGBpixel singlePixel (int (singleColor.red * 255),
        int (singleColor.green * 255),
        int (singleColor.blue * 255),
        int (singleColor.alpha * 255));
      csRef<iImage> image;
      image.AttachNew (new csImageMemory (1, 1, (const void*)&singlePixel, Format));
      context.SetImage (image);
    }
    else if (!filename.IsEmpty ())
    {
      csRef<iThreadReturn> ret = csPtr<iThreadReturn>(new csLoaderReturn(threadman));
      if(!LoadImageTC (ret, false, vfs->GetCwd(), filename, Format, false))
      {
        SyntaxService->Report("crystalspace.maploader.parse.texture",
          CS_REPORTER_SEVERITY_WARNING, node, "Could not load image %s!", filename.GetData());
      }

      csRef<iImage> image = scfQueryInterfaceSafe<iImage>(ret->GetResultRefPtr());
      context.SetImage (image);
      if (image.IsValid() && type.IsEmpty ())
      {
        // special treatment for animated textures
        csRef<iAnimatedImage> anim = scfQueryInterface<iAnimatedImage> (image);
        if (anim && anim->IsAnimated())
        {
          type = PLUGIN_TEXTURELOADER_ANIMIMG;
        }
      }
    }
    /* If an image but no texture type is given use the builtin image texture
       loader */
    if ((context.GetImage() != 0) && type.IsEmpty ())
    {
      csImageTextureLoader* itl = new csImageTextureLoader (0);
      itl->Initialize (object_reg);
      BuiltinImageTexLoader.AttachNew(itl);
      plugin = BuiltinImageTexLoader;
    }

    iBinaryLoaderPlugin* Binplug;
    if ((!type.IsEmpty ()) && !plugin)
    {
      iDocumentNode* defaults = 0;
      iLoaderPlugin* Plugin;
      loaded_plugins.FindPlugin (type, Plugin, Binplug, defaults);
      plugin = Plugin;

      if (defaults != 0)
      {
        ReportWarning (
          "crystalspace.maploader.parse.texture",
          node, "'defaults' section is ignored for textures!");
      }
    }

    if ((!type.IsEmpty ()) && !plugin)
    {
      SyntaxService->Report (
        "crystalspace.maploader.parse.texture",
        CS_REPORTER_SEVERITY_WARNING,
        node, "Could not get plugin '%s', using default", (const char*)type);

      if (!BuiltinImageTexLoader)
      {
        csImageTextureLoader* itl = new csImageTextureLoader (0);
        itl->Initialize (object_reg);
        BuiltinImageTexLoader.AttachNew (itl);
      }
      plugin = BuiltinImageTexLoader;
    }
    if (plugin)
    {
      csRef<iBase> b = plugin->Parse (ParamsNode,
        0/*ssource*/, ldr_context, static_cast<iBase*> (&context));
      if (b) tex = scfQueryInterface<iTextureWrapper> (b);
    }

    if (!tex)
    {
      SyntaxService->Report (
        "crystalspace.maploader.parse.texture",
        CS_REPORTER_SEVERITY_WARNING,
        node, "Could not load texture '%s', using checkerboard instead", txtname);

      csRef<iLoaderPlugin> BuiltinErrorTexLoader;
      BuiltinErrorTexLoader.AttachNew(new csMissingTextureLoader (object_reg));
      csRef<iBase> b = BuiltinErrorTexLoader->Parse (ParamsNode,
        0, ldr_context, static_cast<iBase*> (&context));
      if (!b.IsValid())
      {
        static bool noMissingWarned = false;
        if (!noMissingWarned)
        {
          SyntaxService->Report (
            "crystalspace.maploader.parse.texture",
            CS_REPORTER_SEVERITY_ERROR,
            node, "Could not create default texture!");
          noMissingWarned = true;
          RemoveLoadingTexture(txtname);
          return false;
        }
      }
      tex = scfQueryInterface<iTextureWrapper> (b);
    }

    if (tex)
    {
      CS_ASSERT_MSG("Texture loader did not register texture", 
        tex->GetTextureHandle());
      tex->QueryObject ()->SetName (txtname);
      if (keep_image) tex->SetKeepImage (true);
      if (do_transp)
        tex->SetKeyColor (csQint (transp.red * 255.99),
        csQint (transp.green * 255.99), csQint (transp.blue * 255.99));
      tex->SetTextureClass (context.GetClass ());
      if (overrideAlphaType)
        tex->GetTextureHandle()->SetAlphaType (alphaType);

      csRef<iProcTexture> ipt = scfQueryInterface<iProcTexture> (tex);
      if (ipt)
        ipt->SetAlwaysAnimate (always_animate);
      ldr_context->AddToCollection(tex->QueryObject ());

      size_t i;
      for (i = 0 ; i < key_nodes.GetSize () ; i++)
      {
        if (!ParseKey (key_nodes[i], tex->QueryObject()))
        {
          RemoveLoadingTexture(txtname);
          return false;
        }
      }
    }

    if (texman)
    {
      if (!tex->GetTextureHandle ()) tex->Register (texman);
    }

    AddTextureToList(tex);
    RemoveLoadingTexture(txtname);
    ret->SetResult(scfQueryInterfaceSafe<iBase>(tex));
    return true;
  }

  bool csThreadedLoader::ParseMaterial (iLoaderContext* ldr_context,
    iDocumentNode* node, csWeakRefArray<iMaterialWrapper> &materialArray, const char *prefix)
  {
    const char* matname = node->GetAttributeValue ("name");
    iMaterialWrapper* m = ldr_context->FindMaterial (matname, false);
    if (m)
    {
      ldr_context->AddToCollection(m->QueryObject ());
      return true;
    }

    if(!AddLoadingMaterial(matname))
    {
      // Fixme.
      while(!m)
      {
        m = ldr_context->FindMaterial (matname, false);
      }

      ldr_context->AddToCollection(m->QueryObject ());
      return true;
    }

    csRef<iTextureWrapper> texh = 0;
    bool col_set = false;
    csColor col;

    bool shaders_mentioned = false;	// If true there were shaders.
    csArray<csStringID> shadertypes;
    csArray<iShader*> shaders;
    csRefArray<csShaderVariable> shadervars;

    csRefArray<iDocumentNode> key_nodes;

    csRef<iDocumentNodeIterator> it = node->GetNodes ();
    while (it->HasNext ())
    {
      csRef<iDocumentNode> child = it->Next ();
      if (child->GetType () != CS_NODE_ELEMENT) continue;
      const char* value = child->GetValue ();
      csStringID id = xmltokens.Request (value);
      switch (id)
      {
      case XMLTOKEN_KEY:
        key_nodes.Push (child);
        break;
      case XMLTOKEN_TEXTURE:
        {
          const char* txtname = child->GetContentsValue ();
          texh = ldr_context->FindTexture (txtname);
          if (!texh)
          {
            csRef<iThreadReturn> itr = csPtr<iThreadReturn>(new csLoaderReturn(threadman));
            LoadTextureTC(itr, false, vfs->GetCwd(), txtname, txtname, CS_TEXTURE_3D, 0, true, false, true,
              ldr_context->GetCollection(), ldr_context->GetKeepFlags(), true);
            texh = scfQueryInterface<iTextureWrapper>(itr->GetResultRefPtr());
            if(!texh)
            {
              ReportError (
                "crystalspace.maploader.parse.material",
                "Cannot find texture '%s' for material `%s'", txtname, matname);
              RemoveLoadingMaterial(matname);
              return false;
            }
          }
        }
        break;
      case XMLTOKEN_COLOR:
        {
          col_set = true;
          if (!SyntaxService->ParseColor (child, col))
          {
            RemoveLoadingMaterial(matname);
            return false;
          }
        }
        break;
      case XMLTOKEN_SHADER:
        {
          shaders_mentioned = true;
          csRef<iShaderManager> shaderMgr = 
            csQueryRegistry<iShaderManager> (object_reg);
          if (!shaderMgr)
          {
            ReportNotify ("iShaderManager not found, ignoring shader!");
            break;
          }
          const char* shadername = child->GetContentsValue ();
          iShader* shader = ldr_context->FindShader (shadername);
          if (!shader)
          {
            ReportNotify (
              "Shader (%s) couldn't be found for material %s, ignoring it",
              shadername, matname);
            break;
          }
          const char* shadertype = child->GetAttributeValue ("type");
          if (!shadertype)
          {
            ReportNotify (
              "No shadertype for shader %s in material %s, ignoring it",
              shadername, matname);
            break;
          }
          shadertypes.Push (stringSet->Request(shadertype));
          shaders.Push (shader);
        }
        break;
      case XMLTOKEN_SHADERVAR:
        {
          //create a new variable
          csRef<csShaderVariable> var;
          var.AttachNew (new csShaderVariable);

          if (!SyntaxService->ParseShaderVar (ldr_context, child, *var))
          {
            break;
          }
          shadervars.Push (var);
        }
        break;
      default:
        {
          SyntaxService->ReportBadToken (child);
          RemoveLoadingMaterial(matname);
          return false;
        }
      }
    }

    csRef<iMaterial> material = Engine->CreateBaseMaterial (texh);

    if (col_set)
    {
      csShaderVariable* flatSV = material->GetVariableAdd (
        stringSetSvName->Request (CS_MATERIAL_VARNAME_FLATCOLOR));
      flatSV->SetValue (col);
    }

    csRef<iMaterialWrapper> mat;

    if (prefix)
    {
      char *prefixedname = new char [strlen (matname) + strlen (prefix) + 2];
      strcpy (prefixedname, prefix);
      strcat (prefixedname, "_");
      strcat (prefixedname, matname);
      mat = Engine->GetMaterialList ()->CreateMaterial (material, prefixedname);
      delete [] prefixedname;
    }
    else
    {
      mat = Engine->GetMaterialList ()->CreateMaterial (material, matname);
    }
    AddMaterialToList(mat);
    RemoveLoadingMaterial(matname);

    size_t i;
    for (i=0; i<shaders.GetSize (); i++)
      //if (shaders[i]->Prepare ())
      material->SetShader (shadertypes[i], shaders[i]);
    for (i=0; i<shadervars.GetSize (); i++)
      material->AddVariable (shadervars[i]);

    // dereference material since mat already incremented it

    for (i = 0 ; i < key_nodes.GetSize () ; i++)
    {
      if (!ParseKey (key_nodes[i], mat->QueryObject()))
        return false;
    }
    ldr_context->AddToCollection(mat->QueryObject ());

    materialArray.Push(mat);

    return true;
  }
}
CS_PLUGIN_NAMESPACE_END(csparser)
