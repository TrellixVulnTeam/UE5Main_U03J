// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Tables;
using Microsoft.Extensions.Logging;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.UHT.Types;
using System.IO.Enumeration;

namespace EpicGames.UHT.Utils
{

	/// <summary>
	/// To support the testing framework, source files can be containing in other source files.  
	/// A source fragment represents this possibility.
	/// </summary>
	public struct UhtSourceFragment
	{

		/// <summary>
		/// When not null, this source comes from another source file
		/// </summary>
		public UhtSourceFile? SourceFile;

		/// <summary>
		/// The file path of the source
		/// </summary>
		public string FilePath;

		/// <summary>
		/// The line number of the fragment in the containing source file.
		/// </summary>
		public int LineNumber;

		/// <summary>
		/// Data of the source file
		/// </summary>
		public StringView Data;
	}

	/// <summary>
	/// Implementation of the export factory
	/// </summary>
	class UhtExportFactory : IUhtExportFactory
	{
		public struct Output
		{
			public string FilePath;
			public string TempFilePath;
			public bool bSaved;
		}

		/// <summary>
		/// UHT session
		/// </summary>
		private readonly UhtSession SessionInternal;

		/// <summary>
		/// Module associated with the plugin
		/// </summary>
		private readonly UHTManifest.Module? PluginModuleInternal;

		/// <summary>
		/// Limiter for the number of files being saved to the reference directory.
		/// The OS can get swamped on high core systems
		/// </summary>
		private Semaphore WriteRefSemaphore = new Semaphore(32, 32);

		/// <summary>
		/// Requesting exporter
		/// </summary>
		public readonly UhtExporter Exporter;

		/// <summary>
		/// UHT Session
		/// </summary>
		public UhtSession Session => this.SessionInternal;

		/// <summary>
		/// Plugin module
		/// </summary>
		public UHTManifest.Module? PluginModule => this.PluginModuleInternal;

		/// <summary>
		/// Collection of error from mismatches with the reference files
		/// </summary>
		public Dictionary<string, bool> ReferenceErrorMessages = new Dictionary<string, bool>();

		/// <summary>
		/// List of export outputs
		/// </summary>
		public List<Output> Outputs = new List<Output>();

		/// <summary>
		/// Directory for the reference output
		/// </summary>
		public string ReferenceDirectory = string.Empty;

		/// <summary>
		/// Directory for the verify output
		/// </summary>
		public string VerifyDirectory = string.Empty;

		/// <summary>
		/// Collection of external dependencies
		/// </summary>
		public HashSet<string> ExternalDependencies = new HashSet<string>();

		/// <summary>
		/// Create a new instance of the export factory
		/// </summary>
		/// <param name="Session">UHT session</param>
		/// <param name="PluginModule">Plugin module</param>
		/// <param name="Exporter">Exporter being run</param>
		public UhtExportFactory(UhtSession Session, UHTManifest.Module? PluginModule, UhtExporter Exporter)
		{
			this.SessionInternal = Session;
			this.PluginModuleInternal = PluginModule;
			this.Exporter = Exporter;
			if (this.Session.ReferenceMode != UhtReferenceMode.None)
			{
				this.ReferenceDirectory = Path.Combine(this.Session.ReferenceDirectory, this.Exporter.Name);
				this.VerifyDirectory = Path.Combine(this.Session.VerifyDirectory, this.Exporter.Name);
				Directory.CreateDirectory(this.Session.ReferenceMode == UhtReferenceMode.Reference ? this.ReferenceDirectory : this.VerifyDirectory);
			}
		}

		/// <summary>
		/// Commit the contents of the string builder as the output.
		/// If you have a string builder, use this method so that a 
		/// temporary buffer can be used.
		/// </summary>
		/// <param name="FilePath">Destination file path</param>
		/// <param name="Builder">Source for the content</param>
		public void CommitOutput(string FilePath, StringBuilder Builder)
		{
			using (UhtBorrowBuffer BorrowBuffer = new UhtBorrowBuffer(Builder))
			{
				string TempFilePath = FilePath + ".tmp";
				SaveIfChanged(FilePath, TempFilePath, new StringView(BorrowBuffer.Buffer.Memory));
			}
		}

		/// <summary>
		/// Commit the value of the string as the output
		/// </summary>
		/// <param name="FilePath">Destination file path</param>
		/// <param name="Output">Output to commit</param>
		public void CommitOutput(string FilePath, StringView Output)
		{
			string TempFilePath = FilePath + ".tmp";
			SaveIfChanged(FilePath, TempFilePath, Output);
		}

		/// <summary>
		/// Create a task to export two files
		/// </summary>
		/// <param name="Prereqs">Tasks that must be completed prior to this task running</param>
		/// <param name="Action">Action to be invoked to generate the output</param>
		/// <returns>Task object or null if the task was immediately executed.</returns>
		public Task? CreateTask(List<Task?>? Prereqs, UhtExportTaskDelegate Action)
		{
			if (this.Session.bGoWide)
			{
				Task[]? PrereqTasks = Prereqs != null ? Prereqs.Where(x => x != null).Cast<Task>().ToArray() : null;
				if (PrereqTasks != null && PrereqTasks.Length > 0)
				{
					return Task.Factory.ContinueWhenAll(PrereqTasks, (Task[] Tasks) => { Action(this); });
				}
				else
				{
					return Task.Factory.StartNew(() => { Action(this); });
				}
			}
			else
			{
				Action(this);
				return null;
			}
		}

		/// <summary>
		/// Create a task to export two files
		/// </summary>
		/// <param name="Action">Action to be invoked to generate the output</param>
		/// <returns>Task object or null if the task was immediately executed.</returns>
		public Task? CreateTask(UhtExportTaskDelegate Action)
		{
			return CreateTask(null, Action);
		}

		/// <summary>
		/// Given a header file, generate the output file name.
		/// </summary>
		/// <param name="HeaderFile">Header file</param>
		/// <param name="Suffix">Suffix/extension to be added to the file name.</param>
		/// <returns>Resulting file name</returns>
		public string MakePath(UhtHeaderFile HeaderFile, string Suffix)
		{
			return MakePath(HeaderFile.Package.Module, HeaderFile.FileNameWithoutExtension, Suffix);
		}

		/// <summary>
		/// Given a package file, generate the output file name
		/// </summary>
		/// <param name="Package">Package file</param>
		/// <param name="Suffix">Suffix/extension to be added to the file name.</param>
		/// <returns>Resulting file name</returns>
		public string MakePath(UhtPackage Package, string Suffix)
		{
			return MakePath(Package.Module, Package.ShortName, Suffix);
		}


		/// <summary>
		/// Make a path for an output based on the package output directory.
		/// </summary>
		/// <param name="FileName">Name of the file</param>
		/// <param name="Extension">Extension to add to the file</param>
		/// <returns>Output file path</returns>
		public string MakePath(string FileName, string Extension)
		{
			if (this.PluginModule == null)
			{
				throw new UhtIceException("MakePath with just a filename and extension can not be called from non-plugin exporters");
			}
			return MakePath(this.PluginModule, FileName, Extension);
		}


		/// <summary>
		/// Add an external dependency to the given file path
		/// </summary>
		/// <param name="FilePath">External dependency to add</param>
		public void AddExternalDependency(string FilePath)
		{
			this.ExternalDependencies.Add(FilePath);
		}

		private string MakePath(UHTManifest.Module Module, string FileName, string Suffix)
		{
			if (PluginModule != null)
			{
				Module = PluginModule;
			}
			return Path.Combine(Module.OutputDirectory, FileName) + Suffix;
		}

		/// <summary>
		/// Helper method to test to see if the output has changed.
		/// </summary>
		/// <param name="FilePath">Name of the output file</param>
		/// <param name="TempFilePath">Name of the temporary file</param>
		/// <param name="Exported">Exported contents of the file</param>
		internal void SaveIfChanged(string FilePath, string TempFilePath, StringView Exported)
		{

			ReadOnlySpan<char> ExportedSpan = Exported.Span;

			if (this.Session.ReferenceMode != UhtReferenceMode.None)
			{
				string FileName = Path.GetFileName(FilePath);

				// Writing billions of files to the same directory causes issues.  Use ourselves to throttle reference writes
				try
				{
					this.WriteRefSemaphore.WaitOne();
					{
						string OutPath = Path.Combine(this.Session.ReferenceMode == UhtReferenceMode.Reference ? this.ReferenceDirectory : this.VerifyDirectory, FileName);
						if (!this.Session.WriteSource(OutPath, Exported.Span))
						{
							new UhtSimpleFileMessageSite(this.Session, OutPath).LogWarning($"Unable to write reference file {OutPath}");
						}
					}
				}
				finally
				{
					this.WriteRefSemaphore.Release();
				}

				// If we are verifying, read the existing file and check the contents
				if (this.Session.ReferenceMode == UhtReferenceMode.Verify)
				{
					string Message = String.Empty;
					string RefPath = Path.Combine(this.ReferenceDirectory, FileName);
					UhtBuffer? ExistingRef = this.Session.ReadSourceToBuffer(RefPath);
					if (ExistingRef != null)
					{
						ReadOnlySpan<char> ExistingSpan = ExistingRef.Memory.Span;
						if (ExistingSpan.CompareTo(ExportedSpan, StringComparison.Ordinal) != 0)
						{
							Message = $"********************************* {FileName} has changed.";
						}
						UhtBuffer.Return(ExistingRef);
					}
					else
					{
						Message = $"********************************* {FileName} appears to be a new generated file.";
					}

					if (Message != String.Empty)
					{
						Log.Logger.LogInformation(Message);
						lock (this.ReferenceErrorMessages)
						{
							this.ReferenceErrorMessages.Add(Message, true);
						}
					}
				}
			}

			// Check to see if the contents have changed
			UhtBuffer? Original = this.Session.ReadSourceToBuffer(FilePath);
			bool bSave = Original == null;
			if (Original != null)
			{
				ReadOnlySpan<char> OriginalSpan = Original.Memory.Span;
				if (OriginalSpan.CompareTo(ExportedSpan, StringComparison.Ordinal) != 0)
				{
					if (this.Session.bFailIfGeneratedCodeChanges)
					{
						string ConflictPath = FilePath + ".conflict";
						if (!this.Session.WriteSource(ConflictPath, Exported.Span))
						{
							new UhtSimpleFileMessageSite(this.Session, FilePath).LogError($"Changes to generated code are not allowed - conflicts written to '{ConflictPath}'");
						}
					}
					bSave = true;
				}
				UhtBuffer.Return(Original);
			}

			// If changed of the original didn't exist, then save the new version
			if (bSave && !this.Session.bNoOutput)
			{
				if (!this.Session.WriteSource(TempFilePath, Exported.Span))
				{
					new UhtSimpleFileMessageSite(this.Session, FilePath).LogWarning($"Failed to save export file: '{TempFilePath}'");
				}
			}
			else
			{
				bSave = false;
			}

			// Add this to the list of outputs
			lock (this.Outputs)
			{
				this.Outputs.Add(new Output { FilePath = FilePath, TempFilePath = TempFilePath, bSaved = bSave });
			}
		}       
		
		/// <summary>
		/// Run the output exporter
		/// </summary>
		public void Run()
		{

			// Invoke the exported via the delegate
			this.Exporter.Delegate(this);

			// If outputs were exported
			if (this.Outputs.Count > 0)
			{

				// These outputs are used to cull old outputs from the directories
				Dictionary<string, HashSet<string>> OutputsByDirectory = new Dictionary<string, HashSet<string>>(StringComparer.OrdinalIgnoreCase);
				List<UhtExportFactory.Output> Saves = new List<UhtExportFactory.Output>();

				// Collect information about the outputs
				foreach (UhtExportFactory.Output Output in this.Outputs)
				{

					// Add this output to the list of expected outputs by directory
					string? FileDirectory = Path.GetDirectoryName(Output.FilePath);
					if (FileDirectory != null)
					{
						HashSet<string>? Files;
						if (!OutputsByDirectory.TryGetValue(FileDirectory, out Files))
						{
							Files = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
							OutputsByDirectory.Add(FileDirectory, Files);
						}
						Files.Add(Path.GetFileName(Output.FilePath));
					}

					// Add the save task
					if (Output.bSaved)
					{
						Saves.Add(Output);
					}
				}

				// Perform the renames
				if (this.Session.bGoWide)
				{
					Parallel.ForEach(Saves, (UhtExportFactory.Output Output) =>
					{
						RenameSource(Output);
					});
				}
				else
				{ 
					foreach (UhtExportFactory.Output Output in Saves)
					{
						RenameSource(Output);
					}
				}

				// Perform the culling of the output directories
				if (this.Session.bCullOutput && !this.Session.bNoOutput && 
					(this.Exporter.CppFilters != null || this.Exporter.HeaderFilters != null || this.Exporter.OtherFilters != null))
				{
					if (this.Session.bGoWide)
					{
						Parallel.ForEach(OutputsByDirectory, (KeyValuePair<string, HashSet<string>> Kvp) =>
						{
							CullOutputDirectory(Kvp.Key, Kvp.Value);
						});
					}
					else
					{
						foreach (KeyValuePair<string, HashSet<string>> Kvp in OutputsByDirectory)
						{
							CullOutputDirectory(Kvp.Key, Kvp.Value);
						}
					}
				}
			}
		}

		/// <summary>
		/// Given an output, rename the output file from the temporary file name to the final file name.
		/// If there exists a current final file, it will be replaced.
		/// </summary>
		/// <param name="Output">The output file to rename</param>
		private void RenameSource(UhtExportFactory.Output Output)
		{
			this.Session.RenameSource(Output.TempFilePath, Output.FilePath);
		}

		/// <summary>
		/// Given a directory and a list of known files, delete any unknown file that matches of the supplied filters
		/// </summary>
		/// <param name="OutputDirectory">Output directory to scan</param>
		/// <param name="KnownOutputs">Collection of known output files not to be deleted</param>
		private void CullOutputDirectory(string OutputDirectory, HashSet<string> KnownOutputs)
		{
			foreach (string FilePath in Directory.EnumerateFiles(OutputDirectory))
			{
				string FileName = Path.GetFileName(FilePath);
				if (KnownOutputs.Contains(FileName))
				{
					continue;
				}

				if (IsFilterMatch(FileName, this.Exporter.CppFilters) ||
					IsFilterMatch(FileName, this.Exporter.HeaderFilters) ||
					IsFilterMatch(FileName, this.Exporter.OtherFilters))
				{
					try
					{
						File.Delete(Path.Combine(OutputDirectory, FilePath));
					}
					catch (Exception)
					{
					}
				}
			}
		}

		/// <summary>
		/// Test to see if the given filename (without directory), matches one of the given filters
		/// </summary>
		/// <param name="FileName">File name to test</param>
		/// <param name="Filters">List of wildcard filters</param>
		/// <returns>True if there is a match</returns>
		private static bool IsFilterMatch(string FileName, string[]? Filters)
		{
			if (Filters != null)
			{
				foreach (string Filter in Filters)
				{
					if (FileSystemName.MatchesSimpleExpression(Filter, FileName, true))
					{
						return true;
					}
				}
			}
			return false;
		}
	}

	/// <summary>
	/// UHT supports the exporting of two reference output directories for testing.  The reference version can be used to test
	/// modification to UHT and verify there are no output changes or just expected changes.
	/// </summary>
	public enum UhtReferenceMode
	{
		/// <summary>
		/// Do not export any reference output files
		/// </summary>
		None,

		/// <summary>
		/// Export the reference copy
		/// </summary>
		Reference,

		/// <summary>
		/// Export the verify copy and compare to the reference copy
		/// </summary>
		Verify,
	};

	/// <summary>
	/// Session object that represents a UHT run
	/// </summary>
	public class UhtSession : IUhtMessageSite, IUhtMessageSession
	{

		/// <summary>
		/// Helper class for returning a sequence of auto-incrementing indices
		/// </summary>
		private class TypeCounter
		{

			/// <summary>
			/// Current number of types
			/// </summary>
			private int CountInternal = 0;

			/// <summary>
			/// Get the next type index
			/// </summary>
			/// <returns>Index starting at zero</returns>
			public int GetNext()
			{
				return Interlocked.Increment(ref this.CountInternal) - 1;
			}

			/// <summary>
			/// The number of times GetNext was called.
			/// </summary>
			public int Count => Interlocked.Add(ref this.CountInternal, 0) + 1;
		}

		/// <summary>
		/// Pair that represents a specific value for an enumeration
		/// </summary>
		private struct EnumAndValue
		{
			public UhtEnum Enum;
			public long Value;
		}

		/// <summary>
		/// Collection of reserved names
		/// </summary>
		private static HashSet<string> ReservedNames = new HashSet<string> { "none" };

		#region Configurable settings

		/// <summary>
		/// Interface used to read/write files
		/// </summary>
		public IUhtFileManager? FileManager;

		/// <summary>
		/// Location of the engine code
		/// </summary>
		public string? EngineDirectory;

		/// <summary>
		/// If set, the name of the project file.
		/// </summary>
		public string? ProjectFile;

		/// <summary>
		/// Optional location of the project
		/// </summary>
		public string? ProjectDirectory;

		/// <summary>
		/// Root directory for the engine.  This is usually just EngineDirectory without the Engine directory.
		/// </summary>
		public string? RootDirectory;

		/// <summary>
		/// Directory to store the reference output
		/// </summary>
		public string ReferenceDirectory = string.Empty;

		/// <summary>
		/// Directory to store the verification output
		/// </summary>
		public string VerifyDirectory = string.Empty;

		/// <summary>
		/// Mode for generating and/or testing reference output
		/// </summary>
		public UhtReferenceMode ReferenceMode = UhtReferenceMode.None;

		/// <summary>
		/// If true, warnings are considered to be errors
		/// </summary>
		public bool bWarningsAsErrors = false;

		/// <summary>
		/// If true, include relative file paths in the log file
		/// </summary>
		public bool bRelativePathInLog = false;

		/// <summary>
		/// If true, use concurrent tasks to run UHT
		/// </summary>
		public bool bGoWide = true;

		/// <summary>
		/// If any output file mismatches existing outputs, an error will be generated
		/// </summary>
		public bool bFailIfGeneratedCodeChanges = false;

		/// <summary>
		/// If true, no output files will be saved
		/// </summary>
		public bool bNoOutput = false;

		/// <summary>
		/// If true, cull the output for any extra files
		/// </summary>
		public bool bCullOutput = true;

		/// <summary>
		/// If true, include extra output in code generation
		/// </summary>
		public bool bIncludeDebugOutput = false;

		/// <summary>
		/// If true, disable all exporters which would normally be run by default
		/// </summary>
		public bool bNoDefaultExporters = false;

		/// <summary>
		/// If true, cache any error messages until the end of processing.  This is used by the testing
		/// harness to generate more stable console output.
		/// </summary>
		public bool bCacheMessages = false;

		/// <summary>
		/// Collection of UHT tables
		/// </summary>
		public UhtTables? Tables;

		/// <summary>
		/// Configuration for the session
		/// </summary>
		public IUhtConfig? Config;
		#endregion

		/// <summary>
		/// Manifest file
		/// </summary>
		public UhtManifestFile? ManifestFile { get; set; } = null;

		/// <summary>
		/// Manifest data from the manifest file
		/// </summary>
		public UHTManifest? Manifest { get => this.ManifestFile != null ? this.ManifestFile.Manifest : null; }

		/// <summary>
		/// Collection of packages from the manifest
		/// </summary>
		public IReadOnlyList<UhtPackage> Packages { get => this.PackagesInternal; }

		/// <summary>
		/// Collection of header files from the manifest.  The header files will also appear as the children 
		/// of the packages
		/// </summary>
		public IReadOnlyList<UhtHeaderFile> HeaderFiles { get => this.HeaderFilesInternal; }

		/// <summary>
		/// Collection of header files topologically sorted.  This will not be populated until after header files
		/// are parsed and resolved.
		/// </summary>
		public IReadOnlyList<UhtHeaderFile> SortedHeaderFiles { get => this.SortedHeaderFilesInternal; }

		/// <summary>
		/// Dictionary of stripped file name to the header file
		/// </summary>
		public IReadOnlyDictionary<string, UhtHeaderFile> HeaderFileDictionary { get => this.HeaderFileDictionaryInternal; }

		/// <summary>
		/// After headers are parsed, returns the UObject class.
		/// </summary>
		public UhtClass UObject
		{
			get
			{
				if (this.UObjectInternal == null)
				{
					throw new UhtIceException("UObject was not defined.");
				}
				return this.UObjectInternal;
			}
		}

		/// <summary>
		/// After headers are parsed, returns the UClass class.
		/// </summary>
		public UhtClass UClass
		{
			get
			{
				if (this.UClassInternal == null)
				{
					throw new UhtIceException("UClass was not defined.");
				}
				return this.UClassInternal;
			}
		}

		/// <summary>
		/// After headers are parsed, returns the UInterface class.
		/// </summary>
		public UhtClass UInterface
		{
			get
			{
				if (this.UInterfaceInternal == null)
				{
					throw new UhtIceException("UInterface was not defined.");
				}
				return this.UInterfaceInternal;
			}
		}

		/// <summary>
		/// After headers are parsed, returns the IInterface class.
		/// </summary>
		public UhtClass IInterface
		{
			get
			{
				if (this.IInterfaceInternal == null)
				{
					throw new UhtIceException("IInterface was not defined.");
				}
				return this.IInterfaceInternal;
			}
		}

		/// <summary>
		/// After headers are parsed, returns the AActor class.  Unlike such properties as "UObject", there
		/// is no requirement for AActor to be defined.  May be null.
		/// </summary>
		public UhtClass? AActor = null;

		/// <summary>
		/// After headers are parsed, return the INotifyFieldValueChanged interface.  There is no requirement 
		/// that this interface be defined.
		/// </summary>
		public UhtClass? INotifyFieldValueChanged = null;

		private List<UhtPackage> PackagesInternal = new List<UhtPackage>();
		private List<UhtHeaderFile> HeaderFilesInternal = new List<UhtHeaderFile>();
		private List<UhtHeaderFile> SortedHeaderFilesInternal = new List<UhtHeaderFile>();
		private Dictionary<string, UhtHeaderFile> HeaderFileDictionaryInternal = new Dictionary<string, UhtHeaderFile>(StringComparer.OrdinalIgnoreCase);
		private long ErrorCountInternal = 0;
		private long WarningCountInternal = 0;
		private List<UhtMessage> Messages = new List<UhtMessage>();
		private Task? MessageTask = null;
		private Dictionary<string, UhtSourceFragment> SourceFragments = new Dictionary<string, UhtSourceFragment>();
		private UhtClass? UObjectInternal = null;
		private UhtClass? UClassInternal = null;
		private UhtClass? UInterfaceInternal = null;
		private UhtClass? IInterfaceInternal = null;
		private TypeCounter TypeCounterInternal = new TypeCounter();
		private TypeCounter PackageTypeCountInternal = new TypeCounter();
		private TypeCounter HeaderFileTypeCountInternal = new TypeCounter();
		private TypeCounter ObjectTypeCountInternal = new TypeCounter();
		private UhtSymbolTable SourceNameSymbolTable = new UhtSymbolTable(0);
		private UhtSymbolTable EngineNameSymbolTable = new UhtSymbolTable(0);
		private bool bSymbolTablePopulated = false;
		private Task? ReferenceDeleteTask = null;
		private Dictionary<string, bool> ExporterStates = new Dictionary<string, bool>(StringComparer.OrdinalIgnoreCase);
		private Dictionary<string, EnumAndValue> FullEnumValueLookup = new Dictionary<string, EnumAndValue>();
		private Dictionary<string, UhtEnum> ShortEnumValueLookup = new Dictionary<string, UhtEnum>();
		private JsonDocument? ProjectJson = null;

		/// <summary>
		/// The number of errors
		/// </summary>
		public long ErrorCount
		{
			get => Interlocked.Read(ref this.ErrorCountInternal);
		}

		/// <summary>
		/// The number of warnings
		/// </summary>
		public long WarningCount
		{
			get => Interlocked.Read(ref this.WarningCountInternal);
		}

		/// <summary>
		/// True if any errors have occurred or warnings if warnings are to be treated as errors 
		/// </summary>
		public bool bHasErrors
		{
			get => this.ErrorCount > 0 || (this.bWarningsAsErrors && this.WarningCount > 0);
		}

		#region IUHTMessageSession implementation
		/// <inheritdoc/>
		public IUhtMessageSession MessageSession => this;
		/// <inheritdoc/>
		public IUhtMessageSource? MessageSource => null;
		/// <inheritdoc/>
		public IUhtMessageLineNumber? MessageLineNumber => null;
		/// <inheritdoc/>
		public IUhtMessageExtraContext? MessageExtraContext => null;
		#endregion

		/// <summary>
		/// Return the index for a newly defined type
		/// </summary>
		/// <returns>New index</returns>
		public int GetNextTypeIndex()
		{
			return this.TypeCounterInternal.GetNext();
		}

		/// <summary>
		/// Return the number of types that have been defined.  This includes all types.
		/// </summary>
		public int TypeCount => this.TypeCounterInternal.Count;

		/// <summary>
		/// Return the index for a newly defined packaging
		/// </summary>
		/// <returns>New index</returns>
		public int GetNextHeaderFileTypeIndex()
		{
			return this.HeaderFileTypeCountInternal.GetNext();
		}

		/// <summary>
		/// Return the number of headers that have been defined
		/// </summary>
		public int HeaderFileTypeCount => this.HeaderFileTypeCountInternal.Count;

		/// <summary>
		/// Return the index for a newly defined package
		/// </summary>
		/// <returns>New index</returns>
		public int GetNextPackageTypeIndex()
		{
			return this.PackageTypeCountInternal.GetNext();
		}

		/// <summary>
		/// Return the number of UPackage types that have been defined
		/// </summary>
		public int PackageTypeCount => this.PackageTypeCountInternal.Count;

		/// <summary>
		/// Return the index for a newly defined object
		/// </summary>
		/// <returns>New index</returns>
		public int GetNextObjectTypeIndex()
		{
			return this.ObjectTypeCountInternal.GetNext();
		}

		/// <summary>
		/// Return the total number of UObject types that have been defined
		/// </summary>
		public int ObjectTypeCount => this.ObjectTypeCountInternal.Count;

		/// <summary>
		/// Return the collection of exporters
		/// </summary>
		public UhtExporterTable ExporterTable => this.Tables!.ExporterTable;

		/// <summary>
		/// Return the keyword table for the given table name
		/// </summary>
		/// <param name="TableName">Name of the table</param>
		/// <returns>The requested table</returns>
		public UhtKeywordTable GetKeywordTable(string TableName)
		{
			return this.Tables!.KeywordTables.Get(TableName);
		}

		/// <summary>
		/// Return the specifier table for the given table name
		/// </summary>
		/// <param name="TableName">Name of the table</param>
		/// <returns>The requested table</returns>
		public UhtSpecifierTable GetSpecifierTable(string TableName)
		{
			return this.Tables!.SpecifierTables.Get(TableName);
		}

		/// <summary>
		/// Return the specifier validator table for the given table name
		/// </summary>
		/// <param name="TableName">Name of the table</param>
		/// <returns>The requested table</returns>
		public UhtSpecifierValidatorTable GetSpecifierValidatorTable(string TableName)
		{
			return this.Tables!.SpecifierValidatorTables.Get(TableName);
		}

		/// <summary>
		/// Generate an error for the given unhandled keyword
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Token">Unhandled token</param>
		public void LogUnhandledKeywordError(IUhtTokenReader TokenReader, UhtToken Token)
		{
			this.Tables!.KeywordTables.LogUnhandledError(TokenReader, Token);
		}

		/// <summary>
		/// Test to see if the given class name is a property
		/// </summary>
		/// <param name="Name">Name of the class without the prefix</param>
		/// <returns>True if the class name is a property.  False if the class name isn't a property or isn't an engine class.</returns>
		public bool IsValidPropertyTypeName(StringView Name)
		{
			return this.Tables!.EngineClassTable.IsValidPropertyTypeName(Name);
		}

		/// <summary>
		/// Return the loc text default value associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="LocTextDefaultValue">Loc text default value handler</param>
		/// <returns></returns>
		public bool TryGetLocTextDefaultValue(StringView Name, out UhtLocTextDefaultValue LocTextDefaultValue)
		{
			return this.Tables!.LocTextDefaultValueTable.TryGet(Name, out LocTextDefaultValue);
		}

		/// <summary>
		/// Return the default processor
		/// </summary>
		public UhtPropertyType DefaultPropertyType => this.Tables!.PropertyTypeTable.Default;

		/// <summary>
		/// Return the property type associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="PropertyType">Property type if matched</param>
		/// <returns></returns>
		public bool TryGetPropertyType(StringView Name, out UhtPropertyType PropertyType)
		{
			return this.Tables!.PropertyTypeTable.TryGet(Name, out PropertyType);
		}

		/// <summary>
		/// Fetch the default sanitizer
		/// </summary>
		public UhtStructDefaultValue DefaultStructDefaultValue => this.Tables!.StructDefaultValueTable.Default;

		/// <summary>
		/// Return the structure default value associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="StructDefaultValue">Structure default value handler</param>
		/// <returns></returns>
		public bool TryGetStructDefaultValue(StringView Name, out UhtStructDefaultValue StructDefaultValue)
		{
			return this.Tables!.StructDefaultValueTable.TryGet(Name, out StructDefaultValue);
		}

		/// <summary>
		/// Run UHT on the given manifest.  Use the bHasError property to see if process was successful.
		/// </summary>
		/// <param name="ManifestFilePath">Path to the manifest file</param>
		public void Run(string ManifestFilePath)
		{
			if (this.FileManager == null)
			{
				Interlocked.Increment(ref this.ErrorCountInternal);
				Log.Logger.LogError("No file manager supplied, aborting.");
				return;
			}

			if (this.Config == null)
			{
				Interlocked.Increment(ref this.ErrorCountInternal);
				Log.Logger.LogError("No configuration supplied, aborting.");
				return;
			}

			if (this.Tables == null)
			{
				Interlocked.Increment(ref this.ErrorCountInternal);
				Log.Logger.LogError("No parsing tables supplied, aborting.");
				return;
			}

			switch (this.ReferenceMode)
			{
				case UhtReferenceMode.None:
					break;

				case UhtReferenceMode.Reference:
					if (string.IsNullOrEmpty(this.ReferenceDirectory))
					{
						Log.Logger.LogError("WRITEREF requested but directory not set, ignoring");
						this.ReferenceMode = UhtReferenceMode.None;
					}
					break;

				case UhtReferenceMode.Verify:
					if (string.IsNullOrEmpty(this.ReferenceDirectory) || string.IsNullOrEmpty(this.VerifyDirectory))
					{
						Log.Logger.LogError("VERIFYREF requested but directories not set, ignoring");
						this.ReferenceMode = UhtReferenceMode.None;
					}
					break;
			}

			if (this.ReferenceMode != UhtReferenceMode.None)
			{
				string DirectoryToDelete = this.ReferenceMode == UhtReferenceMode.Reference ? this.ReferenceDirectory : this.VerifyDirectory;
				this.ReferenceDeleteTask = Task.Factory.StartNew(() =>
				{
					try
					{
						Directory.Delete(DirectoryToDelete, true);
					}
					catch (Exception)
					{ }
				});
			}

			StepReadManifestFile(ManifestFilePath);
			StepPrepareModules();
			StepPrepareHeaders();
			StepParseHeaders();
			StepPopulateTypeTable();
			StepResolveInvalidCheck();
			StepResolveBases();
			StepResolveProperties();
			StepResolveFinal();
			StepResolveValidate();
			StepCollectReferences();
			TopologicalSortHeaderFiles();

			// If we are deleting the reference directory, then wait for that task to complete
			if (this.ReferenceDeleteTask != null)
			{
				Log.Logger.LogTrace("Step - Waiting for reference output to be cleared.");
				this.ReferenceDeleteTask.Wait();
			}

			StepExport();
		}

		/// <summary>
		/// Try the given action regardless of any prior errors.  If an exception occurs that doesn't have the required
		/// context, use the supplied context to generate the message.
		/// </summary>
		/// <param name="MessageSource">Message context for when the exception doesn't contain a context.</param>
		/// <param name="Action">The lambda to be invoked</param>
		public void TryAlways(IUhtMessageSource? MessageSource, Action Action)
		{
			try
			{
				Action();
			}
			catch (Exception E)
			{
				HandleException(MessageSource, E);
			}
		}

		/// <summary>
		/// Try the given action.  If an exception occurs that doesn't have the required
		/// context, use the supplied context to generate the message.
		/// </summary>
		/// <param name="MessageSource">Message context for when the exception doesn't contain a context.</param>
		/// <param name="Action">The lambda to be invoked</param>
		public void Try(IUhtMessageSource? MessageSource, Action Action)
		{
			if (!this.bHasErrors)
			{
				try
				{
					Action();
				}
				catch (Exception E)
				{
					HandleException(MessageSource, E);
				}
			}
		}

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="FilePath">Full or relative file path</param>
		/// <returns>Information about the read source</returns>
		public UhtSourceFragment ReadSource(string FilePath)
		{
			if (this.FileManager!.ReadSource(FilePath, out UhtSourceFragment Fragment))
			{
				return Fragment;
			}
			throw new UhtException($"File not found '{FilePath}'");
		}

		/// <summary>
		/// Read the given source file
		/// </summary>
		/// <param name="FilePath">Full or relative file path</param>
		/// <returns>Buffer containing the read data or null if not found.  The returned buffer must be returned to the cache via a call to UhtBuffer.Return</returns>
		public UhtBuffer? ReadSourceToBuffer(string FilePath)
		{
			return this.FileManager!.ReadOutput(FilePath);
		}

		/// <summary>
		/// Write the given contents to the file
		/// </summary>
		/// <param name="FilePath">Path to write to</param>
		/// <param name="Contents">Contents to write</param>
		/// <returns>True if the source was written</returns>
		internal bool WriteSource(string FilePath, ReadOnlySpan<char> Contents)
		{
			return this.FileManager!.WriteOutput(FilePath, Contents);
		}

		/// <summary>
		/// Rename the given file
		/// </summary>
		/// <param name="OldFilePath">Old file path name</param>
		/// <param name="NewFilePath">New file path name</param>
		public void RenameSource(string OldFilePath, string NewFilePath)
		{
			if (!this.FileManager!.RenameOutput(OldFilePath, NewFilePath))
			{
				new UhtSimpleFileMessageSite(this, NewFilePath).LogError($"Failed to rename exported file: '{OldFilePath}'");
			}
		}

		/// <summary>
		/// Given the name of a regular enum value, return the enum type
		/// </summary>
		/// <param name="Name">Enum value</param>
		/// <returns>Associated regular enum type or null if not found or enum isn't a regular enum.</returns>
		public UhtEnum? FindRegularEnumValue(string Name)
		{
			//COMPATIBILITY-TODO - See comment below on a more rebust version of the enum lookup
			//if (this.RegularEnumValueLookup.TryGetValue(Name, out UhtEnum? Enum))
			//{
			//	return Enum;
			//}
			if (this.FullEnumValueLookup.TryGetValue(Name, out EnumAndValue Value))
			{
				if (Value.Value != -1)
				{
					return Value.Enum;
				}
			}

			if (!Name.Contains("::") && this.ShortEnumValueLookup.TryGetValue(Name, out UhtEnum? Enum))
			{
				return Enum;
			}

			return null;
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="StartingType">Starting point for searches</param>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Name">Name of the type.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <param name="LineNumber">Source code line number requesting the lookup.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtType? StartingType, UhtFindOptions Options, string Name, IUhtMessageSite? MessageSite = null, int LineNumber = -1)
		{
			ValidateFindOptions(Options);

			UhtType? Type = FindTypeInternal(StartingType, Options, Name);
			if (Type == null && MessageSite != null)
			{
				FindTypeError(MessageSite, LineNumber, Options, Name);
			}
			return Type;
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="StartingType">Starting point for searches</param>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Name">Name of the type.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtType? StartingType, UhtFindOptions Options, ref UhtToken Name, IUhtMessageSite? MessageSite = null)
		{
			ValidateFindOptions(Options);

			UhtType? Type = FindTypeInternal(StartingType, Options, Name.Value.ToString());
			if (Type == null && MessageSite != null)
			{
				FindTypeError(MessageSite, Name.InputLine, Options, Name.Value.ToString());
			}
			return Type;
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="StartingType">If specified, this represents the starting type to use when searching base/owner chain for a match</param>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Identifiers">Enumeration of identifiers.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <param name="LineNumber">Source code line number requesting the lookup.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtType? StartingType, UhtFindOptions Options, UhtTokenList Identifiers, IUhtMessageSite? MessageSite = null, int LineNumber = -1)
		{
			ValidateFindOptions(Options);

			if (Identifiers.Next != null && Identifiers.Next.Next != null)
			{
				if (MessageSite != null)
				{
					MessageSite.LogError(LineNumber, "UnrealHeaderTool only supports C++ identifiers of two or less identifiers");
					return null;
				}
			}

			UhtType? Type = null;
			if (Identifiers.Next != null)
			{
				Type = FindTypeTwoNamesInternal(StartingType, Options, Identifiers.Token.Value.ToString(), Identifiers.Next.Token.Value.ToString());
			}
			else
			{
				Type = FindTypeInternal(StartingType, Options, Identifiers.Token.Value.ToString());
			}

			if (Type == null && MessageSite != null)
			{
				string FullIdentifier = Identifiers.Join("::");
				FindTypeError(MessageSite, LineNumber, Options, FullIdentifier);
			}
			return Type;
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="StartingType">If specified, this represents the starting type to use when searching base/owner chain for a match</param>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Identifiers">Enumeration of identifiers.</param>
		/// <param name="MessageSite">If supplied, then a error message will be generated if the type can not be found</param>
		/// <param name="LineNumber">Source code line number requesting the lookup.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindType(UhtType? StartingType, UhtFindOptions Options, UhtToken[] Identifiers, IUhtMessageSite? MessageSite = null, int LineNumber = -1)
		{
			ValidateFindOptions(Options);

			if (Identifiers.Length == 0)
			{
				throw new UhtIceException("Empty identifier array");
			}
			if (Identifiers.Length > 2)
			{
				if (MessageSite != null)
				{
					MessageSite.LogError(LineNumber, "UnrealHeaderTool only supports C++ identifiers of two or less identifiers");
					return null;
				}
			}

			UhtType? Type = null;
			if (Identifiers.Length == 0)
			{
				Type = FindTypeTwoNamesInternal(StartingType, Options, Identifiers[0].Value.ToString(), Identifiers[1].Value.ToString());
			}
			else
			{
				Type = FindTypeInternal(StartingType, Options, Identifiers[0].Value.ToString());
			}

			if (Type == null && MessageSite != null)
			{
				string FullIdentifier = string.Join("::", Identifiers);
				FindTypeError(MessageSite, LineNumber, Options, FullIdentifier);
			}
			return Type;
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="StartingType">Starting point for searches</param>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="FirstName">First name of the type.</param>
		/// <param name="SecondName">Second name used by delegates in classes and namespace enumerations</param>
		/// <returns>The located type of null if not found</returns>
		private UhtType? FindTypeTwoNamesInternal(UhtType? StartingType, UhtFindOptions Options, string FirstName, string SecondName)
		{
			// If we have two names
			if (SecondName.Length > 0)
			{
				if (Options.HasAnyFlags(UhtFindOptions.DelegateFunction | UhtFindOptions.Enum))
				{
					UhtFindOptions SubOptions = UhtFindOptions.NoParents | (Options & ~UhtFindOptions.TypesMask) | (Options & UhtFindOptions.Enum);
					if (Options.HasAnyFlags(UhtFindOptions.DelegateFunction))
					{
						SubOptions |= UhtFindOptions.Class;
					}
					UhtType? Type = FindTypeInternal(StartingType, SubOptions, FirstName);
					if (Type == null)
					{
						return null;
					}
					if (Type is UhtEnum)
					{
						return Type;
					}
					if (Type is UhtClass)
					{
						return FindTypeInternal(StartingType, UhtFindOptions.DelegateFunction | UhtFindOptions.NoParents | (Options & ~UhtFindOptions.TypesMask), SecondName);
					}
				}

				// We can't match anything at this point
				return null;
			}

			// Perform the lookup for just a single name
			return FindTypeInternal(StartingType, Options, FirstName);
		}

		/// <summary>
		/// Find the given type in the type hierarchy
		/// </summary>
		/// <param name="StartingType">Starting point for searches</param>
		/// <param name="Options">Options controlling what is searched</param>
		/// <param name="Name">Name of the type.</param>
		/// <returns>The located type of null if not found</returns>
		public UhtType? FindTypeInternal(UhtType? StartingType, UhtFindOptions Options, string Name)
		{
			UhtType? Type = null;
			if (Options.HasAnyFlags(UhtFindOptions.EngineName))
			{
				if (Options.HasAnyFlags(UhtFindOptions.CaseCompare))
				{
					Type = this.EngineNameSymbolTable.FindCasedType(StartingType, Options, Name);
				}
				else
				{
					Type = this.EngineNameSymbolTable.FindCaselessType(StartingType, Options, Name);
				}
			}
			else if (Options.HasAnyFlags(UhtFindOptions.SourceName))
			{
				if (Options.HasAnyFlags(UhtFindOptions.CaselessCompare))
				{
					Type = this.SourceNameSymbolTable.FindCaselessType(StartingType, Options, Name);
				}
				else
				{
					Type = this.SourceNameSymbolTable.FindCasedType(StartingType, Options, Name);
				}
			}
			else
			{
				throw new UhtIceException("Either EngineName or SourceName must be specified in the options");
			}
			return Type;
		}

		/// <summary>
		/// Verify that the options are valid.  Will also check to make sure the symbol table has been populated.
		/// </summary>
		/// <param name="Options">Find options</param>
		private void ValidateFindOptions(UhtFindOptions Options)
		{
			if (!Options.HasAnyFlags(UhtFindOptions.EngineName | UhtFindOptions.SourceName))
			{
				throw new UhtIceException("Either EngineName or SourceName must be specified in the options");
			}

			if (Options.HasAnyFlags(UhtFindOptions.CaseCompare) && Options.HasAnyFlags(UhtFindOptions.CaselessCompare))
			{
				throw new UhtIceException("Both CaseCompare and CaselessCompare can't be specified as FindType options");
			}

			UhtFindOptions TypeOptions = Options & UhtFindOptions.TypesMask;
			if (TypeOptions == 0)
			{
				throw new UhtIceException("No type options specified");
			}

			if (!this.bSymbolTablePopulated)
			{
				throw new UhtIceException("Symbol table has not been populated, don't call FindType until headers are parsed.");
			}
		}

		/// <summary>
		/// Generate an error message for when a given symbol wasn't found.  The text will contain the list of types that the symbol must be
		/// </summary>
		/// <param name="MessageSite">Destination for the message</param>
		/// <param name="LineNumber">Line number generating the error</param>
		/// <param name="Options">Collection of required types</param>
		/// <param name="Name">The name of the symbol</param>
		private static void FindTypeError(IUhtMessageSite MessageSite, int LineNumber, UhtFindOptions Options, string Name)
		{
			List<string> Types = new List<string>();
			if (Options.HasAnyFlags(UhtFindOptions.Enum))
			{
				Types.Add("'enum'");
			}
			if (Options.HasAnyFlags(UhtFindOptions.ScriptStruct))
			{
				Types.Add("'struct'");
			}
			if (Options.HasAnyFlags(UhtFindOptions.Class))
			{
				Types.Add("'class'");
			}
			if (Options.HasAnyFlags(UhtFindOptions.DelegateFunction))
			{
				Types.Add("'delegate'");
			}
			if (Options.HasAnyFlags(UhtFindOptions.Function))
			{
				Types.Add("'function'");
			}
			if (Options.HasAnyFlags(UhtFindOptions.Property))
			{
				Types.Add("'property'");
			}

			MessageSite.LogError(LineNumber, $"Unable to find {UhtUtilities.MergeTypeNames(Types, "or")} with name '{Name}'");
		}

		/// <summary>
		/// Search for the given header file by just the file name
		/// </summary>
		/// <param name="Name">Name to be found</param>
		/// <returns></returns>
		public UhtHeaderFile? FindHeaderFile(string Name)
		{
			UhtHeaderFile? HeaderFile;
			if (this.HeaderFileDictionaryInternal.TryGetValue(Name, out HeaderFile))
			{
				return HeaderFile;
			}
			return null;
		}

		#region IUHTMessageSource implementation
		/// <summary>
		/// Add a message to the collection of output messages
		/// </summary>
		/// <param name="Message">Message being added</param>
		public void AddMessage(UhtMessage Message)
		{
			lock (this.Messages)
			{
				this.Messages.Add(Message);
				
				// If we aren't caching messages and this is the first message,
				// start a task to flush the messages.
				if (!this.bCacheMessages && this.Messages.Count == 1)
				{
					this.MessageTask = Task.Factory.StartNew(() => FlushMessages());
				}
			}

			switch (Message.MessageType)
			{
				case UhtMessageType.Error:
				case UhtMessageType.Ice:
					Interlocked.Increment(ref this.ErrorCountInternal);
					break;

				case UhtMessageType.Warning:
					Interlocked.Increment(ref this.WarningCountInternal);
					break;

				case UhtMessageType.Info:
				case UhtMessageType.Trace:
					break;
			}
		}
		#endregion

		/// <summary>
		/// Log all the collected messages to the log/console.  If messages aren't being
		/// cached, then this just waits until the flush task has completed.  If messages
		/// are being cached, they are sorted by file name and line number to ensure the 
		/// output is stable.
		/// </summary>
		public void LogMessages()
		{
			if (this.MessageTask != null)
			{
				this.MessageTask.Wait();
			}

			foreach (UhtMessage Message in FetchOrderedMessages())
			{
				LogMessage(Message);
			}
		}

		/// <summary>
		/// Flush all pending messages to the logger
		/// </summary>
		private void FlushMessages()
		{
			UhtMessage[]? MessageArray = null;
			lock (this.Messages)
			{
				MessageArray = this.Messages.ToArray();
				this.Messages.Clear();
			}

			foreach (UhtMessage Message in MessageArray)
			{
				LogMessage(Message);
			}
		}

		/// <summary>
		/// Log the given message
		/// </summary>
		/// <param name="Message">The message to be logged</param>
		private void LogMessage(UhtMessage Message)
		{
			string FormattedMessage = FormatMessage(Message);
			LogLevel LogLevel = LogLevel.Information;
			switch (Message.MessageType)
			{
				default:
				case UhtMessageType.Error:
				case UhtMessageType.Ice:
					LogLevel = LogLevel.Error;
					break;

				case UhtMessageType.Warning:
					LogLevel = LogLevel.Warning;
					break;

				case UhtMessageType.Info:
					LogLevel = LogLevel.Information;
					break;

				case UhtMessageType.Trace:
					LogLevel = LogLevel.Trace;
					break;
			}

			Log.Logger.Log(LogLevel, "{0}", FormattedMessage);
		}

		/// <summary>
		/// Return all of the messages into a list
		/// </summary>
		/// <returns>List of all the messages</returns>
		public List<string> CollectMessages()
		{
			List<string> Out = new List<string>();
			foreach (UhtMessage Message in FetchOrderedMessages())
			{
				Out.Add(FormatMessage(Message));
			}
			return Out;
		}

		/// <summary>
		/// Given an existing and a new instance, replace the given type in the symbol table.
		/// This is used by the property resolution system to replace properties created during
		/// the parsing phase that couldn't be resoled until after all headers are parsed.
		/// </summary>
		/// <param name="OldType"></param>
		/// <param name="NewType"></param>
		public void ReplaceTypeInSymbolTable(UhtType OldType, UhtType NewType)
		{
			this.SourceNameSymbolTable.Replace(OldType, NewType, OldType.SourceName);
			if (OldType.EngineType.HasEngineName())
			{
				this.EngineNameSymbolTable.Replace(OldType, NewType, OldType.EngineName);
			}
		}

		/// <summary>
		/// Return an ordered enumeration of all messages.
		/// </summary>
		/// <returns>Enumerator</returns>
		private IOrderedEnumerable<UhtMessage> FetchOrderedMessages()
		{
			List<UhtMessage> Messages = new List<UhtMessage>();
			lock (this.Messages)
			{
				Messages.AddRange(this.Messages);
				this.Messages.Clear();
			}
			return Messages.OrderBy(Context => Context.FilePath).ThenBy(Context => Context.LineNumber + Context.MessageSource?.MessageFragmentLineNumber);
		}

		/// <summary>
		/// Format the given message
		/// </summary>
		/// <param name="Message">Message to be formatted</param>
		/// <returns>Text of the formatted message</returns>
		private string FormatMessage(UhtMessage Message)
		{
			string FilePath;
			string FragmentPath = "";
			int LineNumber = Message.LineNumber;
			if (Message.FilePath != null)
			{
				FilePath = Message.FilePath;
			}
			else if (Message.MessageSource != null)
			{
				if (Message.MessageSource.bMessageIsFragment)
				{
					if (this.bRelativePathInLog)
					{
						FilePath = Message.MessageSource.MessageFragmentFilePath;
					}
					else
					{
						FilePath = Message.MessageSource.MessageFragmentFullFilePath;
					}
					FragmentPath = $"[{Message.MessageSource.MessageFilePath}]";
					LineNumber += Message.MessageSource.MessageFragmentLineNumber;
				}
				else
				{
					if (this.bRelativePathInLog)
					{
						FilePath = Message.MessageSource.MessageFilePath;
					}
					else
					{
						FilePath = Message.MessageSource.MessageFullFilePath;
					}
				}
			}
			else
			{
				FilePath = "UnknownSource";
			}

			switch (Message.MessageType)
			{
				case UhtMessageType.Error:
					return $"{FilePath}({LineNumber}){FragmentPath}: Error: {Message.Message}";
				case UhtMessageType.Warning:
					return $"{FilePath}({LineNumber}){FragmentPath}: Warning: {Message.Message}";
				case UhtMessageType.Info:
					return $"{FilePath}({LineNumber}){FragmentPath}: Info: {Message.Message}";
				case UhtMessageType.Trace:
					return $"{FilePath}({LineNumber}){FragmentPath}: Trace: {Message.Message}";
				default:
				case UhtMessageType.Ice:
					return $"{FilePath}({LineNumber}){FragmentPath}:  Error: Internal Compiler Error - {Message.Message}";
			}
		}

		/// <summary>
		/// Handle the given exception with the provided message context
		/// </summary>
		/// <param name="MessageSource">Context for the exception.  Required to handled all exceptions other than UHTException</param>
		/// <param name="E">Exception being handled</param>
		private void HandleException(IUhtMessageSource? MessageSource, Exception E)
		{
			switch (E)
			{
				case UhtException UHTException:
					UhtMessage Message = UHTException.UhtMessage;
					if (Message.MessageSource == null)
					{
						Message.MessageSource = MessageSource;
					}
					AddMessage(Message);
					break;

				case JsonException JsonException:
					AddMessage(UhtMessage.MakeMessage(UhtMessageType.Error, MessageSource, null, (int)(JsonException.LineNumber + 1 ?? 1), JsonException.Message));
					break;

				default:
					//Log.TraceInformation("{0}", E.StackTrace);
					AddMessage(UhtMessage.MakeMessage(UhtMessageType.Ice, MessageSource, null, 1, $"{E.GetType().ToString()} - {E.Message}"));
					break;
			}
		}

		/// <summary>
		/// Return the normalized path converted to a full path if possible. 
		/// Code should NOT depend on a full path being returned.
		/// 
		/// In general, it is assumed that during normal UHT, all paths are already full paths.
		/// Only the test harness deals in relative paths.
		/// </summary>
		/// <param name="FilePath">Path to normalize</param>
		/// <returns>Normalized path possibly converted to a full path.</returns>
		private string GetNormalizedFullFilePath(string FilePath)
		{
			return NormalizePath(this.FileManager!.GetFullFilePath(FilePath));
		}

		private string NormalizePath(string FilePath)
		{
			return FilePath.Replace('\\', '/');
		}

		private UhtHeaderFileParser? ParseHeaderFile(UhtHeaderFile HeaderFile)
		{
			UhtHeaderFileParser? Parser = null;
			TryAlways(HeaderFile.MessageSource, () =>
			{
				HeaderFile.Read();
				Parser = UhtHeaderFileParser.Parse(HeaderFile);
			});
			return Parser;
		}

		#region Run steps
		private void StepReadManifestFile(string ManifestFilePath)
		{
			this.ManifestFile = new UhtManifestFile(this, ManifestFilePath);

			Try(this.ManifestFile.MessageSource, () =>
			{
				Log.Logger.LogTrace("Step - Read Manifest File");

				this.ManifestFile.Read();

				if (this.Manifest != null && this.Tables != null)
				{
					this.Tables.AddPlugins(this.Manifest.UhtPlugins);
				}
			});
		}

		private void StepPrepareModules()
		{
			if (this.ManifestFile == null || this.ManifestFile.Manifest == null)
			{
				return;
			}

			Try(this.ManifestFile.MessageSource, () =>
			{
				Log.Logger.LogTrace("Step - Prepare Modules");

				foreach (UHTManifest.Module Module in this.ManifestFile.Manifest.Modules)
				{
					EPackageFlags PackageFlags = EPackageFlags.ContainsScript | EPackageFlags.Compiling;

					switch (Module.OverrideModuleType)
					{
						case EPackageOverrideType.None:
							switch (Module.ModuleType)
							{
								case UHTModuleType.GameEditor:
								case UHTModuleType.EngineEditor:
									PackageFlags |= EPackageFlags.EditorOnly;
									break;

								case UHTModuleType.GameDeveloper:
								case UHTModuleType.EngineDeveloper:
									PackageFlags |= EPackageFlags.Developer;
									break;

								case UHTModuleType.GameUncooked:
								case UHTModuleType.EngineUncooked:
									PackageFlags |= EPackageFlags.UncookedOnly;
									break;
							}
							break;

						case EPackageOverrideType.EditorOnly:
							PackageFlags |= EPackageFlags.EditorOnly;
							break;

						case EPackageOverrideType.EngineDeveloper:
						case EPackageOverrideType.GameDeveloper:
							PackageFlags |= EPackageFlags.Developer;
							break;

						case EPackageOverrideType.EngineUncookedOnly:
						case EPackageOverrideType.GameUncookedOnly:
							PackageFlags |= EPackageFlags.UncookedOnly;
							break;
					}

					UhtPackage Package = new UhtPackage(this, Module, PackageFlags);
					this.PackagesInternal.Add(Package);
				}
			});
		}

		private void StepPrepareHeaders(UhtPackage Package, IEnumerable<string> HeaderFiles, UhtHeaderFileType HeaderFileType)
		{
			if (Package.Module == null)
			{
				return;
			}

			string TypeDirectory = HeaderFileType.ToString() + '/';
			string NormalizedModuleBaseFullFilePath = GetNormalizedFullFilePath(Package.Module.BaseDirectory);
			foreach (string HeaderFilePath in HeaderFiles)
			{

				// Make sure this isn't a duplicate
				string NormalizedFullFilePath = GetNormalizedFullFilePath(HeaderFilePath);
				string FileName = Path.GetFileName(NormalizedFullFilePath);
				UhtHeaderFile? ExistingHeaderFile;
				if (HeaderFileDictionaryInternal.TryGetValue(FileName, out ExistingHeaderFile) && ExistingHeaderFile != null)
				{
					string NormalizedExistingFullFilePath = GetNormalizedFullFilePath(ExistingHeaderFile.FilePath);
					if (string.Compare(NormalizedFullFilePath, NormalizedExistingFullFilePath, true) != 0)
					{
						IUhtMessageSite Site = (IUhtMessageSite?)this.ManifestFile ?? this;
						Site.LogError($"Two headers with the same name is not allowed. '{HeaderFilePath}' conflicts with '{ExistingHeaderFile.FilePath}'");
						continue;
					}
				}

				// Create the header file and add to the collections
				UhtHeaderFile HeaderFile = new UhtHeaderFile(Package, HeaderFilePath);
				HeaderFile.HeaderFileType = HeaderFileType;
				HeaderFilesInternal.Add(HeaderFile);
				HeaderFileDictionaryInternal.Add(FileName, HeaderFile);
				Package.AddChild(HeaderFile);

				// Save metadata for the class path, both for it's include path and relative to the module base directory
				if (NormalizedFullFilePath.StartsWith(NormalizedModuleBaseFullFilePath, true, null))
				{
					int StripLength = NormalizedModuleBaseFullFilePath.Length;
					if (StripLength < NormalizedFullFilePath.Length && NormalizedFullFilePath[StripLength] == '/')
					{
						++StripLength;
					}

					HeaderFile.ModuleRelativeFilePath = NormalizedFullFilePath.Substring(StripLength);

					if (NormalizedFullFilePath.Substring(StripLength).StartsWith(TypeDirectory, true, null))
					{
						StripLength += TypeDirectory.Length;
					}

					HeaderFile.IncludeFilePath = NormalizedFullFilePath.Substring(StripLength);
				}
			}
		}

		private void StepPrepareHeaders()
		{
			if (this.ManifestFile == null)
			{
				return;
			}

			Try(this.ManifestFile.MessageSource, () =>
			{
				Log.Logger.LogTrace("Step - Prepare Headers");

				foreach (UhtPackage Package in this.PackagesInternal)
				{
					if (Package.Module != null)
					{
						StepPrepareHeaders(Package, Package.Module.ClassesHeaders, UhtHeaderFileType.Classes);
						StepPrepareHeaders(Package, Package.Module.PublicHeaders, UhtHeaderFileType.Public);
						StepPrepareHeaders(Package, Package.Module.InternalHeaders, UhtHeaderFileType.Internal);
						StepPrepareHeaders(Package, Package.Module.PrivateHeaders, UhtHeaderFileType.Private);
					}
				}

				// Locate the NoExportTypes.h file and add it to every other header file
				if (this.HeaderFileDictionaryInternal.TryGetValue("NoExportTypes.h", out UhtHeaderFile? NoExportTypes))
				{
					foreach (UhtPackage Package in this.PackagesInternal)
					{
						foreach (UhtHeaderFile HeaderFile in Package.Children)
						{
							if (HeaderFile != NoExportTypes)
							{
								HeaderFile.AddReferencedHeader(NoExportTypes);
							}
						}
					}
				}

			});
		}

		private void StepParseHeaders()
		{
			if (this.bHasErrors)
			{
				return;
			}

			Log.Logger.LogTrace("Step - Parse Headers");

			if (this.bGoWide)
			{
				Parallel.ForEach(this.HeaderFilesInternal, HeaderFile =>
				{
					ParseHeaderFile(HeaderFile);
				});
			}
			else
			{
				foreach (UhtHeaderFile HeaderFile in this.HeaderFilesInternal)
				{
					ParseHeaderFile(HeaderFile);
				}
			}
		}

		private void StepPopulateTypeTable()
		{
			Try(null, () =>
			{
				Log.Logger.LogTrace("Step - Populate symbol table");

				this.SourceNameSymbolTable = new UhtSymbolTable(this.TypeCount);
				this.EngineNameSymbolTable = new UhtSymbolTable(this.TypeCount);

				PopulateSymbolTable();

				this.UObjectInternal = (UhtClass?)FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, "UObject");
				this.UClassInternal = (UhtClass?)FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, "UClass");
				this.UInterfaceInternal = (UhtClass?)FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, "UInterface");
				this.IInterfaceInternal = (UhtClass?)FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, "IInterface");
				this.AActor = (UhtClass?)FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, "AActor");
				this.INotifyFieldValueChanged = (UhtClass?)FindType(null, UhtFindOptions.SourceName | UhtFindOptions.Class, "INotifyFieldValueChanged");
			});
		}

		private void StepResolveBases()
		{
			StepForAllHeaders(HeaderFile => Resolve(HeaderFile, UhtResolvePhase.Bases));
		}

		private void StepResolveInvalidCheck()
		{
			StepForAllHeaders(HeaderFile => Resolve(HeaderFile, UhtResolvePhase.InvalidCheck));
		}

		private void StepResolveProperties()
		{
			StepForAllHeaders(HeaderFile => Resolve(HeaderFile, UhtResolvePhase.Properties));
		}

		private void StepResolveFinal()
		{
			StepForAllHeaders(HeaderFile => Resolve(HeaderFile, UhtResolvePhase.Final));
		}

		private void StepResolveValidate()
		{
			StepForAllHeaders(HeaderFile => UhtType.ValidateType(HeaderFile, UhtValidationOptions.None));
		}

		private void StepCollectReferences()
		{
			StepForAllHeaders(HeaderFile =>
			{
				foreach (UhtType Child in HeaderFile.Children)
				{
					Child.CollectReferences(HeaderFile.References);
				}
				foreach (UhtHeaderFile RefHeaderFile in HeaderFile.References.ReferencedHeaders)
				{
					HeaderFile.AddReferencedHeader(RefHeaderFile);
				}
			});
		}

		private void Resolve(UhtHeaderFile HeaderFile, UhtResolvePhase ResolvePhase)
		{
			TryAlways(HeaderFile.MessageSource, () =>
			{
				HeaderFile.Resolve(ResolvePhase);
			});
		}

		private delegate void StepDelegate(UhtHeaderFile HeaderFile);

		private void StepForAllHeaders(StepDelegate Delegate)
		{
			if (this.bHasErrors)
			{
				return;
			}

			if (this.bGoWide)
			{
				Parallel.ForEach(this.HeaderFilesInternal, HeaderFile =>
				{
					Delegate(HeaderFile);
				});
			}
			else
			{
				foreach (UhtHeaderFile HeaderFile in this.HeaderFilesInternal)
				{
					Delegate(HeaderFile);
				}
			}
		}
		#endregion

		#region Symbol table initialization
		private void PopulateSymbolTable()
		{
			foreach (UhtHeaderFile HeaderFile in this.HeaderFilesInternal)
			{
				AddTypeToSymbolTable(HeaderFile);
			}
			this.bSymbolTablePopulated = true;
		}

		private void AddTypeToSymbolTable(UhtType Type)
		{
			UhtEngineType EngineExtendedType = Type.EngineType;

			if (Type is UhtEnum Enum)
			{
				//COMPATIBILITY-TODO: We can get more reliable results by only adding regular enums to the table
				// and then in the lookup code in the property system to look for the '::' and just lookup
				// the raw enum name.  In UHT we only care about the enum and not the value.
				//
				// The current algorithm has issues with two cases:
				//
				//		EnumNamespaceName::EnumTypeName::Value - Where the enum type name is included with a namespace enum
				//		EnumName::Value - Where the value is defined in terms that can't be parsed.  The -1 check causes it
				//			to be kicked out.
				//if (Enum.CppForm == UhtEnumCppForm.Regular)
				//{
				//	foreach (UhtEnumValue Value in Enum.EnumValues)
				//	{
				//		this.RegularEnumValueLookup.Add(Value.Name.ToString(), Enum);
				//	}
				//}
				bool bAddShortNames = Enum.CppForm == UhtEnumCppForm.Namespaced || Enum.CppForm == UhtEnumCppForm.EnumClass;
				string CheckName = $"{Enum.SourceName}::";
				foreach (UhtEnumValue Value in Enum.EnumValues)
				{
					this.FullEnumValueLookup.Add(Value.Name, new EnumAndValue { Enum = Enum, Value = Value.Value });
					if (bAddShortNames)
					{
						if (Value.Name.StartsWith(CheckName))
						{
							this.ShortEnumValueLookup.TryAdd(Value.Name.Substring(CheckName.Length), Enum);
						}
					}
				}
			}

			if (EngineExtendedType.FindOptions() != 0)
			{
				if (EngineExtendedType.MustNotBeReserved())
				{
					if (ReservedNames.Contains(Type.EngineName))
					{
						Type.HeaderFile.LogError(Type.LineNumber, $"{EngineExtendedType.CapitalizedText()} '{Type.EngineName}' uses a reserved type name.");
					}
				}

				if (EngineExtendedType.HasEngineName() && EngineExtendedType.MustBeUnique())
				{
					UhtType? ExistingType = this.EngineNameSymbolTable.FindCaselessType(null, EngineExtendedType.MustBeUniqueFindOptions(), Type.EngineName);
					if (ExistingType != null)
					{
						Type.HeaderFile.LogError(Type.LineNumber, string.Format("{0} '{1}' shares engine name '{2}' with {6} '{3}' in {4}(5)",
							EngineExtendedType.CapitalizedText(), Type.SourceName, Type.EngineName, ExistingType.SourceName,
							ExistingType.HeaderFile.FilePath, ExistingType.LineNumber, ExistingType.EngineType.LowercaseText()));
					}
				}

				if (Type.bVisibleType)
				{
					this.SourceNameSymbolTable.Add(Type, Type.SourceName);
					if (EngineExtendedType.HasEngineName())
					{
						this.EngineNameSymbolTable.Add(Type, Type.EngineName);
					}
				}
			}

			if (EngineExtendedType.AddChildrenToSymbolTable())
			{
				foreach (UhtType Child in Type.Children)
				{
					AddTypeToSymbolTable(Child);
				}
			}
		}
		#endregion

		#region Topological sort of the header files
		private enum TopologicalState
		{
			Unmarked,
			Temporary,
			Permanent,
		}

		private void TopologicalRecursion(List<TopologicalState> States, UhtHeaderFile First, UhtHeaderFile Visit)
		{
			foreach (UhtHeaderFile Referenced in Visit.ReferencedHeadersNoLock)
			{
				if (States[Referenced.HeaderFileTypeIndex] == TopologicalState.Temporary)
				{
					First.LogError($"'{Visit.FilePath}' includes/requires '{Referenced.FilePath}'");
					if (First != Referenced)
					{
						TopologicalRecursion(States, First, Referenced);
					}
					break;
				}
			}
		}

		private UhtHeaderFile? TopologicalVisit(List<TopologicalState> States, UhtHeaderFile Visit)
		{
			switch (States[Visit.HeaderFileTypeIndex])
			{
				case TopologicalState.Unmarked:
					UhtHeaderFile? Recursion = null;
					States[Visit.HeaderFileTypeIndex] = TopologicalState.Temporary;
					foreach (UhtHeaderFile Referenced in Visit.ReferencedHeadersNoLock)
					{
						if (Visit != Referenced)
						{
							UhtHeaderFile? Out = TopologicalVisit(States, Referenced);
							if (Out != null)
							{
								Recursion = Out;
								break;
							}
						}
					}
					States[Visit.HeaderFileTypeIndex] = TopologicalState.Permanent;
					this.SortedHeaderFilesInternal.Add(Visit);
					return null;

				case TopologicalState.Temporary:
					return Visit;

				case TopologicalState.Permanent:
					return null;

				default:
					throw new UhtIceException("Unknown topological state");
			}
		}

		private void TopologicalSortHeaderFiles()
		{
			Try(null, () =>
			{
				Log.Logger.LogTrace("Step - Topological Sort Header Files");

				// Initialize a scratch table for topological states
				this.SortedHeaderFilesInternal.Capacity = this.HeaderFileTypeCount;
				List<TopologicalState> States = new List<TopologicalState>(this.HeaderFileTypeCount);
				for (int Index = 0; Index < this.HeaderFileTypeCount; ++Index)
				{
					States.Add(TopologicalState.Unmarked);
				}

				foreach (UhtHeaderFile HeaderFile in this.HeaderFiles)
				{
					if (States[HeaderFile.HeaderFileTypeIndex] == TopologicalState.Unmarked)
					{
						UhtHeaderFile? Recursion = TopologicalVisit(States, HeaderFile);
						if (Recursion != null)
						{
							HeaderFile.LogError("Circular dependency detected:");
							TopologicalRecursion(States, Recursion, Recursion);
							return;
						}
					}
				}
			});
		}
		#endregion

		#region Validation helpers
		private HashSet<UhtScriptStruct> ScriptStructsValidForNet = new HashSet<UhtScriptStruct>();

		/// <summary>
		/// Validate that the given referenced script structure is valid for network operations.  If the structure
		/// is valid, then the result will be cached.  It not valid, errors will be generated each time the structure
		/// is referenced.
		/// </summary>
		/// <param name="ReferencingProperty">The property referencing a structure</param>
		/// <param name="ReferencedScriptStruct">The script structure being referenced</param>
		/// <returns></returns>
		public bool ValidateScriptStructOkForNet(UhtProperty ReferencingProperty, UhtScriptStruct ReferencedScriptStruct)
		{

			// Check for existing value
			lock (this.ScriptStructsValidForNet)
			{
				if (this.ScriptStructsValidForNet.Contains(ReferencedScriptStruct))
				{
					return true;
				}
			}

			bool bIsStructValid = true;

			// Check the super chain structure
			UhtScriptStruct? SuperScriptStruct = ReferencedScriptStruct.SuperScriptStruct;
			if (SuperScriptStruct != null)
			{
				if (!ValidateScriptStructOkForNet(ReferencingProperty, SuperScriptStruct))
				{
					bIsStructValid = false;
				}
			}

			// Check the structure properties
			foreach (UhtProperty Property in ReferencedScriptStruct.Properties)
			{
				if (!Property.ValidateStructPropertyOkForNet(ReferencingProperty))
				{
					bIsStructValid = false;
					break;
				}
			}

			// Save the results
			if (bIsStructValid)
			{
				lock (this.ScriptStructsValidForNet)
				{
					this.ScriptStructsValidForNet.Add(ReferencedScriptStruct);
				}
			}
			return bIsStructValid;
		}
		#endregion

		#region Exporting

		/// <summary>
		/// Enable/Disable an exporter.  This overrides the default state of the exporter.
		/// </summary>
		/// <param name="Name">Name of the exporter</param>
		/// <param name="Enabled">If true, the exporter is to be enabled</param>
		public void SetExporterStatus(string Name, bool Enabled)
		{
			this.ExporterStates[Name] = Enabled;
		}

		/// <summary>
		/// Test to see if the given exporter plugin is enabled.
		/// </summary>
		/// <param name="PluginName">Name of the plugin</param>
		/// <param name="IncludeTargetCheck">If true, include a target check</param>
		/// <returns>True if enabled</returns>
		public bool IsPluginEnabled(string PluginName, bool IncludeTargetCheck)
		{
			if (this.ProjectJson == null && this.ProjectDirectory != null && this.ProjectFile != null)
			{
				UhtBuffer? Contents = this.ReadSourceToBuffer(this.ProjectFile);
				if (Contents != null)
				{
					this.ProjectJson = JsonDocument.Parse(Contents.Memory);
					UhtBuffer.Return(Contents);
				}
			}

			if (this.ProjectJson == null)
			{
				return false;
			}

			JsonObject RootObject = new JsonObject(this.ProjectJson.RootElement);
			if (RootObject.TryGetObjectArrayField("Plugins", out JsonObject[]? Plugins))
			{
				foreach (JsonObject Plugin in Plugins)
				{
					if (!Plugin.TryGetStringField("Name", out string? TestPluginName) || string.Compare(PluginName, TestPluginName, StringComparison.OrdinalIgnoreCase) != 0)
					{
						continue;
					}
					if (!Plugin.TryGetBoolField("Enabled", out bool Enabled) || !Enabled)
					{
						return false;
					}
					if (IncludeTargetCheck && this.Manifest != null)
					{
						if (Plugin.TryGetStringArrayField("TargetAllowList", out string[]? AllowList))
						{
							if (AllowList.Contains(this.Manifest.TargetName, StringComparer.OrdinalIgnoreCase))
							{
								return true;
							}
						}
						if (Plugin.TryGetStringArrayField("TargetDenyList", out string[]? DenyList))
						{
							if (DenyList.Contains(this.Manifest.TargetName, StringComparer.OrdinalIgnoreCase))
							{
								return false;
							}
						}
					}
					return true;
				}
			}
			return false;
		}

		private void StepExport()
		{
			HashSet<string> ExternalDependencies = new HashSet<string>();
			long TotalWrittenFiles = 0;
			Try(null, () =>
			{
				Log.Logger.LogTrace("Step - Exports");

				foreach (UhtExporter Exporter in this.ExporterTable)
				{
					bool Run = false;
					if (!this.ExporterStates.TryGetValue(Exporter.Name, out Run))
					{
						Run = Config!.IsExporterEnabled(Exporter.Name) || 
							(Exporter.Options.HasAnyFlags(UhtExporterOptions.Default) && !this.bNoDefaultExporters);
					}

					UHTManifest.Module? PluginModule = null;
					if (!string.IsNullOrEmpty(Exporter.ModuleName))
					{
						foreach (UHTManifest.Module Module in this.Manifest!.Modules)
						{
							if (string.Compare(Module.Name, Exporter.ModuleName, StringComparison.OrdinalIgnoreCase) == 0)
							{
								PluginModule = Module;
								break;
							}
						}
						if (PluginModule == null)
						{
							Log.Logger.LogWarning($"Exporter \"{Exporter.Name}\" skipped because module \"{Exporter.ModuleName}\" was not found in manifest");
							continue;
						}
					}

					if (Run)
					{
						Log.Logger.LogTrace($"       Running exporter {Exporter.Name}");
						UhtExportFactory Factory = new UhtExportFactory(this, PluginModule, Exporter);
						Factory.Run();
						foreach (var Output in Factory.Outputs)
						{
							if (Output.bSaved)
							{
								TotalWrittenFiles++;
							}
						}
						foreach (string Dep in Factory.ExternalDependencies)
						{
							ExternalDependencies.Add(Dep);
						}
					}
					else
					{
						Log.Logger.LogTrace($"       Exporter {Exporter.Name} skipped");
					}
				}

				// Save the collected external dependencies
				if (!string.IsNullOrEmpty(this.Manifest!.ExternalDependenciesFile))
				{
					using (StreamWriter Out = new StreamWriter(this.Manifest!.ExternalDependenciesFile))
					{
						foreach (string Dep in ExternalDependencies)
						{
							Out.WriteLine(Dep);
						}
					}
				}
			});

			Log.Logger.LogInformation($"Total of {TotalWrittenFiles} written");
		}
		#endregion
	}
}
