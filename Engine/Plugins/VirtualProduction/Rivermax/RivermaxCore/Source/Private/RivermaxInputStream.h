// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRivermaxInputStream.h"

#include "HAL/Runnable.h"
#include "RivermaxHeader.h"
#include "RivermaxTypes.h"


namespace UE::RivermaxCore::Private
{
	using UE::RivermaxCore::IRivermaxInputStream;
	using UE::RivermaxCore::IRivermaxInputStreamListener;
	using UE::RivermaxCore::FRivermaxStreamOptions;
	
	struct FInputStreamBufferConfiguration
	{
		size_t PayloadSize = 0;
		size_t HeaderSize = 0;
		uint16 PayloadExpectedSize = 1500;
		uint16 HeaderExpectedSize = 20; //for 2110

		rmax_in_memblock DataMemory;
		rmax_in_memblock HeaderMemory;
	};

	struct FInputStreamData
	{
		uint64 LastSequenceNumber = 0;
		uint8* CurrentFrame = nullptr;
		uint32 WritingOffset = 0;
		uint32 ReceivedSize = 0;
		uint32 ExpectedSize = 0;
	};

	class FRivermaxInputStream : public IRivermaxInputStream, public FRunnable
	{
	public:
		FRivermaxInputStream();
		virtual ~FRivermaxInputStream();

	public:

		//~ Begin IRivermaxInputStreamListener interface
		virtual bool Initialize(const FRivermaxStreamOptions& InOptions, IRivermaxInputStreamListener& InListener) override;
		virtual void Uninitialize() override;
		//~ End IRivermaxInputStreamListener interface 

		void Process_AnyThread();

		//~ Begin FRunnable interface
		virtual bool Init() override;
		virtual uint32 Run() override;
		virtual void Stop() override;
		virtual void Exit() override;
		//~ End FRunnable interface

	private:

		struct FRTPParameter
		{
			uint32 SequencerNumber = 0;
			uint32 Timestamp = 0;
			bool bIsMBit = false;
			bool bIsFBit = false;
		};

		bool GetRTPParameter(uint8* InputRTP, FRTPParameter& OutParameter);
		void ParseChunk(const rmax_in_completion& Completion);
		bool IsExtendedSequenceNumber() const;
		void PrepareNextFrame();

	private:



		FRivermaxStreamOptions Options;

		FRunnableThread* RivermaxThread = nullptr;
		std::atomic<bool> bIsActive;

		rmax_stream_id StreamId = 0;
		rmax_in_flow_attr FlowAttribute;
		FInputStreamBufferConfiguration BufferConfiguration;
		bool bIsFirstFrameReceived = false;
		ERivermaxStreamType RivermaxStreamType = ERivermaxStreamType::VIDEO_2110_20_STREAM;
		FInputStreamData StreamData;
		IRivermaxInputStreamListener* Listener;
	};
}


