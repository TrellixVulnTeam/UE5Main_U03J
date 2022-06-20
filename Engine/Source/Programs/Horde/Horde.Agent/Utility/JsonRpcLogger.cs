// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers;
using System.Collections.Generic;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Channels;
using System.Threading.Tasks;
using Horde.Agent.Utility;
using HordeCommon;
using HordeCommon.Rpc;
using Microsoft.Extensions.Logging;

namespace Horde.Agent.Parser
{
	/// <summary>
	/// Class to handle uploading log data to the server in the background
	/// </summary>
	class JsonRpcLogger : JsonLogger, IAsyncDisposable
	{
		class QueueItem
		{
			public byte[] Data { get; }
			public CreateEventRequest? CreateEvent { get; }

			public QueueItem(byte[] data, CreateEventRequest? createEvent)
			{
				Data = data;
				CreateEvent = createEvent;
			}

			public override string ToString()
			{
				return Encoding.UTF8.GetString(Data);
			}
		}

		readonly IRpcConnection _rpcClient;
		readonly string _logId;
		readonly string? _jobId;
		readonly string? _jobBatchId;
		readonly string? _jobStepId;
		readonly Channel<QueueItem> _dataChannel;
		Task? _dataWriter;

		/// <summary>
		/// The current outcome for this step. Updated to reflect any errors and warnings that occurred.
		/// </summary>
		public JobStepOutcome Outcome
		{
			get;
			private set;
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="rpcClient">RPC client to use for server requests</param>
		/// <param name="logId">The log id to write to</param>
		/// <param name="jobId">Id of the job being executed</param>
		/// <param name="jobBatchId">Batch being executed</param>
		/// <param name="jobStepId">Id of the step being executed</param>
		/// <param name="warnings">Whether to include warnings in the output</param>
		/// <param name="inner">Additional logger to write to</param>
		public JsonRpcLogger(IRpcConnection rpcClient, string logId, string? jobId, string? jobBatchId, string? jobStepId, bool? warnings, ILogger inner)
			: base(warnings, inner)
		{
			_rpcClient = rpcClient;
			_logId = logId;
			_jobId = jobId;
			_jobBatchId = jobBatchId;
			_jobStepId = jobStepId;
			_dataChannel = Channel.CreateUnbounded<QueueItem>();
			_dataWriter = Task.Run(() => RunDataWriter());
			Outcome = JobStepOutcome.Success;
		}

		protected override void WriteFormattedEvent(LogLevel level, int lineIndex, int lineCount, byte[] line)
		{
			// Update the state of this job if this is an error status
			if (level == LogLevel.Error || level == LogLevel.Critical)
			{
				Outcome = JobStepOutcome.Failure;
			}
			else if (level == LogLevel.Warning && Outcome != JobStepOutcome.Failure)
			{
				Outcome = JobStepOutcome.Warnings;
			}

			// If we want an event for this log event, create one now
			CreateEventRequest? eventRequest = null;
			if (lineIndex == 0)
			{
				if (level == LogLevel.Warning || level == LogLevel.Error || level == LogLevel.Critical)
				{
					eventRequest = CreateEvent(level, lineCount);
				}
			}

			// Write the data to the output channel
			QueueItem queueItem = new QueueItem(line, eventRequest);
			if (!_dataChannel.Writer.TryWrite(queueItem))
			{
				throw new InvalidOperationException("Expected unbounded writer to complete immediately");
			}
		}

		/// <summary>
		/// Callback to write a systemic event
		/// </summary>
		/// <param name="eventId">The event id</param>
		/// <param name="text">The event text</param>
		protected override void WriteSystemicEvent(EventId eventId, string text)
		{
			if (_jobId == null)
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId}: {Text}", eventId.Id, text);
			}
			else if (_jobBatchId == null)
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId} (job {JobId}): {Text})", eventId.Id, _jobId, text);
			}
			else if (_jobStepId == null)
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId} (job batch {JobId}:{BatchId}): {Text})", eventId.Id, _jobId, _jobStepId, text);
			}
			else
			{
				Inner.LogWarning("Systemic event {KnownLogEventId} in log {LogId} (job step {JobId}:{BatchId}:{StepId}): {Text}", eventId.Id, _logId, _jobId, _jobBatchId, _jobStepId, text);
			}
		}

		/// <summary>
		/// Makes a <see cref="CreateEventRequest"/> for the given parameters
		/// </summary>
		/// <param name="logLevel">Level for this log event</param>
		/// <param name="lineCount">Number of lines in the event</param>
		CreateEventRequest CreateEvent(LogLevel logLevel, int lineCount)
		{
			EventSeverity severity = (logLevel == LogLevel.Warning) ? EventSeverity.Warning : EventSeverity.Error;
			return new CreateEventRequest(severity, _logId, 0, lineCount);
		}

		/// <summary>
		/// Stops the log writer's background task
		/// </summary>
		/// <returns>Async task</returns>
		public async Task StopAsync()
		{
			if (_dataWriter != null)
			{
				_dataChannel.Writer.TryComplete();
				await _dataWriter;
				_dataWriter = null;
			}
		}

		/// <summary>
		/// Dispose of this object. Call StopAsync() to stop asynchronously.
		/// </summary>
		public async ValueTask DisposeAsync()
		{
			await StopAsync();
		}

		/// <summary>
		/// Upload the log data to the server in the background
		/// </summary>
		/// <returns>Async task</returns>
		async Task RunDataWriter()
		{
			// Current position and line number in the log file
			long offset = 0;
			int lineIndex = 0;

			// Total number of errors and warnings
			const int MaxErrors = 50;
			int numErrors = 0;
			const int MaxWarnings = 50;
			int numWarnings = 0;

			// Buffers for chunks and events read in a single iteration
			ArrayBufferWriter<byte> writer = new ArrayBufferWriter<byte>();
			List<CreateEventRequest> events = new List<CreateEventRequest>();

			// Line separator for JSON events
			byte[] newline = { (byte)'\n' };
			JsonEncodedText timestamp = JsonEncodedText.Encode("");

			// The current jobstep outcome
			JobStepOutcome postedOutcome = JobStepOutcome.Success;

			// Whether we've written the flush command
			for (; ; )
			{
				writer.Clear();
				events.Clear();

				// Save off the current line number for sending to the server
				int initialLineIndex = lineIndex;

				// Get the next data
				Task waitTask = Task.Delay(TimeSpan.FromSeconds(2.0));
				while (writer.WrittenCount < 256 * 1024)
				{
					QueueItem? data;
					if (_dataChannel.Reader.TryRead(out data))
					{
						if (data.CreateEvent != null)
						{
							if ((data.CreateEvent.Severity == EventSeverity.Warning && ++numWarnings <= MaxWarnings) || (data.CreateEvent.Severity == EventSeverity.Error && ++numErrors <= MaxErrors))
							{
								data.CreateEvent.LineIndex = lineIndex;
								events.Add(data.CreateEvent);
							}
						}

						writer.Write(data.Data);
						writer.Write(newline);

						lineIndex++;
					}
					else
					{
						Task<bool> readTask = _dataChannel.Reader.WaitToReadAsync().AsTask();
						if (await Task.WhenAny(readTask, waitTask) == waitTask)
						{
							break;
						}
						if (!await readTask)
						{
							break;
						}
					}
				}

				// Upload it to the server
				if (writer.WrittenCount > 0)
				{
					byte[] data = writer.WrittenSpan.ToArray();
					try
					{
						await _rpcClient.InvokeAsync(x => x.WriteOutputAsync(new WriteOutputRequest(_logId, offset, initialLineIndex, data, false)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception ex)
					{
						Inner.LogWarning(ex, "Unable to write data to server (log {LogId}, offset {Offset})", _logId, offset);
					}
					offset += data.Length;
				}

				// Write all the events
				if (events.Count > 0)
				{
					try
					{
						await _rpcClient.InvokeAsync(x => x.CreateEventsAsync(new CreateEventsRequest(events)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception ex)
					{
						Inner.LogWarning(ex, "Unable to create events");
					}
				}

				// Update the outcome of this jobstep
				if (_jobId != null && _jobBatchId != null && _jobStepId != null && Outcome != postedOutcome)
				{
					try
					{
						await _rpcClient.InvokeAsync(x => x.UpdateStepAsync(new UpdateStepRequest(_jobId, _jobBatchId, _jobStepId, JobStepState.Unspecified, Outcome)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception ex)
					{
						Inner.LogWarning(ex, "Unable to update step outcome to {NewOutcome}", Outcome);
					}
					postedOutcome = Outcome;
				}

				// Wait for more data to be available
				if (!await _dataChannel.Reader.WaitToReadAsync())
				{
					try
					{
						await _rpcClient.InvokeAsync(x => x.WriteOutputAsync(new WriteOutputRequest(_logId, offset, lineIndex, Array.Empty<byte>(), true)), new RpcContext(), CancellationToken.None);
					}
					catch (Exception ex)
					{
						Inner.LogWarning(ex, "Unable to flush data to server (log {LogId}, offset {Offset})", _logId, offset);
					}
					break;
				}
			}
		}
	}
}
