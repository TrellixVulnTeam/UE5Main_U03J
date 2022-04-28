// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_1
#include "RenderGraph.h"
#endif
#include "RenderGraphDefinitions.h"
#include "ShaderParameterMacros.h"
#include "RendererInterface.h"

class FRDGBuilder;
struct IPooledRenderTarget;
struct FSceneTextures;

/** A uniform buffer containing common scene textures used by materials or global shaders. */
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, RENDERER_API)
	// Scene Color / Depth
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)

	// GBuffer
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferATexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferBTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferCTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferDTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferETexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferFTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferVelocityTexture)

	// SSAO
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ScreenSpaceAOTexture)

	// Custom Depth / Stencil
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)

	// Misc
	SHADER_PARAMETER_SAMPLER(SamplerState, PointClampSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum class ESceneTextureSetupMode : uint32
{
	None			= 0,
	SceneColor		= 1 << 0,
	SceneDepth		= 1 << 1,
	SceneVelocity	= 1 << 2,
	GBufferA		= 1 << 3,
	GBufferB		= 1 << 4,
	GBufferC		= 1 << 5,
	GBufferD		= 1 << 6,
	GBufferE		= 1 << 7,
	GBufferF		= 1 << 8,
	SSAO			= 1 << 9,
	CustomDepth		= 1 << 10,
	GBuffers		= GBufferA | GBufferB | GBufferC | GBufferD | GBufferE | GBufferF,
	All				= SceneColor | SceneDepth | SceneVelocity | GBuffers | SSAO | CustomDepth
};
ENUM_CLASS_FLAGS(ESceneTextureSetupMode);

/** Fills the shader parameter struct. */
extern RENDERER_API void SetupSceneTextureUniformParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures* SceneTextures,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode,
	FSceneTextureUniformParameters& OutParameters);

/** Returns RDG scene texture uniform buffer. */
extern RENDERER_API TRDGUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures* SceneTextures,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All);

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FMobileSceneTextureUniformParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CustomDepthTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, CustomDepthTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D<uint2>, CustomStencilTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, MobileCustomStencilTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, MobileCustomStencilTextureSampler)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneVelocityTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneVelocityTextureSampler)
	// GBuffer
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferATexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferBTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferCTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, GBufferDTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthAuxTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferATextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferBTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferCTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, GBufferDTextureSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, SceneDepthAuxTextureSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

enum class EMobileSceneTextureSetupMode : uint32
{
	None			= 0,
	SceneColor		= 1 << 0,
	SceneDepth		= 1 << 1,
	CustomDepth		= 1 << 2,
	GBufferA		= 1 << 3,
	GBufferB		= 1 << 4,
	GBufferC		= 1 << 5,
	GBufferD		= 1 << 6,
	SceneDepthAux	= 1 << 7,
	SceneVelocity	= 1 << 8,
	GBuffers		= GBufferA | GBufferB | GBufferC | GBufferD | SceneDepthAux,
	All				= SceneColor | SceneDepth | CustomDepth | GBuffers | SceneVelocity
};
ENUM_CLASS_FLAGS(EMobileSceneTextureSetupMode);

/** Fills the scene texture uniform buffer struct. */
extern RENDERER_API void SetupMobileSceneTextureUniformParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures* SceneTextures,
	EMobileSceneTextureSetupMode SetupMode,
	FMobileSceneTextureUniformParameters& SceneTextureParameters);

/** Creates the RDG mobile scene texture uniform buffer. */
extern RENDERER_API TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBuffer(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures* SceneTextures,
	EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::All);

BEGIN_SHADER_PARAMETER_STRUCT(FSceneTextureShaderParameters, RENDERER_API)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FMobileSceneTextureUniformParameters, MobileSceneTextures)
END_SHADER_PARAMETER_STRUCT()

FORCEINLINE FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.SceneTextures = UniformBuffer;
	return Parameters;
}

FORCEINLINE FSceneTextureShaderParameters GetSceneTextureShaderParameters(TRDGUniformBufferRef<FMobileSceneTextureUniformParameters> UniformBuffer)
{
	FSceneTextureShaderParameters Parameters;
	Parameters.MobileSceneTextures = UniformBuffer;
	return Parameters;
}

/** Returns scene texture shader parameters containing the RDG uniform buffer for either mobile or deferred shading. */
extern RENDERER_API FSceneTextureShaderParameters CreateSceneTextureShaderParameters(
	FRDGBuilder& GraphBuilder,
	const FSceneTextures* SceneTextures,
	ERHIFeatureLevel::Type FeatureLevel,
	ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All);

/** Struct containing references to extracted RHI resources after RDG execution. All textures are
 *  left in an SRV read state, so they can safely be used for read without being re-imported into
 *  RDG. Likewise, the uniform buffer is non-RDG and can be used as is.
 */
class RENDERER_API FSceneTextureExtracts : public FRenderResource
{
public:
	FRHIUniformBuffer* GetUniformBuffer() const
	{
		return UniformBuffer.IsValid() ? UniformBuffer.GetReference() : MobileUniformBuffer.GetReference();
	}

	TUniformBufferRef<FSceneTextureUniformParameters> GetUniformBufferRef() const
	{
		return UniformBuffer;
	}

	TUniformBufferRef<FMobileSceneTextureUniformParameters> GetMobileUniformBufferRef() const
	{
		return MobileUniformBuffer;
	}

	FRHITexture* GetDepthTexture() const
	{
		return Depth ? Depth->GetRHI() : nullptr;
	}

	void QueueExtractions(FRDGBuilder& GraphBuilder, const FSceneTextures& SceneTextures);

private:
	void Release();
	void ReleaseDynamicRHI() override { Release(); }

	// Contains the resolved scene depth target.
	TRefCountPtr<IPooledRenderTarget> Depth;

	// Contains the custom depth targets.
	TRefCountPtr<IPooledRenderTarget> CustomDepth;
	TRefCountPtr<IPooledRenderTarget> MobileCustomDepth;
	TRefCountPtr<IPooledRenderTarget> MobileCustomStencil;

	// Contains RHI scene texture uniform buffers referencing the extracted textures.
	TUniformBufferRef<FSceneTextureUniformParameters> UniformBuffer;
	TUniformBufferRef<FMobileSceneTextureUniformParameters> MobileUniformBuffer;
};

/** Returns the global scene texture extracts struct. */
const RENDERER_API FSceneTextureExtracts& GetSceneTextureExtracts();

/** Pass through to View.GetSceneTexturesConfig().Extent, useful in headers where the FViewInfo structure isn't exposed. */
extern RENDERER_API FIntPoint GetSceneTextureExtentFromView(const FViewInfo& View);

/** Resets the scene texture extent history. Call this method after rendering with very large render
 *  targets. The next scene render will create them at the requested size.
 */
extern RENDERER_API void ResetSceneTextureExtentHistory();

/** Registers system textures into RDG. */
extern RENDERER_API void CreateSystemTextures(FRDGBuilder& GraphBuilder);


/** Returns whether scene textures have been initialized. */
UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.IsValid() instead.")
extern RENDERER_API bool IsSceneTexturesValid();

/** Returns the full-resolution scene texture extent. */
UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.Extent instead.")
extern RENDERER_API FIntPoint GetSceneTextureExtent();

/** Returns the feature level being used by the renderer. */
UE_DEPRECATED(5.1, "Single pass multiple view family rendering makes this obsolete.  Use ViewFamily.SceneTexturesConfig.FeatureLevel instead.")
extern RENDERER_API ERHIFeatureLevel::Type GetSceneTextureFeatureLevel();

///////////////////////////////////////////////////////////////////////////
// Deprecated APIs

class FSceneRenderTargets;

UE_DEPRECATED(5.0, "SetupSceneTextureUniforParameters now requires an FRDGBuilder.")
inline void SetupSceneTextureUniformParameters(const FSceneRenderTargets&, ERHIFeatureLevel::Type, ESceneTextureSetupMode, FSceneTextureUniformParameters&)
{
	checkNoEntry();
}

UE_DEPRECATED(5.0, "CreateSceneTextureUniformBuffer now requires an FRDGBuilder.")
inline TUniformBufferRef<FSceneTextureUniformParameters> CreateSceneTextureUniformBuffer(FRHIComputeCommandList&, ERHIFeatureLevel::Type, ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All)
{
	checkNoEntry();
	return {};
}

UE_DEPRECATED(5.0, "SetupMobileSceneTextureUniformParameters now requires an FRDGBuilder.")
inline void SetupMobileSceneTextureUniformParameters(const FSceneRenderTargets&, EMobileSceneTextureSetupMode, FMobileSceneTextureUniformParameters&)
{
	checkNoEntry();
}

/** Creates the RHI mobile scene texture uniform buffer with passthrough RDG resources. */
UE_DEPRECATED(5.0, "CreateMobileSceneTextureUniformBuffer now requires an FRDGBuilder.")
inline TUniformBufferRef<FMobileSceneTextureUniformParameters> CreateMobileSceneTextureUniformBuffer(FRHIComputeCommandList&, EMobileSceneTextureSetupMode SetupMode = EMobileSceneTextureSetupMode::All)
{
	checkNoEntry();
	return {};
}

UE_DEPRECATED(5.0, "Use CreateSceneTextureShaderParameters instead.")
inline TRefCountPtr<FRHIUniformBuffer> CreateSceneTextureUniformBufferDependentOnShadingPath(FRHIComputeCommandList&, ERHIFeatureLevel::Type, ESceneTextureSetupMode SetupMode = ESceneTextureSetupMode::All)
{
	checkNoEntry();
	return {};
}

UE_DEPRECATED(5.0, "IsSceneTexturesValid no longer requires a command list.")
inline bool IsSceneTexturesValid(FRHICommandListImmediate&)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return IsSceneTexturesValid();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

//////////////////////////////////////////////////////////////////////////