// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "AN_WeaponTransparency.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UAN_WeaponTransparency : public UAnimNotifyState
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere,Category="MaterialParameterCollection")
	TObjectPtr<UMaterialParameterCollection>MaterialParameterCollection;
	
	void NotifyBegin(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)override;
	virtual void NotifyEnd(USkeletalMeshComponent * MeshComp, UAnimSequenceBase * Animation, const FAnimNotifyEventReference& EventReference);
	
};
