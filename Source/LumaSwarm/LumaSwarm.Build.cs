// Copyright 2026 Simulated Flow. All Rights Reserved.

using UnrealBuildTool;

public class LumaSwarm : ModuleRules
{
	public LumaSwarm(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Runtime only. No UnrealEd, no Slate — the whole plugin has to survive a cooked build.
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
		});
	}
}
