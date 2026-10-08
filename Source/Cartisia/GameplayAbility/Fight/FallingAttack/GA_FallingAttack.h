// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_FallingAttack.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_FallingAttack : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_FallingAttack();
	
	UPROPERTY(EditDefaultsOnly, Category = "AnimMontage")
	TObjectPtr<UAnimMontage>JumpAnimMontage;
	
	UPROPERTY(EditDefaultsOnly, Category = "AnimMontage")
	TObjectPtr<UAnimMontage>LoopAnimMontage;
	
	UPROPERTY(EditDefaultsOnly, Category = "AnimMontage")
	TObjectPtr<UAnimMontage>EndAnimMontage;
	
	void PlayMontage(UAnimMontage* MontageToPlay);
	
	UFUNCTION()
	void PlayLoopAnimMontage();
	
	UFUNCTION()
	void EndAttack();
	
	UFUNCTION()
	void EndAnimMontageAttack();
	
	FGameplayTag Data_FallingTag= FGameplayTag::RequestGameplayTag(FName("Data.Airborne.Falling"));
	
	UFUNCTION()
	void PlayEndAnimMontage(FGameplayTag EndTag, int32 Number);
	
	uint8 bCanEndAbility : 1 =true;
	
	FDelegateHandle EndHandle;
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
