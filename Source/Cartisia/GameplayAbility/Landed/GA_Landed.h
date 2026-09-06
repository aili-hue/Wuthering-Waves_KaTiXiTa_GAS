// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Landed.generated.h"

class UAbilityTask_PlayMontageAndWait;
/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_Landed : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Landed();
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>AnimMontage;
	
	UFUNCTION()
	void PlayMontage(UAnimMontage* MontageToPlay);
	
	UFUNCTION()
	void EndMontage();
	
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait>PlayMontageAndWait;
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
