#include "ShowcasePropAuthoring.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_AddComponent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Self.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString UShowcasePropAuthoring::AuditNotifyTemplate(UBlueprint* Blueprint)
{
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Templates;
    if (Blueprint) for (UActorComponent* Template:Blueprint->ComponentTemplates)
    {
        auto Row=MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("path"),GetPathNameSafe(Template));
        Row->SetStringField(TEXT("class"),Template ? Template->GetClass()->GetPathName() : FString());
        Templates.Add(MakeShared<FJsonValueObject>(Row));
    }
    Report->SetArrayField(TEXT("blueprint_templates"),Templates);
    Templates.Reset();
    if (auto* Class=Blueprint ? Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass) : nullptr)
        for (const auto& Template:Class->ComponentTemplates)
            Templates.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Template)));
    Report->SetArrayField(TEXT("generated_templates"),Templates);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return Json;
}

FString UShowcasePropAuthoring::ExerciseNotifyLifecycle(UBlueprint* Blueprint,USkeletalMeshComponent* Parent,UAnimSequence* Animation)
{
    TSharedRef<FJsonObject> Report=MakeShared<FJsonObject>();
    auto* Notify=Blueprint && Blueprint->GeneratedClass ? Cast<UAnimNotifyState>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
    if (Notify && Parent && Animation && Parent->GetOwner())
    {
        auto Collect=[Parent]()
        {
            TArray<USkeletalMeshComponent*> Components;
            Parent->GetOwner()->GetComponents(Components);
            Components.RemoveAll([](auto* C){return !C->ComponentHasTag(TEXT("ShowcaseProp"));});
            return Components;
        };
        const int32 Before=Collect().Num();
        FAnimNotifyEventReference Reference;
        Notify->NotifyBegin(Parent,Animation,.5f,Reference);
        auto Props=Collect();
        Report->SetNumberField(TEXT("spawned"),Props.Num()-Before);
        bool bAttached=true;
        for (auto* Prop:Props) bAttached&=Prop->GetAttachParent()==Parent && Prop->GetSkeletalMeshAsset()!=nullptr;
        Report->SetBoolField(TEXT("attached"),bAttached && Props.Num()>Before);
        Notify->NotifyTick(Parent,Animation,.1f,Reference);
        Notify->NotifyEnd(Parent,Animation,Reference);
        Report->SetNumberField(TEXT("remaining"),Collect().Num()-Before);
    }
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return Json;
}

namespace
{
struct FNotifyGraph
{
    UEdGraph* Graph;
    UK2Node_FunctionEntry* Entry;
    UK2Node_FunctionResult* Result;
    const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
    int32 Column = 0;

    FNotifyGraph(UBlueprint* BP, FName Function)
    {
        Graph = FBlueprintEditorUtils::CreateNewGraph(BP, Function, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
        FBlueprintEditorUtils::AddFunctionGraph(BP, Graph, false, UAnimNotifyState::StaticClass());
        Entry = nullptr;
        Result = nullptr;
        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (auto* Typed = Cast<UK2Node_FunctionEntry>(Node)) Entry = Typed;
            if (auto* Typed = Cast<UK2Node_FunctionResult>(Node)) Result = Typed;
        }
        check(Entry && Result);
        Entry->NodePosX = -400;
        Result->NodePosX = 2500;
        Result->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->DefaultValue = TEXT("true");
        Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then)->BreakAllPinLinks();
    }

    template<class T> T* Node()
    {
        T* Value = NewObject<T>(Graph);
        Graph->AddNode(Value, false, false);
        Value->CreateNewGuid();
        Value->NodePosX = Column++ * 240;
        Value->NodePosY = 120;
        return Value;
    }

    UK2Node_CallFunction* Call(UClass* Class, FName Function)
    {
        auto* Value = Node<UK2Node_CallFunction>();
        Value->SetFromFunction(Class->FindFunctionByName(Function));
        Value->AllocateDefaultPins();
        return Value;
    }

    void Link(UEdGraphPin* A, UEdGraphPin* B)
    {
        checkf(A && B && Schema->TryCreateConnection(A, B), TEXT("Cannot wire %s to %s"),
            A ? *A->GetName() : TEXT("null"), B ? *B->GetName() : TEXT("null"));
    }

    UEdGraphPin* Owner()
    {
        auto* Get = Call(UActorComponent::StaticClass(), TEXT("GetOwner"));
        Link(Entry->FindPinChecked(TEXT("MeshComp")), Get->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        return Get->GetReturnValuePin();
    }

    UK2Node_DynamicCast* CastMesh(UEdGraphPin* Component,UEdGraphPin* Exec,UEdGraphPin* Failure)
    {
        auto* Cast = Node<UK2Node_DynamicCast>();
        Cast->TargetType = USkeletalMeshComponent::StaticClass();
        Cast->SetPurity(false);
        Cast->AllocateDefaultPins();
        Link(Component, Cast->GetCastSourcePin());
        Link(Exec,Cast->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
        Link(Cast->GetInvalidCastPin(),Failure);
        return Cast;
    }

    void Position(UEdGraphPin* Mesh, UEdGraphPin* Exec, UEdGraphPin* End, float Begin, float LoopLength)
    {
        auto* Get = Call(USkeletalMeshComponent::StaticClass(), GET_FUNCTION_NAME_CHECKED(USkeletalMeshComponent, GetPosition));
        Link(Entry->FindPinChecked(TEXT("MeshComp")), Get->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        auto* Subtract = Call(UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Subtract_DoubleDouble));
        Link(Get->GetReturnValuePin(), Subtract->FindPinChecked(TEXT("A")));
        Subtract->FindPinChecked(TEXT("B"))->DefaultValue = FString::SanitizeFloat(Begin);
        UEdGraphPin* Time = Subtract->GetReturnValuePin();
        if (LoopLength > 0.f)
        {
            auto* Modulo = Call(UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, Percent_FloatFloat));
            Link(Time, Modulo->FindPinChecked(TEXT("A")));
            Modulo->FindPinChecked(TEXT("B"))->DefaultValue = FString::SanitizeFloat(LoopLength);
            Time = Modulo->GetReturnValuePin();
        }
        auto* Set = Call(USkeletalMeshComponent::StaticClass(), GET_FUNCTION_NAME_CHECKED(USkeletalMeshComponent, SetPosition));
        Link(Mesh, Set->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        Link(Time, Set->FindPinChecked(TEXT("InPos")));
        Set->FindPinChecked(TEXT("bFireNotifies"))->DefaultValue = TEXT("false");
        Link(Exec, Set->GetExecPin());
        Link(Set->GetThenPin(), End);
    }
};
}

UBlueprint* UShowcasePropAuthoring::AuthorNotify(const FString& AssetPath, USkeletalMesh* Mesh,
    UAnimSequence* PropAnimation, const FString& Tag, float SourceBegin, bool bLooping,
    FName Socket, const FTransform& RelativeTransform)
{
    if (!Mesh || !PropAnimation || Mesh->GetSkeleton() != PropAnimation->GetSkeleton()) return nullptr;
    UPackage* Package = CreatePackage(*AssetPath);
    const FName Name(*FPackageName::GetLongPackageAssetName(AssetPath));
    UBlueprint* BP = FindObject<UBlueprint>(Package, *Name.ToString());
    if (!BP && FPackageName::DoesPackageExist(AssetPath)) BP = LoadObject<UBlueprint>(nullptr, *AssetPath);
    if (!BP)
    {
        BP = FKismetEditorUtilities::CreateBlueprint(UAnimNotifyState::StaticClass(), Package, Name,
            BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
        FAssetRegistryModule::AssetCreated(BP);
    }
    if (!BP || BP->ParentClass != UAnimNotifyState::StaticClass()) return nullptr;
    BP->Modify();
    const auto OldGraphs = BP->FunctionGraphs;
    for (UEdGraph* Graph : OldGraphs)
        if (Graph->GetFName()==GET_FUNCTION_NAME_CHECKED(UAnimNotifyState,Received_NotifyBegin)
            || Graph->GetFName()==GET_FUNCTION_NAME_CHECKED(UAnimNotifyState,Received_NotifyTick)
            || Graph->GetFName()==GET_FUNCTION_NAME_CHECKED(UAnimNotifyState,Received_NotifyEnd))
            FBlueprintEditorUtils::RemoveGraph(BP,Graph);
    const FName TemplateName(TEXT("OriginalShowcaseProp"));
    USkeletalMeshComponent* Template = NewObject<USkeletalMeshComponent>(BP->GeneratedClass, TemplateName, RF_ArchetypeObject | RF_Public);
    Template->SetSkeletalMeshAsset(Mesh);
    Template->SetAnimationMode(EAnimationMode::AnimationSingleNode);
    Template->AnimationData.AnimToPlay = PropAnimation;
    Template->AnimationData.bSavedLooping = bLooping;
    Template->AnimationData.bSavedPlaying = false;
    Template->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Template->SkinCacheUsage = {ESkinCacheUsage::Disabled};
    Template->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Template->SetGenerateOverlapEvents(false);
    Template->SetRenderInMainPass(true);
    Template->SetRenderInDepthPass(true);
    Template->ComponentTags = {FName(TEXT("ShowcaseProp")), FName(*Tag)};
    BP->ComponentTemplates.Add(Template);
    {
        FNotifyGraph G(BP, GET_FUNCTION_NAME_CHECKED(UAnimNotifyState, Received_NotifyBegin));
        // Native AddComponent nodes retain their templates across Blueprint
        // compilation. A plain CallFunction node is pruned by UE's template audit.
        auto* Add = G.Node<UK2Node_AddComponent>();
        Add->TemplateType=USkeletalMeshComponent::StaticClass();
        Add->SetFromFunction(AActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AActor,AddComponent)));
        Add->AllocateDefaultPinsWithoutExposedVariables();
        G.Link(G.Owner(), Add->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        Add->FindPinChecked(TEXT("TemplateName"))->DefaultValue = TemplateName.ToString();
        Add->FindPinChecked(TEXT("bManualAttachment"))->DefaultValue = TEXT("true");
        Add->FindPinChecked(TEXT("bDeferredFinish"))->DefaultValue = TEXT("false");
        auto* Transform = G.Call(UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeTransform));
        const FVector Location = RelativeTransform.GetLocation();
        const FVector Scale = RelativeTransform.GetScale3D();
        Transform->FindPinChecked(TEXT("Location"))->DefaultValue = FString::Printf(TEXT("%f,%f,%f"), Location.X, Location.Y, Location.Z);
        Transform->FindPinChecked(TEXT("Scale"))->DefaultValue = FString::Printf(TEXT("%f,%f,%f"), Scale.X, Scale.Y, Scale.Z);
        auto* Rotation = G.Call(UKismetMathLibrary::StaticClass(), GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeRotator));
        const FRotator Rotator = RelativeTransform.Rotator();
        Rotation->FindPinChecked(TEXT("Roll"))->DefaultValue = FString::SanitizeFloat(Rotator.Roll);
        Rotation->FindPinChecked(TEXT("Pitch"))->DefaultValue = FString::SanitizeFloat(Rotator.Pitch);
        Rotation->FindPinChecked(TEXT("Yaw"))->DefaultValue = FString::SanitizeFloat(Rotator.Yaw);
        G.Link(Rotation->GetReturnValuePin(), Transform->FindPinChecked(TEXT("Rotation")));
        G.Link(Transform->GetReturnValuePin(), Add->FindPinChecked(TEXT("RelativeTransform")));
        auto* Self = G.Node<UK2Node_Self>();
        Self->AllocateDefaultPins();
        G.Link(Self->FindPinChecked(UEdGraphSchema_K2::PN_Self), Add->FindPinChecked(TEXT("ComponentTemplateContext")));
        G.Link(G.Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then), Add->GetExecPin());
        auto* NewMesh=G.CastMesh(Add->GetReturnValuePin(),Add->GetThenPin(),G.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
        auto* Attach = G.Call(USceneComponent::StaticClass(), GET_FUNCTION_NAME_CHECKED(USceneComponent, K2_AttachToComponent));
        G.Link(NewMesh->GetCastResultPin(), Attach->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        G.Link(G.Entry->FindPinChecked(TEXT("MeshComp")), Attach->FindPinChecked(TEXT("Parent")));
        Attach->FindPinChecked(TEXT("SocketName"))->DefaultValue = Socket.ToString();
        for (FName Rule : {FName(TEXT("LocationRule")), FName(TEXT("RotationRule")), FName(TEXT("ScaleRule"))})
            Attach->FindPinChecked(Rule)->DefaultValue = TEXT("KeepRelative");
        G.Link(NewMesh->GetValidCastPin(), Attach->GetExecPin());
        G.Position(NewMesh->GetCastResultPin(), Attach->GetThenPin(), G.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute), SourceBegin,
            bLooping ? PropAnimation->GetPlayLength() : 0.f);
    }
    UBlueprint* Macros = LoadObject<UBlueprint>(nullptr, TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros"));
    UEdGraph* LoopGraph = nullptr;
    if (Macros) for (UEdGraph* Graph : Macros->MacroGraphs)
        if (Graph->GetFName() == TEXT("ForEachLoop")) LoopGraph = Graph;
    if (!LoopGraph) return nullptr;
    for (bool bEnd : {false, true})
    {
        FNotifyGraph G(BP, bEnd ? GET_FUNCTION_NAME_CHECKED(UAnimNotifyState, Received_NotifyEnd)
            : GET_FUNCTION_NAME_CHECKED(UAnimNotifyState, Received_NotifyTick));
        auto* Get = G.Call(AActor::StaticClass(), GET_FUNCTION_NAME_CHECKED(AActor, GetComponentsByTag));
        G.Link(G.Owner(), Get->FindPinChecked(UEdGraphSchema_K2::PN_Self));
        Get->FindPinChecked(TEXT("ComponentClass"))->DefaultObject = USkeletalMeshComponent::StaticClass();
        Get->FindPinChecked(TEXT("Tag"))->DefaultValue = Tag;
        auto* Loop = G.Node<UK2Node_MacroInstance>();
        Loop->SetMacroGraph(LoopGraph);
        Loop->AllocateDefaultPins();
        G.Link(Get->GetReturnValuePin(), Loop->FindPinChecked(TEXT("Array")));
        G.Link(G.Entry->FindPinChecked(UEdGraphSchema_K2::PN_Then), Loop->FindPinChecked(TEXT("Exec")));
        G.Link(Loop->FindPinChecked(TEXT("Completed")), G.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
        if (bEnd)
        {
            auto* Destroy = G.Call(UActorComponent::StaticClass(), GET_FUNCTION_NAME_CHECKED(UActorComponent, K2_DestroyComponent));
            G.Link(Loop->FindPinChecked(TEXT("Array Element")), Destroy->FindPinChecked(UEdGraphSchema_K2::PN_Self));
            G.Link(G.Owner(), Destroy->FindPinChecked(TEXT("Object")));
            G.Link(Loop->FindPinChecked(TEXT("LoopBody")), Destroy->GetExecPin());
        }
        else
        {
            // The source effect uses SyncEventTimeToEffectTime. Evaluate the prop from the
            // owning character's native animation position, including pause and seek.
            auto* Prop=G.CastMesh(Loop->FindPinChecked(TEXT("Array Element")),Loop->FindPinChecked(TEXT("LoopBody")),
                G.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute));
            G.Position(Prop->GetCastResultPin(), Prop->GetValidCastPin(),
                G.Result->FindPinChecked(UEdGraphSchema_K2::PN_Execute), SourceBegin,
                bLooping ? PropAnimation->GetPlayLength() : 0.f);
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);
    BP->MarkPackageDirty();
    return BP->Status == BS_Error ? nullptr : BP;
}

bool UShowcasePropAuthoring::ConfigureNotifies(UAnimSequence* Animation, const FString& SourceJSON, bool bVerifyOnly)
{
    TArray<TSharedPtr<FJsonValue>> Source;
    if (!Animation || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(SourceJSON), Source)) return false;
    const FName TrackName(TEXT("SourceProps"));
    if (!bVerifyOnly)
    {
        Animation->Modify();
        Animation->Notifies.RemoveAll([TrackName, Animation](const FAnimNotifyEvent& Event)
        { return Animation->AnimNotifyTracks.IsValidIndex(Event.TrackIndex) && Animation->AnimNotifyTracks[Event.TrackIndex].TrackName == TrackName; });
        int32 Track = Animation->AnimNotifyTracks.IndexOfByPredicate([TrackName](const FAnimNotifyTrack& Item) { return Item.TrackName == TrackName; });
        if (Track == INDEX_NONE) { Track = Animation->AnimNotifyTracks.AddDefaulted(); Animation->AnimNotifyTracks[Track].TrackName = TrackName; }
        for (const auto& Value : Source)
        {
            auto Item = Value->AsObject();
            UClass* Class = LoadClass<UAnimNotifyState>(nullptr, *Item->GetStringField(TEXT("class")));
            if (!Class) return false;
            FAnimNotifyEvent& Event = Animation->Notifies.AddDefaulted_GetRef();
            Event.NotifyStateClass = NewObject<UAnimNotifyState>(Animation, Class, NAME_None, RF_Transactional);
            Event.NotifyName = FName(*Item->GetStringField(TEXT("name")));
            Event.TrackIndex = Track;
            Event.Guid = FGuid::NewGuid();
            Event.Link(Animation, Item->GetNumberField(TEXT("time")));
            Event.SetDuration(Item->GetNumberField(TEXT("duration")));
            Event.TriggerTimeOffset = Item->GetNumberField(TEXT("trigger_offset"));
            Event.EndTriggerTimeOffset = Item->GetNumberField(TEXT("end_trigger_offset"));
            Event.TriggerWeightThreshold = Item->GetNumberField(TEXT("weight_threshold"));
        }
        Animation->RefreshCacheData();
        Animation->MarkPackageDirty();
    }
    for (const auto& Value : Source)
    {
        auto Item = Value->AsObject();
        const auto* Event = Animation->Notifies.FindByPredicate([&](const FAnimNotifyEvent& E)
        { return E.NotifyStateClass && E.NotifyStateClass->GetClass()->GetPathName() == Item->GetStringField(TEXT("class"))
            && FMath::IsNearlyEqual(E.GetTime(), static_cast<float>(Item->GetNumberField(TEXT("time"))))
            && FMath::IsNearlyEqual(E.GetDuration(), static_cast<float>(Item->GetNumberField(TEXT("duration")))); });
        if (!Event) return false;
    }
    return true;
}
