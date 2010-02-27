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
static char const metainfo_lookat[] =
"<?xml version=\"1.0\"?>"
"<!-- lookat.csplugin -->"
"<plugin>"
"  <scf>"
"    <classes>"
"      <class>"
"        <name>crystalspace.mesh.animesh.controllers.lookat</name>"
"        <implementation>LookAtManager</implementation>"
"        <description>Crystal Space LookAt animation node of an animated mesh</description>"
"      </class>"
"    </classes>"
"  </scf>"
"</plugin>"
;
  #ifndef LookAtManager_FACTORY_REGISTER_DEFINED 
  #define LookAtManager_FACTORY_REGISTER_DEFINED 
    SCF_DEFINE_FACTORY_FUNC_REGISTRATION(LookAtManager) 
  #endif

class lookat
{
SCF_REGISTER_STATIC_LIBRARY(lookat,metainfo_lookat)
  #ifndef LookAtManager_FACTORY_REGISTERED 
  #define LookAtManager_FACTORY_REGISTERED 
    LookAtManager_StaticInit LookAtManager_static_init__; 
  #endif
public:
 lookat();
};
lookat::lookat() {}

}