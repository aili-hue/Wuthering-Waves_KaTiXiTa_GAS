// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_NormalAttack.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "Abilities/Tasks/AbilityTask_WaitGameplayEvent.h"
#include "GameFramework/Character.h"

UGA_NormalAttack::UGA_NormalAttack()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// 作用: 播放一段普攻连招蒙太奇(第CurrentMove段), 并监听其完成/被打断回调以决定结束能力或进入下一段。
void UGA_NormalAttack::PlayMontage(const FAirAttackFight& FightStruct)
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

// 作用: 蒙太奇播放完成——结束整个普攻能力(连招末尾收尾)。
void UGA_NormalAttack::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 蒙太奇被打断时——若处于连招状态(bIsCombo)则仅复位连招标记(等待连打事件), 否则视为整段攻击结束并结束能力。
void UGA_NormalAttack::Interrupted()
{
	if (bIsCombo)
	{
		bIsCombo=false;
		return;
	}
	EndMontage();
}

// 作用: 连招推进核心——收到 Event.Attack 事件时, 连招索引在FightStructs中循环前移(取模), 标记连招状态,
// 按不同段位(第2/3段)触发对应的攻击GameplayCue(特效/音效), 并播放下一段连招蒙太奇。
void UGA_NormalAttack::WaitGameplayEvent(FGameplayEventData EventData)
{
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		const int32 TargetMoveIndex = (CurrentMove + 1) % FightStructs.Num();
		
		CurrentMove = TargetMoveIndex;
		bIsCombo = true;
		
		if (AActor* Actor = Cast<AActor>(GetAvatarActorFromActorInfo()))
		{
			FGameplayCueParameters Params;
			Params.EffectCauser = Actor;
			Params.Instigator = Actor;
			Params.Location = Actor->GetActorLocation();
			Params.RawMagnitude = FightStructs[TargetMoveIndex].Magnification;
			
			switch (TargetMoveIndex)
			{
			case 2:
				if (GC_Attack_2.IsValid())
				{
					AbilitySystemComponent->ExecuteGameplayCue(GC_Attack_2, Params);
				}
				break;
			case 3:
				if (GC_Attack_3.IsValid())
				{
					AbilitySystemComponent->ExecuteGameplayCue(GC_Attack_3, Params);
				}
				break;
			default:
				break;
			}
		}
		PlayMontage(FightStructs[CurrentMove]);
	}
}

// 作用: 能力激活——注册 Event.Attack 连招事件监听; 连招数据为空则直接结束; 从第0段开始播放普攻蒙太奇, 并给自身施加连招状态GameplayEffect(Data.Attack)。
void UGA_NormalAttack::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                       const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                       const FGameplayEventData* TriggerEventData)
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

// 作用: 能力结束——复位连招状态(索引/连打标记), 移除连招GameplayEffect, 并按取消与否选择混合时间停止动画蒙太奇。
void UGA_NormalAttack::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
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
			float BlendTime= bWasCancelled ? 0.1f : 0.2f;
			AnimInstance->Montage_Stop(BlendTime);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
