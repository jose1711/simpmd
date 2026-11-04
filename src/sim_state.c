/** Saved state of the simulated machine.
 *
 *  A state is the processor registers, the output latches of the system 8255, the keyboard column latch,
 *  the processor clock and the 64k of memory. The tape position and the keys held on the host are not saved
 *  (a game is loaded in memory already, the keys are read from the host again).
 *
 *  File: "PMD85SAV", version, model, processor state (little endian), 65536 bytes of memory.
 */

#include "sim_common.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define STATE_MAGIC     "PMD85SAV"
#define STATE_VERSION   1
#define STATE_SIZE      (8 + 4 + 4 + 2 + 2 + 7 + 1 + 1 + 1 + 4 + 65536)
/// The PMD 85-2A (AllRAM) adds the flags of the paging and the 4 kB of RAM under the monitor ROM.
#define STATE_ALLRAM    (1 + 4096)


static void Put16 (byte *&p, uint32 v) { *p ++ = v & 0xFF; *p ++ = (v >> 8) & 0xFF; }
static void Put32 (byte *&p, uint32 v) { Put16 (p, v & 0xFFFF); Put16 (p, v >> 16); }
static uint32 Get16 (const byte *&p) { uint32 v = p [0] | (p [1] << 8); p += 2; return (v); }
static uint32 Get32 (const byte *&p) { uint32 v = Get16 (p); return (v | (Get16 (p) << 16)); }


/** Save the state to a file, the processor is stopped for a moment.
 *
 *  @arg sPath File name, the directory is created.
 *  @arg iModel PMD 85 model of the running game.
 *  @return True when saved.
 */
bool STSave (const std::string &sPath, int iModel)
{
  bool bAllRam = CPUAllRamActive ();
  std::vector <byte> oBuffer (STATE_SIZE + (bAllRam ? STATE_ALLRAM : 0));
  byte *p = &oBuffer [0];
  tCPUState sCPU;

  if (!CPUPause ())
    return (false);
  CPUGetState (&sCPU);
  byte iColumn = KBDReadColumn ();

  memcpy (p, STATE_MAGIC, 8);
  p += 8;
  Put32 (p, STATE_VERSION);
  Put32 (p, iModel);
  Put16 (p, sCPU.wPC);
  Put16 (p, sCPU.wSP);
  *p ++ = sCPU.bA; *p ++ = sCPU.bB; *p ++ = sCPU.bC; *p ++ = sCPU.bD;
  *p ++ = sCPU.bE; *p ++ = sCPU.bH; *p ++ = sCPU.bL;
  *p ++ = sCPU.bFlags;
  *p ++ = sCPU.bPortC;
  *p ++ = iColumn;
  Put32 (p, sCPU.iClock);
  memcpy (p, abMemoryData, 65536);
  p += 65536;
  if (bAllRam)
    CPUAllRamSave (p, p + 1);
  CPUUnpause ();

  // The directory of the file (all levels, like mkdir -p), then write under a temporary name and rename, a failed save keeps
  // the old state.
  size_t iSlash = sPath.rfind ('/');
  if (iSlash != std::string::npos)
    for (size_t iPos = sPath.find ('/', 1); iPos != std::string::npos && iPos <= iSlash; iPos = sPath.find ('/', iPos + 1))
      mkdir (sPath.substr (0, iPos).c_str (), 0755);
  std::string sTemp = sPath + ".tmp";
  FILE *pFile = fopen (sTemp.c_str (), "wb");
  if (!pFile)
    return (false);
  bool bOk = fwrite (&oBuffer [0], 1, oBuffer.size (), pFile) == oBuffer.size ();
  bOk = fclose (pFile) == 0 && bOk;
  if (!bOk || rename (sTemp.c_str (), sPath.c_str ()) != 0)
  {
    remove (sTemp.c_str ());
    return (false);
  }
  return (true);
}


/** Load a state from a file, nothing changes when the file is not usable.
 *
 *  @arg sPath File name.
 *  @arg iModel PMD 85 model of the running game, a state of another model is refused.
 *  @return NULL when loaded, otherwise a short reason.
 */
const char *STLoad (const std::string &sPath, int iModel)
{
  bool bAllRam = CPUAllRamActive ();
  size_t iExpected = STATE_SIZE + (bAllRam ? STATE_ALLRAM : 0);
  std::vector <byte> oBuffer (iExpected + 1);
  FILE *pFile = fopen (sPath.c_str (), "rb");
  if (!pFile)
    return ("no saved state");
  size_t iRead = fread (&oBuffer [0], 1, oBuffer.size (), pFile);
  fclose (pFile);
  if (iRead != iExpected || memcmp (&oBuffer [0], STATE_MAGIC, 8) != 0)
    return ("not a saved state");

  const byte *p = &oBuffer [8];
  if (Get32 (p) != STATE_VERSION)
    return ("saved by another version");
  if ((int) Get32 (p) != iModel)
    return ("saved for another model");
  tCPUState sCPU;
  sCPU.wPC = Get16 (p);
  sCPU.wSP = Get16 (p);
  sCPU.bA = *p ++; sCPU.bB = *p ++; sCPU.bC = *p ++; sCPU.bD = *p ++;
  sCPU.bE = *p ++; sCPU.bH = *p ++; sCPU.bL = *p ++;
  sCPU.bFlags = *p ++;
  sCPU.bPortC = *p ++;
  byte iColumn = *p ++;
  sCPU.iClock = (int) Get32 (p);

  if (!CPUPause ())
    return ("the processor did not stop");
  // The paging of the PMD 85-2A first (the view of 0x8000-0x8FFF is the ROM or the RAM), then the memory.
  if (bAllRam)
    CPUAllRamRestore (p [65536], p + 65537);
  // The read only memory (monitor) is left as it is, the ROM mapped by the AllRAM paging too (mask 2).
  for (int iAddr = 0 ; iAddr < 65536 ; iAddr ++)
    if (abMemoryMask [iAddr] == 1)
      abMemoryData [iAddr] = p [iAddr];
  CPUSetState (&sCPU);
  KBDWriteColumn (iColumn);
  // The sound continues from the new clock, the speaker at the level of the saved port C.
  SNDSynchronize ();
  SNDWriteSpeaker (sCPU.bPortC);
  DSPInvalidate ();
  CPUUnpause ();
  return (NULL);
}
