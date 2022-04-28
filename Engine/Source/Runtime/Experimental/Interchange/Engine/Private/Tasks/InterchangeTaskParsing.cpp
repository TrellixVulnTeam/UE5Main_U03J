// Copyright Epic Games, Inc. All Rights Reserved.
#include "InterchangeTaskParsing.h"

#include "Async/TaskGraphInterfaces.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InterchangeEngineLogPrivate.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "InterchangeTaskCompletion.h"
#include "InterchangeTaskCreateAsset.h"
#include "InterchangeTaskCreateSceneObjects.h"
#include "InterchangeTaskPipeline.h"
#include "InterchangeTranslatorBase.h"
#include "Misc/Paths.h"
#include "Nodes/InterchangeBaseNode.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "PackageUtils/PackageUtils.h"
#include "Stats/Stats.h"
#include "Templates/SharedPointer.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/WeakObjectPtrTemplates.h"

/**
 * For the Dependency sort to work the predicate must be transitive ( A > B > C implying A > C).
 * That means we must take into account the whole dependency chain, not just the immediate dependencies.
 * 
 * This is a helper struct to quickly create the dependencies chain of a node using a cache.
 */
struct FNodeDependencyCache
{
	const TSet<FString>& GetAccumulatedDependencies(const UInterchangeBaseNodeContainer* NodeContainer, const FString& NodeID)
	{
		if (const TSet<FString>* DependenciesPtr = CachedDependencies.Find(NodeID))
		{
			return *DependenciesPtr;
		}

		TSet<FString> Dependencies;
		AccumulateDependencies(NodeContainer, NodeID, Dependencies);
		return CachedDependencies.Add(NodeID, MoveTemp(Dependencies));
	}

private:

	void AccumulateDependencies(const UInterchangeBaseNodeContainer* NodeContainer, const FString& NodeID, TSet<FString>& OutDependenciesSet)
	{
		const UInterchangeFactoryBaseNode* FactoryNode = NodeContainer->GetFactoryNode(NodeID);
		if (!FactoryNode)
		{
			return;
		}

		TArray<FString> FactoryDependencies;
		FactoryNode->GetFactoryDependencies(FactoryDependencies);
		for (const FString& DependencyID : FactoryDependencies)
		{
			bool bAlreadyInSet = false;
			OutDependenciesSet.Add(DependencyID, &bAlreadyInSet);
			// Avoid infinite recursion.
			if (!bAlreadyInSet)
			{
				OutDependenciesSet.Append(GetAccumulatedDependencies(NodeContainer, DependencyID));
			}
		}
	}

	TMap<FString, TSet<FString>> CachedDependencies;
};


void UE::Interchange::FTaskParsing::DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
{
#if INTERCHANGE_TRACE_ASYNCHRONOUS_TASK_ENABLED
	INTERCHANGE_TRACE_ASYNCHRONOUS_TASK(ParsingGraph)
#endif
	FGCScopeGuard GCScopeGuard;

	TSharedPtr<FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	//Parse each graph and prepare import task data, we will then be able to create all the task with the correct dependencies
	struct FTaskData
	{
		FString UniqueID;
		int32 SourceIndex = INDEX_NONE;
		TArray<FString> Dependencies;
		FGraphEventRef GraphEventRef;
		FGraphEventArray Prerequisites;
		const UClass* FactoryClass;

		TArray<UInterchangeFactoryBaseNode*, TInlineAllocator<1>> Nodes; // For scenes, we can group multiple nodes into a single task as they are usually very light
	};

	TArray<FTaskData> AssetTaskDatas;
	TArray<FTaskData> SceneTaskDatas;

	//Avoid creating asset if the asynchronous import is canceled, just create the completion task
	if (!AsyncHelper->bCancel)
	{
		for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
		{
			TArray<FTaskData> SourceAssetTaskDatas;
			TArray<FTaskData> SourceSceneTaskDatas;

			if (!AsyncHelper->BaseNodeContainers.IsValidIndex(SourceIndex))
			{
				continue;
			}
			UInterchangeBaseNodeContainer* BaseNodeContainer = AsyncHelper->BaseNodeContainers[SourceIndex].Get();
			if (!BaseNodeContainer)
			{
				continue;
			}
			const bool bCanImportSceneNode = AsyncHelper->TaskData.ImportType == EImportType::ImportType_Scene;
			BaseNodeContainer->IterateNodesOfType<UInterchangeFactoryBaseNode>([&](const FString& NodeUID, UInterchangeFactoryBaseNode* FactoryNode)
			{
				if (!FactoryNode->IsEnabled())
				{
					//Do not call factory for a disabled node
					return;
				}

				UClass* ObjectClass = FactoryNode->GetObjectClass();
				if (ObjectClass != nullptr)
				{
					const UClass* RegisteredFactoryClass = InterchangeManager->GetRegisteredFactoryClass(ObjectClass);

					const bool bIsAsset = !(FactoryNode->GetObjectClass()->IsChildOf<AActor>() || FactoryNode->GetObjectClass()->IsChildOf<UActorComponent>());

					if (!RegisteredFactoryClass || (!bIsAsset && !bCanImportSceneNode))
					{
						//nothing we can import from this element
						return;
					}

					FTaskData& NodeTaskData = bIsAsset ? SourceAssetTaskDatas.AddDefaulted_GetRef() : SourceSceneTaskDatas.AddDefaulted_GetRef();
					NodeTaskData.UniqueID = FactoryNode->GetUniqueID();
					NodeTaskData.SourceIndex = SourceIndex;
					NodeTaskData.Nodes.Add(FactoryNode);
					FactoryNode->GetFactoryDependencies(NodeTaskData.Dependencies);
					NodeTaskData.FactoryClass = RegisteredFactoryClass;
				}
			});

			{
				FNodeDependencyCache DependencyCache;

				//Sort per dependencies
				auto SortByDependencies =
					[&BaseNodeContainer, &DependencyCache](const FTaskData& A, const FTaskData& B)
				{
					const TSet<FString>& BDependencies = DependencyCache.GetAccumulatedDependencies(BaseNodeContainer, B.UniqueID);
					//if A is a dependency of B then return true to do A before B
					if (BDependencies.Contains(A.UniqueID))
					{
						return true;
					}
					const TSet<FString>& ADependencies = DependencyCache.GetAccumulatedDependencies(BaseNodeContainer, A.UniqueID);
					if (ADependencies.Contains(B.UniqueID))
					{
						return false;
					}
					return ADependencies.Num() <= BDependencies.Num();
				};

				// Nodes cannot depend on a node from another source, so it's faster to sort the dependencies per-source and then append those to the TaskData arrays.
				SourceAssetTaskDatas.Sort(SortByDependencies);
				SourceSceneTaskDatas.Sort(SortByDependencies);
			}

			AssetTaskDatas.Append(MoveTemp(SourceAssetTaskDatas));
			SceneTaskDatas.Append(MoveTemp(SourceSceneTaskDatas));
		}
	}

	auto CreateTasksForEachTaskData = [](TArray<FTaskData>& TaskDatas, TFunction<FGraphEventRef(FTaskData&)> CreateTasksFunc) -> FGraphEventArray
	{
		FGraphEventArray GraphEvents;

		for (int32 TaskIndex = 0; TaskIndex < TaskDatas.Num(); ++TaskIndex)
		{
			FTaskData& TaskData = TaskDatas[TaskIndex];

			if (TaskData.Dependencies.Num() > 0)
			{
				//Search the previous node to find the dependence
				for (int32 DepTaskIndex = 0; DepTaskIndex < TaskIndex; ++DepTaskIndex)
				{
					if (TaskData.Dependencies.Contains(TaskDatas[DepTaskIndex].UniqueID))
					{
						//Add has prerequisite
						TaskData.Prerequisites.Add(TaskDatas[DepTaskIndex].GraphEventRef);
					}
				}
			}

			TaskData.GraphEventRef = CreateTasksFunc(TaskData);
			GraphEvents.Add(TaskData.GraphEventRef);
		}

		return GraphEvents;
	};

	//Assets
	FGraphEventArray AssetsCompletionPrerequistes;
	{
		TFunction<FGraphEventRef(FTaskData&)> CreateTasksForAsset = [this, &AsyncHelper](FTaskData& TaskData)
		{
			check(TaskData.Nodes.Num() == 1); //We expect 1 node per asset task

			const int32 SourceIndex = TaskData.SourceIndex;
			const UClass* const FactoryClass = TaskData.FactoryClass;
			UInterchangeFactoryBaseNode* const FactoryNode = TaskData.Nodes[0];
			const bool bFactoryCanRunOnAnyThread = FactoryClass->GetDefaultObject<UInterchangeFactoryBase>()->CanExecuteOnAnyThread();

			//Add create package task has a prerequisite of FTaskCreateAsset. Create package task is a game thread task
			FGraphEventArray CreatePackagePrerequistes;
			int32 CreatePackageTaskIndex = AsyncHelper->CreatePackageTasks.Add(
				TGraphTask<FTaskCreatePackage>::CreateTask(&(TaskData.Prerequisites)).ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, FactoryNode, FactoryClass)
			);
			CreatePackagePrerequistes.Add(AsyncHelper->CreatePackageTasks[CreatePackageTaskIndex]);

			FGraphEventArray CreateAssetPrerequistes;
			int32 CreateTaskIndex = AsyncHelper->CreateAssetTasks.Add(
				TGraphTask<FTaskCreateAsset>::CreateTask(&(CreatePackagePrerequistes)).ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, FactoryNode, bFactoryCanRunOnAnyThread)
			);
			CreateAssetPrerequistes.Add(AsyncHelper->CreateAssetTasks[CreateTaskIndex]);

			return AsyncHelper->CreateAssetTasks[CreateTaskIndex];
		};

		AssetsCompletionPrerequistes = CreateTasksForEachTaskData(AssetTaskDatas, CreateTasksForAsset);
	}

	//Scenes
	//Note: Scene tasks are delayed until all asset tasks are completed
	FGraphEventArray ScenesCompletionPrerequistes;
	{
		TFunction<FGraphEventRef(FTaskData&)> CreateTasksForSceneObject = [this, &AsyncHelper, &AssetsCompletionPrerequistes](FTaskData& TaskData)
		{
			const int32 SourceIndex = TaskData.SourceIndex;
			const UClass* const FactoryClass = TaskData.FactoryClass;

			return AsyncHelper->SceneTasks.Add_GetRef(
				TGraphTask<FTaskCreateSceneObjects>::CreateTask(&AssetsCompletionPrerequistes)
				.ConstructAndDispatchWhenReady(PackageBasePath, SourceIndex, WeakAsyncHelper, TaskData.Nodes, FactoryClass));
		};

		ScenesCompletionPrerequistes = CreateTasksForEachTaskData(SceneTaskDatas, CreateTasksForSceneObject);
	}

	FGraphEventArray CompletionPrerequistes;
	CompletionPrerequistes.Append(AssetsCompletionPrerequistes);
	CompletionPrerequistes.Append(ScenesCompletionPrerequistes);

	//Add an async task for pre completion
	
	FGraphEventArray PreCompletionPrerequistes;
	AsyncHelper->PreCompletionTask = TGraphTask<FTaskPreCompletion>::CreateTask(&CompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
	PreCompletionPrerequistes.Add(AsyncHelper->PreCompletionTask);

	//Start the Post pipeline task
	for (int32 SourceIndex = 0; SourceIndex < AsyncHelper->SourceDatas.Num(); ++SourceIndex)
	{
		for (int32 GraphPipelineIndex = 0; GraphPipelineIndex < AsyncHelper->Pipelines.Num(); ++GraphPipelineIndex)
		{
			int32 GraphPipelineTaskIndex = AsyncHelper->PipelinePostImportTasks.Add(
				TGraphTask<FTaskPipelinePostImport>::CreateTask(&(PreCompletionPrerequistes)).ConstructAndDispatchWhenReady(SourceIndex, GraphPipelineIndex, WeakAsyncHelper)
			);
			//Ensure we run the pipeline in the same order we create the task, since the pipeline modifies the node container, its important that its not processed in parallel, Adding the one we start to the prerequisites
			//is the way to go here
			PreCompletionPrerequistes.Add(AsyncHelper->PipelinePostImportTasks[GraphPipelineTaskIndex]);
		}
	}

	FGraphEventArray PreAsyncCompletionPrerequistes;
	AsyncHelper->PreAsyncCompletionTask = TGraphTask<FTaskPreAsyncCompletion>::CreateTask(&PreCompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
	PreAsyncCompletionPrerequistes.Add(AsyncHelper->PreAsyncCompletionTask);

	AsyncHelper->CompletionTask = TGraphTask<FTaskCompletion>::CreateTask(&PreAsyncCompletionPrerequistes).ConstructAndDispatchWhenReady(InterchangeManager, WeakAsyncHelper);
}
