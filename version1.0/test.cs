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
function printAudioLoopBackOutput(){
   echo(getAudioLoopBackFreqs());
      
	$ploopbackschedule = schedule(100,0,printAudioLoopBackOutput);
}

function startLBAudio(){
   startAudioLoopBack();
   schedule(50,0,showBandFreqs);
   printAudioLoopBackOutput();
}

function showBandFreqs(){
   echo(getAudioLoopBackBandFreqs());
}

function stopLBAudio(){   
   stopAudioLoopBack();
   cancel($ploopbackschedule);
}