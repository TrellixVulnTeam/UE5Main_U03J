// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxUtils.h"

#include "RivermaxTypes.h"


namespace UE::RivermaxCore::Private::Utils
{
	void StreamOptionsToSDPDescription(const UE::RivermaxCore::FRivermaxStreamOptions& Options, FAnsiStringBuilderBase& OutSDPDescription)
	{
		// Basic SDP string creation from a set of options. At some point, having a proper SDP loader / creator.
		// Refer to https://datatracker.ietf.org/doc/html/rfc4570

		FString FrameRateDescription;
		if (FMath::IsNearlyZero(FMath::Frac(Options.FrameRate.AsDecimal())) == false)
		{
			FrameRateDescription = FString::Printf(TEXT("%d/%d"), Options.FrameRate.Numerator, Options.FrameRate.Denominator);
		}
		else
		{
			FrameRateDescription = FString::Printf(TEXT("%d"), (uint32)Options.FrameRate.AsDecimal());
		}

		OutSDPDescription.Appendf("v=0\n");
		OutSDPDescription.Appendf("s=SMPTE ST2110 20 streams\n");
		OutSDPDescription.Appendf("m=video %d RTP/AVP 96\n", Options.Port);
		OutSDPDescription.Appendf("c=IN IP4 %S/64\n", *Options.DestinationAddress);
		OutSDPDescription.Appendf("a=source-filter: incl IN IP4 %S %S\n", *Options.DestinationAddress, *Options.SourceAddress);
		OutSDPDescription.Appendf("a=rtpmap:96 raw/90000\n");
		OutSDPDescription.Appendf("a=fmtp: 96 sampling=YCbCr-4:2:2; width=%d; height=%d; exactframerate=%S; depth=%d; TCS=SDR; colorimetry=BT709; PM=2110GPM; SSN=ST2110-20:2017; TP=2110TPN;\n"
			, Options.Resolution.X, Options.Resolution.Y, *FrameRateDescription, Options.PixelFormat == ERivermaxOutputPixelFormat::RMAX_10BIT_YCBCR ? 10 : 8);
		OutSDPDescription.Appendf("a=mediaclk:direct=0\n");
		OutSDPDescription.Appendf("a=mid:VID");
	}
}