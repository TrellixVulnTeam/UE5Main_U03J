// Copyright Epic Games, Inc. All Rights Reserved.

#include "MassObserverManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingTypes.h"
#include "MassObserverRegistry.h"

namespace UE::Mass::ObserverManager::Private
{
// a helper function to reduce code duplication in FMassObserverManager::Initialize
template<typename TBitSet>
void SetUpObservers(UMassEntitySubsystem& EntitySubsystem, const TMap<const UScriptStruct*, FMassProcessorClassCollection>& RegisteredObserverTypes, TBitSet& ObservedBitSet, FMassObserversMap& Observers)
{
	ObservedBitSet.Reset();

	for (auto It : RegisteredObserverTypes)
	{
		if (It.Value.ClassCollection.Num() == 0)
		{
			continue;
		}

		ObservedBitSet.Add(*It.Key);
		FMassRuntimePipeline& Pipeline = (*Observers).FindOrAdd(It.Key);

		for (const TSubclassOf<UMassProcessor>& ProcessorClass : It.Value.ClassCollection)
		{
			Pipeline.AppendProcessor(ProcessorClass, EntitySubsystem);
		}
		Pipeline.Initialize(EntitySubsystem);
	}
};

} // UE::Mass::ObserverManager::Private

//----------------------------------------------------------------------//
// FMassObserverManager
//----------------------------------------------------------------------//
FMassObserverManager::FMassObserverManager()
	: EntitySubsystem(*GetMutableDefault<UMassEntitySubsystem>())
{

}

FMassObserverManager::FMassObserverManager(UMassEntitySubsystem& Owner)
	: EntitySubsystem(Owner)
{

}

void FMassObserverManager::Initialize()
{
	// instantiate initializers
	const UMassObserverRegistry& Registry = UMassObserverRegistry::Get();

	using UE::Mass::ObserverManager::Private::SetUpObservers;
	for (int i = 0; i < (int)EMassObservedOperation::MAX; ++i)
	{
		SetUpObservers(EntitySubsystem, *Registry.FragmentObservers[i], ObservedFragments[i], FragmentObservers[i]);
		SetUpObservers(EntitySubsystem, *Registry.TagObservers[i], ObservedTags[i], TagObservers[i]);
	}
}

bool FMassObserverManager::OnPostEntitiesCreated(const FMassArchetypeEntityCollection& EntityCollection)
{
	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	// requesting not to flush commands since handling creation of new entities can result in multiple collections of
	// processors being executed and flushing commands between these runs would ruin EntityCollection since entities could
	// get their composition changed and get moved to new archetypes
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.CommandBuffer = MakeShareable(new FMassCommandBuffer());

	if (OnPostEntitiesCreated(ProcessingContext, EntityCollection))
	{
		EntitySubsystem.FlushCommands(ProcessingContext.CommandBuffer);
		return true;
	}
	return false;
}

bool FMassObserverManager::OnPostEntitiesCreated(FMassProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPostEntitiesCreated")

	check(ProcessingContext.EntitySubsystem);
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = ProcessingContext.EntitySubsystem->GetArchetypeComposition(EntityCollection.GetArchetype());
	const FMassFragmentBitSet Overlap = ObservedFragments[(uint8)EMassObservedOperation::Add].GetOverlap(ArchetypeComposition.Fragments);

	if (Overlap.IsEmpty() == false)
	{
		TArray<const UScriptStruct*> OverlapTypes;
		Overlap.ExportTypes(OverlapTypes);

		HandleFragmentsImpl(ProcessingContext, EntityCollection, MakeArrayView(OverlapTypes), FragmentObservers[(uint8)EMassObservedOperation::Add]);
		return true;
	}

	return false;
}

bool FMassObserverManager::OnPreEntitiesDestroyed(const FMassArchetypeEntityCollection& EntityCollection)
{
	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.CommandBuffer = MakeShareable(new FMassCommandBuffer());

	if (OnPreEntitiesDestroyed(ProcessingContext, EntityCollection))
	{
		EntitySubsystem.FlushCommands(ProcessingContext.CommandBuffer);
		return true;
	}
	return false;
}

bool FMassObserverManager::OnPreEntitiesDestroyed(FMassProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("OnPreEntitiesDestroyed")

	check(ProcessingContext.EntitySubsystem);
	const FMassArchetypeCompositionDescriptor& ArchetypeComposition = ProcessingContext.EntitySubsystem->GetArchetypeComposition(EntityCollection.GetArchetype());
	
	return OnCompositionChanged(EntityCollection, ArchetypeComposition, EMassObservedOperation::Remove, &ProcessingContext);
}

bool FMassObserverManager::OnCompositionChanged(const FMassArchetypeEntityCollection& EntityCollection, const FMassArchetypeCompositionDescriptor& CompositionDelta, const EMassObservedOperation Operation, FMassProcessingContext* InProcessingContext)
{
	const FMassFragmentBitSet FragmentOverlap = ObservedFragments[(uint8)Operation].GetOverlap(CompositionDelta.Fragments);
	const bool bHasFragmentsOverlap = !FragmentOverlap.IsEmpty();
	const FMassTagBitSet TagOverlap = ObservedTags[(uint8)Operation].GetOverlap(CompositionDelta.Tags);
	const bool bHasTagsOverlap = !TagOverlap.IsEmpty();

	if (bHasFragmentsOverlap || bHasTagsOverlap)
	{
		FMassProcessingContext LocalContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		LocalContext.bFlushCommandBuffer = false;
		FMassProcessingContext* ProcessingContext = InProcessingContext ? InProcessingContext : &LocalContext;
		TArray<const UScriptStruct*> ObservedTypesOverlap;

		if (bHasFragmentsOverlap)
		{
			FragmentOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(*ProcessingContext, EntityCollection, ObservedTypesOverlap, FragmentObservers[(uint8)Operation]);
		}

		if (bHasTagsOverlap)
		{
			ObservedTypesOverlap.Reset();
			TagOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(*ProcessingContext, EntityCollection, ObservedTypesOverlap, TagObservers[(uint8)Operation]);
		}
	}

	return bHasFragmentsOverlap || bHasTagsOverlap;
}

bool FMassObserverManager::OnCompositionChanged(const FMassEntityHandle Entity, const FMassArchetypeCompositionDescriptor& CompositionDelta, const EMassObservedOperation Operation)
{
	const FMassFragmentBitSet FragmentOverlap = ObservedFragments[(uint8)Operation].GetOverlap(CompositionDelta.Fragments);
	const bool bHasFragmentsOverlap = !FragmentOverlap.IsEmpty();
	const FMassTagBitSet TagOverlap = ObservedTags[(uint8)Operation].GetOverlap(CompositionDelta.Tags);
	const bool bHasTagsOverlap = !TagOverlap.IsEmpty();

	if (bHasFragmentsOverlap || bHasTagsOverlap)
	{
		TArray<const UScriptStruct*> ObservedTypesOverlap;
		FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
		ProcessingContext.bFlushCommandBuffer = false;
		const FMassArchetypeHandle ArchetypeHandle = EntitySubsystem.GetArchetypeForEntity(Entity);

		if (bHasFragmentsOverlap)
		{
			FragmentOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(ProcessingContext, FMassArchetypeEntityCollection(ArchetypeHandle, MakeArrayView(&Entity, 1)
				, FMassArchetypeEntityCollection::NoDuplicates), ObservedTypesOverlap, FragmentObservers[(uint8)Operation]);
		}

		if (bHasTagsOverlap)
		{
			ObservedTypesOverlap.Reset();
			TagOverlap.ExportTypes(ObservedTypesOverlap);

			HandleFragmentsImpl(ProcessingContext, FMassArchetypeEntityCollection(ArchetypeHandle, MakeArrayView(&Entity, 1)
				, FMassArchetypeEntityCollection::NoDuplicates), ObservedTypesOverlap, TagObservers[(uint8)Operation]);
		}
	}

	return bHasFragmentsOverlap || bHasTagsOverlap;
}

void FMassObserverManager::OnFragmentOrTagOperation(const UScriptStruct& FragmentOrTagType, const FMassArchetypeEntityCollection& EntityCollection, const EMassObservedOperation Operation)
{
	check(FragmentOrTagType.IsChildOf(FMassFragment::StaticStruct()) || FragmentOrTagType.IsChildOf(FMassTag::StaticStruct()));

	if (FragmentOrTagType.IsChildOf(FMassFragment::StaticStruct()))
	{
		if (ObservedFragments[(uint8)Operation].Contains(FragmentOrTagType))
		{
			HandleSingleEntityImpl(FragmentOrTagType, EntityCollection, FragmentObservers[(uint8)Operation]);
		}
	}
	else if (ObservedTags[(uint8)Operation].Contains(FragmentOrTagType))
	{
		HandleSingleEntityImpl(FragmentOrTagType, EntityCollection, TagObservers[(uint8)Operation]);
	}
}

void FMassObserverManager::HandleFragmentsImpl(FMassProcessingContext& ProcessingContext, const FMassArchetypeEntityCollection& EntityCollection
	, TArrayView<const UScriptStruct*> ObservedTypes
	/*, const FMassFragmentBitSet& FragmentsBitSet*/, FMassObserversMap& HandlersContainer)
{	
	check(ObservedTypes.Num() > 0);

	for (const UScriptStruct* Type : ObservedTypes)
	{		
		ProcessingContext.AuxData.InitializeAs(Type);
		FMassRuntimePipeline& Pipeline = (*HandlersContainer).FindChecked(Type);

		UE::Mass::Executor::RunProcessorsView(Pipeline.Processors, ProcessingContext, &EntityCollection);
	}
}

void FMassObserverManager::HandleSingleEntityImpl(const UScriptStruct& FragmentType, const FMassArchetypeEntityCollection& EntityCollection, FMassObserversMap& HandlersContainer)
{
	FMassProcessingContext ProcessingContext(EntitySubsystem, /*DeltaSeconds=*/0.f);
	ProcessingContext.bFlushCommandBuffer = false;
	ProcessingContext.AuxData.InitializeAs(&FragmentType);
	FMassRuntimePipeline& Pipeline = (*HandlersContainer).FindChecked(&FragmentType);

	UE::Mass::Executor::RunProcessorsView(Pipeline.Processors, ProcessingContext, &EntityCollection);
}

void FMassObserverManager::AddObserverInstance(const UScriptStruct& FragmentOrTagType, const EMassObservedOperation Operation, UMassProcessor& ObserverProcessor)
{
	checkSlow(FragmentOrTagType.IsChildOf(FMassFragment::StaticStruct()) || FragmentOrTagType.IsChildOf(FMassTag::StaticStruct()));

	FMassRuntimePipeline* Pipeline = nullptr;

	if (FragmentOrTagType.IsChildOf(FMassFragment::StaticStruct()))
	{
		Pipeline = &(*FragmentObservers[(uint8)Operation]).FindOrAdd(&FragmentOrTagType);
		ObservedFragments[(uint8)Operation].Add(FragmentOrTagType);
	}
	else
	{
		Pipeline = &(*TagObservers[(uint8)Operation]).FindOrAdd(&FragmentOrTagType);
		ObservedTags[(uint8)Operation].Add(FragmentOrTagType);
	}
	Pipeline->AppendProcessor(ObserverProcessor);

	// calling initialize to ensure the given processor is related to the same EntitySubsystem
	ObserverProcessor.Initialize(EntitySubsystem);
}
