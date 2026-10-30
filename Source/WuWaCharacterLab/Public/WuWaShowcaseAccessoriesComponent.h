#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "WuWaCharacterProfile.h"
#include "WuWaShowcaseAccessoriesComponent.generated.h"

class UAudioComponent;
class USkeletalMeshComponent;
class UWuWaWeaponNotifyState;

/** Native weapon attachment and Chinese voice follow the selected saved profile. */
UCLASS(ClassGroup=Showcase, meta=(BlueprintSpawnableComponent))
class WUWACHARACTERLAB_API UWuWaShowcaseAccessoriesComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UWuWaShowcaseAccessoriesComponent();
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    void UpdateAccessories();
    void SetWeaponNotify(UWuWaWeaponNotifyState* Notify, bool bActive);

    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category="Weapon")
    TObjectPtr<USkeletalMeshComponent> WeaponMesh;

    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category="Weapon")
    TArray<TObjectPtr<USkeletalMeshComponent>> AdditionalWeaponMeshes;

    UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category="Voice")
    TObjectPtr<UAudioComponent> VoiceAudio;

    UFUNCTION(BlueprintCallable, Category="Voice")
    bool PlayNatureVoice();

private:
    bool PlayVoice(USoundBase* Sound);
    USkeletalMeshComponent* CreateWeaponMesh();
    TWeakObjectPtr<UWuWaCharacterProfile> BoundProfile;
    EWuWaShowcaseMode PreviousMode = EWuWaShowcaseMode::Nature;
    int32 VoiceVariant = 0;
    bool bAttachmentValid = false;
    TMap<int32, TSet<UWuWaWeaponNotifyState*>> ActiveWeaponNotifies;
};

/** Original animation intervals drive native component visibility. */
UCLASS(meta=(DisplayName="Show Showcase Weapon"))
class WUWACHARACTERLAB_API UWuWaWeaponNotifyState : public UAnimNotifyState
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weapon")
    int32 WeaponIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weapon")
    FName HangSocket;

    virtual void NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
        float TotalDuration, const FAnimNotifyEventReference& EventReference) override;
    virtual void NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
        const FAnimNotifyEventReference& EventReference) override;
};
