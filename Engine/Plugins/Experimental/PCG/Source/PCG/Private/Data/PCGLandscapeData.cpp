// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGLandscapeData.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGPolyLineData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"

#include "Landscape.h"
#include "LandscapeEdit.h"

FPCGLandscapeDataPoint::FPCGLandscapeDataPoint(int InX, int InY, float InHeight)
	: X(InX), Y(InY), Height(InHeight)
{
}

void UPCGLandscapeData::Initialize(ALandscapeProxy* InLandscape, const FBox& InBounds)
{
	check(InLandscape);
	Landscape = InLandscape;
	TargetActor = InLandscape;
	Bounds = InBounds;

	Transform = Landscape->GetActorTransform();
}

FBox UPCGLandscapeData::GetBounds() const
{
	return Bounds;
}

FBox UPCGLandscapeData::GetStrictBounds() const
{
	// TODO: if the landscape contains holes, then the strict bounds
	// should be empty
	return Bounds;
}

FVector UPCGLandscapeData::TransformPosition(const FVector& InPosition) const
{
	TOptional<float> HeightAtVertex = Landscape->GetHeightAtLocation(InPosition);
	if (HeightAtVertex.IsSet())
	{
		return FVector(InPosition.X, InPosition.Y, HeightAtVertex.GetValue());
	}
	else
	{
		return InPosition; // not on landscape
	}
}

FPCGPoint UPCGLandscapeData::TransformPoint(const FPCGPoint& InPoint) const
{
	// TODO: change orientation, ... based on landscape data
	FPCGPoint Point = InPoint;
	FVector PointLocation = InPoint.Transform.GetLocation();

	TOptional<float> HeightAtVertex = Landscape->GetHeightAtLocation(PointLocation);
	if (HeightAtVertex.IsSet())
	{
		PointLocation.Z = HeightAtVertex.GetValue();
		Point.Transform.SetLocation(PointLocation);
	}
	else
	{
		Point.Density = 0;
	}
	
	return Point;
}

float UPCGLandscapeData::GetDensityAtPosition(const FVector& InPosition) const
{
	TOptional<float> HeightAtVertex = Landscape->GetHeightAtLocation(InPosition);
	return HeightAtVertex.IsSet() ? 1.0f : 0;
}

const UPCGPointData* UPCGLandscapeData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGLandscapeData::CreatePointData);

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGLandscapeData*>(this));
	Data->InitializeFromData(this);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	// TODO: add offset to nearest edge, will have an impact if the grid size doesn't match the landscape size
	const FVector MinPt = Transform.InverseTransformPosition(Bounds.Min);
	const FVector MaxPt = Transform.InverseTransformPosition(Bounds.Max);

	const int32 MinX = FMath::FloorToInt(MinPt.X);
	const int32 MaxX = FMath::FloorToInt(MaxPt.X);
	const int32 MinY = FMath::FloorToInt(MinPt.Y);
	const int32 MaxY = FMath::FloorToInt(MaxPt.Y);

	int32 NumIterations = (MaxX - MinX) * (MaxY - MinY);

	FPCGAsync::AsyncPointProcessing(Context, NumIterations, Points, [this, MinX, MaxX, MinY](int32 Index, FPCGPoint& OutPoint)
	{
		const int X = MinX + (Index % (MaxX - MinX));
		const int Y = MinY + (Index / (MaxX - MinX));
		const bool bPlaneCase = Bounds.Min.Z == Bounds.Max.Z;

		FVector VertexLocation = Transform.TransformPosition(FVector(X, Y, 0));
		TOptional<float> HeightAtVertex = Landscape->GetHeightAtLocation(VertexLocation);
		if (HeightAtVertex.IsSet() &&
			HeightAtVertex.GetValue() >= Bounds.Min.Z &&
			(bPlaneCase ? HeightAtVertex.GetValue() <= Bounds.Max.Z : HeightAtVertex.GetValue() < Bounds.Max.Z))
		{
			VertexLocation.Z = HeightAtVertex.GetValue();

			OutPoint = FPCGPoint(FTransform(VertexLocation),
				1.0f,
				PCGHelpers::ComputeSeed(X, Y, (int)HeightAtVertex.GetValue()));
			OutPoint.Extents = Transform.GetScale3D() / 2.0;

			return true;
		}
		else
		{
			return false;
		}
	});

	UE_LOG(LogPCG, Verbose, TEXT("Landscape %s extracted %d of %d potential points"), *Landscape->GetFName().ToString(), Points.Num(), (MaxX - MinX) * (MaxY - MinY));

	return Data;
}