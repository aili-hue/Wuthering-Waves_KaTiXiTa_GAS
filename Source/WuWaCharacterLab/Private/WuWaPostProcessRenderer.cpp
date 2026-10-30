#include "WuWaPostProcessRenderer.h"
#include "WuWaSurfaceRenderer.h"
#include "WuWaBloomPasses.h"

#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRendering.h"
#include "ShaderCompilerCore.h"
#include "ShaderParameterStruct.h"
#include "HAL/IConsoleManager.h"

namespace
{
TAutoConsoleVariable<int32> CVarWuWaBloom(TEXT("r.WuWa.Bloom"), 1,
    TEXT("Enable the source showcase Bloom graph. Zero supports controlled validation."), ECVF_RenderThreadSafe);

class FWuWaBloomPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaBloomPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaBloomPS, FGlobalShader);
public:
    class FKernel : SHADER_PERMUTATION_INT("WUWA_BLOOM_KERNEL", WuWaBloomKernelCount);
    using FPermutationDomain = TShaderPermutationDomain<FKernel>;
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER(uint32, BloomPass)
        SHADER_PARAMETER(FVector4f, OutputViewRect)
        SHADER_PARAMETER_ARRAY(FVector4f, InputUVScaleBias, [4])
        SHADER_PARAMETER_ARRAY(FVector4f, InputUVMinMax, [4])
        SHADER_PARAMETER_ARRAY(FVector4f, InputSizeAndInvSize, [4])
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BloomInput0)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BloomInput1)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BloomInput2)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BloomInput3)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampedSampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, PointClampedSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_BLOOM_PASS"), 1);
    }
};

class FWuWaColorGradingCS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaColorGradingCS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaColorGradingCS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BlackLUTInput)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampedSampler)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, OutputLUT)
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_LUT_PASS"), 1);
    }
};

class FWuWaUnexposePS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaUnexposePS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaUnexposePS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, NativeColor)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampedSampler)
        SHADER_PARAMETER(float, InversePreExposure)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_UNEXPOSE_PASS"), 1);
    }
};

class FWuWaToneMappingPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaToneMappingPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaToneMappingPS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(FVector4f, OutputViewRect)
        SHADER_PARAMETER(FVector4f, SceneViewRect)
        SHADER_PARAMETER(FVector4f, SceneBufferSizeAndInvSize)
        SHADER_PARAMETER(FVector4f, BloomUVScaleBias)
        SHADER_PARAMETER(FVector4f, BloomUVMinMax)
        SHADER_PARAMETER(FVector4f, EncodedBaseUVScaleBias)
        SHADER_PARAMETER(FVector4f, EncodedBaseUVMinMax)
        SHADER_PARAMETER(FVector2f, NoisePhase)
        SHADER_PARAMETER(uint32, OutputIsSRGB)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, HDRSceneColor)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, CombinedBloom)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, BlackEffect)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EncodedBase)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ColorGradingLUT)
        SHADER_PARAMETER_SAMPLER(SamplerState, LinearClampedSampler)
        SHADER_PARAMETER_SAMPLER(SamplerState, PointClampedSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
};

IMPLEMENT_GLOBAL_SHADER(FWuWaColorGradingCS, "/WuWa/Private/WuWaPostProcessPass.usf", "MainLUTCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FWuWaUnexposePS, "/WuWa/Private/WuWaPostProcessPass.usf", "MainUnexposePS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FWuWaToneMappingPS, "/WuWa/Private/WuWaPostProcessPass.usf", "MainToneMappingPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FWuWaBloomPS, "/WuWa/Private/WuWaPostProcessPass.usf", "MainBloomPS", SF_Pixel);

FVector4f RectVector(const FIntRect& Rect)
{ return FVector4f(Rect.Min.X, Rect.Min.Y, Rect.Width(), Rect.Height()); }

FVector4f UVLimits(const FScreenPassTexture& Texture)
{
    const FIntPoint Extent = Texture.Texture->Desc.Extent;
    return FVector4f((Texture.ViewRect.Min.X + 0.5f) / Extent.X, (Texture.ViewRect.Min.Y + 0.5f) / Extent.Y,
        (Texture.ViewRect.Max.X - 0.5f) / Extent.X, (Texture.ViewRect.Max.Y - 0.5f) / Extent.Y);
}

FVector4f UVTransform(const FScreenPassTexture& Scene, const FScreenPassTexture& Destination)
{
    const FIntPoint SceneExtent = Scene.Texture->Desc.Extent;
    const FIntPoint DestExtent = Destination.Texture->Desc.Extent;
    const float ScaleX = float(Destination.ViewRect.Width()) * SceneExtent.X / (DestExtent.X * Scene.ViewRect.Width());
    const float ScaleY = float(Destination.ViewRect.Height()) * SceneExtent.Y / (DestExtent.Y * Scene.ViewRect.Height());
    return FVector4f(ScaleX, ScaleY, float(Destination.ViewRect.Min.X) / DestExtent.X - float(Scene.ViewRect.Min.X) / SceneExtent.X * ScaleX,
        float(Destination.ViewRect.Min.Y) / DestExtent.Y - float(Scene.ViewRect.Min.Y) / SceneExtent.Y * ScaleY);
}

FScreenPassTexture Unexpose(FRDGBuilder& Graph, const FViewInfo& View, const FScreenPassTexture& Input)
{
    if (FMath::IsNearlyEqual(View.PreExposure, 1.0f)) return Input;
    FRDGTextureDesc Desc = FRDGTextureDesc::Create2D(Input.Texture->Desc.Extent, PF_FloatRGBA,
        FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource);
    FScreenPassTexture Output(Graph.CreateTexture(Desc, TEXT("WuWa.UnexposedColor")), Input.ViewRect);
    auto* Parameters = Graph.AllocParameters<FWuWaUnexposePS::FParameters>();
    Parameters->NativeColor = Input.Texture;
    Parameters->LinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->InversePreExposure = 1.0f / View.PreExposure;
    Parameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, ERenderTargetLoadAction::ENoAction);
    TShaderMapRef<FWuWaUnexposePS> Shader(View.ShaderMap);
    AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Restore HDR Exposure"), View,
        FScreenPassTextureViewport(Output), FScreenPassTextureViewport(Input), Shader, Parameters);
    return Output;
}

FScreenPassTexture AddSourceBloom(FRDGBuilder& Graph, const FViewInfo& View,
    const FScreenPassTexture& SceneColor, const FWuWaSurfaceBuffers* Source, FRDGTextureRef Black)
{
    FScreenPassTexture Textures[WuWaBloomResourceCount];
    Textures[0] = SceneColor;
    // The captured classification R/G channels are proven zero for every
    // submitted writer. Its unused alpha is not part of this substitution.
    Textures[1] = FScreenPassTexture(Black, FIntRect(0, 0, 1, 1));
    Textures[2] = FScreenPassTexture(View.GetSceneTextures().Depth.Resolve, View.ViewRect);
    Textures[3] = Source ? FScreenPassTexture(Source->Channels[2], View.ViewRect) : Textures[1];
    TShaderMapRef<FScreenPassVS> VertexShader(View.ShaderMap);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(WuWaBloomPasses); ++Index)
    {
        const FWuWaBloomPassDefinition& Pass = WuWaBloomPasses[Index];
        FScreenPassTexture& Output = Textures[Pass.Output];
        if (!Output.IsValid())
        {
            const FIntPoint Size = FIntPoint::DivideAndRoundUp(SceneColor.ViewRect.Size(), Pass.Divisor);
            Output = FScreenPassTexture(Graph.CreateTexture(FRDGTextureDesc::Create2D(Size, PF_FloatR11G11B10,
                FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("WuWa.Bloom")),
                FIntRect(FIntPoint::ZeroValue, Size));
        }
        auto* Parameters = Graph.AllocParameters<FWuWaBloomPS::FParameters>();
        Parameters->View = View.ViewUniformBuffer;
        Parameters->BloomPass = Index;
        Parameters->OutputViewRect = RectVector(Output.ViewRect);
        FRDGTextureRef Bound[4];
        for (int32 InputIndex = 0; InputIndex < 4; ++InputIndex)
        {
            const FScreenPassTexture& Input = Pass.Inputs[InputIndex] >= 0 ? Textures[Pass.Inputs[InputIndex]] : Textures[1];
            check(Input.IsValid() && Input.Texture != Output.Texture);
            const FIntPoint Extent = Input.Texture->Desc.Extent;
            Parameters->InputUVScaleBias[InputIndex] = FVector4f(float(Input.ViewRect.Width()) / Extent.X,
                float(Input.ViewRect.Height()) / Extent.Y, float(Input.ViewRect.Min.X) / Extent.X, float(Input.ViewRect.Min.Y) / Extent.Y);
            Parameters->InputUVMinMax[InputIndex] = UVLimits(Input);
            Parameters->InputSizeAndInvSize[InputIndex] = FVector4f(Extent.X, Extent.Y, 1.0f / Extent.X, 1.0f / Extent.Y);
            Bound[InputIndex] = Input.Texture;
        }
        Parameters->BloomInput0 = Bound[0]; Parameters->BloomInput1 = Bound[1];
        Parameters->BloomInput2 = Bound[2]; Parameters->BloomInput3 = Bound[3];
        Parameters->LinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->PointClampedSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture,
            Pass.bAdditive ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);
        FWuWaBloomPS::FPermutationDomain Permutation;
        Permutation.Set<FWuWaBloomPS::FKernel>(Pass.Kernel);
        TShaderMapRef<FWuWaBloomPS> Shader(View.ShaderMap, Permutation);
        FRHIBlendState* Blend = Pass.bAdditive
            ? TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One>::GetRHI() : TStaticBlendState<>::GetRHI();
        AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Source Bloom %d", Index), View,
            FScreenPassTextureViewport(Output), FScreenPassTextureViewport(Textures[Pass.Inputs[0]]), VertexShader, Shader, Blend, Parameters);
    }
    return Textures[WuWaBloomOutputSlot];
}
}

FScreenPassTexture AddWuWaToneMappingPass(FRDGBuilder& Graph, const FViewInfo& View,
    const FPostProcessMaterialInputs& Inputs, const FWuWaSurfaceBuffers* Source,
    TRefCountPtr<IPooledRenderTarget>& CachedLUT, uint32& BloomPassCount)
{
    const FScreenPassTexture SceneColor = Unexpose(Graph, View,
        FScreenPassTexture::CopyFromSlice(Graph, Inputs.GetInput(EPostProcessMaterialInput::SceneColor)));
    FRDGTextureRef Black = Graph.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_A32B32G32R32F,
        FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("WuWa.DisplayBlack"));
    AddClearRenderTargetPass(Graph, Black, FLinearColor::Transparent);

    FRDGTextureRef LUT;
    if (CachedLUT.IsValid())
    {
        LUT = Graph.RegisterExternalTexture(CachedLUT);
    }
    else
    {
        // The recovered style is constant. Generate once on the GPU and retain
        // its exact 32-cubed, ten-bit contract across frames and view sizes.
        LUT = Graph.CreateTexture(FRDGTextureDesc::Create3D(FIntVector(32, 32, 32), PF_A2B10G10R10,
            FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("WuWa.ColorGradingLUT"));
        FRDGTextureRef BlackLUT = Graph.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_A32B32G32R32F,
            FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("WuWa.BlackLUTInput"));
        AddClearRenderTargetPass(Graph, BlackLUT, FLinearColor::Black);
        auto* Parameters = Graph.AllocParameters<FWuWaColorGradingCS::FParameters>();
        Parameters->BlackLUTInput = BlackLUT;
        Parameters->LinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
        Parameters->OutputLUT = Graph.CreateUAV(LUT);
        TShaderMapRef<FWuWaColorGradingCS> Shader(View.ShaderMap);
        FComputeShaderUtils::AddPass(Graph, RDG_EVENT_NAME("WuWa Source Color Grading LUT"), Shader, Parameters, FIntVector(8, 8, 8));
        Graph.QueueTextureExtraction(LUT, &CachedLUT);
    }

    BloomPassCount = CVarWuWaBloom.GetValueOnRenderThread() != 0 ? UE_ARRAY_COUNT(WuWaBloomPasses) : 0;
    const FScreenPassTexture Bloom = BloomPassCount ? AddSourceBloom(Graph, View, SceneColor, Source, Black)
        : FScreenPassTexture(Black, FIntRect(0, 0, 1, 1));
    const FScreenPassTexture EncodedBase = Source
        ? FScreenPassTexture(Source->Channels[2], View.ViewRect)
        : FScreenPassTexture(Black, FIntRect(0, 0, 1, 1));
    FScreenPassRenderTarget Output = Inputs.OverrideOutput;
    if (!Output.IsValid())
    {
        Output = FScreenPassRenderTarget(Graph.CreateTexture(FRDGTextureDesc::Create2D(SceneColor.Texture->Desc.Extent,
            PF_A2B10G10R10, FClearValueBinding::None, TexCreate_RenderTargetable | TexCreate_ShaderResource),
            TEXT("WuWa.DisplayColor")), SceneColor.ViewRect, ERenderTargetLoadAction::ENoAction);
    }
    auto* Parameters = Graph.AllocParameters<FWuWaToneMappingPS::FParameters>();
    const FIntPoint Extent = SceneColor.Texture->Desc.Extent;
    Parameters->OutputViewRect = RectVector(Output.ViewRect);
    Parameters->SceneViewRect = RectVector(SceneColor.ViewRect);
    Parameters->SceneBufferSizeAndInvSize = FVector4f(Extent.X, Extent.Y, 1.0f / Extent.X, 1.0f / Extent.Y);
    Parameters->BloomUVScaleBias = UVTransform(SceneColor, Bloom);
    Parameters->BloomUVMinMax = UVLimits(Bloom);
    Parameters->EncodedBaseUVScaleBias = UVTransform(SceneColor, EncodedBase);
    Parameters->EncodedBaseUVMinMax = UVLimits(EncodedBase);
    // Use a current temporal phase; captured frame coordinates never drive the camera.
    const uint32 Phase = View.Family->FrameNumber % 8;
    Parameters->NoisePhase = FVector2f(Phase * 0.61803398875f, Phase * 0.41421356237f);
    Parameters->OutputIsSRGB = EnumHasAnyFlags(Output.Texture->Desc.Flags, TexCreate_SRGB) ? 1 : 0;
    Parameters->HDRSceneColor = SceneColor.Texture;
    Parameters->CombinedBloom = Bloom.Texture;
    Parameters->BlackEffect = Black;
    Parameters->EncodedBase = EncodedBase.Texture;
    Parameters->ColorGradingLUT = LUT;
    Parameters->LinearClampedSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->PointClampedSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    Parameters->RenderTargets[0] = Output.GetRenderTargetBinding();
    TShaderMapRef<FWuWaToneMappingPS> Shader(View.ShaderMap);
    AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Source Tone Mapping"), View,
        FScreenPassTextureViewport(Output), FScreenPassTextureViewport(SceneColor), Shader, Parameters);
    return Output;
}
