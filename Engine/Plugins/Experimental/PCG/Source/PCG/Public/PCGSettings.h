// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGData.h"
#include "PCGElement.h"
#include "PCGDebug.h"

#include "PCGSettings.generated.h"

class UPCGNode;
class UPCGSettings;

typedef TMap<FName, TSet<TWeakObjectPtr<const UPCGSettings>>> FPCGTagToSettingsMap;

UENUM()
enum class EPCGSettingsExecutionMode : uint8
{
	Enabled,
	Debug,
	Isolated,
	Disabled
};

UENUM()
enum class EPCGSettingsType : uint8
{
	InputOutput,
	Spatial,
	Density,
	Blueprint,
	Metadata,
	Filter,
	Sampler,
	Artifact,
	Subgraph,
	Generic
};

#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnPCGSettingsChanged, UPCGSettings*);
#endif

/**
* Base class for settings-as-data in the PCG framework
*/
UCLASS(Abstract, BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGSettings : public UPCGData
{
	GENERATED_BODY()

public:
	// TODO: check if we need this to be virtual, we don't really need if we're always caching
	/*virtual*/ FPCGElementPtr GetElement() const;
	virtual UPCGNode* CreateNode() const;

	virtual bool HasInLabel(const FName& Label) const;
	virtual bool HasOutLabel(const FName& Label) const;
	virtual TArray<FName> InLabels() const { return TArray<FName>(); }
	virtual TArray<FName> OutLabels() const { return TArray<FName>(); }
	virtual bool HasDefaultInLabel() const { return true; }
	virtual bool HasDefaultOutLabel() const { return true; }
	
	bool operator==(const UPCGSettings& Other) const;

#if WITH_EDITOR
	virtual FName GetDefaultNodeName() const { return NAME_None; }
	virtual FLinearColor GetNodeTitleColor() const { return FLinearColor::White; }
	virtual EPCGSettingsType GetType() const { return EPCGSettingsType::Generic; }
	/** Derived classes must implement this to communicate dependencies on external actors */
	virtual void GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings) const {}
#endif

	/** Derived classes can implement this to expose additional name information in the logs */
	virtual FName AdditionalTaskName() const { return NAME_None; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Settings)
	int Seed = 0xC35A9631; // random prime number

	/** TODO: Remove this - Placeholder feature until we have a nodegraph */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Tags")
	TSet<FString> FilterOnTags;

	/** TODO: Remove this - Placeholder feature until we have a nodegraph */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Tags")
	bool bPassThroughFilteredOutInputs = true;

	/** TODO: Remove this - Placeholder feature until we have a nodegraph */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings|Tags")
	TSet<FString> TagsAppliedOnOutput;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debug)
	EPCGSettingsExecutionMode ExecutionMode = EPCGSettingsExecutionMode::Enabled;

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Debug, meta = (ShowOnlyInnerProperties))
	FPCGDebugVisualizationSettings DebugSettings;
#endif

#if WITH_EDITOR
	FOnPCGSettingsChanged OnSettingsChangedDelegate;
#endif

protected:
	virtual FPCGElementPtr CreateElement() const PURE_VIRTUAL(UPCGSettings::CreateElement, return nullptr;);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	/** Method that can be called to dirty the cache data from this settings objects if the operator== does not allow to detect changes */
	void DirtyCache();
#endif 

private:
	mutable FPCGElementPtr CachedElement;
	mutable FCriticalSection CacheLock;
};

/** Trivial / Pass-through settings used for input/output nodes */
UCLASS(BlueprintType, ClassGroup = (Procedural))
class PCG_API UPCGTrivialSettings : public UPCGSettings
{
	GENERATED_BODY()

protected:
	//~UPCGSettings implementation
	virtual FPCGElementPtr CreateElement() const override;
};

class PCG_API FPCGTrivialElement : public FSimplePCGElement
{
protected:
	virtual bool ExecuteInternal(FPCGContext* Context) const override;
	virtual bool IsCacheable(const UPCGSettings* InSettings) const override { return false; }
	virtual bool IsPassthrough() const override { return true; }
};
