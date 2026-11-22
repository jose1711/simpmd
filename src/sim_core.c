/*

SIM_CORE.C

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

 -Added:
 
 static void FlushMemory (int iFrom, int iSize)
 static void EmulationInitialize (int gameIndex)
 static void EmulationShutdown ()

 -Heavily modified main loop.

*/

#include "sim_common.h"
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <strings.h>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>

//--------------------------------------------------------------------------
// System Status Variables

// If we were to be entirely correct, the memory arrays
// should be declared volatile. However, this just
// leads to more complex display code and is
// otherwise unlikely to change anything.

/// Simulated memory array.
byte abMemoryData [65536] __attribute__ ((aligned (SAFE_ALIGNMENT)));
/// Simulator memory mask.
/// Distinguishes readable and writable memory.
byte abMemoryMask [65536] __attribute__ ((aligned (SAFE_ALIGNMENT)));

/// Processor clock counter.
/// Note that access to clock is not ordered !!!
relaxed_int iProcessorClock (0);

/// Simulator shutdown flag.
static relaxed_bool bShutdown (false);

/// Predefined Games
static std::string gameSelect[4] = {
  PMD_SHARE "FLAPPY",
  PMD_SHARE "BOULDER",
  PMD_SHARE "MANIC+",
  PMD_SHARE "FRED"
};

/// Game Tape IN
std::vector <std::string> gameIn;
/// Screen resolution
SDL_DisplayMode fullscreen;
/// Menu
bool inMenu = true;
/// Game Index
int gameIndex = 0;

/// Game from the games list (games.lst in the share directory).
struct tGame
{
  /// Tape file name.
  std::string sTape;
  /// PMD 85 model, 1, 2 or 3 (the PMD 85-2A, with the AllRAM paging).
  int iModel;
  /// Note shown in the list, the commands to type.
  std::string sNote;
  /// Keys of the game remapped to other PMD 85 keys, "Left=Left Shift,Right=V" (SDL key names).
  std::string sRemap;
  /// Buttons of the web remote control, "Label:key,..." (SDL key names), empty for the default set.
  std::string sPad;
  /// Controls shown under the list for the selected game, free text.
  std::string sHelp;
  /// Devices the game can be played with, besides the keyboard: P gamepad, R TV remote control (HDMI-CEC).
  std::string sInput;
  /// Category (a sub list of the games list), empty for the games of the main list.
  std::string sCategory;
};
/// Buttons of the web remote control for the running game and in the menus.
static const char *pPadGame = "E up:e,Y down:z,Left:Left,Right:Right,Space:Space,Enter:Return,H:h,I:i,Shift:Left Shift,RCL:Keypad +,Esc:Escape";
static const char *pPadMenu = "E up:e,Y down:z,Left:Left,Right:Right,Enter:Return,Esc:Escape";
static std::string sPad = pPadMenu;
/// Keys remapped for the running game, host key to PMD 85 key.
static std::map <SDL_Keycode, SDL_Keycode> oRemap;
/// Host keys whose target is pressed together with the cursor key last used (REMAP "Up=@Left Shift", a jump in the direction of
/// the walk for a remote control, which can not press two keys at once).
static std::set <SDL_Keycode> oRemapDirection;
/// How long after the release of a cursor key it still counts as the direction of the walk [ms].
#define PMD_DIRECTION_MS        400

/// Keys with a short and a long press, REMAP "Return=Return|A|@Left Shift": the PMD 85 key of the short press, of the long
/// press and (optional) a key held as long as the host key is down, "@" adds the cursor key last used (a jump in the direction
/// of the walk). The short key is sent when the host key is released (a long press must not send it first).
struct tLongKey
{
  SDL_Keycode iShort, iLong, iHeld;
  bool bHeldDirection;
};
static std::map <SDL_Keycode, tLongKey> oRemapLong;
/// The cursor key pressed together with the held key, until the host key is released.
static SDL_Keycode iLongDirection = SDLK_UNKNOWN;
/// Hold time of a long press [ms].
#define PMD_LONG_MS             450
/// Own event type of the timer of a long press (SDL_USEREVENT is the paint timer, +1 the hold of Esc).
static const Uint32 iLongEvent = SDL_USEREVENT + 2;
/// Own event type of the idle tick: wakes the event loop every 2 s, so that the periodic work (the rescan of the gamepads)
/// runs in the menus too, where no other event arrives while nobody touches anything.
static const Uint32 iTickEvent = SDL_USEREVENT + 7;
#define PMD_TICK_MS             2000
static int iLongSerial = 0;
static SDL_Keycode iLongKey = SDLK_UNKNOWN;
static bool bLongDown = false, bLongFired = false;

static Uint32 LongTimerCallback (Uint32 iInterval, void *pSerial)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iLongEvent;
  sEvent.user.code = (Sint32) (intptr_t) pSerial;
  SDL_PushEvent (&sEvent);
  return (0);
}

/// Own event type of the release of the key of a short press, the key is held for PMD_TAP_MS (a slow keyboard scan of a game).
static const Uint32 iTapEvent = SDL_USEREVENT + 3;
#define PMD_TAP_MS              150

static Uint32 TapTimerCallback (Uint32 iInterval, void *pKey)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iTapEvent;
  sEvent.user.code = (Sint32) (intptr_t) pKey;
  SDL_PushEvent (&sEvent);
  return (0);
}

/// Send a key to the simulated keyboard.
static void KeySend (SDL_Keycode iSym, Uint8 iState)
{
  SDL_KeyboardEvent sKey;
  SDL_zero (sKey);
  sKey.type = iState == SDL_PRESSED ? SDL_KEYDOWN : SDL_KEYUP;
  sKey.state = iState;
  sKey.keysym.sym = iSym;
  KBDEventHandler (&sKey);
}

/** [Ctrl] is the STOP key of the PMD 85 (a game or the monitor stops at it), but it is also the modifier of the
 *  keys of the saved state (Ctrl+F5, Ctrl+F9). The [Ctrl] of the host goes to the simulated keyboard only when
 *  PMD_CTRL_MS pass without F5 or F9 (or the key is released sooner); with F5 or F9 it does not go at all.
 */
#define PMD_CTRL_MS             120
static const Uint32 iCtrlEvent = SDL_USEREVENT + 4;
static int iCtrlSerial = 0;
static bool bCtrlPending = false, bCtrlSent = false, bCtrlSwallow = false;
static SDL_Keycode iCtrlSym = SDLK_LCTRL;

static Uint32 CtrlTimerCallback (Uint32 iInterval, void *pSerial)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iCtrlEvent;
  sEvent.user.code = (Sint32) (intptr_t) pSerial;
  SDL_PushEvent (&sEvent);
  return (0);
}
/// Keys typed on the simulated keyboard after the start of a game (the load command, PMD_KEYS in test mode).
static std::string sKeys;
/// Position in the keys, key press or release.
static int iKeyPos;
/// The minimal hold time of the keys of the game (KEYHOLD, PMD_KBD_HOLD_MS) is set when the autoload has typed its text: a long hold
/// would blend the typed keys into each other (the commands of the monitor would be garbled).
static int iGameKeyHold = 60;
static bool bGameKeyHoldSet = true;
static bool bKeyDown;
/// Time of the next key event.
static Uint32 iKeyTime;
/// Key press and release time.
static int iKeyMs = 150;
/// State of the typing: the keyboard scans seen at the last paint, the seen keys and the time at the press of the key, the
/// time the typing waits for the program to scan the keyboard, [Shift] held for a symbol. Reset when a game starts.
static Uint32 iTypeScanPrev = 0, iTypeSeenAtPress = 0, iTypePressTime = 0, iTypeWaitSince = 0;
static bool bTypeShiftHeld = false;
/// The cursor key last used, a key of REMAP "@" presses it too (a jump in the direction of the walk). Reset when a game starts.
static SDL_Keycode iLastDirection = SDLK_UNKNOWN, iHeldDirection = SDLK_UNKNOWN;
static bool bDirectionDown = false;
static Uint32 iDirectionTime = 0;
/// All games of the list menu.
static std::vector <tGame> oGames;
/// Names (tapes) of the favorite games in the order of the main menu. Without a file: the four games the menu always had.
static std::vector <std::string> oFavorites;

/// The list menu is shown instead of the favorite games.
static bool inList = false;
/// Selected row of the list (see ListView).
static int listIndex = 0;
/// Categories of the games, in the order of their first appearance in games.lst.
static std::vector <std::string> oCategories;
/// The category that is open in the list, -1 for the main list.
static int iCategory = -1;
/// Names of the categories that are families: the versions of one game ("#FAMILY name"), shown among the games
/// in alphabetical order instead of at the top of the list.
static std::vector <std::string> oFamilies;

/// The parent of a category: "Programovani/KAREL" is in "Programovani", the empty string is the main list.
static std::string CategoryParent (const std::string &sName)
{
  size_t iSlash = sName.rfind ('/');
  return (iSlash == std::string::npos ? std::string () : sName.substr (0, iSlash));
}

/// The name of a category without its parent.
static std::string CategoryLabel (const std::string &sName)
{
  size_t iSlash = sName.rfind ('/');
  return (iSlash == std::string::npos ? sName : sName.substr (iSlash + 1));
}

/// The name of a category for a title: "Programovani / KAREL".
static std::string CategoryPath (const std::string &sName)
{
  std::string sPath;
  for (size_t i = 0 ; i < sName.size () ; i ++)
    sPath += sName [i] == '/' ? std::string (" / ") : std::string (1, sName [i]);
  return (sPath);
}

/// Adds a category and its parents ("A/B/C" adds "A", "A/B" and "A/B/C") to oCategories.
static void CategoryAdd (const std::string &sName)
{
  if (sName.empty ())
    return;
  CategoryAdd (CategoryParent (sName));
  if (std::find (oCategories.begin (), oCategories.end (), sName) == oCategories.end ())
    oCategories.push_back (sName);
}

static bool CategoryIsFamily (const std::string &sName)
{
  return (std::find (oFamilies.begin (), oFamilies.end (), sName) != oFamilies.end ());
}


//--------------------------------------------------------------------------
// Simulator Control

/// Request simulator shutdown.
void SIMRequestShutdown ()
{
  // No active notification is needed since the main
  // event loop delivers messages periodically and
  // is therefore bound to observe the variable.

  bShutdown = true;
}


bool SIMQueryShutdown ()
{
  return (bShutdown);
}


//--------------------------------------------------------------------------
// Model Initialization


/** Flush memory.
 *
 *  @arg iFrom From which address.
 *  @arg iSize How large block.
 */
static void FlushMemory (int iFrom, int iSize)
{
  for (int iAddr = iFrom ; iAddr < iFrom + iSize ; iAddr ++)
  {
    abMemoryData [iAddr] = 0;
  }
}


/** Mark a memory area as read write.
 *
 *  @arg iFrom From which address.
 *  @arg iSize How large block.
 */
static void SetMemoryReadWrite (int iFrom, int iSize)
{
  for (int iAddr = iFrom ; iAddr < iFrom + iSize ; iAddr ++)
  {
    abMemoryMask [iAddr] = 1;
  }
}


/** Mark a memory area as read only.
 *
 *  @arg iFrom From which address.
 *  @arg iSize How large block.
 */
static void SetMemoryReadOnly (int iFrom, int iSize)
{
  for (int iAddr = iFrom ; iAddr < iFrom + iSize ; iAddr ++)
  {
    abMemoryMask [iAddr] = 0;
  }
}


/** Fill a memory area with content from file.
 *
 *  @arg iFrom From which address.
 *  @arg iSize How large block.
 *  @arg pFile What file.
 */
static void FillMemoryFromFile (int iFrom, int iSize, const char *pFile)
{
  std::ifstream oFile;
  oFile.open (pFile);
  oFile.read (reinterpret_cast <char *> (abMemoryData + iFrom), iSize);
  if (!oFile.good ())
  {
    // Without the ROM nothing can run: say which file is missing instead of a bare assertion.
    fprintf (stderr, "simpmd: cannot read %d bytes of %s (the share directory is set at build time, PMD_SHARE)\n", iSize, pFile);
    SDL_Quit ();
    exit (1);
  }
  oFile.close ();
}


/// Initialize a PMD 85-1 model.
static void InitializePMD1 ()
{
  CPUAllRamDisable ();
  // Read the monitor image.
  FillMemoryFromFile (0x8000, 4096, PMD_SHARE "M1");
  FillMemoryFromFile (0xA000, 4096, PMD_SHARE "M1");
  // The first 32k is read write.
  // The next 16k is read only.
  // The last 16k is read write.
  SetMemoryReadWrite (0x0000, 32768);
  SetMemoryReadOnly  (0x8000, 16384);
  SetMemoryReadWrite (0xC000, 16384);
}


/// Initialize a PMD 85-2 model.
static void InitializePMD2 ()
{
  CPUAllRamDisable ();
  // Read the monitor image, patched to read the tape the PMD 85-1 way (the only way the tape port is simulated).
  FillMemoryFromFile (0x8000, 4096, PMD_SHARE "M2-Patched-Tape");
  // The entire 64k is read write.
  SetMemoryReadWrite (0x0000, 65536);
}


/// Initialize a PMD 85-2A model: as the PMD 85-2, but the monitor ROM at 0x8000-0x8FFF can be switched off by PC4 of the
/// system 8255 (the RAM under it, AllRAM). The ROM is the monitor of the PMD 85-2 (monit2A.rom of pmd85.borik.net is the
/// same, here patched to read the tape the PMD 85-1 way).
static void InitializePMD2A ()
{
  FillMemoryFromFile (0x8000, 4096, PMD_SHARE "M2-Patched-Tape");
  SetMemoryReadWrite (0x0000, 65536);
  CPUAllRamEnable (abMemoryData + 0x8000);
}


/** File of the saved state of a game and the game name (the tape name, with or without the share path). The directory is
 *  PMD_STATEDIR when set; else, when built with PMD_STATEHOME, "$HOME/<PMD_STATEHOME>" of the running user (every user has
 *  own states); else "states" in the share directory.
 */
static std::string StateDir ()
{
  const char *pDir = getenv ("PMD_STATEDIR");
  std::string sDir = pDir && *pDir ? pDir : PMD_SHARE "states";
#ifdef PMD_STATEHOME
  const char *pHome = getenv ("HOME");
  if (!(pDir && *pDir) && pHome && *pHome)
    sDir = std::string (pHome) + "/" PMD_STATEHOME;
#endif
  return (sDir);
}

static std::string StatePathOf (const std::string &sTape)
{
  std::string sDir = StateDir ();
  size_t iSlash = sTape.rfind ('/');
  return (sDir + "/" + (iSlash == std::string::npos ? sTape : sTape.substr (iSlash + 1)) + ".sav");
}

/// Whether a state of the game was saved (the menus show it).
static bool HasState (const std::string &sTape)
{
  return (access (StatePathOf (sTape).c_str (), F_OK) == 0);
}

/** The note of a game as the list shows it: the commands to type and the ROM files of a BASIC game (ROM, TYPE) are
 *  replaced by "BASIC-G", the other commands (MGLD, JUMP, POKE) stay.
 */
static std::string ListNote (const std::string &sNote)
{
  std::string sResult;
  bool bBasic = false;
  size_t iPos = 0;
  while (iPos < sNote.size ())
  {
    size_t iEnd = sNote.find (',', iPos);
    if (iEnd == std::string::npos)
      iEnd = sNote.size ();
    std::string sPart = sNote.substr (iPos, iEnd - iPos);
    iPos = iEnd + 1;
    while (!sPart.empty () && sPart [0] == ' ')
      sPart.erase (0, 1);
    if (sPart == "FONT" || sPart == "NOBLINK" || sPart == "BEEPSLOW" || sPart == "TAPESIGNAL" || sPart == "TAPELOOP" || sPart == "TAPEAUTOREWIND" || sPart.compare (0, 8, "TAPESAVE") == 0 || sPart.compare (0, 7, "KEYHOLD") == 0)
      continue;
    if (sPart.compare (0, 4, "ROM ") == 0 || sPart.compare (0, 5, "TYPE ") == 0)
      bBasic = bBasic || sPart.find ("basic") != std::string::npos;
    else if (!sPart.empty ())
      sResult += (sResult.empty () ? "" : ", ") + sPart;
  }
  return (bBasic ? std::string ("BASIC-G") + (sResult.empty () ? "" : ", ") + sResult : sResult);
}

/// Is the game in the category? The CATEGORY column may list several categories separated by '|'.
static bool InCategory (const std::string &sList, const std::string &sCategory)
{
  size_t iStart = 0;
  while (iStart <= sList.size ())
  {
    size_t iEnd = sList.find ('|', iStart);
    if (iEnd == std::string::npos)
      iEnd = sList.size ();
    if (sList.compare (iStart, iEnd - iStart, sCategory) == 0)
      return true;
    iStart = iEnd + 1;
  }
  return false;
}

/// Is the game in the category or in one of its subcategories?
static bool InCategoryTree (const std::string &sList, const std::string &sCategory)
{
  size_t iStart = 0;
  while (iStart <= sList.size ())
  {
    size_t iEnd = sList.find ('|', iStart);
    if (iEnd == std::string::npos)
      iEnd = sList.size ();
    std::string sOne = sList.substr (iStart, iEnd - iStart);
    if (sOne == sCategory || sOne.compare (0, sCategory.size () + 1, sCategory + "/") == 0)
      return true;
    iStart = iEnd + 1;
  }
  return false;
}

/// The row "(o uroven vys)" of an open category.
static const int LIST_LEVEL_UP = -1000000;

/** The rows of the games list: a list (the main one or an open category) shows the row LIST_LEVEL_UP (open category
 *  only, back to the parent), then its categories (a value below zero, -(n + 1) is the category n), then the games
 *  of the category (the index of oGames) mixed in alphabetical order with its families (categories of the versions
 *  of one game, "#FAMILY name").
 */
static std::vector <int> ListView ()
{
  std::vector <int> oView;
  std::string sLevel = iCategory < 0 ? std::string () : oCategories [iCategory];
  if (iCategory >= 0)
    oView.push_back (LIST_LEVEL_UP);
  for (size_t k = 0 ; k < oCategories.size () ; k ++)
    if (!CategoryIsFamily (oCategories [k]) && CategoryParent (oCategories [k]) == sLevel)
      oView.push_back (-(int) k - 1);
  std::vector <std::pair <std::string, int> > oMixed;
  for (size_t k = 0 ; k < oCategories.size () ; k ++)
    if (CategoryIsFamily (oCategories [k]) && CategoryParent (oCategories [k]) == sLevel)
      oMixed.push_back (std::make_pair (CategoryLabel (oCategories [k]), -(int) k - 1));
  for (size_t i = 0 ; i < oGames.size () ; i ++)
  {
    const std::string &sCategory = oGames [i].sCategory;
    if (iCategory < 0 ? sCategory.empty () : InCategory (sCategory, sLevel))
      oMixed.push_back (std::make_pair (oGames [i].sTape, (int) i));
  }
  std::stable_sort (oMixed.begin (), oMixed.end (), [] (const std::pair <std::string, int> &a, const std::pair <std::string, int> &b)
                    { return a.first < b.first; });
  for (size_t i = 0 ; i < oMixed.size () ; i ++)
    oView.push_back (oMixed [i].second);
  return oView;
}


/** Host key for a character typed by PMD_KEYS, bShift is set when [Shift] has to be held with it (the symbols of the
 *  PMD 85 keyboard that are on the digits and some other keys).
 */
static SDL_Keycode TypedKey (char cKey, bool &bShift)
{
  bShift = false;
  static const char *pShiftDigits = "!\"#$%&'()";
  const char *pFound = strchr (pShiftDigits, cKey);
  if (cKey && pFound)
  {
    bShift = true;
    return (SDLK_1 + (pFound - pShiftDigits));
  }
  switch (cKey)
  {
    case '-': bShift = true; return (SDLK_0);
    case '=': bShift = true; return (SDLK_MINUS);
    case '+': bShift = true; return (SDLK_SEMICOLON);
    case '*': bShift = true; return (SDLK_QUOTE);
    case '?': bShift = true; return (SDLK_SLASH);
    case ':': return (SDLK_QUOTE);
    // The PMD 85 is QWERTZ: its Y is the host key z and its Z is the host key y.
    case 'Y': case 'y': return (SDLK_z);
    case 'Z': case 'z': return (SDLK_y);
  }
  return ((cKey == '|') ? SDLK_RETURN : (cKey == '<') ? SDLK_LEFT : (cKey == '>') ? SDLK_RIGHT : (cKey == '^') ? SDLK_LSHIFT
          : (cKey >= 'A' && cKey <= 'Z') ? cKey + 32 : cKey);
}


/** The game of games.lst of a tape (its name with or without the share path), NULL when the tape is not in the list.
 */
static const tGame *GameOfTape (const std::string &sTape)
{
  std::string sName = sTape;
  size_t iShare = strlen (PMD_SHARE);
  if (sName.compare (0, iShare, PMD_SHARE) == 0)
    sName.erase (0, iShare);
  for (size_t i = 0 ; i < oGames.size () ; i ++)
    if (oGames [i].sTape == sName)
      return (&oGames [i]);
  return (NULL);
}


/** Looks for a command in the note of a game (the comma separated parts of the third column of games.lst): true when a
 *  part starts with sCommand, its rest without the leading spaces goes to pArgument ("TAPESAVE EVIDENCE" gives "EVIDENCE").
 */
static bool NoteCommand (const std::string &sNote, const char *sCommand, std::string *pArgument)
{
  size_t iLength = strlen (sCommand);
  size_t iPos = 0;
  while (iPos < sNote.size ())
  {
    size_t iEnd = sNote.find (',', iPos);
    if (iEnd == std::string::npos)
      iEnd = sNote.size ();
    std::string sPart = sNote.substr (iPos, iEnd - iPos);
    iPos = iEnd + 1;
    while (!sPart.empty () && sPart [0] == ' ')
      sPart.erase (0, 1);
    if (sPart.compare (0, iLength, sCommand) != 0)
      continue;
    if (pArgument)
    {
      *pArgument = sPart.substr (iLength);
      while (!pArgument->empty () && (*pArgument) [0] == ' ')
        pArgument->erase (0, 1);
    }
    return (true);
  }
  return (false);
}


/** Note of a game of games.lst: true when it has the command (a part of the comma separated note) that starts with
 *  sCommand.
 */
static bool GameHasCommand (const std::string &sTape, const char *sCommand)
{
  const tGame *pGame = GameOfTape (sTape);
  return (pGame && NoteCommand (pGame->sNote, sCommand, NULL));
}


/** Argument of a command of the note of a game ("TAPESAVE EVIDENCE" gives "EVIDENCE"), empty when there is none.
 */
static std::string GameCommandArgument (const std::string &sTape, const char *sCommand)
{
  std::string sArgument;
  const tGame *pGame = GameOfTape (sTape);
  if (pGame)
    NoteCommand (pGame->sNote, sCommand, &sArgument);
  return (sArgument);
}


/** Czech and Slovak letters for the programs of the PMD 85-3 ("FONT" in the note of games.lst). The PMD 85-3 has them
 *  in its character set (codes 0xC0-0xDF small letters, 0xE0-0xFF the capitals 0x20 higher), the font of the PMD 85-2
 *  monitor ends at 0x7F and the other codes are drawn as dots. The monitor takes the glyphs of 32 codes from a block
 *  whose address is in a table (0xC0B0 in RAM, copied from 0x8270 of the monitor): the blocks of 0xC0 and 0xE0 get the
 *  glyphs made here from the letter of the monitor font and a mark, at 0x9000 and 0x9100 (free RAM of the emulator).
 *  The codes are the ones that the programs use in words (a sample: REVERZ and the others of the basic package).
 */
/** "TAPESIGNAL" in the note: the routine of the monitor 0x8890 (counts the time to the next change of the status bit 7,
 *  returns the rest of the counter in C) is back, the patched ROM has it empty and the ROM has no free place. It goes
 *  to the free RAM of the emulator at 0x9200, 0x8890 jumps there.
 */
static void SignalRomPatch ()
{
  static const byte abRoutine [] = {
    0x0E, 0x25,             // 9200  mvi c,25
    0xDB, 0x1F,             // 9202  in 1f
    0x67,                   // 9204  mov h,a
    0x0D,                   // 9205  dcr c
    0xC8,                   // 9206  rz
    0xDB, 0xF5,             // 9207  in f5
    0xE6, 0x40,             // 9209  ani 40  (the key STOP ends it)
    0xC8,                   // 920B  rz
    0xDB, 0x1F,             // 920C  in 1f
    0xAC,                   // 920E  xra h
    0xF2, 0x05, 0x92,       // 920F  jp 9205
    0xC9                    // 9212  ret
  };
  memcpy (abMemoryData + 0x9200, abRoutine, sizeof (abRoutine));
  abMemoryData [0x8890] = 0xC3;       // jmp 9200
  abMemoryData [0x8891] = 0x00;
  abMemoryData [0x8892] = 0x92;
  // The ROM routine 0x848D (push bc, de, hl, jump to 0x8B80: the synchronisation to the first bit after the calibration
  // of the tape speed, a copy of it is in the program) has nothing to do with the bytes read from the USART, but the
  // patched ROM ends it with a bare return: it has to take the registers back and return A = 0.
  static const byte abSync [] = { 0xAF, 0xE1, 0xD1, 0xC1, 0xC9 };  // xra a, pop hl, pop de, pop bc, ret
  memcpy (abMemoryData + 0x8B80, abSync, sizeof (abSync));
}


static void FontPatch ()
{
  struct tLetter { byte iCode; char cBase; char cMark; };
  static const tLetter asLetters [] = {
    { 0xC1, 'a', 'a' }, { 0xC3, 'c', 'c' }, { 0xC4, 'd', 't' }, { 0xC9, 'i', 'a' }, { 0xCC, 'l', 't' },
    { 0xCE, 'n', 'c' }, { 0xD0, 'o', 'o' }, { 0xD3, 's', 'c' }, { 0xD4, 't', 't' }, { 0xD5, 'u', 'a' },
    { 0xD7, 'e', 'a' }, { 0xD9, 'y', 'a' }, { 0xDA, 'z', 'c' }
  };
  const int FONT_BLOCK_SMALL = 0x9000, FONT_BLOCK_CAPITAL = 0x9100;
  for (size_t i = 0 ; i < sizeof (asLetters) / sizeof (asLetters [0]) ; i ++)
  {
    for (int iCapital = 0 ; iCapital < 2 ; iCapital ++)
    {
      char cBase = iCapital ? asLetters [i].cBase - 32 : asLetters [i].cBase;
      // The glyph of the base letter: 8 bytes, the top row first (0x20-0x5F at 0x8600, 0x60-0x7F at 0x88C0).
      int iFrom = cBase < 0x60 ? 0x8600 + (cBase - 0x20) * 8 : 0x88C0 + (cBase - 0x60) * 8;
      byte abGlyph [8];
      memcpy (abGlyph, abMemoryData + iFrom, 8);
      byte iMark = asLetters [i].cMark;
      if (iCapital)
      {
        // No free row above a capital: the letter goes one row down, the mark takes the top row.
        for (int iRow = 7 ; iRow > 0 ; iRow --)
          abGlyph [iRow] = abGlyph [iRow - 1];
        abGlyph [0] = iMark == 'a' ? 0x10 : iMark == 'c' ? 0x14 : iMark == 'o' ? 0x08 : 0x20;
      }
      else if (iMark == 'a')
      {
        // The acute accent; the dot of the i goes.
        abGlyph [0] = 0x10;
        abGlyph [1] = 0x08;
      }
      else if (iMark == 'c')
      {
        abGlyph [0] = 0x14;
        abGlyph [1] = 0x08;
      }
      else if (iMark == 'o')
      {
        abGlyph [0] = 0x08;
        abGlyph [1] = 0x14;
      }
      else
      {
        // The apostrophe of d, l and t, right of the stem (d: next to it).
        byte iTick = asLetters [i].cBase == 't' ? 0x10 : 0x20;
        abGlyph [0] |= iTick;
        abGlyph [1] |= iTick;
      }
      int iCode = asLetters [i].iCode + (iCapital ? 0x20 : 0);
      memcpy (abMemoryData + (iCapital ? FONT_BLOCK_CAPITAL : FONT_BLOCK_SMALL) + (iCode & 0x1F) * 8, abGlyph, 8);
    }
  }
  // The table of the blocks: the glyph of the code c is read from the address in the table + (c & 0x1F) * 8, backwards.
  abMemoryData [0x8270 + 12] = (FONT_BLOCK_SMALL + 8) & 0xFF;
  abMemoryData [0x8270 + 13] = (FONT_BLOCK_SMALL + 8) >> 8;
  abMemoryData [0x8270 + 14] = (FONT_BLOCK_CAPITAL + 8) & 0xFF;
  abMemoryData [0x8270 + 15] = (FONT_BLOCK_CAPITAL + 8) >> 8;
}


/** ROM module of a game: "ROM basic2.rmm kli2.rmm" in the note of games.lst puts the files of the share directory into
 *  the 32 KB ROM pack one after another, each starting on a 1 KB boundary (as the ROM packages of the PMD 85 emulator
 *  do). A game without ROM has no module.
 */
static void RomModuleLoad (const std::string &sTape)
{
  std::string sFiles;
  const tGame *pGame = GameOfTape (sTape);
  if (pGame && NoteCommand (pGame->sNote, "ROM ", &sFiles))
  {
    std::vector <byte> oPack;
    size_t iPos = 0;
    while (iPos < sFiles.size ())
    {
      size_t iSpace = sFiles.find (' ', iPos);
      std::string sFile = sFiles.substr (iPos, iSpace == std::string::npos ? iSpace : iSpace - iPos);
      iPos = iSpace == std::string::npos ? sFiles.size () : iSpace + 1;
      if (sFile.empty ())
        continue;
      std::ifstream oFile (PMD_SHARE + sFile, std::ios::binary);
      if (!oFile)
      {
        fprintf (stderr, "ROM module: no file %s\n", sFile.c_str ());
        continue;
      }
      std::vector <byte> oData ((std::istreambuf_iterator <char> (oFile)), std::istreambuf_iterator <char> ());
      oData.resize ((oData.size () + 1023) / 1024 * 1024, 0xFF);
      oPack.insert (oPack.end (), oData.begin (), oData.end ());
    }
    if (!oPack.empty ())
    {
      CPURomModuleSet (&oPack [0], oPack.size ());
      return;
    }
  }
  CPURomModuleSet (NULL, 0);
}


/** Keys that load a game: the commands of the note of games.lst, "MGLD 00, JUMP 2000" is typed as
 *  "MGLD 00|" and, after the tape stops loading ('~'), "JUMP 2000|". "POKE <hex address> <hex value>" writes a byte
 *  to the memory after the load (for a game that reads a variable that is never set; on the real PMD 85 the RAM is not
 *  cleared, here it is zero).
 */
static std::string AutoloadKeys (const std::string &sNote)
{
  std::string sResult;
  bool bShiftHold = false;
  size_t iPos = 0;
  while (iPos < sNote.size ())
  {
    size_t iEnd = sNote.find (',', iPos);
    if (iEnd == std::string::npos)
      iEnd = sNote.size ();
    std::string sPart = sNote.substr (iPos, iEnd - iPos);
    iPos = iEnd + 1;
    while (!sPart.empty () && sPart [0] == ' ')
      sPart.erase (0, 1);
    if (sPart.compare (0, 4, "MGLD") == 0)
      sResult += sPart + "|";
    else if (sPart.compare (0, 4, "JUMP") == 0)
      sResult += "~" + sPart + "|";
    else if (sPart.compare (0, 5, "TYPE ") == 0)
      // "TYPE LOAD" is typed with [Enter] after the tape has stopped loading (BASIC commands).
      sResult += "~" + sPart.substr (5) + "|";
    else if (sPart == "SHIFT")
      // [Shift] held from the first key until the game has started: the loader of the game picks the monochrome mode
      // that way (Magicland Dizzy reads it at the start of the load, holding it later does not help).
      bShiftHold = true;
    else if (sPart.compare (0, 4, "POKE") == 0)
      // "POKE 39A4 01" becomes "{39A4=01}", written to the memory after the tape stops loading.
      sResult += "~{" + sPart.substr (5, sPart.find (' ', 5) == std::string::npos ? 0 : sPart.find (' ', 5) - 5) + "=" +
                 (sPart.find (' ', 5) == std::string::npos ? "" : sPart.substr (sPart.find (' ', 5) + 1)) + "}";
  }
  if (bShiftHold)
  {
    // [Shift] goes down right after the [Enter] of the first command, not before: with it down the letters of the
    // command are typed as lower case and the monitor answers "No command". The loader of the game reads it later,
    // at the start of the load.
    size_t iEnter = sResult.find ('|');
    sResult = (iEnter == std::string::npos ? "[" + sResult : sResult.substr (0, iEnter + 1) + "[" + sResult.substr (iEnter + 1)) + "_____]";
  }
  return (sResult);
}


/// A text as the content of a JSON string (the names come from games.lst, a quote or a backslash must not break the state).
static std::string JsonEscape (const std::string &sText)
{
  std::string sResult;
  for (size_t i = 0 ; i < sText.size () ; i ++)
  {
    unsigned char cChar = sText [i];
    if (cChar == '"' || cChar == '\\')
      sResult += std::string ("\\") + (char) cChar;
    else if (cChar < 0x20)
    {
      char acCode [8];
      snprintf (acCode, sizeof (acCode), "\\u%04x", cChar);
      sResult += acCode;
    }
    else
      sResult += (char) cChar;
  }
  return (sResult);
}


/** State for the web remote control, JSON: mode (menu, list or game), selected index, names of the games. The rows of the
 *  list are sent only while the list is shown (building them sorts the games, a running game publishes its state often).
 */
static std::string WebStateBuild ()
{
  std::string sState = std::string ("{\"mode\":\"") + (!inMenu ? "game" : inList ? "list" : "menu") + "\",\"typing\":" +
                       (iKeyPos < (int) sKeys.size () ? "true" : "false") + ",\"tape_idle_ms\":" +
                       std::to_string (TAPMsSinceRead ()) + ",\"index\":" +
                       std::to_string (inList ? listIndex : gameIndex) + ",\"pad\":\"" + JsonEscape (inMenu ? pPadMenu : sPad) +
                       "\",\"featured\":[";
  for (size_t i = 0 ; inMenu && i < oFavorites.size () ; i ++)
    sState += std::string (i ? "," : "") + "\"" + JsonEscape (oFavorites [i]) + "\"";
  sState += "],\"games\":[";
  std::vector <int> oView;
  if (inMenu && inList)
    oView = ListView ();
  for (size_t i = 0 ; i < oView.size () ; i ++)
    sState += std::string (i ? "," : "") + "\"" + JsonEscape (oView [i] == LIST_LEVEL_UP ? std::string ("(o uroven vys)") :
              oView [i] < 0 ? (CategoryIsFamily (oCategories [-oView [i] - 1]) ? CategoryLabel (oCategories [-oView [i] - 1]) + " >" :
                               "[ " + CategoryLabel (oCategories [-oView [i] - 1]) + " ]") : oGames [oView [i]].sTape) + "\"";
  return (sState + "]}");
}


/// The state for the web thread: built by the main thread (WebStatePublish), the web thread only copies it.
static std::mutex oWebStateLock;
static std::string sWebState = "{}";

static void WebStatePublish ()
{
  std::string sState = WebStateBuild ();
  std::lock_guard <std::mutex> oGuard (oWebStateLock);
  sWebState.swap (sState);
}

/// Whether the event is a reason to publish the state: the paint ticks of a running game at most 5 times a second (the
/// typing and the tape state change by themselves), the idle ticks never, everything else (keys, timers) always.
static bool WebPublishDue (Uint32 iType)
{
  static Uint32 iLast = 0;
  if (iType == iTickEvent)
    return (false);
  if (iType != SDL_USEREVENT)
    return (true);
  Uint32 iNow = SDL_GetTicks ();
  if (iNow - iLast < 200)
    return (false);
  iLast = iNow;
  return (true);
}

static std::string WebState ()
{
  std::lock_guard <std::mutex> oGuard (oWebStateLock);
  return (sWebState);
}


/** Remap keys of the game from the games list (4th column REMAP), none for the other games.
 */
static void KeysRemap (const std::string &sTape)
{
  oRemap.clear ();
  oRemapDirection.clear ();
  oRemapLong.clear ();
  bLongDown = false;
  iLongSerial ++;
  sPad = pPadGame;
  std::string sPadKeys;
  std::string sKeyboardRemap;
  const tGame *pGame = GameOfTape (sTape);
  if (pGame)
  {
    if (!pGame->sPad.empty ())
      sPad = pGame->sPad;
    const std::string &sRemap = pGame->sRemap;
    size_t iPos = 0;
    while (iPos < sRemap.size ())
    {
      size_t iEnd = sRemap.find (',', iPos);
      if (iEnd == std::string::npos)
        iEnd = sRemap.size ();
      std::string sPair = sRemap.substr (iPos, iEnd - iPos);
      iPos = iEnd + 1;
      // "pad:<input>=<key>": the key of a gamepad input in this game (GPDSetGameKeys)
      if (sPair.compare (0, 4, "pad:") == 0)
      {
        sPadKeys += (sPadKeys.empty () ? "" : ",") + sPair.substr (4);
        continue;
      }
      size_t iEqual = sPair.find ('=');
      if (iEqual == std::string::npos)
        continue;
      sKeyboardRemap += (sKeyboardRemap.empty () ? "" : ", ") + sPair;
      SDL_Keycode iFrom = SDL_GetKeyFromName (sPair.substr (0, iEqual).c_str ());
      std::string sTo = sPair.substr (iEqual + 1);
      // "Short|Long[|Held]": a short press sends the first key, a long press the second one, the third is held while pressed
      size_t iBar = sTo.find ('|');
      if (iBar != std::string::npos)
      {
        tLongKey sLong;
        sLong.iShort = SDL_GetKeyFromName (sTo.substr (0, iBar).c_str ());
        size_t iBar2 = sTo.find ('|', iBar + 1);
        sLong.iLong = SDL_GetKeyFromName (sTo.substr (iBar + 1, iBar2 == std::string::npos ? iBar2 : iBar2 - iBar - 1).c_str ());
        sLong.iHeld = SDLK_UNKNOWN;
        sLong.bHeldDirection = false;
        if (iBar2 != std::string::npos)
        {
          std::string sHeld = sTo.substr (iBar2 + 1);
          sLong.bHeldDirection = sHeld.size () > 1 && sHeld [0] == '@';
          sLong.iHeld = SDL_GetKeyFromName (sHeld.c_str () + (sLong.bHeldDirection ? 1 : 0));
        }
        if (iFrom != SDLK_UNKNOWN && sLong.iShort != SDLK_UNKNOWN && sLong.iLong != SDLK_UNKNOWN)
          oRemapLong [iFrom] = sLong;
        continue;
      }
      // "@Key" (more than one character, the key "@" itself stays "@") adds the cursor key last used
      bool bDirection = sTo.size () > 1 && sTo [0] == '@';
      SDL_Keycode iTo = SDL_GetKeyFromName (sTo.c_str () + (bDirection ? 1 : 0));
      if (iFrom != SDLK_UNKNOWN && iTo != SDLK_UNKNOWN)
      {
        oRemap [iFrom] = iTo;
        if (bDirection)
          oRemapDirection.insert (iFrom);
      }
    }
  }
  GPDSetGameKeys (sPadKeys);
  // The controls of the game to the terminal (the KEYS column of games.lst), for the user who starts it from a shell
  if (pGame)
  {
    printf ("== %s ==\n", pGame->sTape.c_str ());
    if (!pGame->sHelp.empty ())
      printf ("Ovladanie: %s\n", pGame->sHelp.c_str ());
    if (!sKeyboardRemap.empty ())
      printf ("Klavesy PMD (host=PMD): %s\n", sKeyboardRemap.c_str ());
    // The pad as it plays this game: each input, the key it sends and the PMD 85 key after the REMAP of the game.
    std::string sPadList;
    for (int iInput = 0 ; ; iInput ++)
    {
      const char *pName = NULL;
      SDL_Keycode iKey = GPDInputKey (iInput, &pName);
      if (!pName)
        break;
      if (iKey == SDLK_UNKNOWN)
        continue;
      std::map <SDL_Keycode, SDL_Keycode>::const_iterator xRemap = oRemap.find (iKey);
      sPadList += std::string (sPadList.empty () ? "" : ", ") + pName + "=" + SDL_GetKeyName (xRemap != oRemap.end () ? xRemap->second : iKey);
    }
    if (!sPadList.empty ())
      printf ("Gamepad (vstup=klaves PMD): %s\n", sPadList.c_str ());
    if (pGame->sInput.find ('P') != std::string::npos)
      printf ("Hra sa da hrat gamepadom.\n");
    fflush (stdout);
  }
}


/** Start typing the keys of a game: PMD_KEYS in test mode, otherwise the load command from the games list
 *  (PMD_AUTOLOAD=0 turns it off).
 */
static void KeysStart (const std::string &sTape)
{
  sKeys.clear ();
  const char *pKeys = getenv ("PMD_KEYS");
  const char *pAutoload = getenv ("PMD_AUTOLOAD");
  if (pKeys)
    sKeys = pKeys;
  else if (!pAutoload || atoi (pAutoload) != 0)
  {
    const tGame *pGame = GameOfTape (sTape);
    if (pGame)
      sKeys = AutoloadKeys (pGame->sNote);
  }
  KeysRemap (sTape);
  KBDSetJoystickOnly (GameHasCommand (sTape, "JOYONLY"));
  CPUSetJoystickOff (GameHasCommand (sTape, "NOJOY"));
  iKeyPos = 0;
  bKeyDown = false;
  iKeyTime = SDL_GetTicks () + 1500;
  // Nothing of the typing of the previous game stays (it may have been left in the middle of a key).
  iTypeScanPrev = iTypeSeenAtPress = iTypePressTime = iTypeWaitSince = 0;
  bTypeShiftHeld = false;
  iLastDirection = iHeldDirection = SDLK_UNKNOWN;
  bDirectionDown = false;
  iDirectionTime = 0;
}


/** Load the games list, lines NAME;MODEL;NOTE, '#' starts a comment.
 */
static void GamesLoad ()
{
  std::ifstream oFile (PMD_SHARE "games.lst");
  std::string sLine;
  while (std::getline (oFile, sLine))
  {
    // "#CATEGORY name" fixes the order of the categories in the list (otherwise: the first game of a category),
    // "#FAMILY name" declares a family: the versions of one game, a category that is shown among the games in
    // alphabetical order. "Parent/Name" puts a category into another one.
    bool bFamily = sLine.compare (0, 8, "#FAMILY ") == 0;
    if (sLine.compare (0, 10, "#CATEGORY ") == 0 || bFamily)
    {
      std::string sCategory = sLine.substr (bFamily ? 8 : 10);
      while (!sCategory.empty () && (sCategory [sCategory.size () - 1] == '\r' || sCategory [sCategory.size () - 1] == ' '))
        sCategory.erase (sCategory.size () - 1);
      CategoryAdd (sCategory);
      if (bFamily && !sCategory.empty ())
        oFamilies.push_back (sCategory);
      continue;
    }
    if (sLine.empty () || sLine [0] == '#')
      continue;
    size_t iFirst = sLine.find (';');
    size_t iSecond = iFirst == std::string::npos ? iFirst : sLine.find (';', iFirst + 1);
    if (iSecond == std::string::npos)
      continue;
    // NAME;MODEL;NOTE[;REMAP[;PAD[;KEYS[;INPUT[;CATEGORY]]]]], the text of a column may not contain ';'
    std::vector <std::string> oColumns;
    for (size_t iPos = 0 ; ; )
    {
      size_t iEnd = sLine.find (';', iPos);
      oColumns.push_back (sLine.substr (iPos, iEnd == std::string::npos ? iEnd : iEnd - iPos));
      if (iEnd == std::string::npos)
        break;
      iPos = iEnd + 1;
    }
    oColumns.resize (8);
    tGame sGame;
    sGame.sTape = oColumns [0];
    sGame.iModel = oColumns [1] == "2A" ? 3 : atoi (oColumns [1].c_str ()) == 2 ? 2 : 1;
    sGame.sNote = oColumns [2];
    sGame.sRemap = oColumns [3];
    sGame.sPad = oColumns [4];
    sGame.sHelp = oColumns [5];
    sGame.sInput = oColumns [6];
    sGame.sCategory = oColumns [7];
    for (size_t iStart = 0 ; !sGame.sCategory.empty () && iStart <= sGame.sCategory.size () ;)
    {
      size_t iEnd = sGame.sCategory.find ('|', iStart);
      if (iEnd == std::string::npos)
        iEnd = sGame.sCategory.size ();
      std::string sOne = sGame.sCategory.substr (iStart, iEnd - iStart);
      CategoryAdd (sOne);
      iStart = iEnd + 1;
    }
    oGames.push_back (sGame);
  }
  // Alphabetical order whatever the order of the lines in the file (new games are appended at its end).
  std::stable_sort (oGames.begin (), oGames.end (), [] (const tGame & a, const tGame & b) { return a.sTape < b.sTape; });
}


//--------------------------------------------------------------------------
// Favorites: the games of the main menu

/// File of the favorites: PMD_FAVORITES, else favorites.lst in the directory of the saved states.
static std::string FavoritesPath ()
{
  const char *pFile = getenv ("PMD_FAVORITES");
  return (pFile && *pFile ? std::string (pFile) : StateDir () + "/favorites.lst");
}

/// Index of the game of the games list, -1 if there is none of the name.
static int GameFind (const std::string &sTape)
{
  for (size_t i = 0 ; i < oGames.size () ; i ++)
    if (oGames [i].sTape == sTape)
      return ((int) i);
  return (-1);
}

static bool IsFavorite (const std::string &sTape)
{
  return (std::find (oFavorites.begin (), oFavorites.end (), sTape) != oFavorites.end ());
}

/// Saves the favorites: written under a temporary name and renamed, a power off in the middle keeps the old file.
static void FavoritesSave ()
{
  std::string sPath = FavoritesPath ();
  std::string sTemp = sPath + ".tmp";
  SIMMakeDirs (sPath);
  bool bOk;
  {
    std::ofstream oFile (sTemp);
    for (size_t i = 0 ; i < oFavorites.size () ; i ++)
      oFile << oFavorites [i] << "\n";
    oFile.close ();
    bOk = oFile.good ();
  }
  if (!bOk || rename (sTemp.c_str (), sPath.c_str ()) != 0)
  {
    remove (sTemp.c_str ());
    fprintf (stderr, "Favorites not saved: %s\n", sPath.c_str ());
  }
}

/// Reads the favorites, the games that are not in games.lst (any more) are dropped.
static void FavoritesLoad ()
{
  oFavorites.clear ();
  std::ifstream oFile (FavoritesPath ());
  if (!oFile.good ())
  {
    static const char *apDefault [] = {"FLAPPY", "BOULDER", "MANIC+", "FRED"};
    for (size_t i = 0 ; i < sizeof (apDefault) / sizeof (apDefault [0]) ; i ++)
      if (GameFind (apDefault [i]) >= 0)
        oFavorites.push_back (apDefault [i]);
    return;
  }
  std::string sLine;
  while (std::getline (oFile, sLine))
  {
    while (!sLine.empty () && (sLine [sLine.size () - 1] == '\r' || sLine [sLine.size () - 1] == ' '))
      sLine.erase (sLine.size () - 1);
    if (!sLine.empty () && GameFind (sLine) >= 0 && !IsFavorite (sLine))
      oFavorites.push_back (sLine);
  }
}

/// The own picture of a game (or of a family by its name): the logo the four first games always had (BOULD+3 has the one of BOULDER,
/// the family MANIC the one of MANIC+), else its screenshot (screenshots/<name>.png).
static std::string OwnImage (const std::string &sTape)
{
  if (sTape == "FLAPPY") return (PMD_SHARE "flappy.bmp");
  if (sTape == "BOULDER" || sTape == "BOULD+3") return (PMD_SHARE "boulder.bmp");
  if (sTape == "MANIC+" || sTape == "MANIC") return (PMD_SHARE "manic.bmp");
  if (sTape == "FRED") return (PMD_SHARE "fred.bmp");
  std::string sPath = std::string (PMD_SHARE "screenshots/") + sTape + ".png";
  return (access (sPath.c_str (), F_OK) == 0 ? sPath : std::string ());
}

/// The picture of a family (the versions of one game): the one of its name (screenshots/MTRON.png), else of its first game that has one.
static std::string FamilyImage (const std::string &sFamily)
{
  std::string sImage = OwnImage (CategoryLabel (sFamily));
  for (size_t j = 0 ; j < oGames.size () && sImage.empty () ; j ++)
    if (oGames [j].sCategory == sFamily)
      sImage = OwnImage (oGames [j].sTape);
  return (sImage);
}

/// The picture of a game in the menus: its own one, else the one of its family (all the versions of MTRON share one).
static std::string FavoriteImage (const std::string &sTape)
{
  std::string sImage = OwnImage (sTape);
  int iGame = sImage.empty () ? GameFind (sTape) : -1;
  if (iGame >= 0 && CategoryIsFamily (oGames [iGame].sCategory))
    sImage = FamilyImage (oGames [iGame].sCategory);
  return (sImage);
}

/// Draw the main menu or the games list.
static void MenuRender ()
{
  if (!inList)
  {
    if (gameIndex >= (int) oFavorites.size ())
      gameIndex = oFavorites.empty () ? 0 : (int) oFavorites.size () - 1;
    std::vector <tMenuItem> oItems;
    for (size_t i = 0 ; i < oFavorites.size () ; i ++)
    {
      tMenuItem sItem = {oFavorites [i], FavoriteImage (oFavorites [i]), HasState (oFavorites [i])};
      oItems.push_back (sItem);
    }
    DSPRenderMenu (fullscreen.w, fullscreen.h, gameIndex, oItems);
    return;
  }
  std::vector <std::string> oNames, oNotes, oInputs;
  std::vector <int> oView = ListView ();
  if (listIndex >= (int) oView.size ())
    listIndex = oView.empty () ? 0 : (int) oView.size () - 1;
  for (size_t i = 0 ; i < oView.size () ; i ++)
  {
    if (oView [i] == LIST_LEVEL_UP)
    {
      oInputs.push_back ("");
      oNames.push_back ("(o uroven vys)");
      oNotes.push_back ("");
      continue;
    }
    if (oView [i] < 0)
    {
      // A category: counts its games (the note stays empty, the name is longer than the name column).
      const std::string &sCategory = oCategories [-oView [i] - 1];
      int iGames = 0;
      for (size_t j = 0 ; j < oGames.size () ; j ++)
        iGames += InCategoryTree (oGames [j].sCategory, sCategory);
      // The state icon of a category: one of its games has a saved state.
      std::string sSaved;
      for (size_t j = 0 ; j < oGames.size () && sSaved.empty () ; j ++)
        if (InCategoryTree (oGames [j].sCategory, sCategory) && HasState (oGames [j].sTape))
          sSaved = "S";
      if (CategoryIsFamily (sCategory))
      {
        // A family (the versions of one game): the name without brackets, the list draws a triangle after it ('F').
        oInputs.push_back (sSaved + "F");
        oNames.push_back (CategoryLabel (sCategory));
      }
      else
      {
        oInputs.push_back (sSaved);
        oNames.push_back ("[ " + CategoryLabel (sCategory) + " ] (" + std::to_string (iGames) + ")");
      }
      oNotes.push_back ("");
      continue;
    }
    oInputs.push_back (oGames [oView [i]].sInput + (HasState (oGames [oView [i]].sTape) ? "S" : "") + (IsFavorite (oGames [oView [i]].sTape) ? "V" : ""));
    oNames.push_back (oGames [oView [i]].sTape);
    oNotes.push_back ((oGames [oView [i]].iModel == 3 ? "PMD 85-2A " : oGames [oView [i]].iModel == 2 ? "PMD 85-2  " : "PMD 85-1  ") + ListNote (oGames [oView [i]].sNote));
  }
  std::string sHelp;
  if (!oView.empty ())
    sHelp = oView [listIndex] == LIST_LEVEL_UP ? "Enter vrati o uroven vys" : oView [listIndex] < 0 ? (CategoryIsFamily (oCategories [-oView [listIndex] - 1]) ? "Enter otevre verze hry" : "Enter otevre kategorii") : oGames [oView [listIndex]].sHelp;
  // The picture of the selected game or family.
  std::string sImage;
  if (!oView.empty () && oView [listIndex] >= 0)
    sImage = FavoriteImage (oGames [oView [listIndex]].sTape);
  else if (!oView.empty () && oView [listIndex] != LIST_LEVEL_UP && CategoryIsFamily (oCategories [-oView [listIndex] - 1]))
  {
    sImage = FamilyImage (oCategories [-oView [listIndex] - 1]);
  }
  DSPRenderGameList (fullscreen.w, fullscreen.h, listIndex, iCategory < 0 ? std::string ("Vsechny hry") : CategoryPath (oCategories [iCategory]), oNames, oNotes, oInputs, sHelp, sImage);
}


/// Esc leaves a game, the games list or the program only when it is held for PMD_ESC_MS (default 800, 0 = at once),
/// a short press by mistake does nothing.
static int iEscHoldMs = 800;
/// Own event type, SDL_USEREVENT is the paint timer of the display.
static const Uint32 iEscEvent = SDL_USEREVENT + 1;
static int iEscSerial = 0;
static bool bEscDown = false;

static Uint32 EscTimerCallback (Uint32 iInterval, void *pSerial)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iEscEvent;
  sEvent.user.code = (Sint32) (intptr_t) pSerial;
  SDL_PushEvent (&sEvent);
  return (0);
}

static void EmulationShutdown ();

/// Tape (with the share path) and model of the running game, the name of its saved state.
static std::string sCurTape;
static int iCurModel = 1;

/** File of the saved state of the running game: PMD_STATEDIR (default "states" in the share directory) and the game name.
 */
static std::string StatePath ()
{
  return (StatePathOf (sCurTape));
}

/// Save the state of the running game (Alt+F5), one state per game.
static void StateSave ()
{
  if (inMenu)
    return;
  std::string sPath = StatePath ();
  if (STSave (sPath, iCurModel))
    printf ("State saved: %s\n", sPath.c_str ());
  else
    fprintf (stderr, "State not saved: %s\n", sPath.c_str ());
}

/// Load the state of the running game (Alt+F9).
static void StateLoad ()
{
  if (inMenu)
    return;
  std::string sPath = StatePath ();
  const char *pError = STLoad (sPath, iCurModel);
  if (pError)
    fprintf (stderr, "State not loaded (%s): %s\n", pError, sPath.c_str ());
  else
  {
    // A game that is still typing its load commands goes on from the loaded state.
    sKeys.clear ();
    printf ("State loaded: %s\n", sPath.c_str ());
  }
}

/// Back in the games list: from a category to the main list (on the row of the category), from the main list to the menu.
static void ListBack ()
{
  if (iCategory >= 0)
  {
    // To the parent (or the main list), on the row of the category that is left.
    int iLeft = iCategory;
    std::string sParent = CategoryParent (oCategories [iCategory]);
    iCategory = -1;
    for (size_t k = 0 ; k < oCategories.size () && !sParent.empty () ; k ++)
      if (oCategories [k] == sParent)
        iCategory = (int) k;
    std::vector <int> oView = ListView ();
    listIndex = 0;
    for (size_t i = 0 ; i < oView.size () ; i ++)
      if (oView [i] == -iLeft - 1)
        listIndex = (int) i;
  }
  else
    inList = false;
  MenuRender ();
}


/// What Esc does: back from a game to the menu, from the games list to the main menu, quit in the main menu.
static void EscapeAction ()
{
  if (!inMenu) {
    EmulationShutdown ();
    DSPMenuInitialize ();
    MenuRender ();
    inMenu = true;
  } else if (inList) {
    ListBack ();
  } else {
    SIMRequestShutdown ();
  }
}


/// Initialize emulation
static void EmulationInitialize (const std::string &sTape, int iModel)
{
  sCurTape = sTape;
  iCurModel = iModel == 3 ? 3 : iModel == 2 ? 2 : 1;
  // Flush garbage
  FlushMemory(0x0000, 65536);
  // Reset processor clock
  iProcessorClock = 0;
  // Set Tape
  gameIn.clear();
  gameIn.push_back(sTape);
  TAPNextInputFile(gameIn);
  {
    // "TAPESAVE" in the note: what the program writes to the tape is kept in <PMD_SHARE>saves/<game> and read back
    // after the tape of the game.
    std::string sSaveName = sTape;
    size_t iShareLength = strlen (PMD_SHARE);
    if (sSaveName.compare (0, iShareLength, PMD_SHARE) == 0)
      sSaveName.erase (0, iShareLength);
    // "TAPESAVE <name>" shares the saved tape with other games (EVIDEDIT creates the data file of EVIDENCE).
    std::string sSaveShared = GameCommandArgument (sTape, "TAPESAVE");
    if (!sSaveShared.empty ())
      sSaveName = sSaveShared;
    std::string sSaveDir = std::string (PMD_SHARE) + "saves/";
#ifdef PMD_STATEHOME
    // Built with PMD_STATEHOME: the saved tapes go to "$HOME/<PMD_STATEHOME>/saves" of the running user as well.
    const char *pHome = getenv ("HOME");
    if (pHome && *pHome)
    {
      sSaveDir = std::string (pHome) + "/" PMD_STATEHOME "/saves/";
    }
#endif
    TAPSetSaveFile (GameHasCommand (sTape, "TAPESAVE") ? sSaveDir + sSaveName : std::string ());
  }

  // "TAPELOOP" in the note: the tape is rewound to its start when the program reads past its end.
  TAPSetLoopFile (GameHasCommand (sTape, "TAPELOOP") ? sTape : std::string ());
  // "TAPEAUTOREWIND": at the end of the tape (and of the saved tape) it starts again from the beginning.
  TAPSetAutoRewind (GameHasCommand (sTape, "TAPEAUTOREWIND") ? sTape : std::string ());
  TAPSetSignal (GameHasCommand (sTape, "TAPESIGNAL"));

  KeysStart (sTape);
  RomModuleLoad (sTape);
  // The [Ctrl] of the previous game is gone with its keyboard.
  bCtrlPending = bCtrlSent = bCtrlSwallow = false;
  iCtrlSerial ++;

  DSPSetNoBlink (GameHasCommand (sTape, "NOBLINK"));
  CPUSetBeepSlow (GameHasCommand (sTape, "BEEPSLOW"));
  if (iModel >= 2)
  {
    if (iModel == 3)
      InitializePMD2A ();
    else
      InitializePMD2 ();
    if (GameHasCommand (sTape, "FONT"))
      FontPatch ();
    if (GameHasCommand (sTape, "TAPESIGNAL"))
      SignalRomPatch ();
    // The PMD 85-2A keeps a copy of the ROM for the time the RAM is mapped over it: taken again, with the patches.
    if (iModel == 3)
      CPUAllRamEnable (abMemoryData + 0x8000);
  }
  else
    InitializePMD1();
  printf("Model initializing..\n");
  CPUInitialize ();
  printf("CPU initializing..\n");
  DSPInitialize();
  printf("DSP initializing..\n");
  KBDInitialize ();
  // Minimal time a key stays pressed [ms]: 60 by default, "KEYHOLD <ms>" in the note of the game sets another (BOMBARDE reads
  // a key once in a pass of its loop of about 150 ms and misses a shorter tap, VOTRELCI would move the gun twice with that),
  // PMD_KBD_HOLD_MS overrides both.
  {
    std::string sHold = GameCommandArgument (sTape, "KEYHOLD");
    iGameKeyHold = getenv ("PMD_KBD_HOLD_MS") ? atoi (getenv ("PMD_KBD_HOLD_MS")) : !sHold.empty () ? atoi (sHold.c_str ()) : 60;
    KBDSetHold (60);
    bGameKeyHoldSet = false;                   // applied by the event loop after the typed text
  }
  printf("KBD initializing..\n");
  SNDInitialize ();
  printf("SND initializing..\n");
  TAPInitialize ();
  printf("Tape initializing..\n");
 
  CPUStartThread ();
  printf("CPU Thread initializing..\n");
}


/// Shutdown emulation
static void EmulationShutdown ()
{
  sKeys.clear ();
  GPDSetGameKeys (std::string ());
  CPUTerminateThread ();
  printf("CPU Thread shutdown..\n");

  TAPShutdown ();
  printf("TAP shutdown..\n");
  SNDShutdown ();
  printf("SND shutdown..\n");
  KBDShutdown ();
  printf("KBD shutdown..\n");
  DSPShutdown();
  printf("DSP shutdown..\n");
  CPUShutdown ();
  printf("CPU shutdown..\n");
}


//--------------------------------------------------------------------------
// Main


//--------------------------------------------------------------------------
// Menu actions: Enter, the popup menu of a held Enter, the test script

/// Own event types: the hold of Enter in a menu, a step of the menu script (SDL_USEREVENT +1 Esc, +2 long press, +3 tap, +4 Ctrl).
static const Uint32 iOkEvent = SDL_USEREVENT + 5;
static const Uint32 iScriptEvent = SDL_USEREVENT + 6;

static Uint32 TickTimerCallback (Uint32 iInterval, void *pArgs)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iTickEvent;
  SDL_PushEvent (&sEvent);
  return (iInterval);
}
/// Enter held this long [ms] in a menu opens the popup menu of the game (PMD_OK_MS, 0 = no popup menu, Enter acts at once).
/// Longer than the time after which the kernel releases a key of a TV remote control (HDMI-CEC) whose release was not sent,
/// a tap of OK must not turn into a hold.
static int iOkHoldMs = 800;
static int iOkSerial = 0;
static bool bOkDown = false, bOkFired = false;
/// The popup menu: the game it is about, its choices (0 is always Run) and the selected one.
static bool bPopup = false;
static std::string sPopupGame;
static int iPopupSel = 0;

static Uint32 OkTimerCallback (Uint32 iInterval, void *pSerial)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iOkEvent;
  sEvent.user.code = (Sint32) (intptr_t) pSerial;
  SDL_PushEvent (&sEvent);
  return (0);
}

/// Starts a game of the games list from a menu.
static void GameRun (int iGame)
{
  DSPClearPopup ();
  DSPMenuShutdown ();
  EmulationInitialize (PMD_SHARE + oGames [iGame].sTape, oGames [iGame].iModel);
  inMenu = false;
}

/// What Enter does in a menu: starts the selected favorite or game, opens a category or goes one level up.
static void MenuEnter ()
{
  if (inList) {
    std::vector <int> oView = ListView ();
    int iCount = (int) oView.size ();
    if (iCount <= 0)
      return;
    if (oView [listIndex] == LIST_LEVEL_UP) {
      ListBack ();
    } else if (oView [listIndex] < 0) {
      // Open the category, the cursor on its first game.
      iCategory = -oView [listIndex] - 1;
      listIndex = 1;
      MenuRender ();
    } else {
      GameRun (oView [listIndex]);
    }
  } else if (gameIndex < (int) oFavorites.size ()) {
    int iGame = GameFind (oFavorites [gameIndex]);
    if (iGame >= 0)
      GameRun (iGame);
  }
}

/// The choices of the popup menu: the game of the main menu is a favorite, the game of the list may or may not be.
static std::vector <std::string> PopupChoices ()
{
  std::vector <std::string> oChoices;
  oChoices.push_back ("Spusti");
  oChoices.push_back (IsFavorite (sPopupGame) ? "Odebrat z oblibenych" : "Pridat do oblibenych");
  return (oChoices);
}

static void PopupDraw ()
{
  DSPSetPopup (sPopupGame, PopupChoices (), iPopupSel);
  MenuRender ();
}

/// Held Enter: the popup menu of the selected game (not of a category).
static void PopupOpen ()
{
  sPopupGame.clear ();
  if (inList)
  {
    std::vector <int> oView = ListView ();
    if (!oView.empty () && oView [listIndex] >= 0)
      sPopupGame = oGames [oView [listIndex]].sTape;
  }
  else if (gameIndex < (int) oFavorites.size ())
    sPopupGame = oFavorites [gameIndex];
  if (sPopupGame.empty ())
    return;
  bPopup = true;
  iPopupSel = 0;
  PopupDraw ();
}

static void PopupClose ()
{
  bPopup = false;
  DSPClearPopup ();
  MenuRender ();
}

/// Enter in the popup menu: Spusti (run), or adds / removes the favorite and saves the file.
static void PopupActivate ()
{
  if (iPopupSel == 0)
  {
    bPopup = false;
    int iGame = GameFind (sPopupGame);
    if (iGame >= 0)
      GameRun (iGame);
    return;
  }
  std::vector <std::string>::iterator xFound = std::find (oFavorites.begin (), oFavorites.end (), sPopupGame);
  if (xFound == oFavorites.end ())
    oFavorites.push_back (sPopupGame);
  else
  {
    int iRemoved = (int) (xFound - oFavorites.begin ());
    oFavorites.erase (xFound);
    // The selection of the main menu stays on the neighbour of the removed tile.
    if (!inList && gameIndex >= iRemoved && gameIndex > 0 && gameIndex >= (int) oFavorites.size ())
      gameIndex = (int) oFavorites.size () - 1;
  }
  FavoritesSave ();
  PopupClose ();
}

/// PMD_MENUSCRIPT: a test script for the menus, steps separated by spaces: an SDL key name (a tap), "<key>:hold" (held
/// PMD_OK_MS and a bit more), "shot=<file>" (BMP of the menu) and "quit". One step in about 400 ms.
struct tScriptStep
{
  int iKind;                    // 0 key down, 1 key up, 2 screenshot, 3 quit
  SDL_Keycode iSym;
  std::string sFile;
  Uint32 iDelay;
};
static std::vector <tScriptStep> oScript;

static void ScriptParse (const std::string &sText, int iHoldMs)
{
  size_t iPos = 0;
  while (iPos < sText.size ())
  {
    size_t iEnd = sText.find (' ', iPos);
    if (iEnd == std::string::npos)
      iEnd = sText.size ();
    std::string sToken = sText.substr (iPos, iEnd - iPos);
    iPos = iEnd + 1;
    if (sToken.empty ())
      continue;
    tScriptStep sStep = {0, SDLK_UNKNOWN, "", 400};
    if (sToken == "quit")
    {
      sStep.iKind = 3;
      oScript.push_back (sStep);
    }
    else if (sToken.compare (0, 5, "shot=") == 0)
    {
      sStep.iKind = 2;
      sStep.sFile = sToken.substr (5);
      oScript.push_back (sStep);
    }
    else
    {
      bool bHold = sToken.size () > 5 && sToken.compare (sToken.size () - 5, 5, ":hold") == 0;
      if (bHold)
        sToken.erase (sToken.size () - 5);
      SDL_Keycode iSym = SDL_GetKeyFromName (sToken.c_str ());
      if (iSym == SDLK_UNKNOWN)
      {
        fprintf (stderr, "PMD_MENUSCRIPT: unknown key '%s'\n", sToken.c_str ());
        continue;
      }
      sStep.iSym = iSym;
      oScript.push_back (sStep);
      sStep.iKind = 1;
      sStep.iDelay = bHold ? iHoldMs + 300 : 100;
      oScript.push_back (sStep);
    }
  }
}

static Uint32 ScriptTimerCallback (Uint32 iInterval, void *pStep)
{
  SDL_Event sEvent;
  SDL_zero (sEvent);
  sEvent.type = iScriptEvent;
  sEvent.user.code = (Sint32) (intptr_t) pStep;
  SDL_PushEvent (&sEvent);
  return (0);
}

/// Runs a step of the script and schedules the next one.
static void ScriptStep (size_t iStep)
{
  if (iStep >= oScript.size ())
    return;
  const tScriptStep &sStep = oScript [iStep];
  if (sStep.iKind == 0 || sStep.iKind == 1)
  {
    SDL_Event sEvent;
    SDL_zero (sEvent);
    sEvent.type = sStep.iKind == 0 ? SDL_KEYDOWN : SDL_KEYUP;
    sEvent.key.state = sStep.iKind == 0 ? SDL_PRESSED : SDL_RELEASED;
    sEvent.key.keysym.sym = sStep.iSym;
    sEvent.key.keysym.scancode = SDL_GetScancodeFromKey (sStep.iSym);
    SDL_PushEvent (&sEvent);
  }
  else if (sStep.iKind == 2)
    DSPSaveMenuScreenshot (sStep.sFile.c_str ());
  else
  {
    SIMRequestShutdown ();
    return;
  }
  if (iStep + 1 < oScript.size ())
    SDL_AddTimer (oScript [iStep + 1].iDelay, ScriptTimerCallback, (void *) (intptr_t) (iStep + 1));
}

static void Usage (FILE *pOut)
{
  fprintf (pOut, "Usage: simpmd [GAME]   start the game GAME of games.lst (the case does not matter), no argument: the menu\n"
                 "       simpmd --list   print the names of the games\n"
                 "       simpmd --help   this text\n"
                 "The environment variables of the test and debug modes are in doc/usage.md.\n");
}

int main (int iArgC, const char *apArgV [])
{
  // Command line, checked before anything is opened: simpmd [GAME | --list | --help]
  bool bList = false;
  const char *pArgGame = NULL;
  if (iArgC > 2)
  {
    fprintf (stderr, "simpmd: too many arguments\n");
    Usage (stderr);
    return (2);
  }
  if (iArgC == 2)
  {
    if (strcmp (apArgV [1], "-h") == 0 || strcmp (apArgV [1], "--help") == 0)
    {
      Usage (stdout);
      return (0);
    }
    else if (strcmp (apArgV [1], "-l") == 0 || strcmp (apArgV [1], "--list") == 0)
      bList = true;
    else if (apArgV [1][0] == '-')
    {
      fprintf (stderr, "simpmd: unknown option '%s'\n", apArgV [1]);
      Usage (stderr);
      return (2);
    }
    else
      pArgGame = apArgV [1];
  }

  // SDL Init
  SDL_CheckZero (SDL_Init (SDL_INIT_TIMER | SDL_INIT_VIDEO));
  // The sound is not a reason to give up (a machine without an audio device), see SNDInitialize.
  if (SDL_InitSubSystem (SDL_INIT_AUDIO) != 0)
    fprintf (stderr, "No sound: %s\n", SDL_GetError ());
  printf("SDL Initializing..\n");
  // The own event types are SDL_USEREVENT (paint) to SDL_USEREVENT + 7: reserved, so that SDL gives them to nobody else.
  if (SDL_RegisterEvents (8) != SDL_USEREVENT)
    fprintf (stderr, "simpmd: the user events are taken, the timers of the keys may misbehave\n");
  SDL_AddTimer (PMD_TICK_MS, TickTimerCallback, NULL);
  // Gamepads, their buttons arrive as key events (PMD_PAD overrides the keys)
  GPDInitialize ();
  //Disable cursor
  SDL_ShowCursor (0);
  // Get fullscreen resolution
  SDL_CheckZero (SDL_GetCurrentDisplayMode(0, &fullscreen));
  DSPResolution (&fullscreen.w, &fullscreen.h);
  // Test mode: PMD_AUTOSTART=<0-3> skips logo and menu, PMD_KEYS=<text> types
  // the text on the simulated keyboard ('|' is Enter, '_' a 2 s pause), PMD_SCREENSHOT=<file>
  // saves the screen after PMD_SCREENSHOT_MS (default 3000) and exits.
  // PMD_KEY_MS is the key press and release time of PMD_KEYS (default 100).
  const char *pKeyMs = getenv ("PMD_KEY_MS");
  iKeyMs = pKeyMs ? atoi (pKeyMs) : 100;
  const char *pAutostart = getenv ("PMD_AUTOSTART");
  // The game of the command line wins over PMD_GAME.
  const char *pGame = pArgGame ? pArgGame : getenv ("PMD_GAME");
  GamesLoad ();
  FavoritesLoad ();
  if (bList)
  {
    for (size_t i = 0 ; i < oGames.size () ; i ++)
      printf ("%s\n", oGames [i].sTape.c_str ());
    SDL_Quit ();
    return (0);
  }
  // Remote control from a browser: PMD_WEB=<port>, PMD_WEB_TOKEN=<secret> is required in the URL (?t=<secret>).
  const char *pWeb = getenv ("PMD_WEB");
  if (pWeb)
  {
    WebStatePublish ();
    WEBInitialize (atoi (pWeb), getenv ("PMD_WEB_TOKEN"), WebState);
  }
  const char *pScreenshot = getenv ("PMD_SCREENSHOT");
  const char *pScreenshotMs = getenv ("PMD_SCREENSHOT_MS");
  Uint32 iScreenshotTime = SDL_GetTicks () + (pScreenshotMs ? atoi (pScreenshotMs) : 3000);
  if (pAutostart)
  {
    gameIndex = atoi (pAutostart) & 3;
    EmulationInitialize (gameSelect[gameIndex], 1);
    inMenu = false;
  }
  else if (pGame)
  {
    // Test mode: PMD_GAME=<name> starts a game of the games list.
    size_t iFound = 0;
    while (iFound < oGames.size () && strcasecmp (oGames [iFound].sTape.c_str (), pGame) != 0)
      iFound ++;
    if (iFound == oGames.size ())
    {
      fprintf (stderr, "'%s' is not in games.lst (simpmd --list prints the names)\n", pGame);
      // names that start with the same letters
      for (size_t i = 0 ; i < oGames.size () ; i ++)
        if (strncasecmp (oGames [i].sTape.c_str (), pGame, 3) == 0)
          fprintf (stderr, "  maybe %s\n", oGames [i].sTape.c_str ());
      SDL_Quit ();
      return (1);
    }
    EmulationInitialize (PMD_SHARE + oGames [iFound].sTape, oGames [iFound].iModel);
    inMenu = false;
  }
  else
  {
    //Logo
    DSPLogoInitialize ();
    DSPRenderLogo (fullscreen.w, fullscreen.h);
    DSPLogoShutdown ();
    // Menu
    DSPMenuInitialize ();
    MenuRender ();
  }
  const char *pOkMs = getenv ("PMD_OK_MS");
  if (pOkMs && atoi (pOkMs) >= 0)
    iOkHoldMs = atoi (pOkMs);
  const char *pScript = getenv ("PMD_MENUSCRIPT");
  if (pScript && inMenu)
  {
    ScriptParse (pScript, iOkHoldMs);
    if (!oScript.empty ())
      SDL_AddTimer (1500, ScriptTimerCallback, (void *) (intptr_t) 0);
  }
  const char *pEscMs = getenv ("PMD_ESC_MS");
  if (pEscMs && atoi (pEscMs) >= 0)
    iEscHoldMs = atoi (pEscMs);
  // Event loop
  SDL_Event sEvent;
  // Debug: PMD_KEYLOG=<file> logs keyboard and window events.
  const char *pKeyLog = getenv ("PMD_KEYLOG");
  FILE *pLog = pKeyLog ? fopen (pKeyLog, "a") : NULL;

  while (!SIMQueryShutdown ())
  {
    SDL_WaitEvent (&sEvent);
    // Whatever the event changes (also on "continue") is published for the web thread at the end of the pass.
    struct WebPublisher { bool bOn; ~WebPublisher () { if (bOn) WebStatePublish (); } } oPublisher = { pWeb != NULL && WebPublishDue (sEvent.type) };
    if (pLog && sEvent.type != SDL_USEREVENT && sEvent.type != iTickEvent)
    {
      fprintf (pLog, "%u type=0x%x inMenu=%d focus=%p", SDL_GetTicks (), sEvent.type, inMenu, (void *) SDL_GetKeyboardFocus ());
      if (sEvent.type == SDL_KEYDOWN || sEvent.type == SDL_KEYUP)
        fprintf (pLog, " sym=%d (%s) repeat=%d", sEvent.key.keysym.sym, SDL_GetKeyName (sEvent.key.keysym.sym), sEvent.key.repeat);
      if (sEvent.type == SDL_WINDOWEVENT)
        fprintf (pLog, " win=%d", sEvent.window.event);
      fprintf (pLog, "\n");
      fflush (pLog);
    }
    GPDEventHandler (&sEvent);
    if (sEvent.type == iTickEvent)
      continue;
    // The window was exposed, resized or restored, paint the screen again even if it did not change.
    if (sEvent.type == SDL_WINDOWEVENT)
      DSPInvalidate ();
    if (sEvent.type == iCtrlEvent)
    {
      // [Ctrl] was held PMD_CTRL_MS and no F5 or F9 came: it is the STOP key.
      if (bCtrlPending && sEvent.user.code == iCtrlSerial)
      {
        bCtrlPending = false;
        bCtrlSent = true;
        KeySend (iCtrlSym, SDL_PRESSED);
      }
      continue;
    }
    if (sEvent.type == iTapEvent)
    {
      KeySend ((SDL_Keycode) sEvent.user.code, SDL_RELEASED);
      continue;
    }
    if (sEvent.type == iLongEvent)
    {
      // The hold time of a long press has passed: the long key goes down (the short key is not sent).
      if (!inMenu && bLongDown && !bLongFired && sEvent.user.code == iLongSerial)
      {
        bLongFired = true;
        KeySend (oRemapLong [iLongKey].iLong, SDL_PRESSED);
      }
      continue;
    }
    if (sEvent.type == iScriptEvent)
    {
      ScriptStep ((size_t) sEvent.user.code);
      continue;
    }
    if (sEvent.type == iOkEvent)
    {
      // Enter was held PMD_OK_MS in a menu: the popup menu of the game.
      if (inMenu && bOkDown && !bOkFired && !bPopup && sEvent.user.code == iOkSerial)
      {
        bOkFired = true;
        PopupOpen ();
      }
      continue;
    }
    if (sEvent.type == iEscEvent)
    {
      // The hold time of Esc has passed, unless the key was released meanwhile.
      if (bEscDown && sEvent.user.code == iEscSerial)
      {
        bEscDown = false;
        iEscSerial ++;
        EscapeAction ();
      }
      continue;
    }
    if (inMenu && bPopup && (sEvent.type == SDL_KEYDOWN || sEvent.type == SDL_KEYUP) && sEvent.key.keysym.sym == SDLK_ESCAPE)
    {
      // Esc closes the popup menu at once (the hold of Esc is for leaving the menu).
      if (sEvent.type == SDL_KEYDOWN && !sEvent.key.repeat)
        PopupClose ();
      continue;
    }
    if (inMenu && (sEvent.type == SDL_KEYDOWN || sEvent.type == SDL_KEYUP) && sEvent.key.keysym.sym == SDLK_RETURN && (bPopup || iOkHoldMs > 0))
    {
      if (bPopup)
      {
        // In the popup menu a new press of Enter chooses, the release of the Enter that opened it is ignored.
        if (sEvent.type == SDL_KEYDOWN && !sEvent.key.repeat)
          PopupActivate ();
        else if (sEvent.type == SDL_KEYUP)
          bOkDown = false;
      }
      else if (sEvent.type == SDL_KEYDOWN)
      {
        if (!sEvent.key.repeat)
        {
          bOkDown = true;
          bOkFired = false;
          SDL_AddTimer (iOkHoldMs, OkTimerCallback, (void *) (intptr_t) ++iOkSerial);
        }
      }
      else
      {
        // A short press acts when the key is released, unless the hold has opened the popup menu.
        if (bOkDown && !bOkFired)
          MenuEnter ();
        bOkDown = false;
        iOkSerial ++;
      }
      continue;
    }
    if (inMenu && bPopup && sEvent.type == SDL_KEYDOWN)
    {
      // Up and down choose in the popup menu, the other keys do nothing.
      SDL_Keycode iPopupSym = sEvent.key.keysym.sym;
      bool bPopupUp = iPopupSym == SDLK_UP || iPopupSym == SDLK_e;
      bool bPopupDown = iPopupSym == SDLK_DOWN || iPopupSym == SDLK_z;
      if (bPopupUp || bPopupDown)
      {
        int iChoices = (int) PopupChoices ().size ();
        iPopupSel = (iPopupSel + (bPopupDown ? 1 : iChoices - 1)) % iChoices;
        PopupDraw ();
      }
      continue;
    }
    // The other keys do nothing while the popup menu is open; the other events (quit!) go on.
    if (inMenu && bPopup && (sEvent.type == SDL_KEYDOWN || sEvent.type == SDL_KEYUP))
      continue;
    if ((sEvent.type == SDL_KEYDOWN || sEvent.type == SDL_KEYUP) && sEvent.key.keysym.sym == SDLK_ESCAPE && iEscHoldMs > 0)
    {
      if (sEvent.type == SDL_KEYDOWN)
      {
        if (!sEvent.key.repeat)
        {
          // A new press restarts the hold, even when the release of the previous one was lost.
          bEscDown = true;
          SDL_AddTimer (iEscHoldMs, EscTimerCallback, (void *) (intptr_t) ++iEscSerial);
        }
      }
      else
      {
        bEscDown = false;
        iEscSerial ++;
      }
      continue;
    }
    switch (sEvent.type)
    {
      case SDL_KEYUP:
      case SDL_KEYDOWN:
        if (inMenu) {
           if (sEvent.type == SDL_KEYDOWN) {
             SDL_Keycode iSym = sEvent.key.keysym.sym;
             // [E] and [Y] are the PMD 85 up and down, the gamepad sends them
             bool bUp = (iSym == SDLK_UP || iSym == SDLK_e);
             bool bDown = (iSym == SDLK_DOWN || iSym == SDLK_z);
             if (inList) {
               std::vector <int> oView = ListView ();
               int iCount = (int) oView.size ();
               if (iSym == SDLK_ESCAPE) {
                 ListBack ();
                 continue;
               }
               if (bUp || bDown || iSym == SDLK_LEFT || iSym == SDLK_RIGHT || iSym == SDLK_PAGEUP || iSym == SDLK_PAGEDOWN) {
                 int iStep = bUp ? -1 : bDown ? 1 : (iSym == SDLK_LEFT || iSym == SDLK_PAGEUP) ? -10 : 10;
                 // Single steps wrap around, pages stop at the ends.
                 if (iStep == -1 || iStep == 1)
                   listIndex = (listIndex + iStep + iCount) % iCount;
                 else
                   listIndex = std::max (0, std::min (iCount - 1, listIndex + iStep));
                 MenuRender ();
               }
               if (iSym == SDLK_RETURN)
                 MenuEnter ();
             } else {
               if (iSym == SDLK_ESCAPE) { 
                SIMRequestShutdown ();
               }
               if (iSym == SDLK_LEFT && !oFavorites.empty ()) {
                 gameIndex = (gameIndex + (int) oFavorites.size () - 1) % (int) oFavorites.size ();
                 MenuRender ();
               }
               if (iSym == SDLK_RIGHT && !oFavorites.empty ()) {
                 gameIndex = (gameIndex + 1) % (int) oFavorites.size ();
                 MenuRender ();
               }
               if (bUp && !oGames.empty ()) {
                 inList = true;
                 MenuRender ();
               }
               if (iSym == SDLK_RETURN)
                 MenuEnter ();
             }
           }
        } else {
          if (sEvent.key.keysym.sym == SDLK_LCTRL || sEvent.key.keysym.sym == SDLK_RCTRL) {
            // [Ctrl] (STOP of the PMD 85) is held back, see PMD_CTRL_MS.
            if (sEvent.type == SDL_KEYDOWN) {
              if (!sEvent.key.repeat && !bCtrlPending && !bCtrlSent) {
                bCtrlPending = true;
                iCtrlSym = sEvent.key.keysym.sym;
                SDL_AddTimer (PMD_CTRL_MS, CtrlTimerCallback, (void *) (intptr_t) ++iCtrlSerial);
              }
            } else if (bCtrlSwallow) {
              bCtrlSwallow = false;
            } else if (bCtrlPending) {
              // A short press: the key goes down now and up after PMD_TAP_MS.
              bCtrlPending = false;
              iCtrlSerial ++;
              KeySend (iCtrlSym, SDL_PRESSED);
              SDL_AddTimer (PMD_TAP_MS, TapTimerCallback, (void *) (intptr_t) iCtrlSym);
            } else if (bCtrlSent) {
              bCtrlSent = false;
              KeySend (iCtrlSym, SDL_RELEASED);
            }
          } else if ((sEvent.key.keysym.mod & (KMOD_ALT | KMOD_CTRL)) && (sEvent.key.keysym.sym == SDLK_F5 || sEvent.key.keysym.sym == SDLK_F9)) {
            // Alt+F5 or Ctrl+F5 saves, Alt+F9 or Ctrl+F9 loads the state of the game (the keys do not reach the PMD 85).
            if (sEvent.type == SDL_KEYDOWN && !sEvent.key.repeat) {
              if (sEvent.key.keysym.mod & KMOD_CTRL) {
                // The [Ctrl] of the state key is not STOP: not sent yet, or released when it went already.
                if (bCtrlPending) {
                  bCtrlPending = false;
                  iCtrlSerial ++;
                  bCtrlSwallow = true;
                } else if (bCtrlSent) {
                  bCtrlSent = false;
                  KeySend (iCtrlSym, SDL_RELEASED);
                  bCtrlSwallow = true;
                }
              }
              if (sEvent.key.keysym.sym == SDLK_F5)
                StateSave ();
              else
                StateLoad ();
            }
          } else if (sEvent.key.keysym.sym == SDLK_ESCAPE) {
            EmulationShutdown ();
            DSPMenuInitialize ();
            MenuRender ();
            inMenu = true;
          } else {
            SDL_Keycode iHost = sEvent.key.keysym.sym;
            Uint32 iNow = SDL_GetTicks ();

            // The cursor key last used (iLastDirection), a key of REMAP "@" presses it too.
            if ((iHost == SDLK_LEFT || iHost == SDLK_RIGHT) && !sEvent.key.repeat)
            {
              iLastDirection = iHost;
              bDirectionDown = sEvent.type == SDL_KEYDOWN;
              iDirectionTime = iNow;
            }
            std::map <SDL_Keycode, tLongKey>::const_iterator xLong = oRemapLong.find (iHost);
            if (xLong != oRemapLong.end ())
            {
              if (sEvent.type == SDL_KEYDOWN)
              {
                if (!sEvent.key.repeat && !bLongDown)
                {
                  bLongDown = true;
                  bLongFired = false;
                  iLongKey = iHost;
                  SDL_AddTimer (PMD_LONG_MS, LongTimerCallback, (void *) (intptr_t) ++iLongSerial);
                  // The held key goes down at once (a jump must not wait for the release), with the cursor key
                  // last used when it is not pressed any more but was released a moment ago.
                  if (xLong->second.iHeld != SDLK_UNKNOWN)
                  {
                    iLongDirection = SDLK_UNKNOWN;
                    if (xLong->second.bHeldDirection && iLastDirection != SDLK_UNKNOWN && !bDirectionDown
                        && (Sint32) (iNow - iDirectionTime) <= PMD_DIRECTION_MS)
                    {
                      iLongDirection = iLastDirection;
                      KeySend (iLongDirection, SDL_PRESSED);
                    }
                    KeySend (xLong->second.iHeld, SDL_PRESSED);
                  }
                }
              }
              else if (bLongDown && iLongKey == iHost)
              {
                bLongDown = false;
                iLongSerial ++;
                if (xLong->second.iHeld != SDLK_UNKNOWN)
                {
                  KeySend (xLong->second.iHeld, SDL_RELEASED);
                  if (iLongDirection != SDLK_UNKNOWN)
                    KeySend (iLongDirection, SDL_RELEASED);
                  iLongDirection = SDLK_UNKNOWN;
                }
                if (bLongFired)
                  KeySend (xLong->second.iLong, SDL_RELEASED);
                else
                {
                  KeySend (xLong->second.iShort, SDL_PRESSED);
                  SDL_AddTimer (PMD_TAP_MS, TapTimerCallback, (void *) (intptr_t) xLong->second.iShort);
                }
              }
              break;
            }
            std::map <SDL_Keycode, SDL_Keycode>::const_iterator xRemap = oRemap.find (iHost);
            if (xRemap != oRemap.end ())
              sEvent.key.keysym.sym = xRemap->second;
            if (oRemapDirection.count (iHost) && !sEvent.key.repeat)
            {
              SDL_KeyboardEvent sDirection = sEvent.key;
              if (sEvent.type == SDL_KEYDOWN && iLastDirection != SDLK_UNKNOWN && !bDirectionDown
                  && (Sint32) (iNow - iDirectionTime) <= PMD_DIRECTION_MS)
              {
                iHeldDirection = iLastDirection;
                sDirection.keysym.sym = iHeldDirection;
                KBDEventHandler (&sDirection);
              }
              else if (sEvent.type == SDL_KEYUP && iHeldDirection != SDLK_UNKNOWN)
              {
                sDirection.keysym.sym = iHeldDirection;
                KBDEventHandler (&sDirection);
                iHeldDirection = SDLK_UNKNOWN;
              }
            }
            KBDEventHandler ((SDL_KeyboardEvent *) &sEvent);
          }
        }
        break;
      case SDL_USEREVENT:
        if (!inMenu) {
          KBDPoll ();
          Uint32 iPaintStart = SDL_GetTicks ();
          DSPPaintHandler ();
          // Drop paint events queued while painting, a slow machine would fall behind and delay the keys.
          SDL_FlushEvent (SDL_USEREVENT);
          if (pLog)
          {
            // Debug: paint duration and queue lag (events pending) every 25th frame.
            static int iFrame = 0;
            if (++iFrame % 25 == 0)
            {
              SDL_PumpEvents ();
              fprintf (pLog, "%u paint=%ums pending=%d\n", SDL_GetTicks (), SDL_GetTicks () - iPaintStart, SDL_PeepEvents (NULL, 0, SDL_PEEKEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT));
              fflush (pLog);
            }
          }
          // The first key waits until the simulated processor ran for 2 s, the monitor is not ready sooner and
          // a slow machine (Raspberry Pi) loses the first keys when the time is only counted in real time.
          bool bKeysReady = iKeyPos > 0 || bKeyDown || iProcessorClock >= 2 * PMD_CLOCK;
          if (iKeyPos < (int) sKeys.size () && bKeysReady && SDL_GetTicks () >= iKeyTime) {
            SDL_KeyboardEvent sKey = {};
            char cKey = sKeys [iKeyPos];
            Uint32 iNow = SDL_GetTicks ();
            // The typing follows the simulated keyboard instead of guessing delays: a key is pressed only while the
            // program scans the keyboard (a monitor waiting for a command, not a tape load or a game loop that does not
            // look) and released after the program has read it. The limits only keep it from waiting forever.
            Uint32 iScan = KBDScanCount ();
            bool bScanning = iScan != iTypeScanPrev;
            iTypeScanPrev = iScan;
            if (iTypeWaitSince == 0)
              iTypeWaitSince = iNow;
            if (cKey == '_') {
              iKeyPos ++;
              iKeyTime = iNow + 2000;
              iTypeWaitSince = 0;
            } else if (cKey == '~') {
              // Wait until the tape stops loading (nothing was read for PMD_TAPE_IDLE_MS) and the program scans the keyboard again.
              static int iIdleMs = getenv ("PMD_TAPE_IDLE_MS") ? atoi (getenv ("PMD_TAPE_IDLE_MS")) : 700;
              Uint32 iLastRead = TAPLastRead ();
              // No tape was read at all (a BASIC game): the program has to scan the keyboard for 2 s.
              bool bIdle = iLastRead != 0 ? iNow - iLastRead > (Uint32) iIdleMs : iNow - iTypeWaitSince > 2000;
              if (bIdle && bScanning) {
                iKeyPos ++;
                iKeyTime = iNow + 500;
                iTypeWaitSince = 0;
              } else {
                iKeyTime = iNow + 40;
              }
            } else if (cKey == '[' || cKey == ']') {
              // "[" holds [Shift] down, "]" lets it go.
              SDL_KeyboardEvent sShiftKey = {};
              sShiftKey.keysym.sym = SDLK_LSHIFT;
              sShiftKey.state = cKey == '[' ? SDL_PRESSED : SDL_RELEASED;
              KBDEventHandler (&sShiftKey);
              iKeyPos ++;
              iKeyTime = iNow + 40;
              iTypeWaitSince = 0;
            } else if (cKey == '{') {
              // POKE of the games list: "{39A4=01}", a hex address and a hex value.
              unsigned iAddr = 0, iValue = 0;
              size_t iClose = sKeys.find ('}', iKeyPos);
              if (sscanf (sKeys.c_str () + iKeyPos + 1, "%x=%x", &iAddr, &iValue) == 2 && iAddr < 65536)
                abMemoryData [iAddr] = (byte) iValue;
              iKeyPos = iClose == std::string::npos ? sKeys.size () : iClose + 1;
              iKeyTime = iNow + 40;
              iTypeWaitSince = 0;
            } else if (!bKeyDown && !bScanning && iNow - iTypeWaitSince < 5000) {
              // Not scanning yet, try again at the next paint.
              iKeyTime = iNow + 40;
            } else if (bKeyDown && KBDSeenCount () == iTypeSeenAtPress && iNow - iTypePressTime < 3000) {
              // The program has not read the key yet, keep it pressed.
              iKeyTime = iNow + 20;
            } else {
            bool bShiftKey;
            sKey.keysym.sym = TypedKey (cKey, bShiftKey);
            sKey.state = bKeyDown ? SDL_RELEASED : SDL_PRESSED;
            if (!bKeyDown) {
              iTypeSeenAtPress = KBDSeenCount ();
              iTypePressTime = iNow;
              if (bShiftKey) {
                // The symbol needs [Shift]: it goes down before the key and up after it.
                SDL_KeyboardEvent sShift = {};
                sShift.keysym.sym = SDLK_LSHIFT;
                sShift.state = SDL_PRESSED;
                KBDEventHandler (&sShift);
                bTypeShiftHeld = true;
              }
            }
            KBDEventHandler (&sKey);
            if (bKeyDown && bTypeShiftHeld) {
              SDL_KeyboardEvent sShift = {};
              sShift.keysym.sym = SDLK_LSHIFT;
              sShift.state = SDL_RELEASED;
              KBDEventHandler (&sShift);
              bTypeShiftHeld = false;
            }
            if (bKeyDown) {
              iKeyPos ++;
              iTypeWaitSince = 0;
            }
            bKeyDown = !bKeyDown;
            iKeyTime = iNow + iKeyMs;
            }
          }
          if (!bGameKeyHoldSet && iKeyPos >= (int) sKeys.size () && !bKeyDown) {
            KBDSetHold (iGameKeyHold);
            bGameKeyHoldSet = true;
          }
          // Test: PMD_STATE_SCRIPT="s5000,l9000" saves (s) and loads (l) the state at the given simulated times [ms].
          static const char *pStateScript = getenv ("PMD_STATE_SCRIPT");
          static size_t iStatePos = 0;
          if (pStateScript && pStateScript [iStatePos]) {
            long iAt = atol (pStateScript + iStatePos + 1);
            if (iProcessorClock >= (long long) iAt * (PMD_CLOCK / 1000)) {
              if (pStateScript [iStatePos] == 's') StateSave ();
              else StateLoad ();
              const char *pNext = strchr (pStateScript + iStatePos, ',');
              iStatePos = pNext ? pNext - pStateScript + 1 : strlen (pStateScript);
            }
          }
          if (pScreenshot && SDL_GetTicks () >= iScreenshotTime) {
            DSPSaveScreenshot (pScreenshot);
            SIMRequestShutdown ();
          }
        }
        break;
      case SDL_QUIT:
        SIMRequestShutdown ();
        break;
    }
  }

  // Emulation or menu shutdown
  if (inMenu) {
    DSPMenuShutdown ();
  } else {
    EmulationShutdown ();
  }
  // Quit
  WEBShutdown ();
  GPDShutdown ();
  SDL_Quit ();

  return (0);
}


//--------------------------------------------------------------------------

