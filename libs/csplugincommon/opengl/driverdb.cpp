/*
    Copyright (C) 2004 by Jorrit Tyberghein
	      (C) 2004 by Frank Richter

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
#include "imap/services.h"
#include "iutil/cfgmgr.h"
#include "iutil/document.h"
#include "iutil/vfs.h"
#include "ivaria/reporter.h"
#include "csutil/cfgdoc.h"
#include "csutil/hash.h"
#include "csutil/regexp.h"

#include "csplugincommon/opengl/driverdb.h"
#include "csplugincommon/opengl/glcommon2d.h"

CS_LEAKGUARD_IMPLEMENT (csGLDriverDatabase);

#define CS_TOKEN_ITEM_FILE "libs/csplugincommon/opengl/driverdb.tok"
#include "cstool/tokenlist.h"

class csDriverDBReader
{
private:
  csGLDriverDatabase* db;
  csStringHash& tokens;
  iConfigManager* cfgmgr;
  iSyntaxService* synsrv;
  int usedCfgPrio;

  enum FulfillConditions
  {
    All,
    One
  };

  csHash<csRef<csConfigDocument>, csString> configs;
public:
  CS_LEAKGUARD_DECLARE (csDriverDBReader);

  csDriverDBReader (csGLDriverDatabase* db, iConfigManager* cfgmgr, 
    iSyntaxService* synsrv, int usedCfgPrio);

  bool Apply (iDocumentNode* node);

  bool ParseConditions (iDocumentNode* node, 
    bool& result, bool negate = false);
  bool ParseRegexp (iDocumentNode* node, bool& result);
  bool ParseCompareVer (iDocumentNode* node, bool& result);

  bool ParseConfigs (iDocumentNode* node);
  bool ParseRules (iDocumentNode* node);
};

CS_LEAKGUARD_IMPLEMENT (csDriverDBReader);

csDriverDBReader::csDriverDBReader (csGLDriverDatabase* db, 
				    iConfigManager* cfgmgr, 
				    iSyntaxService* synsrv, 
				    int usedCfgPrio) :
  tokens(db->tokens)
{
  csDriverDBReader::db = db;
  csDriverDBReader::cfgmgr = cfgmgr;
  csDriverDBReader::synsrv = synsrv;
  csDriverDBReader::usedCfgPrio = usedCfgPrio;
}

bool csDriverDBReader::Apply (iDocumentNode* node)
{
  csRef<iDocumentNodeIterator> it (node->GetNodes ());
  while (it->HasNext())
  {
    csRef<iDocumentNode> child = it->Next();
    if (child->GetType() != CS_NODE_ELEMENT) continue;
    csStringID token = tokens.Request (child->GetValue ());

    switch (token)
    {
      case XMLTOKEN_USECFG:
	{
	  const char* cfgname = child->GetContentsValue ();
	  csRef<csConfigDocument> cfg (configs.Get (cfgname, 
	    (csConfigDocument*)0));
	  if (!cfg.IsValid ())
	  {
	    synsrv->Report (
	      "crystalspace.canvas.openglcommon.driverdb",
	      CS_REPORTER_SEVERITY_WARNING,
	      child,
	      "unknown config %s", cfgname);
	  }
	  else
	  {
	    cfgmgr->AddDomain (cfg, usedCfgPrio);
	    db->addedConfigs.Push (cfg);
	  }
	}
	break;
      default:
	synsrv->ReportBadToken (child);
	return false;
    }
  }
  return true;
}

bool csDriverDBReader::ParseConditions (iDocumentNode* node, 
					bool& result, 
					bool negate)
{
  FulfillConditions fulfill = All;
  const char* fulfillAttr = node->GetAttributeValue ("fulfill");
  if (fulfillAttr != 0)
  {
    if (strcmp (fulfillAttr, "one") == 0)
      fulfill = One;
    else if (strcmp (fulfillAttr, "all") == 0)
      fulfill = All;
    else
    {
      synsrv->Report (
	"crystalspace.canvas.openglcommon.driverdb",
	CS_REPORTER_SEVERITY_WARNING,
	node,
	"Invalid 'fulfill' attribute '%s'", fulfillAttr);
      return false;
    }
  }

  csRef<iDocumentNodeIterator> it (node->GetNodes ());
  while (it->HasNext())
  {
    csRef<iDocumentNode> child = it->Next();
    if (child->GetType() != CS_NODE_ELEMENT) continue;
    csStringID token = tokens.Request (child->GetValue ());

    bool lastResult = false;

    switch (token)
    {
      case XMLTOKEN_CONDITIONS:
	if (!ParseConditions (child, lastResult))
	  return false;
	break;
      case XMLTOKEN_NEGATE:
	if (!ParseConditions (child, lastResult, true))
	  return false;
	break;
      case XMLTOKEN_REGEXP:
	if (!ParseRegexp (child, lastResult))
	  return false;
	break;
      case XMLTOKEN_COMPAREVER:
	if (!ParseCompareVer (child, lastResult))
	  return false;
	break;
      default:
	synsrv->ReportBadToken (child);
	return false;
    }

    switch (fulfill)
    {
      case One:
	if (lastResult ^ negate)
	{
	  result = true;
	  return true;
	}
	break;
      case All:
	if (!(lastResult ^ negate))
	{
	  result = false;
	  return true;
	}
	break;
    }
  }
  result = (fulfill == All);
  return true;
}

bool csDriverDBReader::ParseRegexp (iDocumentNode* node, bool& result)
{
  const char* string = node->GetAttributeValue ("string");
  if (string == 0)
  {
    synsrv->Report (
      "crystalspace.canvas.openglcommon.driverdb",
      CS_REPORTER_SEVERITY_WARNING,
      node,
      "No 'string' attribute");
    return false;
  }
  const char* pattern = node->GetAttributeValue ("pattern");
  if (pattern == 0)
  {
    synsrv->Report (
      "crystalspace.canvas.openglcommon.driverdb",
      CS_REPORTER_SEVERITY_WARNING,
      node,
      "No 'pattern' attribute");
    return false;
  }
  
  const char* str = db->ogl2d->GetRendererString (string);
  if (str == 0)
  {
    result = false;
    return true;
  }

  csRegExpMatcher re (pattern);
  result = (re.Match (str) == csrxNoError);
  return true;
}

bool csDriverDBReader::ParseCompareVer (iDocumentNode* node, bool& result)
{
  const char* version = node->GetAttributeValue ("version");
  if (version == 0)
  {
    synsrv->Report (
      "crystalspace.canvas.openglcommon.driverdb",
      CS_REPORTER_SEVERITY_WARNING,
      node,
      "No 'version' attribute");
    return false;
  }
  const char* relation = node->GetAttributeValue ("relation");
  if (relation == 0)
  {
    synsrv->Report (
      "crystalspace.canvas.openglcommon.driverdb",
      CS_REPORTER_SEVERITY_WARNING,
      node,
      "No 'relation' attribute");
    return false;
  }

  const char* space = strchr (relation, ' ');
  if (space == 0)
  {
    synsrv->Report (
      "crystalspace.canvas.openglcommon.driverdb",
      CS_REPORTER_SEVERITY_WARNING,
      node,
      "Malformed 'relation'");
    return false;
  }
  int rellen = space - relation;

  csGLDriverDatabase::Relation rel;
  if (strncmp (relation, "eq", rellen) == 0)
    rel = csGLDriverDatabase::eq;
  else if (strncmp (relation, "neq", rellen) == 0)
    rel = csGLDriverDatabase::neq;
  else if (strncmp (relation, "lt", rellen) == 0)
    rel = csGLDriverDatabase::lt;
  else if (strncmp (relation, "le", rellen) == 0)
    rel = csGLDriverDatabase::le;
  else if (strncmp (relation, "gt", rellen) == 0)
    rel = csGLDriverDatabase::gt;
  else if (strncmp (relation, "ge", rellen) == 0)
    rel = csGLDriverDatabase::ge;
  else
  {
    csString relstr;
    relstr.Append (relation, rellen);

    synsrv->Report (
      "crystalspace.canvas.openglcommon.driverdb",
      CS_REPORTER_SEVERITY_WARNING,
      node,
      "Unknown relation '%s'", relstr.GetData());
    return false;
  }

  result = false;
  const char* verStr1 = db->ogl2d->GetVersionString (version);
  if (verStr1 == 0)
  {
    /* @@@ Hmm... a version may just be unknown on some platforms...
       (e.g. win32_driver on Linux), so we leave result as false (check failed)
       and return true, indicating the parsing was successful. */
    return true;
  }
  const char* verStr2 = relation + rellen + 1;
  result = csGLDriverDatabase::VersionCompare (verStr1, verStr2, rel);

  return true;
}

#include "csutil/custom_new_disable.h"
bool csDriverDBReader::ParseConfigs (iDocumentNode* node)
{
  csRef<iDocumentNodeIterator> it (node->GetNodes ());
  while (it->HasNext())
  {
    csRef<iDocumentNode> child = it->Next();
    if (child->GetType() != CS_NODE_ELEMENT) continue;
    csStringID token = tokens.Request (child->GetValue ());

    switch (token)
    {
      case XMLTOKEN_CONFIG:
	{
	  const char* name = child->GetAttributeValue ("name");
	  if (!name)
	  {
	    synsrv->Report (
	      "crystalspace.canvas.openglcommon.driverdb",
	      CS_REPORTER_SEVERITY_WARNING,
	      child,
	      "<config> has no name");
	    return false;
	  }
	  csRef<csConfigDocument> cfg (configs.Get (name, 
	    (csConfigDocument*)0));
	  if (!cfg.IsValid ())
	  {
	    cfg.AttachNew (new csConfigDocument ());
	    configs.Put (name, cfg);
	  }
	  cfg->LoadNode (child, true, true);
	}
	break;
      default:
	synsrv->ReportBadToken (child);
	return false;
    }
  }
  return true;
}
#include "csutil/custom_new_enable.h"

bool csDriverDBReader::ParseRules (iDocumentNode* node)
{
  csRef<iDocumentNodeIterator> it (node->GetNodes ());
  while (it->HasNext())
  {
    csRef<iDocumentNode> child = it->Next();
    if (child->GetType() != CS_NODE_ELEMENT) continue;
    csStringID token = tokens.Request (child->GetValue ());

    switch (token)
    {
      case XMLTOKEN_RULE:
	{
	  const char* rulePhase = child->GetAttributeValue ("phase");
	  if (rulePhase == 0) rulePhase = "";
	  if (strcmp (db->rulePhase, rulePhase) != 0)
	    continue;

	  csRef<iDocumentNode> conditions = child->GetNode ("conditions");
	  csRef<iDocumentNode> applicable = child->GetNode ("applicable");
	  csRef<iDocumentNode> notapplicable = child->GetNode ("notapplicable");

	  bool condTrue = true;
	  bool applied = false;
	  if (conditions.IsValid())
	  {
	    if (!ParseConditions (conditions, condTrue))
	      return false;
	  }
	  if (condTrue)
	  {
	    if (applicable.IsValid ())
	    {
	      if (!Apply (applicable)) return false;
	      applied = true;
	    }
	  }
	  else
	  {
	    if (notapplicable.IsValid ())
	    {
	      if (!Apply (notapplicable)) return false;
	      applied = true;
	    }
	  }
	  if (applied)
	  {
	    const char* descr = child->GetAttributeValue ("description");
	    if (descr != 0)
	    {
	      db->Report (CS_REPORTER_SEVERITY_NOTIFY,
		"Applied: %s", descr);
	    }
	  }
	}
	break;
      default:
	synsrv->ReportBadToken (child);
	return false;
    }
  }
  return true;
}

//-------------------------------------------------------------------------//

bool csGLDriverDatabase::Compare (int a, int b, Relation rel)
{
  switch (rel)
  {
    case eq: return a == b;
    case neq: return a != b;
    case lt: return a < b;
    case le: return a <= b;
    case gt: return a > b;
    case ge: return a >= b;
  }
  return false;
}

/* Returns the offset to the next block of characters with chars from
 * charSet. e.g. NextBlock ("123abc456", "123456") returns 6. */
static size_t NextBlock (const char* str, const char* charSet)
{
  size_t spnpos = strspn (str, charSet);
  return spnpos + strcspn (str + spnpos, charSet);
}

bool csGLDriverDatabase::VersionCompare (const char* verStr1, const char* verStr2,
                                         Relation rel)
{
  static const Relation earlyExitRels[6] = {eq, neq, lt, lt, gt, gt};
  static const Relation nonLastDigitRels[6] = {eq, neq, le, le, ge, ge};
  static const char digits[] = "0123456789";

  bool result = false;
  // Skip leading non-digits
  const char* curpos1 = verStr1 + strcspn (verStr1, digits);
  const char* curpos2 = verStr2 + strcspn (verStr2, digits);

  while ((curpos1 && (*curpos1 != 0)) || (curpos2 && (*curpos2 != 0)))
  {
    const char* nextpos1 = 0;
    const char* nextpos2 = 0;
  
    bool last1 = true, last2 = true;
    if (curpos1 && *curpos1)
    {
      size_t nextpos1dist = NextBlock (curpos1, digits);
      if (nextpos1dist != 0)
      {
        nextpos1 = curpos1 + nextpos1dist;
        last1 = NextBlock (nextpos1, digits) == 0;
      }
    }
    if (curpos2 && *curpos2)
    {
      size_t nextpos2dist = NextBlock (curpos2, digits);
      if (nextpos2dist != 0)
      {
        nextpos2 = curpos2 + nextpos2dist;
        last2 = NextBlock (nextpos2, digits) == 0;
      }
    }

    bool last = last1 && last2;

    int v1 = 0, v2 = 0;
    // If a version component is missing, treat as 0
    if (curpos1 && *curpos1 && (sscanf (curpos1, "%d", &v1) != 1)) break;
    if (curpos2 && *curpos2 && (sscanf (curpos2, "%d", &v2) != 1)) break;

    if (Compare (v1, v2, earlyExitRels[rel]))
    {
      result = true;
      break;
    }
    
    if (!Compare (v1, v2, last ? rel : nonLastDigitRels[rel]))
      break;

    if (last) 
    {
      result = true;
      break;
    }

    curpos1 = nextpos1;
    curpos2 = nextpos2;
  }

  return result;
}

void csGLDriverDatabase::Report (int severity, const char* msg, ...)
{
  va_list args;
  va_start (args, msg);
  csReportV (ogl2d->object_reg, severity, 
    "crystalspace.canvas.openglcommon.driverdb", msg, args);
  va_end (args);
}

csGLDriverDatabase::csGLDriverDatabase () : ogl2d (0)
{
  ::InitTokenTable (tokens);
}

csGLDriverDatabase::~csGLDriverDatabase ()
{
}

void csGLDriverDatabase::Open (csGraphics2DGLCommon* ogl2d, 
			       iDocumentNode* dbRoot, const char* phase, 
			       int configPriority)
{
  csGLDriverDatabase::ogl2d = ogl2d;
  rulePhase = phase ? phase : "";

  csRef<iConfigManager> cfgmgr = 
    csQueryRegistry<iConfigManager> (ogl2d->object_reg);

  csRef<iSyntaxService> synsrv = csQueryRegistryOrLoad<iSyntaxService> (
  	ogl2d->object_reg, "crystalspace.syntax.loader.service.text");

  csDriverDBReader reader (this, cfgmgr, synsrv, configPriority);

  csRef<iDocumentNodeIterator> it (dbRoot->GetNodes ());
  while (it->HasNext())
  {
    csRef<iDocumentNode> node = it->Next();
    if (node->GetType() != CS_NODE_ELEMENT) continue;
    csStringID token = tokens.Request (node->GetValue ());

    switch (token)
    {
      case XMLTOKEN_CONFIGS:
	if (!reader.ParseConfigs (node))
	  return;
	break;
      case XMLTOKEN_RULES:
	if (!reader.ParseRules (node))
	  return;
	break;
      default:
	synsrv->ReportBadToken (node);
	return;
    }
  }
}

void csGLDriverDatabase::Close ()
{
  if (!ogl2d) return;
  csRef<iConfigManager> cfgmgr = 
    csQueryRegistry<iConfigManager> (ogl2d->object_reg);
  for (size_t i = 0; i < addedConfigs.GetSize (); i++)
  {
    cfgmgr->RemoveDomain (addedConfigs[i]);
  }
  addedConfigs.DeleteAll();
}
