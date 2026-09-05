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
	
	FActiveGameplayEffectHandle EffectHandle;
	
	FGameplayTag Event_AbilityJumpTag= FGameplayTag::RequestGameplayTag(FName("Event.EndAbilityJump"));
	
	UFUNCTION()
	void EndMontage(FGameplayEventData Data);
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
