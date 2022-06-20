// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// FUInt32Property
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "UInt32Property", IsProperty = true)]
	public class UhtUInt32Property : UhtNumericProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "UInt32Property"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "uint32"; }

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="IntType">Integer type</param>
		public UhtUInt32Property(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType) : base(PropertySettings, IntType)
		{
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs,
				this.IntType == UhtPropertyIntType.Unsized ? "FUnsizedFIntPropertyParams" : "FUInt32PropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs,
				this.IntType == UhtPropertyIntType.Unsized ? "FUnsizedFIntPropertyParams" : "FUInt32PropertyParams",
				"UECodeGen_Private::EPropertyGenFlags::UInt32");
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			return false;
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtUInt32Property;
		}

		#region Keyword
		[UhtPropertyType(Keyword = "uint32", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		private static UhtProperty? UInt32Property(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (PropertySettings.bIsBitfield)
			{
				return new UhtBoolProperty(PropertySettings, UhtBoolType.UInt8);
			}
			else
			{
				return new UhtUInt32Property(PropertySettings, UhtPropertyIntType.Sized);
			}
		}

		[UhtPropertyType(Keyword = "unsigned", Options = UhtPropertyTypeOptions.Immediate)]
		private static UhtProperty? UnsignedProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			TokenReader
				.Require("unsigned")
				.Optional("int");
			return new UhtUInt32Property(PropertySettings, UhtPropertyIntType.Unsized);
		}
		#endregion
	}
}
