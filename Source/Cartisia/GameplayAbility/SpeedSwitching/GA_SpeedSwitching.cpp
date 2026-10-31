// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_SpeedSwitching.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"


UGA_SpeedSwitching::UGA_SpeedSwitching()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
	
}

// 作用: 播放冲刺切换蒙太奇——先结束并丢弃上一个蒙太奇任务, 再创建新的播放任务, 完成或打断均结束能力。
void UGA_SpeedSwitching::PlayMontage(UAnimMontage* MontageToPlay)
{
	if (PlayMontageAndWait)
	{
		PlayMontageAndWait->EndTask();
		PlayMontageAndWait=nullptr;
	}
	if (MontageToPlay)
	{
		PlayMontageAndWait= UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,MontageToPlay,1.f,NAME_None);
		PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
		PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
		PlayMontageAndWait->ReadyForActivation();
	}
}

// 作用: 蒙太奇播放完成——结束冲刺切换能力。
void UGA_SpeedSwitching::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 能力激活——先提交消耗(CommitAbility), 失败则结束;
// 成功时按事件幅度(EventMagnitude==1 为正向冲刺, 否则反向)播放前冲/后撤蒙太奇, 正向冲刺同时施加冲刺Buff GameplayEffect。
void UGA_SpeedSwitching::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                         const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                         const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo,ActivationInfo,true, true);
		return;
	}
	
	if (UAbilitySystemComponent*ASC= GetAbilitySystemComponentFromActorInfo())
	{
		if (TriggerEventData)
		{
			if (TriggerEventData->EventMagnitude == 1.f)
			{
		
				if (FrontMontage)
				{
					PlayMontage(FrontMontage);
				}
		
				if (Effect)
				{
					FGameplayEffectContextHandle ContextHandle=ASC->MakeEffectContext();
					FGameplayEffectSpecHandle SpecHandle=ASC->MakeOutgoingSpec(Effect,1.f,ContextHandle);
					if (SpecHandle.IsValid())
					{
						ASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
					}
				}
			}
			else
			{
				if (BackMontage)
				{
					PlayMontage(BackMontage);
				}
			}
		}
	}
}

// 作用: 能力结束——结束蒙太奇任务并停止动画蒙太奇。
void UGA_SpeedSwitching::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	if (PlayMontageAndWait)
	{
		PlayMontageAndWait->EndTask();
		PlayMontageAndWait=nullptr;
	}
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* AnimInstance= Character->GetMesh()->GetAnimInstance())
		{
			float BlendTime = bWasCancelled ? 0.1f : 0.2f;
			AnimInstance->Montage_Stop(BlendTime);
		}
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
