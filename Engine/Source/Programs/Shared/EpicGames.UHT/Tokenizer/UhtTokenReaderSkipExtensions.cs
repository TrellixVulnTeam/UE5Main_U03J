// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Tokenizer
{
	/// <summary>
	/// Series of extensions to token reading that are far too specialized to be included in the reader.
	/// </summary>
	public static class UhtTokenReaderSkipExtensions
	{

		/// <summary>
		/// Skip a token regardless of the type.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader SkipOne(this IUhtTokenReader TokenReader)
		{
			TokenReader.PeekToken();
			TokenReader.ConsumeToken();
			return TokenReader;
		}

		/// <summary>
		/// Skip an alignas expression
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader SkipAlignasIfNecessary(this IUhtTokenReader TokenReader)
		{
			const string Identifier = "alignas";
			if (TokenReader.TryOptional(Identifier))
			{
				TokenReader
					.Require('(', Identifier)
					.RequireConstInt(Identifier)
					.Require(')', Identifier);
			}
			return TokenReader;
		}

		/// <summary>
		/// Skip deprecation macro
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader SkipDeprecatedMacroIfNecessary(this IUhtTokenReader TokenReader)
		{
			if (TokenReader.TryOptional(new string[] { "DEPRECATED", "UE_DEPRECATED" }) >= 0)
			{
				TokenReader
					.Require('(', "deprecation macro")
					.RequireConstFloat("version in deprecation macro")
					.Require(',', "deprecation macro")
					.RequireConstString("message in deprecation macro")
					.Require(')', "deprecation macro");
			}
			return TokenReader;
		}

		/// <summary>
		/// Skip alignas and/or deprecation macros
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader SkipAlignasAndDeprecatedMacroIfNecessary(this IUhtTokenReader TokenReader)
		{
			// alignas() can come before or after the deprecation macro.
			// We can't have both, but the compiler will catch that anyway.
			return TokenReader
				.SkipAlignasIfNecessary()
				.SkipDeprecatedMacroIfNecessary()
				.SkipAlignasIfNecessary();
		}

		/// <summary>
		/// Skip any block of tokens wrapped by the given token symbols
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Initiator">Initiating token (i.e. &quot;(&quot;)</param>
		/// <param name="Terminator">Terminating token (i.e. &quot;)&quot;)</param>
		/// <param name="InitialNesting">If true, start with an initial nesting count of one (assume we already parsed an initiator)</param>
		/// <param name="ExceptionContext">Extra context for any errors</param>
		/// <returns>Token reader</returns>
		/// <exception cref="UhtTokenException">Throw if end of file is reached</exception>
		public static IUhtTokenReader SkipBrackets(this IUhtTokenReader TokenReader, char Initiator, char Terminator, int InitialNesting, object? ExceptionContext = null)
		{
			int Nesting = InitialNesting;
			if (Nesting == 0)
			{
				TokenReader.Require(Initiator, ExceptionContext);
				++Nesting;
			}

			do
			{
				UhtToken SkipToken = TokenReader.GetToken();
				if (SkipToken.TokenType.IsEndType())
				{
					throw new UhtTokenException(TokenReader, SkipToken, Terminator, ExceptionContext);
				}
				else if (SkipToken.IsSymbol(Initiator))
				{
					++Nesting;
				}
				else if (SkipToken.IsSymbol(Terminator))
				{
					--Nesting;
				}
			} while (Nesting != 0);
			return TokenReader;
		}

		/// <summary>
		/// Skip tokens until the given terminator is found.  The terminator will not be consumed.
		/// </summary>
		/// <param name="TokenReader">Token reader</param>
		/// <param name="Terminator">Terminator to skip until</param>
		/// <param name="ExceptionContext">Extra context for any exceptions</param>
		/// <returns>Token reader</returns>
		public static IUhtTokenReader SkipUntil(this IUhtTokenReader TokenReader, char Terminator, object? ExceptionContext = null)
		{
			while (true)
			{
				ref UhtToken SkipToken = ref TokenReader.PeekToken();
				if (SkipToken.TokenType.IsEndType())
				{
					throw new UhtTokenException(TokenReader, SkipToken, Terminator, ExceptionContext);
				}
				else if (SkipToken.IsSymbol(Terminator))
				{
					break;
				}
				TokenReader.ConsumeToken();
			}
			return TokenReader;
		}
	}
}
