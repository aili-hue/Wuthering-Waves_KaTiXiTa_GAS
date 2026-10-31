// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_DoubleJump.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"

UGA_DoubleJump::UGA_DoubleJump()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}


// 作用: 播放二段跳蒙太奇, 播放完成/被打断均结束能力。
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

// 作用: 蒙太奇播放完成——结束二段跳能力。
void UGA_DoubleJump::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

// 作用: 能力激活——根据事件幅度(EventMagnitude==1 为前跳, 否则后退)选择播放前跳或后跳二段跳蒙太奇。
void UGA_DoubleJump::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                     const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                     const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (TriggerEventData)
	{
		if (TriggerEventData->EventMagnitude == 1.f)
		{
			PlayMontageJump(Jump_Second_F);
		}
		else
		{
			PlayMontageJump(Jump_Second_B);
		}
	}
}

// 作用: 能力结束——按取消与否选择混合时间停止动画蒙太奇。
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
