/*

SIM_TAPE.C

Copyright 2008 Petr Tuma

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

*/

/* MODIFIED

 -Modified:

 bool TAPNextInputFile (std::vector <std::string> oArgTapeInputs)
 bool TAPNextOutputFile (std::vector <std::string> oArgTapeOutputs)
 bool TAPInputFileStatus ()
 bool TAPOutputFileStatus ()

*/

#include "sim_common.h"

#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>


//--------------------------------------------------------------------------
// Command Line Options

/// Tape input names. What files to open as tape inputs.
static std::vector <std::string> oArgTapeInputs;
/// Tape output names. What files to open as tape outputs.
static std::vector <std::string> oArgTapeOutputs;

/// Time tape input. Whether to simulate timing of the tape input.
static bool bArgTimeTapeInput = false;
/// Time tape output. Whether to simulate timing of the tape output.
static bool bArgTimeTapeOutput = false;

/// Tape rate. What tape rate to assume when simulating timing.
static int iArgTapeRate = 2400;


//--------------------------------------------------------------------------
// Data

/// Number of bits in byte on tape.
#define PMD_TAP_BITS    10

/// Mask for status TxRDY bit.
#define PMD_TAP_TXRDY_MASK        (1 << 0)
/// Mask for status RxRDY bit.
#define PMD_TAP_RXRDY_MASK        (1 << 1)
/// Mask for status TxEMPTY bit.
#define PMD_TAP_TXEMPTY_MASK      (1 << 2)

/// Tape input file stream.
static std::ifstream oTapeInput;
/// Tape output file stream.
static std::ofstream oTapeOutput;

/// Last value of simulated clock at input.
static int iLastInputClock;
/// Time of the last byte read from the tape in ms (SDL ticks + 1), 0 if nothing was read yet.
static std::atomic <Uint32> iLastInputTime (0);
/// Last value of simulated clock at output.
static int iLastOutputClock;

/// File of the tape that keeps what the program saves ("TAPESAVE" in the note of the game), empty: the output is dropped.
static std::string sSavePath;
/// File of the tape of the game that is read again from its start when the program asks for more data after the
/// end ("TAPELOOP" in the note of the game: Lemmings, levels loaded by name), empty: no rewinding.
static std::string sLoopPath;
/// Time (SDL ticks) of the last try to open the save file for reading.
static Uint32 iSaveTry = 0;


//--------------------------------------------------------------------------
// File operations

/** Advance to next file.
 *
 * @arg oStream Stream to advance.
 * @arg oNames Names list to use.
 * @return Indicates whether there was a next file.
 */
template <class tStream> bool TAPNextFile (tStream &oStream, std::vector <std::string> &oNames)
{
  // Close current file if any.
  if (oStream.is_open ()) oStream.close ();

  // Open next file if any.
  if (oNames.empty ()) return (false);

  oStream.clear ();
  oStream.open (oNames.front ());
  //oNames.erase (oNames.begin ());

  return (true);
}

//bool TAPNextInputFile  () { return (TAPNextFile <std::ifstream> (oTapeInput, oArgTapeInputs)); }
bool TAPNextInputFile  (std::vector <std::string> oArgTapeInputs) { return (TAPNextFile <std::ifstream> (oTapeInput, oArgTapeInputs)); }
//bool TAPNextOutputFile () { return (TAPNextFile <std::ofstream> (oTapeOutput, oArgTapeOutputs)); }
bool TAPNextOutputFile (std::vector <std::string> oArgTapeOutputs) { return (TAPNextFile <std::ofstream> (oTapeOutput, oArgTapeOutputs)); }


/// "TAPESIGNAL" of the game (see the signal of the tape below).
static bool bSignalOn = false;
/// The input is the saved tape (not the tape of the game).
static bool bSaveStream = false;
/// A key was pressed since the saved tape was last started: a program with "TAPESIGNAL" reads the saved tape once and
/// then finds its end, the next Load (the keys the user presses) starts it from the beginning.
static bool bSaveRewind = false;

/** A key of the keyboard was pressed. */
void TAPKeyPressed ()
{
  bSaveRewind = true;
}

/// "TAPEAUTOREWIND" of the game: the tape of the game (with the share path), empty without the word.
static std::string sRewindPath;
static bool bAutoRewind = false;

/** The tape of the game from its start (the cassette was rewound), true when it has data. */
static bool TAPRewindGame ()
{
  if (oTapeInput.is_open ()) oTapeInput.close ();
  oTapeInput.clear ();
  oTapeInput.open (sRewindPath.c_str (), std::ios::binary);
  bSaveStream = false;
  return (oTapeInput.is_open () && oTapeInput.peek () != EOF);
}

/** Opens the saved tape from its start, true when it has data. */
static bool TAPOpenSaved ()
{
  // What the program has written must be in the file before it is read.
  // The output is closed: the next save starts a new file (a save replaces the old tape, it is not appended).
  if (oTapeOutput.is_open ()) oTapeOutput.close ();
  if (oTapeInput.is_open ()) oTapeInput.close ();
  oTapeInput.clear ();
  oTapeInput.open (sSavePath.c_str (), std::ios::binary);
  bSaveStream = oTapeInput.is_open ();
  bSaveRewind = false;
  return (oTapeInput.is_open () && oTapeInput.peek () != EOF);
}


/** Check input file status.
 *
 * Advances to next input file as necessary.
 *
 * @return Indicates readiness of tape input file.
 */
bool TAPInputFileStatus ()
{
  // A new Load of a program with "TAPESIGNAL" (a key was pressed): the saved tape starts again.
  if (bSignalOn && bSaveStream && bSaveRewind && !sSavePath.empty ())
    TAPOpenSaved ();
  // We need to peek to know if there will be data.
  // Stream end of file flag is set only on access.
  while (true)
  {
    if (oTapeInput.peek () != EOF) return (true);
    if (!sSavePath.empty ())
    {
      // The saved tape is read to its end: no rewinding without a key of the user (the program of "TAPESIGNAL"
      // looks for the name once, a cassette comes to its end).
      if (bSignalOn && bSaveStream)
        return (false);
      // "TAPEAUTOREWIND": the saved tape is at its end too (or there is none): the tape of the game starts again.
      if (bAutoRewind && bSaveStream)
      {
        if (!TAPRewindGame ())
          return (false);
        continue;
      }
      // The tape of the game is read to its end: the saved tape follows, from its start (as a rewound cassette),
      // and again from the start when the program has read it all and asks for data once more. Looking for the
      // file is limited, the program polls the status very often.
      Uint32 iNow = SDL_GetTicks ();
      if (iNow - iSaveTry < 200)
        return (false);
      iSaveTry = iNow;
      if (!TAPOpenSaved ())
      {
        // No saved tape, or it is empty: with "TAPEAUTOREWIND" the tape of the game starts again.
        if (bAutoRewind && TAPRewindGame ())
          continue;
        return (false);
      }
      iSaveTry = 0;
      continue;
    }
    if (!sLoopPath.empty ())
    {
      // A rewound cassette: the program looks for a block by its name and has passed it.
      if (oTapeInput.is_open ()) oTapeInput.close ();
      oTapeInput.clear ();
      oTapeInput.open (sLoopPath.c_str (), std::ios::binary);
      if (!oTapeInput.is_open () || oTapeInput.peek () == EOF)
        return (false);
      continue;
    }
    //if (!TAPNextInputFile ()) return (false);
    if (!TAPNextInputFile (oArgTapeInputs)) return (false);
  }
}


/** Check output file status.
 *
 * Advances to next output file as necessary.
 *
 * @return Indicates readiness of tape output file.
 */
bool TAPOutputFileStatus ()
{
  // Suffering senseless symmetry :-) ...
  while (true)
  {
    if (oTapeOutput.good ()) return (true);
    //if (!TAPNextOutputFile ()) return (false);
    if (!TAPNextOutputFile (oArgTapeOutputs)) return (false);
  }
}


//--------------------------------------------------------------------------
// Serial operations

/** Check if we should indicate RxRDY.
 */
bool TAPStatusRxRDY ()
{
  if (bArgTimeTapeInput)
  {
    // If the tape input timing is to be simulated,
    // data reads have certain speed.

    int iTimeDelta = iProcessorClock - iLastInputClock;
    int iCharDelta = PMD_TAP_BITS * PMD_CLOCK / iArgTapeRate;

    if (iTimeDelta < iCharDelta) return (false);
  }

  // If timing is fine return input stream status.
  return (TAPInputFileStatus ());
}


/** Check if we should indicate TxRDY.
 */
bool TAPStatusTxRDY ()
{
  // Without an output tape a written byte is dropped, but the character time still has to pass: games
  // (HLIPA) take the TxRDY bit as their timer.
  bool bNoOutput = !oTapeOutput.is_open () && oArgTapeOutputs.empty () && sSavePath.empty ();
  if (bArgTimeTapeOutput || bNoOutput)
  {
    // If the tape output timing is to be simulated,
    // data writes have certain speed.

    int iTimeDelta = iProcessorClock - iLastOutputClock;
    int iCharDelta = PMD_TAP_BITS * PMD_CLOCK / iArgTapeRate;

    if (iTimeDelta < iCharDelta) return (false);
  }

  // If timing is fine return output stream status.
  if (bNoOutput) return (true);
  return (TAPOutputFileStatus ());
}



//--------------------------------------------------------------------------
// Tape signal on the DSR input (bit 7 of the status register)

/// "TAPESIGNAL" in the note: the status bit 7 carries the signal of the tape, as a frequency modulated tone made from
/// the bytes of the input tape (a 1 is a fast tone, a 0 a slow one). A program that measures the tape signal itself
/// before it reads the bytes (DAT-PMD: the ROM routine 0x8890 counts the time to the next change of the bit) gets what
/// it expects; the tape runs in the time of the processor while the program looks at the bit and the bytes of the leader
/// pass by. The bytes are read from the USART as before.
// (bSignalOn is above)
/// The signal is running: the clock the current bit ends, the clock of the next change, the position in the frame
/// (0 start bit, 1-8 data bits, 9 stop bit), the level, the clock of the last look at the bit.
static bool bSignalRun = false;
static long long iSignalBitEnd = 0, iSignalNextToggle = 0, iSignalLast = 0;
static int iSignalIndex = 0, iSignalLevel = 1;
static long long iSignalHalf1 = 0, iSignalHalf0 = 0;

void TAPSetSignal (bool bOn)
{
  bSignalOn = bOn;
  bSignalRun = false;
  const char *pH1 = getenv ("PMD_DSR_H1");
  const char *pH0 = getenv ("PMD_DSR_H0");
  iSignalHalf1 = pH1 ? atoi (pH1) : PMD_CLOCK / iArgTapeRate / 2;
  iSignalHalf0 = pH0 ? atoi (pH0) : PMD_CLOCK / iArgTapeRate;
}

/// The bit of the frame of the byte that is next on the tape: start 0, the data bits from the lowest, stop 1.
static int TAPSignalBit (int iByte, int iIndex)
{
  if (iIndex == 0) return (0);
  if (iIndex <= 8) return ((iByte >> (iIndex - 1)) & 1);
  return (1);
}

/// Level of the signal now.
static int TAPSignalLevel ()
{
  long long iNow = iProcessorClock;
  const long long iBit = PMD_CLOCK / iArgTapeRate;
  if (!bSignalRun || iNow - iSignalLast > 40000)
  {
    // The program has not looked for a while (the tape stands): the signal starts again from the next byte.
    bSignalRun = true;
    iSignalIndex = 0;
    iSignalBitEnd = iNow + iBit;
    iSignalNextToggle = iNow + iSignalHalf0;
  }
  iSignalLast = iNow;
  for (int iGuard = 0 ; iGuard < 4000 ; iGuard ++)
  {
    if (!TAPInputFileStatus ())
      return (1);
    int iByte = oTapeInput.peek ();
    int iBitValue = TAPSignalBit (iByte, iSignalIndex);
    long long iEvent = iSignalNextToggle < iSignalBitEnd ? iSignalNextToggle : iSignalBitEnd;
    if (iEvent > iNow)
      break;
    if (iSignalNextToggle <= iSignalBitEnd)
    {
      iSignalLevel ^= 1;
      iSignalNextToggle += iBitValue ? iSignalHalf1 : iSignalHalf0;
    }
    else
    {
      iSignalBitEnd += iBit;
      if (++ iSignalIndex >= PMD_TAP_BITS)
      {
        iSignalIndex = 0;
        oTapeInput.get ();
      }
    }
  }
  return (iSignalLevel);
}

//--------------------------------------------------------------------------
// Port operations

/** Read from tape data register.
 */
byte TAPReadData ()
{
  if (TAPStatusRxRDY ())
  {
    iLastInputClock = iProcessorClock;
    iLastInputTime = SDL_GetTicks () + 1;
    bSignalRun = false;
    int iByte = oTapeInput.get ();
    static const bool bTrace = getenv ("PMD_TAPETRACE") != NULL;
    if (bTrace)
      fprintf (stderr, "TAPREAD %02x clock %d\n", iByte & 0xFF, (int) iProcessorClock);
    return (iByte);
  }
  return (0);
}


/** Write to tape data register.
 *
 *  @arg iData The written value.
 */
void TAPWriteData (byte bData)
{
  if (TAPStatusTxRDY ())
  {
    iLastOutputClock = iProcessorClock;
    if (!sSavePath.empty () && !oTapeOutput.is_open ())
    {
      // The first byte of a save: a new tape replaces the old one.
      size_t iSlash = sSavePath.rfind ('/');
      if (iSlash != std::string::npos)
        for (size_t iPos = sSavePath.find ('/', 1); iPos != std::string::npos && iPos <= iSlash; iPos = sSavePath.find ('/', iPos + 1))
          mkdir (sSavePath.substr (0, iPos).c_str (), 0755);  // all levels, like mkdir -p
      oTapeOutput.clear ();
      oTapeOutput.open (sSavePath.c_str (), std::ios::binary | std::ios::trunc);
      // The saved tape being read is the old one: the next Load opens the new file from its start.
      if (bSaveStream && oTapeInput.is_open ())
        oTapeInput.close ();
    }
    if (oTapeOutput.is_open () || !oArgTapeOutputs.empty ())
    {
      oTapeOutput.put (bData);
      // A tape runs at 240 bytes/s: flushing every byte is cheap and the save survives a power off of the Pi.
      if (!sSavePath.empty ())
        oTapeOutput.flush ();
    }
  }
}


/** Read from tape status register.
 */
byte TAPReadStatus ()
{
  // TxEMPTY follows TxRDY, a character is sent at once (FLAPPY2K waits for it before it goes on).
  bool bTxRdy = TAPStatusTxRDY ();
  return (bTxRdy * PMD_TAP_TXRDY_MASK |
          TAPStatusRxRDY () * PMD_TAP_RXRDY_MASK |
          bTxRdy * PMD_TAP_TXEMPTY_MASK |
          (bSignalOn ? TAPSignalLevel () << 7 : 0));
}


//--------------------------------------------------------------------------
// Initialization and shutdown


void TAPInitialize ()
{
  iLastInputTime = 0;
}


void TAPSetSaveFile (const std::string &sPath)
{
  sSavePath = sPath;
  iSaveTry = 0;
  // Nothing of the previous game stays: its saved tape is not the input, its output file is not written any more.
  bSaveStream = false;
  bSaveRewind = false;
  if (oTapeOutput.is_open ()) oTapeOutput.close ();
}


void TAPSetAutoRewind (const std::string &sPath)
{
  sRewindPath = sPath;
  bAutoRewind = !sPath.empty ();
  // Without a saved tape (no "TAPESAVE") the existing rewind of "TAPELOOP" does it.
  if (bAutoRewind && sLoopPath.empty ())
    sLoopPath = sPath;
}


void TAPSetLoopFile (const std::string &sPath)
{
  sLoopPath = sPath;
}


Uint32 TAPLastRead ()
{
  return (iLastInputTime);
}


void TAPShutdown ()
{
  // Close tape input and tape output.
  if (oTapeInput.is_open ())  oTapeInput.close ();
  if (oTapeOutput.is_open ()) oTapeOutput.close ();
}


//--------------------------------------------------------------------------

