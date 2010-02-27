/*
  Copyright (C) 2009-10 Christian Van Brussel, Communications and Remote
      Sensing Laboratory of the School of Engineering at the 
      Universite catholique de Louvain, Belgium
      http://www.tele.ucl.ac.be

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Library General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Library General Public License for more details.

  You should have received a copy of the GNU Library General Public
  License along with this library; if not, write to the Free
  Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "avatartest.h"
#include "frankie.h"
#include "krystal.h"

#define MODEL_FRANKIE 1
#define MODEL_KRYSTAL 2

CS_IMPLEMENT_APPLICATION

AvatarTest::AvatarTest ()
  : avatarScene (0)
{
  SetApplicationName ("CrystalSpace.AvatarTest");
}

AvatarTest::~AvatarTest ()
{
  delete avatarScene;
}

void AvatarTest::Frame ()
{
  // First get elapsed time from the virtual clock.
  csTicks elapsed_time = vc->GetElapsedTicks ();

  // Now rotate the camera according to keyboard state
  const float speed = elapsed_time / 1000.0f;

  // Compute camera and animesh position
  iCamera* c = view->GetCamera ();
  csVector3 cameraPosition = c->GetTransform ().GetOrigin ();
  csVector3 avatarPosition = avatarScene->GetCameraTarget ();

  // Move camera
  if (kbd->GetKeyState (CSKEY_SHIFT))
  {
    // If the user is holding down shift, the Up/Down arrow keys will cause
    // the camera to go forwards and backwards (forward only allowed if camera 
    // not too close). Left/Right arrows work also when shift is hold.
    if (kbd->GetKeyState (CSKEY_UP)
	&& (cameraPosition - avatarPosition).Norm () > 0.5f)
      c->Move (CS_VEC_FORWARD * 4 * speed);
    if (kbd->GetKeyState (CSKEY_DOWN))
      c->Move (CS_VEC_BACKWARD * 4 * speed);
    if (kbd->GetKeyState (CSKEY_RIGHT))
      c->Move (CS_VEC_RIGHT * 4 * speed);
    if (kbd->GetKeyState (CSKEY_LEFT))
      c->Move (CS_VEC_LEFT * 4 * speed);
  }
  else
  {
    // Left and right arrows cause the camera to strafe on the X axis; up and 
    // down arrows cause the camera to strafe on the Y axis
    if (kbd->GetKeyState (CSKEY_RIGHT))
      c->Move (CS_VEC_RIGHT * 4 * speed);
    if (kbd->GetKeyState (CSKEY_LEFT))
      c->Move (CS_VEC_LEFT * 4 * speed);

    // Avoid gimbal lock of camera
    cameraPosition.Normalize ();
    float cameraDot = cameraPosition * csVector3 (0.0f, 1.0f, 0.0f);
    if (kbd->GetKeyState (CSKEY_UP)
	&& cameraDot < 0.98f)
      c->Move (CS_VEC_UP * 4 * speed);
    if (kbd->GetKeyState (CSKEY_DOWN)
	&& cameraDot > -0.98f)
      c->Move (CS_VEC_DOWN * 4 * speed);
  }

  // Make the camera look at the animesh
  c->GetTransform ().LookAt (avatarPosition - c->GetTransform ().GetOrigin (),
			     csVector3 (0.0f, 1.0f, 0.0f) );

  // Step the dynamic simulation (we slow down artificially the simulation in
  // order to achieve a 'slow motion' effect)
  if (physicsEnabled)
    dynamics->Step (speed / 4.0f);

  // Update the avatar
  avatarScene->Frame ();

  // Tell 3D driver we're going to display 3D things.
  if (!g3d->BeginDraw (engine->GetBeginDrawFlags () | CSDRAW_3DGRAPHICS))
    return;

  // Tell the camera to render into the frame buffer.
  view->Draw ();

  // Write FPS and other info
  if(!g3d->BeginDraw (CSDRAW_2DGRAPHICS)) return;
  avatarScene->DisplayKeys ();
}

bool AvatarTest::OnKeyboard (iEvent &ev)
{
  csKeyEventType eventtype = csKeyEventHelper::GetEventType(&ev);
  if (eventtype == csKeyEventTypeDown)
  {
    // Check for ESC key
    if (csKeyEventHelper::GetCookedCode (&ev) == CSKEY_ESC)
    {
      csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (GetObjectRegistry ()));
      if (q) q->GetEventOutlet()->Broadcast (csevQuit (GetObjectRegistry ()));
      return true;
    }

    // Check for switching of model
    else if (csKeyEventHelper::GetCookedCode (&ev) == 'm')
    {
      if (avatarModel == MODEL_FRANKIE)
      {
	avatarModel = MODEL_KRYSTAL;
	delete avatarScene;
	avatarScene = new KrystalScene (this);
      }

      else
      {
	avatarModel = MODEL_FRANKIE;
	delete avatarScene;
	avatarScene = new FrankieScene (this);
      }

      if (!avatarScene->CreateAvatar ())
      {
	printf ("Problem loading model. Exiting.\n");
	csRef<iEventQueue> q (csQueryRegistry<iEventQueue> (GetObjectRegistry ()));
	if (q) q->GetEventOutlet()->Broadcast (csevQuit (GetObjectRegistry ()));
	return true;
      }

      return true;
    }
  }

  return avatarScene->OnKeyboard (ev);
}

bool AvatarTest::OnMouseDown (iEvent& ev)
{
  return avatarScene->OnMouseDown (ev);
}

bool AvatarTest::OnInitialize (int /*argc*/, char* /*argv*/ [])
{
  // Check for commandline help.
  if (csCommandLineHelper::CheckHelp (GetObjectRegistry ()))
  {
    csPrintf ("Usage: avatartest\n");
    csPrintf ("Tests on animesh animation\n\n");
    csPrintf ("Options for avatartest:\n");
    csPrintf ("  -model=<name>:     set the starting model (frankie, krystal)\n");
    csPrintf ("  -no_physics:       disable physical animations\n");
    csCommandLineHelper::Help (GetObjectRegistry ());
    return false;
  }

  if (!csInitializer::RequestPlugins (GetObjectRegistry (),
    CS_REQUEST_VFS,
    CS_REQUEST_OPENGL3D,
    CS_REQUEST_ENGINE,
    CS_REQUEST_FONTSERVER,
    CS_REQUEST_IMAGELOADER,
    CS_REQUEST_LEVELLOADER,
    CS_REQUEST_REPORTER,
    CS_REQUEST_REPORTERLISTENER,
    CS_REQUEST_PLUGIN ("crystalspace.mesh.animesh.controllers.lookat",
		       iSkeletonLookAtManager2),
    CS_REQUEST_PLUGIN ("crystalspace.mesh.animesh.controllers.basic",
		       iSkeletonBasicNodesManager2),
    CS_REQUEST_END))
    return ReportError ("Failed to initialize plugins!");

  csBaseEventHandler::Initialize (GetObjectRegistry ());
  if (!RegisterQueue (GetObjectRegistry (), csevAllEvents (GetObjectRegistry ())))
    return ReportError ("Failed to set up event handler!");

  // Check if physics effects are enabled
  csRef<iCommandLineParser> clp =
    csQueryRegistry<iCommandLineParser> (GetObjectRegistry ());
  physicsEnabled = !clp->GetBoolOption ("no_physics", false);

  if (physicsEnabled)
  {
    // Load the Bullet plugin
    csRef<iPluginManager> plugmgr = 
      csQueryRegistry<iPluginManager> (GetObjectRegistry ());
    dynamics = csLoadPlugin<iDynamics> (plugmgr, "crystalspace.dynamics.bullet");

    if (!dynamics)
    {
      ReportWarning
	("Can't load Bullet plugin, continuing with reduced functionalities");
      physicsEnabled = false;
    }
  }

  // Read which model to display at first
  csString modelName = clp->GetOption ("model");
  if (modelName != "krystal")
    avatarModel = MODEL_FRANKIE;
  else
    avatarModel = MODEL_KRYSTAL;

  return true;
}

void AvatarTest::OnExit ()
{
  printer.Invalidate ();
}

bool AvatarTest::Application ()
{
  if (!OpenApplication (GetObjectRegistry ()))
    return ReportError ("Error opening system!");

  g3d = csQueryRegistry<iGraphics3D> (GetObjectRegistry ());
  if (!g3d) return ReportError("Failed to locate 3D renderer!");

  engine = csQueryRegistry<iEngine> (GetObjectRegistry ());
  if (!engine) return ReportError("Failed to locate 3D engine!");

  vc = csQueryRegistry<iVirtualClock> (GetObjectRegistry ());
  if (!vc) return ReportError("Failed to locate Virtual Clock!");

  kbd = csQueryRegistry<iKeyboardDriver> (GetObjectRegistry ());
  if (!kbd) return ReportError("Failed to locate Keyboard Driver!");

  loader = csQueryRegistry<iLoader> (GetObjectRegistry ());
  if (!loader) return ReportError("Failed to locate Loader!");

  g2d = csQueryRegistry<iGraphics2D> (GetObjectRegistry ());
  if (!g2d) return ReportError("Failed to locate 2D renderer!");

  lookAtManager = csQueryRegistry<iSkeletonLookAtManager2> (GetObjectRegistry ());
  if (!lookAtManager) return ReportError("Failed to locate iLookAtManager plugin!");

  basicNodesManager =
    csQueryRegistry<iSkeletonBasicNodesManager2> (GetObjectRegistry ());
  if (!basicNodesManager)
    return ReportError("Failed to locate iSkeletonBasicNodesManager2 plugin!");

  printer.AttachNew (new FramePrinter (GetObjectRegistry ()));

  csRef<iFontServer> fs = g3d->GetDriver2D()->GetFontServer ();
  if (fs)
    courierFont = fs->LoadFont (CSFONT_COURIER);
  else return ReportError ("Failed to locate font server!");

  // Create the dynamic system
  if (physicsEnabled)
  {
    dynamicSystem = dynamics->CreateSystem ();
    if (!dynamicSystem) 
    {
      ReportWarning
	("Can't create dynamic system, continuing with reduced functionalities");
      physicsEnabled = false;
    }

    else
    {
      // Load the ragdoll plugin
      csRef<iPluginManager> plugmgr = 
	csQueryRegistry<iPluginManager> (GetObjectRegistry ());
      ragdollManager = csLoadPlugin<iSkeletonRagdollManager2>
	(plugmgr, "crystalspace.mesh.animesh.controllers.ragdoll");
      if (!ragdollManager)
      {
	ReportWarning
	  ("Can't load ragdoll plugin, continuing with reduced functionalities");
	physicsEnabled = false;
      }
    }
  }

  // Create sector
  room = engine->CreateSector ("room");

  // Initialize camera
  view = csPtr<iView> (new csView (engine, g3d));
  view->GetCamera ()->SetSector (room);
  iGraphics2D* g2d = g3d->GetDriver2D ();
  view->SetRectangle (0, 0, g2d->GetWidth (), g2d->GetHeight ());

  // Create scene
  CreateRoom ();

  // Create avatar
  if (avatarModel == MODEL_KRYSTAL)
    avatarScene = new KrystalScene (this);
  else
    avatarScene = new FrankieScene (this);
  if (!avatarScene->CreateAvatar ())
    return false;

  // Initialize camera position
  view->GetCamera ()->GetTransform ().SetOrigin (avatarScene->GetCameraStart ());

  // Run the application
  Run();

  return true;
}

void AvatarTest::CreateRoom ()
{
  // Creating the background
  // First we make a primitive for our geometry.
  CS::Geometry::DensityTextureMapper bgMapper (0.3f);
  CS::Geometry::TesselatedBox bgBox (csVector3 (-4000), csVector3 (4000));
  bgBox.SetMapper (&bgMapper);
  bgBox.SetFlags (CS::Geometry::Primitives::CS_PRIMBOX_INSIDE);
  
  // Now we make a factory and a mesh at once.
  csRef<iMeshWrapper> background =
    CS::Geometry::GeneralMeshBuilder::CreateFactoryAndMesh (engine, room,
				   "background", "background_factory", &bgBox);

  csRef<iMaterialWrapper> bgMaterial =
    CS::Material::MaterialBuilder::CreateColorMaterial
    (GetObjectRegistry (), "background", csColor (0.398f));
  background->GetMeshObject()->SetMaterialWrapper (bgMaterial);

  // Set up of the physical collider for the roof
  if (physicsEnabled)
    dynamicSystem->AttachColliderPlane (csPlane3 (csVector3 (0.0f, 1.0f, 0.0f), 0.0f),
					10.0f, 0.0f);
  // Creating lights
  csRef<iLight> light;
  iLightList* ll = room->GetLights ();

  // This light is for the background
  // TODO: putting instead the following line creates a black background, otherwise it is grey
  // the behavior doesn't persist if the other lights are removed
  //light = engine->CreateLight(0, csVector3(1, 1, -1), 9000, csColor (1));
  light = engine->CreateLight(0, csVector3(1, 1, 0), 9000, csColor (1));
  light->SetAttenuationMode (CS_ATTN_NONE);
  ll->Add (light);

  // Other lights
  light = engine->CreateLight (0, csVector3 (3, 0, 0), 8, csColor (1));
  light->SetAttenuationMode (CS_ATTN_REALISTIC);
  ll->Add (light);

  light = engine->CreateLight (0, csVector3 (-3, 0,  0), 8, csColor (1));
  light->SetAttenuationMode (CS_ATTN_REALISTIC);
  ll->Add (light);

  light = engine->CreateLight (0, csVector3 (0, 0, -3), 8, csColor (1));
  light->SetAttenuationMode (CS_ATTN_REALISTIC);
  ll->Add (light);

  light = engine->CreateLight (0, csVector3 (0, 0, 3), 8, csColor (1));
  light->SetAttenuationMode (CS_ATTN_REALISTIC);
  ll->Add (light);

  light = engine->CreateLight (0, csVector3 (0, -3, 0), 8, csColor (1));
  light->SetAttenuationMode (CS_ATTN_REALISTIC);
  ll->Add (light);

  engine->Prepare ();

  CS::Lighting::SimpleStaticLighter::ShineLights (room, engine, 4);
}

void AvatarTest::WriteShadow (int x, int y, int fg, const char *str,...)
{
  csString buf;

  va_list arg;
  va_start (arg, str);
  buf.FormatV (str, arg);
  va_end (arg);

  Write (x + 1, y - 1, 0, -1, "%s", buf.GetData());
  Write (x, y, fg, -1, "%s", buf.GetData());
}

void AvatarTest::Write(int x, int y, int fg, int bg, const char *str,...)
{
  va_list arg;
  csString buf;

  va_start (arg,str);
  buf.FormatV (str, arg);
  va_end (arg);

  g2d->Write (courierFont, x, y, fg, bg, buf);
}

//---------------------------------------------------------------------------

int main (int argc, char* argv[])
{
  return AvatarTest ().Main (argc, argv);
}
