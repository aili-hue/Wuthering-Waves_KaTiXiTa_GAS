#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class IMaterialEditor;
struct FWuWaMaterialParameterEditorState;

/** Keep runtime uniforms and compiled layout controls out of the artist interface. */
class FWuWaMaterialParameterEditor
{
public:
    void Startup();
    void Shutdown();
private:
    void OnOpened(TWeakPtr<IMaterialEditor> Editor);
    FDelegateHandle OpenedHandle;
    TArray<FTSTicker::FDelegateHandle> Pending;
    TArray<TSharedPtr<FWuWaMaterialParameterEditorState>> States;
};
