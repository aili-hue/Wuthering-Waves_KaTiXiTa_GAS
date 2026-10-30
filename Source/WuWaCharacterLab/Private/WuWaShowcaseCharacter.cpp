#include "WuWaShowcaseCharacter.h"
#include "WuWaShowcaseAccessoriesComponent.h"
#include "WuWaPhysicsAnimInstance.h"
#include "WuWaPhysicsProfile.h"
#include "WuWaRenderingComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"

AWuWaShowcaseCharacter::AWuWaShowcaseCharacter()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;
    Accessories = CreateDefaultSubobject<UWuWaShowcaseAccessoriesComponent>(TEXT("ShowcaseAccessories"));
    RenderingSettings=CreateDefaultSubobject<UWuWaRenderingComponent>(TEXT("CharacterRendering"));
}

void AWuWaShowcaseCharacter::PostRegisterAllComponents()
{
    Super::PostRegisterAllComponents();
#if WITH_EDITOR
    // Clear editor playback flags inherited from previously saved levels.
    // PIE animation continues through the native skeletal component tick.
    TInlineComponentArray<USkeletalMeshComponent*> Meshes(this);
    for (USkeletalMeshComponent* Mesh : Meshes) Mesh->SetUpdateAnimationInEditor(false);
#endif
}

bool AWuWaShowcaseCharacter::SelectCharacter(UWuWaCharacterProfile* Character)
{
    if (!Character || !Character->Mesh || Character->Surfaces.Num() != Character->Mesh->GetMaterials().Num()) return false;
    TSet<FName> Slots;
    for (const FWuWaCharacterSurface& Surface : Character->Surfaces)
    {
        const int32 Index = Character->Mesh->GetMaterials().IndexOfByPredicate(
            [&Surface](const FSkeletalMaterial& Material) { return Material.MaterialSlotName == Surface.Slot; });
        if (Index == INDEX_NONE || Slots.Contains(Surface.Slot)
            || !Character->Mesh->GetMaterials()[Index].MaterialInterface) return false;
        Slots.Add(Surface.Slot);
    }
    const FWuWaShowcaseAnimation* Nature = Character->Animations.FindByPredicate(
        [](const FWuWaShowcaseAnimation& Animation) { return Animation.Mode == EWuWaShowcaseMode::Nature; });
    if (!Nature || !Nature->Loop) return false;
    for (const FWuWaShowcaseAnimation& Animation : Character->Animations)
        for (UAnimSequence* Sequence : {Animation.Start.Get(), Animation.Loop.Get(), Animation.End.Get()})
            if (Sequence && Sequence->GetSkeleton() != Character->Mesh->GetSkeleton()) return false;
    USkeletalMeshComponent* Mesh = GetSkeletalMeshComponent();
    PlayingAnimationIndex = INDEX_NONE;
    if (UAnimInstance* Instance = Mesh->GetAnimInstance()) Instance->EndNotifyStates();
    Mesh->Stop();
    Mesh->bPauseAnims = false;
    Mesh->ClearMorphTargets();
    Mesh->EmptyOverrideMaterials();
    Mesh->SetOverridePostProcessAnimBP(Character->Physics ? Character->Physics->AnimInstanceClass : nullptr, false);
    Mesh->SetSkeletalMesh(Character->Mesh);
    // The recovered mesh pass owns visibility; native main-pass visibility must
    // not stop pose updates after a character switch.
    Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Mesh->SetComponentTickEnabled(true);
#if WITH_EDITOR
    Mesh->SetUpdateAnimationInEditor(false);
#endif
    ActiveCharacter = Character;
    // Rebuild secondary motion from the selected profile before starting its animation.
    Mesh->SetOverridePostProcessAnimBP(Character->Physics ? Character->Physics->AnimInstanceClass : nullptr);
    Mesh->ResetAnimInstanceDynamics(ETeleportType::ResetPhysics);
    RenderingSettings->HeadBone=Character->HeadBone;
    return PlayShowcaseAnimation(EWuWaShowcaseMode::Nature);
}

bool AWuWaShowcaseCharacter::PlayShowcaseAnimation(EWuWaShowcaseMode Mode)
{
    if (!ActiveCharacter) return false;
    const int32 AnimationIndex = ActiveCharacter->Animations.IndexOfByPredicate(
        [Mode](const FWuWaShowcaseAnimation& Entry) { return Entry.Mode == Mode; });
    if (!ActiveCharacter->Animations.IsValidIndex(AnimationIndex)) return false;
    const FWuWaShowcaseAnimation* Animation = &ActiveCharacter->Animations[AnimationIndex];
    if (!Animation->Loop) return false;
    if (ActiveCharacter->Animations.IsValidIndex(PlayingAnimationIndex))
    {
        const FWuWaShowcaseAnimation& PlayingAnimation = ActiveCharacter->Animations[PlayingAnimationIndex];
        if (AnimationPhase == EAnimationPhase::End) { PendingAnimation = Mode; return true; }
        if (AnimationIndex == PlayingAnimationIndex) return true;
        if (PlayingAnimation.End)
        {
            PendingAnimation = Mode;
            AnimationPhase = EAnimationPhase::End;
            if (UAnimInstance* Instance = GetSkeletalMeshComponent()->GetAnimInstance()) Instance->EndNotifyStates();
            GetSkeletalMeshComponent()->PlayAnimation(PlayingAnimation.End, false);
            return true;
        }
    }
    PlayingAnimationIndex = AnimationIndex;
    AnimationPhase = Animation->Start ? EAnimationPhase::Start : EAnimationPhase::Loop;
    // Single-node asset replacement does not end the outgoing notify states.
    if (UAnimInstance* Instance = GetSkeletalMeshComponent()->GetAnimInstance()) Instance->EndNotifyStates();
    GetSkeletalMeshComponent()->PlayAnimation(Animation->Start ? Animation->Start.Get() : Animation->Loop.Get(),
        AnimationPhase == EAnimationPhase::Loop);
    return true;
}

EWuWaShowcaseMode AWuWaShowcaseCharacter::GetShowcaseMode() const
{
    return ActiveCharacter && ActiveCharacter->Animations.IsValidIndex(PlayingAnimationIndex)
        ? ActiveCharacter->Animations[PlayingAnimationIndex].Mode : EWuWaShowcaseMode::Nature;
}

void AWuWaShowcaseCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    USkeletalMeshComponent* Mesh = GetSkeletalMeshComponent();
    if (ActiveCharacter && ActiveCharacter->Physics && ActiveCharacter->Physics->AnimInstanceClass
        && Mesh->GetPostProcessAnimBPClassToBeUsed() != ActiveCharacter->Physics->AnimInstanceClass)
    {
        Mesh->SetOverridePostProcessAnimBP(ActiveCharacter->Physics->AnimInstanceClass);
    }
    if (ActiveCharacter && ActiveCharacter->Animations.IsValidIndex(PlayingAnimationIndex)
        && AnimationPhase != EAnimationPhase::Loop && !Mesh->bPauseAnims)
    {
        const FWuWaShowcaseAnimation& PlayingAnimation = ActiveCharacter->Animations[PlayingAnimationIndex];
        UAnimSingleNodeInstance* Instance = Mesh->GetSingleNodeInstance();
        UAnimSequence* Sequence = AnimationPhase == EAnimationPhase::Start ? PlayingAnimation.Start.Get() : PlayingAnimation.End.Get();
        if (Instance && Sequence && Instance->GetCurrentTime() >= Sequence->GetPlayLength() - KINDA_SMALL_NUMBER)
        {
            if (AnimationPhase == EAnimationPhase::Start)
            {
                AnimationPhase = EAnimationPhase::Loop;
                Instance->EndNotifyStates();
                Mesh->PlayAnimation(PlayingAnimation.Loop, true);
            }
            else
            {
                PlayingAnimationIndex = INDEX_NONE;
                PlayShowcaseAnimation(PendingAnimation);
            }
        }
    }
    if (Accessories) Accessories->UpdateAccessories();
}
