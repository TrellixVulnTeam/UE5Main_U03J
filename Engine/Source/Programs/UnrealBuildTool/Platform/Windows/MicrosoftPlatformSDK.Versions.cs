// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System.Linq;

namespace UnrealBuildTool
{
	partial class MicrosoftPlatformSDK : UEBuildPlatformSDK
	{
		/// <summary>
		/// The default Windows SDK version to be used, if installed.
		/// </summary>
		static readonly VersionNumber[] PreferredWindowsSdkVersions = new VersionNumber[]
		{
			VersionNumber.Parse("10.0.18362.0")
		};

		/// <summary>
		/// The default compiler version to be used, if installed. 
		/// </summary>
		static readonly VersionNumberRange[] PreferredClangVersions =
		{
			VersionNumberRange.Parse("13.0.0", "13.999"),
			VersionNumberRange.Parse("12.0.0", "12.999"),
			VersionNumberRange.Parse("11.0.0", "11.999"),
			VersionNumberRange.Parse("10.0.0", "10.999")
		};

		static readonly VersionNumber MinimumClangVersion = new VersionNumber(10, 0, 0);

		/// <summary>
		/// Ranges of tested compiler toolchains to be used, in order of preference. If multiple toolchains in a range are present, the latest version will be preferred.
		/// Note that the numbers here correspond to the installation *folders* rather than precise executable versions. 
		/// </summary>
		static readonly VersionNumberRange[] PreferredVisualCppVersions = new VersionNumberRange[]
		{
			VersionNumberRange.Parse("14.29.30133", "14.29.30136"), // VS2019 16.11.5
		};

		/// <summary>
		/// Tested compiler toolchanges that should not be allowed.
		/// </summary>
		static readonly VersionNumberRange[] BannedVisualCppVersions = new VersionNumberRange[]
		{
		};

		static readonly VersionNumber MinimumVisualCppVersion = new VersionNumber(14, 29, 30133);

		/// <summary>
		/// The default compiler version to be used, if installed. 
		/// </summary>
		static readonly VersionNumberRange[] PreferredIntelOneApiVersions =
		{
			VersionNumberRange.Parse("2022.0.0", "2022.9999"),
		};

		static readonly VersionNumber MinimumIntelOneApiVersion = new VersionNumber(2021, 0, 0);


		public override string GetMainVersion()
		{
			// preferred/main version is the top of the Preferred list - 
			return PreferredWindowsSdkVersions.First().ToString();
		}

		protected override void GetValidVersionRange(out string MinVersion, out string MaxVersion)
		{
			MinVersion = "10.0.00000.0";
			MaxVersion = "10.9.99999.0";
		}

		protected override void GetValidSoftwareVersionRange(out string? MinVersion, out string? MaxVersion)
		{
			// minimum version is the oldest version in the Preferred list -
			MinVersion = PreferredWindowsSdkVersions.Min()?.ToString();
			MaxVersion = null;
		}
	}
}
