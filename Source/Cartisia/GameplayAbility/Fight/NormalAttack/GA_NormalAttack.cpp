// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_NormalAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"

UGA_NormalAttack::UGA_NormalAttack()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_NormalAttack::PlayMontage(const FFightStruct& FightStruct)
{
	if (FightStruct.Montage)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait= UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,FightStruct.Montage,1.f,NAME_None))
		{
			PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::Interrupted);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
}

void UGA_NormalAttack::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_NormalAttack::Interrupted()
{
	if (bIsCombo)
	{
		bIsCombo=false;
		return;
	}
	EndMontage();
}

void UGA_NormalAttack::WaitGameplayEvent(FGameplayEventData EventData)
{
	
	CurrentMove= (CurrentMove+1) % FightStructs.Num();
	if (!bIsCombo)bIsCombo=true;
	
	PlayMontage(FightStructs[CurrentMove]);
}

void UGA_NormalAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                       const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (UAbilityTask_WaitGameplayEvent* WaitGameplayEvent=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,Event_Attack))
		{
			WaitGameplayEvent->EventReceived.AddDynamic(this,&ThisClass::WaitGameplayEvent);
			WaitGameplayEvent->ReadyForActivation();
		}
		
		if (FightStructs.Num() == 0) 
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		
		if (CurrentMove== 0 && FightStructs[CurrentMove].Montage)
		{
			PlayMontage(FightStructs[CurrentMove]);
			
			if (GameplayEffect)
			{
				FGameplayEffectContextHandle ContextHandle=AbilitySystemComponent->MakeEffectContext();
				FGameplayEffectSpecHandle SpecHandle=AbilitySystemComponent->MakeOutgoingSpec(GameplayEffect,1.f,ContextHandle);
				if (SpecHandle.IsValid())
				{
					ActiveGameplayEffectHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}
		}
	}
}

void UGA_NormalAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	CurrentMove= 0;
	bIsCombo = false;
	
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (ActiveGameplayEffectHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(ActiveGameplayEffectHandle);
			ActiveGameplayEffectHandle.Invalidate();
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
