// Fill out your copyright notice in the Description page of Project Settings.


#include "AN_SoundEffects.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"

void UAN_SoundEffects::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
                                   const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);

	if (AttackSound && MeshComp)
	{
		//生成一个挂载的音效组件
		if (UAudioComponent* Audio = UGameplayStatics::SpawnSoundAttached(AttackSound, MeshComp, AttachSocketName))
		{
			//打上识别标签，确保在 NotifyEnd 里能找到这一个音效实例
			Audio->ComponentTags.Add(FName("CombatAudioInstance"));
		}
	}
}

void UAN_SoundEffects::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	TArray<USceneComponent*> Children;
	MeshComp->GetChildrenComponents(false, Children);

	for (auto Child : Children)
	{
		if (UAudioComponent* Audio = Cast<UAudioComponent>(Child))
		{
			if (Audio->ComponentTags.Contains("CombatAudioInstance"))
			{
				//使用 FadeOut（平滑淡出）而不是直接 Stop，听感更自然
				Audio->FadeOut(FadeOutTime, 0.0f);
				//标记播放完毕后组件彻底回收
				Audio->bAutoDestroy = true;
			}
		}
	}
	
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
