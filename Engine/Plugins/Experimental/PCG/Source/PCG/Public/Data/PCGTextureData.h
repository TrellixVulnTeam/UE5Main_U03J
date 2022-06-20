// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Data/PCGSurfaceData.h"
#include "Engine/Texture2D.h"

#include "PCGTextureData.generated.h"

UENUM(BlueprintType)
enum class EPCGTextureColorChannel : uint8
{
	Red,
	Green,
	Blue,
	Alpha
};


UENUM(BlueprintType)
enum class EPCGTextureDensityFunction : uint8
{
	Ignore,
	Multiply
};

UCLASS(Abstract)
class PCG_API UPCGBaseTextureData : public UPCGSurfaceData
{
	GENERATED_BODY()

public:
	//~Begin UPCGSpatialData interface
	virtual FBox GetBounds() const override;
	virtual FBox GetStrictBounds() const override;
	virtual float GetDensityAtPosition(const FVector& InPosition) const override;
	virtual FPCGPoint TransformPoint(const FPCGPoint& InPoint) const;
	//~End UPCGSpatialData interface

	//~Begin UPCGSpatialDataWithPointCache interface
	virtual const UPCGPointData* CreatePointData(FPCGContext* Context) const override;
	//~End UPCGSpatialDataWithPointCache interface

public:
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category = SpatialData)
	EPCGTextureDensityFunction DensityFunction = EPCGTextureDensityFunction::Multiply; 

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGTextureColorChannel ColorChannel = EPCGTextureColorChannel::Alpha;

protected:
	UPROPERTY()
	TArray<FLinearColor> ColorData;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	FBox Bounds = FBox(EForceInit::ForceInit);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	int32 Height = 0;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = SpatialData)
	int32 Width = 0;
};

UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCG_API UPCGTextureData : public UPCGBaseTextureData
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = Texture)
	void Initialize(UTexture2D* InTexture, const FTransform& InTransform);

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Properties)
	UTexture2D* Texture = nullptr;
};
