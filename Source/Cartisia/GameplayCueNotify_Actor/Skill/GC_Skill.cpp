// Fill out your copyright notice in the Description page of Project Settings.


#include "GC_Skill.h"

AGC_Skill::AGC_Skill()
{
	USceneComponent* DummyRoot = CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot");
	SetRootComponent(DummyRoot);
	
	SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMesh->SetupAttachment(DummyRoot);
	
	SkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
}

void AGC_Skill::OnMatrixAnimEnded(UAnimMontage* Montage, bool bInterrupted)
{
	Destroy();
}

bool AGC_Skill::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	bool bSuperResult= Super::OnExecute_Implementation(MyTarget, Parameters);
	
	SetActorHiddenInGame(false);
	SkeletalMesh->SetVisibility(true, true);
	
	if (ImpactCameraShake)
	{
		if (APawn* InstigatorPawn = Cast<APawn>(Parameters.Instigator.Get()))
		{
			if (APlayerController* PC = Cast<APlayerController>(InstigatorPawn->GetController()))
			{
				if (PC->IsLocalController())
				{
					PC->ClientStartCameraShake(ImpactCameraShake);
				}
			}
		}
	}
	if (SkeletalMesh)
	{
		if (UAnimInstance* AnimInst = SkeletalMesh->GetAnimInstance())
		{
			AnimInst->Montage_Stop(0.1f);
			
			if (!AnimInst->OnMontageEnded.IsAlreadyBound(this, &ThisClass::OnMatrixAnimEnded))
			{
				AnimInst->OnMontageEnded.AddDynamic(this, &ThisClass::OnMatrixAnimEnded);
			}
			AnimInst->Montage_Play(MatrixAnim);
		}
		else
		{
			Destroy(); 
		}
	}
	return bSuperResult;
}
