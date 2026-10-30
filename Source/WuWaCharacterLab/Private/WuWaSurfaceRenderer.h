#pragma once

#include "CoreMinimal.h"
#include "PrimitiveComponentId.h"
#include "SceneViewExtension.h"
#include "RenderGraphResources.h"
#include "WuWaLightingRenderer.h"
#include "WuWaCharacterProfile.h"

class FSceneInterface;
class FMaterialRenderProxy;
class UWorld;
class FPostOpaqueRenderParameters;

struct FWuWaSurfaceMaterialBinding
{
    FPrimitiveComponentId Primitive;
    const FMaterialRenderProxy* Surface = nullptr;
    const FMaterialRenderProxy* Stencil = nullptr;
    const FMaterialRenderProxy* Outline = nullptr;
    EWuWaSurfacePart Part = EWuWaSurfacePart::Up;
    bool bOutline = true;
};

struct FWuWaSurfaceBuffers
{
    FRDGTextureRef Channels[7] = {};
    FRDGTextureRef DepthStencil = nullptr;
};

/** Native skeletal material evaluation; owns only this view's source targets. */
class FWuWaSurfaceRenderer final : public FWorldSceneViewExtension
{
public:
    FWuWaSurfaceRenderer(const FAutoRegister& AutoRegister, UWorld* World);
    void InitializePasses();
    void ShutdownPasses();
    void SetMaterials(TConstArrayView<FWuWaSurfaceMaterialBinding> Materials,
        const FRenderTarget* ReflectionTarget);
    void ClearMaterials();
    void SetReflectionView(const FRenderTarget* Target, const FPlane& Plane);
    void SetLightingInputs(const FWuWaLightingInputs& Inputs);
    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily&, FSceneView&) override;
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
    virtual void PreRenderViewFamily_RenderThread(FRDGBuilder&, FSceneViewFamily&) override;
    virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder&, FSceneView&,
        const FRenderTargetBindingSlots&, TRDGUniformBufferRef<FSceneTextureUniformParameters>) override;
    virtual void PrePostProcessPass_RenderThread(FRDGBuilder&, const FSceneView&, const FPostProcessingInputs&) override;
    virtual void PostRenderView_RenderThread(FRDGBuilder&, FSceneView&) override;
    virtual void SubscribeToPostProcessingPass(EPostProcessingPass, const FSceneView&,
        FPostProcessingPassDelegateArray&, bool bIsPassEnabled) override;

    const FWuWaSurfaceBuffers* FindBuffers(const FSceneView& View) const;

private:
    const FSceneInterface* Scene;
    FDelegateHandle PostOpaqueHandle;
    const FRenderTarget* ReflectionRenderTarget = nullptr;
    // View construction runs on the game thread; render passes use the queued target above.
    const FRenderTarget* ReflectionViewTarget = nullptr;
    FPlane ReflectionPlane = FPlane(FVector::ZeroVector, FVector::UpVector);
    TArray<FWuWaSurfaceMaterialBinding> Materials;
    TMap<const FSceneView*, FWuWaSurfaceBuffers> ViewBuffers;
    FCriticalSection DiagnosticsLock;
    TSet<FString> MissingShaderMaterials;
    FString WorldName;
    TSet<uint32> SubmittedPasses[2];
    FWuWaLightingInputs Lighting;
    bool bLightingSubmitted = false;
    bool bToneMappingSubmitted = false;
    bool bReflectionLightingSubmitted = false;
    bool bReflectionFilterSubmitted = false;
    TRefCountPtr<IPooledRenderTarget> ColorGradingLUT;
    FScreenPassTexture ToneMapping_RenderThread(FRDGBuilder&, const FSceneView&, const FPostProcessMaterialInputs&);
    void AddTranslucencyPasses(FRDGBuilder&, const FViewInfo&, FRDGTextureRef SceneColor);
    bool IsSourcePrimitive(FPrimitiveComponentId Id) const;
    void PostOpaque_RenderThread(FPostOpaqueRenderParameters& Parameters);
};
