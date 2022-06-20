// Copyright Epic Games, Inc. All Rights Reserved.

#include "ExrImgMediaReader.h"
#include "ExrImgMediaReaderGpu.h"

#if IMGMEDIA_EXR_SUPPORTED_PLATFORM

#include "Async/Async.h"
#include "Misc/Paths.h"
#include "ExrReaderGpu.h"
#include "OpenExrWrapper.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"
#include "HardwareInfo.h"
#include "IImgMediaModule.h"
#include "ImgMediaLoader.h"
#include "ImgMediaMipMapInfo.h"

DECLARE_MEMORY_STAT(TEXT("EXR Reader Pool Memory."), STAT_ExrMediaReaderPoolMem, STATGROUP_ImgMediaPlugin);

static TAutoConsoleVariable<bool> CVarEnableUncompressedExrGpuReader(
	TEXT("r.ExrReadAndProcessOnGPU"),
	true,
	TEXT("Allows reading of Large Uncompressed EXR files directly into Structured Buffer.\n")
	TEXT("and be processed on GPU\n"));


/* FExrImgMediaReader structors
 *****************************************************************************/

FExrImgMediaReader::FExrImgMediaReader(const TSharedRef<FImgMediaLoader, ESPMode::ThreadSafe>& InLoader)
	: LoaderPtr(InLoader)
	, bIsCustomFormat(false)
	, bIsCustomFormatTiled(false)
	, CustomFormatTileSize(EForceInit::ForceInitToZero)
{
	const UImgMediaSettings* Settings = GetDefault<UImgMediaSettings>();
	
	FOpenExr::SetGlobalThreadCount(Settings->ExrDecoderThreads == 0
		? FPlatformMisc::NumberOfCoresIncludingHyperthreads()
		: Settings->ExrDecoderThreads);
}

FExrImgMediaReader::~FExrImgMediaReader()
{
	FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
	CanceledFrames.Reset();
}

/* FExrImgMediaReader interface
 *****************************************************************************/

bool FExrImgMediaReader::GetFrameInfo(const FString& ImagePath, FImgMediaFrameInfo& OutInfo)
{
	return GetInfo(ImagePath, OutInfo);
}

FExrImgMediaReader::EReadResult FExrImgMediaReader::ReadTilesCustom
	( uint16* Buffer
	, const FString& ImagePath
	, int32 FrameId
	, const FIntRect& TileRegion
	, TSharedPtr<FSampleConverterParameters> ConverterParams
	, const int32 CurrentMipLevel)
{
#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS
	EReadResult bResult = Success;

	int64 CurrentBufferPos = 0;
	FExrReader ChunkReader;
	int MipLevelDiv = 1 << CurrentMipLevel;
	FIntPoint DimensionInTiles = ConverterParams->FrameInfo.NumTiles / MipLevelDiv;
	int32 NumTiles = ConverterParams->bCustomExr ? 1 : ConverterParams->FrameInfo.NumTiles.X * ConverterParams->FrameInfo.NumTiles.Y;
	int32 NumMipLevels = ConverterParams->bMipsInSeparateFiles ? 1 : ConverterParams->NumMipLevels;
	if (!ChunkReader.OpenExrAndPrepareForPixelReading(ImagePath, NumTiles, NumMipLevels, ConverterParams->bCustomExr))
	{
		return Fail;
	}

	for (int32 TileRow = TileRegion.Min.Y; TileRow < TileRegion.Max.Y; TileRow++)
	{
		// Check to see if the frame was canceled.
		{
			FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
			if (CanceledFrames.Remove(FrameId) > 0)
			{
				UE_LOG(LogImgMedia, Warning, TEXT("Reader %p: Canceling Frame %i At tile row # %i"), this, FrameId, TileRow);
				bResult = Cancelled;
				break;
			}
		}
		const uint16 Padding = ConverterParams->bCustomExr ? 0 : FExrReader::TILE_PADDING;
		const int64 TileByteStride = ConverterParams->PixelSize * ConverterParams->TileDimWithBorders.X * ConverterParams->TileDimWithBorders.Y + Padding;
		const int StartTileIndex = TileRow * DimensionInTiles.X + TileRegion.Min.X;
		const int EndTileIndex = TileRow * DimensionInTiles.X + TileRegion.Max.X;
		const int BytesToRead = (EndTileIndex - StartTileIndex) * TileByteStride;

		if (ConverterParams->bCustomExr)
		{
			const int64 TileStride = ConverterParams->TileDimWithBorders.X * ConverterParams->TileDimWithBorders.Y * ConverterParams->PixelSize;
			ChunkReader.SeekTileWithinFileCustom(StartTileIndex, TileStride, ConverterParams->bMipsInSeparateFiles ? 0 : CurrentMipLevel, CurrentBufferPos);
		}
		else
		{
			ChunkReader.SeekTileWithinFile(StartTileIndex, DimensionInTiles, ConverterParams->bMipsInSeparateFiles ? 0 : CurrentMipLevel, CurrentBufferPos);
		}

		if (!ChunkReader.ReadExrImageChunk(reinterpret_cast<char*>(Buffer) + CurrentBufferPos, BytesToRead))
		{
			bResult = Fail;
			break;
		}
	}

	if (!ChunkReader.CloseExrFile())
	{
		return Fail;
	}
	return bResult;
#else
	return Fail;
#endif
}

bool FExrImgMediaReader::ReadFrame(int32 FrameId, const TMap<int32, FImgMediaTileSelection>& InMipTiles, TSharedPtr<FImgMediaFrame, ESPMode::ThreadSafe> OutFrame)
{
	TSharedPtr<FImgMediaLoader, ESPMode::ThreadSafe> Loader = LoaderPtr.Pin();
	if (Loader.IsValid() == false)
	{
		return false;
	}
	
	// Get tile info.
	int32 NumTilesX = Loader->GetNumTilesX();
	int32 NumTilesY = Loader->GetNumTilesY();
	bool bHasTiles = (NumTilesX * NumTilesY) > 1;

	int32 BytesPerPixelPerChannel = sizeof(uint16);
	int32 NumChannels = 4;
	int32 BytesPerPixel = BytesPerPixelPerChannel * NumChannels;

	// Do we already have our buffer?
	if (OutFrame->Data.IsValid() == false)
	{
		// Nope. Create it.
		const FString& LargestImage = Loader->GetImagePath(FrameId, 0);
		if (!GetInfo(LargestImage, OutFrame->Info))
		{
			return false;
		}

		// If we have tiles, then this is the size of just a tile, so multiply to get the full size. Same goes for size.
		OutFrame->Info.Dim.X *= NumTilesX;
		OutFrame->Info.Dim.Y *= NumTilesY;
		OutFrame->Info.UncompressedSize *= NumTilesX * NumTilesY;

		const FIntPoint& Dim = OutFrame->Info.Dim;

		if (Dim.GetMin() <= 0)
		{
			return false;
		}

		// allocate frame buffer
		SIZE_T BufferSize = GetMipBufferTotalSize(Dim);
		void* Buffer = FMemory::Malloc(BufferSize, PLATFORM_CACHE_LINE_SIZE);

		auto BufferDeleter = [BufferSize](void* ObjectToDelete) {
#if USE_IMGMEDIA_DEALLOC_POOL
			if (FQueuedThreadPool* ImgMediaThreadPoolSlow = GetImgMediaThreadPoolSlow())
			{
				// free buffers on the thread pool, because memory allocators may perform
				// expensive operations, such as filling the memory with debug values
				TFunction<void()> FreeBufferTask = [ObjectToDelete]()
				{
					FMemory::Free(ObjectToDelete);
				};
				AsyncPool(*ImgMediaThreadPoolSlow, FreeBufferTask);
			}
			else
			{
				FMemory::Free(ObjectToDelete);
			}
#else
			FMemory::Free(ObjectToDelete);
#endif
		};
		
		// The EXR RGBA interface only outputs RGBA data.
		OutFrame->Format = EMediaTextureSampleFormat::FloatRGBA;
		OutFrame->Data = MakeShareable(Buffer, MoveTemp(BufferDeleter));
		OutFrame->MipTilesPresent.Reset();
		OutFrame->Stride = OutFrame->Info.Dim.X * BytesPerPixel;
	}

	// Loop over all mips.
	uint8* MipDataPtr = (uint8*)(OutFrame->Data.Get());
	FIntPoint Dim = OutFrame->Info.Dim;
	
	int32 NumMipLevels = Loader->GetNumMipLevels();
	for (int32 CurrentMipLevel = 0; CurrentMipLevel < NumMipLevels; ++CurrentMipLevel)
	{
		if (InMipTiles.Contains(CurrentMipLevel))
		{
			const FImgMediaTileSelection& CurrentTileSelection = InMipTiles[CurrentMipLevel];

			const int MipLevelDiv = 1 << CurrentMipLevel;
			int32 StartTileX = CurrentTileSelection.TopLeftX;
			int32 StartTileY = CurrentTileSelection.TopLeftY;
			int32 EndTileX = FMath::Min((int32)CurrentTileSelection.BottomRightX, FMath::CeilToInt(float(NumTilesX) / MipLevelDiv));
			int32 EndTileY = FMath::Min((int32)CurrentTileSelection.BottomRightY, FMath::CeilToInt(float(NumTilesY) / MipLevelDiv));

			bool ReadThisMip = true;
			// Avoid reads if the cached frame already contains the current tiles for this mip level.
			if (const FImgMediaTileSelection* CachedSelection = OutFrame->MipTilesPresent.Find(CurrentMipLevel))
			{
				ReadThisMip = !CachedSelection->Contains(CurrentTileSelection);
			}

			if (ReadThisMip)
			{
				FString Image = Loader->GetImagePath(FrameId, CurrentMipLevel);
				FString BaseImage;
				if (bHasTiles)
				{
					// Remove "_x0_y0.exr" so we can add on the correct name for the tile we want.
					BaseImage = Image.LeftChop(10);
				}

				int32 TileWidth = Dim.X / NumTilesX;
				int32 TileHeight = Dim.Y / NumTilesY;
				// The offset into the frame buffer for each row of tiles.
				// Total width * height of a tile * bytes per pixel.
				int32 FrameBufferOffsetPerTileY = Dim.X * TileHeight * BytesPerPixel;
				int32 FrameBufferOffsetY = FrameBufferOffsetPerTileY * StartTileY;

				if (OutFrame->Info.FormatName == TEXT("EXR CUSTOM"))
				{
#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS
					FIntRect TileRegion = FIntRect(
						(int32)CurrentTileSelection.TopLeftX,
						(int32)CurrentTileSelection.TopLeftY,
						FMath::Min((int32)CurrentTileSelection.BottomRightX, FMath::CeilToInt(float(OutFrame->Info.NumTiles.X) / MipLevelDiv)),
						FMath::Min((int32)CurrentTileSelection.BottomRightY, FMath::CeilToInt(float(OutFrame->Info.NumTiles.Y) / MipLevelDiv)));
					FIntPoint NumTiles = OutFrame->Info.NumTiles / MipLevelDiv;
					int32 PixelSize = sizeof(uint16) * OutFrame->Info.NumChannels;
					TSharedPtr<FSampleConverterParameters> ConverterParams = MakeShared<FSampleConverterParameters>();
					ConverterParams->FrameInfo = OutFrame->Info;
					ConverterParams->PixelSize = sizeof(uint16) * ConverterParams->FrameInfo.NumChannels;
					ConverterParams->TileDimWithBorders = OutFrame->Info.TileDimensions + OutFrame->Info.TileBorder * 2;
					ConverterParams->NumMipLevels = Loader->GetNumMipLevels();
					ConverterParams->bCustomExr = OutFrame->Info.FormatName == TEXT("EXR CUSTOM");

					EReadResult ReadResult = ReadTilesCustom((uint16*)MipDataPtr, Image, FrameId, TileRegion, ConverterParams, CurrentMipLevel);
					if (ReadResult != Fail)
					{
						OutFrame->MipTilesPresent.Emplace(CurrentMipLevel, CurrentTileSelection);
					}
					else
					{
						UE_LOG(LogImgMedia, Error, TEXT("Could not load %s"), *Image);
					}
#else
					UE_LOG(LogImgMedia, Error, TEXT("Current platform doesn't support custom EXR file %s"), *Image);
#endif
				}
				else
				{
					for (int32 TileY = StartTileY; TileY < EndTileY; TileY++)
					{
						// The offset into the frame buffer for each column of tiles.
						// Tile width * bytes per pixel.
						int32 FrameBufferOffsetPerTileX = TileWidth * BytesPerPixel;
						int32 FrameBufferOffsetX = FrameBufferOffsetPerTileX * StartTileX;
						for (int32 TileX = StartTileX; TileX < EndTileX; TileX++)
						{
							// Get for our frame/mip level.
							if (bHasTiles)
							{
								Image = FString::Printf(TEXT("%s_x%d_y%d.exr"), *BaseImage, TileX, TileY);
							}
							FRgbaInputFile InputFile(Image, 2);
							if (InputFile.HasInputFile())
							{
								// read frame data
								InputFile.SetFrameBuffer(MipDataPtr + FrameBufferOffsetX + FrameBufferOffsetY, Dim);
								InputFile.ReadPixels(0, TileHeight - 1);

								FrameBufferOffsetX += FrameBufferOffsetPerTileX;

								if (!OutFrame->MipTilesPresent.Contains(CurrentMipLevel))
								{
									OutFrame->MipTilesPresent.Emplace(CurrentMipLevel, CurrentTileSelection);
								}
							}
							else
							{
								UE_LOG(LogImgMedia, Error, TEXT("Could not load %s"), *Image);
							}
						}

						FrameBufferOffsetY += FrameBufferOffsetPerTileY;
					}
				}
			}
		}

		// Next level.
		MipDataPtr += Dim.X * Dim.Y * BytesPerPixel;
		Dim /= 2;
	}

	return true;
}

void FExrImgMediaReader::CancelFrame(int32 FrameNumber)
{
	UE_LOG(LogImgMedia, Verbose, TEXT("Reader %p: Canceling Frame. %i"), this, FrameNumber);
	FScopeLock RegionScopeLock(&CanceledFramesCriticalSection);
	CanceledFrames.Add(FrameNumber);
}

/** Gets reader type (GPU vs CPU) depending on size of EXR and its compression. */
TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> FExrImgMediaReader::GetReader(const TSharedRef <FImgMediaLoader, ESPMode::ThreadSafe>& InLoader, FString FirstImageInSequencePath)
{
	bool bIsCustomFormat = false;
	FIntPoint TileSize(EForceInit::ForceInitToZero);

#if defined(PLATFORM_WINDOWS) && PLATFORM_WINDOWS
	FRgbaInputFile InputFile(FirstImageInSequencePath, 2);
	if (InputFile.HasInputFile() == false)
	{
		TSharedPtr<IImgMediaReader, ESPMode::ThreadSafe> Ptr;
		return Ptr;
	}
	
	FImgMediaFrameInfo Info;
	if (!GetInfo(FirstImageInSequencePath, Info))
	{
		return MakeShareable(new FExrImgMediaReader(InLoader));
	}

	// Is this our custom format?
	int32 CustomFormat = 0;
	InputFile.GetIntAttribute(IImgMediaModule::CustomFormatAttributeName.Resolve().ToString(), CustomFormat);
	bIsCustomFormat = CustomFormat > 0;
	if (bIsCustomFormat)
	{
		// Get tile size.
		InputFile.GetIntAttribute(IImgMediaModule::CustomFormatTileWidthAttributeName.Resolve().ToString(), TileSize.X);
		InputFile.GetIntAttribute(IImgMediaModule::CustomFormatTileHeightAttributeName.Resolve().ToString(), TileSize.Y);
	}

	// Check GetCompressionName of OpenExrWrapper for other compression names.
	if (GDynamicRHI && GDynamicRHI->GetInterfaceType() == ERHIInterfaceType::D3D12
		&& Info.CompressionName == "Uncompressed" 
		&& CVarEnableUncompressedExrGpuReader.GetValueOnAnyThread()
		)
	{
		TSharedRef<FExrImgMediaReaderGpu, ESPMode::ThreadSafe> GpuReader = 
			MakeShared<FExrImgMediaReaderGpu, ESPMode::ThreadSafe>(InLoader);
		GpuReader->SetCustomFormatInfo(bIsCustomFormat, TileSize);
		return GpuReader;
	}
#endif
	FExrImgMediaReader* Reader = new FExrImgMediaReader(InLoader);
	Reader->SetCustomFormatInfo(bIsCustomFormat, TileSize);
	return MakeShareable(Reader);
}

/* FExrImgMediaReader implementation
 *****************************************************************************/

bool FExrImgMediaReader::GetInfo(const FString& FilePath, FImgMediaFrameInfo& OutInfo)
{
	FOpenExrHeaderReader HeaderReader(FilePath);
	if (HeaderReader.HasInputFile() == false)
	{
		return false;
	}

	OutInfo.CompressionName = HeaderReader.GetCompressionName();
	OutInfo.Dim = HeaderReader.GetDataWindow();
	OutInfo.FrameRate = HeaderReader.GetFrameRate(ImgMedia::DefaultFrameRate);
	OutInfo.Srgb = false;
	OutInfo.UncompressedSize = HeaderReader.GetUncompressedSize();
	OutInfo.NumChannels = HeaderReader.GetNumChannels();
	OutInfo.NumMipLevels = 1;

	int32 CustomFormat = 0;
	HeaderReader.GetIntAttribute(IImgMediaModule::CustomFormatAttributeName.Resolve().ToString(), CustomFormat);
	bool bIsCustomFormat = CustomFormat > 0;

	if (bIsCustomFormat)
	{
		OutInfo.FormatName = TEXT("EXR CUSTOM");

		// Get tile size.
		HeaderReader.GetIntAttribute(IImgMediaModule::CustomFormatTileBorderAttributeName.Resolve().ToString(), OutInfo.TileBorder);
		OutInfo.bHasTiles = HeaderReader.GetIntAttribute(IImgMediaModule::CustomFormatTileWidthAttributeName.Resolve().ToString(), OutInfo.TileDimensions.X);
		OutInfo.bHasTiles = OutInfo.bHasTiles && HeaderReader.GetIntAttribute(IImgMediaModule::CustomFormatTileHeightAttributeName.Resolve().ToString(), OutInfo.TileDimensions.Y);
	}
	else
	{
		OutInfo.FormatName = TEXT("EXR");
		OutInfo.bHasTiles = HeaderReader.GetTileSize(OutInfo.TileDimensions);
		OutInfo.TileBorder = 0;
	}

	if (OutInfo.bHasTiles)
	{
		OutInfo.NumTiles = FIntPoint(OutInfo.Dim.X / (OutInfo.TileDimensions.X + OutInfo.TileBorder * 2), OutInfo.Dim.Y / (OutInfo.TileDimensions.Y + OutInfo.TileBorder * 2));
		if (HeaderReader.ContainsMips())
		{
			OutInfo.NumMipLevels = HeaderReader.CalculateNumMipLevels(OutInfo.NumTiles);
		}
	}
	else
	{
		OutInfo.TileDimensions = OutInfo.Dim;
		OutInfo.NumTiles = FIntPoint(1, 1);
	}


	return (OutInfo.UncompressedSize > 0) && (OutInfo.Dim.GetMin() > 0);
}


void FExrImgMediaReader::SetCustomFormatInfo(bool bInIsCustomFormat, const FIntPoint& InTileSize)
{
	bIsCustomFormat = bInIsCustomFormat;
	CustomFormatTileSize = InTileSize;
	bIsCustomFormatTiled = InTileSize.X != 0;
}

SIZE_T FExrImgMediaReader::GetMipBufferTotalSize(FIntPoint Dim)
{
	SIZE_T Size = ((Dim.X * Dim.Y * 4) / 3) * sizeof(uint16) * 4;
	
	return Size;
}

#endif //IMGMEDIA_EXR_SUPPORTED_PLATFORM
