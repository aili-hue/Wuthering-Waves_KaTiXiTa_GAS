// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_NormalAttack.generated.h"


USTRUCT(BlueprintType)
struct FFightStruct
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage> Montage;
	
	UPROPERTY(EditAnywhere,Category="Magnification")
	float Magnification= 1.f;
};

UCLASS()
class CARTISIA_API UGA_NormalAttack : public UGameplayAbility
{
	GENERATED_BODY()
	
protected:
	
	UGA_NormalAttack();
	
	UPROPERTY(EditAnywhere,Category="FightStruct")
	TArray<FFightStruct> FightStructs;
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle;
	
	UFUNCTION()
	void PlayMontage(FFightStruct FightStruct);
	
	UFUNCTION()
	void EndMontage();
	
	UFUNCTION()
	void Interrupted();
	
	//当前招式序列
	int32 CurrentMove= 0;
	
	//连招回调 Tag
	FGameplayTag Event_Attack= FGameplayTag::RequestGameplayTag(FName("Event.Attack"));
	
	//是否连招
	uint8 bIsCombo : 1 =false;
	
	UFUNCTION()
	void WaitGameplayEvent(FGameplayEventData EventData);
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
	
};
