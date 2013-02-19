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

#ifndef _RENDERRTTEXAMPLE_H_
#define _RENDERRTTEXAMPLE_H_

#ifndef _SCENEOBJECT_H_
#include "scene/sceneObject.h"
#endif
#ifndef _GFXSTATEBLOCK_H_
#include "gfx/gfxStateBlock.h"
#endif
#ifndef _GFXVERTEXBUFFER_H_
#include "gfx/gfxVertexBuffer.h"
#endif
#ifndef _GFXPRIMITIVEBUFFER_H_
#include "gfx/gfxPrimitiveBuffer.h"
#endif

// Honestly I don't know why we have the #ifndef checks above.
// I have been getting away this this:
#include "materials/matTextureTarget.h"

#include "gfx/gfxTextureHandle.h"

#include "ts/tsShapeInstance.h"

#include "renderinstance/renderMeshMgr.h"

class BaseMatInstance;


//-----------------------------------------------------------------------------
// This class implements a basic SceneObject that can exist in the world at a
// 3D position and render itself. Note that RenderRTTExample handles its own
// rendering by submitting itself as an ObjectRenderInst (see
// renderInstance\renderPassmanager.h) along with a delegate for its render()
// function. However, the preffered rendering method in the engine is to submit
// a MeshRenderInst along with a Material, vertex buffer, primitive buffer, and
// transform and allow the RenderMeshMgr handle the actual rendering. You can
// see this implemented in RenderMeshExample.
//-----------------------------------------------------------------------------

class RenderRTTExample : public SceneObject
{
   typedef SceneObject Parent;

   // Networking masks
   // We need to implement at least one of these to allow
   // the client version of the object to receive updates
   // from the server version (like if it has been moved
   // or edited)
   enum MaskBits 
   {
      TransformMask = Parent::NextFreeMask << 0,
      UpdateMask    = Parent::NextFreeMask << 1,
      NextFreeMask  = Parent::NextFreeMask << 2
   };

   //--------------------------------------------------------------------------
   // Rendering variables
   //--------------------------------------------------------------------------
   // Define our vertex format here so we don't have to
   // change it in multiple spots later
   typedef GFXVertexPCN VertexType;

   // The handles for our StateBlocks
   GFXStateBlockRef mNormalSB;
   GFXStateBlockRef mReflectSB;
   GFXStateBlockRef mNoCullSB;

   // The GFX vertex and primitive buffers
   GFXVertexBufferHandle< VertexType > mVertexBuffer;

   /*
      A texture target is not a matter of if, but when.
      The target and texture MUST exist before the 
      objects that use them exist.  So having a texture 
      target in an object like this might have loading 
      order issues.  Making sure this object gets loaded
      before other objects is a must if this is to work.
      It may require a special loading order for the
      mission with more loading steps.  This might require
      the texture target holder objects to be loaded before
      the main mission file.
   */

   // Render to texture target
   NamedTexTarget mTextureTarget;
   // The texture to target
   GFXTexHandle mTextureBuffer;
   // The name of the texture to target
   String mTexTargetName;

   // objects to store supporting stuff
   GFXTexHandle mWarningTexture;

   // render management
   SceneRenderState *mState;
   TSShapeInstance *mShapeInstance;
   TSRenderState mRData;

   RenderPassManager *mRenderPass;
   RenderMeshMgr     *mMeshRenderBin;
   GFXTextureTarget *mRenderTarget;

   // 
   F32 mOrthoRadius;
   
   // animation variables
   F32 mRotParm1;
   F32 mRotParm2;
   F32 mRotParm3;
   F32 mRotParm4;

   // font for showing text
   Resource<GFont> mFont;

public:
   RenderRTTExample();
   virtual ~RenderRTTExample();

   // Declare this object as a ConsoleObject so that we can
   // instantiate it into the world and network it
   DECLARE_CONOBJECT(RenderRTTExample);

   //--------------------------------------------------------------------------
   // Object Editing
   // Since there is always a server and a client object in Torque and we
   // actually edit the server object we need to implement some basic
   // networking functions
   //--------------------------------------------------------------------------
   // Set up any fields that we want to be editable (like position)
   static void initPersistFields();

   // Handle when we are added to the scene and removed from the scene
   bool onAdd();
   void onRemove();

   // Override this so that we can dirty the network flag when it is called
   void setTransform( const MatrixF &mat );

   // This function handles sending the relevant data from the server
   // object to the client object
   U32 packUpdate( NetConnection *conn, U32 mask, BitStream *stream );
   // This function handles receiving relevant data from the server
   // object and applying it to the client object
   void unpackUpdate( NetConnection *conn, BitStream *stream );

   //--------------------------------------------------------------------------
   // Object Rendering
   // Torque utilizes a "batch" rendering system. This means that it builds a
   // list of objects that need to render (via RenderInst's) and then renders
   // them all in one batch. This allows it to optimized on things like
   // minimizing texture, state, and shader switching by grouping objects that
   // use the same Materials. For this example, however, we are just going to
   // get this object added to the list of objects that handle their own
   // rendering.
   //--------------------------------------------------------------------------
   // Create the geometry for rendering
   void createGeometry();

   // RTT support functions
   void rttBegin(RectI& vpDims, bool isOrtho=false, F32 orthoRadius=1.0f);
   void rttEnd();

   // This is the function that allows this object to submit itself for rendering
   void prepRenderImage( SceneRenderState *state );

   // This is the function that actually gets called to do the rendering
   // Note that there is no longer a predefined name for this function.
   // Instead, when we submit our ObjectRenderInst in prepRenderImage(),
   // we bind this function as our rendering delegate function
   void render( ObjectRenderInst *ri, SceneRenderState *state, BaseMatInstance *overrideMat );

   // method to signal stuff the editor or otherwise changes
   void inspectPostApply();
   // stuff that needs updating 
   void updateStuff();

   // time stuff
   virtual void advanceTime( F32 dt );
   
};

#endif // _RENDERRTTEXAMPLE_H_