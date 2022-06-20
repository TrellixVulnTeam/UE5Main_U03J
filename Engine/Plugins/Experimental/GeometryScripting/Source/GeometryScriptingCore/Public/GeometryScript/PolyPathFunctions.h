// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GeometryScript/GeometryScriptTypes.h"
#include "Components/SplineComponent.h"
#include "PolyPathFunctions.generated.h"


UENUM(BlueprintType)
enum class EGeometryScriptSampleSpacing : uint8
{
	UniformDistance,
	UniformTime,
	ErrorTolerance
};


USTRUCT(BlueprintType)
struct GEOMETRYSCRIPTINGCORE_API FGeometryScriptSplineSamplingOptions
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (EditCondition = "SampleSpacing != EGeometryScriptSampleSpacing::ErrorTolerance"))
	int32 NumSamples = 10;

	UPROPERTY(BlueprintReadWrite, Category = Options, meta = (EditCondition = "SampleSpacing == EGeometryScriptSampleSpacing::ErrorTolerance"))
	float ErrorTolerance = 1;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	EGeometryScriptSampleSpacing SampleSpacing = EGeometryScriptSampleSpacing::UniformDistance;

	UPROPERTY(BlueprintReadWrite, Category = Options)
	TEnumAsByte<ESplineCoordinateSpace::Type> CoordinateSpace = ESplineCoordinateSpace::Type::Local;
};


UCLASS(meta = (ScriptName = "GeometryScript_PolyPath"))
class GEOMETRYSCRIPTINGCORE_API UGeometryScriptLibrary_PolyPathFunctions : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintPure, Category = "GeometryScript|PolyPath", meta=(ScriptMethod))
	static int GetPolyPathNumVertices(FGeometryScriptPolyPath PolyPath);

	UFUNCTION(BlueprintPure, Category = "GeometryScript|PolyPath", meta=(ScriptMethod))
	static int GetPolyPathLastIndex(FGeometryScriptPolyPath PolyPath);

	UFUNCTION(BlueprintPure, Category = "GeometryScript|PolyPath", meta=(ScriptMethod))
	static FVector GetPolyPathVertex(FGeometryScriptPolyPath PolyPath, int Index, bool& bIsValidIndex);

	UFUNCTION(BlueprintPure, Category = "GeometryScript|PolyPath", meta = (ScriptMethod))
	static FVector GetPolyPathTangent(FGeometryScriptPolyPath PolyPath, int Index, bool& bIsValidIndex);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath", meta = (ScriptMethod))
	static double GetPolyPathArcLength(FGeometryScriptPolyPath PolyPath);

	/** Find the index of the vertex closest to a given point.  Returns -1 if path has no vertices. */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath", meta = (ScriptMethod))
	static int32 GetNearestVertexIndex(FGeometryScriptPolyPath PolyPath, FVector Point);

	/** Flatten to 2D by dropping the given axis, and using the other two coordinates as the new X, Y coordinates.  Returns the modified path for convenience. */
	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath", meta = (ScriptMethod, DisplayName = "Flatten To 2D On Axis"))
	static UPARAM(DisplayName = "Target Poly Path") FGeometryScriptPolyPath FlattenTo2DOnAxis(FGeometryScriptPolyPath TargetPolyPath, EGeometryScriptAxis DropAxis = EGeometryScriptAxis::Z);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath")
	static void ConvertSplineToPolyPath(const USplineComponent* Spline, FGeometryScriptPolyPath& PolyPath, FGeometryScriptSplineSamplingOptions SamplingOptions);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath", meta=(ScriptMethod))
	static void ConvertPolyPathToArray(FGeometryScriptPolyPath PolyPath, TArray<FVector>& VertexArray);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath")
	static void ConvertArrayToPolyPath(const TArray<FVector>& VertexArray, FGeometryScriptPolyPath& PolyPath);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath", meta = (ScriptMethod))
	static void ConvertPolyPathToArrayOfVector2D(FGeometryScriptPolyPath PolyPath, TArray<FVector2D>& VertexArray);

	UFUNCTION(BlueprintCallable, Category = "GeometryScript|PolyPath")
	static void ConvertArrayOfVector2DToPolyPath(const TArray<FVector2D>& VertexArray, FGeometryScriptPolyPath& PolyPath);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "PolyPath To Array Of Vector", CompactNodeTitle = "->", BlueprintAutocast), Category = "GeometryScript|PolyPath")
	static TArray<FVector> Conv_GeometryScriptPolyPathToArray(FGeometryScriptPolyPath PolyPath);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "PolyPath To Array Of Vector2D", CompactNodeTitle = "->", BlueprintAutocast), Category = "GeometryScript|PolyPath")
	static TArray<FVector2D> Conv_GeometryScriptPolyPathToArrayOfVector2D(FGeometryScriptPolyPath PolyPath);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Array Of Vector To PolyPath", CompactNodeTitle = "->", BlueprintAutocast), Category = "GeometryScript|PolyPath")
	static FGeometryScriptPolyPath Conv_ArrayToGeometryScriptPolyPath(const TArray<FVector>& PathVertices);

	UFUNCTION(BlueprintPure, meta = (DisplayName = "Array Of Vector2D To PolyPath", CompactNodeTitle = "->", BlueprintAutocast), Category = "GeometryScript|PolyPath")
	static FGeometryScriptPolyPath Conv_ArrayOfVector2DToGeometryScriptPolyPath(const TArray<FVector2D>& PathVertices);
};
