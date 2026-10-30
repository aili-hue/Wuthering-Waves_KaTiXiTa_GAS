// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Cartisia : ModuleRules
{
	public Cartisia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput","GameplayTags","GameplayAbilities","GameplayTasks","LevelSequence", "MovieScene","Renderer",     
			// FWorldSceneViewExtension / FMeshPassProcessor / FScreenPassVS /
			// ScenePrivate.h / SceneRendering.h / MeshPassProcessor.inl /
			// ScreenPass.h / MeshMaterialShader.h / GlobalShader
			"RenderCore",     // RenderGraphBuilder.h / RenderGraphResources.h /
			// ShaderParameterStruct.h / ShaderCompilerCore.h
			"RHI",            // FRHIUniformBuffer / FRHIBlendState
			"Niagara",        // UWuWaWeaponEffectNotifyState : UAnimNotifyState_TimedNiagaraEffect
			"KawaiiPhysics",  // FAnimNode_KawaiiPhysics（WuWaPhysicsProfile / AnimInstance）
			"Json"
			
		});

		PrivateDependencyModuleNames.AddRange(new string[] {  });

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
