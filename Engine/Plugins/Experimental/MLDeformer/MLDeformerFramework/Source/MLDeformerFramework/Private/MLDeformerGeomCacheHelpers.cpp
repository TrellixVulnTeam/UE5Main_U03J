// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerGeomCacheHelpers.h"
#include "MLDeformerModel.h"

#include "GeometryCache.h"
#include "GeometryCacheMeshData.h"
#include "GeometryCacheTrack.h"

#include "Animation/AnimSequence.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

#include "NeuralNetwork.h"

#define LOCTEXT_NAMESPACE "MLDeformerGeomCacheHelpers"

namespace UE::MLDeformer
{
#if WITH_EDITORONLY_DATA
	FText GetGeomCacheErrorText(USkeletalMesh* InSkeletalMesh, UGeometryCache* InGeomCache)
	{
		FText Result;
		if (InGeomCache)
		{
			FString ErrorString;

			// Verify that we have imported vertex numbers enabled.
			TArray<FGeometryCacheMeshData> MeshData;
			InGeomCache->GetMeshDataAtTime(0.0f, MeshData);
			if (MeshData.Num() == 0)
			{
				ErrorString = FText(LOCTEXT("TargetMeshNoMeshData", "No geometry data is present.")).ToString();
			}
			else
			{
				if (MeshData[0].ImportedVertexNumbers.Num() == 0)
				{
					ErrorString = FText(LOCTEXT("TargetMeshNoImportedVertexNumbers", "Please import Geometry Cache with option 'Store Imported Vertex Numbers' enabled!")).ToString();
				}
			}

			// Check if we flattened the tracks.
			if (InGeomCache->Tracks.Num() == 1 && InGeomCache->Tracks[0]->GetName() == TEXT("Flattened_Track"))
			{
				int32 NumSkelMeshes = 0;
				if (InSkeletalMesh)
				{
					FSkeletalMeshModel* ImportedModel = InSkeletalMesh->GetImportedModel();
					if (ImportedModel)
					{
						NumSkelMeshes = ImportedModel->LODModels[0].ImportedMeshInfos.Num();		
					}
				}

				if (NumSkelMeshes > 1)
				{
					if (!ErrorString.IsEmpty())
					{
						ErrorString += TEXT("\n\n");
					}
					ErrorString += FText(LOCTEXT("TargetMeshFlattened", "Please import Geometry Cache with option 'Flatten Tracks' disabled!")).ToString();
				}
			}

			Result = FText::FromString(ErrorString);
		}

		return Result;
	}

	FText GetGeomCacheVertexErrorText(USkeletalMesh* InSkelMesh, UGeometryCache* InGeomCache, const FText& SkelName, const FText& GeomCacheName)
	{
		FText Result;

		if (InSkelMesh && InGeomCache)
		{
			const int32 SkelVertCount = UMLDeformerModel::ExtractNumImportedSkinnedVertices(InSkelMesh);
			const int32 GeomCacheVertCount = ExtractNumImportedGeomCacheVertices(InGeomCache);
			const bool bHasGeomCacheError = !GetGeomCacheErrorText(InSkelMesh, InGeomCache).IsEmpty();
			if (SkelVertCount != GeomCacheVertCount && !bHasGeomCacheError)
			{
				Result = FText::Format(
					LOCTEXT("MeshVertexNumVertsMismatch", "Vertex count of {0} doesn't match with {1}!\n\n{2} has {3} verts, while {4} has {5} verts."),
					SkelName,
					GeomCacheName,
					SkelName,
					SkelVertCount,
					GeomCacheName,
					GeomCacheVertCount);
			}
		}

		return Result;
	}

	FText GetGeomCacheMeshMappingErrorText(USkeletalMesh* InSkelMesh, UGeometryCache* InGeomCache)
	{
		FText Result;

		if (InGeomCache && InSkelMesh)
		{
			// Check for failed mesh mappings.
			TArray<FMLDeformerGeomCacheMeshMapping> MeshMappings;
			TArray<FString> FailedNames;
			GenerateGeomCacheMeshMappings(InSkelMesh, InGeomCache, MeshMappings, FailedNames);

			// List all mesh names that have issues.
			FString ErrorString;
			for (int32 Index = 0; Index < FailedNames.Num(); ++Index)
			{
				ErrorString += FailedNames[Index];
				if (Index < FailedNames.Num() - 1)
				{
					ErrorString += TEXT("\n");
				}
			}

			Result = FText::FromString(ErrorString);
		}

		return Result;
	}

	// A fuzzy name match.
	// There is a match when the track name starts with the mesh name.
	bool IsPotentialMatch(const FString& TrackName, const FString& MeshName)
	{
		return (TrackName.Find(MeshName) == 0);
	}

	void GenerateGeomCacheMeshMappings(USkeletalMesh* SkelMesh, UGeometryCache* GeomCache, TArray<FMLDeformerGeomCacheMeshMapping>& OutMeshMappings, TArray<FString>& OutFailedImportedMeshNames)
	{
		OutMeshMappings.Empty();
		OutFailedImportedMeshNames.Empty();
		if (SkelMesh == nullptr || GeomCache == nullptr)
		{
			return;
		}

		// If we haven't got any imported mesh infos then the asset needs to be reimported first.
		// We show an error for this in the editor UI already.
		FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
		check(ImportedModel);
		const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = ImportedModel->LODModels[0].ImportedMeshInfos;
		if (SkelMeshInfos.IsEmpty())
		{
			return;
		}

		// For all meshes in the skeletal mesh.
		const float SampleTime = 0.0f;
		FString SkelMeshName;
		for (int32 SkelMeshIndex = 0; SkelMeshIndex < SkelMeshInfos.Num(); ++SkelMeshIndex)
		{
			const FSkelMeshImportedMeshInfo& MeshInfo = SkelMeshInfos[SkelMeshIndex];
			SkelMeshName = MeshInfo.Name.ToString();

			// Find the matching one in the geom cache.
			bool bFoundMatch = false;
			for (int32 TrackIndex = 0; TrackIndex < GeomCache->Tracks.Num(); ++TrackIndex)
			{
				// Check if this is a candidate based on the mesh and track name.
				UGeometryCacheTrack* Track = GeomCache->Tracks[TrackIndex];
				const bool bIsSoloMesh = (GeomCache->Tracks.Num() == 1 && SkelMeshInfos.Num() == 1);	// Do we just have one mesh and one track?
				if (Track && 
					(IsPotentialMatch(Track->GetName(), SkelMeshName) || bIsSoloMesh))
				{	
					// Extract the geom cache mesh data.
					FGeometryCacheMeshData GeomCacheMeshData;
					if (!Track->GetMeshDataAtTime(SampleTime, GeomCacheMeshData))
					{
						continue;
					}

					// Verify that we have imported vertex numbers.
					if (GeomCacheMeshData.ImportedVertexNumbers.IsEmpty())
					{
						continue;
					}

					// Get the number of geometry cache mesh imported verts.
					int32 NumGeomMeshVerts = 0;
					for (int32 GeomVertIndex = 0; GeomVertIndex < GeomCacheMeshData.ImportedVertexNumbers.Num(); ++GeomVertIndex)
					{
						NumGeomMeshVerts = FMath::Max(NumGeomMeshVerts, (int32)GeomCacheMeshData.ImportedVertexNumbers[GeomVertIndex]);
					}
					NumGeomMeshVerts += 1;	// +1 Because we use indices, so a cube's max index is 7, while there are 8 vertices.

					// Make sure the vertex counts match.
					const int32 NumSkelMeshVerts = MeshInfo.NumVertices;
					if (NumSkelMeshVerts != NumGeomMeshVerts)
					{
						continue;
					}

					// Create a new mesh mapping entry.
					OutMeshMappings.AddDefaulted();
					UE::MLDeformer::FMLDeformerGeomCacheMeshMapping& Mapping = OutMeshMappings.Last();
					Mapping.MeshIndex = SkelMeshIndex;
					Mapping.TrackIndex = TrackIndex;
					Mapping.SkelMeshToTrackVertexMap.AddUninitialized(NumSkelMeshVerts);
					Mapping.ImportedVertexToRenderVertexMap.AddUninitialized(NumSkelMeshVerts);

					// For all vertices (both skel mesh and geom cache mesh have the same number of verts here).
					for (int32 VertexIndex = 0; VertexIndex < NumSkelMeshVerts; ++VertexIndex)
					{
						// Find the first vertex with the same dcc vertex in the geom cache mesh.
						// When there are multiple vertices with the same vertex number here, they are duplicates with different normals or uvs etc.
						// However they all share the same vertex position, so we can just find the first hit, as we only need the position later on.
						const int32 GeomCacheVertexIndex = GeomCacheMeshData.ImportedVertexNumbers.Find(VertexIndex);
						Mapping.SkelMeshToTrackVertexMap[VertexIndex] = GeomCacheVertexIndex;
					
						// Map the source asset vertex number to a render vertex. This is the first duplicate of that vertex.
						const int32 RenderVertexIndex = ImportedModel->LODModels[0].MeshToImportVertexMap.Find(MeshInfo.StartImportedVertex + VertexIndex);
						Mapping.ImportedVertexToRenderVertexMap[VertexIndex] = RenderVertexIndex;
					}

					// We found a match, no need to iterate over more Tracks.
					bFoundMatch = true;
					break;
				} // If the track name matches the skeletal meshes internal mesh name.
			} // For all tracks.

			if (!bFoundMatch)
			{
				OutFailedImportedMeshNames.Add(SkelMeshName);
				UE_LOG(LogMLDeformer, Warning, TEXT("Imported mesh '%s' cannot be matched with a geometry cache track."), *SkelMeshName);
			}
		} // For all meshes inside the skeletal mesh.
	}

	void SampleGeomCachePositions(
		int32 InLODIndex,
		float InSampleTime,
		const TArray<UE::MLDeformer::FMLDeformerGeomCacheMeshMapping>& InMeshMappings,
		const USkeletalMesh* SkelMesh,
		const UGeometryCache* InGeometryCache,
		const FTransform& AlignmentTransform,
		TArray<FVector3f>& OutPositions)
	{
		if (InGeometryCache == nullptr)
		{
			return;
		}

		if (!ensure(SkelMesh != nullptr))
		{
			return;
		}

		const FSkeletalMeshModel* ImportedModel = SkelMesh->GetImportedModel();
		if (!ensure(ImportedModel != nullptr))
		{
			return;
		}
	
		const FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[InLODIndex];
		const TArray<FSkelMeshImportedMeshInfo>& SkelMeshInfos = LODModel.ImportedMeshInfos;

		const uint32 NumVertices = LODModel.MaxImportVertex + 1;
		OutPositions.Reset(NumVertices);
		OutPositions.AddZeroed(NumVertices);

		// For all mesh mappings we found.
		for (int32 MeshMappingIndex = 0; MeshMappingIndex < InMeshMappings.Num(); ++MeshMappingIndex)
		{
			const UE::MLDeformer::FMLDeformerGeomCacheMeshMapping& MeshMapping = InMeshMappings[MeshMappingIndex];
			const FSkelMeshImportedMeshInfo& MeshInfo = SkelMeshInfos[MeshMapping.MeshIndex];
			UGeometryCacheTrack* Track = InGeometryCache->Tracks[MeshMapping.TrackIndex];

			FGeometryCacheMeshData GeomCacheMeshData;
			if (!Track->GetMeshDataAtTime(InSampleTime, GeomCacheMeshData))
			{
				continue;
			}

			for (int32 VertexIndex = 0; VertexIndex < MeshInfo.NumVertices; ++VertexIndex)
			{
				const int32 SkinnedVertexIndex = MeshInfo.StartImportedVertex + VertexIndex;
				const int32 GeomCacheVertexIndex = MeshMapping.SkelMeshToTrackVertexMap[VertexIndex];
				if (GeomCacheVertexIndex != INDEX_NONE && GeomCacheMeshData.Positions.IsValidIndex(GeomCacheVertexIndex))
				{
					const FVector3f GeomCacheVertexPos = (FVector3f)AlignmentTransform.TransformPosition((FVector)GeomCacheMeshData.Positions[GeomCacheVertexIndex]);
					OutPositions[SkinnedVertexIndex] = GeomCacheVertexPos;
				}
			}
		}
	}

	int32 ExtractNumImportedGeomCacheVertices(UGeometryCache* GeometryCache)
	{
		if (GeometryCache == nullptr)
		{
			return 0;
		}

		int32 NumGeomCacheImportedVerts = 0;

		// Extract the geom cache number of imported vertices.
		TArray<FGeometryCacheMeshData> MeshDatas;
		GeometryCache->GetMeshDataAtTime(0.0f, MeshDatas);
		for (const FGeometryCacheMeshData& MeshData : MeshDatas)
		{
			const TArray<uint32>& ImportedVertexNumbers = MeshData.ImportedVertexNumbers;
			if (ImportedVertexNumbers.Num() > 0)
			{
				// Find the maximum value.
				int32 MaxIndex = -1;
				for (int32 Index = 0; Index < ImportedVertexNumbers.Num(); ++Index)
				{
					MaxIndex = FMath::Max(static_cast<int32>(ImportedVertexNumbers[Index]), MaxIndex);
				}
				check(MaxIndex > -1);

				NumGeomCacheImportedVerts += MaxIndex + 1;
			}
		}

		return NumGeomCacheImportedVerts;
	}

	FText GetGeomCacheAnimSequenceErrorText(UGeometryCache* InGeomCache, UAnimSequence* InAnimSequence)
	{
		FText Result;

		if (InAnimSequence && InGeomCache)
		{
			const float AnimSeqDuration = InAnimSequence->GetPlayLength();
			const float GeomCacheDuration = InGeomCache->CalculateDuration();
			if (FMath::Abs(AnimSeqDuration - GeomCacheDuration) > 0.001f)
			{
				FNumberFormattingOptions Options;
				Options.SetUseGrouping(false);
				Options.SetMaximumFractionalDigits(4);
				Result = FText::Format(
					LOCTEXT("AnimSeqNumFramesMismatch", "Anim sequence and Geometry Cache durations don't match!\n\nAnimSeq has a duration of {0} seconds, while GeomCache has a duration of {1} seconds.\n\nThis can produce incorrect results."),
					FText::AsNumber(AnimSeqDuration, &Options),
					FText::AsNumber(GeomCacheDuration, &Options));
			}
		}

		return Result;
	}
#endif	// #if WITH_EDITORONLY_DATA
}	// namespace UE::MLDeformer

#undef LOCTEXT_NAMESPACE
