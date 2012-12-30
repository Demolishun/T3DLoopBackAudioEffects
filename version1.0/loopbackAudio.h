#ifndef _LOOPBACK_AUDIO_H_
#define _LOOPBACK_AUDIO_H_

#include <core/util/tVector.h>
#ifndef TORQUE_OS_XENON
#include "platformWin32/platformWin32.h"
#endif
#include "platform/threads/thread.h"
#include "platform/threads/mutex.h"
#include "console/console.h"
#include "platform/platformIntrinsics.h"

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <dsound.h>
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmreg.h>

#define AUDIO_FREQ_BANDS 9
#define AUDIO_FFT_BINS 256
#define AUDIO_DATA_GAIN 1.0f

// new type
union F32_U32
{
F32 f;
U32 i;
};
// transfer variables
volatile F32_U32 AudioFreqOutput[AUDIO_FREQ_BANDS];  // controlled by loopback thread
volatile U32 AudioBandFreqs[AUDIO_FREQ_BANDS]; // controlled by main thread

// working buffers to store data and use in equations in loopback thread
F32 _AudioFreqOutput[AUDIO_FREQ_BANDS]; // internal value
U32 _AudioBandFreqs[AUDIO_FREQ_BANDS]; // internal data

//#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_SEC  (10000000/20) // run every 50 mS
//#define REFTIMES_PER_SEC  (10000000/10) // run every 100 mS
//#define REFTIMES_PER_SEC  (10000000/50) // run every 20 mS
#define REFTIMES_PER_MILLISEC  (REFTIMES_PER_SEC/1000)

#define AUDIOLB_EXIT_ON_ERROR(hres)  \
      if (FAILED(hres)) { goto Exit; }
#define AUDIOLB_SAFE_RELEASE(punk)  \
      if ((punk) != NULL)  \
         { (punk)->Release(); (punk) = NULL; }

class AudioLoopbackThread : public Thread
{
   private:
      HRESULT hr;
      REFERENCE_TIME hnsRequestedDuration;
      REFERENCE_TIME hnsActualDuration;
      UINT32 bufferFrameCount;
      UINT32 numFramesAvailable;
      IMMDeviceEnumerator *pEnumerator;
      IMMDevice *pDevice;
      IAudioClient *pAudioClient;
      IAudioCaptureClient *pCaptureClient;
      WAVEFORMATEX *pwfx;
      UINT32 packetLength;      
      BYTE *pData;
      DWORD flags;    
   
      // internal sample data
      F32 *windowedMonoData;

      // external sample access
      U32 lastSampleSize;
      U32 externalBufferSize;
      F32 *externalBuffer;
      Mutex extBuffMutex;

   public:
      AudioLoopbackThread(bool start_thread = false, bool autodelete = false);
      ~AudioLoopbackThread();

      // overriden methods
      void run(void *arg /* = 0 */);

      // data for calcs in game

      // sample rate of audio
      S32 getSampleRate(){
         if(pwfx != NULL)
            return pwfx->nSamplesPerSec;
         else
            return -1;
      };
      // bin width of FFT         
      U32 getFFTBinWidth(){
         return dAtomicRead(lastSampleSize);
      }
      // number of bands
      U32 getNumFreqBands(){
         return AUDIO_FREQ_BANDS;
      }
      // calculate frequency per BIN as BIN*SR/FFTWIDTH
      // each band = sumof(((AUDIO_FFT_BINS/2)/AUDIO_FREQ_BANDS)) % AUDIO_FREQ_BANDS)
      // basically the bands are clumped using a modulus to determine which bins belong in which band
};

AudioLoopbackThread *_activeLoopbackThread = NULL;

inline F32 hanningWindow(F32 data, U32 i, U32 s);
inline F32 lowPassFilter(F32 input, F32 last, F32 filter);

#endif // _LOOPBACK_AUDIO_H_