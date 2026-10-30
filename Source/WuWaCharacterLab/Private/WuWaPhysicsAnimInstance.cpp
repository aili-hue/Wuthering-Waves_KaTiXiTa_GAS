#include "WuWaPhysicsAnimInstance.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstanceProxy.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "WuWaPhysicsProfile.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"

void UWuWaPhysicsAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    UAnimSingleNodeInstance* Input = Mesh ? Mesh->GetSingleNodeInstance() : nullptr;
    if (!PhysicsProfile || !Input || !Input->GetCurrentAsset())
    {
        RibbonSequence = nullptr;
        RibbonTime = 0.0f;
        ControlSampleTime = 0.0f;
        ControlSequence.Reset();
        return;
    }
    const UAnimationAsset* Sequence = Input->GetCurrentAsset();
    const float Time = Input->GetCurrentTime();
    ControlSampleTime = Time;
    ControlSequence = Sequence ? Sequence->GetPathName() : FString();
    // Feed a standard sequence evaluator and ApplyAdditive node. The original
    // companion's curves then reach the unchanged skeletal-control alpha input.
    const FWuWaPhysicsRibbonClip* Clip = PhysicsProfile->RibbonClips.FindByPredicate(
        [Sequence](const FWuWaPhysicsRibbonClip& Candidate) { return Candidate.Body == Sequence; });
    RibbonSequence = Clip ? Clip->Ribbon : nullptr;
    RibbonTime = Time;
    if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
        for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
        {
            if (Property->Struct != FAnimNode_KawaiiPhysics::StaticStruct()) continue;
            FAnimNode_KawaiiPhysics& Solver = *Property->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(this);
            const FWuWaPhysicsGroup* Group = PhysicsProfile->Groups.FindByPredicate([&](const FWuWaPhysicsGroup& Item)
            { return Item.Solver.RootBone.BoneName == Solver.RootBone.BoneName && Item.Solver.AlphaCurveName == Solver.AlphaCurveName; });
            if (!Group) continue;
            Solver.AlphaScaleBias = Group->Solver.AlphaScaleBias;
            Solver.AlphaScaleBiasClamp = Group->Solver.AlphaScaleBiasClamp;
            Solver.bAlphaBoolEnabled = Group->Solver.bAlphaBoolEnabled;
            if (!bSimulationEnabled)
            {
                Solver.AlphaScaleBias.Scale = Solver.AlphaScaleBias.Bias = 0.0f;
                Solver.AlphaScaleBiasClamp.Scale = Solver.AlphaScaleBiasClamp.Bias = 0.0f;
                Solver.AlphaScaleBiasClamp.bMapRange = Solver.AlphaScaleBiasClamp.bClampResult = Solver.AlphaScaleBiasClamp.bInterpResult = false;
                Solver.bAlphaBoolEnabled = false;
            }
        }
}

void UWuWaPhysicsAnimInstance::SetDiagnosticForce(FVector Force)
{
    GetProxyOnGameThread<FAnimInstanceProxy>();
    if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
        for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
            if (Property->Struct == FAnimNode_KawaiiPhysics::StaticStruct())
                Property->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(this)->SimpleExternalForce = Force;
}

FString UWuWaPhysicsAnimInstance::GetSimulationState()
{
    // Wait for any outstanding animation task before inspecting live graph nodes.
    GetProxyOnGameThread<FAnimInstanceProxy>();
    TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
    State->SetBoolField(TEXT("saved_profile_bound"), PhysicsProfile != nullptr);
    State->SetNumberField(TEXT("saved_ribbon_clips"), PhysicsProfile ? PhysicsProfile->RibbonClips.Num() : 0);
    State->SetStringField(TEXT("ribbon_sequence"), GetPathNameSafe(RibbonSequence));
    State->SetNumberField(TEXT("ribbon_time"), RibbonTime);
    State->SetNumberField(TEXT("control_sample_time"), ControlSampleTime);
    State->SetStringField(TEXT("control_sequence"), ControlSequence);
    TArray<TSharedPtr<FJsonValue>> Groups;
    int32 InvalidBones = 0;
    int32 SimulatedBones = 0;
    int32 SimulatedParticles = 0;
    double MaxDisplacement = 0.0;
    if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(GetClass()))
        for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
        {
            if (Property->Struct != FAnimNode_KawaiiPhysics::StaticStruct()) continue;
            const FAnimNode_KawaiiPhysics& Solver = *Property->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(this);
            TSharedRef<FJsonObject> Group = MakeShared<FJsonObject>();
            Group->SetStringField(TEXT("node"), Property->GetName());
            Group->SetStringField(TEXT("root"), Solver.RootBone.BoneName.ToString());
            Group->SetNumberField(TEXT("alpha"), Solver.GetAlpha());
            Group->SetNumberField(TEXT("input_alpha"), Solver.Alpha);
            Group->SetStringField(TEXT("alpha_curve"), Solver.AlphaCurveName.ToString());
            Group->SetNumberField(TEXT("bones"), Solver.ModifyBones.Num());
            double GroupDisplacement = 0.0;
            double LengthError = 0.0, Penetration = 0.0;
            FString WorstCollisionBone;
            for (const FKawaiiPhysicsModifyBone& Bone : Solver.ModifyBones)
            {
                if (Bone.Location.ContainsNaN() || Bone.PoseLocation.ContainsNaN()) ++InvalidBones;
                else GroupDisplacement = FMath::Max(GroupDisplacement, FVector::Distance(Bone.Location, Bone.PoseLocation));
                if (!Bone.bDummy && Bone.HasParent() && !Bone.bSkipSimulate) ++SimulatedBones;
                if (Bone.HasParent() && !Bone.bSkipSimulate && Solver.GetAlpha() > KINDA_SMALL_NUMBER) ++SimulatedParticles;
                if (!Bone.HasParent() || Bone.bSkipSimulate || Solver.GetAlpha() <= KINDA_SMALL_NUMBER) continue;
                if (Solver.ModifyBones.IsValidIndex(Bone.ParentIndex))
                {
                    const auto& Parent = Solver.ModifyBones[Bone.ParentIndex];
                    // The solver restores current animated length, including scale.
                    const double TargetLength = FVector::Distance(Bone.PoseLocation, Parent.PoseLocation);
                    LengthError = FMath::Max(LengthError, FMath::Abs(FVector::Distance(Bone.Location, Parent.Location) - TargetLength));
                }
                auto Record = [&](double Depth)
                {
                    if (Depth > Penetration) { Penetration = Depth; WorstCollisionBone = Bone.BoneRef.BoneName.ToString(); }
                };
                for (const FSphericalLimit& Limit : Solver.SphericalLimits)
                {
                    if (!Limit.bEnable || Limit.Radius <= 0.0f) continue;
                    const double Distance = FVector::Distance(Bone.Location, Limit.Location);
                    Record(Limit.LimitType == ESphericalLimitType::Outer ? Limit.Radius + Bone.PhysicsSettings.Radius - Distance
                        : Distance - FMath::Max(Limit.Radius - Bone.PhysicsSettings.Radius, 0.0f));
                }
                for (const FCapsuleLimit& Limit : Solver.CapsuleLimits)
                {
                    if (!Limit.bEnable || Limit.Radius <= 0.0f || Limit.Length <= 0.0f) continue;
                    const FVector Half = Limit.Rotation.GetAxisZ() * Limit.Length * 0.5f;
                    Record(Limit.Radius + Bone.PhysicsSettings.Radius - FMath::PointDistToSegment(Bone.Location, Limit.Location-Half, Limit.Location+Half));
                }
            }
            MaxDisplacement = FMath::Max(MaxDisplacement, GroupDisplacement);
            Group->SetNumberField(TEXT("max_displacement_cm"), GroupDisplacement);
            Group->SetNumberField(TEXT("max_length_error_cm"), LengthError);
            Group->SetNumberField(TEXT("max_collider_penetration_cm"), Penetration);
            Group->SetStringField(TEXT("worst_collision_bone"), WorstCollisionBone);
            Groups.Add(MakeShared<FJsonValueObject>(Group));
        }
    State->SetNumberField(TEXT("group_count"), Groups.Num());
    State->SetNumberField(TEXT("simulated_bones"), SimulatedBones);
    State->SetNumberField(TEXT("simulated_particles"), SimulatedParticles);
    State->SetNumberField(TEXT("invalid_bones"), InvalidBones);
    State->SetNumberField(TEXT("max_displacement_cm"), MaxDisplacement);
    State->SetArrayField(TEXT("groups"), Groups);
    FString Json;
    FJsonSerializer::Serialize(State, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
