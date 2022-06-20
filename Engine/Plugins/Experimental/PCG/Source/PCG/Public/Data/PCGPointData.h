// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PCGSpatialData.h"

#include "Math/GenericOctreePublic.h"
#include "Math/GenericOctree.h"

#include "PCGPointData.generated.h"

class AActor;

struct PCG_API FPCGPointRef
{
	FPCGPointRef(const FPCGPoint& InPoint);
	FPCGPointRef(const FPCGPointRef& InPointRef);

	const FPCGPoint* Point;
	FBoxSphereBounds Bounds;
};

struct PCG_API FPCGPointRefSemantics
{
	enum { MaxElementsPerLeaf = 16 };
	enum { MinInclusiveElementsPerNode = 7 };
	enum { MaxNodeDepth = 12 };

	typedef TInlineAllocator<MaxElementsPerLeaf> ElementAllocator;

	FORCEINLINE static const FBoxSphereBounds& GetBoundingBox(const FPCGPointRef& InPoint)
	{
		return InPoint.Bounds;
	}

	FORCEINLINE static const bool AreElementsEqual(const FPCGPointRef& A, const FPCGPointRef& B)
	{
		// TODO: verify if that's sufficient
		return A.Point == B.Point;
	}

	FORCEINLINE static void ApplyOffset(FPCGPointRef& InPoint)
	{
		ensureMsgf(false, TEXT("Not implemented"));
	}

	FORCEINLINE static void SetElementId(const FPCGPointRef& Element, FOctreeElementId2 OctreeElementID)
	{
	}
};

// TODO: Split this in "concrete" vs "api" class (needed for views)
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGPointData : public UPCGSpatialData
{
	GENERATED_BODY()

public:
	typedef TOctree2<FPCGPointRef, FPCGPointRefSemantics> PointOctree;

	// ~Begin UPCGSpatialData interface
	virtual int GetDimension() const override { return 0; }
	virtual FBox GetBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual const UPCGPointData* ToPointData(FPCGContext* Context) const { return this; }
	virtual bool GetPointAtPosition(const FVector& InPosition, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const;
	// ~End UPCGSpatialData interface

	void InitializeFromActor(AActor* InActor);

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	const TArray<FPCGPoint>& GetPoints() const { return Points; }

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	FPCGPoint GetPoint(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void SetPoints(const TArray<FPCGPoint>& InPoints);
	
	UFUNCTION(BlueprintCallable, Category = SpatialData)
	void CopyPointsFrom(const UPCGPointData* InData, const TArray<int>& InDataIndices);

	const FPCGPoint* GetPointAtPosition(const FVector& InPosition) const;

	TArray<FPCGPoint>& GetMutablePoints();

	const PointOctree& GetOctree() const;

protected:
	void RebuildOctree() const;
	void RecomputeBounds() const;

	UPROPERTY()
	TArray<FPCGPoint> Points;

	mutable FCriticalSection CachedDataLock;
	mutable PointOctree Octree;
	mutable FBox Bounds; // TODO: review if this needs to be threadsafe
	mutable bool bBoundsAreDirty = true;
	mutable bool bOctreeIsDirty = true;
};
