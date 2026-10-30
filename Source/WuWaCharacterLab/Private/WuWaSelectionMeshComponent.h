#pragma once

#include "Components/SkeletalMeshComponent.h"
#include "WuWaSelectionMeshComponent.generated.h"

/** Transient editor picking geometry; never participates in scene shading. */
UCLASS(Transient, NotBlueprintable)
class UWuWaSelectionMeshComponent : public USkeletalMeshComponent
{
    GENERATED_BODY()
public:
    TWeakObjectPtr<USkeletalMeshComponent> SourceMesh;
    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
};
