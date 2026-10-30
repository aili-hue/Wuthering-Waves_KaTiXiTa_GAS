#include "WuWaSelectionMeshComponent.h"

#if WITH_EDITOR
#include "SkeletalMeshSceneProxy.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneView.h"

namespace
{
class FWuWaSelectionMeshProxy final : public FSkeletalMeshSceneProxy
{
public:
    FWuWaSelectionMeshProxy(const UWuWaSelectionMeshComponent* Component, FSkeletalMeshRenderData* Data)
        : FSkeletalMeshSceneProxy(Component, Data), SourceMesh(Component->SourceMesh) {}

    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
    {
        auto Result=FSkeletalMeshSceneProxy::GetViewRelevance(View);
        Result.bDrawRelevance &= View->Family->EngineShowFlags.HitProxies;
        Result.bRenderInMainPass=true;
        Result.bShadowRelevance=false;
        Result.bRenderInDepthPass=false;
        Result.bRenderCustomDepth=false;
        Result.bVelocityRelevance=false;
        return Result;
    }

    virtual HHitProxy* CreateHitProxies(UPrimitiveComponent* Component, TArray<TRefCountPtr<HHitProxy>>& OutHitProxies) override
    {
        // Native HActor proxies must refer to the user's component, including
        // component selection and material-section selection in Blueprints.
        auto* Source=SourceMesh.Get();
        return Source ? FSkeletalMeshSceneProxy::CreateHitProxies(Source,OutHitProxies) : nullptr;
    }
private:
    TWeakObjectPtr<USkeletalMeshComponent> SourceMesh;
};
}
#endif

FPrimitiveSceneProxy* UWuWaSelectionMeshComponent::CreateSceneProxy()
{
#if WITH_EDITOR
    auto* Data=GetSkeletalMeshRenderData();
    if (Data && Data->LODRenderData.IsValidIndex(GetPredictedLODLevel()) && !bHideSkin && GetMeshObject())
        return new FWuWaSelectionMeshProxy(this,Data);
#endif
    return nullptr;
}
