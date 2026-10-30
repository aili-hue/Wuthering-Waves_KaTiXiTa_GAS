using UnrealBuildTool;
using System.IO;

public class WuWaCharacterLab : ModuleRules
{
    public WuWaCharacterLab(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AnimGraphRuntime", "KawaiiPhysics", "Niagara", "NiagaraAnimNotifies" });
        PrivateDependencyModuleNames.AddRange(new[] { "RenderCore", "RHI", "Renderer", "EnhancedInput", "InputCore", "Json" });
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Private"));
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Runtime/Renderer/Internal"));
    }
}
