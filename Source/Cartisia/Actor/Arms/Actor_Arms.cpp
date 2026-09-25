// Fill out your copyright notice in the Description page of Project Settings.


#include "Actor_Arms.h"

#include "AbilitySystemComponent.h"

// Sets default values
AActor_Arms::AActor_Arms()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	
	SkeletalMeshComponent= CreateDefaultSubobject<USkeletalMeshComponent>(FName("SpringArmComponent"));
	SetRootComponent(SkeletalMeshComponent);
	
	AbilitySystemComponent= CreateDefaultSubobject<UAbilitySystemComponent>("AbilitySystemComponent");
	
}

UAbilitySystemComponent* AActor_Arms::GetAbilitySystemComponent() const
{
	if (!AbilitySystemComponent)return nullptr;
	return AbilitySystemComponent;
}

// Called when the game starts or when spawned
void AActor_Arms::BeginPlay()
{
	Super::BeginPlay();
	
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this,this);
		
		for (const auto& GA:GameplayAbility)
		{
			AbilitySystemComponent->GiveAbility(GA);
		}
	}
	
}

// Called every frame
void AActor_Arms::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AActor_Arms::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

