// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterLaunchEditorModule.h"

#include "DisplayClusterLaunchEditorLog.h"
#include "DisplayClusterLaunchEditorProjectSettings.h"
#include "DisplayClusterLaunchEditorStyle.h"

#include "ConcertSettings.h"
#include "IMultiUserClientModule.h"

#include "DisplayClusterRootActor.h"
#include "IDisplayClusterConfiguration.h"
#include "DisplayClusterConfigurationTypes.h"

#include "Shared/UdpMessagingSettings.h"

#include "Algo/RemoveIf.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "Engine/GameEngine.h"
#include "ISettingsModule.h"
#include "LevelEditor.h"
#include "Misc/ConfigCacheIni.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "FDisplayClusterLaunchEditorModule"

void CloseAllMenus()
{
	FSlateApplication::Get().DismissAllMenus();
}

FString EnumToString(const TCHAR* EnumName, const int32 EnumValue)
{
	const UEnum* EnumPtr = FindObject<UEnum>(ANY_PACKAGE, EnumName, true);
	
	if (!EnumPtr)
	{
		return LOCTEXT("EnumNotFound", "Enum not found").ToString();
	}

	return EnumPtr->GetNameStringByIndex(EnumValue);
}

static UWorld* GetCurrentWorld()
{
	UWorld* CurrentWorld = nullptr;
	if (GIsEditor)
	{
		CurrentWorld = GEditor->GetEditorWorldContext().World();
	}
	else if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
	{
		CurrentWorld = GameEngine->GetGameWorld();
	}
	return CurrentWorld;
}

FDisplayClusterLaunchEditorModule& FDisplayClusterLaunchEditorModule::Get()
{
	return FModuleManager::GetModuleChecked<FDisplayClusterLaunchEditorModule>("DisplayClusterLaunchEditor");
}

void FDisplayClusterLaunchEditorModule::StartupModule()
{
	FDisplayClusterLaunchEditorStyle::Initialize();

	FCoreDelegates::OnFEngineLoopInitComplete.AddRaw(this, &FDisplayClusterLaunchEditorModule::OnFEngineLoopInitComplete);
}

void FDisplayClusterLaunchEditorModule::ShutdownModule()
{
	UToolMenus::UnregisterOwner(this);

	FCoreDelegates::OnFEngineLoopInitComplete.RemoveAll(this);

	FDisplayClusterLaunchEditorStyle::Shutdown();

	// Unregister project settings
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		SettingsModule.UnregisterSettings("Project", "Plugins", "nDisplay Launch");
	}
}

void FDisplayClusterLaunchEditorModule::OpenProjectSettings()
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings")
		.ShowViewer("Project", "Plugins", "nDisplay Launch");

}

void GetProjectSettingsArguments(const UDisplayClusterLaunchEditorProjectSettings* ProjectSettings, FString& ConcatenatedCommandLineArguments, FString& ConcatenatedConsoleCommands, FString& ConcatenatedDPCvars, FString& ConcatenatedLogCommands)
{
	{
		for (const FString& CommandLineArgument : ProjectSettings->CommandLineArguments)
		{
			if (CommandLineArgument.IsEmpty())
			{
				continue;
			}
			ConcatenatedCommandLineArguments += FString::Printf(TEXT("-%s "), *CommandLineArgument);
		}
		// Remove whitespace
		ConcatenatedCommandLineArguments.TrimStartAndEndInline();
	}

	if (ProjectSettings->ConsoleCommands.Num() > 0)
	{
		ConcatenatedConsoleCommands += FString::Join(ProjectSettings->ConsoleCommands, TEXT(","));
	}

	if (ProjectSettings->DPCvars.Num() > 0)
	{
		ConcatenatedDPCvars += FString::Join(ProjectSettings->DPCvars, TEXT(","));
	}

	{
		for (const FDisplayClusterLaunchLoggingConstruct& LoggingConstruct : ProjectSettings->Logging)
		{
			if (LoggingConstruct.Category.IsNone())
			{
				continue;
			}
			ConcatenatedLogCommands += FString::Printf(TEXT("%s %s, "),
			                                           *LoggingConstruct.Category.ToString(),
			                                           *EnumToString(TEXT("EDisplayClusterLaunchLogVerbosity"), (int32)LoggingConstruct.VerbosityLevel.GetValue()));
		}
		// Remove whitespace
		ConcatenatedLogCommands.TrimStartAndEndInline();
		// Remove last comma
		ConcatenatedLogCommands = ConcatenatedLogCommands.LeftChop(1);
	}
}

bool AddUdpMessagingArguments(FString& ConcatenatedArguments)
{
	// Get from reflection because UUdpMessagingSettings does not export its API
	FConfigFile* EngineConfig = GConfig ? GConfig->FindConfigFileWithBaseName(FName(TEXT("Engine"))) : nullptr;
	if (EngineConfig)
	{
		TArray<FString> Settings;
		FString Setting;

		// Unicast endpoint setting
		EngineConfig->GetString(TEXT("/Script/UdpMessaging.UdpMessagingSettings"), TEXT("UnicastEndpoint"), Setting);

		// if the unicast endpoint port is bound, concatenate it
		if (Setting != "0.0.0.0:0" && !Setting.IsEmpty())
		{
			ConcatenatedArguments += TEXT(" -UDPMESSAGING_TRANSPORT_UNICAST=") + Setting;
		}

		// Multicast endpoint setting
		EngineConfig->GetString(TEXT("/Script/UdpMessaging.UdpMessagingSettings"), TEXT("MulticastEndpoint"), Setting);
		ConcatenatedArguments += TEXT(" -UDPMESSAGING_TRANSPORT_MULTICAST=") + Setting;

		// Static endpoints setting
		Settings.Empty(1);
		EngineConfig->GetArray(TEXT("/Script/UdpMessaging.UdpMessagingSettings"), TEXT("StaticEndpoints"), Settings);
		if (Settings.Num() > 0)
		{
			ConcatenatedArguments += TEXT(" -UDPMESSAGING_TRANSPORT_STATIC=");
			ConcatenatedArguments += Settings[0];
			for (int32 i = 1; i < Settings.Num(); ++i)
			{
				ConcatenatedArguments += ',';
				ConcatenatedArguments += Settings[i];
			}
		}
		return true;
	}
	return false;
}

FString AppendRandomNumbersToString(const FString InString, uint8 NumberToAppend = 6)
{
	FString RandomizedString = "_";

	for (uint8 RandomIteration = 0; RandomIteration < NumberToAppend; RandomIteration++)
	{
		RandomizedString += FString::FromInt(FMath::RandRange(0, 9));
	}
	return InString + RandomizedString;
}

FString GetConcertServerName()
{
	const UDisplayClusterLaunchEditorProjectSettings* Settings = GetDefault<UDisplayClusterLaunchEditorProjectSettings>();
	return Settings->bAutoGenerateServerName ? AppendRandomNumbersToString("nDisplayLaunchServer") : Settings->ExplicitServerName;
}

FString GetConcertSessionName()
{
	const UDisplayClusterLaunchEditorProjectSettings* Settings = GetDefault<UDisplayClusterLaunchEditorProjectSettings>();
	return Settings->bAutoGenerateSessionName ? AppendRandomNumbersToString("nDisplayLaunchSession") : Settings->ExplicitSessionName;
}

FString GetConcertArguments(const FString& ServerName, const FString& SessionName)
{
	const UConcertClientConfig* ConcertClientConfig = GetDefault<UConcertClientConfig>();
	ensureAlwaysMsgf (ConcertClientConfig, TEXT("%hs: Unable to launch nDisplay because there is no UConcertClientConfig object."));

	FString ReturnValue =
		FString::Printf(TEXT("-CONCERTISHEADLESS -CONCERTRETRYAUTOCONNECTONERROR -CONCERTAUTOCONNECT -CONCERTSERVER=\"%s\" -CONCERTSESSION=\"%s\""),
			 *ServerName, *SessionName);

	return ReturnValue;
}

void LaunchConcertServerIfNotRunning(const FString& ServerName, const FString& SessionName)
{
	IMultiUserClientModule& MultiUserClientModule = IMultiUserClientModule::Get();
	if (!MultiUserClientModule.IsConcertServerRunning())
	{
		FServerLaunchOverrides Overrides;
		Overrides.ServerName = ServerName;
		
		MultiUserClientModule.LaunchConcertServer(Overrides);
	}
}

void FDisplayClusterLaunchEditorModule::LaunchDisplayClusterProcess()
{
	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> ConfigsInWorld = GetAllDisplayClusterConfigsInWorld();
	if (!DoesCurrentWorldHaveDisplayClusterConfig())
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because there are no valid nDisplay configurations in the world."), __FUNCTION__);
		return;
	}

	if (!SelectedDisplayClusterConfigActor.IsValid())
	{
		for (const TWeakObjectPtr<ADisplayClusterRootActor>& Config : ConfigsInWorld)
		{
			if (ADisplayClusterRootActor* ConfigPtr = Config.Get())
			{
				TArray<FString> NodeNames;
				ConfigPtr->GetConfigData()->Cluster->Nodes.GenerateKeyArray(NodeNames);

				if (NodeNames.Num() > 0)
				{
					SetSelectedDisplayClusterConfigActor(ConfigPtr);
					break;
				}
			}
		}
	}

	UDisplayClusterConfigurationData* ConfigDataToUse = nullptr;
	FString ConfigActorPath;
	
	// If it's valid we need to check the selected nodes against the current config. If they don't exist, we need to get the first one.
	if (const ADisplayClusterRootActor* ConfigActor = Cast<ADisplayClusterRootActor>(SelectedDisplayClusterConfigActor.ResolveObject()))
	{
		// Duplicate existing config data so we can make non-destructive edits
		ConfigDataToUse = DuplicateObject(ConfigActor->GetConfigData(), GetTransientPackage());

		ApplyDisplayClusterConfigOverrides(ConfigDataToUse);

		const FString FilePath = FPaths::Combine(FPaths::ProjectSavedDir(), "Temp.ndisplay");
		if (!ensureAlways(IDisplayClusterConfiguration::Get().SaveConfig(ConfigDataToUse, FilePath)))
		{
			UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because the selected nDisplay Configuration could not be saved to a .ndisplay file. See the log for more information."), __FUNCTION__);
			return;
		}
		
		ConfigActorPath = FString::Printf(TEXT("-dc_cfg=\"%s\""), *FilePath);
	}
	else
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because the selected nDisplay Config Actor could not be resolved or does not exist in the current level."), __FUNCTION__);
		return;
	}
	
	const UDisplayClusterLaunchEditorProjectSettings* ProjectSettings = GetDefault<UDisplayClusterLaunchEditorProjectSettings>();
	if (!ensureAlwaysMsgf (ProjectSettings, TEXT("%hs: Unable to launch nDisplay because there is no UDisplayClusterLaunchEditorProjectSettings object.")))
	{
		return;
	}
	
	const FString EditorBinary = FPlatformProcess::ExecutablePath();
	
	const FString Project = FPaths::SetExtension(FPaths::Combine(FPaths::ProjectDir(), FApp::GetProjectName()),".uproject");
	const FString Map = GetCurrentWorld()->GetCurrentLevel()->GetPackage()->GetFName().ToString();

	// Create Multi-user params
	FString ConcertArguments;
	if (GetConnectToMultiUser())
	{
		const FString ServerName = GetConcertServerName();
		const FString SessionName = GetConcertSessionName();
		
		ConcertArguments = GetConcertArguments(ServerName, SessionName);
		LaunchConcertServerIfNotRunning(ServerName, SessionName);
	}

	for (const FString& Node : SelectedDisplayClusterConfigActorNodes)
	{
		FString ConcatenatedCommandLineArguments;
		FString ConcatenatedConsoleCommands;
		FString ConcatenatedDPCvars;
		FString ConcatenatedLogCommands;

		// Fullscreen/Windowed
		if (UDisplayClusterConfigurationClusterNode** NodePtrPtr = ConfigDataToUse->Cluster->Nodes.Find(Node))
		{
			if (const UDisplayClusterConfigurationClusterNode* NodePtr = *NodePtrPtr)
			{
				if (NodePtr->bIsFullscreen)
				{
					ConcatenatedCommandLineArguments += "-fullscreen ";
				}
				else
				{
					ConcatenatedCommandLineArguments +=
						FString::Printf(
							TEXT("-windowed -forceres -WinX=%i -WinY=%i, -ResX=%i, -ResY=%i "),
								NodePtr->WindowRect.X, NodePtr->WindowRect.Y,
								NodePtr->WindowRect.W, NodePtr->WindowRect.H
						);
				}
			}
		}
		
		// Open a modal to prompt for save, if dirty. Yes = Save & Continue. No = Continue Without Saving. Cancel = Stop Opening Assets.
		UPackage* PackageToSave = nullptr;
        		
		if (UWorld* World = GetCurrentWorld())
		{
			if (ULevel* Level = World->GetCurrentLevel())
			{
				PackageToSave = Level->GetPackage();
			}
		}

		if (PackageToSave)
		{
			const FEditorFileUtils::EPromptReturnCode DialogueResponse =
					FEditorFileUtils::PromptForCheckoutAndSave(
						{PackageToSave},
						true,
						true,
						LOCTEXT("SavePackagesTitle", "Save Packages"),
						LOCTEXT("ConfirmOpenLevelFormat", "Do you want to save the current level?\n\nCancel to abort launch.\n")
					);
		
			if (DialogueResponse == FEditorFileUtils::EPromptReturnCode::PR_Cancelled)
			{
				return;
			}
		}
	
		GetProjectSettingsArguments(ProjectSettings, ConcatenatedCommandLineArguments, ConcatenatedConsoleCommands, ConcatenatedDPCvars,
					 ConcatenatedLogCommands);
		
		AddUdpMessagingArguments(ConcatenatedCommandLineArguments);

		// Add nDisplay node information
		ConcatenatedCommandLineArguments += " " + ConfigActorPath;
		ConcatenatedCommandLineArguments += FString::Printf(TEXT(" -dc_node=\"%s\""), *Node);

		// Add Multi-User params
		if (!ConcertArguments.IsEmpty())
		{
			ConcatenatedCommandLineArguments += " " + ConcertArguments;
		}

		// Log file
		const FString LogFileName = (ProjectSettings->LogFileName.IsEmpty() ? Node : ProjectSettings->LogFileName) + ".log";

		const FString Params =
			FString::Printf(
				TEXT("\"%s\" -game \"%s\" Log=%s %s -ExecCmds=\"%s\" -DPCVars=\"%s\" -LogCmds=\"%s\""),
				*Project, *Map, *LogFileName, *ConcatenatedCommandLineArguments,
				*ConcatenatedConsoleCommands, *ConcatenatedDPCvars, *ConcatenatedLogCommands
	
		);

		UE_LOG(LogDisplayClusterLaunchEditor, Log, TEXT("Full Command: %s %s"), *EditorBinary, *Params);

		void* WritePipe = nullptr;

		ActiveProcesses.Add(
			FPlatformProcess::CreateProc(
				*EditorBinary, *Params,
				ProjectSettings->bCloseEditorOnLaunch,
				false, false, NULL,
				0, NULL, WritePipe
			)
		);
	}
}

void FDisplayClusterLaunchEditorModule::TerminateActiveDisplayClusterProcesses()
{
	for (FProcHandle& Process : ActiveProcesses)
	{
		FPlatformProcess::TerminateProc(Process);
		FPlatformProcess::CloseProc(Process);
	}

	RemoveTerminatedNodeProcesses();
}

void FDisplayClusterLaunchEditorModule::OnFEngineLoopInitComplete()
{
	RegisterProjectSettings();
	RegisterToolbarItem();
}

void FDisplayClusterLaunchEditorModule::RegisterToolbarItem()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	
	RemoveToolbarItem();

	FToolMenuSection& Section = Menu->AddSection("DisplayClusterLaunch");

	const FToolMenuEntry DisplayClusterLaunchButton =
		FToolMenuEntry::InitToolBarButton("DisplayClusterLaunchToolbarButton",
			FUIAction(
				FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::OnClickToolbarButton)
			),
			TAttribute<FText>(),
			TAttribute<FText>::Create(
				TAttribute<FText>::FGetter::CreateRaw(this, &FDisplayClusterLaunchEditorModule::GetToolbarButtonTooltipText)
			),
			TAttribute<FSlateIcon>::Create(
				TAttribute<FSlateIcon>::FGetter::CreateRaw(this, &FDisplayClusterLaunchEditorModule::GetToolbarButtonIcon)
			)
		);

	const FToolMenuEntry DisplayClusterLaunchComboButton = FToolMenuEntry::InitComboButton(
		"DisplayClusterLaunchMenu",
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FDisplayClusterLaunchEditorModule::CreateToolbarMenuEntries),
		LOCTEXT("DisplayClusterLaunchActions", "Launch nDisplay Actions"),
		LOCTEXT("DisplayClusterLaunchActionsTooltip", "Actions related to nDisplay Launch"),
		FSlateIcon(),
		true //bInSimpleComboBox
	);

	Section.AddEntry(DisplayClusterLaunchButton);
	Section.AddEntry(DisplayClusterLaunchComboButton);
}

FText FDisplayClusterLaunchEditorModule::GetToolbarButtonTooltipText()
{
	if (ActiveProcesses.Num() == 1)
	{
		return LOCTEXT("TerminateActiveProcess","Terminate active nDisplay process");
	}
	else if (ActiveProcesses.Num() > 1)
	{
		return FText::Format(LOCTEXT("TerminateActiveProcessesFormat","Terminate {0} active nDisplay processes"), ActiveProcesses.Num());
	}

	if (!SelectedDisplayClusterConfigActor.ResolveObject())
	{
		return LOCTEXT("GenericLaunchDisplayClusterProcessText_NoConfig", "Launch an nDisplay instance using the first Config Actor found in the current level and the first node found in that configuration.\n\nSet specific configurations and nodes using the overflow menu.");
	}

	if (SelectedDisplayClusterConfigActorNodes.Num() == 0)
	{
		return FText::Format(
			LOCTEXT("GenericLaunchDisplayClusterProcessText_NoNodesFormat", "Launch an nDisplay instance using the Config Actor named '{0}' and the first node found in this configuration.\n\nSet specific configurations and nodes using the overflow menu."),
				FText::FromString(SelectedDisplayClusterConfigActor.GetAssetName())
		);
	}

	FString ConfigActorName = SelectedDisplayClusterConfigActor.ResolveObject()->GetName();

	const FString SplitTerm = "_C";
	if (ConfigActorName.Contains(SplitTerm))
	{
		ConfigActorName = ConfigActorName.Left(ConfigActorName.Find(SplitTerm));
	}
	
	return FText::Format(
		LOCTEXT("LaunchDisplayClusterProcessesFormat", "Launch the following nodes:\n\n{0}\n\nFrom this configuration:\n\n{1}"),
			GetSelectedNodesListText(), FText::FromString(ConfigActorName)
		);
}

FSlateIcon FDisplayClusterLaunchEditorModule::GetToolbarButtonIcon()
{
	RemoveTerminatedNodeProcesses();

	return FSlateIcon(FAppStyle::Get().GetStyleSetName(),
		ActiveProcesses.Num() > 0 ? "Icons.Toolbar.Stop" : "Icons.Toolbar.Play");
}

void FDisplayClusterLaunchEditorModule::OnClickToolbarButton()
{
	if (ActiveProcesses.Num() == 0)
	{
		LaunchDisplayClusterProcess();
	}
	else
	{
		TerminateActiveDisplayClusterProcesses();
	}
}

void FDisplayClusterLaunchEditorModule::RemoveToolbarItem()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.User");
	if (Menu->FindSection("DisplayClusterLaunch"))
	{
		Menu->RemoveSection("DisplayClusterLaunch");
	}
}

void FDisplayClusterLaunchEditorModule::RegisterProjectSettings() const
{
	ISettingsModule& SettingsModule = FModuleManager::LoadModuleChecked<ISettingsModule>("Settings");
	{
		// User Project Settings
		const TSharedPtr<ISettingsSection> ProjectSettingsSectionPtr = SettingsModule.RegisterSettings(
			"Project", "Plugins", "nDisplay Launch",
			LOCTEXT("DisplayClusterLaunchSettingsCategoryDisplayName", "nDisplay Launch"),
			LOCTEXT("DisplayClusterLaunchSettingsDescription", "Configure the nDisplay Launch user settings"),
			GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>());
	}
}

FText FDisplayClusterLaunchEditorModule::GetSelectedNodesListText() const
{
	if (SelectedDisplayClusterConfigActorNodes.Num() > 0)
	{
		FString JoinedNodes = FString::Join(SelectedDisplayClusterConfigActorNodes, TEXT("\n"));
		int32 IndexOfBreakOrLengthIfNoBreakFound = JoinedNodes.Find(TEXT("\n"));
		if (IndexOfBreakOrLengthIfNoBreakFound == INDEX_NONE) { IndexOfBreakOrLengthIfNoBreakFound = JoinedNodes.Len(); }
		JoinedNodes.InsertAt(IndexOfBreakOrLengthIfNoBreakFound, " ({0})");
	
		return FText::Format(FText::FromString(JoinedNodes), LOCTEXT("PrimaryNode", "Primary"));
	}

	return FText::GetEmpty();
}

TArray<TWeakObjectPtr<ADisplayClusterRootActor>> FDisplayClusterLaunchEditorModule::GetAllDisplayClusterConfigsInWorld()
{
	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> CachedDisplayClusterActors;
	for (TActorIterator<ADisplayClusterRootActor> Itr(GetCurrentWorld()); Itr; ++Itr)
	{
		if (ADisplayClusterRootActor* RootActor = *Itr)
		{
			CachedDisplayClusterActors.Add(RootActor);
		}
	}

	bAreConfigsFoundInWorld = CachedDisplayClusterActors.Num() > 0;

	CachedDisplayClusterActors.Sort([](const TWeakObjectPtr<ADisplayClusterRootActor>& A, const TWeakObjectPtr<ADisplayClusterRootActor>& B)
	{
		return A->GetActorLabel() < B->GetActorLabel();
	});

	return CachedDisplayClusterActors;
}

bool FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig()
{
	return bAreConfigsFoundInWorld;
}

void FDisplayClusterLaunchEditorModule::ApplyDisplayClusterConfigOverrides(UDisplayClusterConfigurationData* ConfigDataCopy)
{
	if (!ConfigDataCopy->Scene)
	{
		ConfigDataCopy->Scene = NewObject<UDisplayClusterConfigurationScene>(ConfigDataCopy, NAME_None,
		RF_ArchetypeObject | RF_Public );
	}

	// A Primary Node should always be automatically selected, but this code preempts a crash. Normally we use the PN specified in the UI.
	// If one is not specified in the UI, we check to see if the primary node specified
	// in the original config is in our node array selection from the UI.
	// If it isn't, in the loop below we'll use the first active node.
	bool bIsConfigPrimaryNodeInActiveNodes = false;
	const bool bIsPrimaryNodeUnset = SelectedDisplayClusterConfigActorPrimaryNode.IsEmpty();
	if (bIsPrimaryNodeUnset)
	{
		bIsConfigPrimaryNodeInActiveNodes =
			SelectedDisplayClusterConfigActorNodes.Contains(ConfigDataCopy->Cluster->PrimaryNode.Id);
	}
	else
	{
		ConfigDataCopy->Cluster->PrimaryNode.Id = SelectedDisplayClusterConfigActorPrimaryNode;
	}

	TMap<FString, UDisplayClusterConfigurationClusterNode*> ActiveNodes;
	TMap<FString, UDisplayClusterConfigurationClusterNode*> NodesInConfig = ConfigDataCopy->Cluster->Nodes;
	for (int32 NodeIndex = 0; NodeIndex < SelectedDisplayClusterConfigActorNodes.Num(); NodeIndex++)
	{
		FString NodeId = SelectedDisplayClusterConfigActorNodes[NodeIndex];
		if (UDisplayClusterConfigurationClusterNode** Node = NodesInConfig.Find(NodeId))
		{
			ActiveNodes.Add(NodeId, *Node);

			(*Node)->Host = "127.0.0.1";

			// If we haven't specified a primary node and the config's primary node is not in our selection, use the first active node.
			if (bIsPrimaryNodeUnset && !bIsConfigPrimaryNodeInActiveNodes && ActiveNodes.Num() == 1)
			{
				ConfigDataCopy->Cluster->PrimaryNode.Id = NodeId;
			}
		}
	}

	ConfigDataCopy->Cluster->Nodes = ActiveNodes;
}

void FDisplayClusterLaunchEditorModule::SetSelectedDisplayClusterConfigActor(ADisplayClusterRootActor* SelectedActor)
{
	if (SelectedActor)
	{
		const FSoftObjectPath AsSoftObjectPath(SelectedActor);

		if (AsSoftObjectPath != SelectedDisplayClusterConfigActor)
		{
			SelectedDisplayClusterConfigActor = AsSoftObjectPath;

			SelectedDisplayClusterConfigActorNodes.Empty();

			SelectFirstNode(SelectedActor);
		}
	}
}

void FDisplayClusterLaunchEditorModule::ToggleDisplayClusterConfigActorNodeSelected(FString InNodeName)
{
	if (IsDisplayClusterConfigActorNodeSelected(InNodeName))
	{
		SelectedDisplayClusterConfigActorNodes.Remove(InNodeName);
	}
	else
	{
		SelectedDisplayClusterConfigActorNodes.Add(InNodeName);
	}

	// Clear SelectedDisplayClusterConfigActorPrimaryNode if no nodes are selected
	if (SelectedDisplayClusterConfigActorNodes.Num() == 0)
	{
		SelectedDisplayClusterConfigActorPrimaryNode = "";
	}

	// If a single node is selected, SelectedDisplayClusterConfigActorPrimaryNode must be this node
	if (SelectedDisplayClusterConfigActorNodes.Num() == 1)
	{
		SelectedDisplayClusterConfigActorPrimaryNode = SelectedDisplayClusterConfigActorNodes[0];
	}
}

bool FDisplayClusterLaunchEditorModule::IsDisplayClusterConfigActorNodeSelected(FString InNodeName)
{
	return SelectedDisplayClusterConfigActorNodes.Contains(InNodeName);
}

void FDisplayClusterLaunchEditorModule::SetSelectedConsoleVariablesAsset(const FAssetData InConsoleVariablesAsset)
{
	if (SelectedConsoleVariablesAssetName == InConsoleVariablesAsset.AssetName)
	{
		SelectedConsoleVariablesAssetName = NAME_None;
	}
	else
	{
		SelectedConsoleVariablesAssetName = InConsoleVariablesAsset.AssetName;
	}
}

void FDisplayClusterLaunchEditorModule::SelectFirstNode(ADisplayClusterRootActor* InConfig)
{
	TArray<FString> NodeNames;
	InConfig->GetConfigData()->Cluster->Nodes.GenerateKeyArray(NodeNames);

	if (NodeNames.Num() == 0)
	{
		UE_LOG(LogDisplayClusterLaunchEditor, Error, TEXT("%hs: Unable to launch nDisplay because there are no nDisplay nodes in the selected nDisplay Config named '{0}'."), __FUNCTION__, *InConfig->GetActorNameOrLabel());
		return;
	}
		
	SelectedDisplayClusterConfigActorNodes.SetNum(
		Algo::StableRemoveIf(
			SelectedDisplayClusterConfigActorNodes,
			[NodeNames](const FString& SelectedNode)
			{
				return !NodeNames.Contains(SelectedNode);
			}
		)
	);

	if (SelectedDisplayClusterConfigActorNodes.Num() == 0)
	{
		const FString& NodeName = NodeNames[0];
		UE_LOG(LogDisplayClusterLaunchEditor, Warning, TEXT("%hs: Selected nDisplay nodes were not found on the selected DisplayClusterRootActor. We will select the first valid node."), __FUNCTION__);
		SelectedDisplayClusterConfigActorNodes.Add(NodeName);
		UE_LOG(LogDisplayClusterLaunchEditor, Log, TEXT("%hs: Adding first valid node named '{0}' to selected nodes."), __FUNCTION__, *NodeName);
	}
}

TSharedRef<SWidget>  FDisplayClusterLaunchEditorModule::CreateToolbarMenuEntries()
{
	IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
	
	FMenuBuilder MenuBuilder(false, nullptr);

	TArray<TWeakObjectPtr<ADisplayClusterRootActor>> DisplayClusterConfigs = GetAllDisplayClusterConfigsInWorld();

	MenuBuilder.BeginSection("DisplayClusterLaunch", LOCTEXT("DisplayClusterLauncher", "Launch nDisplay"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("DisplayClusterLaunchLastNode", "Launch Last Node Configuration"),
			LOCTEXT("DisplayClusterLaunchLastNodeTooltip", "Launch the last node configuration."),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(), "Icons.Toolbar.Play"),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::LaunchDisplayClusterProcess),
				FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig)
			),
			NAME_None
		);
	}
	MenuBuilder.EndSection();

	AddDisplayClusterLaunchConfigurations(AssetRegistry, MenuBuilder, DisplayClusterConfigs);

	AddDisplayClusterLaunchNodes(AssetRegistry, MenuBuilder);

	AddConsoleVariablesEditorAssetsToToolbarMenu(AssetRegistry, MenuBuilder);

	AddOptionsToToolbarMenu(MenuBuilder);

	return MenuBuilder.MakeWidget();
}

void FDisplayClusterLaunchEditorModule::AddDisplayClusterLaunchConfigurations(IAssetRegistry* AssetRegistry,
	FMenuBuilder& MenuBuilder, TArray<TWeakObjectPtr<ADisplayClusterRootActor>>& DisplayClusterConfigs)
{
	MenuBuilder.BeginSection("DisplayClusterLaunchConfigurations", LOCTEXT("DisplayClusterLaunchConfigurations", "Configuration"));
	{
		if (DisplayClusterConfigs.Num())
		{
			// If one is not set, select the first one found
			bool bIsConfigActorValid = false;
			if (ADisplayClusterRootActor* SelectedActor = Cast<ADisplayClusterRootActor>(SelectedDisplayClusterConfigActor.ResolveObject()))
			{
				bIsConfigActorValid = DisplayClusterConfigs.ContainsByPredicate([SelectedActor](TWeakObjectPtr<ADisplayClusterRootActor>& Comparator)
				{
					return Comparator.IsValid() && SelectedActor == Comparator.Get();
				});
			}

			if (!bIsConfigActorValid)
			{
				SetSelectedDisplayClusterConfigActor(DisplayClusterConfigs[0].Get());
			}
			
			for (const TWeakObjectPtr<ADisplayClusterRootActor>& Node : DisplayClusterConfigs)
			{
				if (!Node.IsValid())
				{
					continue;
				}
				
				const FText NodeName = FText::FromString(Node->GetActorLabel());
				const FText DisplayClusterConfigTooltip =
					FText::Format(LOCTEXT("SelectDisplayClusterConfigFormat","Select configuration '{0}'"), NodeName);
			
				MenuBuilder.AddMenuEntry(
					NodeName,
					DisplayClusterConfigTooltip,
					FSlateIcon(FDisplayClusterLaunchEditorStyle::Get().GetStyleSetName(),"Icons.DisplayCluster"),
					FUIAction(
						FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::SetSelectedDisplayClusterConfigActor, Node.Get()),
						FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig),
						FIsActionChecked::CreateLambda([this, Node]() { return SelectedDisplayClusterConfigActor == FSoftObjectPath(Node.Get()); })
					),
					NAME_None,
					EUserInterfaceActionType::RadioButton
				);
			}
		}
		else
		{
			SelectedDisplayClusterConfigActor.Reset();
			
			MenuBuilder.AddWidget(SNullWidget::NullWidget,
				LOCTEXT("NoDisplayClusterConfigAssetInLevelText", "Add an nDisplay configuration asset to\nthe current level to launch nDisplay."));
		}
	}
	MenuBuilder.EndSection();
}

void FDisplayClusterLaunchEditorModule::AddDisplayClusterLaunchNodes(IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("DisplayClusterLaunchNodes", LOCTEXT("DisplayClusterLaunchNodes", "Nodes"));
	{
		// Submenu for node selection. Using a WrapperSubMenu to avoid the menu automatically closing when selecting nodes
		// AddWrapperSubMenu does not allow for TAttribute<FText> Labels, it just copies the FText input so we need this entry to display live data
		MenuBuilder.AddMenuEntry(
			TAttribute<FText>::CreateLambda(
				[this] ()
				{
					const int32 NodeCount = SelectedDisplayClusterConfigActorNodes.Num();
										
					if (NodeCount > 0)
					{
						if (NodeCount == 1)
						{
							return FText::Format(
								LOCTEXT("SelectedSingleNodeFormat", "'{0}' Selected"),
								FText::FromString(SelectedDisplayClusterConfigActorNodes[0]));
						}
						else
						{
							return FText::Format(
								LOCTEXT("SelectedMultipleNodesFormat", "Selected {0} Nodes"), FText::AsNumber(NodeCount));
						}
					}
					else
					{
						return LOCTEXT("NoDisplayClusterLaunchNodesSelected", "Please select nDisplay nodes to launch.");
					}
				}
			),
			TAttribute<FText>::CreateRaw(
					this, &FDisplayClusterLaunchEditorModule::GetSelectedNodesListText
			),
			FSlateIcon(FDisplayClusterLaunchEditorStyle::Get().GetStyleSetName(),"Icons.DisplayClusterNode"),
			FUIAction(
				FExecuteAction(),
				FCanExecuteAction::CreateLambda([]() { return false; })
			),
			NAME_None,
			EUserInterfaceActionType::None
		);
		
		MenuBuilder.AddWrapperSubMenu(
			LOCTEXT("SelectDisplayClusterNodes","Select nDisplay Nodes"),
			LOCTEXT("SelectDisplayClusterNodesTooltip","Select nDisplay Nodes.\nThe first node selected will be designated as the primary node when launched unless otherwise specified."),
			FOnGetContent::CreateLambda([this]()
			{
				FMenuBuilder NewMenuBuilder(false, nullptr);

				NewMenuBuilder.AddSubMenu(
					TAttribute<FText>::CreateLambda([this]()
					{
						return
							FText::Format(
								LOCTEXT("SelectPrimaryNodeFormat","Select Primary Node ({0})"),
								SelectedDisplayClusterConfigActorPrimaryNode.IsEmpty() ? LOCTEXT("None", "None") :
								FText::FromString(SelectedDisplayClusterConfigActorPrimaryNode));
					}),
					LOCTEXT("SelectPrimaryNode", "Select the Primary Node"),
					FNewMenuDelegate::CreateLambda([this](FMenuBuilder& InMenuBuilder)
					{
						const FText NodeTooltip =
							LOCTEXT("MakePrimaryNodeTooltip", "Make this node the new Primary Node. Does not affect the original configuration.");
						
						for (const FString& SelectedNode : SelectedDisplayClusterConfigActorNodes)
						{
							InMenuBuilder.AddMenuEntry(
								FText::FromString(SelectedNode),
								NodeTooltip,
								FSlateIcon(),
								FUIAction(
									FExecuteAction::CreateLambda([this, SelectedNode]()
									{
										SelectedDisplayClusterConfigActorPrimaryNode = SelectedNode;
									}),
									FCanExecuteAction(),
									FIsActionChecked::CreateLambda([this, SelectedNode]()
									{
										return SelectedDisplayClusterConfigActorPrimaryNode == SelectedNode;
									})
								),
								NAME_None,
								EUserInterfaceActionType::RadioButton
							);
						}
					}),
					FUIAction(
						FExecuteAction(),
						FCanExecuteAction::CreateLambda([this]()
						{
							return SelectedDisplayClusterConfigActorNodes.Num() > 0;
						})
					),
					NAME_None,
					EUserInterfaceActionType::None
				);

				NewMenuBuilder.AddSeparator();

				if (ADisplayClusterRootActor* SelectedActor = Cast<ADisplayClusterRootActor>(SelectedDisplayClusterConfigActor.ResolveObject()))
				{
					TArray<FString> NodeNames;
				   SelectedActor->GetConfigData()->Cluster->Nodes.GenerateKeyArray(NodeNames);
				
				   for (const FString& NodeName : NodeNames)
				   {
					   const FText DisplayClusterNodeName = FText::FromString(NodeName);
					
					   const FText DisplayClusterNodeTooltip =
						   FText::Format(LOCTEXT("SelectDisplayClusterNodeFormat","Select node '{0}'"), DisplayClusterNodeName);
					
					   NewMenuBuilder.AddMenuEntry(
						   DisplayClusterNodeName,
						   DisplayClusterNodeTooltip,
						   FSlateIcon(),
						   FUIAction(
							   FExecuteAction::CreateRaw(
								   this, &FDisplayClusterLaunchEditorModule::ToggleDisplayClusterConfigActorNodeSelected, NodeName),
							   FCanExecuteAction(),
							   FIsActionChecked::CreateRaw(
								   this, &FDisplayClusterLaunchEditorModule::IsDisplayClusterConfigActorNodeSelected, NodeName)
						   ),
						   NAME_None,
						   EUserInterfaceActionType::Check
					   );
				   }
				}

				return NewMenuBuilder.MakeWidget();
			}),
			FSlateIcon(FDisplayClusterLaunchEditorStyle::Get().GetStyleSetName(),"Icons.DisplayClusterNode")
		);

		MenuBuilder.EndSection();
	}
}

void FDisplayClusterLaunchEditorModule::AddConsoleVariablesEditorAssetsToToolbarMenu(
	IAssetRegistry* AssetRegistry, FMenuBuilder& MenuBuilder)
{
	TArray<FAssetData> FoundConsoleVariablesAssets;
	AssetRegistry->GetAssetsByClass("ConsoleVariablesAsset", FoundConsoleVariablesAssets, true);

	if (FoundConsoleVariablesAssets.Num())
	{
		MenuBuilder.BeginSection("DisplayClusterLaunchCvars", LOCTEXT("DisplayClusterLaunchCvars", "Console Variables"));
		{
			const FText ConsoleVariablesAssetTooltip = LOCTEXT("SelectConsoleVariablesAssetFormat","Select Console Variables Asset");
			
			MenuBuilder.AddSubMenu(
				TAttribute<FText>::Create([this](){ return FText::FromName(SelectedConsoleVariablesAssetName); }),
				ConsoleVariablesAssetTooltip,
				FNewMenuDelegate::CreateLambda([this, FoundConsoleVariablesAssets](FMenuBuilder& NewMenuBuilder)
				{
					for (const FAssetData& Asset : FoundConsoleVariablesAssets)
					{
						const FText ConsoleVariablesAssetName = FText::FromName(Asset.AssetName);
						const FText ConsoleVariablesAssetTooltip =
							FText::Format(LOCTEXT("SelectConsoleVariablesAssetFormat","Select Console Variables Asset '{0}'"), ConsoleVariablesAssetName);
						NewMenuBuilder.AddMenuEntry(
							ConsoleVariablesAssetName,
							ConsoleVariablesAssetTooltip,
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::SetSelectedConsoleVariablesAsset, Asset),
								FCanExecuteAction(),
								FGetActionCheckState::CreateLambda([this, Asset](){ return SelectedConsoleVariablesAssetName == Asset.AssetName ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
							),
							NAME_None,
							EUserInterfaceActionType::RadioButton
						);
					}
				}),
				FUIAction(FExecuteAction(), FCanExecuteAction::CreateRaw(this, &FDisplayClusterLaunchEditorModule::DoesCurrentWorldHaveDisplayClusterConfig)),
				NAME_None,
				EUserInterfaceActionType::None,
				false,
				FSlateIcon(FDisplayClusterLaunchEditorStyle::Get().GetStyleSetName(),"Icons.ConsoleVariablesEditor")
			);
		}
		MenuBuilder.EndSection();
	}
}

void FDisplayClusterLaunchEditorModule::AddOptionsToToolbarMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("DisplayClusterLaunchOptions", LOCTEXT("DisplayClusterLaunchOptions", "Options"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ConnectToMultiUserLabel", "Connect to Multi-User"),
			LOCTEXT("ConnectToMultiUserTooltip", "Connect to Multi-User"),
			FSlateIcon(FDisplayClusterLaunchEditorStyle::Get().GetStyleSetName(),"Icons.MultiUser"),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UDisplayClusterLaunchEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>();
					Settings->bConnectToMultiUser = !GetConnectToMultiUser();
					Settings->SaveConfig();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateRaw(this, &FDisplayClusterLaunchEditorModule::GetConnectToMultiUser)
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("EnableUnrealInsightsLabel", "Enable Unreal Insights"),
			LOCTEXT("EnableUnrealInsightsTooltip", "Enable Unreal Insights"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"UnrealInsights.MenuIcon"),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UDisplayClusterLaunchEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>();
					Settings->bEnableUnrealInsights = !Settings->bEnableUnrealInsights;
					Settings->SaveConfig();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return GetDefault<UDisplayClusterLaunchEditorProjectSettings>()->bEnableUnrealInsights; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CloseEditorOnLaunchLabel", "Close Editor on Launch"),
			LOCTEXT("CloseEditorOnLaunchTooltip", "Close Editor on Launch"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.X"),
			FUIAction(
				FExecuteAction::CreateLambda([this]()
				{
					UDisplayClusterLaunchEditorProjectSettings* Settings = GetMutableDefault<UDisplayClusterLaunchEditorProjectSettings>();
					Settings->bCloseEditorOnLaunch = !Settings->bCloseEditorOnLaunch;
					Settings->SaveConfig();
				}),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this](){ return GetDefault<UDisplayClusterLaunchEditorProjectSettings>()->bCloseEditorOnLaunch; })
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("AdvancedSettingsLabel", "Advanced Settings..."),
			LOCTEXT("AdvancedSettingsTooltip", "Open the nDisplay Launch Project Settings"),
			FSlateIcon(FAppStyle::Get().GetStyleSetName(),"Icons.Settings"),
			FUIAction(
				FExecuteAction::CreateStatic(&FDisplayClusterLaunchEditorModule::OpenProjectSettings)
			),
			NAME_None
		);
	}
	MenuBuilder.EndSection();
}

bool FDisplayClusterLaunchEditorModule::GetConnectToMultiUser() const
{
	return GetDefault<UDisplayClusterLaunchEditorProjectSettings>()->bConnectToMultiUser;
}

void FDisplayClusterLaunchEditorModule::RemoveTerminatedNodeProcesses()
{
	ActiveProcesses.SetNum(
		Algo::StableRemoveIf(
			ActiveProcesses,
			[](FProcHandle& Handle)
			{
				return !FPlatformProcess::IsProcRunning(Handle);
			}
		)
	);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDisplayClusterLaunchEditorModule, DisplayClusterLaunchEditor)
