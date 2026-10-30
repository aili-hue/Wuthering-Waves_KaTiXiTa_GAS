#include "WuWaRenderingSubsystem.h"
#include "WuWaMaterialSettings.h"
#include "WuWaRenderingComponent.h"
#include "WuWaShowcaseStage.h"
#include "WuWaSurfaceRenderer.h"
#include "WuWaSelectionMeshComponent.h"

#include "Animation/Skeleton.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"
#include "UObject/GCObject.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogWuWaRendering,Log,All);

const UWuWaMaterialSettings* UWuWaMaterialSettings::Find(UMaterialInterface* Material)
{
    for (int32 Depth=0;Material && Depth<64;++Depth)
    {
        if (auto* Data=Cast<UWuWaMaterialSettings>(Material->GetAssetUserDataOfClass(StaticClass()))) return Data;
        auto* Instance=Cast<UMaterialInstance>(Material);
        Material=Instance ? Instance->Parent.Get() : nullptr;
    }
    return nullptr;
}

namespace
{
const FName NativeSectionsTag(TEXT("WuWaNativeSections"));

struct FMeshRendering
{
    TWeakObjectPtr<USkeletalMeshComponent> Mesh;
    TWeakObjectPtr<USkeletalMesh> Asset;
    TWeakObjectPtr<USkeletalMeshComponent> NativeSections;
    TWeakObjectPtr<UWuWaSelectionMeshComponent> SelectionMesh;
    TArray<TObjectPtr<UMaterialInterface>> Sources;
    TArray<TObjectPtr<UMaterialInterface>> OriginalOverrides;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> Instances;
    TArray<const UWuWaMaterialSettings*> Settings;
    TArray<ESkinCacheUsage> OriginalSkinCache;
    EVisibilityBasedAnimTickOption OriginalPoseTick=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    bool bOriginalMainPass=true;
    bool bOriginalDepthPass=false;
    bool bManaged=false;
    FString Warning;
};

UWuWaRenderingComponent* GetOverrides(USkeletalMeshComponent* Mesh)
{
    if (!Mesh || !Mesh->GetOwner()) return nullptr;
    TInlineComponentArray<UWuWaRenderingComponent*> Components(Mesh->GetOwner());
    for (auto* Component:Components)
        if (!Component->TargetMesh || Component->TargetMesh==Mesh) return Component;
    return nullptr;
}

bool HasCompatibleMaterial(USkeletalMeshComponent* Mesh)
{
    if (!Mesh->GetSkeletalMeshAsset()) return false;
    if (const auto* Override=GetOverrides(Mesh);Override && !Override->bEnableRendering) return false;
    for (int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        if (UWuWaMaterialSettings::Find(Mesh->GetMaterial(Slot))) return true;
    return false;
}

FVector AxisVector(EWuWaFacingAxis Axis,const FVector& Automatic)
{
    switch (Axis)
    {
    case EWuWaFacingAxis::PositiveX:return FVector::XAxisVector;
    case EWuWaFacingAxis::PositiveY:return FVector::YAxisVector;
    case EWuWaFacingAxis::PositiveZ:return FVector::ZAxisVector;
    case EWuWaFacingAxis::NegativeX:return -FVector::XAxisVector;
    case EWuWaFacingAxis::NegativeY:return -FVector::YAxisVector;
    case EWuWaFacingAxis::NegativeZ:return -FVector::ZAxisVector;
    default:return Automatic;
    }
}

FName FindHead(USkeletalMeshComponent* Mesh,FName Override)
{
    if (!Override.IsNone()) return Mesh->GetBoneIndex(Override)!=INDEX_NONE ? Override : NAME_None;
    for (const TCHAR* Name:{TEXT("Bip001Head"),TEXT("head"),TEXT("Head"),TEXT("J_Bip_C_Head"),TEXT("head_jnt"),TEXT("j_head")})
        if (Mesh->GetBoneIndex(Name)!=INDEX_NONE) return Name;
    return NAME_None;
}

void Restore(FMeshRendering& Item)
{
    if (auto* Selection=Item.SelectionMesh.Get()) Selection->DestroyComponent();
    Item.SelectionMesh.Reset();
    if (auto* Fallback=Item.NativeSections.Get()) Fallback->DestroyComponent();
    Item.NativeSections.Reset();
    auto* Mesh=Item.Mesh.Get();
    if (!Mesh || !Item.bManaged) return;
    // Never overwrite a material the user replaced while automatic rendering ran.
    for (int32 Index=0;Index<Item.Instances.Num();++Index)
        if (Item.Instances[Index] && Mesh->GetMaterial(Index)==Item.Instances[Index])
            Mesh->SetMaterial(Index,Item.OriginalOverrides.IsValidIndex(Index) ? Item.OriginalOverrides[Index] : nullptr);
    bool bEmptyTail=true;
    for (int32 Index=Item.OriginalOverrides.Num();Index<Mesh->OverrideMaterials.Num();++Index)
        bEmptyTail&=Mesh->OverrideMaterials[Index]==nullptr;
    if (bEmptyTail && Mesh->OverrideMaterials.Num()>Item.OriginalOverrides.Num())
        Mesh->OverrideMaterials.SetNum(Item.OriginalOverrides.Num());
    Mesh->SetRenderInMainPass(Item.bOriginalMainPass);
    Mesh->SetRenderInDepthPass(Item.bOriginalDepthPass);
    Mesh->VisibilityBasedAnimTickOption=Item.OriginalPoseTick;
    Mesh->SkinCacheUsage=Item.OriginalSkinCache;
    Mesh->MarkRenderStateDirty();
    Item.bManaged=false;
}

void BuildNativeSections(FMeshRendering& Item)
{
    auto* Mesh=Item.Mesh.Get();
    if (!Mesh || !Mesh->GetOwner()) return;
    const bool bMixed=Item.Settings.Contains(nullptr);
    if (!bMixed) return;
    auto* Native=NewObject<USkeletalMeshComponent>(Mesh->GetOwner(),NAME_None,RF_Transient|RF_DuplicateTransient);
    Native->ComponentTags.Add(NativeSectionsTag);
    Native->SetupAttachment(Mesh);
    Native->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Native->SetGenerateOverlapEvents(false);
    Native->SetSkeletalMeshAsset(Mesh->GetSkeletalMeshAsset());
    Native->SetLeaderPoseComponent(Mesh,true,false);
    Native->SetCastShadow(false);
    Native->bUseAttachParentBound=true;
    Native->SetComponentTickEnabled(false);
    for (int32 Index=0;Index<Item.Sources.Num();++Index) Native->SetMaterial(Index,Item.Sources[Index]);
    if (auto* Data=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering())
        for (int32 LOD=0;LOD<Data->LODRenderData.Num();++LOD)
        {
            const auto* Info=Mesh->GetSkeletalMeshAsset()->GetLODInfo(LOD);
            const auto& Sections=Data->LODRenderData[LOD].RenderSections;
            for (int32 Section=0;Section<Sections.Num();++Section)
            {
                int32 Slot=Sections[Section].MaterialIndex;
                if (Info && Info->LODMaterialMap.IsValidIndex(Slot) && Info->LODMaterialMap[Slot]!=INDEX_NONE)
                    Slot=Info->LODMaterialMap[Slot];
                Native->ShowMaterialSection(Slot,Section,!Item.Settings.IsValidIndex(Slot) || !Item.Settings[Slot],LOD);
            }
        }
    Native->RegisterComponent();
    Item.NativeSections=Native;
}

void BuildSelectionMesh(FMeshRendering& Item)
{
#if WITH_EDITOR
    auto* Mesh=Item.Mesh.Get();
    if (!GIsEditor || !Mesh || !Mesh->GetOwner()) return;
    auto* Selection=NewObject<UWuWaSelectionMeshComponent>(Mesh->GetOwner(),NAME_None,RF_Transient|RF_DuplicateTransient);
    Selection->SourceMesh=Mesh;
    Selection->SetupAttachment(Mesh);
    Selection->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Selection->SetGenerateOverlapEvents(false);
    Selection->SetSkeletalMeshAsset(Mesh->GetSkeletalMeshAsset());
    Selection->SetLeaderPoseComponent(Mesh,true,false);
    Selection->SetCastShadow(false);
    Selection->bUseAttachParentBound=true;
    Selection->SetComponentTickEnabled(false);
    for (int32 Index=0;Index<Item.Sources.Num();++Index) Selection->SetMaterial(Index,Item.Sources[Index]);
    Selection->RegisterComponent();
    Item.SelectionMesh=Selection;
#endif
}
}

/** References only materials; actors/components remain weak and can be deleted normally. */
class FWuWaRenderingState : public FGCObject
{
public:
    TArray<FMeshRendering> Meshes;
    TSharedPtr<FWuWaSurfaceRenderer,ESPMode::ThreadSafe> Renderer;
    TWeakObjectPtr<UDirectionalLightComponent> KeyLight;
    TArray<TWeakObjectPtr<UPointLightComponent>> LocalLights;
    TWeakObjectPtr<USceneCaptureComponent2D> Capture;
    TWeakObjectPtr<UTextureRenderTarget2D> ReflectionTarget;
    TArray<FWuWaSurfaceMaterialBinding> LastBindings;
    const FRenderTarget* LastReflection=nullptr;
    float UntilDiscovery=0;
    double LastDiscoveryTime=0;
    int32 AuthoringScopes=0;
    int32 ActiveLocalLights=0;
    uint64 TickSerial=0;
    uint64 DiscoverySerial=0;

    virtual FString GetReferencerName() const override { return TEXT("WuWaWorldRendering"); }
    virtual void AddReferencedObjects(FReferenceCollector& Collector) override
    {
        for (auto& Item:Meshes)
        {
            Collector.AddReferencedObjects(Item.Sources);
            Collector.AddReferencedObjects(Item.OriginalOverrides);
            Collector.AddReferencedObjects(Item.Instances);
        }
    }
    void Discover(UWorld* World)
    {
        ++DiscoverySerial;
        // Keep discovery scoped to the world's native actor/component inventory.
        for (TActorIterator<AActor> Actor(World);Actor;++Actor)
        {
            TInlineComponentArray<USkeletalMeshComponent*> Components(*Actor);
            for (auto* Mesh:Components)
                if (!Mesh->IsTemplate() && Mesh->IsRegistered() && !Mesh->ComponentHasTag(NativeSectionsTag) && !Mesh->IsA<UWuWaSelectionMeshComponent>()
                    && !Meshes.ContainsByPredicate([Mesh](const auto& Item){return Item.Mesh==Mesh;}) && HasCompatibleMaterial(Mesh))
                    Meshes.AddDefaulted_GetRef().Mesh=Mesh;
        }
        // Some native asset preview scenes register a component without an actor.
        if (World->WorldType==EWorldType::EditorPreview)
            for (TObjectIterator<USkeletalMeshComponent> It;It;++It)
                if (It->GetWorld()==World && !It->IsTemplate() && It->IsRegistered() && !It->GetOwner()
                    && !Meshes.ContainsByPredicate([Mesh=*It](const auto& Item){return Item.Mesh==Mesh;}) && HasCompatibleMaterial(*It))
                    Meshes.AddDefaulted_GetRef().Mesh=*It;
        UDirectionalLightComponent* Best=nullptr;
        for (TObjectIterator<UDirectionalLightComponent> It;It;++It)
        {
            auto* Light=*It;
            if (Light->GetWorld()!=World || !Light->IsRegistered() || !Light->IsVisible() || Light->Intensity<=0) continue;
            if (!Best || Light->ForwardShadingPriority>Best->ForwardShadingPriority
                || (Light->ForwardShadingPriority==Best->ForwardShadingPriority && Light->Intensity>Best->Intensity)) Best=Light;
        }
        for (TActorIterator<AWuWaShowcaseStage> It(World);It;++It)
            if (It->KeyLight) { Best=Cast<UDirectionalLightComponent>(It->KeyLight->GetLightComponent());break; }
        for (const auto& Item:Meshes)
            if (auto* Override=GetOverrides(Item.Mesh.Get());Override && Override->KeyLight)
            { Best=Cast<UDirectionalLightComponent>(Override->KeyLight->GetLightComponent());break; }
        KeyLight=Best;
        LocalLights.Reset();
        for (TObjectIterator<UPointLightComponent> It;It;++It)
            if (It->GetWorld()==World && !It->IsTemplate() && It->IsRegistered()) LocalLights.Add(*It);
    }
};

UWuWaRenderingSubsystem::UWuWaRenderingSubsystem()=default;
UWuWaRenderingSubsystem::~UWuWaRenderingSubsystem()=default;

bool UWuWaRenderingSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
    return Type==EWorldType::Editor || Type==EWorldType::PIE || Type==EWorldType::Game
        || Type==EWorldType::EditorPreview || Type==EWorldType::GamePreview;
}

void UWuWaRenderingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    State=MakeShared<FWuWaRenderingState>();
    RegisteredActorsHandle=GetWorld()->AddOnPostRegisterAllActorComponentsHandler(
        FOnPostRegisterAllActorComponents::FDelegate::CreateLambda([this](AActor*){if(State) State->UntilDiscovery=0;}));
}

void UWuWaRenderingSubsystem::Deinitialize()
{
    GetWorld()->RemoveOnPostRegisterAllActorComponentsHandler(RegisteredActorsHandle);
    if (State)
    {
        for (auto& Item:State->Meshes) Restore(Item);
        if (State->Renderer)
        {
            State->Renderer->ShutdownPasses();
            State->Renderer->ClearMaterials();
        }
        State.Reset();
    }
    Super::Deinitialize();
}

TStatId UWuWaRenderingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UWuWaRenderingSubsystem,STATGROUP_Tickables);
}

UWuWaRenderingSubsystem* UWuWaRenderingSubsystem::GetForWorld(const UObject* Context)
{
    UWorld* World=GEngine ? GEngine->GetWorldFromContextObject(Context,EGetWorldErrorMode::ReturnNull) : nullptr;
    return World ? World->GetSubsystem<UWuWaRenderingSubsystem>() : nullptr;
}

void UWuWaRenderingSubsystem::SuspendAuthoring()
{
    if (!State || State->AuthoringScopes++>0) return;
    for (auto& Item:State->Meshes) Restore(Item);
    if (State->Renderer) State->Renderer->ClearMaterials();
    State->LastBindings.Reset();
}

void UWuWaRenderingSubsystem::ResumeAuthoring()
{
    if (!State) return;
    State->AuthoringScopes=FMath::Max(0,State->AuthoringScopes-1);
    State->UntilDiscovery=0;
}

void UWuWaRenderingSubsystem::Tick(float DeltaTime)
{
    if (!State || State->AuthoringScopes || !GetWorld()->Scene) return;
    ++State->TickSerial;
    State->UntilDiscovery-=DeltaTime;
    const double Now=FPlatformTime::Seconds();
    if (State->UntilDiscovery<=0 || Now-State->LastDiscoveryTime>.25)
    {
        State->UntilDiscovery=.25f;
        State->LastDiscoveryTime=Now;
        State->Discover(GetWorld());
    }
    const auto* Light=State->KeyLight.Get();
    const FVector Direction=Light ? -Light->GetForwardVector() : FVector(-.57922798,-.40557979,.70710678);
    const FVector Horizontal=FVector(Direction.X,Direction.Y,0).GetSafeNormal();
    TArray<FWuWaSurfaceMaterialBinding> Bindings;
    for (int32 ItemIndex=State->Meshes.Num()-1;ItemIndex>=0;--ItemIndex)
    {
        auto& Item=State->Meshes[ItemIndex];
        auto* Mesh=Item.Mesh.Get();
        if (!Mesh || !Mesh->IsRegistered() || (Mesh->GetOwner() && Mesh->GetOwner()->IsActorBeingDestroyed()))
        {
            Restore(Item);State->Meshes.RemoveAtSwap(ItemIndex);continue;
        }
        auto* Override=GetOverrides(Mesh);
        if (Item.bManaged && Item.Asset!=Mesh->GetSkeletalMeshAsset())
        {
            // Remove only our transient overrides before reading the new mesh's
            // material slots. Preserve any explicit user overrides normally.
            Restore(Item);Item.Sources.Reset();Item.Instances.Reset();Item.Settings.Reset();
        }
        const bool bEnabled=(!Override || Override->bEnableRendering) && Mesh->GetSkeletalMeshAsset();
        TArray<TObjectPtr<UMaterialInterface>> Sources;
        TArray<const UWuWaMaterialSettings*> Settings;
        bool bAny=false;
        bool bDetached=false;
        for (int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)
        {
            auto* Material=Mesh->GetMaterial(Slot);
            if (Item.Instances.IsValidIndex(Slot) && Item.Instances[Slot])
            {
                if (Material==Item.Instances[Slot])
                    Material=Item.OriginalOverrides.IsValidIndex(Slot) && Item.OriginalOverrides[Slot]
                        ? Item.OriginalOverrides[Slot].Get() : (Mesh->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(Slot)
                            ? Mesh->GetSkeletalMeshAsset()->GetMaterials()[Slot].MaterialInterface.Get() : nullptr);
                else bDetached=true;
            }
            Sources.Add(Material);
            const auto* Data=bEnabled ? UWuWaMaterialSettings::Find(Material) : nullptr;
            Settings.Add(Data);bAny|=Data!=nullptr;
        }
        if (!bAny)
        {
            if (Item.bManaged) { Restore(Item);Item.Sources.Reset();Item.Instances.Reset();Item.Settings.Reset(); }
            // Discovery rechecks native materials periodically, so ordinary or
            // opted-out meshes need no per-frame renderer state. Later material
            // assignment and re-enabling remain automatic.
            State->Meshes.RemoveAtSwap(ItemIndex);
            continue;
        }
        if (!Item.bManaged || bDetached || Item.Asset!=Mesh->GetSkeletalMeshAsset() || Sources!=Item.Sources || Settings!=Item.Settings)
        {
            Restore(Item);
            Item.Asset=Mesh->GetSkeletalMeshAsset();Item.Sources=Sources;Item.Settings=Settings;
            Item.OriginalOverrides.Reset();Item.Instances.Reset();
            for (UMaterialInterface* Material:Mesh->OverrideMaterials) Item.OriginalOverrides.Add(Material);
            Item.bOriginalMainPass=Mesh->bRenderInMainPass;Item.bOriginalDepthPass=Mesh->bRenderInDepthPass;
            Item.OriginalSkinCache=Mesh->SkinCacheUsage;Item.OriginalPoseTick=Mesh->VisibilityBasedAnimTickOption;
            for (int32 Slot=0;Slot<Sources.Num();++Slot)
            {
                if (Settings[Slot])
                    for (auto* Material:{Sources[Slot].Get(),Settings[Slot]->StencilMaterial.Get(),Settings[Slot]->OutlineMaterial.Get()})
                        if (Material) Material->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh);
                Item.Instances.Add(Settings[Slot] ? Mesh->CreateDynamicMaterialInstance(Slot,Sources[Slot]) : nullptr);
            }
            Mesh->SkinCacheUsage={ESkinCacheUsage::Disabled};
            Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
            Mesh->SetRenderInMainPass(false);Mesh->SetRenderInDepthPass(false);Mesh->MarkRenderStateDirty();
            Item.bManaged=true;
            BuildNativeSections(Item);
            BuildSelectionMesh(Item);
        }
        if (auto* Selection=Item.SelectionMesh.Get())
        {
            Selection->SetVisibility(Mesh->IsVisible());
            Selection->SetHiddenInGame(Mesh->bHiddenInGame);
        }
        if (auto* Native=Item.NativeSections.Get())
        {
            Native->SetVisibility(Mesh->IsVisible());
            Native->SetHiddenInGame(Mesh->bHiddenInGame);
        }
        USkeletalMeshComponent* HeadMesh=Mesh;
        FName Head=FindHead(HeadMesh,Override ? Override->HeadBone : NAME_None);
        for (auto* Parent=Mesh->GetAttachParent();Head.IsNone() && Parent;Parent=Parent->GetAttachParent())
            if (auto* Skinned=Cast<USkeletalMeshComponent>(Parent))
                if (FName Candidate=FindHead(Skinned,Override ? Override->HeadBone : NAME_None);!Candidate.IsNone())
                { HeadMesh=Skinned;Head=Candidate; }
        FQuat Rotation=HeadMesh->GetComponentQuat();
        if (!Head.IsNone() && HeadMesh->GetSkeletalMeshAsset())
        {
            const auto& Skeleton=HeadMesh->GetSkeletalMeshAsset()->GetRefSkeleton();
            FQuat Reference=FQuat::Identity;
            for (int32 Bone=Skeleton.FindBoneIndex(Head);Bone!=INDEX_NONE;Bone=Skeleton.GetParentIndex(Bone))
                Reference=Skeleton.GetRefBonePose()[Bone].GetRotation()*Reference;
            Rotation=HeadMesh->GetBoneQuaternion(Head,EBoneSpaces::WorldSpace)*Reference.Inverse();
        }
        const bool bUEHumanoid=HeadMesh->GetBoneIndex(TEXT("clavicle_l"))!=INDEX_NONE;
        const FVector Forward=AxisVector(Override ? Override->ForwardAxis : EWuWaFacingAxis::Automatic,bUEHumanoid ? FVector::XAxisVector : FVector::YAxisVector);
        const FVector Up=AxisVector(Override ? Override->UpAxis : EWuWaFacingAxis::Automatic,FVector::ZAxisVector);
        const FVector Right=Forward.Cross(Up).GetSafeNormal();
        FString Warning;
        if (Settings.ContainsByPredicate([](const auto* S){return S && S->Part==EWuWaSurfacePart::Face;}))
        {
            if (Head.IsNone()) Warning=TEXT("未识别头部骨骼；可在可选的鸣潮角色渲染组件中指定头部骨骼。");
            else if (!bUEHumanoid && Head!=TEXT("Bip001Head") && (!Override || Override->ForwardAxis==EWuWaFacingAxis::Automatic))
                Warning=TEXT("未知骨架的面部朝向暂按 +Y；请在可选的鸣潮角色渲染组件中确认面部朝向。");
        }
        if (Warning!=Item.Warning)
        {
            Item.Warning=Warning;
            if (!Warning.IsEmpty()) UE_LOG(LogWuWaRendering,Warning,TEXT("%s: %s"),*Mesh->GetPathName(),*Warning);
        }
        TSet<UMaterialInterface*> Submitted;
        for (int32 Slot=0;Slot<Item.Instances.Num();++Slot)
        {
            UMaterialInstanceDynamic* Instance=Item.Instances[Slot];
            const auto* Data=Item.Settings[Slot];
            if (!Instance || !Data) continue;
            Instance->SetVectorParameterValue(TEXT("KeyLightDirection"),FLinearColor(Direction));
            Instance->SetVectorParameterValue(TEXT("HorizontalLightDirection"),FLinearColor(Horizontal));
            Instance->SetVectorParameterValue(TEXT("HeadRight"),FLinearColor(Rotation.RotateVector(Right)));
            Instance->SetVectorParameterValue(TEXT("HeadForward"),FLinearColor(Rotation.RotateVector(Forward)));
            Instance->SetVectorParameterValue(TEXT("HeadUp"),FLinearColor(Rotation.RotateVector(Up)));
            if (Submitted.Contains(Instance)) continue;
            Submitted.Add(Instance);
            FWuWaSurfaceMaterialBinding Binding;
            Binding.Primitive=Mesh->GetPrimitiveSceneId();Binding.Surface=Instance->GetRenderProxy();
            Binding.Stencil=Data->StencilMaterial ? Data->StencilMaterial->GetRenderProxy() : nullptr;
            Binding.Outline=Data->OutlineMaterial ? Data->OutlineMaterial->GetRenderProxy() : nullptr;
            Binding.Part=Data->Part;Binding.bOutline=Data->bOutline;
            Bindings.Add(Binding);
        }
    }
    if (!State->Renderer && !Bindings.IsEmpty())
    {
        State->Renderer=FSceneViewExtensions::NewExtension<FWuWaSurfaceRenderer>(GetWorld());
        State->Renderer->InitializePasses();
    }
    if (!State->Renderer) return;
    if (!State->Capture.IsValid())
    {
        State->ReflectionTarget.Reset();
        State->Renderer->SetReflectionView(nullptr,FPlane(FVector::ZeroVector,FVector::UpVector));
    }
    const FRenderTarget* Reflection=State->ReflectionTarget.IsValid() ? State->ReflectionTarget->GameThread_GetRenderTargetResource() : nullptr;
    bool Changed=Reflection!=State->LastReflection || Bindings.Num()!=State->LastBindings.Num();
    for (int32 Index=0;!Changed && Index<Bindings.Num();++Index)
    {
        const auto& A=Bindings[Index];const auto& B=State->LastBindings[Index];
        Changed=A.Primitive!=B.Primitive || A.Surface!=B.Surface || A.Stencil!=B.Stencil || A.Outline!=B.Outline
            || A.Part!=B.Part || A.bOutline!=B.bOutline;
    }
    if (Changed)
    {
        State->Renderer->SetMaterials(Bindings,Reflection);
        State->LastBindings=Bindings;State->LastReflection=Reflection;
    }
    FWuWaLightingInputs Lighting;
    if (Light)
    {
        Lighting.RimDirection=FVector3f(-(Light->GetComponentQuat()*FRotator(15,0,0).Quaternion()).GetForwardVector());
        const FLinearColor Tint=Light->GetLightColor();Lighting.LightTint=FVector3f(Tint.R,Tint.G,Tint.B);
        Lighting.ShadowDirection=FVector3f(Direction);
        Lighting.ContactShadowLength=Light->CastShadows ? Light->ContactShadowLength:0;
        Lighting.ContactShadowIntensity=Light->ContactShadowCastingIntensity;
        Lighting.ContactShadowNonCastingIntensity=Light->ContactShadowNonCastingIntensity;
        Lighting.bContactShadowLengthInWorldSpace=Light->GetContactShadowLengthInWS();
    }
    struct FCandidate { UPointLightComponent* Light;double Distance; };
    TArray<FCandidate,TInlineAllocator<16>> Candidates;
    for (auto Weak:State->LocalLights)
    {
        auto* Local=Weak.Get();
        if (!Local || !Local->IsVisible() || !Local->bAffectsWorld || Local->Intensity<=0 || Local->AttenuationRadius<=0) continue;
        double Distance=TNumericLimits<double>::Max();
        for (const auto& Item:State->Meshes)
            if (Item.bManaged && Item.Mesh.IsValid())
                Distance=FMath::Min(Distance,FMath::Max(0.,FVector::Distance(Local->GetComponentLocation(),Item.Mesh->Bounds.Origin)-Item.Mesh->Bounds.SphereRadius));
        if (Distance<=Local->AttenuationRadius) Candidates.Add({Local,Distance});
    }
    Candidates.Sort([](const FCandidate& A,const FCandidate& B){return A.Distance==B.Distance ? A.Light->GetPathName()<B.Light->GetPathName() : A.Distance<B.Distance;});
    for (int32 Index=0;Index<FMath::Min(16,Candidates.Num());++Index)
    {
        const auto* Local=Candidates[Index].Light;
        const auto* Spot=Cast<USpotLightComponent>(Local);
        const FVector3f Position(Local->GetComponentLocation());
        const FVector3f Axis(-Local->GetForwardVector());
        const FLinearColor Color=Local->GetLightColor();
        const float Radius=Local->AttenuationRadius;
        const float Strength=Local->bUseInverseSquaredFalloff ? Local->ComputeLightBrightness()/(Radius*Radius) : Local->ComputeLightBrightness();
        const float Outer=Spot ? FMath::Cos(FMath::DegreesToRadians(Spot->OuterConeAngle)) : -1.f;
        const float Inner=Spot ? FMath::Cos(FMath::DegreesToRadians(Spot->InnerConeAngle)) : 1.f;
        const uint32 Channels=(Local->LightingChannels.bChannel0 ? 1u:0u)|(Local->LightingChannels.bChannel1 ? 2u:0u)|(Local->LightingChannels.bChannel2 ? 4u:0u);
        uint32 Type=Spot ? 0x20000u:0u;float TypeBits,ChannelBits;
        FMemory::Memcpy(&TypeBits,&Type,sizeof(float));FMemory::Memcpy(&ChannelBits,&Channels,sizeof(float));
        Lighting.LocalLightData.Append({FVector4f(Position,1.f/Radius),FVector4f(Axis,TypeBits),
            FVector4f(Outer,1.f/FMath::Max(Inner-Outer,.001f),0,0),FVector4f(0,0,0,1),
            FVector4f(1,.01f,1,0),FVector4f(Color.R,Color.G,Color.B,.5f),
            FVector4f(Color.R*.2f,Color.G*.2f,Color.B*.2f,0),FVector4f(Strength,0,0,ChannelBits),FVector4f(.1f,0,0,0)});
    }
    State->ActiveLocalLights=Lighting.LocalLightData.Num()/9;
    State->Renderer->SetLightingInputs(Lighting);
}

void UWuWaRenderingSubsystem::UpdateStageCapture(USceneCaptureComponent2D* Capture,UTextureRenderTarget2D* Target,
    const FPlane& Plane,const TArray<TObjectPtr<AActor>>& Backgrounds)
{
    if (!State || !Capture) return;
    State->Capture=Capture;State->ReflectionTarget=Target;
    Capture->TextureTarget=Target;
    Capture->ShowOnlyActors.Reset();
    for (AActor* Background:Backgrounds) if (IsValid(Background) && Background->GetWorld()==GetWorld()) Capture->ShowOnlyActors.AddUnique(Background);
    for (const auto& Item:State->Meshes)
        if (Item.bManaged && Item.Mesh.IsValid() && Item.Mesh->GetOwner()) Capture->ShowOnlyActors.AddUnique(Item.Mesh->GetOwner());
    if (State->Renderer) State->Renderer->SetReflectionView(Target ? Target->GameThread_GetRenderTargetResource() : nullptr,Plane);
    if (Target) Capture->CaptureSceneDeferred();
}

FString UWuWaRenderingSubsystem::GetRenderingDiagnostics() const
{
    auto Report=MakeShared<FJsonObject>();
    TArray<TSharedPtr<FJsonValue>> Entries;
    if (State) for (const auto& Item:State->Meshes)
    {
        if (!Item.bManaged || !Item.Mesh.IsValid()) continue;
        auto Entry=MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("mesh_component"),Item.Mesh->GetPathName());
        Entry->SetNumberField(TEXT("surface_slots"),Item.Settings.FilterByPredicate([](const auto* S){return S!=nullptr;}).Num());
        Entry->SetBoolField(TEXT("native_sections"),Item.NativeSections.IsValid());
        Entry->SetStringField(TEXT("warning"),Item.Warning);
        Entry->SetStringField(TEXT("bounds_origin"),Item.Mesh->Bounds.Origin.ToString());
        Entry->SetNumberField(TEXT("bounds_radius"),Item.Mesh->Bounds.SphereRadius);
        if (auto* Override=GetOverrides(Item.Mesh.Get()))
        {
            Entry->SetStringField(TEXT("overrides"),Override->GetPathName());
            Entry->SetBoolField(TEXT("enabled"),Override->bEnableRendering);
        }
        Entries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("meshes"),Entries);
    Report->SetNumberField(TEXT("renderer_count"),State && State->Renderer ? 1 : 0);
    Report->SetNumberField(TEXT("tick_serial"),State ? State->TickSerial:0);
    Report->SetNumberField(TEXT("discovery_serial"),State ? State->DiscoverySerial:0);
    Report->SetNumberField(TEXT("authoring_scopes"),State ? State->AuthoringScopes:0);
    Report->SetBoolField(TEXT("engine_exit_requested"),IsEngineExitRequested());
    Report->SetBoolField(TEXT("world_has_scene"),GetWorld()->Scene!=nullptr);
    Report->SetBoolField(TEXT("tickable"),IsTickable());
    TArray<TSharedPtr<FJsonValue>> Inventory;
    for (TActorIterator<AActor> Actor(GetWorld());Actor;++Actor)
    {
        TInlineComponentArray<USkeletalMeshComponent*> Components(*Actor);
        for (auto* Mesh:Components)
        {
            auto Row=MakeShared<FJsonObject>();
            Row->SetStringField(TEXT("path"),Mesh->GetPathName());
            Row->SetBoolField(TEXT("registered"),Mesh->IsRegistered());
            Row->SetBoolField(TEXT("template"),Mesh->IsTemplate());
            Row->SetBoolField(TEXT("destroying"),Actor->IsActorBeingDestroyed());
            Row->SetBoolField(TEXT("compatible_material"),UWuWaMaterialSettings::Find(Mesh->GetMaterial(0))!=nullptr);
            Row->SetBoolField(TEXT("tracked"),State && State->Meshes.ContainsByPredicate([Mesh](const auto& Item){return Item.Mesh==Mesh;}));
            Inventory.Add(MakeShared<FJsonValueObject>(Row));
        }
    }
    Report->SetArrayField(TEXT("mesh_inventory"),Inventory);
    Report->SetNumberField(TEXT("local_lights"),State ? State->ActiveLocalLights:0);
    TArray<TSharedPtr<FJsonValue>> LocalEntries;
    if (State) for (auto Weak:State->LocalLights) if (auto* Local=Weak.Get())
    {
        auto Entry=MakeShared<FJsonObject>();
        Entry->SetStringField(TEXT("path"),Local->GetPathName());
        Entry->SetBoolField(TEXT("visible"),Local->IsVisible());
        Entry->SetBoolField(TEXT("affects_world"),Local->bAffectsWorld);
        Entry->SetNumberField(TEXT("intensity"),Local->Intensity);
        Entry->SetNumberField(TEXT("radius"),Local->AttenuationRadius);
        LocalEntries.Add(MakeShared<FJsonValueObject>(Entry));
    }
    Report->SetArrayField(TEXT("local_light_components"),LocalEntries);
    Report->SetStringField(TEXT("directional_light"),State ? GetPathNameSafe(State->KeyLight.Get()):FString());
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));return Json;
}
