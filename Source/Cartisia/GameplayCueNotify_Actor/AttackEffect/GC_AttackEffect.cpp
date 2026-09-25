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
	FVector FinalLocation = Parameters.Location.IsNearlyZero() ? 
							(Parameters.EffectCauser.IsValid() ? Parameters.EffectCauser->GetActorLocation() : FVector::ZeroVector) : 
							Parameters.Location;
	
	SetActorLocation(FinalLocation);
	if(Parameters.EffectCauser.IsValid()) SetActorRotation(Parameters.EffectCauser->GetActorRotation());
 
	if (SkeletalMeshComponent)
	{
		if (UAnimInstance* AnimInst = SkeletalMeshComponent->GetAnimInstance())
		{
			AnimInst->Montage_Play(MyMatrixAnim);
			AnimInst->OnMontageEnded.AddDynamic(this, &ThisClass::OnMatrixAnimEnded);
		}
		else
		{
			Destroy(); 
		}
	}
	return true;
}