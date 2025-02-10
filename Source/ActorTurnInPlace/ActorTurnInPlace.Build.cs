// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using EpicGames.Core;

public class ActorTurnInPlace : ModuleRules
{
	public ActorTurnInPlace(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"GameplayTags",
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"NetCore",
			}
			);
		
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"Slate",	// FNotificationInfo
				}
			);
		}
		
		// Add pre-processor macros for the GameplayAbilities plugin based on enabled state (optional plugin)
		PublicDefinitions.Add("WITH_SIMPLE_ANIMATION=0");
		if (JsonObject.TryRead(Target.ProjectFile, out var rawObject))
		{
			if (rawObject.TryGetObjectArrayField("Plugins", out var pluginObjects))
			{
				foreach (JsonObject pluginObject in pluginObjects)
				{
					pluginObject.TryGetStringField("Name", out var pluginName);
		
					pluginObject.TryGetBoolField("Enabled", out var pluginEnabled);
		
					if (pluginName == "SimpleAnimation" && pluginEnabled)
					{
						PrivateDependencyModuleNames.Add("SimpleAnimation");
						PublicDefinitions.Add("WITH_SIMPLE_ANIMATION=1");
						PublicDefinitions.Remove("WITH_SIMPLE_ANIMATION=0");
					}
				}
			}
		}
	}
}
