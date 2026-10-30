#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

class FWuWaCharacterLabModule final : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        AddShaderSourceDirectoryMapping(TEXT("/WuWa"), FPaths::Combine(FPaths::ProjectDir(), TEXT("Shaders")));
    }
};

// Cartisia owns IMPLEMENT_PRIMARY_GAME_MODULE; this module must not declare a second primary game module.
IMPLEMENT_MODULE(FWuWaCharacterLabModule, WuWaCharacterLab);
