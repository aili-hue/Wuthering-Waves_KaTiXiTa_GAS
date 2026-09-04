// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_StopWalking.h"

#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UGA_StopWalking::UGA_StopWalking()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
}

void UGA_StopWalking::PlayMontage(UAnimMontage* MontageToPlay)
{
	if (PlayMontageAndWait)
	{
		PlayMontageAndWait->EndTask();
		PlayMontageAndWait=nullptr;
	}
	PlayMontageAndWait= UAbilityTask_PlayMontageAndWait::CreatePlayMontageAndWaitProxy(this,NAME_None,MontageToPlay,1.f,NAME_None);
	PlayMontageAndWait->OnCompleted.AddDynamic(this,&ThisClass::EndMontage);
	PlayMontageAndWait->OnInterrupted.AddDynamic(this,&ThisClass::EndMontage);
	PlayMontageAndWait->ReadyForActivation();
	
}

void UGA_StopWalking::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

void UGA_StopWalking::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                      const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
                                      const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		if (Character->GetCharacterMovement()->MaxWalkSpeed >= 600.f)
		{
			PlayMontage(Stop_Sprint);
			if (GameplayEffect)
			{
				if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
				{
					FGameplayEffectContextHandle ContextHandle = AbilitySystemComponent->MakeEffectContext();
					FGameplayEffectSpecHandle SpecHandle = AbilitySystemComponent->MakeOutgoingSpec(GameplayEffect,1.f,ContextHandle);
					if (SpecHandle.IsValid())
					{
						AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
					}
				}
			}
		}
		else if (Character->GetCharacterMovement()->MaxWalkSpeed >= 400.f)
		{
			PlayMontage(Stop_Run);
		}
		else
		{
			PlayMontage(Stop_Walk);
		}
	}
}

void UGA_StopWalking::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	
	if (PlayMontageAndWait)
	{
		PlayMontageAndWait->EndTask();
		PlayMontageAndWait=nullptr;
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
