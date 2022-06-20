// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// FClassPtrProperty
	/// </summary>
	[UhtEngineClass(Name = "ClassPtrProperty", IsProperty = true)]
	public class UhtClassPtrProperty : UhtClassProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "ClassPtrProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "ObjectPtr"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "OBJECTPTR"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="PropertySettings"></param>
		/// <param name="Class">Referenced class</param>
		/// <param name="MetaClass">Meta data class</param>
		/// <param name="ExtraFlags">Extra property flags to apply to the property</param>
		public UhtClassPtrProperty(UhtPropertySettings PropertySettings, UhtClass Class, UhtClass MetaClass, EPropertyFlags ExtraFlags = EPropertyFlags.None)
			: base(PropertySettings, Class, MetaClass)
		{
			this.PropertyFlags |= ExtraFlags | EPropertyFlags.UObjectWrapper;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				default:
					Builder.Append("TObjectPtr<").Append(this.Class.SourceName).Append(">");
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FClassPtrPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FClassPtrPropertyParams", 
				"UECodeGen_Private::EPropertyGenFlags::Class | UECodeGen_Private::EPropertyGenFlags::ObjectPtr");
			AppendMemberDefRef(Builder, Context, this.Class, false);
			AppendMemberDefRef(Builder, Context, this.MetaClass, false);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override void Validate(UhtStruct OuterStruct, UhtProperty OutermostProperty, UhtValidationOptions Options)
		{
			base.Validate(OuterStruct, OutermostProperty, Options);

			// UFunctions with a smart pointer as input parameter wont compile anyway, because of missing P_GET_... macro.
			// UFunctions with a smart pointer as return type will crash when called via blueprint, because they are not supported in VM.
			if (this.PropertyCategory == UhtPropertyCategory.RegularParameter || this.PropertyCategory == UhtPropertyCategory.ReplicatedParameter)
			{
				OuterStruct.LogError("UFunctions cannot take a TObjectPtr as a parameter.");
			}
		}
	}
}
