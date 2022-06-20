// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Build.Evaluation;
using Microsoft.Build.Execution;
using Microsoft.Build.Framework;
using Microsoft.Build.Graph;
using Microsoft.Build.Locator;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.Json;

namespace EpicGames.MsBuild
{
	public static class CsProjBuilder
	{
		class MLogger : ILogger
		{
			LoggerVerbosity ILogger.Verbosity { get => LoggerVerbosity.Normal; set => throw new NotImplementedException(); }
			string ILogger.Parameters { get => throw new NotImplementedException(); set { } }

			public bool bVeryVerboseLog = false;

			bool bFirstError = true;

			void ILogger.Initialize(IEventSource EventSource)
			{
				EventSource.ProjectStarted += new ProjectStartedEventHandler(eventSource_ProjectStarted);
				EventSource.TaskStarted += new TaskStartedEventHandler(eventSource_TaskStarted);
				EventSource.MessageRaised += new BuildMessageEventHandler(eventSource_MessageRaised);
				EventSource.WarningRaised += new BuildWarningEventHandler(eventSource_WarningRaised);
				EventSource.ErrorRaised += new BuildErrorEventHandler(eventSource_ErrorRaised);
				EventSource.ProjectFinished += new ProjectFinishedEventHandler(eventSource_ProjectFinished);
			}

			void eventSource_ErrorRaised(object Sender, BuildErrorEventArgs e)
			{
				if (bFirstError)
				{
					Trace.WriteLine("");
					Log.WriteLine(LogEventType.Console, "");
					bFirstError = false;
				}
				string Message = $"{e.File}({e.LineNumber},{e.ColumnNumber}): error {e.Code}: {e.Message} ({e.ProjectFile})";
				Trace.WriteLine(Message); // double-clickable message in VS output
				Log.WriteLine(LogEventType.Console, Message);
			}

			void eventSource_WarningRaised(object Sender, BuildWarningEventArgs e)
			{
				string Message = $"{e.File}({e.LineNumber},{e.ColumnNumber}): warning {e.Code}: {e.Message} ({e.ProjectFile})";


				{
					// workaround for warnings that appear after revert of net6.0 upgrade. Delete this block when the net6.0 upgrade is done.
					// ...\Engine\Binaries\ThirdParty\DotNet\Windows\sdk\3.1.403\Microsoft.Common.CurrentVersion.targets(3036,5): warning MSB3088: Could not read state file "obj\Development\[projectname].csproj.GenerateResource.cache". The input stream is not a valid binary format.
					// The starting contents (in bytes) are: 06-01-01-00-00-00-01-19-50-72-6F-70-65-72-74-69-65 ... (...\[projectname].csproj)
					if (String.Equals(e.Code, "MSB3088", StringComparison.Ordinal))
					{
						Log.TraceLog("Suppressed warning: " + Message);
						return;
					}
				}

				if (bFirstError)
                {
					Trace.WriteLine("");
					Log.WriteLine(LogEventType.Console, "");
					bFirstError = false;
                }

				Trace.WriteLine(Message); // double-clickable message in VS output
				Log.WriteLine(LogEventType.Console, Message);
			}

			void eventSource_MessageRaised(object Sender, BuildMessageEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					//if (!String.Equals(e.SenderName, "ResolveAssemblyReference"))
					//if (e.Message.Contains("atic"))
					{
						Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
					}
				}
			}

			void eventSource_ProjectStarted(object Sender, ProjectStartedEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
				}
			}

			void eventSource_ProjectFinished(object Sender, ProjectFinishedEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
				}
			}

			void eventSource_TaskStarted(object Sender, TaskStartedEventArgs e)
			{
				if (bVeryVerboseLog)
				{
					Log.WriteLine(LogEventType.Console, $"{e.SenderName}: {e.Message}");
				}
			}

			void ILogger.Shutdown()
			{
			}
		}

		static FileReference ConstructBuildRecordPath(FileReference ProjectPath, List<DirectoryReference> BaseDirectories)
		{
			DirectoryReference BasePath = null;

			foreach (DirectoryReference ScriptFolder in BaseDirectories)
			{
				if (ProjectPath.IsUnderDirectory(ScriptFolder))
				{
					BasePath = ScriptFolder;
					break;
				}
			}

			if (BasePath == null)
			{
				throw new Exception($"Unable to map csproj {ProjectPath} to Engine, game, or an additional script folder. Candidates were:{Environment.NewLine} {String.Join(Environment.NewLine, BaseDirectories)}");
			}

			DirectoryReference BuildRecordDirectory = DirectoryReference.Combine(BasePath!, "Intermediate", "ScriptModules");
			DirectoryReference.CreateDirectory(BuildRecordDirectory);

			return FileReference.Combine(BuildRecordDirectory, ProjectPath.GetFileName()).ChangeExtension(".json");
		}

		public static Dictionary<FileReference, (CsProjBuildRecord, FileReference)> Build(HashSet<FileReference> FoundProjects,
			bool bForceCompile, out bool bBuildSuccess, CsProjBuildHook Hook, List<DirectoryReference> BaseDirectories, Action<int> OnBuildingProjects)
		{

			// Register the MS build path prior to invoking the internal routine.  By not having the internal routine
			// inline, we avoid having the issue of the Microsoft.Build libraries being resolved prior to the build path
			// being set.
			RegisterMsBuildPath(Hook);
			return BuildInternal(FoundProjects, bForceCompile, out bBuildSuccess, Hook, BaseDirectories, OnBuildingProjects);
		}

		private static Dictionary<FileReference, (CsProjBuildRecord, FileReference)> BuildInternal(HashSet<FileReference> FoundProjects,
			bool bForceCompile, out bool bBuildSuccess, CsProjBuildHook Hook, List<DirectoryReference> BaseDirectories, Action<int> OnBuildingProjects)
		{
			Dictionary<string, string> GlobalProperties = new Dictionary<string, string>
			{
				{ "EngineDir", Hook.EngineDirectory.FullName },
#if DEBUG
				{ "Configuration", "Debug" },
#else
				{ "Configuration", "Development" },
#endif
			};

			Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)> BuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>();

			using var ProjectCollection = new ProjectCollection(GlobalProperties);
			var Projects = new Dictionary<string, Project>();
			var	SkippedProjects = new HashSet<string>();

			// Microsoft.Build.Evaluation.Project provides access to information stored in the .csproj xml that is 
			// not available when using Microsoft.Build.Execution.ProjectInstance (used later in this function and
			// in BuildProjects) - particularly, to access glob information defined in the source file.

			// Load all found projects, and any other referenced projects.
			foreach (FileReference ProjectPath in FoundProjects)
			{
				void LoadProjectAndReferences(string ProjectPath, string ReferencedBy)
				{
					ProjectPath = Path.GetFullPath(ProjectPath);
					if (!Projects.ContainsKey(ProjectPath) && !SkippedProjects.Contains(ProjectPath))
					{
						Project Project;

						// Microsoft.Build.Evaluation.Project doesn't give a lot of useful information if this fails,
						// so make sure to print our own diagnostic info if something goes wrong
						try
						{
							Project = new Project(ProjectPath, GlobalProperties, toolsVersion: null, projectCollection: ProjectCollection);
						}
						catch (Microsoft.Build.Exceptions.InvalidProjectFileException IPFEx)
						{
							Log.TraceError($"Could not load project file {ProjectPath}");
							Log.TraceError(IPFEx.BaseMessage);

							if (!String.IsNullOrEmpty(ReferencedBy))
							{
								Log.TraceError($"Referenced by: {ReferencedBy}");
							}
							if (Projects.Count > 0)
							{
								Log.TraceError("See the log file for the list of previously loaded projects.");
								Log.TraceLog("Loaded projects (most recently loaded first):");
								foreach (string Path in Projects.Keys.Reverse())
								{
									Log.TraceLog($"  {Path}");
								}
							}
							throw IPFEx;
						}

						if (!OperatingSystem.IsWindows())
						{
							// check the TargetFramework of the project: we can't build Windows-only projects on 
							// non-Windows platforms.
							if (Project.GetProperty("TargetFramework").EvaluatedValue.Contains("windows", StringComparison.Ordinal))
							{
								SkippedProjects.Add(ProjectPath);
								Log.TraceInformation($"Skipping windows-only project {ProjectPath}");
								return;
							}
						}

						Projects.Add(ProjectPath, Project);
						ReferencedBy = String.IsNullOrEmpty(ReferencedBy) ? ProjectPath : $"{ProjectPath}{Environment.NewLine}{ReferencedBy}";
						foreach (string ReferencedProject in Project.GetItems("ProjectReference").
							Select(I => I.EvaluatedInclude))
						{
							LoadProjectAndReferences(Path.Combine(Project.DirectoryPath, ReferencedProject), ReferencedBy);
						}
					}
				}
				LoadProjectAndReferences(ProjectPath.FullName, null);
			}

			// generate a BuildRecord for each loaded project - the gathered information will be used to determine if the project is
			// out of date, and if building this project can be skipped. It is also used to populate Intermediate/ScriptModules after the
			// build completes
			foreach (Project Project in Projects.Values)
			{
				string TargetPath = Path.GetRelativePath(Project.DirectoryPath, Project.GetPropertyValue("TargetPath"));

				FileReference ProjectPath = FileReference.FromString(Project.FullPath);
				FileReference BuildRecordPath = ConstructBuildRecordPath(ProjectPath, BaseDirectories);

				CsProjBuildRecord BuildRecord = new CsProjBuildRecord()
				{
					Version = CsProjBuildRecord.CurrentVersion,
					TargetPath = TargetPath,
					TargetBuildTime = Hook.GetLastWriteTime(Project.DirectoryPath, TargetPath),
					ProjectPath = Path.GetRelativePath(BuildRecordPath.Directory.FullName, Project.FullPath)
				};

				// the .csproj
				BuildRecord.Dependencies.Add(Path.GetRelativePath(Project.DirectoryPath, Project.FullPath));

				// Imports: files included in the xml (typically props, targets, etc)
				foreach (ResolvedImport Import in Project.Imports)
				{
					string ImportPath = Path.GetRelativePath(Project.DirectoryPath, Import.ImportedProject.FullPath);

					// nuget.g.props and nuget.g.targets are generated by Restore, and are frequently re-written;
					// it should be safe to ignore these files - changes to references from a .csproj file will
					// show up as that file being out of date.
					if (ImportPath.IndexOf("nuget.g.", StringComparison.Ordinal) != -1)
					{
						continue;
					}

					BuildRecord.Dependencies.Add(ImportPath);
				}

				// References: e.g. Ionic.Zip.Reduced.dll, fastJSON.dll
				foreach (var Item in Project.GetItems("Reference"))
				{
					BuildRecord.Dependencies.Add(Item.GetMetadataValue("HintPath"));
				}

				foreach (ProjectItem ReferencedProjectItem in Project.GetItems("ProjectReference"))
				{
					BuildRecord.ProjectReferences.Add(ReferencedProjectItem.EvaluatedInclude);
				}

				foreach (ProjectItem CompileItem in Project.GetItems("Compile"))
				{
					if (Hook.HasWildcards(CompileItem.UnevaluatedInclude))
					{
						BuildRecord.GlobbedDependencies.Add(CompileItem.EvaluatedInclude);
					}
					else
					{
						BuildRecord.Dependencies.Add(CompileItem.EvaluatedInclude);
					}
				}

				foreach (ProjectItem ContentItem in Project.GetItems("Content"))
				{
					if (Hook.HasWildcards(ContentItem.UnevaluatedInclude))
					{
						BuildRecord.GlobbedDependencies.Add(ContentItem.EvaluatedInclude);
					}
					else
					{
						BuildRecord.Dependencies.Add(ContentItem.EvaluatedInclude);
					}
				}

				foreach (ProjectItem EmbeddedResourceItem in Project.GetItems("EmbeddedResource"))
				{
					if (Hook.HasWildcards(EmbeddedResourceItem.UnevaluatedInclude))
					{
						BuildRecord.GlobbedDependencies.Add(EmbeddedResourceItem.EvaluatedInclude);
					}
					else
					{
						BuildRecord.Dependencies.Add(EmbeddedResourceItem.EvaluatedInclude);
					}
				}

				// this line right here is slow: ~30-40ms per project (which can be more than a second total)
				// making it one of the slowest steps in gathering or checking dependency information from
				// .csproj files (after loading as Microsoft.Build.Evalation.Project)
				// 
				// This also returns a lot more information than we care for - MSBuildGlob objects,
				// which have a range of precomputed values. It may be possible to take source for
				// GetAllGlobs() and construct a version that does less.
				var Globs = Project.GetAllGlobs();

				// FileMatcher.IsMatch() requires directory separators in glob strings to match the
				// local flavor. There's probably a better way.
				string CleanGlobString(string GlobString)
				{
					char Sep = Path.DirectorySeparatorChar;
					char NotSep = Sep == '/' ? '\\' : '/'; // AltDirectorySeparatorChar isn't always what we need (it's '/' on Mac)

					var Chars = GlobString.ToCharArray();
					int P = 0;
					for (int I = 0; I < GlobString.Length; ++I, ++P)
					{
						// Flip a non-native separator
						if (Chars[I] == NotSep)
						{
							Chars[P] = Sep;
						}
						else
						{
							Chars[P] = Chars[I];
						}

						// Collapse adjacent separators
						if (I > 0 && Chars[P] == Sep && Chars[P - 1] == Sep)
						{
							P -= 1;
						}
					}

					return new string(Chars, 0, P);
				}

				foreach (var Glob in Globs)
				{
					if (String.Equals("None", Glob.ItemElement.ItemType, StringComparison.Ordinal))
					{
						// don't record the default "None" glob - it's not (?) a trigger for any rebuild
						continue;
					}

					List<string> Include = new List<string>(Glob.IncludeGlobs.Select(F => CleanGlobString(F))).OrderBy(x => x).ToList();
					List<string> Exclude = new List<string>(Glob.Excludes.Select(F => CleanGlobString(F))).OrderBy(x => x).ToList();
					List<string> Remove = new List<string>(Glob.Removes.Select(F => CleanGlobString(F))).OrderBy(x => x).ToList();

					BuildRecord.Globs.Add(new CsProjBuildRecord.Glob()
					{
						ItemType = Glob.ItemElement.ItemType,
						Include = Include,
						Exclude = Exclude,
						Remove = Remove
					});
				}

				BuildRecords.Add(ProjectPath, (BuildRecord, BuildRecordPath));
			}

			// Potential optimization: Constructing the ProjectGraph here gives the full graph of dependencies - which is nice,
			// but not strictly necessary, and slower than doing it some other way.
			ProjectGraph InputProjectGraph;
			InputProjectGraph = new ProjectGraph(FoundProjects
				// Build the graph without anything that can't be built on this platform
				.Where(x => !SkippedProjects.Contains(x.FullName))
				.Select(P => P.FullName), GlobalProperties, ProjectCollection);

			// A ProjectGraph that will represent the set of projects that we actually want to build
			ProjectGraph BuildProjectGraph = null;

			if (bForceCompile)
			{
				Log.TraceLog("Script modules will build: '-Compile' on command line");
				BuildProjectGraph = InputProjectGraph;
			}
			else
			{
				Dictionary<FileReference, (CsProjBuildRecord, FileReference)> ValidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(BuildRecords.Count);
				Dictionary<FileReference, (CsProjBuildRecord, FileReference)> InvalidBuildRecords = new Dictionary<FileReference, (CsProjBuildRecord, FileReference)>(BuildRecords.Count);

				foreach (ProjectGraphNode Project in InputProjectGraph.ProjectNodesTopologicallySorted)
				{
					Hook.ValidateRecursively(ValidBuildRecords, InvalidBuildRecords, BuildRecords, FileReference.FromString(Project.ProjectInstance.FullPath));
				}

				// Select the projects that have been found to be out of date
				HashSet<ProjectGraphNode> OutOfDateProjects = new HashSet<ProjectGraphNode>(InputProjectGraph.ProjectNodes.Where(x => InvalidBuildRecords.ContainsKey(FileReference.FromString(x.ProjectInstance.FullPath))));

				if (OutOfDateProjects.Count > 0)
				{
					BuildProjectGraph = new ProjectGraph(OutOfDateProjects.Select(P => P.ProjectInstance.FullPath), GlobalProperties, ProjectCollection);
				}
			}

			if (BuildProjectGraph != null)
			{
				OnBuildingProjects(BuildProjectGraph.EntryPointNodes.Count);
				bBuildSuccess = BuildProjects(BuildProjectGraph, GlobalProperties);
			}
			else
			{
				bBuildSuccess = true;
			}

			// write all build records
			foreach (ProjectGraphNode ProjectNode in InputProjectGraph.ProjectNodes)
			{
				FileReference ProjectPath = FileReference.FromString(ProjectNode.ProjectInstance.FullPath);

				(CsProjBuildRecord BuildRecord, FileReference BuildRecordPath) = BuildRecords[ProjectPath];

				// update target build times into build records to ensure everything is up-to-date
				FileReference FullPath = FileReference.Combine(ProjectPath.Directory, BuildRecord.TargetPath);
				BuildRecord.TargetBuildTime = FileReference.GetLastWriteTime(FullPath);

				if (FileReference.WriteAllTextIfDifferent(BuildRecordPath,
					JsonSerializer.Serialize<CsProjBuildRecord>(BuildRecord, new JsonSerializerOptions { WriteIndented = true })))
				{
					Log.TraceLog($"Wrote script module build record to {BuildRecordPath}");
				}
			}

			// todo: re-verify build records after a build to verify that everything is actually up to date

			// even if only a subset was built, this function returns the full list of target assembly paths
			var OutDict = new Dictionary<FileReference, (CsProjBuildRecord BuildRecord, FileReference BuildRecordPath)>();
			foreach (ProjectGraphNode EntryPointNode in InputProjectGraph.EntryPointNodes)
			{
				FileReference ProjectPath = FileReference.FromString(EntryPointNode.ProjectInstance.FullPath);
				OutDict.Add(ProjectPath, BuildRecords[ProjectPath]);
			}
			return OutDict;
		}

		private static bool BuildProjects(ProjectGraph ProjectGraph, Dictionary<string, string> GlobalProperties)
		{
			var Logger = new MLogger();

			string[] TargetsToBuild = { "Restore", "Build" };

			bool Result = true;

			foreach (string TargetToBuild in TargetsToBuild)
			{
				var GraphRequest = new GraphBuildRequestData(ProjectGraph, new string[] { TargetToBuild });

				var BuildMan = BuildManager.DefaultBuildManager;

				var BuildParameters = new BuildParameters();
				BuildParameters.AllowFailureWithoutError = false;
				BuildParameters.DetailedSummary = true;

				BuildParameters.Loggers = new List<ILogger> { Logger };
				BuildParameters.MaxNodeCount = 1; // msbuild bug - more than 1 here and the build stalls. Likely related to https://github.com/dotnet/msbuild/issues/1941

				BuildParameters.OnlyLogCriticalEvents = false;
				BuildParameters.ShutdownInProcNodeOnBuildFinish = false;

				BuildParameters.GlobalProperties = GlobalProperties;

				Log.TraceInformation($" {TargetToBuild}...");

				GraphBuildResult BuildResult = BuildMan.Build(BuildParameters, GraphRequest);

				if (BuildResult.OverallResult == BuildResultCode.Failure)
				{
					Log.TraceInformation("");
					foreach (var NodeResult in BuildResult.ResultsByNode)
					{
						if (NodeResult.Value.OverallResult == BuildResultCode.Failure)
						{
							Log.TraceError($"  Failed to build: {NodeResult.Key.ProjectInstance.FullPath}");
						}
					}
					Result = false;
				}
			}
			Log.TraceInformation(" build complete.");

			return Result;
		}

		static bool _hasRegiteredMsBuildPath = false;

		/// <summary>
		/// Register our bundled dotnet installation to be used by Microsoft.Build
		/// This needs to happen in a function called before the first use of any Microsoft.Build types
		/// </summary>
		public static void RegisterMsBuildPath(CsProjBuildHook Hook)
		{
			if (_hasRegiteredMsBuildPath)
			{
				return;
			}
			_hasRegiteredMsBuildPath = true;

			// Find our bundled dotnet SDK
			List<string> ListOfSdks = new List<string>();
			ProcessStartInfo StartInfo = new ProcessStartInfo
			{
				FileName = Hook.DotnetPath.FullName,
				RedirectStandardOutput = true,
				UseShellExecute = false,
				ArgumentList = { "--list-sdks" }
			};
			StartInfo.EnvironmentVariables["DOTNET_MULTILEVEL_LOOKUP"] = "0"; // use only the bundled dotnet installation - ignore any other/system dotnet install

			Process DotnetProcess = Process.Start(StartInfo);
			{
				string Line;
				while ((Line = DotnetProcess.StandardOutput.ReadLine()) != null)
				{
					ListOfSdks.Add(Line);
				}
			}
			DotnetProcess.WaitForExit();

			if (ListOfSdks.Count != 1)
			{
				throw new Exception("Expected only one sdk installed for bundled dotnet");
			}

			// Expected output has this form:
			// 3.1.403 [D:\UE5_Main\engine\binaries\ThirdParty\DotNet\Windows\sdk]
			string SdkVersion = ListOfSdks[0].Split(' ')[0];

			DirectoryReference DotnetSdkDirectory = DirectoryReference.Combine(Hook.DotnetDirectory, "sdk", SdkVersion);
			if (!DirectoryReference.Exists(DotnetSdkDirectory))
			{
				throw new Exception("Failed to find .NET SDK directory: " + DotnetSdkDirectory.FullName);
			}

			MSBuildLocator.RegisterMSBuildPath(DotnetSdkDirectory.FullName);
		}
	}
}
