// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Jump.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

UGA_Jump::UGA_Jump()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Jump::EndMontage(FGameplayEventData Data)
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}


void UGA_Jump::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (UAbilityTask_WaitGameplayEvent* WaitEvent=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,Event_AbilityJumpTag))
	{
		WaitEvent->EventReceived.AddDynamic(this,&UGA_Jump::EndMontage);
		WaitEvent->ReadyForActivation();
	}
	
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
		{
			if (JumpEffect)
			{
				Character->Jump();
			
				FGameplayEffectContextHandle ContextHandle=AbilitySystemComponent->MakeEffectContext();
				FGameplayEffectSpecHandle SpecHandle=AbilitySystemComponent->MakeOutgoingSpec(JumpEffect,1.f,ContextHandle);
			
				if (SpecHandle.IsValid())
				{
				
					EffectHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}
		}
	}
	else
	{
		EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
	}
}

void UGA_Jump::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (EffectHandle.IsValid())
		{
		
			AbilitySystemComponent->RemoveActiveGameplayEffect(EffectHandle);
		}
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
