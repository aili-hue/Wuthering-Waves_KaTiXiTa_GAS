#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "CharacterInputValidation.generated.h"

class USkeletalMesh;
class UMaterialInterface;
class UMaterial;
class UMaterialExpression;
class UAnimSequence;
class UAnimSequenceBase;
class UAnimComposite;
class UFactory;
class UStaticMesh;
class UTexture;
class USkeletalMeshComponent;
class UCameraComponent;
class UTextureRenderTarget2D;
class AActor;

/** Editor-only source data repair and checks against preserved exports. */
UCLASS()
class WUWACHARACTERLABEDITOR_API UCharacterInputValidation : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Commandlet-only scene check using native camera/view rendering and production extensions. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool RenderEditorScene(AActor* FocusActor, UCameraComponent* Camera, UTextureRenderTarget2D* Target,
        int32 Frames = 1, float SceneTimeSeconds = 2.0f);

    /** Submit the complete asset batch before waiting on native shader jobs. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static int32 FinishMaterialCompilation(const TArray<UMaterialInterface*>& Materials);

    /** PixelDepthOffset is hidden from the engine's Python MaterialProperty enum. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static UMaterialExpression* GetPixelDepthOffsetInput(UMaterial* Material);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool ConnectPixelDepthOffset(UMaterialExpression* Expression, const FString& Output);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditMaterialResources(UMaterialInterface* Material);

    /** Preserve the original intervals on the native animation notify track. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool ConfigureWeaponNotifies(UAnimSequence* Animation, const FString& SourceJSON, bool bVerifyOnly);

    /** Bind source lifecycle curves to native timed Niagara notifies, preserving other tracks. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool ConfigureWeaponEffectNotifies(UAnimSequenceBase* Animation, const FString& SourceJSON, bool bVerifyOnly);

    /** Compare real native phase components against traces produced by the preserved source runtime. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditWeaponEffectPhases(UAnimSequenceBase* Animation, const FString& SourceTraceJSON);

    /** Observe animation-spawned effects in PIE without advancing or changing them. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditLiveWeaponEffects(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool ConfigureAnimationComposite(UAnimComposite* Animation, const FString& SourceJSON, bool bVerifyOnly);

    /** The native factory's skeleton fields are not exposed to editor Python. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static UFactory* CreateAnimationCompositeFactory(USkeletalMesh* PreviewMesh);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool DeleteAssetPackage(const FString& AssetPath);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditComponentPose(USkeletalMeshComponent* Component);

    /** Mark existing mesh morph names as animation-driven morph curves. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static int32 RestoreMorphCurveMetadata(USkeletalMesh* Mesh);

    /** Restore only colors matching the known legacy importer conversion; -1 rejects unrelated edits. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static int32 RestoreSourceVertexColors(USkeletalMesh* Mesh, const FString& ModelSource);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditImportedInputs(USkeletalMesh* Mesh, UAnimSequence* Animation,
        const FString& ModelSource, const FString& AnimationSource);

    /** Preserve imported static-mesh attributes using native build settings, including commandlets. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool PreserveStaticMeshInputs(UStaticMesh* Mesh);

    /** Repair only the legacy material-path-as-slot-name import; -1 rejects unrelated edits. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static int32 RestoreStaticMeshMaterialSlots(UStaticMesh* Mesh, const FString& ModelSource);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditStaticMeshInputs(UStaticMesh* Mesh, const FString& ModelSource);

    /** Read editor source and platform mips against the preserved decoded DDS. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditTextureMipChain(UTexture* Texture, const FString& DDSSource);
};
