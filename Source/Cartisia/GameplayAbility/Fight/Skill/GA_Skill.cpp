// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Skill.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"

UGA_Skill::UGA_Skill()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

// 作用: 播放技能蒙太奇, 完成/被打断回调均结束能力。
void UGA_Skill::PlayAnimMontage(UAnimMontage* Montage)
{
	if (UAbilityTask_PlayMontageAndWait* PlayMontageAndWait= UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,Montage,1.f,NAME_None))
	{
		PlayMontageAndWait->OnCompleted.AddDynamic(this, &UGA_Skill::EndMontage);
		PlayMontageAndWait->OnInterrupted.AddDynamic(this, &UGA_Skill::EndMontage);
		PlayMontageAndWait->ReadyForActivation();
	}
}

// 作用: 蒙太奇播放完成——结束技能能力。
void UGA_Skill::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 能力激活——先提交消耗(CommitAbility, 扣蓝/冷却等), 失败则结束; 成功后执行技能GameplayCue(特效音效)并播放技能蒙太奇。
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

// 作用: 能力结束——停止动画蒙太奇(按取消与否选择混合时间; 注: 当前重复执行了两遍相同的停止逻辑)。
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
