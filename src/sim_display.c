/*

SIM_DISPLAY.C

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

  void DSPInitialize ()

 -Added:

 void DSPRenderMenu (int screen_width, int screen_height, int index)
 void DSPRenderLogo (int screen_width, int screen_height)
 void DSPRenderImage (int screen_width, int screen_height)
 void DSPRenderText (int screen_width, int screen_height)
 void DSPLogoShutdown ()
 void DSPMenuShutdown ()
 void DSPLogoInitialize ()
 void DSPMenuInitialize ()

*/

#include "sim_common.h"
#include "SDL2/SDL_ttf.h"
#include <map>


//-------------------------------------------------------------------------------------------------------

#define FONT_HEIGHT      24
#define FONT_WIDTH       16
#define IMAGE_WIDTH     174
#define FRAME_WIDTH     200
#define FRAME_SPACE       5
#define MENU_WIDTH      815 //4x200 + 3x 5px spacing
/// Width of the slot of one device icon in the games list.
#define ICON_SLOT       36

//-------------------------------------------------------------------------------------------------------
//
// HARDCODED DATA


static const char *menuText[7] = {
  "PMD-85 Retro console",
  "1] Vyber hru pomoci sipek <- -> a stiskni [ Enter ].",
  "2] Hra se nahraje sama (prikaz MGLD se napise automaticky).",
  "3] Dalsi hry: sipka nahoru [ E ] otevre seznam vsech her.",
  "4] Pro ukonceni hry stiskni [ Esc ].",
  "5] Pro konec stiskni [ Esc ].",
  "Richard Bruna (c) 2019"
};

/// The origin of many of the programs and games, shown under the games of the menu.
static const char *creditsText = "Hry a programy: pmd85.borik.net";

static const char *pmdLogo[1] = {PMD_SHARE "pmd85_830_570.bmp"};

static const char *gameLogo[4] = {
  PMD_SHARE "flappy.bmp",
  PMD_SHARE "boulder.bmp",
  PMD_SHARE "manic.bmp",
  PMD_SHARE "fred.bmp"
};

static const char *gameText[4] = {"FLAPPY", "BOULDER", "MANIC", "FRED"};

static const char *gameFont[1] = {PMD_SHARE "atari-classic.ttf"};


//--------------------------------------------------------------------------
// Command Line Options

/// Screen refresh. How many milliseconds per screen refresh.
static int iArgRefresh = 40;


//--------------------------------------------------------------------------
// Data

/// Singleton screen window.
static SDL_Window *pWindow;
/// Singleton screen renderer;
static SDL_Renderer *pRenderer;
/// Singleton screen texture.
static SDL_Texture *pTexture;
/// Singleton menu window.
static SDL_Window *mWindow;
/// Singleton menu renderer.
static SDL_Renderer *mRenderer;
/// Singleton logo window.
static SDL_Window *lWindow;
/// Singleton logo renderer.
static SDL_Renderer *lRenderer;
/// Paint timer.
static SDL_TimerID iPaintTimer;

// Display colors (R, G, B). Gray is the former RGB332 value 0b10010010 as expanded by SDL.
#define DSP_COLOR_BLACK 0x00, 0x00, 0x00
#define DSP_COLOR_WHITE 0xFF, 0xFF, 0xFF
#define DSP_COLOR_GRAY  0x92, 0x92, 0xAA

/** Texture pixel format. ABGR8888 is what OpenGL ES uploads without any conversion, RGB332 would be
 *  converted by SDL in software on every update. */
#define DSP_TEXTURE_FORMAT      SDL_PIXELFORMAT_ABGR8888

/// Base address of the video memory.
#define PMD_VRAM_BASE           0xC000
/// Width of the visible video memory line in bytes.
#define PMD_VRAM_WIDTH          48
/// Width of the invisible video memory line in bytes.
#define PMD_VRAM_BLANK          16
/// Height of the video memory area in lines.
#define PMD_VRAM_HEIGHT         256

/// Number of pixels in one byte.
#define PMD_PIXEL_COUNT         6
/// Gray attribute mask.
#define PMD_PIXEL_GRAY          (1 << 6)
/// Blinking attribute bask.
#define PMD_PIXEL_BLINK         (1 << 7)

/// Blinking rate in miliseconds.
#define PMD_BLINK_RATE          512


/// Width of the screen in pixels.
#define PMD_SCREEN_WIDTH        (PMD_VRAM_WIDTH * PMD_PIXEL_COUNT)
/// Width of the screen line in bytes.
#define PMD_SCREEN_PITCH        (PMD_SCREEN_WIDTH * sizeof (Uint32))

/// Rendered screen data.
static Uint32 aiScreenData [PMD_SCREEN_WIDTH * PMD_VRAM_HEIGHT];

/// Pixels of a video memory byte, by color (0 black, 1 white, 2 gray) and the six pixel bits.
static Uint32 aiPixelTable [3][1 << PMD_PIXEL_COUNT][PMD_PIXEL_COUNT];

/// Visible video memory and blink phase of the last paint, an unchanged screen is not painted again.
static byte abLastVideo [PMD_VRAM_WIDTH * PMD_VRAM_HEIGHT];
static bool bLastBlink;
static bool bLastValid = false;


//--------------------------------------------------------------------------
// Screen paint

/// The blink bit of the video memory is ignored (the note of the game has NOBLINK).
static bool bNoBlink = false;

/** Determine whether blinking pixels are visible.
 *
 *  Visibility of blinking pixels is based on actual time.
 *
 *  @return Flag determining blinking pixel visibility.
 */
bool DSPBlinkVisible ()
{
  // A game with "NOBLINK" in its note uses the blink bit as a color of the PMD 85-2A/3 that cannot be shown here.
  if (bNoBlink)
    return (true);
  return ((SDL_GetTicks () / PMD_BLINK_RATE) & 1);
}

void DSPSetNoBlink (bool bValue)
{
  bNoBlink = bValue;
}

bool DSPNoBlink ()
{
  return (bNoBlink);
}


/** Prepare the table translating a video memory byte to its pixels.
 */
static void DSPInitializePixelTable ()
{
  SDL_PixelFormat *pFormat;
  SDL_CheckNotNull (pFormat = SDL_AllocFormat (DSP_TEXTURE_FORMAT));
  const Uint32 aiColors [3] = {SDL_MapRGB (pFormat, DSP_COLOR_BLACK),
                               SDL_MapRGB (pFormat, DSP_COLOR_WHITE),
                               SDL_MapRGB (pFormat, DSP_COLOR_GRAY)};
  for (int iColor = 0 ; iColor < 3 ; iColor ++)
    for (int iBits = 0 ; iBits < (1 << PMD_PIXEL_COUNT) ; iBits ++)
      for (int iPixel = 0 ; iPixel < PMD_PIXEL_COUNT ; iPixel ++)
        aiPixelTable [iColor][iBits][iPixel] = ((iBits >> iPixel) & 1) ? aiColors [iColor] : aiColors [0];
  SDL_FreeFormat (pFormat);
}


/** Decode the screen content in 1:1 zoom.
 *
 *  Decodes the memory content of the simulated machine
 *  using direct writing to screen texture data. Optimized for 1:1 zoom.
 *
 *  @arg iFirst First line to decode.
 *  @arg iCount Number of lines to decode.
 */
void DSPDecodeScreen (int iFirst, int iCount)
{
  bool bBlinkVisible = DSPBlinkVisible ();

  // Traverse the memory content line by line and byte by byte
  // and paint the screen content as we go.
  byte *pSource = abMemoryData + PMD_VRAM_BASE + iFirst * (PMD_VRAM_WIDTH + PMD_VRAM_BLANK);
  Uint32 *pTarget = aiScreenData + iFirst * PMD_SCREEN_WIDTH;
  for (int iLine = 0 ; iLine < iCount ; iLine ++)
  {
    for (int iByte = 0 ; iByte < PMD_VRAM_WIDTH ; iByte ++)
    {
      byte iData = *pSource;

      // Determine the drawing color. All pixels in one byte share color.
      int iColor = (iData & PMD_PIXEL_GRAY) ? 2 : 1;
      if ((iData & PMD_PIXEL_BLINK) && !bBlinkVisible) iColor = 0;

      memcpy (pTarget, aiPixelTable [iColor][iData & ((1 << PMD_PIXEL_COUNT) - 1)], PMD_PIXEL_COUNT * sizeof (Uint32));
      pTarget += PMD_PIXEL_COUNT;
      pSource ++;
    }

    pSource += PMD_VRAM_BLANK;
  }
}


//--------------------------------------------------------------------------
// Event handlers

/** Handler for painting the screen content.
 *
 *  Decodes the memory content of the simulated machine
 *  and paints the screen content accordingly, using
 *  direct writing and double buffering.
 */
/** Force the next paint, e.g. after the window changed.
 */
void DSPInvalidate ()
{
  bLastValid = false;
}


/// Paint statistics for PMD_PAINTSTATS=1 (stderr every 10 s): calls, painted frames, worst call, worst gap between calls [ms].
static void DSPPaintStats (bool bPainted, Uint32 iBegin, Uint32 iEnd)
{
  static int bOn = getenv ("PMD_PAINTSTATS") != NULL;
  static Uint32 iStart = SDL_GetTicks (), iLastCall = 0, iMaxCall = 0, iMaxGap = 0, iPaintedMax = 0;
  static int iCalls = 0, iPaints = 0, iSlow = 0, iBig = 0;
  if (!bOn)
    return;
  iCalls ++;
  if (iLastCall && iBegin - iLastCall > iMaxGap)
    iMaxGap = iBegin - iLastCall;
  if (iLastCall && iBegin - iLastCall > 60)
    iBig ++;
  iLastCall = iBegin;
  if (bPainted)
  {
    iPaints ++;
    if (iEnd - iBegin > iPaintedMax)
      iPaintedMax = iEnd - iBegin;
    if (iEnd - iBegin > 20)
      iSlow ++;
  }
  if (iEnd - iStart >= 10000)
  {
    fprintf (stderr, "PAINT: %d calls, %d painted, max paint %u ms, %d paints >20 ms, max gap between calls %u ms, %d gaps >60 ms (in %u ms)\n",
             iCalls, iPaints, iPaintedMax, iSlow, iMaxGap, iBig, iEnd - iStart);
    iStart = iEnd; iCalls = iPaints = iSlow = iBig = 0; iMaxGap = iPaintedMax = 0;
  }
}


/// Debug sampler for PMD_PADDLETRACE=1: left edge of the widest lit run in lines 208-215, logged to stderr on a change (every ~2 ms).
static int DSPPaddleThread (void *pArgs)
{
  int iLast = -2;
  for (;;)
  {
    const byte *pVideo = abMemoryData + PMD_VRAM_BASE;
    bool abLit [PMD_VRAM_WIDTH * PMD_PIXEL_COUNT + 6];
    memset (abLit, 0, sizeof (abLit));
    for (int y = 208 ; y < 216 ; y ++)
      for (int x = 0 ; x < PMD_VRAM_WIDTH * PMD_PIXEL_COUNT ; x ++)
        if ((pVideo [y * (PMD_VRAM_WIDTH + PMD_VRAM_BLANK) + x / PMD_PIXEL_COUNT] >> (x % PMD_PIXEL_COUNT)) & 1)
          abLit [x] = true;
    int iEdge = -1;
    for (int x = 17 ; x < 216 && iEdge < 0 ; x ++)
      if (abLit [x])
      {
        int iEnd = x;
        while (iEnd < PMD_VRAM_WIDTH * PMD_PIXEL_COUNT && abLit [iEnd]) iEnd ++;
        if (iEnd - x >= 15) iEdge = x;
        x = iEnd;
      }
    // ARKANOID variables: paddle x 1AD1, its speed 1AD2, width 1AD8, ball x 1B08, key bits 1B74 (see routine 0x35AC).
    int iState = abMemoryData [0x1AD1] | abMemoryData [0x1AD2] << 8 | abMemoryData [0x1B08] << 16 | abMemoryData [0x1B74] << 24;
    static int iLastState = -1;
    if (iEdge != iLast || iState != iLastState)
    {
      int iCenter = (abMemoryData [0x1AD1] + 2 * abMemoryData [0x1AD8] + 0x0A) & 0xFF;
      fprintf (stderr, "PD %u %d x=%d spd=%d ball=%d keys=%02x cf=%d\n", SDL_GetTicks (), iEdge, abMemoryData [0x1AD1],
               (signed char) abMemoryData [0x1AD2], abMemoryData [0x1B08], abMemoryData [0x1B74], iCenter < abMemoryData [0x1B08]);
      iLast = iEdge;
      iLastState = iState;
    }
    SDL_Delay (2);
  }
  return (0);
}


/// Debug sampler for PMD_SCANTRACE=1: the moments the emulated program starts to scan the keyboard (bursts of row reads).
static int DSPScanThread (void *pArgs)
{
  uint32 iLast = KBDScanCount (), iLastSeen = KBDSeenCount ();
  Uint32 iLastChange = 0, iLastSeenTime = 0;
  for (;;)
  {
    uint32 iNow = KBDScanCount ();
    Uint32 iTicks = SDL_GetTicks ();
    if (KBDSeenCount () != iLastSeen)
    {
      if (iTicks - iLastSeenTime >= 6)
        fprintf (stderr, "SEEN %u\n", iTicks);
      iLastSeenTime = iTicks;
      iLastSeen = KBDSeenCount ();
    }
    if (iNow != iLast)
    {
      if (iTicks - iLastChange >= 6)
        fprintf (stderr, "SCAN %u\n", iTicks);
      iLastChange = iTicks;
      iLast = iNow;
    }
    SDL_Delay (1);
  }
  return (0);
}


void DSPPaintHandler ()
{
  static bool bScan = getenv ("PMD_SCANTRACE") && (SDL_CreateThread (DSPScanThread, "scan", NULL), true);
  (void) bScan;
  static bool bPaddle = getenv ("PMD_PADDLETRACE") && (SDL_CreateThread (DSPPaddleThread, "paddle", NULL), true);
  (void) bPaddle;
  Uint32 iPaintBegin = SDL_GetTicks ();
  // TODO Consider using HQX ?

  // Painting a 1080p frame costs most of the CPU on a Raspberry Pi, skip frames that did not change
  // and decode and upload only the lines between the first and the last changed one.
  bool bBlink = DSPBlinkVisible ();
  const byte *pVideo = abMemoryData + PMD_VRAM_BASE;
  int iFirst = 0;
  int iLast = PMD_VRAM_HEIGHT - 1;
  if (bLastValid && bBlink == bLastBlink)
  {
    iFirst = 0;
    while (iFirst < PMD_VRAM_HEIGHT && memcmp (pVideo + iFirst * (PMD_VRAM_WIDTH + PMD_VRAM_BLANK), abLastVideo + iFirst * PMD_VRAM_WIDTH, PMD_VRAM_WIDTH) == 0)
      iFirst ++;
    if (iFirst == PMD_VRAM_HEIGHT)
    {
      DSPPaintStats (false, iPaintBegin, SDL_GetTicks ());
      return;
    }
    while (memcmp (pVideo + iLast * (PMD_VRAM_WIDTH + PMD_VRAM_BLANK), abLastVideo + iLast * PMD_VRAM_WIDTH, PMD_VRAM_WIDTH) == 0)
      iLast --;
  }
  for (int iLine = iFirst ; iLine <= iLast ; iLine ++)
    memcpy (abLastVideo + iLine * PMD_VRAM_WIDTH, pVideo + iLine * (PMD_VRAM_WIDTH + PMD_VRAM_BLANK), PMD_VRAM_WIDTH);
  bLastBlink = bBlink;
  bLastValid = true;

  int iCount = iLast - iFirst + 1;
  static int bTrace = getenv ("PMD_PAINTTRACE") != NULL;
  if (bTrace)
  {
    // Debug: leftmost lit pixel of lines 206-215 (the ARKANOID paddle) at the time of each painted frame.
    int iMinX = -1;
    for (int y = 206 ; y < 216 && iMinX < 0 ; y ++)
      for (int x = 22 ; x < 206 ; x ++)
        if ((pVideo [y * (PMD_VRAM_WIDTH + PMD_VRAM_BLANK) + x / 6] >> (x % 6)) & 1) { if (iMinX < 0 || x < iMinX) iMinX = x; break; }
    fprintf (stderr, "PT %u %d\n", iPaintBegin, iMinX);
  }
  DSPDecodeScreen (iFirst, iCount);

  // TODO Check if render clear is really needed ?

  SDL_Rect sArea = {0, iFirst, PMD_SCREEN_WIDTH, iCount};
  SDL_CheckZero (SDL_SetRenderDrawColor (pRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
  SDL_CheckZero (SDL_RenderClear (pRenderer));
  SDL_CheckZero (SDL_UpdateTexture (pTexture, &sArea, aiScreenData + iFirst * PMD_SCREEN_WIDTH, PMD_SCREEN_PITCH));
  SDL_CheckZero (SDL_RenderCopy (pRenderer, pTexture, NULL, NULL));
  SDL_RenderPresent (pRenderer);
  DSPPaintStats (true, iPaintBegin, SDL_GetTicks ());
}


/** Saves the current screen content as a BMP file.
 *
 *  Reads back the renderer output, so it works without a connected display.
 *
 *  @arg pPath Target file name.
 */
void DSPSaveScreenshot (const char *pPath)
{
  int iWidth, iHeight;
  DSPDecodeScreen (0, PMD_VRAM_HEIGHT);
  SDL_CheckZero (SDL_UpdateTexture (pTexture, NULL, aiScreenData, PMD_SCREEN_PITCH));
  SDL_CheckZero (SDL_RenderCopy (pRenderer, pTexture, NULL, NULL));
  SDL_CheckZero (SDL_GetRendererOutputSize (pRenderer, &iWidth, &iHeight));
  SDL_Surface *pSurface;
  SDL_CheckNotNull (pSurface = SDL_CreateRGBSurfaceWithFormat (0, iWidth, iHeight, 32, SDL_PIXELFORMAT_ARGB8888));
  SDL_CheckZero (SDL_RenderReadPixels (pRenderer, NULL, SDL_PIXELFORMAT_ARGB8888, pSurface->pixels, pSurface->pitch));
  SDL_CheckZero (SDL_SaveBMP (pSurface, pPath));
  SDL_FreeSurface (pSurface);
  // The whole screen was decoded for this moment only, paint it again.
  bLastValid = false;
}


//--------------------------------------------------------------------------
// Callback handlers

/** Handler for painting the screen content.
 *
 *  Pushes an event corresponding to the timer on the
 *  event queue to maintain the threading rules.
 */
Uint32 DSPPaintTimerCallback (Uint32 iInterval, void *pArgs)
{
  SDL_Event sEvent;
  sEvent.type = SDL_USEREVENT;
  sEvent.user.code = 0;
  sEvent.user.data1 = NULL;
  sEvent.user.data2 = NULL;
  SDL_PushEvent (&sEvent);

  return (iArgRefresh);
}


//--------------------------------------------------------------------------
// Initialization and shutdown


/** Picture shape of the emulation window, PMD_ASPECT environment variable.
 *
 *  stretch (default) fills the whole window, square keeps square pixels (288:256),
 *  4:3 is the shape of the original PMD-85 monitor. Black bars fill the rest of the window.
 */
static void DSPApplyAspect ()
{
  const char *pAspect = getenv ("PMD_ASPECT");
  if (!pAspect || strcmp (pAspect, "stretch") == 0)
    return;
  if (strcmp (pAspect, "square") == 0)
  {
    SDL_CheckZero (SDL_RenderSetLogicalSize (pRenderer, PMD_SCREEN_WIDTH, PMD_VRAM_HEIGHT));
  }
  else if (strcmp (pAspect, "4:3") == 0)
  {
    SDL_CheckZero (SDL_RenderSetLogicalSize (pRenderer, 1024, 768));
  }
  else
    fprintf (stderr, "PMD_ASPECT: '%s' is not stretch, square or 4:3\n", pAspect);
}


/** Window size multiplier, PMD_SCALE environment variable (default 3). */
static int DSPWindowScale ()
{
  const char *pScale = getenv ("PMD_SCALE");
  int iScale = pScale ? atoi (pScale) : 3;
  return (iScale > 0 ? iScale : 1);
}


/** Screen resolution used by the menu, logo and fullscreen, PMD_RES=<w>x<h> (default desktop mode).
 *
 *  A lower resolution makes the slow Raspberry Pi paint faster.
 *
 *  @return True if the resolution comes from PMD_RES (real video mode change).
 */
bool DSPResolution (int *pWidth, int *pHeight)
{
  const char *pRes = getenv ("PMD_RES");
  if (pRes && sscanf (pRes, "%dx%d", pWidth, pHeight) == 2 && *pWidth > 0 && *pHeight > 0)
    return (true);
  SDL_DisplayMode sMode;
  SDL_CheckZero (SDL_GetCurrentDisplayMode (0, &sMode));
  *pWidth = sMode.w;
  *pHeight = sMode.h;
  return (false);
}


/** Window flags: fullscreen on the console (KMSDRM), resizable elsewhere. */
static Uint32 DSPWindowFlags ()
{
  const char *pDriver = SDL_GetCurrentVideoDriver ();
  if (pDriver && strcmp (pDriver, "KMSDRM") == 0)
  {
    int iWidth, iHeight;
    return (DSPResolution (&iWidth, &iHeight) ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_FULLSCREEN_DESKTOP);
  }
  return (SDL_WINDOW_RESIZABLE);
}


void DSPInitialize ()
{
  // Initialize the video resources.
  int iWindowW = PMD_VRAM_WIDTH * PMD_PIXEL_COUNT * DSPWindowScale ();
  int iWindowH = PMD_VRAM_HEIGHT * DSPWindowScale ();
  if (DSPWindowFlags () == SDL_WINDOW_FULLSCREEN)
    DSPResolution (&iWindowW, &iWindowH);
  SDL_CheckNotNull (pWindow = SDL_CreateWindow ("SimPMD", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                                iWindowW, iWindowH, DSPWindowFlags ()));
  SDL_CheckNotNull (pRenderer = SDL_CreateRenderer (pWindow, -1, SDL_RENDERER_ACCELERATED));
  SDL_CheckNotNull (pTexture = SDL_CreateTexture (pRenderer, DSP_TEXTURE_FORMAT, SDL_TEXTUREACCESS_STREAMING,
                                                  PMD_SCREEN_WIDTH, PMD_VRAM_HEIGHT));
  DSPApplyAspect ();
  DSPInitializePixelTable ();
  //SDL_CheckTrue (SDL_SetHint (SDL_HINT_RENDER_SCALE_QUALITY, 0));

  // Clear screen.
  SDL_CheckZero (SDL_SetRenderDrawColor (pRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
  SDL_CheckZero (SDL_RenderClear (pRenderer));
  SDL_RenderPresent (pRenderer);
  // Start the timer that paints the screen repeatedly ...
  const char *pRefresh = getenv ("PMD_REFRESH");
  if (pRefresh && atoi (pRefresh) > 0)
    iArgRefresh = atoi (pRefresh);
  bLastValid = false;
  iPaintTimer = SDL_AddTimer (iArgRefresh, DSPPaintTimerCallback, NULL);
}


void DSPMenuInitialize ()
{
  // Menu and logo are laid out for the display resolution, keep it as logical size.
  int iDisplayW, iDisplayH;
  DSPResolution (&iDisplayW, &iDisplayH);
  // Initialize the video resources.
  SDL_CheckNotNull (mWindow = SDL_CreateWindow ("SimPMD - Menu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                                iDisplayW, iDisplayH, DSPWindowFlags ()));
  SDL_CheckNotNull (mRenderer = SDL_CreateRenderer (mWindow, -1, SDL_RENDERER_ACCELERATED));
  SDL_CheckZero (SDL_RenderSetLogicalSize (mRenderer, iDisplayW, iDisplayH));
  //TTF
  TTF_Init();
  // Clear screen.
  SDL_CheckZero (SDL_SetRenderDrawColor (mRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
  SDL_CheckZero (SDL_RenderClear (mRenderer));
  SDL_RenderPresent (mRenderer);
}


void DSPLogoInitialize ()
{
  // Menu and logo are laid out for the display resolution, keep it as logical size.
  int iDisplayW, iDisplayH;
  DSPResolution (&iDisplayW, &iDisplayH);
  // Initialize the video resources.
  SDL_CheckNotNull (lWindow = SDL_CreateWindow ("SimPMD - Logo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                                iDisplayW, iDisplayH, DSPWindowFlags ()));
  SDL_CheckNotNull (lRenderer = SDL_CreateRenderer (lWindow, -1, SDL_RENDERER_ACCELERATED));
  SDL_CheckZero (SDL_RenderSetLogicalSize (lRenderer, iDisplayW, iDisplayH));
  // Clear screen.
  SDL_CheckZero (SDL_SetRenderDrawColor (lRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE));
  SDL_CheckZero (SDL_RenderClear (lRenderer));
  SDL_RenderPresent (lRenderer);
}


void DSPShutdown ()
{
  // Stop the timer that paints the screen repeatedly ...
  SDL_RemoveTimer (iPaintTimer);

  SDL_DestroyTexture (pTexture);
  pTexture = NULL;
  SDL_DestroyRenderer (pRenderer);
  pRenderer = NULL;
  SDL_DestroyWindow (pWindow);
  pWindow = NULL;
}


/** The menu is painted again at every key press, and rendering a text with TTF and uploading it as a texture is slow on a
 *  Raspberry Pi (about 100 ms for a screen of the list). The textures of the texts, the logos and the font are kept for the
 *  life of the menu renderer; PMD_PAINTSTATS=1 prints the time of a menu paint.
 */
static std::map <std::string, SDL_Texture *> oTextCache;
static SDL_Texture *apLogoCache [4];
static TTF_Font *pFontCache = NULL;

static void DSPMenuCacheClear ()
{
  for (std::map <std::string, SDL_Texture *>::iterator x = oTextCache.begin () ; x != oTextCache.end () ; ++ x)
    SDL_DestroyTexture (x->second);
  oTextCache.clear ();
  for (int i = 0 ; i < 4 ; i ++)
  {
    if (apLogoCache [i])
      SDL_DestroyTexture (apLogoCache [i]);
    apLogoCache [i] = NULL;
  }
  if (pFontCache)
    TTF_CloseFont (pFontCache);
  pFontCache = NULL;
}

static TTF_Font *DSPMenuFont ()
{
  if (!pFontCache)
    pFontCache = TTF_OpenFont (gameFont [0], FONT_HEIGHT);
  return (pFontCache);
}

/// Draws a text line on the menu renderer (the font has the same width of all the characters).
static void DSPDrawText (TTF_Font *pFont, const std::string &sText, int iX, int iY)
{
  if (sText.empty ())
    return;
  std::map <std::string, SDL_Texture *>::iterator xFound = oTextCache.find (sText);
  SDL_Texture *pText = xFound == oTextCache.end () ? NULL : xFound->second;
  if (!pText)
  {
    SDL_Color sColor = {255, 255, 255, SDL_ALPHA_OPAQUE};
    SDL_Surface *pSurface = TTF_RenderText_Solid (pFont, sText.c_str (), sColor);
    if (!pSurface)
      return;
    pText = SDL_CreateTextureFromSurface (mRenderer, pSurface);
    SDL_FreeSurface (pSurface);
    if (!pText)
      return;
    // The list has about 150 different texts, a runaway growth is cut.
    if (oTextCache.size () > 1500)
    {
      for (std::map <std::string, SDL_Texture *>::iterator x = oTextCache.begin () ; x != oTextCache.end () ; ++ x)
        SDL_DestroyTexture (x->second);
      oTextCache.clear ();
    }
    oTextCache [sText] = pText;
  }
  SDL_Rect sRect = {iX, iY, FONT_WIDTH * (int) sText.size (), FONT_HEIGHT};
  SDL_RenderCopy (mRenderer, pText, NULL, &sRect);
}

/// PMD_PAINTSTATS=1: the time of a paint of the menu or the list, printed every 10 s (the number of paints, mean, slowest).
static void DSPMenuStat (Uint64 iBegin)
{
  static bool bOn = getenv ("PMD_PAINTSTATS") != NULL;
  static Uint32 iStart = SDL_GetTicks ();
  static int iCount = 0;
  static double dMax = 0, dSum = 0;
  if (!bOn)
    return;
  double dMs = (double) (SDL_GetPerformanceCounter () - iBegin) * 1000.0 / (double) SDL_GetPerformanceFrequency ();
  iCount ++;
  dSum += dMs;
  if (dMs > dMax)
    dMax = dMs;
  fprintf (stderr, "MENU paint %.1f ms\n", dMs);
  if (SDL_GetTicks () - iStart >= 10000)
  {
    fprintf (stderr, "MENU: %d paints in 10 s, mean %.1f ms, slowest %.1f ms\n", iCount, dSum / iCount, dMax);
    iStart = SDL_GetTicks (); iCount = 0; dMax = dSum = 0;
  }
}


void DSPMenuShutdown ()
{
  DSPMenuCacheClear ();
  SDL_DestroyRenderer (mRenderer);
  mRenderer = NULL;
  SDL_DestroyWindow (mWindow);
  mWindow = NULL;
}


void DSPLogoShutdown ()
{
  SDL_DestroyRenderer (lRenderer);
  lRenderer = NULL;
  SDL_DestroyWindow (lWindow);
  lWindow = NULL;
}


void DSPRenderText (int screen_width, int screen_height)
{
  TTF_Font *textFont = DSPMenuFont ();
  if (!textFont)
    return;
  int iX = (screen_width - MENU_WIDTH)/2;

  // Credits and copyright
  DSPDrawText (textFont, creditsText, iX + (MENU_WIDTH - FONT_WIDTH * (int) strlen (creditsText))/2, 630);
  DSPDrawText (textFont, menuText[6], iX + (MENU_WIDTH - FONT_WIDTH * (int) strlen (menuText[6]))/2, 665);
  // About
  DSPDrawText (textFont, menuText[0], iX + (MENU_WIDTH - FONT_WIDTH * (int) strlen (menuText[0]))/2, 100);
  // Help text
  for (int i = 1; i < 6; i++)
    DSPDrawText (textFont, menuText[i], iX, 100 + FONT_HEIGHT + 2 + i * (FONT_HEIGHT + 2));// 2-px v-spacing
  // Game text
  for (int i = 0; i < 4; i++)
    DSPDrawText (textFont, gameText[i], iX + (FRAME_WIDTH - FONT_WIDTH * (int) strlen (gameText[i]))/2 + i * (FRAME_WIDTH + FRAME_SPACE),
                 310 + FRAME_WIDTH + 15);// 15px text spacing
}


void DSPRenderImage (int screen_width, int screen_height)
{
  SDL_Rect imageRectangle;

  for (int i = 0; i < 4; i++) {
    if (!apLogoCache [i])
    {
      SDL_Surface *imageSurface = SDL_LoadBMP (gameLogo[i]);
      if (!imageSurface)
        continue;
      apLogoCache [i] = SDL_CreateTextureFromSurface (mRenderer, imageSurface);
      SDL_FreeSurface (imageSurface);
    }
    if (!apLogoCache [i])
      continue;
    imageRectangle.w = IMAGE_WIDTH;
    imageRectangle.h = imageRectangle.w;
    imageRectangle.x = (screen_width - MENU_WIDTH)/2 + 13 + i * (IMAGE_WIDTH + 26 + FRAME_SPACE);// 13-px spacing
    imageRectangle.y = 320 + 13;

    SDL_RenderCopy (mRenderer, apLogoCache [i], NULL, &imageRectangle);
  }
}


void DSPRenderLogo (int screen_width, int screen_height)
{
  SDL_Surface* logoSurface;
  SDL_Texture* logoTexture;
  SDL_Rect logoRectangle;

  logoSurface = SDL_LoadBMP (pmdLogo[0]);
  logoTexture = SDL_CreateTextureFromSurface (lRenderer, logoSurface);
  
  logoRectangle.w = 830;
  logoRectangle.h = 570;
  logoRectangle.x = (screen_width - 830)/2;
  logoRectangle.y = (screen_height - 570)/2;

  SDL_RenderCopy (lRenderer, logoTexture, NULL, &logoRectangle);
  SDL_RenderPresent (lRenderer);
  SDL_Delay(1500);// wait 1.5 sec.
 
  SDL_DestroyTexture (logoTexture);
  SDL_FreeSurface (logoSurface);
}


/// Icon of a saved state, see below.
static void DSPDrawIconState (int iX, int iY, int iSize);

void DSPRenderMenu (int screen_width, int screen_height, int index, const bool *abState)
{
  SDL_Rect frameRectangle;
  SDL_Rect frameRectangleInner;
  Uint64 iPerfBegin = SDL_GetPerformanceCounter ();

  // The display swaps buffers, without a clear the games list drawn earlier shows through in every other frame.
  SDL_SetRenderDrawColor (mRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear (mRenderer);

  for (int i = 0; i < 4; i++) {
    frameRectangle.w = 200;
    frameRectangle.h = 260;
    frameRectangle.x = (screen_width - MENU_WIDTH)/2 + i *(FRAME_WIDTH + FRAME_SPACE);
    frameRectangle.y = 320;

    frameRectangleInner.w = 198;
    frameRectangleInner.h = 258;
    frameRectangleInner.x = (screen_width - MENU_WIDTH)/2 + 1 + i *(FRAME_WIDTH + FRAME_SPACE);
    frameRectangleInner.y = 320 + 1;

    if (i == index) { SDL_SetRenderDrawColor (mRenderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    } else { SDL_SetRenderDrawColor (mRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE); }

    SDL_RenderDrawRect (mRenderer, &frameRectangle);
    SDL_RenderDrawRect (mRenderer, &frameRectangleInner);
    // A saved state of the game: the disk icon in the corner of the frame.
    if (abState && abState [i])
    {
      SDL_SetRenderDrawColor (mRenderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
      DSPDrawIconState (frameRectangle.x + FRAME_WIDTH - ICON_SLOT - 4, frameRectangle.y + frameRectangle.h + 8, FONT_HEIGHT);
    }
  }
  DSPRenderText (screen_width, screen_height);
  DSPRenderImage (screen_width, screen_height);
 
  SDL_RenderPresent (mRenderer);
  DSPMenuStat (iPerfBegin);
}





/// Gamepad icon: body with the cross of the D-pad on the left and two buttons on the right, iSize pixels high.
static void DSPDrawIconGamepad (int iX, int iY, int iSize)
{
  int iW = ICON_SLOT - 6, iH = iSize * 2 / 3, iTop = iY + (iSize - iH) / 2;
  SDL_Rect sBody = {iX, iTop, iW, iH};
  SDL_RenderDrawRect (mRenderer, &sBody);
  int iCx = iX + iW / 4, iCy = iTop + iH / 2;
  SDL_RenderDrawLine (mRenderer, iCx - 4, iCy, iCx + 4, iCy);
  SDL_RenderDrawLine (mRenderer, iCx, iCy - 4, iCx, iCy + 4);
  SDL_Rect sA = {iX + iW * 2 / 3, iCy - 5, 4, 4};
  SDL_Rect sB = {iX + iW * 2 / 3 + 7, iCy + 1, 4, 4};
  SDL_RenderFillRect (mRenderer, &sA);
  SDL_RenderFillRect (mRenderer, &sB);
}

/// TV remote control icon: tall narrow body with a round button on top and three rows of small buttons.
static void DSPDrawIconRemote (int iX, int iY, int iSize)
{
  int iW = iSize / 2 + 2, iLeft = iX + (ICON_SLOT - 6 - iW) / 2;
  SDL_Rect sBody = {iLeft, iY, iW, iSize};
  SDL_RenderDrawRect (mRenderer, &sBody);
  SDL_Rect sTop = {iLeft + iW / 2 - 2, iY + 3, 4, 4};
  SDL_RenderFillRect (mRenderer, &sTop);
  for (int iRow = 0 ; iRow < 3 ; iRow ++)
    for (int iCol = 0 ; iCol < 2 ; iCol ++)
    {
      SDL_Rect sKey = {iLeft + 3 + iCol * (iW / 2 - 1), iY + 10 + iRow * 4, 3, 2};
      SDL_RenderFillRect (mRenderer, &sKey);
    }
}


/// Saved state icon: a floppy disk with the shutter on top and the label below, iSize pixels high.
static void DSPDrawIconState (int iX, int iY, int iSize)
{
  int iW = iSize * 2 / 3 + 2, iH = iSize * 2 / 3 + 2, iTop = iY + (iSize - iH) / 2, iLeft = iX + (ICON_SLOT - 6 - iW) / 2;
  SDL_Rect sBody = {iLeft, iTop, iW, iH};
  SDL_RenderDrawRect (mRenderer, &sBody);
  SDL_Rect sShutter = {iLeft + iW / 4, iTop, iW / 2, iH / 3};
  SDL_RenderFillRect (mRenderer, &sShutter);
  SDL_Rect sLabel = {iLeft + 3, iTop + iH / 2 + 1, iW - 6, iH / 2 - 4};
  SDL_RenderDrawRect (mRenderer, &sLabel);
}

/// A filled triangle pointing right: the row opens a group of the versions of one game (a family).
static void DSPDrawIconMore (int iX, int iY, int iSize)
{
  int iW = iSize / 2, iH = iSize * 2 / 3, iCenter = iY + iSize / 2;
  for (int i = 0 ; i < iW ; i ++)
  {
    int iHalf = (iH / 2) * (iW - i) / iW;
    SDL_RenderDrawLine (mRenderer, iX + i, iCenter - iHalf, iX + i, iCenter + iHalf);
  }
}

/** Draw a text broken at spaces (preferably double ones) into at most iMaxLines lines of iMaxChars characters (the rest is cut with "..."),
 *  the first line at iY, the next ones iLineHeight lower.
 */
static void DSPDrawWrapped (TTF_Font *pFont, const std::string &sText, int iX, int iY, int iMaxChars, int iMaxLines, int iLineHeight)
{
  std::string sRest = sText;
  for (int iLine = 0 ; iLine < iMaxLines && !sRest.empty () ; iLine ++)
  {
    std::string sPart = sRest;
    if ((int) sRest.size () > iMaxChars)
    {
      // A break at a double space (between the groups of a text) is better than inside a group.
      size_t iBreak = sRest.rfind ("  ", iMaxChars);
      if (iBreak == std::string::npos || iBreak == 0)
        iBreak = sRest.rfind (' ', iMaxChars);
      if (iBreak == std::string::npos || iBreak == 0)
        iBreak = iMaxChars;
      sPart = sRest.substr (0, iBreak);
      sRest.erase (0, iBreak);
      while (!sRest.empty () && sRest [0] == ' ')
        sRest.erase (0, 1);
      if (iLine == iMaxLines - 1 && !sRest.empty () && (int) sPart.size () > 3)
        sPart = sPart.substr (0, sPart.size () - 3) + "...";
    }
    else
      sRest.clear ();
    DSPDrawText (pFont, sPart, iX, iY + iLine * iLineHeight);
  }
}

void DSPRenderGameList (int screen_width, int screen_height, int index, const std::string &sTitle, const std::vector <std::string> &oNames, const std::vector <std::string> &oNotes, const std::vector <std::string> &oInputs, const std::string &sHelp)
{
  const int iRowHeight = FONT_HEIGHT + 8;
  const int iTop = 140;
  const int iBottom = screen_height - 205;
  int iRows = (iBottom - iTop) / iRowHeight;
  if (iRows < 5)
    iRows = 5;
  int iCount = (int) oNames.size ();
  int iFirst = index - iRows / 2;
  if (iFirst > iCount - iRows)
    iFirst = iCount - iRows;
  if (iFirst < 0)
    iFirst = 0;

  Uint64 iPerfBegin = SDL_GetPerformanceCounter ();
  TTF_Font *pFont = DSPMenuFont ();
  if (!pFont)
    return;
  SDL_SetRenderDrawColor (mRenderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
  SDL_RenderClear (mRenderer);
  SDL_SetRenderDrawColor (mRenderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

  int iX = (screen_width - MENU_WIDTH) / 2;
  DSPDrawText (pFont, sTitle + " " + std::to_string (index + 1) + "/" + std::to_string (iCount), iX, 60);
  for (int i = iFirst ; i < iCount && i < iFirst + iRows ; i ++)
  {
    int iY = iTop + (i - iFirst) * iRowHeight;
    if (i == index)
    {
      SDL_Rect sFrame = {iX - 12, iY - 4, MENU_WIDTH + 24, iRowHeight};
      SDL_RenderDrawRect (mRenderer, &sFrame);
      SDL_Rect sInner = {sFrame.x + 1, sFrame.y + 1, sFrame.w - 2, sFrame.h - 2};
      SDL_RenderDrawRect (mRenderer, &sInner);
    }
    // The icons of the devices the game can be played with (gamepad, TV remote control) left of the name.
    if (oInputs [i].find ('P') != std::string::npos)
      DSPDrawIconGamepad (iX, iY, FONT_HEIGHT);
    if (oInputs [i].find ('R') != std::string::npos)
      DSPDrawIconRemote (iX + ICON_SLOT, iY, FONT_HEIGHT);
    // The icon of a saved state of the game.
    if (oInputs [i].find ('S') != std::string::npos)
      DSPDrawIconState (iX + 2 * ICON_SLOT, iY, FONT_HEIGHT);
    DSPDrawText (pFont, oNames [i], iX + 3 * ICON_SLOT, iY);
    // A family (the versions of one game): the triangle after the name says that it opens.
    if (oInputs [i].find ('F') != std::string::npos)
      DSPDrawIconMore (iX + 3 * ICON_SLOT + FONT_WIDTH * (int) oNames [i].size () + 10, iY, FONT_HEIGHT);
    DSPDrawText (pFont, oNotes [i], iX + 3 * ICON_SLOT + FONT_WIDTH * 14, iY);
  }
  DSPDrawIconGamepad (iX, screen_height - 30, FONT_HEIGHT);
  DSPDrawText (pFont, "= gamepad", iX + ICON_SLOT, screen_height - 30);
  DSPDrawIconRemote (iX + FONT_WIDTH * 14, screen_height - 30, FONT_HEIGHT);
  DSPDrawText (pFont, "= ovladac TV (CEC)", iX + FONT_WIDTH * 14 + ICON_SLOT, screen_height - 30);
  DSPDrawIconState (iX + FONT_WIDTH * 35, screen_height - 30, FONT_HEIGHT);
  DSPDrawText (pFont, "= ulozeny stav", iX + FONT_WIDTH * 35 + ICON_SLOT, screen_height - 30);
  // The texts under the list are broken into lines that fit the screen.
  int iChars = std::max (20, (screen_width - iX - 24) / FONT_WIDTH);
  DSPDrawWrapped (pFont, sHelp.empty () ? "Ovladani: neznamo (zkus sipky, mezernik, Enter)" : "Ovladani: " + sHelp, iX, screen_height - 170, iChars, 2, 28);
  DSPDrawWrapped (pFont, "Sipky nebo [ E ] [ Y ]: vyber  <- -> strana  [ Enter ]: spust  [ Esc ]: zpet", iX, screen_height - 100, iChars, 2, 28);
  SDL_RenderPresent (mRenderer);
  DSPMenuStat (iPerfBegin);
}

//-------------------------------------------------------------------------------------------------------
