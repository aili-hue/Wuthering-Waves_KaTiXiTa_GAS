#pragma once

#include "Engine/AssetUserData.h"
#include "WuWaCharacterProfile.h"
#include "WuWaMaterialSettings.generated.h"

/** Cooked material semantics, inherited by material instances without actor setup. */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, meta=(DisplayName="鸣潮材质渲染设置"))
class WUWACHARACTERLAB_API UWuWaMaterialSettings : public UAssetUserData
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering", meta=(DisplayName="材质部位"))
    EWuWaSurfacePart Part=EWuWaSurfacePart::Up;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering", meta=(DisplayName="启用描边"))
    bool bOutline=true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering", meta=(DisplayName="脸／眼遮罩材质"))
    TObjectPtr<UMaterialInterface> StencilMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rendering", meta=(DisplayName="描边材质覆盖"))
    TObjectPtr<UMaterialInterface> OutlineMaterial;

    static const UWuWaMaterialSettings* Find(UMaterialInterface* Material);
};
