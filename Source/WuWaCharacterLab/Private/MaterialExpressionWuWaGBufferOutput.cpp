#include "MaterialExpressionWuWaGBufferOutput.h"

#if WITH_EDITOR
#include "MaterialCompiler.h"

FExpressionInput* UMaterialExpressionWuWaGBufferOutput::GetInput(int32 Index)
{
    switch (Index)
    {
    case 0: return &Emissive;
    case 1: return &EncodedNormal;
    case 2: return &EncodedBase;
    case 3: return &SurfaceColor;
    case 4: return &Motion;
    case 5: return &DeviceDepth;
    case 6: return &ToonData;
    default: return nullptr;
    }
}

FName UMaterialExpressionWuWaGBufferOutput::GetInputName(int32 Index) const
{
    static const FName Names[] = {TEXT("Emissive"), TEXT("EncodedNormal"), TEXT("EncodedBase"),
        TEXT("SurfaceColor"), TEXT("Motion"), TEXT("DeviceDepth"), TEXT("ToonData")};
    return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : NAME_None;
}

int32 UMaterialExpressionWuWaGBufferOutput::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    FExpressionInput* Input = GetInput(OutputIndex);
    if (!Input || !Input->GetTracedInput().Expression)
        return Compiler->Errorf(TEXT("WuWa surface buffer %d is missing"), OutputIndex);
    return Compiler->CustomOutput(this, OutputIndex, Compiler->ForceCast(Input->Compile(Compiler), MCT_Float4));
}

void UMaterialExpressionWuWaGBufferOutput::GetCaption(TArray<FString>& Captions) const
{
    Captions.Add(TEXT("WuWa Surface Buffers"));
}

FExpressionInput* UMaterialExpressionWuWaOutlineStyleOutput::GetInput(int32 Index)
{
    switch (Index)
    {
    case 0: return &WidthStyle;
    case 1: return &AlphaWidth;
    case 2: return &DepthAndAlpha;
    default: return nullptr;
    }
}

FName UMaterialExpressionWuWaOutlineStyleOutput::GetInputName(int32 Index) const
{
    static const FName Names[] = {TEXT("WidthStyle"), TEXT("AlphaWidth"), TEXT("DepthAndAlpha")};
    return Index >= 0 && Index < UE_ARRAY_COUNT(Names) ? Names[Index] : NAME_None;
}

int32 UMaterialExpressionWuWaOutlineStyleOutput::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    FExpressionInput* Input = GetInput(OutputIndex);
    if (!Input || !Input->GetTracedInput().Expression) return Compiler->Errorf(TEXT("WuWa outline style %d is missing"), OutputIndex);
    return Compiler->CustomOutput(this, OutputIndex, Compiler->ForceCast(Input->Compile(Compiler), MCT_Float4));
}

void UMaterialExpressionWuWaOutlineStyleOutput::GetCaption(TArray<FString>& Captions) const
{
    Captions.Add(TEXT("WuWa Outline Style"));
}
#endif
