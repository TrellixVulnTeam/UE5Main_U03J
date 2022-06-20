// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Collection of UHT only flags associated with properties
	/// </summary>
	[Flags]
	public enum UhtPropertyExportFlags
	{
		/// <summary>
		/// Property should be exported as public
		/// </summary>
		Public = 0x00000001,

		/// <summary>
		/// Property should be exported as private
		/// </summary>
		Private = 0x00000002,

		/// <summary>
		/// Property should be exported as protected
		/// </summary>
		Protected = 0x00000004,
		
		/// <summary>
		/// The BlueprintPure flag was set in software and should not be considered an error
		/// </summary>
		ImpliedBlueprintPure = 0x00000008,

		/// <summary>
		/// If true, the property has a getter function
		/// </summary>
		GetterSpecified = 0x00000010,

		/// <summary>
		/// If true, the property has a setter function
		/// </summary>
		SetterSpecified = 0x00000020,

		/// <summary>
		/// If true, the getter has been disabled in the specifiers
		/// </summary>
		GetterSpecifiedNone = 0x00000040,

		/// <summary>
		/// If true, the setter has been disabled in the specifiers
		/// </summary>
		SetterSpecifiedNone = 0x00000080,

		/// <summary>
		/// If true, the getter function was found
		/// </summary>
		GetterFound = 0x00000100,

		/// <summary>
		/// If true, the property has a setter function
		/// </summary>
		SetterFound = 0x00000200,

		/// <summary>
		/// Property is marked as a field notify
		/// </summary>
		FieldNotify = 0x00000400,
	};

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtPropertyExportFlagsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtPropertyExportFlags InFlags, UhtPropertyExportFlags TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtPropertyExportFlags InFlags, UhtPropertyExportFlags TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtPropertyExportFlags InFlags, UhtPropertyExportFlags TestFlags, UhtPropertyExportFlags MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// The context of the property.
	/// </summary>
	public enum UhtPropertyCategory
	{
		/// <summary>
		/// Function parameter for a function that isn't marked as NET
		/// </summary>
		RegularParameter,

		/// <summary>
		/// Function parameter for a function that is marked as NET
		/// </summary>
		ReplicatedParameter,

		/// <summary>
		/// Function return value
		/// </summary>
		Return,

		/// <summary>
		/// Class or a script structure member property
		/// </summary>
		Member,
	};

	/// <summary>
	/// Helper methods for the property category
	/// </summary>
	public static class UhtPropertyCategoryExtensions
	{

		/// <summary>
		/// Return the hint text for the property category
		/// </summary>
		/// <param name="PropertyCategory">Property category</param>
		/// <returns>The user facing hint text</returns>
		/// <exception cref="UhtIceException">Unexpected category</exception>
		public static string GetHintText(this UhtPropertyCategory PropertyCategory)
		{
			switch (PropertyCategory)
			{
				case UhtPropertyCategory.ReplicatedParameter:
				case UhtPropertyCategory.RegularParameter:
					return "Function parameter";

				case UhtPropertyCategory.Return:
					return "Function return type";

				case UhtPropertyCategory.Member:
					return "Member variable declaration";

				default:
					throw new UhtIceException("Unknown variable category");
			}
		}
	}

	/// <summary>
	/// Allocator used for container
	/// </summary>
	public enum UhtPropertyAllocator
	{
		/// <summary>
		/// Default allocator
		/// </summary>
		Default,

		/// <summary>
		/// Memory image allocator
		/// </summary>
		MemoryImage
	};

	/// <summary>
	/// Type of pointer
	/// </summary>
	public enum UhtPointerType
	{

		/// <summary>
		/// No pointer specified
		/// </summary>
		None,

		/// <summary>
		/// Native pointer specified
		/// </summary>
		Native,
	}

	/// <summary>
	/// Size type of an integer
	/// </summary>
	public enum UhtPropertyIntType
	{

		/// <summary>
		/// Property is not an integer
		/// </summary>
		None,

		/// <summary>
		/// Property is a sized integer
		/// </summary>
		Sized,

		/// <summary>
		/// Property is an unsized integer
		/// </summary>
		Unsized,
	};

	/// <summary>
	/// Type of reference
	/// </summary>
	public enum UhtPropertyRefQualifier
	{

		/// <summary>
		/// Property is not a reference
		/// </summary>
		None,

		/// <summary>
		/// Property is a const reference
		/// </summary>
		ConstRef,

		/// <summary>
		/// Property is a non-const reference
		/// </summary>
		NonConstRef,
	};

	/// <summary>
	/// Options that customize the properties.
	/// </summary>
	[Flags]
	public enum UhtPropertyOptions
	{

		/// <summary>
		/// No property options
		/// </summary>
		None = 0,

		/// <summary>
		/// Don't automatically mark properties as CPF_Const
		/// </summary>
		NoAutoConst = 1 << 0,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtPropertyOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtPropertyOptions InFlags, UhtPropertyOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtPropertyOptions InFlags, UhtPropertyOptions TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtPropertyOptions InFlags, UhtPropertyOptions TestFlags, UhtPropertyOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Property capabilities.  Use the caps system instead of patterns such as "Property is UhtObjectProperty"
	/// </summary>
	[Flags]
	public enum UhtPropertyCaps
	{
		/// <summary>
		/// No property caps
		/// </summary>
		None = 0,

		/// <summary>
		/// If true, the property will be passed by reference when generating the full type string
		/// </summary>
		PassCppArgsByRef = 1 << 0,

		/// <summary>
		/// If true, the an argument will need to be added to the constructor
		/// </summary>
		RequiresNullConstructorArg = 1 << 1,

		/// <summary>
		/// If true, the property type can be TArray or TMap value
		/// </summary>
		CanBeContainerValue = 1 << 2,

		/// <summary>
		/// If true, the property type can be a TSet or TMap key
		/// </summary>
		CanBeContainerKey = 1 << 3,

		/// <summary>
		/// True if the property can be instanced
		/// </summary>
		CanBeInstanced = 1 << 4,

		/// <summary>
		/// True if the property can be exposed on spawn
		/// </summary>
		CanExposeOnSpawn = 1 << 5,

		/// <summary>
		/// True if the property can have a config setting
		/// </summary>
		CanHaveConfig = 1 << 6,

		/// <summary>
		/// True if the property allows the BlueprintAssignable flag.
		/// </summary>
		CanBeBlueprintAssignable = 1 << 7,

		/// <summary>
		/// True if the property allows the BlueprintCallable flag.
		/// </summary>
		CanBeBlueprintCallable = 1 << 8,

		/// <summary>
		/// True if the property allows the BlueprintAuthorityOnly flag.
		/// </summary>
		CanBeBlueprintAuthorityOnly = 1 << 9,

		/// <summary>
		/// True to see if the function parameter property is supported by blueprint
		/// </summary>
		IsParameterSupportedByBlueprint = 1 << 10,

		/// <summary>
		/// True to see if the member property is supported by blueprint
		/// </summary>
		IsMemberSupportedByBlueprint = 1 << 11,

		/// <summary>
		/// True if the property supports RigVM
		/// </summary>
		SupportsRigVM = 1 << 12,

		/// <summary>
		/// True if the property should codegen as an enumeration
		/// </summary>
		IsRigVMEnum = 1 << 13,

		/// <summary>
		/// True if the property should codegen as an array
		/// </summary>
		IsRigVMArray = 1 << 14,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtPropertyCapsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtPropertyCaps InFlags, UhtPropertyCaps TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtPropertyCaps InFlags, UhtPropertyCaps TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtPropertyCaps InFlags, UhtPropertyCaps TestFlags, UhtPropertyCaps MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	/// <summary>
	/// Text can be formatted in different context with slightly different results
	/// </summary>
	public enum UhtPropertyTextType
	{
		/// <summary>
		/// Generic type
		/// </summary>
		Generic,

		/// <summary>
		/// Generic function argument or return value
		/// </summary>
		GenericFunctionArgOrRetVal,

		/// <summary>
		/// Generic function argument or return value implementation (specific to booleans and will always return "bool")
		/// </summary>
		GenericFunctionArgOrRetValImpl,

		/// <summary>
		/// Class function argument or return value
		/// </summary>
		ClassFunctionArgOrRetVal,

		/// <summary>
		/// Event function argument or return value
		/// </summary>
		EventFunctionArgOrRetVal,

		/// <summary>
		/// Interface function argument or return value
		/// </summary>
		InterfaceFunctionArgOrRetVal,

		/// <summary>
		/// Sparse property declaration
		/// </summary>
		Sparse,

		/// <summary>
		/// Sparse property short name
		/// </summary>
		SparseShort,

		/// <summary>
		/// Class or structure member
		/// </summary>
		ExportMember,

		/// <summary>
		/// Members of the event parameters structure used by code generation to invoke events
		/// </summary>
		EventParameterMember,

		/// <summary>
		/// Members of the event parameters structure used by code generation to invoke functions
		/// </summary>
		EventParameterFunctionMember,

		/// <summary>
		/// Instance of the property is being constructed
		/// </summary>
		Construction,

		/// <summary>
		/// Used to get the type argument for a function thunk.  This is used for P_GET_ARRAY_*
		/// </summary>
		FunctionThunkParameterArrayType,

		/// <summary>
		/// If the P_GET macro requires an argument, this is used to fetch that argument
		/// </summary>
		FunctionThunkParameterArgType,

		/// <summary>
		/// Used to get the return type for function thunks
		/// </summary>
		FunctionThunkRetVal,

		/// <summary>
		/// Basic RigVM type
		/// </summary>
		RigVMType,

		/// <summary>
		/// Type expected in a getter/setter argument list
		/// </summary>
		GetterSetterArg,
	}

	/// <summary>
	/// Extension methods for the property text type
	/// </summary>
	public static class UhtPropertyTextTypeExtensions
	{

		/// <summary>
		/// Test to see if the text type is for a function
		/// </summary>
		/// <param name="TextType">Type of text</param>
		/// <returns>True if the text type is a function</returns>
		public static bool IsParameter(this UhtPropertyTextType TextType)
		{
			return
				TextType == UhtPropertyTextType.GenericFunctionArgOrRetVal ||
				TextType == UhtPropertyTextType.GenericFunctionArgOrRetValImpl ||
				TextType == UhtPropertyTextType.ClassFunctionArgOrRetVal ||
				TextType == UhtPropertyTextType.EventFunctionArgOrRetVal ||
				TextType == UhtPropertyTextType.InterfaceFunctionArgOrRetVal;
		}
	}

	//ETSTODO - This can be removed since FunctionThunkParameterArgType is only used in conjunction with UhtPGetArgumentType
	/// <summary>
	/// Specifies how the PGet argument type is to be formatted
	/// </summary>
	public enum UhtPGetArgumentType
	{
		/// <summary>
		/// </summary>
		None,

		/// <summary>
		/// </summary>
		EngineClass,

		/// <summary>
		/// </summary>
		TypeText,
	}

	/// <summary>
	/// Property member context provides extra context when formatting the member declaration and definitions.
	/// </summary>
	public interface IUhtPropertyMemberContext
	{
		/// <summary>
		/// The outer/owning structure
		/// </summary>
		public UhtStruct OuterStruct { get; }

		/// <summary>
		/// The outer/owning structure source name.  In some cases, this can differ from the OuterStruct.SourceName
		/// </summary>
		public string OuterStructSourceName { get; }

		/// <summary>
		/// Name of the statics block for static definitions for the outer object
		/// </summary>
		public string StaticsName { get; }

		/// <summary>
		/// Prefix to apply to declaration names
		/// </summary>
		public string NamePrefix { get; }

		/// <summary>
		/// Suffix to add to the declaration name
		/// </summary>
		public string MetaDataSuffix { get; }

		/// <summary>
		/// Return the hash code for a given object
		/// </summary>
		/// <param name="Object">Object in question</param>
		/// <returns>Hash code</returns>
		public uint GetTypeHash(UhtObject Object);

		/// <summary>
		/// Return the singleton name for the given object
		/// </summary>
		/// <param name="Object">Object in question</param>
		/// <param name="bRegistered">If true, the singleton that returns the registered object is returned.</param>
		/// <returns>Singleton function name</returns>
		public string GetSingletonName(UhtObject? Object, bool bRegistered);
	}

	/// <summary>
	/// Property settings is a transient object used during the parsing of properties
	/// </summary>
	public class UhtPropertySettings
	{

		/// <summary>
		/// Source name of the property
		/// </summary>
		public string SourceName = String.Empty;

		/// <summary>
		/// Engine name of the property
		/// </summary>
		public string EngineName = String.Empty;

		/// <summary>
		/// Property's meta data
		/// </summary>
		public UhtMetaData MetaData = UhtMetaData.Empty;

		/// <summary>
		/// Property outer object
		/// </summary>
		public UhtType Outer;

		/// <summary>
		/// Line number of the property declaration
		/// </summary>
		public int LineNumber;

		/// <summary>
		/// Property category
		/// </summary>
		public UhtPropertyCategory PropertyCategory;

		/// <summary>
		/// Engine property flags
		/// </summary>
		public EPropertyFlags PropertyFlags;

		/// <summary>
		/// Property flags not allowed by the context of the property parsing
		/// </summary>
		public EPropertyFlags DisallowPropertyFlags;

		/// <summary>
		/// UHT specified property flags
		/// </summary>
		public UhtPropertyExportFlags PropertyExportFlags;

		/// <summary>
		/// Allocator used for containers
		/// </summary>
		public UhtPropertyAllocator Allocator;

		/// <summary>
		/// Options for property parsing
		/// </summary>
		public UhtPropertyOptions Options;

		/// <summary>
		/// Property pointer type
		/// </summary>
		public UhtPointerType PointerType;

		/// <summary>
		/// Replication notify name
		/// </summary>
		public string? RepNotifyName;

		/// <summary>
		/// If set, the array size of the property
		/// </summary>
		public string? ArrayDimensions;

		/// <summary>
		/// Getter method
		/// </summary>
		public string? Setter;

		/// <summary>
		/// Setter method
		/// </summary>
		public string? Getter;

		/// <summary>
		/// Default value of the property
		/// </summary>
		public List<UhtToken>? DefaultValueTokens;

		/// <summary>
		/// If true, the property is a bit field
		/// </summary>
		public bool bIsBitfield;

		/// <summary>
		/// Construct a new, uninitialized version of the property settings
		/// </summary>
#pragma warning disable CS8618 // Non-nullable field must contain a non-null value when exiting constructor. Consider declaring as nullable.
		public UhtPropertySettings()
#pragma warning restore CS8618 // Non-nullable field must contain a non-null value when exiting constructor. Consider declaring as nullable.
		{
		}

		/// <summary>
		/// Construct property settings based on the property settings for a parent container
		/// </summary>
		/// <param name="ParentPropertySettings">Parent container property</param>
		/// <param name="SourceName">Name of the property</param>
		/// <param name="MessageSite">Message site used to construct meta data object</param>
		public UhtPropertySettings(UhtPropertySettings ParentPropertySettings, string SourceName, IUhtMessageSite MessageSite)
		{
			this.SourceName = SourceName;
			this.EngineName = SourceName;
			this.MetaData = new UhtMetaData(MessageSite, ParentPropertySettings.Outer.Session.Config);
			this.Outer = ParentPropertySettings.Outer;
			this.LineNumber = ParentPropertySettings.LineNumber;
			this.PropertyCategory = ParentPropertySettings.PropertyCategory;
			this.PropertyFlags = ParentPropertySettings.PropertyFlags;
			this.DisallowPropertyFlags = ParentPropertySettings.DisallowPropertyFlags;
			this.PropertyExportFlags = UhtPropertyExportFlags.Public;
			this.RepNotifyName = null;
			this.Allocator = UhtPropertyAllocator.Default;
			this.Options = ParentPropertySettings.Options;
			this.PointerType = UhtPointerType.None;
			this.ArrayDimensions = null;
			this.DefaultValueTokens = null;
			this.Setter = null;
			this.Getter = null;
			this.bIsBitfield = false;
		}

		/// <summary>
		/// Reset the property settings.  Used on a cached property settings object
		/// </summary>
		/// <param name="Outer">Outer/owning type</param>
		/// <param name="LineNumber">Line number of property</param>
		/// <param name="PropertyCategory">Category of property</param>
		/// <param name="DisallowPropertyFlags">Property flags that are not allowed</param>
		public void Reset(UhtType Outer, int LineNumber, UhtPropertyCategory PropertyCategory, EPropertyFlags DisallowPropertyFlags)
		{
			this.SourceName = string.Empty;
			this.EngineName = string.Empty;
			this.MetaData = new UhtMetaData(Outer, Outer.Session.Config);
			this.Outer = Outer;
			this.LineNumber = LineNumber;
			this.PropertyCategory = PropertyCategory;
			this.PropertyFlags = EPropertyFlags.None;
			this.DisallowPropertyFlags = DisallowPropertyFlags;
			this.PropertyExportFlags = UhtPropertyExportFlags.Public;
			this.RepNotifyName = null;
			this.Allocator = UhtPropertyAllocator.Default;
			this.PointerType = UhtPointerType.None;
			this.ArrayDimensions = null;
			this.DefaultValueTokens = null;
			this.Setter = null;
			this.Getter = null;
			this.bIsBitfield = false;
		}

		/// <summary>
		/// Reset property settings based on the given property.  Used to prepare a cached property settings for parsing.
		/// </summary>
		/// <param name="Property">Source property</param>
		/// <param name="Options">Property options</param>
		/// <exception cref="UhtIceException">Thrown if the input property doesn't have an outer</exception>
		public void Reset(UhtProperty Property, UhtPropertyOptions Options)
		{
			if (Property.Outer == null)
			{
				throw new UhtIceException("Property must have an outer specified");
			}
			this.SourceName = Property.SourceName;
			this.EngineName = Property.EngineName;
			this.MetaData = Property.MetaData;
			this.Outer = Property.Outer;
			this.LineNumber = Property.LineNumber;
			this.PropertyCategory = Property.PropertyCategory;
			this.PropertyFlags = Property.PropertyFlags;
			this.DisallowPropertyFlags = Property.DisallowPropertyFlags;
			this.PropertyExportFlags = Property.PropertyExportFlags;
			this.Allocator = Property.Allocator;
			this.Options = Options;
			this.PointerType = Property.PointerType;
			this.RepNotifyName = Property.RepNotifyName;
			this.ArrayDimensions = Property.ArrayDimensions;
			this.DefaultValueTokens = Property.DefaultValueTokens;
			this.Setter = Property.Setter;
			this.Getter = Property.Getter;
			this.bIsBitfield = Property.bIsBitfield;
		}
	}

	/// <summary>
	/// Represents FProperty fields
	/// </summary>
	[UhtEngineClass(Name = "Field", IsProperty = true)] // This is here just so it is defined
	[UhtEngineClass(Name = "Property", IsProperty = true)]
	public abstract class UhtProperty : UhtType
	{
		#region Constants
		/// <summary>
		/// Collection of recognized casts when parsing array dimensions
		/// </summary>
		private static string[] Casts = new string[]
		{
			"(uint32)",
			"(int32)",
			"(uint16)",
			"(int16)",
			"(uint8)",
			"(int8)",
			"(int)",
			"(unsigned)",
			"(signed)",
			"(unsigned int)",
			"(signed int)",
		};

		/// <summary>
		/// Collection of invalid names for parameters 
		/// </summary>
		private static readonly HashSet<string> InvalidParamNames = new HashSet<string>(StringComparer.OrdinalIgnoreCase) { "self" };

		/// <summary>
		/// Standard object flags for properties
		/// </summary>
		protected const string ObjectFlags = "RF_Public|RF_Transient|RF_MarkAsNative";

		/// <summary>
		/// Prefix used when declaring P_GET_ parameters
		/// </summary>
		protected const string FunctionParameterThunkPrefix = "Z_Param_";
		#endregion

		#region Protected property configuration used to simplify implementation details
		/// <summary>
		/// Simple native CPP type text.  Will not include any template arguments
		/// </summary>
		protected abstract string CppTypeText { get; }

		/// <summary>
		/// P_GET_ macro name
		/// </summary>
		protected abstract string PGetMacroText { get; }

		/// <summary>
		/// If true, then references must be passed without a pointer
		/// </summary>
		protected virtual bool bPGetPassAsNoPtr { get => false; }

		/// <summary>
		/// Type of the PGet argument if one is required
		/// </summary>
		protected virtual UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.None; }
		#endregion

		/// <summary>
		/// Property category
		/// </summary>
		public UhtPropertyCategory PropertyCategory = UhtPropertyCategory.Member;

		/// <summary>
		/// Engine property flags
		/// </summary>
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public EPropertyFlags PropertyFlags { get; set; }

		/// <summary>
		/// Capabilities of the property.  Use caps system instead of testing for specific property types.
		/// </summary>
		[JsonIgnore]
		[JsonConverter(typeof(JsonStringEnumConverter))]
		public UhtPropertyCaps PropertyCaps { get; set; }

		/// <summary>
		/// Engine flags that are disallowed on this property
		/// </summary>
		public EPropertyFlags DisallowPropertyFlags = EPropertyFlags.None;

		/// <summary>
		/// UHT specified property flags
		/// </summary>
		public UhtPropertyExportFlags PropertyExportFlags = UhtPropertyExportFlags.Public;

		/// <summary>
		/// Reference type of the property
		/// </summary>
		public UhtPropertyRefQualifier RefQualifier = UhtPropertyRefQualifier.None;

		/// <summary>
		/// Pointer type of the property
		/// </summary>
		public UhtPointerType PointerType = UhtPointerType.None;

		/// <summary>
		/// Allocator to be used with containers
		/// </summary>
		public UhtPropertyAllocator Allocator = UhtPropertyAllocator.Default;

		/// <summary>
		/// Replication notify name
		/// </summary>
		public string? RepNotifyName = null;

		/// <summary>
		/// Fixed array size
		/// </summary>
		public string? ArrayDimensions = null;

		/// <summary>
		/// Property setter
		/// </summary>
		public string? Setter = null;

		/// <summary>
		/// Property getter
		/// </summary>
		public string? Getter = null;

		/// <summary>
		/// Default value of property
		/// </summary>
		public List<UhtToken>? DefaultValueTokens = null;

		/// <summary>
		/// If true, this property is a bit field
		/// </summary>
		public bool bIsBitfield = false;

		///<inheritdoc/>
		[JsonIgnore]
		public override UhtEngineType EngineType => UhtEngineType.Property;

		///<inheritdoc/>
		[JsonIgnore]
		public override bool bDeprecated => this.PropertyFlags.HasAnyFlags(EPropertyFlags.Deprecated);

		///<inheritdoc/>
		protected override UhtSpecifierValidatorTable? SpecifierValidatorTable
		{
			get
			{
				switch (this.PropertyCategory)
				{
					case UhtPropertyCategory.Member:
						return this.Session.GetSpecifierValidatorTable(UhtTableNames.PropertyMember);
					case UhtPropertyCategory.RegularParameter:
					case UhtPropertyCategory.ReplicatedParameter:
					case UhtPropertyCategory.Return:
						return this.Session.GetSpecifierValidatorTable(UhtTableNames.PropertyArgument);
					default:
						throw new UhtIceException("Unknown property category type");
				}
			}
		}

		/// <summary>
		/// If true, the property is a fixed/static array
		/// </summary>
		[JsonIgnore]
		public bool bIsStaticArray => !string.IsNullOrEmpty(this.ArrayDimensions);

		/// <summary>
		/// If true, the property is editor only
		/// </summary>
		[JsonIgnore]
		public bool bIsEditorOnlyProperty => this.PropertyFlags.HasAnyFlags(EPropertyFlags.DevelopmentAssets);

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="Outer">Outer type of the property</param>
		/// <param name="LineNumber">Line number where property was declared</param>
		public UhtProperty(UhtType Outer, int LineNumber) : base(Outer, LineNumber)
		{
			this.PropertyFlags = EPropertyFlags.None;
			this.PropertyCaps = UhtPropertyCaps.CanBeContainerValue | UhtPropertyCaps.CanBeContainerKey | UhtPropertyCaps.CanHaveConfig;
		}

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="PropertySettings">Property settings from parsing</param>
		public UhtProperty(UhtPropertySettings PropertySettings) : base(PropertySettings.Outer, PropertySettings.LineNumber, PropertySettings.MetaData)
		{
			this.SourceName = PropertySettings.SourceName;
			// Engine name defaults to source name.  If it doesn't match what is coming in, then set it.
			if (PropertySettings.EngineName.Length > 0 && PropertySettings.SourceName != PropertySettings.EngineName ) 
			{
				this.EngineName = PropertySettings.EngineName;
			}
			this.PropertyCategory = PropertySettings.PropertyCategory;
			this.PropertyFlags = PropertySettings.PropertyFlags;
			this.DisallowPropertyFlags = PropertySettings.DisallowPropertyFlags;
			this.PropertyExportFlags = PropertySettings.PropertyExportFlags;
			this.Allocator = PropertySettings.Allocator;
			this.PointerType = PropertySettings.PointerType;
			this.RepNotifyName = PropertySettings.RepNotifyName;
			this.ArrayDimensions = PropertySettings.ArrayDimensions;
			this.DefaultValueTokens = PropertySettings.DefaultValueTokens;
			this.Getter = PropertySettings.Getter;
			this.Setter = PropertySettings.Setter;
			this.bIsBitfield = PropertySettings.bIsBitfield;
			this.PropertyCaps = UhtPropertyCaps.CanBeContainerValue | UhtPropertyCaps.CanBeContainerKey | UhtPropertyCaps.CanHaveConfig;
		}

		/// <inheritdoc/>
		protected override string GetDisplayStringFromEngineName()
		{
			return UhtFCString.NameToDisplayString(this.EngineName, this is UhtBoolProperty);
		}

		#region Text and code generation support
		/// <summary>
		/// Internal version of AppendText.  Don't append any text to the builder to get the default behavior
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="TextType">Text type of where the property is being referenced</param>
		/// <param name="bIsTemplateArgument">If true, this property is a template arguments</param>
		/// <returns>Destination builder</returns>
		public virtual StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument = false)
		{
			// By default, we assume it will be just the simple text
			return Builder.Append(this.CppTypeText);
		}

		/// <summary>
		/// Append the full declaration including such things as property name and const<amp/> requirements
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="TextType">Text type of where the property is being referenced</param>
		/// <param name="bSkipParameterName">If true, do not include the property name</param>
		/// <returns>Destination builder</returns>
		public virtual StringBuilder AppendFullDecl(StringBuilder Builder, UhtPropertyTextType TextType, bool bSkipParameterName = false)
		{
			UhtPropertyCaps Caps = this.PropertyCaps;

			bool bIsParameter = TextType.IsParameter();
			bool bIsInterfaceProp = this is UhtInterfaceProperty;

			// When do we need a leading const:
			// 1) If this is a object or object ptr property and the referenced class is const
			// 2) If this is not an out parameter or reference parameter then,
			//		if this is a parameter
			//		AND - if this is a const param OR (if this is an interface property and not an out param)
			// 3) If this is a parameter without array dimensions, must be passed by reference, but not an out parameter or const param
			bool bPassCppArgsByRef = Caps.HasAnyFlags(UhtPropertyCaps.PassCppArgsByRef);
			bool bIsConstParam = bIsParameter && (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm) || (bIsInterfaceProp && !this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm)));
			bool bIsConstArgsByRef = bIsParameter && this.ArrayDimensions == null && bPassCppArgsByRef && !this.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm | EPropertyFlags.OutParm);
			bool bIsOnConstClass = false;
			if (this is UhtObjectProperty ObjectProperty)
			{
				bIsOnConstClass = ObjectProperty.Class.ClassFlags.HasAnyFlags(EClassFlags.Const);
			}
			bool bShouldHaveRef = this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm | EPropertyFlags.ReferenceParm);

			bool bConstAtTheBeginning = bIsOnConstClass || bIsConstArgsByRef || (bIsConstParam && !bShouldHaveRef);
			if (bConstAtTheBeginning)
			{
				Builder.Append("const ");
			}

			this.AppendText(Builder, TextType);

			bool bFromConstClass = false;
			if (TextType == UhtPropertyTextType.ExportMember && this.Outer != null)
			{
				if (this.Outer is UhtClass OuterClass)
				{
					bFromConstClass = OuterClass.ClassFlags.HasAnyFlags(EClassFlags.Const);
				}
			}
			bool bConstAtTheEnd = bFromConstClass || (bIsConstParam && bShouldHaveRef);
			if (bConstAtTheEnd)
			{
				Builder.Append(" const");
			}

			if (bIsParameter && this.ArrayDimensions == null && (bPassCppArgsByRef || this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm | EPropertyFlags.ReferenceParm)))
			{
				Builder.Append('&');
			}

			Builder.Append(' ');
			if (!bSkipParameterName)
			{
				Builder.Append(this.SourceName);
			}

			if (this.ArrayDimensions != null)
			{
				Builder.Append('[').Append(this.ArrayDimensions).Append(']');
			}
			return Builder;
		}

		/// <summary>
		/// Append the required code to declare the property as a member
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Current context</param>
		/// <param name="Name">Name of the property.  This is needed in some cases where the name in the declarations doesn't match the property name.</param>
		/// <param name="NameSuffix">Suffix to the property name</param>
		/// <param name="Tabs">Number of tabs prefix the line with</param>
		/// <returns>Output builder</returns>
		public abstract StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs);

		/// <summary>
		/// Append the required code to declare the property as a member
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Current context</param>
		/// <param name="Name">Name of the property.  This is needed in some cases where the name in the declarations doesn't match the property name.</param>
		/// <param name="NameSuffix">Suffix to the property name</param>
		/// <param name="Tabs">Number of tabs prefix the line with</param>
		/// <param name="ParamsStructName">Structure name</param>
		/// <param name="bAppendMetaDataDecl">If true, add the meta data decl prior to the member decl</param>
		/// <returns>Output builder</returns>
		public StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs, string ParamsStructName, bool bAppendMetaDataDecl = true)
		{
			if (bAppendMetaDataDecl)
			{
				Builder.AppendMetaDataDecl(this, Context, Name, NameSuffix, Tabs);
			}
			Builder.AppendTabs(Tabs).Append("static const UECodeGen_Private::").Append(ParamsStructName).Append(' ').AppendNameDecl(Context, Name, NameSuffix).Append(";\r\n");
			return Builder;
		}

		/// <summary>
		/// Append the required code to define the property as a member
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Context of the call</param>
		/// <param name="Name">Name of the property.  This is needed in some cases where the name in the declarations doesn't match the property name.</param>
		/// <param name="NameSuffix">Suffix to the property name</param>
		/// <param name="Offset">Offset of the property</param>
		/// <param name="Tabs">Number of tabs prefix the line with</param>
		/// <returns>Output builder</returns>
		public abstract StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs);

		/// <summary>
		/// Append the required start of code to define the property as a member
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Context of the call</param>
		/// <param name="Name">Name of the property.  This is needed in some cases where the name in the declarations doesn't match the property name.</param>
		/// <param name="NameSuffix">Suffix to the property name</param>
		/// <param name="Offset">Offset of the property</param>
		/// <param name="Tabs">Number of tabs prefix the line with</param>
		/// <param name="ParamsStructName">Structure name</param>
		/// <param name="ParamsGenFlags">Structure flags</param>
		/// <param name="bAppendMetaDataDef">If true, add the meta data def prior to the member def</param>
		/// <param name="bAppendOffset">If true, add the offset parameter</param>
		/// <returns>Output builder</returns>
		public StringBuilder AppendMemberDefStart(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs,
			string ParamsStructName, string ParamsGenFlags, bool bAppendMetaDataDef = true, bool bAppendOffset = true)
		{
			if (bAppendMetaDataDef)
			{
				Builder.AppendMetaDataDef(this, Context, Name, NameSuffix, Tabs);
			}
			Builder
				.AppendTabs(Tabs)
				.Append("const UECodeGen_Private::").Append(ParamsStructName).Append(' ')
				.AppendNameDef(Context, Name, NameSuffix).Append(" = { ")
				.AppendUTF8LiteralString(this.EngineName).Append(", ")
				.AppendNotifyFunc(this).Append(", ")
				.AppendFlags(this.PropertyFlags).Append(", ")
				.Append(ParamsGenFlags).Append(", ")
				.Append(ObjectFlags).Append(", ")
				.AppendArrayDim(this, Context).Append(", ");

			if (this.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.SetterFound))
			{
				Builder.Append('&').Append(this.Outer!.SourceName).Append("::").AppendPropertySetterWrapperName(this).Append(", ");
			}
			else
			{
				Builder.Append("nullptr, ");
			}

			if (this.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.GetterFound))
			{
				Builder.Append('&').Append(this.Outer!.SourceName).Append("::").AppendPropertyGetterWrapperName(this).Append(", ");
			}
			else
			{
				Builder.Append("nullptr, ");
			}

			if (bAppendOffset)
			{
				if (!string.IsNullOrEmpty(Offset))
				{
					Builder.Append(Offset).Append(", ");
				}
				else
				{
					Builder.Append("STRUCT_OFFSET(").Append(Context.OuterStructSourceName).Append(", ").Append(this.SourceName).Append("), ");
				}
			}
			return Builder;
		}

		/// <summary>
		/// Append the required end of code to define the property as a member
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Context of the call</param>
		/// <param name="Name">Name of the property.  This is needed in some cases where the name in the declarations doesn't match the property name.</param>
		/// <param name="NameSuffix">Suffix to the property name</param>
		/// <returns>Output builder</returns>
		protected StringBuilder AppendMemberDefEnd(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix)
		{
			Builder
				.AppendMetaDataParams(this, Context, Name, NameSuffix)
				.Append(" };")
				.AppendObjectHashes(this, Context)
				.Append("\r\n");
			return Builder;
		}

		/// <summary>
		/// Append the a type reference to the member definition
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Context of the call</param>
		/// <param name="Object">Referenced object</param>
		/// <param name="bRegistered">True if the registered singleton name is to be used</param>
		/// <param name="bAppendNull">True if a "nullptr" is to be appended if the object is null</param>
		/// <returns>Output builder</returns>
		protected StringBuilder AppendMemberDefRef(StringBuilder Builder, IUhtPropertyMemberContext Context, UhtObject? Object, bool bRegistered, bool bAppendNull = false)
		{
			if (Object != null)
			{
				Builder.AppendSingletonName(Context, Object, bRegistered).Append(", ");
			}
			else if (bAppendNull)
			{
				Builder.Append("nullptr, ");
			}
			return Builder;
		}

		/// <summary>
		/// Append the required code to add the properties to a pointer array
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="Context">Context of the call</param>
		/// <param name="Name">Name of the property.  This is needed in some cases where the name in the declarations doesn't match the property name.</param>
		/// <param name="NameSuffix">Suffix to the property name</param>
		/// <param name="Tabs">Number of tabs prefix the line with</param>
		/// <returns>Output builder</returns>
		public virtual StringBuilder AppendMemberPtr(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			Builder.AppendTabs(Tabs).Append("(const UECodeGen_Private::FPropertyParamsBase*)&").AppendNameDef(Context, Name, NameSuffix).Append(",\r\n");
			return Builder;
		}

		/// <summary>
		/// Append a P_GET macro
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <returns>Destination builder</returns>
		public virtual StringBuilder AppendFunctionThunkParameterGet(StringBuilder Builder)
		{
			Builder.Append("P_GET_");
			if (this.ArrayDimensions != null)
			{
				Builder.Append("ARRAY");
			}
			else
			{
				Builder.Append(this.PGetMacroText);
			}
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
			{
				if (this.bPGetPassAsNoPtr)
				{
					Builder.Append("_REF_NO_PTR");
				}
				else
				{
					Builder.Append("_REF");
				}
			}
			Builder.Append("(");
			if (this.ArrayDimensions != null)
			{
				Builder.AppendFunctionThunkParameterArrayType(this).Append(",");
			}
			else
			{
				switch (this.PGetTypeArgument)
				{
					case UhtPGetArgumentType.None:
						break;

					case UhtPGetArgumentType.EngineClass:
						Builder.Append('F').Append(this.EngineClassName).Append(',');
						break;

					case UhtPGetArgumentType.TypeText:
						Builder.AppendPropertyText(this, UhtPropertyTextType.FunctionThunkParameterArgType).Append(",");
						break;
				}
			}
			Builder.AppendFunctionThunkParameterName(this).Append(")");
			return Builder;
		}

		/// <summary>
		/// Append the text for a function thunk call argument
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <returns>Output builder</returns>
		public virtual StringBuilder AppendFunctionThunkParameterArg(StringBuilder Builder)
		{
			return Builder.AppendFunctionThunkParameterName(this);
		}

		/// <summary>
		/// Apppend the name of a function thunk paramter
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <returns>Output builder</returns>
		public StringBuilder AppendFunctionThunkParameterName(StringBuilder Builder)
		{
			Builder.Append(FunctionParameterThunkPrefix);
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
			{
				Builder.Append("Out_");
			}
			Builder.Append(this.EngineName);
			return Builder;
		}

		/// <summary>
		/// Append the appropriate values to initialize the property to a "NULL" value;
		/// </summary>
		/// <param name="Builder"></param>
		/// <param name="bIsInitializer"></param>
		/// <returns></returns>
		public abstract StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer);

		/// <summary>
		/// Return the basic declaration type text for user facing messages
		/// </summary>
		/// <returns></returns>
		public string GetUserFacingDecl()
		{
			StringBuilder Builder = new StringBuilder();
			AppendText(Builder, UhtPropertyTextType.Generic);
			return Builder.ToString();
		}

		/// <summary>
		/// Return the RigVM type
		/// </summary>
		/// <returns></returns>
		public string GetRigVMType()
		{
			using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Small))
			{
				AppendText(Borrower.StringBuilder, UhtPropertyTextType.RigVMType);
				return Borrower.StringBuilder.ToString();
			}
		}

		/// <summary>
		/// Appends any applicable objects and child properties
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="StartingLength">Initial length of the builder prior to appending the hashes</param>
		/// <param name="Context">Context used to lookup the hashes</param>
		public virtual void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
		}
		#endregion

		#region Parsing support
		/// <summary>
		/// Parse a default value for the property and return a sanitized string representation.
		/// 
		/// All tokens in the token reader must be consumed.  Otherwise the default value will be considered to be invalid.
		/// </summary>
		/// <param name="DefaultValueReader">Reader containing the default value</param>
		/// <param name="InnerDefaultValue">Sanitized representation of default value</param>
		/// <returns>True if a default value was parsed.</returns>
		public abstract bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue);
		#endregion

		#region Resolution support
		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResult = base.ResolveSelf(Phase);

			switch (Phase)
			{
				case UhtResolvePhase.Final:
					if (this.ArrayDimensions != null)
					{
						ReadOnlySpan<char> Dim = this.ArrayDimensions.AsSpan();

						bool bAgain = false;
						do
						{
							bAgain = false;

							// Remove any outer brackets
							if (Dim[0] == '(')
							{
								for (int Index = 1, Depth = 1; Index < Dim.Length; ++Index)
								{
									if (Dim[Index] == ')')
									{
										if (--Depth == 0)
										{
											if (Index == Dim.Length - 1)
											{
												Dim = Dim.Slice(1, Index - 1);
												bAgain = true;
											}
											break;
										}
									}
									else if (Dim[Index] == '(')
									{
										++Depth;
									}
								}
							}

							// Remove any well known casts
							if (Dim.Length > 0)
							{
								foreach (string Cast in Casts)
								{
									if (Dim.StartsWith(Cast))
									{
										Dim = Dim.Slice(Cast.Length);
										bAgain = true;
										break;
									}
								}
							}

						} while (bAgain && Dim.Length > 0);

						//COMPATIBILITY-TODO - This method is more robust, but causes differences.  See UhtSession for future
						// plans on fix in this.
						//// Now try to see if this is an enum
						//UhtEnum? Enum = null;
						//int Sep = Dim.IndexOf("::");
						//if (Sep >= 0)
						//{
						//	//COMPATIBILITY-TODO "Bob::Type::Value" did not generate a match with the old code
						//	if (Dim.LastIndexOf("::") != Sep)
						//	{
						//		break;
						//	}
						//	Dim = Dim.Slice(0, Sep);
						//}
						//else
						//{
						//	Enum = this.Session.FindRegularEnumValue(Dim.ToString());
						//}
						//if (Enum == null)
						//{
						//	Enum = this.Session.FindType(this.Outer, UhtFindOptions.Enum | UhtFindOptions.SourceName, Dim.ToString()) as UhtEnum;
						//}
						//if (Enum != null)
						//{
						//	this.MetaData.Add(UhtNames.ArraySizeEnum, Enum.GetPathName());
						//}

						if (Dim.Length > 0 && !UhtFCString.IsDigit(Dim[0]))
						{
							UhtEnum? Enum = this.Session.FindRegularEnumValue(Dim.ToString());
							if (Enum == null)
							{
								Enum = this.Session.FindType(this.Outer, UhtFindOptions.Enum | UhtFindOptions.SourceName, this.ArrayDimensions) as UhtEnum;
							}
							if (Enum != null)
							{
								this.MetaData.Add(UhtNames.ArraySizeEnum, Enum.GetPathName());
							}
						}
					}
					break;
			}
			return bResult;
		}

		/// <summary>
		/// Check properties to see if any instances are referenced.
		/// This method does NOT cache the result.
		/// </summary>
		/// <param name="bDeepScan">If true, the ScanForInstancedReferenced method on the properties will also be called.</param>
		/// <returns></returns>
		public virtual bool ScanForInstancedReferenced(bool bDeepScan)
		{
			return false;
		}
		#endregion

		#region Validation support
		/// <inheritdoc/>
		protected override UhtValidationOptions Validate(UhtValidationOptions Options)
		{
			Options = base.Validate(Options);

			// The outer should never be null...
			if (this.Outer == null)
			{
				return Options;
			}

			// Shadowing checks are done at this level, not in the properties themselves
			// Note: The old UHT would check for all property type where this version only checks for member variables.
			// In the old code if you defined the property after the function with argument with the same name, UHT would
			// not issue an error.  However, if the property was defined PRIOR to the function with the matching argument name,
			// UHT would generate an error.
			if (Options.HasAnyFlags(UhtValidationOptions.Shadowing) && this.PropertyCategory == UhtPropertyCategory.Member)
			{

				// First check for duplicate name in self and then duplicate name in parents
				UhtType? Existing = this.Outer.FindType(UhtFindOptions.PropertyOrFunction | UhtFindOptions.EngineName | UhtFindOptions.SelfOnly, this.EngineName);
				if (Existing == this)
				{
					Existing = this.Outer.FindType(UhtFindOptions.PropertyOrFunction | UhtFindOptions.EngineName | UhtFindOptions.ParentsOnly | UhtFindOptions.NoGlobal | UhtFindOptions.NoIncludes, this.EngineName);
				}

				if (Existing != null && Existing != this)
				{
					if (Existing is UhtProperty ExistingProperty)
					{
						//@TODO: This exception does not seem sound either, but there is enough existing code that it will need to be
						// fixed up first before the exception it is removed.
						bool bExistingPropDeprecated = ExistingProperty.PropertyFlags.HasAnyFlags(EPropertyFlags.Deprecated);
						bool bNewPropDeprecated = this.PropertyCategory == UhtPropertyCategory.Member && this.PropertyFlags.HasAnyFlags(EPropertyFlags.Deprecated);
						if (!bNewPropDeprecated && !bExistingPropDeprecated)
						{
							this.LogShadowingError(ExistingProperty);
						}
					}
					else if (Existing is UhtFunction ExistingFunction)
					{
						if (this.PropertyCategory == UhtPropertyCategory.Member)
						{
							this.LogShadowingError(ExistingFunction);
						}
					}
				}
			}

			Validate((UhtStruct)this.Outer, this, Options);
			return Options;
		}

		private void LogShadowingError(UhtType Shadows)
		{
			this.LogError($"{this.PropertyCategory.GetHintText()}: '{this.SourceName}' cannot be defined in '{this.Outer?.SourceName}' as it is already defined in scope '{Shadows.Outer?.SourceName}' (shadowing is not allowed)");
		}

		/// <summary>
		/// Validate the property settings
		/// </summary>
		/// <param name="OuterStruct">The outer structure for the property.  For properties inside containers, this will be the owning struct of the container</param>
		/// <param name="OutermostProperty">Outer most property being validated.  For properties in containers, 
		/// this will be the container property.  For properties outside of containers or the container itself, this will be the property.</param>
		/// <param name="Options"></param>
		public virtual void Validate(UhtStruct OuterStruct, UhtProperty OutermostProperty, UhtValidationOptions Options)
		{
			// Check for deprecation
			if (Options.HasAnyFlags(UhtValidationOptions.Deprecated) && !this.bDeprecated)
			{
				ValidateDeprecated();
			}

			// Validate the types allowed with arrays
			if (this.ArrayDimensions != null)
			{
				switch (this.PropertyCategory)
				{
					case UhtPropertyCategory.Return:
						this.LogError("Arrays aren't allowed as return types");
						break;

					case UhtPropertyCategory.RegularParameter:
					case UhtPropertyCategory.ReplicatedParameter:
						this.LogError("Arrays aren't allowed as function parameters");
						break;
				}

				if (this is UhtContainerBaseProperty)
				{
					this.LogError("Static arrays of containers are not allowed");
				}

				if (this is UhtBoolProperty)
				{
					this.LogError("Bool arrays are not allowed");
				}
			}

			if (!Options.HasAnyFlags(UhtValidationOptions.IsKey) && this.PropertyFlags.HasAnyFlags(EPropertyFlags.PersistentInstance) && !this.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeInstanced))
			{
				this.LogError("'Instanced' is only allowed on an object property, an array of objects, a set of objects, or a map with an object value type.");
			}

			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.Config) && !this.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanHaveConfig))
			{
				this.LogError("Not allowed to use 'config' with object variables");
			}

			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ExposeOnSpawn))
			{
				if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.DisableEditOnInstance))
				{
					this.LogWarning("Property cannot have both 'DisableEditOnInstance' and 'ExposeOnSpawn' flags");
				}
				if (!this.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintVisible))
				{
					this.LogWarning("Property cannot have 'ExposeOnSpawn' without 'BlueprintVisible' flag.");
				}
			}

			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintAssignable) && !this.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeBlueprintAssignable))
			{
				this.LogError("'BlueprintAssignable' is only allowed on multicast delegate properties");
			}

			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintCallable) && !this.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeBlueprintCallable))
			{
				this.LogError("'BlueprintCallable' is only allowed on multicast delegate properties");
			}

			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.BlueprintAuthorityOnly) && !this.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanBeBlueprintAuthorityOnly))
			{
				this.LogError("'BlueprintAuthorityOnly' is only allowed on multicast delegate properties");
			}

			// Check for invalid transients
			EPropertyFlags Transients = this.PropertyFlags & (EPropertyFlags.DuplicateTransient | EPropertyFlags.TextExportTransient | EPropertyFlags.NonPIEDuplicateTransient);
			if (Transients != 0 && !(OuterStruct is UhtClass))
			{
				this.LogError($"'{string.Join(", ", Transients.ToStringList(false))}' specifier(s) are only allowed on class member variables");
			}

			if (!Options.HasAnyFlags(UhtValidationOptions.IsKeyOrValue))
			{
				switch (this.PropertyCategory)
				{
					case UhtPropertyCategory.Member:
						ValidateMember(OuterStruct, Options);
						break;

					case UhtPropertyCategory.RegularParameter:
					case UhtPropertyCategory.ReplicatedParameter:
						ValidateFunctionArgument((UhtFunction)OuterStruct, Options);
						break;

					case UhtPropertyCategory.Return:
						break;
				}
			}
			return;
		}

		/// <summary>
		/// Validate that we don't reference any deprecated classes
		/// </summary>
		public virtual void ValidateDeprecated()
		{
		}

		/// <summary>
		/// Verify function argument
		/// </summary>
		protected virtual void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (this.PropertyFlags.HasExactFlags(EPropertyFlags.ReferenceParm | EPropertyFlags.ConstParm, EPropertyFlags.ReferenceParm))
				{
					this.LogError($"Replicated parameters cannot be passed by non-const reference");
				}

				if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest))
				{
					if (this.PropertyFlags.HasExactFlags(EPropertyFlags.OutParm | EPropertyFlags.RepSkip, EPropertyFlags.OutParm))
					{
						// This is difficult to trigger since NotReplicated also sets the property category
						this.LogError("Service request functions cannot contain out parameters, unless marked NotReplicated");
					}
				}
				else
				{
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
					{
						this.LogError("Replicated functions cannot contain out parameters");
					}

					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.RepSkip))
					{
						// This is difficult to trigger since NotReplicated also sets the property category
						this.LogError("Only service request functions cannot contain NotReplicated parameters");
					}
				}
			}

			// The following checks are not performed on the value of a container
			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.BlueprintEvent | EFunctionFlags.BlueprintCallable))
			{
				// Check that the parameter name is valid and does not conflict with pre-defined types
				if (InvalidParamNames.Contains(this.SourceName))
				{
					this.LogError($"Parameter name '{this.SourceName}' in function is invalid, '{this.SourceName}' is a reserved name.");
				}
			}
		}

		/// <summary>
		/// Validate member settings
		/// </summary>
		/// <param name="Struct">Containing struct.  This is either a UhtScriptStruct or UhtClass</param>
		/// <param name="Options">Validation options</param>
		protected virtual void ValidateMember(UhtStruct Struct, UhtValidationOptions Options)
		{
			if (!Options.HasAnyFlags(UhtValidationOptions.IsKeyOrValue))
			{
				// First check if the category was specified at all and if the property was exposed to the editor.
				string? Category;
				if (!this.MetaData.TryGetValue(UhtNames.Category, out Category))
				{
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.BlueprintVisible))
					{
						if (this.Package.bIsPartOfEngine)
						{
							this.LogError("An explicit Category specifier is required for any property exposed to the editor or Blueprints in an Engine module.");
						}
					}
				}

				// If the category was specified explicitly, it wins
				if (!string.IsNullOrEmpty(Category) && !this.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit | EPropertyFlags.BlueprintVisible |
					EPropertyFlags.BlueprintAssignable | EPropertyFlags.BlueprintCallable))
				{
					this.LogWarning("Property has a Category set but is not exposed to the editor or Blueprints with EditAnywhere, BlueprintReadWrite, " +
						"VisibleAnywhere, BlueprintReadOnly, BlueprintAssignable, BlueprintCallable keywords.");
				}
			}

			// Make sure that editblueprint variables are editable
			if (!this.PropertyFlags.HasAnyFlags(EPropertyFlags.Edit))
			{
				if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.DisableEditOnInstance))
				{
					this.LogError("Property cannot have 'DisableEditOnInstance' without being editable");
				}

				if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.DisableEditOnTemplate))
				{
					this.LogError("Property cannot have 'DisableEditOnTemplate' without being editable");
				}
			}

			string ExposeOnSpawnValue = this.MetaData.GetValueOrDefault(UhtNames.ExposeOnSpawn);
			if (ExposeOnSpawnValue.Equals("true", StringComparison.OrdinalIgnoreCase) && !this.PropertyCaps.HasAnyFlags(UhtPropertyCaps.CanExposeOnSpawn))
			{
				this.LogError("ExposeOnSpawn - Property cannot be exposed");
			}

			if (this.PropertyExportFlags.HasAnyFlags(UhtPropertyExportFlags.FieldNotify))
			{
				if (this.Outer is UhtClass Class)
				{
					if (Class.ClassType != UhtClassType.Class)
					{
						this.LogError("FieldNofity are not valid on UInterface.");
					}
				}	
				else
				{
					this.LogError("FieldNofity property are only valid as UClass member variable.");
				}
			}
		}

		/// <summary>
		/// Generate an error if the class has been deprecated
		/// </summary>
		/// <param name="Class">Class to check</param>
		protected void ValidateDeprecatedClass(UhtClass? Class)
		{
			if (Class == null)
			{
				return;
			}

			if (!Class.bDeprecated)
			{
				return;
			}

			if (this.PropertyCategory == UhtPropertyCategory.Member)
			{
				this.LogError($"Property is using a deprecated class: '{Class.SourceName}'.  Property should be marked deprecated as well.");
			}
			else
			{
				this.LogError($"Function is using a deprecated class: '{Class.SourceName}'.  Function should be marked deprecated as well.");
			}
		}

		/// <summary>
		/// Check to see if the property is valid as a member of a networked structure
		/// </summary>
		/// <param name="ReferencingProperty">The property referencing the structure property.  All error should be logged on the referencing property.</param>
		/// <returns>True if the property is valid, false if not.  If the property is not valid, an error should also be generated.</returns>
		public virtual bool ValidateStructPropertyOkForNet(UhtProperty ReferencingProperty)
		{
			return true;
		}

		/// <summary>
		/// Test to see if this property references something that requires the argument to be marked as const
		/// </summary>
		/// <param name="ErrorType">If const is required, returns the type that is forcing the const</param>
		/// <returns>True if the argument must be marked as const</returns>
		public virtual bool MustBeConstArgument([NotNullWhen(true)] out UhtType? ErrorType)
		{
			ErrorType = null;
			return false;
		}
		#endregion

		#region Reference support
		/// <inheritdoc/>
		public override void CollectReferences(IUhtReferenceCollector Collector)
		{
			CollectReferencesInternal(Collector, false);
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ParmFlags))
			{
				Collector.AddForwardDeclaration(this.GetForwardDeclarations());
			}
		}

		/// <summary>
		/// Collect the references for the property.  This method is used by container properties to
		/// collect the contained property's references.
		/// </summary>
		/// <param name="Collector">Reference collector</param>
		/// <param name="bIsTemplateProperty">If true, this is a property in a container</param>
		public virtual void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bIsTemplateProperty)
		{
		}

		/// <summary>
		/// Return a string of all the forward declarations.
		/// This method should be removed once C++ UHT is removed.
		/// This information can be collected via CollectReferences.
		/// </summary>
		/// <returns></returns>
		public virtual string? GetForwardDeclarations()
		{
			return null;
		}

		/// <summary>
		/// Enumerate all reference types.  This method is used exclusively by FindNoExportStructsRecursive
		/// </summary>
		/// <returns>Type enumerator</returns>
		public virtual IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			return Enumerable.Empty<UhtType>();
		}
		#endregion

		#region Helper methods
		/// <summary>
		/// Generate a new name suffix based on the current suffix and the new suffix
		/// </summary>
		/// <param name="OuterSuffix">Current suffix</param>
		/// <param name="NewSuffix">Suffix to be added</param>
		/// <returns>Combination of the two suffixes</returns>
		protected string GetNameSuffix(string OuterSuffix, string NewSuffix)
		{
			return string.IsNullOrEmpty(OuterSuffix) ? NewSuffix : $"{OuterSuffix}{NewSuffix}";
		}

		/// <summary>
		/// Test to see if the two properties are the same type
		/// </summary>
		/// <param name="Other">Other property to test</param>
		/// <returns>True if the properies are the same type</returns>
		public abstract bool IsSameType(UhtProperty Other);

		/// <summary>
		/// Test to see if the two properties are the same type and ConstParm/OutParm flags somewhat match
		/// </summary>
		/// <param name="Other">The other property to test</param>
		/// <returns></returns>
		public bool MatchesType(UhtProperty Other)
		{
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
			{
				if (!Other.PropertyFlags.HasAnyFlags(EPropertyFlags.OutParm))
				{
					return false;
				}

				if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm) && !Other.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
				{
					return false;
				}
			}
			if (this.bIsStaticArray != Other.bIsStaticArray)
			{
				return false;
			}
			return IsSameType(Other);
		}
		#endregion
	}

	/// <summary>
	/// Assorted StringBuilder extensions for properties
	/// </summary>
	public static class UhtPropertyStringBuilderExtensions
	{

		/// <summary>
		/// Add the given property text
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="TextType">Type of text to append</param>
		/// <param name="bIsTemplateArgument">If true, this is a template argument property</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendPropertyText(this StringBuilder Builder, UhtProperty Property, UhtPropertyTextType TextType, bool bIsTemplateArgument = false)
		{
			return Property.AppendText(Builder, TextType, bIsTemplateArgument);
		}

		/// <summary>
		/// Append the member declaration
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="Context">Context of the property</param>
		/// <param name="Name">Property name</param>
		/// <param name="NameSuffix">Name suffix</param>
		/// <param name="Tabs">Number of tabs in the formatting</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMemberDecl(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return Property.AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs);
		}

		/// <summary>
		/// Append the member definition
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="Context">Context of the property</param>
		/// <param name="Name">Property name</param>
		/// <param name="NameSuffix">Name suffix</param>
		/// <param name="Offset">Offset of the property in the parent</param>
		/// <param name="Tabs">Number of tabs in the formatting</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMemberDef(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			return Property.AppendMemberDef(Builder, Context, Name, NameSuffix, Offset, Tabs);
		}

		/// <summary>
		/// Append the member pointer
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="Context">Context of the property</param>
		/// <param name="Name">Property name</param>
		/// <param name="NameSuffix">Name suffix</param>
		/// <param name="Tabs">Number of tabs in the formatting</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendMemberPtr(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return Property.AppendMemberPtr(Builder, Context, Name, NameSuffix, Tabs);
		}

		/// <summary>
		/// Append the full declaration including such things as const, *, and &amp;
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="TextType">Type of text to append</param>
		/// <param name="bSkipParameterName">If true, don't append the parameter name</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFullDecl(this StringBuilder Builder, UhtProperty Property, UhtPropertyTextType TextType, bool bSkipParameterName)
		{
			return Property.AppendFullDecl(Builder, TextType, bSkipParameterName);
		}

		/// <summary>
		/// Append the function thunk parameter get
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFunctionThunkParameterGet(this StringBuilder Builder, UhtProperty Property)
		{
			return Property.AppendFunctionThunkParameterGet(Builder);
		}

		/// <summary>
		/// Append the function thunk parameter array type
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFunctionThunkParameterArrayType(this StringBuilder Builder, UhtProperty Property)
		{
			return Property.AppendText(Builder, UhtPropertyTextType.FunctionThunkParameterArrayType);
		}

		/// <summary>
		/// Append the function thunk parameter argument
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFunctionThunkParameterArg(this StringBuilder Builder, UhtProperty Property)
		{
			return Property.AppendFunctionThunkParameterArg(Builder);
		}

		/// <summary>
		/// Append the function thunk return
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFunctionThunkReturn(this StringBuilder Builder, UhtProperty Property)
		{
			return Property.AppendText(Builder, UhtPropertyTextType.FunctionThunkRetVal);
		}

		/// <summary>
		/// Append the function thunk parameter name
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFunctionThunkParameterName(this StringBuilder Builder, UhtProperty Property)
		{
			return Property.AppendFunctionThunkParameterName(Builder);
		}

		/// <summary>
		/// Append the sparse type
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendSparse(this StringBuilder Builder, UhtProperty Property)
		{
			return Property.AppendText(Builder, UhtPropertyTextType.Sparse);
		}

		/// <summary>
		/// Append the property's null constructor arg
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="bIsInitializer">If true this is in an initializer context.</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendNullConstructorArg(this StringBuilder Builder, UhtProperty Property, bool bIsInitializer)
		{
			Property.AppendNullConstructorArg(Builder, bIsInitializer);
			return Builder;
		}

		/// <summary>
		/// Append the replication notify function or a 'nullptr'
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendNotifyFunc(this StringBuilder Builder, UhtProperty Property)
		{
			if (Property.RepNotifyName != null)
			{
				Builder.AppendUTF8LiteralString(Property.RepNotifyName);
			}
			else
			{
				Builder.Append("nullptr");
			}
			return Builder;
		}

		/// <summary>
		/// Append the parameter flags
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="PropertyFlags">Property flags</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendFlags(this StringBuilder Builder, EPropertyFlags PropertyFlags)
		{
			PropertyFlags &= ~EPropertyFlags.ComputedFlags;
			return Builder.Append("(EPropertyFlags)0x").AppendFormat("{0:x16}", (ulong)PropertyFlags);
		}

		/// <summary>
		/// Append the property array dimensions as a CPP_ARRAY_DIM macro or '1' if not a fixed array.
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="Context">Context of the property</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendArrayDim(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context)
		{
			if (Property.ArrayDimensions != null)
			{
				Builder.Append("CPP_ARRAY_DIM(").Append(Property.SourceName).Append(", ").Append(Context.OuterStruct.SourceName).Append(')');
			}
			else
			{
				Builder.Append("1");
			}
			return Builder;
		}

		/// <summary>
		/// Given an object, append the hash (if applicable) to the builder
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="ReferingType">Type asking for an object hash</param>
		/// <param name="StartingLength">Initial length of the builder prior to appending the hashes</param>
		/// <param name="Context">Context used to lookup the hashes</param>
		/// <param name="Object">Object being appended</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendObjectHash(this StringBuilder Builder, int StartingLength, UhtType ReferingType, IUhtPropertyMemberContext Context, UhtObject? Object)
		{
			if (Object == null)
			{
				return Builder;
			}
			else if (Object is UhtClass Class)
			{
				if (Class.ClassExportFlags.HasAnyFlags(UhtClassExportFlags.NoExport))
				{
					return Builder;
				}
			}
			else if (Object is UhtScriptStruct ScriptStruct)
			{
				if (ScriptStruct.ScriptStructFlags.HasAnyFlags(EStructFlags.NoExport))
				{
					return Builder;
				}
			}

			Builder.Append(Builder.Length == StartingLength ? " // " : " ");
			uint Hash = Context.GetTypeHash(Object);
			if (Hash == 0)
			{
				string Type = ReferingType is UhtProperty ? "property" : "object";
				ReferingType.LogWarning($"The {Type} \"{ReferingType.SourceName}\" references type \"{Object.SourceName}\" but the code generation hash is zero");
			}
			Builder.Append(Context.GetTypeHash(Object));
			return Builder;
		}

		/// <summary>
		/// Given an object, append the hash (if applicable) to the builder
		/// </summary>
		/// <param name="Builder">Output builder</param>
		/// <param name="ReferingType">Type asking for an object hash</param>
		/// <param name="Context">Context used to lookup the hashes</param>
		/// <param name="Object">Object being appended</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendObjectHash(this StringBuilder Builder, UhtType ReferingType, IUhtPropertyMemberContext Context, UhtObject? Object)
		{
			return Builder.AppendObjectHash(Builder.Length, ReferingType, Context, Object);
		}

		/// <summary>
		/// Append the object hashes for all referenced objects
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <param name="Context">Context of the property</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendObjectHashes(this StringBuilder Builder, UhtProperty Property, IUhtPropertyMemberContext Context)
		{
			Property.AppendObjectHashes(Builder, Builder.Length, Context);
			return Builder;
		}

		/// <summary>
		/// Append the singleton name for the given type
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Context">Context of the property</param>
		/// <param name="Type">Type to append</param>
		/// <param name="bRegistered">If true, append the registered type singleton.</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendSingletonName(this StringBuilder Builder, IUhtPropertyMemberContext Context, UhtObject? Type, bool bRegistered)
		{
			return Builder.Append(Context.GetSingletonName(Type, bRegistered));
		}

		/// <summary>
		/// Append the getter wrapper name
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendPropertyGetterWrapperName(this StringBuilder Builder, UhtProperty Property)
		{
			return Builder.Append("Get").Append(Property.SourceName).Append("_WrapperImpl");
		}

		/// <summary>
		/// Append the setter wrapper name
		/// </summary>
		/// <param name="Builder">Destination builder</param>
		/// <param name="Property">Property in question</param>
		/// <returns>Destination builder</returns>
		public static StringBuilder AppendPropertySetterWrapperName(this StringBuilder Builder, UhtProperty Property)
		{
			return Builder.Append("Set").Append(Property.SourceName).Append("_WrapperImpl");
		}
	}
}
