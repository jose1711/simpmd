/*

SIM_PROCESSOR.C

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

 - Removed suspend/resume.
 - Reset termination token on CPU init.

*/

#include "sim_common.h"
#include <set>

#include <semaphore.h>
#include <unistd.h>
#include <string.h>


//--------------------------------------------------------------------------
// Synchronization Notes

// The processor simulation executes in an independent thread. The simulator
// needs to initialize the module before starting the module thread and
// terminate the module thread before shutting down the module.
//
// CPUInitialize - CPUStartThread - CPUTerminateThread - CPUShutdown
//
// Access to processor simulation is limited:
//
//  - External functions can be called but their execution will
//    wait until time synchronization occurs during simulation.
//
//  - Processor clock can be accessed atomically.


//--------------------------------------------------------------------------
// Asynchronous Execution Variables

static pthread_t sThread;

// Synchronization variables ...

static relaxed_bool bSuspend (false);
static sem_t sSignalSuspend;
static sem_t sSignalResume;

static relaxed_bool bTerminate (false);

// Pause for the saved state: the processor thread parks itself at the next timing check (about every millisecond
// of the simulated time), so the check costs nothing per instruction.
static relaxed_bool bPauseRequest (false);
static relaxed_bool bPaused (false);


//--------------------------------------------------------------------------
// Processor Status Variables

// Registers

// The registers are stored in unions that allow
// access to the two individual bytes as well
// as to the combined word ...

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#  define RegisterPairStruct(H,L) struct { byte L; byte H; }
#endif

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#  define RegisterPairStruct(H,L) struct { byte H; byte L; }
#endif

#define RegisterPairUnion(H,L) union { word Pair; RegisterPairStruct (H,L) One; } RegisterPair##H##L

static RegisterPairUnion (A,F);
static RegisterPairUnion (B,C);
static RegisterPairUnion (D,E);
static RegisterPairUnion (H,L);

#define RegAF (RegisterPairAF.Pair)
#define RegBC (RegisterPairBC.Pair)
#define RegDE (RegisterPairDE.Pair)
#define RegHL (RegisterPairHL.Pair)

#define RegA (RegisterPairAF.One.A)
#define RegF (RegisterPairAF.One.F)
#define RegB (RegisterPairBC.One.B)
#define RegC (RegisterPairBC.One.C)
#define RegD (RegisterPairDE.One.D)
#define RegE (RegisterPairDE.One.E)
#define RegH (RegisterPairHL.One.H)
#define RegL (RegisterPairHL.One.L)

static word RegPC;
static word RegSP;

// Individual flag variables, kept because it is easier to
// manipulate the individual variables than the flag register.
// Note that the two representations of flags are not kept
// synchronized, rather, individual flag variables take
// precendence over the flag register ...

static bool FlagS;
static bool FlagZ;
static bool FlagH;
static bool FlagP;
static bool FlagC;


//--------------------------------------------------------------------------
// Helper Variables

static bool abParity [256];


//--------------------------------------------------------------------------
// Helper Functions

// These are mostly inline functions rather than
// macros to avoid evaluating arguments multiple
// times.

/// Reads a byte from simulated memory array
inline byte MemReadByte (word iAddr)
{
  return (abMemoryData [iAddr]);
}

/// Reads a word from simulated memory array
inline word MemReadWord (word iAddr)
{
  return (MemReadByte (iAddr) + ((word) MemReadByte (iAddr + (word) 1) << 8));
}

/// The RAM under the monitor ROM of the PMD 85-2A (AllRAM), written while the ROM is mapped.
static byte abRamUnderRom [4096];

/// Writes a byte to simulated memory array if it is writable.
inline void MemWriteByte (word iAddr, byte iData)
{
  byte iMask = abMemoryMask [iAddr];
  if (iMask == 1)
    abMemoryData [iAddr] = iData;
  else if (iMask == 2)
    abRamUnderRom [iAddr & 0x0FFF] = iData;
}

/// Writes a word to simulated memory array if it is writable.
inline void MemWriteWord (word iAddr, word iData)
{
  MemWriteByte (iAddr, iData);
  MemWriteByte (iAddr + (word) 1, iData >> 8);
}

/// Pushes a word onto the stack in the simulated memory array.
inline void MemPushWord (word iData)
{
  RegSP -= 2;
  MemWriteWord (RegSP, iData);
}

/// Pops a byte from the stack in the simulated memory array.
inline byte MemPopByte ()
{
  return (MemReadByte (RegSP ++));
}
/// Pops a word from the stack in the simulated memory array.
inline word MemPopWord ()
{
  byte iLow = MemPopByte ();
  byte iHigh = MemPopByte ();
  return (iLow + ((word) iHigh << 8));
}

/// Fetches a byte from simulated memory array at PC and shifts PC.
inline byte MemFetchByte ()
{
  return (MemReadByte (RegPC ++));
}
/// Fetches a word from simulated memory array at PC and shifts PC.
inline word MemFetchWord ()
{
  byte iLow = MemFetchByte ();
  byte iHigh = MemFetchByte ();
  return (iLow + ((word) iHigh << 8));
}


/// Expands its argument with all registers.
#define InstAllRegisters(I)                     \
  I (B)                                         \
  I (C)                                         \
  I (D)                                         \
  I (E)                                         \
  I (H)                                         \
  I (L)                                         \
  I (A)

/// Expands its arguments with all register pairs.
#define InstAllRegisterPairs(I)                 \
  I (BC,B)                                      \
  I (DE,D)                                      \
  I (HL,H)                                      \
  I (SP,SP)

/// Expands its arguments with all conditions.
#define InstAllConditions(I)                    \
  I (NZ,!FlagZ)                                 \
  I (Z,FlagZ)                                   \
  I (NC,!FlagC)                                 \
  I (C,FlagC)                                   \
  I (PO,!FlagP)                                 \
  I (PE,FlagP)                                  \
  I (P,!FlagS)                                  \
  I (M,FlagS)


/// Displays instructions when instruction tracing is enabled.
#ifdef DEBUG_CPU_TRACE_INSTRUCTIONS
#  define CPU_LOG_INSTRUCTION(X) DEBUG_LOG_PARTIAL (X)
#else
#  define CPU_LOG_INSTRUCTION(X)
#endif

/// Displays a formatted byte value.
#define CPU_LOG_FORMAT_BYTE(X) std::hex << std::uppercase << std::setfill ('0') << std::setw (2) << (int) X
/// Displays a formatted word value.
#define CPU_LOG_FORMAT_WORD(X) std::hex << std::uppercase << std::setfill ('0') << std::setw (4) << (int) X

#define CPU_LOG_INST_X(X)       CPU_LOG_INSTRUCTION (#X)

#define CPU_LOG_INST_B(X)       CPU_LOG_INSTRUCTION (#X " " << CPU_LOG_FORMAT_BYTE (MemReadByte (RegPC)))
#define CPU_LOG_INST_W(X)       CPU_LOG_INSTRUCTION (#X " " << CPU_LOG_FORMAT_WORD (MemReadWord (RegPC)))

#define CPU_LOG_INST_RB(X,R)    CPU_LOG_INSTRUCTION (#X " " #R "," << CPU_LOG_FORMAT_BYTE (MemReadByte (RegPC)))
#define CPU_LOG_INST_RW(X,R)    CPU_LOG_INSTRUCTION (#X " " #R "," << CPU_LOG_FORMAT_WORD (MemReadWord (RegPC)))

#define CPU_LOG_INST_R(X,R)     CPU_LOG_INSTRUCTION (#X " " #R)
#define CPU_LOG_INST_RR(X,D,S)  CPU_LOG_INSTRUCTION (#X " " #D "," #S)


//--------------------------------------------------------------------------
// Control Operations


/// Packs the value of individual flag variables into the flag register.
inline void FlagsPack ()
{
  // The calculation assumes that FALSE is 0 and
  // TRUE is 1, which is what the standard says.
  RegF = 128 * FlagS +
          64 * FlagZ +
          16 * FlagH +
           4 * FlagP +
           2         +
           1 * FlagC;
}

/// Unpacks the value of individual flag variables from the flag register.
inline void FlagsUnpack ()
{
  // The calculation assumes that assignment of nonzero value
  // to boolean will be TRUE, which is what the standard says.
  FlagS = RegF & 128;
  FlagZ = RegF & 64;
  FlagH = RegF & 16;
  FlagP = RegF & 4;
  FlagC = RegF & 1;
}


//==========================================================================
// PROCESSOR INSTRUCTIONS
//==========================================================================
// Functions that implement the individual processor instructions.
// They are sorted in the same way as in the processor manuals.
//--------------------------------------------------------------------------

//--------------------------------------------------------------------------
// Helper Macros

// This code assumes that sizeof (uint) is more than a byte ...

#define MathExpandBinaryFull(L,O,R) ((uint) L) O ((uint) R)
#define MathExpandBinaryHalf(L,O,R) ((uint) L & 0xF) O ((uint) R & 0xF)

#define MathExpandCarryFull(L,O,R) ((uint) L) O ((uint) R) O (uint) FlagC
#define MathExpandCarryHalf(L,O,R) ((uint) L & 0xF) O ((uint) R & 0xF) O (uint) FlagC

// MathGeneric - Performs an arbitrary arithmetic operation
#define MathGeneric(L,O,R,X)                    \
  uint iResult = MathExpand##X##Full (L,O,R);

// MathGenericSZHP - Performs an arbitrary arithmetic operation and adjusts SZHP
#define MathGenericSZHP(L,O,R,X)                \
  MathGeneric(L,O,R,X)                          \
  FlagS = ((byte) iResult > 127);               \
  FlagZ = ((byte) iResult == 0);                \
  FlagH = MathExpand##X##Half (L,O,R) > 0xF;    \
  FlagP = abParity [(byte) iResult];

// MathGenericSZHPC - Performs an arbitrary arithmetic operation and adjusts SZHPC
#define MathGenericSZHPC(L,O,R,X)               \
  MathGenericSZHP(L,O,R,X)                      \
  FlagC = (iResult > 255);

// LogicalGenericSZHPC - Performs an arbitrary logical operation and adjusts SZHPC
#define LogicalGenericSZHPC(L,O,R)              \
  byte iResult = L O R;                         \
  FlagS = (iResult > 127);                      \
  FlagZ = (iResult == 0);                       \
  FlagH = false;                                \
  FlagP = abParity [iResult];                   \
  FlagC = false;


//--------------------------------------------------------------------------
// Transfer Operations
//
// MOV, MVI, LXI, LDA, STA, LHLD, SHLD, LDAX, STAX, XCHG

// MOV between generic registers

#define InstMOVDstSrc(D,S)                      \
void InstMOV##D##S ()                           \
{                                               \
  CPU_LOG_INST_RR (MOV,D,S);                    \
  Reg##D = Reg##S;                              \
  iProcessorClock += 5;                         \
}

#define InstMOVDst(S)                           \
  InstMOVDstSrc (B,S)                           \
  InstMOVDstSrc (C,S)                           \
  InstMOVDstSrc (D,S)                           \
  InstMOVDstSrc (E,S)                           \
  InstMOVDstSrc (H,S)                           \
  InstMOVDstSrc (L,S)                           \
  InstMOVDstSrc (A,S)

InstAllRegisters (InstMOVDst)

#undef InstMOVDstSrc
#undef InstMOVDst

// MOV from register to memory

#define InstMOVMemSrc(S)                        \
void InstMOVM##S ()                             \
{                                               \
  CPU_LOG_INST_RR (MOV,M,S);                    \
  MemWriteByte (RegHL, Reg##S);                 \
  iProcessorClock += 7;                         \
}

InstAllRegisters (InstMOVMemSrc)

#undef InstMOVMemSrc

// MOV from memory to register

#define InstMOVMemDst(D)                        \
void InstMOV##D##M ()                           \
{                                               \
  CPU_LOG_INST_RR (MOV,D,M);                    \
  Reg##D = MemReadByte (RegHL);                 \
  iProcessorClock += 7;                         \
}

InstAllRegisters (InstMOVMemDst)

#undef InstMOVMemDst

// MVI to register

#define InstMVIDst(D)                           \
void InstMVI##D ()                              \
{                                               \
  CPU_LOG_INST_RB (MVI,D);                      \
  Reg##D = MemFetchByte ();                     \
  iProcessorClock += 7;                         \
}

InstAllRegisters (InstMVIDst)

#undef InstMVIDst

// MVI to memory

void InstMVIM ()
{
  CPU_LOG_INST_RB (MVI,M);
  MemWriteByte (RegHL, MemFetchByte ());
  iProcessorClock += 10;
}

// LXI

#define InstLXIDst(D,N)                         \
void InstLXI##N ()                              \
{                                               \
  CPU_LOG_INST_RW (LXI,N);                      \
  Reg##D = MemFetchWord ();                     \
  iProcessorClock += 10;                        \
}

InstAllRegisterPairs (InstLXIDst)

#undef InstLXIDst

// LDA

void InstLDA ()
{
  CPU_LOG_INST_W (LDA);
  RegA = MemReadByte (MemFetchWord ());
  iProcessorClock += 13;
}

// STA

void InstSTA ()
{
  CPU_LOG_INST_W (STA);
  MemWriteByte (MemFetchWord (), RegA);
  iProcessorClock += 13;
}

// LHLD

void InstLHLD ()
{
  CPU_LOG_INST_W (LHLD);
  RegHL = MemReadWord (MemFetchWord ());
  iProcessorClock += 16;
}

// SHLD

void InstSHLD ()
{
  CPU_LOG_INST_W (SHLD);
  int iAddr = MemFetchWord ();
  MemWriteWord (iAddr, RegHL);
  iProcessorClock += 16;
}

// LDAX

#define InstLDAXSrc(S,N)                        \
void InstLDAX##N ()                             \
{                                               \
  CPU_LOG_INST_R (LDAX,N);                      \
  RegA = MemReadByte (Reg##S);                  \
  iProcessorClock += 7;                         \
}

InstAllRegisterPairs (InstLDAXSrc)

#undef InstLDAXSrc

// STAX

#define InstSTAXSrc(S,N)                        \
void InstSTAX##N ()                             \
{                                               \
  CPU_LOG_INST_R (STAX,N);                      \
  MemWriteByte (Reg##S, RegA);                  \
  iProcessorClock += 7;                         \
}

InstAllRegisterPairs (InstSTAXSrc)

#undef InstSTAXSrc

// XCHG

void InstXCHG ()
{
  CPU_LOG_INST_X (XCHG);
  int iTemp = RegDE;
  RegDE = RegHL;
  RegHL = iTemp;
  iProcessorClock += 4;
}


//--------------------------------------------------------------------------
// Arithmetic Instructions
//
// ADD, ADI, ADC, ACI, SUB, SUI, SBB, SBI, INR, DCR, INX, DCX, DAD, DAA

// ADD from register

#define InstADDSrc(S)                           \
void InstADD##S ()                              \
{                                               \
  CPU_LOG_INST_R (ADD,S);                       \
  MathGenericSZHPC (RegA,+,Reg##S,Binary)       \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstADDSrc)

#undef InstADDSrc

// ADD from memory

void InstADDM ()
{
  CPU_LOG_INST_R (ADD,M);
  MathGenericSZHPC (RegA,+,MemReadByte (RegHL),Binary)
  RegA = iResult;
  iProcessorClock += 7;
}

// ADI

void InstADI ()
{
  CPU_LOG_INST_B (ADI);
  byte iOperand = MemFetchByte ();
  MathGenericSZHPC (RegA,+,iOperand,Binary)
  RegA = iResult;
  iProcessorClock += 7;
}

// ADC from register

#define InstADCSrc(S)                           \
void InstADC##S ()                              \
{                                               \
  CPU_LOG_INST_R (ADC,S);                       \
  MathGenericSZHPC (RegA,+,Reg##S,Carry)        \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstADCSrc)

#undef InstADCSrc

// ADC from memory

void InstADCM ()
{
  CPU_LOG_INST_R (ADC,M);
  MathGenericSZHPC (RegA,+,MemReadByte (RegHL),Carry)
  RegA = iResult;
  iProcessorClock += 7;
}

// ACI

void InstACI ()
{
  CPU_LOG_INST_B (ACI);
  byte iOperand = MemFetchByte ();
  MathGenericSZHPC (RegA,+,iOperand,Carry)
  RegA = iResult;
  iProcessorClock += 7;
}

// SUB from register

#define InstSUBSrc(S)                           \
void InstSUB##S ()                              \
{                                               \
  CPU_LOG_INST_R (SUB,S);                       \
  MathGenericSZHPC (RegA,-,Reg##S,Binary)       \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstSUBSrc)

#undef InstSUBSrc

// SUB from memory

void InstSUBM ()
{
  CPU_LOG_INST_R (SUB,M);
  MathGenericSZHPC (RegA,-,MemReadByte (RegHL),Binary)
  RegA = iResult;
  iProcessorClock += 7;
}

// SUI

void InstSUI ()
{
  CPU_LOG_INST_B (SUI);
  byte iOperand = MemFetchByte ();
  MathGenericSZHPC (RegA,-,iOperand,Binary)
  RegA = iResult;
  iProcessorClock += 7;
}

// SBB from register

#define InstSBBSrc(S)                           \
void InstSBB##S ()                              \
{                                               \
  CPU_LOG_INST_R (SBB,S);                       \
  MathGenericSZHPC (RegA,-,Reg##S,Carry)        \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstSBBSrc)

#undef InstSBBSrc

// SBB from memory

void InstSBBM ()
{
  CPU_LOG_INST_R (SBB,M);
  MathGenericSZHPC (RegA,-,MemReadByte (RegHL),Carry)
  RegA = iResult;
  iProcessorClock += 7;
}

// SBI

void InstSBI ()
{
  CPU_LOG_INST_B (SBI);
  byte iOperand = MemFetchByte ();
  MathGenericSZHPC (RegA,-,iOperand,Carry)
  RegA = iResult;
  iProcessorClock += 7;
}

// INR register

#define InstINRDst(D)                           \
void InstINR##D ()                              \
{                                               \
  CPU_LOG_INST_R (INR,D);                       \
  MathGenericSZHP (Reg##D,+,1,Binary)           \
  Reg##D = iResult;                             \
  iProcessorClock += 5;                         \
}

InstAllRegisters (InstINRDst)

#undef InstINRDst

// INR memory

void InstINRM ()
{
  CPU_LOG_INST_R (INR,M);
  MathGenericSZHP (MemReadByte (RegHL),+,1,Binary)
  MemWriteByte (RegHL, iResult);
  iProcessorClock += 10;
}

// DCR register

#define InstDCRDst(D)                           \
void InstDCR##D ()                              \
{                                               \
  CPU_LOG_INST_R (DCR,D);                       \
  MathGenericSZHP (Reg##D,-,1,Binary)           \
  Reg##D = iResult;                             \
  iProcessorClock += 5;                         \
}

InstAllRegisters (InstDCRDst)

#undef InstDCRDst

// DCR memory

void InstDCRM ()
{
  CPU_LOG_INST_R (DCR,M);
  MathGenericSZHP (MemReadByte (RegHL),-,1,Binary)
  MemWriteByte (RegHL, iResult);
  iProcessorClock += 10;
}

// INX

#define InstINXDst(D,N)                         \
void InstINX##N ()                              \
{                                               \
  CPU_LOG_INST_R (INX,N);                       \
  ++ Reg##D;                                    \
  iProcessorClock += 5;                         \
}

InstAllRegisterPairs (InstINXDst)

#undef InstINXDst

// DCX

#define InstDCXDst(D,N)                         \
void InstDCX##N ()                              \
{                                               \
  CPU_LOG_INST_R (DCX,N);                       \
  -- Reg##D;                                    \
  iProcessorClock += 5;                         \
}

InstAllRegisterPairs (InstDCXDst)

#undef InstDCXDst

// DAD

// This code assumes that sizeof (uint) is more than a word ...

#define InstDADSrc(S,N)                         \
void InstDAD##N ()                              \
{                                               \
  CPU_LOG_INST_R (DAD,N);                       \
  uint iResult = RegHL + Reg##S;                \
  FlagC = (iResult > 65535);                    \
  RegHL = iResult;                              \
  iProcessorClock += 10;                        \
}

InstAllRegisterPairs (InstDADSrc)

#undef InstDADSrc

// DAA

// Not at all sure how DAA should really set the flags ...

void InstDAA ()
{
  CPU_LOG_INST_X (DAA);
  uint iResult = RegA;
  // Correct the lower nibble ...
  if ((iResult & 0xF) > 0x9 || FlagH)
  {
    iResult += 0x6;
    FlagH = (iResult & 0xF) < 0x6;
  }
  else FlagH = false;
  // Correct the upper nibble ...
  if ((iResult & 0xF0) > 0x90 || FlagC)
  {
    iResult += 0x60;
    FlagC = (iResult & 0xF0) < 0x60;
  }
  else FlagC = false;
  // Set the remaining flags ...
  FlagS = (iResult > 127);
  FlagZ = ((byte) iResult == 0);
  FlagP = abParity [(byte) iResult];
  // Store the result ...
  RegA = iResult;
  // Update the clock ...
  iProcessorClock += 4;
}


//--------------------------------------------------------------------------
// Logical Instructions
//
// ANA, ANI, XRA, XRI, ORA, ORI, CMP, CPI, RLC, RRC, RAL, RAR, CMA, CMC, STC

// ANA from register

#define InstANASrc(S)                           \
void InstANA##S ()                              \
{                                               \
  CPU_LOG_INST_R (ANA,S);                       \
  LogicalGenericSZHPC (RegA,&,Reg##S)           \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstANASrc)

#undef InstANASrc

// ANA from memory

void InstANAM ()
{
  CPU_LOG_INST_R (ANA,M);
  LogicalGenericSZHPC (RegA,&,MemReadByte (RegHL))
  RegA = iResult;
  iProcessorClock += 7;
}

// ANI

void InstANI ()
{
  CPU_LOG_INST_B (ANI);
  byte iOperand = MemFetchByte ();
  LogicalGenericSZHPC (RegA,&,iOperand)
  RegA = iResult;
  iProcessorClock += 7;
}

// XRA from register

#define InstXRASrc(S)                           \
void InstXRA##S ()                              \
{                                               \
  CPU_LOG_INST_R (XRA,S);                       \
  LogicalGenericSZHPC (RegA,^,Reg##S)           \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstXRASrc)

#undef InstXRASrc

// XRA from memory

void InstXRAM ()
{
  CPU_LOG_INST_R (XRA,M);
  LogicalGenericSZHPC (RegA,^,MemReadByte (RegHL))
  RegA = iResult;
  iProcessorClock += 7;
}

// XRI

void InstXRI ()
{
  CPU_LOG_INST_B (XRI);
  byte iOperand = MemFetchByte ();
  LogicalGenericSZHPC (RegA,^,iOperand)
  RegA = iResult;
  iProcessorClock += 7;
}

// ORA from register

#define InstORASrc(S)                           \
void InstORA##S ()                              \
{                                               \
  CPU_LOG_INST_R (ORA,S);                       \
  LogicalGenericSZHPC (RegA,|,Reg##S)           \
  RegA = iResult;                               \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstORASrc)

#undef InstORASrc

// ORA from memory

void InstORAM ()
{
  CPU_LOG_INST_R (ORA,M);
  LogicalGenericSZHPC (RegA,|,MemReadByte (RegHL))
  RegA = iResult;
  iProcessorClock += 7;
}

// ORI

void InstORI ()
{
  CPU_LOG_INST_B (ORI);
  byte iOperand = MemFetchByte ();
  LogicalGenericSZHPC (RegA,|,iOperand)
  RegA = iResult;
  iProcessorClock += 7;
}

// CMP from register

#define InstCMPSrc(S)                           \
void InstCMP##S ()                              \
{                                               \
  CPU_LOG_INST_R (CMP,S);                       \
  MathGenericSZHPC (RegA,-,Reg##S,Binary)       \
  iProcessorClock += 4;                         \
}

InstAllRegisters (InstCMPSrc)

#undef InstCMPSrc

// CMP from memory

void InstCMPM ()
{
  CPU_LOG_INST_R (CMP,M);
  MathGenericSZHPC (RegA,-,MemReadByte (RegHL),Binary)
  iProcessorClock += 7;
}

// CPI

void InstCPI ()
{
  CPU_LOG_INST_B (CPI);
  byte iOperand = MemFetchByte ();
  MathGenericSZHPC (RegA,-,iOperand,Binary)
  iProcessorClock += 7;
}

// RLC

void InstRLC ()
{
  CPU_LOG_INST_X (RLC);
  FlagC = (RegA & 0x80);
  RegA = (RegA << 1) | (FlagC ? 0x1 : 0);
  iProcessorClock += 4;
}

// RRC

void InstRRC ()
{
  CPU_LOG_INST_X (RRC);
  FlagC = (RegA & 0x1);
  RegA = (RegA >> 1) | (FlagC ? 0x80 : 0);
  iProcessorClock += 4;
}

// RAL

void InstRAL ()
{
  CPU_LOG_INST_X (RAL);
  bool bOverflow = (RegA & 0x80);
  RegA = (RegA << 1) | (FlagC ? 0x1 : 0);
  FlagC = bOverflow;
  iProcessorClock += 4;
}

// RAR

void InstRAR ()
{
  CPU_LOG_INST_X (RAR);
  bool bOverflow = (RegA & 0x1);
  RegA = (RegA >> 1) | (FlagC ? 0x80 : 0);
  FlagC = bOverflow;
  iProcessorClock += 4;
}

// CMA

void InstCMA ()
{
  CPU_LOG_INST_X (CMA);
  RegA = ~RegA;
  iProcessorClock += 4;
}

// CMC

void InstCMC ()
{
  CPU_LOG_INST_X (CMC);
  FlagC = !FlagC;
  iProcessorClock += 4;
}

// STC

void InstSTC ()
{
  CPU_LOG_INST_X (STC);
  FlagC = true;
  iProcessorClock += 4;
}


//--------------------------------------------------------------------------
// Branch Instructions
//
// JMP, CALL, RET, RST, PCHL

// JMP

void InstJMP ()
{
  CPU_LOG_INST_W (JMP);
  RegPC = MemFetchWord ();
  iProcessorClock += 10;
}

// JMP conditional

#define InstJMPCon(N,E)                         \
void InstJ##N ()                                \
{                                               \
  CPU_LOG_INST_W (J##N);                        \
  word iTarget = MemFetchWord ();               \
  if (E) RegPC = iTarget;                       \
  iProcessorClock += 10;                        \
}

InstAllConditions (InstJMPCon)

#undef InstJMPCon

// CALL

void InstCALL ()
{
  CPU_LOG_INST_W (CALL);
  word iTarget = MemFetchWord ();
  MemPushWord (RegPC);
  RegPC = iTarget;
  iProcessorClock += 17;
}

// CALL conditional

#define InstCALLCon(N,E)                        \
void InstC##N ()                                \
{                                               \
  CPU_LOG_INST_W (C##N);                        \
  word iTarget = MemFetchWord ();               \
  if (E)                                        \
  {                                             \
    MemPushWord (RegPC);                        \
    RegPC = iTarget;                            \
    iProcessorClock += 6;                       \
  }                                             \
  iProcessorClock += 11;                        \
}

InstAllConditions (InstCALLCon)

#undef InstCALLCon

// RET

void InstRET ()
{
  CPU_LOG_INST_X (RET);
  RegPC = MemPopWord ();
  iProcessorClock += 10;
}

// RET conditional

#define InstRETCon(N,E)                         \
void InstR##N ()                                \
{                                               \
  CPU_LOG_INST_X (R##N);                        \
  if (E)                                        \
  {                                             \
    RegPC = MemPopWord ();                      \
    iProcessorClock += 6;                       \
  }                                             \
  iProcessorClock += 5;                         \
}

InstAllConditions (InstRETCon)

#undef InstRETCon

// RST

#define InstRSTVec(V)                           \
void InstRST##V ()                              \
{                                               \
  CPU_LOG_INST_X (RST V);                       \
  MemPushWord (RegPC);                          \
  RegPC = V * 8;                                \
  iProcessorClock += 11;                        \
}

InstRSTVec (0)
InstRSTVec (1)
InstRSTVec (2)
InstRSTVec (3)
InstRSTVec (4)
InstRSTVec (5)
InstRSTVec (6)
InstRSTVec (7)

#undef InstRSTVec

// PCHL

void InstPCHL ()
{
  CPU_LOG_INST_X (PCHL);
  RegPC = RegHL;
  iProcessorClock += 5;
}


//--------------------------------------------------------------------------
// Stack Operations
//
// PUSH, POP, XTHL, SPHL

// PUSH

#define InstPUSHSrc(D,N)                        \
void InstPUSH##N ()                             \
{                                               \
  CPU_LOG_INST_R (PUSH,N);                      \
  MemPushWord (Reg##D);                         \
  iProcessorClock += 11;                        \
}

InstAllRegisterPairs (InstPUSHSrc)

#undef InstPUSHSrc

// PUSH PSW

void InstPUSHPSW ()
{
  CPU_LOG_INST_R (PUSH,PSW);
  FlagsPack ();
  MemPushWord (RegAF);
  iProcessorClock += 11;
}

// POP

#define InstPOPDst(D,N)                         \
void InstPOP##N ()                              \
{                                               \
  CPU_LOG_INST_R (POP,N);                       \
  Reg##D = MemPopWord ();                       \
  iProcessorClock += 10;                        \
}

InstAllRegisterPairs (InstPOPDst)

#undef InstPOPDst

// POP PSW

void InstPOPPSW ()
{
  CPU_LOG_INST_R (POP,PSW);
  RegAF = MemPopWord ();
  FlagsUnpack ();
  iProcessorClock += 10;
}

// XTHL

void InstXTHL ()
{
  CPU_LOG_INST_X (XTHL);
  word iData = RegHL;
  RegHL = MemReadWord (RegSP);
  MemWriteWord (RegSP, iData);
  iProcessorClock += 18;
}

// SPHL

void InstSPHL ()
{
  CPU_LOG_INST_X (SPHL);
  RegSP = RegHL;
  iProcessorClock += 5;
}


//--------------------------------------------------------------------------
// Special Operations
//
// IN, OUT, EI, DI, HLT, NOP

// IN

/// Output latch of the port C of the system 8255, the games read it back to change single bits (speaker).
static byte iPortC = 0;

// PMD 85-2A AllRAM. PC4 of the system 8255 chooses what is at 0x8000-0x8FFF: 1 the monitor ROM (a write still goes to the
// RAM under it), 0 the RAM. PC4 has a pull-up, so with the upper port C an input (control word 0x8A, the monitor does it)
// the ROM stays; 0x82 makes it an output with the latches cleared (the RAM), the bit set/reset 0x09 and 0x08 and a write to
// port C change PC4.
static bool bAllRam = false;
static bool bRomMapped = true;
static bool bPchInput = true;
static byte abRomImage [4096];

static void AllRamMap (bool bRom)
{
  if (bRom == bRomMapped)
    return;
  if (bRom)
  {
    memcpy (abRamUnderRom, abMemoryData + 0x8000, 4096);
    memcpy (abMemoryData + 0x8000, abRomImage, 4096);
    for (int i = 0 ; i < 4096 ; i ++)
      abMemoryMask [0x8000 + i] = 2;
  }
  else
  {
    memcpy (abMemoryData + 0x8000, abRamUnderRom, 4096);
    for (int i = 0 ; i < 4096 ; i ++)
      abMemoryMask [0x8000 + i] = 1;
  }
  bRomMapped = bRom;
}

/// Called after every change of port C or of the mode of the system 8255.
static void AllRamUpdate ()
{
  if (bAllRam)
    AllRamMap (bPchInput || (iPortC & 0x10));
}

/// The monitor ROM (4 kB) is at 0x8000-0x8FFF of the memory, the RAM under it is empty and hidden.
void CPUAllRamEnable (const byte *pRom)
{
  memcpy (abRomImage, pRom, 4096);
  memset (abRamUnderRom, 0, sizeof (abRamUnderRom));
  memcpy (abMemoryData + 0x8000, abRomImage, 4096);
  for (int i = 0 ; i < 4096 ; i ++)
    abMemoryMask [0x8000 + i] = 2;
  bAllRam = true;
  bRomMapped = true;
  bPchInput = true;
}

void CPUAllRamDisable ()
{
  bAllRam = false;
  bRomMapped = true;
  bPchInput = true;
}

bool CPUAllRamActive ()
{
  return (bAllRam);
}

void CPUAllRamSave (byte *pFlags, byte *pHidden)
{
  *pFlags = (bRomMapped ? 1 : 0) | (bPchInput ? 2 : 0);
  // Mapped ROM: the RAM is in abRamUnderRom, otherwise it is the memory (the hidden copy is not up to date).
  memcpy (pHidden, bRomMapped ? abRamUnderRom : abMemoryData + 0x8000, 4096);
}

/// Sets the mapping of a saved state (the processor is stopped): the memory of 0x8000-0x8FFF is set by the caller
/// when the RAM is mapped (the loop of STLoad writes where the mask is 1).
void CPUAllRamRestore (byte iFlags, const byte *pHidden)
{
  bPchInput = (iFlags & 2) != 0;
  bool bRom = (iFlags & 1) != 0;
  bRomMapped = !bRom;                  // force the change of the view
  if (bRom)
  {
    memcpy (abRamUnderRom, pHidden, 4096);
    memcpy (abMemoryData + 0x8000, abRomImage, 4096);
    for (int i = 0 ; i < 4096 ; i ++)
      abMemoryMask [0x8000 + i] = 2;
    bRomMapped = true;
  }
  else
  {
    for (int i = 0 ; i < 4096 ; i ++)
      abMemoryMask [0x8000 + i] = 1;
    memcpy (abMemoryData + 0x8000, pHidden, 4096);
    bRomMapped = false;
  }
}

/// Test of the sound timing (PMD_BEEPSLOW=1): the delay loop of the model 1 monitor beep routine (DCR E, JNZ at 0x88B5)
/// takes 15 clocks per round, the model 2/2A one 87 (4x XTHL more), so notes of the games sound ~5.5 times longer there.
/// The loop is slowed down only when called by the beep routine (return address 0x88B1) with a note table in RAM
/// (a game), the other delays of the monitor and its key click (table in ROM) keep their timing, otherwise the
/// typing of the autoload commands would take forever.
#define PMD_BEEP_LOOP           0x88B5
#define PMD_BEEP_CALLER         0x88B1
#define PMD_BEEP_EXTRA          72
static word iBeepLoop = getenv ("PMD_BEEPSLOW") && atoi (getenv ("PMD_BEEPSLOW")) ? PMD_BEEP_LOOP : 0;

/** ROM module (BASIC-G, Pascal, ...): a 32 KB ROM read through an 8255 at the ports 0x88-0x8B (the address
 *  decoding is partial, 0x8C mask): the address is written to port B (low) and C (high), a read of port A returns
 *  the byte (0xFF when the address has the bit 15 set). Without a module the ports read 0xFF.
 */
#define ROM_MODULE_SIZE 0x8000
static byte abRomModule [ROM_MODULE_SIZE];
static bool bRomModule = false;
static byte iRomModuleB = 0, iRomModuleC = 0;

void CPURomModuleSet (const byte *pData, size_t iSize)
{
  memset (abRomModule, 0xFF, sizeof (abRomModule));
  iRomModuleB = iRomModuleC = 0;
  bRomModule = pData != NULL && iSize > 0;
  if (bRomModule)
    memcpy (abRomModule, pData, iSize > ROM_MODULE_SIZE ? ROM_MODULE_SIZE : iSize);
}

static byte RomModuleRead (int iReg)
{
  switch (iReg)
  {
    case 0: {
      word wAddr = iRomModuleB | (iRomModuleC << 8);
      return (wAddr & 0x8000) ? 0xFF : abRomModule [wAddr];
    }
    case 1: return (iRomModuleB);
    case 2: return (iRomModuleC);
    default: return (0xFF);
  }
}

static void RomModuleWrite (int iReg, byte iValue)
{
  switch (iReg)
  {
    case 1: iRomModuleB = iValue; break;
    case 2: iRomModuleC = iValue; break;
    case 3:
      if (iValue & 0x80)
        iRomModuleB = iRomModuleC = 0;          // mode set clears the latches
      else if (iValue & 1)
        iRomModuleC |= 1 << ((iValue >> 1) & 7);
      else
        iRomModuleC &= ~(1 << ((iValue >> 1) & 7));
      break;
  }
}

/** Debug: PMD_JOYFILE=<file> ANDs the first byte of the file into the joystick (a low bit is a pressed input), so a test script
 *  can drive the joystick without the keyboard. The file is read at most every 20 ms.
 */
static byte JoystickFileByte ()
{
  static const char *pFile = getenv ("PMD_JOYFILE");
  static byte iValue = 0xFF;
  static Uint32 iLast = 0;
  if (!pFile)
    return (0xFF);
  Uint32 iNow = SDL_GetTicks ();
  if (iNow - iLast >= 20)
  {
    iLast = iNow;
    FILE *pStream = fopen (pFile, "rb");
    if (pStream)
    {
      int iByte = fgetc (pStream);
      if (iByte != EOF)
        iValue = (byte) iByte;
      fclose (pStream);
    }
  }
  return (iValue);
}


void InstIN ()
{
  CPU_LOG_INST_B (IN);
  int iPort = MemFetchByte ();
  // The system 8255 is decoded only partially, 0x84-0x87 (and the others matching the mask) alias 0xF4-0xF7.
  if ((iPort & 0x8C) == 0x84) iPort |= 0xF0;
  if ((iPort & 0x8C) == 0x88)
  {
    RegA = bRomModule ? RomModuleRead (iPort & 3) : 0xFF;
    iProcessorClock += 10;
    return;
  }
  switch (iPort)
  {
    case 0x1E:  RegA = TAPReadData ();
                break;
    case 0x1F:  RegA = TAPReadStatus ();
                break;
    case 0xF4:  RegA = KBDReadColumn ();
                break;
    case 0xF5:  RegA = KBDReadRow ();
                break;
    case 0xF6:  RegA = iPortC;
                break;
    // Port B of the optional 8255 at 0x5C: ARKANOID (Lasota) waits in a loop until it reads zero, which is the
    // state of a machine without that module; the 0xFF of an unknown port would stop the game on its first sound.
    case 0x5D:  RegA = 0x00;
                break;
    // Joystick (8255 at 0x4C-0x4F, mode 0x92: the ports A and B are inputs, Jetpac "Joystick B" reads 0x4D): low bits are
    // pressed inputs. Both ports show the same joystick: the cursor keys with Space, and the gamepad with the button A.
    case 0x4C:
    case 0x4D:  RegA = KBDJoystickByte () & GPDJoystickByte () & JoystickFileByte ();
                {
                  static const bool bJoyTrace = getenv ("PMD_IOTRACE") != NULL;
                  static std::set <int> oSeenJoy;
                  if (bJoyTrace && oSeenJoy.insert (iPort).second)
                    fprintf (stderr, "IN joystick port %02x pc %04x\n", iPort, (int) RegPC);
                }
                break;
    default:    RegA = 0xFF;
                {
                  static const bool bIoTrace = getenv ("PMD_IOTRACE") != NULL;
                  static std::set <int> oSeen;
                  if (bIoTrace && oSeen.insert (iPort).second)
                    fprintf (stderr, "IN unknown port %02x pc %04x\n", iPort, (int) RegPC);
                }
                break;
  }
  iProcessorClock += 10;
}

// OUT

void InstOUT ()
{
  CPU_LOG_INST_B (OUT);
  int iPort = MemFetchByte ();
  // The system 8255 is decoded only partially, 0x84-0x87 (and the others matching the mask) alias 0xF4-0xF7.
  if ((iPort & 0x8C) == 0x84) iPort |= 0xF0;
  if ((iPort & 0x8C) == 0x88)
  {
    if (bRomModule)
      RomModuleWrite (iPort & 3, RegA);
    iProcessorClock += 10;
    return;
  }
  {
    static const bool bIoTrace = getenv ("PMD_IOTRACE") != NULL;
    static std::set <int> oSeenOut;
    if (bIoTrace && iPort != 0x1E && iPort != 0xF4 && iPort != 0xF6 && iPort != 0xF7 && oSeenOut.insert (iPort).second)
      fprintf (stderr, "OUT port %02x value %02x pc %04x\n", iPort, RegA, (int) RegPC);
  }
  switch (iPort)
  {
    case 0x1E:  TAPWriteData (RegA);
                break;
    case 0xF4:  KBDWriteColumn (RegA);
                break;
    case 0xF6:  iPortC = RegA;
                SNDWriteSpeaker (iPortC);
                AllRamUpdate ();
                break;
    case 0xF7:  if (RegA & 0x80)
                {
                  // Mode set clears the output latches.
                  iPortC = 0;
                  bPchInput = (RegA & 0x08) != 0;
                }
                else
                {
                  // Bit set/reset of one port C bit.
                  byte iMask = 1 << ((RegA >> 1) & 7);
                  iPortC = (RegA & 1) ? (iPortC | iMask) : (iPortC & ~iMask);
                }
                SNDWriteSpeaker (iPortC);
                AllRamUpdate ();
                break;
  }
  iProcessorClock += 10;
}

// EI

void InstEI ()
{
  CPU_LOG_INST_X (EI);
  // No interrupts were used in this computer.
  iProcessorClock += 4;
}

// DI

void InstDI ()
{
  CPU_LOG_INST_X (DI);
  // No interrupts were used in this computer.
  iProcessorClock += 4;
}

// HLT

void InstHLT ()
{
  CPU_LOG_INST_X (HLT);
  // No interrupts were used in this computer and
  // therefore the instruction should halt forever.
  -- RegPC;
  iProcessorClock += 7;
}

// NOP

void InstNOP ()
{
  CPU_LOG_INST_X (NOP);
  // Guess what :-)
  iProcessorClock += 4;
}


//--------------------------------------------------------------------------
// Execution

/// A table of functions implementing the individual instructions.
static void (*apInstructionTable [256]) () __attribute__ ((aligned (SAFE_ALIGNMENT))) =
{
        // 00h
        InstNOP,        InstLXIB,       InstSTAXB,      InstINXB,
        InstINRB,       InstDCRB,       InstMVIB,       InstRLC,
        InstNOP,        InstDADB,       InstLDAXB,      InstDCXB,
        InstINRC,       InstDCRC,       InstMVIC,       InstRRC,
        // 10h
        InstNOP,        InstLXID,       InstSTAXD,      InstINXD,
        InstINRD,       InstDCRD,       InstMVID,       InstRAL,
        InstNOP,        InstDADD,       InstLDAXD,      InstDCXD,
        InstINRE,       InstDCRE,       InstMVIE,       InstRAR,
        // 20h
        InstNOP,        InstLXIH,       InstSHLD,       InstINXH,
        InstINRH,       InstDCRH,       InstMVIH,       InstDAA,
        InstNOP,        InstDADH,       InstLHLD,       InstDCXH,
        InstINRL,       InstDCRL,       InstMVIL,       InstCMA,
        // 30h
        InstNOP,        InstLXISP,      InstSTA,        InstINXSP,
        InstINRM,       InstDCRM,       InstMVIM,       InstSTC,
        InstNOP,        InstDADSP,      InstLDA,        InstDCXSP,
        InstINRA,       InstDCRA,       InstMVIA,       InstCMC,
        // 40h
        InstNOP,        InstMOVBC,      InstMOVBD,      InstMOVBE,
        InstMOVBH,      InstMOVBL,      InstMOVBM,      InstMOVBA,
        InstMOVCB,      InstNOP,        InstMOVCD,      InstMOVCE,
        InstMOVCH,      InstMOVCL,      InstMOVCM,      InstMOVCA,
        // 50h
        InstMOVDB,      InstMOVDC,      InstNOP,        InstMOVDE,
        InstMOVDH,      InstMOVDL,      InstMOVDM,      InstMOVDA,
        InstMOVEB,      InstMOVEC,      InstMOVED,      InstNOP,
        InstMOVEH,      InstMOVEL,      InstMOVEM,      InstMOVEA,
        // 60h
        InstMOVHB,      InstMOVHC,      InstMOVHD,      InstMOVHE,
        InstNOP,        InstMOVHL,      InstMOVHM,      InstMOVHA,
        InstMOVLB,      InstMOVLC,      InstMOVLD,      InstMOVLE,
        InstMOVLH,      InstNOP,        InstMOVLM,      InstMOVLA,
        // 70h
        InstMOVMB,      InstMOVMC,      InstMOVMD,      InstMOVME,
        InstMOVMH,      InstMOVML,      InstHLT,        InstMOVMA,
        InstMOVAB,      InstMOVAC,      InstMOVAD,      InstMOVAE,
        InstMOVAH,      InstMOVAL,      InstMOVAM,      InstNOP,
        // 80h
        InstADDB,       InstADDC,       InstADDD,       InstADDE,
        InstADDH,       InstADDL,       InstADDM,       InstADDA,
        InstADCB,       InstADCC,       InstADCD,       InstADCE,
        InstADCH,       InstADCL,       InstADCM,       InstADCA,
        // 90h
        InstSUBB,       InstSUBC,       InstSUBD,       InstSUBE,
        InstSUBH,       InstSUBL,       InstSUBM,       InstSUBA,
        InstSBBB,       InstSBBC,       InstSBBD,       InstSBBE,
        InstSBBH,       InstSBBL,       InstSBBM,       InstSBBA,
        // 0A0h
        InstANAB,       InstANAC,       InstANAD,       InstANAE,
        InstANAH,       InstANAL,       InstANAM,       InstANAA,
        InstXRAB,       InstXRAC,       InstXRAD,       InstXRAE,
        InstXRAH,       InstXRAL,       InstXRAM,       InstXRAA,
        // 0B0h
        InstORAB,       InstORAC,       InstORAD,       InstORAE,
        InstORAH,       InstORAL,       InstORAM,       InstORAA,
        InstCMPB,       InstCMPC,       InstCMPD,       InstCMPE,
        InstCMPH,       InstCMPL,       InstCMPM,       InstCMPA,
        // 0C0h
        InstRNZ,        InstPOPB,       InstJNZ,        InstJMP,
        InstCNZ,        InstPUSHB,      InstADI,        InstRST0,
        InstRZ,         InstRET,        InstJZ,         InstNOP,
        InstCZ,         InstCALL,       InstACI,        InstRST1,
        // 0D0h
        InstRNC,        InstPOPD,       InstJNC,        InstOUT,
        InstCNC,        InstPUSHD,      InstSUI,        InstRST2,
        InstRC,         InstNOP,        InstJC,         InstIN,
        InstCC,         InstNOP,        InstSBI,        InstRST3,
        // 0E0h
        InstRPO,        InstPOPH,       InstJPO,        InstXTHL,
        InstCPO,        InstPUSHH,      InstANI,        InstRST4,
        InstRPE,        InstPCHL,       InstJPE,        InstXCHG,
        InstCPE,        InstNOP,        InstXRI,        InstRST5,
        // 0F0h
        InstRP,         InstPOPPSW,     InstJP,         InstNOP,
        InstCP,         InstPUSHPSW,    InstORI,        InstRST6,
        InstRM,         InstSPHL,       InstJM,         InstNOP,
        InstCM,         InstNOP,        InstCPI,        InstRST7
};


/// Resets the processor.
void CPUReset ()
{
  // The computer used an invertor on the address bus to
  // fetch the very first instruction after reset from
  // 8000h. Since this instruction was a jump, this
  // was effectively the same as initializing the
  // program counter at 8000h.

  RegPC = 0x8000;

  // No other initialization is done at reset.
}


//--------------------------------------------------------------------------
// Slowed-down processor
//
// The video processor of the PMD 85 steals memory cycles from the processor, a machine cycle that touches the
// memory is longer than on a plain 8080 (CALL takes 20 clocks instead of 17, LHLD 19 instead of 16, ...) and the
// second machine cycle waits one more clock when the clock count is odd. The durations are those of GPMD85Emulator
// (R. Borik, ChipCpu8080P), the games' delay loops and sounds depend on them. PMD_SLOWCPU=0 turns it off.

/// Duration of the instructions in clocks, a taken conditional call or return is handled separately.
static const byte abSlowDuration [256] =
{
   4, 11,  8,  5,  5,  5,  7,  4,  4, 10,  7,  5,  5,  5,  7,  4,
   4, 11,  8,  5,  5,  5,  7,  4,  4, 10,  7,  5,  5,  5,  7,  4,
   4, 11, 18,  5,  5,  5,  7,  4,  4, 10, 19,  5,  5,  5,  7,  4,
   4, 11, 14,  5, 10, 10, 10,  4,  4, 10, 15,  5,  5,  5,  7,  4,
   5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  7,  5,
   5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  7,  5,
   5,  5,  5,  5,  5,  5,  7,  5,  5,  5,  5,  5,  5,  5,  7,  5,
   8,  8,  8,  8,  8,  8,  5,  8,  5,  5,  5,  5,  5,  5,  7,  5,
   4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
   4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
   4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
   4,  4,  4,  4,  4,  4,  7,  4,  4,  4,  4,  4,  4,  4,  7,  4,
   5, 11, 11, 11, 13, 12,  7, 12,  5, 11, 11, 11, 13, 20,  7, 12,
   5, 11, 11, 10, 13, 12,  7, 12,  5, 11, 11, 11, 13, 20,  7, 12,
   5, 11, 11, 20, 13, 12,  7, 12,  5,  5, 11,  4, 13, 20,  7, 12,
   5, 11, 11,  4, 13, 12,  7, 12,  5,  5, 11,  4, 13, 20,  7, 12
};

/// Slowed-down processor is on.
static bool bSlowProcessor = true;

/** Duration of the instruction in clocks when the processor is slowed down by the video processor.
 *
 *  @arg bCode The opcode.
 *  @arg iPlain Clocks the plain 8080 needs, tells whether a conditional call or return was taken.
 *  @arg iStart Clock count at the start of the instruction.
 */
static inline int CPUSlowDuration (byte bCode, int iPlain, int iStart)
{
  int iDuration = abSlowDuration [bCode];
  if ((bCode & 0xC7) == 0xC0 && iPlain > 5)         // Rcc taken
    iDuration = 13;
  else if ((bCode & 0xC7) == 0xC4 && iPlain > 11)   // Ccc taken
    iDuration = 20;
  return (iDuration + (iStart & 1));
}


/// Simulated clock when TIMAdvance has to synchronize next (sim_timing.c), checked inline to save a call per instruction.
extern int iTimingDue;

/// Executes the processor instructions.
void *CPUThread (void *pArgs)
{
  const char *pSlow = getenv ("PMD_SLOWCPU");
  bSlowProcessor = !pSlow || atoi (pSlow) != 0;

  // Synchronize the simulated clock and the actual time.
  SNDSynchronize ();
  TIMSynchronize ();

  while (__builtin_expect (!bTerminate, true))
  {
    //while (__builtin_expect (!bSuspend, true))
    //{

#ifdef DEBUG_CPU_TRACE_REGISTERS

      // Display registers when register tracing is enabled.
      DEBUG_LOG_PARTIAL ( "PC:" << CPU_LOG_FORMAT_WORD (RegPC) <<
                         " BC:" << CPU_LOG_FORMAT_WORD (RegBC) << ":" << CPU_LOG_FORMAT_BYTE (MemReadByte (RegBC)) <<
                         " DE:" << CPU_LOG_FORMAT_WORD (RegDE) << ":" << CPU_LOG_FORMAT_BYTE (MemReadByte (RegDE)) <<
                         " HL:" << CPU_LOG_FORMAT_WORD (RegHL) << ":" << CPU_LOG_FORMAT_BYTE (MemReadByte (RegHL)) <<
                         " SP:" << CPU_LOG_FORMAT_WORD (RegSP) <<
                          " A:" << CPU_LOG_FORMAT_BYTE (RegA) <<
                           " " <<
                         (FlagS ? "S" : "-") <<
                         (FlagZ ? "Z" : "-") <<
                         (FlagH ? "H" : "-") <<
                         (FlagP ? "P" : "-") <<
                         (FlagC ? "C" : "-"));

#ifdef DEBUG_CPU_TRACE_INSTRUCTIONS

      // Display separator if instruction tracing is enabled.
      DEBUG_LOG_PARTIAL ("  ");

#endif
#endif

      // How to get your branch predictor to hate you ...
      int iStart = iProcessorClock;
      byte bCode = MemFetchByte ();
      apInstructionTable [bCode] ();
      if (__builtin_expect (bSlowProcessor, true))
        iProcessorClock = iStart + CPUSlowDuration (bCode, iProcessorClock - iStart, iStart);
      if (__builtin_expect (iBeepLoop != 0, false) && RegPC == iBeepLoop
          && (MemReadByte (RegSP) | (MemReadByte (RegSP + 1) << 8)) == PMD_BEEP_CALLER
          && RegHL < 0x8000)
        iProcessorClock += PMD_BEEP_EXTRA;

#if defined(DEBUG_CPU_TRACE_INSTRUCTIONS) | defined(DEBUG_CPU_TRACE_REGISTERS)

      // Display newline if any tracing is enabled.
      DEBUG_LOG_NEWLINE;

#endif

      // Advance real time to match simulated time.
      if (__builtin_expect ((int) (iProcessorClock - iTimingDue) >= 0, false))
      {
        TIMAdvance ();
        if (__builtin_expect (bPauseRequest, false))
        {
          std::atomic_thread_fence (std::memory_order_seq_cst);
          bPaused = true;
          while (bPauseRequest && !bTerminate)
            usleep (1000);
          bPaused = false;
          std::atomic_thread_fence (std::memory_order_seq_cst);
          // The clock and the state may have changed, start the real time and the sound again from here.
          SNDSynchronize ();
          TIMSynchronize ();
        }
      }
    //}

    // We were asked to suspend the simulation temporarily.
    // This is done to access complex shared data quickly.

    //CheckZero (sem_post (&sSignalSuspend));
    //CheckZero (sem_wait (&sSignalResume));
  }

  return (NULL);
}


//--------------------------------------------------------------------------
// Thread management

/// Suspend the simulation.
void CPUSuspend ()
{
  // Indicate that the instruction cycle should stop.
  bSuspend = true;
  // Wait on the suspension semaphore.
  // Note that the semaphore also enforces memory ordering.
  CheckZero (sem_wait (&sSignalSuspend));
  printf("CPU supend wait..\n");
}


/// Resume the simulation.
void CPUResume ()
{
  // Indicate that the instruction cycle should start.
  bSuspend = false;
  // Post on the resumption semaphore.
  // Note that the semaphore also enforces memory ordering.
  CheckZero (sem_post (&sSignalResume));
  printf("CPU resume wait..\n");
}


/// Stops the processor thread between two instructions, false when it did not stop in 2 s (it is then released again).
bool CPUPause ()
{
  bPauseRequest = true;
  for (int i = 0 ; i < 2000 && !bPaused ; i ++)
    usleep (1000);
  std::atomic_thread_fence (std::memory_order_seq_cst);
  if (!bPaused)
    bPauseRequest = false;
  return (bPaused);
}


/// Lets the processor thread go on.
void CPUUnpause ()
{
  std::atomic_thread_fence (std::memory_order_seq_cst);
  bPauseRequest = false;
}


/// The state of the processor, only while the thread is paused.
void CPUGetState (tCPUState *pState)
{
  pState->wPC = RegPC;
  pState->wSP = RegSP;
  pState->bA = RegA;
  pState->bB = RegB;
  pState->bC = RegC;
  pState->bD = RegD;
  pState->bE = RegE;
  pState->bH = RegH;
  pState->bL = RegL;
  pState->bFlags = FlagS | (FlagZ << 1) | (FlagH << 2) | (FlagP << 3) | (FlagC << 4);
  pState->bPortC = iPortC;
  pState->iClock = iProcessorClock;
}


/// Sets the state of the processor, only while the thread is paused.
void CPUSetState (const tCPUState *pState)
{
  RegPC = pState->wPC;
  RegSP = pState->wSP;
  RegA = pState->bA;
  RegB = pState->bB;
  RegC = pState->bC;
  RegD = pState->bD;
  RegE = pState->bE;
  RegH = pState->bH;
  RegL = pState->bL;
  FlagS = pState->bFlags & 1;
  FlagZ = pState->bFlags & 2;
  FlagH = pState->bFlags & 4;
  FlagP = pState->bFlags & 8;
  FlagC = pState->bFlags & 16;
  iPortC = pState->bPortC;
  iProcessorClock = pState->iClock;
}


/// Starts the processor thread.
void CPUStartThread ()
{
  // SDL threads are not used since it is not
  // possible to send signals through SDL.
  CheckZero (pthread_create (&sThread, NULL, CPUThread, NULL));
  printf("CPU processor thread start..\n");
}


/// Terminates the processor thread.
void CPUTerminateThread ()
{
  bTerminate = true;

  // The termination flag is only checked on transition from suspend to resume.
  // Synchronization inside suspend and resume should enforce sufficient ordering.
  //CPUSuspend ();
  //CPUResume ();

  // Just wait for the thread to terminate.
  CheckZero (pthread_join (sThread, NULL));
  printf("CPU processor thread terminating..\n");
}


//--------------------------------------------------------------------------
// Initialization and shutdown

void CPUInitialize ()
{
  // Initialize the semaphores for suspend and resume handling.
  //CheckZero (sem_init (&sSignalSuspend, false, 0));
  //printf("Suspend semaphore init..\n");
  //CheckZero (sem_init (&sSignalResume, false, 0));
  //printf("Resume semaphore init..\n");

  // Calculate the contents of the parity lookup array

  for (int iValue = 0 ; iValue < 256 ; iValue ++)
  {
    bool bParity = true;
    for (int iMask = 1 ; iMask < 256 ; iMask <<= 1)
    {
      if (iValue & iMask)
      {
        bParity = !bParity;
      }
    }
    abParity [iValue] = bParity;
  }

  // Reset termination token
  bTerminate = false;

  // Reset the processor

  CPUReset ();
  printf("CPU Reseting..\n");
}


void CPUShutdown ()
{
  // Destroy the semaphores for suspend and resume handling.
  //CheckZero (sem_destroy (&sSignalSuspend));
  //printf("Suspend semaphore shutdown..\n");
  //CheckZero (sem_destroy (&sSignalResume));
  //printf("Resume semaphore shutdown..\n");
}


//--------------------------------------------------------------------------

