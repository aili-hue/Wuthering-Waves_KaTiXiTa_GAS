// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_HeavyBlow.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_HeavyBlow : public UGameplayAbility
{
	GENERATED_BODY()
protected:
	
	UGA_HeavyBlow();
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>Montage;
	
	UFUNCTION()
	void PlayMontage(UAnimMontage* AnimMontage);
	
	UFUNCTION()
	void EndMontage();
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
