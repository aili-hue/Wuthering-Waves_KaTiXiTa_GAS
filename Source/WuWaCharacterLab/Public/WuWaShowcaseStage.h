#pragma once

#include "GameFramework/Actor.h"
#include "WuWaShowcaseStage.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class ADirectionalLight;

/** Optional showcase mirror. Character rendering does not depend on this actor. */
UCLASS(meta=(DisplayName="角色展示舞台反射"))
class WUWACHARACTERLAB_API AWuWaShowcaseStage : public AActor
{
    GENERATED_BODY()
public:
    AWuWaShowcaseStage();
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override { return true; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Reflection")
    TObjectPtr<USceneCaptureComponent2D> ReflectionCapture;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reflection", meta=(DisplayName="地面反射纹理"))
    TObjectPtr<UTextureRenderTarget2D> ReflectionTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Reflection", meta=(DisplayName="反射背景"))
    TArray<TObjectPtr<AActor>> ReflectionBackgroundActors;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lighting", meta=(DisplayName="展示主光（留空自动）"))
    TObjectPtr<ADirectionalLight> KeyLight;
};
