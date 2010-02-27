/*
  Copyright (C) 2003 by Marten Svanfeldt

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

#include "cssysdef.h"
#include <ctype.h>

// For builtin shader consts:
#include "iengine/light.h"
#include "iengine/sector.h"
#include "imap/services.h"
#include "iutil/plugin.h"
#include "iutil/vfs.h"
#include "iutil/verbositymanager.h"
#include "ivaria/keyval.h"
#include "ivaria/reporter.h"

#include "csutil/cfgacc.h"
#include "csutil/xmltiny.h"

#include "cpi/docwrap.h"
#include "shader.h"
#include "xmlshader.h"



CS_PLUGIN_NAMESPACE_BEGIN(XMLShader)
{

CS_LEAKGUARD_IMPLEMENT (csXMLShaderCompiler);

//---------------------------------------------------------------------------

SCF_IMPLEMENT_FACTORY (csXMLShaderCompiler)

csXMLShaderCompiler::csXMLShaderCompiler(iBase* parent) : 
  scfImplementationType (this, parent), debugInstrProcessing (false)
{
  static bool staticInited = false;
  if (!staticInited)
  {
    TempHeap::Init();
    Variables::Init();
    
    staticInited = true;
  }

  wrapperFact = 0;
  InitTokenTable (xmltokens);

  // Set up builtin constants
#define BUILTIN_CONSTANT(Type, Value)					    \
  condConstants.AddConstant (#Value, (Type)Value);
#include "cpi/condconstbuiltin.inc"
#undef BUILTIN_CONSTANT
}

csXMLShaderCompiler::~csXMLShaderCompiler()
{
  delete wrapperFact;
}

void csXMLShaderCompiler::Report (int severity, const char* msg, ...)
{
  va_list args;
  va_start (args, msg);
  csReportV (objectreg, severity, 
    "crystalspace.graphics3d.shadercompiler.xmlshader", msg, args);
  va_end (args);
}

void csXMLShaderCompiler::Report (int severity, iDocumentNode* node,
							const char* msg, ...)
{
  va_list args;
  va_start (args, msg);
  csString formattedMsg;
  formattedMsg.FormatV (msg, args);
  synldr->Report ("crystalspace.graphics3d.shadercompiler.xmlshader", 
    severity, node, "%s", formattedMsg.GetData());
  va_end (args);
}

bool csXMLShaderCompiler::Initialize (iObjectRegistry* object_reg)
{
  objectreg = object_reg;

  wrapperFact = new csWrappedDocumentNodeFactory (this);

  csRef<iPluginManager> plugin_mgr = 
      csQueryRegistry<iPluginManager> (object_reg);

  strings = csQueryRegistryTagInterface<iStringSet> (
    object_reg, "crystalspace.shared.stringset");
  stringsSvName = csQueryRegistryTagInterface<iShaderVarStringSet> (
    object_reg, "crystalspace.shader.variablenameset");
  
  string_mixmode_alpha = stringsSvName->Request ("mixmode alpha");
  stringLightCount = stringsSvName->Request ("light count");

  g3d = csQueryRegistry<iGraphics3D> (object_reg);
  vfs = csQueryRegistry<iVFS> (object_reg);
  
  synldr = csQueryRegistryOrLoad<iSyntaxService> (object_reg,
    "crystalspace.syntax.loader.service.text");
  if (!synldr)
    return false;

  csRef<iVerbosityManager> verbosemgr (
    csQueryRegistry<iVerbosityManager> (object_reg));
  if (verbosemgr) 
    do_verbose = verbosemgr->Enabled ("renderer.shader");
  else
    do_verbose = false;
    
  binDocSys = csLoadPluginCheck<iDocumentSystem> (plugin_mgr,
    "crystalspace.documentsystem.binary");
  xmlDocSys.AttachNew (new csTinyDocumentSystem);
  
  csConfigAccess config (object_reg);
  doDumpXML = config->GetBool ("Video.XMLShader.DumpVariantXML");
  doDumpConds = config->GetBool ("Video.XMLShader.DumpConditions");
  doDumpValues = config->GetBool ("Video.XMLShader.DumpPossibleValues");
  debugInstrProcessing = 
    config->GetBool ("Video.XMLShader.DebugInstructionProcessing");
    
  sharedEvaluator.AttachNew (new csConditionEvaluator (stringsSvName,
    condConstants));

  return true;
}

csPtr<iShader> csXMLShaderCompiler::CompileShader (
    	iLoaderContext* ldr_context, iDocumentNode *templ,
	int forcepriority)
{
  if (!templ) return 0;

  if (!ValidateTemplate (templ))
    return 0;

  /* We might only be loaded as a dependency of the engine, so query it
     here instead of Initialize() */
  if (!engine.IsValid())
  {
    engine = csQueryRegistry<iEngine> (objectreg);
    sharedEvaluator->SetEngine (engine);
  }
  
  csTicks startTime = 0, endTime = 0;
  // Create a shader. The actual loading happens later.
  csRef<csXMLShader> shader;
  if (do_verbose) startTime = csGetTicks();
  shader.AttachNew (new csXMLShader (this, ldr_context, templ, forcepriority));
  if (do_verbose) endTime = csGetTicks();
  shader->SetName (templ->GetAttributeValue ("name"));
  shader->SetDescription (templ->GetAttributeValue ("description"));
  if (do_verbose)
  {
    csString str;
    shader->DumpStats (str);
    Report(CS_REPORTER_SEVERITY_NOTIFY, 
      "Shader %s: %s, %u ms", shader->GetName (), str.GetData (),
      endTime - startTime);
  }

  csRef<iDocumentNodeIterator> tagIt = templ->GetNodes ("key");
  while (tagIt->HasNext ())
  {
    // @@@ FIXME: also keeps "editoronly" keys
    csRef<iKeyValuePair> keyvalue = synldr->ParseKey (tagIt->Next ());
    if (keyvalue)
      shader->QueryObject ()->ObjAdd (keyvalue->QueryObject ());
  }

  csRef<iShader> ishader (shader);
  return csPtr<iShader> (ishader);
}
  
bool csXMLShaderCompiler::PrecacheShader(iDocumentNode* templ,
                                         iHierarchicalCache* cache,
                                         bool quick)
{
  if (!templ) return 0;

  if (!ValidateTemplate (templ))
    return 0;
  
  csTicks startTime = 0, endTime = 0;
  // Create a shader. The actual loading happens later.
  csRef<csXMLShader> shader;
  if (do_verbose) startTime = csGetTicks();
  shader.AttachNew (new csXMLShader (this));
  shader->SetName (templ->GetAttributeValue ("name"));
  bool result = shader->Precache (templ, cache, quick);
  if (do_verbose) endTime = csGetTicks();
  if (do_verbose)
  {
    csString str;
    shader->DumpStats (str);
    Report(CS_REPORTER_SEVERITY_NOTIFY, 
      "Shader %s: %s, %u ms", shader->GetName (), str.GetData (),
      endTime - startTime);
  }
  
  return result;
}

csPtr<iShaderPriorityList> csXMLShaderCompiler::GetPriorities (
	iDocumentNode* templ)
{
  csRef<iShaderManager> shadermgr = 
    csQueryRegistry<iShaderManager> (objectreg);
  CS_ASSERT (shadermgr); // Should be present - loads us, after all

  csShaderPriorityList* list = new csShaderPriorityList ();

  /* @@@ A bit awkward, almost the same code as in 
     csXMLShader::ScanForTechniques */
  csRef<iDocumentNodeIterator> it = templ->GetNodes();

  // Read in the techniques.
  while (it->HasNext ())
  {
    csRef<iDocumentNode> child = it->Next ();
    if (child->GetType () == CS_NODE_ELEMENT &&
      xmltokens.Request (child->GetValue ()) == XMLTOKEN_TECHNIQUE)
    {
      //save it
      int p = child->GetAttributeValueAsInt ("priority");
      // Compute the tag's priorities.
      csRef<iDocumentNodeIterator> tagIt = child->GetNodes ("tag");
      while (tagIt->HasNext ())
      {
	csRef<iDocumentNode> tag = tagIt->Next ();
	csStringID tagID = strings->Request (tag->GetContentsValue ());

	csShaderTagPresence presence;
	int priority;
	shadermgr->GetTagOptions (tagID, presence, priority);
	if (presence == TagNeutral)
	{
	  p += priority;
	}
      }
      list->priorities.InsertSorted (p);
    }
  }

  return csPtr<iShaderPriorityList> (list);
}

bool csXMLShaderCompiler::ValidateTemplate(iDocumentNode *templ)
{
  if (!IsTemplateToCompiler(templ))
    return false;

  /*@@TODO: Validation without accual compile. should check correct xml
  syntax, and that we have at least one techqniue which can load. Also check
  that we have valid texturemapping and buffermapping*/

  return true;
}

bool csXMLShaderCompiler::IsTemplateToCompiler(iDocumentNode *templ)
{
  //Check that we got an element
  if (templ->GetType() != CS_NODE_ELEMENT) return false;

  //With name shader  (<shader>....</shader>
  if (xmltokens.Request (templ->GetValue())!=XMLTOKEN_SHADER) return false;

  //Check the type-string in <shader>
  const char* shaderName = templ->GetAttributeValue ("name");
  const char* shaderType = templ->GetAttributeValue ("compiler");
  // Prefer "compiler" about (somewhat ambiguous) "type"
  if (shaderType == 0) shaderType = templ->GetAttributeValue ("type");
  if ((shaderType == 0) || (xmltokens.Request (shaderType) != 
    XMLTOKEN_XMLSHADER))
  {
    Report (CS_REPORTER_SEVERITY_ERROR, 
      "Type of shader '%s' is not 'xmlshader', but '%s'",
      shaderName, shaderType);
    return false;
  }

  //Check that we have children, no children == not a template to this one at
  //least.
  if (!templ->GetNodes()->HasNext()) return false;

  //Ok, passed check. We will try to validate it
  return true;
}
  
csPtr<iDocumentNode> csXMLShaderCompiler::ReadNodeFromBuf (iDataBuffer* buf)
{
  csRef<iDocument> boilerplate;
  csRef<iDocumentSystem> docsys = binDocSys;
  if (docsys.IsValid())
  {
    boilerplate = docsys->CreateDocument ();
    const char* err = boilerplate->Parse (buf);
    if (err != 0)
    {
      if (do_verbose)
	Report (CS_REPORTER_SEVERITY_ERROR, 
	  "Couldn't read document: %s", err);
    }
  }
  if (!boilerplate.IsValid())
  {
    docsys = xmlDocSys;
    if (docsys.IsValid())
    {
      boilerplate = docsys->CreateDocument ();
      const char* err = boilerplate->Parse (buf);
      if (err != 0)
      {
      if (do_verbose)
	Report (CS_REPORTER_SEVERITY_ERROR, 
	  "Couldn't read document: %s", err);
      }
    }
  }
  if (!boilerplate.IsValid()) return 0;
  
  csRef<iDocumentNode> root = boilerplate->GetRoot();
  if (!root.IsValid()) return 0;
  
  csRef<iDocumentNodeIterator> it = root->GetNodes();
  if (!it->HasNext()) return 0;
  csRef<iDocumentNode> firstNode = it->Next();
  if (it->HasNext()) return 0;
  
  return csPtr<iDocumentNode> (firstNode);
}

csPtr<iDataBuffer> csXMLShaderCompiler::WriteNodeToBuf (iDocument* doc)
{
  csMemFile docFile;
  const char* err = doc->Write (&docFile);
  if (err != 0)
  {
    if (do_verbose)
      Report (CS_REPORTER_SEVERITY_WARNING,
	  "Couldn't write document: %s", err);
    return 0;
  }
  return docFile.GetAllData (false);
}
  
csPtr<iDocument> csXMLShaderCompiler::CreateCachingDoc ()
{
  csRef<iDocumentSystem> docsys = binDocSys;
  if (!docsys.IsValid()) docsys = xmlDocSys;
  
  return csPtr<iDocument> (docsys->CreateDocument());
}

}
CS_PLUGIN_NAMESPACE_END(XMLShader)
