// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "DA_AbilitySet.generated.h"

class UGameplayAbility;

USTRUCT(BlueprintType)
struct FAbility
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere,Category="GameplayAbility")
	TSubclassOf<UGameplayAbility>Ability;
	
};

UCLASS(BlueprintType)
class CARTISIA_API UDA_AbilitySet : public UDataAsset
{
	GENERATED_BODY()
public:
	
	UPROPERTY(EditAnywhere,Category="GameplayAbility")
	TArray<FAbility> Abilities;
};
