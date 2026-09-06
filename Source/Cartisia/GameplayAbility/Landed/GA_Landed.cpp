// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Landed.h"

#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"

UGA_Landed::UGA_Landed()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Landed::PlayMontage(UAnimMontage* MontageToPlay)
{
	if (PlayMontageAndWait)
	{
		PlayMontageAndWait->EndTask();
		PlayMontageAndWait=nullptr;
	}
	PlayMontageAndWait= UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,MontageToPlay,1.f,NAME_None);
	PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
	PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
	PlayMontageAndWait->ReadyForActivation();
}

void UGA_Landed::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_Landed::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (AnimMontage)
	{
		PlayMontage(AnimMontage);
	}
	
}

void UGA_Landed::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	if (PlayMontageAndWait)
	{
		PlayMontageAndWait->EndTask();
		PlayMontageAndWait=nullptr;
	}
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance())
		{
			float BlendTime= bWasCancelled ? 0.1f : 0.2f;
			AnimInstance->Montage_Stop(BlendTime);
		}
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
