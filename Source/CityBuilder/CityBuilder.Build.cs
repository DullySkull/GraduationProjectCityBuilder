// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CityBuilder : ModuleRules
{
	public CityBuilder(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"PCG"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"CityBuilder",
			"CityBuilder/Variant_Platforming",
			"CityBuilder/Variant_Platforming/Animation",
			"CityBuilder/Variant_Combat",
			"CityBuilder/Variant_Combat/AI",
			"CityBuilder/Variant_Combat/Animation",
			"CityBuilder/Variant_Combat/Gameplay",
			"CityBuilder/Variant_Combat/Interfaces",
			"CityBuilder/Variant_Combat/UI",
			"CityBuilder/Variant_SideScrolling",
			"CityBuilder/Variant_SideScrolling/AI",
			"CityBuilder/Variant_SideScrolling/Gameplay",
			"CityBuilder/Variant_SideScrolling/Interfaces",
			"CityBuilder/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
