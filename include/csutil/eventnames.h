/*
  Crystal Space 3D engine: Event and module naming interface
  (C) 2005 by Adam D. Bradley <artdodge@cs.bu.edu>
  
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

#ifndef __CS_CSUTIL_EVENTNAMES_H__
#define __CS_CSUTIL_EVENTNAMES_H__

#include "cssysdef.h"
#include "csextern.h"
#include "csutil/scf_implementation.h"
#include "iutil/eventnames.h"
#include "iutil/objreg.h"
#include "csutil/hash.h"
#include "csutil/strset.h"
#include "csutil/csstring.h"

/**\file
 * Event naming and name relations
 */
/**
 * \addtogroup event_handling
 * @{ */

#ifndef CSHASHCOMPUTER_EVENTENGINE_IDS
#define CSHASHCOMPUTER_EVENTENGINE_IDS
template<>
class csHashComputer<const csEventID>
{
public:
  static uint ComputeHash (const csEventID eid) 
  {
    return (uint) eid;
  }
};
#endif // CSHASHCOMPUTER_EVENTENGINE_IDS


/**
 * The csEventNameRegistry transforms textual event names (e.g., 
 * "crystalspace.input.joystick.3.button") into easier-to-manage csEventIDs 
 * (which, in non-debug builds, are really just csStringIDs).
 * Also offers easy methods for querying parentage relationships between
 * two event names.
 * 
 * Note that any modules across which event names will be shared need
 * to call csEventNameRegistry::Register() on the same iObjectRegistry.
 */
class CS_CRYSTALSPACE_EXPORT csEventNameRegistry :
  public scfImplementation1<csEventNameRegistry, iEventNameRegistry>
{
 private:
  /**
   * Do not allocate for yourself!  This object uses the singleton pattern
   * and should be retrieved with the static csEventNameRegistry::GetRegistry 
   * method.
   */
  csEventNameRegistry (iObjectRegistry *);
 public:
  ~csEventNameRegistry ();

  /**\name iEventNameRegistry implementation
   * @{ */
  csEventID GetID (const char* name);

  const char * GetString (const csEventID id);
  static const char * GetString (iObjectRegistry *object_reg, 
						 csEventID id);
  csEventID GetParentID (const csEventID id);
  bool IsImmediateChildOf (const csEventID child, 
					   const csEventID parent);
  bool IsKindOf (const csEventID child, 
				 const csEventID parent);
  /** @} */

  /**
   * Return the singleton iEventNameRegistry object registered in the
   * iObjectRegistry (or create one if none yet exists).
   */
  static csRef<iEventNameRegistry> GetRegistry(iObjectRegistry *object_reg);

  static inline csEventID GetID (iEventNameRegistry *name_reg,
				 const char* name) 
  {
    if (name_reg != 0)
      return name_reg->GetID (name);
    else
      return CS_EVENT_INVALID;
  }
  static inline csEventID GetID (iObjectRegistry *object_reg, 
				 const char* name)
  {
    csRef<iEventNameRegistry> nameRegistry = 
      csQueryRegistry<iEventNameRegistry> (object_reg);
    CS_ASSERT (nameRegistry);
    return nameRegistry->GetID (name);
  };

  static inline bool IsKindOf (iEventNameRegistry *name_reg,
			       csEventID name1, csEventID name2) 
  {
    if (name_reg != 0)
      return name_reg->IsKindOf(name1, name2);
    else
      return false;
  }
  static inline bool IsKindOf (iObjectRegistry *object_reg, 
			       csEventID name1, csEventID name2)
  {
    csRef<iEventNameRegistry> nameRegistry =
      csQueryRegistry<iEventNameRegistry> (object_reg);
    CS_ASSERT(nameRegistry);
    return nameRegistry->IsKindOf (name1, name2);
  };

 private:
  iObjectRegistry *object_reg;
  csHash<csEventID,csEventID> parentage;
  csStringSet names;
};


/** \name Common system events
 * These are the names of some of the most commonly-used system-generated
 * events.
 *
 * Some events are "umbrella events", that is, subscribing to them means that
 * all events of a certain kind are received (e.g. csevMouse for mouse events),
 * but the "umbrella events" themselves are <em>not</em> sent to event
 * handlers! In the mouse events examples, you would have to check for the
 * names of "leaf events" in the event handler, such as csevMouseDown,
 * csevMouseMove etc. For information on event names see the 
 * <a href="../manual/Event-Names.html">user manual, section "Event Names"</a>.
 *
 * Third-party applications can define their own event names.  You should
 * avoid using the "crystalspace." prefix (which is reserved for 
 * internally-generated events).
 * @{ */

/**
 * Root event.  All event names are children of this one.
 * WARNING: it's easy to just subscribe to this event, but this will
 * make subscription computationally expensive and may cause lots of
 * spurious events to be delivered to your handler.  Only subscribe to
 * this event if you know what you're doing!
 * \remarks "Umbrella" event.
 */
#define csevAllEvents(reg)	      \
  (csEventNameRegistry::GetID((reg), ""))

/**
 * Frame event.  This is sent once per frame.  This should drive all
 * rendering, periodic/clocked game state updates, input device polling, 
 * etc.
 */
#define csevFrame(reg)		      \
  (csEventNameRegistry::GetID((reg), "crystalspace.frame"))

/**
 * Generic input event.  All actual input events are children of this one.
 * \remarks "Umbrella" event.
 */
#define csevInput(reg)		      \
  (csEventNameRegistry::GetID((reg), "crystalspace.input"))

/**
 * Generic keyboard event.  All actual keyboard events are children of this 
 * one.
 * \remarks "Umbrella" event.
 */
#define csevKeyboardEvent(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.input.keyboard"))

/// Keyboard key down event.
#define csevKeyboardDown(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.input.keyboard.down"))

/// Keyboard key up event.
#define csevKeyboardUp(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.input.keyboard.up"))

/**
 * Generic mouse event.  All actual mouse events are children of this one.
 * \remarks "Umbrella" event.
 */
#define csevMouseEvent(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.input.mouse"))

static inline csEventID csevMouse (
  iEventNameRegistry *name_reg, uint x)
{
  csString name ("crystalspace.input.mouse.");
  name.Append (x);
  return name_reg->GetID(name);
}

static inline csEventID csevMouse(
  iObjectRegistry *object_reg, uint x) 
{
  return csevMouse(csEventNameRegistry::GetRegistry(object_reg), x);
}

static inline csEventID csevMouseOp(
  iEventNameRegistry *name_reg, uint x, const csString &y)
{
  csString name ("crystalspace.input.mouse.");
  name.Append (x);
  name.Append (".");
  name.Append (y);
  return name_reg->GetID(name);
}

static inline csEventID csevMouseOp(
  iObjectRegistry *object_reg, uint x, const csString &y) 
{
  return csevMouseOp(csEventNameRegistry::GetRegistry(object_reg), x, y);
}

/**
 * Generic button event from mouse x.  All button events from mouse x
 * are children of this one.  The default system mouse is 0.
 * \remarks "Umbrella" event.
 */
#define csevMouseButton(reg,x)	      \
  csevMouseOp ((reg), (x), "button")

/**
 * Button down event from mouse x.  The default system mouse is 0.
 */
#define csevMouseDown(reg,x)	      \
  csevMouseOp ((reg), (x), "button.down")

/**
 * Button up event from mouse x.  The default system mouse is 0.
 */
#define csevMouseUp(reg,x)	      \
  csevMouseOp ((reg), (x), "button.up")

/**
 * Click event from mouse x.  The default system mouse is 0.
 */
#define csevMouseClick(reg,x)	      \
  csevMouseOp ((reg), (x), "button.click")

/**
 * Double-click event from mouse x.  The default system mouse is 0.
 */
#define csevMouseDoubleClick(reg,x)   \
  csevMouseOp((reg), (x), "button.doubleclick")

/**
 * Move event from mouse x.  The default system mouse is 0.
 */
#define csevMouseMove(reg,x)	      \
  csevMouseOp((reg), (x), "move")

/**
 * Generic joystick event.  All actual joystick events are children of
 * this one.
 * \remarks "Umbrella" event.
 */
#define csevJoystickEvent(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.input.joystick"))

static inline csEventID csevJoystick (
  iEventNameRegistry *name_reg, uint x) 
{
  char buffer[64];
  cs_snprintf(buffer, sizeof (buffer) - 1, "crystalspace.input.joystick.%d", 
    x);
  return name_reg->GetID(buffer);
}

static inline csEventID csevJoystick (
  iObjectRegistry *object_reg, uint x)
{
  return csevJoystick(csEventNameRegistry::GetRegistry(object_reg), x);
}

static inline csEventID csevJoystickOp (
  iEventNameRegistry *name_reg, uint x, const csString &y) 
{
  csString name ("crystalspace.input.joystick.");
  name.Append (x);
  name.Append (".");
  name.Append (y);
  return name_reg->GetID(name);
}

static inline csEventID csevJoystickOp (
  iObjectRegistry *object_reg, uint x, const csString &y)
{
  return csevJoystickOp (csEventNameRegistry::GetRegistry(object_reg), x, y);
}

/**
 * Generic joystick button event from joystick x.  All actual joystick 
 * button events are children of this one.  The first system joystick
 * is 0.
 * \remarks "Umbrella" event.
 */
#define csevJoystickButton(reg,x)     \
  csevJoystickOp((reg),(x),"button")

/// Button down event from joystick x.  The first system joystick is 0.
#define csevJoystickDown(reg,x)	      \
  csevJoystickOp((reg),(x),"button.down")

/// Button up event from joystick x.  The first system joystick is 0.
#define csevJoystickUp(reg,x)	      \
  csevJoystickOp((reg),(x),"button.up")

/// Move event from joystick x.  The first system joystick is 0.
#define csevJoystickMove(reg,x)	      \
  csevJoystickOp((reg),(x),"move")

/// Check if an event is a keyboard event
#define CS_IS_KEYBOARD_EVENT(reg,e)   \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevKeyboardEvent(reg))

/// Check if an event is a mouse event
#define CS_IS_MOUSE_EVENT(reg,e)      \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevMouseEvent(reg))

/// Check if an event is a button event from mouse n (basis 0)
#define CS_IS_MOUSE_BUTTON_EVENT(reg,e,n) \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevMouseButton((reg),n))

/// Check if an event is a move event from mouse n (basis 0)
#define CS_IS_MOUSE_MOVE_EVENT(reg,e,n)	\
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevMouseMove((reg),n))

/// Check if an event is a joystick event
#define CS_IS_JOYSTICK_EVENT(reg,e)   \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevJoystickEvent(reg))

/// Check if an event is a button event from mouse n (basis 0)
#define CS_IS_JOYSTICK_BUTTON_EVENT(reg,e,n) \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevJoystickButton((reg),n))

/// Check if an event is a move event from mouse n (basis 0)
#define CS_IS_JOYSTICK_MOVE_EVENT(reg,e,n) \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevJoystickMove((reg),n))

/// Check if an event is any input event
#define CS_IS_INPUT_EVENT(reg,e)      \
  csEventNameRegistry::IsKindOf((reg), ((e).Name), csevInput(reg))

/**
 * This event causes system driver to quit the event loop,
 * even if the event loop has been entered multiple times. The
 * quit condition is never reset thus even if you call ::Loop
 * again, the system driver will quit immediately. Such
 * broadcasts are posted for irreversible finalization of the
 * application such as when user closes the application window.
 */
#define csevQuit(reg)		      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.quit"))

/**
 * Children of this event are generated whenever application (as a whole)
 * receives or loses focus.
 * \remarks "Umbrella" event.
 */
#define csevFocusChanged(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.focus"))

/**
 * This event is generated whenever user application (as a whole) receives 
 * system focus.  The application may react by staring music, showing 
 * software mouse cursor and so on.
 */
#define csevFocusGained(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.focus.gained"))

/**
 * This event is generated whenever user application (as a whole) loses 
 * system focus.  Upon this event the application may react by stopping 
 * music, hiding software mouse cursor and so on.
 */
#define csevFocusLost(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.focus.lost"))

/**
 * This event is broadcasted to all event listeners just after all modules
 * have been initialized.  NOTE - it is not (usually) a good idea to load
 * other modules in the csevSystemOpen event handler, particularly if the
 * so-loaded module will itself depend on receiving csevSystemOpen.  (Its
 * its subscription ordering criteria may render it unloadable once 
 * delivery of this event has begun.)  Instead, modules which will need
 * to receive csevSystemOpen should be loaded inside of other modules' 
 * ::Initialize() methods.
 */
#define csevSystemOpen(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.open"))

/**
 * This event is broadcasted to all event listeners just before modules are
 * about to be shutdown and unloaded (that is, the system is shutting down).
 */
#define csevSystemClose(reg)	      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.close"))

struct iGraphics2D;

CS_CRYSTALSPACE_EXPORT
csEventID csevCanvasOp (csRef<iEventNameRegistry>& reg, 
					const iGraphics2D* g2d, 
					const csString &y);
static inline csEventID csevCanvasOp (
  iObjectRegistry *object_reg, const iGraphics2D* g2d, const csString &y)
{
  csRef<iEventNameRegistry> name_reg = csEventNameRegistry::GetRegistry (object_reg);
  return csevCanvasOp(name_reg, g2d, y);
}


/**
 * This event is generated when user resizes the application window
 * corresponding with context x (where x is an iGraphics2D pointer).
 * <pre>
 * Info: (iGraphics2D *) The context that has been resized
 * </pre>
 */
#define csevCanvasResize(reg, g2d)      \
  csevCanvasOp((reg), (g2d), "resize")

/**
 * This event is sent when a graphics context x is being destroyed
 * (where x is an iGraphics2D pointer).
 * Usually this event is accompanyed by a shutdown
 * but there are exceptions, e.g., when a dynamic texture is closed
 * (a dynamic texture is a graphics context as well).
 * <pre>
 * Info: (iGraphics2D *) The context that has been closed
 * </pre>
 */
#define csevCanvasClose(reg, g2d)	      \
  csevCanvasOp((reg), (g2d), "close")

/**
 * Broadcast indicating that the canvas x is not currently
 * visible to the user, such as being iconified
 * (where x is an iGraphics2D pointer).
 */
#define csevCanvasHidden(reg, g2d)      \
  csevCanvasOp((reg), (g2d), "hidden")

/**
 * Broadcast indicating that the display canvas has just become
 * visible, such as being uniconified (where x is an iGraphics2D pointer).
 */
#define csevCanvasExposed(reg, g2d)     \
  csevCanvasOp((reg), (g2d), "exposed")

/**
 * This event is broadcasted when system driver displays the
 * help text for all supported command-line switches. Upon reception
 * of such event every plugin should display a short help for any
 * of the command-line switches it supports. The general format is:
 * <pre>&lt;2 spaces&gt;&lt;switch - 18 positions&gt;&lt;space&gt;</pre>
 * <pre>&lt;switch description&gt;{default value}</pre>
 * The help should be displayed to standard output.
 */
#define csevCommandLineHelp(reg)      \
  (csEventNameRegistry::GetID((reg), "crystalspace.application.commandlinehelp"))

/** @} */

#define CS_DECLARE_SYSTEM_EVENT_SHORTCUTS			\
  csEventID SystemOpen;						\
  csEventID SystemClose

#define CS_DECLARE_FRAME_EVENT_SHORTCUTS			\
  csEventID Frame

#define CS_DECLARE_INPUT_EVENT_SHORTCUTS			\
  csEventID KeyboardEvent;					\
  csEventID MouseEvent;						\
  csEventID JoystickEvent

/**
 * Shortcut to declare class properties SystemOpen, SystemClose,
 * Frame.
 * Pair with CS_INITIALIZE_EVENT_SHORTCUTS(registry).
 */
#define CS_DECLARE_EVENT_SHORTCUTS				\
  CS_DECLARE_SYSTEM_EVENT_SHORTCUTS;				\
  CS_DECLARE_FRAME_EVENT_SHORTCUTS;				\
  CS_DECLARE_INPUT_EVENT_SHORTCUTS

#define CS_INITIALIZE_SYSTEM_EVENT_SHORTCUTS(object_reg) do {	\
    SystemOpen = csevSystemOpen ((object_reg));			\
    SystemClose = csevSystemClose ((object_reg));		\
  } while (0)

#define CS_INITIALIZE_FRAME_EVENT_SHORTCUTS(object_reg) do {	\
    Frame = csevFrame ((object_reg));				\
  } while (0)

#define CS_INITIALIZE_INPUT_EVENT_SHORTCUTS(object_reg) do {	\
    KeyboardEvent = csevKeyboardEvent ((object_reg));		\
    MouseEvent = csevMouseEvent ((object_reg));			\
    JoystickEvent = csevJoystickEvent ((object_reg));		\
  } while (0)

/**
 * Shortcut to initialize the properties declared by
 * CS_DECLARE_EVENT_SHORTCUTS.
 * Requires an iObjectRegistry or iEventNameRegistry.
 */
#define CS_INITIALIZE_EVENT_SHORTCUTS(object_reg) do {	\
    CS_INITIALIZE_SYSTEM_EVENT_SHORTCUTS (object_reg);	\
    CS_INITIALIZE_FRAME_EVENT_SHORTCUTS (object_reg);	\
    CS_INITIALIZE_INPUT_EVENT_SHORTCUTS (object_reg);	\
  } while (0)

/** @} */

#endif // __CS_CSUTIL_EVENTNAMES_H__
