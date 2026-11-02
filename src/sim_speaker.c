/*

SIM_SPEAKER.C

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

 - Removed options.
 - Larger buffers, thread safe sound generation, bounded latency.

*/


#include "sim_common.h"


//--------------------------------------------------------------------------
// Command Line Options

/// Sampling rate. What sampling rate to use for the audio output.
static int iArgSamplingRate = 48000;

/// Software buffer size. What buffer size to use for software storing the samples, expressed in milliseconds.
static int iArgSoftwareBuffer = 200;

/// Hardware buffer size. What buffer size to use for hardware playing the samples, expressed in milliseconds.
static int iArgHardwareBuffer = 50;

/// Lead of the generated samples before the playback, expressed in milliseconds.
static int iArgLead = 50;


//--------------------------------------------------------------------------
// Data

/// Singleton audio specification.
static SDL_AudioSpec sAudioSpec;

/// Singleton cyclic audio buffer.
static byte *pAudioBuffer;
/// Size of the cyclic audio buffer.
static int iAudioBufferSize;
/// Head of the cyclic audio buffer.
static byte *volatile pAudioBufferHead;
/// Tail of the cyclic audio buffer.
static byte *volatile pAudioBufferTail;
/// Mutex protecting the cyclic audio buffer.
static pthread_mutex_t sAudioBufferLock;

/// Last generated value of simulated clock
static int iLastClock;
/// Last written value of simulated speaker.
static byte iLastSpeaker;
/// Position inside the sample being generated and the part of it the speaker was high (units of 1/PMD_CLOCK sample).
static uint32_t iSamplePhase = 0;
static uint32_t iSampleLevel = 0;
/// Last sample played, repeated on buffer underrun.
static byte bLastSample;
/// Maximum number of buffered samples, excess is dropped to bound latency.
static int iMaxBuffered;
/// Samples kept buffered ahead of the playback, the emulation produces them in real time and without a lead
/// every delay of its thread (display, scheduler) is an underrun.
static int iLeadSamples;
/// Waiting for the lead to build up, the callback plays the last sample until then.
static bool bPriming = true;

/// DC blocker state, the speaker is a 0/255 square wave and the constant level makes the output whistle.
static float fDcInput;
static float fDcOutput;
/// DC blocker pole, ~8 Hz cutoff at 48 kHz.
#define PMD_DC_BLOCK            0.999f
/// Filter output below this level is silence, it is flushed to zero.
#define PMD_DC_FLOOR            0.001f
/// Output amplitude, 1.0 is full range.
#define PMD_VOLUME              0.5f

/// Statistics, printed on shutdown when PMD_SNDSTATS is set.
static int iStatCallbacks;
static int iStatUnderruns;
static long iStatUnderrunSamples;
static long iStatDroppedSamples;
static long iStatOverwrittenSamples;
/// Statistics level, 2 logs the events with their time (at most one line per 100 ms).
static int iStatLevel;
static Uint32 iLogLast;
static int iLogSuppressed;
/// PMD_SNDLEVEL=1 logs the peak output level, the underruns and the buffered samples every 10 s (journal), to tell
/// a silent emulator from a silent sink when the sound drops out.
static int iLevelLog;
static int iLevelPeak;
static Uint32 iLevelLast;
static int iLevelUnderruns;
/// Changes of the speaker register and calls of the generator since the last log line (a silent game or a stopped emulation).
static unsigned iLevelEdges, iLevelCalls;

/// Speaker frequency mask.
#define PMD_SPEAKER_FREQUENCY   (3 << 0)
/// Speaker 1 kHz tone enable mask.
#define PMD_SPEAKER_1KHZ        (1 << 0)
/// Speaker 4 kHz tone enable mask.
#define PMD_SPEAKER_4KHZ        (1 << 1)
/// Clock bits toggling the tones, half period is 1024 (1 kHz) and 256 (4 kHz) clocks at 2.048 MHz.
#define PMD_TONE_1KHZ_HALF      (1 << 10)
#define PMD_TONE_4KHZ_HALF      (1 << 8)
/// Speaker enable mask.
#define PMD_SPEAKER_ENABLE      (1 << 2)


//--------------------------------------------------------------------------
// Buffer operations

/** Logs a buffer event when PMD_SNDSTATS=2, events within 100 ms of the last line are only counted.
 */
static void SNDLogEvent (const char *pEvent, long iSamples)
{
  if (iStatLevel < 2) return;
  Uint32 iNow = SDL_GetTicks ();
  if (iLogLast != 0 && iNow - iLogLast < 100) { iLogSuppressed ++; return; }
  fprintf (stderr, "SND %u ms: %s %ld samples (+%d events within 100 ms)\n", iNow, pEvent, iSamples, iLogSuppressed);
  iLogLast = iNow;
  iLogSuppressed = 0;
}


/// Increments pointer in the cyclic audio buffer.
byte *SNDIncrementInBuffer (byte *pPosition)
{
  byte *pResult = pPosition + 1;
  if (pResult >= pAudioBuffer + iAudioBufferSize)
  {
    pResult -= iAudioBufferSize;
  }
  return (pResult);
}


/** Flushes the cyclic audio buffer.
 *
 *  Must be called with the buffer lock held.
 */
void SNDFlushBuffer ()
{
  pAudioBufferHead = pAudioBufferTail;
}


/** Drops oldest samples above the latency limit.
 *
 *  Must be called with the buffer lock held.
 */
void SNDTrimBuffer ()
{
  int iBuffered = pAudioBufferTail - pAudioBufferHead;
  if (iBuffered < 0) iBuffered += iAudioBufferSize;
  int iExcess = iBuffered - iMaxBuffered;
  if (iExcess > 0)
  {
    iStatDroppedSamples += iExcess;
    SNDLogEvent ("dropped (buffer over latency limit)", iExcess);
    pAudioBufferHead += iExcess;
    if (pAudioBufferHead >= pAudioBuffer + iAudioBufferSize)
    {
      pAudioBufferHead -= iAudioBufferSize;
    }
  }
}


/** Reads pattern from the cyclic audio buffer.
 *
 *  Must be called with the buffer lock held.
 *
 *  @arg pBuffer The buffer to fill.
 *  @arg iCount  The number of samples.
 */
void SNDReadBuffer (byte *pBuffer, int iCount)
{
  int iMissing = 0;
  for (int i = 0 ; i < iCount ; i ++)
  {
    if (pAudioBufferHead != pAudioBufferTail)
    {
      bLastSample = *pAudioBufferHead;
      pAudioBufferHead = SNDIncrementInBuffer (pAudioBufferHead);
    }
    else
    {
      iMissing ++;
    }
    *pBuffer = bLastSample;
    pBuffer ++;
  }
  iStatCallbacks ++;
  if (iMissing > 0)
  {
    iStatUnderruns ++;
    iStatUnderrunSamples += iMissing;
    SNDLogEvent ("underrun", iMissing);
  }
}


/** Writes pattern to the cyclic audio buffer.
 *
 *  Must be called with the buffer lock held.
 *
 *  @arg iSample The value to put.
 *  @arg iCount  The number of samples.
 */
void SNDWriteBuffer (byte bSample, int iCount)
{
  for (int i = 0 ; i < iCount ; i ++)
  {
    (*pAudioBufferTail) = bSample;
    pAudioBufferTail = SNDIncrementInBuffer (pAudioBufferTail);
    if (pAudioBufferTail == pAudioBufferHead)
    {
      pAudioBufferHead = SNDIncrementInBuffer (pAudioBufferHead);
      iStatOverwrittenSamples ++;
      SNDLogEvent ("buffer full, oldest sample overwritten", 1);
    }
  }
}


//--------------------------------------------------------------------------
// Sound generation

/** Synchronizes simulated clock with real time.
 */
void SNDSynchronize ()
{
  CheckZero (pthread_mutex_lock (&sAudioBufferLock));
  // Synchronize the time.
  iLastClock = iProcessorClock;
  // Synchronize the buffer.
  SNDFlushBuffer ();
  bPriming = iLeadSamples > 0;
  CheckZero (pthread_mutex_unlock (&sAudioBufferLock));
}


/** Generate sound.
 *
 *  Calculates position and generates sound.
 *  Must be called with the buffer lock held.
 *
 *  @arg iSpeaker Speaker register value.
 */
void SNDGenerate (byte iSpeaker)
{
  // The speaker is a 1 bit signal toggled by the program much faster than the sampling rate (the sound of
  // INVADERS is made of pulses shorter than one sample), so every sample is the average level over its span
  // instead of the level at one instant (point sampling loses short pulses and aliases the rest).
  // Time is counted in units of 1/PMD_CLOCK sample, a sample is PMD_CLOCK units long.
  // No division in the loop (the Raspberry Pi 1 has no divide instruction, a 64 bit division is a library call).
  int iDeltaInClocks = iProcessorClock - iLastClock;
  if (iDeltaInClocks <= 0) return;
  if (iLevelLog)
  {
    static byte iPrevSpeaker;
    iLevelCalls ++;
    if (iSpeaker != iPrevSpeaker)
      iLevelEdges ++;
    iPrevSpeaker = iSpeaker;
  }
  uint64_t iUnits = (uint64_t) iDeltaInClocks * iArgSamplingRate;

  // Writing more than the buffer holds is pointless.
  uint64_t iMaxUnits = (uint64_t) iAudioBufferSize * PMD_CLOCK;
  if (iUnits > iMaxUnits) iUnits = iMaxUnits;

  bool bDirect = (iSpeaker & PMD_SPEAKER_ENABLE) != 0;
  bool bTones = !bDirect && (iSpeaker & PMD_SPEAKER_FREQUENCY) != 0;
  uint64_t iDone = 0;
  while (iDone < iUnits)
  {
    uint32_t iStep = PMD_CLOCK - iSamplePhase;
    uint64_t iLeft = iUnits - iDone;
    if (iStep > iLeft) iStep = (uint32_t) iLeft;
    // The direct bit is the level itself, the other two bits gate free running 4 kHz and 1 kHz square waves
    // derived from the clock (those are sampled at the start of the step, they are slow).
    bool bOut = bDirect;
    if (bTones)
    {
      unsigned int iClock = iLastClock + (unsigned int) (iDone / iArgSamplingRate);
      bOut = ((iSpeaker & PMD_SPEAKER_4KHZ) && (iClock & PMD_TONE_4KHZ_HALF))
          || ((iSpeaker & PMD_SPEAKER_1KHZ) && (iClock & PMD_TONE_1KHZ_HALF));
    }
    if (bOut) iSampleLevel += iStep;
    iSamplePhase += iStep;
    iDone += iStep;
    if (iSamplePhase >= PMD_CLOCK)
    {
      // The level is at most PMD_CLOCK, 32 bits are enough and the division by a constant is a multiplication.
      SNDWriteBuffer ((byte) ((iSampleLevel * 255u + PMD_CLOCK / 2) / PMD_CLOCK), 1);
      iSamplePhase = 0;
      iSampleLevel = 0;
    }
  }

  // The partial sample stays in the accumulator, so all the elapsed time is consumed.
  iLastClock = iProcessorClock;
}


//--------------------------------------------------------------------------
// Port operations

/** Write to speaker register.
 *
 *  Fills the cyclic audio buffer by data corresponding
 *  to the simulated speaker. Only the binary speaker
 *  output is simulated for now.
 *
 *  @arg iData The written value.
 */
void SNDWriteSpeaker (byte iData)
{
  CheckZero (pthread_mutex_lock (&sAudioBufferLock));
  // Generate whatever sound was heard until now.
  SNDGenerate (iLastSpeaker);
  // Set what sound is heard from now on.
  iLastSpeaker = iData;
  CheckZero (pthread_mutex_unlock (&sAudioBufferLock));
}


//--------------------------------------------------------------------------
// Callback handlers

/** Handler for filling the sound buffer.
 *
 *  @arg pParameters Callback parameters.
 *  @arg pBuffer Sound buffer address.
 *  @arg iLength Sound buffer length.
 */
void SNDFillBufferCallback (void *pParameters, byte *pBuffer, int iLength)
{
  // The callback runs in the audio thread, concurrently
  // with the processor thread writing the speaker.
  CheckZero (pthread_mutex_lock (&sAudioBufferLock));
  // Generate whatever sound was heard until now.
  SNDGenerate (iLastSpeaker);
  // Keep latency bounded when emulation runs ahead of audio clock.
  SNDTrimBuffer ();
  // Fill the buffer with the generated sound, after a start or an underrun first wait for the lead.
  int iBuffered = pAudioBufferTail - pAudioBufferHead;
  if (iBuffered < 0) iBuffered += iAudioBufferSize;
  if (bPriming && iBuffered >= iLeadSamples) bPriming = false;
  if (bPriming)
  {
    memset (pBuffer, bLastSample, iLength);
    iStatCallbacks ++;
  }
  else
  {
    int iUnderruns = iStatUnderruns;
    SNDReadBuffer (pBuffer, iLength);
    if (iStatUnderruns != iUnderruns) bPriming = true;
  }
  CheckZero (pthread_mutex_unlock (&sAudioBufferLock));
  // Remove the constant level and center the signal to the middle of the unsigned range.
  for (int i = 0 ; i < iLength ; i ++)
  {
    float fInput = (float) pBuffer [i] - 128.0f;
    fDcOutput = fInput - fDcInput + PMD_DC_BLOCK * fDcOutput;
    // Silence decays the filter state to denormals, which the VFP of the Raspberry Pi 1 handles by a
    // trap to the kernel on every sample (a quarter of the core).
    if (fDcOutput > -PMD_DC_FLOOR && fDcOutput < PMD_DC_FLOOR) fDcOutput = 0.0f;
    fDcInput = fInput;
    pBuffer [i] = (byte) (128.0f + PMD_VOLUME * fDcOutput);
    if (iLevelLog)
    {
      int iLevel = abs ((int) pBuffer [i] - 128);
      if (iLevel > iLevelPeak) iLevelPeak = iLevel;
    }
  }
  if (iLevelLog)
  {
    Uint32 iNow = SDL_GetTicks ();
    if (iNow - iLevelLast >= 10000)
    {
      fprintf (stderr, "SND level: peak %d of 127, %d underruns in the last %u ms, priming %d, speaker changes %u, generator calls %u\n",
               iLevelPeak, iStatUnderruns - iLevelUnderruns, iNow - iLevelLast, (int) bPriming, iLevelEdges, iLevelCalls);
      iLevelEdges = 0;
      iLevelCalls = 0;
      iLevelPeak = 0;
      iLevelLast = iNow;
      iLevelUnderruns = iStatUnderruns;
    }
  }
}


//--------------------------------------------------------------------------
// Initialization and shutdown


void SNDInitialize ()
{
  const char *pStats = getenv ("PMD_SNDSTATS");
  iStatLevel = pStats ? atoi (pStats) : 0;
  const char *pLevel = getenv ("PMD_SNDLEVEL");
  iLevelLog = pLevel && atoi (pLevel) > 0;
  iLevelLast = SDL_GetTicks ();
  // Tuning, PMD_SNDRATE in Hz and PMD_SNDBUF (hardware buffer) in milliseconds.
  const char *pRate = getenv ("PMD_SNDRATE");
  if (pRate && atoi (pRate) > 0)
    iArgSamplingRate = atoi (pRate);
  const char *pLead = getenv ("PMD_SNDLEAD");
  if (pLead && atoi (pLead) >= 0)
    iArgLead = atoi (pLead);
  const char *pBuffer = getenv ("PMD_SNDBUF");
  if (pBuffer && atoi (pBuffer) > 0)
    iArgHardwareBuffer = atoi (pBuffer);

  // Initialize the cyclic audio buffer ...
  iAudioBufferSize = (iArgSoftwareBuffer * iArgSamplingRate) / 1000;
  pAudioBuffer = new byte [iAudioBufferSize];
  CheckZero (pthread_mutex_init (&sAudioBufferLock, NULL));
  CheckZero (pthread_mutex_lock (&sAudioBufferLock));
  pAudioBufferHead = pAudioBuffer;
  pAudioBufferTail = pAudioBuffer;
  iLastClock = iProcessorClock;
  iLastSpeaker = 0;
  bLastSample = 0;
  CheckZero (pthread_mutex_unlock (&sAudioBufferLock));

  // Prepare audio parameters ...
  sAudioSpec.freq = iArgSamplingRate;
  sAudioSpec.format = AUDIO_U8;
  sAudioSpec.samples = POT ((iArgHardwareBuffer * iArgSamplingRate) / 1000);
  sAudioSpec.channels = 1;
  sAudioSpec.userdata = NULL;
  sAudioSpec.callback = SNDFillBufferCallback;

  // Open the audio device ...
  SDL_CheckZero (SDL_OpenAudio (&sAudioSpec, NULL));

  // Check if audio parameters were granted ...
  assert (sAudioSpec.freq == iArgSamplingRate);
  assert (sAudioSpec.format == AUDIO_U8);

  // Allow two hardware buffers of samples to be queued ...
  iLeadSamples = (iArgLead * iArgSamplingRate) / 1000;
  bPriming = iLeadSamples > 0;
  iMaxBuffered = iLeadSamples + 2 * sAudioSpec.samples;

  // Start the callback ...
  SDL_PauseAudio (0);
}


void SNDShutdown ()
{
  // Close the audio device ...
  SDL_CloseAudio ();

  if (iStatLevel > 0)
  {
    printf ("SND stats: callbacks %d, underruns %d (%ld samples), dropped %ld samples, overwritten %ld samples\n",
            iStatCallbacks, iStatUnderruns, iStatUnderrunSamples, iStatDroppedSamples, iStatOverwrittenSamples);
  }

  // Free the audio buffer mutex ...
  CheckZero (pthread_mutex_destroy (&sAudioBufferLock));
  delete [] (pAudioBuffer);
}


//--------------------------------------------------------------------------

