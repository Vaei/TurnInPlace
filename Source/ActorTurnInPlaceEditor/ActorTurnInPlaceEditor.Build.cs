using UnrealBuildTool;

public class ActorTurnInPlaceEditor : ModuleRules
{
    public ActorTurnInPlaceEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "AnimationModifiers",
                "AnimationBlueprintLibrary",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "ActorTurnInPlace",
            }
        );
    }
}