#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShowcasePropAuthoring.generated.h"

class UAnimSequence;
class UBlueprint;
class USkeletalMesh;

/** Editor authoring of native Blueprint animation-notify graphs and component templates. */
UCLASS()
class WUWACHARACTERLABEDITOR_API UShowcasePropAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditNotifyTemplate(UBlueprint* Blueprint);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString ExerciseNotifyLifecycle(UBlueprint* Blueprint, class USkeletalMeshComponent* Parent,
        UAnimSequence* Animation);

    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static UBlueprint* AuthorNotify(const FString& AssetPath, USkeletalMesh* Mesh,
        UAnimSequence* PropAnimation, const FString& Tag, float SourceBegin, bool bLooping,
        FName Socket, const FTransform& RelativeTransform);

    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static bool ConfigureNotifies(UAnimSequence* Animation, const FString& SourceJSON, bool bVerifyOnly);
};
