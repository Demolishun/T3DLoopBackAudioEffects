#include "loopbackAudio.h"

#include "console/engineAPI.h"
#include "core/stream/memStream.h"
//#include <avrt.h>
//#pragma comment(lib, "Avrt.lib")

#include "kiss_fft/kiss_fft.h"
#include "kiss_fft/kiss_fftr.h"

//#include <complex>

/*
The audio frequency data will be divided into freq bands from low to high.
AUDIO_FREQ_BANDS determines the number of bands.
MS_SLEEP_TIME time to sleep between calculations.
AudioFreqOutput is the output buffer that contains the filtered band magnitude data.
AudioFilterValues are the filter values used to calcuate each band progressively using an exponential filter.
*/

Mutex AudioLoopbackThread::loopbackObjectsMutex;
Vector<SimObjectPtr<LoopBackObject>> AudioLoopbackThread::loopbackObjects;

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

   // sane defaults
   U32 freq = 30;
   for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
      AudioBandFreqs[count] = freq;
      //Con::printf("band %d: %d",count,freq);
      freq *= 2;      
   }
   
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){      
      AudioFreqOutput[count].f = 0.0f;
      _AudioFreqOutput[count] = 0.0f;      
   }

   internalSampleData = NULL;
}

AudioLoopbackThread::~AudioLoopbackThread(){
   // free memory
   if(internalSampleData)
      free(internalSampleData);  

   //Con::printf("Deallocating LoopBackObject::sampleBuffer");
   MutexHandle mutex;
   mutex.lock( &LoopBackObject::sampleBufferMutex, true );
   LoopBackObject::sampleBufferSize = 0;
   LoopBackObject::sampleBufferSamples = 0;
   LoopBackObject::samplesPerSecond = 0;
   if(LoopBackObject::sampleBuffer){
      free(LoopBackObject::sampleBuffer);
      LoopBackObject::sampleBuffer = NULL;
   }
   //mutex.unlock();
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
   //U32 sum_divisor = 0;   

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
            //F32 *windowedMonoData = new F32[packetLength];            
            
            for(U32 count=0; count<packetLength; count++){
               // repack into mono and run through window function
               //F32 packed = (pFloatData[count*pwfx->nChannels+0])*AUDIO_DATA_GAIN;
               /*
               F32 packed;
               if(pwfx->nChannels > 1)
                  packed = (pFloatData[count*pwfx->nChannels+0] + pFloatData[count*pwfx->nChannels+1])*AUDIO_DATA_GAIN;
               else
                  packed = (pFloatData[count*pwfx->nChannels+0])*AUDIO_DATA_GAIN;
               */
               //Con::printf("%.8f",packed);        
               internalSampleData[currentindex*AUDIO_NUM_CHANNELS+count*AUDIO_NUM_CHANNELS+0] = pFloatData[count*pwfx->nChannels+0];
               internalSampleData[currentindex*AUDIO_NUM_CHANNELS+count*AUDIO_NUM_CHANNELS+1] = pFloatData[count*pwfx->nChannels+1];
               //windowedMonoData[currentindex+count] = packed;//hanningWindow(packed, count, packetLength);
               //windowedMonoData[count] = packed;
            }            

            /*
            kiss_fftr_cfg st = kiss_fftr_alloc(packetLength,0,0,0);            
            kiss_fft_cpx* out = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*(packetLength/2+1));
            kiss_fftr(st,windowedMonoData,(kiss_fft_cpx*)out);  
            
            // combine freqs into bands
            U32 bandstep = 0;
            U32 currentfreqbin = 0;
            
            for(U32 count=0; count<(packetLength/2) && bandstep<AUDIO_FREQ_BANDS;){ 
               // update value              
               F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
               summing_buffer[bandstep] += combined;
               
               // increment and ready comparison data
               count++;
               currentfreqbin = (count*pwfx->nSamplesPerSec)/packetLength;
               //Con::printf("%d", currentfreqbin);
               if(currentfreqbin > _AudioBandFreqs[bandstep]){
                  bandstep++;
                  //Con::printf("%d", currentfreqbin);
               }
            }

            sum_divisor++;
            */
            
            /*
            for(U32 count=0; count<(AUDIO_FFT_BINS/2); count++){
               U32 resIndex = (count/((AUDIO_FFT_BINS/2)/AUDIO_FREQ_BANDS)) % AUDIO_FREQ_BANDS;
               F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
               //summing_buffer[resIndex] = lowPassFilter(combined, summing_buffer[resIndex], 0.2f);
               summing_buffer[resIndex] += combined;
            } 
            */           
               
            /*                            
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

      // dB stuff
      /*
      if(sum_divisor){
         for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
            F32 sum = summing_buffer[count]/(F32)sum_divisor;
            _AudioFreqOutput[count] = (F32)mLog(sum);
             
            //_AudioFreqOutput[count] = sum; 
         }
      }
      */

      //samplesize &= ~0x1;
      //Con::printf("%d",samplesize);
      if(!samplesize)
         continue;

      // resize external buffer as needed
      // access to the external buffer is controlled by mutex
      MutexHandle mutex;
      mutex.lock( &LoopBackObject::sampleBufferMutex, true );
      if(buffersize > LoopBackObject::sampleBufferSize){
         LoopBackObject::sampleBufferSize = buffersize;
         LoopBackObject::sampleBuffer = (F32 *)realloc(LoopBackObject::sampleBuffer, sizeof(F32)*LoopBackObject::sampleBufferSize*AUDIO_NUM_CHANNELS);    
      }
      // copy sample details
      LoopBackObject::samplesPerSecond = pwfx->nSamplesPerSec;
      LoopBackObject::sampleBufferSamples = samplesize;      
      // copy raw sample data to LoopBackObject buffer
      memcpy(LoopBackObject::sampleBuffer,internalSampleData,sizeof(F32)*samplesize*AUDIO_NUM_CHANNELS);      
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

      // call script function in main thread
      // called too fast and it will cause timing problems
      // 100mS too fast, 200mS okay?
      //const char* argv[] = {"onLoopBackAudioAcquire"};
      //Con::execute(1,argv);
      //const char* argv[] = {"onProcessAudioLoopBack"};
      //Con::execute(1,argv);     

      /*

      samplesize &= ~0x1;
      if(!samplesize)
         continue;

      // window the data
      F32 packed;
      for(U32 count=0; count<samplesize; count++){           
         packed = (internalSampleData[count*AUDIO_NUM_CHANNELS+0] + internalSampleData[count*AUDIO_NUM_CHANNELS+1])*AUDIO_DATA_GAIN;       
         internalSampleData[count] = hanningWindow(packed, count, samplesize);         
      }

      kiss_fftr_cfg st = kiss_fftr_alloc(samplesize,0,0,0);            
      kiss_fft_cpx* out = (kiss_fft_cpx*)malloc(sizeof(kiss_fft_cpx)*(samplesize/2+1));
      kiss_fftr(st,internalSampleData,(kiss_fft_cpx*)out);  
      
      // combine freqs into bands
      U32 bandstep = 0;
      U32 currentfreqbin = 0;
      
      for(U32 count=0; count<(samplesize/2) && bandstep<AUDIO_FREQ_BANDS;){ 
         // update value              
         F32 combined = out[count].r * out[count].r + out[count].i * out[count].i;
         summing_buffer[bandstep] += combined;
         
         // increment and ready comparison data
         count++;
         currentfreqbin = (count*pwfx->nSamplesPerSec)/samplesize;
         //Con::printf("%d", currentfreqbin);
         if(currentfreqbin > _AudioBandFreqs[bandstep]){
            bandstep++;
            //Con::printf("%d", currentfreqbin);
         }
      }

      if(st)
         kiss_fft_free(st);
      if(out)
         free(out);

      //static F32 lowest = 0;
      for(U32 count=0; count<AUDIO_FREQ_BANDS; count++){
         F32 sum = summing_buffer[count];
         F32 logged = (F32)mLog(sum);
         //if(logged < lowest)
         //   lowest = logged;
         //_AudioFreqOutput[count] = logged;// + mFabs(lowest);
         _AudioFreqOutput[count] = lowPassFilter(logged,_AudioFreqOutput[count],0.5f);
          
         //_AudioFreqOutput[count] = sum; 
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

// static members
Mutex LoopBackObject::sampleBufferMutex;
F32 *LoopBackObject::sampleBuffer = NULL;
U32 LoopBackObject::sampleBufferSize = 0;
U32 LoopBackObject::sampleBufferSamples = 0;
U32 LoopBackObject::samplesPerSecond = 0;
// set to keep track of objects
//Mutex LoopBackObject::loopbackObjectsMutex;
//Vector<LoopBackObject*> LoopBackObject::loopbackObjects;

// console defs
IMPLEMENT_CONOBJECT(LoopBackObject);

// functions
LoopBackObject::LoopBackObject(){
   objectSampleFilter = 0.2f;
   objectSampleBufferSize = 0;
   objectSampleBufferSamples = 0;
   objectSampleBuffer = NULL;

   /*
   MutexHandle mutex;
   mutex.lock( &LoopBackObject::loopbackObjectsMutex, true );
   LoopBackObject::loopbackObjects.push_back(this);
   */
}
LoopBackObject::~LoopBackObject(){
   /*
   MutexHandle mutex;
   mutex.lock( &LoopBackObject::loopbackObjectsMutex, true );
   LoopBackObject::loopbackObjects.remove(this);   
   */
}

/*
void LoopBackObject::processLoopBack(){ 
   Vector<LoopBackObject*>::iterator i; 

   MutexHandle mutex;
   mutex.lock( &loopbackObjectsMutex, true );   

   // if the set exists   
   for(i = loopbackObjects.begin(); i != loopbackObjects.end(); i++)  
   {  
       LoopBackObject *obj = (LoopBackObject *)(*i);  
         
       obj->process();
   }      
}
*/

void LoopBackObject::process(){
   //Con::printf("LoopBackObject::process() - Processing audio data: %d",this->getId());
   
   // get global sample buffer
   MutexHandle mutex;
   mutex.lock( &sampleBufferMutex, true );   

   // get object sample buffer
   MutexHandle objectMutex;
   objectMutex.lock( &objectSampleBufferMutex, true );

   // resize buffer as needed
   if(sampleBufferSize > objectSampleBufferSize){
      //U32 diff = sampleBufferSize - objectSampleBufferSize;
      objectSampleBufferSize = sampleBufferSize;
      objectSampleBuffer = (F32 *)realloc(objectSampleBuffer, sizeof(F32)*objectSampleBufferSize*AUDIO_NUM_CHANNELS);          
      // init new memory
      /*
      for(U32 count=(objectSampleBufferSize-diff)*AUDIO_NUM_CHANNELS; count < objectSampleBufferSize*AUDIO_NUM_CHANNELS; count++){
         objectSampleBuffer[count] = 0.0f;            
      }
      */
   }
   objectSampleBufferSamples = sampleBufferSamples;
   /*
   // filter sample data using lowPassFilter - not a good idea
   for(U32 count=0; count < objectSampleBufferSamples*AUDIO_NUM_CHANNELS; count++){
      objectSampleBuffer[count] = lowPassFilter(sampleBuffer[count],objectSampleBuffer[count],objectSampleFilter);
   } 
   */  
   // copy sample data
   memcpy(objectSampleBuffer,sampleBuffer,sizeof(F32)*objectSampleBufferSamples*AUDIO_NUM_CHANNELS);   

   // done with global sample buffer
   mutex.unlock();

   // now perform additional processing on sample data
   // keeps from needing to reacquire mutex or call additional functions
   process_unique();

   // release object mutex
   objectMutex.unlock();   
}

// FFT Object
IMPLEMENT_CONOBJECT(FFTObject);

FFTObject::FFTObject(){
   // sane defaults
   U32 freq = 60;
   for(U32 count=0; count < 9; count++){
      AudioFreqBands.push_back(freq);
      freq *= 2;
   }
   AudioFreqOutput.setSize(AudioFreqBands.size());
   AudioFreqOutput.fill(0.0f);
}
FFTObject::~FFTObject(){
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
      currentfreqbin = (count*samplesPerSecond)/samplesize;      
      U32 tempFreq;

      // discriminate frequencies as a center freq for each band
      if(bandstep != AudioFreqBands.size()-1){
         tempFreq = (AudioFreqBands[bandstep]+AudioFreqBands[bandstep+1])/2;
      }else{
         tempFreq = AudioFreqBands[bandstep]+AudioFreqBands[bandstep]/2;
      }
      if(currentfreqbin > tempFreq){ 
         // divide magnitude of each band by the number of bins that it took to build the band          
         summing_buffer[bandstep] /= (F32)binsperband;
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
   "@return Top frequency for each band in a space/comma delimited string.\n"
   "@ingroup AudioLoopBack" )
{  
   char *buff = new char[dStrlen(bandfreqstr)];
   dStrncpy(buff,bandfreqstr,dStrlen(bandfreqstr));
   char *value;
   value = dStrtok(buff, " ,");   
   U32 count = 0;
   while(value != NULL && count < AUDIO_FREQ_BANDS){
      U32 tmp = dAtoui(value);
      AudioBandFreqs[count] = tmp;
      count++;
      value = dStrtok(NULL, " ,");
   }

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