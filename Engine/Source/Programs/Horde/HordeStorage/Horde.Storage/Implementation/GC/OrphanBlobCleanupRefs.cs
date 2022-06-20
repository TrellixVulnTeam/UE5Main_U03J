// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using async_enumerable_dotnet;
using Dasync.Collections;
using Datadog.Trace;
using EpicGames.Horde.Storage;
using Horde.Storage.Controllers;
using Horde.Storage.Implementation.Blob;
using Jupiter;
using Jupiter.Common;
using Jupiter.Implementation;
using Microsoft.Extensions.Options;
using Serilog;

namespace Horde.Storage.Implementation
{
    public class OrphanBlobCleanupRefs : IBlobCleanup
    {
        private readonly IBlobService _blobService;
        private readonly IObjectService _objectService;
        private readonly IBlobIndex _blobIndex;
        private readonly ILeaderElection _leaderElection;
        private readonly IOptionsMonitor<GCSettings> _gcSettings;
        private readonly INamespacePolicyResolver _namespacePolicyResolver;
        private readonly ILogger _logger = Log.ForContext<OrphanBlobCleanupRefs>();

        // ReSharper disable once UnusedMember.Global
        public OrphanBlobCleanupRefs(IBlobService blobService, IObjectService objectService, IBlobIndex blobIndex, ILeaderElection leaderElection, IOptionsMonitor<GCSettings> gcSettings, INamespacePolicyResolver namespacePolicyResolver)
        {
            _blobService = blobService;
            _objectService = objectService;
            _blobIndex = blobIndex;
            _leaderElection = leaderElection;
            _gcSettings = gcSettings;
            _namespacePolicyResolver = namespacePolicyResolver;
        }

        public bool ShouldRun()
        {
            if (!_leaderElection.IsThisInstanceLeader())
            {
                return false;
            }

            return true;
        }

        public async Task<ulong> Cleanup(CancellationToken cancellationToken)
        {
            if (!_leaderElection.IsThisInstanceLeader())
            {
                _logger.Information("Skipped orphan blob (refs) cleanup run as this instance is not the leader");
                return 0;
            }

            List<NamespaceId> namespaces = await ListNamespaces().Where(NamespaceShouldBeCleaned).ToListAsync();
            
            // enumerate all namespaces, and check if the old blob is valid in any of them to allow for a blob store to just store them in a single pile if it wants to
            ulong countOfBlobsRemoved = 0;
            foreach (NamespaceId @namespace in namespaces)
            {
                if (cancellationToken.IsCancellationRequested)
                    break;

                // only consider blobs that have been around for 60 minutes
                // this due to cases were blobs are uploaded first
                DateTime cutoff = DateTime.Now.AddMinutes(-60);
                await foreach ((BlobIdentifier blob, DateTime lastModified) in _blobService.ListObjects(@namespace).WithCancellation(cancellationToken))
                {
                    if (lastModified > cutoff)
                        continue;
                    
                    if (cancellationToken.IsCancellationRequested)
                        break;

                    using IScope removeBlobScope = Tracer.Instance.StartActive("gc.blob");
                    removeBlobScope.Span.ResourceName = $"{@namespace}.{blob}";

                    bool found = false;
                    NamespaceSettings.PerNamespaceSettings policy = _namespacePolicyResolver.GetPoliciesForNs(@namespace);
                    
                    // check all other namespaces that share the same storage pool for presence of the blob
                    foreach (NamespaceId blobNamespace in namespaces.Where(ns => _namespacePolicyResolver.GetPoliciesForNs(ns).StoragePool == policy.StoragePool))
                    {
                        if (cancellationToken.IsCancellationRequested)
                            break;
                        
                        IBlobIndex.BlobInfo? blobIndex = await _blobIndex.GetBlobInfo(blobNamespace, blob);

                        if (blobIndex == null)
                        {
                            continue;
                        }

                        foreach ((BucketId bucket, IoHashKey key) in blobIndex.References)
                        {
                            if (cancellationToken.IsCancellationRequested)
                                break;

                            try
                            {
                                (ObjectRecord, BlobContents?) _ = await _objectService.Get(blobNamespace, bucket, key, new string[] {"name"});
                                found = true;
                            }
                            catch (ObjectNotFoundException)
                            {
                                // this is not a valid reference so we should delete
                            }
                            catch (MissingBlobsException)
                            {
                                // we do not care if there are missing blobs, as long as the record is valid we keep this blob around
                                found = true;
                            }
                        }
                    }

                    if (cancellationToken.IsCancellationRequested)
                        break;

                    removeBlobScope.Span.SetTag("removed", (!found).ToString());

                    if (!found)
                    {
                        await RemoveBlob(@namespace, blob);
                        ++countOfBlobsRemoved;
                    }
                }
            }

            return countOfBlobsRemoved;
        }

        private async Task RemoveBlob(NamespaceId ns, BlobIdentifier blob)
        {
            _logger.Information("Attempting to GC Orphan blob {Blob} from {Namespace}", blob, ns);
            try
            {
                await _blobService.DeleteObject(ns, blob);
            }
            catch (Exception e)
            {
                _logger.Warning("Failed to delete blob {Blob} from {Namespace} due to {Error}", blob, ns, e.Message);
            }
        }

        private bool NamespaceShouldBeCleaned(NamespaceId ns)
        {
            try
            {
                NamespaceSettings.PerNamespaceSettings policy = _namespacePolicyResolver.GetPoliciesForNs(ns);

                return policy.IsLegacyNamespace.HasValue && !policy.IsLegacyNamespace.Value;
            }
            catch (UnknownNamespaceException)
            {
                _logger.Warning("Namespace {Namespace} does not configure any policy, not running cleanup on it.", ns);
                return false;
            }
        }

        private IAsyncEnumerable<NamespaceId> ListNamespaces()
        {
            return _objectService.GetNamespaces();
        }
    }
}
