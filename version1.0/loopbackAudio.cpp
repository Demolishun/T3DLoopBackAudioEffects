#include "loopbackAudio.h"

#include "console/engineAPI.h"
#include "core/stream/memStream.h"
#include <avrt.h>

#pragma comment(lib, "Avrt.lib")

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
}

void AudioLoopbackThread::run(void *arg /* = 0 */)
{   
   // init vars   
   for(int count=0; count<AUDIO_FREQ_BANDS; count++){      
      AudioFreqOutput[count].f = 0.0f;
      _AudioFreqOutput[count] = 0.0f;      
   }

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

   // thread control loop
   while(!checkForStop()){            
      Sleep(hnsActualDuration/REFTIMES_PER_MILLISEC/2);

      hr = pCaptureClient->GetNextPacketSize(&packetLength);
      AUDIOLB_EXIT_ON_ERROR(hr)

      while (packetLength != 0)
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
            /*
            Con::printf("packetlength: %d",packetLength);
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

HRESULT LoopbackCapture(
    IMMDevice *pMMDevice,
    HMMIO hFile,
    bool bInt16,
    HANDLE hStartedEvent,
    HANDLE hStopEvent,
    PUINT32 pnFrames
);

void audioLoopbackFunction(void *data){  
}

// atomic read console function
DefineEngineFunction( getAudioLoopBackFreqs, const char*, (),,
   "Get the frequency information from processing the current AudioLoopBackData\n"
   "@param No parameters.\n"
   "@return Frequency band magnitudes in a comma delimited string.\n"
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
      dSprintf(buff,32,"%.4f,",audioFreqOutput[count]);
      tempStream.writeText(buff);
   }   
   char *ret = Con::getReturnBuffer(tempStream.getStreamSize());
   dStrncpy(ret, (char *)tempStream.getBuffer(), tempStream.getStreamSize()-1);
   ret[tempStream.getStreamSize()-1] = '\0';

   return ret;
}

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

DefineEngineFunction( startAudioLoopBack, void, (),,
   "Get the frequency information from processing the current AudioLoopBackData\n"
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
   "Get the frequency information from processing the current AudioLoopBackData\n"
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