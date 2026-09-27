// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "GC_Skill.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API AGC_Skill : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()
public:
	AGC_Skill();
	
	UPROPERTY(EditAnywhere,Category="Mesh")
	TObjectPtr<USkeletalMeshComponent> SkeletalMesh;
	
	UPROPERTY(EditAnywhere,Category="AnimMontage")
	TObjectPtr<UAnimMontage>MatrixAnim;
	
	UPROPERTY(EditAnywhere, Category = "Effects")
	TSubclassOf<UCameraShakeBase> ImpactCameraShake;
	
	UFUNCTION()
	void OnMatrixAnimEnded(UAnimMontage* Montage, bool bInterrupted);
	
	bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)override;
};
