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

	if (AbilitySystemComponent)
	{
		FGameplayEventData  Data;
		Data.Instigator=this;
		Data.EventTag=Ability_SprintTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_SprintTag,&Data);
	}
}

void AKaTiXiYa::EndMoveInputEvent(const FInputActionValue& InputEvent)
{
	if (AbilitySystemComponent)
	{
		FGameplayEventData  Data;
		Data.Instigator=this;
		Data.EventTag=Ability_StopWalkingTag;
		AbilitySystemComponent->HandleGameplayEvent(Ability_StopWalkingTag,&Data);
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
