// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_MovementMode.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UAN_MovementMode : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere,Category="MovementMode")
	TEnumAsByte<EMovementMode> StartMovementMode;
	
	UPROPERTY(EditAnywhere,Category="MovementMode")
	TEnumAsByte<EMovementMode> EndMovementMode;
	
	
	virtual void NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)override;
	virtual void NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference)override;
};
