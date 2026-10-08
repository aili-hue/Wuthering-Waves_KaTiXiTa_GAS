// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_AirAttack.generated.h"

USTRUCT(BlueprintType)
struct FAirAttackFightData
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> Montage;
	
	UPROPERTY(EditAnywhere,Category="Magnification")
	float Magnification= 1.f;
};

UCLASS()
class CARTISIA_API UGA_AirAttack : public UGameplayAbility
{
	GENERATED_BODY()
public:
	
	UGA_AirAttack();
	
	UPROPERTY(EditAnywhere,Category="FightStruct")
	TArray<FAirAttackFightData> FightStructs;
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle;
	
	UFUNCTION()
	void PlayMontage(const FAirAttackFightData& FightStruct);
	
	UFUNCTION()
	void EndMontage();
	
	UFUNCTION()
	void Interrupted();
	
	//是否连招
	uint8 bIsCombo : 1 =false;
	
	//连招回调 Tag
	FGameplayTag Event_Attack= FGameplayTag::RequestGameplayTag(FName("Event.Attack"));
	
	//当前招式序列
	int32 CurrentMove= 0;
	
	UFUNCTION()
	void WaitGameplayEvent(FGameplayEventData EventData);
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
};
