// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Walk.h"
#include "AbilitySystemComponent.h"
#include "Cartisia/AttributeSet/PlayerAttributes.h"



UGA_Walk::UGA_Walk()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Walk::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (UAbilitySystemComponent* AbilitySystemComponent=GetAbilitySystemComponentFromActorInfo())
	{
		if (const UPlayerAttributes* Attributes =AbilitySystemComponent->GetSet<UPlayerAttributes>())
		{
			float FinalSpeed= Attributes->GetMovementSpeed() >= 400.f ? 100.f : 400.f;
			
			if (GameplayEffect)
			{
				FGameplayEffectContextHandle Context=AbilitySystemComponent->MakeEffectContext();
				FGameplayEffectSpecHandle SpecHandle=AbilitySystemComponent->MakeOutgoingSpec(GameplayEffect,1.f,Context);
				if (SpecHandle.IsValid())
				{
					SpecHandle.Data->SetSetByCallerMagnitude(GameplayTag,FinalSpeed);
					AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}
		}
	}
	
}

void UGA_Walk::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
