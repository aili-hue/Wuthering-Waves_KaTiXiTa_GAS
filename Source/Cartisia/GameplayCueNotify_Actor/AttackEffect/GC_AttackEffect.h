// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "GC_AttackEffect.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API AGC_AttackEffect : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()
public:
	
	AGC_AttackEffect();
	
	UPROPERTY(EditAnywhere,Category="Mesh")
	TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>MyMatrixAnim;
	
	UFUNCTION()
	void OnMatrixAnimEnded(UAnimMontage* Montage, bool bInterrupted);
	
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) override;
};
