using UnrealBuildTool;

public class WuWaCharacterLabEditor : ModuleRules
{
    public WuWaCharacterLabEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.Add("GraphEditor");
        PrivateDependencyModuleNames.Add("InputCore");
        PrivateDependencyModuleNames.Add("PropertyEditor");
        PrivateDependencyModuleNames.AddRange(new[] { "WuWaCharacterLab", "AnimGraph", "AssetTools", "KawaiiPhysicsEd", "KawaiiPhysics", "JsonUtilities", "Json", "UEFormat", "MeshDescription", "StaticMeshDescription", "SkeletalMeshDescription", "UnrealEd", "MainFrame", "LevelEditor", "Slate", "SlateCore", "SlateRHIRenderer", "ApplicationCore", "UMG", "UMGEditor", "BlueprintGraph", "KismetCompiler", "RHI", "RenderCore", "Renderer", "MaterialEditor", "Niagara", "NiagaraEditor" });
    }
}
