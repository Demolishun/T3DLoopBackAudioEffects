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

   return true;
}

void RenderRTTExample::onRemove()
{
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
   mNormalSB = GFX->createStateBlock( desc );

   // The reflection needs its culling reversed
   desc.cullDefined = true;
   desc.cullMode = GFXCullCW;
   mReflectSB = GFX->createStateBlock( desc );
}

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

      // Save previous render settings.
      // This preserves transforms and restores 
      // them when this object goes out of scope.
      GFXTransformSaver transSaver;
      GFXFrustumSaver frustSaver;
      
      // prepare render texture
      GFXTextureTargetRef mGFXTextureTarget;
      mGFXTextureTarget = GFX->allocRenderToTextureTarget();        
      mGFXTextureTarget->attachTexture(GFXTextureTarget::Color0,tmpTexHandle);
      // save render target and set new render target
      GFX->pushActiveRenderTarget();      
      GFX->setActiveRenderTarget(mGFXTextureTarget);

      // setup transforms
      GFX->setWorldMatrix(MatrixF::Identity); 
      GFX->setProjectionMatrix(MatrixF::Identity);
      GFX->setViewMatrix(MatrixF::Identity);

      // notes:
      // ImposterCapture::begin

      // set ortho for 2D
      MatrixF outMat;
      MathUtils::makeOrthoProjection(&outMat,-1.0f,-1.0f,1.0f,1.0f,0.1,-10,false);
      GFX->setOrtho(-1.0f,-1.0f,1.0f,1.0f,0.1f,-10.0f);
      GFX->setProjectionMatrix(outMat);      

      // draw background
      GFX->getDrawUtil()->draw2DSquare(Point2F(0.1f,0.1f),tmpTexHandle->getWidth()*0.9f,0.0f);

      // setup frustrum
      F32 left, right, top, bottom;
      F32 fnear = 0.01f;
      F32 ffar = -10.0f;
      // handy dandy tool!
      MathUtils::makeFrustum( &left, &right, &top, &bottom, M_HALFPI_F, 1.0f, fnear );      
      GFX->setFrustum( left, right, bottom, top, fnear, ffar );

      // setup more crap
      GFX->clear(GFXClearTarget,ColorI(0,0,0),1.0f,0);
      // might need to clear z buffer
      // GFX->clear(GFXClearTarget|GFXClearZBuffer|GFXClearStencil,ColorI(0,0,0),1.0f,0);
      GFX->setStateBlock( mNormalSB );          
      GFX->setupGenericShaders( GFXDevice::GSModColorTexture );  

      // setup transforms
      GFX->setWorldMatrix(MatrixF::Identity); 
      GFX->setProjectionMatrix(MatrixF::Identity);
      GFX->setViewMatrix(MatrixF::Identity);

      // now lets render something
      if(0)
      {      
         // lets use the warning texture for stuff that needs a texture
         //GFX->setTexture(0, mWarningTexture);      

         // debug draw the frustum
         GFX->getDrawUtil()->drawFrustum(GFX->getFrustum(),ColorI(255,0,0));

         // drawing a line
         GFX->getDrawUtil()->drawLine(Point2F(-1.0f,0.0f),Point2F(1.0f,0.0f),ColorI(255,255,255));

         //GFX->getDrawUtil()->drawBitmap(mWarningTexture,Point2F(-1.0,-1.0));
         RectF destR(Point2F(0.0,0.0),Point2F(1.0,1.0));
         GFX->getDrawUtil()->drawBitmapStretch(mWarningTexture,destR,GFXBitmapFlip_Y);
      }
            
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

// You need this to change stuff when you edit it in the editor, I think.
DefineEngineMethod( RenderRTTExample, postApply, void, (),,
   "A utility method for forcing a network update.\n")
{
	object->inspectPostApply();
}