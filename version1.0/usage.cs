/*
 Define target and texture size.
 Create before any objects that use dynamic texture.
*/
singleton NamedTexTargetObject(MyTexTargetObject) 
{
   targetName = "hellotex";
   texDims = "1024 1024";
};

/*
 Map to material reference in object.
*/
singleton Material(testcube_Hello)
{
   mapTo = "Hello";
   diffuseMap[0] = "#hellotex";
};

/*
 Helper function
*/
function createAudioObjects(){
   warn("Starting up audio loopback visualization...");

   // audio loopback
   startAudioLoopBack();
   if(!isObject($FFTObj)){
      //$FFTObj = new FFTObject(FFTObject1); 
      $FFTObj = new LoopBackObject(LoopBackObject1); 
      addAudioLoopBackObject($FFTObj);
   }   

   // set object on AudioTexObject1
   if(isObject(AudioTexObject1)){
      AudioTexObject1.setAudioObject($FFTObj.getName());
   }else{
      warn("Could not find: AudioTexObject1");
   }  
}

/*
 Example of object that could be put in a mission file, or loaded using other means.
*/
new AudioTextureObject(AudioTexObject1) {
   texture = "hellotex";
   lineTexture = "art/textures/lines_standard.png";
   geomShapeFilename = "art/shapes/rttShapes.dae";
   position = "-3.90218 5.99547 1.92351";
   rotation = "1 0 0 0";
   scale = "1 1 1";
   canSave = "1";
   canSaveDynamicFields = "1";
};