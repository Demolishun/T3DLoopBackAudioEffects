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
//#include "scene/sceneObject.h"
#include "console/simObject.h"
#include "console/simSet.h"
//#include "materials/matTextureTarget.h"
#include "gui/core/guiTypes.h"
#include "gui/worldEditor/gizmo.h"

#include <mmsystem.h>
#undef INITGUID
#include <mmdeviceapi.h>
#include <dsound.h>
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmreg.h>

class BaseMatInstance;

#define AUDIO_FREQ_BANDS 9
#define AUDIO_FFT_BINS 256
#define AUDIO_DATA_GAIN 1.0f

#define AUDIO_NUM_CHANNELS 2

//#define REFTIMES_PER_SEC  10000000
//#define REFTIMES_PER_SEC  (10000000/20) // run every 50 mS
#define REFTIMES_PER_SEC  (10000000/10) // run every 100 mS - much better freq delineation at the low end
//#define REFTIMES_PER_SEC  (10000000/5) // run every 200 mS
//#define REFTIMES_PER_SEC  (10000000/50) // run every 20 mS
#define REFTIMES_PER_MILLISEC  (REFTIMES_PER_SEC/1000)

#define AUDIOLB_EXIT_ON_ERROR(hres)  \
      if (FAILED(hres)) { goto Exit; }
#define AUDIOLB_SAFE_RELEASE(punk)  \
      if ((punk) != NULL)  \
         { (punk)->Release(); (punk) = NULL; }

class LoopBackObject;

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
   F32 *internalSampleData;

   static Mutex loopbackObjectsMutex;
   static Vector<SimObjectPtr<LoopBackObject>> loopbackObjects;

private:
   // contol access to buffer and details
   static Mutex sampleBufferMutex;      
   // 2 channels in the sample buffer
   static F32* sampleBuffer;  // should this memory ever be freed?
   static U32 sampleBufferSize; // total size of buffer divided by 2 (stereo data)
   static U32 sampleBufferSamples; // total number of samples in buffer
   static U32 samplesPerSecond;  // used to calculate bin freqs
             
public:
   AudioLoopbackThread(bool start_thread = false, bool autodelete = false);
   ~AudioLoopbackThread();

   // overriden methods
   void run(void *arg /* = 0 */);

   // add/remove objects to process loop
   static void addLoopbackObject(LoopBackObject* obj);        
   static void removeLoopbackObject(LoopBackObject* obj);   
};

//AudioLoopbackThread *_activeLoopbackThread = NULL;

class LoopBackObject : public SimObject
{
typedef SimObject Parent;

protected:       
   // external data source buffer
   Mutex* extSampleBufferMutex;
   F32** extSampleBuffer;
   U32* extSampleBufferSize;
   U32* extSampleBufferSamples;
   U32* extSamplesPerSecond; 

   // internal object data
   Mutex objectSampleBufferMutex; 
   F32* objectSampleBuffer;
   U32 objectSampleBufferSize;
   U32 objectSampleBufferSamples;
   U32 objectSamplesPerSecond;
   // flag to indicate that the data has changed
   // is updated after both the raw data is copied and the processed data is updated on derived classes
   U32 mDataChanged;

   // hook to properly remove object from any list it belongs to
   void (*removeFunc)(LoopBackObject* object);

public:
   LoopBackObject();
   virtual ~LoopBackObject();      

   virtual void setExtSampleBuffer(Mutex* extmut, F32** extbuff, U32* extbuffsize, U32* extbuffsamples, U32* extsamplessecond);
   void setRemoveFunction(void (*rfunc)(LoopBackObject* object)){removeFunc = rfunc;}
   virtual void clearExtSampleBuffer();

   virtual void process();
   // placeholder for sub classes
   // objectSampleBufferMutex should be acquired before calling this function, see LoopBackObject::process()
   virtual void process_unique(){};

   // check for data changed
   //    the mDataChanged flag will update on each new sample from the source
   //    it is simply a counter that will roll over after 4 billion plus counts
   U32 getDataChanged(){
      MutexHandle objectMutex;
      objectMutex.lock( &objectSampleBufferMutex, true );

      return mDataChanged;
   }
   // get the raw audio in stereo 
   // returns changed flag  
   virtual U32 getAudioOutput(Vector<F32>& retoutput){
      MutexHandle mutex;
      mutex.lock( &objectSampleBufferMutex, true );      

      retoutput.clear();
      retoutput.set( objectSampleBuffer, objectSampleBufferSize*AUDIO_NUM_CHANNELS );   

      return mDataChanged;
   }
   // get the processed output, redefined in derived objects
   // returns changed flag
   virtual U32 getProcessedOutput(Vector<F32>& retoutput){      
      // just clear the provided buffer to indicate there is nothing to get from this object
      retoutput.clear();
      // no data, just return zero for changed
      return 0;
   }
   
   DECLARE_CONOBJECT(LoopBackObject);
};

class FFTObject : public LoopBackObject
{
typedef LoopBackObject Parent;

private:
   // protect FFT data in FFTObject
   Mutex objectFFTDataMutex;     
   Vector<U32> AudioFreqBands;
   Vector<F32> AudioFreqOutput;      

public:
   FFTObject();
   virtual ~FFTObject();      

   // custom processing for FFT 
   virtual void process_unique();

   // set the freq bands
   void setAudioFreqBands(Vector<U32>& bands){
      MutexHandle mutex;
      mutex.lock( &objectFFTDataMutex, true );  
             
      AudioFreqBands.clear();
      AudioFreqBands.merge(bands);
      U32 outsize = AudioFreqOutput.size();
      U32 bandsize = AudioFreqBands.size();
      if(outsize != bandsize){                    
         AudioFreqOutput.setSize(bandsize);
         if(outsize < bandsize){
            U32 diff = bandsize - outsize;
            for(U32 count=0; count<diff; count++){
               AudioFreqOutput[outsize+count] = 0.0f;
            }
         }
      }
   }
   // get the freq bands
   void getAudioFreqBands(Vector<U32>& retbands){
      MutexHandle mutex;
      mutex.lock( &objectFFTDataMutex, true );

      retbands.clear();
      retbands.merge(AudioFreqBands);         
   }
   // get the processed FFT output divided up into bands
   void getAudioFreqOutput(Vector<F32>& retoutput){
      MutexHandle mutex;
      mutex.lock( &objectFFTDataMutex, true );

      retoutput.clear();
      retoutput.merge(AudioFreqOutput);         
   }
   // get the processed FFT output
   // returns changed flag
   virtual U32 getProcessedOutput(Vector<F32>& retoutput){  
      // return object specific data    
      getAudioFreqOutput(retoutput);
      // get mutex protected changed flag
      return getDataChanged();
   }

   DECLARE_CONOBJECT(FFTObject);
};

// support functions for processing sampled data

// "windowing" (Math term) function for digitally sampled data
//    increases the ability to separate frequencies using FFT
inline F32 hanningWindow(F32 data, U32 i, U32 s);
// DSP method for filtering data, freq response is approximately that of a moving average, but is more tunable, flexible, and uses less memory
//    used to "smooth" the data
inline F32 lowPassFilter(F32 input, F32 last, F32 filter);

#endif // _LOOPBACK_AUDIO_H_