// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace EpicGames.UHT.Exporters.Json
{
	[UnrealHeaderTool]
	internal class UhtJsonExporter
	{

		[UhtExporter(Name = "Json", Description = "Json description of packages", Options = UhtExporterOptions.None)]
		public static void JsonExporter(IUhtExportFactory Factory)
		{
			new UhtJsonExporter(Factory).Export();
		}

		public readonly IUhtExportFactory Factory;
		public UhtSession Session => Factory.Session;

		private UhtJsonExporter(IUhtExportFactory Factory)
		{
			this.Factory = Factory;
		}

		private void Export()
		{
			// Generate the files for the packages
			List<Task?> GeneratedPackages = new List<Task?>(this.Session.PackageTypeCount);
			foreach (UhtPackage Package in this.Session.Packages)
			{
				UHTManifest.Module Module = Package.Module;
				GeneratedPackages.Add(Factory.CreateTask(
					(IUhtExportFactory Factory) =>
					{
						string JsonPath = Factory.MakePath(Package, ".json");
						JsonSerializerOptions Options = new JsonSerializerOptions { WriteIndented = true, DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull };
						Factory.CommitOutput(JsonPath, JsonSerializer.Serialize(Package, Options));
					}));
			}

			// Wait for all the packages to complete
			List<Task> PackageTasks = new List<Task>(this.Session.PackageTypeCount);
			foreach (Task? Output in GeneratedPackages)
			{
				if (Output != null)
				{
					PackageTasks.Add(Output);
				}
			}
			Task.WaitAll(PackageTasks.ToArray());
		}
	}
}
