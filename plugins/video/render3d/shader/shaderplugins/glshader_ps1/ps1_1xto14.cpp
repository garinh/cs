/*
  Copyright (C) 2004 by John Harger
            (C) 2004 by Frank Richter
  
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

#include "ps1_1xto14.h"
#include "ps1_instr.h"

CS_PLUGIN_NAMESPACE_BEGIN(GLShaderPS1)
{

#define BIT_RGB			    0x1
#define BIT_ALPHA		    0x2
#define BIT_RGBA		    0x3

#define TEXREG_BIT(regnum, bits)    ((bits) << ((regnum) * 2))
#define TEMPREG_BIT(regnum, bits)   ((bits) << (16 + (regnum) * 2))

/// For a destination write mask, return the components that are written to.
static inline int DestModBits (uint destMod)
{
  uint result = 0;
  if ((destMod & (CS_PS_WMASK_RED | CS_PS_WMASK_GREEN | CS_PS_WMASK_BLUE)) ||
    (destMod == CS_PS_WMASK_NONE)) result |= BIT_RGB;
  if ((destMod & CS_PS_WMASK_ALPHA) ||
    (destMod == CS_PS_WMASK_NONE)) result |= BIT_ALPHA;
  return result;
}

/**
 * For a source swizzle, return the components that are read from.
 * Makes the assumption that only the components in \p destBits will
 * actually be used.
 */
static inline int SrcModBits (uint srcMod, uint destBits = BIT_RGBA)
{
  uint result = 0;
  const uint repMaskRGB = CS_PS_RMOD_REP_RED | CS_PS_RMOD_REP_GREEN | 
    CS_PS_RMOD_REP_BLUE;
  const uint repMask = repMaskRGB | CS_PS_RMOD_REP_ALPHA;
  // Simple case: no replication
  if ((srcMod & repMask) == 0) return destBits;
  // Replication is used.
  if ((srcMod & (repMaskRGB | CS_PS_RMOD_XYZ)))
    result |= BIT_RGB;
  if ((srcMod & (CS_PS_RMOD_REP_ALPHA | CS_PS_RMOD_XYW)))
    result |= BIT_ALPHA;
  return result;
}

static const uint instrFlags[CS_PS_INS_END_OF_LIST] = {
  0, /* CS_PS_INS_INVALID */
#define PS_INSTR(instr, args, psflags)				            \
  psflags & CS_PS_MASKFLAGS,
#define PS_VER_INSTR(x,y)						    \
  0,
#include "ps1_instr.inc"
};

const char* csPS1xTo14Converter::SetLastError (const char* fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  lastError.FormatV (fmt, args);
  va_end (args);

  return lastError.GetData();
}

void csPS1xTo14Converter::ResetState()
{
  texInsertPos = 0;
  newInstructions.Empty();

  int i;
  for (i = 0; i < 2; i++)
  {
    tempRegisterMap[i][0] = -1;
    tempRegisterMap[i][1] = -1;
    tempRegisterExpire[i][0] = 0;
    tempRegisterExpire[i][1] = 0;
  }
  neededRegs.Empty();
}

const char* csPS1xTo14Converter::GetTempReg (int oldReg, size_t instrIndex, 
					     uint usedBits, int& newReg)
{
  newReg = -1;

  switch (usedBits)
  {
    case BIT_RGB:
      if ((tempRegisterMap[oldReg][0] != -1) && 
	(instrIndex < tempRegisterExpire[oldReg][0]))
      {
	newReg = tempRegisterMap[oldReg][0];
	return 0;
      }
      break;
    case BIT_ALPHA:
      if ((tempRegisterMap[oldReg][1] != -1) && 
	(instrIndex < tempRegisterExpire[oldReg][1]))
      {
	newReg = tempRegisterMap[oldReg][1];
	return 0;
      }
      break;
    case BIT_RGBA:
      if ((tempRegisterMap[oldReg][0] != -1) && 
	(tempRegisterMap[oldReg][1] != -1) && 
	(instrIndex < tempRegisterExpire[oldReg][0]) &&
	(instrIndex < tempRegisterExpire[oldReg][1]))
      {
	newReg = tempRegisterMap[oldReg][0];
	return 0;
      }
      break;
  }

  // Determine timespan the original temp reg was used.
  size_t firstNeeded = instrIndex + 1, lastNeeded = firstNeeded;
  while ((lastNeeded < neededRegs.GetSize ()) && 
    (neededRegs[lastNeeded] & TEMPREG_BIT(oldReg, usedBits)))
    lastNeeded++;

  int checkOrder[4];
  checkOrder[0] = oldReg;
  int o, r = 1;
  for (o = 0; o < 4; o++)
  {
    if (o != oldReg)
      checkOrder[r++] = o;
  }

  for (o = 0; o < 4; o++)
  {
    r = checkOrder[o];

    // Test if the temp reg is free for the time we need it
    // (r0-r3 double as texture registers)
    bool isFree = true;
    
    for (size_t i = firstNeeded; i < lastNeeded; i++)
    {
      if (neededRegs[i] & TEXREG_BIT(r, usedBits))
      {
	isFree = false;
	break;
      }
    }

    if (isFree) 
    {
      newReg = r;
      break;
    }
  }

  if (newReg == -1)
  {
    bool realTempUsed[2] = {false, false};
    for (r = 0; r < 1; r++)
    {
      if (tempRegisterMap[r][0] != -1)
	realTempUsed[tempRegisterMap[r][0] - 4] = true;
      if (tempRegisterMap[r][1] != -1)
	realTempUsed[tempRegisterMap[r][1] - 4] = true;
    }
    for (r = 0; r < 1; r++)
    {
      bool isFree = true;

      if (realTempUsed[r])
      {
	uint bits = TEMPREG_BIT(/*tempRegisterMap[*/r/*]*/, usedBits);
	for (size_t i = firstNeeded; i < lastNeeded; i++)
	{
	  if (neededRegs[i] & bits)
	  {
	    isFree = false;
	    break;
	  }
	}

      }
      if (isFree) 
      {
	newReg = r + 4;
	break;
      }
    }
  }

  if (newReg == -1)
    return SetLastError ("(%zu): Could not find register to alias r%d",
      instrIndex, oldReg);

  if (usedBits & BIT_RGB) tempRegisterMap[oldReg][0] = newReg;
  if (usedBits & BIT_ALPHA) tempRegisterMap[oldReg][1] = newReg;
  if (usedBits & BIT_RGB) tempRegisterExpire[oldReg][0] = lastNeeded;
  if (usedBits & BIT_ALPHA) tempRegisterExpire[oldReg][1] = lastNeeded;

  return 0;
}

const char* csPS1xTo14Converter::GetTexTempReg (int oldReg,
  size_t /*instrIndex*/, int& newReg)
{
  newReg = oldReg;
  return 0;
}

const char* csPS1xTo14Converter::AddInstruction (
  const csPSProgramInstruction &instr, size_t instrIndex)
{
  switch (instr.instruction)
  {
    case CS_PS_INS_BEM:
    case CS_PS_INS_TEXCRD:
    case CS_PS_INS_TEXDEPTH:
    case CS_PS_INS_TEXLD:
      // PS1.4 only instructions
      return SetLastError ("PS1.4 only instruction '%s'(%zu)",
	GetInstructionName (instr.instruction), instrIndex);

    case CS_PS_INS_ADD:
    case CS_PS_INS_CMP:
    case CS_PS_INS_CND:
    case CS_PS_INS_DP3:
    case CS_PS_INS_DP4:
    case CS_PS_INS_LRP:
    case CS_PS_INS_MAD:
    case CS_PS_INS_MUL:
    case CS_PS_INS_MOV:
    case CS_PS_INS_NOP:
    case CS_PS_INS_SUB:
      // Arithmetic instructions 
      return AddArithmetic (instr, instrIndex);

    case CS_PS_INS_TEX:
      return AddTEX (instr, instrIndex);
    case CS_PS_INS_TEXCOORD:
      return AddTEXCOORD (instr, instrIndex);

    default:
      return SetLastError ("Instruction '%s'(%zu) not supported yet",
	GetInstructionName (instr.instruction), instrIndex);
  }

  return 0;
}

const char* csPS1xTo14Converter::AddArithmetic (
  const csPSProgramInstruction &instr, size_t instrIndex)
{
  csPSProgramInstruction newInstr (instr);
  const char* err;

  int i;
  for (i = 0; i < 3; i++)
  {
    if (newInstr.src_reg[i] == CS_PS_REG_TEX)
    {
      if ((err = GetTexTempReg (newInstr.src_reg_num[i], instrIndex, 
	newInstr.src_reg_num[i])) != 0)
      {
	return err;
      }
      newInstr.src_reg[i] = CS_PS_REG_TEMP;
    } 
    else if (newInstr.src_reg[i] == CS_PS_REG_TEMP)
    {
      if ((tempRegisterMap[newInstr.src_reg_num[i]][0] == -1) ||
	(tempRegisterMap[newInstr.src_reg_num[i]][1] == -1))
	return SetLastError ("%s(%zu): Temp register %d hasn't been "
	  "assigned yet", GetInstructionName (instr.instruction), 
	  instrIndex, newInstr.src_reg_num[i]);

      uint srcBits = SrcModBits (newInstr.src_reg_mods[i]);
      if (srcBits == BIT_RGBA)
      {
	srcBits = DestModBits (newInstr.dest_reg_mods);
      }
      switch (srcBits)
      {
	case BIT_RGB:
	  newInstr.src_reg_num[i] = 
	    tempRegisterMap[newInstr.src_reg_num[i]][0];
	  break;
	case BIT_ALPHA:
	  newInstr.src_reg_num[i] = 
	    tempRegisterMap[newInstr.src_reg_num[i]][1];
	  break;
	case BIT_RGBA:
	  CS_ASSERT(tempRegisterMap[newInstr.src_reg_num[i]][0] ==
	    tempRegisterMap[newInstr.src_reg_num[i]][1]);
	  newInstr.src_reg_num[i] = 
	    tempRegisterMap[newInstr.src_reg_num[i]][0];
	  break;
      }
    }
  }
  
  if (newInstr.dest_reg == CS_PS_REG_TEX)
  {
    if ((err = GetTexTempReg (newInstr.dest_reg_num, instrIndex, 
      newInstr.dest_reg_num)) != 0)
    {
      return err;
    }
    newInstr.dest_reg = CS_PS_REG_TEMP;
  } 
  else if (newInstr.dest_reg == CS_PS_REG_TEMP)
  {
    uint destBits = DestModBits (newInstr.dest_reg_mods);
    if ((err = GetTempReg (newInstr.dest_reg_num, instrIndex, destBits,
      newInstr.dest_reg_num)) != 0)
    {
      return err;
    }
  }

  // Check for no-ops
  if ((newInstr.instruction == CS_PS_INS_MOV) && 
    (newInstr.dest_reg == newInstr.src_reg[0]) &&
    (newInstr.dest_reg_num == newInstr.src_reg_num[0]) &&
    (newInstr.dest_reg_mods == CS_PS_WMASK_NONE) && 
    (newInstr.src_reg_mods[0] == CS_PS_RMOD_NONE) &&
    (newInstr.inst_mods == 0))
    return 0;

  newInstructions.Push (newInstr);

  return 0;
}

const char* csPS1xTo14Converter::AddTEX (const csPSProgramInstruction &instr, 
					 size_t instrIndex)
{
  if (instr.dest_reg != CS_PS_REG_TEX)
  {
    return SetLastError ("%s (%zu): Destination is not a texture register",
      GetInstructionName (instr.instruction), instrIndex);
  }

  csPSProgramInstruction newInstr;

  newInstr.instruction = CS_PS_INS_TEXLD;
  newInstr.inst_mods = instr.inst_mods;
  newInstr.dest_reg = CS_PS_REG_TEMP;
  newInstr.dest_reg_num = instr.dest_reg_num;
  newInstr.dest_reg_mods = instr.dest_reg_mods;
  newInstr.src_reg[0] = CS_PS_REG_TEX;
  newInstr.src_reg_num[0] = instr.dest_reg_num;
  newInstr.src_reg_mods[0] = instr.src_reg_mods[0];

  newInstructions.Insert (texInsertPos, newInstr);
  texInsertPos++;

  return 0;
}

const char* csPS1xTo14Converter::AddTEXCOORD (const csPSProgramInstruction &instr, 
					      size_t instrIndex)
{
  if (instr.dest_reg != CS_PS_REG_TEX)
  {
    return SetLastError ("%s (%zu): Destination is not a texture register",
      GetInstructionName (instr.instruction), instrIndex);
  }

  csPSProgramInstruction newInstr;

  newInstr.instruction = CS_PS_INS_TEXCRD;
  newInstr.inst_mods = instr.inst_mods;
  newInstr.dest_reg = CS_PS_REG_TEMP;
  newInstr.dest_reg_num = instr.dest_reg_num;
  newInstr.dest_reg_mods = instr.dest_reg_mods;
  newInstr.src_reg[0] = CS_PS_REG_TEX;
  newInstr.src_reg_num[0] = instr.dest_reg_num;
  newInstr.src_reg_mods[0] = instr.src_reg_mods[0];

  newInstructions.Insert (texInsertPos, newInstr);
  texInsertPos++;

  // TEXCOORD clamps, emulate that
  newInstr.instruction = CS_PS_INS_MOV;
  newInstr.inst_mods = instr.inst_mods | CS_PS_IMOD_SAT;
  newInstr.dest_reg = CS_PS_REG_TEMP;
  newInstr.dest_reg_num = instr.dest_reg_num;
  newInstr.dest_reg_mods = instr.dest_reg_mods;
  newInstr.src_reg[0] = CS_PS_REG_TEMP;
  newInstr.src_reg_num[0] = instr.dest_reg_num;
  newInstr.src_reg_mods[0] = CS_PS_RMOD_NONE;

  newInstructions.Push (newInstr);

  return 0;
}

const char* csPS1xTo14Converter::CollectUsage (
  const csArray<csPSProgramInstruction>*& instrs)
{
  /*
    Collect what registers are 'used' at every line in the PS.
    A register is 'used' if it was written to and later read from.
   */

  size_t i;
  uint currentBits = 0;
  size_t lastTempUse[2][2] = {
    {(size_t)-1, (size_t)-1}, 
    {(size_t)-1, (size_t)-1}
  };
  size_t lastTexUse[4][2] = {
    {(size_t)-1, (size_t)-1}, 
    {(size_t)-1, (size_t)-1}, 
    {(size_t)-1, (size_t)-1}, 
    {(size_t)-1, (size_t)-1}
  };

  /* 
    @@@ DP3 and DP4 are executed in both the RGB and A pipe... 
    that prolly needs to be reflected somehow.
   */

  for (i = 0; i < instrs->GetSize (); i++)
  {
    const csPSProgramInstruction& instr = instrs->Get (i);
    int j;

    uint destMods = DestModBits (instr.dest_reg_mods);
    const int dr = instr.dest_reg_num;
    uint nextBits = currentBits;
    if (instr.dest_reg == CS_PS_REG_TEMP)
    {
      uint bit = TEMPREG_BIT (dr, destMods);
      currentBits &= ~bit;
      nextBits |= bit;
    }
    else if (instr.dest_reg == CS_PS_REG_TEX)
    {
      uint bit = TEXREG_BIT (instr.dest_reg_num, destMods);
      currentBits &= ~bit;
      nextBits |= bit;
    }

    for (j = 0; j < 3; j++)
    {
      const int sr = instr.src_reg_num[j];
      if (instr.src_reg[j] == CS_PS_REG_TEMP)
      {
        /* If the destination has a write mask and the result directly depends
         * on the source registers, we can treat some source components as 
         * "not read". */
        uint srcMods = SrcModBits (instr.src_reg_mods[j],
          (instrFlags[instr.instruction] 
            & CS_PS_DEST_COMP_DEPENDS_SOURCE) ? destMods : BIT_RGBA);
	currentBits |= TEMPREG_BIT (sr, srcMods);
        if (srcMods & BIT_RGB) lastTempUse[sr][0] = i;
        if (srcMods & BIT_ALPHA) lastTempUse[sr][1] = i;
      }
      else if (instr.src_reg[j] == CS_PS_REG_TEX)
      {
        uint srcMods = SrcModBits (instr.src_reg_mods[j],
          (instrFlags[instr.instruction] 
            & CS_PS_DEST_COMP_DEPENDS_SOURCE) ? destMods : BIT_RGBA);
	currentBits |= TEXREG_BIT (sr, srcMods);
	if (srcMods & BIT_RGB) lastTexUse[sr][0] = i;
	if (srcMods & BIT_ALPHA) lastTexUse[sr][1] = i;
      }
      else if (instr.src_reg[j] == CS_PS_REG_NONE)
	break;
    }

    if (instr.dest_reg == CS_PS_REG_TEMP)
    {
      // Register is free up to the last instr it was read.
      for (int m = 0; m < 2; m++)
      {
        const uint bit = 1 << m;
        if (destMods & bit)
        {
          const uint mask = ~TEMPREG_BIT (dr, bit);
          size_t j = i;
          while (--j != lastTempUse[dr][m]) neededRegs[j] &= mask;
        }
      }
    }

    neededRegs.Push (currentBits);
    currentBits = nextBits;
  }

  // Clear out those that have been written to, but are not read from.
  for (int m = 0; m < 2; m++)
  {
    const uint bit = 1 << m;
    int reg;
    for (reg = 1; reg < 2; reg++)
    {
      const uint mask = ~TEMPREG_BIT (reg, bit);
      size_t j = instrs->GetSize ();
      while (--j != lastTempUse[reg][m]) neededRegs[j] &= mask;
    }				  
    for (reg = 0; reg < 4; reg++)
    {
      const uint mask = ~TEXREG_BIT (reg, bit);
      size_t j = instrs->GetSize ();
      while (--j != lastTexUse[reg][m]) neededRegs[j] &= mask;
    }
  }

  return 0;
}

const char* csPS1xTo14Converter::GetNewInstructions (
  const csArray<csPSProgramInstruction>*& instrs)
{
  ResetState();

  const char* err;
  if ((err = CollectUsage (instrs)) != 0)
    return err;

  for (size_t i = 0; i < instrs->GetSize (); i++)
  {
    if ((err = AddInstruction (instrs->Get (i), i)) != 0)
      return err;
  }

  if ((tempRegisterMap[0][0] == -1) ||
    (tempRegisterMap[0][1] == -1))
  {
    char writeLack[5] = "";
    if (tempRegisterMap[0][0] == -1) strcat (writeLack, "rgb");
    if (tempRegisterMap[0][1] == -1) strcat (writeLack, "a");
    return SetLastError ("r0.%s isn't written to!", writeLack);
  }

  int moveBits = 0;
  if (tempRegisterMap[0][0] != 0) moveBits |= BIT_RGB;
  if (tempRegisterMap[0][1] != 0) moveBits |= BIT_ALPHA;
  if (moveBits != 0)
  {
    csPSProgramInstruction newInstr;

    newInstr.instruction = CS_PS_INS_MOV;
    newInstr.dest_reg = CS_PS_REG_TEMP;
    newInstr.dest_reg_num = 0;
    newInstr.src_reg[0] = CS_PS_REG_TEMP;

    switch (moveBits)
    {
      case BIT_RGB:
	newInstr.src_reg_num[0] = tempRegisterMap[0][0];
	newInstr.dest_reg_mods = CS_PS_WMASK_RED | CS_PS_WMASK_GREEN |
	  CS_PS_WMASK_BLUE;
	break;
      case BIT_ALPHA:
	newInstr.src_reg_num[0] = tempRegisterMap[0][1];
	newInstr.dest_reg_mods = CS_PS_WMASK_ALPHA;
	break;
      case BIT_RGBA:
	newInstr.src_reg_num[0] = tempRegisterMap[0][0];
	break;
    }

    newInstructions.Push (newInstr);
  }

  instrs = &newInstructions;
  return 0;
}

}
CS_PLUGIN_NAMESPACE_END(GLShaderPS1)
