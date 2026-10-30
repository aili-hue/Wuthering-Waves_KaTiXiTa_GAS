#pragma once

#include "CoreMinimal.h"
#include "ScreenPass.h"

class FViewInfo;
struct IPooledRenderTarget;
struct FPostProcessMaterialInputs;
struct FWuWaSurfaceBuffers;

/** Replace native display conversion with the recovered tone curve and LUT. */
FScreenPassTexture AddWuWaToneMappingPass(FRDGBuilder& Graph, const FViewInfo& View,
    const FPostProcessMaterialInputs& Inputs, const FWuWaSurfaceBuffers* Source,
    TRefCountPtr<IPooledRenderTarget>& ColorGradingLUT, uint32& BloomPassCount);
