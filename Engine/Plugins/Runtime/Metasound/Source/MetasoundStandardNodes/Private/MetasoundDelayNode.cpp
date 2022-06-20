// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/Text.h"
#include "MetasoundExecutableOperator.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundParamHelper.h"
#include "MetasoundPrimitives.h"
#include "MetasoundStandardNodesNames.h"
#include "MetasoundTrigger.h"
#include "MetasoundTime.h"
#include "MetasoundAudioBuffer.h"
#include "DSP/Delay.h"
#include "MetasoundStandardNodesCategories.h"
#include "MetasoundFacade.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	namespace Delay
	{
		METASOUND_PARAM(InParamAudioInput, "In", "Audio input.")
		METASOUND_PARAM(InParamDelayTime, "Delay Time", "The amount of time to delay the audio, in seconds.")
		METASOUND_PARAM(InParamDryLevel, "Dry Level", "The dry level of the delay.")
		METASOUND_PARAM(InParamWetLevel, "Wet Level", "The wet level of the delay.")
		METASOUND_PARAM(InParamFeedbackAmount, "Feedback", "Feedback amount.")
		METASOUND_PARAM(OutParamAudio, "Out", "Audio output.")

		// TODO: make this a static vertex
		static float MaxDelaySeconds = 5.0f;
	}

	class FDelayOperator : public TExecutableOperator<FDelayOperator>
	{
	public:

		static const FNodeClassMetadata& GetNodeInfo();
		static const FVertexInterface& GetVertexInterface();
		static TUniquePtr<IOperator> CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors);

		FDelayOperator(const FOperatorSettings& InSettings, 
			const FAudioBufferReadRef& InAudioInput, 
			const FTimeReadRef& InDelayTime, 
			const FFloatReadRef& InDryLevel, 
			const FFloatReadRef& InWetLevel, 
			const FFloatReadRef& InFeedback);

		virtual FDataReferenceCollection GetInputs() const override;
		virtual FDataReferenceCollection GetOutputs() const override;
		void Execute();

	private:
		float GetInputDelayTimeMsec() const;

		// The input audio buffer
		FAudioBufferReadRef AudioInput;

		// The amount of delay time
		FTimeReadRef DelayTime;

		// The dry level
		FFloatReadRef DryLevel;

		// The wet level
		FFloatReadRef WetLevel;

		// The feedback amount
		FFloatReadRef Feedback;

		// The audio output
		FAudioBufferWriteRef AudioOutput;

		// The internal delay buffer
		Audio::FDelay DelayBuffer;

		// The previous delay time
		float PrevDelayTimeMsec;

		// Feedback sample
		float FeedbackSample;
	};

	FDelayOperator::FDelayOperator(const FOperatorSettings& InSettings,
		const FAudioBufferReadRef& InAudioInput,
		const FTimeReadRef& InDelayTime,
		const FFloatReadRef& InDryLevel,
		const FFloatReadRef& InWetLevel,
		const FFloatReadRef& InFeedback)

		: AudioInput(InAudioInput)
		, DelayTime(InDelayTime)
		, DryLevel(InDryLevel)
		, WetLevel(InWetLevel)
		, Feedback(InFeedback)
		, AudioOutput(FAudioBufferWriteRef::CreateNew(InSettings))
		, PrevDelayTimeMsec(GetInputDelayTimeMsec())
		, FeedbackSample(0.0f)
	{
		DelayBuffer.Init(InSettings.GetSampleRate(), Delay::MaxDelaySeconds);
		DelayBuffer.SetDelayMsec(PrevDelayTimeMsec);
	}

	FDataReferenceCollection FDelayOperator::GetInputs() const
	{
		using namespace Delay;

		FDataReferenceCollection InputDataReferences;
		InputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(InParamAudioInput), FAudioBufferReadRef(AudioInput));
		InputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(InParamDelayTime), FTimeReadRef(DelayTime));
		InputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(InParamDryLevel), FFloatReadRef(DryLevel));
		InputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(InParamWetLevel), FFloatReadRef(WetLevel));
		InputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(InParamFeedbackAmount), FFloatReadRef(Feedback));

		return InputDataReferences;
	}

	FDataReferenceCollection FDelayOperator::GetOutputs() const
	{
		using namespace Delay;

		FDataReferenceCollection OutputDataReferences;
		OutputDataReferences.AddDataReadReference(METASOUND_GET_PARAM_NAME(OutParamAudio), FAudioBufferReadRef(AudioOutput));
		return OutputDataReferences;
	}

	float FDelayOperator::GetInputDelayTimeMsec() const
	{
		// Clamp the delay time to the max delay allowed
		return 1000.0f * FMath::Clamp((float)DelayTime->GetSeconds(), 0.0f, Delay::MaxDelaySeconds);
	}

	void FDelayOperator::Execute()
	{
		// Get clamped delay time
		float CurrentInputDelayTime = GetInputDelayTimeMsec();

		// Check to see if our delay amount has changed
		if (!FMath::IsNearlyEqual(PrevDelayTimeMsec, CurrentInputDelayTime))
		{
			PrevDelayTimeMsec = CurrentInputDelayTime;
			DelayBuffer.SetEasedDelayMsec(PrevDelayTimeMsec);
		}

		const float* InputAudio = AudioInput->GetData();

		float* OutputAudio = AudioOutput->GetData();
		int32 NumFrames = AudioInput->Num();

		// Clamp the feedback amount to make sure it's bounded. Clamp to a number slightly less than 1.0
		float FeedbackAmount = FMath::Clamp(*Feedback, 0.0f, 1.0f - SMALL_NUMBER);
		float CurrentDryLevel = FMath::Clamp(*DryLevel, 0.0f, 1.0f);
		float CurrentWetLevel = FMath::Clamp(*WetLevel, 0.0f, 1.0f);

		if (FMath::IsNearlyZero(FeedbackAmount))
		{
			FeedbackSample = 0.0f;

			for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
			{
				OutputAudio[FrameIndex] = CurrentWetLevel * DelayBuffer.ProcessAudioSample(InputAudio[FrameIndex]) + CurrentDryLevel * InputAudio[FrameIndex];
			}
		}
		else
		{
			// There is some amount of feedback so we do the feedback mixing
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
			{
				OutputAudio[FrameIndex] = CurrentWetLevel * DelayBuffer.ProcessAudioSample(InputAudio[FrameIndex] + FeedbackSample * FeedbackAmount) + CurrentDryLevel * InputAudio[FrameIndex];
				FeedbackSample = OutputAudio[FrameIndex];
			}
		}
	}

	const FVertexInterface& FDelayOperator::GetVertexInterface()
	{
		using namespace Delay;

		static const FVertexInterface Interface(
			FInputVertexInterface(
				TInputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamAudioInput)),
				TInputDataVertex<FTime>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamDelayTime), 1.0f),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamDryLevel), 0.0f),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamWetLevel), 1.0f),
				TInputDataVertex<float>(METASOUND_GET_PARAM_NAME_AND_METADATA(InParamFeedbackAmount), 0.0f)
			),
			FOutputVertexInterface(
				TOutputDataVertex<FAudioBuffer>(METASOUND_GET_PARAM_NAME_AND_METADATA(OutParamAudio))
			)
		);

		return Interface;
	}

	const FNodeClassMetadata& FDelayOperator::GetNodeInfo()
	{
		auto InitNodeInfo = []() -> FNodeClassMetadata
		{
			FNodeClassMetadata Info;
			Info.ClassName = { StandardNodes::Namespace, "Delay", StandardNodes::AudioVariant };
			Info.MajorVersion = 1;
			Info.MinorVersion = 1;
			Info.DisplayName = METASOUND_LOCTEXT("DelayNode_DisplayName", "Delay");
			Info.Description = METASOUND_LOCTEXT("DelayNode_Description", "Delays an audio buffer by the specified amount.");
			Info.Author = PluginAuthor;
			Info.PromptIfMissing = PluginNodeMissingPrompt;
			Info.DefaultInterface = GetVertexInterface();
			Info.CategoryHierarchy.Emplace(NodeCategories::Delays);
			return Info;
		};

		static const FNodeClassMetadata Info = InitNodeInfo();

		return Info;
	}

	TUniquePtr<IOperator> FDelayOperator::CreateOperator(const FCreateOperatorParams& InParams, FBuildErrorArray& OutErrors)
	{
		using namespace Delay; 

		const FDataReferenceCollection& InputCollection = InParams.InputDataReferences;
		const FInputVertexInterface& InputInterface = GetVertexInterface().GetInputInterface();

		FAudioBufferReadRef AudioIn = InputCollection.GetDataReadReferenceOrConstruct<FAudioBuffer>(METASOUND_GET_PARAM_NAME(InParamAudioInput), InParams.OperatorSettings);
		FTimeReadRef DelayTime = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<FTime>(InputInterface, METASOUND_GET_PARAM_NAME(InParamDelayTime), InParams.OperatorSettings);
		FFloatReadRef DryLevel = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InParamDryLevel), InParams.OperatorSettings);
		FFloatReadRef WetLevel = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InParamWetLevel), InParams.OperatorSettings);
		FFloatReadRef Feedback = InputCollection.GetDataReadReferenceOrConstructWithVertexDefault<float>(InputInterface, METASOUND_GET_PARAM_NAME(InParamFeedbackAmount), InParams.OperatorSettings);

		return MakeUnique<FDelayOperator>(InParams.OperatorSettings, AudioIn, DelayTime, DryLevel, WetLevel, Feedback);
	}

	class FDelayNode : public FNodeFacade
	{
	public:
		/**
		 * Constructor used by the Metasound Frontend.
		 */
		FDelayNode(const FNodeInitData& InitData)
			: FNodeFacade(InitData.InstanceName, InitData.InstanceID, TFacadeOperatorClass<FDelayOperator>())
		{
		}
	};


	METASOUND_REGISTER_NODE(FDelayNode)
}

#undef LOCTEXT_NAMESPACE
