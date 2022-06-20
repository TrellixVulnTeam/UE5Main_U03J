// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaCapture.h"

#include "IRivermaxOutputStream.h"
#include "Misc/FrameRate.h"
#include "RivermaxMediaOutput.h"

#include "RivermaxMediaCapture.generated.h"


namespace UE::RivermaxCore
{
	struct FRivermaxStreamOptions;
}

class URivermaxMediaOutput;

/**
 * Output Media for Rivermax streams.
 */
UCLASS(BlueprintType)
class RIVERMAXMEDIA_API URivermaxMediaCapture : public UMediaCapture, public UE::RivermaxCore::IRivermaxOutputStreamListener
{
	GENERATED_BODY()

public:

	//~ Begin UMediaCapture interface
	virtual bool HasFinishedProcessing() const override;
protected:
	virtual bool ValidateMediaOutput() const override;
	virtual bool CaptureSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool CaptureRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget) override;
	virtual bool UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport) override;
	virtual bool UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget) override;
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override;
	virtual bool ShouldCaptureRHITexture() const override;
	
	virtual void BeforeFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture) override;
	virtual void OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow) override;
	virtual void OnRHITextureCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture) override;
	//~ End UMediaCapture interface

	//~ Begin IRivermaxOutputStreamListener interface
	virtual void OnInitializationCompleted(bool bHasSucceed) override;
	virtual void OnStreamError() override;
	//~ End IRivermaxOutputStreamListener interface

private:
	bool Initialize(URivermaxMediaOutput* InMediaOutput);
	bool ConfigureStream(URivermaxMediaOutput* InMediaOutput, UE::RivermaxCore::FRivermaxStreamOptions& OutOptions) const;

private:

	TUniquePtr<UE::RivermaxCore::IRivermaxOutputStream> RivermaxStream;
};
