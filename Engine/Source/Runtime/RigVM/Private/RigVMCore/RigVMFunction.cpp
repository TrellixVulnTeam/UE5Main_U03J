// Copyright Epic Games, Inc. All Rights Reserved.

#include "RigVMCore/RigVMFunction.h"
#include "RigVMCore/RigVMTemplate.h"
#include "RigVMCore/RigVMRegistry.h"

FString FRigVMFunction::GetName() const
{
	return Name;
}

FName FRigVMFunction::GetMethodName() const
{
	FString FullName(Name);
	FString Right;
	if (FullName.Split(TEXT("::"), nullptr, &Right))
	{
		return *Right;
	}
	return NAME_None;
}

FString FRigVMFunction::GetModuleName() const
{
#if WITH_EDITOR
	if (Struct)
	{
		if (UPackage* Package = Struct->GetPackage())
		{
			return Package->GetName();
		}
	}
#endif
	return FString();
}

FString FRigVMFunction::GetModuleRelativeHeaderPath() const
{
#if WITH_EDITOR
	if (Struct)
	{
		FString ModuleRelativePath;
		if (Struct->GetStringMetaDataHierarchical(TEXT("ModuleRelativePath"), &ModuleRelativePath))
		{
			return ModuleRelativePath;
		}
	}
#endif
	return FString();
}

bool FRigVMFunction::IsAdditionalArgument(const FRigVMFunctionArgument& InArgument) const
{
#if WITH_EDITOR
	if (Struct)
	{
		return Struct->FindPropertyByName(InArgument.Name) == nullptr;
	}
#endif
	return false;
}

const FRigVMTemplate* FRigVMFunction::GetTemplate() const
{
	if(TemplateIndex == INDEX_NONE)
	{
		return nullptr;
	}

	const FRigVMTemplate* Template = &FRigVMRegistry::Get().GetTemplates()[TemplateIndex];
	if(Template->NumPermutations() <= 1)
	{
		return nullptr;
	}

	return Template;
}
