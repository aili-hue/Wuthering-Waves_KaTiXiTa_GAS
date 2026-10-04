// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Skill.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"

UGA_Skill::UGA_Skill()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_Skill::PlayAnimMontage(UAnimMontage* Montage)
{
	if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait= UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,Montage,1.f,NAME_None))
	{
		PlayMontageAndWait->OnCompleted.AddDynamic(this, &UGA_Skill::EndMontage);
		PlayMontageAndWait->OnInterrupted.AddDynamic(this, &UGA_Skill::EndMontage);
		PlayMontageAndWait->ReadyForActivation();
	}
}

void UGA_Skill::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_Skill::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo,ActivationInfo,true, true);
		return;
	}
	
	if (UAbilitySystemComponent* AbilitySystemComp =GetAbilitySystemComponentFromActorInfo())
	{
		AbilitySystemComp->ExecuteGameplayCue(GameplayCue_Skill);
	}
	if (AnimMontage){PlayAnimMontage(AnimMontage);}
}

void UGA_Skill::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance())
		{
			AnimInstance->Montage_Stop(0.2f);
		}
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
