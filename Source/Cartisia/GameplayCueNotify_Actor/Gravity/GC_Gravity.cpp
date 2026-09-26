// Fill out your copyright notice in the Description page of Project Settings.


#include "GC_Gravity.h"

AGC_Gravity::AGC_Gravity()
{
	USceneComponent* DummyRoot = CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot");
	SetRootComponent(DummyRoot);
	
	SkeletalMesh = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMesh->SetupAttachment(DummyRoot);
	
	SkeletalMesh->SetAnimationMode(EAnimationMode::AnimationBlueprint);
}


void AGC_Gravity::OnMatrixAnimEnded(UAnimMontage* Montage, bool bInterrupted)
{
	Destroy();
}

bool AGC_Gravity::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	if (SkeletalMesh)
	{
		if (UAnimInstance* AnimInst = SkeletalMesh->GetAnimInstance())
		{
			AnimInst->OnMontageEnded.RemoveAll(this);
			AnimInst->StopAllMontages(0.f);
		}
	}
	
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
	
	bool bSuperResult = Super::OnExecute_Implementation(MyTarget, Parameters);
	
	FVector FinalLocation = Parameters.Location.IsNearlyZero() ? 
							(Parameters.EffectCauser.IsValid() ? Parameters.EffectCauser->GetActorLocation() : FVector::ZeroVector) : 
							Parameters.Location;
	
	SetActorLocation(FinalLocation);
	if(Parameters.EffectCauser.IsValid()) SetActorRotation(Parameters.EffectCauser->GetActorRotation());
 
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
