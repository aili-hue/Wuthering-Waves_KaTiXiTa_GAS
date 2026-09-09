// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_InterruptAnimation.generated.h"

class UGameplayEffect;
/**
 * 
 */
UCLASS()
class CARTISIA_API UAN_InterruptAnimation : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	
	UPROPERTY(EditAnywhere,Category="GameplayEffect")
	TSubclassOf<UGameplayEffect> GameplayEffect;
	
	FActiveGameplayEffectHandle ActiveGameplayEffectHandle;
	
	virtual void NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)override;
	virtual void NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference)override;
};
