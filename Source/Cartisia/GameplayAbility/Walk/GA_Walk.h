// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Walk.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_Walk : public UGameplayAbility
{
	GENERATED_BODY()
public:
	
	UGA_Walk();
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	UPROPERTY(EditAnywhere,Category="GameplayTag")
	FGameplayTag GameplayTag;
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
