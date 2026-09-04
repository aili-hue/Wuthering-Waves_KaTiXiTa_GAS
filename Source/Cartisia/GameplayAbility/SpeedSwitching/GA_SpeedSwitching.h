// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_SpeedSwitching.generated.h"

class UAbilityTask_PlayMontageAndWait;
/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_SpeedSwitching : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	
	UGA_SpeedSwitching();
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> FrontMontage;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> BackMontage;
	
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait>PlayMontageAndWait;
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> Effect;
	
	UFUNCTION()
	void EndMontage();
	
	UFUNCTION()
	void PlayMontage(UAnimMontage* MontageToPlay);
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
