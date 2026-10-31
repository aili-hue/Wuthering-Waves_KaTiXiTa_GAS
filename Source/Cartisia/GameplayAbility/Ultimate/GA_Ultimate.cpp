// Fill out your copyright notice in the Description page of Project Settings.


#include "GA_Ultimate.h"

#include "Camera/CameraComponent.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Cartisia/Character/KaTiXiYa.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "AbilitySystemComponent.h"

void UGA_Ultimate::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                   const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ActivateAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);
	
	AKaTiXiYa* Hero = Cast<AKaTiXiYa>(GetAvatarActorFromActorInfo());
	if (!Hero)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	if (USpringArmComponent* SpringArm = Hero->SpringArmComponent)
	{
		SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 120.f));
	}

	if (USkeletalMeshComponent* MeshComp = Hero->GetMesh())
	{
		if (UltimateMesh)
		{
			MeshComp->SetSkeletalMesh(UltimateMesh);
		}
		if (UltimateAnimBP)
		{
			MeshComp->SetAnimInstanceClass(UltimateAnimBP);
		}
	}

	if (!UltimateSequence)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 1. 设置播放参数
	FMovieSceneSequencePlaybackSettings Settings;
	// 关闭移动/视角输入：ULevelSequencePlayer 播放时内部会调用 PlayerController->SetCinematicMode，
	// 结束时（Stop）会自动还回来，所以不需要自己再 DisableInput / EnableInput。
	// 手动那套做法在能力被打断、或序列没创建成功时会把输入永久关掉。
	Settings.bDisableMovementInput = true;
	Settings.bDisableLookAtInput = true;
	Settings.bHidePlayer = false; // 确保不隐藏真实角色
	// 关键：播到最后一帧先“暂停”，不要自动 Stop。
	// 暂停时序列还没有 Restore 它改过的属性，我们才能读到“动画结尾”的角色 Transform。
	Settings.bPauseAtEnd = true;

	// 2. 创建播放器
	SequencePlayer = ULevelSequencePlayer::CreateLevelSequencePlayer(
		GetWorld(),
		UltimateSequence,
		Settings,
		SequenceActor // 这是一个 ALevelSequenceActor* 指针
	);

	if (!SequencePlayer || !SequenceActor)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// ALevelSequenceActor 也是一个 Actor。由于你在编辑器里 K 的帧通常是基于 (0,0,0) 的相对坐标，
	// 那么你只需要把这个 Actor 的世界位置挪到玩家目前所在的脚下
	SequenceActor->SetActorTransform(Hero->GetActorTransform());

	// 3. 动态绑定角色到占位符
	SequenceActor->SetBindingByTag(FName("HeroPlaceholder"), TArray<AActor*>{ Hero });

	// 4. 监听结束并开始播放
	SequencePlayer->OnFinished.AddDynamic(this, &UGA_Ultimate::OnSequenceFinished);

	// 5. 物理模式保护：消除重力干扰，防止由于镜头旋转导致的位移偏差
	Hero->GetCharacterMovement()->DisableMovement();
	Hero->GetCharacterMovement()->StopMovementImmediately();

	SequencePlayer->Play();
}

void UGA_Ultimate::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// 能力被外部打断（没等到 OnSequenceFinished）时，别把摄像机和 spawn 出来的临时 Actor 留在世界里
	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer = nullptr;
	}
	if (SequenceActor)
	{
		SequenceActor->Destroy();
		SequenceActor = nullptr;
	}
	
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Ultimate::OnSequenceFinished()
{
	AKaTiXiYa* Hero = Cast<AKaTiXiYa>(GetAvatarActorFromActorInfo());
	if (!Hero)
	{
		EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
		return;
	}

	// ---------- 1. 趁序列还停在最后一帧，先把“动画结尾的状态”抄下来 ----------
	// Sequencer 里每个 Section 默认是 When Finished = Restore State
	//（LevelSequence 的 DefaultCompletionMode 在 BaseEngine.ini 里就是 RestoreState），
	// 所以一旦 Stop，序列会把角色 Transform 还原成“播放前”的值。
	// 而玩家的摄像机是挂在角色身上的 SpringArm 上的：角色被拉回去，摄像机自然也跟着回到变身前的点。
	const FTransform FinalHeroTransform = Hero->GetActorTransform();

	// 最后一个镜头（CineCamera）的世界变换：过场结束后玩家摄像机就停在这个机位上
	FTransform FinalCameraTransform(Hero->GetActorRotation(), Hero->GetActorLocation());
	if (SequencePlayer)
	{
		if (const UCameraComponent* ActiveCamera = SequencePlayer->GetActiveCameraComponent())
		{
			FinalCameraTransform = ActiveCamera->GetComponentTransform();
		}
	}

	// ---------- 2. 真正结束序列 ----------
	// 这一步负责：还原被序列改过的属性 / 把 view target 还给玩家 / 销毁临时相机和 spawnable
	if (SequencePlayer)
	{
		SequencePlayer->Stop();
		SequencePlayer = nullptr;
	}
	if (SequenceActor)
	{
		SequenceActor->Destroy();
		SequenceActor = nullptr;
	}

	// ---------- 3. 把角色放回“动画结尾”的位置 ----------
	Hero->SetActorTransform(FinalHeroTransform, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCharacterMovementComponent* MoveComp = Hero->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->SetMovementMode(MOVE_Walking);
	}

	//限制玩家摄像机高度（要先把支点定下来，下面才算得准）
	if (USpringArmComponent* SpringArm = Hero->SpringArmComponent)
	{
		SpringArm->SetRelativeLocation(FVector(0.0f, 0.0f, 120.f));
	}

	// ---------- 4. 把玩家摄像机停在过场结尾的机位上 ----------
	// 玩家摄像机是 SpringArm 的末端：位置 = 支点 - 镜头朝向 * ArmLength
	// 所以反解出 ArmLength，末端就正好落在最后一个镜头的位置上。
	// 只取 Pitch / Yaw，Roll 归零 —— 第三人称镜头一旦带上 Roll，画面就是歪的。
	const FRotator CameraRotation = FinalCameraTransform.Rotator();
	const FRotator CinematicRotation(CameraRotation.Pitch, CameraRotation.Yaw, 0.f);
	
	
	if (USpringArmComponent* SpringArm = Hero->SpringArmComponent)
	{
		const FVector PivotLocation = Hero->GetActorTransform().TransformPosition(SpringArm->GetRelativeLocation());
		const FVector CameraForward = CinematicRotation.Vector();

		// 相机在角色“正面朝外”时算出来会是负数，用最小值兜底（否则 Boom 长度不能为负）
		ArmLength = FMath::Max(
			FVector::DotProduct(PivotLocation - FinalCameraTransform.GetLocation(), CameraForward), 30.f);

		Hero->SetCameraArmLength(ArmLength);
		SpringArm->SetWorldRotation(CinematicRotation);

		// 立刻让 SpringArm 自己算一次，别等它下一次 Tick。
		// 否则这一帧摄像机还停在旧机位上，下一帧才跳过来 —— 就是结尾那下顿挫。
		SpringArm->TickComponent(0.f, LEVELTICK_All, nullptr);
	}

	// 玩家朝向也要掰到结尾镜头的方向，否则下一帧 ControllerRotation 一刷新，角色又会转回老朝向
	if (APlayerController* PlayerController = Cast<APlayerController>(Hero->GetController()))
	{
		PlayerController->SetControlRotation(CinematicRotation);
		
	}
	
	// ---------- 5. 变身效果 ----------
	if (UAbilitySystemComponent* AbilitySystemComponent = GetAbilitySystemComponentFromActorInfo())
	{
		if (UltimateEffect)
		{
			FGameplayEffectContextHandle ContextHandle = AbilitySystemComponent->MakeEffectContext();
			FGameplayEffectSpecHandle EffectSpecHandle = AbilitySystemComponent->MakeOutgoingSpec(UltimateEffect, 1.f, ContextHandle);
			if (EffectSpecHandle.IsValid())
			{
				AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*EffectSpecHandle.Data.Get());
			}
		}
	}

	EndAbility(CurrentSpecHandle, CurrentActorInfo, CurrentActivationInfo, true, false);
}
