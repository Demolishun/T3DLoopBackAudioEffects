#include "audioTextureObject.h"

#include "console/engineAPI.h"
#include "core/stream/bitStream.h"
#include "core/resourceManager.h"

#include "math/mathIO.h"
#include "math/mathUtils.h"

#include "materials/materialManager.h"
#include "materials/baseMatInstance.h"

#include "gfx/gfxTransformSaver.h"
#include "gfx/gfxDrawUtil.h"
#include "gfx/gFont.h"
#include "gfx/gfxDebugEvent.h"

#include "scene/sceneRenderState.h"
#include "renderInstance/renderPassManager.h"
#include "ts/tsShapeInstance.h"

extern bool gEditingMission;

IMPLEMENT_CO_NETOBJECT_V1(AudioTextureObject);

AudioTextureObject::AudioTextureObject(){
   // Flag this object so that it will always
   // be sent across the network to clients
   mNetFlags.set( Ghostable | ScopeAlways );
   //mNetFlags.set( ScopeAlways );

   mTypeMask |= StaticObjectType | StaticShapeObjectType;

   mTextureTarget = NULL;   

   mTexture = NULL; 

   mProfile = NULL;

   //mGeomShapeInstance = NULL;

   // generate UV coords for line drawing
   /*
      Texture coords:
      0.0,     0.125
      0.0,     0.0
      0.125,   0.125
      0.125,   0.0
      0.250,   0.125
      0.250,   0.0
      0.375,   0.125
      0.375,   0.0
   */   
   mUVCoords.setSize(64);
   F32 inc = 1.0f/8.0f;
   for(U32 i=0; i < 8; i++){
      F32 fi = F32(i>>1);
      F32 yoff = i%2 ? 0.0f : inc;
      mUVCoords[i].set(fi*inc, yoff);
   }
   for(U32 i=8; i < 64; i++){
      //U32 yflag = i%2 ? 1 : 0;
      //F32 yoff = F32((i>>3)+yflag)*inc;
      F32 yoff = F32(i>>3)*inc+mUVCoords[i%8].y;
      mUVCoords[i].set(mUVCoords[i%8].x, yoff);
   }
   /*
   for(U32 i=0; i < 64; i++){
      Con::warnf("%.4f,%.4f",mUVCoords[i].x,mUVCoords[i].y);
   }
   */
}
AudioTextureObject::~AudioTextureObject(){   
}

void AudioTextureObject::initPersistFields(){
   addGroup( "Rendering" );
   /*addField( "material",      TypeMaterialName, Offset( mMaterialName, AudioTextureObject ),
      "" );*/
   addField( "texture",      TypeRealString, Offset( mTextureName, AudioTextureObject ),
      "The target render texture." );
   addField( "lineTexture",      TypeImageFilename, Offset( mLineTextureName, AudioTextureObject ),
      "The target render texture." );
   addField( "geomShapeFilename",      TypeStringFilename, Offset( mGeomShapeFileName, AudioTextureObject ),
      "The path to the geometry DTS shape file." );

   endGroup( "Rendering" );
   addGroup("Control");   
   //addField("profile", TYPEID< GuiControlProfile >(), Offset(mProfile, AudioTextureObject));
   addField("profileName", TypeRealString, Offset(mProfileName, AudioTextureObject));
   endGroup("Control");

   // SceneObject already handles exposing the transform
   Parent::initPersistFields();
}

void AudioTextureObject::inspectPostApply()
{
   Parent::inspectPostApply();

   // Flag the network mask to send the updates
   // to the client object
   setMaskBits( UpdateMask );
}

bool AudioTextureObject::onAdd(){
   if ( !Parent::onAdd() )
      return false;

   // setup material
   if( isClientObject() ){
      updateMaterial();
   }      

   // Set up a 1x1x1 bounding box
   mObjBox.set( Point3F( -0.5f, -0.5f, -0.5f ),
                Point3F(  0.5f,  0.5f,  0.5f ) );

   resetWorldBox();

   // Add this object to the scene
   addToScene();

   return true;
}
void AudioTextureObject::onRemove(){
   // Remove this object from the scene
   removeFromScene();

   if(mWarningTexture)
      SAFE_DELETE(mWarningTexture);   

   // profile management
   if ( mProfile )
   {
      mProfile->unregisterReference((SimObject**)&mProfile);
      mProfile = NULL;
   }

   mLineTexture.free();

   // Remove our TSShapeInstance
   //if ( mGeomShapeInstance )
   //   SAFE_DELETE( mGeomShapeInstance );

   Parent::onRemove();
}

void AudioTextureObject::setTransform( const MatrixF &mat ){
   // Let SceneObject handle all of the matrix manipulation
   Parent::setTransform( mat );

   // Dirty our network mask so that the new transform gets
   // transmitted to the client object
   setMaskBits( TransformMask );
}

U32 AudioTextureObject::packUpdate( NetConnection *conn, U32 mask, BitStream *stream ){
   // Allow the Parent to get a crack at writing its info
   U32 retMask = Parent::packUpdate( conn, mask, stream );

   // Write our transform information
   if ( stream->writeFlag( mask & TransformMask ) )
   {
      mathWrite(*stream, getTransform());
      mathWrite(*stream, getScale());
   }

   // Write out any of the updated editable properties
   if ( stream->writeFlag( mask & UpdateMask ) ){
      stream->write( mTextureName );
      stream->write( mProfileName );
      stream->write( mLineTextureName );      
      //stream->write( mGeomShapeFileName );   
      if(mLoopBackObject){
         stream->writeFlag(true);  
         stream->write( String(mLoopBackObject->getName()) );      
      }else{
         stream->writeFlag(false);
      }
   }

   return retMask;
}

void AudioTextureObject::unpackUpdate( NetConnection *conn, BitStream *stream ){
   // Let the Parent read any info it sent
   Parent::unpackUpdate(conn, stream);

   if ( stream->readFlag() )  // TransformMask
   {
      mathRead(*stream, &mObjToWorld);
      mathRead(*stream, &mObjScale);

      setTransform( mObjToWorld );
   }

   if ( stream->readFlag() )  // UpdateMask
   {
      stream->read( &mTextureName );
      stream->read( &mProfileName );
      stream->read( &mLineTextureName );      
      //stream->read( &mGeomShapeFileName );
      if( stream->readFlag() )
         stream->read( &mLoopBackObjectName );
   
      if ( isProperlyAdded() )       
         updateMaterial();      
   }
}

// search for TSLastDetail::render to find info on possible engine support of billboards
void AudioTextureObject::createGeometry(){

   U32 numPoints = 12;
   
   static const Point3F cubePoints[4] = 
   {
      Point3F( -1.0f, 0.0f, -1.0f), Point3F( -1.0f, 0.0f,  1.0f),
      Point3F( 1.0f,  0.0f, 1.0f), Point3F( 1.0f,  0.0f,  -1.0f)      
   };

   static const Point3F cubeNormals[2] = 
   {
      Point3F( 0.0f, 1.0f, 0.0f), Point3F(0.0f, -1.0f, 0.0f)
   };

   static const ColorI cubeColors[4] = 
   {
      ColorI( 128,   128,   128, 255 ),
      ColorI( 255,   128,   128, 255 ),     
      ColorI( 128,   255,   128, 255 ),
      ColorI( 128,   128,   255, 255 ),
   };

   static const Point2F cubeTexCoords[4] = 
   {
      Point2F( 0,  0), Point2F( 0, 1),
      Point2F( 1,  0), Point2F( 1, 1)
   };

   // for color
   /*
   static const U32 cubeFaces[12][3] = 
   {
      { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 },
      { 2, 0, 1 }, { 3, 0, 1 }, { 0, 0, 1 },
      { 2, 1, 2 }, { 1, 1, 2 }, { 0, 1, 2 },
      { 2, 1, 3 }, { 0, 1, 3 }, { 3, 1, 3 },
   };
   */  
   // for texture
   static const U32 cubeFaces[12][3] = 
   {
      { 0, 0, 1 }, { 1, 0, 0 }, { 2, 0, 2 },
      { 2, 0, 2 }, { 3, 0, 3 }, { 0, 0, 1 },
      { 3, 1, 1 }, { 2, 1, 0 }, { 1, 1, 2 },
      { 1, 1, 2 }, { 0, 1, 3 }, { 3, 1, 1 },
   }; 

   // Fill the vertex buffer
   VertexType *pVert = NULL;
   
   mVertexBuffer.set( GFX, numPoints, GFXBufferTypeStatic );
   //mVertexBuffer.set( GFX, numPoints, GFXBufferTypeDynamic );
   pVert = mVertexBuffer.lock();

   Point3F halfSize = getObjBox().getExtents() * 0.5f;
   
   for (U32 i = 0; i < numPoints; i++)
   {
      const U32& vdx = cubeFaces[i][0];
      const U32& ndx = cubeFaces[i][1];
      //const U32& cdx = cubeFaces[i][2];
      const U32& tdx = cubeFaces[i][2];

      pVert[i].point  = cubePoints[vdx] * halfSize;
      pVert[i].normal = cubeNormals[ndx];
      //pVert[i].color  = cubeColors[cdx];
      pVert[i].texCoord  = cubeTexCoords[tdx];
   }

   mVertexBuffer.unlock();

   // Set up our normal and reflection StateBlocks   
   GFXStateBlockDesc desc;
   /*
   desc.setCullMode( GFXCullCCW );
   desc.setBlend( true );
   desc.setZReadWrite( false, false );
   desc.samplersDefined = true;
   desc.samplers[0].addressModeU = GFXAddressWrap;
   desc.samplers[0].addressModeV = GFXAddressWrap;
   desc.samplers[0].addressModeW = GFXAddressWrap;
   desc.samplers[0].magFilter = GFXTextureFilterLinear;
   desc.samplers[0].minFilter = GFXTextureFilterLinear;
   desc.samplers[0].mipFilter = GFXTextureFilterLinear;
   desc.samplers[0].textureColorOp = GFXTOPModulate;
   //desc.samplers[0].textureColorOp = GFXTOPAdd;
   */

   //desc.samplers[0].textureColorOp = GFXTOPModulate;  

   // DrawBitmapStretchSR   
   desc.setCullMode(GFXCullCCW);
   desc.setZReadWrite(false);
   desc.setBlend(true, GFXBlendSrcAlpha, GFXBlendInvSrcAlpha);
   desc.samplersDefined = true;

   // Linear: Create wrap SB
   //desc.samplers[0] = GFXSamplerStateDesc::getWrapLinear();
   //mBitmapStretchWrapLinearSB = mDevice->createStateBlock(bitmapStretchSR);

   // Linear: Create clamp SB
   desc.samplers[0] = GFXSamplerStateDesc::getClampLinear();
   //mBitmapStretchLinearSB = mDevice->createStateBlock(bitmapStretchSR);

   // Point:
   desc.samplers[0].minFilter = GFXTextureFilterPoint;
   desc.samplers[0].mipFilter = GFXTextureFilterPoint;
   desc.samplers[0].magFilter = GFXTextureFilterPoint;

   // Point: Create clamp SB, last created clamped so no work required here
   //mBitmapStretchSB = mDevice->createStateBlock(bitmapStretchSR);    

   // The normal StateBlock only needs a default StateBlock
   mNormalSB = GFX->createStateBlock( desc );

   // The reflection needs its culling reversed
   desc.cullDefined = true;
   desc.cullMode = GFXCullCW;
   mReflectSB = GFX->createStateBlock( desc );   

   desc.cullDefined = true;
   desc.cullMode = GFXCullNone;
   mNoCullSB = GFX->createStateBlock( desc ); 

   // get shader
   if(1){
      ShaderData *shaderData;
      String sName;
      sName = "TestShader";
      mShader = Sim::findObject( sName.c_str(), shaderData ) ? shaderData->getShader() : NULL; 
      if(mShader.isNull()){
         Con::warnf("Error using shader: %s", sName.c_str());
      }else{
         //mShader->registerResourceWithDevice(GFX);      
      }
   }
}

void AudioTextureObject::updateMaterial()
{   
   //BaseMatInstance* tmpMat = MATMGR->createMatInstance( tmpMatName, getGFXVertexFormat< VertexType >() );   
   if(mWarningTexture.isNull()){
      BaseMatInstance* tmpMat = MATMGR->createWarningMatInstance();
      if(tmpMat){
         const char* tmpTexName = tmpMat->getMaterial()->getDataField(StringTable->insert("diffuseMap"),"0");
         if(tmpTexName){
            mWarningTexture.set(String(tmpTexName),&GFXDefaultStaticDiffuseProfile,"");                                              
            //Con::warnf("AudioTextureObject::updateMaterial - setting WarningMaterial texture: %s",tmpTexName);
         }  
    
         // get rid of temp material instance
         SAFE_DELETE( tmpMat );
      }
   }    

   if(mTextureName.isNotEmpty()){      
      
      if(mTextureTarget && !mTextureName.equal( mTextureTarget->getName(), String::NoCase )){
         mTextureTarget = NULL;
      }

      if(!mTextureTarget){
         // find texture target with this name
         mTextureTarget = NamedTexTarget::find(mTextureName);
         if(!mTextureTarget)
            Con::errorf("AudioTextureObject::updateMaterial - could not find texture target: %s.",mTextureName.c_str());
      }            
     
      if(mTextureTarget){     
         mTexture = mTextureTarget->getTexture();
      }else{
         mTexture = NULL;
      }   
      
   }else{
      mTextureTarget = NULL;
      mTexture = NULL;      
   }   

   if(mProfileName.isNotEmpty()){
      GuiControlProfile *profile = NULL;
      if ( Sim::findObject( mProfileName, profile ) ){
         //setControlProfile( profile ); 
         if(mProfile){
            mProfile->unregisterReference((SimObject**)&mProfile);
            mProfile = NULL;
         }
         
         if ( profile != mProfile ){
            mProfile = profile;
            mProfile->registerReference((SimObject**)&mProfile);
         }
      }
   }else{
      if(mProfile){
         mProfile->unregisterReference((SimObject**)&mProfile);
         mProfile = NULL;
      }
   } 

   if(mLineTextureName.isNotEmpty()){
      mLineTexture.set(mLineTextureName, &GFXDefaultStaticDiffuseProfile, String("Line Texture"));
   }

   if(mLoopBackObjectName.isNotEmpty()){
      Con::warnf("AudioTextureObject::updateMaterial : %s",mLoopBackObjectName.c_str());
      SimObject* tmpObj = Sim::findObject(mLoopBackObjectName.c_str());
      LoopBackObject* tmpLBObj = dynamic_cast<LoopBackObject*>(tmpObj);      
      mLoopBackObject = tmpLBObj;      

      if(!mLoopBackObject){
         Con::warnf("AudioTextureObject::updateMaterial : loopbackobject not found.");
      }
   }

   /*
   if(!mGeomShapeFileName.isEmpty()){
      // if name does not match
      if (mGeomShapeInstance && !mGeomShapeFileName.equal( mGeomShapeResource.getPath().getFullPath(), String::NoCase )){
         // get rid of old shape
         if ( mGeomShapeInstance )
            SAFE_DELETE( mGeomShapeInstance );
         mGeomShapeResource = NULL;
      }
      if (!mGeomShapeInstance){
         // Attempt to get the resource from the ResourceManager
         mGeomShapeResource = ResourceManager::get().load( mGeomShapeFileName );

         if (mGeomShapeResource){
            // Attempt to preload the Materials for this shape
            if ( mGeomShapeResource->preloadMaterialList(mGeomShapeResource.getPath())){
               // Create the TSShapeInstance
               mGeomShapeInstance = new TSShapeInstance( mGeomShapeResource, isClientObject() );
            }else{
               mGeomShapeResource = NULL;               
               //Con::errorf( "AudioTextureObject::updateMaterial() - No preloadMaterialList: %s", mGeomShapeFileName.c_str() );
            }
            
            // testing: list meshes in object
            if(mGeomShapeInstance){
               mGeomShapeInstance->listMeshes(String("All"));              
            }
         }else{
            Con::errorf( "AudioTextureObject::updateMaterial() - Unable to load shape: %s", mGeomShapeFileName.c_str() );
            return;
         }
      }      
   }
   */

   /*
   if( mMaterialName.isEmpty() )
      return;

   // If the material name matches then don't bother updating it.
   if ( mMaterialInst && mMaterialName.equal( mMaterialInst->getMaterial()->getName(), String::NoCase ) )
      return;

   SAFE_DELETE( mMaterialInst );

   mMaterialInst = MATMGR->createMatInstance( mMaterialName, getGFXVertexFormat< VertexType >() );
   if ( !mMaterialInst )
      Con::errorf( "AudioTextureObject::updateMaterial - no Material called '%s'", mMaterialName.c_str() );   

   mTextureName = mMaterialInst->getMaterial()->getDataField(StringTable->insert("diffuseMap"),"0");   
   //Con::warnf("AudioTextureObject::updateMaterial - Texture name '%s'",mTextureName.c_str());
   */
}

void AudioTextureObject::prepRenderImage( SceneRenderState *state ){
   //Con::warnf("AudioTextureObject::prepRenderImage - called");

   // Do a little prep work if needed
   if ( mVertexBuffer.isNull() )
      createGeometry();   

   // do RTT here
   // if mTexture is not valid there is no valid target
   // check for data changed from the data source

   /*
   if(isClientObject())
      Con::printf("Running on client");
   else
      Con::printf("Running on server");
   */

   Vector<F32> sourceData;
   U32 changed=0;
   LoopBackObject* tmpObj = mLoopBackObject.getObject();
   if(tmpObj){
      changed = mLoopBackObject->getAudioOutput(sourceData);
   }else{
      //Con::printf("No object to get data from.");
   }
   if(mTexture && sourceData.size() && changed != mLoopBackObjectChanged){
      mLoopBackObjectChanged = changed;

      static F32 texrot = 0.0f;
      
      // save render state 
      GFXTransformSaver subsaver;     

      // prepare render texture
      mGFXTextureTarget = GFX->allocRenderToTextureTarget();        
      mGFXTextureTarget->attachTexture(GFXTextureTarget::Color0,mTexture);
      // save render target and set new render target
      GFX->pushActiveRenderTarget();      
      GFX->setActiveRenderTarget(mGFXTextureTarget);
      
      /*      
      GFX->setWorldMatrix(MatrixF::Identity);   
      GFX->setProjectionMatrix(MatrixF::Identity);
      
      MatrixF newproj(EulerF(0.0,0.0,0.0),Point3F(0.0,0.0,-10.0));
      MatrixF newtrans = GFX->getWorldMatrix();
      newtrans = newtrans.set(EulerF(0.0f,0.0f,texrot));  
      texrot += 0.001f;
      if(texrot >= 360.0f)
         texrot = 0.0f;   
      newtrans.setColumn(3, Point3F(0.0f,0.0f,0.0f)); 
      
      MatrixF newview = MatrixF::Identity;
      newview.setColumn(3, Point3F(0.0f,1.0f,0.0f));
      GFX->setViewMatrix(newview);   
      */
      
      
      //GFX->setWorldMatrix(MatrixF::Identity); 
      //GFX->setProjectionMatrix(MatrixF::Identity);
      //GFX->setViewMatrix(MatrixF::Identity);
                
      F32 left, right, top, bottom;
      F32 fnear = 0.01f;
      F32 ffar = -10.0f;
      MathUtils::makeFrustum( &left, &right, &top, &bottom, M_HALFPI_F, 1.0f, fnear );
      //Con::printf("%f,%f,%f,%f",left,right,top,bottom);
      Frustum tmpFrust = GFX->getFrustum();
      GFX->setFrustum( left, right, bottom, top, fnear, ffar );                       
     
      // clear the texture out
      //GFX->clear(GFXClearTarget|GFXClearZBuffer|GFXClearStencil,ColorI(0,0,0),1.0f,0);            
      GFX->clear(GFXClearTarget,ColorI(0,0,0),1.0f,0);
      GFX->setStateBlock( mNormalSB );
      //GFX->setVertexBuffer( mVertexBuffer );
      //GFX->setTexture(0, mWarningTexture);     
      GFX->setupGenericShaders( GFXDevice::GSModColorTexture );        

      //GFX->setWorldMatrix(MatrixF::Identity);
      //GFX->multWorld(newtrans);

      if(mShader.isValid()){         
         //GFX->setShader(mShader);
      }
      //GFX->drawPrimitive( GFXTriangleList, 0, 4 );
            
      GFX->setWorldMatrix(MatrixF::Identity); 
      GFX->setProjectionMatrix(MatrixF::Identity);
      GFX->setViewMatrix(MatrixF::Identity);
      
      if(mShader.isValid()){         
         //GFX->setShader(mShader);
      } 

      // texture for line drawing
      if(mLineTexture.isValid())
         GFX->setTexture(0, mLineTexture);
      else
         GFX->setTexture(0, NULL);            
      
      /*
      F32 size = float(mTexture->getSize().x);
      //drawTriLineTex(0.0f,0.0f,0.5f,0.5f,ColorI(255,255,255),size*0.01f/size);      
      //drawTriLineTex(0.0f,0.0f,0.5f,0.5f,ColorI(255,255,255),0.01f);
      //GFX->setStateBlock(mNoCullSB);
      F32 rad = mDegToRad(texrot);
      F32 x = mCos(texrot);
      F32 y = mSin(texrot);
      drawTriLineTex(0.0f,0.0f,x*0.5f,y*0.5f,ColorI(255,255,255),0.1f);
      */

      static Vector<Point2F> lineList1, lineList2;
      U32 maxPoints = sourceData.size()/2;
      F32 ratio = F32(maxPoints)/512.0f;
      if(maxPoints > 512)
         maxPoints = 512;
      
      //lineList1.fill(Point2F(0.0,0.0));
      //lineList1.clear();
      lineList1.setSize(maxPoints);         
      //lineList2.fill(Point2F(0.0,0.0));
      //lineList2.clear();
      lineList2.setSize(maxPoints);      
                
      F32 xinc = (1.0f/F32(maxPoints-1))*2.0f;  
      //F32 sinval = 1.0f/F32(maxPoints-1) * 360.0f;      
          
      U32 ratIndex = 0;
      F32 ratFilter = 1.0f/ratio;
      F32 ratSum = 0.0f;
      F32 ampVal = 1.0f;
      for(U32 i=0; i < maxPoints; i++){   
         //F32 sinx = mSin(sinval*F32(i))/2.0f;
         //lineList[i].set(-1.0 + F32(i)*xinc,lowPassFilter(mRandF(-1.0f,1.0f),lineList[i].y,0.05f));
         if(ratio > 1.0f){
            F32 x = sourceData[ratIndex*2];
            F32 y = sourceData[ratIndex*2+1];
            ratIndex++;
            ratSum += ratio - 1.0f;
            while(ratSum >= 1.0f){               
               x = lowPassFilter(sourceData[ratIndex*2],x,ratFilter);
               y = lowPassFilter(sourceData[ratIndex*2+1],y,ratFilter);
               ratIndex++;
               ratSum -= 1.0f;
            }
            lineList1[i].set(-1.0 + F32(i)*xinc, x * ampVal - 0.5f);
            lineList2[i].set(-1.0 + F32(i)*xinc, y * ampVal + 0.5f);
         }else{
            lineList1[i].set(-1.0 + F32(i)*xinc, sourceData[i*2] * ampVal - 0.5f);
            lineList2[i].set(-1.0 + F32(i)*xinc, sourceData[i*2+1] * ampVal + 0.5f);
         }
         //Con::printf("%.4f,%.4f",lineList[i].x,lineList[i].y);
      }
      drawTriLineTexN(lineList1,ColorI(255,64,64),0.005f);
      drawTriLineTexN(lineList2,ColorI(64,64,255),0.005f);

      //GFX->setStateBlock(mReflectSB);      

      if(mProfile)
         GFX->getDrawUtil()->drawText(mProfile->mFont,Point2I(0,0),"hello texture");

      mGFXTextureTarget->resolve();      

      GFX->setFrustum(tmpFrust);
            
      GFX->popActiveRenderTarget();
   }

   // only display textured object when in the editor
   //    if the render request is not submitted to the render pass then ::render is not called
   if(!gEditingMission){
      return; 
   }

   // Allocate an ObjectRenderInst so that we can submit it to the RenderPassManager
   ObjectRenderInst *ri = state->getRenderPass()->allocInst<ObjectRenderInst>();

   // Now bind our rendering function so that it will get called
   ri->renderDelegate.bind( this, &AudioTextureObject::render );

   // Set our RenderInst as a standard object render
   ri->type = RenderPassManager::RIT_Object;

   // Set our sorting keys to a default value
   ri->defaultKey = 0;
   ri->defaultKey2 = 0;

   // Submit our RenderInst to the RenderPassManager
   state->getRenderPass()->addInst( ri );      
}

void AudioTextureObject::render( ObjectRenderInst *ri, SceneRenderState *state, BaseMatInstance *overrideMat ){
   //Con::warnf("AudioTextureObject::render - called");

   if ( overrideMat )
      return;

   if ( mVertexBuffer.isNull() )
      return;     

   PROFILE_SCOPE(AudioTextureObject_Render);

   // Set up a GFX debug event (this helps with debugging rendering events in external tools)
   GFXDEBUGEVENT_SCOPE( AudioTextureObject_Render, ColorI::RED );

   // GFXTransformSaver is a handy helper class that restores
   // the current GFX matrices to their original values when
   // it goes out of scope at the end of the function
   GFXTransformSaver saver;   

   // Calculate our object to world transform matrix
   MatrixF objectToWorld = getRenderTransform();
   objectToWorld.scale( getScale() );

   // Apply our object transform
   GFX->multWorld( objectToWorld );    

   // Deal with reflect pass otherwise
   // set the normal StateBlock
   if ( state->isReflectPass() )
      GFX->setStateBlock( mReflectSB );      
   else
      GFX->setStateBlock( mNormalSB );   

   // Set up the "generic" shaders
   // These handle rendering on GFX layers that don't support
   // fixed function. Otherwise they disable shaders.
   //GFX->setupGenericShaders( GFXDevice::GSModColorTexture );
   //GFX->setupGenericShaders( GFXDevice::GSTexture );

   // Set the vertex buffer
   GFX->setVertexBuffer( mVertexBuffer );

   // set texture 
   if (!mTextureTarget){      
      GFX->setTexture(0, mWarningTexture);      
   }else{
      GFX->setTexture(0, mTexture);
   }

   // define shader type
   GFX->setupGenericShaders( GFXDevice::GSModColorTexture );

   // Draw our triangles
   GFX->drawPrimitive( GFXTriangleList, 0, 4 );      
}

void AudioTextureObject::drawTriLine( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color, F32 thickness )
{
   GFXVertexBufferHandle<GFXVertexPC> verts( GFX, 12, GFXBufferTypeVolatile );

   F32 offset = thickness/2.0f;

   F32 xdiff = x2-x1;
   F32 ydiff = y2-y1;   

   Point3F vect(xdiff,ydiff,0.0f);   
   vect = mNormalize(vect); 
   Point2F ovect(offset*vect.x,offset*vect.y);

   verts.lock();
   
   verts[0].point.set( x1 - ovect.x, y1 - ovect.y, 0.0f );
   verts[1].point.set( x1 - ovect.x, y1 + ovect.y, 0.0f );
   verts[2].point.set( x1 + ovect.x, y1 - ovect.y, 0.0f );   

   verts[3].point.set( x2 + ovect.x, y2 + ovect.y, 0.0f );
   verts[4].point.set( x2 + ovect.x, y2 - ovect.y, 0.0f );
   verts[5].point.set( x2 - ovect.x, y2 + ovect.y, 0.0f );   

   verts[6].point.set( x1 - ovect.x, y1 + ovect.y, 0.0f );
   verts[7].point.set( x2 - ovect.x, y2 + ovect.y, 0.0f );
   verts[8].point.set( x2 + ovect.x, y2 - ovect.y, 0.0f );

   verts[9].point.set( x2 + ovect.x, y2 - ovect.y, 0.0f );
   verts[10].point.set( x1 + ovect.x, y1 - ovect.y, 0.0f );
   verts[11].point.set( x1 - ovect.x, y1 + ovect.y, 0.0f );

   verts[0].color = color;
   verts[1].color = color;
   verts[2].color = color;
   verts[3].color = color;
   verts[4].color = color;
   verts[5].color = color;
   verts[6].color = color;
   verts[7].color = color;
   verts[8].color = color;
   verts[9].color = color;
   verts[10].color = color;
   verts[11].color = color;

   verts.unlock();

   GFX->setVertexBuffer( verts );
   GFX->drawPrimitive( GFXTriangleList, 0, 4 );   
}

void AudioTextureObject::drawTriLineTex( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color, F32 thickness, U32 uvIndex )
{
   U32 numVerts = 8;
   GFXVertexBufferHandle<GFXVertexPCT> verts( GFX, numVerts, GFXBufferTypeVolatile );

   F32 offset = thickness/2.0f;

   F32 xdiff = x2-x1;
   F32 ydiff = y2-y1;   

   Point3F vect(xdiff,ydiff,0.0f);   
   vect = mNormalize(vect);    
   Point2F ovect(offset*vect.x,offset*vect.y);   

   verts.lock();

   // note: in a vector swapping x and y and negating one will produce a perpendicular vector
   verts[0].point.set( x1 - ovect.x*2 + ovect.y, y1 - ovect.y*2 - ovect.x, 0.0f ); // bottom
   verts[1].point.set( x1 - ovect.x*2 - ovect.y, y1 - ovect.y*2 + ovect.x, 0.0f ); // top
   verts[2].point.set( x1 + ovect.y, y1 - ovect.x, 0.0f ); // bottom r
   verts[3].point.set( x1 - ovect.y, y1 + ovect.x, 0.0f ); // top r
   verts[4].point.set( x2 + ovect.y, y2 - ovect.x, 0.0f ); // bottom r
   verts[5].point.set( x2 - ovect.y, y2 + ovect.x, 0.0f ); // top r
   verts[6].point.set( x2 + ovect.x*2 + ovect.y, y2 + ovect.y*2 - ovect.x, 0.0f ); // bottom
   verts[7].point.set( x2 + ovect.x*2 - ovect.y, y2 + ovect.y*2 + ovect.x, 0.0f ); // top

   /*
   for(U32 i = 0; i < 4; i++){
      Con::printf("point: %d:%.4f,%.4f,%.4f",i,verts[i].point.x,verts[i].point.y,verts[i].point.z);
   }
   */   

   // grab uv coords
   U32 uvoffset = uvIndex*8; 
   for(U32 i = 0; i < numVerts; i++){
      verts[i].texCoord.set(mUVCoords[i+uvoffset].x,mUVCoords[i+uvoffset].y);
   }   

   for(U32 i = 0; i < numVerts; i++){
      verts[i].color = color;
   }
   
   verts.unlock();

   GFX->setVertexBuffer( verts );   
   GFX->drawPrimitive( GFXTriangleStrip, 0, 6 );
}

void AudioTextureObject::drawTriLineTexN( Vector<Point2F> &points, const ColorI &color, F32 thickness, U32 uvIndex )
{
   U32 lines = points.size() - 1;
   if(!lines)
      return;

   U32 numLineVerts = 10;  // each line needs 8, but we need 2 per to have linking degenerate tris
   U32 numVerts = lines * numLineVerts; // calculate the number of verts
  
   GFXVertexBufferHandle<GFXVertexPCT> verts( GFX, numVerts, GFXBufferTypeVolatile );

   F32 offset = thickness/2.0f;

   verts.lock();
      
   for(U32 i=0; i < lines; i++){
      F32 x1,x2,y1,y2;
      x1 = points[i].x;
      x2 = points[i+1].x;
      y1 = points[i].y;
      y2 = points[i+1].y;

      F32 xdiff = x2-x1;
      F32 ydiff = y2-y1;

      Point3F vect(xdiff,ydiff,0.0f);   
      vect = mNormalize(vect);    
      Point2F ovect(offset*vect.x,offset*vect.y);

      U32 voff = i*numLineVerts;                  
      verts[0+voff].point.set( x1 - ovect.x*2 + ovect.y, y1 - ovect.y*2 - ovect.x, 0.0f ); // bottom
      verts[1+voff].point.set( x1 - ovect.x*2 - ovect.y, y1 - ovect.y*2 + ovect.x, 0.0f ); // top
      verts[2+voff].point.set( x1 + ovect.y, y1 - ovect.x, 0.0f ); // bottom r
      verts[3+voff].point.set( x1 - ovect.y, y1 + ovect.x, 0.0f ); // top r
      verts[4+voff].point.set( x2 + ovect.y, y2 - ovect.x, 0.0f ); // bottom r
      verts[5+voff].point.set( x2 - ovect.y, y2 + ovect.x, 0.0f ); // top r
      verts[6+voff].point.set( x2 + ovect.x*2 + ovect.y, y2 + ovect.y*2 - ovect.x, 0.0f ); // bottom
      verts[7+voff].point.set( x2 + ovect.x*2 - ovect.y, y2 + ovect.y*2 + ovect.x, 0.0f ); // top      
      verts[8+voff].point.set( verts[7+voff].point ); // eat this vert, degenerate triangle
      //verts[9+voff].point.set( verts[7+voff].point ); // eat this vert, degenerate triangle

      // grab uv coords
      U32 uvoffset = uvIndex*8; 
      for(U32 i = 0; i < 8; i++){
         verts[i+voff].texCoord.set(mUVCoords[i+uvoffset].x,mUVCoords[i+uvoffset].y);         
      } 
      //verts[8+voff].texCoord.set(verts[7+voff].texCoord.x,verts[7+voff].texCoord.y);  // make sure textures coords are valid
      //verts[9+voff].texCoord.set(verts[7+voff].texCoord.x,verts[7+voff].texCoord.y);  // make sure textures coords are valid

      for(U32 i = 0; i < numLineVerts-2; i++){
         verts[i+voff].color = color;
      }      

      if(i >= 1){
         // fixup this vert
         verts[-1+voff].point.set(verts[0+voff].point);  // eat this vert, degenerate triangle
         //verts[-1+voff].texCoord.set(verts[0+voff].texCoord.x,verts[0+voff].texCoord.y);  // make sure textures coords are valid
      }
   }
   
   verts.unlock();

   GFX->setVertexBuffer( verts );   
   GFX->drawPrimitive( GFXTriangleStrip, 0, numVerts-4 );  // clip off 2 unused vertexes
}

/*
TSMesh* AudioTextureObject::findShape(String shapeName){
   for ( U32 i = 0; i < mGeomShapeInstance->mMeshObjects.size(); i++ ){
      const TSShapeInstance::MeshObjectInstance &mesh = mGeomShapeInstance->mMeshObjects[i];
      if(shapeName.equal(mGeomShapeInstance->getShape()->getMeshName(i)))        
         return mesh.getMesh(0);
   }   

   return NULL;
}

void AudioTextureObject::drawLineShape( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color, F32 thickness)
{
   static U32 err = 0;
   static U32 last = 0;

   // check for meshes
   if(!mGeomShapeInstance)
      return;
   TSShape* tmpShape = mGeomShapeInstance->getShape();
   TSMesh* lineEnd=NULL;
   TSMesh* lineSegment=NULL;

   mGeomShapeInstance->setCurrentDetail(0);
   if(tmpShape){
      lineEnd = findShape(String("LineEnd0"));       
      lineSegment = findShape(String("LineEnd0"));           
   }else{     
      //Con::warnf("no shape"); 
      return;
   }

   if(!lineEnd || !lineSegment){
      //Con::warnf("no meshes"); 
      err = -1;
      //return;
   }else{
      err = 1;
   }

   if(err == -1 && last != err){
      last = err;
      Con::warnf("no meshes");       
   }

   if(err < 0)
      return;

   if(err == 1 && last != err){
      last = err;
      Con::warnf("meshes found"); 
   }

   TSVertexBufferHandle instanceVB;
   GFXPrimitiveBufferHandle instancePB;

   //mGeomShapeInstance->mMaterialList   

   lineEnd->createVBIB();   
   //lineEnd->convertToAlignedMeshData();
   //lineEnd->assemble(false);
   //lineEnd->mVertexFormat
   //lineEnd->render(instanceVB, instancePB);

   //lineEnd->primitives

   //TSSkinMesh* sLineEnd = dynamic_cast<TSSkinMesh*>(lineEnd);
   //if(sLineEnd){
   //   Con::printf("Is skin mesh");
   //}
   

   U32 totalverts = lineEnd->mNumVerts;
   
   GFXVertexBufferHandle<GFXVertexPCT> verts( GFX, totalverts, GFXBufferTypeVolatile );

   //Con::warnf("num verts %d",verts->mNumVerts);   

   F32 offset = thickness/2.0f;

   F32 xdiff = x2-x1;
   F32 ydiff = y2-y1;   

   Point3F vect(xdiff,ydiff,0.0f);   
   vect = mNormalize(vect); 
   Point2F ovect(offset*vect.x,offset*vect.y);   

   verts.lock();
   
   U32 ti;
   Point3F tpoint;
   for(U32 i = 0; i < lineEnd->mNumVerts; i++){
      ti = lineEnd->indices[i];
      verts[i].point = lineEnd->mVertexData[ti].vert()*0.1f+Point3F(x1,y1,0.0f); 
      //tpoint = lineEnd->mVertexData[i].vert();     
      Con::printf("point: %d:%.4f,%.4f,%.4f",i,tpoint.x,tpoint.y,tpoint.z);
      Con::printf("point: %d:%.4f,%.4f,%.4f",i,verts[i].point.x,verts[i].point.y,verts[i].point.z);
      verts[i].color = color;
      //verts[i].color = lineEnd->mVertexData[i].color();
      verts[i].texCoord = lineEnd->mVertexData[ti].tvert();
   }

   verts.unlock();

   GFX->setVertexBuffer( verts );    
   GFX->drawPrimitive( GFXTriangleStrip, 0, 2 );         
}
*/

void AudioTextureObject::drawLine( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color )
{
   drawLine( x1, y1, 0.0f, x2, y2, 0.0f, color );
}
void AudioTextureObject::drawLine( F32 x1, F32 y1, F32 z1, F32 x2, F32 y2, F32 z2, const ColorI &color )
{
   GFXVertexBufferHandle<GFXVertexPC> verts( GFX, 2, GFXBufferTypeVolatile );
   verts.lock();

   verts[0].point.set( x1, y1, z1 );
   verts[1].point.set( x2, y2, z2 );

   verts[0].color = color;
   verts[1].color = color;

   verts.unlock();

   GFX->setVertexBuffer( verts );
   //GFX->setStateBlock( mRectFillSB );
   GFX->drawPrimitive( GFXLineList, 0, 1 );
}


DefineEngineMethod( AudioTextureObject, postApply, void, (),,
   "A utility method for forcing a network update.\n")
{
	object->inspectPostApply();
}
DefineEngineMethod( AudioTextureObject, setAudioObject, void, (SimObject* aObj),,
//DefineEngineMethod( AudioTextureObject, setAudioObject, void, (String objName),,
   "Set audio object as source audio data.  Must be a LoopBackObject based object.\n")
{   
   LoopBackObject* tObj = dynamic_cast<LoopBackObject*>(aObj);   
   if(!tObj && aObj){
      Con::warnf("AudioTextureObject.setAudioObject - console method failed.  Object provided is not a LoopBackObject derived object.");
   }else{
      object->setAudioObject(tObj);
      object->inspectPostApply();	
      
      if(object->isClientObject())
         Con::errorf("Running on client");
      else
         Con::errorf("Running on server");
   }
}


IMPLEMENT_CONOBJECT(NamedTexTargetObject);

NamedTexTargetObject::NamedTexTargetObject(){
   //mTexSize = 1024;
   mTexDims.x = 1024;
   mTexDims.y = 1024;
}
NamedTexTargetObject::~NamedTexTargetObject(){   
}

void NamedTexTargetObject::initPersistFields(){
   addGroup( "Settings" );
      addField( "targetName", TypeRealString, Offset(mTexTargetName, NamedTexTargetObject), 
         "Name used to define NamedTexTarget");
      //addField( "texSize", TypeS32, Offset( mTexSize, NamedTexTargetObject ),
      //   "Texture size setting for both x and y.  Not dynamically updated.");
      // mTexDims
      addField( "texDims", TypePoint2I, Offset( mTexDims, NamedTexTargetObject ),
         "Texture size setting for x and y.  Not dynamically updated.");
   endGroup( "Settings" );

   Parent::initPersistFields();
}

bool NamedTexTargetObject::onAdd(){
   if(!Parent::onAdd())
      return false;

   if(mTexTargetName.isEmpty()){  
      Con::warnf("NamedTexTargetObject::onAdd - targetName is not set, cannot register target.");
      return true;
   }    
   
   if(NamedTexTarget::find(mTexTargetName) != NULL){
      Con::warnf("NamedTexTargetObject::onAdd - targetName is already registered, cannot register target name: %s",mTexTargetName.c_str());         
      return true;
   }

   Con::warnf("NamedTexTargetObject::onAdd - Texture target registered: %s",mTexTargetName.c_str());
   mTexTarget.registerWithName(mTexTargetName);
   
   mTextureBuffer.set(mTexDims.x, mTexDims.y, GFXFormatR8G8B8X8, &GFXDefaultRenderTargetProfile, "", 0); 
   if(mTextureBuffer.getPointer()){
      mTexTarget.setTexture(mTextureBuffer.getPointer());
   }

   return true;
}
void NamedTexTargetObject::onRemove(){
   if(mTexTarget.isRegistered())
      mTexTarget.unregister();   

   if(mTextureBuffer.getPointer())
      mTextureBuffer.free();

   Parent::onRemove();
}