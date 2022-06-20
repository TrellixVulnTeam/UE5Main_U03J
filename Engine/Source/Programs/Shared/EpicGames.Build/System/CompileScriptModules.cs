// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using Microsoft.Build.Shared;
using EpicGames.MsBuild;

namespace UnrealBuildBase
{
	public static class CompileScriptModule
	{
		class Hook : CsProjBuildHook
		{
			private Dictionary<string, DateTime> WriteTimes = new Dictionary<string, DateTime>();

			public DateTime GetLastWriteTime(DirectoryReference BasePath, string RelativeFilePath)
			{
				return GetLastWriteTime(BasePath.FullName, RelativeFilePath);
			}

			public DateTime GetLastWriteTime(string BasePath, string RelativeFilePath)
			{
				string NormalizedPath = Path.GetFullPath(RelativeFilePath, BasePath);
				if (!WriteTimes.TryGetValue(NormalizedPath, out DateTime WriteTime))
				{
					WriteTimes.Add(NormalizedPath, WriteTime = File.GetLastWriteTime(NormalizedPath));
				}
				return WriteTime;
			}

			public void ValidateRecursively(
				Dictionary<FileReference, (CsProjBuildRecord, FileReference)> ValidBuildRecords,
				Dictionary<FileReference, (CsProjBuildRecord, FileReference)> InvalidBuildRecords,
				Dictionary<FileReference, (CsProjBuildRecord, FileReference)> BuildRecords,
				FileReference ProjectPath)
			{
				CompileScriptModule.ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, ProjectPath, this);
			}

			public bool HasWildcards(string FileSpec)
			{
				return FileMatcher.HasWildcards(FileSpec);
			}

			DirectoryReference CsProjBuildHook.EngineDirectory => Unreal.EngineDirectory;

			DirectoryReference CsProjBuildHook.DotnetDirectory => Unreal.DotnetDirectory;

			FileReference CsProjBuildHook.DotnetPath => Unreal.DotnetPath;
		}

		/// <summary>
		/// Return the target paths from the collection of build records
		/// </summary>
		/// <param name="BuildRecords">Input build records</param>
		/// <returns>Set of target files</returns>
		public static HashSet<FileReference> GetTargetPaths(IReadOnlyDictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> BuildRecords)
		{
			return new HashSet<FileReference>(BuildRecords.Select(x => GetTargetPath(x)));
		}

		/// <summary>
		/// Return the target path for the given build record
		/// </summary>
		/// <param name="BuildRecord">Build record</param>
		/// <returns>File reference for the target</returns>
		public static FileReference GetTargetPath(KeyValuePair<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> BuildRecord)
		{
			return FileReference.Combine(BuildRecord.Key.Directory, BuildRecord.Value.BuildRecord.TargetPath!);
		}

		/// <summary>
		/// Locates script modules, builds them if necessary, returns set of .dll files
		/// </summary>
		/// <param name="RulesFileType"></param>
		/// <param name="ScriptsForProjectFileName"></param>
		/// <param name="AdditionalScriptsFolders"></param>
		/// <param name="bForceCompile"></param>
		/// <param name="bNoCompile"></param>
		/// <param name="bUseBuildRecords"></param>
		/// <param name="bBuildSuccess"></param>
		/// <param name="OnBuildingProjects">Action to invoke when projects get built</param>
		/// <returns>Collection of all the projects.  They will have been compiled.</returns>
		public static HashSet<FileReference> InitializeScriptModules(Rules.RulesFileType RulesFileType, 
			string? ScriptsForProjectFileName, List<string>? AdditionalScriptsFolders, bool bForceCompile, bool bNoCompile, bool bUseBuildRecords, 
			out bool bBuildSuccess, Action<int> OnBuildingProjects)
		{
			List<DirectoryReference> GameDirectories = GetGameDirectories(ScriptsForProjectFileName);
			List<DirectoryReference> AdditionalDirectories = GetAdditionalDirectories(AdditionalScriptsFolders);
			List<DirectoryReference> GameBuildDirectories = GetAdditionalBuildDirectories(GameDirectories);

			// List of directories used to locate Intermediate/ScriptModules dirs for writing build records
			List<DirectoryReference> BaseDirectories = new List<DirectoryReference>(1 + GameDirectories.Count + AdditionalDirectories.Count);
			BaseDirectories.Add(Unreal.EngineDirectory);
			BaseDirectories.AddRange(GameDirectories);
			BaseDirectories.AddRange(AdditionalDirectories);

			HashSet<FileReference> FoundProjects = new HashSet<FileReference>(
				Rules.FindAllRulesSourceFiles(RulesFileType,
				// Project scripts require source engine builds
				GameFolders: Unreal.IsEngineInstalled() ? GameDirectories : new List<DirectoryReference>(), 
				ForeignPlugins: null, AdditionalSearchPaths: AdditionalDirectories.Concat(GameBuildDirectories).ToList()));

			return GetTargetPaths(Build(RulesFileType, FoundProjects, BaseDirectories, bForceCompile, bNoCompile, bUseBuildRecords, out bBuildSuccess, OnBuildingProjects));
		}

		/// <summary>
		/// Locates script modules, builds them if necessary, returns set of .dll files
		/// </summary>
		/// <param name="RulesFileType"></param>
		/// <param name="FoundProjects">Projects to be compiled</param>
		/// <param name="BaseDirectories">Base directories for all the projects</param>
		/// <param name="bForceCompile"></param>
		/// <param name="bNoCompile"></param>
		/// <param name="bUseBuildRecords"></param>
		/// <param name="bBuildSuccess"></param>
		/// <param name="OnBuildingProjects">Action to invoke when projects get built</param>
		/// <returns>Collection of all the projects.  They will have been compiled.</returns>
		public static Dictionary<FileReference, (CsProjBuildRecord, FileReference)> Build(Rules.RulesFileType RulesFileType,
			HashSet<FileReference> FoundProjects, List<DirectoryReference> BaseDirectories, bool bForceCompile, bool bNoCompile, bool bUseBuildRecords,
			out bool bBuildSuccess, Action<int> OnBuildingProjects)
		{
			CsProjBuildHook Hook = new Hook();

			bool bUseBuildRecordsOnlyForProjectDiscovery = bNoCompile || Unreal.IsEngineInstalled();

			// Load existing build records, validating them only if (re)compiling script projects is an option
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> ExistingBuildRecords = LoadExistingBuildRecords(BaseDirectories);

			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> ValidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(ExistingBuildRecords.Count);
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> InvalidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(ExistingBuildRecords.Count);

			if (bUseBuildRecords)
			{
				foreach (FileReference Project in FoundProjects)
				{
					ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, ExistingBuildRecords, Project, Hook);
				}
			}

			if (bUseBuildRecordsOnlyForProjectDiscovery)
			{
				string FilterExtension = String.Empty;
				switch (RulesFileType)
				{
					case Rules.RulesFileType.AutomationModule:
						FilterExtension = ".Automation.json";
						break;
					case Rules.RulesFileType.UbtPlugin:
						FilterExtension = ".ubtplugin.json";
						break;
					default:
						throw new Exception("Unsupported rules file type");
				}
				Dictionary<FileReference, (CsProjBuildRecord, FileReference)> OutRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(ExistingBuildRecords.Count);
				foreach (KeyValuePair<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> Record in
					ExistingBuildRecords.Where(x => x.Value.BuildRecordPath.HasExtension(FilterExtension)))
				{
					FileReference TargetPath = FileReference.Combine(Record.Key.Directory, Record.Value.BuildRecord.TargetPath!);

					if (FileReference.Exists(TargetPath))
					{
						OutRecords.Add(Record.Key, Record.Value);
					}
					else
					{
						if (bNoCompile)
						{
							// when -NoCompile is on the command line, try to run with whatever is available
							Log.TraceWarning($"Script module \"{TargetPath}\" not found for record \"{Record.Value.BuildRecordPath}\"");
						}
						else
						{
							// when the engine is installed, expect to find a built target assembly for every record that was found
							throw new Exception($"Script module \"{TargetPath}\" not found for record \"{Record.Value.BuildRecordPath}\"");
						}

					}
				}
				bBuildSuccess = true;
				return OutRecords;
			}
			else
			{
				// when the engine is not installed, delete any build record .json file that is not valid
				foreach ((CsProjBuildRecord _, FileReference BuildRecordPath) in InvalidBuildRecords.Values)
				{
					if (BuildRecordPath != null)
					{
						Log.TraceLog($"Deleting invalid build record \"{BuildRecordPath}\"");
						FileReference.Delete(BuildRecordPath);
					}
				}
			}

			if (!bForceCompile && bUseBuildRecords)
			{
				// If all found records are valid, we can return their targets directly
				if (FoundProjects.All(x => ValidBuildRecords.ContainsKey(x)))
				{
					bBuildSuccess = true;
					return new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(ValidBuildRecords.Where(x => FoundProjects.Contains(x.Key)));
				}
			}

			// Fall back to the slower approach: use msbuild to load csproj files & build as necessary
			return Build(FoundProjects, bForceCompile || !bUseBuildRecords, out bBuildSuccess, Hook, BaseDirectories, OnBuildingProjects);
		}

		/// <summary>
		/// This method exists purely to prevent EpicGames.MsBuild from being loaded until the absolute last moment.
		/// If it is placed in the caller directly, then when the caller is invoked, the assembly will be loaded resulting
		/// in the possible Microsoft.Build.Framework load issue later on is this method isn't invoked.
		/// </summary>
		static Dictionary<FileReference, (CsProjBuildRecord, FileReference)> Build(HashSet<FileReference> FoundProjects,
			bool bForceCompile, out bool bBuildSuccess, CsProjBuildHook Hook, List<DirectoryReference> BaseDirectories,
			Action<int> OnBuildingProjects)
		{
			return CsProjBuilder.Build(FoundProjects, bForceCompile, out bBuildSuccess, Hook, BaseDirectories, OnBuildingProjects);
		}

		/// <summary>
		/// Find and load existing build record .json files from any Intermediate/ScriptModules found in the provided lists
		/// </summary>
		/// <param name="BaseDirectories"></param>
		/// <returns></returns>
		static Dictionary<FileReference, (CsProjBuildRecord, FileReference)> LoadExistingBuildRecords(List<DirectoryReference> BaseDirectories)
        {
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> LoadedBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>();

			foreach (DirectoryReference Directory in BaseDirectories)
			{
				DirectoryReference IntermediateDirectory = DirectoryReference.Combine(Directory, "Intermediate", "ScriptModules");
				if (!DirectoryReference.Exists(IntermediateDirectory))
				{
					continue;
				}

				foreach (FileReference JsonFile in DirectoryReference.EnumerateFiles(IntermediateDirectory, "*.json"))
                {
					CsProjBuildRecord? BuildRecord = default;

					// filesystem errors or json parsing might result in an exception. If that happens, we fall back to the
					// slower path - if compiling, buildrecord files will be re-generated; other filesystem errors may persist
					try
					{
						BuildRecord = JsonSerializer.Deserialize<CsProjBuildRecord>(FileReference.ReadAllText(JsonFile));
						Log.TraceLog($"Loaded script module build record {JsonFile}");
					}
					catch(Exception Ex)
					{
						Log.TraceWarning($"[{JsonFile}] Failed to load build record: {Ex.Message}");
					}

					if (BuildRecord != null && BuildRecord.ProjectPath != null)
					{
						LoadedBuildRecords.Add(FileReference.FromString(Path.GetFullPath(BuildRecord.ProjectPath, JsonFile.Directory.FullName)), (BuildRecord, JsonFile));
					}
					else
                    {
						// Delete the invalid build record
						Log.TraceWarning($"Deleting invalid build record {JsonFile}");
                    }
                }
			}

			return LoadedBuildRecords;
        }

		private static bool ValidateGlobbedFiles(DirectoryReference ProjectDirectory, 
			List<CsProjBuildRecord.Glob> Globs, HashSet<string> GlobbedDependencies, out string ValidationFailureMessage)
		{
			// First, evaluate globs
			
			// Files are grouped by ItemType (e.g. Compile, EmbeddedResource) to ensure that Exclude and
			// Remove act as expected.
			Dictionary<string, HashSet<string>> Files = new Dictionary<string, HashSet<string>>();
			foreach (CsProjBuildRecord.Glob Glob in Globs)
			{
				HashSet<string>? TypedFiles;
				if (!Files.TryGetValue(Glob.ItemType!, out TypedFiles))
				{
					TypedFiles = new HashSet<string>();
					Files.Add(Glob.ItemType!, TypedFiles);
				}
				
				foreach (string IncludePath in Glob.Include!)
				{
					TypedFiles.UnionWith(FileMatcher.Default.GetFiles(ProjectDirectory.FullName, IncludePath, Glob.Exclude));
				}

				foreach (string Remove in Glob.Remove!)
				{
					// FileMatcher.IsMatch() doesn't handle inconsistent path separators correctly - which is why globs
					// are normalized when they are added to CsProjBuildRecord
					TypedFiles.RemoveWhere(F => FileMatcher.IsMatch(F, Remove));
				}
			}

			// Then, validation that our evaluation matches what we're comparing against

			bool bValid = true;
			StringBuilder ValidationFailureText = new StringBuilder();
			
			// Look for extra files that were found
			foreach (HashSet<string> TypedFiles in Files.Values)
			{
				foreach (string File in TypedFiles)
				{
					if (!GlobbedDependencies.Contains(File))
					{
						ValidationFailureText.AppendLine($"Found additional file {File}");
						bValid = false;
					}
				}
			}
			
			// Look for files that are missing
			foreach (string File in GlobbedDependencies)
			{
				bool bFound = false;
				foreach (HashSet<string> TypedFiles in Files.Values)
				{
					if (TypedFiles.Contains(File))
					{
						bFound = true;
						break;
					}
				}

				if (!bFound)
				{
					ValidationFailureText.AppendLine($"Did not find {File}");
					bValid = false;
				}
			}

			ValidationFailureMessage = ValidationFailureText.ToString();
			return bValid;
		}

		private static bool ValidateBuildRecord(CsProjBuildRecord BuildRecord, DirectoryReference ProjectDirectory, out string ValidationFailureMessage, CsProjBuildHook Hook)
		{
			string TargetRelativePath =
				Path.GetRelativePath(Unreal.EngineDirectory.FullName, BuildRecord.TargetPath!);

			if (BuildRecord.Version != CsProjBuildRecord.CurrentVersion)
			{
				ValidationFailureMessage =
					$"version does not match: build record has version {BuildRecord.Version}; current version is {CsProjBuildRecord.CurrentVersion}";
				return false;
			}

			DateTime TargetWriteTime = Hook.GetLastWriteTime(ProjectDirectory, BuildRecord.TargetPath!);

			if (BuildRecord.TargetBuildTime != TargetWriteTime)
			{
				ValidationFailureMessage =
					$"recorded target build time ({BuildRecord.TargetBuildTime}) does not match {TargetRelativePath} ({TargetWriteTime})";
				return false;
			}

			foreach (string Dependency in BuildRecord.Dependencies)
			{
				if (Hook.GetLastWriteTime(ProjectDirectory, Dependency) > TargetWriteTime)
				{
					ValidationFailureMessage = $"{Dependency} is newer than {TargetRelativePath}";
					return false;
				}
			}

			if (!ValidateGlobbedFiles(ProjectDirectory, BuildRecord.Globs, BuildRecord.GlobbedDependencies,
				out ValidationFailureMessage))
			{
				return false;
			}

			foreach (string Dependency in BuildRecord.GlobbedDependencies)
			{
				if (Hook.GetLastWriteTime(ProjectDirectory, Dependency) > TargetWriteTime)
				{
					ValidationFailureMessage = $"{Dependency} is newer than {TargetRelativePath}";
					return false;
				}
			}

			return true;
		}

		static void ValidateBuildRecordRecursively(
			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference)> ValidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> InvalidBuildRecords,
			Dictionary<FileReference, (CsProjBuildRecord, FileReference)> BuildRecords,
			FileReference ProjectPath, CsProjBuildHook Hook)
		{
			if (ValidBuildRecords.ContainsKey(ProjectPath) || InvalidBuildRecords.ContainsKey(ProjectPath))
			{
				// Project validity has already been determined
				return;
			}

			// Was a build record loaded for this project path? (relevant when considering referenced projects)
			if (!BuildRecords.TryGetValue(ProjectPath, out (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath) Entry))
			{
				Log.TraceLog($"Found project {ProjectPath} with no existing build record");
				return;
			}

			// Is this particular build record valid?
			if (!ValidateBuildRecord(Entry.BuildRecord, ProjectPath.Directory, out string ValidationFailureMessage, Hook))
			{
				string ProjectRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
				Log.TraceLog($"[{ProjectRelativePath}] {ValidationFailureMessage}");

				InvalidBuildRecords.Add(ProjectPath, Entry);
				return;
			}

			// Are all referenced build records valid?
			foreach (string ReferencedProjectPath in Entry.BuildRecord.ProjectReferences)
			{
				FileReference FullProjectPath = FileReference.FromString(Path.GetFullPath(ReferencedProjectPath, ProjectPath.Directory.FullName));
				ValidateBuildRecordRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, FullProjectPath, Hook);

				if (!ValidBuildRecords.ContainsKey(FullProjectPath))
				{
					string ProjectRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
					string DependencyRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, FullProjectPath.FullName);
					Log.TraceLog($"[{ProjectRelativePath}] Existing output is not valid because dependency {DependencyRelativePath} is not valid");
					InvalidBuildRecords.Add(ProjectPath, Entry);
					return;
				}

				// Ensure that the dependency was not built more recently than the project
				if (Entry.BuildRecord.TargetBuildTime < ValidBuildRecords[FullProjectPath].BuildRecord.TargetBuildTime)
				{
					string ProjectRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, ProjectPath.FullName);
					string DependencyRelativePath = Path.GetRelativePath(Unreal.EngineDirectory.FullName, FullProjectPath.FullName);
					Log.TraceLog($"[{ProjectRelativePath}] Existing output is not valid because dependency {DependencyRelativePath} is newer");
					InvalidBuildRecords.Add(ProjectPath, Entry);
					return;
				}
			}

			ValidBuildRecords.Add(ProjectPath, Entry);
		}

		static List<DirectoryReference> GetGameDirectories(string? ScriptsForProjectFileName)
        {
			List<DirectoryReference> GameDirectories = new List<DirectoryReference>();

			if (String.IsNullOrEmpty(ScriptsForProjectFileName))
			{
				GameDirectories = NativeProjectsBase.EnumerateProjectFiles().Select(x => x.Directory).ToList();
			}
			else
			{
				DirectoryReference ScriptsDir = new DirectoryReference(Path.GetDirectoryName(ScriptsForProjectFileName)!);
				ScriptsDir = DirectoryReference.FindCorrectCase(ScriptsDir);
				GameDirectories.Add(ScriptsDir);
			}
			return GameDirectories;
        }

		static List<DirectoryReference> GetAdditionalDirectories(List<string>? AdditionalScriptsFolders) =>
			AdditionalScriptsFolders == null ? new List<DirectoryReference>() :
				AdditionalScriptsFolders.Select(x => DirectoryReference.FindCorrectCase(new DirectoryReference(x))).ToList();

		static List<DirectoryReference> GetAdditionalBuildDirectories(List<DirectoryReference> GameDirectories) =>
			GameDirectories.Select(x => DirectoryReference.Combine(x, "Build")).Where(x => DirectoryReference.Exists(x)).ToList();
    }
}

