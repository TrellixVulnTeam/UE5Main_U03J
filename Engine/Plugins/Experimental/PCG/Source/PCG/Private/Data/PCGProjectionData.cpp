// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGProjectionData.h"
#include "Helpers/PCGAsync.h"
#include "Metadata/PCGMetadataAccessor.h"

void UPCGProjectionData::Initialize(const UPCGSpatialData* InSource, const UPCGSpatialData* InTarget)
{
	check(InSource && InTarget);
	// TODO: improve support for higher-dimension projection.
	// The problem is that there isn't a valid 1:1 mapping otherwise
	check(InSource->GetDimension() <= InTarget->GetDimension());
	Source = InSource;
	Target = InTarget;
	TargetActor = InSource->TargetActor;

	CachedBounds = ProjectBounds(Source->GetBounds());
	CachedStrictBounds = ProjectBounds(Source->GetStrictBounds());
}

int UPCGProjectionData::GetDimension() const
{
	check(Source && Target);
	return FMath::Min(Source->GetDimension(), Target->GetDimension());
}

FBox UPCGProjectionData::GetBounds() const
{
	check(Source && Target);
	return CachedBounds;
}

FBox UPCGProjectionData::GetStrictBounds() const
{
	check(Source && Target);
	return CachedStrictBounds;
}

FVector UPCGProjectionData::GetNormal() const
{
	check(Source && Target);
	if (Source->GetDimension() > Target->GetDimension())
	{
		return Source->GetNormal();
	}
	else
	{
		return Target->GetNormal();
	}
}

FBox UPCGProjectionData::ProjectBounds(const FBox& InBounds) const
{
	FBox Bounds(EForceInit::ForceInit);

	for (int Corner = 0; Corner < 8; ++Corner)
	{
		Bounds += Target->TransformPosition(
			FVector(
				(Corner / 4) ? InBounds.Max.X : InBounds.Min.X,
				((Corner / 2) % 2) ? InBounds.Max.Y : InBounds.Min.Y,
				(Corner % 2) ? InBounds.Max.Z : InBounds.Min.Z));
	}

	// Fixup the Z direction, as transforming the corners is not sufficient
	const FVector::FReal HalfHeight = 0.5 * (InBounds.Max.Z - InBounds.Min.Z);
	FVector BoundsCenter = InBounds.GetCenter();
	Bounds += BoundsCenter + Target->GetNormal() * HalfHeight;
	Bounds += BoundsCenter - Target->GetNormal() * HalfHeight;

	return Bounds;
}

float UPCGProjectionData::GetDensityAtPosition(const FVector& InPosition) const
{
	// TODO: improve projection/unprojection mechanism
	return Source->GetDensityAtPosition(InPosition);
}

FVector UPCGProjectionData::TransformPosition(const FVector& InPosition) const
{
	// TODO: improve projection/unprojection mechanism
	return Target->TransformPosition(Source->TransformPosition(InPosition));
}

FPCGPoint UPCGProjectionData::TransformPoint(const FPCGPoint& InPoint) const
{
	// TODO: improve projection/unprojection mechanism
	return Target->TransformPoint(Source->TransformPoint(InPoint));
}

bool UPCGProjectionData::GetPointAtPosition(const FVector& InPosition, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	FPCGPoint PointFromSource;
	if (!Source->GetPointAtPosition(InPosition, PointFromSource, OutMetadata))
	{
		return false;
	}

	FPCGPoint PointFromTarget;
	if (!Target->GetPointAtPosition(PointFromSource.Transform.GetLocation(), PointFromTarget, OutMetadata))
	{
		return false;
	}

	// Merge points into a single point
	OutPoint = PointFromSource;
	OutPoint.Transform = PointFromTarget.Transform;
	OutPoint.Density *= PointFromTarget.Density;
	OutPoint.Color *= PointFromTarget.Color;
	
	if (OutMetadata)
	{
		//METADATA TODO Review op
		OutMetadata->MergePointAttributes(PointFromSource, PointFromTarget, OutPoint, EPCGMetadataOp::Max);
	}

	return true;
}

bool UPCGProjectionData::HasNonTrivialTransform() const
{
	return Target->HasNonTrivialTransform();
}

const UPCGPointData* UPCGProjectionData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGProjectionData::CreatePointData);
	// TODO: add mechanism in the ToPointData so we can pass in a transform
	// so we can forego creating the points twice if they're not used.
	const UPCGPointData* SourcePointData = Source->ToPointData(Context);
	const TArray<FPCGPoint>& SourcePoints = SourcePointData->GetPoints();

	UPCGPointData* PointData = NewObject<UPCGPointData>(const_cast<UPCGProjectionData*>(this));
	PointData->InitializeFromData(this, SourcePointData->Metadata);

	TArray<FPCGPoint>& Points = PointData->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(Context, SourcePoints.Num(), Points, [this, SourcePointData, PointData, &SourcePoints](int32 Index, FPCGPoint& OutPoint)
	{
		const FPCGPoint& SourcePoint = SourcePoints[Index];

		FPCGPoint PointFromTarget;
#if WITH_EDITORONLY_DATA
		if (!Target->GetPointAtPosition(SourcePoint.Transform.GetLocation(), PointFromTarget, PointData->Metadata) && !bKeepZeroDensityPoints)
#else
		if (!Target->GetPointAtPosition(SourcePoint.Transform.GetLocation(), PointFromTarget, PointData->Metadata))
#endif
		{
			return false;
		}

		// Merge points into a single point
		OutPoint = SourcePoint;
		UPCGMetadataAccessorHelpers::InitializeMetadata(OutPoint, PointData->Metadata, SourcePoint);
		OutPoint.Transform = PointFromTarget.Transform;
		OutPoint.Density *= PointFromTarget.Density;
		OutPoint.Color *= PointFromTarget.Color;

		if (PointData->Metadata)
		{
			//METADATA TODO review op
			PointData->Metadata->MergePointAttributes(SourcePoint, SourcePointData->Metadata, PointFromTarget, PointData->Metadata, OutPoint, EPCGMetadataOp::Max);
		}

		return true;
	});

	UE_LOG(LogPCG, Verbose, TEXT("Projection generated %d points from %d source points"), Points.Num(), SourcePoints.Num());

	return PointData;
}