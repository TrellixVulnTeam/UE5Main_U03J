// Copyright Epic Games, Inc. All Rights Reserved.

#include "RHI.h"
#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/MessageDialog.h"

#if WINDOWS_USE_FEATURE_DYNAMIC_RHI

#include "Windows/WindowsPlatformApplicationMisc.h"

#if defined(NV_GEFORCENOW) && NV_GEFORCENOW
#include "GeForceNOWWrapper.h"
#endif

static const TCHAR* GLoadedRHIModuleName;

enum class WindowsRHI
{
	D3D11,
	D3D12,
	Vulkan,
	OpenGL,
};

static TArray<EShaderPlatform> GetTargetedShaderPlatforms()
{
	TArray<FString> TargetedShaderFormats;
	GConfig->GetArray(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("TargetedRHIs"), TargetedShaderFormats, GEngineIni);

	TArray<EShaderPlatform> ShaderPlatforms;
	ShaderPlatforms.Reserve(TargetedShaderFormats.Num());

	for (const FString& ShaderFormat : TargetedShaderFormats)
	{
		ShaderPlatforms.Add(ShaderFormatToLegacyShaderPlatform(FName(*ShaderFormat)));
	}

	return ShaderPlatforms;
}

static bool IsD3DSM6Platform(EShaderPlatform InShaderPlatform)
{
	return IsD3DPlatform(InShaderPlatform) && IsFeatureLevelSupported(InShaderPlatform, ERHIFeatureLevel::SM6);
}

static bool IsD3DSM6PlatformTargeted(const TArray<EShaderPlatform>& TargetedShaderPlatforms)
{
	for (const EShaderPlatform ShaderPlatform : TargetedShaderPlatforms)
	{
		if (IsD3DSM6Platform(ShaderPlatform))
		{
			return true;
		}
	}

	return false;
}

static TOptional<ERHIFeatureLevel::Type> ParseFeatureLevelFromSetting(const TCHAR* InName)
{
	TOptional<ERHIFeatureLevel::Type> ResultFeatureLevel{};

	FString ConfigFeatureLevel;
	if (GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), InName, ConfigFeatureLevel, GEngineIni))
	{
		const FName FeatureLevelName(*ConfigFeatureLevel);

		ERHIFeatureLevel::Type ParsedFeatureLevel = ERHIFeatureLevel::Num;
		if (GetFeatureLevelFromName(FeatureLevelName, ParsedFeatureLevel))
		{
			ResultFeatureLevel = ParsedFeatureLevel;
		}
	}

	return ResultFeatureLevel;
}

static ERHIFeatureLevel::Type FilterFeatureLevel(ERHIFeatureLevel::Type InFeatureLevel, const TCHAR* MinSetting, const TCHAR* MaxSetting)
{
	const TOptional<ERHIFeatureLevel::Type> MinFeatureLevel = ParseFeatureLevelFromSetting(MinSetting);
	const TOptional<ERHIFeatureLevel::Type> MaxFeatureLevel = ParseFeatureLevelFromSetting(MaxSetting);

	if (MinFeatureLevel && InFeatureLevel < MinFeatureLevel.GetValue())
	{
		return MinFeatureLevel.GetValue();
	}
	else if (MaxFeatureLevel && InFeatureLevel > MaxFeatureLevel.GetValue())
	{
		return MaxFeatureLevel.GetValue();
	}

	return InFeatureLevel;
}

static ERHIFeatureLevel::Type FilterFeatureLevel(ERHIFeatureLevel::Type FeatureLevel, WindowsRHI ChosenRHI)
{
	if (ChosenRHI == WindowsRHI::OpenGL)
	{
		// Seriously locked down.
		return ERHIFeatureLevel::ES3_1;
	}

	if (ChosenRHI == WindowsRHI::D3D12)
	{
		return FilterFeatureLevel(FeatureLevel, TEXT("D3D12MinimumFeatureLevel"), TEXT("D3D12MaximumFeatureLevel"));
	}

	if (ChosenRHI == WindowsRHI::D3D11)
	{
		return FilterFeatureLevel(FeatureLevel, TEXT("D3D11MinimumFeatureLevel"), TEXT("D3D11MaximumFeatureLevel"));
	}

	if (ChosenRHI == WindowsRHI::Vulkan)
	{
		return FilterFeatureLevel(FeatureLevel, TEXT("VulkanMinimumFeatureLevel"), TEXT("VulkanMaximumFeatureLevel"));
	}

	return FeatureLevel;
}

// Default to Performance Mode on low-end machines
static bool DefaultFeatureLevelES31()
{
	static TOptional<bool> ForceES31;
	if (ForceES31.IsSet())
	{
		return ForceES31.GetValue();
	}

	// Force Performance mode for machines with too few cores including hyperthreads
	int MinCoreCount = 0;
	if (GConfig->GetInt(TEXT("PerformanceMode"), TEXT("MinCoreCount"), MinCoreCount, GEngineIni) && FPlatformMisc::NumberOfCoresIncludingHyperthreads() < MinCoreCount)
	{
		ForceES31 = true;
		return true;
	}

	FWindowsPlatformApplicationMisc::FGPUInfo BestGPUInfo = FWindowsPlatformApplicationMisc::GetBestGPUInfo();

	FString MinMemorySizeBucketString;
	FString MinIntegratedMemorySizeBucketString;
	if (GConfig->GetString(TEXT("PerformanceMode"), TEXT("MinMemorySizeBucket"), MinMemorySizeBucketString, GEngineIni) && GConfig->GetString(TEXT("PerformanceMode"), TEXT("MinIntegratedMemorySizeBucket"), MinIntegratedMemorySizeBucketString, GEngineIni))
	{
		for (int EnumIndex = int(EPlatformMemorySizeBucket::Largest); EnumIndex <= int(EPlatformMemorySizeBucket::Tiniest); EnumIndex++)
		{
			const TCHAR* BucketString = LexToString(EPlatformMemorySizeBucket(EnumIndex));
			// Force Performance mode for machines with too little memory
			if (MinMemorySizeBucketString == BucketString)
			{
				if (FPlatformMemory::GetMemorySizeBucket() >= EPlatformMemorySizeBucket(EnumIndex))
				{
					ForceES31 = true;
					return true;
				}
			}

			// Force Performance mode for machines with too little memory when shared with the GPU
			if (MinIntegratedMemorySizeBucketString == BucketString)
			{
				const int MIN_GPU_MEMORY = 512 * 1024 * 1024;
				if (FPlatformMemory::GetMemorySizeBucket() >= EPlatformMemorySizeBucket(EnumIndex) && BestGPUInfo.DedicatedVideoMemory < MIN_GPU_MEMORY)
				{
					ForceES31 = true;

					return true;
				}
			}
		}
	}

	TArray<FString> DeviceDefaultRHIList;
	GConfig->GetArray(TEXT("Devices"), TEXT("DeviceDefaultRHIList"), DeviceDefaultRHIList, GHardwareIni);

	FString GPUBrand = FPlatformMisc::GetPrimaryGPUBrand();
	for (const FString& DeviceDefaultRHIString : DeviceDefaultRHIList)
	{
		const TCHAR* Line = *DeviceDefaultRHIString;

		ensure(Line[0] == TCHAR('('));

		FString RHIName;
		FParse::Value(Line+1, TEXT("RHI="), RHIName);

		FString DeviceName;
		FParse::Value(Line+1, TEXT("DeviceName="), DeviceName);

		if (RHIName.Compare("D3D11_ES31", ESearchCase::IgnoreCase) == 0 && GPUBrand.Compare(DeviceName, ESearchCase::IgnoreCase) == 0)
		{
			ForceES31 = true;

			return true;
		}

		FString VendorId;
		FParse::Value(Line + 1, TEXT("VendorId="), VendorId);
		uint32 VendorIdInt = FParse::HexNumber(*VendorId);

		FString DeviceId;
		FParse::Value(Line + 1, TEXT("DeviceId="), DeviceId);
		uint32 DeviceIdInt = FParse::HexNumber(*DeviceId);

		if (BestGPUInfo.VendorId && BestGPUInfo.DeviceId &&
			BestGPUInfo.VendorId == VendorIdInt && BestGPUInfo.DeviceId == DeviceIdInt &&
			RHIName.Compare("D3D11_ES31", ESearchCase::IgnoreCase) == 0)
		{
			ForceES31 = true;

			return true;
		}
	}

	ForceES31 = false;
	return false;
}

static bool PreferFeatureLevelES31()
{
	if (!GIsEditor)
	{
		bool bIsRunningInGFN = false;
#if defined(NV_GEFORCENOW) && NV_GEFORCENOW
		//Prevent ES31 from being forced since we have other ways of setting scalability issues on GFN.
		GeForceNOWWrapper::Get().Initialize();
		bIsRunningInGFN = GeForceNOWWrapper::Get().IsRunningInGFN();
#endif

		bool bPreferFeatureLevelES31 = false;
		bool bFoundPreference = GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bPreferFeatureLevelES31"), bPreferFeatureLevelES31, GGameUserSettingsIni);

		// Force low-spec users into performance mode but respect their choice once they have set a preference
		bool bDefaultES31 = false;
		if (!bFoundPreference && !bIsRunningInGFN)
		{
			bDefaultES31 = DefaultFeatureLevelES31();
		}

		if (bPreferFeatureLevelES31 || bDefaultES31)
		{
			if (!bFoundPreference)
			{
				GConfig->SetBool(TEXT("D3DRHIPreference"), TEXT("bPreferFeatureLevelES31"), true, GGameUserSettingsIni);
			}
			return true;
		}
	}
	return false;
}

static bool IsES31D3DOnly()
{
	bool bES31DXOnly = false;
#if !WITH_EDITOR
	if (!GIsEditor)
	{
		GConfig->GetBool(TEXT("PerformanceMode"), TEXT("bES31DXOnly"), bES31DXOnly, GEngineIni);
	}
#endif
	return bES31DXOnly;
}

static bool AllowD3D12FeatureLevelES31()
{
	if (!GIsEditor)
	{
		return FilterFeatureLevel(ERHIFeatureLevel::ES3_1, WindowsRHI::D3D12) == ERHIFeatureLevel::ES3_1;
	}
	return true;
}

// Choose the default from DefaultGraphicsRHI or TargetedRHIs. DefaultGraphicsRHI has precedence.
static WindowsRHI ChooseDefaultRHI(const TArray<EShaderPlatform>& TargetedShaderPlatforms)
{
	// Make sure the DDSPI is initialized before we try and read from it
	FGenericDataDrivenShaderPlatformInfo::Initialize();

	WindowsRHI DefaultRHI = WindowsRHI::D3D11;

	// Default graphics RHI is the main project setting that governs the choice, so it takes the priority
	FString DefaultGraphicsRHI;
	if (GConfig->GetString(TEXT("/Script/WindowsTargetPlatform.WindowsTargetSettings"), TEXT("DefaultGraphicsRHI"), DefaultGraphicsRHI, GEngineIni))
	{
		const FString NAME_DX11(TEXT("DefaultGraphicsRHI_DX11"));
		const FString NAME_DX12(TEXT("DefaultGraphicsRHI_DX12"));
		const FString NAME_VULKAN(TEXT("DefaultGraphicsRHI_Vulkan"));

		if (DefaultGraphicsRHI == NAME_DX11)
		{
			DefaultRHI = WindowsRHI::D3D11;
		}
		else if (DefaultGraphicsRHI == NAME_DX12)
		{
			DefaultRHI = WindowsRHI::D3D12;
		}
		else if (DefaultGraphicsRHI == NAME_VULKAN)
		{
			DefaultRHI = WindowsRHI::Vulkan;
		}
		else if (DefaultGraphicsRHI != TEXT("DefaultGraphicsRHI_Default"))
		{
			UE_LOG(LogRHI, Error, TEXT("Unrecognized setting '%s' for DefaultGraphicsRHI"), *DefaultGraphicsRHI);
		}
	}
	else if (TargetedShaderPlatforms.Num() > 0)
	{
		// If we don't have DefaultGraphicsRHI set, try to deduce it from the list of targeted shader platforms
		// Pick the first one
		const EShaderPlatform TargetedPlatform = TargetedShaderPlatforms[0];

		// not checking D3D as DefaultRHI begins initialized as D3D12
		if (IsVulkanPlatform(TargetedPlatform))
		{
			DefaultRHI = WindowsRHI::Vulkan;
		}
		else if (IsOpenGLPlatform(TargetedPlatform))
		{
			DefaultRHI = WindowsRHI::OpenGL;
		}
		else if (IsD3DSM6Platform(TargetedPlatform))
		{
			DefaultRHI = WindowsRHI::D3D12;
		}
		else
		{
			DefaultRHI = WindowsRHI::D3D11;
		}
	}

	return DefaultRHI;
}

static TOptional<WindowsRHI> ChoosePreferredRHI(WindowsRHI InDefaultRHI)
{
	TOptional<WindowsRHI> RHIPreference{};

	// If we are in game, there is a separate setting that can make it prefer D3D12 over D3D11 (but not over other RHIs).
	if (!GIsEditor && (InDefaultRHI == WindowsRHI::D3D11 || InDefaultRHI == WindowsRHI::D3D12))
	{
		bool bUseD3D12InGame = false;
		if (GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bUseD3D12InGame"), bUseD3D12InGame, GGameUserSettingsIni) && bUseD3D12InGame)
		{
			RHIPreference = WindowsRHI::D3D12;
		}
	}

	return RHIPreference;
}

static TOptional<WindowsRHI> ChooseForcedRHI()
{
	TOptional<WindowsRHI> ForcedRHI = {};

	// Command line overrides
	uint32 Sum = 0;
	if (FParse::Param(FCommandLine::Get(), TEXT("vulkan")))
	{
		ForcedRHI = WindowsRHI::Vulkan;
		Sum++;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("opengl")))
	{
		ForcedRHI = WindowsRHI::OpenGL;
		Sum++;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("d3d11")) || FParse::Param(FCommandLine::Get(), TEXT("dx11")))
	{
		ForcedRHI = WindowsRHI::D3D11;
		Sum++;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("d3d12")) || FParse::Param(FCommandLine::Get(), TEXT("dx12")))
	{
		ForcedRHI = WindowsRHI::D3D12;
		Sum++;
	}

	if (Sum > 1)
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIOptionsError", "-d3d12/dx12, -d3d11/dx11, -vulkan, and -opengl are mutually exclusive options, but more than one was specified on the command-line."));
		UE_LOG(LogRHI, Fatal, TEXT("-d3d12, -d3d11, -vulkan, and -opengl are mutually exclusive options, but more than one was specified on the command-line."));
	}

#if	!WITH_EDITOR && UE_BUILD_SHIPPING
	// In Shipping builds we can limit ES31 on Windows to only DX11. All RHIs are allowed by default.

	// FeatureLevelES31 is also a command line override, so it will determine the underlying RHI unless one is specified
	if (IsES31D3DOnly() && (FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES3_1"))))
	{
		if (ForcedRHI == WindowsRHI::OpenGL)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIPerformanceOpenGL", "OpenGL is not supported for Performance Mode."));
			UE_LOG(LogRHI, Fatal, TEXT("OpenGL is not supported for Performance Mode."));
		}
		else if (ForcedRHI == WindowsRHI::Vulkan)
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIPerformanceVulkan", "Vulkan is not supported for Performance Mode."));
			UE_LOG(LogRHI, Fatal, TEXT("Vulkan is not supported for Performance Mode."));
		}
		else if (ForcedRHI == WindowsRHI::D3D12)
		{
			if (!AllowD3D12FeatureLevelES31())
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RHIPerformanceDX12", "DirectX 12 is not supported for Performance Mode."));
				UE_LOG(LogRHI, Fatal, TEXT("DirectX 12 is not supported for Performance Mode."));
			}
		}
		else
		{
			ForcedRHI = WindowsRHI::D3D11;
		}
	}
#endif //!WITH_EDITOR && UE_BUILD_SHIPPING

	return ForcedRHI;
}

static TOptional<ERHIFeatureLevel::Type> ChooseForcedFeatureLevel(TOptional<WindowsRHI> ChosenRHI, TOptional<WindowsRHI> ForcedRHI)
{
	TOptional<ERHIFeatureLevel::Type> ForcedFeatureLevel{};

	if (FParse::Param(FCommandLine::Get(), TEXT("es31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES31")) || FParse::Param(FCommandLine::Get(), TEXT("FeatureLevelES3_1")))
	{
		ForcedFeatureLevel = ERHIFeatureLevel::ES3_1;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("sm5")))
	{
		ForcedFeatureLevel = ERHIFeatureLevel::SM5;
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("sm6")))
	{
		ForcedFeatureLevel = ERHIFeatureLevel::SM6;
	}

	return ForcedFeatureLevel;
}

static ERHIFeatureLevel::Type ChooseFeatureLevel(TOptional<WindowsRHI> ChosenRHI, TOptional<ERHIFeatureLevel::Type> ForcedFeatureLevel, const TArray<EShaderPlatform>& TargetedShaderPlatforms)
{
	if (ForcedFeatureLevel)
	{
		// Allow the forced feature level if we're in a position to compile its shaders
		if (!FPlatformProperties::RequiresCookedData())
		{
			return ForcedFeatureLevel.GetValue();
		}

		// Make sure the feature level is supported by the runtime, otherwise fall back to the default
		const ERHIFeatureLevel::Type FilteredForcedFeatureLevel = FilterFeatureLevel(ForcedFeatureLevel.GetValue(), ChosenRHI.GetValue());
		if (FilteredForcedFeatureLevel == ForcedFeatureLevel)
		{
			return FilteredForcedFeatureLevel;
		}
	}

	ERHIFeatureLevel::Type FeatureLevel;

	if (ChosenRHI == WindowsRHI::OpenGL)
	{
		// OpenGL can only be used for mobile preview
		FeatureLevel = ERHIFeatureLevel::ES3_1;
	}
	else if (ChosenRHI == WindowsRHI::D3D12)
	{
		FeatureLevel = IsD3DSM6PlatformTargeted(TargetedShaderPlatforms) ? ERHIFeatureLevel::SM6 : ERHIFeatureLevel::SM5;
	}
	else
	{
		FeatureLevel = ERHIFeatureLevel::SM5;
	}

	ERHIFeatureLevel::Type FilteredFeatureLevel = FilterFeatureLevel(FeatureLevel, ChosenRHI.GetValue());

	// If the user wanted to force a feature level and we couldn't set it, log out why and what we're actually running with
	if (ForcedFeatureLevel)
	{
		const FString ForcedName = LexToString(ForcedFeatureLevel.GetValue());
		const FString UsedName = LexToString(FilteredFeatureLevel);
		UE_LOG(LogRHI, Warning, TEXT("User requested Feature Level '%s' but that is not supported by this project. Falling back to Feature Level '%s'."), *ForcedName, *UsedName);
	}

	return FilteredFeatureLevel;
}

static IDynamicRHIModule* LoadDynamicRHIModule(ERHIFeatureLevel::Type& DesiredFeatureLevel, const TCHAR*& LoadedRHIModuleName)
{
	bool bUseGPUCrashDebugging = false;
	if (!GIsEditor && GConfig->GetBool(TEXT("D3DRHIPreference"), TEXT("bUseGPUCrashDebugging"), bUseGPUCrashDebugging, GGameUserSettingsIni))
	{
		auto GPUCrashDebuggingCVar = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.GPUCrashDebugging"));
		*GPUCrashDebuggingCVar = bUseGPUCrashDebugging;
	}

	const TArray<EShaderPlatform> TargetedShaderPlatforms = GetTargetedShaderPlatforms();

	// RHI is chosen by the project settings (first DefaultGraphicsRHI, then TargetedRHIs are consulted, "Default" maps to D3D12). 
	// After this, a separate game-only setting (does not affect editor) bPreferD3D12InGame selects between D3D12 or D3D11 (but will not have any effect if Vulkan or OpenGL are chosen).
	// Commandline switches apply after this and can force an arbitrary RHIs. If RHI isn't supported, the game will refuse to start.

	WindowsRHI DefaultRHI = ChooseDefaultRHI(TargetedShaderPlatforms);
	const TOptional<WindowsRHI> PreferredRHI = ChoosePreferredRHI(DefaultRHI);
	const TOptional<WindowsRHI> ForcedRHI = ChooseForcedRHI();

	WindowsRHI ChosenRHI = DefaultRHI;
	if (ForcedRHI)
	{
		ChosenRHI = ForcedRHI.GetValue();
	}
	else if (PreferredRHI)
	{
		ChosenRHI = PreferredRHI.GetValue();
	}

	TOptional<ERHIFeatureLevel::Type> ForcedFeatureLevel = ChooseForcedFeatureLevel(ChosenRHI, ForcedRHI);
	DesiredFeatureLevel = ChooseFeatureLevel(ChosenRHI, ForcedFeatureLevel, TargetedShaderPlatforms);

	// Load the dynamic RHI module.
	IDynamicRHIModule* DynamicRHIModule = nullptr;

	if (ChosenRHI == WindowsRHI::OpenGL)
	{
		FApp::SetGraphicsRHI(TEXT("OpenGL"));
		const TCHAR* OpenGLRHIModuleName = TEXT("OpenGLDrv");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(OpenGLRHIModuleName);

		if (!DynamicRHIModule->IsSupported(DesiredFeatureLevel))
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredOpenGL", "OpenGL 4.3 is required to run the engine."));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = nullptr;
		}

		LoadedRHIModuleName = OpenGLRHIModuleName;
	}
	else if (ChosenRHI == WindowsRHI::Vulkan)
	{
		FApp::SetGraphicsRHI(TEXT("Vulkan"));
		const TCHAR* VulkanRHIModuleName = TEXT("VulkanRHI");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(VulkanRHIModuleName);

		if (!DynamicRHIModule->IsSupported(DesiredFeatureLevel))
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredVulkan", "Vulkan Driver is required to run the engine."));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = nullptr;
		}
		LoadedRHIModuleName = VulkanRHIModuleName;
	}
	else if (ChosenRHI == WindowsRHI::D3D12)
	{
		FString FeatureLevelName;
		GetFeatureLevelName(DesiredFeatureLevel, FeatureLevelName);

		const FString RHIDisplayName = FString::Printf(TEXT("DirectX 12 (%s)"), *FeatureLevelName);
		FApp::SetGraphicsRHI(RHIDisplayName);

		LoadedRHIModuleName = TEXT("D3D12RHI");
		DynamicRHIModule = FModuleManager::LoadModulePtr<IDynamicRHIModule>(LoadedRHIModuleName);

		bool bD3D12Supported = DynamicRHIModule && DynamicRHIModule->IsSupported(DesiredFeatureLevel);

		// Fallback to SM5 if SM6 is not supported
		if (!bD3D12Supported && DynamicRHIModule && DesiredFeatureLevel == ERHIFeatureLevel::SM6)
		{
			if (ForcedFeatureLevel)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredDX12SM6", "DX12 SM6 is not supported on your system. Try running without the -sm6 command line argument."));
				FPlatformMisc::RequestExit(1);
			}

			if (FilterFeatureLevel(ERHIFeatureLevel::SM5, WindowsRHI::D3D12) == ERHIFeatureLevel::SM5)
			{
				UE_LOG(LogRHI, Log, TEXT("D3D12 SM6 is not supported, trying SM5"));

				DesiredFeatureLevel = ERHIFeatureLevel::SM5;
				bD3D12Supported = DynamicRHIModule->IsSupported(DesiredFeatureLevel);
			}
			
			if (!bD3D12Supported)
			{
				UE_LOG(LogRHI, Log, TEXT("D3D12 SM5 is not supported, trying D3D11"));
			}
		}

		if (!bD3D12Supported)
		{
			if (ForcedRHI == WindowsRHI::D3D12)
			{
				FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredDX12", "DX12 is not supported on your system. Try running without the -dx12 or -d3d12 command line argument."));
				FPlatformMisc::RequestExit(1);
			}
			if (DynamicRHIModule)
			{
				FModuleManager::Get().UnloadModule(LoadedRHIModuleName);
			}
			DynamicRHIModule = nullptr;
			LoadedRHIModuleName = nullptr;
		}
	}

	// Fallback to D3D11RHI if nothing is selected
	if (!DynamicRHIModule)
	{
		// We need to filter one more time to handle the D3D12 fallback
		DesiredFeatureLevel = FilterFeatureLevel(DesiredFeatureLevel, WindowsRHI::D3D11);

		FApp::SetGraphicsRHI(TEXT("DirectX 11"));
		const TCHAR* D3D11RHIModuleName = TEXT("D3D11RHI");
		DynamicRHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(D3D11RHIModuleName);

		if (!DynamicRHIModule->IsSupported(DesiredFeatureLevel))
		{
			FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("WindowsDynamicRHI", "RequiredDX11Feature_11_SM5", "A D3D11-compatible GPU (Feature Level 11.0, Shader Model 5.0) is required to run the engine."));
			FPlatformMisc::RequestExit(1);
			DynamicRHIModule = nullptr;
		}

		LoadedRHIModuleName = D3D11RHIModuleName;
	}
	return DynamicRHIModule;
}

FDynamicRHI* PlatformCreateDynamicRHI()
{
	FDynamicRHI* DynamicRHI = nullptr;

	ERHIFeatureLevel::Type RequestedFeatureLevel;
	const TCHAR* LoadedRHIModuleName;
	IDynamicRHIModule* DynamicRHIModule = LoadDynamicRHIModule(RequestedFeatureLevel, LoadedRHIModuleName);

	if (DynamicRHIModule)
	{
		// Create the dynamic RHI.
		DynamicRHI = DynamicRHIModule->CreateRHI(RequestedFeatureLevel);
		GLoadedRHIModuleName = LoadedRHIModuleName;
	}

	return DynamicRHI;
}

const TCHAR* GetSelectedDynamicRHIModuleName(bool bCleanup)
{
	check(FApp::CanEverRender());

	if (GDynamicRHI)
	{
		check(!!GLoadedRHIModuleName);
		return GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1 ? TEXT("ES31") : GLoadedRHIModuleName;
	}
	else
	{
		ERHIFeatureLevel::Type DesiredFeatureLevel;
		const TCHAR* RHIModuleName;
		IDynamicRHIModule* DynamicRHIModule = LoadDynamicRHIModule(DesiredFeatureLevel, RHIModuleName);
		check(DynamicRHIModule);
		check(RHIModuleName);
		if (bCleanup)
		{
			FModuleManager::Get().UnloadModule(RHIModuleName);
		}

		return DesiredFeatureLevel == ERHIFeatureLevel::ES3_1 ? TEXT("ES31") : RHIModuleName;
	}
}

#endif //WINDOWS_USE_FEATURE_DYNAMIC_RHI
