// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_ReplaceTheModel.h"

#include "AbilitySystemComponent.h"

void UAN_ReplaceTheModel::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                      float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	if (UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner()))
	{
		AbilitySystemComponent->AddLooseGameplayTag(State_Visual_Doll);
	}
}

void UAN_ReplaceTheModel::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	
	if (UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner()))
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(State_Visual_Doll);
	}
	
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
