// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_SoundEffects.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UAN_SoundEffects : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,Category = "Audio")
	TObjectPtr<USoundBase>AttackSound;
	
	UPROPERTY(EditAnywhere, Category = "Audio")
	float FadeOutTime = 0.15f;
	
	UPROPERTY(EditAnywhere, Category = "Audio")
	FName AttachSocketName = NAME_None;
	
	virtual void NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)override;
	virtual void NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference)override;
};
