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

void UGA_Jump::EndLandMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_Jump::EndJumpMontage()
{
	if (bLand)return;
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

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

void UGA_Jump::EndMontage(FGameplayEventData Data)
{
	
	if (bPhysicaljumps)
	{
		EndLandMontage();
		return;
	}
	LandMontage();
}


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
