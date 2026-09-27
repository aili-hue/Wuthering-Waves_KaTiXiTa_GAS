// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Skill.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_Skill : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_Skill();
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>AnimMontage;
	
	UFUNCTION()
	void PlayAnimMontage(UAnimMontage* Montage);
	
	UFUNCTION()
	void EndMontage();
	
	FGameplayTag GameplayCue_Skill =FGameplayTag::RequestGameplayTag(FName("GameplayCue.Skill"));
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
