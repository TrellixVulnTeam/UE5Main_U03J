// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/PCGTextureData.h"

#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGAsync.h"

namespace PCGTextureDataMaths
{
	float ComputeDensity(float InDensityA, float InDensityB, EPCGTextureDensityFunction InDensityFunction)
	{
		if (InDensityFunction == EPCGTextureDensityFunction::Multiply)
		{
			return InDensityA * InDensityB;
		}
		else // default: Ignore
		{
			return InDensityA;
		}
	}
}

namespace PCGTextureSampling
{
	template<typename ValueType>
	ValueType Sample(const FVector2D& InPosition, const FBox2D& InSurface, int32 Width, int32 Height, TFunctionRef<ValueType(int32 Index)> SamplingFunction)
	{
		// TODO: There seems to be a bias issue here, as the bounds size are not in the same space as the texels.
		// Implementation note: Supports only stretch fit
		FVector2D Pos = (InPosition - InSurface.Min) * FVector2D(Width, Height) / InSurface.GetSize();

		// TODO: this isn't super robust, if that becomes an issue
		int32 X0 = FMath::FloorToInt(Pos.X);
		if (X0 < 0 || X0 >= Width)
		{
			X0 = 0;
		}

		int32 X1 = FMath::CeilToInt(Pos.X);
		if (X1 < 0 || X1 >= Width)
		{
			X1 = 0;
		}

		int32 Y0 = FMath::FloorToInt(Pos.Y);
		if (Y0 < 0 || Y0 >= Height)
		{
			Y0 = 0;
		}

		int32 Y1 = FMath::CeilToInt(Pos.Y);
		if (Y1 < 0 || Y1 >= Height)
		{
			Y1 = 0;
		}

		ValueType SampleX0Y0 = SamplingFunction(X0 + Y0 * Width);
		ValueType SampleX1Y0 = SamplingFunction(X1 + Y0 * Width);
		ValueType SampleX0Y1 = SamplingFunction(X0 + Y1 * Width);
		ValueType SampleX1Y1 = SamplingFunction(X1 + Y1 * Width);

		ValueType BilinearInterpolation = FMath::BiLerp(SampleX0Y0, SampleX1Y0, SampleX0Y1, SampleX1Y1, Pos.X - X0, Pos.Y - Y0);
		return BilinearInterpolation;
	}

	float SampleFloatChannel(const FLinearColor& InColor, EPCGTextureColorChannel ColorChannel)
	{
		switch (ColorChannel)
		{
		case EPCGTextureColorChannel::Red:
			return InColor.R;
		case EPCGTextureColorChannel::Green:
			return InColor.G;
		case EPCGTextureColorChannel::Blue:
			return InColor.B;
		case EPCGTextureColorChannel::Alpha:
		default:
			return InColor.A;
		}
	}
}

FBox UPCGBaseTextureData::GetBounds() const
{
	return Bounds;
}

FBox UPCGBaseTextureData::GetStrictBounds() const
{
	return Bounds;
}

float UPCGBaseTextureData::GetDensityAtPosition(const FVector& InPosition) const
{
	FVector LocalPosition = Transform.InverseTransformPosition(InPosition);
	FVector2D Position2D(LocalPosition.X, LocalPosition.Y);

	FBox2D Surface(FVector2D(-1.0f, -1.0f), FVector2D(1.0f, 1.0f));

	return PCGTextureSampling::Sample<float>(Position2D, Surface, Width, Height, [this](int32 Index) { return PCGTextureSampling::SampleFloatChannel(ColorData[Index], ColorChannel); });
}

FPCGPoint UPCGBaseTextureData::TransformPoint(const FPCGPoint& InPoint) const
{
	FPCGPoint Point = InPoint;

	// Update point location: put it on the surface plane
	FVector PointPositionInLocalSpace = TransformPosition(InPoint.Transform.GetLocation());
	PointPositionInLocalSpace.Z = 0;
	Point.Transform.SetLocation(Transform.TransformPosition(PointPositionInLocalSpace));

	// Set/Update density & color
	FVector2D Position2D(PointPositionInLocalSpace.X, PointPositionInLocalSpace.Y);
	FBox2D Surface(FVector2D(-1.0f, -1.0f), FVector2D(1.0f, 1.0f));

	FLinearColor Color = PCGTextureSampling::Sample<FLinearColor>(Position2D, Surface, Width, Height, [this](int32 Index) { return ColorData[Index]; });

	Point.Color *= Color;
	Point.Density = PCGTextureDataMaths::ComputeDensity(Point.Density, PCGTextureSampling::SampleFloatChannel(Color, ColorChannel), DensityFunction);

	return Point;
}

const UPCGPointData* UPCGBaseTextureData::CreatePointData(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPCGBaseTextureData::CreatePointData);
	// TODO: this is a trivial implementation
	// A better sampler would allow to sample a fixed number of points in either direction
	// or based on a given texel size
	FBox2D LocalSurfaceBounds(FVector2D(-1.0f, -1.0f), FVector2D(1.0f, 1.0f));

	UPCGPointData* Data = NewObject<UPCGPointData>(const_cast<UPCGBaseTextureData*>(this));
	Data->InitializeFromData(this);
	TArray<FPCGPoint>& Points = Data->GetMutablePoints();

	// TODO: There's a bias issue here where we should correct by a 0.5 unit...
	const FVector::FReal XScale = LocalSurfaceBounds.GetSize().X / Width;
	const FVector::FReal YScale = LocalSurfaceBounds.GetSize().Y / Height;
	const FVector2D Bias = LocalSurfaceBounds.Min;

	FPCGAsync::AsyncPointProcessing(Context, Width * Height, Points, [this, XScale, YScale, Bias](int32 Index, FPCGPoint& OutPoint)
	{
		const int X = Index % Width;
		const int Y = Index / Width;

		const float Density = PCGTextureSampling::SampleFloatChannel(ColorData[X + Y * Width], ColorChannel);

#if WITH_EDITORONLY_DATA
		if (Density > 0 || bKeepZeroDensityPoints)
#else
		if (Density > 0)
#endif
		{
			FVector LocalPosition(X * XScale + Bias.X, Y * YScale + Bias.Y, 0);
			OutPoint = FPCGPoint(FTransform(Transform.TransformPosition(LocalPosition)),
				Density,
				PCGHelpers::ComputeSeed(X, Y));

			const FVector TransformScale = Transform.GetScale3D();
			// Note: divided by 4 here because the scale is doubled before, and the extents represent half a pixel
			OutPoint.Extents = FVector(TransformScale.X * XScale / 4.0, TransformScale.Y * YScale / 4.0, 1.0);
			OutPoint.Color = ColorData[X + Y * Width];

			return true;
		}
		else
		{
			return false;
		}
	});

	return Data;
}

void UPCGTextureData::Initialize(UTexture2D* InTexture, const FTransform& InTransform)
{
	Texture = InTexture;
	Transform = InTransform;
	Width = 0;
	Height = 0;

	if (Texture)
	{
#if WITH_EDITORONLY_DATA
		if (Texture->GetPlatformData()->Mips.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPCGTextureData::Initialize::ReadData);
			Width = Texture->GetSizeX();
			Height = Texture->GetSizeY();

			UTexture2D* TempTexture2D = DuplicateObject<UTexture2D>(InTexture, GetTransientPackage());
			TempTexture2D->CompressionSettings = TextureCompressionSettings::TC_VectorDisplacementmap;
			TempTexture2D->MipGenSettings = TextureMipGenSettings::TMGS_NoMipmaps;
			TempTexture2D->SRGB = false;
			TempTexture2D->UpdateResource();
			const FColor* FormattedImageData = static_cast<const FColor*>(TempTexture2D->GetPlatformData()->Mips[0].BulkData.LockReadOnly());

			ColorData.SetNum(Width * Height);
			for (int32 D = 0; D < Width * Height; ++D)
			{
				ColorData[D] = FormattedImageData[D].ReinterpretAsLinear();
			}

			TempTexture2D->GetPlatformData()->Mips[0].BulkData.Unlock();
		}
#endif
	}

	Bounds = FBox(EForceInit::ForceInit);
	Bounds += FVector(-1.0f, -1.0f, 0.0f);
	Bounds += FVector(1.0f, 1.0f, 0.0f);
	Bounds = Bounds.TransformBy(Transform);
}