#include "loopbackAudio.h"

#include "console/engineAPI.h"
#include "core/stream/memStream.h"
#include <avrt.h>
#pragma comment(lib, "Avrt.lib")

#include "kiss_fft/kiss_fft.h"
#include "kiss_fft/kiss_fftr.h"

#include <complex>

// init static values
AudioLoopbackThread* LoopbackObject::activeLoopbackThread = NULL;
volatile U32 FFTLoopbackObject::mBinWidth = 128;
volatile bool FFTLoopbackObject::mBinResized = false;
volatile F32* FFTLoopbackObject::mBinData = NULL;
volatile bool FFTLoopbackObject::mBinCalculated = false;

LoopbackObject::LoopbackObject()
:SimObject()
{
   if(activeLoopbackThread == NULL){
      activeLoopbackThread = new AudioLoopbackThread(false, false);       
   }

   mCapturedDataSamples = 0;
   mCapturedData = NULL;

   // add self to thread vector
   activeLoopbackThread->addLoopbackObject(this);
}
LoopbackObject::~LoopbackObject()
{
   activeLoopbackThread->removeLoopbackObject(this);
   if(mCapturedData){
      free(mCapturedData);
   }
}

// false if loopback thread does not exist or is already running
// true if thread was started
bool LoopbackObject::startLoopback(){
   if(activeLoopbackThread){
      if(!activeLoopbackThread->isAlive()){
         activeLoopbackThread->start();
         return true;
      }else{
         Con::warnf("LoopbackObject::startLoopback - loopback already running.");
         return false;
      }
   }
   Con::warnf("LoopbackObject::startLoopback - loopback thread does not exist.");
   return false;
}
// false if loopback thread does not exist or is already stopped
// true if thread was stopped
bool LoopbackObject::stopLoopback(){
   if(activeLoopbackThread){
      if(activeLoopbackThread->isAlive()){
         activeLoopbackThread->stop();
         return true;
      }else{
         Con::warnf("LoopbackObject::startLoopback - loopback already stopped.");
         return false;
      }
   }
   
   Con::warnf("LoopbackObject::startLoopback - loopback thread does not exist.");
   return false;
}
// callback to process the returned audio data
// This will be called by the loopback thread
//    During that time the loopback thread will own all the LoopbackObjects.
//    The main thread can attempt to access the processed data, but will
//    be denied until all callbacks have finished processing the audio data.
void LoopbackObject::process(){
   // block = true, timeout = 10mS
   // block = false
   mGotCapturedDataAccess = mCapturedDataAccess.acquire(false);
   if(mGotCapturedDataAccess){        
      // copy data
      U32 numsamples = activeLoopbackThread->getNumCapturedSamples();
      // only realloc if the data is larger than the current buffer can hold
      if(numsamples > mCapturedDataSamples){
         mCapturedData = (F32*)realloc((void *)mCapturedData, sizeof(F32)*numsamples);
      }
      if(!mCapturedData)
         // this would be bad
         return;
      // update num samples and copy data
      mCapturedDataSamples = numsamples;
      activeLoopbackThread->copyCapturedSamples(mCapturedData);      
   }   
}

// pre process
void LoopbackObject::preprocess(){
   
}

// release semaphore(s)
void LoopbackObject::done(){
   if(mGotCapturedDataAccess){
      mGotCapturedDataAccess = false;
      mCapturedDataAccess.release();    
   }
}

FFTLoopbackObject::FFTLoopbackObject()
:LoopbackObject()
{
   if(mBinData == NULL){
      mBinData = (F32*)malloc(sizeof(F32)*mBinWidth);
   }   
}

void FFTLoopbackObject::process(){
   // call to retrieve the audio samples
   LoopbackObject::process();
   
   // FFT code
   //    Calculate bins once for all FFT objects.
   if(!mBinCalculated){
      if(mBinResized){
         mBinData = (F32*)realloc((void *)mBinData,sizeof(F32)*mBinWidth);

         mBinResized = false;
      }
      mCapturedDataSamples &= 0xFFFFFFFE;
      if(mCapturedDataSamples){
         S32 numChannels = activeLoopbackThread->getNumChannels();         

         // get windowed data
         F32* windowedMonoData = (F32*)malloc(sizeof(F32)*mCapturedDataSamples);
         for(U32 count=0; count<mCapturedDataSamples; count++){
            F32 packed = (mCapturedData[count*numChannels+0] + mCapturedData[count*numChannels+1])*AUDIO_DATA_GAIN;
            windowedMonoData[count] = hanningWindow(packed, count, mCapturedDataSamples);
         }

         // perform transform
         kiss_fftr_cfg st = kiss_fftr_alloc(mCapturedDataSamples,0,0,0);            
         kiss_fft_cpx* out = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*(mCapturedDataSamples/2+1));
         kiss_fftr(st,windowedMonoData,(kiss_fft_cpx*)out); 

         // sort freq data into bands
             
         
         // deallocate memory
         if(st)
            kiss_fft_free(st);
         if(out)
            free(out);
         if(windowedMonoData)
            free(windowedMonoData);
      }

      // make sure this is only calced once per cycle
      mBinCalculated = true;
   }
}

// pre process
void FFTLoopbackObject::preprocess(){
   mBinCalculated = false;
}

/*
The audio frequency data will be divided into freq bands from low to high.
AUDIO_FREQ_BANDS determines the number of bands.
MS_SLEEP_TIME time to sleep between calculations.
AudioFreqOutput is the output buffer that contains the filtered band magnitude data.
AudioFilterValues are the filter values used to calcuate each band progressively using an exponential filter.
*/

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

   captured = NULL;
   capturedSamples = 0;

   // sane defaults
   U32 freq = 60;
   for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
      AudioBandFreqs[count] = freq;
      //Con::printf("band %d: %d",count,freq);
      freq *= 2;      
   }
   
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){      
      AudioFreqOutput[count].f = 0.0f;
      _AudioFreqOutput[count] = 0.0f;      
   }
}

AudioLoopbackThread::~AudioLoopbackThread(){
   if(captured)
      free(captured);
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
      if(pwfx->nChannels != 2) // need stereo
         hr = -1;
   }else if(pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
      PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
      if(IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)){
         if(pwfx->nChannels != 2) // need stereo
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
   DWORD nTaskIndex = 0;
   HANDLE hTask = AvSetMmThreadCharacteristics(L"Capture", &nTaskIndex);
   if (hTask == NULL){
      hr = -1;
      AUDIOLB_EXIT_ON_ERROR(hr)
   } 

   // format is in pwfx

   // Calculate the actual duration of the allocated buffer.
   hnsActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

   hr = pAudioClient->Start();  // Start recording.
   AUDIOLB_EXIT_ON_ERROR(hr)

   // freq per bin in FFT
   // Fs/N where Fs=sample rate N=FFT width
   // ignore upper half of FFT output   

   //F32 summing_buffer[AUDIO_FREQ_BANDS];

   U32 totalSamples = 0;

   // thread control loop
   while(!checkForStop()){            
      Sleep(hnsActualDuration/REFTIMES_PER_MILLISEC/2);

      // get frequencies per band
         // allows real time updates
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         _AudioBandFreqs[count] = dAtomicRead(AudioBandFreqs[count]);
      }

      hr = pCaptureClient->GetNextPacketSize(&packetLength);
      AUDIOLB_EXIT_ON_ERROR(hr)

      // keep packet length an even number
      //packetLength &= 0xFFFFFFFE;      

      // clear summing buffer    
      /*  
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         summing_buffer[count] = 0.0f;
      } 
      */     

      totalSamples = 0;
      capturedSamples = 0;

      while(packetLength != 0)
      {        
         // track total samples
         totalSamples += packetLength;
      
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
            F32 *pFloatData = reinterpret_cast<F32*>(pData);
            //Con::printf("packetlength: %d",packetLength);

            if(totalSamples > allocedSamples){
               captured = (F32*)realloc((void *)captured, sizeof(F32)*totalSamples*pwfx->nChannels);
               allocedSamples = totalSamples;
            }
            memcpy((void*)(captured+capturedSamples),(void*)pFloatData,sizeof(F32)*packetLength*pwfx->nChannels);
            capturedSamples = totalSamples;
            
            /*
            F32 *windowedMonoData = new F32[packetLength];
            for(U32 count=0; count<packetLength; count++){
               // repack into mono and run through window function
               //F32 packed = (pFloatData[count*pwfx->nChannels+0])*AUDIO_DATA_GAIN;
               F32 packed = (pFloatData[count*pwfx->nChannels+0] + pFloatData[count*pwfx->nChannels+1])*AUDIO_DATA_GAIN;
               //Con::printf("%.8f",packed);
               windowedMonoData[count] = hanningWindow(packed, count, packetLength);
               //windowedMonoData[count] = packed;
            }

            kiss_fftr_cfg st = kiss_fftr_alloc(AUDIO_FFT_BINS,0,0,0);            
            kiss_fft_cpx* out = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*(AUDIO_FFT_BINS/2+1));
            kiss_fftr(st,windowedMonoData,(kiss_fft_cpx*)out);  
            
            // combine freqs into bands
            U32 bandstep = 0;
            U32 currentfreqbin = 0;
            
            for(U32 count=0; count<(AUDIO_FFT_BINS/2) && bandstep<AUDIO_FREQ_BANDS;){ 
               // update value              
               F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
               summing_buffer[bandstep] += combined;
               
               // increment and ready comparison data
               count++;
               currentfreqbin = (count*pwfx->nSamplesPerSec)/AUDIO_FFT_BINS;
               if(currentfreqbin > _AudioBandFreqs[bandstep]){
                  bandstep++;
               }
            }                               
                                           
            if(windowedMonoData)
               delete windowedMonoData;
            if(st)
               kiss_fft_free(st);
            if(out)
               free(out);
            */
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

      // check for adding/removing LoopbackObjects
      if(loopbackObjectsAccess.acquire(false)){
         for(Vector<LoopbackObject*>::iterator lbObj = loopbackObjectsAdd.begin(); lbObj != loopbackObjectsAdd.end(); ++lbObj){
            loopbackObjects.push_front(*lbObj);
            loopbackObjectsAdd.erase(lbObj);
         }
         for(Vector<LoopbackObject*>::iterator lbObj = loopbackObjectsRemove.begin(); lbObj != loopbackObjectsRemove.end(); ++lbObj){
            loopbackObjects.remove(*lbObj);
            loopbackObjectsRemove.erase(lbObj);
         }

         loopbackObjectsAccess.release();
      }

      // run process on loopback objects here
      for(Vector<LoopbackObject*>::iterator lbObj = loopbackObjects.begin(); lbObj != loopbackObjects.end(); ++lbObj){
         (*lbObj)->preprocess();
      }
      for(Vector<LoopbackObject*>::iterator lbObj = loopbackObjects.begin(); lbObj != loopbackObjects.end(); ++lbObj){
         (*lbObj)->process();
         (*lbObj)->done();
      }

      /*
      // dB stuff
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         //_AudioFreqOutput[count] = 10.0f * (F32)log10(summing_buffer[count]);
         _AudioFreqOutput[count] = summing_buffer[count]; 
      }

      // update output variables      
      for(int count=0; count<AUDIO_FREQ_BANDS; count++){              
         AudioFreqOutput[count].f = _AudioFreqOutput[count];         
      }          
      //Con::printf("%.4f,%.4f",_AudioFreqOutput[0],_AudioFreqOutput[1]);
      //Con::printf("%.4f,%.4f",AudioFreqOutput[0].f,AudioFreqOutput[1].f);
      */
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

/*
void AudioLoopbackThread::run(void *arg)
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
      if(pwfx->nChannels != 2) // need stereo
         hr = -1;
   }else if(pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE){
      PWAVEFORMATEXTENSIBLE pEx = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(pwfx);
      if(IsEqualGUID(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, pEx->SubFormat)){
         if(pwfx->nChannels != 2) // need stereo
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
   
   DWORD nTaskIndex = 0;
   HANDLE hTask = AvSetMmThreadCharacteristics(L"Capture", &nTaskIndex);
   if (hTask == NULL){
      hr = -1;
      AUDIOLB_EXIT_ON_ERROR(hr)
   } 
     

   // format is in pwfx

   // Calculate the actual duration of the allocated buffer.
   hnsActualDuration = (double)REFTIMES_PER_SEC * bufferFrameCount / pwfx->nSamplesPerSec;

   hr = pAudioClient->Start();  // Start recording.
   AUDIOLB_EXIT_ON_ERROR(hr)

   // freq per bin in FFT
   // Fs/N where Fs=sample rate N=FFT width
   // ignore upper half of FFT output   

   F32 summing_buffer[AUDIO_FREQ_BANDS];

   // thread control loop
   while(!checkForStop()){            
      Sleep(hnsActualDuration/REFTIMES_PER_MILLISEC/2);

      // get frequencies per band
         // allows real time updates
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         _AudioBandFreqs[count] = dAtomicRead(AudioBandFreqs[count]);
      }

      hr = pCaptureClient->GetNextPacketSize(&packetLength);
      AUDIOLB_EXIT_ON_ERROR(hr)

      // keep packet length an even number
      packetLength &= 0xFFFFFFFE;      

      // clear summing buffer      
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         summing_buffer[count] = 0.0f;
      }      

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
            F32 *pFloatData = reinterpret_cast<F32*>(pData);
            //Con::printf("packetlength: %d",packetLength);
            F32 *windowedMonoData = new F32[packetLength];
            for(U32 count=0; count<packetLength; count++){
               // repack into mono and run through window function
               //F32 packed = (pFloatData[count*pwfx->nChannels+0])*AUDIO_DATA_GAIN;
               F32 packed = (pFloatData[count*pwfx->nChannels+0] + pFloatData[count*pwfx->nChannels+1])*AUDIO_DATA_GAIN;
               //Con::printf("%.8f",packed);
               windowedMonoData[count] = hanningWindow(packed, count, packetLength);
               //windowedMonoData[count] = packed;
            }

            kiss_fftr_cfg st = kiss_fftr_alloc(AUDIO_FFT_BINS,0,0,0);            
            kiss_fft_cpx* out = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*(AUDIO_FFT_BINS/2+1));
            kiss_fftr(st,windowedMonoData,(kiss_fft_cpx*)out);  
            
            // combine freqs into bands
            U32 bandstep = 0;
            U32 currentfreqbin = 0;
            
            for(U32 count=0; count<(AUDIO_FFT_BINS/2) && bandstep<AUDIO_FREQ_BANDS;){ 
               // update value              
               F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
               summing_buffer[bandstep] += combined;
               
               // increment and ready comparison data
               count++;
               currentfreqbin = (count*pwfx->nSamplesPerSec)/AUDIO_FFT_BINS;
               if(currentfreqbin > _AudioBandFreqs[bandstep]){
                  bandstep++;
               }
            }                                
                                           
            if(windowedMonoData)
               delete windowedMonoData;
            if(st)
               kiss_fft_free(st);
            if(out)
               free(out);
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

      // dB stuff
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         //_AudioFreqOutput[count] = 10.0f * (F32)log10(summing_buffer[count]);
         _AudioFreqOutput[count] = summing_buffer[count]; 
      }

      // update output variables      
      for(int count=0; count<AUDIO_FREQ_BANDS; count++){              
         AudioFreqOutput[count].f = _AudioFreqOutput[count];         
      }          
      //Con::printf("%.4f,%.4f",_AudioFreqOutput[0],_AudioFreqOutput[1]);
      //Con::printf("%.4f,%.4f",AudioFreqOutput[0].f,AudioFreqOutput[1].f);
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
*/

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

/*
// atomic read console function
DefineEngineFunction( getAudioLoopBackFreqs, const char*, (),,
   "Get the frequency information from processing the current AudioLoopBack\n"
   "@param No parameters.\n"
   "@return Frequency band magnitudes in a space delimited string.\n"
   "@ingroup AudioLoopBack" )
{
   // grab data from loopback thread
   F32 audioFreqOutput[AUDIO_FREQ_BANDS];
   F32_U32 tmp;
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){
      // data tranfers as U32           
      tmp.i = dAtomicRead(AudioFreqOutput[count].i);      
      // retrieve from union as F32
      audioFreqOutput[count] = tmp.f;        
   }

   // convert data to string and prepare return buffer
   MemStream tempStream(256);
   char buff[32];
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){
      dSprintf(buff,32,"%.4f ",audioFreqOutput[count]);
      tempStream.writeText(buff);
   }   
   char *ret = Con::getReturnBuffer(tempStream.getStreamSize());
   dStrncpy(ret, (char *)tempStream.getBuffer(), tempStream.getStreamSize()-1);
   ret[tempStream.getStreamSize()-1] = '\0';

   return ret;
}

DefineEngineFunction( getAudioLoopBackBandFreqs, const char*, (),,
   "Get the top frequency of each band from the current AudioLoopBack\n"
   "@param No parameters.\n"
   "@return Top frequency in each bands magnitudes in a space delimited string.\n"
   "@ingroup AudioLoopBack" )
{   
   U32 audioBandFreqs[AUDIO_FREQ_BANDS];
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){
      // data tranfers as U32           
      audioBandFreqs[count] = dAtomicRead(AudioBandFreqs[count]);          
   }
   MemStream tempStream(256);
   char buff[32];
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){
      dSprintf(buff,32,"%d ",audioBandFreqs[count]);
      tempStream.writeText(buff);
   }   
   char *ret = Con::getReturnBuffer(tempStream.getStreamSize());
   dStrncpy(ret, (char *)tempStream.getBuffer(), tempStream.getStreamSize()-1);
   ret[tempStream.getStreamSize()-1] = '\0';

   return ret;
}

DefineEngineFunction( setAudioLoopBackBandFreqs, void, (const char *bandfreqstr),,
   "Set the top frequency of each band for the current AudioLoopBack\n"
   "@param No parameters.\n"
   "@return Top frequency for each band in a space delimited string.\n"
   "@ingroup AudioLoopBack" )
{  
   // create buffer
   char *buff = new char[dStrlen(bandfreqstr)];

   dStrncpy(buff,bandfreqstr,dStrlen(bandfreqstr));
   char *value;
   value = dStrtok(buff, " ");   
   U32 count = 0;
   while(value != NULL && count < AUDIO_FREQ_BANDS){
      U32 tmp = dAtoui(value);
      AudioBandFreqs[count] = tmp;
      count++;
      value = dStrtok(NULL, " ");
   }

   // delete buffer
   delete buff;     
}
*/

/*
DefineEngineFunction( setAudioLoopBackFilters, void, (F32 filter1,F32 filter2,F32 filter3,F32 filter4,F32 filter5),,
   "Get the frequency information from processing the current AudioLoopBackData\n"
   "@param No parameters.\n"
   "@return Frequency band magnitudes in a comma delimited string.\n"
   "@ingroup AudioLoopBack" )
{
   //AudioFilterValues[AUDIO_FREQ_BANDS]
   AudioFilterValues[0].f = filter1;
   AudioFilterValues[1].f = filter2;
   AudioFilterValues[2].f = filter3;
   AudioFilterValues[3].f = filter4;
   AudioFilterValues[4].f = filter5;
}
*/
/*
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
*/

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