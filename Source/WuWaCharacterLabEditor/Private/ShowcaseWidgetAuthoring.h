#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "ShowcaseWidgetAuthoring.generated.h"

class UWidget;
class UWidgetBlueprint;
class UFont;
class UFontFace;

/** Editor scripting access to the protected native widget template tree. */
UCLASS()
class UShowcaseWidgetAuthoring : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static UWidget* GetRoot(UWidgetBlueprint* Blueprint);

    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static UWidget* MakeWidget(UWidgetBlueprint* Blueprint, TSubclassOf<UWidget> Type, FName Name, bool bVariable = false);

    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static UClass* GetParent(UWidgetBlueprint* Blueprint);

    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static bool ConfigureFont(UFont* Font, UFontFace* Chinese, UFontFace* Latin);

    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static bool BindSelectionFrames(UWidgetBlueprint* Blueprint);

    /** Saves the viewport tree through native offscreen widget rendering for editor-only visual checks. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static bool CaptureViewportUI(UObject* WorldContextObject, const FString& Filename);

    /** Renders a saved template with native Slate; does not construct a gameplay widget or run PIE. */
    UFUNCTION(BlueprintCallable, Category="WuWa|Authoring")
    static bool CaptureWidgetPreview(UWidgetBlueprint* Blueprint, const FString& Filename, FVector2D DrawSize);
};
