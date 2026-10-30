// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Jump.h"

#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"

UGA_Jump::UGA_Jump()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// 作用: 收到攻击事件(Event.Attack)时——发出 Ability.Fight.AirAttack 空中连招事件, 实现跳跃中接空中攻击。
void UGA_Jump::WaitGameplayEvent(FGameplayEventData Data)
{
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
		{
			FGameplayEventData EventData;
			EventData.Instigator= Character;
			EventData.EventTag =Ability_Fight_AirAttack;
			AbilitySystemComponent->HandleGameplayEvent(Ability_Fight_AirAttack,&EventData);
		}
	}
}

// 作用: 播放落地蒙太奇, 标记bLand已落地, 完成/被打断回调结束能力。
void UGA_Jump::LandMontage()
{
	
	if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, LandAnimMontage,1.f,NAME_None))
	{
		PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndLandMontage);
		PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndLandMontage);
		PlayMontageAndWait->ReadyForActivation();
	}
	bLand= true;
	
}

/*
void UGA_Jump::PlayLandMontage(UAnimMontage* MontageToPlay)
{
	if (MontageToPlay)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,MontageToPlay,1.f,NAME_None))
		{
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&UGA_Jump::EndLandMontage);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
}
*/

// 作用: 空中循环蒙太奇——起跳蒙太奇淡出后播放滞空循环动画; 未配置循环动画则直接结束能力。
void UGA_Jump::LoopPlayMontage()
{
	if (!LoopAnimMontage)
	{
		EndLandMontage();
		return;
	}
	
	if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,LoopAnimMontage,1.f,NAME_None))
	{
		PlayMontageAndWait->ReadyForActivation();
	}
}

// 作用: 落地蒙太奇结束——结束跳跃能力(bLand复位由EndAbility处理)。
void UGA_Jump::EndLandMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 起跳蒙太奇被打断时——若已落地(bLand)则保持(等待落地蒙太奇收尾), 否则直接结束跳跃能力。
void UGA_Jump::EndJumpMontage()
{
	if (bLand)return;
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 播放起跳蒙太奇, 淡出后接循环滞空动画(LoopPlayMontage), 被打断走EndJumpMontage。
void UGA_Jump::PlayMontage(UAnimMontage* MontageToPlay)
{
	if (MontageToPlay)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait = UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,MontageToPlay,1.f,FName("Default")))
		{
			PlayMontageAndWait->OnBlendOut.AddDynamic(this,&UGA_Jump::LoopPlayMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&UGA_Jump::EndJumpMontage);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
}

// 作用: 收到"跳跃结束"(Event.EndAbilityJump)事件时——物理跳跃(bPhysicaljumps)直接结束能力, 否则播放落地蒙太奇收尾。
void UGA_Jump::EndMontage(FGameplayEventData Data)
{
	
	if (bPhysicaljumps)
	{
		EndLandMontage();
		return;
	}
	LandMontage();
}


// 作用: 能力激活——注册攻击事件(空中连招)与跳跃结束事件监听; 给自身施加连招GameplayEffect;
// 播放起跳蒙太奇, 并(按是否物理跳跃选择调用Character->Jump)施加跳跃GameplayEffect; 无角色则结束能力。
void UGA_Jump::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (Event_Attack.IsValid())
	{
		if (UAbilityTask_WaitGameplayEvent* WaitGameplayEvent=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,Event_Attack))
		{
			WaitGameplayEvent->EventReceived.AddDynamic(this, &ThisClass::WaitGameplayEvent);
			WaitGameplayEvent->ReadyForActivation();
		}
	}
	
	if (UAbilityTask_WaitGameplayEvent* WaitEvent=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,Event_AbilityJumpTag))
	{
		WaitEvent->EventReceived.AddDynamic(this,&UGA_Jump::EndMontage);
		WaitEvent->ReadyForActivation();
	}
	
	
	
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
		{
			/*LandHandle= AbilitySystemComponent->RegisterGameplayTagEvent(Data_FallingTag).AddUObject(this,&ThisClass::LandMontage);*/
			
			//应用连招GE
			if (GameplayEffect)
			{
				FGameplayEffectContextHandle ContextHandle= AbilitySystemComponent->MakeEffectContext();
				FGameplayEffectSpecHandle SpecHandle=AbilitySystemComponent->MakeOutgoingSpec(GameplayEffect,1.f,ContextHandle);
				if (SpecHandle.IsValid())
				{
					GameplayEffectHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}
			
			PlayMontage(JumpAnimMontage);
			
			if (JumpEffect)
			{
				if (bPhysicaljumps)Character->Jump();
			
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

// 作用: 能力结束——复位落地标记, 移除跳跃/连招GameplayEffect并注销坠落标签监听, 停止动画蒙太奇。
void UGA_Jump::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	bLand= false;
	
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (EffectHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(EffectHandle);
		}
		if (LandHandle.IsValid())
		{
			AbilitySystemComponent->RegisterGameplayTagEvent(Data_FallingTag).Remove(LandHandle);
		}
		if (GameplayEffectHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(GameplayEffectHandle);
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
