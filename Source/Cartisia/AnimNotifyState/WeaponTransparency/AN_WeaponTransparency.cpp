// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_WeaponTransparency.h"
#include "Materials/MaterialParameterCollectionInstance.h"

void UAN_WeaponTransparency::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                                         float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	
	UMaterialParameterCollectionInstance* MPCInstance = 
		MeshComp->GetWorld()->GetParameterCollectionInstance(MaterialParameterCollection);

	if (MPCInstance)
	{
		MPCInstance->SetScalarParameterValue(FName("ce"), 1.f);
	}
	
}

void UAN_WeaponTransparency::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	
	UMaterialParameterCollectionInstance* MPCInstance = 
		MeshComp->GetWorld()->GetParameterCollectionInstance(MaterialParameterCollection);

	if (MPCInstance)
	{
		MPCInstance->SetScalarParameterValue(FName("ce"), 0.f);
	}
	
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
