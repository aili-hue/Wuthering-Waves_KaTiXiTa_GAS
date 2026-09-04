// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_StopWalking.generated.h"

class UAbilityTask_PlayMontageAndWait;
/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_StopWalking : public UGameplayAbility
{
	GENERATED_BODY()
public:
	
	UGA_StopWalking();
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> Stop_Sprint;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>Stop_Run;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> Stop_Walk;
	
	UPROPERTY()
	TObjectPtr<UAbilityTask_PlayMontageAndWait>PlayMontageAndWait;
	
	UFUNCTION()
	void PlayMontage(UAnimMontage* MontageToPlay);
	
	UFUNCTION()
	void EndMontage();
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
