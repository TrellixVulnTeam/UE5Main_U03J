// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Misc/FrameRate.h"

namespace UE::RivermaxCore
{
	enum class RIVERMAXCORE_API ERivermaxOutputPixelFormat
	{
		RMAX_8BIT_YCBCR,
		RMAX_8BIT_RGB,
		RMAX_10BIT_YCBCR,
		RMAX_10BIT_RGB,
	};

	struct RIVERMAXCORE_API FRivermaxStreamOptions
	{
		/** Desired stream resolution */
		FIntPoint Resolution = { 1920, 1080 };

		/** Stream FrameRate */
		FFrameRate FrameRate = { 24,1 };

		/** Source IP to bind to */
		FString SourceAddress;

		/** Destination IP to send to. Defaults to multicast group IP. */
		FString DestinationAddress = TEXT("224.1.1.1");

		/** Port to be used by stream */
		uint32 Port = 50000;

		/** Desired stream pixel format */
		ERivermaxOutputPixelFormat PixelFormat = ERivermaxOutputPixelFormat::RMAX_8BIT_YCBCR;

		/** Sample count to buffer. */
		int32 NumberOfBuffers = 2;
	};

	enum class RIVERMAXCORE_API ERivermaxStreamType : uint8
	{
		VIDEO_2110_20_STREAM,

		/** Todo add additional stream types */
		//VIDEO_2110_22_STREAM, compressed video
		//AUDIO_2110_30_31_STREAM,
		//ANCILLARY_2110_40_STREAM,
	};
}


