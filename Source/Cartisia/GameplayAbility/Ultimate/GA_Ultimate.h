// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GA_Ultimate.generated.h"

class ALevelSequenceActor;
class ULevelSequencePlayer;
class ULevelSequence;
/**
 * 
 */
UCLASS()
class CARTISIA_API UGA_Ultimate : public UGameplayAbility
{
	GENERATED_BODY()
	
public:
	
	// 变身后的大招形态 Mesh
	UPROPERTY(EditDefaultsOnly, Category = "Ultimate|Form")
	TObjectPtr<USkeletalMesh> UltimateMesh;

	// 变身后的大招形态 AnimBP
	UPROPERTY(EditDefaultsOnly, Category = "Ultimate|Form")
	TSubclassOf<UAnimInstance> UltimateAnimBP;
	
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)override;
	
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)override;
	
	// 暴露给蓝图，用来指定大招序列资产 (在编辑器里拖入 LS_KaTiXiYa_Ultimate)
	UPROPERTY(EditDefaultsOnly, Category = "Ultimate")
	ULevelSequence* UltimateSequence;

	// 保存播放器指针，防止被垃圾回收，同时方便后续控制
	UPROPERTY()
	ULevelSequencePlayer* SequencePlayer;

	UPROPERTY()
	ALevelSequenceActor* SequenceActor;
	
	UPROPERTY(EditAnywhere, Category = "GameplayEffect")
	TSubclassOf<UGameplayEffect> UltimateEffect;
	
	UFUNCTION()
	void OnSequenceFinished();
};
