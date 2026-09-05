// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_SpeedSwitching.h"
#include "AbilitySystemComponent.h"
#include "Abilities/Tasks/AbilityTask_PlayMontageAndWait.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"


UGA_SpeedSwitching::UGA_SpeedSwitching()
{
	InstancingPolicy=EGameplayAbilityInstancingPolicy::InstancedPerActor;
	
}

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

void UGA_SpeedSwitching::EndMontage()
{
	EndAbility(CurrentSpecHandle,CurrentActorInfo,CurrentActivationInfo,true,true);
}

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
		if (ACharacter* Character = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
		{
			if (auto* MovementComponent= Character->GetCharacterMovement())
			{
				FVector Forward = Character->GetActorForwardVector();
				float ForwardSpeed = FVector::DotProduct(MovementComponent->Velocity, Forward);
				
				if (ForwardSpeed > 1.f)
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
				else if (ForwardSpeed <= 0.f)
				{
					if (BackMontage)
					{
						PlayMontage(BackMontage);
					}
				}
			}
		}
	}
}

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
