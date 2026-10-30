#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericApplication.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IMainFrameModule.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "Widgets/SWindow.h"
#include "MaterialParameterEditor.h"
#include "Widgets/SViewport.h"
#include "Editor.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "EditorViewportClient.h"
#include "Slate/SceneViewport.h"
#include "WuWaRenderingSubsystem.h"
#include "UObject/ObjectSaveContext.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogWuWaEditor, Log, All);

/** Preserve native viewport behavior while avoiding animated hover readbacks in SIE. */
class FWuWaSimulateViewport : public ISlateViewport
{
public:
    explicit FWuWaSimulateViewport(TSharedRef<FSceneViewport> InNative) : Native(InNative), Scene(InNative) {}
    virtual FCursorReply OnCursorQuery(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        auto View=Native.Pin();
        auto SceneViewport=Scene.Pin();
        if (!View || !SceneViewport) return FCursorReply::Cursor(EMouseCursor::Default);
        // Hover and captured camera navigation both query the cursor every frame.
        // Neither requires picking. Native mouse clicks still perform exact picking.
        if (GEditor && GEditor->bIsSimulatingInEditor)
        {
            const auto* Client=static_cast<const FEditorViewportClient*>(SceneViewport->GetClient());
            if (Client && SceneViewport->HasMouseCapture() && Client->IsMovingCamera())
                return FCursorReply::Cursor(EMouseCursor::None);
            if (!SceneViewport->IsSoftwareCursorVisible())
                return FCursorReply::Cursor(EMouseCursor::Default);
        }
        return View->OnCursorQuery(Geometry,Event);
    }
    virtual TUniquePtr<UE::Slate::FPaintScope> Paint(const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, const int32 LayerId, const FWidgetStyle& InWidgetStyle, const bool bParentEnabled) override { if (auto View=Native.Pin()) { return View->Paint(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled); } return {}; }
    virtual FIntPoint GetSize() const override { if (auto View=Native.Pin()) { return View->GetSize(); } return FIntPoint::ZeroValue; }
    virtual class FSlateShaderResource* GetViewportRenderTargetTexture() const override { if (auto View=Native.Pin()) { return View->GetViewportRenderTargetTexture(); } return nullptr; }
    virtual EPixelFormat GetSceneTargetFormat() const override { if (auto View=Native.Pin()) { return View->GetSceneTargetFormat(); } return PF_Unknown; }
    virtual bool IsViewportTextureAlphaOnly() const override { if (auto View=Native.Pin()) { return View->IsViewportTextureAlphaOnly(); } return false; }
    virtual bool IsStereoscopic3D() const override { if (auto View=Native.Pin()) { return View->IsStereoscopic3D(); } return false; }
    virtual bool IsCameraCut() const override { if (auto View=Native.Pin()) { return View->IsCameraCut(); } return false; }
    virtual bool UseSeparateRenderTarget() const override { if (auto View=Native.Pin()) { return View->UseSeparateRenderTarget(); } return false; }
    virtual ESlateViewportDynamicRange GetViewportDynamicRange() const override { if (auto View=Native.Pin()) { return View->GetViewportDynamicRange(); } return ESlateViewportDynamicRange::SDR; }
    virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float DeltaTime) override { if (auto View=Native.Pin()) { View->Tick(AllottedGeometry, InCurrentTime, DeltaTime); } return; }
    virtual bool RequiresVsync() const override { if (auto View=Native.Pin()) { return View->RequiresVsync(); } return false; }
    virtual bool AllowScaling() const override { if (auto View=Native.Pin()) { return View->AllowScaling(); } return false; }
    virtual TOptional<TSharedRef<SWidget>> OnMapCursor(const FCursorReply& CursorReply) override { if (auto View=Native.Pin()) { return View->OnMapCursor(CursorReply); } return {}; }
    virtual bool IsSoftwareCursorVisible() const override { if (auto View=Native.Pin()) { return View->IsSoftwareCursorVisible(); } return false; }
    virtual FVector2D GetSoftwareCursorPosition() const override { if (auto View=Native.Pin()) { return View->GetSoftwareCursorPosition(); } return FVector2D::ZeroVector; }
    virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { if (auto View=Native.Pin()) { return View->OnMouseButtonDown(MyGeometry, MouseEvent); } return FReply::Unhandled(); }
    virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { if (auto View=Native.Pin()) { return View->OnMouseButtonUp(MyGeometry, MouseEvent); } return FReply::Unhandled(); }
    virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { if (auto View=Native.Pin()) { View->OnMouseEnter(MyGeometry, MouseEvent); } return; }
    virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override { if (auto View=Native.Pin()) { View->OnMouseLeave(MouseEvent); } return; }
    virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { if (auto View=Native.Pin()) { return View->OnMouseMove(MyGeometry, MouseEvent); } return FReply::Unhandled(); }
    virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override { if (auto View=Native.Pin()) { return View->OnMouseWheel(MyGeometry, MouseEvent); } return FReply::Unhandled(); }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent) override { if (auto View=Native.Pin()) { return View->OnMouseButtonDoubleClick(InMyGeometry, InMouseEvent); } return FReply::Unhandled(); }
    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { if (auto View=Native.Pin()) { return View->OnKeyDown(MyGeometry, InKeyEvent); } return FReply::Unhandled(); }
    virtual FReply OnKeyUp(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override { if (auto View=Native.Pin()) { return View->OnKeyUp(MyGeometry, InKeyEvent); } return FReply::Unhandled(); }
    virtual FReply OnAnalogValueChanged(const FGeometry& MyGeometry, const FAnalogInputEvent& InAnalogInputEvent) override { if (auto View=Native.Pin()) { return View->OnAnalogValueChanged(MyGeometry, InAnalogInputEvent); } return FReply::Unhandled(); }
    virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override { if (auto View=Native.Pin()) { return View->OnKeyChar(MyGeometry, InCharacterEvent); } return FReply::Unhandled(); }
    virtual FReply OnFocusReceived(const FFocusEvent& InFocusEvent) override { if (auto View=Native.Pin()) { return View->OnFocusReceived(InFocusEvent); } return FReply::Unhandled(); }
    virtual FReply OnTouchStarted(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override { if (auto View=Native.Pin()) { return View->OnTouchStarted(MyGeometry, InTouchEvent); } return FReply::Unhandled(); }
    virtual FReply OnTouchMoved(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override { if (auto View=Native.Pin()) { return View->OnTouchMoved(MyGeometry, InTouchEvent); } return FReply::Unhandled(); }
    virtual FReply OnTouchEnded(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override { if (auto View=Native.Pin()) { return View->OnTouchEnded(MyGeometry, InTouchEvent); } return FReply::Unhandled(); }
    virtual FReply OnTouchForceChanged(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override { if (auto View=Native.Pin()) { return View->OnTouchForceChanged(MyGeometry, InTouchEvent); } return FReply::Unhandled(); }
    virtual FReply OnTouchFirstMove(const FGeometry& MyGeometry, const FPointerEvent& InTouchEvent) override { if (auto View=Native.Pin()) { return View->OnTouchFirstMove(MyGeometry, InTouchEvent); } return FReply::Unhandled(); }
    virtual FReply OnTouchGesture(const FGeometry& MyGeometry, const FPointerEvent& InGestureEvent) override { if (auto View=Native.Pin()) { return View->OnTouchGesture(MyGeometry, InGestureEvent); } return FReply::Unhandled(); }
    virtual FReply OnMotionDetected(const FGeometry& MyGeometry, const FMotionEvent& InMotionEvent) override { if (auto View=Native.Pin()) { return View->OnMotionDetected(MyGeometry, InMotionEvent); } return FReply::Unhandled(); }
    virtual TOptional<bool> OnQueryShowFocus(const EFocusCause InFocusCause) const override { if (auto View=Native.Pin()) { return View->OnQueryShowFocus(InFocusCause); } return {}; }
    virtual void OnFinishedPointerInput() override { if (auto View=Native.Pin()) { View->OnFinishedPointerInput(); } return; }
    virtual FPopupMethodReply OnQueryPopupMethod() const override { if (auto View=Native.Pin()) { return View->OnQueryPopupMethod(); } return FPopupMethodReply::Unhandled(); }
    virtual FNavigationReply OnNavigation(const FGeometry& MyGeometry, const FNavigationEvent& InNavigationEvent) override { if (auto View=Native.Pin()) { return View->OnNavigation(MyGeometry, InNavigationEvent); } return FNavigationReply::Stop(); }
    virtual bool HandleNavigation(const uint32 InUserIndex, TSharedPtr<SWidget> InDestination) override { if (auto View=Native.Pin()) { return View->HandleNavigation(InUserIndex, InDestination); } return false; }
    virtual void OnFocusLost(const FFocusEvent& InFocusEvent) override { if (auto View=Native.Pin()) { View->OnFocusLost(InFocusEvent); } return; }
    virtual FReply OnRequestWindowClose() override { if (auto View=Native.Pin()) { return View->OnRequestWindowClose(); } return FReply::Unhandled(); }
    virtual void OnViewportClosed() override { if (auto View=Native.Pin()) { View->OnViewportClosed(); } return; }
    virtual TWeakPtr<SWidget> GetWidget() override { if (auto View=Native.Pin()) { return View->GetWidget(); } return {}; }
    virtual FReply OnViewportActivated(const FWindowActivateEvent& InActivateEvent) override { if (auto View=Native.Pin()) { return View->OnViewportActivated(InActivateEvent); } return FReply::Unhandled(); }
    virtual void OnViewportDeactivated(const FWindowActivateEvent& InActivateEvent) override { if (auto View=Native.Pin()) { View->OnViewportDeactivated(InActivateEvent); } return; }
private:
    TWeakPtr<ISlateViewport> Native;
    TWeakPtr<FSceneViewport> Scene;
};

class FWuWaSimulateNavigation
{
public:
    bool Tick(float)
    {
        auto* Module=FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor"));
        auto Asset=Module ? Module->GetFirstActiveViewport() : nullptr;
        if (!GEditor || !GEditor->bIsSimulatingInEditor || !Asset)
        {
            Restore();
            return true;
        }
        auto Native=Asset->GetSharedActiveViewport();
        auto Widget=Native->GetViewportWidget().Pin();
        if (!Widget) return true;
        if (BoundWidget.Pin()!=Widget || Widget->GetViewportInterface().Pin()!=Proxy)
        {
            Restore();
            Original=Widget->GetViewportInterface().Pin();
            if (Original.Pin()!=StaticCastSharedPtr<ISlateViewport>(Native)) return true;
            Proxy=MakeShared<FWuWaSimulateViewport>(Native.ToSharedRef());
            BoundWidget=Widget;
            Widget->SetViewportInterface(Proxy.ToSharedRef());
        }
        return true;
    }
    void Restore()
    {
        if (auto Widget=BoundWidget.Pin())
            if (auto View=Original.Pin())
                if (Widget->GetViewportInterface().Pin()==Proxy) Widget->SetViewportInterface(View.ToSharedRef());
        BoundWidget.Reset();Original.Reset();Proxy.Reset();
    }
private:
    TWeakPtr<SViewport> BoundWidget;
    TWeakPtr<ISlateViewport> Original;
    TSharedPtr<FWuWaSimulateViewport> Proxy;
};

class FWuWaCharacterLabEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        PreSaveHandle=FEditorDelegates::PreSaveWorldWithContext.AddLambda([](UWorld* World,FObjectPreSaveContext)
        { if (World) if (auto* Rendering=World->GetSubsystem<UWuWaRenderingSubsystem>()) Rendering->SuspendAuthoring(); });
        PostSaveHandle=FEditorDelegates::PostSaveWorldWithContext.AddLambda([](UWorld* World,FObjectPostSaveContext)
        { if (World) if (auto* Rendering=World->GetSubsystem<UWuWaRenderingSubsystem>()) Rendering->ResumeAuthoring(); });
        PrePIEHandle=FEditorDelegates::PreBeginPIE.AddLambda([this](bool)
        {
            if (GEditor && !DuplicatingWorld.IsValid())
            {
                DuplicatingWorld=GEditor->GetEditorWorldContext().World();
                if (auto* World=DuplicatingWorld.Get())
                    if (auto* Rendering=World->GetSubsystem<UWuWaRenderingSubsystem>()) Rendering->SuspendAuthoring();
            }
        });
        EndPIEHandle=FEditorDelegates::EndPIE.AddLambda([this](bool){ResumeEditorWorld();});
        CancelPIEHandle=FEditorDelegates::CancelPIE.AddLambda([this](){ResumeEditorWorld();});
        if (!IsRunningCommandlet())
        {
            ParameterEditor.Startup();
            SimulateNavigation=MakeShared<FWuWaSimulateNavigation>();
            NavigationTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateSP(SimulateNavigation.ToSharedRef(), &FWuWaSimulateNavigation::Tick));
        }
        FString Display;
        if (IsRunningCommandlet() || !FParse::Value(FCommandLine::Get(), TEXT("WuWaEditorDisplay="), Display)
            || !Display.Equals(TEXT("Right"), ESearchCase::IgnoreCase))
        {
            return;
        }
        if (!ConfigureWindow())
        {
            return;
        }
        // MainFrame reads RootWindow before it creates and shows the window.
        // Set the current desktop coordinates before that first visible frame.
        GConfig->SetString(TEXT("RootWindow"), TEXT("ScreenPosition"), *RequestedPosition.ToString(), GEditorPerProjectIni);
        GConfig->SetString(TEXT("RootWindow"), TEXT("WindowSize"), *RequestedSize.ToString(), GEditorPerProjectIni);
        GConfig->SetBool(TEXT("RootWindow"), TEXT("InitiallyMaximized"), false, GEditorPerProjectIni);
        IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>(TEXT("MainFrame"));
        MainFrameHandle = MainFrame.OnMainFrameCreationFinished().AddRaw(this, &FWuWaCharacterLabEditorModule::PositionWindow);
        if (MainFrame.GetParentWindow().IsValid())
        {
            PositionWindow(MainFrame.GetParentWindow(), false);
        }
    }

    virtual void ShutdownModule() override
    {
        FEditorDelegates::PreSaveWorldWithContext.Remove(PreSaveHandle);
        FEditorDelegates::PostSaveWorldWithContext.Remove(PostSaveHandle);
        FEditorDelegates::PreBeginPIE.Remove(PrePIEHandle);
        FEditorDelegates::EndPIE.Remove(EndPIEHandle);
        FEditorDelegates::CancelPIE.Remove(CancelPIEHandle);
        ResumeEditorWorld();
        if (SimulateNavigation)
        {
            SimulateNavigation->Restore();
            FTSTicker::GetCoreTicker().RemoveTicker(NavigationTicker);
            SimulateNavigation.Reset();
        }
        ParameterEditor.Shutdown();
        if (IMainFrameModule* MainFrame = FModuleManager::GetModulePtr<IMainFrameModule>(TEXT("MainFrame")))
        {
            MainFrame->OnMainFrameCreationFinished().Remove(MainFrameHandle);
        }
        FTSTicker::GetCoreTicker().RemoveTicker(PlacementTicker);
    }

private:
    FDelegateHandle PreSaveHandle,PostSaveHandle,PrePIEHandle,EndPIEHandle,CancelPIEHandle;
    TWeakObjectPtr<UWorld> DuplicatingWorld;
    void ResumeEditorWorld()
    {
        if (auto* World=DuplicatingWorld.Get())
            if (auto* Rendering=World->GetSubsystem<UWuWaRenderingSubsystem>()) Rendering->ResumeAuthoring();
        DuplicatingWorld.Reset();
    }
    TSharedPtr<FWuWaSimulateNavigation> SimulateNavigation;
    FTSTicker::FDelegateHandle NavigationTicker;
    FWuWaMaterialParameterEditor ParameterEditor;
    bool ConfigureWindow()
    {
        FDisplayMetrics Metrics;
        FSlateApplication::Get().GetCachedDisplayMetrics(Metrics);
        if (Metrics.MonitorInfo.Num() < 2)
        {
            UE_LOG(LogWuWaEditor, Error, TEXT("WUWA_EDITOR_WINDOW_FAILED: Right display is unavailable"));
            return false;
        }
        int32 RightIndex = 0;
        for (int32 Index = 1; Index < Metrics.MonitorInfo.Num(); ++Index)
        {
            if (Metrics.MonitorInfo[Index].WorkArea.Left > Metrics.MonitorInfo[RightIndex].WorkArea.Left)
            {
                RightIndex = Index;
            }
        }
        const FPlatformRect& Area = Metrics.MonitorInfo[RightIndex].WorkArea;
        TargetArea = FIntRect(Area.Left, Area.Top, Area.Right, Area.Bottom);
        RequestedSize = FVector2D(FMath::Min(1920, TargetArea.Width() - 64), FMath::Min(1080, TargetArea.Height() - 64));
        RequestedPosition = FVector2D(Area.Left + (TargetArea.Width() - RequestedSize.X) * 0.5,
            Area.Top + (TargetArea.Height() - RequestedSize.Y) * 0.5);
        return true;
    }

    void PositionWindow(TSharedPtr<SWindow> Window, bool)
    {
        if (!Window.IsValid())
        {
            return;
        }
        Window->Restore();
        Window->ReshapeWindow(RequestedPosition, RequestedSize);
        MainWindow = Window;
        VerifyWindow(0.0f);
        FTSTicker::GetCoreTicker().RemoveTicker(PlacementTicker);
        PlacementTicker = FTSTicker::GetCoreTicker().AddTicker(
            FTickerDelegate::CreateRaw(this, &FWuWaCharacterLabEditorModule::VerifyWindow), 2.0f);
    }

    bool VerifyWindow(float)
    {
        const TSharedPtr<SWindow> Window = MainWindow.Pin();
        const TSharedPtr<FGenericWindow> NativeWindow = Window.IsValid() ? Window->GetNativeWindow() : nullptr;
        const FVector2D Position = Window.IsValid() ? FVector2D(Window->GetPositionInScreen()) : FVector2D::ZeroVector;
        const FVector2D Size = Window.IsValid() ? FVector2D(Window->GetSizeInScreen()) : FVector2D::ZeroVector;
        int32 X = 0, Y = 0, Width = 0, Height = 0;
        if (NativeWindow.IsValid() && NativeWindow->GetRestoredDimensions(X, Y, Width, Height)
            && X >= TargetArea.Min.X && Y >= TargetArea.Min.Y
            && X + Width <= TargetArea.Max.X && Y + Height <= TargetArea.Max.Y
            && Position.X >= TargetArea.Min.X && Position.Y >= TargetArea.Min.Y
            && Position.X + Size.X <= TargetArea.Max.X && Position.Y + Size.Y <= TargetArea.Max.Y)
        {
            UE_LOG(LogWuWaEditor, Display,
                TEXT("WUWA_EDITOR_WINDOW_VERIFIED Process=%u X=%d Y=%d Width=%d Height=%d Target=(%d,%d,%d,%d) Slate=(%.0f,%.0f,%.0f,%.0f)"),
                FPlatformProcess::GetCurrentProcessId(), X, Y, Width, Height,
                TargetArea.Min.X, TargetArea.Min.Y, TargetArea.Max.X, TargetArea.Max.Y,
                Position.X, Position.Y, Size.X, Size.Y);
        }
        else
        {
            UE_LOG(LogWuWaEditor, Error, TEXT("WUWA_EDITOR_WINDOW_FAILED: Native window is outside the right display"));
        }
        return false;
    }

    FDelegateHandle MainFrameHandle;
    FTSTicker::FDelegateHandle PlacementTicker;
    TWeakPtr<SWindow> MainWindow;
    FIntRect TargetArea;
    FVector2D RequestedPosition;
    FVector2D RequestedSize;
};

IMPLEMENT_MODULE(FWuWaCharacterLabEditorModule, WuWaCharacterLabEditor);
