/*

SIM_GAMEPAD.C

Gamepad support: buttons, D-pad and the left stick are translated to key events of the
simulated keyboard, so the gamepad works in the menu and in the games alike.

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
#include <string>
#include <string.h>


//--------------------------------------------------------------------------
// Data

/// Gamepad inputs that can be mapped to a key.
enum tPadInput
{
  PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT,
  PAD_A, PAD_B, PAD_X, PAD_Y,
  PAD_LB, PAD_RB, PAD_BACK, PAD_START,
  PAD_LT, PAD_RT, PAD_LS, PAD_RS,
  PAD_COUNT
};

/// Names of the inputs, used by PMD_PAD=<name>=<key>,...
static const char *apPadName [PAD_COUNT] =
{
  "up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "back", "start", "lt", "rt", "ls", "rs"
};

/// Default keys, taken from the instructions of the games: up and down are [E] and [Y] of the PMD 85
/// keyboard (the host key Z, not the cursor keys, which are other keys on the PMD 85), left and right are
/// the cursor keys, [A] is Space (action, start), [B] and [Start] are Enter (menu select), [X] is H (play),
/// [Y] is I (information), [LB] is Shift (jump in MANIC+), [RB] is the keypad plus (RCL, speed up in FRED,
/// melody in MANIC+), [Back] is Escape (game back to menu, in the menu quit).
static const SDL_Keycode aiPadDefault [PAD_COUNT] =
{
  SDLK_e, SDLK_z, SDLK_LEFT, SDLK_RIGHT,
  SDLK_SPACE, SDLK_RETURN, SDLK_h, SDLK_i,
  SDLK_LSHIFT, SDLK_KP_PLUS, SDLK_ESCAPE, SDLK_RETURN,
  SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN, SDLK_UNKNOWN
};

/// Stick dead zone, the axis range is -32768 to 32767.
#define PMD_PAD_DEADZONE        16000

/// Key assigned to each input.
static SDL_Keycode aiPadKey [PAD_COUNT];
/// Keys without the per-game overrides (the defaults and PMD_PAD).
static SDL_Keycode aiPadBase [PAD_COUNT];
/// Pressed inputs, bit mask by tPadInput (all devices together).
static volatile Uint32 iPadState;
/// Pressed inputs of a device, bit masks by tPadInput.
struct tDeviceState
{
  /// Buttons, D-pad and hat.
  Uint32 iButtons;
  /// Stick directions.
  Uint32 iStick;

  tDeviceState () : iButtons (0), iStick (0) { };
};
/// State per device, instance id to state.
static std::map <SDL_JoystickID, tDeviceState> oDeviceState;
/// Opened game controllers and plain joysticks.
static std::map <SDL_JoystickID, SDL_GameController *> oControllers;
static std::map <SDL_JoystickID, SDL_Joystick *> oJoysticks;


//--------------------------------------------------------------------------
// Operations

/** Send a key event to the event queue as if it came from the keyboard.
 */
static void GPDSendKey (SDL_Keycode iSym, bool bDown)
{
  if (iSym == SDLK_UNKNOWN)
    return;
  SDL_Event sEvent;
  memset (&sEvent, 0, sizeof (sEvent));
  sEvent.type = bDown ? SDL_KEYDOWN : SDL_KEYUP;
  sEvent.key.state = bDown ? SDL_PRESSED : SDL_RELEASED;
  sEvent.key.keysym.sym = iSym;
  SDL_PushEvent (&sEvent);
}


/** The state of the pad as the PMD 85 joystick (the 8255 ports 0x4C and 0x4D): a low bit is a pressed direction or the fire
 *  button, 0xFF is no input. Bit 0 down (Jetpac: stop falling), 1 up, 2 right, 3 left, 4 fire (the pad button A).
 */
byte GPDJoystickByte ()
{
  Uint32 iState = iPadState;
  byte iResult = 0xFF;
  if (iState & (1u << PAD_DOWN))   iResult &= ~0x01;
  if (iState & (1u << PAD_UP))     iResult &= ~0x02;
  if (iState & (1u << PAD_RIGHT))  iResult &= ~0x04;
  if (iState & (1u << PAD_LEFT))   iResult &= ~0x08;
  if (iState & (1u << PAD_A))      iResult &= ~0x10;
  return (iResult);
}


/** Set the state of the inputs of a device and send the key events of the changes.
 *
 *  Inputs of several devices are combined, a key is released when no device holds an input mapped to it.
 */
static void GPDSetState (SDL_JoystickID iDevice, Uint32 iButtons, Uint32 iStick)
{
  Uint32 iOldAll = iPadState;
  oDeviceState [iDevice].iButtons = iButtons;
  oDeviceState [iDevice].iStick = iStick;
  Uint32 iNewAll = 0;
  for (std::map <SDL_JoystickID, tDeviceState>::iterator xDevice = oDeviceState.begin () ; xDevice != oDeviceState.end () ; xDevice ++)
    iNewAll |= xDevice->second.iButtons | xDevice->second.iStick;
  iPadState = iNewAll;
  static const bool bTrace = getenv ("PMD_PADTRACE") != NULL;
  if (bTrace && iOldAll != iNewAll)
  {
    printf ("PAD inputs:");
    for (int i = 0 ; i < PAD_COUNT ; i ++)
      if (iNewAll & (1u << i))
        printf (" %s", apPadName [i]);
    printf ("%s\n", iNewAll ? "" : " (none)");
    fflush (stdout);
  }
  for (int i = 0 ; i < PAD_COUNT ; i ++)
  {
    Uint32 iMask = 1u << i;
    if ((iOldAll & iMask) == (iNewAll & iMask))
      continue;
    // A key held by another input mapped to the same key stays pressed.
    bool bKeyHeld = false;
    for (int j = 0 ; j < PAD_COUNT ; j ++)
      if (j != i && aiPadKey [j] == aiPadKey [i] && (iNewAll & (1u << j)))
        bKeyHeld = true;
    if (!bKeyHeld)
      GPDSendKey (aiPadKey [i], (iNewAll & iMask) != 0);
  }
}


/** Set or clear a button bit in the state of a device.
 */
static void GPDSetInput (SDL_JoystickID iDevice, Uint32 iMask, bool bOn)
{
  Uint32 iButtons = oDeviceState [iDevice].iButtons;
  GPDSetState (iDevice, bOn ? (iButtons | iMask) : (iButtons & ~iMask), oDeviceState [iDevice].iStick);
}


/** Input of a game controller button, or -1.
 */
static int GPDControllerButton (Uint8 iButton)
{
  switch (iButton)
  {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:         return (PAD_UP);
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:       return (PAD_DOWN);
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:       return (PAD_LEFT);
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:      return (PAD_RIGHT);
    case SDL_CONTROLLER_BUTTON_A:               return (PAD_A);
    case SDL_CONTROLLER_BUTTON_B:               return (PAD_B);
    case SDL_CONTROLLER_BUTTON_X:               return (PAD_X);
    case SDL_CONTROLLER_BUTTON_Y:               return (PAD_Y);
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:    return (PAD_LB);
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:   return (PAD_RB);
    case SDL_CONTROLLER_BUTTON_BACK:            return (PAD_BACK);
    case SDL_CONTROLLER_BUTTON_START:           return (PAD_START);
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:       return (PAD_LS);
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:      return (PAD_RS);
  }
  return (-1);
}


/** Input of a button of a plain joystick without a controller mapping, or -1 (common layout).
 */
static int GPDJoystickButton (Uint8 iButton)
{
  static const int aiInput [] = { PAD_A, PAD_B, PAD_X, PAD_Y, PAD_LB, PAD_RB, PAD_BACK, PAD_START };
  return (iButton < sizeof (aiInput) / sizeof (aiInput [0]) ? aiInput [iButton] : -1);
}


/** Stick axis to the pair of opposite directions.
 */
static void GPDAxis (SDL_JoystickID iDevice, bool bVertical, Sint16 iValue)
{
  Uint32 iLow = 1u << (bVertical ? PAD_UP : PAD_LEFT);
  Uint32 iHigh = 1u << (bVertical ? PAD_DOWN : PAD_RIGHT);
  // The stick state is kept apart from the buttons, so the stick never releases a held D-pad button.
  Uint32 iStick = oDeviceState [iDevice].iStick & ~(iLow | iHigh);
  if (iValue < -PMD_PAD_DEADZONE)
    iStick |= iLow;
  if (iValue > PMD_PAD_DEADZONE)
    iStick |= iHigh;
  GPDSetState (iDevice, oDeviceState [iDevice].iButtons, iStick);
}


/// Test pad: PMD_VIRTPAD=<file> attaches a virtual game controller inside this process (no other program sees it), the first two
/// bytes of the file are the pressed buttons, bit n is SDL_CONTROLLER_BUTTON n (0 A, 1 B, 2 X, 3 Y, 4 Back, 6 Start, 9 LB, 10 RB,
/// 11 up, 12 down, 13 left, 14 right). It goes the whole way a real pad does, which makes the pad testable without one.
static SDL_Joystick *pVirtualPad = NULL;
static const char *pVirtualPadFile = NULL;

static void GPDVirtualPoll ()
{
  static Uint32 iLast = 0;
  Uint32 iNow = SDL_GetTicks ();
  if (!pVirtualPad || iNow - iLast < 10)
    return;
  iLast = iNow;
  FILE *pStream = fopen (pVirtualPadFile, "rb");
  if (!pStream)
    return;
  int iLow = fgetc (pStream);
  int iHigh = fgetc (pStream);
  fclose (pStream);
  if (iLow == EOF || iHigh == EOF)
    return;
  int iMask = iLow | (iHigh << 8);
  for (int i = 0 ; i < 15 ; i ++)
    SDL_JoystickSetVirtualButton (pVirtualPad, i, (iMask >> i) & 1);
}


void GPDInitialize ()
{
  SDL_SetHint (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_InitSubSystem (SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
  pVirtualPadFile = getenv ("PMD_VIRTPAD");
  if (pVirtualPadFile)
  {
    int iIndex = SDL_JoystickAttachVirtual (SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 15, 0);
    if (iIndex >= 0)
      pVirtualPad = SDL_JoystickOpen (iIndex);
  }
  for (int i = 0 ; i < PAD_COUNT ; i ++)
    aiPadKey [i] = aiPadDefault [i];
  // PMD_PAD=<input>=<key>,... overrides the keys, for example PMD_PAD='a=Space,b=Return,x=H,back=Escape'.
  // The input names are up, down, left, right, a, b, x, y, lb, rb, back, start, lt, rt (triggers), ls, rs (stick clicks), no key by default, the keys are SDL key names.
  const char *pPad = getenv ("PMD_PAD");
  if (pPad)
  {
    std::string sSpec (pPad);
    size_t iPos = 0;
    while (iPos < sSpec.size ())
    {
      size_t iEnd = sSpec.find (',', iPos);
      if (iEnd == std::string::npos)
        iEnd = sSpec.size ();
      std::string sItem = sSpec.substr (iPos, iEnd - iPos);
      iPos = iEnd + 1;
      size_t iEqual = sItem.find ('=');
      if (iEqual == std::string::npos)
        continue;
      std::string sName = sItem.substr (0, iEqual);
      SDL_Keycode iKey = SDL_GetKeyFromName (sItem.substr (iEqual + 1).c_str ());
      if (iKey == SDLK_UNKNOWN)
      {
        fprintf (stderr, "PMD_PAD: unknown key in '%s'\n", sItem.c_str ());
        continue;
      }
      bool bFound = false;
      for (int i = 0 ; i < PAD_COUNT ; i ++)
        if (sName == apPadName [i])
        {
          aiPadKey [i] = iKey;
          bFound = true;
        }
      if (!bFound)
        fprintf (stderr, "PMD_PAD: unknown input in '%s'\n", sItem.c_str ());
    }
  }
  for (int i = 0 ; i < PAD_COUNT ; i ++)
    aiPadBase [i] = aiPadKey [i];
}


/** Keys of the pad for the running game: "input=key,..." (the names of PMD_PAD), the rest is as without a game.
 *  Called when a game starts (REMAP "pad:lb=F2,pad:rb=F3" in games.lst) and with an empty text when it ends.
 */
void GPDSetGameKeys (const std::string &sSpec)
{
  for (int i = 0 ; i < PAD_COUNT ; i ++)
    aiPadKey [i] = aiPadBase [i];
  size_t iPos = 0;
  while (iPos < sSpec.size ())
  {
    size_t iEnd = sSpec.find (',', iPos);
    if (iEnd == std::string::npos)
      iEnd = sSpec.size ();
    std::string sItem = sSpec.substr (iPos, iEnd - iPos);
    iPos = iEnd + 1;
    size_t iEqual = sItem.find ('=');
    if (iEqual == std::string::npos)
      continue;
    std::string sName = sItem.substr (0, iEqual);
    SDL_Keycode iKey = SDL_GetKeyFromName (sItem.substr (iEqual + 1).c_str ());
    if (iKey == SDLK_UNKNOWN)
      continue;
    for (int i = 0 ; i < PAD_COUNT ; i ++)
      if (sName == apPadName [i])
        aiPadKey [i] = iKey;
  }
}


void GPDShutdown ()
{
  for (std::map <SDL_JoystickID, SDL_GameController *>::iterator xDevice = oControllers.begin () ; xDevice != oControllers.end () ; xDevice ++)
    SDL_GameControllerClose (xDevice->second);
  for (std::map <SDL_JoystickID, SDL_Joystick *>::iterator xDevice = oJoysticks.begin () ; xDevice != oJoysticks.end () ; xDevice ++)
    SDL_JoystickClose (xDevice->second);
  oControllers.clear ();
  oJoysticks.clear ();
  oDeviceState.clear ();
  iPadState = 0;
}


void GPDEventHandler (const SDL_Event *pEvent)
{
  GPDVirtualPoll ();
  static const bool bTrace = getenv ("PMD_PADTRACE") != NULL;
  if (bTrace && pEvent->type >= SDL_JOYAXISMOTION && pEvent->type <= SDL_CONTROLLERDEVICEREMAPPED)
  {
    if (pEvent->type == SDL_CONTROLLERBUTTONDOWN || pEvent->type == SDL_CONTROLLERBUTTONUP)
      printf ("PAD event: button %d %s\n", pEvent->cbutton.button, pEvent->type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up");
    else if (pEvent->type == SDL_CONTROLLERAXISMOTION)
      printf ("PAD event: axis %d value %d\n", pEvent->caxis.axis, pEvent->caxis.value);
    else if (pEvent->type == SDL_JOYBUTTONDOWN || pEvent->type == SDL_JOYBUTTONUP)
      printf ("PAD event: raw joystick button %d %s\n", pEvent->jbutton.button, pEvent->type == SDL_JOYBUTTONDOWN ? "down" : "up");
    else if (pEvent->type == SDL_JOYHATMOTION)
      printf ("PAD event: raw hat %d value %d\n", pEvent->jhat.hat, pEvent->jhat.value);
    fflush (stdout);
  }
  switch (pEvent->type)
  {
    case SDL_CONTROLLERDEVICEADDED:
    {
      SDL_GameController *pController = SDL_GameControllerOpen (pEvent->cdevice.which);
      if (pController)
      {
        SDL_JoystickID iId = SDL_JoystickInstanceID (SDL_GameControllerGetJoystick (pController));
        oControllers [iId] = pController;
        printf ("Gamepad: %s\n", SDL_GameControllerName (pController));
      }
      break;
    }
    case SDL_CONTROLLERDEVICEREMOVED:
    {
      std::map <SDL_JoystickID, SDL_GameController *>::iterator xDevice = oControllers.find (pEvent->cdevice.which);
      if (xDevice != oControllers.end ())
      {
        GPDSetState (xDevice->first, 0, 0);
        oDeviceState.erase (xDevice->first);
        SDL_GameControllerClose (xDevice->second);
        oControllers.erase (xDevice);
      }
      break;
    }
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    {
      int iInput = GPDControllerButton (pEvent->cbutton.button);
      if (iInput >= 0)
        GPDSetInput (pEvent->cbutton.which, 1u << iInput, pEvent->type == SDL_CONTROLLERBUTTONDOWN);
      break;
    }
    case SDL_CONTROLLERAXISMOTION:
      if (pEvent->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || pEvent->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
      {
        // Triggers are axes 0..32767: pressed over a quarter of the travel, released under a tenth (hysteresis).
        Uint32 iMask = 1u << (pEvent->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ? PAD_LT : PAD_RT);
        bool bHeld = (oDeviceState [pEvent->caxis.which].iButtons & iMask) != 0;
        if (!bHeld && pEvent->caxis.value > 8000)
          GPDSetInput (pEvent->caxis.which, iMask, true);
        else if (bHeld && pEvent->caxis.value < 3000)
          GPDSetInput (pEvent->caxis.which, iMask, false);
      }
      else if (pEvent->caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
        GPDAxis (pEvent->caxis.which, false, pEvent->caxis.value);
      else if (pEvent->caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
        GPDAxis (pEvent->caxis.which, true, pEvent->caxis.value);
      break;
    // Joysticks without a controller mapping: hat as the D-pad, axes 0 and 1 as the stick.
    case SDL_JOYDEVICEADDED:
    {
      if (SDL_IsGameController (pEvent->jdevice.which))
        break;
      SDL_Joystick *pJoystick = SDL_JoystickOpen (pEvent->jdevice.which);
      if (pJoystick)
      {
        oJoysticks [SDL_JoystickInstanceID (pJoystick)] = pJoystick;
        printf ("Gamepad (no mapping): %s\n", SDL_JoystickName (pJoystick));
      }
      break;
    }
    case SDL_JOYDEVICEREMOVED:
    {
      std::map <SDL_JoystickID, SDL_Joystick *>::iterator xDevice = oJoysticks.find (pEvent->jdevice.which);
      if (xDevice != oJoysticks.end ())
      {
        GPDSetState (xDevice->first, 0, 0);
        oDeviceState.erase (xDevice->first);
        SDL_JoystickClose (xDevice->second);
        oJoysticks.erase (xDevice);
      }
      break;
    }
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
    {
      if (!oJoysticks.count (pEvent->jbutton.which))
        break;
      int iInput = GPDJoystickButton (pEvent->jbutton.button);
      if (iInput >= 0)
        GPDSetInput (pEvent->jbutton.which, 1u << iInput, pEvent->type == SDL_JOYBUTTONDOWN);
      break;
    }
    case SDL_JOYHATMOTION:
    {
      if (!oJoysticks.count (pEvent->jhat.which))
        break;
      Uint32 iHat = (pEvent->jhat.value & SDL_HAT_UP ? 1u << PAD_UP : 0)
                  | (pEvent->jhat.value & SDL_HAT_DOWN ? 1u << PAD_DOWN : 0)
                  | (pEvent->jhat.value & SDL_HAT_LEFT ? 1u << PAD_LEFT : 0)
                  | (pEvent->jhat.value & SDL_HAT_RIGHT ? 1u << PAD_RIGHT : 0);
      Uint32 iDirections = (1u << PAD_UP) | (1u << PAD_DOWN) | (1u << PAD_LEFT) | (1u << PAD_RIGHT);
      GPDSetState (pEvent->jhat.which, (oDeviceState [pEvent->jhat.which].iButtons & ~iDirections) | iHat, oDeviceState [pEvent->jhat.which].iStick);
      break;
    }
    case SDL_JOYAXISMOTION:
      if (!oJoysticks.count (pEvent->jaxis.which))
        break;
      if (pEvent->jaxis.axis == 0)
        GPDAxis (pEvent->jaxis.which, false, pEvent->jaxis.value);
      else if (pEvent->jaxis.axis == 1)
        GPDAxis (pEvent->jaxis.which, true, pEvent->jaxis.value);
      break;
  }
}
