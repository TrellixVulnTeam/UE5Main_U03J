// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class GeometryCollectionEngine : ModuleRules
	{
        public GeometryCollectionEngine(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateIncludePaths.Add("Runtime/Experimental/GeometryCollectionEngine/Private");
            PublicIncludePaths.Add(ModuleDirectory + "/Public");

			SetupModulePhysicsSupport(Target);

			PublicDependencyModuleNames.AddRange(
				new string[]
				{
					"Core",
					"CoreUObject",
                    "Engine",
                    "RenderCore",
                    "RHI",
					"Renderer",
                    "FieldSystemEngine",
	                "ChaosSolverEngine",
					"NetCore",
                    "IntelISPC"
                }
                );

			PrivateIncludePathModuleNames.Add("DerivedDataCache");

			if (Target.bBuildEditor)
            {
				DynamicallyLoadedModuleNames.Add("NaniteBuilder");
				PrivateIncludePathModuleNames.Add("NaniteBuilder");

				PublicDependencyModuleNames.Add("EditorFramework");
                PublicDependencyModuleNames.Add("UnrealEd");
			}

			PrivateDefinitions.Add("CHAOS_INCLUDE_LEVEL_1=1");
		}
	}
}
