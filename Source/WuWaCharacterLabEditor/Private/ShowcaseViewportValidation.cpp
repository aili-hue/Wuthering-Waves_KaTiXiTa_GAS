#include "ShowcaseViewportValidation.h"
#include "WuWaRenderingComponent.h"
#include "GameFramework/Actor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ObjectTools.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

bool UShowcaseViewportValidation::RemoveRenderingValidationMap()
{
    const FString Name=TEXT("/Game/WuWaShowcase/Showcase/Maps/L_RenderingValidation");
    auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    TArray<FName> Referencers;
    Registry.GetReferencers(FName(Name),Referencers);
    for (FName Reference:Referencers)
        if (Reference!=FName(Name) && Reference.ToString().StartsWith(TEXT("/Game/"))) return false;
    UPackage* Package=LoadPackage(nullptr,*Name,LOAD_None);
    if (!Package) return !FPackageName::DoesPackageExist(Name);
    if (GEditor && GEditor->GetEditorWorldContext().World()
        && GEditor->GetEditorWorldContext().World()->GetPackage()==Package) return false;
    // Package cleanup is needed for test maps: deleting only the UWorld leaves
    // their level/subsystem objects referenced. No production package can enter.
    ObjectTools::CleanupAfterSuccessfulDelete({Package},false);
    return !FPackageName::DoesPackageExist(Name);
}

UWuWaRenderingComponent* UShowcaseViewportValidation::AddRenderingOverrides(AActor* Actor)
{
    if (!Actor) return nullptr;
    auto* Component=NewObject<UWuWaRenderingComponent>(Actor,NAME_None,RF_Transactional);
    Actor->AddInstanceComponent(Component);
    Component->RegisterComponent();
    return Component;
}

#include "Dom/JsonObject.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "PlayInEditorDataTypes.h"
#include "Serialization/JsonSerializer.h"
#include "Slate/SceneViewport.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "ImageUtils.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "Application/ThrottleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "Engine/Selection.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
TWeakPtr<IAssetViewport> ProfileAssetViewport;
bool bValidationThrottleDisabled=false;
TWeakPtr<FSceneViewport> ProfileSceneViewport;
FIntPoint OriginalSize = FIntPoint::ZeroValue;
bool bOriginalFixedSize = false;
bool bOriginalPreviewing = false;
FVector2D NavigationPointer=FVector2D::ZeroVector;
TStrongObjectPtr<ULevelEditorPlaySettings> PresentationPlaySettings;

FString ViewportReport(const TSharedRef<FJsonObject>& Report)
{
    FString Json;
    FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json));
    return Json;
}
}

FString UShowcaseViewportValidation::AuditPresentationViewport()
{
    auto Report=MakeShared<FJsonObject>();
    auto& Module=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    auto Asset=Module.GetFirstActiveViewport();
    if (!Asset) return ViewportReport(Report);
    auto& Client=Asset->GetAssetViewportClient();
    const auto Size=Asset->GetSharedActiveViewport()->GetSizeXY();
    Report->SetNumberField(TEXT("width"),Size.X);Report->SetNumberField(TEXT("height"),Size.Y);
    Report->SetBoolField(TEXT("realtime"),Client.IsRealtime());
    Report->SetBoolField(TEXT("simulating"),GEditor && GEditor->bIsSimulatingInEditor);
    Report->SetBoolField(TEXT("unattended"),FApp::IsUnattended());
    Report->SetBoolField(TEXT("has_focus"),FApp::HasFocus());
    Report->SetBoolField(TEXT("foreground"),FPlatformApplicationMisc::IsThisApplicationForeground());
    Report->SetBoolField(TEXT("cpu_throttled"),GEditor && GEditor->ShouldThrottleCPUUsage());
    Report->SetBoolField(TEXT("expensive_tasks_allowed"),FSlateThrottleManager::Get().IsAllowingExpensiveTasks());
    Report->SetBoolField(TEXT("mouse_captured"),Asset->GetSharedActiveViewport()->HasMouseCapture());
    Report->SetBoolField(TEXT("cursor_visible"),Asset->GetSharedActiveViewport()->IsCursorVisible());
    if (auto Widget=Asset->GetSharedActiveViewport()->GetViewportWidget().Pin())
        Report->SetBoolField(TEXT("viewport_interface_wrapped"),Widget->GetViewportInterface().Pin()
            !=StaticCastSharedPtr<ISlateViewport>(Asset->GetSharedActiveViewport()));
    if (UWorld* World=GEditor && GEditor->PlayWorld ? GEditor->PlayWorld.Get() : Client.GetWorld())
        if (APlayerController* Player=World->GetFirstPlayerController())
        {
            Report->SetStringField(TEXT("player_controller"),Player->GetClass()->GetPathName());
            Report->SetBoolField(TEXT("player_cursor_requested"),Player->ShouldShowMouseCursor());
            Report->SetBoolField(TEXT("player_cursor_setting"),Player->bShowMouseCursor);
            const ULocalPlayer* LocalPlayer=Player->GetLocalPlayer();
            Report->SetBoolField(TEXT("game_viewport_simulating"),LocalPlayer && LocalPlayer->ViewportClient
                && LocalPlayer->ViewportClient->IsSimulateInEditorViewport());
        }
    Report->SetBoolField(TEXT("play_viewport"),Asset->HasPlayInEditorViewport());
    Report->SetBoolField(TEXT("preview_percentage"),Client.IsPreviewingScreenPercentage());
    Report->SetNumberField(TEXT("preview_percent"),Client.GetPreviewScreenPercentage());
    Report->SetNumberField(TEXT("default_fraction"),Client.GetDefaultPrimaryResolutionFractionTarget());
    Report->SetNumberField(TEXT("frame_ms"),FApp::GetDeltaTime()*1000.0);
    Report->SetNumberField(TEXT("game_ms"),FPlatformTime::ToMilliseconds(GGameThreadTime));
    Report->SetNumberField(TEXT("render_ms"),FPlatformTime::ToMilliseconds(GRenderThreadTime));
    Report->SetNumberField(TEXT("gpu_ms"),FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
    for (const TCHAR* Name : {TEXT("r.ScreenPercentage"),TEXT("r.DynamicRes.OperationMode"),TEXT("r.AntiAliasingMethod"),
        TEXT("r.Editor.Viewport.ScreenPercentageMode.RealTime"),TEXT("r.Editor.Viewport.ScreenPercentageMode.NonRealTime"),
        TEXT("r.Editor.Viewport.ScreenPercentage"),TEXT("r.Editor.Viewport.OverridePIEScreenPercentage"),
        TEXT("r.SecondaryScreenPercentage.GameViewport"),TEXT("r.MotionBlurQuality"),TEXT("sg.ResolutionQuality")})
        if (auto* CVar=IConsoleManager::Get().FindConsoleVariable(Name)) Report->SetNumberField(Name,CVar->GetFloat());
    return ViewportReport(Report);
}

bool UShowcaseViewportValidation::SetPresentationRealtime(bool bEnabled)
{
    auto& Module=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    auto Asset=Module.GetFirstActiveViewport();
    if (!Asset) return false;
    Asset->GetAssetViewportClient().SetRealtime(bEnabled);
    return true;
}

double UShowcaseViewportValidation::MeasurePresentationCursorQuery(bool bNativeQuery)
{
    auto& Module=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    auto Asset=Module.GetFirstActiveViewport();
    if (!Asset) return -1.0;
    auto Viewport=Asset->GetSharedActiveViewport();
    const bool bOriginalVisibility=Viewport->IsCursorVisible();
    Viewport->ShowCursor(true);
    Viewport->InvalidateHitProxy();
    auto Widget=Viewport->GetViewportWidget().Pin();
    if (!Widget) return -1.0;
    TSharedPtr<ISlateViewport> Query=bNativeQuery ? StaticCastSharedPtr<ISlateViewport>(Viewport)
        : Widget->GetViewportInterface().Pin();
    if (!Query) return -1.0;
    const double Start=FPlatformTime::Seconds();
    Query->OnCursorQuery(Widget->GetCachedGeometry(),FPointerEvent());
    const double Milliseconds=(FPlatformTime::Seconds()-Start)*1000.0;
    Viewport->ShowCursor(bOriginalVisibility);
    return Milliseconds;
}

FString UShowcaseViewportValidation::ExercisePresentationNavigation(bool bDragging,float DeltaX,float DeltaY)
{
    auto Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("exercised"),false);
    if (!FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen"))) return ViewportReport(Report);
    auto& Module=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    auto Asset=Module.GetFirstActiveViewport();
    if (!Asset) return ViewportReport(Report);
    auto Viewport=Asset->GetSharedActiveViewport();
    auto Widget=Viewport->GetViewportWidget().Pin();
    if (!Widget) return ViewportReport(Report);
    auto Interface=Widget->GetViewportInterface().Pin();
    FWidgetPath Path;
    if (!Interface || !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Widget.ToSharedRef(),Path)) return ViewportReport(Report);
    const FGeometry Geometry=Widget->GetCachedGeometry();
    const bool bWasDragging=Viewport->KeyState(EKeys::RightMouseButton);
    if (!bWasDragging) NavigationPointer=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
    // RenderOffScreen uses a generic cursor with no OS window. Keep its Slate
    // position consistent with the supplied events for native ITF navigation.
    FSlateApplication::Get().SetCursorPos(NavigationPointer+FVector2D(DeltaX,DeltaY));
    TSet<FKey> Buttons;
    if (bDragging) Buttons.Add(EKeys::RightMouseButton);
    FPointerEvent Event(0,NavigationPointer+FVector2D(DeltaX,DeltaY),NavigationPointer,Buttons,EKeys::RightMouseButton,0,FModifierKeysState());
    if (bDragging && !bWasDragging)
    {
        Interface->OnMouseButtonDown(Geometry,Event);
        // Exercise Slate capture in an off-screen window, without OS input.
        FSlateApplication::Get().ProcessReply(Path,FReply::Handled().CaptureMouse(Widget.ToSharedRef()),&Path,&Event);
    }
    if (bDragging)
    {
        Interface->OnMouseMove(Geometry,Event);
        Interface->OnFinishedPointerInput();
        NavigationPointer+=FVector2D(DeltaX,DeltaY);
    }
    else if (bWasDragging)
    {
        FReply Reply=Interface->OnMouseButtonUp(Geometry,Event);
        FSlateApplication::Get().ProcessReply(Path,Reply.ReleaseMouseCapture(),&Path,&Event);
    }
    auto& Client=Asset->GetAssetViewportClient();
    Report->SetBoolField(TEXT("exercised"),true);
    Report->SetBoolField(TEXT("captured"),Viewport->HasMouseCapture());
    Report->SetBoolField(TEXT("tracking"),Client.IsTracking());
    Report->SetBoolField(TEXT("moving_camera"),Client.IsMovingCamera());
    Report->SetBoolField(TEXT("navigation_cursor_hidden"),Interface->OnCursorQuery(Geometry,Event).GetCursorType()==EMouseCursor::None);
    Report->SetNumberField(TEXT("yaw"),Client.GetViewRotation().Yaw);
    Report->SetNumberField(TEXT("pitch"),Client.GetViewRotation().Pitch);
    return ViewportReport(Report);
}

FString UShowcaseViewportValidation::ExercisePresentationClick(AActor* ExpectedActor)
{
    auto Report=MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("selected"),false);
    if (!FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen"))) return ViewportReport(Report);
    auto& Module=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    auto Asset=Module.GetFirstActiveViewport();
    if (!Asset) return ViewportReport(Report);
    auto Viewport=Asset->GetSharedActiveViewport();
    auto Widget=Viewport->GetViewportWidget().Pin();
    if (!Widget) return ViewportReport(Report);
    auto Interface=Widget->GetViewportInterface().Pin();
    if (!Interface) return ViewportReport(Report);
    const FIntPoint Size=Viewport->GetSizeXY();
    Viewport->InvalidateHitProxy();
    const int32 Divisions=ExpectedActor ? 20 : 5;
    for (int32 Y=1;Y<Divisions;++Y)
        for (int32 X=1;X<Divisions;++X)
        {
            HHitProxy* Hit=Viewport->GetHitProxy(Size.X*X/Divisions,Size.Y*Y/Divisions);
            if (!Hit || !Hit->IsA(HActor::StaticGetType())) continue;
            AActor* Actor=static_cast<HActor*>(Hit)->Actor.Get();
            if (!Actor || !Actor->IsSelectable()) continue;
            if (ExpectedActor && Actor!=ExpectedActor) continue;
            const FString ComponentPath=GetPathNameSafe(static_cast<HActor*>(Hit)->PrimComponent.Get());
            GEditor->SelectNone(false,true,false);
            const FGeometry Geometry=Widget->GetCachedGeometry();
            const FVector2D Position=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*FVector2D(float(X)/Divisions,float(Y)/Divisions));
            FPointerEvent Down(0,Position,Position,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState());
            FPointerEvent Up(0,Position,Position,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState());
            Interface->OnMouseButtonDown(Geometry,Down);
            Interface->OnMouseButtonUp(Geometry,Up);
            Report->SetStringField(TEXT("actor"),Actor->GetPathName());
            Report->SetStringField(TEXT("component"),ComponentPath);
            Report->SetBoolField(TEXT("selected"),GEditor->GetSelectedActors()->IsSelected(Actor));
            return ViewportReport(Report);
        }
    return ViewportReport(Report);
}

bool UShowcaseViewportValidation::CapturePresentationViewport(const FString& Filename)
{
    // Authoring artifacts live under the project so the tooling works inside Cartisia.
    const FString Allowed=FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("Saved/WuWaAuthoring/Artifacts"));
    if (!FPaths::IsUnderDirectory(FPaths::ConvertRelativePathToFull(Filename),Allowed)) return false;
    auto& Module=FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
    auto Asset=Module.GetFirstActiveViewport();
    if (!Asset) return false;
    auto Viewport=Asset->GetSharedActiveViewport();
    TArray<FColor> Pixels;
    if (!Viewport->ReadPixels(Pixels)) return false;
    const auto Size=Viewport->GetSizeXY();
    return FImageUtils::SaveImageByExtension(*Filename,FImageView(Pixels.GetData(),Size.X,Size.Y));
}

bool UShowcaseViewportValidation::RequestPresentationPlay(int32 Width, int32 Height)
{
    if (!GEditor || GEditor->PlayWorld || Width < 640 || Height < 360 || Width > 8192 || Height > 8192)
        return false;
    FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
    TSharedPtr<IAssetViewport> Asset = LevelEditor ? LevelEditor->GetFirstActiveViewport() : nullptr;
    TSharedPtr<FSceneViewport> Viewport = Asset ? Asset->GetSharedActiveViewport() : nullptr;
    TSharedPtr<SWindow> Window = Viewport ? Viewport->FindWindow() : nullptr;
    if (!Window) return false;

    // Keep validation overrides out of the default play settings during startup.
    PresentationPlaySettings.Reset(DuplicateObject<ULevelEditorPlaySettings>(
        GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage()));
    ULevelEditorPlaySettings* Settings = PresentationPlaySettings.Get();
    Settings->NewWindowWidth = Width;
    Settings->NewWindowHeight = Height;
    Settings->CenterNewWindow = false;
    const FVector2D Position = Window->GetPositionInScreen() + FVector2D(20, 20);
    Settings->NewWindowPosition = FIntPoint(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
    Settings->LastExecutedPlayModeType = PlayMode_InEditorFloating;
    FRequestPlaySessionParams Session;
    Session.EditorPlaySettings = Settings;
    GEditor->RequestPlaySession(Session);
    return true;
}

bool UShowcaseViewportValidation::FitPresentationViewport(int32 Width, int32 Height)
{
    UGameViewportClient* GameViewport = GEditor && GEditor->PlayWorld
        ? GEditor->PlayWorld->GetGameViewport() : nullptr;
    FSceneViewport* Viewport = GameViewport ? GameViewport->GetGameViewport() : nullptr;
    TSharedPtr<SWindow> Window = Viewport ? Viewport->FindWindow() : nullptr;
    if (!Window || !PresentationPlaySettings.IsValid() || Width < 640 || Height < 360
        || Width > 8192 || Height > 8192)
        return false;

    const FIntPoint Size = Viewport->GetSizeXY();
    Window->ReshapeWindow(Window->GetPositionInScreen(), FVector2D(Window->GetSizeInScreen())
        + FVector2D(Width - Size.X, Height - Size.Y));
    return true;
}

FString UShowcaseViewportValidation::ConfigureViewport(int32 Width, int32 Height)
{
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetBoolField(TEXT("configured"), false);
    if (ProfileSceneViewport.IsValid() || Width < 0 || Height < 0
        || ((Width == 0) != (Height == 0)) || Width > 8192 || Height > 8192)
    {
        Report->SetStringField(TEXT("error"), TEXT("Invalid dimensions or an active viewport capture"));
        return ViewportReport(Report);
    }

    FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
    TSharedPtr<IAssetViewport> Asset = LevelEditor ? LevelEditor->GetFirstActiveViewport() : nullptr;
    TSharedPtr<FSceneViewport> Viewport = Asset ? Asset->GetSharedActiveViewport() : nullptr;
    TSharedPtr<SWindow> Window = Viewport ? Viewport->FindWindow() : nullptr;
    if (!Viewport || !Window)
    {
        Report->SetStringField(TEXT("error"), TEXT("The level editor has no active scene viewport window"));
        return ViewportReport(Report);
    }

    ProfileAssetViewport = Asset;
    ProfileSceneViewport = Viewport;
    OriginalSize = Viewport->GetSizeXY();
    bOriginalFixedSize = Viewport->HasFixedSize();
    FEditorViewportClient& Client = Asset->GetAssetViewportClient();
    bOriginalPreviewing = Client.IsPreviewingScreenPercentage();
    const FVector2D WindowSize = Window->GetSizeInScreen();
    const FVector2D WindowPosition = Window->GetPositionInScreen();

    // Keep the optional user preview fraction untouched. The capture script
    // temporarily selects the engine's manual 100% default instead.
    Client.SetPreviewingScreenPercentage(false);
    if (Width > 0) Viewport->SetFixedViewportSize(Width, Height);
    const FIntPoint Size = Viewport->GetSizeXY();
    const FVector2D FinalWindowSize = Window->GetSizeInScreen();
    const FVector2D FinalWindowPosition = Window->GetPositionInScreen();
    const bool bWindowUnchanged = FinalWindowSize.Equals(WindowSize) && FinalWindowPosition.Equals(WindowPosition);
    const bool bConfigured = (Width == 0 || Size == FIntPoint(Width, Height)) && bWindowUnchanged;
    Report->SetBoolField(TEXT("configured"), bConfigured);
    if (bConfigured && !bValidationThrottleDisabled && FParse::Param(FCommandLine::Get(),TEXT("RenderOffScreen")))
    {
        // Offscreen Slate has no OS focus and may otherwise stop editor-world
        // ticks while still drawing. Scope this to native automated captures.
        FSlateThrottleManager::Get().DisableThrottle(true);
        bValidationThrottleDisabled=true;
    }
    Report->SetBoolField(TEXT("window_unchanged"), bWindowUnchanged);
    const bool bPlayInEditor = Asset->HasPlayInEditorViewport();
    Report->SetBoolField(TEXT("play_in_editor"), bPlayInEditor);
    Report->SetBoolField(TEXT("had_fixed_size"), bOriginalFixedSize);
    Report->SetNumberField(TEXT("original_width"), OriginalSize.X);
    Report->SetNumberField(TEXT("original_height"), OriginalSize.Y);
    Report->SetNumberField(TEXT("width"), Size.X);
    Report->SetNumberField(TEXT("height"), Size.Y);
    if (!bPlayInEditor)
        Report->SetNumberField(TEXT("editor_default_resolution_fraction"), Client.GetDefaultPrimaryResolutionFractionTarget());
    if (!bConfigured) RestoreViewport();
    return ViewportReport(Report);
}

FString UShowcaseViewportValidation::RestoreViewport()
{
    if (bValidationThrottleDisabled)
    {
        FSlateThrottleManager::Get().DisableThrottle(false);
        bValidationThrottleDisabled=false;
    }
    TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    TSharedPtr<IAssetViewport> Asset = ProfileAssetViewport.Pin();
    TSharedPtr<FSceneViewport> Viewport = ProfileSceneViewport.Pin();
    bool bRestored = Asset && Viewport;
    if (Asset)
    {
        Asset->GetAssetViewportClient().SetPreviewingScreenPercentage(bOriginalPreviewing);
        bRestored &= Asset->GetAssetViewportClient().IsPreviewingScreenPercentage() == bOriginalPreviewing;
    }
    if (Viewport)
    {
        Viewport->SetFixedViewportSize(bOriginalFixedSize ? OriginalSize.X : 0, bOriginalFixedSize ? OriginalSize.Y : 0);
        bRestored &= Viewport->HasFixedSize() == bOriginalFixedSize;
        if (bOriginalFixedSize) bRestored &= Viewport->GetSizeXY() == OriginalSize;
    }
    Report->SetBoolField(TEXT("restored"), bRestored);
    ProfileAssetViewport.Reset();
    ProfileSceneViewport.Reset();
    return ViewportReport(Report);
}
