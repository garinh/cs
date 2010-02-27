/*
    Copyright (C) 2000-2001 by Jorrit Tyberghein
    Copyright (C) 2000 by Samuel Humphreys

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

#ifndef __CS_PROCTEX_H__
#define __CS_PROCTEX_H__

/**\file
 * Superclass for procedural textures. 
 */

#include "csextern.h"

#include "csutil/csobject.h"
#include "csutil/ref.h"
#include "csutil/scf_implementation.h"
#include "csutil/threadmanager.h"
#include "itexture/iproctex.h"
#include "itexture/itexfact.h"
#include "iengine/texture.h"
#include "igraphic/image.h"
#include "iutil/event.h"
#include "iutil/eventh.h"
#include "ivideo/graph2d.h"


struct iEngine;
struct iGraphics3D;
struct iMaterialWrapper;
struct iObjectRegistry;
struct iTextureManager;
struct iTextureWrapper;

class csProcTexEventHandler;

/**
 * A callback for when a iTextureWrapper is used.
 */
struct iProcTexCallback : public virtual iBase
{
  SCF_INTERFACE(iProcTexCallback, 2,0,0);
  /// Get csProcTexture.
  virtual iProcTexture* GetProcTexture() const = 0;
};

/**
 * Generic superclass for procedural textures. This class
 * takes care of scheduling when a procedural texture needs updating.
 */
class CS_CRYSTALSPACE_EXPORT csProcTexture : 
  public scfImplementationExt2<csProcTexture, csObject, iTextureWrapper,
  iProcTexture>, public ThreadedCallable<csProcTexture>
{
  friend struct csProcTexCallback;
  friend class csProcTexEventHandler;

private:
  // Setup the procedural event handler (used for updating visible
  // proc textures).
  THREADED_CALLABLE_DECL1(csProcTexture, SetupProcEventHandler, csThreadReturn, iObjectRegistry*, object_reg, HIGH, false, false);
  csRef<iEventHandler> proceh;

protected:
  // Will be set to true as soon as pt is initialized.
  bool ptReady;

  // Flags uses for the texture.
  int texFlags;

  // Texture wrapper.
  csRef<iTextureWrapper> tex;
  // Dimensions of texture.
  int mat_w, mat_h;
  csRef<iImage> proc_image;
  csRef<iGraphics3D> g3d;
  csRef<iGraphics2D> g2d;
  iObjectRegistry* object_reg;
  bool anim_prepared;

  bool key_color;
  int key_red, key_green, key_blue;

  // If true (default) then proc texture will register a callback
  // so that the texture is automatically updated (Animate is called)
  // whenever it is used.
  bool use_cb;
  // always animate, even if not visible
  bool always_animate;
  // Are we visible? Can be 'false' if animated w/ 'always animate'.
  bool visible;

  /**
   * Create the actual texture wrapper.
   * The default implementation creates a texture from proc_image if that is
   * valid, or a texture of format "rgb8" otherwise. Override this function
   * to realize different texture formats.
   */
  virtual iTextureWrapper* CreateTexture (iObjectRegistry* object_reg);

  virtual iObject *QueryObject();
  virtual iTextureWrapper *Clone () const;
  virtual void SetImageFile (iImage *Image);
  virtual iImage* GetImageFile ();
  virtual void SetTextureHandle (iTextureHandle *tex);
  virtual iTextureHandle* GetTextureHandle ();
  virtual void GetKeyColor (int &red, int &green, int &blue) const;
  virtual void SetFlags (int flags);
  virtual int GetFlags () const;
  virtual void Register (iTextureManager *txtmng);
  virtual void SetUseCallback (iTextureCallback* callback);
  virtual iTextureCallback* GetUseCallback () const;
  virtual void Visit ();
  virtual bool IsVisitRequired () const;
  virtual void SetKeepImage (bool k);
  virtual bool KeepImage () const;
  virtual void SetTextureClass (const char* className);
  virtual const char* GetTextureClass ();

  virtual bool GetAlwaysAnimate () const;
  virtual void SetAlwaysAnimate (bool enable);
  virtual iTextureFactory* GetFactory();

  virtual void UseTexture ();
public:
  // The current time the previous time the callback was called.
  // This is used to detect if the callback is called multiple times
  // in one frame.
  csTicks last_cur_time;

private:
  static void ProcCallback (iTextureWrapper* txt, void* data);

  /// Parent factory
  csRef<iTextureFactory> parent;

public:
  csProcTexture (iTextureFactory* p = 0, iImage* image = 0);
  virtual ~csProcTexture ();

  iGraphics3D* GetG3D () const { return g3d; }
  iGraphics2D* GetG2D () const { return g2d; }
  iObjectRegistry* GetObjectRegistry () const { return object_reg; }

  /**
   * Disable auto-update. By default csProcTexture will register
   * a callback so that every time the texture is visible Animate
   * will automatically be called. If you don't want this and you want
   * to call Animate on your own then you can disable this feature.
   * You need to call DisableAutoUpdate() before calling Initialize().
   */
  void DisableAutoUpdate () { use_cb = false; }

  /**
   * Do everything needed to initialize this texture.
   * At this stage only will settings like the key color be used.
   * The correct init sequence is:
   * - csProcTexture::Initialize()
   * - iTextureWrapper::Register()
   * - csProcTexture::PrepareAnim()
   *
   * Alternatively you can use Initialize(engine,name) which does all this
   * work for you.
   */
  virtual bool Initialize (iObjectRegistry* object_reg);

  /**
   * Initialize this procedural texture, create a material associated
   * with it, properly register the texture and material and prepare
   * them. This function assumes that the texture manager has already
   * been set up. It is a convenience function that offers
   * less flexibility but is sufficient for most cases. The texture and
   * material will get the name that is given by this routine.
   */
  iMaterialWrapper* Initialize (iObjectRegistry* object_reg, iEngine* engine,
      	iTextureManager* txtmgr, const char* name);

  /**
   * Prepare the animation for use.
   */
  virtual bool PrepareAnim ();

  /// Set the key color to use for this texture.
  void SetKeyColor (int red, int green, int blue)
  {
    key_color = true;
    key_red = red;
    key_green = green;
    key_blue = blue;
    tex->SetKeyColor (red, green, blue);
  }

  /**
   * Animate this texture. Subclasses of csProcTexture must implement
   * this to implement some kind of animation on the procedural texture.
   */
  virtual void Animate (csTicks current_time) = 0;

  /// get dimension
  virtual void GetDimension (int &w, int &h) const
  { w = mat_w; h = mat_h; }

  static int GetRandom (int max)
  {
    return int ((float(max)*rand()/(RAND_MAX+1.0)));
  }

  /// Get the texture corresponding with this procedural texture.
  iTextureWrapper* GetTextureWrapper ()
  { return this; }

};

#endif // __CS_PROCTEX_H__
