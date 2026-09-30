// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Ultimate.h"

#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Cartisia/Character/KaTiXiYa.h"
#include "GameFramework/CharacterMovementComponent.h"


void UGA_Ultimate::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
 
	if (AKaTiXiYa* Hero = Cast<AKaTiXiYa>(GetAvatarActorFromActorInfo()))
	{
		if (USkeletalMeshComponent* MeshComp = Hero->GetMesh())
		{
			if (UltimateAnimBP)
			{
				MeshComp->SetSkeletalMesh(UltimateMesh);
				MeshComp->SetAnimInstanceClass(UltimateAnimBP);
			}
		}
		if (!Hero || !UltimateSequence)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		// 1. 设置播放参数
		FMovieSceneSequencePlaybackSettings Settings;
		Settings.bDisableMovementInput = true;
		Settings.bDisableLookAtInput = true;
		Settings.bHidePlayer = false; // 确保不隐藏真实角色
 
		// 2. 创建播放器
		SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
			GetWorld(), 
			UltimateSequence, 
			Settings, 
			SequenceActor // 这是一个 ALevelSequenceActor* 指针
		);
 
		if (SequencePlayer && SequenceActor)
		{
			// ALevelSequenceActor 也是一个 Actor。由于你在编辑器里 K 的帧通常是基于 (0,0,0) 的相对坐标，
			// 那么你只需要把这个 Actor 的世界位置挪到玩家目前所在的脚下
			SequenceActor->SetActorTransform(Hero->GetActorTransform());
 
			// 3. 动态绑定角色到占位符
			SequenceActor->SetBindingByTag(FName("HeroPlaceholder"), TArray<AActor*>{ Hero });
 
			// 4. 监听结束并开始播放
			SequencePlayer->OnFinished.AddDynamic(this, &UGA_Ultimate::OnSequenceFinished);
			SequencePlayer->Play();
 
			// 5. 物理模式保护：切换至 Flying 消除重力干扰，防止由于镜头旋转导致的位移偏差
			Hero->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
			Hero->GetCharacterMovement()->StopMovementImmediately();
		}
		else
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		}
	}
	
}

void UGA_Ultimate::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Ultimate::OnSequenceFinished()
{
	if (ACharacter* MyCharacter = Cast<ACharacter>(GetAvatarActorFromActorInfo()))
	{
		MyCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	}
	
	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
