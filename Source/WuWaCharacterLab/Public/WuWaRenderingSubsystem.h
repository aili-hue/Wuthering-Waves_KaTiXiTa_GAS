#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "WuWaRenderingSubsystem.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class FWuWaRenderingState;

/** One renderer per world; discovers compatible materials on native skeletal components. */
UCLASS()
class WUWACHARACTERLAB_API UWuWaRenderingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    UWuWaRenderingSubsystem();
    virtual ~UWuWaRenderingSubsystem() override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableInEditor() const override { return true; }
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

    UFUNCTION(BlueprintPure, Category="Rendering", meta=(WorldContext="WorldContext"))
    static UWuWaRenderingSubsystem* GetForWorld(const UObject* WorldContext);

    UFUNCTION(BlueprintPure, Category="Rendering")
    FString GetRenderingDiagnostics() const;

    /** Restore authored values while native saving or PIE duplication runs. Nested scopes supported. */
    void SuspendAuthoring();
    void ResumeAuthoring();

    /** Showcase reflection is optional and independent of a character actor. */
    void UpdateStageCapture(USceneCaptureComponent2D* Capture, UTextureRenderTarget2D* Target,
        const FPlane& Plane, const TArray<TObjectPtr<AActor>>& Backgrounds);
private:
    TSharedPtr<FWuWaRenderingState> State;
    FDelegateHandle RegisteredActorsHandle;
};
