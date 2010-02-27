// This file is automatically generated.
#include "cssysdef.h"
#include "csutil/scf.h"

// Put static linking stuff into own section.
// The idea is that this allows the section to be swapped out but not
// swapped in again b/c something else in it was needed.
#if !defined(CS_DEBUG) && defined(CS_COMPILER_MSVC)
#pragma const_seg(".CSmetai")
#pragma comment(linker, "/section:.CSmetai,r")
#pragma code_seg(".CSmeta")
#pragma comment(linker, "/section:.CSmeta,er")
#pragma comment(linker, "/merge:.CSmetai=.CSmeta")
#endif

namespace csStaticPluginInit
{
static char const metainfo_bugplug[] =
"<?xml version=\"1.0\"?>"
"<!-- bugplug.csplugin -->"
"<plugin>"
"  <scf>"
"    <classes>"
"      <class>"
"        <name>crystalspace.utilities.bugplug</name>"
"        <implementation>csBugPlug</implementation>"
"        <description>Debugging utility</description>"
"      </class>"
"    </classes>"
"  </scf>"
"</plugin>"
;
  #ifndef csBugPlug_FACTORY_REGISTER_DEFINED 
  #define csBugPlug_FACTORY_REGISTER_DEFINED 
    SCF_DEFINE_FACTORY_FUNC_REGISTRATION(csBugPlug) 
  #endif

class bugplug
{
SCF_REGISTER_STATIC_LIBRARY(bugplug,metainfo_bugplug)
  #ifndef csBugPlug_FACTORY_REGISTERED 
  #define csBugPlug_FACTORY_REGISTERED 
    csBugPlug_StaticInit csBugPlug_static_init__; 
  #endif
public:
 bugplug();
};
bugplug::bugplug() {}

}
