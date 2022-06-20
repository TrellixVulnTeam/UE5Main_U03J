// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Templates/SharedPointer.h"
#include "Modules/ModuleInterface.h"

class FImgMediaGlobalCache;
class FImgMediaPlayer;
class IMediaEventSink;
class IMediaPlayer;

CSV_DECLARE_CATEGORY_MODULE_EXTERN(IMGMEDIA_API, ImgMedia);

/** Callback when a player gets created. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnImgMediaPlayerCreated, const TSharedPtr<FImgMediaPlayer>&);

/**
 * Interface for the ImgMedia module.
 */
class IMGMEDIA_API IImgMediaModule
	: public IModuleInterface
{
public:

	/**
	 * Creates a media player for image sequences.
	 *
	 * @param EventHandler The object that will receive the player's events.
	 * @return A new media player, or nullptr if a player couldn't be created.
	 */
	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventHandler) = 0;

public:

	/** Virtual destructor. */
	virtual ~IImgMediaModule() { }

	/** Add to this callback to get called whenever a player is created. */
	FOnImgMediaPlayerCreated OnImgMediaPlayerCreated;

	/**
	 * Call this to get the global cache.
	 * 
	 * @return Global cache.
	 */
	static FImgMediaGlobalCache* GetGlobalCache() { return GlobalCache.Get(); }

	/** Name of attribute in the Exr file that marks it as our custom format. */
	static FLazyName CustomFormatAttributeName;
	/** Name of attribute in the Exr file for the tile width for our custom format. */
	static FLazyName CustomFormatTileWidthAttributeName;
	/** Name of attribute in the Exr file for the tile height for our custom format. */
	static FLazyName CustomFormatTileHeightAttributeName;
	/** Name of attribute in the Exr file for the tile border size for our custom format. */
	static FLazyName CustomFormatTileBorderAttributeName;

protected:

	/** Holds the global cache. */
	static TSharedPtr<FImgMediaGlobalCache, ESPMode::ThreadSafe> GlobalCache;
};
