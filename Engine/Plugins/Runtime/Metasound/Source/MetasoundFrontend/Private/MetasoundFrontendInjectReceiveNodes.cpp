// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundFrontendInjectReceiveNodes.h"

#include "MetasoundFacade.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundParamHelper.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundVertex.h"

#define LOCTEXT_NAMESPACE "MetasoundFrontend"

namespace Metasound
{
	METASOUND_PARAM(OutputAddress, "Address", "Address")

	namespace Frontend 
	{
		namespace MetasoundFrontendInjectReceiveNodesPrivate
		{
			// Metasound Operator which returns the transmission address for the injected receive node.
			class FAddressOperator : public FNoOpOperator
			{
			public:
				static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams&, TArray<TUniquePtr<IOperatorBuildError>>&);
				static const FNodeClassMetadata& GetNodeInfo();

				FAddressOperator(TDataReadReference<FSendAddress> InAddress)
				: Address(InAddress)
				{
				}

				virtual ~FAddressOperator() = default;

				virtual FDataReferenceCollection GetOutputs() const override
				{
					FDataReferenceCollection Outputs;
					Outputs.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutputAddress), Address);
					return Outputs;
				}

			private:
				TDataReadReference<FSendAddress> Address;
			};

			// Metasound Node which returns the transmission address for the injected receive node.
			class FAddressNode : public FNodeFacade
			{
			public:
				FAddressNode(const FGuid& InID, const FVertexName& InVertexName, const FName& InTypeName, FReceiveNodeAddressFunction InAddressFunction)
				: FNodeFacade(*FString::Format(TEXT("ReceiveAddressInject_{0}"), { InVertexName.ToString() }), InID, TFacadeOperatorClass<FAddressOperator>())
				, VertexKey(InVertexName)
				, TypeName(InTypeName)
				, AddressFunction(InAddressFunction)
				{
				}

				FSendAddress GetAddress(const FMetasoundEnvironment& InEnvironment) const
				{
					return AddressFunction(InEnvironment, VertexKey, TypeName);
				}

			private:
				FVertexName VertexKey;
				FName TypeName;
				FReceiveNodeAddressFunction AddressFunction;
			};

			TUniquePtr<IOperator> FAddressOperator::CreateOperator(const FCreateOperatorParams& InParams, TArray<TUniquePtr<IOperatorBuildError>>& OutBuildErrors)
			{
				const FAddressNode& AddressNode = static_cast<const FAddressNode&>(InParams.Node);
				FSendAddress Address = AddressNode.GetAddress(InParams.Environment);
				return MakeUnique<FAddressOperator>(TDataReadReference<FSendAddress>::CreateNew(Address));
			}

			const FNodeClassMetadata& FAddressOperator::GetNodeInfo()
			{
				auto CreateVertexInterface = []() -> FVertexInterface
				{
					FVertexInterface Interface
					{
						FInputVertexInterface{},
						FOutputVertexInterface
						{
							TOutputDataVertex<FSendAddress>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutputAddress))
						}
					};

					return Interface;
				};

				auto CreateNodeClassMetadata = [&CreateVertexInterface]() -> FNodeClassMetadata
				{
					FNodeClassMetadata Metadata
					{
						FNodeClassName { "MetasoundFrontendInjectReceiveNodes", "ReceiveNodeAddress", "" },
						1, // MajorVersion
						0, // MinorVersion
						FText::GetEmpty(), // DisplayName
						FText::GetEmpty(), // Description
						FString(), // Author
						FText::GetEmpty(), // Prompt If Missing
						CreateVertexInterface(), // DefaultInterface
						TArray<FText>(), // CategoryHierachy
						TArray<FText>(), // Keywords
						FNodeDisplayStyle() // NodeDisplayStyle
					}; 

					return Metadata;
				};

				static const FNodeClassMetadata Metadata = CreateNodeClassMetadata();
				return Metadata;
			}

			TUniquePtr<INode> CreateReceiveNodeForDataType(const FGuid& InID, const FVertexName& InVertexName, const FName& InDataType)
			{
				FNodeInitData ReceiveNodeInitData 
				{ 
					*FString::Format(TEXT("ReceiveInject_{0}"), { InVertexName.ToString() }),
					InID
				};
				return IDataTypeRegistry::Get().CreateReceiveNode(InDataType, ReceiveNodeInitData);
			}
		}

		bool InjectReceiveNode(FFrontendGraph& InGraph, const FReceiveNodeAddressFunction& InAddressFunction, const FInputDataDestination& InputDestination)
		{
			using namespace MetasoundFrontendInjectReceiveNodesPrivate;
			using namespace ReceiveNodeInfo; 

			// should never contain null nodes for input destination.
			check(InputDestination.Node != nullptr);

			const FVertexName& VertexKey = InputDestination.Vertex.VertexName;
			const FName& DataType = InputDestination.Vertex.DataTypeName;

			// Create a receive node.
			const FGuid ReceiveNodeID = FGuid::NewGuid();
			TSharedPtr<INode> ReceiveNode(CreateReceiveNodeForDataType(ReceiveNodeID, VertexKey, DataType).Release());

			if (!ReceiveNode.IsValid())
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to create receive node while injecting receive node for graph input [VertexKey:%s, VertexDescription:%s, DataTypeName:%s]"), *VertexKey.ToString(), *InputDestination.Vertex.Metadata.Description.ToString(), *DataType.ToString());
				return false;
			}

			// Create a node for address of receive node.
			const FGuid AddressNodeID = FGuid::NewGuid();
			TSharedPtr<INode> AddressNode = MakeShared<FAddressNode>(AddressNodeID, VertexKey, DataType, InAddressFunction);

			if (!AddressNode.IsValid())
			{
				UE_LOG(LogMetaSound, Warning, TEXT("Failed to create address node while injecting receive node for graph input [VertexKey:%s, VertexDescription:%s, DataTypeName:%s]"), *VertexKey.ToString(), *InputDestination.Vertex.Metadata.Description.ToString(), *DataType.ToString());
				return false;
			}

			// Add receive node and value node to graph.
			InGraph.AddNode(ReceiveNodeID, ReceiveNode);
			InGraph.AddNode(AddressNodeID, AddressNode);

			bool bDataEdgeAdded = InGraph.AddDataEdge(*AddressNode, METASOUND_GET_PARAM_NAME(OutputAddress), *ReceiveNode, METASOUND_GET_PARAM_NAME(AddressInput));
			ensureAlways(bDataEdgeAdded);

			auto IsEdgeConnectedToCurrentInput = [&InputDestination](const FDataEdge& InEdge)
			{
				return (InEdge.From.Node == InputDestination.Node) && (InEdge.From.Vertex.VertexName == InputDestination.Vertex.VertexName);
			};

			// Cache previous connections from input node.
			TArray<FDataEdge> EdgesFromInput = InGraph.GetDataEdges().FilterByPredicate(IsEdgeConnectedToCurrentInput);
			// Remove existing connections from input node.
			InGraph.RemoveDataEdgeByPredicate(IsEdgeConnectedToCurrentInput);

			// Connect previous connections to receive node output.
			FOutputDataSource ReceiveOutputSource { *ReceiveNode, ReceiveNode->GetVertexInterface().GetOutputVertex(METASOUND_GET_PARAM_NAME(Output)) };
			for (const FDataEdge& Edge : EdgesFromInput)
			{
				FDataEdge NewEdge { ReceiveOutputSource, Edge.To };
				InGraph.AddDataEdge(FDataEdge{ ReceiveOutputSource, Edge.To });
			}

			// Connect input node to receive node.
			bDataEdgeAdded = InGraph.AddDataEdge(*InputDestination.Node, VertexKey, *ReceiveNode, METASOUND_GET_PARAM_NAME(DefaultDataInput));
			ensureAlways(bDataEdgeAdded);

			return true;
		}

		bool InjectReceiveNodes(FFrontendGraph& InGraph, const FReceiveNodeAddressFunction& InAddressFunction, const TSet<FVertexName>& InInputVertexNames)
		{
			using namespace Metasound::Frontend;
			bool bSuccess = true;

			const FInputDataDestinationCollection& InputDestinations = InGraph.GetInputDataDestinations();

			for (const FInputDataDestinationCollection::ElementType& Pair : InputDestinations)
			{
				const FInputDataDestination& InputDestination = Pair.Value;
				if (!ensure(InputDestination.Node != nullptr))
				{
					// should never contain null nodes for input destination.
					bSuccess = false;
					continue;
				}

				const FVertexName& VertexKey = InputDestination.Vertex.VertexName;
				if (InInputVertexNames.Contains(VertexKey))
				{
					const bool bInjectionSuccess = InjectReceiveNode(InGraph, InAddressFunction, InputDestination);
					bSuccess = bSuccess || bInjectionSuccess;
				}
			}

			return bSuccess;
		}
	}
}

#undef LOCTEXT_NAMESPACE
