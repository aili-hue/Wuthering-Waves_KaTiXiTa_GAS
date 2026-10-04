// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_FallingAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"

UGA_FallingAttack::UGA_FallingAttack()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_FallingAttack::PlayMontage(UAnimMontage* MontageToPlay)
{
	if (MontageToPlay)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, MontageToPlay,1.f,NAME_None))
		{
			PlayMontageAndWait->OnBlendOut.AddDynamic(this,&ThisClass::PlayLoopAnimMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndAttack);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
	
}

void UGA_FallingAttack::PlayLoopAnimMontage()
{
	if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, LoopAnimMontage,1.f,NAME_None))
	{
		/*PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndAttack);*/
		PlayMontageAndWait->ReadyForActivation();
	}
}

void UGA_FallingAttack::EndAttack()
{
	if (!bCanEndAbility)return;
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_FallingAttack::EndAnimMontageAttack()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_FallingAttack::PlayEndAnimMontage(FGameplayTag EndTag, int32 Number)
{
	if (Number> 0)
	{
		EndAttack();
		return;
	}
	bCanEndAbility= false;
	if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, EndAnimMontage,1.f,NAME_None))
	{
		PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndAnimMontageAttack);
		PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndAnimMontageAttack);
		PlayMontageAndWait->ReadyForActivation();
	}
}

void UGA_FallingAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                        const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	PlayMontage(JumpAnimMontage);
	
	if (UAbilitySystemComponent* ASC= GetAbilitySystemComponentFromActorInfo())
	{
		EndHandle= ASC->RegisterGameplayTagEvent(Data_FallingTag).AddUObject(this,&ThisClass::PlayEndAnimMontage);
	}
}

void UGA_FallingAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	bCanEndAbility= true;
	
	if (UAbilitySystemComponent* ASC= GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RegisterGameplayTagEvent(Data_FallingTag).Remove(EndHandle);
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
