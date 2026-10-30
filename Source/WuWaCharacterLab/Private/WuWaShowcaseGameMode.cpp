#include "WuWaShowcaseGameMode.h"
#include "WuWaShowcasePlayerController.h"

AWuWaShowcaseGameMode::AWuWaShowcaseGameMode()
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = AWuWaShowcasePlayerController::StaticClass();
}

void AWuWaShowcaseGameMode::HandleStartingNewPlayer_Implementation(APlayerController*)
{
    // PlayerController::BeginPlay installs the saved presentation references.
}
