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

void UGA_HeavyBlow_Special::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_HeavyBlow_Special::SpecialEndMontage()
{
	if (bSpecialAttack)return;
	EndMontage();
}

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
