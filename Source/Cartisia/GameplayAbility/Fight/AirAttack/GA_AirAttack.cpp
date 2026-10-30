// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_AirAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"

UGA_AirAttack::UGA_AirAttack()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// 作用: 播放空中连招蒙太奇(第CurrentMove段), 完成回调结束能力, 被打断回调走Interrupted(连招中则复位标记)。
void UGA_AirAttack::PlayMontage(const FAirAttackFightData& FightStruct)
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

// 作用: 蒙太奇播放完成——结束空中连招能力。
void UGA_AirAttack::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}


// 作用: 蒙太奇被打断时——若处于连招状态(bIsCombo)则仅复位连招标记, 否则结束能力。
void UGA_AirAttack::Interrupted()
{
	if (bIsCombo)
	{
		bIsCombo=false;
		return;
	}
	
	EndMontage();
}

// 作用: 连招推进——收到 Event.Attack 事件时索引前移播放下一段空连; 若已到最后一段(NextIndex>=Num),
// 则发出 Ability.Fight.HeavyBlow 事件衔接下落重击, 并结束空中连招能力。
void UGA_AirAttack::WaitGameplayEvent(FGameplayEventData EventData)
{
	const int32 NextIndex = CurrentMove + 1;
	
	// 已经到连招末尾,触发下落攻击结束技能
	if (NextIndex >= FightStructs.Num())
	{
		if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
		{
			if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
			{
				FGameplayEventData AbilityData;
				AbilityData.Instigator=Character;
				AbilityData.EventTag=Ability_Fight_HeavyBlowTag;
				AbilitySystemComponent->HandleGameplayEvent(Ability_Fight_HeavyBlowTag,&AbilityData);
			}
		}
		bIsCombo = false;
		EndMontage();
		return;
	}
	CurrentMove = NextIndex;
	bIsCombo = true;

	PlayMontage(FightStructs[CurrentMove]);
}

// 作用: 能力激活——注册 Event.Attack 连招监听; 连招数据为空直接结束; 从第0段开始播放空连蒙太奇, 并给自身施加连招GameplayEffect。
void UGA_AirAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
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
			
			//给自身赋予连招 Effect
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

// 作用: 能力结束——复位连招状态, 移除连招Effect, 遍历所有连招蒙太奇停止仍在播放的段(兼容被打断情况)。
void UGA_AirAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
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
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (UAnimInstance* AnimInstance = Character->GetMesh()->GetAnimInstance())
		{
			float BlendTime = bWasCancelled ? 0.1f : 0.2f;
			
			for (const FAirAttackFightData& Data : FightStructs)
			{
				if (Data.Montage && AnimInstance->Montage_IsPlaying(Data.Montage))
				{
					AnimInstance->Montage_Stop(BlendTime, Data.Montage);
				}
			}
		}
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
