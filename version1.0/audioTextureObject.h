#ifndef _AUDIO_TEXTURE_OBJECT_H_
#define _AUDIO_TEXTURE_OBJECT_H_

#include "scene/sceneObject.h"

#include "materials/shaderData.h"
#include "materials/matTextureTarget.h"

#include "gfx/gfxTextureHandle.h"
#include "gfx/gfxTarget.h"
#include "gfx/gfxVertexBuffer.h"

#include "gui/core/guiTypes.h"

// Object that can display sampled data on a texture
class AudioTextureObject : public SceneObject
{
private:
   typedef SceneObject Parent;

   enum MaskBits 
   {
      TransformMask = Parent::NextFreeMask << 0,
      UpdateMask    = Parent::NextFreeMask << 1,
      NextFreeMask  = Parent::NextFreeMask << 2
   };

   // buffers and pointers to texture
   //    texture writing is double buffered
   GFXTexHandle   mTextureBuffer1; 
   GFXTexHandle   mTextureBuffer2;
   GFXTexHandle   mWarningTexture;
   GFXTextureObject* mTexture; // 

   // The size in pixels of the backing texture
   S32 mTexSize;

   // texture reference name for use in materials
   //    eg: diffuseMap[0] = "#MyWebTexture";
   String mTextureName;  

   // texture target reg object
   NamedTexTarget* mTextureTarget; 

   GFXTextureTargetRef mGFXTextureTarget;

   // enable rendering texture onto an object in the scene
   //    this will default to false to prevent rendering when used as a texture source for materials
   //    the texture will be mapped to a simple quad
   bool mEnableRender;  

   // The name of the Material we will use for rendering
   //String            mMaterialName;
   // The actual Material instance
   //aseMatInstance*  mMaterialInst;

   // render variables
   //typedef GFXVertexPCN VertexType;
   typedef GFXVertexPNT VertexType;

   // The handles for our StateBlocks
   GFXStateBlockRef mNormalSB;
   GFXStateBlockRef mReflectSB;

   // The GFX vertex and primitive buffers
   GFXVertexBufferHandle< VertexType > mVertexBuffer;
   //GFXPrimitiveBufferHandle            mPrimitiveBuffer;

   // shader
   GFXShaderRef mShader;

   // gui profile for fonts and stuff
   GuiControlProfile* mProfile;
   String mProfileName;

public:
   AudioTextureObject();
   virtual ~AudioTextureObject();
   
   // get the current texture handle
   GFXTextureObject* getTexture(){
      return mTexture;
   };

   // Allows the object to update its editable settings
   // from the server object to the client
   virtual void inspectPostApply();

   // Set up any fields that we want to be editable (like position or render enable)
   static void initPersistFields();

   // custom drawing methods
   void drawTriLine( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color, F32 thickness = 0.1f );
   void drawLine( F32 x1, F32 y1, F32 x2, F32 y2, const ColorI &color );
   void drawLine( F32 x1, F32 y1, F32 z1, F32 x2, F32 y2, F32 z2, const ColorI &color );

   // Handle when we are added to the scene and removed from the scene
   bool onAdd();
   void onRemove();

   // Override this so that we can dirty the network flag when it is called
   void setTransform( const MatrixF &mat );

   // networking
   U32 packUpdate( NetConnection *conn, U32 mask, BitStream *stream );      
   void unpackUpdate( NetConnection *conn, BitStream *stream );

   // Create the geometry for rendering
   //    would used for debug only
   void createGeometry();

   void updateMaterial();

   // This is the function that allows this object to submit itself for rendering
   void prepRenderImage( SceneRenderState *state );

   // render the bitmap
   void render( ObjectRenderInst *ri, SceneRenderState *state, BaseMatInstance *overrideMat );   

   DECLARE_CONOBJECT(AudioTextureObject);
};

class NamedTexTargetObject : public SimObject
{
private:
   typedef SimObject Parent;

   // name used to define NameTexTarget
   String mTexTargetName;
   // actual texture target
   NamedTexTarget mTexTarget;

public:
   NamedTexTargetObject();
   virtual ~NamedTexTargetObject();

   static void initPersistFields();

   virtual bool onAdd();
   virtual void onRemove();

   /*
   NamedTexTarget* getNamedTexTarget(){
      return &mTexTarget;
   }
   */

   DECLARE_CONOBJECT(NamedTexTargetObject);
};

#endif  // _AUDIO_TEXTURE_OBJECT_H_