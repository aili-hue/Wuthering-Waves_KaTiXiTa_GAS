// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_DoubleJump.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"

UGA_DoubleJump::UGA_DoubleJump()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}


void UGA_DoubleJump::PlayMontageJump(UAnimMontage* MontageToPlay)
{
	if (MontageToPlay)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait =UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,MontageToPlay,1.f,NAME_None))
		{
			PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
}

void UGA_DoubleJump::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_DoubleJump::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                     const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (TriggerEventData->EventMagnitude == 1.f)
	{
		PlayMontageJump(Jump_Second_F);
	}
	else
	{
		PlayMontageJump(Jump_Second_B);
	}
	
}

void UGA_DoubleJump::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance())
		{
			float WasCancelled = bWasCancelled ? 0.1f : 0.2f ;
			AnimInstance->Montage_Stop(WasCancelled);
		}
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
