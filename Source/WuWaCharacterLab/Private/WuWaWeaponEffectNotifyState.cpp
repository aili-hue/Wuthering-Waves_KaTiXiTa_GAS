#include "WuWaWeaponEffectNotifyState.h"

#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequenceBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

UFXSystemComponent* UWuWaWeaponEffectNotifyState::SpawnEffect(USkeletalMeshComponent* MeshComp,
    UAnimSequenceBase* Animation) const
{
    if (!MeshComp || !MeshComp->GetOwner() || !ValidateParameters(MeshComp)) return nullptr;
    FFXSystemSpawnParameters Params;
    Params.SystemTemplate = Template;
    Params.AttachToComponent = MeshComp;
    Params.AttachPointName = SocketName;
    Params.Location = LocationOffset;
    Params.Rotation = RotationOffset;
    Params.Scale = Scale;
    Params.LocationType = EAttachLocation::KeepRelativeOffset;
    Params.bAutoDestroy = false;
    Params.bAutoActivate = false;
    UNiagaraComponent* Niagara = UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(Params);
    if (!Niagara) return nullptr;
    // Notifies can spawn after this frame's weapon visibility propagation.
    Niagara->SetVisibility(MeshComp->IsVisible(), true);
    UWuWaWeaponEffectPhaseComponent* Phase = NewObject<UWuWaWeaponEffectPhaseComponent>(MeshComp->GetOwner());
    Phase->Initialize(this, MeshComp, Animation, Niagara);
    Phase->RegisterComponent();
    Niagara->AddTickPrerequisiteComponent(Phase);
    if (bApplyRateScaleAsTimeDilation && Animation) Niagara->SetCustomTimeDilation(Animation->RateScale);
    Niagara->Activate(true);
    return Niagara;
}

void UWuWaWeaponEffectNotifyState::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
    const FAnimNotifyEventReference& EventReference)
{
    if (bDestroyAtEnd)
    {
        Super::NotifyEnd(MeshComp, Animation, EventReference);
        return;
    }
    if (UNiagaraComponent* Niagara = Cast<UNiagaraComponent>(GetSpawnedEffect(MeshComp)))
    {
        // A new animation loop can spawn this notify again while the previous instance finishes.
        Niagara->ComponentTags.Remove(GetSpawnedComponentTag());
        TInlineComponentArray<UWuWaWeaponEffectPhaseComponent*> Phases(MeshComp->GetOwner());
        for (UWuWaWeaponEffectPhaseComponent* Phase : Phases)
        {
            if (Phase->Controls(Niagara))
            {
                Phase->RequestStop();
                UAnimNotifyState::NotifyEnd(MeshComp, Animation, EventReference);
                return;
            }
        }
        Niagara->DestroyComponent();
    }
    UAnimNotifyState::NotifyEnd(MeshComp, Animation, EventReference);
}

UWuWaWeaponEffectPhaseComponent::UWuWaWeaponEffectPhaseComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    bTickInEditor = true;
}

void UWuWaWeaponEffectPhaseComponent::Initialize(const UWuWaWeaponEffectNotifyState* Notify,
    USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation, UNiagaraComponent* Niagara)
{
    Source = Notify;
    Attachment = Mesh;
    BoundMesh = Mesh->GetSkeletalMeshAsset();
    BoundAnimation = Animation;
    bTrackSingleNodeAnimation = Mesh->GetSingleNodeInstance() != nullptr;
    Effect = Niagara;
    AddTickPrerequisiteComponent(Mesh);
    ApplyParameters();
}

void UWuWaWeaponEffectPhaseComponent::ApplyParameters() const
{
    if (UNiagaraComponent* Niagara = Effect.Get())
    {
        for (const auto& Parameter : Source->FloatConstants)
            Niagara->SetVariableFloat(Parameter.Key, Parameter.Value);
        for (const auto& Parameter : Source->FloatCurves)
            Niagara->SetVariableFloat(Parameter.Key, Parameter.Value.GetRichCurveConst()->Eval(PassTime));
    }
}

void UWuWaWeaponEffectPhaseComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    USkeletalMeshComponent* Mesh = Attachment.Get();
    UNiagaraComponent* Niagara = Effect.Get();
    UAnimSingleNodeInstance* Animation = Mesh ? Mesh->GetSingleNodeInstance() : nullptr;
    if (!Source || !Mesh || !Mesh->IsRegistered() || !Niagara || !Niagara->IsRegistered()
        || Mesh->GetSkeletalMeshAsset() != BoundMesh.Get()
        || (bTrackSingleNodeAnimation && (!Animation || Animation->GetCurrentAsset() != BoundAnimation.Get())))
    {
        Finish();
        return;
    }
    Niagara->SetPaused(Mesh->bPauseAnims);
    if (Mesh->bPauseAnims) return;
    // Lifecycle curves must advance on the same clock as the native simulation.
    PassTime += DeltaTime * Niagara->GetCustomTimeDilation();
    const double LoopEnd = static_cast<double>(Source->StartTime) + Source->LoopTime;
    // EffectLifeTime.SeekTo wraps only the middle interval, preserving its initial interval.
    if (!bStopping && (Source->StartTime < 0.0f || Source->LoopTime > 0.0f) && PassTime >= LoopEnd)
    {
        if (Source->LoopTime <= 0.001f) PassTime = Source->StartTime;
        else if (PassTime >= LoopEnd + Source->LoopTime)
            PassTime = Source->StartTime + FMath::Fmod(PassTime - Source->StartTime, static_cast<double>(Source->LoopTime));
        else PassTime -= Source->LoopTime;
    }
    if ((bStopping || Source->LoopTime <= 0.0f) && PassTime > LoopEnd && !bPreStopped)
    {
        bPreStopped = true;
        if (Source->bDeactivateOnStop) Niagara->Deactivate();
    }
    ApplyParameters();
    if ((bStopping || Source->LoopTime <= 0.0f) && PassTime >= LoopEnd + Source->EndTime) Finish();
}

void UWuWaWeaponEffectPhaseComponent::Finish()
{
    if (UNiagaraComponent* Niagara = Effect.Get()) Niagara->DestroyComponent();
    Effect.Reset();
    DestroyComponent();
}

void UWuWaWeaponEffectPhaseComponent::OnUnregister()
{
    if (UNiagaraComponent* Niagara = Effect.Get()) Niagara->DestroyComponent();
    Effect.Reset();
    Super::OnUnregister();
}
