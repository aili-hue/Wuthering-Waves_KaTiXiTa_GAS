#include "WuWaPhysicsAuthoring.h"
#include "WuWaCharacterProfile.h"
#include "WuWaPhysicsProfile.h"
#include "WuWaPhysicsAnimInstance.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Engine/SkeletalMesh.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonSerializer.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimGraphNode_LinkedInputPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_Root.h"
#include "Factories/AnimBlueprintFactory.h"
#include "AssetToolsModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "AnimGraphNode_ApplyAdditive.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "K2Node_VariableGet.h"
#include "Readers/UEFAnimReader.h"

void UWuWaPhysicsAuthoring::SetSimulationEnabled(USkeletalMeshComponent* Component, bool Enabled)
{
    if (Component)
        if (UWuWaPhysicsAnimInstance* Instance = Cast<UWuWaPhysicsAnimInstance>(Component->GetPostProcessInstance()))
            Instance->bSimulationEnabled = Enabled;
}

void UWuWaPhysicsAuthoring::SetAnimationPaused(USkeletalMeshComponent* Component, bool Paused)
{
    if (Component) Component->bPauseAnims = Paused;
}

UAnimBlueprint* UWuWaPhysicsAuthoring::BuildPostProcessBlueprint(UWuWaCharacterProfile* Character, const FString& AssetPath)
{
    if (!Character || !Character->Mesh || !Character->Physics) return nullptr;
    const FString ObjectPath = AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn);
    if (!Blueprint)
    {
        UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
        Factory->ParentClass = UWuWaPhysicsAnimInstance::StaticClass();
        Factory->TargetSkeleton = Character->Mesh->GetSkeleton();
        Blueprint = Cast<UAnimBlueprint>(FAssetToolsModule::GetModule().Get().CreateAsset(
            FPackageName::GetLongPackageAssetName(AssetPath), FPackageName::GetLongPackagePath(AssetPath),
            UAnimBlueprint::StaticClass(), Factory));
    }
    if (!Blueprint || Blueprint->ParentClass != UWuWaPhysicsAnimInstance::StaticClass()
        || Blueprint->TargetSkeleton != Character->Mesh->GetSkeleton()) return nullptr;
    TArray<UEdGraph*> Graphs;
    Blueprint->GetAllGraphs(Graphs);
    UEdGraph** Found = Graphs.FindByPredicate([](const UEdGraph* Graph) { return Graph->GetFName() == TEXT("AnimGraph"); });
    if (!Found) return nullptr;
    UEdGraph* Graph = *Found;
    UAnimGraphNode_Root* Root = nullptr;
    const TArray<TObjectPtr<UEdGraphNode>> Existing = Graph->Nodes;
    for (UEdGraphNode* Node : Existing)
    {
        if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node)) Root = Candidate;
        else if (!Node->IsA<UAnimGraphNode_LinkedInputPose>() && !Node->IsA<UAnimGraphNode_LocalToComponentSpace>()
            && !Node->IsA<UAnimGraphNode_ComponentToLocalSpace>() && !Node->IsA<UAnimGraphNode_KawaiiPhysics>()
            && !Node->IsA<UAnimGraphNode_ApplyAdditive>() && !Node->IsA<UAnimGraphNode_SequenceEvaluator>()
            && !(Node->IsA<UK2Node_VariableGet>() &&
                (CastChecked<UK2Node_VariableGet>(Node)->GetVarName() == TEXT("RibbonSequence")
                 || CastChecked<UK2Node_VariableGet>(Node)->GetVarName() == TEXT("RibbonTime"))))
            return nullptr;
    }
    if (!Root) return nullptr;
    Blueprint->Modify();
    Graph->Modify();
    for (UEdGraphNode* Node : Existing)
        if (Node != Root) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
    auto AddNode = [Graph]<typename T>()
    {
        FGraphNodeCreator<T> Creator(*Graph);
        T* Node = Creator.CreateNode();
        Creator.Finalize();
        return Node;
    };
    auto Output = [](UEdGraphNode* Node) -> UEdGraphPin*
    {
        for (UEdGraphPin* Pin : Node->Pins) if (Pin->Direction == EGPD_Output) return Pin;
        return nullptr;
    };
    auto Connect = [Graph, &Output](UEdGraphNode* From, UEdGraphNode* To, FName Input)
    {
        UEdGraphPin* Pin = To->FindPin(Input, EGPD_Input);
        return Pin && Output(From) && Graph->GetSchema()->TryCreateConnection(Output(From), Pin);
    };
    UAnimGraphNode_LinkedInputPose* Input = AddNode.operator()<UAnimGraphNode_LinkedInputPose>();
    Input->NodePosX = -600;
    UAnimGraphNode_LocalToComponentSpace* ToComponent = AddNode.operator()<UAnimGraphNode_LocalToComponentSpace>();
    ToComponent->NodePosX = -350;
    UAnimGraphNode_SequenceEvaluator* Ribbon = AddNode.operator()<UAnimGraphNode_SequenceEvaluator>();
    if (Character->Physics->RibbonClips.IsEmpty()) return nullptr;
    Ribbon->SetAnimationAsset(Character->Physics->RibbonClips[0].Ribbon);
    for (FOptionalPinFromProperty& Option : Ribbon->ShowPinForProperties)
        if (Option.PropertyName == TEXT("Sequence") || Option.PropertyName == TEXT("ExplicitTime")) Option.bShowPin = true;
    Ribbon->ReconstructNode();
    auto Bind = [&](FName Property, FName PinName)
    {
        FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
        UK2Node_VariableGet* Getter = Creator.CreateNode();
        Getter->VariableReference.SetSelfMember(Property);
        Creator.Finalize();
        Getter->NodePosX = -1100;
        Getter->NodePosY = Property == TEXT("RibbonTime") ? 550 : 350;
        return Connect(Getter, Ribbon, PinName);
    };
    if (!Bind(TEXT("RibbonSequence"), TEXT("Sequence")) || !Bind(TEXT("RibbonTime"), TEXT("ExplicitTime"))) return nullptr;
    Ribbon->NodePosX = -800;
    Ribbon->NodePosY = 350;
    UAnimGraphNode_ApplyAdditive* Additive = AddNode.operator()<UAnimGraphNode_ApplyAdditive>();
    Additive->Node.Alpha = 1.0f;
    Additive->NodePosX = -550;
    if (!Connect(Input, Additive, TEXT("Base")) || !Connect(Ribbon, Additive, TEXT("Additive"))
        || !Connect(Additive, ToComponent, TEXT("LocalPose"))) return nullptr;
    UEdGraphNode* Previous = ToComponent;
    int32 Position = 0;
    for (const FWuWaPhysicsGroup& Group : Character->Physics->Groups)
    {
        // Allocate pins only after assigning source values. Otherwise shown
        // Alpha/AlphaCurveName pins retain C++ defaults and override the struct
        // during compilation, silently losing the original curve binding.
        FGraphNodeCreator<UAnimGraphNode_KawaiiPhysics> Creator(*Graph);
        UAnimGraphNode_KawaiiPhysics* Node = Creator.CreateNode();
        Node->Node = Group.Solver;
        Creator.Finalize();
        // Keep the original alpha mode and scale/bias. Companion curves reach
        // this node through the native ApplyAdditive input pose above.
        Node->NodeComment = Group.SourceNode.ToString();
        Node->NodePosX = Position;
        Position += 350;
        if (!Connect(Previous, Node, TEXT("ComponentPose"))) return nullptr;
        Previous = Node;
    }
    UAnimGraphNode_ComponentToLocalSpace* ToLocal = AddNode.operator()<UAnimGraphNode_ComponentToLocalSpace>();
    ToLocal->NodePosX = Position;
    Root->NodePosX = Position + 300;
    if (!Connect(Previous, ToLocal, TEXT("ComponentPose")) || !Connect(ToLocal, Root, TEXT("Result"))) return nullptr;
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status == BS_Error || !Blueprint->GeneratedClass) return nullptr;
    CastChecked<UWuWaPhysicsAnimInstance>(Blueprint->GeneratedClass->GetDefaultObject())->PhysicsProfile = Character->Physics;
    Blueprint->MarkPackageDirty();
    Character->Physics->AnimInstanceClass = Blueprint->GeneratedClass;
    Character->Physics->MarkPackageDirty();
    return Blueprint;
}

bool UWuWaPhysicsAuthoring::ConfigureProfile(UWuWaPhysicsProfile* Profile, const FString& SettingsJson)
{
    TSharedPtr<FJsonObject> Settings;
    if (!Profile || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SettingsJson), Settings) || !Settings) return false;
    TArray<FWuWaPhysicsGroup> Groups;
    for (const TSharedPtr<FJsonValue>& Value : Settings->GetArrayField(TEXT("Groups")))
    {
        FWuWaPhysicsGroup& Group = Groups.AddDefaulted_GetRef();
        if (!FJsonObjectConverter::JsonObjectToUStruct(Value->AsObject().ToSharedRef(), &Group)) return false;
    }
    if (Groups.IsEmpty()) return false;
    TArray<FWuWaPhysicsRibbonClip> Clips;
    if (Settings->HasField(TEXT("RibbonClips")))
        for (const TSharedPtr<FJsonValue>& Value : Settings->GetArrayField(TEXT("RibbonClips")))
        {
            const TSharedPtr<FJsonObject> Object = Value->AsObject();
            FWuWaPhysicsRibbonClip& Clip = Clips.AddDefaulted_GetRef();
            Clip.Body = LoadObject<UAnimSequence>(nullptr, *Object->GetStringField(TEXT("Body")));
            Clip.Ribbon = LoadObject<UAnimSequence>(nullptr, *Object->GetStringField(TEXT("Ribbon")));
            Clip.SourceSHA256 = Object->GetStringField(TEXT("SourceSHA256"));
            if (!Clip.Body || !Clip.Ribbon || !Clip.Ribbon->IsValidAdditive()) return false;
        }
    Profile->Modify();
    Profile->SourceSHA256 = Settings->GetStringField(TEXT("SourceSHA256"));
    Profile->Groups = MoveTemp(Groups);
    Profile->RibbonClips = MoveTemp(Clips);
    Profile->MarkPackageDirty();
    return true;
}

bool UWuWaPhysicsAuthoring::ConfigureRibbon(UAnimSequence* Animation, const FString& SourceJson)
{
    TSharedPtr<FJsonObject> Source;
    if (!Animation || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJson), Source) || !Source) return false;
    const TSharedPtr<FJsonObject> Properties = Source->GetObjectField(TEXT("Properties"));
    if (Properties->GetStringField(TEXT("AdditiveAnimType")) != TEXT("AAT_LocalSpaceBase")
        || Properties->GetStringField(TEXT("RefPoseType")) != TEXT("ABPT_RefPose")
        || FMath::Abs(Animation->GetPlayLength()-Properties->GetNumberField(TEXT("SequenceLength"))) > 0.0001) return false;
    Animation->Modify();
    // CUE4Parse ConvertAdditive reconstructs the absolute reference-based pose.
    // Marking that raw pose additive lets UE compute the original local delta;
    // applying it as an ordinary pose would incorrectly replace the body.
    Animation->AdditiveAnimType = AAT_LocalSpaceBase;
    Animation->RefPoseType = ABPT_RefPose;
    bool ForceRootLock = false;
    Properties->TryGetBoolField(TEXT("bForceRootLock"), ForceRootLock);
    Animation->bForceRootLock = ForceRootLock;
    IAnimationDataController& Controller = Animation->GetController();
    Controller.OpenBracket(FText::FromString(TEXT("Preserve original ribbon curves")), false);
    const TSharedPtr<FJsonObject>* Curves;
    if (Source->TryGetObjectField(TEXT("CompressedCurveData"), Curves))
        for (const TSharedPtr<FJsonValue>& Value : (*Curves)->GetArrayField(TEXT("FloatCurves")))
        {
            const TSharedPtr<FJsonObject> Curve = Value->AsObject();
            FRichCurve Exact;
            if (!FJsonObjectConverter::JsonObjectToUStruct(Curve->GetObjectField(TEXT("FloatCurve")).ToSharedRef(), &Exact))
            { Controller.CloseBracket(false); return false; }
            const FAnimationCurveIdentifier Identifier(FName(Curve->GetStringField(TEXT("CurveName"))), ERawCurveTrackTypes::RCT_Float);
            if (!Animation->GetDataModel()->FindFloatCurve(Identifier)) Controller.AddCurve(Identifier, AACF_DefaultCurve, false);
            Controller.SetCurveKeys(Identifier, Exact.GetConstRefOfKeys(), false);
        }
    Controller.CloseBracket(false);
    Animation->PostEditChange();
    Animation->MarkPackageDirty();
    return Animation->IsValidAdditive();
}

bool UWuWaPhysicsAuthoring::VerifyRibbonSource(UAnimSequence* Animation, const FString& Filename)
{
    UEFAnimReader Source(Filename);
    if (!Animation || !Source.Read() || !Animation->GetDataModel()
        || Animation->GetDataModel()->GetNumberOfKeys() != Source.NumFrames)
    {
        UE_LOG(LogTemp, Warning, TEXT("WUWA_RIBBON_SOURCE_LAYOUT %s keys=%d source=%d"), *GetNameSafe(Animation),
            Animation && Animation->GetDataModel() ? Animation->GetDataModel()->GetNumberOfKeys() : -1, Source.NumFrames);
        return false;
    }
    const IAnimationDataModel* Model = Animation->GetDataModel();
    const bool UsesEulerChannels = Animation->GetDataModelInterface().GetObject()->GetClass()->GetFName() == TEXT("AnimationSequencerDataModel");
    double SourceRotationError = 0.0;
    for (const FTrack& Track : Source.Tracks)
    {
        const FName Name(UTF8_TO_TCHAR(Track.TrackName.c_str()));
        if (!Model->IsValidBoneTrackName(Name)) return false;
        FVector3f Position = FVector3f::ZeroVector, Scale = FVector3f::OneVector;
        FQuat4f Rotation = FQuat4f::Identity;
        const FVector3f FirstEuler = Track.TrackRotKeys[0].QuatValue.GetNormalized().Euler();
        FIntVector VaryingEuler = FIntVector::ZeroValue;
        for (const FQuatKey& Key : Track.TrackRotKeys)
        {
            const FVector3f Euler = Key.QuatValue.GetNormalized().Euler();
            for (int32 Axis = 0; Axis < 3; ++Axis)
                VaryingEuler[Axis] |= !FMath::IsNearlyEqual(FirstEuler[Axis], Euler[Axis], UE_KINDA_SMALL_NUMBER);
        }
        int32 P = 0, R = 0, S = 0;
        for (int32 Frame = 0; Frame < Source.NumFrames; ++Frame)
        {
            if (P < Track.TrackPosKeys.Num() && Track.TrackPosKeys[P].Frame == Frame) Position = Track.TrackPosKeys[P++].VectorValue;
            if (R < Track.TrackRotKeys.Num() && Track.TrackRotKeys[R].Frame == Frame) Rotation = Track.TrackRotKeys[R++].QuatValue.GetNormalized();
            if (S < Track.TrackScaleKeys.Num() && Track.TrackScaleKeys[S].Frame == Frame) Scale = Track.TrackScaleKeys[S++].VectorValue;
            const FTransform Value = Model->GetBoneTrackTransform(Name, FFrameNumber(Frame));
            const FQuat UnitActual = Value.GetRotation().GetNormalized();
            const FQuat SourceRotation = FQuat(Rotation).GetNormalized();
            const double Difference = FMath::Min((UnitActual-SourceRotation).Size(), (UnitActual+SourceRotation).Size());
            SourceRotationError = FMath::Max(SourceRotationError, FMath::RadiansToDegrees(4.0*FMath::Asin(FMath::Min(Difference*0.5, 1.0))));
            FVector3f Euler = Rotation.Euler();
            for (int32 Axis = 0; Axis < 3; ++Axis) if (!VaryingEuler[Axis]) Euler[Axis] = FirstEuler[Axis];
            const FQuat NativeExpected = UsesEulerChannels ? FQuat::MakeFromEuler(FVector(Euler)).GetNormalized() : SourceRotation;
            if (!Value.GetTranslation().Equals(FVector(Position), 0.0001)
                || !UnitActual.Equals(NativeExpected, 0.0001)
                || !Value.GetScale3D().Equals(FVector(Scale), 0.0001))
            {
                UE_LOG(LogTemp, Warning, TEXT("WUWA_RIBBON_SOURCE_VALUE %s %s frame=%d position_error=%g rotation_error=%g scale_error=%g"),
                    *Animation->GetName(), *Name.ToString(), Frame, (Value.GetTranslation()-FVector(Position)).Size(),
                    Value.GetRotation().AngularDistance(FQuat(Rotation)), (Value.GetScale3D()-FVector(Scale)).Size());
                UE_LOG(LogTemp, Warning, TEXT("WUWA_RIBBON_SOURCE_QUAT actual=%s expected=%s"),
                    *Value.GetRotation().ToString(), *FQuat(Rotation).ToString());
                return false;
            }
        }
    }
    UE_LOG(LogTemp, Display, TEXT("WUWA_RIBBON_NATIVE_ROUNDTRIP %s source_rotation_max_degrees=%g"), *Animation->GetName(), SourceRotationError);
    return true;
}

FString UWuWaPhysicsAuthoring::AuditProfile(UWuWaCharacterProfile* Character)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> MissingBones, AnimationCurves;
    TArray<TSharedPtr<FJsonValue>> FallbackGroups;
    if (Character && Character->Mesh && Character->Physics)
    {
        const FReferenceSkeleton& Skeleton = Character->Mesh->GetRefSkeleton();
        TSet<FName> AlphaCurves;
        for (const FWuWaPhysicsGroup& Group : Character->Physics->Groups)
        {
            const FAnimNode_KawaiiPhysics& Solver = Group.Solver;
            auto CheckBone = [&](FName Name)
            {
                if (!Name.IsNone() && Skeleton.FindBoneIndex(Name) == INDEX_NONE)
                    MissingBones.Add(MakeShared<FJsonValueString>(Name.ToString()));
            };
            CheckBone(Solver.RootBone.BoneName);
            for (const auto& Root : Solver.AdditionalRootBones) CheckBone(Root.RootBone.BoneName);
            for (const auto& Bone : Solver.ExcludeBones) CheckBone(Bone.BoneName);
            for (const auto& Limit : Solver.SphericalLimits) CheckBone(Limit.DrivingBone.BoneName);
            for (const auto& Limit : Solver.CapsuleLimits) CheckBone(Limit.DrivingBone.BoneName);
            if (Solver.AlphaInputType == EAnimAlphaInputType::Curve) AlphaCurves.Add(Solver.AlphaCurveName);
        }
        for (const FWuWaShowcaseAnimation& Animation : Character->Animations)
            for (const UAnimSequence* Sequence : {Animation.Start.Get(), Animation.Loop.Get(), Animation.End.Get()})
            {
                if (!Sequence) continue;
                TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
                Entry->SetStringField(TEXT("animation"), Sequence->GetPathName());
                TArray<TSharedPtr<FJsonValue>> Curves;
                for (FName Name : AlphaCurves)
                {
                    TSharedRef<FJsonObject> Curve = MakeShared<FJsonObject>();
                    Curve->SetStringField(TEXT("name"), Name.ToString());
                    const FFloatCurve* Track = Sequence->GetDataModel()->FindFloatCurve(FAnimationCurveIdentifier(Name, ERawCurveTrackTypes::RCT_Float));
                    const FWuWaPhysicsRibbonClip* Clip = Character->Physics->RibbonClips.FindByPredicate(
                        [&](const FWuWaPhysicsRibbonClip& Item) { return Item.Body == Sequence; });
                    const FFloatCurve* Control = Clip ? Clip->Ribbon->GetDataModel()->FindFloatCurve(
                        FAnimationCurveIdentifier(Name, ERawCurveTrackTypes::RCT_Float)) : nullptr;
                    Curve->SetBoolField(TEXT("present"), Track != nullptr);
                    Curve->SetBoolField(TEXT("companion_control_present"), Control != nullptr);
                    Curve->SetStringField(TEXT("live_control_policy"), Control ? TEXT("original_companion_curve") : TEXT("native_missing_curve_zero"));
                    if (Control)
                    {
                        float Min, Max;
                        Control->FloatCurve.GetValueRange(Min, Max);
                        Curve->SetNumberField(TEXT("control_min"), Min);
                        Curve->SetNumberField(TEXT("control_max"), Max);
                    }
                    if (Track)
                    {
                        float Min, Max;
                        Track->FloatCurve.GetValueRange(Min, Max);
                        Curve->SetNumberField(TEXT("min"), Min);
                        Curve->SetNumberField(TEXT("max"), Max);
                    }
                    Curves.Add(MakeShared<FJsonValueObject>(Curve));
                }
                Entry->SetArrayField(TEXT("alpha_curves"), Curves);
                AnimationCurves.Add(MakeShared<FJsonValueObject>(Entry));
            }
        Report->SetNumberField(TEXT("group_count"), Character->Physics->Groups.Num());
    }
    Report->SetArrayField(TEXT("missing_bones"), MissingBones);
    Report->SetArrayField(TEXT("source_float_alpha_fallback_groups"), FallbackGroups);
    Report->SetArrayField(TEXT("animations"), AnimationCurves);
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
