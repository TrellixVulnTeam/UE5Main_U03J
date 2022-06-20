// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Threading;

namespace EpicGames.UHT.Tokenizer
{

	/// <summary>
	/// Token reader to replay previously recorded token stream
	/// </summary>
	public class UhtTokenReplayReader : IUhtTokenReader, IUhtMessageLineNumber
	{
		static private ThreadLocal<UhtTokenReplayReader> Tls = new ThreadLocal<UhtTokenReplayReader>(() => new UhtTokenReplayReader());

		const int MaxSavedStates = 2;
		private struct SavedState
		{
			public int TokenIndex;
			public bool bHasToken;
		}

		private IUhtMessageSite MessageSiteInternal;
		private ReadOnlyMemory<UhtToken> Tokens;
		private ReadOnlyMemory<char> Data;
		private int CurrentTokenIndex = -1;
		private bool bHasToken = false;
		private UhtToken CurrentToken = new UhtToken();
		private SavedState[] SavedStates = new SavedState[MaxSavedStates];
		private int SavedStateCount = 0;
		private UhtTokenType EndTokenType = UhtTokenType.EndOfFile;

		/// <summary>
		/// Construct new token reader
		/// </summary>
		/// <param name="MessageSite">Message site for generating errors</param>
		/// <param name="Data">Complete data for the token (i.e. original source)</param>
		/// <param name="Tokens">Tokens to replay</param>
		/// <param name="EndTokenType">Token type to return when end of tokens reached</param>
		public UhtTokenReplayReader(IUhtMessageSite MessageSite, ReadOnlyMemory<char> Data, ReadOnlyMemory<UhtToken> Tokens, UhtTokenType EndTokenType)
		{
			this.MessageSiteInternal = MessageSite;
			this.Tokens = new UhtToken[0].AsMemory();
			this.Data = new char[0].AsMemory();
			this.EndTokenType = EndTokenType;
			this.CurrentToken = new UhtToken(EndTokenType);
		}

		/// <summary>
		/// Construct token replay reader intended for caching.  Use Reset method to prepare it for use
		/// </summary>
		public UhtTokenReplayReader()
		{
			this.MessageSiteInternal = new UhtEmptyMessageSite();
			this.Tokens = new UhtToken[0].AsMemory();
			this.Data = new char[0].AsMemory();
		}

		/// <summary>
		/// Reset a cached replay reader for replaying a new stream of tokens
		/// </summary>
		/// <param name="MessageSite">Message site for generating errors</param>
		/// <param name="Data">Complete data for the token (i.e. original source)</param>
		/// <param name="Tokens">Tokens to replay</param>
		/// <param name="EndTokenType">Token type to return when end of tokens reached</param>
		/// <returns>The replay reader</returns>
		public UhtTokenReplayReader Reset(IUhtMessageSite MessageSite, ReadOnlyMemory<char> Data, ReadOnlyMemory<UhtToken> Tokens, UhtTokenType EndTokenType)
		{
			this.MessageSiteInternal = MessageSite;
			this.Tokens = Tokens;
			this.Data = Data;
			this.CurrentTokenIndex = -1;
			this.bHasToken = false;
			this.CurrentToken = new UhtToken(EndTokenType);
			this.SavedStateCount = 0;
			this.EndTokenType = EndTokenType;
			return this;
		}

		/// <summary>
		/// Return the replay reader associated with the current thread.  Only one replay reader is cached per thread.
		/// </summary>
		/// <param name="MessageSite">The message site used to log errors</param>
		/// <param name="Data">Source data where tokens were originally parsed</param>
		/// <param name="Tokens">Collection of tokens to replay</param>
		/// <param name="EndTokenType">Type of end token marker to return when the end of the token list is reached.  This is used to produce errors in the context of the replay</param>
		/// <returns>The threaded instance of the replay reader</returns>
		/// <exception cref="UhtIceException">Thrown if the TLS value can not be retrieved.</exception>
		public static UhtTokenReplayReader GetThreadInstance(IUhtMessageSite MessageSite, ReadOnlyMemory<char> Data, ReadOnlyMemory<UhtToken> Tokens, UhtTokenType EndTokenType)
		{
			UhtTokenReplayReader? Reader = Tls.Value;
			if (Reader == null)
			{
				throw new UhtIceException("Unable to acquire a UhtTokenReplayReader");
			}
			Reader.Reset(MessageSite, Data, Tokens, EndTokenType);
			return Reader;
		}

		#region IUHTMessageSite Implementation
		IUhtMessageSession IUhtMessageSite.MessageSession => this.MessageSiteInternal.MessageSession;
		IUhtMessageSource? IUhtMessageSite.MessageSource => this.MessageSiteInternal.MessageSource;
		IUhtMessageLineNumber? IUhtMessageSite.MessageLineNumber => this;
		#endregion

		#region ITokenReader Implementation
		/// <inheritdoc/>
		public bool bIsEOF
		{
			get
			{
				if (!this.bHasToken)
				{
					GetTokenInternal();
				}
				return this.CurrentTokenIndex == this.Tokens.Span.Length;
			}
		}

		/// <inheritdoc/>
		public int InputPos 
		{
			get
			{
				if (!this.bHasToken)
				{
					GetTokenInternal();
				}
				return CurrentToken.InputStartPos;
			}
		}

		/// <inheritdoc/>
		public int InputLine 
		{
			get
			{
				if (!this.bHasToken)
				{
					GetTokenInternal();
				}
				return CurrentToken.InputLine;
			}
			set => throw new NotImplementedException(); 
		}

		/// <inheritdoc/>
		public IUhtTokenPreprocessor? TokenPreprocessor { get => throw new NotImplementedException(); set => throw new NotImplementedException(); }

		/// <inheritdoc/>
		public int LookAheadEnableCount { get => throw new NotImplementedException(); set => throw new NotImplementedException(); }

		/// <inheritdoc/>
		public ReadOnlySpan<StringView> Comments => throw new NotImplementedException();

		/// <inheritdoc/>
		public void ClearComments()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public void ConsumeToken()
		{
			this.bHasToken = false;
		}

		/// <inheritdoc/>
		public void DisableComments()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public void EnableComments()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public void CommitPendingComments()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public UhtToken GetLine()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public StringView GetRawString(char Terminator, UhtRawStringOptions Options)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public StringView GetStringView(int StartPos, int Count)
		{
			return new StringView(this.Data, StartPos, Count);
		}

		/// <inheritdoc/>
		public UhtToken GetToken()
		{
			UhtToken Token = PeekToken();
			ConsumeToken();
			return Token;
		}

		/// <inheritdoc/>
		public bool IsFirstTokenInLine(ref UhtToken Token)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public ref UhtToken PeekToken()
		{
			if (!this.bHasToken)
			{
				GetTokenInternal();
			}
			return ref this.CurrentToken;
		}

		/// <inheritdoc/>
		public void SkipWhitespaceAndComments()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public void SaveState()
		{
			if (this.SavedStateCount == MaxSavedStates)
			{
				throw new UhtIceException("Token reader saved states full");
			}
			this.SavedStates[this.SavedStateCount] = new SavedState { TokenIndex = this.CurrentTokenIndex, bHasToken = this.bHasToken };
			++this.SavedStateCount;
		}

		/// <inheritdoc/>
		public void RestoreState()
		{
			if (this.SavedStateCount == 0)
			{
				throw new UhtIceException("Attempt to restore a state when none have been saved");
			}

			--this.SavedStateCount;
			this.CurrentTokenIndex = this.SavedStates[this.SavedStateCount].TokenIndex;
			this.bHasToken = this.SavedStates[this.SavedStateCount].bHasToken;
		}

		/// <inheritdoc/>
		public void AbandonState()
		{
			if (this.SavedStateCount == 0)
			{
				throw new UhtIceException("Attempt to abandon a state when none have been saved");
			}

			--this.SavedStateCount;
		}

		/// <inheritdoc/>
		public void EnableRecording()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public void DisableRecording()
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public void RecordToken(ref UhtToken Token)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public List<UhtToken> RecordedTokens
		{
			get
			{
				throw new NotImplementedException();
			}
		}
		#endregion

		#region IUHTMessageLineNumber implementation
		int IUhtMessageLineNumber.MessageLineNumber
		{
			get
			{
				if (this.Tokens.Length == 0)
				{
					return -1;
				}
				if (this.CurrentTokenIndex < 0)
				{
					return this.Tokens.Span[0].InputLine;
				}
				if (this.CurrentTokenIndex < this.Tokens.Length)
				{
					return this.Tokens.Span[this.CurrentTokenIndex].InputLine;
				}
				return this.Tokens.Span[this.Tokens.Length - 1].InputLine;
			}
		}
		#endregion

		private ref UhtToken GetTokenInternal()
		{
			if (this.CurrentTokenIndex < this.Tokens.Length)
			{
				++this.CurrentTokenIndex;
			}
			if (this.CurrentTokenIndex < this.Tokens.Length)
			{
				this.CurrentToken = this.Tokens.Span[this.CurrentTokenIndex];
			}
			else if (this.Tokens.Length == 0)
			{
				this.CurrentToken = new UhtToken();
			}
			else
			{
				UhtToken LastToken = this.Tokens.Span[this.Tokens.Length - 1];
				int EndPos = LastToken.InputEndPos;
				this.CurrentToken = new UhtToken(this.EndTokenType, EndPos, LastToken.InputLine, EndPos, LastToken.InputLine, new StringView());
			}
			this.bHasToken = true;
			return ref this.CurrentToken;
		}
	}
}
