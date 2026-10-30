#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "MaterialGraphAuthoring.generated.h"

class UMaterial;
class UMaterialExpression;
class UMaterialInstanceConstant;
class UTexture;
class UMaterialExpressionComment;
class UMaterialExpressionNamedRerouteDeclaration;
class UMaterialExpressionNamedRerouteUsage;

/** Editor graph operations missing from MaterialEditingLibrary's Python API. */
UCLASS()
class WUWACHARACTERLABEDITOR_API UMaterialGraphAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static FString AuditPersistentProperties(UObject* Object);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static TArray<FString> GetInputConnections(UMaterialExpression* Expression);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static UMaterialExpression* TraceInputSource(UMaterialExpression* Expression);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static bool SetOriginalParameterValues(UMaterialInstanceConstant* Instance,
        const TMap<FName, float>& Scalars, const TMap<FName, FLinearColor>& Vectors,
        const TMap<FName, UTexture*>& Textures);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static FString InspectInstanceParameterEditor(UMaterialInstanceConstant* Instance, const FString& Filename);

    /** Transient copies for reading the saved parameter graph without editing assets. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static UMaterial* CopyMaterialForParameterProbe(UMaterial* Material);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static UMaterialInstanceConstant* CopyInstanceForParameterProbe(UMaterialInstanceConstant* Instance, UMaterial* ProbeMaterial);

    /** Draw the real native material graph; use in -RenderOffScreen editor jobs. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static bool ExportGraphRegion(UMaterial* Material, const FString& Filename, float X, float Y,
        float Zoom = 0.5f, int32 Width = 1800, int32 Height = 1000);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static bool LinkNamedReroute(UMaterialExpressionNamedRerouteUsage* Usage,
        UMaterialExpressionNamedRerouteDeclaration* Declaration);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static UMaterialExpressionComment* CreateModuleComment(UMaterial* Material, const FString& Title,
        int32 X, int32 Y, int32 Width, int32 Height, FLinearColor Color);

    UFUNCTION(BlueprintCallable, Category="WuWa|Materials")
    static bool PositionOutput(UMaterial* Material, int32 X, int32 Y);
};
