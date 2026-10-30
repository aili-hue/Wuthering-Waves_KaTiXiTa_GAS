#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "WuWaPhysicsAuthoring.generated.h"

class UWuWaPhysicsProfile;
class UWuWaCharacterProfile;
class UAnimBlueprint;
class USkeletalMeshComponent;
class UAnimSequence;

/** Source conversion and reference checks remain editor-only. */
UCLASS()
class UWuWaPhysicsAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Use runtime setters without editor property-change reinitialization. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static void SetSimulationEnabled(USkeletalMeshComponent* Component, bool Enabled);

    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static void SetAnimationPaused(USkeletalMeshComponent* Component, bool Paused);

    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static UAnimBlueprint* BuildPostProcessBlueprint(UWuWaCharacterProfile* Character, const FString& AssetPath);

    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static bool ConfigureProfile(UWuWaPhysicsProfile* Profile, const FString& SettingsJson);

    /** UEFormat contains reconstructed absolute poses; UE derives their original
     * local additive deltas against the reference pose during compression. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static bool ConfigureRibbon(UAnimSequence* Animation, const FString& SourceJson);

    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static bool VerifyRibbonSource(UAnimSequence* Animation, const FString& Filename);

    UFUNCTION(BlueprintCallable, Category="WuWa|Physics")
    static FString AuditProfile(UWuWaCharacterProfile* Character);
};
