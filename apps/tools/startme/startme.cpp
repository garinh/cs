/*
    Copyright (C) 2005 by Jorrit Tyberghein

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

#include "startme.h"

CS_IMPLEMENT_APPLICATION

//---------------------------------------------------------------------------

StartMe::StartMe ()
{
  SetApplicationName ("CrystalSpace.StartMe");
  last_selected = (size_t)-1;
  description_selected = (size_t)-1;
}

StartMe::~StartMe ()
{
}

void StartMe::Frame ()
{
  // First get elapsed time from the virtual clock.
  csTicks elapsed_time = vc->GetElapsedTicks ();
  if (elapsed_time > 200) elapsed_time = 200;
  // Now rotate the camera according to keyboard state
  float elapsed_seconds = float (elapsed_time) / 1000.0f;

  int mouse_x = mouse->GetLastX ();
  int mouse_y = mouse->GetLastY ();
  iCamera* camera = view->GetCamera ();
  csVector2 p (mouse_x, camera->GetShiftY() * 2 - mouse_y);

  csVector3 light_v, star_v;

  star_v = camera->InvPerspective (p, DEMO_MESH_Z-5);
  star_ticks += elapsed_time;
  while (star_ticks > star_timeout)
  {
    star_ticks -= star_timeout;
    star_v.x += ((float)rand() / (float)RAND_MAX - 0.5f) / 4.0f;
    star_v.y += ((float)rand() / (float)RAND_MAX - 0.5f) / 4.0f;
    star_v.z += ((float)rand() / (float)RAND_MAX - 0.5f) / 4.0f;

    size_t max = star_count;
    while (stars[cur_star].inqueue)
    {
      cur_star++;
      if (cur_star >= star_count) cur_star = 0;
      max--;
      if (max <= 0) break;
    }
    if (max <= 0) { printf ("MAX!\n"); fflush (stdout); break; }
    StarInfo& si = stars[cur_star];
    si.star->GetMovable ()->GetTransform ().SetOrigin (star_v);
    si.star->GetMovable ()->UpdateMove ();
    si.r = 0;
    si.stars_mesh->SetColor (csColor (0, 0, 0));
    si.star->GetFlags ().Reset (CS_ENTITY_INVISIBLE);
    si.inqueue = true;
    star_queue.Push (int (cur_star));
    cur_star++;
    if (cur_star >= star_count) cur_star = 0;
  }

  float dr = elapsed_seconds / star_maxage;
  size_t j = star_queue.GetSize ();
  while (j > 0)
  {
    j--;
    int star_idx = star_queue[j];
    StarInfo& si = stars[star_idx];
    si.r += dr;
    if (si.r >= 1)
    {
      si.star->GetFlags ().Set (CS_ENTITY_INVISIBLE);
      si.inqueue = false;
      star_queue.DeleteIndex (j);
    }
    else
    {
      float f = 1.0f;
      if (si.r < star_fade1)
      {
        f = 1.0f - (star_fade1-si.r) / star_fade1;
      }
      else if (si.r >= star_fade2)
      {
        f = 1.0f - (si.r - star_fade2) / (1.0f - star_fade2);
      }
      si.stars_mesh->SetColor (csColor (f+.2, f, f));
    }
  }

  light_v = camera->InvPerspective (p, DEMO_MESH_Z-3);
  pointer_light->SetCenter (light_v);

  csVector3 start_v, end_v;
  start_v = camera->InvPerspective (p, DEMO_MESH_Z-4);
  end_v = camera->InvPerspective (p, 100.0f);
  csVector3 start = camera->GetTransform ().This2Other (start_v);
  csVector3 end = camera->GetTransform ().This2Other (end_v);

  iSector* sector = camera->GetSector ();
  csVector3 isect;
  csIntersectingTriangle closest_tri;
  iMeshWrapper* sel_mesh;
  float sqdist = 1.0f;

  if (InDescriptionMode ())
    sel_mesh = demos[description_selected].mesh;
  else
    sqdist = csColliderHelper::TraceBeam (cdsys, sector,
	start, end, true,
	closest_tri, isect, &sel_mesh);

  size_t i, sel = (size_t)-1;
  if (sqdist >= 0 && sel_mesh)
  {
    const char* name = sel_mesh->QueryObject ()->GetName ();
    for (i = 0 ; i < demos.GetSize () ; i++)
      if (!strcmp (demos[i].name, name))
      {
        demos[i].spinning_speed += elapsed_seconds / 80.0f;
	if (demos[i].spinning_speed > 0.05f) demos[i].spinning_speed = 0.05f;
	sel = i;
        break;
      }
  }
  last_selected = sel;

  for (i = 0 ; i < demos.GetSize () ; i++)
  {
    if (sel != i)
    {
      if (demos[i].spinning_speed > 0)
      {
        demos[i].spinning_speed -= elapsed_seconds / 80.0f;
	if (demos[i].spinning_speed < 0)
	  demos[i].spinning_speed = 0;
      }
    }
    csYRotMatrix3 rot (demos[i].spinning_speed);
    demos[i].mesh->GetMovable ()->Transform (rot);
    demos[i].mesh->GetMovable ()->UpdateMove ();
  }

  // Tell 3D driver we're going to display 3D things.
  if (!g3d->BeginDraw (engine->GetBeginDrawFlags () | CSDRAW_3DGRAPHICS |
  	CSDRAW_CLEARSCREEN | CSDRAW_CLEARZBUFFER))
    return;

  // Tell the camera to render into the frame buffer.
  view->Draw ();

  if (InDescriptionMode ())
  {
    g3d->BeginDraw (CSDRAW_2DGRAPHICS);
    iGraphics2D* g2d = g3d->GetDriver2D ();
    csString desc = demos[description_selected].description->GetData ();
    size_t idx = desc.FindFirst ('#');
    int y = 50;
    int fw, fh;
    font->GetMaxSize (fw, fh);
    while (idx != (size_t)-1)
    {
      csString start, remainder;
      desc.SubString (start, 0, idx);
      desc.SubString (remainder, idx+1);
      g2d->Write (font, 30, y, font_fg, font_bg, start);
      y += fh + 5;
      desc = remainder;
      idx = desc.FindFirst ('#');
    }
    g2d->Write (font, 30, y, font_fg, font_bg, desc);
  }
}

void StartMe::EnterDescriptionMode ()
{
  description_selected = last_selected;
  main_light->SetColor (MAIN_LIGHT_OFF);
  pointer_light->SetColor (POINTER_LIGHT_OFF);
}

void StartMe::LeaveDescriptionMode ()
{
  description_selected = (size_t)-1;
  main_light->SetColor (MAIN_LIGHT_ON);
  pointer_light->SetColor (POINTER_LIGHT_ON);
}

bool StartMe::OnKeyboard(iEvent& ev)
{
  // We got a keyboard event.
  csKeyEventType eventtype = csKeyEventHelper::GetEventType(&ev);
  if (eventtype == csKeyEventTypeDown)
  {
    // The user pressed a key (as opposed to releasing it).
    utf32_char code = csKeyEventHelper::GetCookedCode(&ev);
    if (code == CSKEY_ESC)
    {
      if (InDescriptionMode ())
        LeaveDescriptionMode ();
      else
      {
        // The user pressed escape to exit the application.
        // The proper way to quit a Crystal Space application
        // is by broadcasting a csevQuit event. That will cause the
        // main runloop to stop. To do that we get the event queue from
        // the object registry and then post the event.
        csRef<iEventQueue> q = 
          csQueryRegistry<iEventQueue> (GetObjectRegistry());
        if (q.IsValid()) 
	  q->GetEventOutlet()->Broadcast(csevQuit(GetObjectRegistry()));
      }
    }
  }
  return false;
}

bool StartMe::OnMouseDown (iEvent& /*event*/)
{
  if (InDescriptionMode ())
  {
    csRef<iCommandLineParser> cmdline =
        csQueryRegistry<iCommandLineParser> (GetObjectRegistry());
    csString appdir = cmdline->GetAppDir ();
    system (csString("\"") << appdir << CS_PATH_SEPARATOR <<
        csInstallationPathsHelper::GetAppFilename (
            demos[description_selected].exec) << "\" " << 
        demos[description_selected].args);

    LeaveDescriptionMode ();
    return true;
  }

  if (last_selected != (size_t)-1)
  {
    EnterDescriptionMode ();
  }
  return true;
}

bool StartMe::LoadTextures ()
{
  if (!loader->LoadTexture ("spark", "/lib/std/spark.png"))
    return ReportError ("Error loading '%s' texture!", "spark");

  vfs->ChDir ("/lib/startme");
  size_t i;
  for (i = 0 ; i < demos.GetSize () ; i++)
  {
    if (!loader->LoadTexture (demos[i].name, demos[i].image))
      return ReportError ("Error loading '%s' texture!", demos[i].image);
  }

  return true;
}

bool StartMe::OnInitialize(int /*argc*/, char* /*argv*/ [])
{
  if (!csInitializer::SetupConfigManager (GetObjectRegistry (),
  	"/config/startme.cfg"))
    return ReportError ("Error reading config file 'startme.cfg'!");

  // RequestPlugins() will load all plugins we specify. In addition
  // it will also check if there are plugins that need to be loaded
  // from the config system (both the application config and CS or
  // global configs). In addition it also supports specifying plugins
  // on the commandline.
  if (!csInitializer::RequestPlugins(GetObjectRegistry(),
      CS_REQUEST_VFS,
      CS_REQUEST_OPENGL3D,
      CS_REQUEST_ENGINE,
      CS_REQUEST_FONTSERVER,
      CS_REQUEST_IMAGELOADER,
      CS_REQUEST_LEVELLOADER,
      CS_REQUEST_REPORTER,
      CS_REQUEST_REPORTERLISTENER,
      CS_REQUEST_PLUGIN("crystalspace.collisiondetection.opcode",
		iCollideSystem),
      CS_REQUEST_END))
    return ReportError ("Failed to initialize plugins!");

  csBaseEventHandler::Initialize(GetObjectRegistry());

  // Now we need to setup an event handler for our application.
  // Crystal Space is fully event-driven. Everything (except for this
  // initialization) happens in an event.
  if (!RegisterQueue (GetObjectRegistry(), csevAllEvents(GetObjectRegistry())))
    return ReportError ("Failed to set up event handler!");

  return true;
}

void StartMe::OnExit()
{
  printer.Invalidate ();
}

bool StartMe::Application()
{
  // Open the main system. This will open all the previously loaded plug-ins.
  // i.e. all windows will be opened.
  if (!OpenApplication(GetObjectRegistry()))
    return ReportError("Error opening system!");

  // Now get the pointer to various modules we need. We fetch them
  // from the object registry. The RequestPlugins() call we did earlier
  // registered all loaded plugins with the object registry.
  g3d = csQueryRegistry<iGraphics3D> (GetObjectRegistry());
  if (!g3d) return ReportError("Failed to locate 3D renderer!");

  engine = csQueryRegistry<iEngine> (GetObjectRegistry());
  if (!engine) return ReportError("Failed to locate 3D engine!");

  vc = csQueryRegistry<iVirtualClock> (GetObjectRegistry());
  if (!vc) return ReportError("Failed to locate Virtual Clock!");

  kbd = csQueryRegistry<iKeyboardDriver> (GetObjectRegistry());
  if (!kbd) return ReportError("Failed to locate Keyboard Driver!");

  mouse = csQueryRegistry<iMouseDriver> (GetObjectRegistry());
  if (!mouse) return ReportError("Failed to locate Mouse Driver!");

  cdsys = csQueryRegistry<iCollideSystem> (GetObjectRegistry());
  if (!cdsys) return ReportError("Failed to locate CollDet System!");

  loader = csQueryRegistry<iLoader> (GetObjectRegistry());
  if (!loader) return ReportError("Failed to locate Loader!");

  vfs = csQueryRegistry<iVFS> (GetObjectRegistry());
  if (!vfs) return ReportError("Failed to locate VFS!");

  confman = csQueryRegistry<iConfigManager> (GetObjectRegistry());
  if (!confman) return ReportError("Failed to locate Config Manager!");

  // We need a View to the virtual world.
  view.AttachNew(new csView (engine, g3d));
  iGraphics2D* g2d = g3d->GetDriver2D ();
  // We use the full window to draw the world.
  view->SetRectangle (0, 0, g2d->GetWidth (), g2d->GetHeight ());

  LoadConfig ();

  // Load textures.
  if (!LoadTextures ())
    return false;

  // Here we create our world.
  CreateRoom ();

  // Let the engine prepare all lightmaps for use and also free all images 
  // that were loaded for the texture manager.
  engine->Prepare ();

  // Now we need to position the camera in our world.
  view->GetCamera ()->SetSector (room);
  view->GetCamera ()->GetTransform ().SetOrigin (csVector3 (0, 0, 0));
  view->GetCamera ()->SetViewportSize (g2d->GetWidth(), g2d->GetHeight ());

  printer.AttachNew (new FramePrinter (object_reg));

  // Get our font.
  font = g2d->GetFontServer ()->LoadFont (CSFONT_LARGE);
  font_fg = g2d->FindRGB (255, 255, 255);
  font_bg = -1;

  // This calls the default runloop. This will basically just keep
  // broadcasting process events to keep the game going.
  Run();

  return true;
}

csPtr<iMeshWrapper> StartMe::CreateDemoMesh (const char* name,
	const csVector3& pos)
{
  csRef<iMeshWrapper> m;
  m = engine->CreateMeshWrapper (box_fact, name, room, pos);
  m->SetRenderPriority (engine->GetWallRenderPriority ());
  iMaterialWrapper* mat = engine->FindMaterial (name);
  m->GetMeshObject ()->SetMaterialWrapper (mat);
  return (csPtr<iMeshWrapper>)m;
}

void StartMe::CreateRoom ()
{
  // We create a new sector called "room".
  room = engine->CreateSector ("room");

  iMaterialWrapper* spark_mat = engine->FindMaterial ("spark");
  csRef<iMeshFactoryWrapper> spark_fact = engine->CreateMeshFactory (
  	"crystalspace.mesh.object.genmesh", "spark_fact");
  csRef<iGeneralFactoryState> spark_state = 
    scfQueryInterface<iGeneralFactoryState> (
      spark_fact->GetMeshObjectFactory ());
  spark_state->SetVertexCount (4);
  spark_state->GetVertices ()[0].Set (-.1f, -.1f, 0);
  spark_state->GetVertices ()[1].Set (.1f, -.1f, 0);
  spark_state->GetVertices ()[2].Set (.1f, .1f, 0);
  spark_state->GetVertices ()[3].Set (-.1f, .1f, 0);
  spark_state->GetTexels ()[0].Set (0, 0);
  spark_state->GetTexels ()[1].Set (1, 0);
  spark_state->GetTexels ()[2].Set (1, 1);
  spark_state->GetTexels ()[3].Set (0, 1);
  spark_state->GetNormals ()[0].Set (0, 0, 1);
  spark_state->GetNormals ()[1].Set (0, 0, 1);
  spark_state->GetNormals ()[2].Set (0, 0, 1);
  spark_state->GetNormals ()[3].Set (0, 0, 1);
  spark_state->SetTriangleCount (2);
  spark_state->GetTriangles ()[0].Set (2, 1, 0);
  spark_state->GetTriangles ()[1].Set (3, 2, 0);
  spark_state->SetLighting (false);
  spark_fact->GetMeshObjectFactory ()->SetMixMode (CS_FX_ADD);
  spark_state->SetColor (csColor (1, 1, 1));
  spark_fact->GetMeshObjectFactory ()->SetMaterialWrapper (spark_mat);
  size_t i;
  for (i = 0 ; i < star_count ; i++)
  {
    StarInfo starinfo;
    stars.Push (starinfo);
    stars[i].star = engine->CreateMeshWrapper (spark_fact, "star", room);
    stars[i].star->GetFlags ().Set (CS_ENTITY_INVISIBLE);
    stars[i].star->SetRenderPriority (engine->GetObjectRenderPriority ());
    stars[i].star->SetZBufMode (CS_ZBUF_NONE);
    stars[i].stars_mesh = stars[i].star->GetMeshObject ();
  }
  cur_star = 0;
  star_ticks = 0;

  box_fact = engine->CreateMeshFactory (
  	"crystalspace.mesh.object.genmesh", "box_fact");
  csRef<iGeneralFactoryState> box_state = 
    scfQueryInterface<iGeneralFactoryState> (
      box_fact->GetMeshObjectFactory ());
  csBox3 b (-1, -1, -1, 1, 1, 1);
  box_state->GenerateBox (b);
  box_state->CalculateNormals ();

  int cols = 4;
  int rows = int (demos.GetSize ()-1) / cols + 1;
  float dx = (DEMO_MESH_MAXX-DEMO_MESH_MINX) / float (cols-1);
  float dy = (DEMO_MESH_MAXY-DEMO_MESH_MINY) / float (rows-1);
  int x = 0, y = rows-1;
  for (i = 0 ; i < demos.GetSize () ; i++)
  {
    demos[i].mesh = CreateDemoMesh (demos[i].name,
      	csVector3 (DEMO_MESH_MINX + dx * float (x),
		   DEMO_MESH_MINY + dy * float (y),
		   DEMO_MESH_Z));
    x++;
    if (x >= cols) { y--; x = 0; }
    demos[i].spinning_speed = 0;
  }

  // Now we need light to see something.
  iLightList* ll = room->GetLights ();

  main_light = engine->CreateLight(0, csVector3(0, 0, -5), 100,
  	MAIN_LIGHT_ON, CS_LIGHT_DYNAMICTYPE_DYNAMIC);
  ll->Add (main_light);

  pointer_light = engine->CreateLight(0, csVector3(0, 0, DEMO_MESH_Z-3), 5,
  	POINTER_LIGHT_ON, CS_LIGHT_DYNAMICTYPE_DYNAMIC);
  ll->Add (pointer_light);

  csColliderHelper::InitializeCollisionWrappers (cdsys, engine, 0);
}

void StartMe::LoadConfig ()
{
  // Retrieve star cursor informations.
  star_count = confman->GetInt ("Stars.Count", 100);
  star_timeout = confman->GetInt ("Stars.Timeout", 10);
  star_maxage = confman->GetFloat ("Stars.MaxAge", 0.5f);
  star_fade1 = confman->GetFloat ("Stars.Fade1", 0.2f);
  star_fade2 = confman->GetFloat ("Stars.Fade2", 0.4f);
  
  // Retrieve demo programs informations.
  size_t i = 0;
  csString pattern;
  while (confman->SubsectionExists (pattern.Format ("StartMe.%zu.", i)))
  {
    DemoData demo;
    demo.description = new scfString ();
    csRef<iConfigIterator> iterator (confman->Enumerate (pattern.GetData()));
    while (iterator->HasNext ())
    {
      iterator->Next();
      csString key (iterator->GetKey ());
      csString leaf;
      key.SubString (leaf,
          key.FindLast ('.', key.Length ()) + 1,
          key.Length ());
      if (!strcmp(leaf.GetData (), "name"))
        demo.name = iterator->GetStr ();
      else if (!strcmp(leaf.GetData (), "exec"))
        demo.exec = iterator->GetStr ();
      else if (!strcmp(leaf.GetData (), "args"))
        demo.args = iterator->GetStr ();
      else if (!strcmp(leaf.GetData (), "image"))
        demo.image = iterator->GetStr ();
      else
      {
        demo.description->Append (iterator->GetStr ());
        demo.description->Append ("#");
      }
    }
    demos.Push (demo);
    i++;
  }
}

/*-------------------------------------------------------------------------*
 * Main function
 *-------------------------------------------------------------------------*/
int main (int argc, char* argv[])
{
  /* Runs the application. 
   *
   * csApplicationRunner<> is a small wrapper to support "restartable" 
   * applications (ie where CS needs to be completely shut down and loaded 
   * again). StartMe1 does not use that functionality itself, however, it
   * allows you to later use "StartMe.Restart();" and it'll just work.
   */
  return csApplicationRunner<StartMe>::Run (argc, argv);
}
