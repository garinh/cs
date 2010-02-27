/*
    Copyright (C) 1998-2001 by Jorrit Tyberghein

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

#ifndef __CS_INITAPP_H__
#define __CS_INITAPP_H__

/**\file
 * Application initialization helper class
 */
 
/**
 * \addtogroup appframe
 * @{ */

#include "csextern.h"

#include "csutil/array.h"
#include "csutil/eventnames.h"
#include "csutil/csstring.h"
#include "csutil/scf.h"
#include "iengine/engine.h"
#include "igraphic/imageio.h"
#include "imap/loader.h"
#include "imap/saver.h"
#include "iutil/evdefs.h"
#include "iutil/vfs.h"
#include "iutil/objreg.h"
#include "iutil/plugin.h"
#include "ivaria/conout.h"
#include "ivaria/reporter.h"
#include "ivaria/stdrep.h"
#include "ivideo/graph3d.h"
#include "ivideo/fontserv.h"

struct iCommandLineParser;
struct iConfigManager;
struct iEvent;
struct iEventHandler;
struct iEventQueue;
struct iObjectRegistry;
struct iPluginManager;
struct iThreadManager;
struct iVirtualClock;
struct iSystemOpenManager;
struct iVerbosityManager;

/**\name Plugin request macros
 * Utility macros to select what plugins you want to have loaded.
 * @{ */
/// Request a plugin.
#define CS_REQUEST_PLUGIN(Name,Interface)                                   \
  Name, scfInterfaceTraits<Interface>::GetName(),                           \
  scfInterfaceTraits<Interface>::GetID(),                                   \
  scfInterfaceTraits<Interface>::GetVersion()
/// Request a plugin, but with a custom tag.
#define CS_REQUEST_PLUGIN_TAG(Name,Interface,Tag)                           \
  Name ":" Tag , scfInterfaceTraits<Interface>::GetName(),                  \
  scfInterfaceTraits<Interface>::GetID(),                                   \
  scfInterfaceTraits<Interface>::GetVersion()

// !!! NOTE !!!
// When editing this list, you *must* ensure that initapp.h #include the
// appropriate header for each SCF interface mentioned by a CS_REQUEST_PLUGIN()
// invocation. This is necessary to guarantee that the interface-specialized
// version of scfInterfaceTraits<> is seen by clients rather than the non-specialized
// template.

/// Marker for the end of the requested plugins list.
#define CS_REQUEST_END \
  (const char*)0
/// Request VFS plugin.
#define CS_REQUEST_VFS \
  CS_REQUEST_PLUGIN("crystalspace.kernel.vfs", iVFS)
/// Request default font server.
#define CS_REQUEST_FONTSERVER \
  CS_REQUEST_PLUGIN("crystalspace.font.server.multiplexer", iFontServer)
/// Request default image loader.
#define CS_REQUEST_IMAGELOADER \
  CS_REQUEST_PLUGIN("crystalspace.graphic.image.io.multiplexer", iImageIO)
/// Request null 3D renderer
#define CS_REQUEST_NULL3D \
  CS_REQUEST_PLUGIN("crystalspace.graphics3d.null",iGraphics3D)
/// Request software 3D renderer.
#define CS_REQUEST_SOFTWARE3D \
  CS_REQUEST_PLUGIN("crystalspace.graphics3d.software",iGraphics3D)
/// Request OpenGL 3D renderer
#define CS_REQUEST_OPENGL3D \
  CS_REQUEST_PLUGIN("crystalspace.graphics3d.opengl", iGraphics3D)
/// Request 3D engine.
#define CS_REQUEST_ENGINE \
  CS_REQUEST_PLUGIN("crystalspace.engine.3d", iEngine)
/// Request map loader.
#define CS_REQUEST_LEVELLOADER \
  CS_REQUEST_PLUGIN("crystalspace.level.threadedloader", iThreadedLoader), \
  CS_REQUEST_PLUGIN("crystalspace.level.loader", iLoader)
/// Request map writer.
#define CS_REQUEST_LEVELSAVER \
  CS_REQUEST_PLUGIN("crystalspace.level.saver", iSaver)
/// Request reporter.
#define CS_REQUEST_REPORTER \
  CS_REQUEST_PLUGIN("crystalspace.utilities.reporter", iReporter)
/// Request default reporter listener.
#define CS_REQUEST_REPORTERLISTENER \
  CS_REQUEST_PLUGIN("crystalspace.utilities.stdrep", iStandardReporterListener)
/// Request standard console output.
#define CS_REQUEST_CONSOLEOUT \
  CS_REQUEST_PLUGIN("crystalspace.console.output.standard", iConsoleOutput)
/// Request joystick plugin
#define CS_REQUEST_JOYSTICK \
  CS_REQUEST_PLUGIN("crystalspace.device.joystick", iEventPlug)
/** @} */

/**
 * Function to handle events for apps.
 */
typedef bool (*csEventHandlerFunc) (iEvent&);

/**
 * This class represents a single plugin request for
 * csInitializer::RequestPlugins().  As a shortcut, rather than constructing a
 * csPluginRequest with individual arguments, you can use CS_REQUEST_PLUGIN()
 * or one of its derivatives, such as CS_REQUEST_VFS.  For example:
 * \code
 * csPluginRequest r1(CS_REQUEST_VFS);
 * csPluginRequest r2(CS_REQUEST_PLUGIN("myproj.foobar",iFoobar));
 * \endcode
 */
class CS_CRYSTALSPACE_EXPORT csPluginRequest
{
private:
  csString class_name;
  csString interface_name;
  scfInterfaceID interface_id;
  int interface_version;
  void set(csPluginRequest const&);
public:
  csPluginRequest(csString class_name, csString interface_name,
    scfInterfaceID interface_id, int interface_version);
  csPluginRequest(csPluginRequest const& r) { set(r); }
  csPluginRequest& operator=(csPluginRequest const& r) {set(r); return *this;}
  bool operator==(csPluginRequest const&) const;
  bool operator!=(csPluginRequest const& r) const { return !operator==(r); }
  csString GetClassName() const { return class_name; }
  csString GetInterfaceName() const { return interface_name; }
  scfInterfaceID GetInterfaceID() const { return interface_id; }
  int GetInterfaceVersion() const { return interface_version; }
};


/**
 * This class contains several static member functions that can help
 * setup an application. It is possible to do all the setup on your own
 * but using the functions below will help considerably.
 */
class CS_CRYSTALSPACE_EXPORT csInitializer
{
public:
  /**
   * Create everything needed to get a CS application operational.
   * This function is completely equivalent to calling:
   * - #CS_INITIALIZE_PLATFORM_APPLICATION macro
   * - InitializeSCF()
   * - CreateObjectRegistry()
   * - CreatePluginManager()
   * - CreateEventQueue()
   * - CreateVirtualClock()
   * - CreateCommandLineParser()
   * - CreateVerbosityManager()
   * - CreateConfigManager()
   * - CreateThreadManager()
   * - CreateInputDrivers()
   * - CreateStringSet()
   * - csPlatformStartup()
   *
   * You may want to call all or some of those manually if you don't wish
   * to use this function. It is suggested that 
   * #CS_INITIALIZE_PLATFORM_APPLICATION, InitializeSCF(),
   * and csPlatformStartup() are always invoked; 
   * the other calls can be replaced 
   * with manual creation of the respective objects. However, the order above
   * should be retained when doing so.
   * \param argc argc argument from main().
   * \param argv argv argument from main().
   * \param scanDefaultPluginPaths Whether the default plugin paths are scanned.
   *   Forwarded to InitializeSCF(), see there for more information.
   * \return This function will return the pointer to the object registry where
   * all the created objects will be registered.
   */
  static iObjectRegistry* CreateEnvironment(int argc, char const* const argv[],
    bool scanDefaultPluginPaths = true);

  /**
   * This very important function initializes the SCF sub-system.
   * Without this you can do almost nothing in CS.
   * \param argc argc argument from main().
   * \param argv argv argument from main().
   * \param scanDefaultPluginPaths Whether the default plugin paths provided by
   *   csGetPluginPaths() are scanned. Otherwise, no plugin scanning is done.
   *   In this case the csPathsList* version of scfInitialize() should be
   *   invoked manually if some paths should be scanned for plugins.
   */
  static bool InitializeSCF (int argc, char const* const argv[],
    bool scanDefaultPluginPaths = true);

  /**
   * This function should be called second. It will create the object
   * registry and return a pointer to it. If there is a problem it will
   * return 0.
   */
  static iObjectRegistry* CreateObjectRegistry ();

  /**
   * You will almost certainly want to call this function. It will
   * create the plugin manager which is essential for nearly everything.
   * The created plugin manager will be registered with the object registry
   * as the default plugin manager (using 0 tag).
   */
  static iPluginManager* CreatePluginManager (iObjectRegistry*);

  /**
   * This essential function creates the event queue which is the main
   * driving force between the event-driven CS model. In addition this function
   * will register the created event queue with the object registry as
   * the default event queue (using 0 tag).
   */
  static iEventQueue* CreateEventQueue (iObjectRegistry*);

  /**
   * This function creates the thread manager, which coordinates
   * the thread queue system in CS.
   */
  static iThreadManager* CreateThreadManager (iObjectRegistry*);

  /**
   * Create the virtual clock. This clock is responsible for keeping
   * track of virtual time in the game system. This function will
   * register the created virtual clock with the object registry as the
   * default virtual clock (using 0 tag).
   */
  static iVirtualClock* CreateVirtualClock (iObjectRegistry*);

  /**
   * Create the commandline parser. This function will register the created
   * commandline parser with the object registry as the default
   * parser (using 0 tag).
   */
  static iCommandLineParser* CreateCommandLineParser (
    iObjectRegistry*, int argc, char const* const argv[]);

  /**
   * Create and, if needed, register the verbosity manager. It is used by a 
   * lot of plugins to control diagnostic output while running.
   */
  static iVerbosityManager* CreateVerbosityManager (iObjectRegistry*);

  /**
   * Create the config manager. This function will register the created
   * config manager with the object registry as the default config manager
   * (using 0 tag).
   */
  static iConfigManager* CreateConfigManager (iObjectRegistry*);

  /**
   * This function will create the three common input drivers
   * (csKeyboardDriver, csMouseDriver, and csJoystickDriver) and register
   * them with the object registry. Note that this function must be
   * called after creating the config manager (CreateConfigManager()).
   */
  static bool CreateInputDrivers (iObjectRegistry*);

  /**
   * Create the global shared string sets and register them with the registry.
   * The first can be used if multiple, distinct modules want to share string IDs.
   * The set can be requested with:
   * \code
   * csRef<iStringSet> strings = csQueryRegistryTagInterface<iStringSet> (
   *   object_reg, "crystalspace.shared.stringset");
   * \endcode
   *
   * The second string set is used for shader variable names and can be
   * requested by:
   * \code
   * csRef<iShaderVarStringSet> strings =
   *   csQueryRegistryTagInterface<iShaderVarStringSet> (
   *     objectRegistry, "crystalspace.shader.variablenameset");
   * \endcode
   */
  static bool CreateStringSet (iObjectRegistry*);

  /**
   * Create the global system open manager sets and it them with the registry.
   * Must be called after CreateEventQueue() and is most sensibly called
   * before RequestPlugins().
   */
  static iSystemOpenManager* CreateSystemOpenManager (iObjectRegistry*);

  /**
   * Setup the config manager. If you have no config file then you can still
   * call this routine using a 0 parameter. If you don't call this then
   * either RequestPlugins() or Initialize() will call this routine with
   * 0 parameter. The 'ApplicationID' parameter is used to determine the
   * correct user-specific domain; if 0, the return value of GetDefaultAppID()
   * is used. It is possibly overriden by the application config file option 
   * "System.ApplicationID".
   *
   * This method will load the VFS plugin if not already present in the given
   * object registry.
   */
  static bool SetupConfigManager (iObjectRegistry*, const char* configName,
    const char *ApplicationID = 0);

  /**
   * Find or load the VFS plugin, add it to the given object registry, and
   * return it. An alternate plugin ID for VFS may be given as well.
   * Use this method if you need to make changes to VFS, or use an alternate
   * VFS plugin, before calling SetupConfigManager. 
   * Otherwise, SetupConfigManager will load the default VFS plugin
   * automatically.
   * objectReg can be the object registry object returned by CreateEnvironment,
   * or one that you have manually set up with plugin and config manager
   * objects.
   */
  static iVFS* SetupVFS(iObjectRegistry* objectReg, 
          const char* pluginID = "crystalspace.kernel.vfs");

  /**
   * Request a few widely used standard plugins and also read the standard
   * configuration file and command line for potential other plugins.  This
   * routine must be called before Initialize().
   * <p>
   * The variable arguments should contain four entries for every plugin you
   * want to load: SCF class name, SCF interface name, inteface ID, and
   * interface version. To make this easier it is recommended you use one of
   * the CS_REQUEST_xxx macros above. <b>WARNING</b> Make sure to end the list
   * with CS_REQUEST_END!
   */
  static bool RequestPlugins (iObjectRegistry*, ...);

  /**
   * This is just like RequestPlugins(...), which accepts a variable list of
   * arguments at compile-time, except that arguments are passed as a
   * `va_list'.
   */
  static bool RequestPluginsV (iObjectRegistry*, va_list);

  /**
   * Request a few widely used standard plugins and also read the standard
   * configuration file and command line for potential other plugins.  This
   * routine must be called before Initialize().
   * <p>
   * Unlike the variable-argument RequestPlugins(...) method which expects you
   * to know the list of requested plugins at compile-time, this overload
   * allows you to construct an array of plugins at run-time.  You do this by
   * constructing a csArray<> of csPluginRequest records.  For example:
   * \code
   * csArray<csPluginRequest> a;
   * a.Push(csPluginRequest(CS_REQUEST_VFS));
   * a.Push(csPluginRequest(CS_REQUEST_ENGINE));
   * a.Push(csPluginRequest(CS_REQUEST_PLUGIN("myproj.foobar",iFoobar)));
   * csInitializer::RequestPlugins(registry,a);
   * \endcode
   * <b>WARNING</b> csArray<> already knows its own size, so do <b>not</b>
   * terminate the list with CS_REQUEST_END.
   */
  static bool RequestPlugins(iObjectRegistry*,csArray<csPluginRequest> const&);

  /**
   * Send the csevSystemOpen command to all loaded plugins.
   * This should be done after initializing them (Initialize()).
   */
  static bool OpenApplication (iObjectRegistry*);

  /**
   * Send the csevSystemClose command to all loaded plugins.
   */
  static void CloseApplication (iObjectRegistry*);

  /**
   * Initialize an event handler for the application. This is the
   * most general routine. This event handler will receive all events
   * that are sent through the event manager. Use this function to know
   * about keyboard, mouse and other events. Note that you also have to
   * use this function to be able to render something as rendering
   * happens as a result of one event (csevFrame).
   */
  static bool SetupEventHandler (iObjectRegistry*, iEventHandler*, const csEventID[]);

  /**
   * Initialize an event handler function. This is an easier version
   * of SetupEventHandler() that takes a function and will register an
   * event handler to call that function for all stated events and their
   * children, or (if the third argument is omitted) for all events.
   */
  static bool SetupEventHandler (iObjectRegistry*, csEventHandlerFunc, const csEventID events[]);

  /**
   * Initialize an event handler function.  This function will receive EVERY
   * event that passes through the system.  This is usually not what you 
   * want, since (1) it means you'll probably receive a lot of events you're
   * not interested in, and (2) it's the most costly operation for the event
   * subscription scheduler.
   */
  static bool SetupEventHandler (iObjectRegistry*, csEventHandlerFunc);

  /**
   * Destroy the application.<p>
   * Undo all of the initialization done by CreateEnvironment() or any of the
   * other setup functions.
   * \remarks
   * This will also unload all loaded plugins! So if you keep any references
   * to plugins in the same scope as DestroyApplication (), clear those
   * BEFORE calling DestroyApplication (). Example:
   * \code
   * class MyApp
   * {
   *   csRef<iVFS> vfs;
   *  public:
   *   MyApp ();
   *   ~MyApp ()
   *   {
   *     csInitializer::DestroyApplication (...);
   *   }
   * }
   * \endcode
   * The above snippet will likely return in a crash at the end of the 
   * program - DestroyApplication () causes the unloading of all plugins,
   * including VFS, however, the csRef<iVFS> is destroyed AFTER 
   * DestroyApplication () was called, so it'll try to DecRef() an object that
   * has already been deleted. To fix the problem, either set 'vfs' to 0, or
   * move DestroyApplication () into another scope (e.g. the main() of an 
   * application.)
   */
  static void DestroyApplication (iObjectRegistry*);
  
  /**
   * The default application string name; computed from the executable name
   * as passed in argv[0] to CreateEnvironment(). If the latter was not called
   * or argv[0] did not contain a sensible name, a default string is used.
   */
  static const char* GetDefaultAppID();
protected:
};

/** @} */

#endif // __CS_INITAPP_H__
