// Fill out your copyright notice in the Description page of Project Settings.


#include "PlayerAttributes.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

UPlayerAttributes::UPlayerAttributes()
{
	InitHP(100.f);
	InitMaxHP(100.f);
	
	InitMovementSpeed(400.f);
	InitMaxMovementSpeed(600.f);
	
}

void UPlayerAttributes::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	
	if (Data.EvaluatedData.Attribute== GetHPAttribute())
	{
		SetHP(FMath::Clamp(GetHP(),0.f,GetMaxHP()));
	}
	if (Data.EvaluatedData.Attribute== GetMovementSpeedAttribute())
	{
		SetMovementSpeed(FMath::Clamp(GetMovementSpeed(),0.f,GetMaxMovementSpeed()));
	}
	
}

void UPlayerAttributes::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	
	if (Attribute==GetHPAttribute())
	{
		NewValue=FMath::Clamp(NewValue,0.f,GetMaxHP());
	}
	if (Attribute==GetMovementSpeedAttribute())
	{
		NewValue=FMath::Clamp(NewValue,0.f,GetMaxMovementSpeed());
	}

}

void UPlayerAttributes::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	
	if (Attribute==GetHPAttribute())
	{
		SetHP(FMath::Clamp(GetHP(),0.f,GetMaxHP()));
		
		if (GetHP()<=0.f)
		{
			UE_LOG(LogTemp,Error,TEXT("死亡"));
		}
	}
	if (Attribute==GetMovementSpeedAttribute())
	{
		if (AActor* Actor = GetOwningActor())
		{
			if (ACharacter* Character = Cast<ACharacter>(Actor))
			{
				if (auto* CharacterMoveComponent = Character->GetCharacterMovement())
				{
					CharacterMoveComponent->MaxWalkSpeed=GetMovementSpeed();
				}
			}
		}
	}
}
