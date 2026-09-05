// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "KaTiXiYa.generated.h"

struct FGameplayTag;
class UPlayerAttributes;
class UGameplayAbility;
struct FInputActionValue;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UAbilitySystemComponent;

UCLASS()
class CARTISIA_API AKaTiXiYa : public ACharacter , public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AKaTiXiYa();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	//lens
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SpringArm")
	TObjectPtr<USpringArmComponent>SpringArmComponent;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Camera")
	TObjectPtr<UCameraComponent> CameraComponent;
	
	float DefaultLength= 150.f;
	float TargetLength= 150.f;
	
	FTimerHandle TimerHandle;
	
	void StartTimer();
	void StopTimer();
	
	//Input
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputMappingContext")
	TObjectPtr<UInputMappingContext>IMC_Foundation;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Move;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Perspective;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_RightMouseButton;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Shift_L;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Shift_R;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Ctrl;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_MouseWheel;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Space;
	
	//Init
	UFUNCTION()
	void InitInputMappingContext();
	
	//InputEvent
	
	UFUNCTION()
	void MoveInputEvent(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void PerspectiveEvent(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void SprintEvent(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void EndMoveInputEvent(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void CtrlEvent(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void MouseWheelEvent(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void SpaceEvent(const FInputActionValue&InputEvent);
	
	//GAS
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay")
	TObjectPtr<UAbilitySystemComponent>AbilitySystemComponent;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Attributes")
	TObjectPtr<UPlayerAttributes>PlayerAttributes;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="GameplayAbility")
	TArray<TSubclassOf<UGameplayAbility>> GameplayAbility;
	
	//GameplayTag
	FGameplayTag Ability_SprintTag= FGameplayTag::RequestGameplayTag(FName("Ability.Sprint"));
	FGameplayTag Ability_StopWalkingTag= FGameplayTag::RequestGameplayTag(FName("Ability.StopWalking"));
	FGameplayTag Ability_WalkTag= FGameplayTag::RequestGameplayTag(FName("Ability.Walk"));
	FGameplayTag Ability_Jump= FGameplayTag::RequestGameplayTag(FName("Ability.Jump"));
	
	FGameplayTag State_InterruptibleTag= FGameplayTag::RequestGameplayTag(FName("State.Interruptible"));
	
	FGameplayTag Event_AbilityJumpTag= FGameplayTag::RequestGameplayTag(FName("Event.EndAbilityJump"));
	
	UFUNCTION()
	void InterruptAnimation();
	
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	virtual void PossessedBy(AController* NewController)override;
	
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
	virtual void Landed(const FHitResult& Hit) override;
};
