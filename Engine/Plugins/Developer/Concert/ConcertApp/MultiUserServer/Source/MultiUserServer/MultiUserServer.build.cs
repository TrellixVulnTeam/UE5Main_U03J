// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealBuildTool.Rules
{
	public class MultiUserServer : ModuleRules
	{
		public MultiUserServer(ReadOnlyTargetRules Target) : base(Target)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"ApplicationCore",
					"Core",
					"CoreUObject",
					"Concert",
					"ConcertSharedSlate",
					"ConcertSyncCore",
					"ConcertTransport",
					"EditorStyle",			// Needed so OutputLog module to work correctly
					"InputCore",
					"OutputLog",
					"Projects",
					"Slate",
					"SlateCore",
					"StandaloneRenderer",	
					"ToolWidgets",
					"WorkspaceMenuStructure" // Needed so OutputLog module to work correctly
				}
			);
			
			PrivateIncludePathModuleNames.AddRange(
				new string[] {
					"ConcertSyncServer",
				}
			);
			
			DynamicallyLoadedModuleNames.AddRange(
				new string[]
				{
					"ConcertSyncServer"
				});
		}
	}
}
