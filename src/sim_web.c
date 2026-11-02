/*

SIM_WEB.C

Remote control over HTTP: a web page shows the screen of the simulated machine and sends the keys
of the browser (or of an on-screen gamepad) to the simulator, so it works in a desktop or phone browser.

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
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>


//--------------------------------------------------------------------------
// Data

/// Base address of the video memory, bytes per line in memory, visible bytes per line and lines.
#define PMD_WEB_VRAM_BASE       0xC000
#define PMD_WEB_VRAM_LINE       64
#define PMD_WEB_VRAM_WIDTH      48
#define PMD_WEB_VRAM_HEIGHT     256

/// Maximal size of the request head.
#define PMD_WEB_REQUEST_MAX     4096

static std::thread oThread;
static std::atomic <bool> bRunning (false);
static int iListen = -1;
static std::string sToken;
static std::string (*pStateFunction) () = NULL;

/// The web page. The screen is 288x256, 6 pixels per byte, bit 6 is gray and bit 7 blinks (as in sim_display.c).
static const char *pPage = R"HTML(<!doctype html>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>PMD-85</title>
<style>
body{background:#111;color:#ddd;font:16px monospace;margin:0;display:flex;flex-direction:column;align-items:center;touch-action:manipulation}
canvas{width:min(96vw,864px,calc((100vh - 300px) * 1.125));image-rendering:pixelated;background:#000;margin-top:8px}
#menu{width:min(96vw,864px);min-height:3em;white-space:pre-wrap;margin-top:6px}
.sel{background:#ddd;color:#000}
#pad{display:grid;grid-template-columns:repeat(4,70px);gap:6px;margin:10px 0 6px}
button{height:46px;font:16px monospace;background:#333;color:#eee;border:1px solid #666;border-radius:8px;user-select:none;-webkit-user-select:none}
button.on{background:#888}
</style>
<canvas id="c" width="288" height="256"></canvas>
<div id="menu"></div>
<div id="pad"></div>
<script>
const T = new URLSearchParams(location.search).get('t') || '';
const q = p => p + (p.includes('?') ? '&' : '?') + 't=' + encodeURIComponent(T);
const cv = document.getElementById('c'), cx = cv.getContext('2d'), img = cx.createImageData(288, 256);
let mode = 'menu';

async function screen() {
  try {
    const b = new Uint8Array(await (await fetch(q('/screen'), {cache: 'no-store'})).arrayBuffer());
    if (b.length === 48 * 256) {
      const vis = (Date.now() / 512 | 0) & 1, px = img.data;
      for (let y = 0; y < 256; y++) for (let x = 0; x < 48; x++) {
        const d = b[y * 48 + x];
        let col = (d & 64) ? 146 : 255;
        if ((d & 128) && !vis) col = 0;
        for (let k = 0; k < 6; k++) {
          const i = (y * 288 + x * 6 + k) * 4, v = (d >> k) & 1 ? col : 0;
          px[i] = px[i + 1] = px[i + 2] = v; px[i + 3] = 255;
        }
      }
      cx.putImageData(img, 0, 0);
    }
  } catch (e) {}
  setTimeout(screen, 100);
}

async function state() {
  try {
    const s = await (await fetch(q('/state'), {cache: 'no-store'})).json();
    mode = s.mode;
    if (s.pad !== padText) buildPad(s.pad);
    const m = document.getElementById('menu');
    if (s.mode === 'game') { m.textContent = ''; }
    else if (s.mode === 'menu') {
      m.innerHTML = 'Menu: ' + s.featured.map((n, i) => i === s.index ? '<span class="sel"> ' + n + ' </span>' : ' ' + n + ' ').join('') +
        '\n[Left] [Right] select, [Enter] start, [E] list of all games';
    } else {
      const from = Math.max(0, Math.min(s.index - 7, s.games.length - 15));
      m.innerHTML = 'All games ' + (s.index + 1) + '/' + s.games.length + '\n' +
        s.games.slice(from, from + 15).map((n, i) => from + i === s.index ? '<span class="sel"> ' + n + ' </span>' : ' ' + n).join('\n');
    }
  } catch (e) {}
  setTimeout(state, 400);
}

const names = {ArrowLeft: 'Left', ArrowRight: 'Right', ArrowUp: 'Up', ArrowDown: 'Down', Enter: 'Return', ' ': 'Space',
  Escape: 'Escape', Backspace: 'Backspace', Delete: 'Delete', Insert: 'Insert', Home: 'Home', End: 'End',
  PageUp: 'PageUp', PageDown: 'PageDown', Shift: 'Left Shift', Control: 'Left Ctrl'};
function name(k) {
  if (names[k]) return names[k];
  if (/^F\d+$/.test(k)) return k;
  return k.length === 1 ? k.toLowerCase() : null;
}
const down = new Set();
function send(n, d) { fetch(q('/key?k=' + encodeURIComponent(n) + '&d=' + d), {cache: 'no-store'}).catch(() => {}); }
// Esc in the main menu quits the emulator and halts the machine.
function press(n) {
  if (n === 'Escape' && mode === 'menu' && !confirm('Quit the emulator (and halt the machine)?')) return;
  if (!down.has(n)) { down.add(n); send(n, 1); }
}
function release(n) { if (down.has(n)) { down.delete(n); send(n, 0); } }
addEventListener('keydown', e => { const n = name(e.key); if (!n) return; e.preventDefault(); if (!e.repeat) press(n); });
addEventListener('keyup', e => { const n = name(e.key); if (!n) return; e.preventDefault(); release(n); });
addEventListener('blur', () => [...down].forEach(release));

// On-screen gamepad: the buttons come from the server (state.pad, "Label:key,..."), they depend on the game.
// Up and down are the PMD 85 keys E and Y (host key Z). "key&key" presses two keys at once.
let padText = null;
function buildPad(text) {
  padText = text;
  const box = document.getElementById('pad');
  [...down].forEach(release);
  box.textContent = '';
  for (const item of text.split(',')) {
    const i = item.lastIndexOf(':');
    const label = i < 0 ? item : item.slice(0, i), keys = (i < 0 ? item : item.slice(i + 1)).split('&');
    const b = document.createElement('button');
    b.textContent = label;
    b.addEventListener('pointerdown', e => { e.preventDefault(); b.classList.add('on'); keys.forEach(press); });
    for (const ev of ['pointerup', 'pointercancel', 'pointerleave']) b.addEventListener(ev, () => { b.classList.remove('on'); keys.forEach(release); });
    box.appendChild(b);
  }
}
screen(); state();
</script>
)HTML";


//--------------------------------------------------------------------------
// Operations

/** Decode a URL encoded string.
 */
static std::string WEBDecode (const std::string &sText)
{
  std::string sResult;
  for (size_t i = 0 ; i < sText.size () ; i ++)
  {
    if (sText [i] == '%' && i + 2 < sText.size () && isxdigit (sText [i + 1]) && isxdigit (sText [i + 2]))
    {
      sResult += (char) strtol (sText.substr (i + 1, 2).c_str (), NULL, 16);
      i += 2;
    }
    else if (sText [i] == '+')
      sResult += ' ';
    else
      sResult += sText [i];
  }
  return (sResult);
}


/** Parse the query string "a=1&b=2" to a map.
 */
static std::map <std::string, std::string> WEBQuery (const std::string &sQuery)
{
  std::map <std::string, std::string> oResult;
  size_t iPos = 0;
  while (iPos < sQuery.size ())
  {
    size_t iEnd = sQuery.find ('&', iPos);
    if (iEnd == std::string::npos)
      iEnd = sQuery.size ();
    std::string sItem = sQuery.substr (iPos, iEnd - iPos);
    iPos = iEnd + 1;
    size_t iEqual = sItem.find ('=');
    if (iEqual != std::string::npos)
      oResult [WEBDecode (sItem.substr (0, iEqual))] = WEBDecode (sItem.substr (iEqual + 1));
  }
  return (oResult);
}


/** Send a response.
 */
static void WEBRespond (int iClient, int iCode, const char *pType, const std::string &sBody)
{
  const char *pStatus = iCode == 200 ? "OK" : iCode == 403 ? "Forbidden" : "Not Found";
  std::string sHead = "HTTP/1.1 " + std::to_string (iCode) + " " + pStatus + "\r\nContent-Type: " + pType +
                      "\r\nContent-Length: " + std::to_string (sBody.size ()) +
                      "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  std::string sAll = sHead + sBody;
  size_t iSent = 0;
  while (iSent < sAll.size ())
  {
    ssize_t iResult = send (iClient, sAll.data () + iSent, sAll.size () - iSent, MSG_NOSIGNAL);
    if (iResult <= 0)
      break;
    iSent += iResult;
  }
}


/** Handle one request.
 */
static void WEBHandle (int iClient)
{
  struct timeval sTimeout = {2, 0};
  setsockopt (iClient, SOL_SOCKET, SO_RCVTIMEO, &sTimeout, sizeof (sTimeout));
  std::string sRequest;
  char acBuffer [512];
  while (sRequest.find ("\r\n\r\n") == std::string::npos && sRequest.size () < PMD_WEB_REQUEST_MAX)
  {
    ssize_t iRead = recv (iClient, acBuffer, sizeof (acBuffer), 0);
    if (iRead <= 0)
      return;
    sRequest.append (acBuffer, iRead);
  }
  // Request line: GET <path>?<query> HTTP/1.1
  size_t iEnd = sRequest.find (' ', 4);
  if (sRequest.compare (0, 4, "GET ") != 0 || iEnd == std::string::npos)
  {
    WEBRespond (iClient, 404, "text/plain", "not found");
    return;
  }
  std::string sTarget = sRequest.substr (4, iEnd - 4);
  size_t iQuery = sTarget.find ('?');
  std::string sPath = sTarget.substr (0, iQuery);
  std::map <std::string, std::string> oQuery = WEBQuery (iQuery == std::string::npos ? "" : sTarget.substr (iQuery + 1));

  if (!sToken.empty () && oQuery ["t"] != sToken)
  {
    WEBRespond (iClient, 403, "text/plain", "forbidden");
    return;
  }
  if (sPath == "/")
    WEBRespond (iClient, 200, "text/html; charset=utf-8", pPage);
  else if (sPath == "/screen")
  {
    std::string sScreen;
    for (int iLine = 0 ; iLine < PMD_WEB_VRAM_HEIGHT ; iLine ++)
      sScreen.append (reinterpret_cast <const char *> (abMemoryData + PMD_WEB_VRAM_BASE + iLine * PMD_WEB_VRAM_LINE), PMD_WEB_VRAM_WIDTH);
    if (DSPNoBlink ())
      for (size_t i = 0 ; i < sScreen.size () ; i ++)
        sScreen [i] &= 0x7F;
    WEBRespond (iClient, 200, "application/octet-stream", sScreen);
  }
  else if (sPath == "/state")
    WEBRespond (iClient, 200, "application/json", pStateFunction ? pStateFunction () : "{}");
  else if (sPath == "/key")
  {
    SDL_Keycode iSym = SDL_GetKeyFromName (oQuery ["k"].c_str ());
    if (iSym == SDLK_UNKNOWN)
    {
      WEBRespond (iClient, 404, "text/plain", "unknown key");
      return;
    }
    bool bDown = oQuery ["d"] == "1";
    SDL_Event sEvent;
    memset (&sEvent, 0, sizeof (sEvent));
    sEvent.type = bDown ? SDL_KEYDOWN : SDL_KEYUP;
    sEvent.key.state = bDown ? SDL_PRESSED : SDL_RELEASED;
    sEvent.key.keysym.sym = iSym;
    SDL_PushEvent (&sEvent);
    WEBRespond (iClient, 200, "text/plain", "ok");
  }
  else
    WEBRespond (iClient, 404, "text/plain", "not found");
}


/** Server thread, requests are handled one by one.
 */
static void WEBServe ()
{
  while (bRunning)
  {
    fd_set sSet;
    FD_ZERO (&sSet);
    FD_SET (iListen, &sSet);
    struct timeval sWait = {0, 200000};
    if (select (iListen + 1, &sSet, NULL, NULL, &sWait) <= 0)
      continue;
    int iClient = accept (iListen, NULL, NULL);
    if (iClient < 0)
      continue;
    WEBHandle (iClient);
    close (iClient);
  }
}


void WEBInitialize (int iPort, const char *pToken, std::string (*pState) ())
{
  sToken = pToken ? pToken : "";
  pStateFunction = pState;
  iListen = socket (AF_INET, SOCK_STREAM, 0);
  if (iListen < 0)
    return;
  int iOne = 1;
  setsockopt (iListen, SOL_SOCKET, SO_REUSEADDR, &iOne, sizeof (iOne));
  struct sockaddr_in sAddress;
  memset (&sAddress, 0, sizeof (sAddress));
  sAddress.sin_family = AF_INET;
  sAddress.sin_addr.s_addr = htonl (INADDR_ANY);
  sAddress.sin_port = htons (iPort);
  if (bind (iListen, (struct sockaddr *) &sAddress, sizeof (sAddress)) < 0 || listen (iListen, 8) < 0)
  {
    fprintf (stderr, "PMD_WEB: cannot listen on port %d\n", iPort);
    close (iListen);
    iListen = -1;
    return;
  }
  bRunning = true;
  oThread = std::thread (WEBServe);
  printf ("Web remote control on port %d%s\n", iPort, sToken.empty () ? " (no token!)" : "");
}


void WEBShutdown ()
{
  if (iListen < 0)
    return;
  bRunning = false;
  oThread.join ();
  close (iListen);
  iListen = -1;
}
