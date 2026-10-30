#pragma once

#include "CoreMinimal.h"
#include "RenderGraphResources.h"
#include "SceneRendering.h"
#include "ShaderParameterStruct.h"

struct FWuWaSurfaceBuffers;

// Installed Renderer metadata is private. Both character lighting paths bind
// current native shadow values here and use the engine's sampling functions.
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FWuWaShadowMapParameters, )
    SHADER_PARAMETER(uint32, NumDirectionalLightCascades)
    SHADER_PARAMETER(FVector4f, CascadeEndDepths)
    SHADER_PARAMETER_ARRAY(FMatrix44f, DirectionalLightTranslatedWorldToShadowMatrix, [GMaxForwardShadowCascades])
    SHADER_PARAMETER_ARRAY(FVector4f, DirectionalLightShadowmapMinMax, [GMaxForwardShadowCascades])
    SHADER_PARAMETER(FVector4f, DirectionalLightShadowmapAtlasBufferSize)
    SHADER_PARAMETER(float, DirectionalLightDepthBias)
    SHADER_PARAMETER(uint32, DirectionalLightUseStaticShadowing)
    SHADER_PARAMETER(FVector4f, DirectionalLightStaticShadowBufferSize)
    SHADER_PARAMETER(FMatrix44f, DirectionalLightTranslatedWorldToStaticShadow)
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DirectionalLightShadowmapAtlas)
    SHADER_PARAMETER_SAMPLER(SamplerState, ShadowmapSampler)
    SHADER_PARAMETER_TEXTURE(Texture2D, DirectionalLightStaticShadowmap)
    SHADER_PARAMETER_SAMPLER(SamplerState, StaticShadowmapSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

TRDGUniformBufferRef<FWuWaShadowMapParameters> CreateWuWaShadowParameters(FRDGBuilder& Graph, const FViewInfo& View);

struct FWuWaLightingInputs
{
    FVector3f RimDirection = FVector3f(-0.7094065f, -0.4967318f, 0.5f);
    FVector3f LightTint = FVector3f(1, 1, 1);
    // Original ToonLocalLightData contract: sixteen lights, nine float4 rows each.
    TArray<FVector4f> LocalLightData;
    FVector3f ShadowDirection=FVector3f(0,0,1);
    float ContactShadowLength=0;
    float ContactShadowIntensity=1;
    float ContactShadowNonCastingIntensity=0;
    bool bContactShadowLengthInWorldSpace=false;
};

/** Evaluate the recovered lighting using this view's native shadow resources. */
bool AddWuWaLightingPasses(FRDGBuilder& Graph, const FViewInfo& View,
    const FWuWaSurfaceBuffers& Source, FRDGTextureRef SceneColor, const FWuWaLightingInputs& Lighting);

/** Apply the source four-pass filter to the live showcase mirror view. */
void AddWuWaReflectionBlurPasses(FRDGBuilder& Graph, const FViewInfo& View, FRDGTextureRef SceneColor);
