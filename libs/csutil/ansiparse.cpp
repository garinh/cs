/*
    Copyright (C) 2005 by Jorrit Tyberghein
	      (C) 2005 by Frank Richter
              (C) 2006 by Marten Svanfeldt

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

#include "csutil/ansiparse.h"
#include "csutil/csstring.h"

bool csAnsiParser::ParseAnsi (const char* str, size_t& ansiCommandLen, 
			      CommandClass& cmdClass, size_t& textLen)
{
  if (*str == 0) return false;

  if ((str[0] == '\033') && (str[1] == '['))
  {
    ansiCommandLen = strcspn (str, 
      "AaBbCcDdEeFfGgHhIiJjKkLlMmNnOoPpQqRrSsTtUuVvWwXxYyZz");
    if (str[1] == '[')
    {
      if (str[ansiCommandLen] == 'm')
        cmdClass = classFormat;
      else if (str[ansiCommandLen] == 'J' ||
               str[ansiCommandLen] == 'K')
        cmdClass = classClear;
      else if (str[ansiCommandLen] == 'H' ||
               str[ansiCommandLen] == 'f' ||
               str[ansiCommandLen] == 'A' ||
               str[ansiCommandLen] == 'B' ||
               str[ansiCommandLen] == 'C' ||
               str[ansiCommandLen] == 'D')
        cmdClass = classCursor;
    }
    else
      cmdClass = classUnknown;

    if (str[ansiCommandLen] != 0) ansiCommandLen++;
  }
  else
  {
    cmdClass = classNone;
    ansiCommandLen = 0;
  }
  const char* text = str + ansiCommandLen;
  const char* nextEscape = strchr (text, '\033');
  if (nextEscape == 0)
  {
    textLen = strlen (text);
  }
  else
  {
    textLen = nextEscape - text;
  }
  return true;
}

bool csAnsiParser::DecodeCommand (const char*& cmd, size_t& cmdLen, 
				  Command& command, 
				  CommandParams& commandParams)
{
  if (cmdLen == 0) return false;

  command = cmdUnknown;
  if ((cmd[0] == '\033') && (cmd[1] == '['))
  {
    cmd += 2; cmdLen -= 2;
  }
  if (cmd[cmdLen-1] == 'm')
  {
    const char* semicolon = strchr (cmd, ';');
    size_t paramLen;
    if ((semicolon == 0) || ((size_t)(semicolon - cmd) >= cmdLen))
    {
      paramLen = cmdLen - 1;
    }
    else
    {
      paramLen = semicolon - cmd;
    }
    csString paramStr;
    paramStr.Append (cmd, paramLen);
    int param;
    char dummy;
    if (sscanf (paramStr, "%d%c", &param, &dummy) == 1)
    {
      if (param == 0)
        command = cmdFormatAttrReset;
      else if (param == 1)
      {
        command = cmdFormatAttrEnable;
        commandParams.attrVal = attrBold;
      }
      else if (param == 22)
      {
        command = cmdFormatAttrDisable;
        commandParams.attrVal = attrBold;
      }
      else if (param == 3)
      {
        command = cmdFormatAttrEnable;
        commandParams.attrVal = attrItalics;
      }
      else if (((param >= 0) && (param <= 9))
        || ((param >= 20) && (param <= 29)))
      {
        command = (param >= 20) ? cmdFormatAttrDisable : cmdFormatAttrEnable;
        switch (param % 20)
        {
          case 2: commandParams.attrVal = attrDim;		       break;
          case 4: commandParams.attrVal = attrUnderline;	   break;
          case 5: commandParams.attrVal = attrBlink;		     break;
          case 7: commandParams.attrVal = attrReverse;		   break;
          case 8: commandParams.attrVal = attrInvisible;	   break;
          case 9: commandParams.attrVal = attrStrikethrough; break;
        }
      }
      else if ((param >= 30) && (param < 38))
      {
        command = cmdFormatAttrForeground;
        commandParams.colorVal = (FormatColor)(param - 30);
      }
      else if ((param >= 40) && (param < 48))
      {
        command = cmdFormatAttrBackground;
        commandParams.colorVal = (FormatColor)(param - 40);
      }
    }
    cmd += paramLen + 1;
    cmdLen -= paramLen + 1;
    return true;
  }
  else if (cmd[cmdLen-1] == 'J')
  {
    command = cmdClearScreen;
    cmd += 1;
    cmdLen -= 1;
    return true;
  }
  else if (cmd[cmdLen-1] == 'K')
  {
    command = cmdClearLine;
    cmd += 1;
    cmdLen -= 1;
    return true;
  }
  else if (cmd[cmdLen-1] == 'H' || cmd[cmdLen-1] == 'j')
  {
    int pl, pc;
    if (sscanf (cmd, "%d;%d", &pl, &pc) == 2)
    {
      command = cmdCursorSetPosition;
      commandParams.cursorVal.x = pc;
      commandParams.cursorVal.y = pl;
    }

    cmd += cmdLen;
    cmdLen = 0;
    return true;
  }
  else if (cmd[cmdLen-1] == 'A')
  {
    int pl;
    if (sscanf (cmd, "%d", &pl) == 1)
    {
      command = cmdCursorMoveRelative;
      commandParams.cursorVal.x = 0;
      commandParams.cursorVal.y = -pl;
    }
    cmd += cmdLen;
    cmdLen = 0;
    return true;
  }
  else if (cmd[cmdLen-1] == 'B')
  {
    int pl;
    if (sscanf (cmd, "%d", &pl) == 1)
    {
      command = cmdCursorMoveRelative;
      commandParams.cursorVal.x = 0;
      commandParams.cursorVal.y = pl;
    }
    cmd += cmdLen;
    cmdLen = 0;
    return true;
  }
  else if (cmd[cmdLen-1] == 'C')
  {
    int pc;
    if (sscanf (cmd, "%d", &pc) == 1)
    {
      command = cmdCursorMoveRelative;
      commandParams.cursorVal.x = pc;
      commandParams.cursorVal.y = 0;
    }
    cmd += cmdLen;
    cmdLen = 0;
    return true;
  }
  else if (cmd[cmdLen-1] == 'D')
  {
    int pc;
    if (sscanf (cmd, "%d", &pc) == 1)
    {
      command = cmdCursorMoveRelative;
      commandParams.cursorVal.x = -pc;
      commandParams.cursorVal.y = 0;
    }
    cmd += cmdLen;
    cmdLen = 0;
    return true;
  }
  else
  {
    return false;
  }
} 
