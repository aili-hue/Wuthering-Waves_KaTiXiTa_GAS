// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_HeavyBlow_Special.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_HeavyBlow_Special : public UGameplayAbility
{
	GENERATED_BODY()
public:
	UGA_HeavyBlow_Special();
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>Montage;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>SpecialMontage;
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle;
	
	UFUNCTION()
	void PlayMontage(UAnimMontage* AnimMontage);
	
	UFUNCTION()
	void EndMontage();
	
	UFUNCTION()
	void SpecialEndMontage();
	
	FGameplayTag Event_Attack= FGameplayTag::RequestGameplayTag(FName("Event.Attack"));
	
	UFUNCTION()
	void WaitGameplayEvent(FGameplayEventData Data);
	
	uint8 bSpecialAttack :1 =false;
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;

};
