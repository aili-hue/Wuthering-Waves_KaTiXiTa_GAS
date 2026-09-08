// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_DoubleJump.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_DoubleJump : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	
	UGA_DoubleJump();
	
	UPROPERTY(EditAnywhere,Category="Montage")
	TObjectPtr<UAnimMontage> Jump_Second_F;
	
	UPROPERTY(EditAnywhere,Category="Montage")
	TObjectPtr<UAnimMontage> Jump_Second_B;
	
	UFUNCTION()
	void PlayMontageJump(UAnimMontage* MontageToPlay);
	
	UFUNCTION()
	void EndMontage();
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
