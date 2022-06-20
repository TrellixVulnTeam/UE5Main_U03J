// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System.Collections.Generic;
using System.IO;
using UnrealBuildBase;

namespace UnrealBuildTool
{
	/// <summary>
	/// TargetRules extension for low level tests.
	/// </summary>
	public class TestTargetRules : TargetRules
	{
		/// <summary>
		/// Keeps track if low level tests executable must build with the Editor.
		/// </summary>
		public static bool bTestsRequireEditor = false;

		/// <summary>
		/// Keeps track if low level tests executable must build with the Engine.
		/// </summary>
		public static bool bTestsRequireEngine = false;

		/// <summary>
		/// Keeps track if low level tests executable must build with the ApplicationCore.
		/// </summary>
		public static bool bTestsRequireApplicationCore = false;

		/// <summary>
		/// Keeps track if low level tests executable must build with the CoreUObject.
		/// </summary>
		public static bool bTestsRequireCoreUObject = false;

		/// <summary>
		/// Keep track of low level test runner module instance.
		/// </summary>
		public static ModuleRules? LowLevelTestsRunnerModule;

		/// <summary>
		/// Associated tested target of this test target.
		/// </summary>
		public TargetRules TestedTarget { get; private set; }

		/// <summary>
		/// TestTargetRules is setup as a program and is linked monolithically .
		/// It removes a lot of default compilation behavior in order to produce a minimal test environment.
		/// </summary>
		public TestTargetRules(TargetRules TestedTarget, TargetInfo Target) : base(Target)
		{
			if (TestedTarget is TestTargetRules)
			{
				throw new BuildException("TestedTarget can't be of type TestTargetRules.");
			}

			bIsTestTargetOverride = true;

			this.TestedTarget = TestedTarget;

			DefaultBuildSettings = BuildSettingsVersion.V2;

			ExeBinariesSubFolder = Name = TestedTarget.Name + "Tests";
			TargetSourceFile = File = TestedTarget.File;
			LaunchModuleName = TestedTarget.LaunchModuleName + "Tests";

			ManifestFileNames = TestedTarget.ManifestFileNames;

			WindowsPlatform = TestedTarget.WindowsPlatform;

			Type = TargetType.Program;
			LinkType = TargetLinkType.Monolithic;

			bBuildInSolutionByDefault = false;

			bDeployAfterCompile = Target.Platform != UnrealTargetPlatform.Android;
			bIsBuildingConsoleApplication = true;

			// Disabling default true flags that aren't necessary for tests

			// Lean and Mean mode
			bBuildDeveloperTools = false;

			// No localization
			bCompileICU = false;

			// No need for shaders by default
			bForceBuildShaderFormats = false;

			// Do not link against the engine, no Chromium Embedded Framework etc.
			bCompileAgainstEngine = false;
			bCompileCEF3 = false;
			bCompileAgainstCoreUObject = false;
			bCompileAgainstApplicationCore = false;
			bUseLoggingInShipping = true;

			// Allow exception handling
			bForceEnableExceptions = true;

			bool bDebugOrDevelopment = Target.Configuration == UnrealTargetConfiguration.Debug || Target.Configuration == UnrealTargetConfiguration.Development;
			bBuildWithEditorOnlyData = Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop) && bDebugOrDevelopment;

			// Disable malloc profiling in tests
			bUseMallocProfiler = false;

			// Useful for debugging test failures
			if (Target.Configuration == UnrealTargetConfiguration.Debug)
			{
				bDebugBuildsActuallyUseDebugCRT = true;
			}

			GlobalDefinitions.Add("STATS=0");
			GlobalDefinitions.Add("TEST_FOR_VALID_FILE_SYSTEM_MEMORY=0");

			// Platform specific setup
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				UndecoratedConfiguration = Target.Configuration;

				string VersionScriptFile = Path.Combine(Directory.GetCurrentDirectory(), @"Developer\LowLevelTestsRunner\Private\Platform\Android\HideSymbols.ldscript");
				AdditionalLinkerArguments = " -Wl,--version-script=" + VersionScriptFile;

				GlobalDefinitions.Add("USE_ANDROID_INPUT=0");
				GlobalDefinitions.Add("USE_ANDROID_OPENGL=0");
				GlobalDefinitions.Add("USE_ANDROID_LAUNCH=0");
				GlobalDefinitions.Add("USE_ANDROID_JNI=0");
			}
			else if (Target.Platform == UnrealTargetPlatform.IOS)
			{
				GlobalDefinitions.Add("HAS_METAL=0");
			}
		}
	}
}
