// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_FallingAttack.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayTag.h"

UGA_FallingAttack::UGA_FallingAttack()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_FallingAttack::PlayMontage(UAnimMontage* Montage)
{
	if (UAbilityTask_PlayMontageAndWait* MontageAndWait =UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,Montage,1.f,MontageName))
	{
		MontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::LoopMontage_Name);
		MontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
		MontageAndWait->ReadyForActivation();
	}
}

void UGA_FallingAttack::GeneralMontage(UAnimMontage* Montage)
{
	if (UAbilityTask_PlayMontageAndWait* MontageAndWait =UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,Montage,1.f,MontageName))
	{
		MontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
		MontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
		MontageAndWait->ReadyForActivation();
	}
}

void UGA_FallingAttack::LoopMontage_Name()
{
	MontageName="Loop";
	GeneralMontage(PlayMontageAndWait);
}

void UGA_FallingAttack::LoopMontage()
{
	if (bIsLoopMontage)
	{
		bIsLoopMontage= false;
		return;
	}
	bIsLoopMontage= true;
	
	MontageName="End";
	GeneralMontage(PlayMontageAndWait);
}

void UGA_FallingAttack::EndMontage()
{
	if (bIsLoopMontage)
	{
		return;
	}
	
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,false);
}

void UGA_FallingAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                        const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                        const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (UAbilityTask_WaitGameplayTagAdded* WaitLandedTask = UAbilityTask_WaitGameplayTagAdded::WaitGameplayTagAdd(this,Data_LandedTag))
	{
		WaitLandedTask->Added.AddDynamic(this,&ThisClass::LoopMontage);
		WaitLandedTask->ReadyForActivation();
	}
	PlayMontage(PlayMontageAndWait);
}

void UGA_FallingAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (bIsLoopMontage)bIsLoopMontage= false;
	MontageName="Default";
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
