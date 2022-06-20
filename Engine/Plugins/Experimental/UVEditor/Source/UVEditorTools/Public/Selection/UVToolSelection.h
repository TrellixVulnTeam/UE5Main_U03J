// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ToolTargets/UVEditorToolMeshInput.h"
#include "Selection/StoredMeshSelectionUtil.h" //FEdgesFromTriangleSubIndices
#include "DynamicMesh/DynamicMesh3.h"
#include "Util/IndexUtil.h"

namespace UE {
namespace Geometry {

/**
 * Class that represents a selection in the canonical unwrap of a UV editor input object.
 */
class UVEDITORTOOLS_API FUVToolSelection
{
public:
	enum class EType
	{
		Vertex,
		Edge,
		Triangle
	};

	TWeakObjectPtr<UUVEditorToolMeshInput> Target = nullptr;
	EType Type = EType::Vertex;
	TSet<int32> SelectedIDs;

	void Clear()
	{
		Target = nullptr;
		SelectedIDs.Reset();
		StableEdgeIDs.Reset();
	}

	bool IsEmpty() const
	{
		return SelectedIDs.IsEmpty();
	}

	bool HasStableEdgeIdentifiers() const
	{
		return !StableEdgeIDs.IsEmpty();
	}

	bool operator==(const FUVToolSelection& Other) const
	{
		return Target == Other.Target
			&& Type == Other.Type
			&& SelectedIDs.Num() == Other.SelectedIDs.Num()
			// Don't need to check the reverse because we checked Num above.
			&& SelectedIDs.Includes(Other.SelectedIDs); 
	}

	bool operator!=(const FUVToolSelection& Other) const
	{
		return !(*this == Other);
	}

	void SaveStableEdgeIdentifiers(const FDynamicMesh3& Mesh);

	void RestoreFromStableEdgeIdentifiers(const FDynamicMesh3& Mesh);

	bool AreElementsPresentInMesh(FDynamicMesh3& Mesh) const;

protected:
	
	FMeshEdgesFromTriangleSubIndices StableEdgeIDs;
};

}} //end namespace UE::Geometry