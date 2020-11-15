#define NOMINMAX

// API Abstraction
#include "PrimeEngine/APIAbstraction/APIAbstractionDefines.h"

// Outer-Engine includes

// Inter-Engine includes
#include "../../Lua/LuaEnvironment.h"
#include "../../Lua/EventGlue/EventDataCreators.h"

#include "PrimeEngine/Render/ShaderActions/SetPerObjectGroupConstantsShaderAction.h"
#include "PrimeEngine/Render/ShaderActions/SetPerFrameConstantsShaderAction.h"
#include "PrimeEngine/Render/ShaderActions/SA_Bind_Resource.h"
#include "PrimeEngine/Render/ShaderActions/SetInstanceControlConstantsShaderAction.h"
#include "PrimeEngine/Render/ShaderActions/SA_SetAndBind_ConstResource_PerInstanceData.h"
#include "PrimeEngine/Render/ShaderActions/SA_SetAndBind_ConstResource_SingleObjectAnimationPalette.h"
#include "PrimeEngine/Render/ShaderActions/SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes.h"

#include "PrimeEngine/APIAbstraction/GPUBuffers/AnimSetBufferGPU.h"
#include "PrimeEngine/APIAbstraction/Texture/GPUTextureManager.h"
#include "PrimeEngine/Scene/DrawList.h"
#include "PrimeEngine/Scene/RootSceneNode.h"
#include "PrimeEngine/Scene/MeshInstance.h"
#include "PrimeEngine/Scene/CameraSceneNode.h"
#include "PrimeEngine/Scene/CameraManager.h"
#include "PrimeEngine/APIAbstraction/GPUMaterial/GPUMaterialSet.h"
#include "PrimeEngine/Scene/SH_DRAW.h"
#include "PrimeEngine/Scene/SkeletonInstance.h"
#include "PrimeEngine/Scene/DefaultAnimationSM.h"

// Sibling/Children includes
#include "EffectManager.h"
#include "Effect.h"

extern int g_disableSkinRender;
extern int g_iDebugBoneSegment;

namespace PE {

using namespace Components;

Handle EffectManager::s_myHandle;
	
EffectManager::EffectManager(PE::GameContext &context, PE::MemoryArena arena)
	: m_map(context, arena, 128)
	, m_pixelShaderSubstitutes(context, arena)
	, m_pCurRenderTarget(NULL)
	, m_pixelShaders(context, arena, 256)
	, m_vertexShaders(context, arena, 256)
#if APIABSTRACTION_D3D11
		, m_cbuffers(context, arena, 64)
	#endif
	, m_doMotionBlur(false)
	, m_glowSeparatedTextureGPU(context, arena)
	, m_2ndGlowTargetTextureGPU(context, arena)
	, m_shadowMapDepthTexture(context, arena)
	, m_MirrorTargetTextureGPU(context, arena)
	, m_ReflectionTargetTextureGPU(context, arena)
	, m_frameBufferCopyTexture(context, arena)
{
	m_arena = arena; m_pContext = &context;
}
void EffectManager::setupConstantBuffersAndShaderResources()
{
#if PE_PLAT_IS_PSVITA
	
#elif APIABSTRACTION_D3D9

#	elif APIABSTRACTION_D3D11

		D3D11Renderer *pD3D11Renderer = static_cast<D3D11Renderer *>(m_pContext->getGPUScreen());
		ID3D11Device *pDevice = pD3D11Renderer->m_pD3DDevice;
		ID3D11DeviceContext *pDeviceContext = pD3D11Renderer->m_pD3DContext;

		ID3D11Buffer *pBuf;
		{
			//cbuffers
			HRESULT hr;
			D3D11_BUFFER_DESC cbDesc;
			cbDesc.Usage = D3D11_USAGE_DYNAMIC; // can only write to it, can't read
			cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // make sure we can access it with cpu for writing only
			cbDesc.MiscFlags = 0;
		
			cbDesc.ByteWidth = sizeof( SetPerFrameConstantsShaderAction::Data );

			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf );
			assert(SUCCEEDED(hr));
			SetPerFrameConstantsShaderAction::s_pBuffer = pBuf;
		
			m_cbuffers.add("cbPerFrame", pBuf);

			cbDesc.ByteWidth = sizeof( SetPerObjectGroupConstantsShaderAction::Data );
			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
		
			SetPerObjectGroupConstantsShaderAction::s_pBuffer = pBuf;
			m_cbuffers.add("cbPerObjectGroup", pBuf);

			cbDesc.ByteWidth = sizeof( SetPerObjectConstantsShaderAction::Data );
			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
			SetPerObjectConstantsShaderAction::s_pBuffer = pBuf;
			m_cbuffers.add("cbPerObject", pBuf);

			cbDesc.ByteWidth = sizeof( SetPerMaterialConstantsShaderAction::Data );
			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
			SetPerMaterialConstantsShaderAction::s_pBuffer = pBuf;
			m_cbuffers.add("cbPerMaterial", pBuf);

			cbDesc.ByteWidth = sizeof( SetInstanceControlConstantsShaderAction::Data );
			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
			SetInstanceControlConstantsShaderAction::s_pBuffer = pBuf;
			m_cbuffers.add("cbInstanceControlConstants", pBuf);
		}

		{
			// cbDesc.Usage = D3D11_USAGE_DEFAULT can't have cpu access flag set

			// if need cpu access for writing (using map) usage must be DYNAMIC or STAGING. resource cant be set as output
			// if need cpu access for reading and writing (using map) usage must be STAGING. resource cant be set as output
			// note, map & DYNAMIC is used for resrouces that get updated at least once per frame
			// also note that map allows updating part of resource while gpu might be using other part
			// can use UpdateSubresource with DEFAULT or IMMUTABLE, but suggested to use only with DEFAULT
			
			// if usage is default (gpu reads and writes) then UpdateSubresource() is used to write to it from cpu
			
			
			//tbuffers
			D3D11_BUFFER_DESC cbDesc;
			cbDesc.Usage = D3D11_USAGE_DYNAMIC; // can only write to it, can't read
			cbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE; //means will need shader resource view // D3D11_BIND_CONSTANT_BUFFER;
			cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // make sure we can access it with cpu for writing only
#if PE_DX11_USE_STRUCTURED_BUFFER_INSTEAD_OF_TBUFFER
			cbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			cbDesc.StructureByteStride = sizeof(Matrix4x4);
#else
			cbDesc.MiscFlags = 0;
			cbDesc.StructureByteStride = sizeof(Vector4);
#endif
			cbDesc.ByteWidth = sizeof(SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes::Data);
			HRESULT hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
			SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes::s_pBuffer = pBuf;
			

			cbDesc.ByteWidth = sizeof(SA_SetAndBind_ConstResource_SingleObjectAnimationPalette::Data);
			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
			SA_SetAndBind_ConstResource_SingleObjectAnimationPalette::s_pBufferSinglePalette = pBuf;


			cbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			cbDesc.StructureByteStride = sizeof(SA_SetAndBind_ConstResource_PerInstanceData::PerObjectInstanceData);
			cbDesc.ByteWidth = sizeof( SA_SetAndBind_ConstResource_PerInstanceData::Data );
			hr = pDevice->CreateBuffer( &cbDesc, NULL, &pBuf);
			assert(SUCCEEDED(hr));
			SA_SetAndBind_ConstResource_PerInstanceData::s_pBuffer = pBuf;


			D3D11_SHADER_RESOURCE_VIEW_DESC vdesc;
#if PE_DX11_USE_STRUCTURED_BUFFER_INSTEAD_OF_TBUFFER
			vdesc.Format = DXGI_FORMAT_UNKNOWN;
			vdesc.Buffer.NumElements = sizeof(SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes::Data) / (4*4*4);
#else
			vdesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			vdesc.Buffer.NumElements = sizeof(SetPerObjectAnimationConstantsShaderAction::Data) / (4*4);
#endif
			vdesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			vdesc.Buffer.ElementOffset = 0;
			
			hr = pDevice->CreateShaderResourceView(SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes::s_pBuffer, &vdesc, &SA_SetAndBind_ConstResource_InstancedObjectsAnimationPalettes::s_pShaderResourceView);
			assert(SUCCEEDED(hr));
			
#if PE_DX11_USE_STRUCTURED_BUFFER_INSTEAD_OF_TBUFFER
			vdesc.Buffer.NumElements = sizeof(SA_SetAndBind_ConstResource_SingleObjectAnimationPalette::Data) / (4*4*4);
#else
			vdesc.Buffer.NumElements = sizeof(SA_SetAndBind_ConstResource_SingleObjectAnimationPalette::Data) / (4*4);
#endif
			hr = pDevice->CreateShaderResourceView(SA_SetAndBind_ConstResource_SingleObjectAnimationPalette::s_pBufferSinglePalette, &vdesc, &SA_SetAndBind_ConstResource_SingleObjectAnimationPalette::s_pShaderResourceViewSinglePalette);
			assert(SUCCEEDED(hr));


			vdesc.Buffer.NumElements = sizeof(SA_SetAndBind_ConstResource_PerInstanceData::Data) / sizeof(SA_SetAndBind_ConstResource_PerInstanceData::PerObjectInstanceData);
			hr = pDevice->CreateShaderResourceView(SA_SetAndBind_ConstResource_PerInstanceData::s_pBuffer, &vdesc, &SA_SetAndBind_ConstResource_PerInstanceData::s_pShaderResourceView);
			assert(SUCCEEDED(hr));
		}

		AnimSetBufferGPU::createGPUBufferForAnimationCSResult(*m_pContext);
#	endif
}

void EffectManager::createSetShadowMapShaderValue(PE::Components::DrawList *pDrawList)
{
	Handle &h = pDrawList->nextGlobalShaderValue();
	h = Handle("SA_Bind_Resource", sizeof(SA_Bind_Resource));
	SA_Bind_Resource *p = new(h) SA_Bind_Resource(*m_pContext, m_arena);
	p->set(SHADOWMAP_TEXTURE_2D_SAMPLER_SLOT, m_shadowMapDepthTexture.m_samplerState, API_CHOOSE_DX11_DX9_OGL_PSVITA(m_shadowMapDepthTexture.m_pDepthShaderResourceView, m_shadowMapDepthTexture.m_pTexture, m_shadowMapDepthTexture.m_texture, m_shadowMapDepthTexture.m_texture));
}

void EffectManager::buildFullScreenBoard()
{
	//todo: use createBillboard() functionality of cpu buffers
	PositionBufferCPU vbcpu(*m_pContext, m_arena);
	float fw = (float)(m_pContext->getGPUScreen()->getWidth());
	float fh = (float)(m_pContext->getGPUScreen()->getHeight());
	float fx = -1.0f / fw / 2.0f;
	float fy = 1.0f / fh / 2.0f;

	vbcpu.createNormalizeBillboardCPUBufferXYWithPtOffsets(fx, fy);
	
	ColorBufferCPU tcbcpu(*m_pContext, m_arena);
	tcbcpu.m_values.reset(3 * 4);
	#if APIABSTRACTION_OGL
		// flip up vs down in ogl
		tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(0.0f);
		tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(0.0f);
		tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(0.0f);
		tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(0.0f);
	#else
		tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(0.0f);
		tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(0.0f);
		tcbcpu.m_values.add(1.0f); tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(0.0f);
		tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(0.0f); tcbcpu.m_values.add(0.0f);
	#endif
	
	IndexBufferCPU ibcpu(*m_pContext, m_arena);
	ibcpu.createBillboardCPUBuffer();

	m_hVertexBufferGPU = Handle("VERTEX_BUFFER_GPU", sizeof(VertexBufferGPU));
	VertexBufferGPU *pvbgpu = new(m_hVertexBufferGPU) VertexBufferGPU(*m_pContext, m_arena);
	pvbgpu->createGPUBufferFromSource_ColoredMinimalMesh(vbcpu, tcbcpu);

	m_hIndexBufferGPU = Handle("INDEX_BUFFER_GPU", sizeof(IndexBufferGPU));
	IndexBufferGPU *pibgpu = new(m_hIndexBufferGPU) IndexBufferGPU(*m_pContext, m_arena);
	pibgpu->createGPUBuffer(ibcpu);

	m_hFirstGlowPassEffect = getEffectHandle("firstglowpass.fx");
	m_hSecondGlowPassEffect = getEffectHandle("verticalblur.fx");
	m_hGlowSeparationEffect = getEffectHandle("glowseparationpass.fx");
	m_hMotionBlurEffect = getEffectHandle("motionblur.fx");
	m_hColoredMinimalMeshTech = getEffectHandle("ColoredMinimalMesh_Tech");
}

void EffectManager::setFrameBufferCopyRenderTarget()
{
	#if APIABSTRACTION_D3D9
		m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(&m_frameBufferCopyTexture, true);
	#elif APIABSTRACTION_OGL
		assert(!"not implemented");
	#elif APIABSTRACTION_D3D11

		m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(&m_frameBufferCopyTexture, true);
	#endif
}

void EffectManager::setShadowMapRenderTarget()
{
	if (m_pCurRenderTarget)
	{
		assert(!"There should be no active render target when we set shadow map as render target!");
	}

#if APIABSTRACTION_D3D9
	m_pContext->getGPUScreen()->setDepthStencilOnlyRenderTargetAndViewport(&m_shadowMapDepthTexture, true);
	m_pCurRenderTarget = &m_shadowMapDepthTexture;
#elif APIABSTRACTION_D3D11
	m_pContext->getGPUScreen()->setDepthStencilOnlyRenderTargetAndViewport(&m_shadowMapDepthTexture, true);
	m_pCurRenderTarget = &m_shadowMapDepthTexture;
#elif APIABSTRACTION_OGL
	m_pContext->getGPUScreen()->setDepthStencilOnlyRenderTargetAndViewport(&m_shadowMapDepthTexture, true);
	m_pCurRenderTarget = &m_shadowMapDepthTexture;
#endif
}

void EffectManager::endCurrentRenderTarget()
{
	m_pContext->getGPUScreen()->endRenderTarget(m_pCurRenderTarget);
	m_pCurRenderTarget = NULL;
}

void EffectManager::setTextureAndDepthTextureRenderTargetForGlow(bool clearTarget, bool clearZbuffer)
{
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithDepth(m_hGlowTargetTextureGPU.getObject<TextureGPU>(), m_hGlowTargetTextureGPU.getObject<TextureGPU>(), clearTarget, clearZbuffer);
	m_pCurRenderTarget = m_hGlowTargetTextureGPU.getObject<TextureGPU>();
}

void EffectManager::setTextureAndDepthTextureRenderTargetForMirror()
{
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithDepth(&m_MirrorTargetTextureGPU, &m_MirrorTargetTextureGPU, true, true);
	m_pCurRenderTarget = &m_MirrorTargetTextureGPU;
}

void EffectManager::setTextureAndDepthTextureRenderTargetForReflection()
{
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithDepth(&m_ReflectionTargetTextureGPU, &m_ReflectionTargetTextureGPU, true, true);
	m_pCurRenderTarget = &m_ReflectionTargetTextureGPU;
}


void EffectManager::setTextureAndDepthTextureRenderTargetForDefaultRendering()
{
	// use device back buffer and depth
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithDepth(0, 0, true, true);
}


void EffectManager::set2ndGlowRenderTarget()
{
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(&m_2ndGlowTargetTextureGPU, true);
	m_pCurRenderTarget = &m_2ndGlowTargetTextureGPU;
}


void EffectManager::drawFullScreenQuad()
{
	Effect &curEffect = *m_hColoredMinimalMeshTech.getObject<Effect>();

	if (!curEffect.m_isReady)
		return;

	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();

	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);

	TextureGPU *pTex = PE::GPUTextureManager::Instance()->getRandomTexture().getObject<TextureGPU>();

 	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, pTex->m_samplerState, API_CHOOSE_DX11_DX9_OGL_PSVITA(pTex->m_pShaderResourceView, pTex->m_pTexture, pTex->m_texture, pTex->m_texture));
 	setTextureAction.bindToPipeline(&curEffect);

	PE::SetPerFrameConstantsShaderAction sa(*m_pContext, m_arena);
	sa.m_data.gGameTime = 1.0f;

	sa.bindToPipeline(&curEffect);

	PE::SetPerObjectConstantsShaderAction objSa;
	objSa.m_data.gW = Matrix4x4();
	objSa.m_data.gW.loadIdentity();

	static float x = 0;
	//objSa.m_data.gW.importScale(0.5f, 1.0f, 1.0f);
	objSa.m_data.gW.setPos(Vector3(x, 0, 1.0f));
	//x+=0.01;
	if (x > 1)
		x = 0;
	objSa.m_data.gWVP = objSa.m_data.gW;

	objSa.bindToPipeline(&curEffect);

	pibGPU->draw(1, 0);

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);

	sa.unbindFromPipeline(&curEffect);
	objSa.unbindFromPipeline(&curEffect);

	setTextureAction.unbindFromPipeline(&curEffect);

}

// this function has to be called right after rendering scene into render target
// the reason why mipmaps have to be generated is because we separate glow
// into a smaller texture, so we need to generate mipmaps so that when we sample glow, it is not aliased
// if mipmaps are not generated, but do exist glow might not work at all since a lower mip will be sampled
void EffectManager::drawGlowSeparationPass()
{
	Effect &curEffect = *m_hGlowSeparationEffect.getObject<Effect>();

	if (!curEffect.m_isReady)
		return;
//todo: generate at least one mipmap on all platforms so that glow downsampling is smoother
// in case it doesnt look smooth enough. as long as we are nto donwsampling to ridiculous amount it should be ok without mipmaps
/*

		m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_pTexture->GenerateMipSubLevels();

#if APIABSTRACTION_D3D11

			D3D11Renderer *pD3D11Renderer = static_cast<D3D11Renderer *>(m_pContext->getGPUScreen());
			ID3D11Device *pDevice = pD3D11Renderer->m_pD3DDevice;
			ID3D11DeviceContext *pDeviceContext = pD3D11Renderer->m_pD3DContext;

			pDeviceContext->GenerateMips(
			m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_pShaderResourceView);
#endif		
*/
	
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(&m_glowSeparatedTextureGPU, true);

	m_pCurRenderTarget = &m_glowSeparatedTextureGPU;

	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();
	
	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);
	
	TextureGPU *t = m_hGlowTargetTextureGPU.getObject<TextureGPU>();
	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, t->m_samplerState, API_CHOOSE_DX11_DX9_OGL(t->m_pShaderResourceView, t->m_pTexture,t->m_texture));
	setTextureAction.bindToPipeline(&curEffect);

	PE::SetPerObjectConstantsShaderAction objSa;
	objSa.m_data.gW = Matrix4x4();
	objSa.m_data.gW.loadIdentity();
	objSa.m_data.gWVP = objSa.m_data.gW;

	objSa.bindToPipeline(&curEffect);
	
	pibGPU->draw(1, 0);

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);

	setTextureAction.unbindFromPipeline(&curEffect);
	objSa.unbindFromPipeline(&curEffect);
}

void EffectManager::drawFirstGlowPass()
{
	Effect &curEffect = *m_hFirstGlowPassEffect.getObject<Effect>();

	if (!curEffect.m_isReady)
		return;
	
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(&m_2ndGlowTargetTextureGPU, true);

	m_pCurRenderTarget = &m_2ndGlowTargetTextureGPU;

	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();
	
	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);

	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, m_glowSeparatedTextureGPU.m_samplerState, API_CHOOSE_DX11_DX9_OGL(m_glowSeparatedTextureGPU.m_pShaderResourceView, m_glowSeparatedTextureGPU.m_pTexture, m_glowSeparatedTextureGPU.m_texture));
	setTextureAction.bindToPipeline(&curEffect);

	PE::SetPerObjectConstantsShaderAction objSa;
	objSa.m_data.gW = Matrix4x4();
	objSa.m_data.gW.loadIdentity();
	objSa.m_data.gWVP = objSa.m_data.gW;

	objSa.bindToPipeline(&curEffect);

	pibGPU->draw(1, 0);

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);

	setTextureAction.unbindFromPipeline(&curEffect);
	objSa.unbindFromPipeline(&curEffect);
}

void EffectManager::drawSecondGlowPass()
{
	Effect &curEffect = *m_hSecondGlowPassEffect.getObject<Effect>();
	if (!curEffect.m_isReady)
		return;
	
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>(), true);
	
	m_pCurRenderTarget = m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>();
	
	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();
	
	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);

	PE::SA_Bind_Resource setBlurTextureAction(*m_pContext, m_arena, DIFFUSE_BLUR_TEXTURE_2D_SAMPLER_SLOT, m_2ndGlowTargetTextureGPU.m_samplerState, API_CHOOSE_DX11_DX9_OGL(m_2ndGlowTargetTextureGPU.m_pShaderResourceView, m_2ndGlowTargetTextureGPU.m_pTexture, m_2ndGlowTargetTextureGPU.m_texture));
	setBlurTextureAction.bindToPipeline(&curEffect);

	TextureGPU *t = m_hGlowTargetTextureGPU.getObject<TextureGPU>();
	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, t->m_samplerState, API_CHOOSE_DX11_DX9_OGL(t->m_pShaderResourceView, t->m_pTexture, t->m_texture));
	setTextureAction.bindToPipeline(&curEffect);

	PE::SetPerObjectConstantsShaderAction objSa;
	objSa.m_data.gW = Matrix4x4();
	objSa.m_data.gW.loadIdentity();
	objSa.m_data.gWVP = objSa.m_data.gW;

	objSa.bindToPipeline(&curEffect);


	// the effect blurs vertically the horizontally blurred glow and adds it to source
	pibGPU->draw(1, 0);

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);

	setBlurTextureAction.unbindFromPipeline(&curEffect);
	setTextureAction.unbindFromPipeline(&curEffect);
	objSa.unbindFromPipeline(&curEffect);
}

void EffectManager::drawMirrorPass()
{
	



}
void EffectManager::drawReflectionPass()
{

	Effect& curEffect = *m_hReflectionEffect.getObject<Effect>();
	if (!curEffect.m_isReady)
		return;

	EffectManager::Instance()->setTextureAndDepthTextureRenderTargetForGlow(false, false);

	// find mesh
	RootSceneNode* proot = RootSceneNode::Instance();
	Mesh* pMesh = proot->GetMeshForEffect("StdMesh_Reflected_Tech");
	if (!pMesh)
		return;

	// index buffer
	Handle hIBuf = pMesh->m_hIndexBufferGPU;
	IndexBufferGPU* pibGPU = hIBuf.getObject<IndexBufferGPU>();
	//pibGPU->setAsCurrent();

	// vertex buf
	VertexBufferGPU* pvbGPU;
	pvbGPU = pMesh->m_vertexBuffersGPUHs[0].getObject<VertexBufferGPU>();
	//pvbGPU->setAsCurrent(&curEffect);
	
	// Check for vertex buffer(s)
	Handle hVertexBuffersGPU[4]; // list of bufers to pass to GPU
	Vector4 vbufWeights;
	int numVBufs = pMesh->m_vertexBuffersGPUHs.m_size;
	assert(numVBufs < 4);
	for (int ivbuf = 0; ivbuf < numVBufs; ivbuf++)
	{
		hVertexBuffersGPU[ivbuf] = pMesh->m_vertexBuffersGPUHs[ivbuf];
		vbufWeights.m_values[ivbuf] = hVertexBuffersGPU[ivbuf].getObject<VertexBufferGPU>()->m_weight;
	}

	if (numVBufs > 1)
	{
		for (int ivbuf = numVBufs; ivbuf < 4; ivbuf++)
		{
			hVertexBuffersGPU[ivbuf] = hVertexBuffersGPU[0];
			vbufWeights.m_values[ivbuf] = vbufWeights.m_values[0];
		}
		numVBufs = 4; // blend shape shader works with 4 shapes. so we extend whatever slots with base
	}

	PE::Handle hSA("SA_Bind_Resource", sizeof(SA_Bind_Resource));
	PE::Handle hSPMCSA("SetPerMaterialConstantsShaderAction", sizeof(SetPerMaterialConstantsShaderAction));
	PE::Handle hSPOCSA("SetPerObjectConstantsShaderAction", sizeof(SetPerObjectConstantsShaderAction));

	// Check for material set
	if (!pMesh->m_hMaterialSetGPU.isValid())
		return;

	// draw all pixel ranges with different materials
	PrimitiveTypes::UInt32 numRanges = MeshHelpers::getNumberOfRangeCalls(pibGPU);

	for (PrimitiveTypes::UInt32 iRange = 0; iRange < numRanges; iRange++)
	{
		// we might have several passes (several effects) so we need to check which effect list to use
		PEStaticVector<PE::Handle, 4>* pEffectsForRange = NULL;

		IndexBufferGPU* pibGPU = hIBuf.getObject<IndexBufferGPU>();

		IndexRange& ir = pibGPU->m_indexRanges[iRange];
		bool hasJointSegments = ir.m_boneSegments.m_size > 0;


		{
			//if instance count is 1, then regular effects are preferred
			//if instance count is > 1 then, if instance effect is available use it, otherwise use normal effect instanceCount times

			//first of all, return if have no appropriate effects
			if (pMesh->m_effects[iRange].m_size == 0 && pMesh->m_instanceEffects[iRange].m_size == 0) // this effect does not render in normal passes
			{
				return;
			}

			pEffectsForRange = &pMesh->m_effects[iRange];

			// check that have normal (non-instance) effects that are all valid
			// try to use effect for instances, otherwise will have to render objects one at a time
			bool haveEffect = pMesh->m_effects[iRange].m_size > 0;
			for (unsigned int iPass = 0; iPass < pMesh->m_effects[iRange].m_size; ++iPass)
			{
				if (!pMesh->m_effects[iRange][iPass].isValid())
				{
					haveEffect = false;
					break;
				}
			}
			PEASSERT(haveEffect, "Can't find an effect to render asset with. Note there is only one instance of the asset " \
				"and default behavior is to use no instance version of effect. Potentially we could add code to use instanced version of effect even if there is only one instance rendered");
			if (!haveEffect)
			{
				// some are not valid or don't have effects at all
				return;
			}
			
		}

		CameraSceneNode* pcam = CameraManager::Instance()->getActiveCamera()->getCamSceneNode();
		Matrix4x4 evtProjectionViewWorldMatrix = pcam->m_viewToProjectedTransform * pcam->m_worldToViewTransform;

		GPUMaterialSet* pGpuMatSet = pMesh->m_hMaterialSetGPU.getObject<GPUMaterialSet>();
		GPUMaterial& curMat = pGpuMatSet->m_materials[iRange];

		for (PrimitiveTypes::UInt32 iEffect = 0; iEffect < pEffectsForRange->m_size; ++iEffect)
		{
			Handle hEffect = (*pEffectsForRange)[iEffect];
			Effect* pEffect = hEffect.getObject<Effect>();

			int numRenderGroups = pMesh->m_numVisibleInstances;

			// each render group is group of instances that can be submitted at once
			// when there is no instancing effect
			int iSrcInstance = 0; // tracks next instance index to be submitted. we might skip meshes based on culling
			// so this value might not be equal to iRender group even for non-instanced case
			for (int iRenderGroup = 0; iRenderGroup < numRenderGroups; ++iRenderGroup)
			{
				Handle hLodIB = hIBuf;
				IndexBufferGPU* pLodibGPU = pibGPU;
				Handle hLODVB[4];
				PEASSERT(numVBufs < 4, "Too many vbs");

				PE::Handle* pHVBs = &hVertexBuffersGPU[0];

				for (int ivbuf = 0; ivbuf < numVBufs; ivbuf++)
				{
					hLODVB[ivbuf] = pHVBs[ivbuf];
				}

				PrimitiveTypes::UInt32 numJointSegments = hasJointSegments ? ir.m_boneSegments.m_size : 1;

				if (g_disableSkinRender && hasJointSegments)
					numJointSegments = 0;

				int iSrcInstanceInBoneSegment = 0;
				for (PrimitiveTypes::UInt32 _iBoneSegment = 0; _iBoneSegment < numJointSegments; _iBoneSegment++)
				{
					PrimitiveTypes::UInt32 iBoneSegment = _iBoneSegment;
					if (g_iDebugBoneSegment >= 0 && g_iDebugBoneSegment < numJointSegments)
					{
						iBoneSegment = g_iDebugBoneSegment;
						if (_iBoneSegment) break;
					}

					iSrcInstanceInBoneSegment = iSrcInstance; // reset instance id for each bone segment since we want to process same instances
					while (pMesh->m_instances[iSrcInstanceInBoneSegment].getObject<MeshInstance>()->m_culledOut)
						++iSrcInstanceInBoneSegment;

					if (hLodIB.isValid())
						pLodibGPU->setAsCurrent();
						
					

					if (numVBufs == 1)
					{
						VertexBufferGPU* pCurVertBuf = hLODVB[0].getObject<VertexBufferGPU>();
						pCurVertBuf->setAsCurrent(pEffect);
					}


					for (int ivbuf = 0; ivbuf < numVBufs; ivbuf++)
					{
						pvbGPU->setAsCurrent(pEffect);
					}

					pEffect->setCurrent(pvbGPU);

					// shader actions
					for (PrimitiveTypes::UInt32 itex = 0; itex < curMat.m_textures.m_size; itex++)
					{
						Handle hCurTex = curMat.m_textures[itex];
						TextureGPU& curTex = *hCurTex.getObject<TextureGPU>();

						SA_Bind_Resource* pSetTextureAction = new(hSA) SA_Bind_Resource(*m_pContext, m_arena);

						pSetTextureAction->set(
							DIFFUSE_TEXTURE_2D_SAMPLER_SLOT,
							curTex.m_samplerState,
#if APIABSTRACTION_D3D9
							curTex.m_pTexture,
#elif APIABSTRACTION_D3D11
							curTex.m_pShaderResourceView,
#elif APIABSTRACTION_OGL
							curTex.m_texture,
#elif PE_PLAT_IS_PSVITA
							curTex.m_texture,
#endif
							curTex.m_name
						);
						pSetTextureAction->bindToPipeline(pEffect);

						// set constant buffer
						// this handle will be released on end of draw call
						SetPerMaterialConstantsShaderAction* pSV = new(hSPMCSA) SetPerMaterialConstantsShaderAction();

						pSV->m_data.m_diffuse = curMat.m_diffuse;
						pSV->m_data.gxyzVSpecular_w.asVector3Ref() = curMat.m_specular;
						pSV->m_data.gxyzVEmissive_wVShininess.asVector3Ref() = curMat.m_emissive;
						pSV->m_data.gxyzVEmissive_wVShininess.m_w = curMat.m_shininess;
						pSV->m_data.m_xHasNrm_yHasSpec_zHasGlow_wHasDiff = curMat.m_xHasNrm_yHasSpec_zHasGlow_wHasDiff;
						pSV->bindToPipeline(pEffect);

					}

					// SetPerObjectConstantsShaderAction
					{
						MeshInstance* pInst = pMesh->m_instances[0].getObject<MeshInstance>();

						SetPerObjectConstantsShaderAction* psvPerObject = new(hSPOCSA) SetPerObjectConstantsShaderAction();

						memset(&psvPerObject->m_data, 0, sizeof(SetPerObjectConstantsShaderAction::Data));


						Handle hParentSN = pInst->getFirstParentByType<SceneNode>();
						SkeletonInstance* pParentSkelInstance = NULL;
						if (!hParentSN.isValid())
						{
							// allow skeleton to be in chain
							if (pParentSkelInstance = pInst->getFirstParentByTypePtr<SkeletonInstance>())
							{
								hParentSN = pParentSkelInstance->getFirstParentByType<SceneNode>();
							}
						}
						PEASSERT(hParentSN.isValid(), "Each instance must have a scene node parent");

						Matrix4x4& m_worldTransform = hParentSN.getObject<SceneNode>()->m_worldTransform;

						Matrix4x4 worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;

						Vector3 worldPos;
						worldPos = worldMatrix.getPos();
						worldMatrix.setPos(Vector3(worldPos.m_x, worldPos.m_y, -worldPos.m_z));

						psvPerObject->m_data.gWVP = evtProjectionViewWorldMatrix * worldMatrix; // these values are only used by non-instance version
						psvPerObject->m_data.gWVPInverse = psvPerObject->m_data.gWVP.inverse(); // these values are only used by non-instance version

						psvPerObject->m_data.gW = worldMatrix;  // these values are only used by non-instance version

						// Set blend weights
						if (numVBufs > 1)
						{
							psvPerObject->m_data.gVertexBufferWeights = vbufWeights;
						}

						if (hasJointSegments)
						{
							DefaultAnimationSM* pAnimSM = pParentSkelInstance ? pParentSkelInstance->getFirstComponent<DefaultAnimationSM>() : NULL;

							if (pAnimSM)
							{
								psvPerObject->m_useBones = true;

								IndexRange::BoneSegment& bs = ir.m_boneSegments[iBoneSegment];
								Matrix4x4* curPalette = pAnimSM->m_curPalette.getFirstPtr();
								for (int ibone = 0; ibone < (int)(bs.m_boneSegmentBones.m_size > PE_MAX_BONE_COUNT_IN_DRAW_CALL ? PE_MAX_BONE_COUNT_IN_DRAW_CALL : bs.m_boneSegmentBones.m_size); ibone++)
								{
									if (pAnimSM->m_curPalette.m_size > 0)
									{
										memcpy(&psvPerObject->m_data.gJoints[ibone], &(curPalette[bs.m_boneSegmentBones[ibone]]), sizeof(Matrix4x4));
									}
								}
							}
						} // end if has bone segments
						psvPerObject->bindToPipeline(pEffect);
					} 

					
				}	// for each bone segment (if no bone segment, just run the code once, for a non skinned mesh)

				iSrcInstance = iSrcInstanceInBoneSegment + 1;
				//pLodibGPU->draw(1, 0);
				pLodibGPU->draw(iRange, 1, 0);
				//pSetTextureAction->unbindFromPipeline(&curEffect);

			}
		}	// loop through all effects for this iRange

	}
	

	hSA.release();
	hSPMCSA.release();
	hSPOCSA.release();

//	// effect
//	
//	curEffect.setCurrent(pvbGPU);
//
//	// texture
//	GPUMaterialSet* pGpuMatSet = pMesh->m_hMaterialSetGPU.getObject<GPUMaterialSet>();
//	PE::Handle hSV("SA_Bind_Resource", sizeof(SA_Bind_Resource));
//	SA_Bind_Resource* pSetTextureAction = new(hSV) SA_Bind_Resource(*m_pContext, m_arena);
//	for (int i = 0; i < pGpuMatSet->m_materials.m_size; i++)
//	{
//		GPUMaterial& curMat = pGpuMatSet->m_materials[i];
//		for (PrimitiveTypes::UInt32 itex = 0; itex < curMat.m_textures.m_size; itex++)
//		{
//
//			
//			// create object referenced by Handle in DrawList
//			// this handle will be released on end of draw call, os no need to worry about realeasing this object
//			
//
//			//SA_Bind_Resource *pSetTextureAction;
//			TextureGPU& curTex = *curMat.m_textures[itex].getObject<TextureGPU>();
//			pSetTextureAction->set(
//				DIFFUSE_TEXTURE_2D_SAMPLER_SLOT,
//				curTex.m_samplerState,
//#if APIABSTRACTION_D3D9
//				curTex.m_pTexture,
//#elif APIABSTRACTION_D3D11
//				curTex.m_pShaderResourceView,
//#elif APIABSTRACTION_OGL
//				curTex.m_texture,
//#elif PE_PLAT_IS_PSVITA
//				curTex.m_texture,
//#endif
//				curTex.m_name
//			);
//			pSetTextureAction->bindToPipeline(&curEffect);
//		}
//	}
//	
//
//	//TextureGPU* t = m_hGlowTargetTextureGPU.getObject<TextureGPU>();
//	//PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, t->m_samplerState, API_CHOOSE_DX11_DX9_OGL(t->m_pShaderResourceView, t->m_pTexture, t->m_texture));
//	//setTextureAction.bindToPipeline(&curEffect);
//
//	// Per Object
//
//	PE::SetPerObjectConstantsShaderAction objSa;
//
//	MeshInstance* pInst = pMesh->m_instances[0].getObject<MeshInstance>();
//
//	//Handle& hsvPerObject = Handle("RAW_DATA", sizeof(SetPerObjectConstantsShaderAction));
//	//SetPerObjectConstantsShaderAction* psvPerObject = new(hsvPerObject) SetPerObjectConstantsShaderAction();
//
//	//memset(&psvPerObject->m_data, 0, sizeof(SetPerObjectConstantsShaderAction::Data));
//
//	Handle hParentSN = pInst->getFirstParentByType<SceneNode>();
//
//	Matrix4x4& m_worldTransform = hParentSN.getObject<SceneNode>()->m_worldTransform;
//
//	Matrix4x4 worldMatrix = hParentSN.getObject<SceneNode>()->m_worldTransform;
//
//	CameraSceneNode* pcam = CameraManager::Instance()->getActiveCamera()->getCamSceneNode();
//
//	Matrix4x4 evtProjectionViewWorldMatrix = pcam->m_viewToProjectedTransform * pcam->m_worldToViewTransform;
//
//	Vector3 worldPos;
//	worldPos = worldMatrix.getPos();
//	worldMatrix.setPos(Vector3(worldPos.m_x, worldPos.m_y, -worldPos.m_z));
//
//	objSa.m_data.gWVP = evtProjectionViewWorldMatrix * worldMatrix; // these values are only used by non-instance version
//	objSa.m_data.gWVPInverse = objSa.m_data.gWVP.inverse(); // these values are only used by non-instance version
//
//	objSa.m_data.gW = worldMatrix;  // these values are only used by non-instance version
//	objSa.bindToPipeline(&curEffect);
//
//	//SetPerMaterialConstantsShaderAction* pSV = new(hSV) SetPerMaterialConstantsShaderAction();
//
//	//pSV->m_data.m_diffuse = m_diffuse;
//	//pSV->m_data.gxyzVSpecular_w.asVector3Ref() = m_specular;
//	//pSV->m_data.gxyzVEmissive_wVShininess.asVector3Ref() = m_emissive;
//	//pSV->m_data.gxyzVEmissive_wVShininess.m_w = m_shininess;
//	//pSV->m_data.m_xHasNrm_yHasSpec_zHasGlow_wHasDiff = m_xHasNrm_yHasSpec_zHasGlow_wHasDiff;
//	pibGPU->draw(1, 0);
//
//	pSetTextureAction->unbindFromPipeline(&curEffect);
//
//	hSV.release();
	
}

void EffectManager::drawMotionBlur()
{
	Effect &curEffect = *m_hMotionBlurEffect.getObject<Effect>();
	if (!curEffect.m_isReady)
		return;
	
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(0, true);

	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();
	
	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);

	TextureGPU *t = m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>();
	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT,t->m_samplerState, API_CHOOSE_DX11_DX9_OGL(t->m_pShaderResourceView, t->m_pTexture, t->m_texture));
	setTextureAction.bindToPipeline(&curEffect);

	//todo: enable this to get motion blur working. need to make shader use depth map, not shadow map
	//PE::SA_Bind_Resource setDepthTextureAction(DEPTHMAP_TEXTURE_2D_SAMPLER_SLOT, API_CHOOSE_DX11_DX9_OGL(m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_pShaderResourceView, m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_pTexture, m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_texture));
	//setDepthTextureAction.bindToPipeline(&curEffect);

	SetPerObjectGroupConstantsShaderAction cb(*m_pContext, m_arena);
	cb.m_data.gPreviousViewProjMatrix = m_previousViewProjMatrix;
	cb.m_data.gViewProjInverseMatrix = m_currentViewProjMatrix.inverse();
	cb.m_data.gDoMotionBlur = m_doMotionBlur;

	cb.bindToPipeline();

	PE::SetPerObjectConstantsShaderAction objSa;
	objSa.m_data.gW = Matrix4x4();
	objSa.m_data.gW.loadIdentity();
	objSa.m_data.gWVP = objSa.m_data.gW;

	objSa.bindToPipeline(&curEffect);


	pibGPU->draw(1, 0);
	m_previousViewProjMatrix = m_currentViewProjMatrix;

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);

	setTextureAction.unbindFromPipeline(&curEffect);
	cb.unbindFromPipeline(&curEffect);
	objSa.unbindFromPipeline(&curEffect);
	//todo: enable this to get motion blur working
	//setDepthTextureAction.unbindFromPipeline(&curEffect);
}

void EffectManager::drawFrameBufferCopy()
{
	Effect &curEffect = *m_hMotionBlurEffect.getObject<Effect>();

	if (!curEffect.m_isReady)
		return;

	setFrameBufferCopyRenderTarget();

	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();
	
	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);

	TextureGPU *t = m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>();
	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena, DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, t->m_samplerState, API_CHOOSE_DX11_DX9_OGL(t->m_pShaderResourceView, t->m_pTexture, t->m_texture));
	setTextureAction.bindToPipeline(&curEffect);

	SetPerObjectGroupConstantsShaderAction cb(*m_pContext, m_arena);
	cb.m_data.gPreviousViewProjMatrix = m_previousViewProjMatrix;
	cb.m_data.gViewProjInverseMatrix = m_currentViewProjMatrix.inverse();
	cb.m_data.gDoMotionBlur = m_doMotionBlur;

	cb.bindToPipeline();

	pibGPU->draw(1, 0);
	m_previousViewProjMatrix = m_currentViewProjMatrix;

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);

	setTextureAction.unbindFromPipeline(&curEffect);
	}

Effect *EffectManager::operator[] (const char *pEffectFilename)
{
	return m_map.findHandle(pEffectFilename).getObject<Effect>();
}

void EffectManager::debugDrawRenderTarget(bool drawGlowRenderTarget, bool drawSeparatedGlow, bool drawGlow1stPass, bool drawGlow2ndPass, bool drawShadowRenderTarget)
{
	// use motion blur for now since it doesnt do anything but draws the surface
	Effect &curEffect = *m_hMotionBlurEffect.getObject<Effect>();
	if (!curEffect.m_isReady)
		return;
	
#	if APIABSTRACTION_D3D9
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithDepth(0, 0, true, true);
	// this is called in function above: IRenderer::Instance()->getDevice()->BeginScene();
#elif APIABSTRACTION_OGL
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithDepth(0, 0, true, true);
#	elif APIABSTRACTION_D3D11
	m_pContext->getGPUScreen()->setRenderTargetsAndViewportWithNoDepth(0, true);
#	endif

	IndexBufferGPU *pibGPU = m_hIndexBufferGPU.getObject<IndexBufferGPU>();
	pibGPU->setAsCurrent();

	VertexBufferGPU *pvbGPU = m_hVertexBufferGPU.getObject<VertexBufferGPU>();
	pvbGPU->setAsCurrent(&curEffect);

	curEffect.setCurrent(pvbGPU);

	PE::SA_Bind_Resource setTextureAction(*m_pContext, m_arena);
	
	if (drawGlowRenderTarget)
		setTextureAction.set(DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_samplerState,  API_CHOOSE_DX11_DX9_OGL(m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_pShaderResourceView, m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_pTexture, m_hGlowTargetTextureGPU.getObject<TextureGPU>()->m_texture));
	if (drawSeparatedGlow)
		setTextureAction.set(DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, m_glowSeparatedTextureGPU.m_samplerState, API_CHOOSE_DX11_DX9_OGL(m_glowSeparatedTextureGPU.m_pShaderResourceView, m_glowSeparatedTextureGPU.m_pTexture, m_glowSeparatedTextureGPU.m_texture));
	if (drawGlow1stPass)
		setTextureAction.set(DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, m_2ndGlowTargetTextureGPU.m_samplerState, API_CHOOSE_DX11_DX9_OGL(m_2ndGlowTargetTextureGPU.m_pShaderResourceView, m_2ndGlowTargetTextureGPU.m_pTexture, m_2ndGlowTargetTextureGPU.m_texture));
	if (drawGlow2ndPass)
		setTextureAction.set(DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>()->m_samplerState, API_CHOOSE_DX11_DX9_OGL(m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>()->m_pShaderResourceView, m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>()->m_pTexture, m_hFinishedGlowTargetTextureGPU.getObject<TextureGPU>()->m_texture));
	if (drawShadowRenderTarget)
		setTextureAction.set(DIFFUSE_TEXTURE_2D_SAMPLER_SLOT, m_shadowMapDepthTexture.m_samplerState, API_CHOOSE_DX11_DX9_OGL(m_shadowMapDepthTexture.m_pShaderResourceView, m_shadowMapDepthTexture.m_pTexture, m_shadowMapDepthTexture.m_texture));
	
	setTextureAction.bindToPipeline(&curEffect);
	
	SetPerObjectGroupConstantsShaderAction cb(*m_pContext, m_arena);
	cb.m_data.gPreviousViewProjMatrix = m_previousViewProjMatrix;
	cb.m_data.gViewProjInverseMatrix = m_currentViewProjMatrix.inverse();
	cb.m_data.gDoMotionBlur = m_doMotionBlur;

	cb.bindToPipeline();

	pibGPU->draw(1, 0);
	m_previousViewProjMatrix = m_currentViewProjMatrix;

	setTextureAction.unbindFromPipeline(&curEffect);

	pibGPU->unbindFromPipeline();
	pvbGPU->unbindFromPipeline(&curEffect);
}


}; // namespace PE
