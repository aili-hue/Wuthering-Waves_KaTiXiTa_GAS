// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_HeavyBlow_Special.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"

UGA_HeavyBlow_Special::UGA_HeavyBlow_Special()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// 作用: 播放蓄力重击蒙太奇, 完成回调结束能力(EndMontage), 被打断回调走SpecialEndMontage(特殊攻击尚未触发时才结束)。
void UGA_HeavyBlow_Special::PlayMontage(UAnimMontage* AnimMontage)
{
	if (AnimMontage)
	{
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait=UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, AnimMontage,1.f,NAME_None))
		{
			PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::SpecialEndMontage);
			PlayMontageAndWait->ReadyForActivation();
		}
	}
}

// 作用: 蒙太奇正常完成——结束重击特殊能力。
void UGA_HeavyBlow_Special::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 蓄力阶段蒙太奇被打断——若特殊攻击已触发(bSpecialAttack)则保持能力(等待特殊攻击状态), 否则按普通重击结束能力。
void UGA_HeavyBlow_Special::SpecialEndMontage()
{
	if (bSpecialAttack)return;
	EndMontage();
}

// 作用: 攻击事件(Event.Attack)回调——重击蓄力期间首次收到攻击事件: 触发特殊攻击(播放SpecialMontage); 已触发过特殊攻击后再收到事件: 衔接跳跃(发出 Ability.Jump 事件), 每段仅执行一次。
void UGA_HeavyBlow_Special::WaitGameplayEvent(FGameplayEventData Data)
{
	if (SpecialMontage && !bSpecialAttack)
	{
		bSpecialAttack=true;
		
		if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait=UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None, SpecialMontage,1.f,NAME_None))
		{
			PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
			PlayMontageAndWait->ReadyForActivation();
		}
		return;
	}
	//如果已经触发过特殊攻击，那就执行这个JumpAbility
	if (bSpecialAttack&& !bIsConnectionJump)
	{
		bIsConnectionJump= true;
		if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			FGameplayEventData JumpData;
			JumpData.Instigator= Character;
			JumpData.EventTag= Ability_Jump;
			if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
			{
				AbilitySystemComponent->HandleGameplayEvent(Ability_Jump,&JumpData);
			}
		}
	}
}

// 作用: 能力激活——注册攻击事件监听(蓄力期间连打触发特殊攻击/衔接跳跃); 配置了GameplayEffect则给自身施加蓄力Buff; 播放蓄力蒙太奇。
void UGA_HeavyBlow_Special::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                            const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                            const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (UAbilityTask_WaitGameplayEvent* WaitGameplayEvent=UAbilityTask_WaitGameplayEvent::WaitGameplayEvent(this,Event_Attack))
	{
		WaitGameplayEvent->EventReceived.AddDynamic(this,&ThisClass::WaitGameplayEvent);
			
		WaitGameplayEvent->ReadyForActivation();
	}
	UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo();
	
	if (GameplayEffect)
	{
		FGameplayEffectContextHandle ContextHandle=AbilitySystemComponent->MakeEffectContext();
		FGameplayEffectSpecHandle SpecHandle=AbilitySystemComponent->MakeOutgoingSpec(GameplayEffect,1.f,ContextHandle);
		if (SpecHandle.IsValid())
		{
			ActiveGameplayEffectHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}
	PlayMontage(Montage);
}

// 作用: 能力结束——复位特殊攻击/衔接跳跃标记, 移除蓄力Buff并停止动画蒙太奇。
void UGA_HeavyBlow_Special::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	
	bSpecialAttack= false;
	bIsConnectionJump= false;
	
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (ActiveGameplayEffectHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(ActiveGameplayEffectHandle);
			ActiveGameplayEffectHandle.Invalidate();
		}
	}
	
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* CharacterAnim = Character->GetMesh()->GetAnimInstance())
		{
			float BlendTime= bWasCancelled ? 0.1f : 0.2f;
			CharacterAnim->Montage_Stop(BlendTime);
		}
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
