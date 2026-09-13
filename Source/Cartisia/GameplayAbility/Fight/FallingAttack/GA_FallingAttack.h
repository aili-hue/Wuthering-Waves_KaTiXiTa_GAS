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
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> PlayMontageAndWait;
	
	uint8 bIsLoopMontage=false;
	
	UFUNCTION()
	void PlayMontage(UAnimMontage* Montage);
	
	UFUNCTION()
	void GeneralMontage(UAnimMontage* Montage);
	
	UFUNCTION()
	void LoopMontage_Name();
	
	FName MontageName= "Default";
	FGameplayTag Data_LandedTag= FGameplayTag::RequestGameplayTag(FName("Data.Landed"));
	
	UFUNCTION()
	void LoopMontage();
	
	UFUNCTION()
	void EndMontage();
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
	
};
