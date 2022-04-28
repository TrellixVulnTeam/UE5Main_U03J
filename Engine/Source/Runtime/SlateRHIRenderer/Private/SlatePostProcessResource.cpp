// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlatePostProcessResource.h"
#include "RenderUtils.h"

DECLARE_MEMORY_STAT(TEXT("PostProcess RenderTargets"), STAT_SLATEPPRenderTargetMem, STATGROUP_SlateMemory);

FSlatePostProcessResource::FSlatePostProcessResource(int32 InRenderTargetCount)
	: RenderTargetSize(FIntPoint::ZeroValue)
	, RenderTargetCount(InRenderTargetCount)
{
}

FSlatePostProcessResource::~FSlatePostProcessResource()
{

}

void FSlatePostProcessResource::Update(const FIntPoint& NewSize)
{
	if(NewSize.X > RenderTargetSize.X || NewSize.Y > RenderTargetSize.Y || RenderTargetSize == FIntPoint::ZeroValue || RenderTargets.Num() == 0 )
	{
		if(!IsInitialized())
		{
			InitResource();
		}

		FIntPoint NewMaxSize(FMath::Max(NewSize.X, RenderTargetSize.X), FMath::Max(NewSize.Y, RenderTargetSize.Y));
		ResizeTargets(NewMaxSize);
	}
}

void FSlatePostProcessResource::ResizeTargets(const FIntPoint& NewSize)
{
	check(IsInRenderingThread());

	RenderTargets.Empty();

	RenderTargetSize = NewSize;
	PixelFormat = PF_B8G8R8A8;
	if (RenderTargetSize.X > 0 && RenderTargetSize.Y > 0)
	{
		for (int32 TexIndex = 0; TexIndex < RenderTargetCount; ++TexIndex)
		{
			const FRHITextureCreateDesc Desc =
				FRHITextureCreateDesc::Create2D(TEXT("FSlatePostProcessResource"))
				.SetExtent(RenderTargetSize)
				.SetFormat(PixelFormat);

			FTexture2DRHIRef RenderTargetTextureRHI;
			RHICreateTargetableShaderResource(Desc, ETextureCreateFlags::RenderTargetable, RenderTargetTextureRHI);

			RenderTargets.Add(RenderTargetTextureRHI);
		}
	}

	STAT(int64 TotalMemory = RenderTargetCount * GPixelFormats[PixelFormat].BlockBytes*RenderTargetSize.X*RenderTargetSize.Y);
	SET_MEMORY_STAT(STAT_SLATEPPRenderTargetMem, TotalMemory);
}

void FSlatePostProcessResource::CleanUp()
{
	BeginReleaseResource(this);

	BeginCleanup(this);
}

void FSlatePostProcessResource::InitDynamicRHI()
{
}

void FSlatePostProcessResource::ReleaseDynamicRHI()
{
	SET_MEMORY_STAT(STAT_SLATEPPRenderTargetMem, 0);

	RenderTargetSize = FIntPoint::ZeroValue;

	RenderTargets.Empty();
}

