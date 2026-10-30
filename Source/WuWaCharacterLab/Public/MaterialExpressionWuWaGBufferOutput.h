#pragma once

#include "CoreMinimal.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "MaterialExpressionWuWaGBufferOutput.generated.h"

/** Source surface channels evaluated by UE's native material/vertex factories. */
UCLASS()
class WUWACHARACTERLAB_API UMaterialExpressionWuWaGBufferOutput : public UMaterialExpressionCustomOutput
{
    GENERATED_BODY()

public:
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput Emissive;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput EncodedNormal;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput EncodedBase;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput SurfaceColor;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput Motion;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput DeviceDepth;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput ToonData;

    virtual int32 GetNumOutputs() const override { return 7; }
    virtual FString GetFunctionName() const override { return TEXT("WuWaGBuffer"); }
    virtual FString GetDisplayName() const override { return TEXT("WuWa Surface Buffers"); }

#if WITH_EDITOR
    virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual FExpressionInput* GetInput(int32 Index) override;
    virtual FName GetInputName(int32 Index) const override;
    virtual EMaterialValueType GetInputValueType(int32 Index) override { return MCT_Float4; }
    virtual void GetCaption(TArray<FString>& Captions) const override;
#endif
};

/** Original per-material extrusion controls consumed by the native skin VS. */
UCLASS()
class WUWACHARACTERLAB_API UMaterialExpressionWuWaOutlineStyleOutput : public UMaterialExpressionCustomOutput
{
    GENERATED_BODY()
public:
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput WidthStyle;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput AlphaWidth;
    UPROPERTY(meta=(RequiredInput="true")) FExpressionInput DepthAndAlpha;

    virtual int32 GetNumOutputs() const override { return 3; }
    virtual FString GetFunctionName() const override { return TEXT("WuWaOutlineStyle"); }
    virtual FString GetDisplayName() const override { return TEXT("WuWa Outline Style"); }
#if WITH_EDITOR
    virtual EShaderFrequency GetShaderFrequency(uint32 OutputIndex) override { return SF_Vertex; }
    virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual FExpressionInput* GetInput(int32 Index) override;
    virtual FName GetInputName(int32 Index) const override;
    virtual EMaterialValueType GetInputValueType(int32 Index) override { return MCT_Float4; }
    virtual void GetCaption(TArray<FString>& Captions) const override;
#endif
};
