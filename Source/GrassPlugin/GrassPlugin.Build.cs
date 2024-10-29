// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GrassPlugin : ModuleRules
{
    public GrassPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // M�dulos p�blicos requeridos
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "AssetRegistry",
            "Landscape",
            "RenderCore", // M�dulo correcto para virtual texturas
            "Foliage",
            "LandscapeEditor",
            "PhysicsCore"
        });

        // M�dulos privados
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // M�dulos adicionales para el editor
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "UnrealEd",
                "Blutility",
                "UMGEditor",
                "Slate",
                "SlateCore",
                "EditorSubsystem"
            });
        }
    }
}
