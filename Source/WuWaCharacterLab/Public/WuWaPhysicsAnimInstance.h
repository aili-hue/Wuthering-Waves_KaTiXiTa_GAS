#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "WuWaPhysicsAnimInstance.generated.h"

class UWuWaPhysicsProfile;
class UAnimSequence;

/** Secondary motion consumes the existing single-node animation as a post-process. */
UCLASS(Transient, BlueprintType, Blueprintable)
class WUWACHARACTERLAB_API UWuWaPhysicsAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    /** Assigned on the generated post-process class, independent of the actor. */
    UPROPERTY()
    TObjectPtr<UWuWaPhysicsProfile> PhysicsProfile;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Showcase|Physics")
    TObjectPtr<UAnimSequence> RibbonSequence;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Showcase|Physics")
    float RibbonTime = 0.0f;

    /** Diagnostic switch keeps the original ribbon layer active. */
    bool bSimulationEnabled = true;

    /** Reads the completed proxy on demand; no per-frame pose readback is required. */
    UFUNCTION(BlueprintCallable, Category="Showcase|Physics")
    FString GetSimulationState();

    /** Temporary validation input; zero restores the authored source force. */
    UFUNCTION(BlueprintCallable, Category="Showcase|Physics")
    void SetDiagnosticForce(FVector Force);

private:
    float ControlSampleTime = 0.0f;
    FString ControlSequence;
};
