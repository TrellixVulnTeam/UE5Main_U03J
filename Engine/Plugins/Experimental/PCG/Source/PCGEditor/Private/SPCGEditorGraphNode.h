// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SGraphNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class UPCGEditorGraphNodeBase;

class SPCGEditorGraphNode : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SPCGEditorGraphNode){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UPCGEditorGraphNodeBase* InNode);

	//~ Begin SGraphNode Interface
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override;
	//~ End SGraphNode Interface

protected:
	void OnNodeChanged();

private:
	UPCGEditorGraphNodeBase* PCGEditorGraphNode = nullptr;
};
