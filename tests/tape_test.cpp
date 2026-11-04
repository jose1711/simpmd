// Test of the tape module (sim_tape.c): it is linked with a stub of the processor clock and driven through its functions.
// Run by "make check"; the argument is an empty directory for the saved tapes. The same file can be built against an older
// sim_tape.c to compare two versions (A/B).
#include "sim_common.h"
#include <fstream>
#include <unistd.h>
#include <stdio.h>
relaxed_int iProcessorClock (0);
bool TAPInputFileStatus ();
bool TAPStatusRxRDY ();
static int iFail = 0;
#define CHECK(c, m) do { bool ok = (c); printf ("  %s %s\n", ok ? "ok  " : "FAIL", m); if (!ok) iFail ++; } while (0)
static std::string Slurp (const char *p) { std::ifstream f (p, std::ios::binary); return std::string ((std::istreambuf_iterator <char> (f)), std::istreambuf_iterator <char> ()); }
static void Put (const char *p, const char *s) { std::ofstream f (p, std::ios::binary); f << s; }
static void Write (const char *s) { for (; *s ; s ++) TAPWriteData (*s); }
static void Wait () { SDL_Delay (260); }

int main (int, char **argv)
{
  SDL_Init (SDL_INIT_TIMER);
  const std::string sDir = argv [1];
  std::string sA = sDir + "/saveA", sB = sDir + "/saveB", sG = sDir + "/game";
  remove (sA.c_str ()); remove (sB.c_str ());
  Put (sG.c_str (), "XYZ");
  TAPInitialize ();
  Wait ();

  printf ("Fix 2: a second save replaces the first\n");
  TAPSetSaveFile (sA);
  Write ("AAA");
  TAPInputFileStatus ();                      // the program loads: the saved tape is opened (and flushed)
  CHECK (Slurp (sA.c_str ()) == "AAA", "first save is on the disk when it is read");
  Write ("BB");                               // the second save
  Wait ();
  TAPInputFileStatus ();
  CHECK (Slurp (sA.c_str ()) == "BB", "second save replaced the file (not AAABB)");

  printf ("Fix 3: the tape flags do not survive into the next game\n");
  Put (sB.c_str (), "SSS");
  TAPSetSaveFile (sA);
  Write ("Q");
  Wait ();
  TAPInputFileStatus ();                      // game 1 reads its saved tape: bSaveStream is on
  TAPSetSaveFile (sB);                        // game 2 starts
  TAPSetAutoRewind (sG);
  TAPNextInputFile (std::vector <std::string> (1, sG));
  std::string sRead;
  for (int i = 0 ; i < 3 ; i ++) sRead += (char) TAPReadData ();
  CHECK (sRead == "XYZ", "game 2 reads its own tape");
  Wait ();
  CHECK (TAPStatusRxRDY (), "data after the end of the game tape");
  char c = TAPReadData ();
  printf ("  next byte after the end of the game tape: '%c'\n", c);
  CHECK (c == 'S', "it is the saved tape of game 2 (not the game tape rewound)");

  printf ("Fix 3b: the output of game 1 is not written into game 2\n");
  TAPSetSaveFile (sA);
  Write ("1111");
  TAPSetSaveFile (sB);
  Write ("2222");
  TAPShutdown ();
  CHECK (Slurp (sA.c_str ()) .find ("2222") == std::string::npos, "game 2 bytes are not in the file of game 1");
  CHECK (Slurp (sB.c_str ()) == "2222", "file of game 2 holds its own save");
  printf (iFail ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", iFail);
  return (iFail != 0);
}
