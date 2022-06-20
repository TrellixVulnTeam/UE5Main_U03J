// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MediaOutput.h"

#include "MediaIOCoreDefinitions.h"
#include "UObject/ObjectMacros.h"

#include "RivermaxMediaOutput.generated.h"


UENUM()
enum class ERivermaxMediaOutputPixelFormat : uint8
{
	PF_8BIT_YUV UMETA(DisplayName = "8bit YUV"),
	//Todo add 10bit YUV, 8/10 bit RGB
};


/**
 * Output information for a Rivermax media capture.
 */
UCLASS(BlueprintType, meta=(MediaIOCustomLayout="Rivermax"))
class RIVERMAXMEDIA_API URivermaxMediaOutput : public UMediaOutput
{
	GENERATED_BODY()

public:
	
	//~ Begin UMediaOutput interface
	virtual bool Validate(FString& FailureReason) const override;
	virtual FIntPoint GetRequestedSize() const override;
	virtual EPixelFormat GetRequestedPixelFormat() const override;
	virtual EMediaCaptureConversionOperation GetConversionOperation(EMediaCaptureSourceType InSourceType) const override;

protected:
	virtual UMediaCapture* CreateMediaCaptureImpl() override;
	//~ End UMediaOutput interface


public:
	//~ UObject interface
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif //WITH_EDITOR
	//~ End UObject interface

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Format")
	FIntPoint Resolution = {1920, 1080};
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	FFrameRate FrameRate = {24,1};
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	ERivermaxMediaOutputPixelFormat PixelFormat = ERivermaxMediaOutputPixelFormat::PF_8BIT_YUV;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	FString SourceAddress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	FString DestinationAddress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Format")
	int32 Port = 50000;
};
