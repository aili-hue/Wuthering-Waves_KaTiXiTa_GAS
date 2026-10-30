// Fill out your copyright notice in the Description page of Project Settings.


#include "KaTiXiYa.h"
#include "EnhancedInputComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Cartisia/AttributeSet/PlayerAttributes.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Cartisia/Actor/Arms/Actor_Arms.h"
#include "Cartisia/DataAsset/AbilitySet/DA_AbilitySet.h"


// Sets default values
AKaTiXiYa::AKaTiXiYa()
{
	PrimaryActorTick.bCanEverTick = true;
	
	SpringArmComponent=CreateDefaultSubobject<USpringArmComponent>(FName("SpringArmComponent"));
	SpringArmComponent->SetupAttachment(RootComponent);
	SpringArmComponent->TargetArmLength= DefaultLength;
	
	CameraComponent=CreateDefaultSubobject<UCameraComponent>(FName("CameraComponent"));
	CameraComponent->SetupAttachment(SpringArmComponent);
	
	AbilitySystemComponent=CreateDefaultSubobject<UAbilitySystemComponent>(FName("AbilitySystemComponent"));
	PlayerAttributes=CreateDefaultSubobject<UPlayerAttributes>(TEXT("PlayerAttributes"));
	
	DollMesh= CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("DollMesh"));
	DollMesh->SetupAttachment(GetMesh(),TEXT("Bip001RHandDoll"));
	
	SkeletalMesh= CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("SkeletalMesh"));
	SkeletalMesh->SetupAttachment(GetMesh(),TEXT("Jian"));
	SkeletalMesh->SetVisibility(false);
}

// Called when the game starts or when spawned
// 作用: 初始化输入映射、重生后授予落地Buff、按默认武器生成并挂载武器Actor,
// 并注册"娃娃模型切换(State.Visual.Doll)"与"终极形态(State.Form.Ultimate)"两个标签事件的监听回调。
void AKaTiXiYa::BeginPlay()
{
	Super::BeginPlay();
	
	InitInputMappingContext();
	Data_LandedEvent();
	SpawnAttachmentActor(WeaponMesh,WeaponSocket);
	
	if(DollMesh)
	{
		DollMesh->SetVisibility(false,false);
	}
	
	if (AbilitySystemComponent)
	{
		DollHandle= AbilitySystemComponent->RegisterGameplayTagEvent(State_Visual_Doll).AddUObject(this,&ThisClass::DollEvent);
		UltimateFormHandle= AbilitySystemComponent->RegisterGameplayTagEvent(State_Form_UltimateTag).AddUObject(this,&ThisClass::OnUltimateFormChanged);
	}
}

// 作用: State.Visual.Doll 标签事件的回调——当标签存在(Number>0)时隐藏武器手臂、显示手中的娃娃模型, 反之恢复武器显示。
void AKaTiXiYa::DollEvent(FGameplayTag EventTag, int32 Number)
{
	if (!WeaponMesh)return;
	if (AActor_Arms* Arms= Cast<AActor_Arms>(AttachmentActor))
	{
		Arms->SkeletalMeshComponent->SetVisibility(Number>0 ? false : true);
	}
	DollMesh->SetVisibility(Number>0);
}


// 作用: 生成指定武器类型的Actor(销毁旧武器), 并将其挂接到角色骨架上的指定Socket。
void AKaTiXiYa::SpawnAttachmentActor(TSubclassOf<AActor> WeaponMeshs,FName SocketName)
{
	if (!WeaponMeshs)return;
	
	if (AttachmentActor)
	{
		AttachmentActor->Destroy(); 
		AttachmentActor = nullptr;  
	}
	
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Owner = this;
	SpawnInfo.Instigator =this;
	SpawnInfo.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AttachmentActor = GetWorld()->SpawnActor<AActor>(WeaponMeshs,SpawnInfo);
	if (AttachmentActor)
	{
		AttachmentActor->AttachToComponent(GetMesh(),FAttachmentTransformRules::SnapToTargetIncludingScale,SocketName);
	}
}

// 作用: 向本地玩家注册基础输入映射上下文(IMC_Foundation), 使角色默认输入(移动/攻击/技能等)生效。
void AKaTiXiYa::InitInputMappingContext()
{
	if (AController* InputController = Cast<AController>(GetController()))
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(InputController))
		{
			if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
			{
				if (UEnhancedInputLocalPlayerSubsystem* LocalPlayerSubsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
				{
					if (IMC_Foundation)
					{
						LocalPlayerSubsystem->AddMappingContext(IMC_Foundation,0);
					}
				}
			}
		}
	}
}

// 作用: 移动输入(IA_Move)触发时——先通过InterruptAnimation打断可打断的技能, 再在非移动状态下施加移动Buff,
// 最后将输入方向换算成基于控制器朝向的前/右向量, 调用AddMovementInput驱动角色移动(相对摄像机方向移动)。
void AKaTiXiYa::MoveInputEvent(const FInputActionValue& InputEvent)
{
	for (const FGameplayTag& Tag: AbilityTag)
	{
		InterruptAnimation(Tag);
	}
	
	if (!AbilitySystemComponent->HasMatchingGameplayTag(Data_MovingTag) && BuffEffect.Find(Data_MovingTag))
	{
		FGameplayEffectContextHandle ContextHandle= AbilitySystemComponent->MakeEffectContext();
		FGameplayEffectSpecHandle SpecHandle= AbilitySystemComponent->MakeOutgoingSpec(BuffEffect[Data_MovingTag],1.f,ContextHandle);
		if (SpecHandle.IsValid())
		{
			MovingHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
		}
	}
	
	FVector2D MousePosition2D=InputEvent.Get<FVector2D>();
	FRotator Rotation= GetController()->GetControlRotation();
	FRotator YawRotation(0.f,Rotation.Yaw,0.f);
	FVector ForwardVector=FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	FVector RightVector=FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(ForwardVector,MousePosition2D.X);
	AddMovementInput(RightVector,MousePosition2D.Y);
}

// 作用: 视角输入(IA_Perspective)触发时——将二维鼠标偏移分别转换为相机俯仰(Pitch)与偏航(Yaw)输入。
void AKaTiXiYa::PerspectiveEvent(const FInputActionValue& InputEvent)
{
	FVector2D MousePosition2D=InputEvent.Get<FVector2D>();
	AddControllerPitchInput(MousePosition2D.Y);
	AddControllerYawInput(MousePosition2D.X);
}

// 作用: 冲刺输入(右键/Shift)触发时——根据当前是否处于移动状态设置EventMagnitude(移动中=1), 发出 Ability.Sprint 事件激活冲刺GA。
void AKaTiXiYa::SprintEvent(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		FGameplayEventData  Data;
		Data.Instigator=this;
		
		Data.EventMagnitude= AbilitySystemComponent->HasMatchingGameplayTag(Data_MovingTag)? 1.f : 0.f;
		
		Data.EventTag=Ability_SprintTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_SprintTag,&Data);
	}
}

// 作用: 移动输入结束(IA_Move Completed)时——移除移动Buff; 若不在坠落状态则发出 Ability.StopWalking 事件(急停/调整数值),
// 再调用SpeedSwitching施加停止类Buff。注释说明: 因跳跃会阻止急停动画GA, 故把"数值修改"与"调整"拆成两个GA处理。
void AKaTiXiYa::EndMoveInputEvent(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent->HasMatchingGameplayTag(Data_MovingTag))
	{
		if (MovingHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(MovingHandle);
			MovingHandle.Invalidate();
		}
	}
	//因为跳跃会阻止急停动画Ability，所以我将他的修改数值和数值调整分为了两个GA
	
	if (AbilitySystemComponent)
	{
		if (!AbilitySystemComponent->HasMatchingGameplayTag(Data_FallingTag))
		{
			FGameplayEventData  AbilityData;
			AbilityData.Instigator=this;
			AbilityData.EventTag=Ability_StopWalkingTag;
			AbilitySystemComponent->HandleGameplayEvent(Ability_StopWalkingTag,&AbilityData);
		}
		
		SpeedSwitching();
	}
}

// 作用: 行走切换输入(IA_Ctrl)触发时——发出 Ability.Walk 事件激活行走GA(切换步行/跑步速度)。
void AKaTiXiYa::CtrlEvent(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		FGameplayEventData  Data;
		Data.Instigator=this;
		Data.EventTag=Ability_WalkTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_WalkTag,&Data);
	}
}

// 作用: 鼠标滚轮输入触发时——按滚轮方向以步长30调整相机弹簧臂目标长度(TargetLength), 限制在[60, MaxTargetLength]区间, 由Tick负责插值平滑。
void AKaTiXiYa::MouseWheelEvent(const FInputActionValue& InputEvent)
{
	float Direction= InputEvent.Get<float>();
	
	if ((TargetLength <= 60.f && Direction < 0) || (TargetLength >= MaxTargetLength && Direction > 0))
	{
		return;
	}
	
	TargetLength= FMath::Clamp(TargetLength+ Direction* 30.f,60.f,MaxTargetLength); 
	
}

// 作用: 外部(如终极技能结尾)设置相机弹簧臂长度——同步更新TargetLength与SpringArmComponent, 防止Tick插值把长度拉回滚轮缩放值。
void AKaTiXiYa::SetCameraArmLength(float NewArmLength)
{
	// 必须同时改 TargetLength，否则 Tick 里的 FInterpTo 会立刻把长度拉回滚轮缩放值
	TargetLength = FMath::Max(NewArmLength, 10.f);

	if (SpringArmComponent)
	{
		SpringArmComponent->TargetArmLength = NewArmLength;
	}
}

// 作用: 跳跃输入(IA_Space)触发时——若当前无跳跃禁止标签(Data.Jump), 先打断可打断技能, 再发出 Ability.Jump 事件激活跳跃GA。
void AKaTiXiYa::SpaceEvent(const FInputActionValue& InputEvent)
{
	
	if (AbilitySystemComponent)
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(Data_JumpTag))return;
		
		for (const FGameplayTag& Tag: AbilityTag)
		{
			InterruptAnimation(Tag);
		}
		
		FGameplayEventData  Data;
		Data.Instigator=this;
		Data.EventTag=Ability_Jump;
		AbilitySystemComponent->HandleGameplayEvent(Ability_Jump,&Data);
	}
}

// 作用: 攻击键按下(IA_Left_MouseButton Started)时——记录按下时刻, 用于后续判定重击蓄力时长。
void AKaTiXiYa::AttackInputStarted(const FInputActionValue& InputEvent)
{
	AttackStartTime= GetWorld()->GetTimeSeconds();
}

// 作用: 攻击键按住(IA_Left_MouseButton Triggered)时——持续计时, 按住时长超过HeavyAttackThreshold即自动触发重击(Ability.Fight.HeavyBlow)事件, 并用bHeavyAttackAutoTriggered防止重复触发。
void AKaTiXiYa::AttackInputHold(const FInputActionValue& InputEvent)
{
	if (bHeavyAttackAutoTriggered)return;
	
	float CurrentHoldTime = GetWorld()->GetTimeSeconds() - AttackStartTime;
	
	if (CurrentHoldTime >= HeavyAttackThreshold)
	{
		if (AbilitySystemComponent)
		{
			bHeavyAttackAutoTriggered=true;
	
			FGameplayEventData Data;
			Data.Instigator=this;
			Data.EventTag=Ability_Fight_HeavyBlow;
			AbilitySystemComponent->HandleGameplayEvent(Ability_Fight_HeavyBlow,&Data);
		}
	}
	
}

// 作用: 攻击键抬起(IA_Left_MouseButton Completed)时——若角色处于连招状态(Data.Attack):
//   ①有"连续打断"标签(State.ContinuousInterruption)则发出连招推进事件 Event.Attack(蒙太奇通知施加该标签时连打即连招);
//   ②有 Data.StopGA 标签则取消普攻Ability;
// 若本次非蓄力重击, 再发出 Ability.Fight.NormalAttack 事件激活普攻; 最后复位蓄力计时状态。
void AKaTiXiYa::AttackInputReleased(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		//如果GA赋予角色连招状态Data_AttackTag
		if (AbilitySystemComponent->HasMatchingGameplayTag(Data_AttackTag))
		{
			//如果有打断tag，让后GA在动画蒙太奇通知特定时间应用State_ContinuousInterruption，角色就发送连招Event_Attack
			
			if (AbilitySystemComponent->HasMatchingGameplayTag(State_ContinuousInterruption))
			{
				FGameplayEventData EventData;
				EventData.Instigator=this;
				EventData.EventTag=Event_Attack;
				AbilitySystemComponent->HandleGameplayEvent(Event_Attack,&EventData);
				
				return;
			}
			if (AbilitySystemComponent->HasMatchingGameplayTag(Data_StopGATag))
			{
				FGameplayTagContainer Container;
				Container.AddTag(Ability_Fight_NormalAttack);
				AbilitySystemComponent->CancelAbilities(&Container);
			}
		}
		if (!bHeavyAttackAutoTriggered)
		{
			FGameplayEventData Data;
			Data.Instigator=this;
			Data.EventTag=Ability_Fight_NormalAttack;
			AbilitySystemComponent->HandleGameplayEvent(Ability_Fight_NormalAttack,&Data);
		}
	}
	
	AttackStartTime= 0;
	bHeavyAttackAutoTriggered=false;
}

// 作用: 技能键(IA_E)触发时——发出 Ability.Skill 事件激活技能GA。
void AKaTiXiYa::E_Event(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		FGameplayEventData Data;
		Data.Instigator=this;
		Data.EventTag=Ability_SkillTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_SkillTag,&Data);
	}
}

// 作用: 终极技键(IA_Q)触发时——发出 Ability.Ultimate 事件激活终极技GA。
void AKaTiXiYa::Q_Event(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		FGameplayEventData Data;
		Data.Instigator=this;
		Data.EventTag=Ability_UltimateTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_UltimateTag,&Data);
	}
}

// 作用: 落地事件(由OnMovementModeChanged在坠落→行走/寻路时调用)——
//   ①发出 Event.EndAbilityJump 结束跳跃GA;
//   ②依据腾空时长(距上次进入坠落的时间)判定落地形态(滚动/重着陆/轻着陆)并记录到LandedEnum, 达到滚动阈值时发出 Ability.Landed 事件;
//   ③清除坠落Buff(Data.Airborne.Falling)与飞行Buff(Data.Airborne.Flying)以结束相关状态。
void AKaTiXiYa::LandedEvent()
{
	
	if (AbilitySystemComponent)
	{
		FGameplayEventData Data;
		Data.Instigator=this;
		Data.EventTag=Event_AbilityJumpTag;
		AbilitySystemComponent->HandleGameplayEvent(Event_AbilityJumpTag,&Data);
	}
	
	float Time=GetWorld()->TimeSeconds-LandedTime;
	
	if (AbilitySystemComponent)
	{
		if (Time >= Land_RollTime)
		{
			FGameplayEventData Data;
			Data.Instigator=this;
			Data.EventTag=Ability_LandedTag;
			
			AbilitySystemComponent->HandleGameplayEvent(Ability_LandedTag,&Data);
			LandedEnum=ELandedEnum::Land_Roll;
		}
		else if (Time<= Land_RollTime && Time > Land_LightTime)
		{
			LandedEnum=ELandedEnum::Land_Heavy;
		}
		else
		{
			LandedEnum=ELandedEnum::Land_Light;
		}
		
		LandedTime= 0.f;
	}
	if (AbilitySystemComponent->HasMatchingGameplayTag(Data_FallingTag))
	{
		if (FallingHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(FallingHandle);
			FallingHandle.Invalidate();
		}
	}
	if (AbilitySystemComponent->HasMatchingGameplayTag(Data_FlyingTag))
	{
		if (FlingHandle.IsValid())
		{
			AbilitySystemComponent->RemoveActiveGameplayEffect(FlingHandle);
			FlingHandle.Invalidate();
		}
	}
}

// 作用: 急停Buff切换(移动停止时调用)——当角色不在移动状态时, 向自身施加"停止(Data.Stop)"Buff用于调整移动相关数值(如速度曲线), 供StopWalking GA拆分工序使用。
void AKaTiXiYa::SpeedSwitching()
{
	if (!AbilitySystemComponent || !BuffEffect.Find(Data_StopTag)) return;
	
	FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
	FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(BuffEffect[Data_StopTag], 1.f, Context);
	if (Spec.IsValid())
	{
		AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
	}
}


// 作用: 打断动画GA——当角色持有"可打断"标签(State.Interruptible)时, 取消指定的技能Ability(供移动/跳跃等输入事件在发起前打断当前动画技能)。
void AKaTiXiYa::InterruptAnimation(FGameplayTag AbilityAnimationTag)
{
	if (AbilitySystemComponent)
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(State_InterruptibleTag))
		{
			FGameplayTagContainer Container;
			Container.AddTag(AbilityAnimationTag);
			AbilitySystemComponent->CancelAbilities(&Container);
		}
	}
}


// 作用: 落地Buff授予——若BuffEffect表配置了Data.Landed对应buff, 则向自身施加"着陆(Data.Landed)"Buff(落地后短时间内生效的状态效果, 如落地硬直/滚动)。
void AKaTiXiYa::Data_LandedEvent()
{
	if (BuffEffect.Find(Data_LandedTag))
	{
		if (Data_LandedTag.IsValid())
		{
			FGameplayEffectContextHandle Context=AbilitySystemComponent->MakeEffectContext();
			FGameplayEffectSpecHandle EffectSpec=AbilitySystemComponent->MakeOutgoingSpec(BuffEffect[Data_LandedTag],1.f,Context);
			if (EffectSpec.IsValid())
			{
				LandedHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*EffectSpec.Data.Get());
			}
		}
	}
}

// 作用: 切换技能组——先清除当前已授予的所有Ability(通过CurrentAbilityHandles回收), 再将新技能组(DA_AbilitySet)中的每个技能授予能力系统并记录Handle。
// 用于变身(终极形态)与变回基础形态时整体替换可用技能。
void AKaTiXiYa::SwitchAbilitySet(UDA_AbilitySet* NewAbilitySet)
{
	if (!AbilitySystemComponent|| !NewAbilitySet)return;
	
	for (const FGameplayAbilitySpecHandle& Handle : CurrentAbilityHandles)
	{
		AbilitySystemComponent->ClearAbility(Handle);
	}
	CurrentAbilityHandles.Empty();
	
	for (const FAbility& Entry : NewAbilitySet->Abilities)
	{
		if (Entry.Ability)
		{
			FGameplayAbilitySpec Spec(Entry.Ability, 1.f, INDEX_NONE, this);
			FGameplayAbilitySpecHandle Handle= AbilitySystemComponent->GiveAbility(Spec);
			CurrentAbilityHandles.Add(Handle);
		}
	}
}

// 作用: State.Form.Ultimate 标签事件回调——标签出现(NewValue>0)时进入终极形态: 生成剑武器并切换为终极技能组; 标签消失时切回基础技能组并恢复默认武器。
void AKaTiXiYa::OnUltimateFormChanged(FGameplayTag EventTag, int32 NewValue)
{
	
	if (NewValue>0)
	{
		SpawnAttachmentActor(SwordMesh,SwordName);
		SwitchAbilitySet(UltimateAbilitySet);
		return;
	}
	SwitchAbilitySet(BaseAbilitySet);
	SpawnAttachmentActor(WeaponMesh,WeaponSocket);
}

// Called every frame
// 作用: 每帧将相机弹簧臂长度向目标长度(TargetLength, 滚轮缩放/终极技设置的目标)以速率5平滑插值, 实现相机缩放的平滑过渡。
void AKaTiXiYa::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	if (!FMath::IsNearlyEqual(SpringArmComponent->TargetArmLength, TargetLength, 0.1f))
	{
		SpringArmComponent->TargetArmLength = FMath::FInterpTo(
			SpringArmComponent->TargetArmLength, 
			TargetLength, 
			DeltaTime, 
			5.f 
		);
	}
	
}

// Called to bind functionality to input
// 作用: 绑定增强输入——将各InputAction(移动/视角/冲刺/行走/滚轮/跳跃/攻击/技能/终极)按事件类型(Triggered/Started/Completed)绑到对应处理函数。
void AKaTiXiYa::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	
	if (UEnhancedInputComponent* EnhancedInputComponent=Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (IA_Move)
		{
			EnhancedInputComponent->BindAction(IA_Move,ETriggerEvent::Triggered,this,&ThisClass::MoveInputEvent);
			EnhancedInputComponent->BindAction(IA_Move,ETriggerEvent::Completed,this,&ThisClass::EndMoveInputEvent);
		}
		if (IA_Perspective)
		{
			EnhancedInputComponent->BindAction(IA_Perspective,ETriggerEvent::Triggered,this,&ThisClass::PerspectiveEvent);
		}
		if (IA_Right_MouseButton)
		{
			EnhancedInputComponent->BindAction(IA_Right_MouseButton,ETriggerEvent::Started,this,&ThisClass::SprintEvent);
		}
		if (IA_Shift_L)
		{
			EnhancedInputComponent->BindAction(IA_Shift_L,ETriggerEvent::Started,this,&ThisClass::SprintEvent);
		}
		if (IA_Shift_R)
		{
			EnhancedInputComponent->BindAction(IA_Shift_R,ETriggerEvent::Started,this,&ThisClass::SprintEvent);
		}
		if (IA_Ctrl)
		{
			EnhancedInputComponent->BindAction(IA_Ctrl,ETriggerEvent::Started,this,&ThisClass::CtrlEvent);
		}
		if (IA_Mouse_Wheel)
		{
			EnhancedInputComponent->BindAction(IA_Mouse_Wheel,ETriggerEvent::Started,this,&ThisClass::MouseWheelEvent);
		}
		if (IA_Space)
		{
			EnhancedInputComponent->BindAction(IA_Space,ETriggerEvent::Started,this,&ThisClass::SpaceEvent);
			
		}
		if (IA_Left_MouseButton)
		{
			EnhancedInputComponent->BindAction(IA_Left_MouseButton,ETriggerEvent::Started,this,&ThisClass::AttackInputStarted);
			EnhancedInputComponent->BindAction(IA_Left_MouseButton,ETriggerEvent::Triggered,this,&ThisClass::AttackInputHold);
			EnhancedInputComponent->BindAction(IA_Left_MouseButton,ETriggerEvent::Completed,this,&ThisClass::AttackInputReleased);
		}
		if (IA_E)
		{
			EnhancedInputComponent->BindAction(IA_E,ETriggerEvent::Started,this,&ThisClass::E_Event);
		}
		if (IA_Q)
		{
			EnhancedInputComponent->BindAction(IA_Q,ETriggerEvent::Started,this,&ThisClass::Q_Event);
		}
	}
}

// 作用: 被Controller接管时调用——初始化能力系统的Actor信息(Owner/Avatar均为自身), 并授予基础技能组。GAS 生命周期的关键入口。
void AKaTiXiYa::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this,this);
		
		SwitchAbilitySet(BaseAbilitySet);
	}
	
}

// 作用: 实现IAbilitySystemInterface——向GAS系统暴露本角色的AbilitySystemComponent(供其它模块通过接口安全获取)。
UAbilitySystemComponent* AKaTiXiYa::GetAbilitySystemComponent() const
{
	if (AbilitySystemComponent)
	{
		return AbilitySystemComponent;
	}
	return nullptr;
}

// 作用: 移动模式变化(GAS 状态机核心驱动)时——
//   ①进入坠落(MOVE_Falling): 移除飞行Buff并授予坠落Buff(Data.Airborne.Falling)、记录坠落起始时间、移除着陆Buff;
//   ②从坠落→行走/寻路(落地): 打断二段跳GA、授予着陆Buff(Data.Landed)并调用LandedEvent结算落地;
//   ③进入飞行(MOVE_Flying): 授予飞行Buff(Data.Airborne.Flying)。
void AKaTiXiYa::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	
	if (!AbilitySystemComponent)return;
	
	//坠落
	if (GetCharacterMovement()->MovementMode == MOVE_Falling)
	{
		//销毁Flying
		if (AbilitySystemComponent->HasMatchingGameplayTag(Data_FlyingTag))
		{
			if (FlingHandle.IsValid())
			{
				AbilitySystemComponent->RemoveActiveGameplayEffect(FlingHandle);
				FlingHandle.Invalidate();
			}
		}
		
		if (!AbilitySystemComponent->HasMatchingGameplayTag(Data_FallingTag))
		{
			if (BuffEffect.Find(Data_FallingTag))
			{
				FGameplayEffectContextHandle ContextHandle= AbilitySystemComponent->MakeEffectContext();
				FGameplayEffectSpecHandle SpecHandle= AbilitySystemComponent->MakeOutgoingSpec(BuffEffect[Data_FallingTag],1.f,ContextHandle);
				if (SpecHandle.IsValid())
				{
					FallingHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}
		}
		
		LandedTime = GetWorld()->GetTimeSeconds();
		
		if (AbilitySystemComponent->HasMatchingGameplayTag(Data_LandedTag))
		{
			if (LandedHandle.IsValid())
			{
				AbilitySystemComponent->RemoveActiveGameplayEffect(LandedHandle);
				LandedHandle.Invalidate();
			}
		}
		
	}
	
	//从掉落到落地
	if (PreviousMovementMode== MOVE_Falling && (GetCharacterMovement()->MovementMode == MOVE_Walking|| GetCharacterMovement()->MovementMode == MOVE_NavWalking))
	{
		InterruptAnimation(Ability_DoubleJumpTag);
		Data_LandedEvent();
		LandedEvent();
	}
	
	//飞行模式
	if (GetCharacterMovement()->MovementMode == MOVE_Flying)
	{
		
		if (!AbilitySystemComponent->HasMatchingGameplayTag(Data_FlyingTag))
		{
			if (BuffEffect.Find(Data_FlyingTag))
			{
				FGameplayEffectContextHandle ContextHandle= AbilitySystemComponent->MakeEffectContext();
				FGameplayEffectSpecHandle SpecHandle= AbilitySystemComponent->MakeOutgoingSpec(BuffEffect[Data_FlyingTag],1.f,ContextHandle);
				if (SpecHandle.IsValid())
				{
					FlingHandle= AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
				}
			}
		}
	}
}

// 作用: 角色销毁/退出游玩时——移除标签事件监听(娃娃切换/终极形态)并销毁挂载的武器Actor, 防止委托悬垂与Actor残留。
void AKaTiXiYa::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->RegisterGameplayTagEvent(State_Visual_Doll).Remove(DollHandle);
		AbilitySystemComponent->RegisterGameplayTagEvent(State_Form_UltimateTag).Remove(UltimateFormHandle);
	}
	if (AttachmentActor)
	{
		AttachmentActor->Destroy();
		AttachmentActor = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}
