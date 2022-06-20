// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using EpicGames.BuildGraph.Expressions;
using EpicGames.Core;
using Microsoft.Extensions.Logging;

namespace EpicGames.BuildGraph
{
	/// <summary>
	/// Specification for a diagnostic message
	/// </summary>
	public class BgDiagnosticSpec : IBgExpr<BgDiagnosticSpec>
	{
		/// <summary>
		/// Verbosity to output the message at
		/// </summary>
		public LogLevel Level { get; }

		/// <summary>
		/// The message to display
		/// </summary>
		public BgString Message { get; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="level">Output level</param>
		/// <param name="message"></param>
		public BgDiagnosticSpec(LogLevel level, BgString message)
		{
			Level = level;
			Message = message;
		}

		/// <summary>
		/// Adds a diagnostic to the graph
		/// </summary>
		internal void AddToGraph(BgExprContext context, BgGraph graph, BgAgent? enclosingAgent, BgNode? enclosingNode)
		{
			BgScriptLocation location = new BgScriptLocation("(unknown)", "(unknown)", 1);
			string messageValue = Message.Compute(context);
			graph.Diagnostics.Add(new BgGraphDiagnostic(location, (LogEventType)Level, messageValue, enclosingNode, enclosingAgent));
		}

		/// <inheritdoc/>
		public BgDiagnosticSpec IfThen(BgBool condition, BgDiagnosticSpec valueIfTrue) => throw new NotImplementedException();

		/// <inheritdoc/>
		object IBgExpr.Compute(BgExprContext context) => this;

		/// <inheritdoc/>
		public BgString ToBgString() => Message;
	}
}
