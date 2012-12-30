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
   %obj = FreqPlot1; 
   %freqs = getAudioLoopBackFreqs();
   echo(%freqs);
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

$logo_pos = MainMenuAppLogo.position;
$play_pos = PlayButton.position;
$options_pos = OptionsButton.position;
$exit_pos = ExitButton.position;
$haspos = true;

function startLBAudio(){
   startAudioLoopBack();
   schedule(50,0,showBandFreqs);
   plotAudioLoopBackOutput();
}

function showBandFreqs(){
   echo(getAudioLoopBackBandFreqs());
}

function stopLBAudio(){   
   stopAudioLoopBack();
   cancel($ploopbackschedule);
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