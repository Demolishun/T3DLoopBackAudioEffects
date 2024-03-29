#include "loopbackAudio.h"

#include "console/engineAPI.h"
#include "core/stream/memStream.h"
#include "math/mathIO.h"
#include "core/stream/bitStream.h"
#include "scene/sceneRenderState.h"
#include "materials/sceneData.h"
#include "materials/materialManager.h"
#include "materials/baseMatInstance.h"
#include "gfx/gfxDebugEvent.h"
#include "gfx/gfxTransformSaver.h"
#include "gfx/gfxDrawUtil.h"
#include "gfx/gFont.h"
//#include "gfx/bitmap/gBitmap.h"
#include "renderInstance/renderPassManager.h"
#include "math/mathUtils.h"


#include "gfx/gfxDebugEvent.h"
//#include <avrt.h>
//#pragma comment(lib, "Avrt.lib")

#include "kiss_fft/kiss_fft.h"
#include "kiss_fft/kiss_fftr.h"

#include <D3dx9core.h>
#define INITGUID
#include <mmdeviceapi.h>
#undef INITGUID

AudioLoopbackThread *_activeLoopbackThread = NULL;

/*
The audio frequency data will be divided into freq bands from low to high.
AUDIO_FREQ_BANDS determines the number of bands.
MS_SLEEP_TIME time to sleep between calculations.
AudioFreqOutput is the output buffer that contains the filtered band magnitude data.
AudioFilterValues are the filter values used to calcuate each band progressively using an exponential filter.
*/

// static data
Mutex AudioLoopbackThread::loopbackObjectsMutex;
Vector<SimObjectPtr<LoopBackObject>> AudioLoopbackThread::loopbackObjects;

Mutex AudioLoopbackThread::sampleBufferMutex;
F32 *AudioLoopbackThread::sampleBuffer = NULL;
U32 AudioLoopbackThread::sampleBufferSize = 0;
U32 AudioLoopbackThread::sampleBufferSamples = 0;
U32 AudioLoopbackThread::samplesPerSecond = 0;

AudioLoopbackThread::AudioLoopbackThread(bool start_thread, bool autodelete)
:Thread(NULL,NULL,start_thread,autodelete)
{
   hnsRequestedDuration = REFTIMES_PER_SEC;
   pEnumerator = NULL;
   pDevice = NULL;
   pAudioClient = NULL;
   pCaptureClient = NULL;
   pwfx = NULL;
   packetLength = 0;    

   internalSampleData = NULL;
}

AudioLoopbackThread::~AudioLoopbackThread(){
   // free memory
   if(internalSampleData)
      free(internalSampleData);  

   //Con::printf("Deallocating LoopBackObject::sampleBuffer");
   
   MutexHandle mutex;
   mutex.lock( &sampleBufferMutex, true );
   sampleBufferSize = 0;
   sampleBufferSamples = 0;
   samplesPerSecond = 0;
   if(sampleBuffer){
      free(sampleBuffer);
      sampleBuffer = NULL;
   }
   
   //Con::printf("Done Deallocating LoopBackObject::sampleBuffer");
}

void AudioLoopbackThread::run(void *arg /* = 0 */)
{      
   // init audio device
   hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), 
      NULL, CLSCTX_ALL, 
      __uuidof(IMMDeviceEnumerator),
      (void**)&pEnumerator);
   AUDIOLB_EXIT_ON_ERROR(hr)

   hr = pEnumerator->GetDefaultAudioEndpoint(
      eRender, eConsole, &pDevice); // eCapture changed to eRender for loopback
   AUDIOLB_EXIT_ON_ERROR(hr)

   hr = pDevice->Activate(
      __uuidof(IAudioClient), 
      CLSCTX_ALL, NULL, 
      (void**)&pAudioClient);
   AUDIOLB_EXIT_ON_ERROR(hr)

   hr = pAudioClient->GetMixFormat(&pwfx);
   AUDIOLB_EXIT_ON_ERROR(hr)

   // ensure format is something we can use
   if(pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT){
      if(pwfx->nChannels < AUDIO_NUM_CHANNELS) // need stereo
         hr = -1;
   }else if(pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
      PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
      if(IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)){
         if(pwfx->nChannels < AUDIO_NUM_CHANNELS) // need stereo
            hr = -1;
      }else{
         hr = -1;
      }
   }else{
      hr = -1;      
   }
   AUDIOLB_EXIT_ON_ERROR(hr)

   hr = pAudioClient->Initialize(
      AUDCLNT_SHAREMODE_SHARED,
      AUDCLNT_STREAMFLAGS_LOOPBACK, // 0 changed to AUDCLNT_STREAMFLAGS_LOOPBACK for loopback
      hnsRequestedDuration,
      0,
      pwfx,
      NULL);
   AUDIOLB_EXIT_ON_ERROR(hr)

   // Get the size of the allocated buffer.
   hr = pAudioClient->GetBufferSize(&bufferFrameCount);
   AUDIOLB_EXIT_ON_ERROR(hr)

   hr = pAudioClient->GetService(
      __uuidof(IAudioCaptureClient),
      (void**)&pCaptureClient);
   AUDIOLB_EXIT_ON_ERROR(hr)

   // register with MMCSS
   // I think this controls priority to be realtime, might need this
   /*
   DWORD nTaskIndex = 0;
   HANDLE hTask = AvSetMmThreadCharacteristics(L"Capture", &nTaskIndex);
   if (hTask == NULL){
      hr = -1;
      AUDIOLB_EXIT_ON_ERROR(hr)
   } 
   */  

   // format is in pwfx

   // Calculate the actual duration of the allocated buffer.
   hnsActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

   hr = pAudioClient->Start();  // Start recording.
   AUDIOLB_EXIT_ON_ERROR(hr)

   // freq per bin in FFT
   // Fs/N where Fs=sample rate N=FFT width
   // ignore upper half of FFT output   

   F32 summing_buffer[AUDIO_FREQ_BANDS];
   U32 samplesize = 0; 
   U32 buffersize = 0;  

   // thread control loop
   while(!checkForStop()){            
      Sleep(hnsActualDuration/REFTIMES_PER_MILLISEC/2);      

      hr = pCaptureClient->GetNextPacketSize(&packetLength);
      AUDIOLB_EXIT_ON_ERROR(hr)      

      // clear summing buffer      
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         summing_buffer[count] = 0.0f;
      }    

      //sum_divisor = 0;  
      samplesize = 0;      

      while(packetLength != 0)
      {        
         // Get the available data in the shared buffer.
         hr = pCaptureClient->GetBuffer(
            &pData,
            &numFramesAvailable,
            &flags, NULL, NULL);
         AUDIOLB_EXIT_ON_ERROR(hr)

         if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
         {
             pData = NULL;  // Tell CopyData to write silence.
         }               

         // grab and manipulate data here
         #define TEMP_LB_FILTER_VAL 0.1f
         #define TEMP_LB_GAIN 1.0f
         
         if(pData != NULL){
            U32 currentindex = samplesize;
            samplesize += packetLength;            
            if(samplesize > buffersize || internalSampleData == NULL){               
               buffersize = samplesize;
               internalSampleData = (F32 *)realloc(internalSampleData, sizeof(F32)*buffersize*AUDIO_NUM_CHANNELS);               
               //Con::printf("%d", buffersize);  // verify allocation is working
            }
            
            F32 *pFloatData = reinterpret_cast<F32*>(pData);
            //Con::printf("packetlength: %d",packetLength);                   
            
            for(U32 count=0; count<packetLength; count++){               
               internalSampleData[currentindex*AUDIO_NUM_CHANNELS+count*AUDIO_NUM_CHANNELS+0] = pFloatData[count*pwfx->nChannels+0];
               internalSampleData[currentindex*AUDIO_NUM_CHANNELS+count*AUDIO_NUM_CHANNELS+1] = pFloatData[count*pwfx->nChannels+1];               
            }                        
         }else{
            // do calcs with zero for values
            // not needed, the value will zero out after audio source is removed
         }                    
      
         // release data
         hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
         AUDIOLB_EXIT_ON_ERROR(hr)

         // start next capture
         hr = pCaptureClient->GetNextPacketSize(&packetLength);
         AUDIOLB_EXIT_ON_ERROR(hr)
      }      
      
      //Con::printf("%d",samplesize);
      if(!samplesize)
         continue;

      // resize external buffer as needed
      // access to the external buffer is controlled by mutex      
      MutexHandle mutex;
      mutex.lock( &sampleBufferMutex, true );
      if(buffersize > sampleBufferSize){
         sampleBufferSize = buffersize;
         sampleBuffer = (F32 *)realloc(sampleBuffer, sizeof(F32)*sampleBufferSize*AUDIO_NUM_CHANNELS);    
      }
      // copy sample details
      samplesPerSecond = pwfx->nSamplesPerSec;
      sampleBufferSamples = samplesize;      
      // copy raw sample data to LoopBackObject buffer
      memcpy(sampleBuffer,internalSampleData,sizeof(F32)*samplesize*AUDIO_NUM_CHANNELS);      
      mutex.unlock();

      // process loopback objects
      //LoopBackObject::processLoopBack();      
      mutex.lock( &loopbackObjectsMutex, true );
      Vector<SimObjectPtr<LoopBackObject>>::iterator i; 
      for(i = loopbackObjects.begin(); i != loopbackObjects.end();)  
      {  
         LoopBackObject *obj = (*i); // (LoopBackObject *)
         if(!obj){ 
            loopbackObjects.remove(*i);
            continue;
         }
         else
            i++;
            
         obj->process();
      } 
      mutex.unlock();            
   }   

   hr = pAudioClient->Stop();  // Stop recording.
   AUDIOLB_EXIT_ON_ERROR(hr)

Exit:

   if(FAILED(hr)){
      // debug, not really thread safe
      Con::warnf("AudioLoopbackThread::run - loopback error: %X",hr);
   }

   // clean up init
   CoTaskMemFree(pwfx);
   AUDIOLB_SAFE_RELEASE(pEnumerator)
   AUDIOLB_SAFE_RELEASE(pDevice)
   AUDIOLB_SAFE_RELEASE(pAudioClient)
   AUDIOLB_SAFE_RELEASE(pCaptureClient)   
   //if(hTask)
   //   AvRevertMmThreadCharacteristics(hTask);
}

void AudioLoopbackThread::addLoopbackObject(LoopBackObject* obj){
   MutexHandle mutex;
   mutex.lock( &loopbackObjectsMutex, true );
   loopbackObjects.push_back(obj); 

   obj->setExtSampleBuffer(&sampleBufferMutex, &sampleBuffer, &sampleBufferSize, &sampleBufferSamples, &samplesPerSecond);
   obj->setRemoveFunction(AudioLoopbackThread::removeLoopbackObject);
}   
void AudioLoopbackThread::removeLoopbackObject(LoopBackObject* obj){
   MutexHandle mutex;
   mutex.lock( &loopbackObjectsMutex, true );
   loopbackObjects.remove(obj);   

   obj->clearExtSampleBuffer();
   obj->setRemoveFunction(NULL);
}

// console defs
IMPLEMENT_CONOBJECT(LoopBackObject);

// functions
LoopBackObject::LoopBackObject(){
   //objectSampleFilter = 0.2f;
   objectSampleBufferSize = 0;
   objectSampleBufferSamples = 0;
   objectSampleBuffer = NULL;

   extSampleBufferMutex = NULL;
   extSampleBuffer = NULL;
   extSampleBufferSize = NULL;
   extSampleBufferSamples = NULL;
   extSamplesPerSecond = NULL;

   removeFunc = NULL;  

   mDataChanged = 0;
}
LoopBackObject::~LoopBackObject(){   
   // call remove function
   if(removeFunc != NULL)
      removeFunc(this);   

   // acquire mutex before delete
   MutexHandle objectMutex;
   objectMutex.lock( &objectSampleBufferMutex, true );  

   free(objectSampleBuffer); 

   // this printf will crash the engine is a large number of objects are deleted at once
   //Con::printf("LoopBackObject::~LoopBackObject() - acquired objectSampleBufferMutex mutex.");
}

void LoopBackObject::process(){
   //Con::printf("LoopBackObject::process() - Processing audio data: %d",this->getId());
       
   if(!extSampleBufferMutex){
      Con::warnf("LoopBackObject::process() - This object not properly associated with a sample source.");
   }

   // get object sample buffer
   MutexHandle objectMutex;
   objectMutex.lock( &objectSampleBufferMutex, true );

   // get global sample buffer
   MutexHandle mutex;
   mutex.lock( extSampleBufferMutex, true );

   // resize buffer as needed
   if(*extSampleBufferSize > objectSampleBufferSize){
      //U32 diff = sampleBufferSize - objectSampleBufferSize;
      objectSampleBufferSize = *extSampleBufferSize;
      objectSampleBuffer = (F32 *)realloc(objectSampleBuffer, sizeof(F32)*objectSampleBufferSize*AUDIO_NUM_CHANNELS);          
      // init new memory
      /*
      for(U32 count=(objectSampleBufferSize-diff)*AUDIO_NUM_CHANNELS; count < objectSampleBufferSize*AUDIO_NUM_CHANNELS; count++){
         objectSampleBuffer[count] = 0.0f;            
      }
      */
   }
   objectSampleBufferSamples = *extSampleBufferSamples;
   objectSamplesPerSecond = *extSamplesPerSecond;
   /*
   // filter sample data using lowPassFilter - not a good idea
   for(U32 count=0; count < objectSampleBufferSamples*AUDIO_NUM_CHANNELS; count++){
      objectSampleBuffer[count] = lowPassFilter(sampleBuffer[count],objectSampleBuffer[count],objectSampleFilter);
   } 
   */  
   // copy sample data
   memcpy(objectSampleBuffer,*extSampleBuffer,sizeof(F32)*objectSampleBufferSamples*AUDIO_NUM_CHANNELS);   

   // done with global sample buffer
   mutex.unlock();

   // now perform additional processing on sample data
   // keeps from needing to reacquire mutex or call additional functions
   process_unique();

   // All done, update counter
   mDataChanged++;

   // release object mutex
   objectMutex.unlock();   
}

// use objectSampleBufferMutex to protect this call
void LoopBackObject::setExtSampleBuffer(Mutex* extmut, F32** extbuff, U32* extbuffsize, U32* extbuffsamples, U32* extsamplessecond){
   if(extSampleBufferMutex){
      Con::warnf("LoopBackObject::setExtSampleBuffer - object is already associated with a sample source.  Remove the object form that source first.");
      return;
   }

   MutexHandle objectMutex;
   objectMutex.lock( &objectSampleBufferMutex, true );

   extSampleBufferMutex = extmut;
   extSampleBuffer = extbuff;
   extSampleBufferSize = extbuffsize;
   extSampleBufferSamples = extbuffsamples;
   extSamplesPerSecond = extsamplessecond;
}
void LoopBackObject::clearExtSampleBuffer(){
   if(!extSampleBufferMutex){
      Con::warnf("LoopBackObject::clearExtSampleBuffer - object is not associated with a sample source.");
      return;
   }

   MutexHandle objectMutex;
   objectMutex.lock( &objectSampleBufferMutex, true );

   extSampleBufferMutex = NULL;
   extSampleBuffer = NULL;
   extSampleBufferSize = NULL;
   extSampleBufferSamples = NULL;
   extSamplesPerSecond = NULL;
}

// FFT Object
IMPLEMENT_CONOBJECT(FFTObject);

FFTObject::FFTObject(){
   // sane defaults
   U32 freq = 30;
   for(U32 count=0; count < 9; count++){
      AudioFreqBands.push_back(freq);
      freq *= 2;
   }
   AudioFreqOutput.setSize(AudioFreqBands.size());
   AudioFreqOutput.fill(0.0f);
}
FFTObject::~FFTObject(){
   // acquire mutex before delete
   MutexHandle mutex;
   mutex.lock( &objectFFTDataMutex, true ); 

   // this printf will crash the engine is a large number of objects are deleted at once
   //Con::printf("FFTObject::~FFTObject() - acquired objectFFTDataMutex mutex.");
}
// custom processing for FFT 
void FFTObject::process_unique(){
   //Con::printf("FFTObject::process_unique() - Processing sample data: %d",this->getId());

   MutexHandle mutex;
   mutex.lock( &objectFFTDataMutex, true ); 
    
   U32 samplesize = objectSampleBufferSamples;
   samplesize &= ~0x1; // force even

   //Con::printf("samplesize: %d",samplesize);

   // make mono and window the data in place
   F32 packed;
   for(U32 count=0; count<samplesize; count++){           
      packed = (objectSampleBuffer[count*AUDIO_NUM_CHANNELS+0] + objectSampleBuffer[count*AUDIO_NUM_CHANNELS+1])*AUDIO_DATA_GAIN;       
      objectSampleBuffer[count] = hanningWindow(packed, count, samplesize);         
   }

   // these buffers should be able to grow rather than alloc new and deleting
   //    todo: make these grow, a max size will be reached and not need to grow bigger
   kiss_fftr_cfg st = kiss_fftr_alloc(samplesize,0,0,0);            
   kiss_fft_cpx* out = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*(samplesize/2+1));
   kiss_fftr(st,objectSampleBuffer,(kiss_fft_cpx*)out);  
   
   // combine freqs into bands
   U32 bandstep = 0;
   U32 binsperband = 0;
   U32 currentfreqbin = 0;

   Vector<F32> summing_buffer;
   summing_buffer.setSize(AudioFreqOutput.size()); 
   summing_buffer.fill(0.0f); 
   for(U32 count=0; count<(samplesize/2) && bandstep<AudioFreqBands.size();){ 
      // update value              
      F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
      binsperband++;
      summing_buffer[bandstep] += combined;
      
      // increment and ready comparison data
      count++;
      currentfreqbin = (count*objectSamplesPerSecond)/samplesize;      
      U32 tempFreq;

      // discriminate frequencies as a center freq for each band
      if(bandstep != AudioFreqBands.size()-1){
         tempFreq = (AudioFreqBands[bandstep]+AudioFreqBands[bandstep+1])/2;
      }else{
         tempFreq = AudioFreqBands[bandstep]+AudioFreqBands[bandstep]/2;
      }
      if(currentfreqbin > tempFreq){ 
         // divide magnitude of each band by the number of bins that it took to build the band          
         //summing_buffer[bandstep] /= (F32)binsperband; // messes up the response on the high end
         binsperband = 0;      
         bandstep++;
      }
   }

   if(st)
      kiss_fft_free(st);
   if(out)
      free(out);

   for(U32 count=0; count<AudioFreqBands.size(); count++){
      F32 logged = (F32)mLog(summing_buffer[count]);
      
      AudioFreqOutput[count] = lowPassFilter(logged,AudioFreqOutput[count],0.5f);            
   }   
}

// window functions
// data = sample
// i = index
// s = number of samples
inline F32 hanningWindow(F32 data, U32 i, U32 s)
{
   return data*0.5f*(1.0f-mCos(M_2PI*(F32)(i)/(F32)(s-1.0f)));
}

inline F32 lowPassFilter(F32 input, F32 last, F32 filter)
{
   return last + filter * (input - last);
}

DefineEngineFunction( startAudioLoopBack, void, (),,
   "Get the frequency information from processing the current AudioLoopBack\n"
   "@param No parameters.\n"
   "@return Nothing.\n"
   "@ingroup AudioLoopBack" )
{
   if(_activeLoopbackThread == NULL){
      _activeLoopbackThread = new AudioLoopbackThread(false, true); // autodelete is true to be self cleaning
      _activeLoopbackThread->start();
   }else{
      Con::warnf("startAudioLoopBack: Existing active audio loopback thread.  New loopback thread not created.");
   }
}

DefineEngineFunction( stopAudioLoopBack, void, (),,
   "Get the frequency information from processing the current AudioLoopBack\n"
   "@param No parameters.\n"
   "@return Nothing.\n"
   "@ingroup AudioLoopBack" )
{
   if(_activeLoopbackThread != NULL){
      _activeLoopbackThread->stop();      
      _activeLoopbackThread = NULL;
   }else{
      Con::warnf("startAudioLoopBack: No active audio loopback to stop.");
   }
}
/*
DefineEngineFunction( onProcessAudioLoopBack, void, (),,
   "Called by the loopback thread or from script to process audio data.\n"
   "@param No parameters.\n"
   "@return Nothing.\n"
   "@ingroup AudioLoopBack" )
{
  
}
*/
DefineEngineFunction( addAudioLoopBackObject, void, (SimObject* obj),,
   "Add LoopBackObject to AudioLoopBack processing.\n"
   "@param No parameters.\n"
   "@return Nothing.\n"
   "@ingroup AudioLoopBack" )
{   
   LoopBackObject *tobj = dynamic_cast<LoopBackObject*>(obj);
   if(tobj)
      AudioLoopbackThread::addLoopbackObject(tobj);
   else
      Con::warnf("addAudioLoopBackObject - Attempt to add non LoopBackObject to AudioLoopBack processing.");
}

DefineEngineFunction( removeAudioLoopBackObject, void, (SimObject* obj),,
   "Remove LoopBackObject to AudioLoopBack processing.\n"
   "@param No parameters.\n"
   "@return Nothing.\n"
   "@ingroup AudioLoopBack" )
{   
   LoopBackObject *tobj = dynamic_cast<LoopBackObject*>(obj);
   if(tobj)
      AudioLoopbackThread::removeLoopbackObject(tobj);
   else
      Con::warnf("addAudioLoopBackObject - Attempt to remove non LoopBackObject from AudioLoopBack processing.");
}

/*
// can override method, but deletions by groups and other mechanisms will not call this method
DefineConsoleMethod( LoopBackObject, delete, void, (),,
   "LoopBackObject: Special Delete and remove the object." )
{
   Con::printf("LoopBackObject: Special Delete called.");
   if(_activeLoopbackThread != NULL){
      _activeLoopbackThread->removeLoopbackObject(object);
   }
   object->deleteObject();
}
*/

DefineEngineMethod(FFTObject, setAudioFreqBands, void, (const char* bandfreqstr),,
   "Set FFTObject frequency bands.\n"
   "@param Comma or space separated list of positive integers.\n"
   "@return Nothing.\n"
   "@ingroup AudioLoopBack")
{
   Vector<U32> tmpbands;

   char *buff = new char[dStrlen(bandfreqstr)];
   dStrncpy(buff,bandfreqstr,dStrlen(bandfreqstr));
   char *value;
   value = dStrtok(buff, " ,");   
      
   while(value != NULL){
      U32 tmp = dAtoui(value);
      tmpbands.push_back(tmp);
      
      value = dStrtok(NULL, " ,");
   }   

   // set bands on object
   object->setAudioFreqBands(tmpbands);

   delete buff;   
}

DefineEngineMethod(FFTObject, getAudioFreqBands, const char*, (),,
   "Get FFTObject frequency bands.\n"
   "@param Nothing.\n"
   "@return Space separated list of positive integers..\n"
   "@ingroup AudioLoopBack")
{
   Vector<U32> tmpbands;
   Vector<U32>::iterator i;
   
   // get bands from object
   object->getAudioFreqBands(tmpbands);

   MemStream tempStream(256);
   char buff[32];
   for(i=tmpbands.begin(); i != tmpbands.end(); i++){
      dSprintf(buff,32,"%d ",*i);
      tempStream.writeText(buff);
   }   
   char *ret = Con::getReturnBuffer(tempStream.getStreamSize());
   dStrncpy(ret, (char *)tempStream.getBuffer(), tempStream.getStreamSize()-1);
   ret[tempStream.getStreamSize()-1] = '\0';

   return ret;
}

DefineEngineMethod(FFTObject, getAudioFreqOutput, const char*, (),,
   "Get FFTObject frequency magnitude output.\n"
   "@param Nothing.\n"
   "@return Space separated list of floats.\n"
   "@ingroup AudioLoopBack")
{
   Vector<F32> tmpoutput;
   Vector<F32>::iterator i;
   
   // get bands from object
   object->getAudioFreqOutput(tmpoutput);

   MemStream tempStream(256);
   char buff[32];
   for(i=tmpoutput.begin(); i != tmpoutput.end(); i++){
      dSprintf(buff,32,"%.4f ",*i);
      tempStream.writeText(buff);
   }   
   char *ret = Con::getReturnBuffer(tempStream.getStreamSize());
   dStrncpy(ret, (char *)tempStream.getBuffer(), tempStream.getStreamSize()-1);
   ret[tempStream.getStreamSize()-1] = '\0';

   return ret;
}

// resources
// http://stackoverflow.com/questions/9645983/fft-applying-window-on-pcm-data
// http://stackoverflow.com/questions/4675457/how-to-generate-the-audio-spectrum-using-fft-in-c

/*
// possibly useful code

// calculate frequencies per band
for(U32 count=0; count<(AUDIO_FFT_BINS/2); count++){
   U32 resIndex = (count/((AUDIO_FFT_BINS/2)/AUDIO_FREQ_BANDS)) % AUDIO_FREQ_BANDS;
   AudioBandFreqs[resIndex] = (count*pwfx->nSamplesPerSec)/AUDIO_FFT_BINS;
}
// ignorant division of frequencies into bands
for(U32 count=0; count<(AUDIO_FFT_BINS/2); count++){
   U32 resIndex = (count/((AUDIO_FFT_BINS/2)/AUDIO_FREQ_BANDS)) % AUDIO_FREQ_BANDS;
   F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
   //summing_buffer[resIndex] = lowPassFilter(combined, summing_buffer[resIndex], 0.2f);
   summing_buffer[resIndex] += combined;
}           
          
for(U32 count=0; count<packetLength; count++){         
   F32 mixedchannels = (pFloatData[count*pwfx->nChannels+0] + pFloatData[count*pwfx->nChannels+1]);               
   //F32 left = pFloatData[count*pwfx->nChannels+0]*TEMP_LB_GAIN;
   //F32 right = pFloatData[count*pwfx->nChannels+1]*TEMP_LB_GAIN;
   _AudioFreqOutput[0] = _AudioFreqOutput[0] + TEMP_LB_FILTER_VAL * (mixedchannels - _AudioFreqOutput[0]);
   //if(left >= 0.0f)
   //_AudioFreqOutput[0] = _AudioFreqOutput[0] + TEMP_LB_FILTER_VAL * (mFabs(left) - _AudioFreqOutput[0]);
   //if(right >= 0.0f)
   //_AudioFreqOutput[1] = _AudioFreqOutput[1] + TEMP_LB_FILTER_VAL * (mFabs(right) - _AudioFreqOutput[1]);
}

*/