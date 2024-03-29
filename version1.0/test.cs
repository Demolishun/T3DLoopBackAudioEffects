/*
if(!isObject(ObjSet)){
	new SimSet(ObjSet){
		new SimObject();
		new SimObject();
		new SimObject();
	};
}

function testfunc1(%parm1){
	%num = %parm1;
	%num2 = 2;
	%output = %num%%num2; 
	echo(%output);
	%output = %num % 5;
	echo(%output);

	%one = "hello";
	%two = "world";
	echo("hello"SPC"world");
	echo(%one SPC%two);

	%string = "single line";
	%stringml = "no muli line"; 

	%string = "";
	foreach(%obj in ObjSet){
		%string = %string SPC %obj.getId();
	}
	echo(%string);

	foreach$(%str in trim(%string)){
		echo(%str);
	}

	$string1 = "one" SPC "two";
	$string2 = "three" SPC "four";
}
*/

cancel($ploopbackschedule);

if(getVariable("$haspos") !$= ""){
   MainMenuAppLogo.position = $logo_pos;
   PlayButton.position = $play_pos;
   OptionsButton.position = $options_pos;
   ExitButton.position = $exit_pos;
}

function plotAudioLoopBackOutput(){   
   if(!isObject($FFTObj)){
      warn("plotAudioLoopBackOutput() - No FFTObj, stopping schedule.");
      $ploopbackschedule = "";
      return;
   }
   
   %obj = FreqPlot1; 
   //%freqs = getAudioLoopBackFreqs();
   %freqs = $FFTObj.getAudioFreqOutput();
   //echo("old:" SPC %freqs);
   //echo("new:" SPC %freqs);
   %freqsNormalized = "";
   for(%count=0; %count<getWordCount(%freqs); %count++){                   
      $lb_band[%count] = getWord(%freqs,%count);
      //%freqsNormalized = %freqsNormalized SPC $lb_band[%count]/500.0;
      if($lb_band[%count] < 0.0)
         %freqsNormalized = %freqsNormalized SPC "0.0";
      else
         %freqsNormalized = %freqsNormalized SPC ($lb_band[%count]-0.0)/12.0;
      
      if(%count<6){
         FreqPlot1.addDatum(%count,$lb_band[%count]);
      }else{         
         FreqPlot2.addDatum(%count-6,$lb_band[%count]);
      }
   }
   
   %freqsNormalized = trim(%freqsNormalized);
   //echo(%freqsNormalized);
   
   if(isObject(Tele60)){
      
   }
   
   if(isObject(PESound60) && isObject(PESound60Holder) && 0){
      if(PESound60.orgvel $= ""){         
         PESound60.orgvel = PESound60.velocity;
         PESound60Holder.orgpos = PESound60Holder.getposition();
      }      
      
      %freq60 = getWord(%freqsNormalized,1);
      PESound60.velocity = PESound60.orgvel+%freq60*5;
      if(%freqsNormalized > 0.25){
         PESound60.setActive(true);
      }else{
         PESound60.setActive(false);
      }

      //echo(PESound60Holder.orgpos);    
      
      %x = getWord(PESound60Holder.orgpos,0);
      %y = getWord(PESound60Holder.orgpos,1);   
      %z = getWord(PESound60Holder.orgpos,2);
      //%newpos =          
      //PESound60Holder.position = %x SPC %y SPC %z+%freq60;
      //echo(PESound60Holder.position);
   }
   
   slider0.setValue(getWord(%freqsNormalized,0));
   slider1.setValue(getWord(%freqsNormalized,1));
   slider2.setValue(getWord(%freqsNormalized,2));
   slider3.setValue(getWord(%freqsNormalized,3));
   slider4.setValue(getWord(%freqsNormalized,4));
   slider5.setValue(getWord(%freqsNormalized,5));
   slider6.setValue(getWord(%freqsNormalized,6));
   slider7.setValue(getWord(%freqsNormalized,7));
   slider8.setValue(getWord(%freqsNormalized,8));
   
   Filter1.setValue(%freqsNormalized);
   
   // tweak gui elements
   
   // logo   
   %x = getWord($logo_pos,0);
   %y = getWord($logo_pos,1);
   MainMenuAppLogo.position = %x-(getWord(%freqsNormalized,1)*50) SPC %y+(getWord(%freqsNormalized,0)*50);
   
   %x = getWord($play_pos,0);
   %y = getWord($play_pos,1);
   PlayButton.position = %x-(getWord(%freqsNormalized,3)*50) SPC %y;
   
   %x = getWord($options_pos,0);
   %y = getWord($options_pos,1);
   OptionsButton.position = %x-(getWord(%freqsNormalized,4)*50) SPC %y;
   
   %x = getWord($exit_pos,0);
   %y = getWord($exit_pos,1);
   ExitButton.position = %x-(getWord(%freqsNormalized,5)*50) SPC %y;
         
	$ploopbackschedule = schedule(100,0,plotAudioLoopBackOutput);
}

function PESound60Holder::onAdd(%data, %this){
   echo(%data SPC %this);
}

$logo_pos = MainMenuAppLogo.position;
$play_pos = PlayButton.position;
$options_pos = OptionsButton.position;
$exit_pos = ExitButton.position;
$haspos = true;

function startLBAudio(){
   startAudioLoopBack();   
   
   if(!isObject($FFTObj)){
      $FFTObj = new FFTObject(); 
      addAudioLoopBackObject($FFTObj);
   }
   
   if($ploopbackschedule $= ""){
      plotAudioLoopBackOutput();
   }else{
      warn("startLBAudio() - Loop Back Audio already running.");
   }
   
   if(!isObject($LBGroup)){
      $LBGroup = new SimGroup();
      
      if(1){
         for(%count=0; %count<10; %count++){
            %obj = new FFTObject();
            $LBGroup.add(%obj);
            addAudioLoopBackObject(%obj);
         }
      }
      
      // stress test the destructor and removing from object list
      if(1){
         for(%count=0; %count<1000; %count++){
            %obj = new LoopBackObject();
            $LBGroup.add(%obj);
            addAudioLoopBackObject(%obj);
         }
      }
      
   }
}

function stopLBAudio(){   
   //stopAudioLoopBack();
   
   cancel($ploopbackschedule);
   $ploopbackschedule = "";
   
   if(isObject($FFTObj)){
      $FFTObj.delete();
   }
   
   if(isObject($LBGroup)){
      $LBGroup.delete();
   }
   
   //stopAudioLoopBack();
}

// create loopback plot vars
// FreqPlot1
$lb_band[0] = 0.0;
$lb_band[1] = 0.0;
$lb_band[2] = 0.0;
$lb_band[3] = 0.0;
$lb_band[4] = 0.0;
$lb_band[5] = 0.0;
// FreqPlot2
$lb_band[6] = 0.0;
$lb_band[7] = 0.0;
$lb_band[8] = 0.0;

FreqPlot1.addDatum(0,0.0);
FreqPlot1.addDatum(1,0.0);
FreqPlot1.addDatum(2,0.0);
FreqPlot1.addDatum(3,0.0);
FreqPlot1.addDatum(4,0.0);
FreqPlot1.addDatum(5,0.0);

FreqPlot2.addDatum(0,0.0);
FreqPlot2.addDatum(1,0.0);
FreqPlot2.addDatum(2,0.0);

/*
FreqPlot1.addAutoPlot(0, "$lb_band[0]", 100);
FreqPlot1.addAutoPlot(1, "$lb_band[1]", 100);
FreqPlot1.addAutoPlot(2, "$lb_band[2]", 100);
FreqPlot1.addAutoPlot(3, "$lb_band[3]", 100);
FreqPlot1.addAutoPlot(4, "$lb_band[4]", 100);
FreqPlot1.addAutoPlot(5, "$lb_band[5]", 100);

FreqPlot2.addAutoPlot(0, "$lb_band[6]", 100);
FreqPlot2.addAutoPlot(1, "$lb_band[7]", 100);
FreqPlot2.addAutoPlot(2, "$lb_band[8]", 100);
*/
/*
FreqPlot1.setActive();
FreqPlot2.setActive();
*/

function onLoopBackAudioAcquire(){
   plotAudioLoopBackOutput();
}

function makeLoopBackObjects(){
   %obj = new FFTObject();   
   echo("Output:" SPC %obj.getAudioFreqOutput());
   echo("Bands:" SPC %obj.getAudioFreqBands());
   /*
   %obj.setAudioFreqBands("5,10,20,40");
   echo("Output:" SPC %obj.getAudioFreqOutput());
   echo("Bands:" SPC %obj.getAudioFreqBands());
   
   %obj.setAudioFreqBands("10,20,40,80,160,320,640,1280,2560,5120,10240,20480");
   echo("Output:" SPC %obj.getAudioFreqOutput());
   echo("Bands:" SPC %obj.getAudioFreqBands());
   */
}