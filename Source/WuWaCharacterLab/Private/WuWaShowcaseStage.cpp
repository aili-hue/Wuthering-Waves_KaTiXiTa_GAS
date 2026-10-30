#include "WuWaShowcaseStage.h"
#include "WuWaRenderingSubsystem.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

AWuWaShowcaseStage::AWuWaShowcaseStage()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot")));
    ReflectionCapture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("ShowcaseReflection"));
    ReflectionCapture->SetupAttachment(GetRootComponent());
    ReflectionCapture->SetAbsolute(true,true,true);
    ReflectionCapture->bCaptureEveryFrame=false;
    ReflectionCapture->bCaptureOnMovement=false;
    ReflectionCapture->bMainViewCamera=true;
    ReflectionCapture->CaptureSource=SCS_SceneColorHDRNoAlpha;
    ReflectionCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    ReflectionCapture->ShowFlags.SetFog(false);
    ReflectionCapture->ShowFlags.SetVolumetricFog(false);
    ReflectionCapture->ShowFlags.SetAtmosphere(false);
    ReflectionCapture->ShowFlags.SetTemporalAA(false);
    ReflectionCapture->ShowFlags.SetMotionBlur(false);
}

void AWuWaShowcaseStage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (auto* Rendering=GetWorld()->GetSubsystem<UWuWaRenderingSubsystem>())
        Rendering->UpdateStageCapture(ReflectionCapture,ReflectionTarget,FPlane(GetActorLocation(),FVector::UpVector),ReflectionBackgroundActors);
}
