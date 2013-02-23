//-----------------------------------------------------------------------------
// Copyright (c) 2012 GarageGames, LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include "renderRTTExample.h"

#include "math/mathIO.h"
#include "scene/sceneRenderState.h"
#include "core/stream/bitStream.h"
#include "materials/sceneData.h"
#include "gfx/gfxDebugEvent.h"
#include "gfx/gfxTransformSaver.h"
#include "renderInstance/renderPassManager.h"

#include "console/engineAPI.h"
#include "math/mathUtils.h"
#include "gfx/util/GFXFrustumSaver.h"
#include "gfx/gfxDrawUtil.h"
#include "materials/materialManager.h"
#include "materials/baseMatInstance.h"
#include "gfx/gFont.h"

// missing editing flag
extern bool gEditingMission;

IMPLEMENT_CO_NETOBJECT_V1(RenderRTTExample);

ConsoleDocClass( RenderRTTExample, 
   "@brief An example scene object which renders using a callback.\n\n"
   "This class implements a basic SceneObject that can exist in the world at a "
   "3D position and render itself. Note that RenderRTTExample handles its own "
   "rendering by submitting itself as an ObjectRenderInst (see "
   "renderInstance\renderPassmanager.h) along with a delegate for its render() "
   "function. However, the preffered rendering method in the engine is to submit "
   "a MeshRenderInst along with a Material, vertex buffer, primitive buffer, and "
   "transform and allow the RenderMeshMgr handle the actual rendering. You can "
   "see this implemented in RenderMeshExample.\n\n"
   "See the C++ code for implementation details.\n\n"
   "@ingroup Examples\n" );

//-----------------------------------------------------------------------------
// Object setup and teardown
//-----------------------------------------------------------------------------
RenderRTTExample::RenderRTTExample()
{
   // Flag this object so that it will always
   // be sent across the network to clients
   mNetFlags.set( Ghostable | ScopeAlways );

   // Set it as a "static" object
   mTypeMask |= StaticObjectType | StaticShapeObjectType;

   mRotParm1 = mRotParm2 = mRotParm3 = mRotParm4 = 0.0f;

   String fontCacheDir = Con::getVariable("$GUI::fontCacheDirectory");
   mFont = GFont::create("Arial", 12, fontCacheDir);
}

RenderRTTExample::~RenderRTTExample()
{
}

//-----------------------------------------------------------------------------
// Object Editing
//-----------------------------------------------------------------------------
void RenderRTTExample::initPersistFields()
{
   
   addGroup( "Settings" );
      addField( "textureTargetName", TypeRealString, Offset(mTexTargetName, RenderRTTExample), 
         "Name used to define NamedTexTarget");
   endGroup( "Settings" );

   // SceneObject already handles exposing the transform
   Parent::initPersistFields();
}

bool RenderRTTExample::onAdd()
{
   if ( !Parent::onAdd() )
      return false;   

   // Create the texture
   // You can use other formats, I have used GFXFormatR8G8B8A8 and it works too.
   // The profile is important, don't change that.
   // I believe the texture gets destroyed when the mTextureBuffer goes out of scope.
   mTextureBuffer.set(1024, 1024, GFXFormatR8G8B8X8, &GFXDefaultRenderTargetProfile, "", 0); 

   // setup material
   // this is here because it will fail on the first network update
   if( isClientObject() ){
      updateStuff();
   }  

   // Set up a 1x1x1 bounding box
   mObjBox.set( Point3F( -0.5f, -0.5f, -0.5f ),
                Point3F(  0.5f,  0.5f,  0.5f ) );

   resetWorldBox();

   // Add this object to the scene
   addToScene();

   setProcessTick( true );

   return true;
}

void RenderRTTExample::onRemove()
{
   setProcessTick( false );

   // Remove this object from the scene
   removeFromScene();

   Parent::onRemove();
}

void RenderRTTExample::setTransform(const MatrixF & mat)
{
   // Let SceneObject handle all of the matrix manipulation
   Parent::setTransform( mat );

   // Dirty our network mask so that the new transform gets
   // transmitted to the client object
   setMaskBits( TransformMask );
}

U32 RenderRTTExample::packUpdate( NetConnection *conn, U32 mask, BitStream *stream )
{
   // Allow the Parent to get a crack at writing its info
   U32 retMask = Parent::packUpdate( conn, mask, stream );

   // Write our transform information
   if ( stream->writeFlag( mask & TransformMask ) )
   {
      mathWrite(*stream, getTransform());
      mathWrite(*stream, getScale());
   }

   if ( stream->writeFlag( mask & UpdateMask ) )
   {
      stream->write( mTexTargetName );
   }

   return retMask;
}

void RenderRTTExample::unpackUpdate(NetConnection *conn, BitStream *stream)
{
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
      stream->read( &mTexTargetName );

      if ( isProperlyAdded() )
         updateStuff();
   }
}

//-----------------------------------------------------------------------------
// Object Rendering
//-----------------------------------------------------------------------------
void RenderRTTExample::createGeometry()
{
   U32 numPoints = 36;
   static const Point3F cubePoints[8] = 
   {
      Point3F( 1.0f, -1.0f, -1.0f), Point3F( 1.0f, -1.0f,  1.0f),
      Point3F( 1.0f,  1.0f, -1.0f), Point3F( 1.0f,  1.0f,  1.0f),
      Point3F(-1.0f, -1.0f, -1.0f), Point3F(-1.0f,  1.0f, -1.0f),
      Point3F(-1.0f, -1.0f,  1.0f), Point3F(-1.0f,  1.0f,  1.0f)
   };

   static const Point3F cubeNormals[6] = 
   {
      Point3F( 1.0f,  0.0f,  0.0f), Point3F(-1.0f,  0.0f,  0.0f),
      Point3F( 0.0f,  1.0f,  0.0f), Point3F( 0.0f, -1.0f,  0.0f),
      Point3F( 0.0f,  0.0f,  1.0f), Point3F( 0.0f,  0.0f, -1.0f)
   };

   static const ColorI cubeColors[3] = 
   {
      ColorI( 255,   0,   0, 255 ),
      ColorI(   0, 255,   0, 255 ),
      ColorI(   0,   0, 255, 255 )
   };

   static const U32 cubeFaces[36][3] = 
   {
      { 3, 0, 0 }, { 0, 0, 0 }, { 1, 0, 0 },
      { 2, 0, 0 }, { 0, 0, 0 }, { 3, 0, 0 },
      { 7, 1, 0 }, { 4, 1, 0 }, { 5, 1, 0 },
      { 6, 1, 0 }, { 4, 1, 0 }, { 7, 1, 0 },
      { 3, 2, 1 }, { 5, 2, 1 }, { 2, 2, 1 },
      { 7, 2, 1 }, { 5, 2, 1 }, { 3, 2, 1 },
      { 1, 3, 1 }, { 4, 3, 1 }, { 6, 3, 1 },
      { 0, 3, 1 }, { 4, 3, 1 }, { 1, 3, 1 },
      { 3, 4, 2 }, { 6, 4, 2 }, { 7, 4, 2 },
      { 1, 4, 2 }, { 6, 4, 2 }, { 3, 4, 2 },
      { 2, 5, 2 }, { 4, 5, 2 }, { 0, 5, 2 },
      { 5, 5, 2 }, { 4, 5, 2 }, { 2, 5, 2 }
   };

   // Fill the vertex buffer
   VertexType *pVert = NULL;

   //mVertexBuffer.set( GFX, 36, GFXBufferTypeStatic );
   mVertexBuffer.set( GFX, numPoints, GFXBufferTypeStatic );
   pVert = mVertexBuffer.lock();

   Point3F halfSize = getObjBox().getExtents() * 0.5f;

   //for (U32 i = 0; i < 36; i++)
   for (U32 i = 0; i < numPoints; i++)
   {
      const U32& vdx = cubeFaces[i][0];
      const U32& ndx = cubeFaces[i][1];
      const U32& cdx = cubeFaces[i][2];

      pVert[i].point  = cubePoints[vdx] * halfSize;
      pVert[i].normal = cubeNormals[ndx];
      pVert[i].color  = cubeColors[cdx];
   }

   mVertexBuffer.unlock();

   // Set up our normal and reflection StateBlocks
   GFXStateBlockDesc desc;

   // The normal StateBlock only needs a default StateBlock
   desc.cullDefined = true;
   desc.cullMode = GFXCullCCW;     
   mNormalSB = GFX->createStateBlock( desc );

   desc.cullMode = GFXCullNone;
   mNoCullSB = GFX->createStateBlock( desc );

   // The reflection needs its culling reversed
   desc.cullDefined = true;
   desc.cullMode = GFXCullCW;
   mReflectSB = GFX->createStateBlock( desc );   

   GFXStateBlockDesc rectFill;
   rectFill.setCullMode(GFXCullNone);
   rectFill.setZReadWrite(false);
   rectFill.setBlend(true, GFXBlendSrcAlpha, GFXBlendInvSrcAlpha);
   mRectFillSB = GFX->createStateBlock(rectFill);

   desc.samplersDefined = true;
   desc.setCullMode(GFXCullNone);
   desc.setZReadWrite(false);
   desc.setBlend(true, GFXBlendSrcAlpha, GFXBlendInvSrcAlpha);
   desc.samplers[0] = GFXSamplerStateDesc::getClampLinear();
   desc.samplers[0].minFilter = GFXTextureFilterPoint;
   desc.samplers[0].mipFilter = GFXTextureFilterPoint;
   desc.samplers[0].magFilter = GFXTextureFilterPoint;
   mRectTexSB = GFX->createStateBlock(desc);
}

// these methods were modeled after the methods in imposterCapture.cpp that uses RTT
void RenderRTTExample::rttBegin(RectI& vpDims, bool isOrtho, F32 orthoRadius)
{
   // set gfx up for render to texture
   // this keeps viewport from being messed with on default target
   GFX->pushActiveRenderTarget();

   if(isOrtho)
      mOrthoRadius = orthoRadius;
   else
      mOrthoRadius = 10.0f; // eh, whatever

   GFX->setViewport( vpDims );
   // if ortho then setup ortho projection
   if(isOrtho)
   {
      GFX->setOrtho( -mOrthoRadius, mOrthoRadius, -mOrthoRadius, mOrthoRadius, 1, 20.0f * mOrthoRadius );
   }
   
   // Position camera looking out the X axis.
   MatrixF cameraMatrix( true );
   //cameraMatrix.setColumn( 0, Point3F( 0, 0, 1 ) );
   //cameraMatrix.setColumn( 1, Point3F( 1, 0, 0 ) );
   //cameraMatrix.setColumn( 2, Point3F( 0, 1, 0 ) );

   // Setup frustum
   F32 left, right, top, bottom, nearPlane, farPlane;
   GFX->getFrustum( &left, &right, &bottom, &top, &nearPlane, &farPlane, &isOrtho );
   Frustum frust( isOrtho, left, right, top, bottom, nearPlane, farPlane, cameraMatrix );

   mRenderPass = new RenderPassManager();
   mRenderPass->assignName( "DiffuseRenderPass" );
   mMeshRenderBin = new RenderMeshMgr();
   mRenderPass->addManager( mMeshRenderBin );
   
   // Set up scene state.
   mState = new SceneRenderState(
      gClientSceneGraph,
      SPT_Diffuse,
      SceneCameraState( vpDims, frust, GFX->getWorldMatrix(),GFX->getProjectionMatrix() ),
      mRenderPass,
      false
   );

   // Set up our TS render state.
   mRData.setSceneState( mState );
   mRData.setCubemap( NULL );
   mRData.setFadeOverride( 1.0f );
   
   mRenderTarget = GFX->allocRenderToTextureTarget();
}
void RenderRTTExample::rttEnd()
{
   GFX->popActiveRenderTarget();

   mShapeInstance = NULL;

   mRenderTarget = NULL;
   mMeshRenderBin = NULL; // Deleted by mRenderPass
   SAFE_DELETE( mState );
   SAFE_DELETE( mRenderPass );   
}

/*
reference:
http://zophusx.byethost11.com/tutorial.php?lan=dx9&num=8
*/
void RenderRTTExample::prepRenderImage( SceneRenderState *state )
{   
   // Do a little prep work if needed
   // This will prepare some simple crap we need for 
   // our RTT as well as rendering in the scene.
   // It really is up to you.
   if ( mVertexBuffer.isNull() )
      createGeometry();

   // do our custom RTT here
   
   // Only do render if our texture is valid
   // You might consider checking that the render target is valid too.
   // Especially if you move your render target to an external object.
   // I did that with my other audio objects to solve some loading issues.
   if(mTextureBuffer.isValid())
   {
      GFXTexHandle tmpTexHandle = mTextureBuffer.getPointer();

      RectI viewport(0,0,tmpTexHandle->getWidth(),tmpTexHandle->getHeight());

      // Save previous render settings.
      // This preserves transforms and restores 
      // them when this object goes out of scope.
      GFXTransformSaver transSaver;
      GFXFrustumSaver frustSaver;

      // grab a copy of all matrices for doing 3D rtt
      MatrixF worldMatrix = GFX->getWorldMatrix();
      MatrixF viewMatrix = GFX->getViewMatrix();
      MatrixF projectionMatrix = GFX->getProjectionMatrix();

      // could not get this to work, left in for people to experiment with. Modeled after imposters
      if(0)
      {
      rttBegin(viewport);

      {         
         // The object to world transform.
         MatrixF rotMatrix;
         Point3F center(0.0f,0.0f,0.0f);
      
         MatrixF centerMat( true );
         centerMat.setPosition( -center );
         MatrixF objMatrix( rotMatrix );
         objMatrix.mul( centerMat );
         GFX->setWorldMatrix( objMatrix );

         // The view transform.
         MatrixF view( EulerF( M_PI_F / 2.0f, 0, M_PI_F ), Point3F( 0, 0, -10.0f * mOrthoRadius ) );
         mRenderPass->assignSharedXform( RenderPassManager::View, view );

         mRenderPass->assignSharedXform( RenderPassManager::Projection, GFX->getProjectionMatrix() );

         // Render the diffuse pass.
         mRenderPass->clear();
         //mMeshRenderBin->getMatOverrideDelegate().bind( ImposterCaptureMaterialHook::getDiffuseInst );
         // rtt here
         mRenderTarget->attachTexture( GFXTextureTarget::Color0, tmpTexHandle );
         mRenderTarget->attachTexture( GFXTextureTarget::DepthStencil, GFXTextureTarget::sDefaultDepthStencil );
         GFX->setActiveRenderTarget( mRenderTarget );
         
         //GFX->clear( GFXClearZBuffer | GFXClearStencil | GFXClearTarget, ColorI(0,0,255), 1.0f, 0 );
         GFX->clear( GFXClearTarget, ColorI(0,0,255), 1.0f, 0 );

         // draw calls here
         GFX->getDrawUtil()->setBitmapModulation(ColorI(255,128,128));
         GFX->getDrawUtil()->draw2DSquare(Point2F(0.1f,0.1f),tmpTexHandle->getWidth()*0.9f,0.0f);

         mState->getRenderPass()->renderPass( mState );

         GFX->updateStates();

         mRenderTarget->resolve();
      }

      rttEnd();
      return;
      }           
      
      // prepare render texture
      GFXTextureTargetRef mGFXTextureTarget;
      mGFXTextureTarget = GFX->allocRenderToTextureTarget();        
      mGFXTextureTarget->attachTexture(GFXTextureTarget::Color0,tmpTexHandle);
      // save render target and set new render target
      GFX->pushActiveRenderTarget();      
      GFX->setActiveRenderTarget(mGFXTextureTarget);

      //GFX->setViewport( viewport ); // this is done in setActiveRenderTarget
      //GFX->setClipRect(viewport); 

      // setup frustrum
      F32 left, right, top, bottom;
      F32 fnear = 0.001f;
      F32 ffar = -10.0f;
      // handy dandy tool!
      MathUtils::makeFrustum( &left, &right, &top, &bottom, M_HALFPI_F, 1.0f, fnear );      
      GFX->setFrustum( left, right, bottom, top, fnear, ffar );  
      //MatrixF outMat;      
      //     
       
      // this is set for 2D rendering just like in GUI canvas
      GFX->setWorldMatrix(MatrixF::Identity);  // leave GG set world matrix alone
      GFX->setProjectionMatrix(MatrixF::Identity);
      GFX->setViewMatrix(MatrixF::Identity);  
      

      // set ortho for 2D  
      /*
      MatrixF outMat;
      MathUtils::makeOrthoProjection(&outMat,-1.0f,-1.0f,1.0f,1.0f,0.1f,-10.0f,false);
      GFX->setOrtho(-1.0f,-1.0f,1.0f,1.0f,0.1f,-10.0f);
      GFX->setProjectionMatrix(outMat);       
      */       

      // setup more crap
      if(GFX->isFrustumOrtho())
         GFX->clear(GFXClearTarget/*|GFXClearZBuffer*/,ColorI(0,64,0),1.0f,0);
      else
         GFX->clear(GFXClearTarget/*|GFXClearZBuffer*/,ColorI(0,0,64),1.0f,0);
      // might need to clear z buffer
      // GFX->clear(GFXClearTarget|GFXClearZBuffer|GFXClearStencil,ColorI(0,0,0),1.0f,0);
      //if ( state->isReflectPass() )
         GFX->setStateBlock( mReflectSB );
      //else
         //GFX->setStateBlock( mNormalSB );         
      GFX->setupGenericShaders( GFXDevice::GSModColorTexture ); 
      //GFX->setupGenericShaders( GFXDevice::GSColor ); 

      // draw background
      //GFX->getDrawUtil()->draw2DSquare(Point2F(0.1f,0.1f),tmpTexHandle->getWidth()*0.9f,0.0f);
      GFX->getDrawUtil()->setBitmapModulation(ColorI(255,255,255));            
      //GFX->getDrawUtil()->draw2DSquare(Point2F(0.0f,0.0f),1.5f,mRotParm1);
      //draw2DSquare(Point3F(0.0f,0.0f,-10.0f),1.5f,mRotParm1);

      // setup frustrum
      //F32 left, right, top, bottom;
      //F32 fnear = 0.01f;
      //F32 ffar = -10.0f;
      // handy dandy tool!
      /*
      MathUtils::makeFrustum( &left, &right, &top, &bottom, M_HALFPI_F, 1.0f, fnear );      
      GFX->setFrustum( left, right, bottom, top, fnear, ffar );       

      // setup transforms
      GFX->setWorldMatrix(MatrixF::Identity); 
      GFX->setProjectionMatrix(MatrixF::Identity);
      GFX->setViewMatrix(MatrixF::Identity);
      */

      // now lets render something else
      if(1)
      {      
         // lets use the warning texture for stuff that needs a texture
         //GFX->setTexture(0, mWarningTexture);      

         // debug draw the frustum
         GFX->getDrawUtil()->drawFrustum(GFX->getFrustum(),ColorI(255,0,0));

         // drawing a line
         GFX->getDrawUtil()->drawLine(Point2F(-1.0f,0.0f),Point2F(1.0f,0.0f),ColorI(255,255,255));

         // do some rotation of the world before this next render
         /*
         MatrixF newtrans = GFX->getWorldMatrix();
         newtrans = newtrans.set(EulerF(0.0f,mRotParm2,0.0f));  
         newtrans.setColumn(3, Point3F(0.0f,0.0f,0.0f)); 
         //GFX->setWorldMatrix(MatrixF::Identity);
         //GFX->multWorld(newtrans);   
         MatrixF newview = MatrixF::Identity;
         newview.setColumn(3, Point3F(0.0f,0.0f,0.0f));
         //GFX->setViewMatrix(newview);      
         //Con::printf("%.4f",mRotParm2);
         */

         //GFX->getDrawUtil()->drawBitmap(mWarningTexture,Point2F(-1.0,-1.0));
         //GFX->setStateBlock(mNoCullSB);
         RectF destR(Point2F(0.0,0.0),Point2F(1.0,1.0));
         //GFX->getDrawUtil()->drawBitmapStretch(mWarningTexture,destR,GFXBitmapFlip_Y);
         
      }

      // write some text
      // not working as the drawText routines use the font render batcher, I have no idea how to use that
      //GFX->getDrawUtil()->drawText(mFont, Point2I(1,1), "Ooh, text...");

      // do 3D render
      MatrixF outMat(true);
      MathUtils::makeProjection(&outMat,M_HALFPI_F,1.0f,fnear,ffar,true);
      outMat.setPosition(Point3F(0.0f,0.0f,0.0f));
      GFX->setProjectionMatrix(outMat);
      GFX->setViewMatrix(MatrixF::Identity);
      GFX->setWorldMatrix(outMat.inverse());
      
      MatrixF newtrans = GFX->getWorldMatrix();
      newtrans = newtrans.set(EulerF(0.0f,mRotParm2,0.0f));  
      newtrans.setColumn(3, Point3F(0.0f,0.0f,mRotParm3+10.0f)); 
      //GFX->setWorldMatrix(MatrixF::Identity);
      GFX->multWorld(newtrans);  
      //MatrixF vm = GFX->getWorldMatrix();
      //GFX->setViewMatrix(vm.inverse());
      
      F32 colorMod = mRotParm3/10.0f*128;
      GFX->getDrawUtil()->setBitmapModulation(ColorI(255+colorMod,255+colorMod,255+colorMod));
      GFX->setTexture(0,mWarningTexture);
      draw2DSquare(Point3F(0.0f,0.0f,0.0f),1.5f,0.0f);//mRotParm2);

      // resolve texture
      mGFXTextureTarget->resolve();
            
      // restore render target settings
      GFX->popActiveRenderTarget(); 

   }

   // done doing our custom RTT

   // I have decided to only render this object if the editor is open
   // This requires an extern definition in this file.
   if(!gEditingMission){
      return; 
   }

   // Beyond this point I really only know is 
   // that this code makes it so the render manager
   // calls the render method on this object.

   // Allocate an ObjectRenderInst so that we can submit it to the RenderPassManager
   ObjectRenderInst *ri = state->getRenderPass()->allocInst<ObjectRenderInst>();

   // Now bind our rendering function so that it will get called
   ri->renderDelegate.bind( this, &RenderRTTExample::render );

   // Set our RenderInst as a standard object render
   ri->type = RenderPassManager::RIT_Object;

   // Set our sorting keys to a default value
   ri->defaultKey = 0;
   ri->defaultKey2 = 0;

   // Submit our RenderInst to the RenderPassManager
   state->getRenderPass()->addInst( ri );
}

void RenderRTTExample::advanceTime( F32 dt )
{
   static F32 p1Dir = 1.0f;
   mRotParm1 += dt*p1Dir*1.0f;
   F32 tP1 = mClampF(mRotParm1,-M_PI_F,M_PI_F);
   if(tP1 != mRotParm1){
      p1Dir *= -1.0f;      
   }
   mRotParm1 = tP1;

   mRotParm2 += dt*3.0f;
   F32 tP2 = mClampF(mRotParm2,0.0f,M_PI_F*2.0f);
   if(tP2 != mRotParm2)
      mRotParm2 = 0.0f;
   else
      mRotParm2 = tP2;

   static F32 p3Dir = 1.0f;
   mRotParm3 += dt*p3Dir*5.0f;
   F32 tP3 = mClampF(mRotParm3,-7.5f,0.0f);
   if(tP3 != mRotParm3){
      p3Dir *= -1.0f;
   }
   mRotParm3 = tP3;
   
}

void RenderRTTExample::render( ObjectRenderInst *ri, SceneRenderState *state, BaseMatInstance *overrideMat )
{
   if ( overrideMat )
      return;

   if ( mVertexBuffer.isNull() )
      return;

   PROFILE_SCOPE(RenderRTTExample_Render);

   // Set up a GFX debug event (this helps with debugging rendering events in external tools)
   GFXDEBUGEVENT_SCOPE( RenderRTTExample_Render, ColorI::RED );

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
   GFX->setupGenericShaders( GFXDevice::GSModColorTexture );

   // Set the vertex buffer
   GFX->setVertexBuffer( mVertexBuffer );

   // Draw our triangles
   GFX->drawPrimitive( GFXTriangleList, 0, 12 );
}

// something changed, tell clients about it
void RenderRTTExample::inspectPostApply()
{
   Parent::inspectPostApply();

   // Flag the network mask to send the updates
   // to the client object
   setMaskBits( UpdateMask );
}
// Stuff that needs updating
// This is running on the client ghost
// RTT is all client side, remember that!
// Doing RTT in a GUI is a GOOD idea because it is client side too!
// So doing it on a networked object is just being a glutton for punishment.
void RenderRTTExample::updateStuff()
{
   // do we have a name we can use?
   if(mTexTargetName.isNotEmpty())
   {  
      // make sure there is a texture defined
      if(mTextureBuffer.isValid())
      {
         // assign texture
         mTextureTarget.setTexture(mTextureBuffer.getPointer());
         // register target name
         // If this succeeds then you can set the target on other objects/materials using this name prefixed by #
         if(!mTextureTarget.registerWithName(mTexTargetName))
         {
            Con::warnf("RenderRTTExample::updateStuff(): Ah man, your texture registration crashed and burned!");
         }
      }
   }

   // get warning texture reference
   if(mWarningTexture.isNull())
   {  
      // when you just gotta warn people...
      BaseMatInstance* tmpMat = MATMGR->createWarningMatInstance();
      if(tmpMat){
         // this is proabably a hack...
         const char* tmpTexName = tmpMat->getMaterial()->getDataField(StringTable->insert("diffuseMap"),"0");
         if(tmpTexName){
            mWarningTexture.set(String(tmpTexName),&GFXDefaultStaticDiffuseProfile,"");                                                          
         }  
    
         // get rid of temp material instance
         SAFE_DELETE( tmpMat );
      }
   }
}

void RenderRTTExample::draw2DSquare( const Point3F &screenPoint, F32 width, F32 spinAngle )
{
   width *= 0.5;

   Point3F offset( screenPoint.x, screenPoint.y, 0.0f );

   GFXVertexBufferHandle<GFXVertexPCT> verts( GFX, 4, GFXBufferTypeVolatile );
   verts.lock();

   verts[0].point.set( -width, -width, screenPoint.z );
   verts[1].point.set( -width, width, screenPoint.z );
   verts[2].point.set( width,  -width, screenPoint.z );
   verts[3].point.set( width,  width, screenPoint.z );

   verts[0].texCoord.set(0.0f,1.0f);
   verts[1].texCoord.set(0.0f,0.0f);
   verts[2].texCoord.set(1.0f,1.0f);
   verts[3].texCoord.set(1.0f,0.0f);

   ColorI bmColor;
   GFX->getDrawUtil()->getBitmapModulation(&bmColor);
   verts[0].color = verts[1].color = verts[2].color = verts[3].color = bmColor;

   if(spinAngle != 0.f)
   {
      MatrixF rotMatrix( EulerF( 0.0, 0.0, spinAngle ) );

      for( S32 i = 0; i < 4; i++ )
      {
         rotMatrix.mulP( verts[i].point );
         verts[i].point += offset;
      }
   }

   verts.unlock();
   GFX->setVertexBuffer( verts );

   //GFX->setStateBlock(mRectFillSB);
   GFX->setStateBlock(mRectTexSB);
   //GFX->setupGenericShaders();
   GFX->setupGenericShaders( GFXDevice::GSModColorTexture );

   GFX->drawPrimitive( GFXTriangleStrip, 0, 2 );
}

// You need this to change stuff when you edit it in the editor, I think.
DefineEngineMethod( RenderRTTExample, postApply, void, (),,
   "A utility method for forcing a network update.\n")
{
	object->inspectPostApply();
}