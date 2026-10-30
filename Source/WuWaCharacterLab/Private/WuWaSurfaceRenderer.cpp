#include "WuWaSurfaceRenderer.h"
#include "RendererInterface.h"
#include "Modules/ModuleManager.h"

#include "Engine/World.h"
#include "Engine/Texture.h"
#include "GlobalRenderResources.h"
#include "GlobalShader.h"
#include "InstanceCulling/InstanceCullingContext.h"
#include "MaterialShaderType.h"
#include "MaterialShared.h"
#include "Materials/MaterialRenderProxy.h"
#include "MeshMaterialShader.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "Misc/ScopeLock.h"
#include "PrimitiveSceneProxy.h"
#include "PostProcess/PostProcessInputs.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "WuWaPostProcessRenderer.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "ShaderCompilerCore.h"
#include "SystemTextures.h"
#include "VertexFactory.h"

DEFINE_LOG_CATEGORY_STATIC(LogWuWaSurface, Log, All);

namespace
{
enum class EWuWaPass : uint8 { Surface, Stencil, HairMask, HairBlend, Outline, TranslucentDepth, Translucent };

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FWuWaTranslucencyParameters, )
    SHADER_PARAMETER_RDG_TEXTURE(Texture2D, EncodedSceneColor)
    SHADER_PARAMETER_TEXTURE(Texture2D, WhiteTexture)
    SHADER_PARAMETER_TEXTURE(Texture3D, BlackVolume)
    SHADER_PARAMETER(FVector3f, RimDirection)
    SHADER_PARAMETER(FVector3f, LightTint)
    SHADER_PARAMETER(FVector4f, ExponentialFogParameters)
    SHADER_PARAMETER(FVector4f, ExponentialFogParameters2)
    SHADER_PARAMETER(FVector4f, ExponentialFogColorParameter)
    SHADER_PARAMETER(FVector4f, ExponentialFogParameters3)
    SHADER_PARAMETER(FVector4f, SkyAtmosphereAmbientContributionColorScale)
    SHADER_PARAMETER(FVector4f, InscatteringLightDirection)
    SHADER_PARAMETER(FVector4f, DirectionalInscatteringColor)
    SHADER_PARAMETER(FVector2f, SinCosInscatteringColorCubemapRotation)
    SHADER_PARAMETER(FVector3f, FogInscatteringTextureParameters)
    SHADER_PARAMETER(float, SkyLightCaptureAffectsHeightFogStrength)
    SHADER_PARAMETER(float, SkyLightCaptureAffectsHeightFogRoughness)
    SHADER_PARAMETER(float, SkyLightCaptureAffectsHeightFogMipCount)
    SHADER_PARAMETER(float, EndDistance)
    SHADER_PARAMETER(float, ApplyVolumetricFog)
    SHADER_PARAMETER(float, VolumetricFogStartDistance)
    SHADER_PARAMETER(float, VolumetricFogNearFadeInDistanceInv)
    SHADER_PARAMETER(FVector3f, VolumetricFogAlbedo)
    SHADER_PARAMETER(float, VolumetricFogPhaseG)
    SHADER_PARAMETER(float, EnableFSSS)
    SHADER_PARAMETER(float, FSSSSceneColorScatteringAmountScale)
    SHADER_PARAMETER(float, FSSSSceneColorScatteringAmountPower)
    SHADER_PARAMETER(float, FSSSSpreadScale)
    SHADER_PARAMETER(float, FSSSBlurControl)
    SHADER_PARAMETER_TEXTURE(TextureCube, FogInscatteringColorCubemap)
    SHADER_PARAMETER_SAMPLER(SamplerState, FogInscatteringColorSampler)
    SHADER_PARAMETER_RDG_TEXTURE(Texture3D, IntegratedLightScattering)
    SHADER_PARAMETER_SAMPLER(SamplerState, IntegratedLightScatteringSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FWuWaTranslucencyParameters, "WuWaTranslucency");

struct FWuWaSurfaceShaderElementData : FMeshMaterialShaderElementData
{
    FRHIUniformBuffer* Translucency = nullptr;
    FRHIUniformBuffer* Shadow = nullptr;
};

class FWuWaSceneColorCopyPS final : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FWuWaSceneColorCopyPS);
    SHADER_USE_PARAMETER_STRUCT(FWuWaSceneColorCopyPS, FGlobalShader);
public:
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneColorTexture)
        SHADER_PARAMETER_SAMPLER(SamplerState, SceneColorTextureSampler)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    { return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5); }
    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("SCENE_COLOR_COPY_ENCODED"), 1);
    }
};
IMPLEMENT_GLOBAL_SHADER(FWuWaSceneColorCopyPS, "/WuWa/Private/WuWaTranslucentSceneColorPass.usf", "MainCopyPS", SF_Pixel);

const TCHAR* PassName(EWuWaPass Pass)
{
    switch (Pass)
    {
    case EWuWaPass::Stencil: return TEXT("Stencil");
    case EWuWaPass::HairMask: return TEXT("HairMask");
    case EWuWaPass::HairBlend: return TEXT("HairBlend");
    case EWuWaPass::Outline: return TEXT("Outline");
    case EWuWaPass::TranslucentDepth: return TEXT("TranslucentDepth");
    case EWuWaPass::Translucent: return TEXT("Translucent");
    default: return TEXT("Surface");
    }
}

class FWuWaSurfaceVS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FWuWaSurfaceVS, MeshMaterial);
public:
    FWuWaSurfaceVS() = default;
    FWuWaSurfaceVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    {
        if (!Parameters.VertexFactoryType || Parameters.MaterialParameters.MaterialDomain != MD_Surface
            || !Parameters.MaterialParameters.bIsUsedWithSkeletalMesh) return false;
        const FString Filename = Parameters.VertexFactoryType->GetShaderFilename();
        // Source geometry and outlines share the native raw skin tangent basis.
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5)
            && Filename.EndsWith(TEXT("GpuSkinVertexFactory.ush"));
    }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FMeshMaterialShader::ModifyCompilationEnvironment(Parameters, Environment);
    }
};

class FWuWaOutlineVS final : public FWuWaSurfaceVS
{
    DECLARE_SHADER_TYPE(FWuWaOutlineVS, MeshMaterial);
public:
    class FPart : SHADER_PERMUTATION_INT("WUWA_OUTLINE_PART", 6);
    using FPermutationDomain = TShaderPermutationDomain<FPart>;
    FWuWaOutlineVS() = default;
    FWuWaOutlineVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FWuWaSurfaceVS(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    {
        return FWuWaSurfaceVS::ShouldCompilePermutation(Parameters)
            && (Parameters.MaterialParameters.BlendMode == BLEND_Opaque
                || Parameters.MaterialParameters.BlendMode == BLEND_Masked)
            && FPermutationDomain(Parameters.PermutationId).Get<FPart>() != 3;
    }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FWuWaSurfaceVS::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_OUTLINE_PASS"), 1);
    }
};

class FWuWaTranslucentVS final : public FWuWaSurfaceVS
{
    DECLARE_SHADER_TYPE(FWuWaTranslucentVS, MeshMaterial);
public:
    FWuWaTranslucentVS() = default;
    FWuWaTranslucentVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FWuWaSurfaceVS(Initializer)
    { TranslucencyParameter.Bind(Initializer.ParameterMap, TEXT("WuWaTranslucency")); }
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    { return FWuWaSurfaceVS::ShouldCompilePermutation(Parameters) && Parameters.MaterialParameters.BlendMode == BLEND_Translucent; }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FWuWaSurfaceVS::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_TRANSLUCENT_PASS"), 1);
        Environment.SetDefine(TEXT("NEEDS_BASEPASS_VERTEX_FOGGING"), 1);
    }
    void GetShaderBindings(const FScene* Scene, ERHIFeatureLevel::Type FeatureLevel,
        const FPrimitiveSceneProxy* Proxy, const FMaterialRenderProxy& MaterialProxy, const FMaterial& Material,
        const FWuWaSurfaceShaderElementData& Data, FMeshDrawSingleShaderBindings& ShaderBindings) const
    {
        FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, Proxy, MaterialProxy, Material, Data, ShaderBindings);
        if (TranslucencyParameter.IsBound()) ShaderBindings.Add(TranslucencyParameter, Data.Translucency);
    }
private:
    LAYOUT_FIELD(FShaderUniformBufferParameter, TranslucencyParameter);
};

class FWuWaTranslucentPS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FWuWaTranslucentPS, MeshMaterial);
public:
    FWuWaTranslucentPS() = default;
    FWuWaTranslucentPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer)
    {
        TranslucencyParameter.Bind(Initializer.ParameterMap, TEXT("WuWaTranslucency"));
        ShadowParameter.Bind(Initializer.ParameterMap, TEXT("WuWaShadow"));
    }
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    { return FWuWaTranslucentVS::ShouldCompilePermutation(Parameters); }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    { FWuWaTranslucentVS::ModifyCompilationEnvironment(Parameters, Environment); }
    void GetShaderBindings(const FScene* Scene, ERHIFeatureLevel::Type FeatureLevel,
        const FPrimitiveSceneProxy* Proxy, const FMaterialRenderProxy& MaterialProxy, const FMaterial& Material,
        const FWuWaSurfaceShaderElementData& Data, FMeshDrawSingleShaderBindings& ShaderBindings) const
    {
        FMeshMaterialShader::GetShaderBindings(Scene, FeatureLevel, Proxy, MaterialProxy, Material, Data, ShaderBindings);
        if (TranslucencyParameter.IsBound()) ShaderBindings.Add(TranslucencyParameter, Data.Translucency);
        if (ShadowParameter.IsBound()) ShaderBindings.Add(ShadowParameter, Data.Shadow);
    }
private:
    LAYOUT_FIELD(FShaderUniformBufferParameter, TranslucencyParameter);
    LAYOUT_FIELD(FShaderUniformBufferParameter, ShadowParameter);
};

class FWuWaTranslucentDepthPS final : public FWuWaTranslucentPS
{
    DECLARE_SHADER_TYPE(FWuWaTranslucentDepthPS, MeshMaterial);
public:
    FWuWaTranslucentDepthPS() = default;
    FWuWaTranslucentDepthPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FWuWaTranslucentPS(Initializer) {}
};

class FWuWaSurfacePS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FWuWaSurfacePS, MeshMaterial);
public:
    FWuWaSurfacePS() = default;
    FWuWaSurfacePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    { return FWuWaSurfaceVS::ShouldCompilePermutation(Parameters); }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FWuWaSurfaceVS::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("OUTPUT_PIXEL_DEPTH_OFFSET"), Parameters.MaterialParameters.bHasPixelDepthOffsetConnected ? 1 : 0);
    }
};

class FWuWaOutlinePS final : public FWuWaSurfacePS
{
    DECLARE_SHADER_TYPE(FWuWaOutlinePS, MeshMaterial);
public:
    FWuWaOutlinePS() = default;
    FWuWaOutlinePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FWuWaSurfacePS(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    {
        return FWuWaSurfacePS::ShouldCompilePermutation(Parameters)
            && (Parameters.MaterialParameters.BlendMode == BLEND_Opaque
                || Parameters.MaterialParameters.BlendMode == BLEND_Masked);
    }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FWuWaSurfacePS::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_OUTLINE_PASS"), 1);
    }
};

class FWuWaHairMaskPS final : public FWuWaSurfacePS
{
    DECLARE_SHADER_TYPE(FWuWaHairMaskPS, MeshMaterial);
public:
    FWuWaHairMaskPS() = default;
    FWuWaHairMaskPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FWuWaSurfacePS(Initializer) {}
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FWuWaSurfacePS::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_HAIR_MASK_PASS"), 1);
    }
};

class FWuWaHairBlendPS final : public FWuWaSurfacePS
{
    DECLARE_SHADER_TYPE(FWuWaHairBlendPS, MeshMaterial);
public:
    FWuWaHairBlendPS() = default;
    FWuWaHairBlendPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FWuWaSurfacePS(Initializer) {}
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    {
        FWuWaSurfacePS::ModifyCompilationEnvironment(Parameters, Environment);
        Environment.SetDefine(TEXT("WUWA_HAIR_BLEND_PASS"), 1);
    }
};

class FWuWaStencilPS final : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FWuWaStencilPS, MeshMaterial);
public:
    FWuWaStencilPS() = default;
    FWuWaStencilPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters)
    { return FWuWaSurfaceVS::ShouldCompilePermutation(Parameters) && Parameters.MaterialParameters.BlendMode == BLEND_Masked; }
    static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
    { FWuWaSurfaceVS::ModifyCompilationEnvironment(Parameters, Environment); }
};

IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaSurfaceVS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaSurfacePS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaStencilPS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainStencilPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaHairMaskPS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaHairBlendPS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaOutlineVS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaOutlinePS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaTranslucentVS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaTranslucentPS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainTranslucentPS"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FWuWaTranslucentDepthPS, TEXT("/WuWa/Private/WuWaSurfacePass.usf"), TEXT("MainTranslucentDepthPS"), SF_Pixel);

class FWuWaSurfaceMeshProcessor final : public FMeshPassProcessor
{
public:
    FWuWaSurfaceMeshProcessor(const FViewInfo& View, FMeshPassDrawListContext* Context, TSet<FString>& InMissingShaderMaterials, FCriticalSection& InDiagnosticsLock, EWuWaPass InPass, int32 InMaterialIndex,
        FRHIUniformBuffer* InTranslucency = nullptr, FRHIUniformBuffer* InShadow = nullptr)
        : FMeshPassProcessor(TEXT("WuWaSourceSurface"), View.Family->Scene->GetRenderScene(),
                            View.GetFeatureLevel(), &View, Context), MissingShaderMaterials(InMissingShaderMaterials), DiagnosticsLock(InDiagnosticsLock), Pass(InPass), MaterialIndex(InMaterialIndex), Translucency(InTranslucency), Shadow(InShadow) {}

    virtual void AddMeshBatch(const FMeshBatch& Mesh, uint64 Mask, const FPrimitiveSceneProxy* Proxy, int32 StaticId = -1) override
    {
        switch (Pass)
        {
        case EWuWaPass::Stencil: ProcessMesh<FWuWaStencilPS>(Mesh, Mask, Proxy, StaticId); break;
        case EWuWaPass::HairMask: ProcessMesh<FWuWaHairMaskPS>(Mesh, Mask, Proxy, StaticId); break;
        case EWuWaPass::HairBlend: ProcessMesh<FWuWaHairBlendPS>(Mesh, Mask, Proxy, StaticId); break;
        case EWuWaPass::Outline: ProcessMesh<FWuWaOutlinePS, FWuWaOutlineVS>(Mesh, Mask, Proxy, StaticId, MaterialIndex); break;
        case EWuWaPass::TranslucentDepth: ProcessMesh<FWuWaTranslucentDepthPS, FWuWaTranslucentVS>(Mesh, Mask, Proxy, StaticId); break;
        case EWuWaPass::Translucent: ProcessMesh<FWuWaTranslucentPS, FWuWaTranslucentVS>(Mesh, Mask, Proxy, StaticId); break;
        default: ProcessMesh<FWuWaSurfacePS>(Mesh, Mask, Proxy, StaticId); break;
        }
    }
    int32 GetSubmittedCount() const { return SubmittedCount; }

private:
    template<typename TPixelShader, typename TVertexShader = FWuWaSurfaceVS>
    void ProcessMesh(const FMeshBatch& Mesh, uint64 Mask, const FPrimitiveSceneProxy* Proxy, int32 StaticId, int32 VertexPermutation = 0)
    {
        if (!Mesh.VertexFactory || !Mesh.MaterialRenderProxy || Mesh.Elements.IsEmpty()) return;
        const FMaterial* Material = Mesh.MaterialRenderProxy->GetMaterialNoFallback(FeatureLevel);
        if (!Material) return;
        const bool bTranslucentPass = Pass == EWuWaPass::TranslucentDepth || Pass == EWuWaPass::Translucent;
        if (IsTranslucentBlendMode(Material->GetBlendMode()) != bTranslucentPass) return;
        FMaterialShaderTypes Types;
        Types.AddShaderType<TVertexShader>(VertexPermutation);
        Types.AddShaderType<TPixelShader>();
        FMaterialShaders Compiled;
        if (!Material->TryGetShaders(Types, Mesh.VertexFactory->GetType(), Compiled))
        {
            FMaterialShaderTypes VertexTypes, PixelTypes;
            VertexTypes.AddShaderType<TVertexShader>(VertexPermutation);
            PixelTypes.AddShaderType<TPixelShader>();
            FMaterialShaders Available;
            const bool bVertexAvailable = Material->TryGetShaders(VertexTypes, Mesh.VertexFactory->GetType(), Available);
            const bool bPixelAvailable = Material->TryGetShaders(PixelTypes, Mesh.VertexFactory->GetType(), Available);
            const FString Name = FString::Printf(TEXT("%s: %s [%s] VS=%d PS=%d Masked=%d"), PassName(Pass),
                *Material->GetFriendlyName(), Mesh.VertexFactory->GetType()->GetName(), bVertexAvailable, bPixelAvailable, Material->IsMasked());
            FScopeLock Lock(&DiagnosticsLock);
            if (!MissingShaderMaterials.Contains(Name))
            {
                MissingShaderMaterials.Add(Name);
                UE_LOG(LogWuWaSurface, Warning, TEXT("Source shaders unavailable: %s"), *Name);
            }
            return;
        }
        TMeshProcessorShaders<TVertexShader, TPixelShader> Shaders;
        Compiled.TryGetVertexShader(Shaders.VertexShader);
        Compiled.TryGetPixelShader(Shaders.PixelShader);
        FMeshPassProcessorRenderState State;
        if (Pass == EWuWaPass::TranslucentDepth)
        {
            State.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());
            State.SetDepthStencilState(TStaticDepthStencilState<true, CF_GreaterEqual>::GetRHI());
        }
        else if (Pass == EWuWaPass::Translucent)
        {
            State.SetBlendState(TStaticBlendState<CW_RGB, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha>::GetRHI());
            State.SetDepthStencilState(TStaticDepthStencilState<false, CF_GreaterEqual>::GetRHI());
        }
        else if (Pass == EWuWaPass::Stencil)
        {
            State.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());
            State.SetDepthStencilState(TStaticDepthStencilState<true, CF_GreaterEqual,
                true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
                true, CF_Always, SO_Keep, SO_Keep, SO_Replace, 0x1e, 0xfe>::GetRHI());
            State.SetStencilRef(6);
        }
        else if (Pass == EWuWaPass::HairMask)
        {
            State.SetBlendState(TStaticBlendState<
                CW_NONE, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_BLUE, BO_Min, BF_One, BF_Zero, BO_Min, BF_One, BF_Zero,
                CW_NONE, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_NONE, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_NONE, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_NONE, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_NONE>::GetRHI());
            State.SetDepthStencilState(TStaticDepthStencilState<false, CF_GreaterEqual,
                true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
                true, CF_Equal, SO_Keep, SO_Keep, SO_Keep, 0x1a, 0xfa>::GetRHI());
            State.SetStencilRef(2);
        }
        else if (Pass == EWuWaPass::HairBlend)
        {
            State.SetBlendState(TStaticBlendState<
                CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_Zero>::GetRHI());
            State.SetDepthStencilState(TStaticDepthStencilState<true, CF_GreaterEqual,
                true, CF_Equal, SO_Keep, SO_Keep, SO_Keep,
                true, CF_Equal, SO_Keep, SO_Keep, SO_Keep, 0x04, 0xfe>::GetRHI());
            State.SetStencilRef(6);
        }
        else
        {
            State.SetBlendState(TStaticBlendState<>::GetRHI());
            if (Pass == EWuWaPass::Surface && MaterialIndex == 4)
            {
                State.SetDepthStencilState(TStaticDepthStencilState<true, CF_GreaterEqual,
                    true, CF_NotEqual, SO_Keep, SO_Keep, SO_Replace,
                    true, CF_NotEqual, SO_Keep, SO_Keep, SO_Replace, 0x1e, 0xfa>::GetRHI());
                State.SetStencilRef(6);
            }
            else
            {
                State.SetDepthStencilState(TStaticDepthStencilState<true, CF_GreaterEqual,
                    true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
                    true, CF_Always, SO_Keep, SO_Keep, SO_Replace, 0x02, 0xfa>::GetRHI());
                State.SetStencilRef(2);
            }
            if (Pass == EWuWaPass::Outline)
                State.SetBlendState(TStaticBlendState<
                    CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                    CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                    CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                    CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                    CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero,
                    CW_NONE>::GetRHI());
        }
        State.SetDepthStencilAccess(Pass == EWuWaPass::Translucent ? FExclusiveDepthStencil::DepthRead_StencilNop :
            Pass == EWuWaPass::TranslucentDepth ? FExclusiveDepthStencil::DepthWrite_StencilNop : FExclusiveDepthStencil::DepthWrite_StencilWrite);
        const FMeshDrawingPolicyOverrideSettings Overrides = ComputeMeshOverrideSettings(Mesh);
        FWuWaSurfaceShaderElementData Data;
        Data.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, Proxy, Mesh, StaticId, false);
        Data.Translucency = Translucency;
        Data.Shadow = Shadow;
        ERasterizerCullMode CullMode = ComputeMeshCullMode(*Material, Overrides);
        if (Pass == EWuWaPass::Outline && CullMode != CM_None)
            CullMode = CullMode == CM_CW ? CM_CCW : CM_CW;
        BuildMeshDrawCommands(Mesh, Mask, Proxy, *Mesh.MaterialRenderProxy, *Material, State, Shaders,
            ComputeMeshFillMode(*Material, Overrides), CullMode,
            CalculateMeshStaticSortKey(Shaders.VertexShader, Shaders.PixelShader), EMeshPassFeatures::Default, Data);
        ++SubmittedCount;
    }

    TSet<FString>& MissingShaderMaterials;
    FCriticalSection& DiagnosticsLock;
    EWuWaPass Pass;
    int32 MaterialIndex;
    int32 SubmittedCount = 0;
    FRHIUniformBuffer* Translucency;
    FRHIUniformBuffer* Shadow;
};

BEGIN_SHADER_PARAMETER_STRUCT(FWuWaSurfacePassParameters, )
    SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
    SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FWuWaTranslucentPassParameters, )
    SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters, View)
    SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FWuWaTranslucencyParameters, Translucency)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FWuWaShadowMapParameters, Shadow)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FWuWaSurfacePassParameters* AllocateWuWaPassParameters(FRDGBuilder& Graph, const FViewInfo& View, FRDGTextureRef DepthStencil)
{
    auto* Parameters = Graph.AllocParameters<FWuWaSurfacePassParameters>();
    Parameters->View.View = View.ViewUniformBuffer;
    Parameters->View.InstancedView = View.GetInstancedViewUniformBuffer();
    Parameters->InstanceCullingDrawParams.Scene = View.GetSceneUniforms().GetBuffer(Graph);
    Parameters->InstanceCullingDrawParams.InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(Graph);
    Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthStencil,
        ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad, FExclusiveDepthStencil::DepthWrite_StencilWrite);
    return Parameters;
}
}

FWuWaSurfaceRenderer::FWuWaSurfaceRenderer(const FAutoRegister& AutoRegister, UWorld* World)
    : FWorldSceneViewExtension(AutoRegister, World), Scene(World->Scene), WorldName(World->GetPathName()) {}

void FWuWaSurfaceRenderer::InitializePasses()
{
    PostOpaqueHandle=FModuleManager::LoadModuleChecked<IRendererModule>(TEXT("Renderer"))
        .RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateSP(
            StaticCastSharedRef<FWuWaSurfaceRenderer>(AsShared()),&FWuWaSurfaceRenderer::PostOpaque_RenderThread));
}

void FWuWaSurfaceRenderer::ShutdownPasses()
{
    if (PostOpaqueHandle.IsValid())
        if (auto* Module=FModuleManager::GetModulePtr<IRendererModule>(TEXT("Renderer")))
            Module->RemovePostOpaqueRenderDelegate(PostOpaqueHandle);
    PostOpaqueHandle.Reset();
}

void FWuWaSurfaceRenderer::PostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters)
{
    if (!Parameters.View || Parameters.View->Family->Scene!=Scene || !Parameters.GraphBuilder) return;
    const auto* Source=FindBuffers(*Parameters.View);
    if (!Source) return;
    // Commit character color/depth before native translucency, including normal
    // materials mixed into the same skeletal mesh. Tone mapping remains once per view.
    if (AddWuWaLightingPasses(*Parameters.GraphBuilder,*Parameters.View,*Source,Parameters.ColorTexture,Lighting))
    {
        bool& Submitted=Parameters.View->bIsSceneCapture ? bReflectionLightingSubmitted : bLightingSubmitted;
        if (!Submitted)
        {
            Submitted=true;
            UE_LOG(LogWuWaSurface,Display,TEXT("Source lighting composed: Stage=PostOpaque World=%s Capture=%d"),
                *WorldName,Parameters.View->bIsSceneCapture ? 1 : 0);
        }
    }
}

void FWuWaSurfaceRenderer::SetReflectionView(const FRenderTarget* Target, const FPlane& Plane)
{
    check(IsInGameThread());
    ReflectionViewTarget = Target;
    ReflectionPlane = Plane;
}

void FWuWaSurfaceRenderer::SetupView(FSceneViewFamily& Family, FSceneView& View)
{
    if (!View.bIsSceneCapture || !ReflectionViewTarget || Family.RenderTarget != ReflectionViewTarget) return;
    // Use UE's planar mirror transform before shading, including screen-space background evaluation.
    View.UpdatePlanarReflectionViewMatrix(View, FMirrorMatrix(ReflectionPlane));
    View.ViewLocation = View.ViewMatrices.GetViewOrigin();
    View.bReverseCulling = View.ViewMatrices.GetWorldToView().Determinant() < 0.0f;
}

void FWuWaSurfaceRenderer::SetMaterials(TConstArrayView<FWuWaSurfaceMaterialBinding> InMaterials,
    const FRenderTarget* ReflectionTarget)
{
    const TSharedRef<FWuWaSurfaceRenderer, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FWuWaSurfaceRenderer>(AsShared());
    TArray<FWuWaSurfaceMaterialBinding> Bindings;
    Bindings.Append(InMaterials.GetData(), InMaterials.Num());
    ENQUEUE_RENDER_COMMAND(WuWaRegisterSurface)([Self, ReflectionTarget, Bindings = MoveTemp(Bindings)](FRHICommandListImmediate&) mutable
    {
        Self->ReflectionRenderTarget = ReflectionTarget;
        Self->Materials = MoveTemp(Bindings);
        FScopeLock Lock(&Self->DiagnosticsLock);
        Self->MissingShaderMaterials.Reset();
        Self->SubmittedPasses[0].Reset();
        Self->SubmittedPasses[1].Reset();
        Self->bLightingSubmitted = false;
        Self->bToneMappingSubmitted = false;
        Self->bReflectionLightingSubmitted = false;
        Self->bReflectionFilterSubmitted = false;
    });
}

void FWuWaSurfaceRenderer::ClearMaterials()
{
    ReflectionViewTarget = nullptr;
    const TSharedRef<FWuWaSurfaceRenderer, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FWuWaSurfaceRenderer>(AsShared());
    ENQUEUE_RENDER_COMMAND(WuWaRemoveSurface)([Self](FRHICommandListImmediate&)
    {
        Self->ReflectionRenderTarget = nullptr;
        Self->Materials.Reset();
        FScopeLock Lock(&Self->DiagnosticsLock);
        Self->MissingShaderMaterials.Reset();
        Self->SubmittedPasses[0].Reset();
        Self->SubmittedPasses[1].Reset();
    });
}

void FWuWaSurfaceRenderer::SetLightingInputs(const FWuWaLightingInputs& Inputs)
{
    const TSharedRef<FWuWaSurfaceRenderer, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FWuWaSurfaceRenderer>(AsShared());
    ENQUEUE_RENDER_COMMAND(WuWaUpdateLighting)([Self, Inputs](FRHICommandListImmediate&) { Self->Lighting = Inputs; });
}

bool FWuWaSurfaceRenderer::IsSourcePrimitive(FPrimitiveComponentId Id) const
{
    return Materials.ContainsByPredicate(
        [Id](const FWuWaSurfaceMaterialBinding& Binding) { return Binding.Primitive == Id; });
}

void FWuWaSurfaceRenderer::AddTranslucencyPasses(FRDGBuilder& Graph, const FViewInfo& View, FRDGTextureRef SceneColor)
{
    if (!SceneColor || Materials.IsEmpty()) return;
    TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> Meshes;
    for (const FMeshBatchAndRelevance& Mesh : View.DynamicMeshElements)
    {
        if (!Mesh.PrimitiveSceneProxy || !IsSourcePrimitive(Mesh.PrimitiveSceneProxy->GetPrimitiveComponentId())
            || !Mesh.Mesh->MaterialRenderProxy) continue;
        if (!Materials.ContainsByPredicate([&Mesh](const FWuWaSurfaceMaterialBinding& Binding)
            { return Binding.Primitive == Mesh.PrimitiveSceneProxy->GetPrimitiveComponentId()
                && Binding.Surface == Mesh.Mesh->MaterialRenderProxy; })) continue;
        const FMaterial* Material = Mesh.Mesh->MaterialRenderProxy->GetMaterialNoFallback(View.GetFeatureLevel());
        if (Material && Material->GetBlendMode() == BLEND_Translucent) Meshes.Add(Mesh);
    }
    if (Meshes.IsEmpty()) return;
    auto Shadow = CreateWuWaShadowParameters(Graph, View);
    if (!Shadow) return;

    FRDGTextureRef Copy = Graph.CreateTexture(FRDGTextureDesc::Create2D(SceneColor->Desc.Extent, PF_B8G8R8A8,
        FClearValueBinding::Transparent, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("WuWa.TranslucentSceneColor"));
    auto* CopyParameters = Graph.AllocParameters<FWuWaSceneColorCopyPS::FParameters>();
    CopyParameters->View = View.ViewUniformBuffer;
    CopyParameters->SceneColorTexture = SceneColor;
    CopyParameters->SceneColorTextureSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    CopyParameters->RenderTargets[0] = FRenderTargetBinding(Copy, ERenderTargetLoadAction::EClear);
    TShaderMapRef<FWuWaSceneColorCopyPS> CopyShader(View.ShaderMap);
    const FScreenPassTextureViewport Viewport(SceneColor, View.ViewRect);
    AddDrawScreenPass(Graph, RDG_EVENT_NAME("WuWa Native Encoded Scene Color"), View, Viewport, Viewport, CopyShader, CopyParameters);

    auto* TranslucentInputs = Graph.AllocParameters<FWuWaTranslucencyParameters>();
    TranslucentInputs->EncodedSceneColor = Copy;
    TranslucentInputs->WhiteTexture = GWhiteTexture->TextureRHI;
    TranslucentInputs->BlackVolume = GBlackVolumeTexture->TextureRHI;
    TranslucentInputs->RimDirection = Lighting.RimDirection;
    TranslucentInputs->LightTint = Lighting.LightTint;
    // Forward height-fog math remains native. Copy the current view values
    // because the installed renderer's private fog metadata is not exported.
    TranslucentInputs->ExponentialFogParameters = View.ExponentialFogParameters;
    TranslucentInputs->ExponentialFogParameters2 = View.ExponentialFogParameters2;
    TranslucentInputs->ExponentialFogColorParameter = FVector4f(View.ExponentialFogColor, 1.f - View.FogMaxOpacity);
    TranslucentInputs->ExponentialFogParameters3 = View.ExponentialFogParameters3;
    TranslucentInputs->SkyAtmosphereAmbientContributionColorScale = View.SkyAtmosphereAmbientContributionColorScale;
    TranslucentInputs->InscatteringLightDirection = FVector4f(FVector3f(View.InscatteringLightDirection),
        View.bUseDirectionalInscattering ? FMath::Max(0.f, View.DirectionalInscatteringStartDistance) : -1.f);
    TranslucentInputs->DirectionalInscatteringColor = FVector4f(FVector3f(View.DirectionalInscatteringColor), View.DirectionalInscatteringExponent);
    TranslucentInputs->SinCosInscatteringColorCubemapRotation = View.SinCosInscatteringColorCubemapRotation;
    TranslucentInputs->FogInscatteringTextureParameters = FVector3f(View.FogInscatteringTextureParameters);
    TranslucentInputs->SkyLightCaptureAffectsHeightFogStrength = 0;
    TranslucentInputs->SkyLightCaptureAffectsHeightFogRoughness = 0;
    TranslucentInputs->SkyLightCaptureAffectsHeightFogMipCount = 0;
    TranslucentInputs->EndDistance = View.FogEndDistance;
    TranslucentInputs->ApplyVolumetricFog = View.VolumetricFogResources.IntegratedLightScatteringTexture ? 1.f : 0.f;
    TranslucentInputs->VolumetricFogStartDistance = View.VolumetricFogStartDistance;
    TranslucentInputs->VolumetricFogNearFadeInDistanceInv = View.VolumetricFogNearFadeInDistanceInv;
    TranslucentInputs->VolumetricFogAlbedo = View.VolumetricFogAlbedo;
    TranslucentInputs->VolumetricFogPhaseG = View.VolumetricFogPhaseG;
    TranslucentInputs->EnableFSSS = 0;
    TranslucentInputs->FSSSSceneColorScatteringAmountScale = 0;
    TranslucentInputs->FSSSSceneColorScatteringAmountPower = 1;
    TranslucentInputs->FSSSSpreadScale = 0;
    TranslucentInputs->FSSSBlurControl = 0;
    TranslucentInputs->FogInscatteringColorCubemap = View.FogInscatteringColorCubemap && View.FogInscatteringColorCubemap->GetResource()
        ? View.FogInscatteringColorCubemap->GetResource()->TextureRHI : GBlackTextureCube->TextureRHI;
    TranslucentInputs->FogInscatteringColorSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    TranslucentInputs->IntegratedLightScattering = View.VolumetricFogResources.IntegratedLightScatteringTexture
        ? View.VolumetricFogResources.IntegratedLightScatteringTexture : FRDGSystemTextures::Get(Graph).VolumetricBlackAlphaOne;
    TranslucentInputs->IntegratedLightScatteringSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
    auto Translucency = Graph.CreateUniformBuffer(TranslucentInputs);
    const TSharedRef<FWuWaSurfaceRenderer, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FWuWaSurfaceRenderer>(AsShared());
    for (EWuWaPass Pass : {EWuWaPass::TranslucentDepth, EWuWaPass::Translucent})
    {
        const bool bDepthOnly = Pass == EWuWaPass::TranslucentDepth;
        auto* Parameters = Graph.AllocParameters<FWuWaTranslucentPassParameters>();
        Parameters->View.View = View.ViewUniformBuffer;
        Parameters->View.InstancedView = View.GetInstancedViewUniformBuffer();
        Parameters->InstanceCullingDrawParams.Scene = View.GetSceneUniforms().GetBuffer(Graph);
        Parameters->InstanceCullingDrawParams.InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(Graph);
        Parameters->Translucency = Translucency;
        Parameters->Shadow = Shadow;
        if (!bDepthOnly) Parameters->RenderTargets[0] = FRenderTargetBinding(SceneColor, ERenderTargetLoadAction::ELoad);
        Parameters->RenderTargets.DepthStencil = FDepthStencilBinding(View.GetSceneTextures().Depth.Target,
            ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ELoad,
            bDepthOnly ? FExclusiveDepthStencil::DepthWrite_StencilNop : FExclusiveDepthStencil::DepthRead_StencilNop);
        Graph.AddPass(RDG_EVENT_NAME("WuWa %s", PassName(Pass)), Parameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
            [&View, Self, Meshes, Pass, Translucency, Shadow](FRHICommandList& Commands)
        {
            Commands.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
            int32 SubmittedCount = 0;
            DrawDynamicMeshPass(View, Commands, [&View, &Self, &Meshes, Pass, Translucency, Shadow, &SubmittedCount](FDynamicPassMeshDrawListContext* Context)
            {
                FWuWaSurfaceMeshProcessor Processor(View, Context, Self->MissingShaderMaterials, Self->DiagnosticsLock,
                    Pass, 0, Translucency->GetRHI(), Shadow->GetRHI());
                for (const FMeshBatchAndRelevance& Mesh : Meshes)
                    Processor.AddMeshBatch(*Mesh.Mesh, ~0ull, Mesh.PrimitiveSceneProxy);
                SubmittedCount = Processor.GetSubmittedCount();
            });
            FScopeLock Lock(&Self->DiagnosticsLock);
            TSet<uint32>& Submitted = Self->SubmittedPasses[View.bIsSceneCapture ? 1 : 0];
            const uint32 PassKey = 0x10000u + static_cast<uint8>(Pass);
            if (SubmittedCount > 0 && !Submitted.Contains(PassKey))
            {
                Submitted.Add(PassKey);
                UE_LOG(LogWuWaSurface, Display, TEXT("Source forward commands submitted: %s Batches=%d World=%s Capture=%d"),
                    PassName(Pass), SubmittedCount, *Self->WorldName, View.bIsSceneCapture ? 1 : 0);
            }
        });
    }
}

void FWuWaSurfaceRenderer::PrePostProcessPass_RenderThread(FRDGBuilder& Graph, const FSceneView& InView,
    const FPostProcessingInputs& Inputs)
{
    if (InView.Family->Scene != Scene || !InView.bIsViewInfo || InView.bIsSceneCapture || !Inputs.SceneTextures) return;
    const FWuWaSurfaceBuffers* Source = FindBuffers(InView);
    if (!Source) return;
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    AddTranslucencyPasses(Graph, View, Inputs.SceneTextures->GetContents()->SceneColorTexture);
}

void FWuWaSurfaceRenderer::PreRenderViewFamily_RenderThread(FRDGBuilder&, FSceneViewFamily&)
{
    ViewBuffers.Reset();
}

void FWuWaSurfaceRenderer::PostRenderView_RenderThread(FRDGBuilder& Graph, FSceneView& InView)
{
    if (InView.Family->Scene != Scene || !InView.bIsViewInfo || !InView.bIsSceneCapture
        || !ReflectionRenderTarget || InView.Family->RenderTarget != ReflectionRenderTarget) return;
    // Deferred scene captures copy the complete HDR scene before OnRenderFinish
    // invokes this hook. Filter that output so translucent backdrop effects are
    // included, without applying display conversion or reading pixels on CPU.
    FRDGTextureRef Output = Graph.FindExternalTexture(ReflectionRenderTarget->GetRenderTargetTexture());
    // Scene captures copy after the native backdrop translucency. Glass reads
    // that complete image and is drawn before the source reflection filter.
    AddTranslucencyPasses(Graph, static_cast<const FViewInfo&>(InView), Output);
    if (!Output) return;
    AddWuWaReflectionBlurPasses(Graph, static_cast<const FViewInfo&>(InView), Output);
    if (!bReflectionFilterSubmitted)
    {
        bReflectionFilterSubmitted = true;
        UE_LOG(LogWuWaSurface, Display, TEXT("Source reflection filtered: BlurPasses=4 Stage=AfterSceneCaptureCopy World=%s"), *WorldName);
    }
}

void FWuWaSurfaceRenderer::SubscribeToPostProcessingPass(EPostProcessingPass Pass, const FSceneView& View,
    FPostProcessingPassDelegateArray& Callbacks, bool bIsPassEnabled)
{
    if (Pass == EPostProcessingPass::ReplacingTonemapper && bIsPassEnabled
        && View.Family->Scene == Scene && View.bIsViewInfo && !View.bIsSceneCapture && !Materials.IsEmpty())
    {
        Callbacks.Add(FPostProcessingPassDelegate::CreateSP(this, &FWuWaSurfaceRenderer::ToneMapping_RenderThread));
    }
}

FScreenPassTexture FWuWaSurfaceRenderer::ToneMapping_RenderThread(FRDGBuilder& Graph, const FSceneView& InView,
    const FPostProcessMaterialInputs& Inputs)
{
    const FWuWaSurfaceBuffers* Source = FindBuffers(InView);
    uint32 BloomPassCount = 0;
    FScreenPassTexture Output = AddWuWaToneMappingPass(Graph, static_cast<const FViewInfo&>(InView), Inputs, Source, ColorGradingLUT, BloomPassCount);
    if (!bToneMappingSubmitted)
    {
        bToneMappingSubmitted = true;
        UE_LOG(LogWuWaSurface, Display, TEXT("Source tone mapping composed: LUT=32x32x32 World=%s SourceBuffers=%d BloomPasses=%u"), *WorldName, Source ? 1 : 0, BloomPassCount);
    }
    return Output;
}

const FWuWaSurfaceBuffers* FWuWaSurfaceRenderer::FindBuffers(const FSceneView& View) const
{
    check(IsInRenderingThread());
    return ViewBuffers.Find(&View);
}

void FWuWaSurfaceRenderer::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& Graph, FSceneView& InView,
    const FRenderTargetBindingSlots&, TRDGUniformBufferRef<FSceneTextureUniformParameters>)
{
    if (Materials.IsEmpty() || InView.Family->Scene != Scene || !InView.bIsViewInfo) return;
    const FViewInfo& View = static_cast<const FViewInfo&>(InView);
    TArray<FMeshBatchAndRelevance, SceneRenderingAllocator> Meshes;
    for (const FMeshBatchAndRelevance& Mesh : View.DynamicMeshElements)
        if (Mesh.PrimitiveSceneProxy && IsSourcePrimitive(Mesh.PrimitiveSceneProxy->GetPrimitiveComponentId())) Meshes.Add(Mesh);
    if (Meshes.IsEmpty()) return;

    static const EPixelFormat Formats[7] = {PF_FloatRGBA, PF_A2B10G10R10, PF_B8G8R8A8,
        PF_B8G8R8A8, PF_A16B16G16R16, PF_R16F, PF_B8G8R8A8};
    static const TCHAR* Names[7] = {TEXT("WuWa.Emissive"), TEXT("WuWa.EncodedNormal"), TEXT("WuWa.EncodedBase"),
        TEXT("WuWa.SurfaceColor"), TEXT("WuWa.Motion"), TEXT("WuWa.CharacterDepth"), TEXT("WuWa.ToonData")};
    FWuWaSurfaceBuffers Buffers;
    FRDGTextureRef NativeDepth = View.GetSceneTextures().Depth.Target;
    FRDGTextureDesc DepthDesc = NativeDepth->Desc;
    DepthDesc.ClearValue = FClearValueBinding::DepthFar;
    Buffers.DepthStencil = Graph.CreateTexture(DepthDesc, TEXT("WuWa.SourceDepthStencil"));
    // Build the character layers in source order. Native scene depth already
    // contains the hair and would reject the earlier face/eye stencil layers.
    // Scene occlusion belongs to composition; UE depth/stencil remains untouched.
    AddClearDepthStencilPass(Graph, Buffers.DepthStencil);
    const FIntPoint Extent = NativeDepth->Desc.Extent;
    for (int32 Index = 0; Index < 7; ++Index)
    {
        ETextureCreateFlags Flags = TexCreate_RenderTargetable | TexCreate_ShaderResource;
        if (Index == 3) Flags |= TexCreate_SRGB;
        Buffers.Channels[Index] = Graph.CreateTexture(FRDGTextureDesc::Create2D(Extent, Formats[Index],
            FClearValueBinding::Transparent, Flags), Names[Index]);
    }
    const TSharedRef<FWuWaSurfaceRenderer, ESPMode::ThreadSafe> Self = StaticCastSharedRef<FWuWaSurfaceRenderer>(AsShared());
    bool bTargetsInitialized = false;
    auto AddMaterialPass = [&](int32 BindingIndex, EWuWaPass Pass)
    {
        const bool bStencil = Pass == EWuWaPass::Stencil;
        const FWuWaSurfaceMaterialBinding Binding = Materials[BindingIndex];
        if (!Binding.Surface || (bStencil && !Binding.Stencil)) return;
        const int32 MaterialIndex = static_cast<int32>(Binding.Part);
        auto* Parameters = AllocateWuWaPassParameters(Graph, View, Buffers.DepthStencil);
        if (!bStencil)
        {
            for (int32 Index = 0; Index < 7; ++Index)
                Parameters->RenderTargets[Index] = FRenderTargetBinding(Buffers.Channels[Index],
                    !bTargetsInitialized ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad);
            bTargetsInitialized = true;
        }
        Graph.AddPass(RDG_EVENT_NAME("WuWa %s %d Part=%d", PassName(Pass), BindingIndex, MaterialIndex),
            Parameters, ERDGPassFlags::Raster | ERDGPassFlags::NeverCull,
        [&View, Self, Meshes, Binding, bStencil, MaterialIndex, BindingIndex, Pass](FRHICommandList& Commands)
        {
            Commands.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1);
            if (!Binding.Surface || (bStencil && !Binding.Stencil))
            {
                FScopeLock Lock(&Self->DiagnosticsLock);
                const FString Warning = TEXT("Incomplete source material binding");
                if (!Self->MissingShaderMaterials.Contains(Warning))
                {
                    Self->MissingShaderMaterials.Add(Warning);
                    UE_LOG(LogWuWaSurface, Warning, TEXT("%s"), *Warning);
                }
                return;
            }
            int32 SubmittedCount = 0;
            DrawDynamicMeshPass(View, Commands, [&View, &Meshes, &Self, Binding, bStencil, Pass, MaterialIndex, &SubmittedCount](FDynamicPassMeshDrawListContext* Context)
            {
                FWuWaSurfaceMeshProcessor Processor(View, Context, Self->MissingShaderMaterials, Self->DiagnosticsLock, Pass, MaterialIndex);
                for (const FMeshBatchAndRelevance& Mesh : Meshes)
                    if (Mesh.PrimitiveSceneProxy->GetPrimitiveComponentId() == Binding.Primitive
                        && Mesh.Mesh->MaterialRenderProxy == Binding.Surface)
                    {
                        FMeshBatch SourceMesh = *Mesh.Mesh;
                        if (bStencil) SourceMesh.MaterialRenderProxy = Binding.Stencil;
                        if (Pass == EWuWaPass::Outline && Binding.Outline) SourceMesh.MaterialRenderProxy = Binding.Outline;
                        Processor.AddMeshBatch(SourceMesh, ~0ull, Mesh.PrimitiveSceneProxy);
                    }
                SubmittedCount = Processor.GetSubmittedCount();
            });
            constexpr uint32 PassCount = static_cast<uint8>(EWuWaPass::Translucent) + 1;
            const uint32 PassKey = BindingIndex * PassCount + static_cast<uint8>(Pass);
            FScopeLock Lock(&Self->DiagnosticsLock);
            TSet<uint32>& Submitted = Self->SubmittedPasses[View.bIsSceneCapture ? 1 : 0];
            if (SubmittedCount > 0 && !Submitted.Contains(PassKey))
            {
                Submitted.Add(PassKey);
                UE_LOG(LogWuWaSurface, Display, TEXT("Source mesh commands submitted: %s %d Batches=%d World=%s Capture=%d Part=%d"),
                    PassName(Pass), BindingIndex, SubmittedCount, *Self->WorldName, View.bIsSceneCapture ? 1 : 0, MaterialIndex);
            }
        });
    };
    auto AddPartPasses = [&](EWuWaSurfacePart Part, EWuWaPass Pass)
    {
        for (int32 Index = 0; Index < Materials.Num(); ++Index)
            if (Materials[Index].Part == Part && (Pass != EWuWaPass::Outline || Materials[Index].bOutline))
                AddMaterialPass(Index, Pass);
    };
    // Stable semantic order supports any number of body/cloth/tail sections.
    // Source stencil remains before bangs and its separate mask/blend passes.
    for (EWuWaSurfacePart Part : {EWuWaSurfacePart::Face, EWuWaSurfacePart::Down,
        EWuWaSurfacePart::Up, EWuWaSurfacePart::Eye}) AddPartPasses(Part, EWuWaPass::Surface);
    AddPartPasses(EWuWaSurfacePart::Face, EWuWaPass::Stencil);
    AddPartPasses(EWuWaSurfacePart::Eye, EWuWaPass::Stencil);
    AddPartPasses(EWuWaSurfacePart::Bangs, EWuWaPass::HairMask);
    AddPartPasses(EWuWaSurfacePart::Bangs, EWuWaPass::Surface);
    AddPartPasses(EWuWaSurfacePart::Hair, EWuWaPass::Surface);
    AddPartPasses(EWuWaSurfacePart::Bangs, EWuWaPass::HairBlend);
    AddPartPasses(EWuWaSurfacePart::Star, EWuWaPass::Surface);
    for (EWuWaSurfacePart Part : {EWuWaSurfacePart::Down, EWuWaSurfacePart::Bangs,
        EWuWaSurfacePart::Hair, EWuWaSurfacePart::Face, EWuWaSurfacePart::Up}) AddPartPasses(Part, EWuWaPass::Outline);
    if (!bTargetsInitialized) return;
    ViewBuffers.Add(&InView, Buffers);

}
