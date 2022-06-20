// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGExecuteBlueprint.h"
#include "PCGHelpers.h"
#include "Data/PCGPointData.h"
#include "Data/PCGSpatialData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGSettingsHelpers.h"

#if WITH_EDITOR
#include "Engine/World.h"

namespace PCGBlueprintHelper
{
	TSet<TObjectPtr<UObject>> GetDataDependencies(UPCGBlueprintElement* InElement)
	{
		check(InElement && InElement->GetClass());
		UClass* BPClass = InElement->GetClass();

		TSet<TObjectPtr<UObject>> Dependencies;
		PCGHelpers::GatherDependencies(InElement, Dependencies);
		return Dependencies;
	}
}
#endif // WITH_EDITOR

UWorld* UPCGBlueprintElement::GetWorld() const
{
#if WITH_EDITOR
	return GWorld;
#else
	return nullptr;
#endif
}

void UPCGBlueprintElement::PostLoad()
{
	Super::PostLoad();
	Initialize();
}

void UPCGBlueprintElement::BeginDestroy()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
#endif

	Super::BeginDestroy();
}

void UPCGBlueprintElement::ExecuteWithContext_Implementation(FPCGContext& InContext, const FPCGDataCollection& Input, FPCGDataCollection& Output)
{
	Execute(Input, Output);
}

void UPCGBlueprintElement::Initialize()
{
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UPCGBlueprintElement::OnDependencyChanged);
	DataDependencies = PCGBlueprintHelper::GetDataDependencies(this);
#endif
}

#if WITH_EDITOR
void UPCGBlueprintElement::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Since we don't really know what changed, let's just rebuild our data dependencies
	DataDependencies = PCGBlueprintHelper::GetDataDependencies(this);

	OnBlueprintChangedDelegate.Broadcast(this);
}

void UPCGBlueprintElement::OnDependencyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
	{
		return;
	}

	if (!DataDependencies.Contains(Object))
	{
		return;
	}

	OnBlueprintChangedDelegate.Broadcast(this);
}

#endif // WITH_EDITOR

FName UPCGBlueprintElement::NodeTitleOverride_Implementation() const
{
	return NAME_None;
}

FLinearColor UPCGBlueprintElement::NodeColorOverride_Implementation() const
{
	return FLinearColor::White;
}

EPCGSettingsType UPCGBlueprintElement::NodeTypeOverride_Implementation() const
{
	return EPCGSettingsType::Blueprint;
}

void UPCGBlueprintSettings::SetupBlueprintEvent()
{
#if WITH_EDITOR
	if (BlueprintElementType)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintElementType->ClassGeneratedBy))
		{
			Blueprint->OnChanged().AddUObject(this, &UPCGBlueprintSettings::OnBlueprintChanged);
		}
	}
#endif
}

void UPCGBlueprintSettings::TeardownBlueprintEvent()
{
#if WITH_EDITOR
	if (BlueprintElementType)
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintElementType->ClassGeneratedBy))
		{
			Blueprint->OnChanged().RemoveAll(this);
		}
	}
#endif
}

void UPCGBlueprintSettings::SetupBlueprintElementEvent()
{
#if WITH_EDITOR
	if (BlueprintElementInstance)
	{
		BlueprintElementInstance->OnBlueprintChangedDelegate.AddUObject(this, &UPCGBlueprintSettings::OnBlueprintElementChanged);
	}
#endif
}

void UPCGBlueprintSettings::TeardownBlueprintElementEvent()
{
#if WITH_EDITOR
	if (BlueprintElementInstance)
	{
		BlueprintElementInstance->OnBlueprintChangedDelegate.RemoveAll(this);
	}
#endif
}

void UPCGBlueprintSettings::PostLoad()
{
	Super::PostLoad();

	if (BlueprintElement_DEPRECATED && !BlueprintElementType)
	{
		BlueprintElementType = BlueprintElement_DEPRECATED;
		BlueprintElement_DEPRECATED = nullptr;
	}

	SetupBlueprintEvent();

	if (!BlueprintElementInstance)
	{
		RefreshBlueprintElement();
	}
	else
	{
		SetupBlueprintElementEvent();
	}
}

void UPCGBlueprintSettings::BeginDestroy()
{
	TeardownBlueprintElementEvent();
	TeardownBlueprintEvent();

	Super::BeginDestroy();
}

#if WITH_EDITOR
void UPCGBlueprintSettings::PreEditChange(FProperty* PropertyAboutToChange)
{
	if (PropertyAboutToChange)
	{
		if (PropertyAboutToChange->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGBlueprintSettings, BlueprintElementType))
		{
			TeardownBlueprintEvent();
		}
	}
}

void UPCGBlueprintSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPCGBlueprintSettings, BlueprintElementType))
		{
			SetupBlueprintEvent();
		}
	}

	if (!BlueprintElementInstance || BlueprintElementInstance->GetClass() != BlueprintElementType)
	{
		RefreshBlueprintElement();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UPCGBlueprintSettings::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	// When the blueprint changes, the element gets recreated, so we must rewire it here.
	DirtyCache();
	TeardownBlueprintElementEvent();
	SetupBlueprintElementEvent();

	OnSettingsChangedDelegate.Broadcast(this);
}

void UPCGBlueprintSettings::OnBlueprintElementChanged(UPCGBlueprintElement* InElement)
{
	if (InElement == BlueprintElementInstance)
	{
		// When a data dependency is changed, this means we have to dirty the cache, otherwise it will not register as a change.
		DirtyCache();

		OnSettingsChangedDelegate.Broadcast(this);
	}
}
#endif // WITH_EDITOR

void UPCGBlueprintSettings::SetElementType(TSubclassOf<UPCGBlueprintElement> InElementType, UPCGBlueprintElement*& ElementInstance)
{
	if (!BlueprintElementInstance || InElementType != BlueprintElementType)
	{
		if (InElementType != BlueprintElementType)
		{
			TeardownBlueprintEvent();
			BlueprintElementType = InElementType;
			SetupBlueprintEvent();
		}
		
		RefreshBlueprintElement();
	}

	ElementInstance = BlueprintElementInstance;
}

void UPCGBlueprintSettings::RefreshBlueprintElement()
{
	TeardownBlueprintElementEvent();

	if (BlueprintElementType)
	{
		BlueprintElementInstance = NewObject<UPCGBlueprintElement>(this, BlueprintElementType);
		BlueprintElementInstance->Initialize();
		SetupBlueprintElementEvent();
	}
	else
	{
		BlueprintElementInstance = nullptr;
	}	
}

#if WITH_EDITOR
FLinearColor UPCGBlueprintSettings::GetNodeTitleColor() const
{
	if (BlueprintElementInstance && BlueprintElementInstance->NodeColorOverride() != FLinearColor::White)
	{
		return BlueprintElementInstance->NodeColorOverride();
	}
	else
	{
		return Super::GetNodeTitleColor();
	}
}

EPCGSettingsType UPCGBlueprintSettings::GetType() const
{
	if (BlueprintElementInstance)
	{
		return BlueprintElementInstance->NodeTypeOverride();
	}
	else
	{
		return EPCGSettingsType::Blueprint;
	}
}

void UPCGBlueprintSettings::GetTrackedActorTags(FPCGTagToSettingsMap& OutTagToSettings) const
{
#if WITH_EDITORONLY_DATA
	for (const FName& Tag : TrackedActorTags)
	{
		OutTagToSettings.FindOrAdd(Tag).Add(this);
	}
#endif // WITH_EDITORONLY_DATA
}
#endif // WITH_EDITOR

FName UPCGBlueprintSettings::AdditionalTaskName() const
{
	if (BlueprintElementInstance && BlueprintElementInstance->NodeTitleOverride() != NAME_None)
	{
		return BlueprintElementInstance->NodeTitleOverride();
	}
	else
	{
#if WITH_EDITOR
		return (BlueprintElementType && BlueprintElementType->ClassGeneratedBy) ? BlueprintElementType->ClassGeneratedBy->GetFName() : Super::AdditionalTaskName();
#else
		return BlueprintElementType ? BlueprintElementType->GetFName() : Super::AdditionalTaskName();
#endif
	}
}

TArray<FName> UPCGBlueprintSettings::InLabels() const
{
	return BlueprintElementInstance ? BlueprintElementInstance->InputPinLabels.Array() : TArray<FName>();
}

TArray<FName> UPCGBlueprintSettings::OutLabels() const
{
	return BlueprintElementInstance ? BlueprintElementInstance->OutputPinLabels.Array() : TArray<FName>();
}

bool UPCGBlueprintSettings::HasDefaultInLabel() const
{
	return !BlueprintElementInstance || BlueprintElementInstance->bHasDefaultInPin;
}

bool UPCGBlueprintSettings::HasDefaultOutLabel() const
{
	return !BlueprintElementInstance || BlueprintElementInstance->bHasDefaultOutPin;
}

FPCGElementPtr UPCGBlueprintSettings::CreateElement() const
{
	return MakeShared<FPCGExecuteBlueprintElement>();
}

bool FPCGExecuteBlueprintElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGExecuteBlueprintElement::Execute);
	FPCGBlueprintExecutionContext* Context = static_cast<FPCGBlueprintExecutionContext*>(InContext);

	if (Context && Context->BlueprintElementInstance)
	{
		UClass* BPClass = Context->BlueprintElementInstance->GetClass();

#if WITH_EDITOR
		/** Check if the blueprint has been successfully compiled */
		if (UBlueprint* Blueprint = Cast<UBlueprint>(BPClass ? BPClass->ClassGeneratedBy : nullptr))
		{
			if (Blueprint->Status == BS_Error)
			{
				UE_LOG(LogPCG, Error, TEXT("PCG blueprint element cannot be executed since %s is not properly compiled"), *Blueprint->GetFName().ToString());
				return true;
			}
		}
#endif

		/** Apply params overrides to variables if any */
		if (UPCGParamData* Params = Context->InputData.GetParams())
		{
			for (TFieldIterator<FProperty> PropertyIt(BPClass); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				if (Property->IsNative())
				{
					continue;
				}

				// Apply params if any
				PCGSettingsHelpers::SetValue(Params, Context->BlueprintElementInstance, Property);
			}
		}

		// Log info on inputs
		for (int32 InputIndex = 0; InputIndex < Context->InputData.TaggedData.Num(); ++InputIndex)
		{
			const FPCGTaggedData& Input = Context->InputData.TaggedData[InputIndex];
			if (const UPCGPointData* PointData = Cast<UPCGPointData>(Input.Data))
			{
				PCGE_LOG(Verbose, "Input %d has %d points", InputIndex, PointData->GetPoints().Num());
			}
		}

		/** Finally, execute the actual blueprint */
		Context->BlueprintElementInstance->ExecuteWithContext(*Context, Context->InputData, Context->OutputData);

		// Log info on outputs
		for (int32 OutputIndex = 0; OutputIndex < Context->OutputData.TaggedData.Num(); ++OutputIndex)
		{
			const FPCGTaggedData& Output = Context->OutputData.TaggedData[OutputIndex];
			if (const UPCGPointData* PointData = Cast<UPCGPointData>(Output.Data))
			{
				PCGE_LOG(Verbose, "Output %d has %d points", OutputIndex, PointData->GetPoints().Num());
			}
		}
	}
	else if(Context)
	{
		// Nothing to do but forward data
		Context->OutputData = Context->InputData;
	}
	
	return true;
}

void UPCGBlueprintElement::LoopOnPoints(FPCGContext& InContext, const UPCGPointData* InData, UPCGPointData*& OutData, UPCGPointData* OptionalOutData) const
{
	if (!InData)
	{
		PCGE_LOG_C(Error, &InContext, "Invalid input data in LoopOnPoints");
		return;
	}

	OutData = OptionalOutData ? OptionalOutData : NewObject<UPCGPointData>(const_cast<UPCGPointData*>(InData));
	OutData->InitializeFromData(InData);

	const TArray<FPCGPoint>& InPoints = InData->GetPoints();
	TArray<FPCGPoint>& OutPoints = OutData->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(&InContext, InPoints.Num(), OutPoints, [this, &InContext, InData, OutData, &InPoints](int32 Index, FPCGPoint& OutPoint)
	{
		return PointLoopBody(InContext, InData, InPoints[Index], OutPoint, OutData->Metadata);
	});
}

void UPCGBlueprintElement::LoopOnPointPairs(FPCGContext& InContext, const UPCGPointData* InA, const UPCGPointData* InB, UPCGPointData*& OutData, UPCGPointData* OptionalOutData) const
{
	if (!InA || !InB)
	{
		PCGE_LOG_C(Error, &InContext, "Invalid input data in LoopOnPointPairs");
		return;
	}

	OutData = OptionalOutData ? OptionalOutData : NewObject<UPCGPointData>(const_cast<UPCGPointData*>(InA));
	OutData->InitializeFromData(InA); //METADATA TODO should we remove the parenting here?

	const TArray<FPCGPoint>& InPointsA = InA->GetPoints();
	const TArray<FPCGPoint>& InPointsB = InB->GetPoints();
	TArray<FPCGPoint>& OutPoints = OutData->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(&InContext, InPointsA.Num() * InPointsB.Num(), OutPoints, [this, &InContext, InA, InB, OutData, &InPointsA, &InPointsB](int32 Index, FPCGPoint& OutPoint)
	{
		return PointPairLoopBody(InContext, InA, InB, InPointsA[Index / InPointsB.Num()], InPointsB[Index % InPointsB.Num()], OutPoint, OutData->Metadata);
	});
}

void UPCGBlueprintElement::LoopNTimes(FPCGContext& InContext, int64 NumIterations, UPCGPointData*& OutData, const UPCGSpatialData* InA, const UPCGSpatialData* InB, UPCGPointData* OptionalOutData) const
{
	if (NumIterations < 0)
	{
		UE_LOG(LogPCG, Error, TEXT("Invalid number of iterations in PCG blueprint element"));
		return;
	}

	const UPCGSpatialData* Owner = (InA ? InA : InB);
	OutData = OptionalOutData;

	if (!OutData)
	{
		OutData = Owner ? NewObject<UPCGPointData>(const_cast<UPCGSpatialData*>(Owner)) : NewObject<UPCGPointData>();
	}

	if (Owner)
	{
		OutData->InitializeFromData(Owner);
	}

	TArray<FPCGPoint>& OutPoints = OutData->GetMutablePoints();

	FPCGAsync::AsyncPointProcessing(&InContext, NumIterations, OutPoints, [this, &InContext, InA, InB, OutData](int32 Index, FPCGPoint& OutPoint)
	{
		return IterationLoopBody(InContext, Index, InA, InB, OutPoint, OutData->Metadata);
	});
}

FPCGContext* FPCGExecuteBlueprintElement::Initialize(const FPCGDataCollection& InputData, UPCGComponent* SourceComponent, const UPCGNode* Node)
{
	FPCGBlueprintExecutionContext* Context = new FPCGBlueprintExecutionContext();
	Context->InputData = InputData;
	Context->SourceComponent = SourceComponent;
	Context->Node = Node;

	const UPCGBlueprintSettings* Settings = Context->GetInputSettings<UPCGBlueprintSettings>();
	if (Settings && Settings->BlueprintElementInstance)
	{
		Context->BlueprintElementInstance = CastChecked<UPCGBlueprintElement>(StaticDuplicateObject(Settings->BlueprintElementInstance, Settings->BlueprintElementInstance, FName()));
	}
	else
	{
		Context->BlueprintElementInstance = nullptr;
	}

	return Context;
}

bool FPCGExecuteBlueprintElement::IsCacheable(const UPCGSettings* InSettings) const
{
	if (const UPCGBlueprintSettings* BPSettings = Cast<const UPCGBlueprintSettings>(InSettings))
	{
		return !BPSettings->bCreatesArtifacts;
	}
	else
	{
		return false;
	}
}