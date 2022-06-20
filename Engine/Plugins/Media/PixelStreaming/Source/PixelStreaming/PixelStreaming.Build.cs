// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using EpicGames.Core;


namespace UnrealBuildTool.Rules
{
	public class PixelStreaming : ModuleRules
	{
		const string PixelStreamingProgramsDirectory = "../../Samples/PixelStreaming";

		private void AddFolder(string Folder)
		{
			string DirectoryToAdd = new DirectoryInfo(PixelStreamingProgramsDirectory + "/WebServers/" + Folder).FullName;

			if (!Directory.Exists(DirectoryToAdd))
			{
				return;
			}

			List<string> DependenciesToAdd = new List<string>();
			DependenciesToAdd.AddRange(Directory.GetFiles(DirectoryToAdd, "*.*", SearchOption.AllDirectories));

			foreach(var DependencySource in DependenciesToAdd)
			{
				if ( ! (DependencySource.Contains("node") | DependencySource.Contains("logs")))
				{
					RuntimeDependencies.Add(DependencySource, StagedFileType.NonUFS);
				}
			}
		}

		public PixelStreaming(ReadOnlyTargetRules Target) : base(Target)
		{
			// use private PCH to include lots of WebRTC headers
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
			PrivatePCHHeaderFile = "Private/PCH.h";

			var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);

			// This is so for game projects using our public headers don't have to include extra modules they might not know about.
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"InputDevice",
				"WebRTC"
			});

			// NOTE: General rule is not to access the private folder of another module
			PrivateIncludePaths.AddRange(new string[]
			{
				Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"),
			});

			// WebRTC third party includes (just libyuv for colour format conversions for now)
			PublicIncludePaths.AddRange(new string[]
				{
					Path.Combine(EngineDir, "Source/ThirdParty/WebRTC/4147/Include/third_party/libyuv/include"),
				});

			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"EngineSettings",
				"InputCore",
				"Json",
				"Renderer",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"AudioMixer",
				"WebRTC",
				"WebSockets",
				"Sockets",
				"MediaUtils",
				"DeveloperSettings",
				"AVEncoder",
				"PixelStreamingShaders"
			});

			PrivateDependencyModuleNames.Add("VulkanRHI");
			AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan", "CUDA");

			if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
			{
				PrivateDependencyModuleNames.Add("D3D11RHI");
				PrivateDependencyModuleNames.Add("D3D12RHI");

				AddEngineThirdPartyPrivateStaticDependencies(Target, "DX11", "DX12");
			}

			// When we build a Game target we also package the servers with it as runtime dependencies
			if(Target.Type == TargetType.Game)
			{
				AddFolder("SignallingWebServer");
				AddFolder("Matchmaker");
				AddFolder("SFU");
			}
		}
	}
}
