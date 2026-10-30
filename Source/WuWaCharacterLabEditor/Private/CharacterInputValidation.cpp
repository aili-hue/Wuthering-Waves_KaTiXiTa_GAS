#include "CharacterInputValidation.h"
#include "WuWaShowcaseAccessoriesComponent.h"
#include "WuWaWeaponEffectNotifyState.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Editor.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimComposite.h"
#include "Factories/AnimCompositeFactory.h"
#include "Animation/AnimInstance.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimCurveTypes.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/MorphTarget.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Readers/UEFAnimReader.h"
#include "Readers/UEFModelReader.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonSerializer.h"
#include "SkeletalMeshAttributes.h"
#include "SkinnedAssetCompiler.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "TextureCompiler.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "VertexFactory.h"
#include "MaterialEditingLibrary.h"
#include "AssetRegistry/AssetData.h"
#include "ObjectTools.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "CanvasTypes.h"
#include "Kismet/GameplayStatics.h"
#include "LegacyScreenPercentageDriver.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "TextureResource.h"
#include "AssetCompilingManager.h"
#include "ContentStreaming.h"

bool UCharacterInputValidation::RenderEditorScene(AActor* FocusActor, UCameraComponent* Camera,
    UTextureRenderTarget2D* Target, int32 Frames, float SceneTimeSeconds)
{
    UWorld* World = FocusActor ? FocusActor->GetWorld() : nullptr;
    if (!IsRunningCommandlet() || !World || World->IsGameWorld() || !World->Scene || !Camera || Camera->GetWorld() != World
        || !Target || Frames < 1 || Frames > 8 || !FMath::IsFinite(SceneTimeSeconds)) return false;
    FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
    if (!Resource) return false;
    // Commandlets do not run the editor's idle frames while newly loaded
    // materials compile. Complete native asset work before judging an image.
    FAssetCompilingManager::Get().FinishAllCompilation();
    for (int32 Frame = 0; Frame < Frames; ++Frame)
    {
        if (USkeletalMeshComponent* Mesh = FocusActor->FindComponentByClass<USkeletalMeshComponent>())
        {
            const bool bPaused = Mesh->bPauseAnims;
            Mesh->bPauseAnims = false;
            Mesh->TickAnimation(0.f, false);
            Mesh->RefreshBoneTransforms();
            Mesh->bPauseAnims = bPaused;
        }
        FocusActor->Tick(0.f);
        if (Frame == 0)
        {
            FAssetCompilingManager::Get().FinishAllCompilation();
            IStreamingManager::Get().AddViewInformation(Camera->GetComponentLocation(), Target->SizeX, Target->SizeX);
            IStreamingManager::Get().StreamAllResources(10.f);
        }
        World->SendAllEndOfFrameUpdates();
        TArray<USceneCaptureComponent2D*> Captures;
        FocusActor->GetComponents(Captures);
        for (USceneCaptureComponent2D* Capture : Captures)
            if (Capture->TextureTarget) Capture->CaptureScene();
        FlushRenderingCommands();

        FMinimalViewInfo CameraView;
        Camera->GetCameraView(0.f, CameraView);
        CameraView.AspectRatio = float(Target->SizeX) / Target->SizeY;
        FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
        UGameplayStatics::CalculateViewProjectionMatricesFromMinimalView(CameraView, TOptional<FMatrix>(),
            ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);
        FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(Resource, World->Scene, FEngineShowFlags(ESFIM_Game))
            .SetTime(FGameTime::CreateUndilated(SceneTimeSeconds, 0.f)).SetRealtimeUpdate(true));
        Family.EngineShowFlags.ScreenPercentage = false;
        Family.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(Family, 1.f));
        Family.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(World->Scene));
        FSceneViewInitOptions Options;
        Options.SetViewRectangle(FIntRect(0, 0, Target->SizeX, Target->SizeY));
        Options.ViewFamily = &Family;
        Options.ViewOrigin = CameraView.Location;
        Options.ViewRotationMatrix = ViewMatrix.RemoveTranslation();
        Options.ProjectionMatrix = ProjectionMatrix;
        Options.ViewActor = Camera->GetOwner();
        FSceneView* View = new FSceneView(Options);
        Family.Views.Add(View);
        View->StartFinalPostprocessSettings(CameraView.Location);
        View->OverridePostProcessSettings(CameraView.PostProcessSettings, CameraView.PostProcessBlendWeight);
        View->EndFinalPostprocessSettings(Options);
        for (const FSceneViewExtensionRef& Extension : Family.ViewExtensions)
        {
            Extension->SetupViewFamily(Family);
            Extension->SetupView(Family, *View);
        }
        FCanvas Canvas(Resource, nullptr, World, World->GetFeatureLevel());
        GetRendererModule().BeginRenderingViewFamily(&Canvas, &Family);
        FlushRenderingCommands();
    }
    return true;
}

UFactory* UCharacterInputValidation::CreateAnimationCompositeFactory(USkeletalMesh* PreviewMesh)
{
    if (!PreviewMesh || !PreviewMesh->GetSkeleton()) return nullptr;
    UAnimCompositeFactory* Factory = NewObject<UAnimCompositeFactory>();
    Factory->TargetSkeleton = PreviewMesh->GetSkeleton();
    Factory->PreviewSkeletalMesh = PreviewMesh;
    return Factory;
}

bool UCharacterInputValidation::ConfigureAnimationComposite(UAnimComposite* Animation, const FString& SourceJSON, bool bVerifyOnly)
{
    TSharedPtr<FJsonObject> Source;
    if (!Animation || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJSON), Source) || !Source) return false;
    const TArray<TSharedPtr<FJsonValue>>& Segments = Source->GetArrayField(TEXT("segments"));
    if (Segments.IsEmpty() || !Animation->GetSkeleton()) return false;
    if (!bVerifyOnly)
    {
        Animation->Modify();
        Animation->AnimationTrack.AnimSegments.Reset();
        for (const TSharedPtr<FJsonValue>& Value : Segments)
        {
            const TSharedPtr<FJsonObject> Item = Value->AsObject();
            UAnimSequenceBase* Sequence = LoadObject<UAnimSequenceBase>(nullptr, *Item->GetStringField(TEXT("asset")));
            if (!Sequence || Sequence->GetSkeleton() != Animation->GetSkeleton()) return false;
            FAnimSegment& Segment = Animation->AnimationTrack.AnimSegments.AddDefaulted_GetRef();
            Segment.SetAnimReference(Sequence);
            Segment.StartPos = Item->GetNumberField(TEXT("StartPos"));
            Segment.AnimStartTime = Item->GetNumberField(TEXT("AnimStartTime"));
            Segment.AnimEndTime = Item->GetNumberField(TEXT("AnimEndTime"));
            Segment.AnimPlayRate = Item->GetNumberField(TEXT("AnimPlayRate"));
            Segment.LoopingCount = Item->GetIntegerField(TEXT("LoopingCount"));
        }
        Animation->SetCompositeLength(Source->GetNumberField(TEXT("length")));
        Animation->UpdateCommonTargetFrameRate();
        Animation->RefreshCacheData();
        Animation->MarkPackageDirty();
    }
    if (Animation->AnimationTrack.AnimSegments.Num() != Segments.Num()
        || !FMath::IsNearlyEqual(Animation->GetPlayLength(), Source->GetNumberField(TEXT("length")), 0.00001)) return false;
    for (int32 Index = 0; Index < Segments.Num(); ++Index)
    {
        const FAnimSegment& Segment = Animation->AnimationTrack.AnimSegments[Index];
        const TSharedPtr<FJsonObject> Item = Segments[Index]->AsObject();
        if (!Segment.GetAnimReference() || Segment.GetAnimReference()->GetPathName() != Item->GetStringField(TEXT("asset"))
            || Segment.StartPos != static_cast<float>(Item->GetNumberField(TEXT("StartPos")))
            || Segment.AnimStartTime != static_cast<float>(Item->GetNumberField(TEXT("AnimStartTime")))
            || Segment.AnimEndTime != static_cast<float>(Item->GetNumberField(TEXT("AnimEndTime")))
            || Segment.AnimPlayRate != static_cast<float>(Item->GetNumberField(TEXT("AnimPlayRate")))
            || Segment.LoopingCount != Item->GetIntegerField(TEXT("LoopingCount"))) return false;
    }
    return true;
}

FString UCharacterInputValidation::AuditWeaponEffectPhases(UAnimSequenceBase* Animation, const FString& SourceTraceJSON)
{
    TArray<TSharedPtr<FJsonValue>> Cases;
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!IsRunningCommandlet() || !Animation || !World
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceTraceJSON), Cases)) return TEXT("{}");
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Results;
    int32 CheckedSteps = 0;
    bool bPassed = true;
    for (const auto& CaseValue : Cases)
    {
        const auto Case = CaseValue->AsObject();
        const FString Name = Case->GetStringField(TEXT("notify"));
        UWuWaWeaponEffectNotifyState* Source = nullptr;
        for (const FAnimNotifyEvent& Event : Animation->Notifies)
        {
            auto* Candidate = Cast<UWuWaWeaponEffectNotifyState>(Event.NotifyStateClass);
            if (Candidate && Candidate->SourceNotify == Name) Source = Candidate;
        }
        if (!Source) { bPassed = false; break; }
        FActorSpawnParameters Spawn;
        Spawn.ObjectFlags = RF_Transient;
        AActor* Actor = World->SpawnActor<AActor>(Spawn);
        if (!Actor) { bPassed = false; break; }
        auto* Mesh = NewObject<USkeletalMeshComponent>(Actor);
        Mesh->SetSkeletalMeshAsset(Animation->GetPreviewMesh(true));
        Mesh->RegisterComponentWithWorld(World);
        Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Mesh->SetAnimation(Animation);
        if (!Mesh->GetSingleNodeInstance())
        {
            Actor->Destroy();
            bPassed = false;
            break;
        }
        auto* Niagara = NewObject<UNiagaraComponent>(Actor);
        Niagara->SetAutoActivate(false);
        Niagara->RegisterComponentWithWorld(World);
        double TimeDilation = 1.0;
        if (Case->TryGetNumberField(TEXT("time_dilation"), TimeDilation))
            Niagara->SetCustomTimeDilation(static_cast<float>(TimeDilation));
        auto* Phase = NewObject<UWuWaWeaponEffectPhaseComponent>(Actor);
        Phase->Initialize(Source, Mesh, Animation, Niagara);
        Phase->RegisterComponentWithWorld(World);
        bool bCasePassed = true;
        double MaxPhaseError = 0.0;
        int32 StepIndex = 0;
        for (const auto& StepValue : Case->GetArrayField(TEXT("steps")))
        {
            const auto Step = StepValue->AsObject();
            Mesh->bPauseAnims = Step->GetBoolField(TEXT("paused"));
            if (Step->GetBoolField(TEXT("stop"))) Phase->RequestStop();
            if (Step->GetBoolField(TEXT("switch_animation"))) Mesh->SetAnimation(nullptr);
            Phase->TickComponent(Step->GetNumberField(TEXT("delta")), LEVELTICK_All, &Phase->PrimaryComponentTick);
            const double Error = FMath::Abs(Phase->PassTime - Step->GetNumberField(TEXT("phase")));
            MaxPhaseError = FMath::Max(MaxPhaseError, Error);
            const bool bFinished = Step->GetBoolField(TEXT("finished"));
            bCasePassed &= Error < 1.e-7 && Niagara->IsRegistered() != bFinished
                && Phase->IsRegistered() != bFinished
                && Phase->bPreStopped == Step->GetBoolField(TEXT("pre_stopped"));
            if (!bFinished)
            {
                bCasePassed &= Niagara->IsPaused() == Mesh->bPauseAnims;
                for (const auto& Parameter : Source->FloatCurves)
                {
                    bool bValid = false;
                    const float Value = Niagara->GetVariableFloat(Parameter.Key, bValid);
                    const float Expected = Parameter.Value.GetRichCurveConst()->Eval(Step->GetNumberField(TEXT("phase")));
                    bCasePassed &= bValid && FMath::IsNearlyEqual(Value, Expected, 1.e-6f);
                }
                for (const auto& Parameter : Source->FloatConstants)
                {
                    bool bValid = false;
                    const float Value = Niagara->GetVariableFloat(Parameter.Key, bValid);
                    bCasePassed &= bValid && Value == Parameter.Value;
                }
            }
            ++CheckedSteps;
            ++StepIndex;
            if (!bCasePassed || bFinished) break;
        }
        Actor->Destroy();
        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("notify"), Name);
        Result->SetStringField(TEXT("case"), Case->GetStringField(TEXT("name")));
        Result->SetBoolField(TEXT("passed"), bCasePassed);
        Result->SetNumberField(TEXT("checked_steps"), StepIndex);
        Result->SetNumberField(TEXT("max_phase_error"), MaxPhaseError);
        Results.Add(MakeShared<FJsonValueObject>(Result));
        bPassed &= bCasePassed;
    }
    Report->SetBoolField(TEXT("passed"), bPassed && Results.Num() == Cases.Num());
    Report->SetNumberField(TEXT("checked_steps"), CheckedSteps);
    Report->SetArrayField(TEXT("cases"), Results);
    Report->SetStringField(TEXT("scope"), TEXT("Registered native component manual ticks; no Niagara simulation or PIE"));
    FString Output;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Output));
    return Output;
}

FString UCharacterInputValidation::AuditLiveWeaponEffects(AActor* Actor)
{
    if (!IsValid(Actor) || !Actor->GetWorld() || Actor->GetWorld()->WorldType != EWorldType::PIE) return TEXT("{}");
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Results;
    TInlineComponentArray<UWuWaWeaponEffectPhaseComponent*> Phases(Actor);
    TSet<const UNiagaraComponent*> Controlled;
    for (const auto* Phase : Phases)
    {
        const auto* Source = Phase->Source.Get();
        const auto* Mesh = Phase->Attachment.Get();
        const auto* Effect = Phase->Effect.Get();
        if (!Phase->IsRegistered() || !Source || !Mesh || !Effect) continue;
        Controlled.Add(Effect);
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("instance"), Phase->GetPathName());
        Item->SetStringField(TEXT("notify"), Source->SourceNotify);
        Item->SetStringField(TEXT("system"), GetPathNameSafe(Effect->GetAsset()));
        Item->SetStringField(TEXT("mesh"), Mesh->GetPathName());
        Item->SetStringField(TEXT("socket"), Effect->GetAttachSocketName().ToString());
        Item->SetNumberField(TEXT("phase"), Phase->PassTime);
        Item->SetNumberField(TEXT("animation_position"), Mesh->GetPosition());
        Item->SetBoolField(TEXT("stopping"), Phase->bStopping);
        Item->SetBoolField(TEXT("pre_stopped"), Phase->bPreStopped);
        Item->SetBoolField(TEXT("paused"), Effect->IsPaused());
        Item->SetBoolField(TEXT("mesh_paused"), Mesh->bPauseAnims);
        Item->SetBoolField(TEXT("registered"), Effect->IsRegistered());
        Item->SetBoolField(TEXT("active"), Effect->IsActive());
        Item->SetBoolField(TEXT("visible"), Effect->IsVisible());
        Item->SetBoolField(TEXT("mesh_visible"), Mesh->IsVisible());
        Item->SetBoolField(TEXT("attachment_valid"), Effect->GetAttachParent() == Mesh
            && Effect->GetAttachSocketName() == Source->SocketName && Mesh->DoesSocketExist(Source->SocketName));
        Item->SetBoolField(TEXT("transform_valid"), Effect->GetRelativeLocation().Equals(Source->LocationOffset, 1.e-4f)
            && Effect->GetRelativeRotation().Equals(Source->RotationOffset, 1.e-4f)
            && Effect->GetRelativeScale3D().Equals(Source->Scale, 1.e-4f));
        double MaxError = 0.0;
        bool bParametersValid = true;
        TSharedRef<FJsonObject> Parameters = MakeShared<FJsonObject>();
        auto CheckParameter = [&](FName Name, float Expected)
        {
            bool bValid = false;
            const float Value = Effect->GetVariableFloat(Name, bValid);
            bParametersValid &= bValid;
            MaxError = FMath::Max(MaxError, FMath::Abs(double(Value) - Expected));
            Parameters->SetNumberField(Name.ToString(), Value);
        };
        for (const auto& Parameter : Source->FloatCurves)
            CheckParameter(Parameter.Key, Parameter.Value.GetRichCurveConst()->Eval(Phase->PassTime));
        for (const auto& Parameter : Source->FloatConstants) CheckParameter(Parameter.Key, Parameter.Value);
        Item->SetObjectField(TEXT("parameters"), Parameters);
        Item->SetBoolField(TEXT("parameters_valid"), bParametersValid && MaxError < 1.e-6);
        Item->SetNumberField(TEXT("parameter_max_error"), MaxError);
        Results.Add(MakeShared<FJsonValueObject>(Item));
    }
    TInlineComponentArray<UNiagaraComponent*> Effects(Actor);
    int32 Registered = 0, Orphans = 0;
    for (const auto* Effect : Effects)
    {
        if (!Effect->IsRegistered()) continue;
        ++Registered;
        if (!Controlled.Contains(Effect)) ++Orphans;
    }
    Report->SetArrayField(TEXT("effects"), Results);
    Report->SetNumberField(TEXT("registered_niagara"), Registered);
    Report->SetNumberField(TEXT("orphan_niagara"), Orphans);
    Report->SetBoolField(TEXT("pie"), true);
    FString Output;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Output));
    return Output;
}

bool UCharacterInputValidation::ConfigureWeaponEffectNotifies(UAnimSequenceBase* Animation,
    const FString& SourceJSON, bool bVerifyOnly)
{
    TArray<TSharedPtr<FJsonValue>> Items;
    if (!Animation || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJSON), Items)) return false;
    struct FBinding
    {
        UWuWaWeaponEffectNotifyState* State;
        TSharedPtr<FJsonObject> Item;
        FName Track;
    };
    TArray<FBinding> Bindings;
    auto ReadVector = [](const TSharedPtr<FJsonObject>& Item, const TCHAR* Name)
    {
        const auto& Values = Item->GetArrayField(Name);
        return FVector(Values[0]->AsNumber(), Values[1]->AsNumber(), Values[2]->AsNumber());
    };
    // Validate and load the complete batch before changing the animation package.
    for (const auto& Value : Items)
    {
        const auto Item = Value->AsObject();
        if (!Item) return false;
        auto* State = NewObject<UWuWaWeaponEffectNotifyState>();
        State->SourceNotify = Item->GetStringField(TEXT("notify"));
        State->Template = LoadObject<UNiagaraSystem>(nullptr, *Item->GetStringField(TEXT("system")));
        if (!State->Template) return false;
        State->SocketName = FName(*Item->GetStringField(TEXT("socket")));
        const FTransform Group(FQuat::MakeFromEuler(ReadVector(Item, TEXT("group_rotation"))),
            ReadVector(Item, TEXT("group_location")), ReadVector(Item, TEXT("group_scale")));
        const FTransform Model(FQuat::MakeFromEuler(ReadVector(Item, TEXT("model_rotation"))),
            ReadVector(Item, TEXT("model_location")), ReadVector(Item, TEXT("model_scale")));
        const FTransform Local = Model * Group;
        State->LocationOffset = Local.GetLocation();
        State->RotationOffset = Local.Rotator();
        State->Scale = Local.GetScale3D();
        State->StartTime = Item->GetNumberField(TEXT("start"));
        State->LoopTime = Item->GetNumberField(TEXT("loop"));
        State->EndTime = Item->GetNumberField(TEXT("end"));
        State->bDestroyAtEnd = Item->GetBoolField(TEXT("faster_stop"));
        State->bDeactivateOnStop = Item->GetBoolField(TEXT("deactivate_on_stop"));
        for (const auto& Entry : Item->GetObjectField(TEXT("constants"))->Values)
            State->FloatConstants.Add(FName(*Entry.Key), Entry.Value->AsNumber());
        for (const auto& Entry : Item->GetObjectField(TEXT("curves"))->Values)
        {
            FRichCurve* Curve = State->FloatCurves.Add(FName(*Entry.Key)).GetRichCurve();
            Curve->PreInfinityExtrap = RCCE_Constant;
            Curve->PostInfinityExtrap = RCCE_Constant;
            for (const auto& Key : Entry.Value->AsArray())
            {
                const auto& Pair = Key->AsArray();
                const FKeyHandle Handle = Curve->AddKey(Pair[0]->AsNumber(), Pair[1]->AsNumber());
                Curve->SetKeyInterpMode(Handle, RCIM_Linear);
            }
        }
        Bindings.Add({State, Item, FName(*FString::Printf(TEXT("SourceWeaponEffect_%d"), Item->GetIntegerField(TEXT("track"))))});
    }
    if (!bVerifyOnly)
    {
        Animation->Modify();
        Animation->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
            { return Event.NotifyStateClass && Event.NotifyStateClass->IsA<UWuWaWeaponEffectNotifyState>(); });
        for (const FBinding& Binding : Bindings)
        {
            int32 TrackIndex = Animation->AnimNotifyTracks.IndexOfByPredicate([&Binding](const FAnimNotifyTrack& Track)
                { return Track.TrackName == Binding.Track; });
            if (TrackIndex == INDEX_NONE)
            {
                TrackIndex = Animation->AnimNotifyTracks.AddDefaulted();
                Animation->AnimNotifyTracks[TrackIndex].TrackName = Binding.Track;
            }
            auto* State = DuplicateObject<UWuWaWeaponEffectNotifyState>(Binding.State, Animation);
            State->SetFlags(RF_Transactional);
            FAnimNotifyEvent& Event = Animation->Notifies.AddDefaulted_GetRef();
            Event.NotifyStateClass = State;
            Event.NotifyName = TEXT("SourceWeaponNiagaraEffect");
            Event.TrackIndex = TrackIndex;
            Event.Link(Animation, Binding.Item->GetNumberField(TEXT("time")));
            Event.SetDuration(Binding.Item->GetNumberField(TEXT("duration")));
            Event.TriggerTimeOffset = Binding.Item->GetNumberField(TEXT("trigger_offset"));
            Event.EndTriggerTimeOffset = Binding.Item->GetNumberField(TEXT("end_trigger_offset"));
            Event.TriggerWeightThreshold = Binding.Item->GetNumberField(TEXT("weight_threshold"));
        }
        Animation->RefreshCacheData();
        Animation->MarkPackageDirty();
    }
    int32 Count = 0;
    for (const FAnimNotifyEvent& Event : Animation->Notifies)
    {
        const auto* State = Cast<UWuWaWeaponEffectNotifyState>(Event.NotifyStateClass);
        if (!State) continue;
        ++Count;
        const FBinding* Binding = Bindings.FindByPredicate([State](const FBinding& B)
            { return B.State->SourceNotify == State->SourceNotify; });
        if (!Binding) return false;
        const auto* Expected = Binding->State;
        const auto& Item = Binding->Item;
        if (State->Template != Expected->Template || State->SocketName != Expected->SocketName
            || State->LocationOffset != Expected->LocationOffset || State->RotationOffset != Expected->RotationOffset
            || State->Scale != Expected->Scale || State->StartTime != Expected->StartTime
            || State->LoopTime != Expected->LoopTime || State->EndTime != Expected->EndTime
            || State->bDestroyAtEnd != Expected->bDestroyAtEnd || State->bDeactivateOnStop != Expected->bDeactivateOnStop
            || State->FloatConstants.OrderIndependentCompareEqual(Expected->FloatConstants) == false
            || State->FloatCurves.Num() != Expected->FloatCurves.Num()
            || Event.TrackIndex >= Animation->AnimNotifyTracks.Num()
            || Animation->AnimNotifyTracks[Event.TrackIndex].TrackName != Binding->Track
            || !FMath::IsNearlyEqual(Event.GetTime(), static_cast<float>(Item->GetNumberField(TEXT("time"))), 1.e-6f)
            || !FMath::IsNearlyEqual(Event.GetDuration(), static_cast<float>(Item->GetNumberField(TEXT("duration"))), 1.e-6f)
            || Event.TriggerTimeOffset != static_cast<float>(Item->GetNumberField(TEXT("trigger_offset")))
            || Event.EndTriggerTimeOffset != static_cast<float>(Item->GetNumberField(TEXT("end_trigger_offset")))
            || Event.TriggerWeightThreshold != static_cast<float>(Item->GetNumberField(TEXT("weight_threshold")))) return false;
        for (const auto& Curve : Expected->FloatCurves)
        {
            const auto* Actual = State->FloatCurves.Find(Curve.Key);
            if (!Actual || *Actual->GetRichCurveConst() != *Curve.Value.GetRichCurveConst()) return false;
        }
    }
    return Count == Bindings.Num();
}

bool UCharacterInputValidation::ConfigureWeaponNotifies(UAnimSequence* Animation, const FString& SourceJSON, bool bVerifyOnly)
{
    TArray<TSharedPtr<FJsonValue>> Source;
    if (!Animation || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJSON), Source)) return false;
    const FName TrackName(TEXT("SourceWeapon"));
    if (!bVerifyOnly)
    {
        Animation->Modify();
        Animation->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
            { return Event.NotifyStateClass && Event.NotifyStateClass->IsA<UWuWaWeaponNotifyState>(); });
        int32 TrackIndex = Animation->AnimNotifyTracks.IndexOfByPredicate([TrackName](const FAnimNotifyTrack& Track)
            { return Track.TrackName == TrackName; });
        if (TrackIndex == INDEX_NONE && !Source.IsEmpty())
        {
            TrackIndex = Animation->AnimNotifyTracks.AddDefaulted();
            Animation->AnimNotifyTracks[TrackIndex].TrackName = TrackName;
        }
        for (const TSharedPtr<FJsonValue>& Value : Source)
        {
            const TSharedPtr<FJsonObject> Item = Value->AsObject();
            if (!Item) return false;
            UWuWaWeaponNotifyState* State = NewObject<UWuWaWeaponNotifyState>(Animation, NAME_None, RF_Transactional);
            State->WeaponIndex = Item->GetIntegerField(TEXT("index"));
            State->HangSocket = FName(*Item->GetStringField(TEXT("socket")));
            FAnimNotifyEvent& Event = Animation->Notifies.AddDefaulted_GetRef();
            Event.NotifyStateClass = State;
            Event.NotifyName = TEXT("ShowShowcaseWeapon");
            Event.TrackIndex = TrackIndex;
            Event.Link(Animation, Item->GetNumberField(TEXT("time")));
            Event.SetDuration(Item->GetNumberField(TEXT("duration")));
            Event.TriggerTimeOffset = Item->GetNumberField(TEXT("trigger_offset"));
            Event.EndTriggerTimeOffset = Item->GetNumberField(TEXT("end_trigger_offset"));
            Event.TriggerWeightThreshold = Item->GetNumberField(TEXT("weight_threshold"));
        }
        Animation->RefreshCacheData();
        Animation->MarkPackageDirty();
    }
    TArray<const FAnimNotifyEvent*> Events;
    for (const FAnimNotifyEvent& Event : Animation->Notifies)
        if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UWuWaWeaponNotifyState>()) Events.Add(&Event);
    if (Events.Num() != Source.Num()) return false;
    for (int32 Index = 0; Index < Source.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> Item = Source[Index]->AsObject();
        // RefreshCacheData sorts and assigns tracks to overlapping notifies.
        // Simultaneous weapon events have no source-order contract.
        const int32 EventIndex = Events.IndexOfByPredicate([&Item](const FAnimNotifyEvent* Candidate)
        {
            const UWuWaWeaponNotifyState* State = CastChecked<UWuWaWeaponNotifyState>(Candidate->NotifyStateClass);
            return State->WeaponIndex == Item->GetIntegerField(TEXT("index"))
                && State->HangSocket == FName(*Item->GetStringField(TEXT("socket")))
                && Candidate->GetTime() == static_cast<float>(Item->GetNumberField(TEXT("time")));
        });
        if (EventIndex == INDEX_NONE) return false;
        const FAnimNotifyEvent& Event = *Events[EventIndex];
        const UWuWaWeaponNotifyState* State = CastChecked<UWuWaWeaponNotifyState>(Event.NotifyStateClass);
        if (State->WeaponIndex != Item->GetIntegerField(TEXT("index"))
            || State->HangSocket != FName(*Item->GetStringField(TEXT("socket")))
            || Event.GetTime() != static_cast<float>(Item->GetNumberField(TEXT("time")))
            || Event.GetDuration() != static_cast<float>(Item->GetNumberField(TEXT("duration")))
            || Event.TriggerTimeOffset != static_cast<float>(Item->GetNumberField(TEXT("trigger_offset")))
            || Event.EndTriggerTimeOffset != static_cast<float>(Item->GetNumberField(TEXT("end_trigger_offset")))
            || Event.TriggerWeightThreshold != static_cast<float>(Item->GetNumberField(TEXT("weight_threshold"))))
        {
            UE_LOG(LogTemp, Error, TEXT("Weapon notify mismatch %s index %d: time %.9g/%.9g duration %.9g/%.9g offsets %.9g/%.9g %.9g/%.9g weight %.9g/%.9g"),
                *Animation->GetPathName(), State->WeaponIndex, Event.GetTime(), Item->GetNumberField(TEXT("time")),
                Event.GetDuration(), Item->GetNumberField(TEXT("duration")), Event.TriggerTimeOffset, Item->GetNumberField(TEXT("trigger_offset")),
                Event.EndTriggerTimeOffset, Item->GetNumberField(TEXT("end_trigger_offset")), Event.TriggerWeightThreshold, Item->GetNumberField(TEXT("weight_threshold")));
            return false;
        }
        Events.RemoveAt(EventIndex);
    }
    return true;
}

FString UCharacterInputValidation::AuditMaterialResources(UMaterialInterface* Material)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    const FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
    Report->SetBoolField(TEXT("valid"), Resource != nullptr);
    if (Resource)
    {
        Report->SetBoolField(TEXT("compilation_finished"), Resource->IsCompilationFinished());
        Report->SetBoolField(TEXT("uses_pixel_depth_offset"), Resource->MaterialUsesPixelDepthOffset_GameThread());
        Report->SetBoolField(TEXT("skeletal_mesh_usage"), Resource->IsUsedWithSkeletalMesh());
        Report->SetBoolField(TEXT("morph_targets_usage"), Resource->IsUsedWithMorphTargets());
        Report->SetNumberField(TEXT("blend_mode"), static_cast<int32>(Resource->GetBlendMode()));
        Report->SetNumberField(TEXT("compile_errors"), Resource->GetCompileErrors().Num());
        if (Resource->GetGameThreadShaderMap())
        {
            TMap<FShaderId, TShaderRef<FShader>> Shaders;
            Resource->GetGameThreadShaderMap()->GetShaderList(Shaders);
            TArray<TSharedPtr<FJsonValue>> ForwardShaders;
            TArray<TSharedPtr<FJsonValue>> OutlineShaders;
            for (const auto& Entry : Shaders)
            {
                const FString Name = Entry.Key.Type->GetName();
                const bool bForward = Name.StartsWith(TEXT("FWuWaTranslucent"));
                const bool bOutline = Name.StartsWith(TEXT("FWuWaOutline"));
                if (!bForward && !bOutline) continue;
                TSharedRef<FJsonObject> Shader = MakeShared<FJsonObject>();
                Shader->SetStringField(TEXT("shader_type"), Name);
                Shader->SetStringField(TEXT("vertex_factory"), Entry.Key.VFType ? Entry.Key.VFType->GetName() : TEXT(""));
                if (bForward) ForwardShaders.Add(MakeShared<FJsonValueObject>(Shader));
                if (bOutline) OutlineShaders.Add(MakeShared<FJsonValueObject>(Shader));
            }
            Report->SetArrayField(TEXT("native_forward_shaders"), ForwardShaders);
            Report->SetArrayField(TEXT("native_outline_shaders"), OutlineShaders);
        }
    }
    FString Result;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
    FJsonSerializer::Serialize(Report, Writer);
    return Result;
}

bool UCharacterInputValidation::DeleteAssetPackage(const FString& AssetPath)
{
    // Loading internally avoids a Python wrapper becoming a live GC referencer
    // while the standard safe-delete model checks memory references.
    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    return Asset && Asset->IsAsset() && ObjectTools::DeleteAssets({FAssetData(Asset)}, false) == 1;
}

UMaterialExpression* UCharacterInputValidation::GetPixelDepthOffsetInput(UMaterial* Material)
{
    return UMaterialEditingLibrary::GetMaterialPropertyInputNode(Material, MP_PixelDepthOffset);
}

bool UCharacterInputValidation::ConnectPixelDepthOffset(UMaterialExpression* Expression, const FString& Output)
{
    return UMaterialEditingLibrary::ConnectMaterialProperty(Expression, Output, MP_PixelDepthOffset);
}

int32 UCharacterInputValidation::FinishMaterialCompilation(const TArray<UMaterialInterface*>& Materials)
{
    TArray<FMaterialResource*> Resources;
    for (UMaterialInterface* Material : Materials)
    {
        FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
        if (!Resource) return -1;
        Resources.AddUnique(Resource);
        if (!Resource->IsGameThreadShaderMapComplete())
            Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
    }
    for (FMaterialResource* Resource : Resources) Resource->FinishCompilation();
    return Resources.Num();
}

int32 UCharacterInputValidation::RestoreMorphCurveMetadata(USkeletalMesh* Mesh)
{
    USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
    if (!Skeleton) return -1;
    int32 Restored = 0;
    for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
    {
        if (!Skeleton->GetCurveMetaDataMorphTarget(Morph->GetFName()))
        {
            Skeleton->AccumulateCurveMetaData(Morph->GetFName(), false, true);
            ++Restored;
        }
    }
    return Restored;
}

FString UCharacterInputValidation::AuditComponentPose(USkeletalMeshComponent* Component)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
    UAnimInstance* Instance = Component ? Component->GetAnimInstance() : nullptr;
    Report->SetBoolField(TEXT("valid"), Mesh && Component->IsRegistered());
    Report->SetBoolField(TEXT("has_animation_instance"), Instance!=nullptr);
    if (Mesh)
    {
        Report->SetNumberField(TEXT("animation_time"), Component->GetPosition());
        TArray<TSharedPtr<FJsonValue>> Bones;
        for (int32 Index = 0; Index < Mesh->GetRefSkeleton().GetNum(); ++Index)
        {
            const FName Name = Mesh->GetRefSkeleton().GetBoneName(Index);
            const FTransform Transform = Component->GetSocketTransform(Name, RTS_Component);
            TSharedRef<FJsonObject> Bone = MakeShared<FJsonObject>();
            Bone->SetStringField(TEXT("name"), Name.ToString());
            TArray<TSharedPtr<FJsonValue>> Position, Rotation;
            const FVector P = Transform.GetTranslation();
            const FQuat Q = Transform.GetRotation();
            for (double Value : {P.X, P.Y, P.Z}) Position.Add(MakeShared<FJsonValueNumber>(Value));
            for (double Value : {Q.X, Q.Y, Q.Z, Q.W}) Rotation.Add(MakeShared<FJsonValueNumber>(Value));
            Bone->SetArrayField(TEXT("position"), Position);
            Bone->SetArrayField(TEXT("rotation"), Rotation);
            Bones.Add(MakeShared<FJsonValueObject>(Bone));
        }
        Report->SetArrayField(TEXT("bones"), Bones);
        TSharedRef<FJsonObject> Curves = MakeShared<FJsonObject>();
        TSharedRef<FJsonObject> Flags = MakeShared<FJsonObject>();
        for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
        {
            Curves->SetNumberField(Morph->GetName(), Instance ? Instance->GetCurveValue(Morph->GetFName()) : 0.f);
            Flags->SetBoolField(Morph->GetName(), Mesh->GetSkeleton() && Mesh->GetSkeleton()->GetCurveMetaDataMorphTarget(Morph->GetFName()));
        }
        Report->SetObjectField(TEXT("animation_curves_for_morph_names"), Curves);
        Report->SetObjectField(TEXT("skeleton_morph_curve_flags"), Flags);
        TSharedRef<FJsonObject> Active = MakeShared<FJsonObject>();
        for (const auto& Morph : Component->ActiveMorphTargets)
        {
            if (Morph.Key && Component->MorphTargetWeights.IsValidIndex(Morph.Value))
                Active->SetNumberField(Morph.Key->GetName(), Component->MorphTargetWeights[Morph.Value]);
        }
        Report->SetObjectField(TEXT("active_morph_weights"), Active);
    }
    FString Result;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Result));
    return Result;
}

namespace
{
FString SerializeAudit(const TSharedRef<FJsonObject>& Report)
{
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
}

int32 UCharacterInputValidation::RestoreSourceVertexColors(USkeletalMesh* Mesh, const FString& ModelSource)
{
    UEFModelReader Model(ModelSource);
    if (!Mesh || !Model.Read() || Model.LODs.Num() != 1 || Model.LODs[0].VertexColors.Num() != 1)
        return -1;
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    const FLODData& Source = Model.LODs[0];
    if (!Description || Description->Vertices().Num() != Source.Vertices.Num() ||
        Source.VertexColors[0].Data.Num() != Source.Vertices.Num())
    {
        UE_LOG(LogTemp, Error, TEXT("Vertex color repair: source/native vertex counts differ or MeshDescription is missing"));
        return -1;
    }
    FSkeletalMeshAttributes Attributes(*Description);
    const auto Positions = Attributes.GetVertexPositions();
    const auto Colors = Attributes.GetVertexInstanceColors();
    TArray<FVertexInstanceID> LegacyInstances;
    for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
    {
        const FVertexID Vertex = Description->GetVertexInstanceVertex(Instance);
        if (!Source.Vertices.IsValidIndex(Vertex.GetValue()) || Positions[Vertex] != Source.Vertices[Vertex.GetValue()])
        {
            UE_LOG(LogTemp, Error, TEXT("Vertex color repair: position differs at native vertex %d"), Vertex.GetValue());
            return -1;
        }
        const FColor SourceColor = Source.VertexColors[0].Data[Vertex.GetValue()];
        const FVector4f Expected(SourceColor.ReinterpretAsLinear());
        if (Colors[Instance].ContainsNaN())
            return -1;
        if ((Colors[Instance] - Expected).Size() < 0.000001f)
            continue;
        if ((Colors[Instance] - FVector4f(FLinearColor(SourceColor))).Size() >= 0.000001f)
        {
            UE_LOG(LogTemp, Error, TEXT("Vertex color repair: unknown color at instance %d, vertex %d: actual=%s source=%s legacy=%s"),
                Instance.GetValue(), Vertex.GetValue(), *Colors[Instance].ToString(), *Expected.ToString(),
                *FVector4f(FLinearColor(SourceColor)).ToString());
            return -1;
        }
        LegacyInstances.Add(Instance);
    }
    if (LegacyInstances.IsEmpty())
        return 0;
    Mesh->Modify();
    // The return value reports undo-buffer capture, which is normally false in
    // commandlets. Source compatibility was checked above; commit below reports
    // whether the actual MeshDescription operation succeeds.
    Mesh->ModifyMeshDescription(0);
    for (const FVertexInstanceID Instance : LegacyInstances)
    {
        const int32 Vertex = Description->GetVertexInstanceVertex(Instance).GetValue();
        Colors.Set(Instance, FVector4f(Source.VertexColors[0].Data[Vertex].ReinterpretAsLinear()));
    }
    if (!Mesh->CommitMeshDescription(0))
        return -1;
    Mesh->PostEditChange();
    USkinnedAsset* Assets[] = {Mesh};
    FSkinnedAssetCompilingManager::Get().FinishCompilation(Assets);
    return LegacyInstances.Num();
}

FString UCharacterInputValidation::AuditImportedInputs(USkeletalMesh* Mesh, UAnimSequence* Animation,
    const FString& ModelSource, const FString& AnimationSource)
{
    const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    auto Finish = [&Report]()
    {
        return SerializeAudit(Report);
    };
    Report->SetBoolField(TEXT("passed"), false);
    UEFModelReader Model(ModelSource);
    UEFAnimReader Anim(AnimationSource);
    if (!Mesh || !Animation || !Model.Read() || Model.LODs.Num() != 1 || !Anim.Read())
    {
        Report->SetStringField(TEXT("error"), TEXT("Missing asset or invalid source export"));
        return Finish();
    }
    const FMeshDescription* Description = Mesh->GetMeshDescription(0);
    const FSkeletalMeshModel* ImportedModel = Mesh->GetImportedModel();
    const IAnimationDataModel* DataModel = Animation->GetDataModel();
    if (!Description || !ImportedModel || !ImportedModel->LODModels.Num() || !DataModel)
    {
        Report->SetStringField(TEXT("error"), TEXT("Native source mesh or animation data is unavailable"));
        return Finish();
    }
    const FLODData& Source = Model.LODs[0];
    const FSkeletalMeshConstAttributes Attributes(*Description);
    const auto Positions = Attributes.GetVertexPositions();
    const auto Normals = Attributes.GetVertexInstanceNormals();
    const auto Tangents = Attributes.GetVertexInstanceTangents();
    const auto UVs = Attributes.GetVertexInstanceUVs();
    const auto Colors = Attributes.GetVertexInstanceColors();
    double PositionError = 0.0, MorphPositionError = 0.0, MorphNormalError = 0.0;
    double NormalError = 0.0, TangentError = 0.0, UVError = 0.0, RenderMorphError = 0.0;
    int32 MissingMorphs = 0, CheckedMorphDeltas = 0, RenderMorphDeltas = 0;
    double ColorError = 0.0;
    int32 BuiltColorMismatches = 0, CheckedBuiltColors = 0;
    const bool ValidSourceColors = Source.VertexColors.Num() == 1 &&
        Source.VertexColors[0].Data.Num() == Source.Vertices.Num();
    const bool NoSourceColors = Source.VertexColors.IsEmpty();
    const bool SameVertices = Description->Vertices().Num() == Source.Vertices.Num();
    if (SameVertices)
        for (int32 Index = 0; Index < Source.Vertices.Num(); ++Index)
            PositionError = FMath::Max(PositionError, static_cast<double>((Positions[FVertexID(Index)] - Source.Vertices[Index]).Size()));

    const bool SameUVChannels = UVs.GetNumChannels() == Source.TextureCoordinates.Num();
    for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
    {
        const int32 Vertex = Description->GetVertexInstanceVertex(Instance).GetValue();
        const FVector4f& Normal = Source.Normals[Vertex];
        NormalError = FMath::Max(NormalError, static_cast<double>((Normals[Instance] - FVector3f(Normal.Y, Normal.Z, Normal.W)).Size()));
        TangentError = FMath::Max(TangentError, static_cast<double>((Tangents[Instance] - Source.Tangents[Vertex]).Size()));
        if (ValidSourceColors)
            ColorError = FMath::Max(ColorError, static_cast<double>((Colors[Instance] -
                FVector4f(Source.VertexColors[0].Data[Vertex].ReinterpretAsLinear())).Size()));
        else if (NoSourceColors)
            ColorError = FMath::Max(ColorError, static_cast<double>((Colors[Instance] - FVector4f(1.0f, 1.0f, 1.0f, 1.0f)).Size()));
        if (SameUVChannels)
            for (int32 Channel = 0; Channel < Source.TextureCoordinates.Num(); ++Channel)
                UVError = FMath::Max(UVError, static_cast<double>((UVs.Get(Instance, Channel) - Source.TextureCoordinates[Channel][Vertex]).Size()));
    }

    const FSkeletalMeshLODModel& LOD = ImportedModel->LODModels[0];
    if (ValidSourceColors || NoSourceColors)
        for (const FSkelMeshSection& Section : LOD.Sections)
            for (int32 Index = 0; Index < Section.SoftVertices.Num(); ++Index)
            {
                const int32 BuiltVertex = Section.BaseVertexIndex + Index;
                const int32 SourceVertex = LOD.MeshToImportVertexMap.IsValidIndex(BuiltVertex) ?
                    LOD.MeshToImportVertexMap[BuiltVertex] : INDEX_NONE;
                ++CheckedBuiltColors;
                if (ValidSourceColors ? (!Source.VertexColors[0].Data.IsValidIndex(SourceVertex) ||
                    Section.SoftVertices[Index].Color != Source.VertexColors[0].Data[SourceVertex]) :
                    Section.SoftVertices[Index].Color != FColor::White)
                    ++BuiltColorMismatches;
            }
    TSet<int32> RenderedSourceVertices;
    for (const int32 SourceVertex : LOD.MeshToImportVertexMap)
        RenderedSourceVertices.Add(SourceVertex);
    int32 EquivalentMergedVertices = 0;
    for (int32 Vertex = 0; Vertex < Source.Vertices.Num(); ++Vertex)
    {
        if (RenderedSourceVertices.Contains(Vertex))
            continue;
        for (const int32 Candidate : RenderedSourceVertices)
        {
            if (Source.Vertices[Vertex] != Source.Vertices[Candidate] || Source.Normals[Vertex] != Source.Normals[Candidate] || Source.Tangents[Vertex] != Source.Tangents[Candidate])
                continue;
            bool Equivalent = true;
            for (const auto& Channel : Source.TextureCoordinates)
                Equivalent &= Channel[Vertex] == Channel[Candidate];
            for (const auto& Color : Source.VertexColors)
                Equivalent &= Color.Data[Vertex] == Color.Data[Candidate];
            TMap<int32, float> VertexWeights, CandidateWeights;
            for (const FWeightChunk& Weight : Source.Weights)
            {
                if (Weight.WeightVertexIndex == Vertex) VertexWeights.Add(Weight.WeightBoneIndex, Weight.WeightAmount);
                if (Weight.WeightVertexIndex == Candidate) CandidateWeights.Add(Weight.WeightBoneIndex, Weight.WeightAmount);
            }
            Equivalent &= VertexWeights.OrderIndependentCompareEqual(CandidateWeights);
            for (const FMorphTargetChunk& Morph : Source.Morphs)
            {
                FVector3f VertexDelta = FVector3f::ZeroVector, CandidateDelta = FVector3f::ZeroVector;
                for (const FMorphTargetDataChunk& Delta : Morph.MorphDeltas)
                {
                    if (Delta.MorphVertexIndex == Vertex) VertexDelta = Delta.MorphPosition;
                    if (Delta.MorphVertexIndex == Candidate) CandidateDelta = Delta.MorphPosition;
                }
                Equivalent &= VertexDelta == CandidateDelta;
            }
            if (Equivalent)
            {
                ++EquivalentMergedVertices;
                break;
            }
        }
    }
    for (const FMorphTargetChunk& Morph : Source.Morphs)
    {
        const FName Name(UTF8_TO_TCHAR(Morph.MorphName.c_str()));
        const auto MorphPositions = Attributes.GetVertexMorphPositionDelta(Name);
        const auto MorphNormals = Attributes.GetVertexInstanceMorphNormalDelta(Name);
        const UMorphTarget* RuntimeMorph = Mesh->FindMorphTarget(Name);
        if (!MorphPositions.IsValid() || !MorphNormals.IsValid() || !RuntimeMorph)
        {
            ++MissingMorphs;
            continue;
        }
        for (const FMorphTargetDataChunk& Delta : Morph.MorphDeltas)
        {
            const FVertexID Vertex(Delta.MorphVertexIndex);
            MorphPositionError = FMath::Max(MorphPositionError, static_cast<double>((MorphPositions[Vertex] - Delta.MorphPosition).Size()));
            for (const FVertexInstanceID Instance : Description->GetVertexVertexInstanceIDs(Vertex))
                MorphNormalError = FMath::Max(MorphNormalError, static_cast<double>((MorphNormals[Instance] - Delta.MorphNormals).Size()));
            ++CheckedMorphDeltas;
        }
        TMap<int32, FVector3f> SourceDeltas;
        for (const FMorphTargetDataChunk& Delta : Morph.MorphDeltas)
            SourceDeltas.Add(Delta.MorphVertexIndex, Delta.MorphPosition);
        const auto RuntimeDeltas = RuntimeMorph->GetMorphTargetDeltas(0);
        RenderMorphDeltas += RuntimeDeltas.Num();
        for (const FMorphTargetDelta& Delta : RuntimeDeltas)
        {
            if (!LOD.MeshToImportVertexMap.IsValidIndex(Delta.SourceIdx))
            {
                ++MissingMorphs;
                continue;
            }
            const int32 Vertex = LOD.MeshToImportVertexMap[Delta.SourceIdx];
            RenderMorphError = FMath::Max(RenderMorphError, static_cast<double>((Delta.PositionDelta - SourceDeltas.FindRef(Vertex)).Size()));
        }
    }

    double TranslationError = 0.0, RotationError = 0.0, ScaleError = 0.0;
    double RelativeScaleError = 0.0;
    double NormalizedRotationError = 0.0, RotationNormError = 0.0;
    double NativeRotationError = 0.0;
    double NativeTranslationError = 0.0, NativeScaleError = 0.0;
    const bool UsesEulerChannels = Animation->GetDataModelInterface().GetObject()->GetClass()->GetFName() == TEXT("AnimationSequencerDataModel");
    int32 CheckedKeys = 0, MissingTracks = 0;
    for (const FTrack& Track : Anim.Tracks)
    {
        const FName Name(UTF8_TO_TCHAR(Track.TrackName.c_str()));
        if (!DataModel->IsValidBoneTrackName(Name))
        {
            ++MissingTracks;
            continue;
        }
        // Sequencer stores a channel as its first value when every key differs
        // by at most UE_KINDA_SMALL_NUMBER. Verify that native representation
        // independently while retaining the original source errors below.
        auto VaryingVectorChannels = [](const TArray<FVectorKey>& Keys)
        {
            FIntVector Varying = FIntVector::ZeroValue;
            for (const FVectorKey& Key : Keys)
                for (int32 Axis = 0; Axis < 3; ++Axis)
                    Varying[Axis] |= !FMath::IsNearlyEqual(Keys[0].VectorValue[Axis], Key.VectorValue[Axis], UE_KINDA_SMALL_NUMBER);
            return Varying;
        };
        const FIntVector VaryingTranslation = UsesEulerChannels ? VaryingVectorChannels(Track.TrackPosKeys) : FIntVector(1);
        const FIntVector VaryingScale = UsesEulerChannels ? VaryingVectorChannels(Track.TrackScaleKeys) : FIntVector(1);
        FQuat4f FirstRotation = Track.TrackRotKeys[0].QuatValue;
        FirstRotation.Normalize();
        const FVector3f FirstEuler = FirstRotation.Euler();
        FIntVector VaryingEuler = FIntVector::ZeroValue;
        for (const FQuatKey& Key : Track.TrackRotKeys)
        {
            FQuat4f Rotation = Key.QuatValue;
            Rotation.Normalize();
            const FVector3f Euler = Rotation.Euler();
            for (int32 Axis = 0; Axis < 3; ++Axis)
                VaryingEuler[Axis] |= !FMath::IsNearlyEqual(FirstEuler[Axis], Euler[Axis], UE_KINDA_SMALL_NUMBER);
        }
        for (const FVectorKey& Key : Track.TrackPosKeys)
        {
            const FTransform Value = DataModel->GetBoneTrackTransform(Name, FFrameNumber(Key.Frame));
            TranslationError = FMath::Max(TranslationError, (Value.GetTranslation() - FVector(Key.VectorValue)).Size());
            FVector Expected(Key.VectorValue);
            for (int32 Axis = 0; Axis < 3; ++Axis)
                if (!VaryingTranslation[Axis]) Expected[Axis] = Track.TrackPosKeys[0].VectorValue[Axis];
            NativeTranslationError = FMath::Max(NativeTranslationError, (Value.GetTranslation() - Expected).Size());
            ++CheckedKeys;
        }
        for (const FQuatKey& Key : Track.TrackRotKeys)
        {
            const FQuat Expected = FQuat(Key.QuatValue).GetNormalized();
            const FQuat Actual = DataModel->GetBoneTrackTransform(Name, FFrameNumber(Key.Frame)).GetRotation();
            RotationError = FMath::Max(RotationError, FMath::Min((Expected - Actual).Size(), (Expected + Actual).Size()));
            // Compare orientation independently of the float data model's small
            // quaternion-length error; retain and bound that error separately.
            const FQuat UnitActual = Actual.GetNormalized();
            NormalizedRotationError = FMath::Max(NormalizedRotationError,
                FMath::Min((Expected - UnitActual).Size(), (Expected + UnitActual).Size()));
            RotationNormError = FMath::Max(RotationNormError, FMath::Abs(Actual.SizeSquared() - 1.0));
            // UE 5.8's Sequencer controller stores FQuat4f keys as FVector3f
            // Euler channels. Verify that documented conversion independently
            // and retain the source-orientation difference above.
            FQuat4f Imported = Key.QuatValue;
            Imported.Normalize();
            FVector3f NativeEuler = Imported.Euler();
            for (int32 Axis = 0; Axis < 3; ++Axis)
                if (!VaryingEuler[Axis]) NativeEuler[Axis] = FirstEuler[Axis];
            const FQuat NativeExpected = UsesEulerChannels
                ? FQuat::MakeFromEuler(FVector(NativeEuler)).GetNormalized() : Expected;
            NativeRotationError = FMath::Max(NativeRotationError,
                FMath::Min((NativeExpected - UnitActual).Size(), (NativeExpected + UnitActual).Size()));
            ++CheckedKeys;
        }
        for (const FVectorKey& Key : Track.TrackScaleKeys)
        {
            const FTransform Value = DataModel->GetBoneTrackTransform(Name, FFrameNumber(Key.Frame));
            const double Error = (Value.GetScale3D() - FVector(Key.VectorValue)).Size();
            ScaleError = FMath::Max(ScaleError, Error);
            RelativeScaleError = FMath::Max(RelativeScaleError, Error / FMath::Max(1.0, FVector(Key.VectorValue).Size()));
            FVector Expected(Key.VectorValue);
            for (int32 Axis = 0; Axis < 3; ++Axis)
                if (!VaryingScale[Axis]) Expected[Axis] = Track.TrackScaleKeys[0].VectorValue[Axis];
            NativeScaleError = FMath::Max(NativeScaleError, (Value.GetScale3D() - Expected).Size());
            ++CheckedKeys;
        }
    }
    double CurveError = 0.0;
    int32 MissingCurves = 0, CheckedCurveKeys = 0;
    for (const auto& SourceCurve : Anim.Curves)
    {
        const FAnimationCurveIdentifier Identifier(FName(UTF8_TO_TCHAR(SourceCurve.CurveName.c_str())), ERawCurveTrackTypes::RCT_Float);
        const FFloatCurve* Curve = DataModel->FindFloatCurve(Identifier);
        if (!Curve)
        {
            ++MissingCurves;
            continue;
        }
        for (const auto& Key : SourceCurve.CurveKeys)
        {
            const float Time = Key.Frame / DataModel->GetFrameRate().AsDecimal();
            CurveError = FMath::Max(CurveError, static_cast<double>(FMath::Abs(Curve->FloatCurve.Eval(Time) - Key.FloatValue)));
            ++CheckedCurveKeys;
        }
    }
    Report->SetNumberField(TEXT("animation_curve_value_max_error"), CurveError);
    Report->SetNumberField(TEXT("animation_curve_keys_checked"), CheckedCurveKeys);
    Report->SetNumberField(TEXT("animation_missing_curves"), MissingCurves);
    Report->SetNumberField(TEXT("source_vertices"), Source.Vertices.Num());
    Report->SetNumberField(TEXT("saved_source_vertices"), Description->Vertices().Num());
    Report->SetNumberField(TEXT("render_vertices"), LOD.NumVertices);
    Report->SetNumberField(TEXT("source_position_max_error"), PositionError);
    Report->SetNumberField(TEXT("source_normal_max_error"), NormalError);
    Report->SetNumberField(TEXT("source_tangent_max_error"), TangentError);
    Report->SetNumberField(TEXT("source_uv_max_error"), UVError);
    Report->SetNumberField(TEXT("source_uv_channels"), Source.TextureCoordinates.Num());
    Report->SetBoolField(TEXT("source_vertex_colors_valid"), ValidSourceColors);
    Report->SetBoolField(TEXT("source_vertex_colors_absent"), NoSourceColors);
    Report->SetNumberField(TEXT("source_vertex_color_max_error"), ColorError);
    Report->SetNumberField(TEXT("built_vertex_colors_checked"), CheckedBuiltColors);
    Report->SetNumberField(TEXT("built_vertex_color_mismatches"), BuiltColorMismatches);
    Report->SetNumberField(TEXT("render_source_vertices"), RenderedSourceVertices.Num());
    Report->SetNumberField(TEXT("equivalent_merged_vertices"), EquivalentMergedVertices);
    Report->SetNumberField(TEXT("morph_render_position_max_error"), RenderMorphError);
    Report->SetNumberField(TEXT("morph_position_max_error"), MorphPositionError);
    Report->SetNumberField(TEXT("morph_normal_max_error"), MorphNormalError);
    Report->SetNumberField(TEXT("morph_source_deltas_checked"), CheckedMorphDeltas);
    Report->SetNumberField(TEXT("morph_render_deltas"), RenderMorphDeltas);
    Report->SetNumberField(TEXT("missing_morphs"), MissingMorphs);
    int32 MissingMorphCurveMetadata = 0;
    for (const TObjectPtr<UMorphTarget>& Morph : Mesh->GetMorphTargets())
    {
        if (!Mesh->GetSkeleton()->GetCurveMetaDataMorphTarget(Morph->GetFName())) ++MissingMorphCurveMetadata;
    }
    Report->SetNumberField(TEXT("missing_morph_curve_metadata"), MissingMorphCurveMetadata);
    Report->SetNumberField(TEXT("animation_keys_checked"), CheckedKeys);
    Report->SetNumberField(TEXT("animation_translation_max_error"), TranslationError);
    Report->SetNumberField(TEXT("animation_rotation_max_error"), RotationError);
    Report->SetNumberField(TEXT("animation_rotation_normalized_max_error"), NormalizedRotationError);
    Report->SetNumberField(TEXT("animation_rotation_squared_norm_max_error"), RotationNormError);
    Report->SetNumberField(TEXT("animation_rotation_native_conversion_max_error"), NativeRotationError);
    const bool RotationMatchesSource = NormalizedRotationError < 0.00001;
    Report->SetBoolField(TEXT("animation_rotation_matches_source"), RotationMatchesSource);
    Report->SetBoolField(TEXT("animation_native_euler_channels"), UsesEulerChannels);
    Report->SetNumberField(TEXT("animation_scale_max_error"), ScaleError);
    Report->SetNumberField(TEXT("animation_scale_relative_max_error"), RelativeScaleError);
    Report->SetNumberField(TEXT("animation_translation_native_conversion_max_error"), NativeTranslationError);
    Report->SetNumberField(TEXT("animation_scale_native_conversion_max_error"), NativeScaleError);
    Report->SetNumberField(TEXT("native_near_constant_channel_tolerance"), UsesEulerChannels ? UE_KINDA_SMALL_NUMBER : 0.0);
    Report->SetNumberField(TEXT("missing_tracks"), MissingTracks);
    Report->SetNumberField(TEXT("animation_float_curves"), DataModel->GetNumberOfFloatCurves());
    const bool NativeConversionPassed = SameVertices && SameUVChannels && MissingMorphs == 0 && MissingTracks == 0 && MissingMorphCurveMetadata == 0 &&
        (ValidSourceColors || NoSourceColors) && ColorError < 0.000001 && BuiltColorMismatches == 0 && CheckedBuiltColors == LOD.NumVertices &&
        RenderedSourceVertices.Num() + EquivalentMergedVertices == Source.Vertices.Num() &&
        NormalError < 0.00001 && TangentError < 0.00001 && UVError < 0.00001 && RenderMorphError < 0.0001 &&
        PositionError < 0.00001 && MorphPositionError < 0.00001 && MorphNormalError < 0.00001 &&
        (NativeTranslationError < 0.00001 || TranslationError < 0.00001) && (NativeRotationError < 0.00001 || RotationMatchesSource) &&
        RotationNormError < 0.0001 && (NativeScaleError < 0.000001 || ScaleError < 0.000001) &&
        MissingCurves == 0 && CurveError < 0.00001 && DataModel->GetNumberOfFloatCurves() == Anim.Curves.Num();
    Report->SetBoolField(TEXT("native_conversion_passed"), NativeConversionPassed);
    // The legacy native conversion may clamp pitch early. Exact source
    // orientations are valid too; retain the strict fidelity gate below.
    Report->SetBoolField(TEXT("passed"), NativeConversionPassed &&
        NormalizedRotationError < (UsesEulerChannels ? 0.00005 : 0.00001) && TranslationError < 0.0001 && ScaleError < 0.00001);
    return Finish();
}

bool UCharacterInputValidation::PreserveStaticMeshInputs(UStaticMesh* Mesh)
{
    if (!Mesh || Mesh->GetNumSourceModels() == 0) return false;
    UStaticMesh* Assets[] = {Mesh};
    FStaticMeshCompilingManager::Get().FinishCompilation(Assets);
    Mesh->Modify();
    for (int32 Index = 0; Index < Mesh->GetNumSourceModels(); ++Index)
    {
        FStaticMeshSourceModel& Model = Mesh->GetSourceModel(Index);
        Model.BuildSettings.bRecomputeNormals = false;
        Model.BuildSettings.bRecomputeTangents = false;
        Model.BuildSettings.bRemoveDegenerates = false;
        Model.BuildSettings.bGenerateLightmapUVs = false;
        Model.BuildSettings.bUseFullPrecisionUVs = true;
        Model.BuildSettings.bUseHighPrecisionTangentBasis = true;
    }
    Mesh->PostEditChange();
    FStaticMeshCompilingManager::Get().FinishCompilation(Assets);
    return true;
}

int32 UCharacterInputValidation::RestoreStaticMeshMaterialSlots(UStaticMesh* Mesh, const FString& ModelSource)
{
    UEFModelReader Model(ModelSource);
    if (!Mesh || !Model.Read() || Model.LODs.Num() != 1 || Mesh->GetNumSourceModels() != 1) return -1;
    UStaticMesh* Assets[] = {Mesh};
    FStaticMeshCompilingManager::Get().FinishCompilation(Assets);
    FMeshDescription* Description = Mesh->GetMeshDescription(0);
    const FLODData& Source = Model.LODs[0];
    if (!Description || Description->PolygonGroups().Num() != Source.Materials.Num()
        || Mesh->GetStaticMaterials().Num() != Source.Materials.Num()) return -1;
    FStaticMeshAttributes Attributes(*Description);
    const auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    TArray<FPolygonGroupID> Repairs;
    for (int32 Index = 0; Index < Source.Materials.Num(); ++Index)
    {
        const FPolygonGroupID Group(Index);
        const FName SourceName(UTF8_TO_TCHAR(Source.Materials[Index].Name.c_str()));
        const FName LegacyPath(UTF8_TO_TCHAR(Source.Materials[Index].Path.c_str()));
        const FStaticMaterial& Material = Mesh->GetStaticMaterials()[Index];
        if (!Description->PolygonGroups().IsValid(Group) || Material.MaterialSlotName != SourceName
            || Material.ImportedMaterialSlotName != SourceName) return -1;
        if (SlotNames[Group] != SourceName)
        {
            if (SlotNames[Group] != LegacyPath) return -1;
            Repairs.Add(Group);
        }
    }
    if (!Repairs.IsEmpty())
    {
        Mesh->Modify();
        for (const FPolygonGroupID Group : Repairs)
            SlotNames[Group] = FName(UTF8_TO_TCHAR(Source.Materials[Group.GetValue()].Name.c_str()));
        Mesh->CommitMeshDescription(0);
        Mesh->PostEditChange();
        FStaticMeshCompilingManager::Get().FinishCompilation(Assets);
    }
    return Repairs.Num();
}

FString UCharacterInputValidation::AuditStaticMeshInputs(UStaticMesh* Mesh, const FString& ModelSource)
{
    const auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    UEFModelReader Model(ModelSource);
    if (!Mesh || !Model.Read() || Model.LODs.Num() != 1) return SerializeAudit(Report);
    UStaticMesh* Assets[] = {Mesh};
    FStaticMeshCompilingManager::Get().FinishCompilation(Assets);
    const FLODData& Source = Model.LODs[0];
    const FMeshDescription* Description = Mesh->GetMeshDescription(0);
    const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
    if (!Description || !RenderData || RenderData->LODResources.Num() != 1 || Mesh->GetNumSourceModels() != 1) return SerializeAudit(Report);
    const FStaticMeshConstAttributes Attributes(*Description);
    const auto Positions = Attributes.GetVertexPositions();
    const auto Normals = Attributes.GetVertexInstanceNormals();
    const auto Tangents = Attributes.GetVertexInstanceTangents();
    const auto Signs = Attributes.GetVertexInstanceBinormalSigns();
    const auto Colors = Attributes.GetVertexInstanceColors();
    const auto UVs = Attributes.GetVertexInstanceUVs();
    const FStaticMeshLODResources& LOD = RenderData->LODResources[0];
    const auto& BuiltUVs = LOD.VertexBuffers.StaticMeshVertexBuffer;
    const bool bCounts = Description->Vertices().Num() == Source.Vertices.Num()
        && Source.Normals.Num() == Source.Vertices.Num() && Source.Tangents.Num() == Source.Vertices.Num()
        && Source.Indices.Num() % 3 == 0 && !Source.Materials.IsEmpty()
        && (Source.VertexColors.IsEmpty() || (Source.VertexColors.Num() == 1
            && Source.VertexColors[0].Data.Num() == Source.Vertices.Num()))
        && UVs.GetNumChannels() == Source.TextureCoordinates.Num()
        && BuiltUVs.GetNumTexCoords() == Source.TextureCoordinates.Num();
    if (!bCounts) return SerializeAudit(Report);
    for (const auto& Channel : Source.TextureCoordinates)
        if (Channel.Num() != Source.Vertices.Num()) return SerializeAudit(Report);
    for (int32 Index : Source.Indices)
        if (!Source.Vertices.IsValidIndex(Index)) return SerializeAudit(Report);
    const FMeshBuildSettings& Settings = Mesh->GetSourceModel(0).BuildSettings;
    if (Settings.BuildScale3D != FVector(1.0) || !BuiltUVs.GetUseHighPrecisionTangentBasis())
    {
        Report->SetStringField(TEXT("error"), TEXT("The source-preserving audit requires unit build scale and native 16-bit tangents"));
        return SerializeAudit(Report);
    }
    double PositionError = 0, UVError = 0, NormalError = 0, TangentError = 0, SignError = 0, ColorError = 0;
    for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
        PositionError = FMath::Max(PositionError, static_cast<double>((Positions[Vertex] - Source.Vertices[Vertex.GetValue()]).Size()));
    for (const FVertexInstanceID Instance : Description->VertexInstances().GetElementIDs())
    {
        const int32 Vertex = Description->GetVertexInstanceVertex(Instance).GetValue();
        const FVector4f& Normal = Source.Normals[Vertex];
        NormalError = FMath::Max(NormalError, static_cast<double>((Normals[Instance] - FVector3f(Normal.Y, Normal.Z, Normal.W)).Size()));
        TangentError = FMath::Max(TangentError, static_cast<double>((Tangents[Instance] - Source.Tangents[Vertex]).Size()));
        SignError = FMath::Max(SignError, static_cast<double>(FMath::Abs(Signs[Instance] - Normal.X)));
        const FColor Color = Source.VertexColors.IsEmpty() ? FColor::White : Source.VertexColors[0].Data[Vertex];
        // StaticMeshBuilder encodes linear mesh-description colors with ToFColor(true).
        ColorError = FMath::Max(ColorError, static_cast<double>((Colors[Instance] - FVector4f(FLinearColor(Color))).Size()));
        for (int32 Channel = 0; Channel < Source.TextureCoordinates.Num(); ++Channel)
            UVError = FMath::Max(UVError, static_cast<double>((UVs.Get(Instance, Channel) - Source.TextureCoordinates[Channel][Vertex]).Size()));
    }
    TMap<FVector3f, TArray<int32>> ByPosition;
    TArray<FVector4f> ExpectedNormals;
    TArray<FVector3f> ExpectedTangents;
    TArray<FColor> ExpectedColors;
    for (int32 Index = 0; Index < Source.Vertices.Num(); ++Index)
    {
        ByPosition.FindOrAdd(Source.Vertices[Index]).Add(Index);
        const FVector4f& Normal = Source.Normals[Index];
        const FVector3f UnitNormal = FVector3f(Normal.Y, Normal.Z, Normal.W).GetSafeNormal();
        ExpectedNormals.Add(FPackedRGBA16N(FVector4f(UnitNormal, Normal.X)).ToFVector4f());
        ExpectedTangents.Add(FPackedRGBA16N(Source.Tangents[Index].GetSafeNormal()).ToFVector3f());
        ExpectedColors.Add(Source.VertexColors.IsEmpty() ? FColor::White : Source.VertexColors[0].Data[Index]);
    }
    // Welding and cache optimization may renumber identical vertices. Compare
    // oriented triangle multisets using their complete native vertex attributes.
    TArray<int32> Representatives;
    for (int32 Index = 0; Index < Source.Vertices.Num(); ++Index)
    {
        int32 Representative = Index;
        for (int32 Candidate : ByPosition.FindChecked(Source.Vertices[Index]))
        {
            bool bEqual = ExpectedNormals[Candidate] == ExpectedNormals[Index]
                && ExpectedTangents[Candidate] == ExpectedTangents[Index] && ExpectedColors[Candidate] == ExpectedColors[Index];
            for (int32 Channel = 0; Channel < Source.TextureCoordinates.Num(); ++Channel)
                bEqual &= Source.TextureCoordinates[Channel][Candidate] == Source.TextureCoordinates[Channel][Index];
            if (bEqual) { Representative = Candidate; break; }
        }
        Representatives.Add(Representative);
    }
    int32 BuiltMismatches = 0, BuiltBasisMismatches = 0, BuiltColorMismatches = 0, BuiltAttributeMismatches = 0;
    double BuiltNormalError = 0, BuiltTangentError = 0;
    TArray<int32> BuiltToSource;
    for (uint32 Index = 0; Index < LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices(); ++Index)
    {
        const auto* Candidates = ByPosition.Find(LOD.VertexBuffers.PositionVertexBuffer.VertexPosition(Index));
        const FVector4f BuiltNormal = BuiltUVs.VertexTangentZ(Index);
        const FVector3f BuiltTangent(BuiltUVs.VertexTangentX(Index));
        const FColor BuiltColor = LOD.VertexBuffers.ColorVertexBuffer.GetNumVertices()
            ? LOD.VertexBuffers.ColorVertexBuffer.VertexColor(Index) : FColor::White;
        bool bMatched = false, bBasisMatched = false, bColorMatched = false;
        int32 MatchedSource = INDEX_NONE;
        double MinNormalError = TNumericLimits<double>::Max(), MinTangentError = TNumericLimits<double>::Max();
        if (Candidates) for (int32 Candidate : *Candidates)
        {
            bool bEqual = true;
            for (int32 Channel = 0; Channel < Source.TextureCoordinates.Num(); ++Channel)
                bEqual &= BuiltUVs.GetVertexUV(Index, Channel) == Source.TextureCoordinates[Channel][Candidate];
            bMatched |= bEqual;
            if (!bEqual) continue;
            const bool bBasis = BuiltNormal == ExpectedNormals[Candidate] && BuiltTangent == ExpectedTangents[Candidate];
            const bool bColor = BuiltColor == ExpectedColors[Candidate];
            bBasisMatched |= bBasis;
            bColorMatched |= bColor;
            MinNormalError = FMath::Min(MinNormalError, static_cast<double>((BuiltNormal - ExpectedNormals[Candidate]).Size()));
            MinTangentError = FMath::Min(MinTangentError, static_cast<double>((BuiltTangent - ExpectedTangents[Candidate]).Size()));
            if (bBasis && bColor) MatchedSource = Representatives[Candidate];
        }
        if (!bMatched) ++BuiltMismatches;
        if (!bBasisMatched) ++BuiltBasisMismatches;
        if (!bColorMatched) ++BuiltColorMismatches;
        if (MatchedSource == INDEX_NONE) ++BuiltAttributeMismatches;
        if (bMatched)
        {
            BuiltNormalError = FMath::Max(BuiltNormalError, MinNormalError);
            BuiltTangentError = FMath::Max(BuiltTangentError, MinTangentError);
        }
        BuiltToSource.Add(MatchedSource);
    }
    const auto TriangleKey = [](int32 A, int32 B, int32 C)
    {
        const FIntVector Keys[] = {FIntVector(A, B, C), FIntVector(B, C, A), FIntVector(C, A, B)};
        FIntVector Key = Keys[0];
        for (int32 Index = 1; Index < 3; ++Index)
            if (Keys[Index].X < Key.X || (Keys[Index].X == Key.X && (Keys[Index].Y < Key.Y
                || (Keys[Index].Y == Key.Y && Keys[Index].Z < Key.Z)))) Key = Keys[Index];
        return Key;
    };
    using FTriangleCounts = TMap<FIntVector, int32>;
    TArray<FTriangleCounts> SourceTriangles, DescriptionTriangles, BuiltTriangles;
    SourceTriangles.SetNum(Source.Materials.Num());
    DescriptionTriangles.SetNum(Source.Materials.Num());
    BuiltTriangles.SetNum(Source.Materials.Num());
    TArray<int32> TriangleMaterials;
    TriangleMaterials.Init(INDEX_NONE, Source.Indices.Num() / 3);
    int32 MaterialMismatches = FMath::Abs(Mesh->GetStaticMaterials().Num() - Source.Materials.Num());
    TArray<TSharedPtr<FJsonValue>> MaterialSlots;
    const auto SlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
    for (int32 Material = 0; Material < Source.Materials.Num(); ++Material)
    {
        const FMaterialChunk& Chunk = Source.Materials[Material];
        const FName SourceName(UTF8_TO_TCHAR(Chunk.Name.c_str()));
        if (!Mesh->GetStaticMaterials().IsValidIndex(Material)
            || Mesh->GetStaticMaterials()[Material].ImportedMaterialSlotName != SourceName
            || Mesh->GetStaticMaterials()[Material].MaterialSlotName != SourceName) ++MaterialMismatches;
        const FPolygonGroupID Group(Material);
        if (!Description->PolygonGroups().IsValid(Group)) return SerializeAudit(Report);
        if (SlotNames[Group] != SourceName) ++MaterialMismatches;
        const auto Slot = MakeShared<FJsonObject>();
        Slot->SetStringField(TEXT("source_name"), SourceName.ToString());
        Slot->SetStringField(TEXT("source_path"), UTF8_TO_TCHAR(Chunk.Path.c_str()));
        Slot->SetStringField(TEXT("polygon_group_name"), SlotNames[Group].ToString());
        if (Mesh->GetStaticMaterials().IsValidIndex(Material))
        {
            Slot->SetStringField(TEXT("slot_name"), Mesh->GetStaticMaterials()[Material].MaterialSlotName.ToString());
            Slot->SetStringField(TEXT("imported_slot_name"), Mesh->GetStaticMaterials()[Material].ImportedMaterialSlotName.ToString());
        }
        MaterialSlots.Add(MakeShared<FJsonValueObject>(Slot));
        if (Chunk.FirstIndex < 0 || Chunk.FirstIndex % 3 || Chunk.NumFaces < 0
            || Chunk.FirstIndex / 3 + Chunk.NumFaces > TriangleMaterials.Num()) return SerializeAudit(Report);
        for (int32 Face = Chunk.FirstIndex / 3; Face < Chunk.FirstIndex / 3 + Chunk.NumFaces; ++Face)
        {
            if (TriangleMaterials[Face] != INDEX_NONE) return SerializeAudit(Report);
            TriangleMaterials[Face] = Material;
            ++SourceTriangles[Material].FindOrAdd(TriangleKey(Representatives[Source.Indices[Face * 3]],
                Representatives[Source.Indices[Face * 3 + 1]], Representatives[Source.Indices[Face * 3 + 2]]));
        }
    }
    if (TriangleMaterials.Contains(INDEX_NONE)) return SerializeAudit(Report);
    int32 InvalidDescriptionTriangles = 0, InvalidBuiltTriangles = 0;
    for (const FTriangleID Triangle : Description->Triangles().GetElementIDs())
    {
        const FPolygonGroupID Group = Description->GetTrianglePolygonGroup(Triangle);
        const int32 Material = Group.GetValue();
        if (!DescriptionTriangles.IsValidIndex(Material)) { ++InvalidDescriptionTriangles; continue; }
        const auto Vertices = Description->GetTriangleVertices(Triangle);
        ++DescriptionTriangles[Material].FindOrAdd(TriangleKey(Representatives[Vertices[0].GetValue()],
            Representatives[Vertices[1].GetValue()], Representatives[Vertices[2].GetValue()]));
    }
    const auto Indices = LOD.IndexBuffer.GetArrayView();
    int32 BuiltTriangleCount = 0;
    for (const FStaticMeshSection& Section : LOD.Sections)
        for (uint32 Face = 0; Face < Section.NumTriangles; ++Face)
        {
            ++BuiltTriangleCount;
            const uint32 First = Section.FirstIndex + Face * 3;
            if (!BuiltTriangles.IsValidIndex(Section.MaterialIndex) || First + 2 >= static_cast<uint32>(Indices.Num()))
            { ++InvalidBuiltTriangles; continue; }
            const int32 A = BuiltToSource.IsValidIndex(Indices[First]) ? BuiltToSource[Indices[First]] : INDEX_NONE;
            const int32 B = BuiltToSource.IsValidIndex(Indices[First + 1]) ? BuiltToSource[Indices[First + 1]] : INDEX_NONE;
            const int32 C = BuiltToSource.IsValidIndex(Indices[First + 2]) ? BuiltToSource[Indices[First + 2]] : INDEX_NONE;
            if (A == INDEX_NONE || B == INDEX_NONE || C == INDEX_NONE) { ++InvalidBuiltTriangles; continue; }
            ++BuiltTriangles[Section.MaterialIndex].FindOrAdd(TriangleKey(A, B, C));
        }
    const auto TriangleMismatches = [&SourceTriangles](const TArray<FTriangleCounts>& Compared)
    {
        int32 Count = 0;
        for (int32 Material = 0; Material < SourceTriangles.Num(); ++Material)
        {
            for (const auto& Pair : SourceTriangles[Material])
                Count += FMath::Abs(Pair.Value - Compared[Material].FindRef(Pair.Key));
            for (const auto& Pair : Compared[Material])
                if (!SourceTriangles[Material].Contains(Pair.Key)) Count += Pair.Value;
        }
        return Count;
    };
    const int32 DescriptionTopologyMismatches = InvalidDescriptionTriangles + TriangleMismatches(DescriptionTriangles);
    const int32 BuiltTopologyMismatches = InvalidBuiltTriangles + TriangleMismatches(BuiltTriangles);
    Report->SetNumberField(TEXT("source_vertices"), Source.Vertices.Num());
    Report->SetNumberField(TEXT("render_vertices"), LOD.VertexBuffers.PositionVertexBuffer.GetNumVertices());
    Report->SetNumberField(TEXT("uv_channels"), Source.TextureCoordinates.Num());
    Report->SetNumberField(TEXT("native_lods"), Mesh->GetNumLODs());
    Report->SetBoolField(TEXT("full_precision_uvs"), Settings.bUseFullPrecisionUVs);
    Report->SetBoolField(TEXT("generate_lightmap_uvs"), Settings.bGenerateLightmapUVs);
    Report->SetBoolField(TEXT("recompute_normals"), Settings.bRecomputeNormals);
    Report->SetBoolField(TEXT("recompute_tangents"), Settings.bRecomputeTangents);
    Report->SetBoolField(TEXT("remove_degenerates"), Settings.bRemoveDegenerates);
    Report->SetBoolField(TEXT("high_precision_tangent_basis"), Settings.bUseHighPrecisionTangentBasis);
    Report->SetNumberField(TEXT("position_max_error"), PositionError);
    Report->SetNumberField(TEXT("source_uv_max_error"), UVError);
    Report->SetNumberField(TEXT("source_normal_max_error"), NormalError);
    Report->SetNumberField(TEXT("source_tangent_max_error"), TangentError);
    Report->SetNumberField(TEXT("source_binormal_sign_max_error"), SignError);
    Report->SetNumberField(TEXT("source_color_max_error"), ColorError);
    Report->SetNumberField(TEXT("source_color_sets"), Source.VertexColors.Num());
    Report->SetNumberField(TEXT("built_position_uv_mismatches"), BuiltMismatches);
    Report->SetNumberField(TEXT("built_tangent_basis_mismatches"), BuiltBasisMismatches);
    Report->SetNumberField(TEXT("built_color_mismatches"), BuiltColorMismatches);
    Report->SetNumberField(TEXT("built_attribute_mismatches"), BuiltAttributeMismatches);
    Report->SetNumberField(TEXT("built_packed_normal_max_error"), BuiltNormalError);
    Report->SetNumberField(TEXT("built_packed_tangent_max_error"), BuiltTangentError);
    Report->SetNumberField(TEXT("source_triangles"), Source.Indices.Num() / 3);
    Report->SetNumberField(TEXT("render_triangles"), BuiltTriangleCount);
    Report->SetNumberField(TEXT("source_topology_mismatches"), DescriptionTopologyMismatches);
    Report->SetNumberField(TEXT("built_topology_mismatches"), BuiltTopologyMismatches);
    Report->SetNumberField(TEXT("material_slot_mismatches"), MaterialMismatches);
    Report->SetArrayField(TEXT("material_slots"), MaterialSlots);
    Report->SetBoolField(TEXT("passed"), PositionError == 0 && UVError == 0 && NormalError == 0 && TangentError == 0
        && SignError == 0 && ColorError == 0 && BuiltAttributeMismatches == 0
        && DescriptionTopologyMismatches == 0 && BuiltTopologyMismatches == 0 && MaterialMismatches == 0);
    return SerializeAudit(Report);
}

FString UCharacterInputValidation::AuditTextureMipChain(UTexture* Texture, const FString& DDSSource)
{
    const auto Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("passed"), false);
    TArray<uint8> DDS;
    if (!Texture || !FFileHelper::LoadFileToArray(DDS, *DDSSource) || DDS.Num() < 148
        || FMemory::Memcmp(DDS.GetData(), "DDS ", 4) != 0) return SerializeAudit(Report);
    auto Header = [&DDS](int32 Offset) { uint32 Value; FMemory::Memcpy(&Value, DDS.GetData() + Offset, 4); return Value; };
    const uint32 Width = Header(16), Height = Header(12), Mips = Header(28), Format = Header(128);
    const uint32 BytesPerPixel = Format == 10 ? 8 : 4;
    const bool bCube = (Header(136) & 4) != 0;
    const uint32 Slices = bCube ? 6 : 1;
    UTexture2D* Texture2D = Cast<UTexture2D>(Texture);
    UTextureCube* TextureCube = Cast<UTextureCube>(Texture);
    if (Header(4) != 124 || Header(84) != 0x30315844 || Header(132) != 3 || Header(140) != 1
        || !Width || !Height || !Mips || Mips > 32 || (Format != 87 && Format != 91 && Format != 10)) return SerializeAudit(Report);
    UTexture* Assets[] = {Texture};
    // Cube imports can have an allocated but empty platform-data object. The
    // normal resource update starts caching; FinishCache alone only waits.
    const FTexturePlatformData* Existing = Texture2D ? Texture2D->GetPlatformData() : TextureCube ? TextureCube->GetPlatformData() : nullptr;
    const UTexture::EUpdateResourceFlags Flags = !Existing || Existing->Mips.IsEmpty()
        ? static_cast<UTexture::EUpdateResourceFlags>(static_cast<uint32>(UTexture::EUpdateResourceFlags::Synchronous)
            | static_cast<uint32>(UTexture::EUpdateResourceFlags::ForceRebuild))
        : UTexture::EUpdateResourceFlags::Synchronous;
    Texture->UpdateResourceWithParams(Flags);
    Texture->FinishCachePlatformData();
    FTextureCompilingManager::Get().FinishCompilation(Assets);
    FTexturePlatformData* Platform = Texture2D ? Texture2D->GetPlatformData() : TextureCube ? TextureCube->GetPlatformData() : nullptr;
    const bool bShape = Platform && Platform->Mips.Num() == Mips && Texture->Source.GetNumMips() == Mips
        && Texture->Source.GetSizeX() == Width && Texture->Source.GetSizeY() == Height
        && Texture->Source.GetNumSlices() == Slices && bCube == (TextureCube != nullptr)
        && Platform->PixelFormat == (Format == 10 ? PF_FloatRGBA : PF_B8G8R8A8);
    Report->SetNumberField(TEXT("slices"), Slices);
    Report->SetBoolField(TEXT("source_valid"), Texture->Source.IsValid());
    Report->SetBoolField(TEXT("can_render"), FApp::CanEverRender());
    Report->SetNumberField(TEXT("source_width"), Texture->Source.GetSizeX());
    Report->SetNumberField(TEXT("source_height"), Texture->Source.GetSizeY());
    Report->SetNumberField(TEXT("source_slices"), Texture->Source.GetNumSlices());
    Report->SetNumberField(TEXT("source_format"), Texture->Source.GetFormat());
    Report->SetNumberField(TEXT("expected_mips"), Mips);
    Report->SetNumberField(TEXT("source_mips"), Texture->Source.GetNumMips());
    Report->SetNumberField(TEXT("platform_mips"), Platform ? Platform->Mips.Num() : 0);
    Report->SetNumberField(TEXT("platform_format"), Platform ? static_cast<int32>(Platform->PixelFormat) : -1);
    if (!bShape) return SerializeAudit(Report);
    TArray<void*> Built;
    Built.SetNumZeroed(Mips);
    if (Texture2D) Texture2D->GetMipData(0, Built.GetData());
    else TextureCube->GetMipData(0, Built.GetData());
    int64 FaceBytes = 0;
    for (uint32 Mip = 0; Mip < Mips; ++Mip)
        FaceBytes += static_cast<int64>(FMath::Max(Width >> Mip, 1u)) * FMath::Max(Height >> Mip, 1u) * BytesPerPixel;
    int64 Offset = 148, Compared = 0;
    int32 SourceEqual = 0, PlatformEqual = 0;
    for (uint32 Mip = 0; Mip < Mips; ++Mip)
    {
        const uint32 X = FMath::Max(Width >> Mip, 1u), Y = FMath::Max(Height >> Mip, 1u);
        const int64 SliceSize = static_cast<int64>(X) * Y * BytesPerPixel;
        const int64 Size = SliceSize * Slices;
        TArray64<uint8> Source;
        TArray64<uint8> Expected;
        for (uint32 Slice = 0; Slice < Slices; ++Slice)
        {
            const int64 SliceOffset = Offset + Slice * FaceBytes;
            if (SliceOffset + SliceSize <= DDS.Num()) Expected.Append(DDS.GetData() + SliceOffset, SliceSize);
        }
        if (Expected.Num() == Size)
        {
            SourceEqual += Texture->Source.GetMipData(Source, Mip) && Source.Num() == Size
                && FMemory::Memcmp(Source.GetData(), Expected.GetData(), Size) == 0;
            PlatformEqual += Built[Mip] && Platform->Mips[Mip].SizeX == X && Platform->Mips[Mip].SizeY == Y
                && FMemory::Memcmp(Built[Mip], Expected.GetData(), Size) == 0;
            Compared += Size;
        }
        FMemory::Free(Built[Mip]);
        Offset += SliceSize;
    }
    Report->SetNumberField(TEXT("source_equal_mips"), SourceEqual);
    Report->SetNumberField(TEXT("platform_equal_mips"), PlatformEqual);
    Report->SetNumberField(TEXT("bytes_compared"), Compared);
    Report->SetBoolField(TEXT("passed"), SourceEqual == Mips && PlatformEqual == Mips && 148 + FaceBytes * Slices == DDS.Num());
    return SerializeAudit(Report);
}
