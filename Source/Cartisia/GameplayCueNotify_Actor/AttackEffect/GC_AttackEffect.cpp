// Fill out your copyright notice in the Description page of Project Settings.


#include "GC_AttackEffect.h"

AGC_AttackEffect::AGC_AttackEffect()
{
	USceneComponent* DummyRoot = CreateDefaultSubobject<USceneComponent>("DefaultSceneRoot");
	SetRootComponent(DummyRoot);
	
	SkeletalMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>("SkeletalMeshComponent");
	SkeletalMeshComponent->SetupAttachment(DummyRoot);
}

void AGC_AttackEffect::OnMatrixAnimEnded(UAnimMontage* Montage, bool bInterrupted)
{
	Destroy();
}

bool AGC_AttackEffect::OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters)
{
	if (SkeletalMeshComponent)
	{
		if (UAnimInstance* AnimInst = SkeletalMeshComponent->GetAnimInstance())
		{
			AnimInst->OnMontageEnded.RemoveAll(this);
			AnimInst->StopAllMontages(0.f);
		}
	}
	
	bool bSuperResult = Super::OnExecute_Implementation(MyTarget, Parameters);
	
	FVector FinalLocation = Parameters.Location.IsNearlyZero() ? 
							(Parameters.EffectCauser.IsValid() ? Parameters.EffectCauser->GetActorLocation() : FVector::ZeroVector) : 
							Parameters.Location;
	
	SetActorLocation(FinalLocation);
	if(Parameters.EffectCauser.IsValid()) SetActorRotation(Parameters.EffectCauser->GetActorRotation());
 
	if (SkeletalMeshComponent)
	{
		if (UAnimInstance* AnimInst = SkeletalMeshComponent->GetAnimInstance())
		{
			AnimInst->Montage_Stop(0.1f);
			
			if (!AnimInst->OnMontageEnded.IsAlreadyBound(this, &ThisClass::OnMatrixAnimEnded))
			{
				AnimInst->OnMontageEnded.AddDynamic(this, &ThisClass::OnMatrixAnimEnded);
			}
			AnimInst->Montage_Play(MyMatrixAnim);
		}
		else
		{
			Destroy(); 
		}
	}
	return bSuperResult;
}