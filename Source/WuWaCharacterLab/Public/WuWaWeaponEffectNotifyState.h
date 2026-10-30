#pragma once

#include "AnimNotifyState_TimedNiagaraEffect.h"
#include "Components/ActorComponent.h"
#include "Curves/CurveFloat.h"
#include "WuWaWeaponEffectNotifyState.generated.h"

class UNiagaraComponent;
class USkeletalMesh;

/** Native timed Niagara attachment with the source effect's repeating phase and stop interval. */
UCLASS(meta=(DisplayName="Source Weapon Niagara Effect"))
class WUWACHARACTERLAB_API UWuWaWeaponEffectNotifyState : public UAnimNotifyState_TimedNiagaraEffect
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Source Effect") FString SourceNotify;
    UPROPERTY(EditAnywhere, Category="Source Effect") float StartTime = 0.0f;
    UPROPERTY(EditAnywhere, Category="Source Effect") float LoopTime = 0.0f;
    UPROPERTY(EditAnywhere, Category="Source Effect") float EndTime = 0.0f;
    UPROPERTY(EditAnywhere, Category="Source Effect") bool bDeactivateOnStop = true;
    UPROPERTY(EditAnywhere, Category="Source Effect") TMap<FName, float> FloatConstants;
    UPROPERTY(EditAnywhere, Category="Source Effect") TMap<FName, FRuntimeFloatCurve> FloatCurves;

    virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;
protected:
    virtual UFXSystemComponent* SpawnEffect(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation) const override;
};

/** Per-instance state: notify objects are shared by every character using the animation. */
UCLASS(Transient)
class WUWACHARACTERLAB_API UWuWaWeaponEffectPhaseComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UWuWaWeaponEffectPhaseComponent();
    void Initialize(const UWuWaWeaponEffectNotifyState* Notify, USkeletalMeshComponent* Mesh,
        UAnimSequenceBase* Animation, UNiagaraComponent* Niagara);
    void RequestStop() { bStopping = true; }
    bool Controls(const UNiagaraComponent* Niagara) const { return Effect.Get() == Niagara; }
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void OnUnregister() override;
private:
    friend class UCharacterInputValidation;
    void ApplyParameters() const;
    void Finish();
    UPROPERTY(Transient) TObjectPtr<const UWuWaWeaponEffectNotifyState> Source;
    UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> Attachment;
    UPROPERTY(Transient) TWeakObjectPtr<USkeletalMesh> BoundMesh;
    UPROPERTY(Transient) TWeakObjectPtr<UAnimSequenceBase> BoundAnimation;
    UPROPERTY(Transient) TWeakObjectPtr<UNiagaraComponent> Effect;
    double PassTime = 0.0;
    bool bStopping = false;
    bool bPreStopped = false;
    bool bTrackSingleNodeAnimation = false;
};
