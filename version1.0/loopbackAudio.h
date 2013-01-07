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
#include "scene/sceneObject.h"
#include "console/simObject.h"
#include "console/simSet.h"
#include "materials/matTextureTarget.h"

#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <dsound.h>
#include <Audioclient.h>
#include <Audiopolicy.h>
#include <Mmreg.h>

#define AUDIO_FREQ_BANDS 9
#define AUDIO_FFT_BINS 256
#define AUDIO_DATA_GAIN 1.0f

#define AUDIO_NUM_CHANNELS 2

/*
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
*/

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

   // sample rate of audio
   /*
   S32 getSampleRate(){
      if(pwfx != NULL)
         return pwfx->nSamplesPerSec;
      else
         return -1;
   };
   */
   // bin width of FFT         
   /*
   U32 getFFTBinWidth(){
      
      //return dAtomicRead(lastSampleSize);
      return 0;
   }
   */
   // number of bands
   /*
   U32 getNumFreqBands(){
      return AUDIO_FREQ_BANDS;
   }
   */
   // calculate frequency per BIN as BIN*SR/FFTWIDTH
   // each band = sumof(((AUDIO_FFT_BINS/2)/AUDIO_FREQ_BANDS)) % AUDIO_FREQ_BANDS)
   // basically the bands are clumped using a modulus to determine which bins belong in which band
};

AudioLoopbackThread *_activeLoopbackThread = NULL;

class LoopBackObject : public SimObject
{
typedef SimObject Parent;

//friend AudioLoopbackThread;
protected:
   /*      
   // contol access to buffer and details
   static Mutex sampleBufferMutex;      
   // 2 channels in the sample buffer
   static F32* sampleBuffer;  // should this memory ever be freed?
   static U32 sampleBufferSize; // total size of buffer divided by 2 (stereo data)
   static U32 sampleBufferSamples; // total number of samples in buffer
   static U32 samplesPerSecond;  // used to calculate bin freqs
   */

   /*
   static Mutex loopbackObjectsMutex;
   static Vector<LoopBackObject*> loopbackObjects;
   */  
   
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

   DECLARE_CONOBJECT(FFTObject);
};

// support functions for processing sampled data

// "windowing" (Math term) function for digitally sampled data
//    increases the ability to separate frequencies using FFT
inline F32 hanningWindow(F32 data, U32 i, U32 s);
// DSP method for filtering data, freq response is approximately that of a moving average, but is more tunable, flexible, and uses less memory
//    used to "smooth" the data
inline F32 lowPassFilter(F32 input, F32 last, F32 filter);

// Object that can display sampled data on a texture
class AudioTextureObject : public SceneObject
{
private:
   typedef SceneObject Parent;

   enum MaskBits 
   {
      TransformMask = Parent::NextFreeMask << 0,
      NextFreeMask  = Parent::NextFreeMask << 1
   };

   // buffers and pointers to texture
   //    texture writing is double buffered
   GFXTexHandle   mTextureBuffer1;
   GFXTexHandle   mTextureBuffer2;
   GFXTextureObject* mTexture;

   // The size in pixels of the backing texture
   S32 mTexSize;

   // texture reference name for use in materials
   //    eg: diffuseMap[0] = "#MyWebTexture";
   String mTextureName;   

   // enable rendering texture onto an object in the scene
   //    this will default to false to prevent rendering when used as a texture source for materials
   //    the texture will be mapped to a simple quad
   bool mEnableRender;   

public:
   AudioTextureObject();
   virtual ~AudioTextureObject();
   
   // get the current texture handle
   GFXTextureObject* getTexture(){
      return mTexture;
   };

   // Set up any fields that we want to be editable (like position or render enable)
   static void initPersistFields();

   // Handle when we are added to the scene and removed from the scene
   bool onAdd();
   void onRemove();

   // Override this so that we can dirty the network flag when it is called
   void setTransform( const MatrixF &mat );

   // networking
   U32 packUpdate( NetConnection *conn, U32 mask, BitStream *stream );      
   void unpackUpdate( NetConnection *conn, BitStream *stream );

   // Create the geometry for rendering
   //    would used for debug only
   void createGeometry();

   // This is the function that allows this object to submit itself for rendering
   void prepRenderImage( SceneRenderState *state );

   // render the bitmap
   void render( ObjectRenderInst *ri, SceneRenderState *state, BaseMatInstance *overrideMat );

   DECLARE_CONOBJECT(AudioTextureObject);
};

// displaying audio data
class AudioTextureMap : public NamedTexTarget
{
public:
   AudioTextureMap(AudioTextureObject* tobj) {mAudioTextureObject = tobj;}
   ~AudioTextureMap() {  }

   // MatTextureTarget
   virtual GFXTextureObject* getTexture( U32 mrtIndex ) const { return mAudioTextureObject->getTexture(); }
   virtual const RectI& getTargetViewport() const { return RectI::One; }
   virtual void setupSamplerState( GFXSamplerStateDesc *desc ) const { TORQUE_UNUSED( desc ); }
   virtual ConditionerFeature* getTargetConditioner() const { return NULL; }

private:
   AudioTextureObject* mAudioTextureObject;
};

#endif // _LOOPBACK_AUDIO_H_