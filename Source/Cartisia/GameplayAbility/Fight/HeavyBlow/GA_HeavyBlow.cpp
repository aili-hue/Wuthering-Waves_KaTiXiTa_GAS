// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_HeavyBlow.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"

UGA_HeavyBlow::UGA_HeavyBlow()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// 作用: 播放重击蒙太奇, 完成/淡出/被打断回调均结束能力(EndMontage)。
void UGA_HeavyBlow::PlayMontage(UAnimMontage* AnimMontage)
{
	if (AnimMontage)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait=UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, AnimMontage,1.f,NAME_None))
		{
			PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->OnBlendOut.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
}

// 作用: 蒙太奇播放完成——若角色处于跳跃中(持有Data.Jump标签)则先取消跳跃GA, 再结束重击能力。
void UGA_HeavyBlow::EndMontage()
{
	//取消掉JumpGA
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(Data_JumpTag))
		{
			FGameplayTagContainer Container;
			Container.AddTag(Ability_Jump);
			AbilitySystemComponent->CancelAbilities(&Container);
		}
	}
	
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 能力激活——直接播放重击蒙太奇。
void UGA_HeavyBlow::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	PlayMontage(Montage);
}

// 作用: 能力结束——按取消与否选择混合时间停止动画蒙太奇。
void UGA_HeavyBlow::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
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
