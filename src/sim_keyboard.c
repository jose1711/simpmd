/*

SIM_KEYBOARD.C

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

#include "sim_common.h"

#include <map>
#include <utility>


//--------------------------------------------------------------------------
// Data

/// Number of simulated keyboard columns.
#define PMD_KBD_COLUMNS         16
/// Mask for keyboard column information.
#define PMD_KBD_COLUMN_MASK     0xF;

/// Number of simulated keyboard rows.
#define PMD_KBD_ROWS            5
/// Mask for keyboard row information.
#define PMD_KBD_ROW_MASK        0x1F;

/// Mask for keyboard SHIFT key.
#define PMD_KBD_SHIFT_MASK      (1 << 5)
/// Mask for keyboard STOP key.
#define PMD_KBD_STOP_MASK       (1 << 6)

/// Mask for defined bits.
#define PMD_KBD_DEF_MASK        0x7F;


struct tKeyLocation
{
  byte iRowMask;
  byte iColumnIndex;

  tKeyLocation (byte iNewRowMask, byte iNewColumnIndex) : iRowMask (iNewRowMask), iColumnIndex (iNewColumnIndex) { };
};

/// Map for translation of keyboard events.
typedef std::map <SDL_Keycode, tKeyLocation> tKeyMap;
static tKeyMap oKeyMap;


/// Current keyboard matrix state.
static volatile byte abKeyMatrix [PMD_KBD_COLUMNS];
/// Current keyboard shifts state.
static volatile byte iKeyShifts;
/// Current keyboard column
static volatile byte iKeyColumn;


//--------------------------------------------------------------------------
// Port operations

/** Read from keyboard row register.
 */
/// Reads of the keyboard row by the simulated processor, and those that returned a pressed key (for typing PMD_KEYS).
static volatile uint32 iScanCount;
static volatile uint32 iSeenCount;

byte KBDReadRow ()
{
  byte iRow = iKeyShifts & abKeyMatrix [iKeyColumn];
  iScanCount ++;
  // The bits 0-4 are the keys of the column, 5 and 6 [Shift] and [Stop]; all set is nothing pressed.
  if ((iRow & 0x7F) != 0x7F)
    iSeenCount ++;
  return (iRow);
}

/// Number of keyboard row reads so far, it grows only while the program scans the keyboard (waits for a key).
uint32 KBDScanCount ()
{
  return (iScanCount);
}

/// Number of keyboard row reads that saw a pressed key.
uint32 KBDSeenCount ()
{
  return (iSeenCount);
}


/** Read from keyboard column register.
 */
byte KBDReadColumn ()
{
  return (iKeyColumn);
}


/** Write to keyboard column register.
 *
 *  @arg iData The written value.
 */
void KBDWriteColumn (byte iData)
{
  iKeyColumn = iData & PMD_KBD_COLUMN_MASK;
}


//--------------------------------------------------------------------------
// Event handlers

/** Handler for keyboard events.
 *
 *  Sets the information in the simulated keyboard
 *  matrix based on the keyboard events.
 *
 *  @arg pEvent The event to handle.
 */
/// Games with JOYONLY in the note: while the program polls the joystick, the keys that are the joystick (the cursor keys and Space,
/// also the pad keys on them) do not go to the keyboard matrix, only to the joystick bits. JETSET WILLY takes Space on the keyboard
/// for pause, so the fire button would pause the game when it is not held together with a direction.
static volatile bool bJoystickOnly = false;
static volatile Uint32 iJoystickRead = 0;

void KBDSetJoystickOnly (bool bOn)
{
  bJoystickOnly = bOn;
}

void KBDJoystickPolled ()
{
  iJoystickRead = SDL_GetTicks () + 1;
}

static void KBDApply (SDL_Keycode iSym, Uint8 iState)
{
  if (iState == SDL_PRESSED && bJoystickOnly && iJoystickRead && SDL_GetTicks () - iJoystickRead < 150 &&
      (iSym == SDLK_SPACE || iSym == SDLK_LEFT || iSym == SDLK_RIGHT || iSym == SDLK_UP || iSym == SDLK_DOWN))
    return;                     // a release is always applied, so no key stays down
  // Find the location of the key in the key map
  tKeyMap::iterator xLocation = oKeyMap.find (iSym);
  if (xLocation != oKeyMap.end ())
  {
    switch (iState)
    {
      case SDL_PRESSED:
        // Pressing a key resets the relevant bit of the keyboard matrix
        abKeyMatrix [xLocation->second.iColumnIndex] &= ~xLocation->second.iRowMask;
        break;
      case SDL_RELEASED:
        // Releasing a key sets the relevant bit of the keyboard matrix
        abKeyMatrix [xLocation->second.iColumnIndex] |= xLocation->second.iRowMask;
        break;
    }
  }
  // Shift keys have special handling
  else switch (iSym)
  {
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
      switch (iState)
      {
        case SDL_PRESSED:  iKeyShifts &= ~PMD_KBD_SHIFT_MASK; break;
        case SDL_RELEASED: iKeyShifts |=  PMD_KBD_SHIFT_MASK; break;
      }
      break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      switch (iState)
      {
        case SDL_PRESSED:  iKeyShifts &= ~PMD_KBD_STOP_MASK; break;
        case SDL_RELEASED: iKeyShifts |=  PMD_KBD_STOP_MASK; break;
      }
      break;
  }
}



/** Minimal time a key stays pressed [ms].
 *
 *  The event loop can fall behind on a slow machine and deliver press and release
 *  of a key at once, the emulated keyboard scan would then never see the key.
 */
#define PMD_KBD_HOLD_MS         60

/// Minimal hold time of the running game [ms], PMD_KBD_HOLD_MS by default (see KBDSetHold).
static int iHoldMs = PMD_KBD_HOLD_MS;

/** Sets the minimal time a key stays pressed.
 *
 *  A game in BASIC reads the key (INKEY) once in a pass of its loop, in BOMBARDE it is about 150 ms,
 *  a shorter tap of a gamepad button is missed in a part of the passes.
 */
void KBDSetHold (int iMs)
{
  iHoldMs = iMs;
}

/// Time of the last press of a key.
static std::map <SDL_Keycode, Uint32> oPressTime;
/// Releases waiting for the minimal hold time, key and time of the release.
static std::map <SDL_Keycode, Uint32> oPendingRelease;


/// The cursor keys and Space as the PMD 85 joystick, a low bit is a pressed key (see GPDJoystickByte for the bits).
static volatile byte iJoystickKeys = 0xFF;

byte KBDJoystickByte ()
{
  return (iJoystickKeys);
}

static void KBDJoystickKey (SDL_Keycode iSym, bool bPressed)
{
  byte iMask = iSym == SDLK_DOWN ? 0x01 : iSym == SDLK_UP ? 0x02 : iSym == SDLK_RIGHT ? 0x04 :
               iSym == SDLK_LEFT ? 0x08 : iSym == SDLK_SPACE ? 0x10 : 0;
  if (iMask)
    iJoystickKeys = bPressed ? iJoystickKeys & ~iMask : iJoystickKeys | iMask;
}


void KBDEventHandler (const SDL_KeyboardEvent *pEvent)
{
  SDL_Keycode iSym = pEvent->keysym.sym;
  KBDJoystickKey (iSym, pEvent->state == SDL_PRESSED);
  Uint32 iNow = SDL_GetTicks ();
  switch (pEvent->state)
  {
    case SDL_PRESSED:
      oPressTime [iSym] = iNow;
      oPendingRelease.erase (iSym);
      TAPKeyPressed ();
      KBDApply (iSym, SDL_PRESSED);
      break;
    case SDL_RELEASED:
      if (oPressTime.count (iSym) && iNow - oPressTime [iSym] < (Uint32) iHoldMs)
        oPendingRelease [iSym] = oPressTime [iSym] + iHoldMs;
      else
        KBDApply (iSym, SDL_RELEASED);
      break;
  }
}


/** Releases the keys held for the minimal time, called periodically. */
void KBDPoll ()
{
  Uint32 iNow = SDL_GetTicks ();
  for (std::map <SDL_Keycode, Uint32>::iterator xKey = oPendingRelease.begin (); xKey != oPendingRelease.end ();)
  {
    if ((Sint32) (iNow - xKey->second) >= 0)
    {
      KBDApply (xKey->first, SDL_RELEASED);
      oPendingRelease.erase (xKey++);
    }
    else
      ++xKey;
  }
}


//--------------------------------------------------------------------------
// Initialization and shutdown

void KBDInitialize ()
{
  // Initialize the key map for lookup of keyboard events

  struct tKeyEvent
  {
    SDL_Keycode iKey;
    int         iRow;
    int         iColumn;
  };

  // TODO Separate keypad codes ?

  const tKeyEvent asKeyEvents [] = {
    // Row 1
    { SDLK_F1,                  1, 1 },                 // K0
    { SDLK_F2,                  1, 2 },                 // K1
    { SDLK_F3,                  1, 3 },                 // K2
    { SDLK_F4,                  1, 4 },                 // K3
    { SDLK_F5,                  1, 5 },                 // K4
    { SDLK_F6,                  1, 6 },                 // K5
    { SDLK_F7,                  1, 7 },                 // K6
    { SDLK_F8,                  1, 8 },                 // K7
    { SDLK_F9,                  1, 9 },                 // K8
    { SDLK_F10,                 1, 10 },                // K9
    { SDLK_F11,                 1, 11 },                // K10
    { SDLK_F12,                 1, 12 },                // K11
    { SDLK_INSERT,              1, 13 },                // WRK
    { SDLK_DELETE,              1, 14 },                // CD
    { SDLK_KP_PLUS,             1, 15 },                // RCL
    { SDLK_TAB,                 1, 15 },                // RCL as well, for the keyboards without the numeric keypad
    // Row 2
    { SDLK_1,                   2, 1 },                 // 1
    { SDLK_2,                   2, 2 },                 // 2
    { SDLK_3,                   2, 3 },                 // 3
    { SDLK_4,                   2, 4 },                 // 4
    { SDLK_5,                   2, 5 },                 // 5
    { SDLK_6,                   2, 6 },                 // 6
    { SDLK_7,                   2, 7 },                 // 7
    { SDLK_8,                   2, 8 },                 // 8
    { SDLK_9,                   2, 9 },                 // 9
    { SDLK_0,                   2, 10 },                // 0
    { SDLK_MINUS,               2, 11 },                // UNDERSCORE
    { SDLK_PLUS,                2, 12 },                // CURLY BRACE
    { SDLK_HOME,                2, 13 },                // INS
    { SDLK_UP,                  2, 14 },                // DEL
    { SDLK_PAGEUP,              2, 15 },                // CLR
    // Row 3
    { SDLK_q,                   3, 1 },                 // Q
    { SDLK_w,                   3, 2 },                 // W
    { SDLK_e,                   3, 3 },                 // E
    { SDLK_r,                   3, 4 },                 // R
    { SDLK_t,                   3, 5 },                 // T
    { SDLK_y,                   3, 6 },                 // Z
    { SDLK_u,                   3, 7 },                 // U
    { SDLK_i,                   3, 8 },                 // I
    { SDLK_o,                   3, 9 },                 // O
    { SDLK_p,                   3, 10 },                // P
    { SDLK_LEFTBRACKET,         3, 11 },                // AT SIGN
    { SDLK_RIGHTBRACKET,        3, 12 },                // BACKSLASH
    { SDLK_BACKSPACE,           3, 13 },                // LEFT
    { SDLK_LEFT,                3, 13 },                // LEFT
    { SDLK_KP_5,                3, 14 },                // HOME
    { SDLK_RIGHT,               3, 15 },                // RIGHT
    // Row 4
    { SDLK_a,                   4, 1 },                 // A
    { SDLK_s,                   4, 2 },                 // S
    { SDLK_d,                   4, 3 },                 // D
    { SDLK_f,                   4, 4 },                 // F
    { SDLK_g,                   4, 5 },                 // G
    { SDLK_h,                   4, 6 },                 // H
    { SDLK_j,                   4, 7 },                 // J
    { SDLK_k,                   4, 8 },                 // K
    { SDLK_l,                   4, 9 },                 // L
    { SDLK_SEMICOLON,           4, 10 },                // SEMICOLON
    { SDLK_QUOTE,               4, 11 },                // COLON
    { SDLK_BACKQUOTE,           4, 12 },                // BRACE
    { SDLK_END,                 4, 13 },                // LLEFT
    { SDLK_DOWN,                4, 14 },                // END
    { SDLK_PAGEDOWN,            4, 15 },                // RRIGHT
    // Row 5
    { SDLK_SPACE,               5, 1 },                 // SPACE
    { SDLK_z,                   5, 2 },                 // Z
    { SDLK_x,                   5, 3 },                 // X
    { SDLK_c,                   5, 4 },                 // C
    { SDLK_v,                   5, 5 },                 // V
    { SDLK_b,                   5, 6 },                 // B
    { SDLK_n,                   5, 7 },                 // N
    { SDLK_m,                   5, 8 },                 // M
    { SDLK_COMMA,               5, 9 },                 // COMMA
    { SDLK_PERIOD,              5, 10 },                // PERIOD
    { SDLK_SLASH,               5, 11 },                // SLASH
    { SDLK_RETURN,              5, 15 }                 // ENTER
  };

  for (int iEvent = 0 ; iEvent < sizeof (asKeyEvents) / sizeof (tKeyEvent) ; iEvent ++)
  {
    const tKeyEvent &sEvent = asKeyEvents [iEvent];
    oKeyMap.insert (std::make_pair (sEvent.iKey, tKeyLocation (1 << sEvent.iRow - 1, sEvent.iColumn - 1)));
  }

  // Initialize the field with the current simulated
  // keyboard state as if no key was pressed

  for (int iColumn = 0 ; iColumn < PMD_KBD_COLUMNS ; iColumn ++)
  {
    abKeyMatrix [iColumn] = 0xFF;
  }

  iKeyShifts = PMD_KBD_DEF_MASK;
  iKeyColumn = 0;
}

void KBDShutdown ()
{
  // Free the key map
  oKeyMap.clear ();
}


//--------------------------------------------------------------------------
