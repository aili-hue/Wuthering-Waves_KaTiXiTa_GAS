#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WuWaCharacterProfile.generated.h"

class UWuWaPhysicsProfile;
class UAnimSequence;
class UAnimSequenceBase;
class UMaterialInterface;
class USoundBase;
class USkeletalMesh;
class UTexture2D;

/** Semantic surface kernels shared by every character; values also select outlines. */
UENUM(BlueprintType)
enum class EWuWaSurfacePart : uint8
{
    Face UMETA(DisplayName="脸部"), Down UMETA(DisplayName="下身"), Up UMETA(DisplayName="身体／上身"),
    Eye UMETA(DisplayName="眼睛"), Bangs UMETA(DisplayName="刘海"), Hair UMETA(DisplayName="头发"),
    Star UMETA(DisplayName="星痕特效")
};

USTRUCT(BlueprintType)
struct FWuWaCharacterSurface
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
    FName Slot;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
    EWuWaSurfacePart Part = EWuWaSurfacePart::Up;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
    TObjectPtr<UMaterialInterface> Stencil;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
    TObjectPtr<UMaterialInterface> OutlineMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface")
    bool bOutline = true;
};

UENUM(BlueprintType)
enum class EWuWaShowcaseMode : uint8 { Nature, Weapon, Resonant, Chip };

USTRUCT(BlueprintType)
struct FWuWaShowcaseAnimation
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
    EWuWaShowcaseMode Mode = EWuWaShowcaseMode::Nature;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
    TObjectPtr<UAnimSequence> Start;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
    TObjectPtr<UAnimSequence> Loop;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Animation")
    TObjectPtr<UAnimSequence> End;
};

/** Saved native assets are the sole runtime source for character switching. */
UCLASS(BlueprintType)
class WUWACHARACTERLAB_API UWuWaCharacterProfile : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    int32 RoleId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TObjectPtr<USkeletalMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TObjectPtr<UWuWaPhysicsProfile> Physics;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    FName HeadBone = TEXT("Bip001Head");

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TObjectPtr<UTexture2D> HeadIcon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TArray<FWuWaCharacterSurface> Surfaces;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Character")
    TArray<FWuWaShowcaseAnimation> Animations;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
    TObjectPtr<USkeletalMesh> Weapon;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
    TObjectPtr<UAnimSequenceBase> WeaponIdleAnimation;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
    FName WeaponSocket;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
    TArray<FName> AdditionalWeaponSockets;

    /** Optional distinct models, in the same order as AdditionalWeaponSockets. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
    TArray<TObjectPtr<USkeletalMesh>> AdditionalWeaponModels;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
    TArray<FWuWaCharacterSurface> WeaponSurfaces;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voice")
    TArray<TObjectPtr<USoundBase>> JoinTeamVoices;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Voice")
    TObjectPtr<USoundBase> NatureVoice;
};
