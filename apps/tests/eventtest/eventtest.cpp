/*
    Copyright (C) 2006 by Jorrit Tyberghein

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

#include "eventtest.h"

CS_IMPLEMENT_APPLICATION

//---------------------------------------------------------------------------

EventTest::EventTest ()
{
  SetApplicationName ("CrystalSpace.EventTest1");
}

EventTest::~EventTest ()
{
}

bool EventTest::HandleEvent (iEvent &ev)
{
  csRef<iEventNameRegistry> namereg = csEventNameRegistry::GetRegistry (
  	GetObjectRegistry ());
  if (CS_IS_KEYBOARD_EVENT (namereg, ev))
  {
    utf32_char key = csKeyEventHelper::GetRawCode (&ev);
    utf32_char cooked = csKeyEventHelper::GetCookedCode (&ev);
    bool autorep = csKeyEventHelper::GetAutoRepeat (&ev);
    csKeyModifiers key_modifiers;
    csKeyEventHelper::GetModifiers (&ev, key_modifiers);
    uint32 modifiers = csKeyEventHelper::GetModifiersBits (key_modifiers);
    uint32 type = csKeyEventHelper::GetEventType (&ev);
    csString str = csInputDefinition::GetKeyString (namereg, key,
    	&key_modifiers, true);
    printf ("Key %s: raw=%" PRId32 "(%c) "
        "cooked=%" PRId32 "(%c) rep=%d mods=%08" PRIu32 " desc='%s'\n",
    	type == csKeyEventTypeUp ? "UP" : "DO",
	key, (key >= 32 && key < 128) ? (char)key : '-',
	cooked, (cooked >= 32 && cooked < 128) ? (char)cooked : '-',
	autorep, modifiers, str.GetData ());
    fflush (stdout);
  }
  else if (CS_IS_MOUSE_EVENT (namereg, ev))
  {
    uint device = csMouseEventHelper::GetNumber (&ev);
    csMouseEventType type = csMouseEventHelper::GetEventType (&ev);
    csKeyModifiers key_modifiers;
    csKeyEventHelper::GetModifiers (&ev, key_modifiers);
    uint32 modifiers = csMouseEventHelper::GetModifiers (&ev);

    csMouseEventData data;
    csMouseEventHelper::GetEventData (&ev, data);
    int x = csMouseEventHelper::GetX (&ev);
    int y = csMouseEventHelper::GetY (&ev);
    uint but = csMouseEventHelper::GetButton (&ev);
    bool butstate = csMouseEventHelper::GetButtonState (&ev);
    uint32 butmask = csMouseEventHelper::GetButtonMask (&ev);

    csInputDefinition def (namereg, &ev, modifiers, true); //do we want cooked?
    csString str = def.ToString ();
    printf ("Mouse %s: but=%d(state=%d,mask=%08" PRIu32 ") "
        "device=%d x=%d y=%d mods=%08" PRIu32 " desc='%s'\n",
	type == csMouseEventTypeMove ? "MOVE" :
    	type == csMouseEventTypeUp ? "UP" :
	type == csMouseEventTypeDown ? "DO" :
	type == csMouseEventTypeClick ? "CLICK" :
	type == csMouseEventTypeDoubleClick ? "DBL" :
	"?",
	but, butstate, butmask, device, x, y,
	modifiers, str.GetData ());
    fflush (stdout);
  }
  else if (CS_IS_JOYSTICK_EVENT (namereg, ev))
  {
    uint device = csJoystickEventHelper::GetNumber (&ev);
    csKeyModifiers key_modifiers;
    csKeyEventHelper::GetModifiers (&ev, key_modifiers);
    uint32 modifiers = csJoystickEventHelper::GetModifiers (&ev);
    csJoystickEventData data;
    csJoystickEventHelper::GetEventData (&ev, data);
    csInputDefinition def (namereg, &ev, modifiers, true);
    csString str = def.ToString (false);
    csString desc ("");
    if (CS_IS_JOYSTICK_BUTTON_EVENT (namereg, ev, device))
    {
      uint but = csJoystickEventHelper::GetButton (&ev);
      bool butstate = csJoystickEventHelper::GetButtonState (&ev);
      uint32 butmask = csJoystickEventHelper::GetButtonMask (&ev);
      printf ("Joystick %s: device=%d but=%d(state=%d,mask=%08" PRIu32 ") "
          "mods=%08" PRIu32 " desc='%s'\n",
          butstate ? "DO" : "UP", device, but, butstate, butmask,
          modifiers, str.GetData ());
    }
    else if (CS_IS_JOYSTICK_MOVE_EVENT (namereg, ev, device))
    {
      size_t pos = str.Find ("Axis");
      str.SubString (desc, pos + 4, (size_t)-1);
      uint axisnum = atoi(desc.GetData ());
      printf ("Joystick MOVE: device=%d axis=%" PRId32 " value=%d "
          "mods=%08" PRIu32 " desc='%s'\n",
          device, axisnum, data.axes[axisnum], modifiers, str.GetData ());
    }
    fflush(stdout);
  }

  csBaseEventHandler::HandleEvent(ev);
  return false;
}

bool EventTest::OnKeyboard(iEvent& ev)
{
  // We got a keyboard event.
  csKeyEventType eventtype = csKeyEventHelper::GetEventType(&ev);
  if (eventtype == csKeyEventTypeDown)
  {
    // The user pressed a key (as opposed to releasing it).
    utf32_char code = csKeyEventHelper::GetCookedCode(&ev);
    if (code == CSKEY_ESC)
    {
      // The user pressed escape to exit the application.
      // The proper way to quit a Crystal Space application
      // is by broadcasting a csevQuit event. That will cause the
      // main runloop to stop. To do that we get the event queue from
      // the object registry and then post the event.
      csRef<iEventQueue> q = 
        csQueryRegistry<iEventQueue> (GetObjectRegistry());
      if (q.IsValid()) q->GetEventOutlet()->Broadcast(
      	csevQuit(GetObjectRegistry()));
    }
  }
  return false;
}

bool EventTest::OnInitialize(int /*argc*/, char* /*argv*/ [])
{
  // RequestPlugins() will load all plugins we specify. In addition
  // it will also check if there are plugins that need to be loaded
  // from the config system (both the application config and CS or
  // global configs). In addition it also supports specifying plugins
  // on the commandline.
  if (!csInitializer::RequestPlugins(GetObjectRegistry(),
    CS_REQUEST_VFS,
    CS_REQUEST_OPENGL3D,
    CS_REQUEST_FONTSERVER,
    CS_REQUEST_END))
    return ReportError("Failed to initialize plugins!");

  // Attempt to load a joystick plugin.
  csRef<iStringArray> joystickClasses =
    iSCF::SCF->QueryClassList ("crystalspace.device.joystick.");
  if (joystickClasses.IsValid())
  {
    csRef<iPluginManager> plugmgr = 
      csQueryRegistry<iPluginManager> (object_reg);
    for (size_t i = 0; i < joystickClasses->GetSize (); i++)
    {
      const char* className = joystickClasses->Get (i);
      csRef<iBase> b = plugmgr->LoadPlugin (className);

      csReport (object_reg, CS_REPORTER_SEVERITY_NOTIFY,
        "crystalspace.application.joytest", "Attempt to load plugin '%s' %s",
        className, (b != 0) ? "successful" : "failed");
      if (b != 0) b->DecRef ();
    }
  }

  // "Warm up" the event handler so it can interact with the world
  csBaseEventHandler::Initialize(GetObjectRegistry());

  // Now we need to register the event handler for our application.
  // Crystal Space is fully event-driven. Everything (except for this
  // initialization) happens in an event.
  if (!RegisterQueue(GetObjectRegistry(), csevAllEvents(GetObjectRegistry())))
    return ReportError("Failed to set up event handler!");

  return true;
}

void EventTest::OnExit()
{
}

bool EventTest::Application()
{
  // Open the main system. This will open all the previously loaded plug-ins.
  // i.e. all windows will be opened.
  if (!OpenApplication(GetObjectRegistry()))
    return ReportError("Error opening system!");

  Run();

  return true;
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
   * again). EventTest1 does not use that functionality itself, however, it
   * allows you to later use "EventTest.Restart();" and it'll just work.
   */
  return csApplicationRunner<EventTest>::Run (argc, argv);
}
