// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_MovementMode.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

void UAN_MovementMode::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
                                   const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->SetMovementMode(StartMovementMode);
		}
	}
}

void UAN_MovementMode::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (ACharacter* Character = Cast<ACharacter>(MeshComp->GetOwner()))
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->SetMovementMode(EndMovementMode);
		}
	}
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
