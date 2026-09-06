// Fill out your copyright notice in the Description page of Project Settings.


#include "KaTiXiYa.h"
#include "EnhancedInputComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Cartisia/AttributeSet/PlayerAttributes.h"
#include "AbilitySystemComponent.h"


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
}

// Called when the game starts or when spawned
void AKaTiXiYa::BeginPlay()
{
	Super::BeginPlay();
	
	InitInputMappingContext();
	
}

void AKaTiXiYa::StartTimer()
{
	GetWorldTimerManager().ClearTimer(TimerHandle);
	GetWorldTimerManager().SetTimer(TimerHandle,this,&ThisClass::StopTimer,0.01f,true);
}

void AKaTiXiYa::StopTimer()
{
	DefaultLength=FMath::FInterpTo(DefaultLength,TargetLength,0.01,3.f);
	if (SpringArmComponent)
	{
		SpringArmComponent->TargetArmLength= DefaultLength;
	}
	if (FMath::IsNearlyEqual(DefaultLength, TargetLength, 0.01f))
	{
		DefaultLength = TargetLength;
		GetWorldTimerManager().ClearTimer(TimerHandle);
	}
}

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

void AKaTiXiYa::MoveInputEvent(const FInputActionValue& InputEvent)
{
	InterruptAnimation();
	
	if (!bIsMoving)
	{
		bIsMoving=true;
	}
	
	FVector2D MousePosition2D=InputEvent.Get<FVector2D>();
	FRotator Rotation= GetController()->GetControlRotation();
	FRotator YawRotation(0.f,Rotation.Yaw,0.f);
	FVector ForwardVector=FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	FVector RightVector=FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	AddMovementInput(ForwardVector,MousePosition2D.X);
	AddMovementInput(RightVector,MousePosition2D.Y);
}

void AKaTiXiYa::PerspectiveEvent(const FInputActionValue& InputEvent)
{
	FVector2D MousePosition2D=InputEvent.Get<FVector2D>();
	AddControllerPitchInput(MousePosition2D.Y);
	AddControllerYawInput(MousePosition2D.X);
}

void AKaTiXiYa::SprintEvent(const FInputActionValue& InputEvent)
{
	if (!bIsLanded)
	{
		if (AbilitySystemComponent)
		{
			FGameplayEventData  Data;
			Data.Instigator=this;
			Data.EventTag=Ability_SprintTag;
			AbilitySystemComponent->HandleGameplayEvent(Ability_SprintTag,&Data);
		}
	}
}

void AKaTiXiYa::EndMoveInputEvent(const FInputActionValue& InputEvent)
{
	if (bIsMoving)
	{
		bIsMoving=false;
	}
	if (bIsLanded)
	{
		return;
	}
	if (AbilitySystemComponent)
	{
		FGameplayEventData  Data;
		Data.Instigator=this;
		Data.EventTag=Ability_StopWalkingTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_StopWalkingTag,&Data);
	}
}

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

void AKaTiXiYa::MouseWheelEvent(const FInputActionValue& InputEvent)
{
	float Direction= InputEvent.Get<float>();
	
	if ((TargetLength <= 60.f && Direction < 0) || (TargetLength >= 200.f && Direction > 0))
	{
		return;
	}
	
	TargetLength= FMath::Clamp(TargetLength+ Direction* 30.f,60.f,200.f); 
	StartTimer();
	
}

void AKaTiXiYa::SpaceEvent(const FInputActionValue& InputEvent)
{
	LandedTime= 0.f;
	LandedTime= GetWorld()->TimeSeconds;
	
	bIsLanded = true;
	
	if (AbilitySystemComponent)
	{
		InterruptAnimation();
		
		FGameplayEventData  Data;
		Data.Instigator=this;
		Data.EventTag=Ability_Jump;
		AbilitySystemComponent->HandleGameplayEvent(Ability_Jump,&Data);
	}
}

void AKaTiXiYa::EndSpaceEvent(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		FGameplayEventData Data;
		Data.Instigator=this;
		Data.EventTag=Event_AbilityJumpTag;
		AbilitySystemComponent->HandleGameplayEvent(Event_AbilityJumpTag,&Data);
	}
}

void AKaTiXiYa::InterruptAnimation()
{
	if (AbilitySystemComponent)
	{
		if (AbilitySystemComponent->HasMatchingGameplayTag(State_InterruptibleTag))
		{
			FGameplayTagContainer Container;
			Container.AddTag(FGameplayTag::RequestGameplayTag(FName("Ability")));
			AbilitySystemComponent->CancelAbilities(&Container);
		}
	}
}

// Called every frame
void AKaTiXiYa::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

// Called to bind functionality to input
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
		if (IA_RightMouseButton)
		{
			EnhancedInputComponent->BindAction(IA_RightMouseButton,ETriggerEvent::Started,this,&ThisClass::SprintEvent);
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
		if (IA_MouseWheel)
		{
			EnhancedInputComponent->BindAction(IA_MouseWheel,ETriggerEvent::Started,this,&ThisClass::MouseWheelEvent);
		}
		if (IA_Space)
		{
			EnhancedInputComponent->BindAction(IA_Space,ETriggerEvent::Started,this,&ThisClass::SpaceEvent);
			EnhancedInputComponent->BindAction(IA_Space,ETriggerEvent::Completed,this,&ThisClass::EndSpaceEvent);
		}
	}
}

void AKaTiXiYa::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this,this);
		for (const TSubclassOf<UGameplayAbility>& Ability : GameplayAbility)
		{
			AbilitySystemComponent->GiveAbility(Ability);
		}
	}
	
}

UAbilitySystemComponent* AKaTiXiYa::GetAbilitySystemComponent() const
{
	if (AbilitySystemComponent)
	{
		return AbilitySystemComponent;
	}
	return nullptr;
}

void AKaTiXiYa::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	
	float Time=GetWorld()->TimeSeconds-LandedTime;
	
	if (AbilitySystemComponent)
	{
		if (Time >= 1.5f)
		{
			FGameplayEventData Data;
			Data.Instigator=this;
			Data.EventTag=Ability_LandedTag;
			
			AbilitySystemComponent->HandleGameplayEvent(Ability_LandedTag,&Data);
			LandedEnum=ELandedEnum::Land_Roll;
		}
		else if (Time<= 1.5f && Time > 1.f)
		{
			LandedEnum=ELandedEnum::Land_Heavy;
		}
		else
		{
			LandedEnum=ELandedEnum::Land_Light;
		}
	}
	bIsLanded=false;
}
