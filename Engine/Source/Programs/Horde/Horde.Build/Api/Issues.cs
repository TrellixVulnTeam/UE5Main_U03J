// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using EpicGames.Core;
using Horde.Build.Models;
using Horde.Build.Utilities;
using MongoDB.Bson;

namespace Horde.Build.Api
{
	using StreamId = StringId<IStream>;
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Identifies a particular changelist and job
	/// </summary>
	public class GetIssueStepResponse
	{
		/// <summary>
		/// The changelist number
		/// </summary>
		public int Change { get; set; }

		/// <summary>
		/// Severity of the issue in this step
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Name of the job containing this step
		/// </summary>
		public string JobName { get; set; }

		/// <summary>
		/// The unique job id
		/// </summary>
		public string JobId { get; set; }

		/// <summary>
		/// The unique batch id
		/// </summary>
		public string BatchId { get; set; }

		/// <summary>
		/// The unique step id
		/// </summary>
		public string StepId { get; set; }

		/// <summary>
		/// Time at which the step ran
		/// </summary>
		public DateTime StepTime { get; set; }

		/// <summary>
		/// The unique log id
		/// </summary>
		public string? LogId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="issueStep">The issue step to construct from</param>
		public GetIssueStepResponse(IIssueStep issueStep)
		{
			Change = issueStep.Change;
			Severity = issueStep.Severity;
			JobName = issueStep.JobName;
			JobId = issueStep.JobId.ToString();
			BatchId = issueStep.BatchId.ToString();
			StepId = issueStep.StepId.ToString();
			StepTime = issueStep.StepTime;
			LogId = issueStep.LogId?.ToString();
		}
	}

	/// <summary>
	/// Trace of a set of node failures across multiple steps
	/// </summary>
	public class GetIssueSpanResponse
	{
		/// <summary>
		/// Unique id of this span
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// The template containing this step
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Name of the step
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The previous build 
		/// </summary>
		public GetIssueStepResponse? LastSuccess { get; set; }

		/// <summary>
		/// The failing builds for a particular event
		/// </summary>
		public List<GetIssueStepResponse> Steps { get; set; }

		/// <summary>
		/// The following successful build
		/// </summary>
		public GetIssueStepResponse? NextSuccess { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="span">The node to construct from</param>
		/// <param name="steps">Failing steps for this span</param>
		public GetIssueSpanResponse(IIssueSpan span, List<IIssueStep> steps)
		{
			Id = span.Id.ToString();
			Name = span.NodeName;
			TemplateId = span.TemplateRefId.ToString();
			LastSuccess = (span.LastSuccess != null) ? new GetIssueStepResponse(span.LastSuccess) : null;
			Steps = steps.ConvertAll(x => new GetIssueStepResponse(x));
			NextSuccess = (span.NextSuccess != null) ? new GetIssueStepResponse(span.NextSuccess) : null;
		}
	}

	/// <summary>
	/// Information about a particular step
	/// </summary>
	public class GetIssueStreamResponse
	{
		/// <summary>
		/// Unique id of the stream
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// Minimum changelist affected by this issue (ie. last successful build)
		/// </summary>
		public int? MinChange { get; set; }

		/// <summary>
		/// Maximum changelist affected by this issue (ie. next successful build)
		/// </summary>
		public int? MaxChange { get; set; }

		/// <summary>
		/// Map of steps to (event signature id -> trace id)
		/// </summary>
		public List<GetIssueSpanResponse> Nodes { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="streamId">The stream to construct from</param>
		/// <param name="spans">List of spans for the given stream</param>
		/// <param name="steps">List of steps for the given stream</param>
		public GetIssueStreamResponse(StreamId streamId, List<IIssueSpan> spans, List<IIssueStep> steps)
		{
			StreamId = streamId.ToString();

			foreach (IIssueSpan span in spans)
			{
				if (span.LastSuccess != null && (MinChange == null || span.LastSuccess.Change < MinChange.Value))
				{
					MinChange = span.LastSuccess.Change;
				}
				if (span.NextSuccess != null && (MaxChange == null || span.NextSuccess.Change > MaxChange.Value))
				{
					MaxChange = span.NextSuccess.Change;
				}
			}

			Nodes = spans.ConvertAll(x => new GetIssueSpanResponse(x, steps.Where(y => y.SpanId == x.Id).ToList()));
		}
	}

	/// <summary>
	/// Outcome of a particular build
	/// </summary>
	public enum IssueBuildOutcome
	{
		/// <summary>
		/// Unknown outcome
		/// </summary>
		Unknown,

		/// <summary>
		/// Build succeeded
		/// </summary>
		Success,

		/// <summary>
		/// Build failed
		/// </summary>
		Error,

		/// <summary>
		/// Build finished with warnings
		/// </summary>
		Warning,
	}

	/// <summary>
	/// Information about a diagnostic
	/// </summary>
	public class GetIssueDiagnosticResponse
	{
		/// <summary>
		/// The corresponding build id
		/// </summary>
		public long? BuildId { get; set; }

		/// <summary>
		/// Message for the diagnostic
		/// </summary>
		public string Message { get; set; }

		/// <summary>
		/// Link to the error
		/// </summary>
		public Uri Url { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="buildId">The corresponding build id</param>
		/// <param name="message">Message for the diagnostic</param>
		/// <param name="url">Link to the diagnostic</param>
		public GetIssueDiagnosticResponse(long? buildId, string message, Uri url)
		{
			BuildId = buildId;
			Message = message;
			Url = url;
		}
	}

	/// <summary>
	/// Information about a template affected by an issue
	/// </summary>
	public class GetIssueAffectedTemplateResponse
	{
		/// <summary>
		/// The template id
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// The template name
		/// </summary>
		public string TemplateName { get; set; }

		/// <summary>
		/// Whether it has been resolved or not
		/// </summary>
		public bool Resolved { get; set; }

		/// <summary>
		/// The issue severity of the affected template
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="templateId"></param>
		/// <param name="templateName"></param>
		/// <param name="resolved"></param>
		/// <param name="severity"></param>
		public GetIssueAffectedTemplateResponse(string templateId, string templateName, bool resolved, IssueSeverity severity = IssueSeverity.Unspecified)
		{
			TemplateId = templateId;
			TemplateName = templateName;
			Resolved = resolved;
			Severity = severity;
		}
	}

	/// <summary>
	/// Summary for the state of a stream in an issue
	/// </summary>
	public class GetIssueAffectedStreamResponse
	{
		/// <summary>
		/// Id of the stream
		/// </summary>
		public string StreamId { get; set; }

		/// <summary>
		/// Name of the stream
		/// </summary>
		public string StreamName { get; set; }

		/// <summary>
		/// Whether the issue has been resolved in this stream
		/// </summary>
		public bool Resolved { get; set; }

		/// <summary>
		/// The affected templates
		/// </summary>
		public List<GetIssueAffectedTemplateResponse> AffectedTemplates { get; set; }

		/// <summary>
		/// List of affected template ids
		/// </summary>
		public List<string> TemplateIds { get; set; }

		/// <summary>
		/// List of resolved template ids
		/// </summary>
		public List<string> ResolvedTemplateIds { get; set; }

		/// <summary>
		/// List of unresolved template ids
		/// </summary>
		public List<string> UnresolvedTemplateIds { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="details">Issue to construct from</param>
		/// <param name="stream"></param>
		/// <param name="spans">The spans to construct from</param>
		public GetIssueAffectedStreamResponse(IIssueDetails details, IStream? stream, IEnumerable<IIssueSpan> spans)
		{
			IIssueSpan firstSpan = spans.First();
			StreamId = firstSpan.StreamId.ToString();
			StreamName = firstSpan.StreamName;
			Resolved = spans.All(x => x.NextSuccess != null);

			AffectedTemplates = new List<GetIssueAffectedTemplateResponse>();
			foreach (IGrouping<TemplateRefId, IIssueSpan> template in spans.GroupBy(x => x.TemplateRefId))
			{
				string templateName = template.Key.ToString();
				if (stream != null && stream.Templates.TryGetValue(template.Key, out TemplateRef? templateRef))
				{
					templateName = templateRef.Name;
				}

				HashSet<ObjectId> unresolvedTemplateSpans = new HashSet<ObjectId>(template.Where(x => x.NextSuccess == null).Select(x => x.Id));

				IIssueStep? templateStep = details.Steps.Where(x => unresolvedTemplateSpans.Contains(x.SpanId)).OrderByDescending(x => x.StepTime).FirstOrDefault();

				AffectedTemplates.Add(new GetIssueAffectedTemplateResponse(template.Key.ToString(), templateName, template.All(x => x.NextSuccess != null), templateStep?.Severity ?? IssueSeverity.Unspecified));
			}

			HashSet<TemplateRefId> templateIdsSet = new HashSet<TemplateRefId>(spans.Select(x => x.TemplateRefId));
			TemplateIds = templateIdsSet.Select(x => x.ToString()).ToList();

			HashSet<TemplateRefId> unresolvedTemplateIdsSet = new HashSet<TemplateRefId>(spans.Where(x => x.NextSuccess == null).Select(x => x.TemplateRefId));
			UnresolvedTemplateIds = unresolvedTemplateIdsSet.Select(x => x.ToString()).ToList();
			ResolvedTemplateIds = templateIdsSet.Except(unresolvedTemplateIdsSet).Select(x => x.ToString()).ToList();
		}
	}

	/// <summary>
	/// Stores information about a build health issue
	/// </summary>
	public class GetIssueResponse
	{
		/// <summary>
		/// The unique object id
		/// </summary>
		public int Id { get; set; }

		/// <summary>
		/// Time at which the issue was created
		/// </summary>
		public DateTime CreatedAt { get; set; }

		/// <summary>
		/// Time at which the issue was retrieved
		/// </summary>
		public DateTime RetrievedAt { get; set; }

		/// <summary>
		/// The associated project for the issue
		/// </summary>
		public string? Project { get; set; }

		/// <summary>
		/// The summary text for this issue
		/// </summary>
		public string Summary { get; set; }

		/// <summary>
		/// Detailed description text
		/// </summary>
		public string? Description { get; set; }

		/// <summary>
		/// Severity of this issue
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Whether the issue is promoted
		/// </summary>
		public bool Promoted { get; set; }

		/// <summary>
		/// Owner of the issue [DEPRECATED]
		/// </summary>
		public string? Owner { get; set; }

		/// <summary>
		/// User id of the owner [DEPRECATED]
		/// </summary>
		public string? OwnerId { get; set; }

		/// <summary>
		/// Owner of the issue
		/// </summary>
		public GetThinUserInfoResponse? OwnerInfo { get; set; }

		/// <summary>
		/// User that nominated the current owner [DEPRECATED]
		/// </summary>
		public string? NominatedBy { get; set; }

		/// <summary>
		/// Owner of the issue
		/// </summary>
		public GetThinUserInfoResponse? NominatedByInfo { get; set; }

		/// <summary>
		/// Time that the issue was acknowledged
		/// </summary>
		public DateTime? AcknowledgedAt { get; set; }

		/// <summary>
		/// Changelist that fixed this issue
		/// </summary>
		public int? FixChange { get; set; }

		/// <summary>
		/// Time at which the issue was resolved
		/// </summary>
		public DateTime? ResolvedAt { get; set; }

		/// <summary>
		/// Name of the user that resolved the issue [DEPRECATED]
		/// </summary>
		public string? ResolvedBy { get; set; }

		/// <summary>
		/// User id of the person that resolved the issue [DEPRECATED]
		/// </summary>
		public string? ResolvedById { get; set; }

		/// <summary>
		/// User that resolved the issue
		/// </summary>
		public GetThinUserInfoResponse? ResolvedByInfo { get; set; }

		/// <summary>
		/// Time at which the issue was verified
		/// </summary>
		public DateTime? VerifiedAt { get; set; }

		/// <summary>
		/// Time that the issue was last seen
		/// </summary>
		public DateTime LastSeenAt { get; set; }

		/// <summary>
		/// List of stream paths affected by this issue
		/// </summary>
		public List<string> Streams { get; set; }

		/// <summary>
		/// List of affected stream ids
		/// </summary>
		public List<string> ResolvedStreams { get; set; }

		/// <summary>
		/// List of unresolved streams
		/// </summary>
		public List<string> UnresolvedStreams { get; set; }

		/// <summary>
		/// List of affected streams
		/// </summary>
		public List<GetIssueAffectedStreamResponse> AffectedStreams { get; set; }

		/// <summary>
		/// Most likely suspects for causing this issue [DEPRECATED]
		/// </summary>
		public List<string> PrimarySuspects { get; set; }

		/// <summary>
		/// User ids of the most likely suspects [DEPRECATED]
		/// </summary>
		public List<string> PrimarySuspectIds { get; set; }

		/// <summary>
		/// Most likely suspects for causing this issue
		/// </summary>
		public List<GetThinUserInfoResponse> PrimarySuspectsInfo { get; set; }

		/// <summary>
		/// Whether to show alerts for this issue
		/// </summary>
		public bool ShowDesktopAlerts { get; set; }

		/// <summary>
		/// Constructs a new issue
		/// </summary>
		/// <param name="details">Issue to construct from</param>
		/// <param name="affectedStreams">The affected streams</param>
		/// <param name="showDesktopAlerts">Whether to show alerts for this issue</param>
		public GetIssueResponse(IIssueDetails details, List<GetIssueAffectedStreamResponse> affectedStreams, bool showDesktopAlerts)
		{
			IIssue issue = details.Issue;
			Id = issue.Id;
			CreatedAt = issue.CreatedAt;
			RetrievedAt = DateTime.UtcNow;
			Summary = String.IsNullOrEmpty(issue.UserSummary)? issue.Summary : issue.UserSummary;
			Description = issue.Description;
			Severity = issue.Severity;
			Promoted = issue.Promoted;
			Owner = details.Owner?.Login;
			OwnerId = details.Owner?.Id.ToString();
			if(details.Owner != null)
			{
				OwnerInfo = new GetThinUserInfoResponse(details.Owner);
			}
			NominatedBy = details.NominatedBy?.Login;
			if (details.NominatedBy != null)
			{
				NominatedByInfo = new GetThinUserInfoResponse(details.NominatedBy);
			}
			AcknowledgedAt = issue.AcknowledgedAt;
			FixChange = issue.FixChange;
			ResolvedAt = issue.ResolvedAt;
			ResolvedBy = details.ResolvedBy?.Login;
			ResolvedById = details.ResolvedBy?.Id.ToString();
			if (details.ResolvedBy != null)
			{
				ResolvedByInfo = new GetThinUserInfoResponse(details.ResolvedBy);
			}
			VerifiedAt = issue.VerifiedAt;
			LastSeenAt = issue.LastSeenAt;
			Streams = details.Spans.Select(x => x.StreamName).Distinct().ToList()!;
			ResolvedStreams = new List<string>();
			UnresolvedStreams = new List<string>();
			AffectedStreams = affectedStreams;
			foreach (IGrouping<StreamId, IIssueSpan> stream in details.Spans.GroupBy(x => x.StreamId))
			{
				if (stream.All(x => x.NextSuccess != null))
				{
					ResolvedStreams.Add(stream.Key.ToString());
				}
				else
				{
					UnresolvedStreams.Add(stream.Key.ToString());
				}
			}
			PrimarySuspects = details.SuspectUsers.Where(x => x.Login != null).Select(x => x.Login).ToList();
			PrimarySuspectIds= details.SuspectUsers.Select(x => x.Id.ToString()).ToList();
			PrimarySuspectsInfo = details.SuspectUsers.ConvertAll(x => new GetThinUserInfoResponse(x));
			ShowDesktopAlerts = showDesktopAlerts;
		}
	}

	/// <summary>
	/// Information about a span within an issue
	/// </summary>
	public class FindIssueSpanResponse
	{
		/// <summary>
		/// Unique id of this span
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// The template containing this step
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Name of the step
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The previous build 
		/// </summary>
		public GetIssueStepResponse? LastSuccess { get; set; }

		/// <summary>
		/// The following successful build
		/// </summary>
		public GetIssueStepResponse? NextSuccess { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="span"></param>
		public FindIssueSpanResponse(IIssueSpan span)
		{
			Id = span.Id.ToString();
			TemplateId = span.TemplateRefId.ToString();
			Name = span.NodeName;
			if (span.LastSuccess != null)
			{
				LastSuccess = new GetIssueStepResponse(span.LastSuccess);
			}
			if (span.NextSuccess != null)
			{
				NextSuccess = new GetIssueStepResponse(span.NextSuccess);
			}
		}
	}

	/// <summary>
	/// Stores information about a build health issue
	/// </summary>
	public class FindIssueResponse
	{
		/// <summary>
		/// The unique object id
		/// </summary>
		public int Id { get; set; }

		/// <summary>
		/// Time at which the issue was created
		/// </summary>
		public DateTime CreatedAt { get; set; }

		/// <summary>
		/// Time at which the issue was retrieved
		/// </summary>
		public DateTime RetrievedAt { get; set; }

		/// <summary>
		/// The associated project for the issue
		/// </summary>
		public string? Project { get; set; }

		/// <summary>
		/// The summary text for this issue
		/// </summary>
		public string Summary { get; set; }

		/// <summary>
		/// Detailed description text
		/// </summary>
		public string? Description { get; set; }

		/// <summary>
		/// Severity of this issue
		/// </summary>
		public IssueSeverity Severity { get; set; }

		/// <summary>
		/// Severity of this issue in the stream
		/// </summary>
		public IssueSeverity? StreamSeverity { get; set; }

		/// <summary>
		/// Whether the issue is promoted
		/// </summary>
		public bool Promoted { get; set; }

		/// <summary>
		/// Owner of the issue
		/// </summary>
		public GetThinUserInfoResponse? Owner { get; set; }

		/// <summary>
		/// Owner of the issue
		/// </summary>
		public GetThinUserInfoResponse? NominatedBy { get; set; }

		/// <summary>
		/// Time that the issue was acknowledged
		/// </summary>
		public DateTime? AcknowledgedAt { get; set; }

		/// <summary>
		/// Changelist that fixed this issue
		/// </summary>
		public int? FixChange { get; set; }

		/// <summary>
		/// Time at which the issue was resolved
		/// </summary>
		public DateTime? ResolvedAt { get; set; }

		/// <summary>
		/// User that resolved the issue
		/// </summary>
		public GetThinUserInfoResponse? ResolvedBy { get; set; }

		/// <summary>
		/// Time at which the issue was verified
		/// </summary>
		public DateTime? VerifiedAt { get; set; }

		/// <summary>
		/// Time that the issue was last seen
		/// </summary>
		public DateTime LastSeenAt { get; set; }

		/// <summary>
		/// Spans for this issue
		/// </summary>
		public List<FindIssueSpanResponse> Spans { get; set; }

		/// <summary>
		/// Constructs a new issue
		/// </summary>
		/// <param name="issue">The isseu information</param>
		/// <param name="owner">Owner of the issue</param>
		/// <param name="nominatedBy">User that nominated the current fixer</param>
		/// <param name="resolvedBy">User that resolved the issue</param>
		/// <param name="streamSeverity">The current severity in the stream</param>
		/// <param name="spans">Spans for this issue</param>
		public FindIssueResponse(IIssue issue, IUser? owner, IUser? nominatedBy, IUser? resolvedBy, IssueSeverity? streamSeverity, List<FindIssueSpanResponse> spans)
		{
			Id = issue.Id;
			CreatedAt = issue.CreatedAt;
			RetrievedAt = DateTime.UtcNow;
			Summary = String.IsNullOrEmpty(issue.UserSummary) ? issue.Summary : issue.UserSummary;
			Description = issue.Description;
			Severity = issue.Severity;
			StreamSeverity = streamSeverity;
			Promoted = issue.Promoted;
			if (owner != null)
			{
				Owner = new GetThinUserInfoResponse(owner);
			}
			if (nominatedBy != null)
			{
				NominatedBy = new GetThinUserInfoResponse(nominatedBy);
			}
			AcknowledgedAt = issue.AcknowledgedAt;
			FixChange = issue.FixChange;
			ResolvedAt = issue.ResolvedAt;
			if (resolvedBy != null)
			{
				ResolvedBy = new GetThinUserInfoResponse(resolvedBy);
			}
			VerifiedAt = issue.VerifiedAt;
			LastSeenAt = issue.LastSeenAt;
			Spans = spans;
		}
	}

	/// <summary>
	/// Request an issue to be updated
	/// </summary>
	public class UpdateIssueRequest
	{
		/// <summary>
		/// Summary of the issue
		/// </summary>
		public string? Summary { get; set; }

		/// <summary>
		/// Description of the issue
		/// </summary>
		public string? Description { get; set; }

		/// <summary>
		/// Whether the issue is promoted or not
		/// </summary>
		public bool? Promoted { get; set; }

		/// <summary>
		/// New user id for owner of the issue, can be cleared by passing empty string
		/// </summary>
		public string? OwnerId { get; set; }

		/// <summary>
		/// User id that nominated the new owner
		/// </summary>
		public string? NominatedById { get; set; }

		/// <summary>
		/// Whether the issue has been acknowledged
		/// </summary>
		public bool? Acknowledged { get; set; }

		/// <summary>
		/// Whether the user has declined this issue
		/// </summary>
		public bool? Declined { get; set; }

		/// <summary>
		/// The change at which the issue is claimed fixed. 0 = not fixed, -1 = systemic issue.
		/// </summary>
		public int? FixChange { get; set; }

		/// <summary>
		/// Whether the issue should be marked as resolved
		/// </summary>
		public bool? Resolved { get; set; }

		/// <summary>
		/// List of spans to add to this issue
		/// </summary>
		public List<string>? AddSpans { get; set; }

		/// <summary>
		/// List of spans to remove from this issue
		/// </summary>
		public List<string>? RemoveSpans { get; set; }
	}
}
