// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using Horde.Build.Models;
using Horde.Build.Utilities;
using HordeCommon;

namespace Horde.Build.Api
{
	using TemplateRefId = StringId<TemplateRef>;

	/// <summary>
	/// Query selecting the base changelist to use
	/// </summary>
	public class ChangeQueryRequest
	{
		/// <summary>
		/// The template id to query
		/// </summary>
		public string? TemplateId { get; set; }

		/// <summary>
		/// The target to query
		/// </summary>
		public string? Target { get; set; }

		/// <summary>
		/// Whether to match a job that produced warnings
		/// </summary>
		public List<JobStepOutcome>? Outcomes { get; set; }

		/// <summary>
		/// Convert to a model object
		/// </summary>
		/// <returns></returns>
		public ChangeQuery ToModel()
		{
			ChangeQuery query = new ChangeQuery();
			if (TemplateId != null)
			{
				query.TemplateRefId = new StringId<TemplateRef>(TemplateId);
			}
			query.Target = Target;
			query.Outcomes = Outcomes;
			return query;
		}
	}

	/// <summary>
	/// Specifies defaults for running a preflight
	/// </summary>
	public class DefaultPreflightRequest
	{
		/// <summary>
		/// The template id to query
		/// </summary>
		public string? TemplateId { get; set; }

		/// <summary>
		/// The last successful job type to use for the base changelist
		/// </summary>
		[Obsolete("Use Change.TemplateId instead")]
		public string? ChangeTemplateId { get; set; }

		/// <summary>
		/// Query for the change to use
		/// </summary>
		public ChangeQueryRequest? Change { get; set; }

		/// <summary>
		/// Convert to a model object
		/// </summary>
		/// <returns></returns>
		public DefaultPreflight ToModel()
		{
#pragma warning disable CS0618 // Type or member is obsolete
			if (ChangeTemplateId != null)
			{
				Change ??= new ChangeQueryRequest();
				Change.TemplateId = ChangeTemplateId;
			}
			return new DefaultPreflight((TemplateId != null) ? (TemplateRefId?)new TemplateRefId(TemplateId) : null, Change?.ToModel());
#pragma warning restore CS0618 // Type or member is obsolete
		}
	}

	/// <summary>
	/// Mapping from a BuildGraph agent type to a set of machines on the farm
	/// </summary>
	public class CreateAgentTypeRequest
	{
		/// <summary>
		/// Pool of agents to use for this agent type
		/// </summary>
		[Required]
		public string Pool { get; set; } = null!;

		/// <summary>
		/// Name of the workspace to sync
		/// </summary>
		public string? Workspace { get; set; }

		/// <summary>
		/// Path to the temporary storage dir
		/// </summary>
		public string? TempStorageDir { get; set; }

		/// <summary>
		/// Environment variables to be set when executing the job
		/// </summary>
		public Dictionary<string, string>? Environment { get; set; }
	}

	/// <summary>
	/// Information about a workspace type
	/// </summary>
	public class CreateWorkspaceTypeRequest
	{
		/// <summary>
		/// Name of the Perforce server cluster to use
		/// </summary>
		public string? Cluster { get; set; }

		/// <summary>
		/// The Perforce server and port (eg. perforce:1666)
		/// </summary>
		public string? ServerAndPort { get; set; }

		/// <summary>
		/// User to log into Perforce with (defaults to buildmachine)
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Password to use to log into the workspace
		/// </summary>
		public string? Password { get; set; }

		/// <summary>
		/// Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.
		/// </summary>
		public string? Identifier { get; set; }

		/// <summary>
		/// Override for the stream to sync
		/// </summary>
		public string? Stream { get; set; }

		/// <summary>
		/// Custom view for the workspace
		/// </summary>
		public List<string>? View { get; set; }

		/// <summary>
		/// Whether to use an incrementally synced workspace
		/// </summary>
		public bool Incremental { get; set; }

		/// <summary>
		/// Whether to use the AutoSDK
		/// </summary>
		public bool UseAutoSdk { get; set; } = true;
	}

	/// <summary>
	/// Trigger for another template
	/// </summary>
	public class CreateChainedJobTemplateRequest
	{
		/// <summary>
		/// Name of the target that needs to complete before starting the other template
		/// </summary>
		[Required]
		public string Trigger { get; set; } = String.Empty;

		/// <summary>
		/// Id of the template to trigger
		/// </summary>
		[Required]
		public string TemplateId { get; set; } = String.Empty;
	}

	/// <summary>
	/// Parameters to create a template within a stream
	/// </summary>
	public class CreateTemplateRefRequest : CreateTemplateRequest
	{
		/// <summary>
		/// Optional identifier for this ref. If not specified, an id will be generated from the name.
		/// </summary>
		public string? Id { get; set; }

		/// <summary>
		/// Whether to show badges in UGS for these jobs
		/// </summary>
		public bool ShowUgsBadges { get; set; }

		/// <summary>
		/// Whether to show alerts in UGS for these jobs
		/// </summary>
		public bool ShowUgsAlerts { get; set; }

		/// <summary>
		/// Notification channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Triage channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// Schedule to execute this template
		/// </summary>
		public CreateScheduleRequest? Schedule { get; set; }

		/// <summary>
		/// List of chained job triggers
		/// </summary>
		public List<CreateChainedJobTemplateRequest>? ChainedJobs { get; set; }

		/// <summary>
		/// The ACL for this template
		/// </summary>
		public UpdateAclRequest? Acl { get; set; }
	}

	/// <summary>
	/// Mapping from a BuildGraph agent type to a set of machines on the farm
	/// </summary>
	public class GetAgentTypeResponse
	{
		/// <summary>
		/// Pool of agents to use for this agent type
		/// </summary>
		public string Pool { get; set; }

		/// <summary>
		/// Name of the workspace to sync
		/// </summary>
		public string? Workspace { get; set; }

		/// <summary>
		/// Path to the temporary storage dir
		/// </summary>
		public string? TempStorageDir { get; set; }

		/// <summary>
		/// Environment variables to be set when executing the job
		/// </summary>
		public Dictionary<string, string>? Environment { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="pool">Pool of agents to use for this agent type</param>
		/// <param name="workspace">Name of the workspace to sync</param>
		/// <param name="tempStorageDir">Path to the temp storage directory</param>
		/// <param name="environment">Environment variables to be set when executing this job</param>
		public GetAgentTypeResponse(string pool, string? workspace, string? tempStorageDir, Dictionary<string, string>? environment)
		{
			Pool = pool;
			Workspace = workspace;
			TempStorageDir = tempStorageDir;
			Environment = environment;
		}
	}

	/// <summary>
	/// Information about a workspace type
	/// </summary>
	public class GetWorkspaceTypeResponse
	{
		/// <summary>
		/// The Perforce server cluster
		/// </summary>
		public string? Cluster { get; set; }

		/// <summary>
		/// The Perforce server and port (eg. perforce:1666)
		/// </summary>
		public string? ServerAndPort { get; set; }

		/// <summary>
		/// User to log into Perforce with (defaults to buildmachine)
		/// </summary>
		public string? UserName { get; set; }

		/// <summary>
		/// Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.
		/// </summary>
		public string? Identifier { get; set; }

		/// <summary>
		/// Override for the stream to sync
		/// </summary>
		public string? Stream { get; set; }

		/// <summary>
		/// Custom view for the workspace
		/// </summary>
		public List<string>? View { get; set; }

		/// <summary>
		/// Whether to use an incrementally synced workspace
		/// </summary>
		public bool Incremental { get; set; }

		/// <summary>
		/// Whether to use the AutoSDK
		/// </summary>
		public bool UseAutoSdk { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public GetWorkspaceTypeResponse()
		{
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="cluster">The server cluster</param>
		/// <param name="serverAndPort">The perforce server</param>
		/// <param name="userName">The perforce user name</param>
		/// <param name="identifier">Identifier to distinguish this workspace from other workspaces. Defaults to the workspace type name.</param>
		/// <param name="stream">Override for the stream to sync</param>
		/// <param name="view">Custom view for the workspace</param>
		/// <param name="bIncremental">Whether to use an incrementally synced workspace</param>
		/// <param name="bUseAutoSdk">Whether to use the AutoSDK</param>
		public GetWorkspaceTypeResponse(string? cluster, string? serverAndPort, string? userName, string? identifier, string? stream, List<string>? view, bool bIncremental, bool bUseAutoSdk)
		{
			Cluster = cluster;
			ServerAndPort = serverAndPort;
			UserName = userName;
			Identifier = identifier;
			Stream = stream;
			View = view;
			Incremental = bIncremental;
			UseAutoSdk = bUseAutoSdk;
		}
	}

	/// <summary>
	/// Trigger for another template
	/// </summary>
	public class GetChainedJobTemplateResponse
	{
		/// <summary>
		/// Name of the target that needs to complete before starting the other template
		/// </summary>
		public string Trigger { get; set; }

		/// <summary>
		/// Id of the template to trigger
		/// </summary>
		public string TemplateId { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="trigger">The trigger definition</param>
		public GetChainedJobTemplateResponse(ChainedJobTemplate trigger)
		{
			Trigger = trigger.Trigger;
			TemplateId = trigger.TemplateRefId.ToString();
		}
	}

	/// <summary>
	/// Information about a template in this stream
	/// </summary>
	public class GetTemplateRefResponse : GetTemplateResponseBase
	{
		/// <summary>
		/// Id of the template ref
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Hash of the template definition
		/// </summary>
		public string Hash { get; set; }

		/// <summary>
		/// Whether to show badges in UGS for these jobs
		/// </summary>
		public bool ShowUgsBadges { get; set; }

		/// <summary>
		/// Whether to show alerts in UGS for these jobs
		/// </summary>
		public bool ShowUgsAlerts { get; set; }

		/// <summary>
		/// Notification channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Triage channel for this template. Overrides the stream channel if set.
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// The schedule for this ref
		/// </summary>
		public GetScheduleResponse? Schedule { get; set; }

		/// <summary>
		/// List of templates to trigger
		/// </summary>
		public List<GetChainedJobTemplateResponse>? ChainedJobs { get; set; }

		/// <summary>
		/// ACL for this template
		/// </summary>
		public GetAclResponse? Acl { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">The template ref id</param>
		/// <param name="templateRef">The template ref</param>
		/// <param name="template">The template instance</param>
		/// <param name="bIncludeAcl">Whether to include the ACL in the response</param>
		public GetTemplateRefResponse(TemplateRefId id, TemplateRef templateRef, ITemplate template, bool bIncludeAcl)
			: base(template)
		{
			Id = id.ToString();
			Hash = templateRef.Hash.ToString();
			ShowUgsBadges = templateRef.ShowUgsBadges;
			ShowUgsAlerts = templateRef.ShowUgsAlerts;
			NotificationChannel = templateRef.NotificationChannel;
			NotificationChannelFilter = templateRef.NotificationChannelFilter;
			Schedule = (templateRef.Schedule != null) ? new GetScheduleResponse(templateRef.Schedule) : null;
			ChainedJobs = (templateRef.ChainedJobs != null && templateRef.ChainedJobs.Count > 0) ? templateRef.ChainedJobs.ConvertAll(x => new GetChainedJobTemplateResponse(x)) : null;
			Acl = (bIncludeAcl && templateRef.Acl != null)? new GetAclResponse(templateRef.Acl) : null;
		}
	}

	/// <summary>
	/// Response describing a stream
	/// </summary>
	public class GetStreamResponse
	{
		/// <summary>
		/// Unique id of the stream
		/// </summary>
		public string Id { get; set; }

		/// <summary>
		/// Unique id of the project containing this stream
		/// </summary>
		public string ProjectId { get; set; }

		/// <summary>
		/// Name of the stream
		/// </summary>
		public string Name { get; set; }

		/// <summary>
		/// The config file path on the server
		/// </summary>
		public string ConfigPath { get; set; } = String.Empty;

		/// <summary>
		/// Revision of the config file 
		/// </summary>
		public string ConfigRevision { get; set; } = String.Empty;

		/// <summary>
		/// Order to display in the list
		/// </summary>
		public int Order { get; set; }

		/// <summary>
		/// Notification channel for all jobs in this stream
		/// </summary>
		public string? NotificationChannel { get; set; }

		/// <summary>
		/// Notification channel filter for this template. Can be Success|Failure|Warnings
		/// </summary>
		public string? NotificationChannelFilter { get; set; }

		/// <summary>
		/// Channel to post issue triage notifications
		/// </summary>
		public string? TriageChannel { get; set; }

		/// <summary>
		/// Default template for running preflights
		/// </summary>
		public string? DefaultPreflightTemplate { get; set; }

		/// <summary>
		/// Default template to use for preflights
		/// </summary>
		public DefaultPreflightRequest? DefaultPreflight { get; set; }

		/// <summary>
		/// List of tabs to display for this stream
		/// </summary>
		public List<GetStreamTabResponse> Tabs { get; set; }

		/// <summary>
		/// Map of agent name to type
		/// </summary>
		public Dictionary<string, GetAgentTypeResponse> AgentTypes { get; set; }

		/// <summary>
		/// Map of workspace name to type
		/// </summary>
		public Dictionary<string, GetWorkspaceTypeResponse>? WorkspaceTypes { get; set; }

		/// <summary>
		/// Templates for jobs in this stream
		/// </summary>
		public List<GetTemplateRefResponse> Templates { get; set; }

		/// <summary>
		/// Custom permissions for this object
		/// </summary>
		public GetAclResponse? Acl { get; set; }
		
		/// <summary>
		/// Stream paused for new builds until this date
		/// </summary>
		public DateTime? PausedUntil { get; set; }
		
		/// <summary>
		/// Reason for stream being paused
		/// </summary>
		public string? PauseComment { get; set; }

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="id">Unique id of the stream</param>
		/// <param name="projectId">Unique id of the project containing the stream</param>
		/// <param name="name">Name of the stream</param>
		/// <param name="configPath">Path to the config file for this stream</param>
		/// <param name="configRevision">The config file path on the server</param>
		/// <param name="order">Order to display this stream</param>
		/// <param name="notificationChannel"></param>
		/// <param name="notificationChannelFilter"></param>
		/// <param name="triageChannel"></param>
		/// <param name="defaultPreflight">The default template to use for preflights</param>
		/// <param name="tabs">List of tabs to display for this stream</param>
		/// <param name="agentTypes">Map of agent type name to description</param>
		/// <param name="workspaceTypes">Map of workspace name to description</param>
		/// <param name="templates">Templates for this stream</param>
		/// <param name="acl">Permissions for this object</param>
		/// <param name="pausedUntil">Stream paused for new builds until this date</param>
		/// <param name="pauseComment">Reason for stream being paused</param>
		public GetStreamResponse(string id, string projectId, string name, string configPath, string configRevision, int order, string? notificationChannel, string? notificationChannelFilter, string? triageChannel, DefaultPreflightRequest? defaultPreflight, List<GetStreamTabResponse> tabs, Dictionary<string, GetAgentTypeResponse> agentTypes, Dictionary<string, GetWorkspaceTypeResponse>? workspaceTypes, List<GetTemplateRefResponse> templates, GetAclResponse? acl, DateTime? pausedUntil, string? pauseComment)
		{
			Id = id;
			ProjectId = projectId;
			Name = name;
			ConfigPath = configPath;
			ConfigRevision = configRevision;
			Order = order;
			NotificationChannel = notificationChannel;
			NotificationChannelFilter = notificationChannelFilter;
			TriageChannel = triageChannel;
			DefaultPreflightTemplate = defaultPreflight?.TemplateId;
			DefaultPreflight = defaultPreflight;
			Tabs = tabs;
			AgentTypes = agentTypes;
			WorkspaceTypes = workspaceTypes;
			Templates = templates;
			Acl = acl;
			PausedUntil = pausedUntil;
			PauseComment = pauseComment;
		}
	}
}
