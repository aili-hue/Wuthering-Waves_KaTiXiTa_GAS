#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "WuWaShowcasePlayerController.generated.h"

class AWuWaShowcaseCharacter;
class ACameraActor;
class ADirectionalLight;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** Enhanced Input operates only the explicitly bound showcase scene. */
UCLASS()
class WUWACHARACTERLAB_API AWuWaShowcasePlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AWuWaShowcasePlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void SetupInputComponent() override;
    UFUNCTION(BlueprintCallable, Category="Showcase|Input") FString GetConfigurationError() const;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputMappingContext> InputMapping;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> PointerAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> OrbitDragAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> PanDragAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> LightModifierAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> ZoomAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> PauseAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") TObjectPtr<UInputAction> ResetAction;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Input") FName CharacterTag = TEXT("ShowcaseCharacter");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Camera", meta=(ClampMin="1")) float MinimumDistance = 80;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Camera", meta=(ClampMin="1")) float MaximumDistance = 1200;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Camera", meta=(ClampMin="0.001", DisplayName="水平旋转灵敏度", ToolTip="鼠标水平移动每单位产生的环绕角度。")) float HorizontalOrbitSensitivity = 0.5f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Camera", meta=(ClampMin="0.001")) float OrbitSensitivity = 0.25f;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Showcase|Camera", meta=(ClampMin="0.001")) float ZoomSensitivity = 0.12f;

private:
    bool ValidateConfiguration(FString& Error) const;
    void Pointer(const FInputActionValue& Value);
    void Zoom(const FInputActionValue& Value);
    void ToggleAnimation();
    void ResetPresentation();
    void ApplyCamera();

    TWeakObjectPtr<AWuWaShowcaseCharacter> TargetCharacter;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<ADirectionalLight> Light;
    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> InstalledMapping;
    FTransform InitialCameraTransform;
    FVector InitialFocus = FVector::ZeroVector;
    FVector Focus = FVector::ZeroVector;
    FRotator InitialLightRotation = FRotator::ZeroRotator;
    FRotator OrbitRotation = FRotator::ZeroRotator;
    float Distance = 0;
    float InitialDistance = 0;
};
