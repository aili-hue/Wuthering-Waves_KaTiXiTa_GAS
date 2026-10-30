#include "WuWaShowcaseAccessoriesComponent.h"
#include "WuWaShowcaseCharacter.h"

#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Sound/SoundBase.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequenceBase.h"

UWuWaShowcaseAccessoriesComponent::UWuWaShowcaseAccessoriesComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UWuWaShowcaseAccessoriesComponent::OnRegister()
{
    Super::OnRegister();
    AWuWaShowcaseCharacter* Character = Cast<AWuWaShowcaseCharacter>(GetOwner());
    if (!Character || IsTemplate()) return;
    WeaponMesh = CreateWeaponMesh();
    VoiceAudio = NewObject<UAudioComponent>(Character, NAME_None, RF_Transient);
    VoiceAudio->ComponentTags.Add(TEXT("ShowcaseVoice"));
    VoiceAudio->bAutoActivate = false;
    VoiceAudio->bAutoDestroy = false;
    VoiceAudio->bAllowSpatialization = false;
    VoiceAudio->SetupAttachment(Character->GetSkeletalMeshComponent());
    VoiceAudio->RegisterComponent();
}

USkeletalMeshComponent* UWuWaShowcaseAccessoriesComponent::CreateWeaponMesh()
{
    AWuWaShowcaseCharacter* Character = CastChecked<AWuWaShowcaseCharacter>(GetOwner());
    USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Character, NAME_None, RF_Transient);
    Mesh->ComponentTags.Add(TEXT("ShowcaseWeapon"));
    Mesh->SetupAttachment(Character->GetSkeletalMeshComponent());
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Mesh->SetGenerateOverlapEvents(false);
    Mesh->SkinCacheUsage = {ESkinCacheUsage::Disabled};
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    // The showcase actor applies pause and native notify visibility after the
    // character pose. Tick child animations afterward to share that state in
    // the same frame, including on the first frame of a pause.
    Mesh->PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    Mesh->AddTickPrerequisiteActor(Character);
    // Native unlit effect sections use UE's base/transparent passes. The
    // original toon sections are composed by the existing shared renderer.
    Mesh->SetRenderInMainPass(true);
    Mesh->SetRenderInDepthPass(true);
    Mesh->SetVisibility(false);
    Mesh->RegisterComponent();
    return Mesh;
}

void UWuWaShowcaseAccessoriesComponent::OnUnregister()
{
    if (VoiceAudio) { VoiceAudio->Stop(); VoiceAudio->DestroyComponent(); VoiceAudio = nullptr; }
    if (WeaponMesh) { WeaponMesh->DestroyComponent(); WeaponMesh = nullptr; }
    for (USkeletalMeshComponent* Mesh : AdditionalWeaponMeshes) if (Mesh) Mesh->DestroyComponent();
    AdditionalWeaponMeshes.Reset();
    BoundProfile.Reset();
    ActiveWeaponNotifies.Reset();
    Super::OnUnregister();
}

bool UWuWaShowcaseAccessoriesComponent::PlayVoice(USoundBase* Sound)
{
    if (!Sound || !VoiceAudio || !GetWorld() || !GetWorld()->IsGameWorld()) return false;
    VoiceAudio->Stop();
    VoiceAudio->SetSound(Sound);
    VoiceAudio->Play();
    return true;
}

bool UWuWaShowcaseAccessoriesComponent::PlayNatureVoice()
{
    const AWuWaShowcaseCharacter* Character = Cast<AWuWaShowcaseCharacter>(GetOwner());
    return Character && Character->ActiveCharacter && PlayVoice(Character->ActiveCharacter->NatureVoice);
}

void UWuWaShowcaseAccessoriesComponent::UpdateAccessories()
{
    AWuWaShowcaseCharacter* Character = Cast<AWuWaShowcaseCharacter>(GetOwner());
    if (!Character || !WeaponMesh || !VoiceAudio) return;
    UWuWaCharacterProfile* Profile = Character->ActiveCharacter;
    const EWuWaShowcaseMode Mode = Character->GetShowcaseMode();
    if (BoundProfile.Get() != Profile)
    {
        VoiceAudio->Stop();
        for (USkeletalMeshComponent* Mesh : AdditionalWeaponMeshes) if (Mesh) Mesh->DestroyComponent();
        AdditionalWeaponMeshes.Reset();
        ActiveWeaponNotifies.Reset();
        TArray<FName> Sockets;
        if (Profile && Profile->Weapon)
        {
            Sockets.Add(Profile->WeaponSocket);
            Sockets.Append(Profile->AdditionalWeaponSockets);
        }
        bAttachmentValid = Profile && Profile->Weapon && !Profile->WeaponSocket.IsNone()
            && Character->GetSkeletalMeshComponent()->DoesSocketExist(Profile->WeaponSocket);
        WeaponMesh->EmptyOverrideMaterials();
        WeaponMesh->SetSkeletalMeshAsset(Profile ? Profile->Weapon.Get() : nullptr);
        for (int32 Index = 0; Index < Sockets.Num(); ++Index)
        {
            USkeletalMeshComponent* Mesh = Index == 0 ? WeaponMesh.Get() : CreateWeaponMesh();
            if (Index > 0) AdditionalWeaponMeshes.Add(Mesh);
            USkeletalMesh* Model = Index > 0 && Profile->AdditionalWeaponModels.IsValidIndex(Index - 1)
                ? Profile->AdditionalWeaponModels[Index - 1].Get() : Profile->Weapon.Get();
            Mesh->SetSkeletalMeshAsset(Model);
            if (Profile->WeaponIdleAnimation && Model &&
                Profile->WeaponIdleAnimation->GetSkeleton() == Model->GetSkeleton())
                Mesh->PlayAnimation(Profile->WeaponIdleAnimation, true);
#if WITH_EDITOR
            Mesh->SetUpdateAnimationInEditor(false);
#endif
            const bool bSocketValid = !Sockets[Index].IsNone()
                && Character->GetSkeletalMeshComponent()->DoesSocketExist(Sockets[Index]);
            bAttachmentValid &= bSocketValid;
            if (bSocketValid)
            {
                Mesh->AttachToComponent(Character->GetSkeletalMeshComponent(),
                    FAttachmentTransformRules::SnapToTargetNotIncludingScale, Sockets[Index]);
                Mesh->SetRelativeTransform(FTransform::Identity);
            }
        }
        BoundProfile = Profile;
        PreviousMode = Mode;
        if (Profile && !Profile->JoinTeamVoices.IsEmpty())
            PlayVoice(Profile->JoinTeamVoices[VoiceVariant++ % Profile->JoinTeamVoices.Num()]);
    }
    else if (Mode != PreviousMode)
    {
        if (Mode == EWuWaShowcaseMode::Nature) PlayNatureVoice();
        PreviousMode = Mode;
    }
    auto ShowWeapon = [&](USkeletalMeshComponent* Mesh, int32 Index)
    {
        const TSet<UWuWaWeaponNotifyState*>* Active = ActiveWeaponNotifies.Find(Index);
        if (Mesh)
        {
            Mesh->bPauseAnims = Character->GetSkeletalMeshComponent()->bPauseAnims;
            Mesh->SetVisibility(bAttachmentValid && Active && !Active->IsEmpty(), true);
        }
    };
    ShowWeapon(WeaponMesh, 0);
    for (int32 Index = 0; Index < AdditionalWeaponMeshes.Num(); ++Index) ShowWeapon(AdditionalWeaponMeshes[Index], Index + 1);
    VoiceAudio->SetPaused(Character->GetSkeletalMeshComponent()->bPauseAnims);
}

void UWuWaShowcaseAccessoriesComponent::SetWeaponNotify(UWuWaWeaponNotifyState* Notify, bool bActive)
{
    if (!Notify) return;
    UpdateAccessories();
    TSet<UWuWaWeaponNotifyState*>& States = ActiveWeaponNotifies.FindOrAdd(Notify->WeaponIndex);
    if (bActive) States.Add(Notify); else States.Remove(Notify);
    AWuWaShowcaseCharacter* Character = Cast<AWuWaShowcaseCharacter>(GetOwner());
    UWuWaCharacterProfile* Profile = Character ? Character->ActiveCharacter.Get() : nullptr;
    USkeletalMeshComponent* Mesh = Notify->WeaponIndex == 0 ? WeaponMesh.Get() :
        AdditionalWeaponMeshes.IsValidIndex(Notify->WeaponIndex - 1) ? AdditionalWeaponMeshes[Notify->WeaponIndex - 1].Get() : nullptr;
    if (!Profile || !Mesh) return;
    const FName DefaultSocket = Notify->WeaponIndex == 0 ? Profile->WeaponSocket :
        Profile->AdditionalWeaponSockets.IsValidIndex(Notify->WeaponIndex - 1) ? Profile->AdditionalWeaponSockets[Notify->WeaponIndex - 1] : NAME_None;
    const FName Socket = bActive && !Notify->HangSocket.IsNone() ? Notify->HangSocket : DefaultSocket;
    if (!Socket.IsNone() && Character->GetSkeletalMeshComponent()->DoesSocketExist(Socket))
        Mesh->AttachToComponent(Character->GetSkeletalMeshComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
    UpdateAccessories();
}

void UWuWaWeaponNotifyState::NotifyBegin(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
    float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyBegin(Mesh, Animation, TotalDuration, EventReference);
    if (const AWuWaShowcaseCharacter* Character = Mesh ? Cast<AWuWaShowcaseCharacter>(Mesh->GetOwner()) : nullptr)
        if (Character->Accessories) Character->Accessories->SetWeaponNotify(this, true);
}

void UWuWaWeaponNotifyState::NotifyEnd(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    Super::NotifyEnd(Mesh, Animation, EventReference);
    if (const AWuWaShowcaseCharacter* Character = Mesh ? Cast<AWuWaShowcaseCharacter>(Mesh->GetOwner()) : nullptr)
        if (Character->Accessories) Character->Accessories->SetWeaponNotify(this, false);
}
