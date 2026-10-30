#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShowcaseViewportValidation.generated.h"

/** Transient viewport controls for native Editor/PIE performance capture. */
UCLASS()
class UShowcaseViewportValidation : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static class UWuWaRenderingComponent* AddRenderingOverrides(AActor* Actor);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool RemoveRenderingValidationMap();

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString AuditPresentationViewport();

    /** Measure the same synchronous picking path used by editor cursor queries. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static double MeasurePresentationCursorQuery(bool bNativeQuery);

    /** Native off-screen viewport events; does not generate OS input. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString ExercisePresentationNavigation(bool bDragging, float DeltaX, float DeltaY);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString ExercisePresentationClick(AActor* ExpectedActor=nullptr);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool SetPresentationRealtime(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool CapturePresentationViewport(const FString& Filename);

    /** Start in-process PIE in a native window on the editor's display. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool RequestPresentationPlay(int32 Width, int32 Height);

    /** Fit the floating PIE window using the measured viewport and native window sizes. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static bool FitPresentationViewport(int32 Width, int32 Height);

    /** Zero dimensions keep the current viewport size. No window is resized. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString ConfigureViewport(int32 Width, int32 Height);

    UFUNCTION(BlueprintCallable, Category="WuWa|Validation")
    static FString RestoreViewport();
};
