// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NEW_DIRECTLINK_PLUGIN

#include "DatasmithMaxDirectLink.h"

#include "DatasmithMaxExporter.h"

#include "DatasmithMaxSceneExporter.h"
#include "DatasmithMaxHelper.h"
#include "DatasmithMaxWriter.h"
#include "DatasmithMaxClassIDs.h"

#include "DatasmithMaxLogger.h"
#include "DatasmithMaxSceneParser.h"
#include "DatasmithMaxCameraExporter.h"
#include "DatasmithMaxAttributes.h"
#include "DatasmithMaxProgressManager.h"
#include "DatasmithMaxMeshExporter.h"
#include "DatasmithMaxExporterUtils.h"

#include "Modules/ModuleManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

#include "DatasmithExporterManager.h"
#include "DatasmithExportOptions.h"
#include "DatasmithSceneExporter.h"

#include "DatasmithMesh.h"
#include "DatasmithMeshExporter.h"

#include "IDatasmithExporterUIModule.h"
#include "IDirectLinkUI.h"



#include "DatasmithSceneFactory.h"

#include "DatasmithDirectLink.h"

#include "HAL/PlatformTime.h"
#include "Logging/LogMacros.h"

#include "Async/Async.h"

#include "Windows/AllowWindowsPlatformTypes.h"
MAX_INCLUDES_START
	#include "Max.h"
	#include "bitmap.h"
	#include "gamma.h"

	#include "notify.h"

	#include "ilayer.h"
	#include "ilayermanager.h"

	#include "ISceneEventManager.h"

	#include "IFileResolutionManager.h" // for GetActualPath

	#include "xref/iXrefObj.h"
MAX_INCLUDES_END

namespace DatasmithMaxDirectLink
{

typedef Texmap* FTexmapKey;

class FDatasmith3dsMaxScene
{
public:
	FDatasmith3dsMaxScene()
	{
		ResetScene();
	}

	void ResetScene()
	{
		DatasmithSceneRef.Reset();
		SceneExporterRef.Reset();
	}

	void SetupScene()
	{
		DatasmithSceneRef = FDatasmithSceneFactory::CreateScene(TEXT(""));
		SceneExporterRef = MakeShared<FDatasmithSceneExporter>();

		MSTR Renderer;
		FString Host;
		Host = TEXT("Autodesk 3dsmax ") + FString::FromInt(MAX_VERSION_MAJOR) + TEXT(".") + FString::FromInt(MAX_VERSION_MINOR) + TEXT(".") + FString::FromInt(MAX_VERSION_POINT);
		GetCOREInterface()->GetCurrentRenderer()->GetClassName(Renderer);

		DatasmithSceneRef->SetProductName(TEXT("3dsmax"));
		DatasmithSceneRef->SetHost( *( Host + Renderer ) );

		// Set the vendor name of the application used to build the scene.
		DatasmithSceneRef->SetVendor(TEXT("Autodesk"));

		FString Version = FString::FromInt(MAX_VERSION_MAJOR) + TEXT(".") + FString::FromInt(MAX_VERSION_MINOR) + TEXT(".") + FString::FromInt(MAX_VERSION_POINT);
		DatasmithSceneRef->SetProductVersion(*Version);
	}

	TSharedPtr<IDatasmithScene> GetDatasmithScene()
	{
		return DatasmithSceneRef;
	}

	FDatasmithSceneExporter& GetSceneExporter()
	{
		return *SceneExporterRef;
	}

	void SetName(const TCHAR* InName)
	{
		SceneExporterRef->SetName(InName);
		DatasmithSceneRef->SetName(InName);
		DatasmithSceneRef->SetLabel(InName);
	}

	void SetOutputPath(const TCHAR* InOutputPath)
	{
		// Set the output folder where this scene will be exported.
		SceneExporterRef->SetOutputPath(InOutputPath);
		DatasmithSceneRef->SetResourcePath(SceneExporterRef->GetOutputPath());
	}

	void PreExport()
	{
		// Start measuring the time taken to export the scene.
		SceneExporterRef->PreExport();
	}

	TSharedPtr<IDatasmithScene> DatasmithSceneRef;
	TSharedPtr<FDatasmithSceneExporter> SceneExporterRef;
};

class FNodeTrackerHandle
{
public:
	explicit FNodeTrackerHandle(FNodeKey InNodeKey, INode* InNode) : Impl(MakeShared<FNodeTracker>(InNodeKey, InNode)) {}

	FNodeTracker* GetNodeTracker() const
	{
		return Impl.Get();
	}

private:
	TSharedPtr<FNodeTracker> Impl;
};

// Every node which is resolved to the same object is considered an instance
// This class holds all this nodes which resolve to the same object
struct FInstances: FNoncopyable
{
	AnimHandle Handle; // Handle of anim that is instanced

	Object* EvaluatedObj = nullptr;
	Mtl* Material = nullptr; // Material assigned to Datasmith StaticMesh, used to check if a particular instance needs to verride it

	TSet<class FNodeTracker*> NodeTrackers;

	// Datasmith mesh conversion results
	FMeshConverted Converted;

	bool HasMesh()
	{
		return Converted.DatasmithMeshElement.IsValid();
	}

	const TCHAR* GetStaticMeshPathName()
	{
		return Converted.DatasmithMeshElement->GetName();
	}

	void AssignMaterialToStaticMesh(Mtl* InMaterial)
	{
		Material = InMaterial;
		AssignMeshMaterials(Converted.DatasmithMeshElement, Material, Converted.SupportedChannels);
	}
};

// todo: rename Manager
//   Groups all geometry nodes by their prototype object(geom they resolve to)
class FInstancesManager
{
public:
	void Reset()
	{
		InstancesForAnimHandle.Reset();
	}

	FInstances& AddNodeTracker(FNodeTracker& NodeTracker, FMeshNodeConverter& Converter, Object* Obj)
	{
		TUniquePtr<FInstances>& Instances = InstancesForAnimHandle.FindOrAdd(Converter.InstanceHandle);

		if (!Instances)
		{
			Instances = MakeUnique<FInstances>();
			Instances->Handle = Converter.InstanceHandle;
			Instances->EvaluatedObj = Obj;
		}

		// need to invalidate mesh assignment to node that wasn't the first to add to instances(so if instances weren't invalidated - this node needs mesh anyway)
		Instances->NodeTrackers.Add(&NodeTracker);
		return *Instances;
	}

	FInstances* RemoveNodeTracker(FNodeTracker& NodeTracker)
	{
		if (FInstances* Instances = GetInstancesForNodeTracker(NodeTracker))
		{
			// need to invalidate mesh assignment to node that wasn't the first to add to instances(so if instances weren't invalidated - this node needs mesh anyway)
			Instances->NodeTrackers.Remove(&NodeTracker);
			return Instances;
		}

		return nullptr;
	}

	FInstances* GetInstancesForNodeTracker(FNodeTracker& NodeTracker)
	{
		if (!ensure(NodeTracker.GetConverter().ConverterType == FNodeConverter::MeshNode))
		{
			return nullptr;
		}
		if (TUniquePtr<FInstances>* InstancesPtr = InstancesForAnimHandle.Find(static_cast<FMeshNodeConverter&>(NodeTracker.GetConverter()).InstanceHandle))
		{
			return InstancesPtr->Get();
		}
		return nullptr;
	}

	void RemoveInstances(const FInstances& Instances)
	{
		ensure(Instances.NodeTrackers.IsEmpty()); // Supposed to remove only unused Instances
		InstancesForAnimHandle.Remove(Instances.Handle);
	}

private:
	TMap<AnimHandle, TUniquePtr<FInstances>> InstancesForAnimHandle; // set of instanced nodes for each AnimHandle
};

class FLayerTracker
{
public:
	FLayerTracker(const FString& InName, bool bInIsHidden): Name(InName), bIsHidden(bInIsHidden)
	{
	}

	void SetName(const FString& InName)
	{
		if (Name == InName)
		{
			return;
		}
		bIsInvalidated = true;
		Name = InName;
	}

	void SetIsHidden(bool bInIsHidden)
	{
		if (bIsHidden == bInIsHidden)
		{
			return;
		}
		bIsInvalidated = true;
		bIsHidden = bInIsHidden;
	}

	FString Name;
	bool bIsHidden = false;

	bool bIsInvalidated = true;
};

class FUpdateProgress
{
public:
	class FStage: FNoncopyable
	{
	public:
		FUpdateProgress& UpdateProgress;

		FStage(FUpdateProgress& InUpdateProgress, const TCHAR* InName, int32 InStageCount) : UpdateProgress(InUpdateProgress), Name(InName), StageCount(InStageCount)
		{
			TimeStart =  FDateTime::UtcNow();
		}

		void Finished()
		{
			TimeFinish =  FDateTime::UtcNow();
		}

		FStage& ProgressStage(const TCHAR* SubstageName, int32 InStageCount)
		{
			LogDebug(SubstageName);
			if (UpdateProgress.ProgressManager)
			{
				StageIndex++;
				UpdateProgress.ProgressManager->SetMainMessage(*FString::Printf(TEXT("%s (%d of %d)"), SubstageName, StageIndex, StageCount));
				UpdateProgress.ProgressManager->ProgressEvent(0, TEXT(""));
			}
			return Stages.Emplace_GetRef(UpdateProgress, SubstageName, InStageCount);
		}

		void ProgressEvent(float Progress, const TCHAR* Message)
		{
			LogDebug(FString::Printf(TEXT("%f %s"), Progress, Message));
			if (UpdateProgress.ProgressManager)
			{
				UpdateProgress.ProgressManager->ProgressEvent(Progress, Message);
			}
		}

		void SetResult(const FString& Text)
		{
			Result = Text;
		}

		FString Name;
		int32 StageCount;
		int32 StageIndex = 0;

		FDateTime TimeStart;
		FDateTime TimeFinish;

		FString Result;

		TArray<FStage> Stages;
	};

	FUpdateProgress(bool bShowProgressBar, int32 InStageCount) : MainStage(*this, TEXT("Total"), InStageCount)
	{
		if (bShowProgressBar)
		{
			ProgressManager = MakeUnique<FDatasmithMaxProgressManager>();
		}
	}

	void PrintStatisticss()
	{
		PrintStage(MainStage);
	}

	void PrintStage(FStage& Stage, FString Indent=TEXT(""))
	{
		LogInfo(Indent + FString::Printf(TEXT("    %s - %s"), *Stage.Name, *(Stage.TimeFinish-Stage.TimeStart).ToString()));
		if (!Stage.Result.IsEmpty())
		{
			LogInfo(Indent + TEXT("      #") + Stage.Result);
		}
		for(FStage& ChildStage: Stage.Stages)
		{
			PrintStage(ChildStage, Indent + TEXT("  "));
		}
	}

	void Finished()
	{
		MainStage.Finished();
	}

	TUniquePtr<FDatasmithMaxProgressManager> ProgressManager;

	FStage MainStage;
};

class FProgressCounter
{
public:

	FProgressCounter(FUpdateProgress::FStage& InProgressStage, int32 InCount)
		: ProgressStage(InProgressStage)
		, Count(InCount)
		, SecondsOfLastUpdate(FPlatformTime::Seconds())
	{
	}

	void Next()
	{
		double CurrentTime = FPlatformTime::Seconds();
		if (CurrentTime - SecondsOfLastUpdate > UpdateIntervalMin) // Don't span progress bar
		{
			ProgressStage.ProgressEvent(float(Index) / Count, *FString::Printf(TEXT("%d of %d"), Index, Count) );
			SecondsOfLastUpdate = CurrentTime;
		}
		Index++;
	}
private:
	FUpdateProgress::FStage& ProgressStage;
	int32 Count;
	int32 Index = 0;
	const double UpdateIntervalMin = 0.05; // Don't update progress it last update was just recently
	double SecondsOfLastUpdate;
};

class FProgressStageGuard
{
public:
	FUpdateProgress::FStage& Stage;
	FProgressStageGuard(FUpdateProgress::FStage& ParentStage, const TCHAR* InName, int32 Count=0) : Stage(ParentStage.ProgressStage(InName, Count))
	{
	}

	~FProgressStageGuard()
	{
		Stage.Finished();
		if (ComputeResultDeferred)
		{
			Stage.Result = ComputeResultDeferred();
		}
	}

	TFunction<FString()> ComputeResultDeferred;
};

#define PROGRESS_STAGE(Name) FProgressStageGuard ProgressStage(MainStage, TEXT(Name));
#define PROGRESS_STAGE_COUNTER(Count) FProgressCounter ProgressCounter(ProgressStage.Stage, Count);
#define PROGRESS_STAGE_RESULT(Text) ProgressStage.Stage.SetResult(Text);
// Simplily creation of Stage result (called in Guard dtor)
#define PROGRESS_STAGE_RESULT_DEFERRED ProgressStage.ComputeResultDeferred = [&]()

// Convert various node data to Datasmith tags
class FTagsConverter
{
public:
	void ConvertNodeTags(FNodeTracker& NodeTracker)
	{
		INode* Node = NodeTracker.Node;
		INode* ParentNode = Node->GetParentNode();
		DatasmithMaxExporterUtils::ExportMaxTagsForDatasmithActor( NodeTracker.GetConverted().DatasmithActorElement, Node, ParentNode, KnownMaxDesc, KnownMaxSuperClass );
	}

private:
	// We don't know how the 3ds max lookup_MaxClass is implemented so we use this map to skip it when we can
	TMap<TPair<uint32, TPair<uint32, uint32>>, MAXClass*> KnownMaxDesc;
	// Same for the lookup_MAXSuperClass.
	TMap<uint32, MAXSuperClass*> KnownMaxSuperClass;

};


// In order to retrieve Render geometry rather than Viewport geometry
// RenderBegin need to be called for all RefMakers to be exported (and RenderEnd afterwards)
// e.g. When using Optimize modifier on a geometry it has separate LODs for Render and Viewport and
// GetRenderMesh would return Viewport lod if called without RenderBegin first. Consequently
// without RenderEnd it would display Render LOD in viewport.
class FNodesPreparer
{
public:

	class FBeginRefEnumProc : public RefEnumProc
	{
	public:
		void SetTime(TimeValue StartTime)
		{
			Time = StartTime;
		}

		virtual int proc(ReferenceMaker* RefMaker) override
		{
			RefMaker->RenderBegin(Time);
			return REF_ENUM_CONTINUE;
		}

	private:
		TimeValue Time;
	};

	class FEndRefEnumProc : public RefEnumProc
	{
	public:
		void SetTime(TimeValue EndTime)
		{
			Time = EndTime;
		}

		virtual int32 proc(ReferenceMaker* RefMaker) override
		{
			RefMaker->RenderEnd(Time);
			return REF_ENUM_CONTINUE;
		}

	private:
		TimeValue Time;
	};

	void Start(TimeValue Time, bool bInRenderQuality)
	{
		bRenderQuality = bInRenderQuality;

		BeginProc.SetTime(Time);
		EndProc.SetTime(Time);

		if (bRenderQuality)
		{
			BeginProc.BeginEnumeration();
		}
	}

	void Finish()
	{
		if (bRenderQuality)
		{
			BeginProc.EndEnumeration();

			// Call RenderEnd on every node that had RenderBegin called
			EndProc.BeginEnumeration();
			for(INode* Node: NodesPrepared)
			{
				Node->EnumRefHierarchy(EndProc);
			}
			EndProc.EndEnumeration();
			NodesPrepared.Reset();
		}
	}

	void PrepareNode(INode* Node)
	{
		if (bRenderQuality)
		{
			// Skip if node was already Prepared
			bool bIsAlreadyPrepared;
			NodesPrepared.FindOrAdd(Node, &bIsAlreadyPrepared);
			if (bIsAlreadyPrepared)
			{
				return;
			}

			Node->EnumRefHierarchy(BeginProc);
		}
	}

	bool bRenderQuality = false; // If need to call RenderBegin on all nodes to make them return Render-quality mesh

	FBeginRefEnumProc BeginProc;
	FEndRefEnumProc EndProc;

	TSet<INode*> NodesPrepared;
};



struct FExportOptions
{
	// Default options for DirectLink change-tracking
	bool bSelectedOnly = false;
	bool bAnimatedTransforms = false;

	bool bStatSync = false;
};

// Global export options, stored in preferences
class FPersistentExportOptions: public IPersistentExportOptions
{
public:
	void Load()
	{
		if (bLoaded)
		{
			return;
		}
		GetBool(TEXT("SelectedOnly"), Options.bSelectedOnly);
		GetBool(TEXT("AnimatedTransforms"), Options.bAnimatedTransforms);
		bLoaded = true;
	}

	virtual void SetSelectedOnly(bool bValue) override
	{
		Options.bSelectedOnly = bValue;
		SetBool(TEXT("SelectedOnly"), bValue);
	}
	virtual bool GetSelectedOnly() override
	{
		return Options.bSelectedOnly;
	}

	virtual void SetAnimatedTransforms(bool bValue) override
	{
		Options.bAnimatedTransforms = bValue;
		SetBool(TEXT("AnimatedTransforms"), bValue);
	}

	virtual bool GetAnimatedTransforms() override
	{
		return Options.bAnimatedTransforms;
	}

	void GetBool(const TCHAR* Name, bool& bValue)
	{
		if (!GConfig)
		{
			return;
		}
		FString ConfigPath = GetConfigPath();
		GConfig->GetBool(TEXT("Export"), Name, bValue, ConfigPath);
	}

	void SetBool(const TCHAR* Name, bool bValue)
	{
		if (!GConfig)
		{
			return;
		}
		FString ConfigPath = GetConfigPath();
		GConfig->SetBool(TEXT("Export"), Name, bValue, ConfigPath);
		GConfig->Flush(false, ConfigPath);
	}

	FString GetConfigPath()
	{
		FString PlugCfgPath = GetCOREInterface()->GetDir(APP_PLUGCFG_DIR);
		return FPaths::Combine(PlugCfgPath, TEXT("UnrealDatasmithMax.ini"));
	}

	virtual void SetStatSync(bool bValue) override
	{
		Options.bStatSync = bValue;
		SetBool(TEXT("StatExport"), bValue);
	}

	virtual bool GetStatSync() override
	{
		return Options.bStatSync;
	}


	FExportOptions Options;
	bool bLoaded = false;
};

class FIncludeXRefGuard
{
public:


	FIncludeXRefGuard(bool bInIncludeXRefWhileParsing): bIncludeXRefWhileParsing(bInIncludeXRefWhileParsing)
	{
		if (bIncludeXRefWhileParsing)
		{
			bIncludeXRefsInHierarchyStored = GetCOREInterface()->GetIncludeXRefsInHierarchy();
			GetCOREInterface()->SetIncludeXRefsInHierarchy(true);
		}
	}
	~FIncludeXRefGuard()
	{
		if (bIncludeXRefWhileParsing)
		{
			GetCOREInterface()->SetIncludeXRefsInHierarchy(bIncludeXRefsInHierarchyStored);
		}
		
	}

	bool bIncludeXRefWhileParsing;
	BOOL bIncludeXRefsInHierarchyStored;
};

class FInvalidatedNodeTrackers: FNoncopyable
{
public:
	void Add(FNodeTracker& NodeTracker)
	{
		InvalidatedNodeTrackers.Add(&NodeTracker);
	}

	/**
	 * @return if anything was deleted
	 */
	bool PurgeDeletedNodeTrackers(class FSceneTracker& Scene);

	template <typename Func>
	void EnumerateAll(Func Callable)
	{
		for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
		{
			Callable(*NodeTracker);
		}
	}

	int32 Num()
	{
		return InvalidatedNodeTrackers.Num();
	}

	void Append(const TSet<FNodeTracker*>& NodeTrackers)
	{
		InvalidatedNodeTrackers.Append(NodeTrackers);
	}

	// Called when update is finished and all changes are processed and recorded
	void Finish()
	{
		InvalidatedNodeTrackers.Reset();
	}

	// Scene is reset so invalidation is reset too
	void Reset()
	{
		InvalidatedNodeTrackers.Reset();
	}

	bool HasInvalidated()
	{
		return !InvalidatedNodeTrackers.IsEmpty();
	}

	void RemoveFromInvalidated(FNodeTracker& NodeTracker)
	{
		InvalidatedNodeTrackers.Remove(&NodeTracker);
	}

private:
	TSet<FNodeTracker*> InvalidatedNodeTrackers;
};

class FNodeTrackersNames: FNoncopyable
{
public:
	TMap<FString, TSet<FNodeTracker*>> NodesForName;  // Each name can be used by a set of nodes
	void Reset()
	{
		NodesForName.Reset();
	}

	const FString& GetNodeName(FNodeTracker& NodeTracker)
	{
		return NodeTracker.Name;
	}

	void Update(FNodeTracker& NodeTracker)
	{
		FString Name = NodeTracker.Node->GetName();
		if (Name != NodeTracker.Name)
		{
			NodesForName[NodeTracker.Name].Remove(&NodeTracker);

			NodeTracker.Name = Name;
			NodesForName.FindOrAdd(NodeTracker.Name).Add(&NodeTracker);
		}
	}

	void Add(FNodeTracker& NodeTracker)
	{
		FString Name = NodeTracker.Node->GetName();

		// todo: dupclicated with Update
		NodeTracker.Name = Name;
		NodesForName.FindOrAdd(NodeTracker.Name).Add(&NodeTracker);
	}

	void Remove(const FNodeTracker& NodeTracker)
	{
		if (TSet<FNodeTracker*>* NodeTrackersPtr = NodesForName.Find(NodeTracker.Name))
		{
			NodeTrackersPtr->Remove(&NodeTracker);
		}
	}

	template <typename Func>
	void EnumerateForName(const FString& Name, Func Callable)
	{
		if (TSet<FNodeTracker*>* NodeTrackersPtr = NodesForName.Find(Name))
		{
			for (FNodeTracker* NodeTracker: *NodeTrackersPtr)
			{
				Callable(*NodeTracker);
			}
		}
	}
};


// Holds states of entities for syncronization and handles change events
class FSceneTracker: public ISceneTracker
{
public:
	FSceneTracker(const FExportOptions& InOptions, FDatasmith3dsMaxScene& InExportedScene, FNotifications* InNotificationsHandler)
		: Options(InOptions)
		, ExportedScene(InExportedScene)
		, NotificationsHandler(InNotificationsHandler)
		, MaterialsCollectionTracker(*this) {}

	virtual TSharedRef<IDatasmithScene> GetDatasmithSceneRef() override
	{
		return ExportedScene.GetDatasmithScene().ToSharedRef();
	}

	bool ParseScene()
	{
		FIncludeXRefGuard IncludeXRefGuard(bIncludeXRefWhileParsing);
		INode* Node = GetCOREInterface()->GetRootNode();
		bSceneParsed = ParseScene(Node);
		return bSceneParsed;
	}

	bool bParseXRefScenes = true;
	bool bIncludeXRefWhileParsing = false; 

	// Parse scene or XRef scene(in this case attach to parent datasmith actor)
	bool ParseScene(INode* SceneRootNode, FXRefScene XRefScene=FXRefScene())
	{
		LogDebugNode(TEXT("ParseScene"), SceneRootNode);
		// todo: do we need Root Datasmith node of scene/XRefScene in the hierarchy?
		// is there anything we need to handle for main file root node?
		// for XRefScene? Maybe addition/removal? Do we need one node to consolidate XRefScene under?

		// nodes comming from XRef Scenes/Objects could be null
		if (!SceneRootNode)
		{
			return false;
		}

		if (!bIncludeXRefWhileParsing)
		{
			// Parse XRefScenes
			for (int XRefChild = 0; XRefChild < SceneRootNode->GetXRefFileCount(); ++XRefChild)
			{
				DWORD XRefFlags = SceneRootNode->GetXRefFlags(XRefChild);

				SCENE_UPDATE_STAT_INC(ParseScene, XRefFileEncountered);

				// XRef is disabled - not shown in viewport/render. Not loaded.
				if (XRefFlags & XREF_DISABLED)
				{
					SCENE_UPDATE_STAT_INC(ParseScene, XRefFileDisabled);
					// todo: baseline - doesn't check this - exports even disabled and XREF_HIDDEN scenes
					continue;
				}

				FString Path = FDatasmithMaxSceneExporter::GetActualPath(SceneRootNode->GetXRefFile(XRefChild).GetFileName());
				if (FPaths::FileExists(Path) == false)
				{
					SCENE_UPDATE_STAT_INC(ParseScene, XRefFileMissing);
					FString Error = FString("XRefScene file \"") + FPaths::GetCleanFilename(*Path) + FString("\" cannot be found");
					// todo: logging
					// DatasmithMaxLogger::Get().AddMissingAssetError(*Error);
				}
				else
				{
					SCENE_UPDATE_STAT_INC(ParseScene, XRefFileToParse);
					ParseScene(SceneRootNode->GetXRefTree(XRefChild), FXRefScene{SceneRootNode, XRefChild});
				}
			}
		}

		int32 ChildNum = SceneRootNode->NumberOfChildren();
		for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
		{
			if (FNodeTracker* NodeTracker = ParseNode(SceneRootNode->GetChildNode(ChildIndex)))
			{
				// Record XRef this child node is at the root of
				// It's needed restore hierarchy when converting
				NodeTracker->SetXRefIndex(XRefScene);
			}
		}
		return true;
	}

	FNodeTracker* ParseNode(INode* Node)
	{
		LogDebugNode(TEXT("ParseNode"), Node);

		SCENE_UPDATE_STAT_INC(ParseNode, NodesEncountered);

		FNodeTracker* NodeTracker =  GetNodeTracker(Node);

		if (NodeTracker)
		{
			// Node being added might already be tracked(e.g. if it was deleted before but Update wasn't called to SceneTracker yet)
			//ensure(NodeTracker->bDeleted);
			NodeTracker->bDeleted = false;
			InvalidateNode(*NodeTracker);
			return NodeTracker;
		}
		else
		{
			NodeTracker = AddNode(NodeEventNamespace::GetKeyByNode(Node), Node);

			// Parse children
			int32 ChildNum = Node->NumberOfChildren();
			for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				ParseNode(Node->GetChildNode(ChildIndex));
			}
			return NodeTracker;
		}
	}

	// Check every layer and if it's modified invalidate nodes assigned to it
	// 3ds Max doesn't have events for all Layer changes(e.g. Name seems to be just an UI thing and has no notifications) so
	// we need to go through all layers every update to see what's changed
	bool UpdateLayers()
	{
		bool bChangeEncountered = false;

		ILayerManager* LayerManager = GetCOREInterface13()->GetLayerManager();
		int LayerCount = LayerManager->GetLayerCount();

		for (int LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
		{
			ILayer* Layer = LayerManager->GetLayer(LayerIndex);

			AnimHandle Handle = Animatable::GetHandleByAnim(Layer);

			TUniquePtr<FLayerTracker>& LayerTracker = LayersForAnimHandle.FindOrAdd(Handle);

			bool bIsHidden = Layer->IsHidden(TRUE);
			FString Name = Layer->GetName().data();

			if (!LayerTracker)
			{
				LayerTracker = MakeUnique<FLayerTracker>(Name, bIsHidden);
			}

			LayerTracker->SetName(Name);
			LayerTracker->SetIsHidden(bIsHidden);

			if (LayerTracker->bIsInvalidated)
			{
				bChangeEncountered = true;
				if (TSet<FNodeTracker*>* NodeTrackersPtr = NodesPerLayer.Find(LayerTracker.Get()))
				{
					for(FNodeTracker* NodeTracker:* NodeTrackersPtr)
					{
						InvalidateNode(*NodeTracker, false);
					}
				}
				LayerTracker->bIsInvalidated = false;
			}
		}
		return bChangeEncountered;
	}

	// Applies all recorded changes to Datasmith scene
	bool Update(FUpdateProgress::FStage& MainStage, bool bRenderQuality)
	{
		// Disable Undo, editing, redraw, messages during export/sync so that nothing changes the scene
		GetCOREInterface()->EnableUndo(false);
		GetCOREInterface()->DisableSceneRedraw();
		SuspendAll UberSuspend(TRUE, TRUE, TRUE, TRUE, TRUE, TRUE);

		// Flush all updates for SceneEventManager - so they are not received in mid of Update
		// When ProgressBar is updated it calls internal max event loop which can send unprocessed events to the callback
		if (NotificationsHandler)
		{
			NotificationsHandler->PrepareForUpdate();
		}

		DatasmithMaxLogger::Get().Purge();

		NodesPreparer.Start(GetCOREInterface()->GetTime(), bRenderQuality);

		bUpdateInProgress = true;

		bool bResult = false;

		{
			const int32 StageCount = 12;
			FProgressStageGuard ProgressStage(MainStage, TEXT("Update"), StageCount);
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				if (TSharedPtr<IDatasmithScene> ScenePtr = ExportedScene.GetDatasmithScene())
				{
					IDatasmithScene& Scene = *ScenePtr;
					return FString::Printf(TEXT("Actors: %d; Meshes: %d, Materials: %d"), 
					                       Scene.GetActorsCount(),
					                       Scene.GetMeshesCount(),
					                       Scene.GetMaterialsCount(),
					                       Scene.GetTexturesCount()
					);
				}
				return FString(TEXT("<no scene>"));
			};

			bResult = UpdateInternalSafe(ProgressStage.Stage);

		}
		bUpdateInProgress = false;

		NodesPreparer.Finish();

		UberSuspend.Resume();
		GetCOREInterface()->EnableSceneRedraw();
		GetCOREInterface()->EnableUndo(true);

		return bResult;
	}

	bool UpdateInternalSafe(FUpdateProgress::FStage& MainStage)
	{
		bool bResult = false;
		__try
		{
			bResult = UpdateInternal(MainStage);
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			LogWarning(TEXT("Update finished with exception"));
		}
		return bResult;
	}

	bool UpdateInternal(FUpdateProgress::FStage& MainStage)
	{

		CurrentSyncPoint.Time = GetCOREInterface()->GetTime();

		bool bChangeEncountered = false;

		Stats.Reset();

		if (!bSceneParsed) // Parse whole scene only once
		{
			PROGRESS_STAGE("Parse Scene")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsParseScene();
			};

			ParseScene();
		}

		{
			PROGRESS_STAGE("Refresh layers")
			bChangeEncountered = UpdateLayers() && bChangeEncountered;
		}

		// Changes present only when there are modified layers(changes checked manually), nodes(notified by Max) or materials(notified by Max with all changes in dependencies)
		bChangeEncountered |= !MaterialsCollectionTracker.GetInvalidatedMaterials().IsEmpty();

		{
			PROGRESS_STAGE("Remove deleted nodes")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsRemoveDeletedNodes();
			};

			bChangeEncountered |= InvalidatedNodeTrackers.PurgeDeletedNodeTrackers(*this);
		}

		{
			PROGRESS_STAGE("Check Time Slider Validity")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsCheckTimeSliderValidity();
			};

			FIncludeXRefGuard IncludeXRefGuard(bIncludeXRefWhileParsing);

			INode* SceneRootNode = GetCOREInterface()->GetRootNode();

			int32 ChildNum = SceneRootNode->NumberOfChildren();
			for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				INode* Node = SceneRootNode->GetChildNode(ChildIndex);

				if (FNodeTracker* NodeTracker = GetNodeTracker(Node))
				{
					InvalidateOutdatedNodeTracker(*NodeTracker);
				}
			}
		}

		{
			PROGRESS_STAGE("Refresh collisions") // Update set of nodes used for collision
			PROGRESS_STAGE_COUNTER(InvalidatedNodeTrackers.Num());

			TSet<FNodeTracker*> NodesWithChangedCollisionStatus; // Need to invalidate these nodes to make them renderable or to keep them from renderable depending on collision status

			InvalidatedNodeTrackers.EnumerateAll([&](FNodeTracker& NodeTracker)
				{
					ProgressCounter.Next();
					UpdateCollisionStatus(NodeTracker, NodesWithChangedCollisionStatus);
				});

			// Rebuild all nodes that has changed them being colliders
			for (FNodeTracker* NodeTracker : NodesWithChangedCollisionStatus)
			{
				InvalidateNode(*NodeTracker, false);
			}

			SCENE_UPDATE_STAT_SET(RefreshCollisions, ChangedNodes, NodesWithChangedCollisionStatus.Num());

			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsRefreshCollisions();
			};
		}

		{
			PROGRESS_STAGE("Process invalidated nodes")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsProcessInvalidatedNodes();
			};

			PROGRESS_STAGE_COUNTER(InvalidatedNodeTrackers.Num());
			InvalidatedNodeTrackers.EnumerateAll([&](FNodeTracker& NodeTracker)
				{
					ProgressCounter.Next();
					UpdateNode(NodeTracker);
				});
		}

		{
			PROGRESS_STAGE("Process invalidated instances")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsProcessInvalidatedInstances();
			};
			PROGRESS_STAGE_COUNTER(InvalidatedInstances.Num());
			for (FInstances* Instances : InvalidatedInstances)
			{
				ProgressCounter.Next();
				UpdateInstances(*Instances);

				// Need to re-convert and reattach all instances of an updated node
				InvalidatedNodeTrackers.Append(Instances->NodeTrackers);
			}

			InvalidatedInstances.Reset();
		}

		{
			PROGRESS_STAGE("Convert nodes to datasmith")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsConvertNodesToDatasmith();
			};

			PROGRESS_STAGE_COUNTER(InvalidatedNodeTrackers.Num());
			InvalidatedNodeTrackers.EnumerateAll([&](FNodeTracker& NodeTracker)
				{
					ProgressCounter.Next();

					if (NodeTracker.HasConverter())
					{
						SCENE_UPDATE_STAT_INC(ConvertNodes, Converted)
						NodeTracker.GetConverter().ConvertToDatasmith(*this, NodeTracker);
					}
				});
		}

		{
			PROGRESS_STAGE("Reparent Datasmith Actors");
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsReparentDatasmithActors();
			};
			
			InvalidatedNodeTrackers.EnumerateAll([&](FNodeTracker& NodeTracker)
				{
					AttachNodeToDatasmithScene(NodeTracker);
				});
			;
		}

		{
			PROGRESS_STAGE("Mark nodes validated")
			// Finally mark all nodetrackers as valid(we didn't interrupt update as it went to this point)
			// And calculate subtree validity for each subtree of each updated node

			// Maximize each invalidated node's subtree validity interval before recalculating it from updated nodes
			InvalidatedNodeTrackers.EnumerateAll([](FNodeTracker& NodeTracker)
				{
					NodeTracker.SubtreeValidity.Invalidate();
					NodeTracker.SubtreeValidity.ResetValidityInterval();
				});

			// Recalculate subtree validity 
			InvalidatedNodeTrackers.EnumerateAll([&](FNodeTracker& NodeTracker)
				{
					PromoteValidity(NodeTracker, NodeTracker.Validity);
				});

			InvalidatedNodeTrackers.EnumerateAll([](FNodeTracker& NodeTracker)
				{
					NodeTracker.SubtreeValidity.SetValid();
					NodeTracker.SetValid();
				});

			bChangeEncountered |= InvalidatedNodeTrackers.HasInvalidated(); // Right before resetting invalidated nodes, record that anything was invalidated
			InvalidatedNodeTrackers.Finish();
		}

		// Each tracked(i.e. assigned to a visible node) Max material can result in multiple actual materials
		// e.g. an assigned MultiSubObj material is aclually a set of material not directly assigned to a node
		TSet<Mtl*> ActualMaterialToUpdate; 
		{
			PROGRESS_STAGE("Process invalidated materials");
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsProcessInvalidatedMaterials();
			};

			PROGRESS_STAGE_COUNTER(MaterialsCollectionTracker.GetInvalidatedMaterials().Num());
			for (FMaterialTracker* MaterialTracker : MaterialsCollectionTracker.GetInvalidatedMaterials())
			{
				ProgressCounter.Next();
				SCENE_UPDATE_STAT_INC(ProcessInvalidatedMaterials, Invalidated);

				MaterialsCollectionTracker.UpdateMaterial(MaterialTracker);

				for (Mtl* ActualMaterial : MaterialTracker->GetActualMaterials())
				{
					SCENE_UPDATE_STAT_INC(ProcessInvalidatedMaterials, ActualToUpdate);
					ActualMaterialToUpdate.Add(ActualMaterial);
				}
				MaterialTracker->bInvalidated = false;
			}

			MaterialsCollectionTracker.ResetInvalidatedMaterials();
		}

		TSet<Texmap*> ActualTexmapsToUpdate;
		{
			PROGRESS_STAGE("Update materials")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsUpdateMaterials();
			};
			PROGRESS_STAGE_COUNTER(ActualMaterialToUpdate.Num());
			for (Mtl* ActualMaterial: ActualMaterialToUpdate)
			{
				ProgressCounter.Next();

				MaterialsCollectionTracker.ConvertMaterial(ActualMaterial, ExportedScene.GetDatasmithScene().ToSharedRef(), ExportedScene.GetSceneExporter().GetAssetsOutputPath(), ActualTexmapsToUpdate);
			}
		}

		{
			PROGRESS_STAGE("Update textures")
			PROGRESS_STAGE_RESULT_DEFERRED
			{
				return FormatStatsUpdateTextures();
			};
			PROGRESS_STAGE_COUNTER(ActualTexmapsToUpdate.Num());

			for (Texmap* Texture : ActualTexmapsToUpdate)
			{
				ProgressCounter.Next();
				SCENE_UPDATE_STAT_INC(UpdateTextures, Total);

				TArray<TSharedPtr<IDatasmithTextureElement>> TextureElements;
				FDatasmithMaxMatExport::GetXMLTexture(ExportedScene.GetDatasmithScene().ToSharedRef(), Texture, ExportedScene.GetSceneExporter().GetAssetsOutputPath(), &TextureElements);
				MaterialsCollectionTracker.UsedTextureToDatasmithElement.Add(Texture, TextureElements);
			}
		}

		// todo: removes textures that were added again(materials were updated). Need to fix this by identifying exactly which textures are being updated and removing ahead
		//TMap<FString, TSharedPtr<IDatasmithTextureElement>> TexturesAdded;
		//TArray<TSharedPtr<IDatasmithTextureElement>> TexturesToRemove;
		//for(int32 TextureIndex = 0; TextureIndex < ExportedScene.GetDatasmithScene()->GetTexturesCount(); ++TextureIndex )
		//{
		//	TSharedPtr<IDatasmithTextureElement> TextureElement = ExportedScene.GetDatasmithScene()->GetTexture(TextureIndex);
		//	FString Name = TextureElement->GetName();
		//	if (TexturesAdded.Contains(Name))
		//	{
		//		TexturesToRemove.Add(TexturesAdded[Name]);
		//		TexturesAdded[Name] = TextureElement;
		//	}
		//	else
		//	{
		//		TexturesAdded.Add(Name, TextureElement);
		//	}
		//}

		//for(TSharedPtr<IDatasmithTextureElement> Texture: TexturesToRemove)
		//{
		//	ExportedScene.GetDatasmithScene()->RemoveTexture(Texture);
		//}
		return bChangeEncountered;
	}

	FString FormatStatsParseScene()
	{
		return FString::Printf(TEXT("Nodes: parsed %d"), SCENE_UPDATE_STAT_GET(ParseNode, NodesEncountered));
	}

	FString FormatStatsRemoveDeletedNodes()
	{
		return FString::Printf(TEXT("Nodes: deleted %d"), SCENE_UPDATE_STAT_GET(RemoveDeletedNodes, Nodes));
	}

	FString FormatStatsUpdateNodeNames()
	{
		return FString::Printf(TEXT("Nodes: updated %d of total %d"), InvalidatedNodeTrackers.Num(), NodeTrackers.Num());
	}

	FString FormatStatsRefreshCollisions()
	{
		return FString::Printf(TEXT("Nodes: added %d to invalidated %d"), SCENE_UPDATE_STAT_GET(RefreshCollisions, ChangedNodes), InvalidatedNodeTrackers.Num());
	}

	FString FormatStatsCheckTimeSliderValidity()
	{
		return FString::Printf(TEXT("Check TimeSlider: checked %d, invalidated %d, skipped  - already invalidated %d, subtree valid %d"),
			SCENE_UPDATE_STAT_GET(CheckTimeSlider, TotalChecks),
			SCENE_UPDATE_STAT_GET(CheckTimeSlider, Invalidated),
			SCENE_UPDATE_STAT_GET(CheckTimeSlider, SkippedAsAlreadyInvalidated),
			SCENE_UPDATE_STAT_GET(CheckTimeSlider, SkippedAsSubtreeValid)
			);
	}

	FString FormatStatsProcessInvalidatedNodes()
	{
		return FString::Printf(TEXT("Nodes: %d updated, %d skipped unselected, %d skipped hidden"), SCENE_UPDATE_STAT_GET(UpdateNode, NodesUpdated), SCENE_UPDATE_STAT_GET(UpdateNode, SkippedAsUnselected), SCENE_UPDATE_STAT_GET(UpdateNode, SkippedAsHiddenNode));
	}

	FString FormatStatsConvertNodesToDatasmith()
	{
		return FString::Printf(TEXT("Nodes: %d converted"), SCENE_UPDATE_STAT_GET(ConvertNodes, Converted));
	}

	FString FormatStatsProcessInvalidatedInstances()
	{
		return FString::Printf(TEXT("Instances: %ld updated"), InvalidatedInstances.Num());
	}

	FString FormatStatsReparentDatasmithActors()
	{
		return FString::Printf(TEXT("Nodes: %d attached, to root %d, skipped %d"), SCENE_UPDATE_STAT_GET(ReparentActors, Attached), SCENE_UPDATE_STAT_GET(ReparentActors, AttachedToRoot), SCENE_UPDATE_STAT_GET(ReparentActors, SkippedWithoutDatasmithActor));
	}

	FString FormatStatsProcessInvalidatedMaterials()
	{
		return FString::Printf(TEXT("Materials: %d reparsed, found %d actual to update"), SCENE_UPDATE_STAT_GET(ProcessInvalidatedMaterials, Invalidated), SCENE_UPDATE_STAT_GET(ProcessInvalidatedMaterials, ActualToUpdate));
	}

	FString FormatStatsUpdateMaterials()
	{
		return FString::Printf(TEXT("Materials: %d updated, %d converted, %d skipped as already converted"), SCENE_UPDATE_STAT_GET(UpdateMaterials, Total), SCENE_UPDATE_STAT_GET(UpdateMaterials, Converted), SCENE_UPDATE_STAT_GET(UpdateMaterials, SkippedAsAlreadyConverted));
	}

	FString FormatStatsUpdateTextures()
	{
		return FString::Printf(TEXT("Texmaps: %d updated"), SCENE_UPDATE_STAT_GET(UpdateTextures, Total));
	}

	void ExportAnimations()
	{
		FDatasmithConverter Converter;
		// Use the same name for the unique level sequence as the scene name
		TSharedRef<IDatasmithLevelSequenceElement> LevelSequence = FDatasmithSceneFactory::CreateLevelSequence(GetDatasmithScene().GetName());
		LevelSequence->SetFrameRate(GetFrameRate());

		for (TPair<FNodeKey, FNodeTrackerHandle> NodeKeyAndNodeTracker: NodeTrackers)
		{
			FNodeTracker* NodeTracker = NodeKeyAndNodeTracker.Value.GetNodeTracker();

			if (NodeTracker->HasConverted())
			{

				if (NodeTracker->GetConverterType() == FNodeConverter::LightNode)
				{
					const TSharedPtr<IDatasmithLightActorElement> LightElement = StaticCastSharedPtr< IDatasmithLightActorElement >(NodeTracker->GetConverted().DatasmithActorElement);
					const FMaxLightCoordinateConversionParams LightParams(NodeTracker->Node,
						LightElement->IsA(EDatasmithElementType::AreaLight) ? StaticCastSharedPtr<IDatasmithAreaLightElement>(LightElement)->GetLightShape() : EDatasmithLightShape::None);
					FDatasmithMaxSceneExporter::ExportAnimation(LevelSequence, NodeTracker->Node, NodeTracker->GetConverted().DatasmithActorElement->GetName(), Converter.UnitToCentimeter, LightParams);
				}
				else
				{
					FDatasmithMaxSceneExporter::ExportAnimation(LevelSequence, NodeTracker->Node, NodeTracker->GetConverted().DatasmithActorElement->GetName(), Converter.UnitToCentimeter);
				}
			}
		}
		if (LevelSequence->GetAnimationsCount() > 0)
		{
			GetDatasmithScene().AddLevelSequence(LevelSequence);
		}
	}

	FORCENOINLINE
	FNodeTracker* AddNode(FNodeKey NodeKey, INode* Node)
	{
		LogDebugNode(TEXT("AddNode"), Node);
		FNodeTracker* NodeTracker = NodeTrackers.Emplace(NodeKey, FNodeTrackerHandle(NodeKey, Node)).GetNodeTracker();

		NodeTrackersNames.Add(*NodeTracker);
		InvalidatedNodeTrackers.Add(*NodeTracker);

		return NodeTracker;
	}

	virtual void RemoveMaterial(const TSharedPtr<IDatasmithBaseMaterialElement>& DatasmithMaterial) override
	{
		ExportedScene.DatasmithSceneRef->RemoveMaterial(DatasmithMaterial);
	}

	virtual void RemoveTexture(const TSharedPtr<IDatasmithTextureElement>& DatasmithTextureElement) override
	{
		ExportedScene.DatasmithSceneRef->RemoveTexture(DatasmithTextureElement);		
	}

	FNodeTracker* GetNodeTracker(FNodeKey NodeKey)
	{
		if (FNodeTrackerHandle* NodeTrackerHandle = NodeTrackers.Find(NodeKey))
		{
			return NodeTrackerHandle->GetNodeTracker();
		}
		return nullptr;
	}

	FNodeTracker* GetNodeTracker(INode* ParentNode)
	{
		return GetNodeTracker(NodeEventNamespace::GetKeyByNode(ParentNode));
	}

	// Promote validity up the ancestor chain
	// Each Subtree Validity interval represents intersection of all invervals of descendants
	// Used to quickly determine if some whole subtree needs to be updated when Time Slider changes
	void PromoteValidity(FNodeTracker& NodeTracker, const FValidity& Validity)
	{
		if (Validity.Overlaps(NodeTracker.SubtreeValidity))
		{
			// Subtree validity is already fully within new validity - don't need to compute intersection and promote it further
			return;
		}

		NodeTracker.SubtreeValidity.NarrowValidityToInterval(Validity);

		// Promote recalculated SubtreeValidity to parent
		if (FNodeTracker* ParentNodeTracker = GetNodeTracker(NodeTracker.Node->GetParentNode()))
		{
			PromoteValidity(*ParentNodeTracker, NodeTracker.SubtreeValidity);
		}
	}

	// Node invalidated:
	//   - on initial parsing
	//   - change event received for node itself
	//   - node up hierachy invalidated - previous or current
	//   - node's validity interval is invalid for current time
	// Possible improvements:
	//   - do we really need to invalidate hierachy immediately? Might be long task(even though it's just setting flags) 
	//      - probably do this on Update
	//          'MarkedForUpdate' => recursively Invalidate(i.e. rebuild)
	//          separate invalidation to 'Changed'(received event, interval, etc) and 'NeedRebuild'?
	//            'NeedRebuild' are caused by 'Changed', not just 'invalidated' all.
	//         - e.g. on initial parsing - only root nodes are 'changed'
	//         - mark 'changed' as 'need rebuild' and descend to children, if a child has no 'need rebuild' set it, else skip its hierarchy

	void InvalidateNode(FNodeTracker& NodeTracker, bool bCheckCalledInProgress = true)
	{
		// We don't expect node chances while Update inprogress(unless Invalidate called explicitly)
		if (bCheckCalledInProgress)
		{
			ensure(!bUpdateInProgress);
		}

		if (NodeTracker.bDeleted)
		{
			// Change events sometimes received for nodes that are already deleted
			// skipping processing of node subtree because INode pointer may already be invalid
			// Test case: create container, add node to it. Close it, open it, close again, then sync
			// Change event will be received for NodeKey that returns NULL from NodeEventNamespace::GetNodeByKey
			if (bIncludeXRefWhileParsing)
			{
				check(NodeEventNamespace::GetNodeByKey(NodeTracker.NodeKey));
				NodeTracker.bDeleted = false;
			}
			else
			{
				return;
			}
		}

		if (NodeTracker.IsInvalidated())
		{
			// Don't do work twice - already invalidated node would have its subhierarchy invalidated
			//   in case subhierarchy was changed for already invalidated node - the changed nodes would invalidate too(responding to reparent event)
			return;
		}

		NodeTracker.Invalidate();
		InvalidatedNodeTrackers.Add(NodeTracker);

		// Invalidate whole sub-hierarchy of nodes are now children.
		// E.g. a node could have been hidden so its children were attached to grandparent(parent of hidden node)
		// Need to invalidate those to reattach
		FIncludeXRefGuard IncludeXRefGuard(bIncludeXRefWhileParsing);
		int32 ChildNum = NodeTracker.Node->NumberOfChildren();
		for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
		{
			InvalidateNode(NodeEventNamespace::GetKeyByNode(NodeTracker.Node->GetChildNode(ChildIndex)), bCheckCalledInProgress);
		}
	}

	void InvalidateOutdatedNodeTracker(FNodeTracker& NodeTracker)
	{
		SCENE_UPDATE_STAT_INC(CheckTimeSlider, TotalChecks);
		// Skip check in case whole subtree valid for current time OR node was already invalidated(and so its whole subtree)
		if (NodeTracker.IsInvalidated())
		{
			SCENE_UPDATE_STAT_INC(CheckTimeSlider, SkippedAsAlreadyInvalidated);
			return;
		}

		if (NodeTracker.IsSubtreeValidForSyncPoint(CurrentSyncPoint))
		{
			SCENE_UPDATE_STAT_INC(CheckTimeSlider, SkippedAsSubtreeValid);
			return;
		}

		// If node is invalid - reevaliate whole subtree
		// todo: it it possible to optimize reevaluation of whole subtree?
		//   certain types of invalidation need not to propagate down to descendants(e.g. geometry change)
		//   but some need to, like transform change
		if (!NodeTracker.IsValidForSyncPoint(CurrentSyncPoint))
		{
			SCENE_UPDATE_STAT_INC(CheckTimeSlider, Invalidated);
			InvalidateNode(NodeTracker, false);
		}
		else
		{
			FIncludeXRefGuard IncludeXRefGuard(bIncludeXRefWhileParsing);
			int32 ChildNum = NodeTracker.Node->NumberOfChildren();
			for (int32 ChildIndex = 0; ChildIndex < ChildNum; ++ChildIndex)
			{
				if (FNodeTracker* ChildNodeTracker = GetNodeTracker(NodeTracker.Node->GetChildNode(ChildIndex)))
				{
					InvalidateOutdatedNodeTracker(*ChildNodeTracker);
				}
			}
		}
	}
	
	// todo: make fine invalidates - full only something like geometry change, but finer for transform, name change and more
	FNodeTracker* InvalidateNode(FNodeKey NodeKey, bool bCheckCalledInProgress = true)
	{
		FIncludeXRefGuard IncludeXRefGuard(bIncludeXRefWhileParsing);

		LogDebugNode(TEXT("InvalidateNode"), NodeEventNamespace::GetNodeByKey(NodeKey));
		// We don't expect node chances while Update inprogress(unless Invalidate called explicitly)
		if (bCheckCalledInProgress)
		{
			ensure(!bUpdateInProgress);
		}

		if (FNodeTracker* NodeTracker =  GetNodeTracker(NodeKey))
		{
			if (NodeEventNamespace::GetNodeByKey(NodeKey))
			{
				InvalidateNode(*NodeTracker, bCheckCalledInProgress);
				return NodeTracker;
			}
			else
			{
				// Sometimes node update received without node Delete event
				// Test case: create container, add node to it. Close it, open it, close again, then sync
				InvalidatedNodeTrackers.Add(*NodeTracker);
				NodeTracker->bDeleted = true;
			}
		}
		else
		{
			NodeAdded(NodeEventNamespace::GetNodeByKey(NodeKey));
		}
		return nullptr;
	}

	void ClearNodeFromDatasmithScene(FNodeTracker& NodeTracker)
	{
		ReleaseNodeTrackerFromDatasmithMetadata(NodeTracker);

		// remove from hierarchy
		if (NodeTracker.HasConverted())
		{
			FNodeConverted& Converted = NodeTracker.GetConverted();
			TSharedPtr<IDatasmithMeshActorElement> DatasmithMeshActor = Converted.DatasmithMeshActor;

			// Remove mesh actor before removing its parent Actor in case there a separate MeshActor
			if (Converted.DatasmithMeshActor)
			{
				// if (NodeTracker.DatasmithMeshActor != NodeTracker.DatasmithActorElement)
				{
					Converted.DatasmithActorElement->RemoveChild(Converted.DatasmithMeshActor);
				}
				Converted.DatasmithMeshActor.Reset();
				// todo: consider pool of MeshActors
			}

			if (TSharedPtr<IDatasmithActorElement> ParentActor = Converted.DatasmithActorElement->GetParentActor())
			{
				ParentActor->RemoveChild(Converted.DatasmithActorElement);
			}
			else
			{
				// Detach all children(so they won't be reattached automatically to root when actor is detached from parent below)
				// Children reattachment will happen later in Update
				int32 ChildCount = Converted.DatasmithActorElement->GetChildrenCount();
				// Remove last child each time to optimize array elements relocation
				for(int32 ChildIndex = ChildCount-1; ChildIndex >= 0; --ChildIndex)
				{
					Converted.DatasmithActorElement->RemoveChild(Converted.DatasmithActorElement->GetChild(ChildIndex));
				}
				ExportedScene.DatasmithSceneRef->RemoveActor(Converted.DatasmithActorElement, EDatasmithActorRemovalRule::RemoveChildren);
			}
			Converted.DatasmithActorElement.Reset();

			NodeTracker.ReleaseConverted();
		}
		
	}

	// Called when mesh element is not needed anymore and should be removed from the scene
	virtual void ReleaseMeshElement(FMeshConverted& Converted) override
	{
		GetDatasmithScene().RemoveMesh(Converted.DatasmithMeshElement);
		Converted.ReleaseMeshConverted();
	}

	void ReleaseNodeTrackerFromLayer(FNodeTracker& NodeTracker)
	{
		if (NodeTracker.Layer)
		{
			if (TSet<FNodeTracker*>* NodeTrackerPtr = NodesPerLayer.Find(NodeTracker.Layer))
			{
				NodeTrackerPtr->Remove(&NodeTracker);
			}
			NodeTracker.Layer = nullptr;
		}
	}

	void ReleaseNodeTrackerFromDatasmithMetadata(FNodeTracker& NodeTracker)
	{
		TSharedPtr<IDatasmithMetaDataElement> DatasmithMetadata;
		if (NodeDatasmithMetadata.RemoveAndCopyValue(&NodeTracker, DatasmithMetadata))
		{
			GetDatasmithScene().RemoveMetaData(DatasmithMetadata);
		}
	}

	// Release node from any connection to other tracked objects
	// When node is removed or to clear converter that is configured for invalidated node in Update
	// BUT excluding node name and collision status - these two are updated before UpdateNode is called
	void RemoveFromTracked(FNodeTracker& NodeTracker)
	{
		// todo: record previous converter Node type to speed up cleanup. Or just add 'unconverted' flag to speed up this for nodes that weren't converted yet

		ReleaseNodeTrackerFromLayer(NodeTracker);

		if (NodeTracker.HasConverter())
		{
			NodeTracker.GetConverter().RemoveFromTracked(*this, NodeTracker);
			NodeTracker.ReleaseConverter();
		}
	}

	void UpdateCollisionStatus(FNodeTracker& NodeTracker, TSet<FNodeTracker*>& NodesWithChangedCollisionStatus)
	{
		// Check if collision assigned for node changed
		{
			TOptional<FDatasmithMaxStaticMeshAttributes> DatasmithAttributes = FDatasmithMaxStaticMeshAttributes::ExtractStaticMeshAttributes(NodeTracker.Node);

			bool bOutFromDatasmithAttributes;
			INode* CollisionNode = GeomUtils::GetCollisionNode(*this, NodeTracker.Node, DatasmithAttributes ? &DatasmithAttributes.GetValue() : nullptr, bOutFromDatasmithAttributes);

			FNodeTracker* CollisionNodeTracker = GetNodeTracker(CollisionNode); // This node should be tracked

			if (NodeTracker.Collision != CollisionNodeTracker)
			{
				// Update usage counters for collision nodes

				// Remove previous
				if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(NodeTracker.Collision))
				{
					TSet<FNodeTracker*>& CollisionUsers = *CollisionUsersPtr;
					CollisionUsers.Remove(&NodeTracker);

					if (CollisionUsers.IsEmpty())
					{
						CollisionNodes.Remove(NodeTracker.Collision);
						NodesWithChangedCollisionStatus.Add(NodeTracker.Collision);
					}
				}

				// Add new
				if (CollisionNodeTracker)
				{
					if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(CollisionNodeTracker))
					{
						TSet<FNodeTracker*>& CollisionUsers = *CollisionUsersPtr;
						CollisionUsers.Add(&NodeTracker);
					}
					else
					{
						TSet<FNodeTracker*>& CollisionUsers = CollisionNodes.Add(CollisionNodeTracker);
						CollisionUsers.Add(&NodeTracker);
						NodesWithChangedCollisionStatus.Add(CollisionNodeTracker);
					}
				}
				NodeTracker.Collision = CollisionNodeTracker;
			}
		}

		// Check if node changed its being assigned as collision
		{
			if (FDatasmithMaxSceneParser::HasCollisionName(NodeTracker.Node))
			{
				CollisionNodes.Add(&NodeTracker); // Always view node with 'collision' name as a collision node(i.e. no render)

				//Check named collision assignment(e.g. 'UCP_<other nothe name>')
				// Split collision prefix and find node that might use this node as collision mesh
				FString NodeName = NodeTrackersNames.GetNodeName(NodeTracker);
				FString LeftString, RightString;
				NodeName.Split(TEXT("_"), &LeftString, &RightString);

				NodeTrackersNames.EnumerateForName(RightString, [&](FNodeTracker& CollisionUserNodeTracker)
				{
					if (CollisionUserNodeTracker.Collision != &NodeTracker)
					{
						NodesWithChangedCollisionStatus.Add(&CollisionUserNodeTracker); // Invalidate each node that has collision changed
					}
				});
			}
			else
			{
				// Remove from registered collision nodes if there's not other users(i.e. using Datasmith attributes reference)
				if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(&NodeTracker))
				{
					if (CollisionUsersPtr->IsEmpty())
					{
						CollisionNodes.Remove(&NodeTracker);
					}
				}
			}
		}
	}

	void RemoveNodeTracker(FNodeTracker& NodeTracker)
	{
		SCENE_UPDATE_STAT_INC(RemoveDeletedNodes, Nodes);

		InvalidatedNodeTrackers.RemoveFromInvalidated(NodeTracker);

		ClearNodeFromDatasmithScene(NodeTracker);
		RemoveFromTracked(NodeTracker);

		NodeTrackersNames.Remove(NodeTracker);

		if (TSet<FNodeTracker*>* CollisionUsersPtr = CollisionNodes.Find(NodeTracker.Collision))
		{
			TSet<FNodeTracker*>& CollisionUsers = *CollisionUsersPtr;
			CollisionUsers.Remove(&NodeTracker);

			if (CollisionUsers.IsEmpty())
			{
				CollisionNodes.Remove(NodeTracker.Collision);
			}
		}

		NodeTrackers.Remove(NodeTracker.NodeKey);
	}

	void UpdateNode(FNodeTracker& NodeTracker)
	{
		SCENE_UPDATE_STAT_INC(UpdateNode, NodesUpdated);
		// Forget anything that this node was before update: place in datasmith hierarchy, datasmith objects, instances connection. Updating may change anything
		ClearNodeFromDatasmithScene(NodeTracker);
		RemoveFromTracked(NodeTracker); // todo: might keep it tracked by complicating conversions later(e.g. to avoid extra work if object stays the same)

		NodeTracker.ResetValidityInterval(); // Make infinite validity to narrow it during update

		ConvertNodeObject(NodeTracker);
	}

	void ConvertNodeObject(FNodeTracker& NodeTracker)
	{
		// Update layer connection
		ILayer* Layer = (ILayer*)NodeTracker.Node->GetReference(NODE_LAYER_REF);
		if (Layer)
		{
			if (TUniquePtr<FLayerTracker>* LayerPtr = LayersForAnimHandle.Find(Animatable::GetHandleByAnim(Layer)))
			{
				FLayerTracker* LayerTracker =  LayerPtr->Get();
				NodeTracker.Layer = LayerTracker;
				NodesPerLayer.FindOrAdd(LayerTracker).Add(&NodeTracker);
			}
		}

		if (CollisionNodes.Contains(&NodeTracker))
		{
			SCENE_UPDATE_STAT_INC(UpdateNode, SkippedAsCollisionNode);
			return;
		}

		if (NodeTracker.Node->IsNodeHidden(TRUE) || !NodeTracker.Node->Renderable())
		{
			SCENE_UPDATE_STAT_INC(UpdateNode, SkippedAsHiddenNode);
			return;
		}

		if (Options.bSelectedOnly && !NodeTracker.Node->Selected())
		{
			SCENE_UPDATE_STAT_INC(UpdateNode, SkippedAsUnselected);
			return;
		}

		ObjectState ObjState = NodeTracker.Node->EvalWorldState(CurrentSyncPoint.Time);
		Object* Obj = ObjState.obj;

		if (!Obj)
		{
			return;
		}

		SClass_ID SuperClassID = Obj->SuperClassID();
		switch (SuperClassID)
		{
		case HELPER_CLASS_ID:
			SCENE_UPDATE_STAT_INC(UpdateNode, HelpersEncontered);
			NodeTracker.CreateConverter<FHelperNodeConverter>();
			break;
		case CAMERA_CLASS_ID:
			SCENE_UPDATE_STAT_INC(UpdateNode, CamerasEncontered);
			NodeTracker.CreateConverter<FCameraNodeConverter>();
			break;
		case LIGHT_CLASS_ID:
		{
			SCENE_UPDATE_STAT_INC(UpdateNode, LightsEncontered);

			if (EMaxLightClass::Unknown == FDatasmithMaxSceneParser::GetLightClass(NodeTracker.Node))
			{
				SCENE_UPDATE_STAT_INC(UpdateNode, LightsSkippedAsUnknown);
				break;
			}

			NodeTracker.CreateConverter<FLightNodeConverter>();
			break;
		}
		case SHAPE_CLASS_ID:
		case GEOMOBJECT_CLASS_ID:
		{
			SCENE_UPDATE_STAT_INC(UpdateNode, GeomObjEncontered);
			Class_ID ClassID = ObjState.obj->ClassID();
			if (ClassID.PartA() == TARGET_CLASS_ID) // Convert camera target as regular actor
			{
				NodeTracker.CreateConverter<FHelperNodeConverter>();
			}
			else if (ClassID == RAILCLONE_CLASS_ID)
			{
				NodeTracker.CreateConverter<FRailCloneNodeConverter>();
				break;
			}
			else if (ClassID == ITOOFOREST_CLASS_ID)
			{
				NodeTracker.CreateConverter<FForestNodeConverter>();
				break;
			}
			else
			{
				if (FDatasmithMaxSceneParser::HasCollisionName(NodeTracker.Node))
				{
					ConvertNamedCollisionNode(NodeTracker);
				}
				else
				{
					if (Obj->IsRenderable()) // Shape's Enable In Render flag(note - different from Node's Renderable flag)
					{
						SCENE_UPDATE_STAT_INC(UpdateNode, GeomObjConverted);

						NodeTracker.CreateConverter<FMeshNodeConverter>();
					}
					else
					{
						SCENE_UPDATE_STAT_INC(UpdateNode, GeomObjSkippedAsNonRenderable);
					}
				}
			}
			break;
		}
		case SYSTEM_CLASS_ID:
		{
			//When a referenced file is not found XRefObj is not resolved then it's kept as XREFOBJ_CLASS_ID instead of resolved class that it references
			if (Obj->ClassID() == XREFOBJ_CLASS_ID)
			{
				FString Path = FDatasmithMaxSceneExporter::GetActualPath(static_cast<IXRefObject8*>(Obj)->GetFile(FALSE).GetFileName());
				if (!FPaths::FileExists(Path))
				{
					LogWarning(FString("XRefObj file \"") + FPaths::GetCleanFilename(*Path) + FString("\" cannot be found"));
				}
			}

			break;
		}
		default:;
		}

		if (NodeTracker.HasConverter())
		{
			NodeTracker.GetConverter().Parse(*this, NodeTracker);
		}
	}

	void InvalidateInstances(FInstances& Instances)
	{
		InvalidatedInstances.Add(&Instances);
	}

	void UpdateInstances(FInstances& Instances)
	{
		if (Instances.NodeTrackers.IsEmpty())
		{
			// Invalidated instances without actual instances left(all removed)

			// NOTE: release mesh element and release usage of Instances pointer
			//   BEFORE deallocating Instances instance from the map below
			ReleaseMeshElement(Instances.Converted);

			InstancesManager.RemoveInstances(Instances);
			return;
		}

		for (FNodeTracker* NodeTrackerPtr : Instances.NodeTrackers)
		{
			FNodeTracker& NodeTracker = *NodeTrackerPtr;
			ClearNodeFromDatasmithScene(NodeTracker);
			if (ensure(NodeTracker.GetConverter().ConverterType == FNodeConverter::MeshNode))
			{
				static_cast<FMeshNodeConverter&>(NodeTracker.GetConverter()).bMaterialsAssignedToStaticMesh = false;
			}
		}

		// Export static mesh using first lucky node
		// todo: possible optimization to reuse previous node. Somehow. 
		for (FNodeTracker* NodeTrackerPtr : Instances.NodeTrackers)
		{
			FNodeTracker& NodeTracker = *NodeTrackerPtr;

			// todo: use single EnumProc instance to enumerate all nodes during update to:
			//    - have single call to BeginEnumeration and EndEnumeration
			//    - track all Begin'd nodes to End them together after all is updated(to prevent duplicated Begin's of referenced objects that might be shared by different noвes)
			NodesPreparer.PrepareNode(NodeTracker.Node);
			UpdateInstancesGeometry(Instances, NodeTracker);

			// assign materials to static mesh for the first instance(others will use override on mesh actors)
			static_cast<FMeshNodeConverter&>(NodeTracker.GetConverter()).bMaterialsAssignedToStaticMesh = true;
			if (Mtl* Material = UpdateGeometryNodeMaterial(*this, Instances, NodeTracker))
			{
				Instances.AssignMaterialToStaticMesh(Material);
			}
			break;
		}
	}

	void UpdateNodeMetadata(FNodeTracker& NodeTracker)
	{
		TSharedPtr<IDatasmithMetaDataElement> MetadataElement = FDatasmithMaxSceneExporter::ParseUserProperties(NodeTracker.Node, NodeTracker.GetConverted().DatasmithActorElement.ToSharedRef(), ExportedScene.GetDatasmithScene().ToSharedRef());
		NodeDatasmithMetadata.Add(&NodeTracker, MetadataElement);
	}

	// Get parent node, transparently resolving XRefScene binding
	FNodeTracker* GetParentNodeTracker(FNodeTracker& NodeTracker)
	{
		INode* XRefParent = NodeTracker.GetXRefParent(); // Node may be at the root of an XRefScene, in this case get the scene node that this XRef if bound to(by XRef UI 'bind' or being a Container node)
		return GetNodeTracker(XRefParent ? XRefParent : NodeTracker.Node->GetParentNode());
	}

	// Not all nodes result in creation of DatasmithActor for them(e.g. skipped as invisible), find first ascestor that has it
	FNodeTracker* GetAncestorNodeTrackerWithDatasmithActor(FNodeTracker& InNodeTracker)
	{
		FNodeTracker* NodeTracker = &InNodeTracker;
		while (FNodeTracker* ParentNodeTracker = GetParentNodeTracker(*NodeTracker))
		{
			if (ParentNodeTracker->HasConverted())
			{
				return ParentNodeTracker;
			}
			NodeTracker = ParentNodeTracker;
		}
		return nullptr;
	}

	bool AttachNodeToDatasmithScene(FNodeTracker& NodeTracker)
	{
		if (!NodeTracker.HasConverted())
		{
			SCENE_UPDATE_STAT_INC(ReparentActors, SkippedWithoutDatasmithActor);

			return false;
		}
		SCENE_UPDATE_STAT_INC(ReparentActors, Attached);

		if (FNodeTracker* ParentNodeTracker = GetAncestorNodeTrackerWithDatasmithActor(NodeTracker))
		{
			ParentNodeTracker->GetConverted().DatasmithActorElement->AddChild(NodeTracker.GetConverted().DatasmithActorElement, EDatasmithActorAttachmentRule::KeepWorldTransform);
		}
		else
		{
			SCENE_UPDATE_STAT_INC(ReparentActors, AttachedToRoot);
			// If there's no ancestor node with DatasmithActor assume node it at root
			// (node's parent might be node that was skipped - e.g. it was hidden in Max or not selected when exporting only selected objects)
			GetDatasmithScene().AddActor(NodeTracker.GetConverted().DatasmithActorElement);
		}
		return true;
	}

	void GetNodeObjectTransform(FNodeTracker& NodeTracker, FDatasmithConverter Converter, FTransform& ObjectTransform)
	{
		FVector Translation, Scale;
		FQuat Rotation;

		const FMaxLightCoordinateConversionParams LightParams = FMaxLightCoordinateConversionParams(NodeTracker.Node);
		// todo: do we really need to call GetObjectTM if there's no WSM attached? Maybe just call GetObjTMAfterWSM always?
		Interval ValidityInterval;
		ValidityInterval.SetInfinite();
		if (NodeTracker.Node->GetWSMDerivedObject() != nullptr)
		{
			FDatasmithMaxSceneExporter::MaxToUnrealCoordinates(NodeTracker.Node->GetObjTMAfterWSM(CurrentSyncPoint.Time, &ValidityInterval), Translation, Rotation, Scale, Converter.UnitToCentimeter, LightParams);
		}
		else
		{
			FDatasmithMaxSceneExporter::MaxToUnrealCoordinates(NodeTracker.Node->GetObjectTM(CurrentSyncPoint.Time, &ValidityInterval), Translation, Rotation, Scale, Converter.UnitToCentimeter, LightParams);
		}
		LogDebug(FString::Printf(TEXT("Validity: (%ld, %ld)"), ValidityInterval.Start(), ValidityInterval.End()));
		Rotation.Normalize();
		ObjectTransform = FTransform(Rotation, Translation, Scale);

		NodeTracker.NarrowValidityToInterval(ValidityInterval);
	}

	void RegisterNodeForMaterial(FNodeTracker& NodeTracker, Mtl* Material)
	{
		FMaterialTracker* MaterialTracker = MaterialsCollectionTracker.AddMaterial(Material);
		NodeTracker.MaterialTrackers.Add(MaterialTracker);
		MaterialsAssignedToNodes.FindOrAdd(MaterialTracker).Add(&NodeTracker);
	}

	virtual void UnregisterNodeForMaterial(FNodeTracker& NodeTracker) override 
	{
		for (FMaterialTracker* MaterialTracker: NodeTracker.MaterialTrackers)
		{
			MaterialsAssignedToNodes[MaterialTracker].Remove(&NodeTracker);
			if (MaterialsAssignedToNodes[MaterialTracker].IsEmpty())
			{
				MaterialsCollectionTracker.ReleaseMaterial(*MaterialTracker);
				MaterialsAssignedToNodes.Remove(MaterialTracker);
			}
		}
		NodeTracker.MaterialTrackers.Reset();
	}

	static Mtl* UpdateGeometryNodeMaterial(FSceneTracker& SceneTracker, FInstances& Instances, FNodeTracker& NodeTracker)
	{
		if (Instances.HasMesh())
		{
			if (Mtl* Material = NodeTracker.Node->GetMtl())
			{
				bool bMaterialRegistered = false;
				for(FMaterialTracker* MaterialTracker: NodeTracker.MaterialTrackers)
				{
					bMaterialRegistered = bMaterialRegistered || (MaterialTracker->Material == Material);
				}

				if (!bMaterialRegistered) // Different material
				{
					// Release old material
					SceneTracker.UnregisterNodeForMaterial(NodeTracker);
					// Record new connection
					SceneTracker.RegisterNodeForMaterial(NodeTracker, Material);
				}
				return Material;
			}

			// Release old material when node has no material now
			SceneTracker.UnregisterNodeForMaterial(NodeTracker);
		}
		return nullptr;
	}

	virtual void AddGeometryNodeInstance(FNodeTracker& NodeTracker, FMeshNodeConverter& MeshConverter, Object* Obj) override
	{
		InvalidateInstances(InstancesManager.AddNodeTracker(NodeTracker, MeshConverter, Obj));
		
	}

	virtual void RemoveGeometryNodeInstance(FNodeTracker& NodeTracker) override
	{
		if (FInstances* Instances = InstancesManager.RemoveNodeTracker(NodeTracker))
		{
			// Invalidate instances that had a node removed
			//  - need to rebuild for various reasons(mesh might have been built from removed node, material assignment needds rebuild), remove empty
			InvalidateInstances(*Instances);
		}
	}

	virtual void ConvertGeometryNodeToDatasmith(FNodeTracker& NodeTracker, FMeshNodeConverter& MeshConverter) override
	{
		FInstances* InstancesPtr = InstancesManager.GetInstancesForNodeTracker(NodeTracker);
		if (!InstancesPtr)
		{
			return;
		}
		FInstances& Instances = *InstancesPtr;

		FDatasmithConverter Converter;

		FTransform ObjectTransform;
		GetNodeObjectTransform(NodeTracker, Converter, ObjectTransform);

		FTransform Pivot = FDatasmithMaxSceneExporter::GetPivotTransform(NodeTracker.Node, Converter.UnitToCentimeter);

		// Create separate actor only when there are multiple instances, 
		bool bNeedPivotComponent = !Pivot.Equals(FTransform::Identity) && (Instances.NodeTrackers.Num() > 1) && Instances.HasMesh();  

		TSharedPtr<IDatasmithActorElement> DatasmithActorElement;
		TSharedPtr<IDatasmithMeshActorElement> DatasmithMeshActor;

		FString UniqueName = FString::FromInt(NodeTracker.Node->GetHandle());
		FString Label = NodeTrackersNames.GetNodeName(NodeTracker);

		// Create and setup mesh actor if there's a mesh
		if (Instances.HasMesh())
		{
			FString MeshActorName = UniqueName;
			if (bNeedPivotComponent)
			{
				MeshActorName += TEXT("_Pivot");
			}

			FString MeshActorLabel = NodeTrackersNames.GetNodeName(NodeTracker);
			DatasmithMeshActor = FDatasmithSceneFactory::CreateMeshActor(*MeshActorName);
			DatasmithMeshActor->SetLabel(*Label);

			TOptional<FDatasmithMaxStaticMeshAttributes> DatasmithAttributes = FDatasmithMaxStaticMeshAttributes::ExtractStaticMeshAttributes(NodeTracker.Node);
			if (DatasmithAttributes &&  (DatasmithAttributes->GetExportMode() == EStaticMeshExportMode::BoundingBox))
			{
				DatasmithMeshActor->AddTag(TEXT("Datasmith.Attributes.Geometry: BoundingBox"));
			}

			DatasmithMeshActor->SetStaticMeshPathName(Instances.GetStaticMeshPathName());
		}

		// Create a dummy actor in case pivot is non-degenerate or there's no mesh(so no mesh actor)
		if (bNeedPivotComponent || !Instances.HasMesh())
		{
			DatasmithActorElement = FDatasmithSceneFactory::CreateActor(*UniqueName);
			DatasmithActorElement->SetLabel(*Label);
		}
		else
		{
			DatasmithActorElement = DatasmithMeshActor;
		}

		// Set transforms
		if (bNeedPivotComponent) 
		{
			// Remove pivot from the node actor transform
			FTransform NodeTransform = Pivot.Inverse() * ObjectTransform;

			DatasmithActorElement->SetTranslation(NodeTransform.GetTranslation());
			DatasmithActorElement->SetScale(NodeTransform.GetScale3D());
			DatasmithActorElement->SetRotation(NodeTransform.GetRotation());

			// Setup mesh actor with (relative)pivot transform
			DatasmithMeshActor->SetTranslation(Pivot.GetTranslation());
			DatasmithMeshActor->SetRotation(Pivot.GetRotation());
			DatasmithMeshActor->SetScale(Pivot.GetScale3D());
			DatasmithMeshActor->SetIsAComponent( true );

			DatasmithActorElement->AddChild(DatasmithMeshActor, EDatasmithActorAttachmentRule::KeepRelativeTransform);
		}
		else
		{
			FTransform NodeTransform = ObjectTransform;

			DatasmithActorElement->SetTranslation(NodeTransform.GetTranslation());
			DatasmithActorElement->SetScale(NodeTransform.GetScale3D());
			DatasmithActorElement->SetRotation(NodeTransform.GetRotation());
		}

		FNodeConverted& Converted = NodeTracker.CreateConverted();
		Converted.DatasmithActorElement = DatasmithActorElement;
		Converted.DatasmithMeshActor = DatasmithMeshActor;

		UpdateNodeMetadata(NodeTracker);
		TagsConverter.ConvertNodeTags(NodeTracker);
		if (NodeTracker.Layer)
		{
			Converted.DatasmithActorElement->SetLayer(*NodeTracker.Layer->Name);
		}

		// Apply material
		if (Mtl* Material = UpdateGeometryNodeMaterial(*this, Instances, NodeTracker))
		{
			// Assign materials
			if (!MeshConverter.bMaterialsAssignedToStaticMesh)
			{
				if (Instances.Material != Material)
				{
					TSharedRef<IDatasmithMeshActorElement> DatasmithMeshActorRef = NodeTracker.GetConverted().DatasmithMeshActor.ToSharedRef();
					FDatasmithMaxSceneExporter::ParseMaterialForMeshActor(Material, DatasmithMeshActorRef, Instances.Converted.SupportedChannels, FVector3f(NodeTracker.GetConverted().DatasmithMeshActor->GetTranslation()));
				}
			}
		}

	}

	IDatasmithScene& GetDatasmithScene()
	{
		return *ExportedScene.GetDatasmithScene();
	}

	virtual void AddMeshElement(TSharedPtr<IDatasmithMeshElement>& DatasmithMeshElement, FDatasmithMesh& DatasmithMesh, FDatasmithMesh* CollisionMesh) override
	{
		GetDatasmithScene().AddMesh(DatasmithMeshElement);

		// todo: parallelize this
		FDatasmithMeshExporter DatasmithMeshExporter;
		if (DatasmithMeshExporter.ExportToUObject(DatasmithMeshElement, ExportedScene.GetSceneExporter().GetAssetsOutputPath(), DatasmithMesh, CollisionMesh, FDatasmithExportOptions::LightmapUV))
		{
			// todo: handle error exporting mesh?
		}
	}

	void UpdateInstancesGeometry(FInstances& Instances, FNodeTracker& NodeTracker)
	{
		INode* Node = NodeTracker.Node;
		Object* Obj = Instances.EvaluatedObj;

		FString MeshName = FString::FromInt(Node->GetHandle());

		FMeshConverterSource MeshSource = {
			Node, MeshName,
			GeomUtils::GetMeshForGeomObject(CurrentSyncPoint.Time, Node, Obj), false,
			GeomUtils::GetMeshForCollision(CurrentSyncPoint.Time, *this, Node),
		};

		if (MeshSource.RenderMesh.GetMesh())
		{
			bool bHasInstanceWithMultimat = false;
			for (FNodeTracker* InstanceNodeTracker : Instances.NodeTrackers)
			{
				if (Mtl* Material = InstanceNodeTracker->Node->GetMtl())
				{
					if (FDatasmithMaxMatHelper::GetMaterialClass(Material) == EDSMaterialType::MultiMat)
					{
						bHasInstanceWithMultimat = true;
					}
				}
			}

			MeshSource.bConsolidateMaterialIds = !bHasInstanceWithMultimat;

			FMeshes::AddMesh(*this, MeshSource, Instances.Converted, [&](bool bHasConverted, FMeshConverted& MeshConverted)
			{
				if (bHasConverted)
				{
					MeshConverted.DatasmithMeshElement->SetLabel(*NodeTrackersNames.GetNodeName(NodeTracker));
				}
				else
				{
					MeshConverted.DatasmithMeshElement.Reset();
				}
				Instances.Converted = MeshConverted;
			});
		}
		else
		{
			// Whe RenderMesh can be null?
			// seems like the only way is to static_cast<GeomObject*>(Obj) return null in GetMeshFromRenderMesh?
			// Where Obj is return value of GetBaseObject(Node, CurrentTime)
			// Or ObjectState.obj is null
			ensure(false);
			ReleaseMeshElement(Instances.Converted);
		}
	}

	virtual void SetupActor(FNodeTracker& NodeTracker) override
	{
		NodeTracker.GetConverted().DatasmithActorElement->SetLabel(*NodeTrackersNames.GetNodeName(NodeTracker));

		UpdateNodeMetadata(NodeTracker);
		TagsConverter.ConvertNodeTags(NodeTracker);
		if (NodeTracker.Layer)
		{
			NodeTracker.GetConverted().DatasmithActorElement->SetLayer(*NodeTracker.Layer->Name);
		}

		FDatasmithConverter Converter;
		FTransform ObjectTransform;
		GetNodeObjectTransform(NodeTracker, Converter, ObjectTransform);

		FTransform NodeTransform = ObjectTransform;
		TSharedRef<IDatasmithActorElement> DatasmithActorElement = NodeTracker.GetConverted().DatasmithActorElement.ToSharedRef();
		DatasmithActorElement->SetTranslation(NodeTransform.GetTranslation());
		DatasmithActorElement->SetScale(NodeTransform.GetScale3D());
		DatasmithActorElement->SetRotation(NodeTransform.GetRotation());
	}

	class FMeshes
	{
	public:
		static void AddMesh(FSceneTracker& Scene, FMeshConverterSource& MeshSource, FMeshConverted& MeshConverted, TFunction<void(bool, FMeshConverted&)> CompletionCallback)
		{
			// Reset old mesh
			// todo: potential mesh reuse - when DatasmithMeshElement allows to reset materials(as well as other params)
			Scene.ReleaseMeshElement(MeshConverted);

			bool bConverted = ConvertMaxMeshToDatasmith(Scene.CurrentSyncPoint.Time, Scene, MeshSource, MeshConverted);
			CompletionCallback(bConverted, MeshConverted);
		}
	};

	virtual void SetupDatasmithHISMForNode(FNodeTracker& NodeTracker, FMeshConverterSource& MeshSource, Mtl* Material, int32 MeshIndex, const TArray<Matrix3>& Transforms) override
	{
		FString MeshName = FString::FromInt(NodeTracker.Node->GetHandle()) + TEXT("_") + FString::FromInt(MeshIndex);

		// note: when export Mesh goes to other place due to parallellizing it's result would be unknown here so MeshIndex handling will change(i.e. increment for any mesh)

		MeshSource.MeshName = MeshName; // todo: !!!

		FMeshConverted MeshConvertedDummy; // todo: possible reuse of previous converted mesh
		FMeshes::AddMesh(*this, MeshSource, MeshConvertedDummy, [&](bool bHasConverted, FMeshConverted& MeshConverted)
		{
			if (bHasConverted)
			{
				FHismNodeConverter& NodeConverter = static_cast<FHismNodeConverter&>(NodeTracker.GetConverter());

				NodeConverter.Meshes.Add(MeshConverted);

				RegisterNodeForMaterial(NodeTracker, Material);
				AssignMeshMaterials(MeshConverted.DatasmithMeshElement, Material, MeshConverted.SupportedChannels);

				FString MeshLabel = NodeTrackersNames.GetNodeName(NodeTracker) + (TEXT("_") + FString::FromInt(MeshIndex));
				MeshConverted.DatasmithMeshElement->SetLabel(*MeshLabel);

				FDatasmithConverter Converter;

				// todo: override material
				TSharedPtr< IDatasmithActorElement > InversedHISMActor;
				// todo: ExportHierarchicalInstanceStaticMeshActor CustomMeshNode only used for Material - can be simplified, Material anyway is dealt with outside too
				TSharedRef<IDatasmithActorElement> HismActorElement = FDatasmithMaxSceneExporter::ExportHierarchicalInstanceStaticMeshActor(NodeTracker.Node, MeshSource.Node, *MeshLabel, MeshConverted.SupportedChannels,
					Material, &Transforms, *MeshName, Converter.UnitToCentimeter, EStaticMeshExportMode::Default, InversedHISMActor);
				NodeTracker.GetConverted().DatasmithActorElement->AddChild(HismActorElement, EDatasmithActorAttachmentRule::KeepWorldTransform);
				if (InversedHISMActor)
				{
					NodeTracker.GetConverted().DatasmithActorElement->AddChild(InversedHISMActor, EDatasmithActorAttachmentRule::KeepWorldTransform);
				}
				MeshIndex++;
			}
		});
	}

	void ConvertNamedCollisionNode(FNodeTracker& NodeTracker)
	{
		// Split collision prefix and find node that might use this node as collision mesh
		FString NodeName = NodeTrackersNames.GetNodeName(NodeTracker);
		FString LeftString, RightString;
		NodeName.Split(TEXT("_"), &LeftString, &RightString);

		FNodeTracker* CollisionUserNodeTracker = GetNodeTrackerByNodeName(*RightString);
		
		if (!CollisionUserNodeTracker)
		{
			return;
		}

		if (CollisionUserNodeTracker->GetConverterType() == FNodeConverter::MeshNode)
		{
			if (FInstances* Instances = InstancesManager.GetInstancesForNodeTracker(*CollisionUserNodeTracker))
			{
				InvalidateInstances(*Instances);
			}
		}
	}

	/******************* Events *****************************/

	virtual void NodeAdded(INode* Node) override
	{
		LogDebugNode(TEXT("NodeAdded"), Node);
		// Node sometimes is null. 'Added' NodeEvent might come after node was actually deleted(immediately after creation)
		// e.g.[mxs]: b = box(); delete b
		// NodeEvents are delayed(not executed in the same stack frame as command that causes them) so they come later.
		if (!Node)
		{
			return;
		}

		if (NotificationsHandler)
		{
			NotificationsHandler->AddNode(Node);
		}

		ParseNode(Node);
	}

	virtual void NodeXRefMerged(INode* Node) override
	{
		if (!Node)
		{
			return;
		}

		// Search where this XRef Tree is attached to the scene
		int32 XRefIndex = -1; // Node that has this xref scene attached to(e.g. to place in hierarchy and to transform)
		INode* SceneRootNode = GetCOREInterface()->GetRootNode();
		for (int XRefChild = 0; XRefChild < SceneRootNode->GetXRefFileCount(); ++XRefChild)
		{
			if (Node == SceneRootNode->GetXRefTree(XRefChild))
			{
				XRefIndex = XRefChild;
			}
		}

		NotificationsHandler->AddNode(Node);

		FNodeKey NodeKey = NodeEventNamespace::GetKeyByNode(Node);
		InvalidateNode(NodeKey);

		if (!bIncludeXRefWhileParsing)
		{
			ParseScene(Node, FXRefScene{SceneRootNode, XRefIndex}); // Parse xref hierarchy - it won't add itself! Or will it?
		}
	}

	virtual void NodeDeleted(INode* Node) override
	{
		LogDebugNode(TEXT("NodeDeleted"), Node);

		if (FNodeTracker* NodeTracker = GetNodeTracker(Node))
		{
			InvalidatedNodeTrackers.Add(*NodeTracker);
			NodeTracker->bDeleted = true;
		}
	}

	virtual void NodeTransformChanged(FNodeKey NodeKey) override
	{
		// todo: invalidate transform only

		// todo: grouping makes this crash. Need to handle event before?
		InvalidateNode(NodeKey);
	}

	virtual void NodeMaterialAssignmentChanged(FNodeKey NodeKey) override
	{
		//todo: handle more precisely
		InvalidateNode(NodeKey);
	}

	virtual void NodeMaterialGraphModified(FNodeKey NodeKey) override
	{
		//identify material tree and update all materials
		//todo: possible to handle this more precisely(only refresh changed materials) - see FMaterialObserver

		if (FNodeTracker* NodeTracker = GetNodeTracker(NodeKey))
		{
			// todo: investigate why NodeEventNamespace::GetNodeByKey may stil return NULL
			// testcase - add XRef Material - this will immediately have this
			// even though NOTIFY_SCENE_ADDED_NODE was called for node and NOTIFY_SCENE_PRE_DELETED_NODE wasn't!
			INode* Node = NodeEventNamespace::GetNodeByKey(NodeKey);
			if (Node)
			{
				if (Mtl* Material = Node->GetMtl())
				{
					MaterialsCollectionTracker.InvalidateMaterial(Material);
				}
			}
		}

		InvalidateNode(NodeKey); // Invalidate node that has this material assigned. This is needed to trigger rebuild - exported geometry might change(e.g. multimaterial changed to slots will change on static mesh)
	}

	virtual void NodeGeometryChanged(FNodeKey NodeKey) override
	{
		// GeometryChanged is executed to handle:
		// - actual geometry modification(in any way)
		// - change of baseObject

		InvalidateNode(NodeKey);
	}

	virtual void NodeHideChanged(FNodeKey NodeKey) override
	{
		// todo: invalidate visibility only - note to handle this not enought add/remove
		// actor. make sure to invalidate instances(in case geometry usage changed - like hidden node with multimat), materials

		InvalidateNode(NodeKey);
	}

	virtual void NodeNameChanged(FNodeKey NodeKey) override
	{
		if (FNodeTracker* NodeTracker =  GetNodeTracker(NodeKey))
		{
			NodeTrackersNames.Update(*NodeTracker);
			InvalidateNode(*NodeTracker);
		}
	}

	virtual void NodePropertiesChanged(FNodeKey NodeKey) override
	{
		// todo: invalidate visibility only - note to handle this not enought add/remove
		// actor. make sure to invalidate instances(in case geometry usage changed - like hidden node with multimat), materials

		InvalidateNode(NodeKey);
	}

	virtual void NodeLinkChanged(FNodeKey NodeKey) override
	{
		InvalidateNode(NodeKey);
	}

	virtual FSceneUpdateStats& GetStats() override
	{
		return Stats;
	}

	virtual FNodeTracker* GetNodeTrackerByNodeName(const TCHAR* Name) override
	{
		FNodeTracker* Result = nullptr;
		NodeTrackersNames.EnumerateForName(Name, [&](FNodeTracker& NodeTracker)
		{
			// todo: currently support only one name/node(as Max api GetINodeByName)
			//   so collision with prefix only takes one node. But this can change.
			Result = &NodeTracker;
		});

		return Result;
	}
	
	void Reset()
	{
		bSceneParsed = false;

		NodeTrackers.Reset();
		NodeTrackersNames.Reset();
		InstancesManager.Reset();
		CollisionNodes.Reset();

		LayersForAnimHandle.Reset();
		NodesPerLayer.Reset();

		MaterialsCollectionTracker.Reset();
		MaterialsAssignedToNodes.Reset();

		NodeDatasmithMetadata.Reset();

		InvalidatedNodeTrackers.Reset();
		InvalidatedInstances.Reset();
	}

	///////////////////////////////////////////////

	const FExportOptions& Options;
	FDatasmith3dsMaxScene& ExportedScene;

	FNotifications* NotificationsHandler;

	bool bUpdateInProgress = false;

	// Scene tracked/converted entities and connections beetween them
	bool bSceneParsed = false;
	TMap<FNodeKey, FNodeTrackerHandle> NodeTrackers; // All scene nodes
	FNodeTrackersNames NodeTrackersNames; // Nodes grouped by name, for faster access
	FInstancesManager InstancesManager; // Groups geometry nodes by their shared mesh
	TMap<FNodeTracker*, TSet<FNodeTracker*>> CollisionNodes; // Nodes used as collision meshes for other nodes, counted by each user

	TMap<AnimHandle, TUniquePtr<FLayerTracker>> LayersForAnimHandle;
	TMap<FLayerTracker*, TSet<FNodeTracker*>> NodesPerLayer;

	FMaterialsCollectionTracker MaterialsCollectionTracker;
	TMap<FMaterialTracker*, TSet<FNodeTracker*>> MaterialsAssignedToNodes;

	TMap<FNodeTracker*, TSharedPtr<IDatasmithMetaDataElement>> NodeDatasmithMetadata; // All scene nodes

	// Nodes/instances that need to be rebuilt
	FInvalidatedNodeTrackers InvalidatedNodeTrackers;
	TSet<FInstances*> InvalidatedInstances;

	// Utility
	FSceneUpdateStats Stats;
	FTagsConverter TagsConverter; // Converts max node information to Datasmith tags
	FNodesPreparer NodesPreparer;

};

class FExporter: public IExporter
{
public:
	FExporter(FExportOptions& InOptions): Options(InOptions), NotificationsHandler(*this), SceneTracker(Options, ExportedScene, &NotificationsHandler)
	{
		ResetSceneTracking();
		InitializeDirectLinkForScene(); // Setup DL connection immediately when plugin loaded
	}

	virtual void Shutdown() override;

	virtual void SetOutputPath(const TCHAR* Path) override
	{
		OutputPath = Path;
		ExportedScene.SetOutputPath(*OutputPath);
	}

	virtual void SetName(const TCHAR* Name) override
	{
		ExportedScene.SetName(Name);
	}

	virtual void InitializeScene() override
	{
		ExportedScene.SetupScene();
	}

	virtual void ParseScene() override
	{
		SceneTracker.ParseScene();
	}

	virtual void InitializeDirectLinkForScene() override
	{
		if (DirectLinkImpl)
		{
			return;
		}

		InitializeScene();

		// XXX: PreExport needs to be called before DirectLink instance is constructed -
		// Reason - it calls initialization of FTaskGraphInterface. Callstack:
		// PreExport:
		//  - FDatasmithExporterManager::Initialize
		//	-- DatasmithGameThread::InitializeInCurrentThread
		//  --- GEngineLoop.PreInit
		//  ---- PreInitPreStartupScreen
		//  ----- FTaskGraphInterface::Startup
		ExportedScene.PreExport();

		SetOutputPath(GetDirectlinkCacheDirectory());
		FString SceneName = FPaths::GetCleanFilename(GetCOREInterface()->GetCurFileName().data());
		SetName(*SceneName);

		DirectLinkImpl.Reset(new FDatasmithDirectLink);
		DirectLinkImpl->InitializeForScene(ExportedScene.GetDatasmithScene().ToSharedRef());
	}

	virtual void UpdateDirectLinkScene() override
	{
		if (!DirectLinkImpl)
		{
			// InitializeDirectLinkForScene wasn't called yet. This rarely happens when Sync is pressed right before event like PostSceneReset(for New All UI command) was handled
			// Very quickly! Unfortunately code needs to wait for PostSceneReset to get proper scene name there(no earlier event signals that name is available)
			InitializeDirectLinkForScene();
		}

		LogDebug(TEXT("UpdateDirectLinkScene"));
		DirectLinkImpl->UpdateScene(ExportedScene.GetDatasmithScene().ToSharedRef());
		StartSceneChangeTracking(); // Always track scene changes if it's synced with DirectLink
	}

	static VOID AutoSyncTimerProc(HWND, UINT, UINT_PTR TimerIdentifier, DWORD)
	{
		reinterpret_cast<FExporter*>(TimerIdentifier)->UpdateAutoSync();
	}

	// Update is user was idle for some time
	void UpdateAutoSync()
	{
		LASTINPUTINFO LastInputInfo;
		LastInputInfo.cbSize = sizeof(LASTINPUTINFO);
		LastInputInfo.dwTime = 0;
		if (GetLastInputInfo(&LastInputInfo))
		{
			DWORD CurrentTime = GetTickCount();
			int32 IdlePeriod = GetTickCount() - LastInputInfo.dwTime;
			LogDebug(FString::Printf(TEXT("CurrentTime: %ld, Idle time: %ld, IdlePeriod: %ld"), CurrentTime, LastInputInfo.dwTime, IdlePeriod));

			if (IdlePeriod > FMath::RoundToInt(AutoSyncIdleDelaySeconds*1000))
			{
				PerformAutoSync();
			}
		}
	}

	virtual bool IsAutoSyncEnabled() override
	{
		return bAutoSyncEnabled;
	}

	virtual bool ToggleAutoSync() override
	{
		if (bAutoSyncEnabled)
		{
			KillTimer(GetCOREInterface()->GetMAXHWnd(), reinterpret_cast<UINT_PTR>(this));
		}
		else
		{
			// Perform full Sync when AutoSync is first enabled
			PerformSync(false);

			const uint32 AutoSyncCheckIntervalMs = FMath::RoundToInt(AutoSyncDelaySeconds*1000);
			SetTimer(GetCOREInterface()->GetMAXHWnd(), reinterpret_cast<UINT_PTR>(this), AutoSyncCheckIntervalMs, AutoSyncTimerProc);
		}
		bAutoSyncEnabled = !bAutoSyncEnabled;

		LogDebug(bAutoSyncEnabled ? TEXT("AutoSync ON") : TEXT("AutoSync OFF"));
		return bAutoSyncEnabled;
	}

	virtual void SetAutoSyncDelay(float Seconds) override
	{
		AutoSyncDelaySeconds = Seconds;
	}

	virtual void SetAutoSyncIdleDelay(float Seconds) override
	{
		AutoSyncIdleDelaySeconds = Seconds;
	}

	// Install change notification systems
	virtual void StartSceneChangeTracking() override
	{
		NotificationsHandler.StartSceneChangeTracking();
	}

	virtual bool UpdateScene(bool bQuiet) override
	{
		FUpdateProgress ProgressManager(!bQuiet, 1);

		bool bResult = SceneTracker.Update(ProgressManager.MainStage, false);

		ProgressManager.Finished();

		if (Options.bStatSync)
		{
			ProgressManager.PrintStatisticss();
		}
		return bResult;
	}

	virtual void PerformSync(bool bQuiet) override
	{
		FUpdateProgress ProgressManager(!bQuiet, 1);
		FUpdateProgress::FStage& MainStage = ProgressManager.MainStage;

		SceneTracker.Update(MainStage, false);
		{
			PROGRESS_STAGE("Sync With DirectLink")
			UpdateDirectLinkScene();
		}

		ProgressManager.Finished();

		if (Options.bStatSync)
		{
			LogInfo("Sync completed:");
			ProgressManager.PrintStatisticss();
		}
	}

	virtual void PerformAutoSync()
	{
		// Don't create progress bar for autosync - it steals focus, closes listener and what else
		// todo: consider creating progress when a big change in scene is detected, e.g. number of nodes?
		bool bQuiet = true;

		FUpdateProgress ProgressManager(!bQuiet, 1);
		FUpdateProgress::FStage& MainStage = ProgressManager.MainStage;

		if (SceneTracker.Update(MainStage, false))  // Don't sent redundant update if scene change wasn't detected
		{
			PROGRESS_STAGE("Sync With DirectLink")
			UpdateDirectLinkScene();
		}

		ProgressManager.Finished();

		if (Options.bStatSync)
		{
			LogInfo("AutoSync completed:");
			ProgressManager.PrintStatisticss();
		}
	}

	virtual void ResetSceneTracking() override
	{
		NotificationsHandler.StopSceneChangeTracking();
		if (IsAutoSyncEnabled())
		{
			ToggleAutoSync();
		}

		ExportedScene.ResetScene();

		SceneTracker.Reset();

		DirectLinkImpl.Reset();
	}

	virtual ISceneTracker& GetSceneTracker() override
	{
		return SceneTracker;
	}

	FExportOptions& Options;

	FDatasmith3dsMaxScene ExportedScene;
	TUniquePtr<FDatasmithDirectLink> DirectLinkImpl;
	FString OutputPath;

	FNotifications NotificationsHandler;
	FSceneTracker SceneTracker;

	bool bAutoSyncEnabled = false;
	float AutoSyncDelaySeconds = 0.5f; // AutoSync is attempted periodically using this interval
	float AutoSyncIdleDelaySeconds = 0.5f; // Time period user should be idle to run AutoSync

};

FPersistentExportOptions PersistentExportOptions;

TUniquePtr<IExporter> Exporter;

bool CreateExporter(bool bEnableUI, const TCHAR* EnginePath)
{
	FDatasmithExporterManager::FInitOptions Options;
	Options.bEnableMessaging = true; // DirectLink requires the Messaging service.
	Options.bSuppressLogs = false;   // Log are useful, don't suppress them
	Options.bUseDatasmithExporterUI = bEnableUI;
	Options.RemoteEngineDirPath = EnginePath;

	if (!FDatasmithExporterManager::Initialize(Options))
	{
		return false;
	}

	if (int32 ErrorCode = FDatasmithDirectLink::ValidateCommunicationSetup())
	{
		return false;
	}

	PersistentExportOptions.Load(); // Access GConfig only after FDatasmithExporterManager::Initialize finishes, which ensures that Unreal game thread was initialized(GConfig is created there)
	Exporter = MakeUnique<FExporter>(PersistentExportOptions.Options);
	return true;
}

void ShutdownExporter()
{
	ShutdownScripts();
	Exporter.Reset();
	FDatasmithDirectLink::Shutdown();
	FDatasmithExporterManager::Shutdown();
}

IExporter* GetExporter()
{
	return Exporter.Get();
}

IPersistentExportOptions& GetPersistentExportOptions()
{
	return PersistentExportOptions;
}

bool FInvalidatedNodeTrackers::PurgeDeletedNodeTrackers(FSceneTracker& Scene)
{
	TArray<FNodeTracker*> DeletedNodeTrackers;
	for (FNodeTracker* NodeTracker : InvalidatedNodeTrackers)
	{
		if (NodeTracker->bDeleted)
		{
			DeletedNodeTrackers.Add(NodeTracker);
		}
	}

	for (FNodeTracker* NodeTrackerPtr : DeletedNodeTrackers)
	{
		Scene.RemoveNodeTracker(*NodeTrackerPtr);
	}

	return !DeletedNodeTrackers.IsEmpty(); // If the only change is deleted node than we need to record it(deleted will be removed from InvalidatedNodeTrackers)
}

void FExporter::Shutdown()
{
	Exporter.Reset();
	FDatasmithDirectLink::Shutdown();
	FDatasmithExporterManager::Shutdown();
}

bool Export(const TCHAR* Name, const TCHAR* OutputPath, bool bQuiet)
{
	FUpdateProgress ProgressManager(!bQuiet, 3);
	FUpdateProgress::FStage& MainStage = ProgressManager.MainStage;

	FDatasmith3dsMaxScene ExportedScene;
	ExportedScene.SetupScene();
	ExportedScene.SetName(Name);
	ExportedScene.SetOutputPath(OutputPath);

	FSceneTracker SceneTracker(PersistentExportOptions.Options, ExportedScene, nullptr);

	SceneTracker.Update(MainStage, true);

	if (PersistentExportOptions.Options.bAnimatedTransforms)
	{
		PROGRESS_STAGE("Export Animations");
		SceneTracker.ExportAnimations();
	}

	{
		PROGRESS_STAGE("Save Datasmith Scene");

		IDatasmithScene& Scene = *ExportedScene.GetDatasmithScene();
		ExportedScene.GetSceneExporter().Export(ExportedScene.GetDatasmithScene().ToSharedRef(), false);

		PROGRESS_STAGE_RESULT(FString::Printf(TEXT("Actors: %d; Meshes: %d, Materials: %d"), 
			Scene.GetActorsCount(),
			Scene.GetMeshesCount(),
			Scene.GetMaterialsCount(),
			Scene.GetTexturesCount()
			));
	}

	ProgressManager.Finished();

	LogInfo(TEXT("Export completed:"));
	ProgressManager.PrintStatisticss();

	return true;
}

bool OpenDirectLinkUI()
{
	if (IDatasmithExporterUIModule* Module = IDatasmithExporterUIModule::Get())
	{
		if (IDirectLinkUI* UI = Module->GetDirectLinkExporterUI())
		{
			UI->OpenDirectLinkStreamWindow();
			return true;
		}
	}
	return false;
}

const TCHAR* GetDirectlinkCacheDirectory()
{
	if (IDatasmithExporterUIModule* Module = IDatasmithExporterUIModule::Get())
	{
		if (IDirectLinkUI* UI = Module->GetDirectLinkExporterUI())
		{
			return UI->GetDirectLinkCacheDirectory();
		}
	}
	return nullptr;
}

FDatasmithConverter::FDatasmithConverter(): UnitToCentimeter(FMath::Abs(GetSystemUnitScale(UNITS_CENTIMETERS)))
{
}

}

#include "Windows/HideWindowsPlatformTypes.h"

#endif // NEW_DIRECTLINK_PLUGIN
