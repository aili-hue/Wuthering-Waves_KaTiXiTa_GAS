#include "WuWaLightingRenderer.h"
#include "WuWaSurfaceRenderer.h"

#include "GlobalShader.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ScreenPass.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "SystemTextures.h"
#include "RenderUtils.h"

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FWuWaShadowMapParameters, "WuWaShadow");

namespace
{
class FWuWaCommitDepthPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaCommitDepthPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaCommitDepthPS,FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,SourceDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,SourceBase)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform,ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FWuWaCommitDepthPS,"/WuWa/Private/WuWaLightingPass.usf","MainCommitDepthPS",SF_Pixel);
class FWuWaShadowMaskPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaShadowMaskPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaShadowMaskPS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FWuWaShadowMapParameters, WuWaShadow)
        SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTexturesStruct)
        SHADER_PARAMETER(FVector3f, ShadowDirection)
        SHADER_PARAMETER(float, ContactShadowLength)
        SHADER_PARAMETER(float, ContactShadowIntensity)
        SHADER_PARAMETER(float, ContactShadowNonCastingIntensity)
        SHADER_PARAMETER(uint32, ContactShadowLengthInWorldSpace)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceBase)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_SHADOW_PASS"), 1);
    }
};

class FWuWaToonLightingPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaToonLightingPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaToonLightingPS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER(FVector3f, RimDirection)
        SHADER_PARAMETER(FVector3f, LightTint)
        SHADER_PARAMETER_ARRAY(FVector4f, ToonLocalLightData, [144])
        SHADER_PARAMETER(uint32, ToonLocalLightCount)
        SHADER_PARAMETER(FVector4f, BufferSizeAndInvSize)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceNormal)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceBase)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceToonData)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceCharacterDepth)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceEmissive)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SourceShadowMask)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BlackEffect)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ManualExposure)
        SHADER_PARAMETER_SAMPLER(SamplerState, PointClampedSampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, PointWrappedSampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampedSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};

IMPLEMENT_GLOBAL_SHADER(FWuWaShadowMaskPS, "/WuWa/Private/WuWaLightingPass.usf", "MainShadowPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FWuWaToonLightingPS, "/WuWa/Private/WuWaLightingPass.usf", "MainLightingPS", SF_Pixel);

class FWuWaReflectionBlurPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaReflectionBlurPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaReflectionBlurPS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector4f, BufferSizeAndInvSize)
        SHADER_PARAMETER(FVector4f, ViewRectMinAndSize)
        SHADER_PARAMETER(uint32, FilterPass)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ReflectionSource)
        SHADER_PARAMETER_SAMPLER(SamplerState, BilinearClampedSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FWuWaReflectionBlurPS, "/WuWa/Private/WuWaReflectionBlurPass.usf", "MainPS", SF_Pixel);
}

void AddWuWaReflectionBlurPasses(FRDGBuilder& Graph, const FViewInfo& View, FRDGTextureRef SceneColor)
{
    FRDGTextureDesc Desc = SceneColor->Desc;
    Desc.Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
    // Source intermediates are exactly the mirrored view size, even when the
    // first input is a sub-rectangle of the main scene allocation.
    Desc.Extent = View.ViewRect.Size();
    FRDGTextureRef Scratch[2] = {
        Graph.CreateTexture(Desc, TEXT("WuWa.ReflectionBlurHorizontal")),
        Graph.CreateTexture(Desc, TEXT("WuWa.ReflectionBlurVertical"))};
    FRDGTextureRef Input = SceneColor;
    TShaderMapRef<FWuWaReflectionBlurPS> Shader(View.ShaderMap);
    for (uint32 Pass = 0; Pass < 4; ++Pass)
    {
        FRDGTextureRef Output = Pass == 3 ? SceneColor : Scratch[Pass % 2];
        const FIntPoint Extent = Input->Desc.Extent;
        const FIntRect InputRect = Pass == 0 ? View.ViewRect : FIntRect(FIntPoint::ZeroValue, Desc.Extent);
        const FIntRect OutputRect = Pass == 3 ? View.ViewRect : FIntRect(FIntPoint::ZeroValue, Desc.Extent);
        auto* Parameters = Graph.AllocParameters<FWuWaReflectionBlurPS::FParameters>();
        Parameters->BufferSizeAndInvSize = FVector4f(Extent.X, Extent.Y, 1.f / Extent.X, 1.f / Extent.Y);
        Parameters->ViewRectMinAndSize = FVector4f(InputRect.Min.X, InputRect.Min.Y, InputRect.Width(), InputRect.Height());
        Parameters->FilterPass = Pass;
        Parameters->ReflectionSource = Input;
        Parameters->BilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->RenderTargets[0] = FRenderTargetBinding(Output,
            Pass == 3 ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);
        AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Source Reflection Blur %u", Pass),
            View, FScreenPassTextureViewport(Output, OutputRect), FScreenPassTextureViewport(Input, InputRect), Shader, Parameters);
        Input = Output;
    }
}

TRDGUniformBufferRef<FWuWaShadowMapParameters> CreateWuWaShadowParameters(FRDGBuilder& Graph, const FViewInfo& View)
{
    const auto* NativeShadow = View.ForwardLightingResources.ForwardDirLightShadowParameters;
    auto* ShadowBinding = Graph.AllocParameters<FWuWaShadowMapParameters>();
    if (!NativeShadow)
    {
        // Asset previews and empty levels need no character/showcase actor or
        // directional-light shadow allocation to evaluate the same material.
        ShadowBinding->NumDirectionalLightCascades=0;
        ShadowBinding->CascadeEndDepths=FVector4f(0,0,0,0);
        for (int32 Index=0;Index<GMaxForwardShadowCascades;++Index)
        {
            ShadowBinding->DirectionalLightTranslatedWorldToShadowMatrix[Index]=FMatrix44f::Identity;
            ShadowBinding->DirectionalLightShadowmapMinMax[Index]=FVector4f(0,0,1,1);
        }
        ShadowBinding->DirectionalLightShadowmapAtlasBufferSize=FVector4f(1,1,1,1);
        ShadowBinding->DirectionalLightDepthBias=0;
        ShadowBinding->DirectionalLightUseStaticShadowing=0;
        ShadowBinding->DirectionalLightStaticShadowBufferSize=FVector4f(1,1,1,1);
        ShadowBinding->DirectionalLightTranslatedWorldToStaticShadow=FMatrix44f::Identity;
        ShadowBinding->DirectionalLightShadowmapAtlas=FRDGSystemTextures::Get(Graph).White;
        ShadowBinding->DirectionalLightStaticShadowmap=GWhiteTexture->TextureRHI;
        ShadowBinding->ShadowmapSampler=TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
        ShadowBinding->StaticShadowmapSampler=TStaticSamplerState<SF_Point,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI();
        return Graph.CreateUniformBuffer(ShadowBinding);
    }
    ShadowBinding->NumDirectionalLightCascades = NativeShadow->NumDirectionalLightCascades;
    ShadowBinding->CascadeEndDepths = NativeShadow->CascadeEndDepths;
    for (int32 Index = 0; Index < GMaxForwardShadowCascades; ++Index)
    {
        ShadowBinding->DirectionalLightTranslatedWorldToShadowMatrix[Index] = NativeShadow->DirectionalLightTranslatedWorldToShadowMatrix[Index];
        ShadowBinding->DirectionalLightShadowmapMinMax[Index] = NativeShadow->DirectionalLightShadowmapMinMax[Index];
    }
    ShadowBinding->DirectionalLightShadowmapAtlasBufferSize = NativeShadow->DirectionalLightShadowmapAtlasBufferSize;
    ShadowBinding->DirectionalLightDepthBias = NativeShadow->DirectionalLightDepthBias;
    ShadowBinding->DirectionalLightUseStaticShadowing = NativeShadow->DirectionalLightUseStaticShadowing;
    ShadowBinding->DirectionalLightStaticShadowBufferSize = NativeShadow->DirectionalLightStaticShadowBufferSize;
    ShadowBinding->DirectionalLightTranslatedWorldToStaticShadow = NativeShadow->DirectionalLightTranslatedWorldToStaticShadow;
    ShadowBinding->DirectionalLightShadowmapAtlas = NativeShadow->DirectionalLightShadowmapAtlas;
    ShadowBinding->ShadowmapSampler = NativeShadow->ShadowmapSampler;
    ShadowBinding->DirectionalLightStaticShadowmap = NativeShadow->DirectionalLightStaticShadowmap;
    ShadowBinding->StaticShadowmapSampler = NativeShadow->StaticShadowmapSampler;
    return Graph.CreateUniformBuffer(ShadowBinding);
}

bool AddWuWaLightingPasses(FRDGBuilder& Graph, const FViewInfo& View,
    const FWuWaSurfaceBuffers& Source, FRDGTextureRef SceneColor, const FWuWaLightingInputs& Lighting)
{
    auto Shadow = CreateWuWaShadowParameters(Graph, View);
    if (!SceneColor || !Source.DepthStencil || !Shadow) return false;
    const FIntPoint Extent = Source.DepthStencil->Desc.Extent;
    const FScreenPassTextureViewport Viewport(SceneColor, View.ViewRect);
    // Native contact rays must see custom-rendered characters as well as scene objects.
    auto* DepthParameters=Graph.AllocParameters<FWuWaCommitDepthPS::FParameters>();
    DepthParameters->SourceDepth=Source.DepthStencil;
    DepthParameters->SourceBase=Source.Channels[2];
    DepthParameters->RenderTargets.DepthStencil=FDepthStencilBinding(View.GetSceneTextures().Depth.Target,
        ERenderTargetLoadAction::ELoad,ERenderTargetLoadAction::ELoad,FExclusiveDepthStencil::DepthWrite_StencilNop);
    TShaderMapRef<FScreenPassVS> DepthVertexShader(View.ShaderMap);
    TShaderMapRef<FWuWaCommitDepthPS> DepthShader(View.ShaderMap);
    AddDrawScreenPass(Graph,RDG_EVENT_NAME("WuWa Native Character Depth"),View,Viewport,Viewport,
        DepthVertexShader,DepthShader,TStaticBlendState<>::GetRHI(),TStaticDepthStencilState<true,CF_GreaterEqual>::GetRHI(),DepthParameters);
    FRDGTextureRef ShadowMask = Graph.CreateTexture(FRDGTextureDesc::Create2D(Extent, PF_B8G8R8A8,
        FClearValueBinding::White, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("WuWa.ShadowMask"));
    auto* ShadowParameters = Graph.AllocParameters<FWuWaShadowMaskPS::FParameters>();
    ShadowParameters->View = View.ViewUniformBuffer;
    ShadowParameters->WuWaShadow = Shadow;
    ShadowParameters->SceneTexturesStruct=View.GetSceneTextures().UniformBuffer;
    ShadowParameters->ShadowDirection=Lighting.ShadowDirection;
    ShadowParameters->ContactShadowLength=Lighting.ContactShadowLength;
    ShadowParameters->ContactShadowIntensity=Lighting.ContactShadowIntensity;
    ShadowParameters->ContactShadowNonCastingIntensity=Lighting.ContactShadowNonCastingIntensity;
    ShadowParameters->ContactShadowLengthInWorldSpace=Lighting.bContactShadowLengthInWorldSpace ? 1u:0u;
    ShadowParameters->SourceDepth = Source.DepthStencil;
    ShadowParameters->SourceBase = Source.Channels[2];
    ShadowParameters->RenderTargets[0] = FRenderTargetBinding(ShadowMask, ERenderTargetLoadAction::EClear);
    TShaderMapRef<FWuWaShadowMaskPS> ShadowShader(View.ShaderMap);
    AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Native Shadow Mask"), View, Viewport, Viewport, ShadowShader, ShadowParameters);

    auto* Parameters = Graph.AllocParameters<FWuWaToonLightingPS::FParameters>();
    Parameters->View = View.ViewUniformBuffer;
    Parameters->RimDirection = Lighting.RimDirection;
    Parameters->LightTint = Lighting.LightTint;
    Parameters->ToonLocalLightCount=FMath::Min(16,Lighting.LocalLightData.Num()/9);
    for (int32 Row=0;Row<144;++Row)
        Parameters->ToonLocalLightData[Row]=Lighting.LocalLightData.IsValidIndex(Row) ? Lighting.LocalLightData[Row] : FVector4f(0,0,0,0);
    Parameters->BufferSizeAndInvSize = FVector4f(Extent.X, Extent.Y, 1.0f / Extent.X, 1.0f / Extent.Y);
    Parameters->SourceDepth = Source.DepthStencil;
    Parameters->SourceNormal = Source.Channels[1];
    Parameters->SourceBase = Source.Channels[2];
    Parameters->SourceColor = Source.Channels[3];
    Parameters->SourceToonData = Source.Channels[6];
    Parameters->SourceCharacterDepth = Source.Channels[5];
    Parameters->SourceEmissive = Source.Channels[0];
    Parameters->SourceShadowMask = ShadowMask;
    // Native immutable constants have the exact required 0/1 values, including
    // zero black alpha, without allocating and clearing textures for each view.
    Parameters->BlackEffect = GSystemTextures.GetBlackDummy(Graph);
    // The showcase uses manual exposure. Keep source math unexposed and apply
    // the current UE pre-exposure once when writing native HDR scene color.
    Parameters->ManualExposure = GSystemTextures.GetWhiteDummy(Graph);
    Parameters->PointClampedSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->PointWrappedSampler = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
    Parameters->BilinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
    Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(View.GetSceneTextures().Depth.Target,
        ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilNop);
    TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
    TShaderMapRef<FWuWaToonLightingPS> LightingShader(View.ShaderMap);
    AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Source Toon Lighting"), View, Viewport, Viewport,
        VertexShader, LightingShader, TStaticBlendState<>::GetRHI(),
        TStaticDepthStencilState<true, CF_GreaterEqual>::GetRHI(), Parameters);
    return true;
}
