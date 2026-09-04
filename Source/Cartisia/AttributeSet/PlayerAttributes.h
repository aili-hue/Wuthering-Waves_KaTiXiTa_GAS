// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "PlayerAttributes.generated.h"

/**
 * 
 */
UCLASS()
class CARTISIA_API UPlayerAttributes : public UAttributeSet
{
	GENERATED_BODY()
	
protected:
	UPlayerAttributes();
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FGameplayAttributeData HP;
	ATTRIBUTE_ACCESSORS_BASIC(UPlayerAttributes,HP);
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FGameplayAttributeData MaxHP;
	ATTRIBUTE_ACCESSORS_BASIC(UPlayerAttributes,MaxHP);
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FGameplayAttributeData SpringArmLength;
	ATTRIBUTE_ACCESSORS_BASIC(UPlayerAttributes,SpringArmLength);
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FGameplayAttributeData MaxSpringArmLength;
	ATTRIBUTE_ACCESSORS_BASIC(UPlayerAttributes,MaxSpringArmLength);
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FGameplayAttributeData MovementSpeed;
	ATTRIBUTE_ACCESSORS_BASIC(UPlayerAttributes,MovementSpeed);
	
	UPROPERTY(EditAnywhere,BlueprintReadWrite)
	FGameplayAttributeData MaxMovementSpeed;
	ATTRIBUTE_ACCESSORS_BASIC(UPlayerAttributes,MaxMovementSpeed);
	
public:
	
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData &Data)override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)override;
	
};
