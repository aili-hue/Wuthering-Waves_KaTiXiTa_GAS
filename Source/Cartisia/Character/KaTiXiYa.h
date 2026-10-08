// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayTagContainer.h"
#include "KaTiXiYa.generated.h"

struct FGameplayAbilitySpecHandle;
class UDA_AbilitySet;
class UGameplayEffect;
struct FGameplayTag;
class UPlayerAttributes;
class UGameplayAbility;
struct FInputActionValue;
class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UAbilitySystemComponent;

UENUM(BlueprintType)
enum class ELandedEnum :uint8
{
	Land_Heavy UMETA(DisplayName="重着陆"),
	Land_Light UMETA(DisplayName="轻着陆地"),
	Land_Roll UMETA(DisplayName="着陆滚动")
};

UCLASS()
class CARTISIA_API AKaTiXiYa : public ACharacter , public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	// Sets default values for this character's properties
	AKaTiXiYa();
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="SpringArm")
	TObjectPtr<USpringArmComponent>SpringArmComponent;
	
protected:
	
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	
	//Mesh
	
	UPROPERTY(EditAnywhere)
	TObjectPtr<USkeletalMeshComponent>SkeletalMesh;
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<AActor>WeaponMesh;
	
	UPROPERTY(EditAnywhere)
	TSubclassOf<AActor>SwordMesh;
	
	UPROPERTY()
	TObjectPtr<AActor>AttachmentActor;
	
	UPROPERTY(EditAnywhere,Category="MeshName")
	FName WeaponSocket;
	
	UPROPERTY(EditAnywhere,Category="MeshName")
	FName SwordName;
	
	void SpawnAttachmentActor(TSubclassOf<AActor> WeaponMeshs,FName SocketName);
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Mesh")
	TObjectPtr<USkeletalMeshComponent> DollMesh;
	
	//lens
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Camera")
	TObjectPtr<UCameraComponent> CameraComponent;
	
	UPROPERTY(EditAnywhere,Category="SpringArm")
	float DefaultLength= 150.f;
	
	UPROPERTY(EditAnywhere,Category="SpringArm")
	float TargetLength= 150.f;
	
	UPROPERTY(EditAnywhere,Category="SpringArm")
	float MaxTargetLength= 500.f;
	
	//Input
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputMappingContext")
	TObjectPtr<UInputMappingContext>IMC_Foundation;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Move;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Perspective;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Right_MouseButton;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Shift_L;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Shift_R;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Ctrl;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Mouse_Wheel;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Space;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Left_MouseButton;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_E;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="InputAction")
	TObjectPtr<UInputAction> IA_Q;
	
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
	
	UFUNCTION()
	void AttackInputStarted(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void AttackInputHold(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void AttackInputReleased(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void E_Event(const FInputActionValue&InputEvent);
	
	UFUNCTION()
	void Q_Event(const FInputActionValue&InputEvent);
	
	//Landed
	
	float LandedTime= 0.f;
	
	void LandedEvent();
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Landed")
	ELandedEnum LandedEnum= ELandedEnum::Land_Light;
	
	//GAS
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay")
	TObjectPtr<UAbilitySystemComponent>AbilitySystemComponent;
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Attributes")
	TObjectPtr<UPlayerAttributes>PlayerAttributes;
	
	//GameplayTag
	
	//Ability
	FGameplayTag Ability_SprintTag= FGameplayTag::RequestGameplayTag(FName("Ability.Sprint"));
	FGameplayTag Ability_StopWalkingTag= FGameplayTag::RequestGameplayTag(FName("Ability.StopWalking"));
	FGameplayTag Ability_WalkTag= FGameplayTag::RequestGameplayTag(FName("Ability.Walk"));
	FGameplayTag Ability_Jump= FGameplayTag::RequestGameplayTag(FName("Ability.Jump"));
	FGameplayTag Ability_LandedTag= FGameplayTag::RequestGameplayTag(FName("Ability.Landed"));
	FGameplayTag Ability_DoubleJumpTag=FGameplayTag::RequestGameplayTag(FName("Ability.DoubleJump"));
	FGameplayTag Ability_SkillTag= FGameplayTag::RequestGameplayTag(FName("Ability.Skill"));
	FGameplayTag Ability_UltimateTag= FGameplayTag::RequestGameplayTag(FName("Ability.Ultimate"));
	
	//Ability_Fight
	FGameplayTag Ability_Fight_NormalAttack= FGameplayTag::RequestGameplayTag(FName("Ability.Fight.NormalAttack"));
	FGameplayTag Ability_Fight_HeavyBlow= FGameplayTag::RequestGameplayTag(FName("Ability.Fight.HeavyBlow"));
	
	//Data
	FGameplayTag Data_StopTag= FGameplayTag::RequestGameplayTag(FName("Data.Stop"));
	FGameplayTag Data_LandedTag= FGameplayTag::RequestGameplayTag(FName("Data.Landed"));
	FGameplayTag Data_FallingTag= FGameplayTag::RequestGameplayTag(FName("Data.Airborne.Falling"));
	FGameplayTag Data_MovingTag= FGameplayTag::RequestGameplayTag(FName("Data.Moving"));
	FGameplayTag Data_AttackTag= FGameplayTag::RequestGameplayTag(FName("Data.Attack"));
	FGameplayTag Data_StopGATag= FGameplayTag::RequestGameplayTag(FName("Data.StopGA"));
	FGameplayTag Data_JumpTag= FGameplayTag::RequestGameplayTag(FName("Data.Jump"));
	FGameplayTag Data_FlyingTag= FGameplayTag::RequestGameplayTag(FName("Data.Airborne.Flying"));
	
	//InterruptAnimation
	
	UPROPERTY(EditAnywhere,Category="CancelSkill_AbilityTag")
	TArray<FGameplayTag> AbilityTag;
	
	//Stop_GE_Buff
	void SpeedSwitching();
	
	//State
	
	//常规移动打断
	FGameplayTag State_InterruptibleTag= FGameplayTag::RequestGameplayTag(FName("State.Interruptible"));
	//连招打断
	FGameplayTag State_ContinuousInterruption=FGameplayTag::RequestGameplayTag(FName("State.ContinuousInterruption"));
	//切换模型
	FGameplayTag State_Visual_Doll=FGameplayTag::RequestGameplayTag(FName("State.Visual.Doll"));
	//变身
	FGameplayTag State_Form_UltimateTag= FGameplayTag::RequestGameplayTag(FName("State.Form.Ultimate"));
	
	UFUNCTION()
	void DollEvent(FGameplayTag EventTag,int32 Number);
	
	FDelegateHandle DollHandle;
	
	//Event
	FGameplayTag Event_AbilityJumpTag= FGameplayTag::RequestGameplayTag(FName("Event.EndAbilityJump"));
	FGameplayTag Event_Attack= FGameplayTag::RequestGameplayTag(FName("Event.Attack"));
	
	//用于打断当前GA
	UFUNCTION(BlueprintCallable,Category="Animation")
	void InterruptAnimation(FGameplayTag AbilityAnimationTag);
	
	//由角色本身产生的标签Buff状态，比如:Jumping,Moving 而产生的 buff
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="GameplayEffect")
	TMap<FGameplayTag,TSubclassOf<UGameplayEffect>> BuffEffect;
	
	//FActiveGameplayEffectHandle
	
	void Data_LandedEvent();
	
	UPROPERTY()
	FActiveGameplayEffectHandle LandedHandle;
	
	UPROPERTY()
	FActiveGameplayEffectHandle FallingHandle;
	
	UPROPERTY()
	FActiveGameplayEffectHandle FlingHandle;
	
	UPROPERTY()
	FActiveGameplayEffectHandle MovingHandle;
	
	// Landing Time
	
	UPROPERTY(EditAnywhere,Category="LandingTime")
	float Land_RollTime= 1.3f;
	
	UPROPERTY(EditAnywhere,Category="LandingTime")
	float Land_LightTime= 1.f;
	
	//AttackTime
	float AttackStartTime = 0.f;
	
	//重击蓄力所需时间
	UPROPERTY(EditAnywhere,Category="Combat")
	float HeavyAttackThreshold = 0.3f;
	
	//是否重击
	uint8 bHeavyAttackAutoTriggered :1= false;
	
	//技能组
	
	UPROPERTY(EditAnywhere,Category="AbilitySet")
	TObjectPtr<UDA_AbilitySet>BaseAbilitySet;
	
	UPROPERTY(EditDefaultsOnly, Category = "AbilitySet")
	TObjectPtr<UDA_AbilitySet> UltimateAbilitySet;

	// 切换技能组
	void SwitchAbilitySet(UDA_AbilitySet* NewAbilitySet);
	
	// 记录当前已授予的技能 Handle，用于下次切换时回收
	TArray<FGameplayAbilitySpecHandle> CurrentAbilityHandles;
	
	UFUNCTION()
	void OnUltimateFormChanged(FGameplayTag EventTag,int32 NewValue);
	
	FDelegateHandle UltimateFormHandle;
	
public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	virtual void PossessedBy(AController* NewController)override;
	
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode = 0) override;
	
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};
