#pragma once

#include "Components/ActorComponent.h"
#include "WuWaRenderingComponent.generated.h"

class USkeletalMeshComponent;
class ADirectionalLight;

UENUM(BlueprintType)
enum class EWuWaFacingAxis : uint8
{
    Automatic UMETA(DisplayName="自动识别"),
    PositiveX UMETA(DisplayName="+X"), PositiveY UMETA(DisplayName="+Y"), PositiveZ UMETA(DisplayName="+Z"),
    NegativeX UMETA(DisplayName="-X"), NegativeY UMETA(DisplayName="-Y"), NegativeZ UMETA(DisplayName="-Z")
};

/** Optional overrides. Compatible skeletal meshes render without this component. */
UCLASS(ClassGroup=(Rendering), meta=(BlueprintSpawnableComponent,DisplayName="鸣潮角色渲染（可选设置）"))
class WUWACHARACTERLAB_API UWuWaRenderingComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UWuWaRenderingComponent() { PrimaryComponentTick.bCanEverTick=false; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(DisplayName="启用角色渲染"))
    bool bEnableRendering=true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rendering", meta=(DisplayName="目标网格（留空自动）"))
    TObjectPtr<USkeletalMeshComponent> TargetMesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face", meta=(DisplayName="头部骨骼（留空自动）"))
    FName HeadBone;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face", meta=(DisplayName="面部朝向"))
    EWuWaFacingAxis ForwardAxis=EWuWaFacingAxis::Automatic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Face", meta=(DisplayName="头顶方向"))
    EWuWaFacingAxis UpAxis=EWuWaFacingAxis::Automatic;

    /** Scene lighting is shared, like UE's primary directional light. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lighting", meta=(DisplayName="场景主方向光覆盖（留空自动）",ToolTip="覆盖当前场景的角色渲染主光，场景内共享；通常无需填写。"))
    TObjectPtr<ADirectionalLight> KeyLight;
};
