// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_ChangeofDirection.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UAN_ChangeofDirection::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                        float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner()))
	{
		Character->GetCharacterMovement()->bAllowPhysicsRotationDuringAnimRootMotion=true;
	}
}

void UAN_ChangeofDirection::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyEnd(MeshComp, Animation, EventReference);
	if (ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner()))
	{
		Character->GetCharacterMovement()->bAllowPhysicsRotationDuringAnimRootMotion=false;
	}
}
