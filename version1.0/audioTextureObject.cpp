#include "audioTextureObject.h"

#include "console/engineAPI.h"
#include "core/stream/bitStream.h"

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

extern bool gEditingMission;

IMPLEMENT_CO_NETOBJECT_V1(AudioTextureObject);

AudioTextureObject::AudioTextureObject(){
   // Flag this object so that it will always
   // be sent across the network to clients
   mNetFlags.set( Ghostable | ScopeAlways );

   mTypeMask |= StaticObjectType | StaticShapeObjectType;

   mTextureTarget = NULL;   

   mTexture = NULL; 

   mProfile = NULL;
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

   // do render to texture here
   // render to texture
   if(mTexture){   
      static F32 texrot = 0.0f;
       
      GFXTransformSaver subsaver;     

      mGFXTextureTarget = GFX->allocRenderToTextureTarget();        
      mGFXTextureTarget->attachTexture(GFXTextureTarget::Color0,mTexture);

      GFX->pushActiveRenderTarget();      
      GFX->setActiveRenderTarget(mGFXTextureTarget);
            
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
                
      F32 left, right, top, bottom;
      F32 fnear = 0.01f;
      F32 ffar = -10.0f;
      MathUtils::makeFrustum( &left, &right, &top, &bottom, M_HALFPI_F, 1.0f, fnear );
      //Con::printf("%f,%f,%f,%f",left,right,top,bottom);
      Frustum tmpFrust = GFX->getFrustum();
      GFX->setFrustum( left, right, bottom, top, fnear, ffar );                       
     
      //GFX->clear(GFXClearTarget|GFXClearZBuffer|GFXClearStencil,ColorI(0,0,0),1.0f,0);      
      GFX->clear(GFXClearTarget,ColorI(0,0,0),1.0f,0);
      GFX->setStateBlock( mNormalSB );
      GFX->setVertexBuffer( mVertexBuffer );
      GFX->setTexture(0, mWarningTexture);     
      GFX->setupGenericShaders( GFXDevice::GSModColorTexture );        

      GFX->setWorldMatrix(MatrixF::Identity);
      GFX->multWorld(newtrans);

      if(mShader.isValid()){         
         //GFX->setShader(mShader);
      }
      GFX->drawPrimitive( GFXTriangleList, 0, 4 );
            
      GFX->setProjectionMatrix(MatrixF::Identity);
      GFX->setViewMatrix(MatrixF::Identity);
      GFX->setWorldMatrix(MatrixF::Identity);      
      
      if(mShader.isValid()){         
         //GFX->setShader(mShader);
      } 

      // texture for line drawing
      if(mLineTexture.isValid())
         GFX->setTexture(0, mLineTexture);
      else
         GFX->setTexture(0, NULL);            
      
      F32 size = float(mTexture->getSize().x);
      //drawTriLineTex(0.0f,0.0f,0.5f,0.5f,ColorI(255,255,255),size*0.01f/size);      
      drawTriLineTex(0.0f,0.0f,0.5f,0.5f,ColorI(255,255,255),0.01f);
      //drawTriLineTex(0.0f,0.0f,0.5f,0.5f,ColorI(255,255,255),0.1f);

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

void AudioTextureObject::drawTriLineTex( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color, F32 thickness )
{
   GFXVertexBufferHandle<GFXVertexPCT> verts( GFX, 12, GFXBufferTypeVolatile );

   F32 offset = thickness/2.0f;

   F32 xdiff = x2-x1;
   F32 ydiff = y2-y1;   

   Point3F vect(xdiff,ydiff,0.0f);   
   vect = mNormalize(vect); 
   Point2F ovect(offset*vect.x,offset*vect.y);

   verts.lock();
   
   verts[0].point.set( x1 - ovect.x, y1 - ovect.y, 0.0f );  // end point
   verts[1].point.set( x1 - ovect.x, y1 + ovect.y, 0.0f );  // top
   verts[2].point.set( x1 + ovect.x, y1 - ovect.y, 0.0f );  // bottom 

   verts[3].point.set( x2 + ovect.x, y2 + ovect.y, 0.0f ); // end point
   verts[4].point.set( x2 + ovect.x, y2 - ovect.y, 0.0f ); // bottom
   verts[5].point.set( x2 - ovect.x, y2 + ovect.y, 0.0f ); // top  

   verts[6].point.set( x1 - ovect.x, y1 + ovect.y, 0.0f );
   verts[7].point.set( x2 - ovect.x, y2 + ovect.y, 0.0f );
   verts[8].point.set( x2 + ovect.x, y2 - ovect.y, 0.0f );

   verts[9].point.set( x2 + ovect.x, y2 - ovect.y, 0.0f );
   verts[10].point.set( x1 + ovect.x, y1 - ovect.y, 0.0f );
   verts[11].point.set( x1 - ovect.x, y1 + ovect.y, 0.0f );

   verts[0].texCoord.set(0.010f, 0.029f);
   verts[1].texCoord.set(0.041f, 0.010f);
   verts[2].texCoord.set(0.041f, 0.050f);

   verts[3].texCoord.set(0.010f, 0.029f);
   verts[4].texCoord.set(0.041f, 0.050f);
   verts[5].texCoord.set(0.041f, 0.010f);
   
   verts[6].texCoord.set(0.041f, 0.010f);
   verts[7].texCoord.set(0.141f, 0.010f);
   verts[8].texCoord.set(0.141f, 0.050f);

   verts[9].texCoord.set(0.141f, 0.050f);
   verts[10].texCoord.set(0.041f, 0.050f);
   verts[11].texCoord.set(0.041f, 0.010f);

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
   "Set audio object as source audio data.  Must be a LoopBackObject based object.\n")
{
   LoopBackObject* tObj = dynamic_cast<LoopBackObject*>(aObj);   
   object->setAudioObject(tObj);	
}


IMPLEMENT_CONOBJECT(NamedTexTargetObject);

NamedTexTargetObject::NamedTexTargetObject(){
   mTexSize = 1024;
}
NamedTexTargetObject::~NamedTexTargetObject(){   
}

void NamedTexTargetObject::initPersistFields(){
   addGroup( "Settings" );
      addField( "targetName", TypeRealString, Offset(mTexTargetName, NamedTexTargetObject), 
         "Name used to define NamedTexTarget");
      addField( "texSize", TypeS32, Offset( mTexSize, NamedTexTargetObject ),
         "Texture size setting for both x and y.  Not dynamically updated.");
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
   
   mTextureBuffer.set(mTexSize, mTexSize, GFXFormatR8G8B8X8, &GFXDefaultRenderTargetProfile, "", 0); 
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