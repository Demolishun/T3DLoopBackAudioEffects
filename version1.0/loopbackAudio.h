#ifndef _LOOPBACK_AUDIO_H_
#define _LOOPBACK_AUDIO_H_

#include <core/util/tVector.h>
#ifndef TORQUE_OS_XENON
#include "platformWin32/platformWin32.h"
#endif
#include "platform/threads/thread.h"
#include "console/console.h"
#include "platform/platformIntrinsics.h"

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <dsound.h>
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmreg.h>
//#include <windows.h>
//#include <winnt.h>

#define AUDIO_FREQ_BANDS 5
#define MS_SLEEP_TIME 10
// new type
union F32_U32
{
F32 f;
U32 i;
};
// transfer variables
volatile F32_U32 AudioFreqOutput[AUDIO_FREQ_BANDS];  // controlled by loopback thread
volatile F32_U32 AudioFilterValues[AUDIO_FREQ_BANDS];  // controlled by main thread
// working buffers to store data and use in equations in loopback thread
F32 _AudioFreqOutput[AUDIO_FREQ_BANDS];
F32 _AudioFilterValues[AUDIO_FREQ_BANDS];

//#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_SEC  (10000000/20) // run every 50 mS
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

   public:
      AudioLoopbackThread(bool start_thread = false, bool autodelete = false);

      // overriden methods
      void run(void *arg /* = 0 */);
};

AudioLoopbackThread *_activeLoopbackThread = NULL;

void audioLoopbackFunction(void *data);

#endif // _LOOPBACK_AUDIO_H_