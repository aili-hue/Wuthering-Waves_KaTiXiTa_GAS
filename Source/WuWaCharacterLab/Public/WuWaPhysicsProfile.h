#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AnimNode_KawaiiPhysics.h"
#include "WuWaPhysicsProfile.generated.h"

class UWuWaPhysicsAnimInstance;
class UAnimSequence;

/** Original companion clip, synchronized with its corresponding body clip. */
USTRUCT()
struct FWuWaPhysicsRibbonClip
{
    GENERATED_BODY()

    UPROPERTY()
    TObjectPtr<UAnimSequence> Body;

    UPROPERTY()
    TObjectPtr<UAnimSequence> Ribbon;

    UPROPERTY()
    FString SourceSHA256;
};

/** Original PC group order and parameters, adapted to the public solver. */
USTRUCT(BlueprintType)
struct FWuWaPhysicsGroup
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Physics")
    FName SourceNode;

    UPROPERTY(EditAnywhere, Category="Physics")
    FAnimNode_KawaiiPhysics Solver;

    // Retain unsupported original collision metadata; this is not a planar solver.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
    bool bOriginalPlanarShape = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
    float OriginalHalfWidth = 0.0f;
};

UCLASS(BlueprintType)
class WUWACHARACTERLAB_API UWuWaPhysicsProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Physics")
    TSubclassOf<UWuWaPhysicsAnimInstance> AnimInstanceClass;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Source")
    FString SourceSHA256;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Physics")
    TArray<FWuWaPhysicsGroup> Groups;

    UPROPERTY(VisibleAnywhere, Category="Physics")
    TArray<FWuWaPhysicsRibbonClip> RibbonClips;
};
