#include "ShowcaseWidgetAuthoring.h"
#include "AssetCompilingManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "ImageUtils.h"
#include "Interfaces/ISlateRHIRendererModule.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "Slate/WidgetRenderer.h"
#include "TextureResource.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SViewport.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "WidgetBlueprint.h"

namespace
{
bool CaptureSlateWidget(const TSharedRef<SWidget>& Widget, const FString& Filename, FVector2D DrawSize, float Scale, bool bWarmUpLayout = false)
{
    if (!FSlateApplication::IsInitialized()) return false;
    const FIntPoint Size(FMath::RoundToInt(DrawSize.X), FMath::RoundToInt(DrawSize.Y));
    if (Size.X < 1 || Size.Y < 1) return false;

    FWidgetRenderer* Renderer = new FWidgetRenderer(true);
    TStrongObjectPtr<UTextureRenderTarget2D> Target(FWidgetRenderer::CreateTargetFor(FVector2D(Size), TF_Bilinear, true));
    if (!Target.IsValid())
    {
        BeginCleanup(Renderer);
        return false;
    }
    // SScaleBox normalizes its child prepass using geometry from the first paint.
    if (bWarmUpLayout) Renderer->DrawWidget(Target.Get(), Widget, Scale, FVector2D(Size), 0.f);
    Renderer->DrawWidget(Target.Get(), Widget, Scale, FVector2D(Size), 0.f);
    BeginCleanup(Renderer);

    TArray<FColor> Pixels;
    FReadSurfaceDataFlags Flags(RCM_UNorm);
    Flags.SetLinearToGamma(false);
    if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags) || Pixels.Num() != Size.X * Size.Y) return false;
    for (FColor& Pixel : Pixels) Pixel.A = 255;
    TArray64<uint8> Compressed;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, Compressed);
    return !Compressed.IsEmpty() && FFileHelper::SaveArrayToFile(Compressed, *Filename);
}
}

bool UShowcaseWidgetAuthoring::CaptureViewportUI(UObject* WorldContextObject, const FString& Filename)
{
    UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
    UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    TSharedPtr<SViewport> Widget = Viewport ? Viewport->GetGameViewportWidget() : nullptr;
    if (!Widget) return false;
    const FGeometry& Geometry = Widget->GetCachedGeometry();
    // Render the existing viewport tree offscreen; Slate window screenshots read the swap-chain backbuffer.
    return CaptureSlateWidget(Widget.ToSharedRef(), Filename, Geometry.GetAbsoluteSize(), Geometry.GetAccumulatedLayoutTransform().GetScale());
}

bool UShowcaseWidgetAuthoring::CaptureWidgetPreview(UWidgetBlueprint* Blueprint, const FString& Filename, FVector2D DrawSize)
{
    UWidget* Root = GetRoot(Blueprint);
    if (!Root || !FApp::CanEverRender()) return false;
    if (!FSlateApplication::IsInitialized())
    {
        if (!IsRunningCommandlet()) return false;
        // Commandlets have no editor window. Initialize only Slate's native renderer.
        auto& Module = FModuleManager::LoadModuleChecked<ISlateRHIRendererModule>(TEXT("SlateRHIRenderer"));
        FSlateApplication::InitializeAsStandaloneApplication(Module.CreateSlateRHIRenderer());
    }
    // Texture dimensions and Slate resources use placeholders during async compilation.
    FAssetCompilingManager::Get().FinishAllCompilation();
    // Each template preview owns a fresh Slate tree; no application frame updates its cached geometry.
    Root->ReleaseSlateResources(true);
    return CaptureSlateWidget(Root->TakeWidget(), Filename, DrawSize, 1.f, true);
}

UWidget* UShowcaseWidgetAuthoring::GetRoot(UWidgetBlueprint* Blueprint)
{
    return Blueprint && Blueprint->WidgetTree ? Blueprint->WidgetTree->RootWidget.Get() : nullptr;
}

UWidget* UShowcaseWidgetAuthoring::MakeWidget(UWidgetBlueprint* Blueprint, TSubclassOf<UWidget> Type, FName Name, bool bVariable)
{
    if (!Blueprint || !Blueprint->WidgetTree || !Type) return nullptr;
    UWidget* Widget = FindObject<UWidget>(Blueprint->WidgetTree, *Name.ToString());
    if (Widget && !Widget->IsA(Type)) return nullptr;
    if (!Widget) Widget = Blueprint->WidgetTree->ConstructWidget<UWidget>(Type, Name);
    Widget->RemoveFromParent();
    Widget->bIsVariable = bVariable;
    return Widget;
}

bool UShowcaseWidgetAuthoring::ConfigureFont(UFont* Font, UFontFace* Chinese, UFontFace* Latin)
{
    if (!Font || !Chinese || !Latin) return false;
    Font->FontCacheType = EFontCacheType::Runtime;
    auto& Composite = Font->GetMutableInternalCompositeFont();
    Composite.DefaultTypeface.Fonts.Reset();
    FTypefaceEntry Regular; Regular.Name = TEXT("Regular"); Regular.Font = FFontData(Chinese);
    FTypefaceEntry English; English.Name = TEXT("Latin"); English.Font = FFontData(Latin);
    Composite.DefaultTypeface.Fonts.Add(Regular); Composite.DefaultTypeface.Fonts.Add(English);
    Composite.MakeDirty(); Font->PostEditChange(); Font->MarkPackageDirty();
    return true;
}

bool UShowcaseWidgetAuthoring::BindSelectionFrames(UWidgetBlueprint* Blueprint)
{
    if (!Blueprint || !Blueprint->SkeletonGeneratedClass) return false;
    const auto* Schema = GetDefault<UEdGraphSchema_K2>();
    int32 Bound = 0;
    for (UEdGraph* Graph : Blueprint->UbergraphPages)
    {
        for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
            if (Node->NodeComment == TEXT("Official selector frame state")) FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
        for (UEdGraphNode* Node : TArray<UEdGraphNode*>(Graph->Nodes))
        {
            auto* Event = Cast<UK2Node_ComponentBoundEvent>(Node);
            if (!Event || !Event->GetComponentPropertyName().ToString().StartsWith(TEXT("Character"))) continue;
            const int32 Selected = FCString::Atoi(*Event->GetComponentPropertyName().ToString().RightChop(9));
            UEdGraphPin* Tail = Event->FindPin(UEdGraphSchema_K2::PN_Then);
            for (int32 Step = 0; Tail && !Tail->LinkedTo.IsEmpty() && Step < 32; ++Step)
                Tail = Tail->LinkedTo[0]->GetOwningNode()->FindPin(UEdGraphSchema_K2::PN_Then);
            if (!Tail || !Tail->LinkedTo.IsEmpty()) return false;
            for (int32 Index = 0; Index < 7; ++Index)
            {
                auto* Get = NewObject<UK2Node_VariableGet>(Graph);
                Get->VariableReference.SetSelfMember(FName(*FString::Printf(TEXT("SelectionFrame%d"), Index)));
                auto* Set = NewObject<UK2Node_CallFunction>(Graph);
                Set->SetFromFunction(UWidget::StaticClass()->FindFunctionByName(TEXT("SetVisibility")));
                for (UK2Node* NewNode : {static_cast<UK2Node*>(Get), static_cast<UK2Node*>(Set)})
                {
                    Graph->AddNode(NewNode, false, false); NewNode->CreateNewGuid(); NewNode->PostPlacedNewNode(); NewNode->AllocateDefaultPins();
                    NewNode->NodeComment = TEXT("Official selector frame state");
                    NewNode->NodePosX = Event->NodePosX + 2200 + Index * 230; NewNode->NodePosY = Event->NodePosY + 50;
                }
                if (!Schema->TryCreateConnection(Tail, Set->GetExecPin())
                    || !Schema->TryCreateConnection(Get->GetValuePin(), Set->FindPin(UEdGraphSchema_K2::PN_Self))) return false;
                Schema->TrySetDefaultValue(*Set->FindPin(TEXT("InVisibility")), Selected == Index ? TEXT("HitTestInvisible") : TEXT("Hidden"));
                Tail = Set->GetThenPin();
            }
            ++Bound;
        }
    }
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    return Bound == 7 && Blueprint->Status != BS_Error;
}

UClass* UShowcaseWidgetAuthoring::GetParent(UWidgetBlueprint* Blueprint)
{
    return Blueprint ? Blueprint->ParentClass.Get() : nullptr;
}
