// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_InterruptAnimation.h"
#include "AbilitySystemComponent.h"

void UAN_InterruptAnimation::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	//重置 Handle
	ActiveGameplayEffectHandle= FActiveGameplayEffectHandle();
	
	if (UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner()))
	{
		if (GameplayEffect)
		{
			FGameplayEffectContextHandle ContextHandle= AbilitySystemComponent->MakeEffectContext();
			FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(GameplayEffect,1.f,ContextHandle);
			
			if (SpecHandle.IsValid())
			{
				ActiveGameplayEffectHandle = AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
			}
		}
	}
}

void UAN_InterruptAnimation::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	
	if (UAbilitySystemComponent* AbilitySystemComponent = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(MeshComp->GetOwner()))
	{
		if (ActiveGameplayEffectHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(ActiveGameplayEffectHandle);
		}
	}
	ActiveGameplayEffectHandle= FActiveGameplayEffectHandle();
	
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
