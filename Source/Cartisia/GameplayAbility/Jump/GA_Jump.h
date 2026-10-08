// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Jump.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_Jump : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	
	UGA_Jump();
	
	UPROPERTY(EditAnywhere, Category = "GamepalyEffect")
	TSubclassOf<UGameplayEffect> JumpEffect;
	
	//连招GE
	UPROPERTY(EditAnywhere, Category = "GamepalyEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	//接受角色发送的允许连招Tag
	FGameplayTag Event_Attack= FGameplayTag::RequestGameplayTag(FName("Event.Attack"));
	
	UFUNCTION()
	void WaitGameplayEvent(FGameplayEventData Data);
	
	FGameplayTag Ability_Fight_AirAttack= FGameplayTag::RequestGameplayTag(FName("Ability.Fight.AirAttack"));
	
	FActiveGameplayEffectHandle EffectHandle;
	FActiveGameplayEffectHandle GameplayEffectHandle;
	
	FGameplayTag Event_AbilityJumpTag= FGameplayTag::RequestGameplayTag(FName("Event.EndAbilityJump"));
	FGameplayTag Data_FallingTag= FGameplayTag::RequestGameplayTag(FName("Data.Airborne.Falling"));
	FGameplayTag Data_FlyingTag= FGameplayTag::RequestGameplayTag(FName("Data.Airborne.Flying"));
	
	UPROPERTY(EditAnywhere, Category = "AnimMontage")
	TObjectPtr<UAnimMontage>JumpAnimMontage;
	
	UPROPERTY(EditAnywhere, Category = "AnimMontage")
	TObjectPtr<UAnimMontage> LoopAnimMontage;
	
	UPROPERTY(EditAnywhere, Category = "AnimMontage")
	TObjectPtr<UAnimMontage> LandAnimMontage;
	
	UFUNCTION()
	void LandMontage();
	
	/*void PlayLandMontage(UAnimMontage* MontageToPlay);*/
	
	UFUNCTION()
	void LoopPlayMontage();
	
	UFUNCTION()
	void EndLandMontage();
	
	uint8 bLand :1 = false;
	
	UFUNCTION()
	void EndJumpMontage();
	
	FDelegateHandle LandHandle;
	
	UPROPERTY(EditAnywhere, Category = "Jump")
	bool bPhysicaljumps= false;
	
	void PlayMontage(UAnimMontage* MontageToPlay);
	
	UFUNCTION()
	void EndMontage(FGameplayEventData Data);
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
