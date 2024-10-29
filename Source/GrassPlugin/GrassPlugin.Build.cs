// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class GrassPlugin : ModuleRules
{
    public GrassPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        // Módulos públicos requeridos
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "AssetRegistry",
            "Landscape",
            "RenderCore", // Módulo correcto para virtual texturas
            "Foliage",
            "LandscapeEditor",
            "PhysicsCore"
        });

        // Módulos privados
        PrivateDependencyModuleNames.AddRange(new string[] { });

        // Módulos adicionales para el editor
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
