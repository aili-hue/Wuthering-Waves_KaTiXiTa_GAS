#include "WuWaShowcasePlayerController.h"
#include "WuWaShowcaseCharacter.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"

DEFINE_LOG_CATEGORY_STATIC(LogWuWaShowcase, Log, All);

AWuWaShowcasePlayerController::AWuWaShowcasePlayerController()
{
    bShowMouseCursor = true;
    bAutoManageActiveCameraTarget = false;
}

bool AWuWaShowcasePlayerController::ValidateConfiguration(FString& Error) const
{
    if (CharacterTag.IsNone()) { Error = TEXT("Showcase character tag is empty"); return false; }
    if (!InputMapping) { Error = TEXT("Showcase input mapping is missing"); return false; }
    TSet<const UInputAction*> Seen;
    for (const UInputAction* Action : {PointerAction.Get(), OrbitDragAction.Get(), PanDragAction.Get(),
        LightModifierAction.Get(), ZoomAction.Get(), PauseAction.Get(), ResetAction.Get()})
    {
        if (!Action || Seen.Contains(Action)) { Error = TEXT("Showcase actions are missing or duplicated"); return false; }
        Seen.Add(Action);
        const EInputActionValueType Expected = Action == PointerAction.Get() ? EInputActionValueType::Axis2D
            : Action == ZoomAction.Get() ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
        if (Action->ValueType != Expected || !InputMapping->GetMappings().ContainsByPredicate(
            [Action](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action; }))
        { Error = TEXT("Showcase action type or mapping is invalid: ") + GetNameSafe(Action); return false; }
    }
    if (!FMath::IsFinite(MinimumDistance) || !FMath::IsFinite(MaximumDistance) || MinimumDistance <= 0 || MaximumDistance < MinimumDistance
        || !FMath::IsFinite(HorizontalOrbitSensitivity) || HorizontalOrbitSensitivity <= 0
        || !FMath::IsFinite(OrbitSensitivity) || OrbitSensitivity <= 0 || !FMath::IsFinite(ZoomSensitivity) || ZoomSensitivity <= 0)
    { Error = TEXT("Showcase camera limits or sensitivity are invalid"); return false; }
    return true;
}

FString AWuWaShowcasePlayerController::GetConfigurationError() const
{
    FString Error;
    ValidateConfiguration(Error);
    if (Error.IsEmpty() && HasActorBegunPlay() && IsLocalController())
    {
        if (!Cast<UEnhancedPlayerInput>(PlayerInput))
            Error = TEXT("Showcase requires EnhancedPlayerInput at runtime");
        else if (!Cast<UEnhancedInputComponent>(InputComponent))
            Error = TEXT("Showcase requires an Enhanced Input component at runtime");
        else if (!TargetCharacter.IsValid() || !Camera.IsValid())
            Error = TEXT("Showcase runtime character or camera binding is missing");
    }
    return Error;
}

void AWuWaShowcasePlayerController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;
    FString Error;
    if (!ValidateConfiguration(Error)) { UE_LOG(LogWuWaShowcase, Error, TEXT("%s"), *Error); return; }
    int32 Count = 0;
    for (TActorIterator<AWuWaShowcaseCharacter> It(GetWorld()); It; ++It)
        if (It->ActorHasTag(CharacterTag)) { TargetCharacter = *It; ++Count; }
    if (Count != 1 || !IsValid(TargetCharacter->ShowcaseCamera.Get()) || TargetCharacter->ShowcaseCamera->GetWorld() != GetWorld()
        || (IsValid(TargetCharacter->KeyLight.Get()) && TargetCharacter->KeyLight->GetWorld() != GetWorld()))
    { UE_LOG(LogWuWaShowcase, Error, TEXT("Showcase requires one tagged character with a saved camera reference")); TargetCharacter.Reset(); return; }
    if (TargetCharacter->ActiveCharacter) TargetCharacter->SelectCharacter(TargetCharacter->ActiveCharacter);
    Camera = TargetCharacter->ShowcaseCamera.Get();
    Light = TargetCharacter->KeyLight.Get();
    InitialCameraTransform = Camera->GetActorTransform();
    OrbitRotation = InitialCameraTransform.Rotator();
    InitialDistance = FMath::Clamp(FVector::DotProduct(TargetCharacter->GetSkeletalMeshComponent()->Bounds.Origin
        - InitialCameraTransform.GetLocation(), OrbitRotation.Vector()), double(MinimumDistance), double(MaximumDistance));
    Distance = InitialDistance;
    InitialFocus = InitialCameraTransform.GetLocation() + OrbitRotation.Vector() * Distance;
    Focus = InitialFocus;
    if (Light.IsValid()) InitialLightRotation = Light->GetActorRotation();
    ULocalPlayer* LocalPlayer = GetLocalPlayer();
    auto* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
    if (!Subsystem || !Cast<UEnhancedInputComponent>(InputComponent) || !Cast<UEnhancedPlayerInput>(PlayerInput))
    { UE_LOG(LogWuWaShowcase, Error, TEXT("Showcase requires a local Enhanced Input subsystem and component")); return; }
    if (!Subsystem->HasMappingContext(InputMapping))
    {
        Subsystem->AddMappingContext(InputMapping, 0);
        InstalledMapping = InputMapping;
    }
    SetViewTarget(Camera.Get());
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
}

void AWuWaShowcasePlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if (ULocalPlayer* LocalPlayer = GetLocalPlayer())
        if (auto* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            if (InstalledMapping) Subsystem->RemoveMappingContext(InstalledMapping);
    InstalledMapping = nullptr;
    Super::EndPlay(Reason);
}

void AWuWaShowcasePlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();
    auto* Input = Cast<UEnhancedInputComponent>(InputComponent);
    if (!Input) return;
    if (PointerAction) Input->BindAction(PointerAction, ETriggerEvent::Triggered, this, &ThisClass::Pointer);
    if (ZoomAction) Input->BindAction(ZoomAction, ETriggerEvent::Triggered, this, &ThisClass::Zoom);
    if (PauseAction) Input->BindAction(PauseAction, ETriggerEvent::Started, this, &ThisClass::ToggleAnimation);
    if (ResetAction) Input->BindAction(ResetAction, ETriggerEvent::Started, this, &ThisClass::ResetPresentation);
    if (OrbitDragAction) Input->BindActionValue(OrbitDragAction);
    if (PanDragAction) Input->BindActionValue(PanDragAction);
    if (LightModifierAction) Input->BindActionValue(LightModifierAction);
}

void AWuWaShowcasePlayerController::ApplyCamera()
{
    if (Camera.IsValid()) Camera->SetActorLocationAndRotation(Focus - OrbitRotation.Vector() * Distance, OrbitRotation);
}

void AWuWaShowcasePlayerController::Pointer(const FInputActionValue& Value)
{
    if (!Camera.IsValid()) return;
    const FVector2D Delta = Value.Get<FVector2D>();
    if (!FMath::IsFinite(Delta.X) || !FMath::IsFinite(Delta.Y)) return;
    const auto* Input = Cast<UEnhancedPlayerInput>(PlayerInput);
    if (!Input) return;
    // Read this frame's evaluated action values, avoiding delegate-order lag.
    const bool bPanHeld = Input->GetActionValue(PanDragAction).Get<bool>();
    const bool bOrbitHeld = Input->GetActionValue(OrbitDragAction).Get<bool>();
    const bool bLightModifierHeld = Input->GetActionValue(LightModifierAction).Get<bool>();
    if (bPanHeld)
    {
        int32 Width, Height; GetViewportSize(Width, Height);
        if (Width <= 0) return;
        const float FieldOfView = Camera->GetCameraComponent()->FieldOfView;
        if (!FMath::IsFinite(FieldOfView) || FieldOfView <= 0 || FieldOfView >= 179) return;
        const double UnitsPerPixel = 2 * Distance * FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5)) / Width;
        Focus += (Camera->GetActorRightVector() * -Delta.X + Camera->GetActorUpVector() * Delta.Y) * UnitsPerPixel;
        ApplyCamera();
    }
    else if (bOrbitHeld && bLightModifierHeld && Light.IsValid())
    {
        FRotator Rotation = Light->GetActorRotation();
        Rotation.Yaw = FMath::UnwindDegrees(Rotation.Yaw + Delta.X * OrbitSensitivity);
        Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Delta.Y * OrbitSensitivity, -89.0, 89.0);
        Light->SetActorRotation(Rotation);
    }
    else if (bOrbitHeld && !bLightModifierHeld)
    {
        OrbitRotation.Yaw = FMath::UnwindDegrees(OrbitRotation.Yaw + Delta.X * HorizontalOrbitSensitivity);
        OrbitRotation.Pitch = FMath::Clamp(OrbitRotation.Pitch + Delta.Y * OrbitSensitivity, -80.0, 80.0);
        ApplyCamera();
    }
}

void AWuWaShowcasePlayerController::Zoom(const FInputActionValue& Value)
{
    const float Axis = Value.Get<float>();
    if (!Camera.IsValid() || !FMath::IsFinite(Axis)) return;
    Distance = FMath::Clamp(Distance * FMath::Exp(FMath::Clamp(-Axis * ZoomSensitivity, -10.0f, 10.0f)), MinimumDistance, MaximumDistance);
    ApplyCamera();
}

void AWuWaShowcasePlayerController::ToggleAnimation()
{
    if (TargetCharacter.IsValid())
    {
        USkeletalMeshComponent* Mesh = TargetCharacter->GetSkeletalMeshComponent();
        Mesh->bPauseAnims = !Mesh->bPauseAnims;
    }
}

void AWuWaShowcasePlayerController::ResetPresentation()
{
    if (!Camera.IsValid()) return;
    Camera->SetActorTransform(InitialCameraTransform);
    OrbitRotation = InitialCameraTransform.Rotator();
    Focus = InitialFocus;
    Distance = InitialDistance;
    if (Light.IsValid()) Light->SetActorRotation(InitialLightRotation);
}
