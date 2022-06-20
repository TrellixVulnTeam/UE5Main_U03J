// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Parser.Matchers
{
	class SystemicEventMatcher : ILogEventMatcher
	{
		public LogEventMatch? Match(ILogCursor cursor)
		{
			if (cursor.IsMatch(@"LogDerivedDataCache: .*queries/writes will be limited"))
			{
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_SlowDDC);
			}
			if (cursor.IsMatch(@"^\s*ERROR: Error: EC_OK"))
			{
				LogEventBuilder builder = new LogEventBuilder(cursor);
				while (builder.Next.IsMatch(@"^\s*ERROR:\s*$"))
				{
					builder.MoveNext();
				}
				return new LogEventBuilder(cursor).ToMatch(LogEventPriority.High, LogLevel.Information, KnownLogEvents.Systemic_PdbUtil);
			}
			return null;
		}
	}
}
