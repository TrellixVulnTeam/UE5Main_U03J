// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGEditorGraph.h"

#include "PCGGraph.h"
#include "PCGEdge.h"
#include "PCGEditorGraphNode.h"
#include "PCGEditorGraphNodeInput.h"
#include "PCGEditorGraphNodeOutput.h"

#include "EdGraph/EdGraphPin.h"

void UPCGEditorGraph::InitFromNodeGraph(UPCGGraph* InPCGGraph)
{
	check(InPCGGraph && !PCGGraph);
	PCGGraph = InPCGGraph;

	TMap<UPCGNode*, UPCGEditorGraphNodeBase*> NodeLookup;
	const bool bSelectNewNode = false;

	UPCGNode* InputNode = PCGGraph->GetInputNode();
	FGraphNodeCreator<UPCGEditorGraphNodeInput> InputNodeCreator(*this);
	UPCGEditorGraphNodeInput* InputGraphNode = InputNodeCreator.CreateNode(bSelectNewNode);
	InputGraphNode->Construct(InputNode, EPCGEditorGraphNodeType::Input);
	InputNodeCreator.Finalize();
	NodeLookup.Add(InputNode, InputGraphNode);

	UPCGNode* OutputNode = PCGGraph->GetOutputNode();
	FGraphNodeCreator<UPCGEditorGraphNodeOutput> OutputNodeCreator(*this);
	UPCGEditorGraphNodeOutput* OutputGraphNode = OutputNodeCreator.CreateNode(bSelectNewNode);
	OutputGraphNode->Construct(OutputNode, EPCGEditorGraphNodeType::Output);
	OutputNodeCreator.Finalize();
	NodeLookup.Add(OutputNode, OutputGraphNode);

	for (UPCGNode* PCGNode : PCGGraph->GetNodes())
	{
		FGraphNodeCreator<UPCGEditorGraphNode> NodeCreator(*this);
		UPCGEditorGraphNode* GraphNode = NodeCreator.CreateNode(bSelectNewNode);
		GraphNode->Construct(PCGNode, EPCGEditorGraphNodeType::Settings);
		NodeCreator.Finalize();
		NodeLookup.Add(PCGNode, GraphNode);
	}

	for (const auto& NodeLookupIt : NodeLookup)
	{
		UPCGNode* PCGNode = NodeLookupIt.Key;
		UPCGEditorGraphNodeBase* GraphNode = NodeLookupIt.Value;

		for (UPCGEdge* OutboundEdge : PCGNode->GetOutboundEdges())
		{
			const FName OutPinName = (OutboundEdge->InboundLabel == NAME_None) ? TEXT("Out") : OutboundEdge->InboundLabel;
			UEdGraphPin* OutPin = GraphNode->FindPin(OutPinName, EEdGraphPinDirection::EGPD_Output);

			if (!OutPin)
			{
				continue;
			}
			
			UPCGNode* OutboundNode = OutboundEdge->OutboundNode;
			if (UPCGEditorGraphNodeBase** ConnectedGraphNode = NodeLookup.Find(OutboundNode))
			{
				const FName InPinName = (OutboundEdge->OutboundLabel == NAME_None) ? TEXT("In") : OutboundEdge->OutboundLabel;

				if(UEdGraphPin* InPin = (*ConnectedGraphNode)->FindPin(InPinName, EEdGraphPinDirection::EGPD_Input))
				{
					OutPin->MakeLinkTo(InPin);
				}
			}
		}
	}
}
