﻿// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.ComponentModel.DataAnnotations;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using EpicGames.Horde.Storage;
using Horde.Storage.Implementation;
using Jupiter.Implementation;
using Microsoft.AspNetCore.Authorization;
using Microsoft.AspNetCore.Mvc;
using Microsoft.Extensions.Configuration;
using Newtonsoft.Json;

namespace Horde.Storage.Controllers
{
    [ApiController]
    [Route("api/v1/admin")]
    [InternalApiFilter]
    public class AdminController : Controller
    {
        private readonly LastAccessService _lastAccessService;
        private readonly IRefCleanup _refCleanup;
        private readonly BlobCleanupService _blobCleanupService;
        private readonly IConfiguration _configuration;

        public AdminController(LastAccessService lastAccessService, IRefCleanup refCleanup, BlobCleanupService blobCleanupService, IConfiguration configuration)
        {
            _lastAccessService = lastAccessService;
            _refCleanup = refCleanup;
            _blobCleanupService = blobCleanupService;
            _configuration = configuration;
        }

        /// <summary>
        /// Report last access times to refs store
        /// </summary>
        /// <remarks>
        /// Manually triggers a aggregation of the last access records and commits these to the refs store. This is done automatically so the only reason to use this endpoint is for debugging purposes.
        /// </remarks>
        /// <returns></returns>
        [HttpPost("startLastAccessRollup")]
        [Authorize("Admin")]
        [ProducesResponseType(type: typeof(UpdatedRecordsResponse), 200)]
        public async Task<IActionResult> StartLastAccessRollup()
        {
            Task<List<(RefRecord, DateTime)>>? updateRecordsTask = _lastAccessService.ProcessLastAccessRecords();
            List<(RefRecord, DateTime)>? updatedRecords = null;
            if (updateRecordsTask != null)
            {
                updatedRecords = await updateRecordsTask;
            }

            return Ok(new UpdatedRecordsResponse(
                updatedRecords?.Select(tuple => new UpdatedRecordsResponse.UpdatedRecord(tuple.Item1, tuple.Item2))
                    .ToList() ??
                new List<UpdatedRecordsResponse.UpdatedRecord>()));
        }

        /// <summary>
        /// Manually run the refs cleanup
        /// </summary>
        /// <remarks>
        /// Manually triggers a cleanup of the refs keys based on last access time. This is done automatically so the only reason to use this endpoint is for debugging purposes.
        /// </remarks>
        /// <returns></returns>
        [HttpPost("refCleanup/{ns}")]
        public async Task<IActionResult> RefCleanup([FromRoute] [Required] NamespaceId ns)
        {
            int countOfDeletedRecords = await _refCleanup.Cleanup(ns, CancellationToken.None);
            return Ok(new RemovedRefRecordsResponse(countOfDeletedRecords));
        }
        
        /// <summary>
        /// Dumps all settings currently in use
        /// </summary>
        /// <returns></returns>
        [HttpGet("settings")]
        public IActionResult Settings()
        {
            Dictionary<string, Dictionary<string, object>> settings = new Dictionary<string, Dictionary<string, object>>();

            Dictionary<string, object> ResolveSection(IConfigurationSection section)
            {
                Dictionary<string, object> values = new Dictionary<string, object>();
                foreach (IConfigurationSection childSection in section.GetChildren())
                {
                    if (childSection.Value == null)
                    {
                        values.Add(childSection.Key, ResolveSection(childSection));
                    }
                    else
                    {
                        values.Add(childSection.Key, childSection.Value);
                    }
                }

                return values;
            }

            foreach (IConfigurationSection section in _configuration.GetChildren())
            {
                Dictionary<string, object> values = ResolveSection(section);
                if (values.Count != 0)
                    settings.Add(section.Key, values);
            }

            return new JsonResult(new
            {
                Settings = settings
            }, new JsonSerializerSettings
            {
                Formatting = Formatting.Indented
            })
            {
                StatusCode = (int)HttpStatusCode.OK
            };
        }
    }

    public class RemovedBlobRecords
    {
        public BlobIdentifier[] Blobs { get; }

        public RemovedBlobRecords(IEnumerable<BlobIdentifier> blobs)
        {
            Blobs = blobs.ToArray();
        }
    }

    public class RemovedRefRecordsResponse
    {
        public RemovedRefRecordsResponse(int countOfRemovedRecords)
        {
            CountOfRemovedRecords = countOfRemovedRecords;
        }

        public int CountOfRemovedRecords { get; }
    }

    public class UpdatedRecordsResponse
    {
        public UpdatedRecordsResponse(List<UpdatedRecord> updatedRecords)
        {
            UpdatedRecords = updatedRecords;
        }

        public class UpdatedRecord
        {
            public UpdatedRecord(RefRecord record, DateTime time)
            {
                Record = record;
                Time = time;
            }

            public RefRecord Record { get; }
            public DateTime Time { get; }
        }

        public List<UpdatedRecord> UpdatedRecords { get; }
    }
}
