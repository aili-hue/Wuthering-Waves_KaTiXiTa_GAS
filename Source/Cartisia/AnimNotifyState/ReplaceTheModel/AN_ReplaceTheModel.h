// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_ReplaceTheModel.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UAN_ReplaceTheModel : public UAnimNotifyState
{
	GENERATED_BODY()
	
public:
	
	FGameplayTag State_Visual_Doll=FGameplayTag::RequestGameplayTag(FName("State.Visual.Doll"));
	
	virtual void NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)override;
	virtual void NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference)override;
};
