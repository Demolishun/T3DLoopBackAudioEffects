/*
Loopback Audio design:
- One thread to capture loopback data.
- FFT function called at a fixed bin width and data summed into a single bin structure.
- Stack of objects notified of loopback data.  The stack of objects will spawn a thread
to extract the desired bin information.  Further calcs can be applied on the object data
via inheritance to define the output calcs on the data.
- Each object does its own calculations based upon the output needed.
*/

#ifndef _LOOPBACK_AUDIO_H_
#define _LOOPBACK_AUDIO_H_

#include <core/util/tVector.h>
#include <console/simObject.h>
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

#define AUDIO_FREQ_BANDS 9
#define AUDIO_FFT_BINS 256
#define AUDIO_DATA_GAIN 1.0f

// forward declarations
class LoopbackObject;

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
#define REFTIMES_PER_MILLISEC  (REFTIMES_PER_SEC/1000)

#define AUDIOLB_EXIT_ON_ERROR(hres)  \
      if (FAILED(hres)) { goto Exit; }
#define AUDIOLB_SAFE_RELEASE(punk)  \
      if ((punk) != NULL)  \
         { (punk)->Release(); (punk) = NULL; }

class AudioLoopbackThread : public Thread
{
   private:
      // windows specific data types
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

      // captured data
      U32 allocedSamples;
      U32 capturedSamples;
      F32 *captured;       

      // loopbackObject tracking
      Semaphore loopbackObjectsAccess;
      // used to add/remove objects from loopbackObjects
      VectorPtr<LoopbackObject*> loopbackObjectsAdd;
      VectorPtr<LoopbackObject*> loopbackObjectsRemove;
      // used to hold working list of loopbackObjects
      VectorPtr<LoopbackObject*> loopbackObjects;

   public:
      AudioLoopbackThread(bool start_thread = false, bool autodelete = false);
      ~AudioLoopbackThread();

      // overriden methods
      void run(void *arg /* = 0 */);

      // data for calcs in game
      U32 getNumCapturedSamples(){return capturedSamples;};
      void copyCapturedSamples(F32 *dest){
         if(dest){
            memcpy((void*)dest,(void*)captured,sizeof(F32)*capturedSamples);            
         }
      };

      // sample rate of audio
      S32 getSampleRate(){
         if(pwfx != NULL)
            return pwfx->nSamplesPerSec;
         else
            return -1;
      };

      // get number channels
      S32 getNumChannels(){
         if(pwfx != NULL)
            return pwfx->nChannels;
         else
            return -1;
      }

      // manage LoopbackObjects
      void addLoopbackObject(LoopbackObject* lbObj){
         if(loopbackObjectsAccess.acquire()){
            loopbackObjectsAdd.push_front(lbObj);
            loopbackObjectsAccess.release();
         }
      }
      void removeLoopbackObject(LoopbackObject* lbObj){
         if(loopbackObjectsAccess.acquire()){
            loopbackObjectsRemove.remove(lbObj);
            loopbackObjectsAccess.release();
         }
      }

      /*
      // bin width of FFT            
      U32 getFFTBinWidth(){
         return AUDIO_FFT_BINS;
      }
      // number of bands
      U32 getFreqBands(){
         return AUDIO_FREQ_BANDS;
      }
      */
      // calculate frequency per BIN as BIN*SR/FFTWIDTH
      // each band = sumof(((AUDIO_FFT_BINS/2)/AUDIO_FREQ_BANDS)) % AUDIO_FREQ_BANDS)
      // basically the bands are clumped using a modulus to determine which bins belong in which band
};

AudioLoopbackThread *_activeLoopbackThread = NULL;
/*
LoopbackObject - object to hold loopback sampled data
Base class for loopback data capture and processing
*/
class LoopbackObject: public SimObject
{
   protected:
      // loopback thread
      static AudioLoopbackThread *activeLoopbackThread;
   private:
      // semaphore to control captured audio data access
      Semaphore mCapturedDataAccess;
      bool mGotCapturedDataAccess;

   protected:
      // data controlled by mCapturedDataAccess semaphore
      U32 mCapturedDataSamples;
      F32* mCapturedData;            

   public:
      LoopbackObject();
      ~LoopbackObject();

      // loopback thread control
      bool startLoopback();
      bool stopLoopback();

      // callback to process the returned audio data
      virtual void process();
      // callback to setup preprocess data
      virtual void preprocess();
      // callback to release semaphore grabbed by process
      void done();
};

class FFTLoopbackObject: public LoopbackObject
{
   private:
      volatile static U32 mBinWidth;
      volatile static bool mBinResized;
      volatile static bool mBinCalculated;
      volatile static F32 *mBinData;

      volatile U32 mNumBands;
      volatile F32 *mBands; 
      
   public:
      FFTLoopbackObject();

      // callback to process the returned audio data
      virtual void process();
      // callback to setup preprocess data
      virtual void preprocess();

      // set bin width
      //    warning this sets the bin width for all FFT objects
      void setBinWidth(U32 width){
         mBinWidth=width;
         mBinResized=true;
      }
};

inline F32 hanningWindow(F32 data, U32 i, U32 s);
inline F32 lowPassFilter(F32 input, F32 last, F32 filter);

#endif // _LOOPBACK_AUDIO_H_