#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WuWaShowcaseGameMode.generated.h"

/** The saved camera is the view target; no movement pawn is required. */
UCLASS()
class WUWACHARACTERLAB_API AWuWaShowcaseGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AWuWaShowcaseGameMode();
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
};
