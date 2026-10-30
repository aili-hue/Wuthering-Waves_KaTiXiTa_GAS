#pragma once

#include "CoreMinimal.h"
#include "Animation/SkeletalMeshActor.h"
#include "WuWaCharacterProfile.h"
#include "WuWaShowcaseCharacter.generated.h"

class ADirectionalLight;
class ACameraActor;
class UWuWaShowcaseAccessoriesComponent;
class UWuWaRenderingComponent;

/** Showcase selection/animation only. Rendering is discovered from mesh materials. */
UCLASS()
class WUWACHARACTERLAB_API AWuWaShowcaseCharacter : public ASkeletalMeshActor
{
    GENERATED_BODY()

public:
    AWuWaShowcaseCharacter();
    virtual void PostRegisterAllComponents() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Showcase|Lighting")
    TObjectPtr<ADirectionalLight> KeyLight;

    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Showcase|Camera")
    TObjectPtr<ACameraActor> ShowcaseCamera;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Showcase|Rendering")
    TObjectPtr<UWuWaRenderingComponent> RenderingSettings;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Showcase|Character")
    TArray<TObjectPtr<UWuWaCharacterProfile>> Characters;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Showcase|Character")
    TObjectPtr<UWuWaCharacterProfile> ActiveCharacter;

    UFUNCTION(BlueprintCallable, Category="Showcase|Character")
    bool SelectCharacter(UWuWaCharacterProfile* Character);

    UFUNCTION(BlueprintCallable, Category="Showcase|Animation")
    bool PlayShowcaseAnimation(EWuWaShowcaseMode Mode);

    UFUNCTION(BlueprintPure, Category="Showcase|Animation")
    EWuWaShowcaseMode GetShowcaseMode() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Showcase|Accessories")
    TObjectPtr<UWuWaShowcaseAccessoriesComponent> Accessories;

private:
    int32 PlayingAnimationIndex = INDEX_NONE;
    enum class EAnimationPhase : uint8 { Start, Loop, End };
    EAnimationPhase AnimationPhase = EAnimationPhase::Loop;
    EWuWaShowcaseMode PendingAnimation = EWuWaShowcaseMode::Nature;

};
