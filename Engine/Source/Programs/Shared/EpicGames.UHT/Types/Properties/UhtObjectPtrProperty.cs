// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Types
{
	/// <summary>
	/// FObjectPtrProperty
	/// </summary>
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "ObjectPtrProperty", IsProperty = true)]
	public class UhtObjectPtrProperty : UhtObjectProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "ObjectPtrProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "ObjectPtr"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "OBJECTPTR"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		/// <summary>
		/// Construct a new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="Class">Referenced class</param>
		/// <param name="ExtraFlags">Extra property flags to apply</param>
		public UhtObjectPtrProperty(UhtPropertySettings PropertySettings, UhtClass Class, EPropertyFlags ExtraFlags = EPropertyFlags.None)
			: base(PropertySettings, Class)
		{
			this.PropertyFlags |= ExtraFlags | EPropertyFlags.UObjectWrapper;
			this.PropertyCaps |= UhtPropertyCaps.PassCppArgsByRef;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				case UhtPropertyTextType.GetterSetterArg:
					if (bIsTemplateArgument)
					{
						Builder.Append("TObjectPtr<").Append(this.Class.SourceName).Append(">");
					}
					else
					{
						Builder.Append(this.Class.SourceName).Append("*");
					}
					break;

				case UhtPropertyTextType.FunctionThunkRetVal:
					if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.ConstParm))
					{
						Builder.Append("const ");
					}
					Builder.Append("TObjectPtr<").Append(this.Class.SourceName).Append(">");
					break;

				default:
					Builder.Append("TObjectPtr<").Append(this.Class.SourceName).Append(">");
					break;
			}
			return Builder;
		}
		
		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FObjectPtrPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FObjectPtrPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Object | UECodeGen_Private::EPropertyGenFlags::ObjectPtr");
			AppendMemberDefRef(Builder, Context, this.Class, false);
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

		#region Keyword
		[UhtPropertyType(Keyword = "TObjectPtr")]
		private static UhtProperty? ObjectPtrProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			UhtSession Session = PropertySettings.Outer.Session;
			int TypeStartPos = TokenReader.PeekToken().InputStartPos;

			UhtClass? PropertyClass = ParseTemplateObject(PropertySettings, TokenReader, MatchedToken, true);
			if (PropertyClass == null)
			{
				return null;
			}

			ConditionalLogPointerUsage(PropertySettings, Session.Config!.EngineObjectPtrMemberBehavior, 
				Session.Config!.NonEngineObjectPtrMemberBehavior, "ObjectPtr", TokenReader, TypeStartPos, null);

			if (PropertyClass.IsChildOf(PropertyClass.Session.UClass))
			{
				// UObject specifies that there is no limiter
				return new UhtClassPtrProperty(PropertySettings, PropertyClass, PropertyClass.Session.UObject);
			}
			else
			{
				return new UhtObjectPtrProperty(PropertySettings, PropertyClass);
			}
		}
		#endregion
	}
}
