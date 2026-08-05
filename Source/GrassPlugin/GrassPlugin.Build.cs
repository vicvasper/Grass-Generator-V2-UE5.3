// Copyright (c) Victor Rivas Perez. All Rights Reserved.

using UnrealBuildTool;

public class GrassPlugin : ModuleRules
{
	public GrassPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Landscape",
			"PhysicsCore",
			"RenderCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
		});

		// Editor-only. "LandscapeEditor" used to sit in the public list unconditionally, which
		// is an editor module - so this target could not link in a packaged build at all. The
		// generation code it backs is already behind WITH_EDITOR; the dependency now matches.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"UnrealEd",
				"LandscapeEditor",
			});
		}

		// Removed: "InputCore", "AssetRegistry" and "Foliage" (no references),
		// "Blutility", "UMGEditor", "EditorSubsystem", "Slate" and "SlateCore" (this module
		// has no UI). All eight were linked without a single use.
	}
}
