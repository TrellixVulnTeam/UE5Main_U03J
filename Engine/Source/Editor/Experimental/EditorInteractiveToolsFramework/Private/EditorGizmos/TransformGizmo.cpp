// Copyright Epic Games, Inc. All Rights Reserved.

#include "EditorGizmos/TransformGizmo.h"
#include "BaseGizmos/AxisSources.h"
#include "BaseGizmos/GizmoElementGroup.h"
#include "BaseGizmos/GizmoElementShapes.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseGizmos/TransformSources.h"
#include "EditorGizmos/EditorAxisSources.h"
#include "EditorGizmos/EditorTransformGizmoSource.h"
#include "EditorGizmos/EditorTransformProxy.h"
#include "EditorGizmos/EditorParameterToTransformAdapters.h"
#include "Elements/Interfaces/TypedElementObjectInterface.h"
#include "Elements/Interfaces/TypedElementWorldInterface.h"
#include "Engine/CollisionProfile.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "EditorModeTools.h"
#include "UnrealEdGlobals.h"
#include "UnrealEngine.h"

#define LOCTEXT_NAMESPACE "UTransformGizmo"

void UTransformGizmo::SetDisallowNegativeScaling(bool bDisallow)
{
	bDisallowNegativeScaling = bDisallow;
}

void UTransformGizmo::Setup()
{
	UInteractiveGizmo::Setup();

	GizmoElementRoot = NewObject<UGizmoElementGroup>();
	GizmoElementRoot->SetConstantScale(true);

	UMaterial* AxisMaterialBase = GEngine->ArrowMaterial;

	AxisMaterialX = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialX->SetVectorParameterValue("GizmoColor", AxisColorX);

	AxisMaterialY = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialY->SetVectorParameterValue("GizmoColor", AxisColorY);

	AxisMaterialZ = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	AxisMaterialZ->SetVectorParameterValue("GizmoColor", AxisColorZ);

	GreyMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	GreyMaterial->SetVectorParameterValue("GizmoColor", GreyColor);

	WhiteMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	WhiteMaterial->SetVectorParameterValue("GizmoColor", WhiteColor);

	CurrentAxisMaterial = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	CurrentAxisMaterial->SetVectorParameterValue("GizmoColor", CurrentColor);

	OpaquePlaneMaterialXY = UMaterialInstanceDynamic::Create(AxisMaterialBase, NULL);
	OpaquePlaneMaterialXY->SetVectorParameterValue("GizmoColor", FLinearColor::White);

	TransparentVertexColorMaterial = (UMaterial*)StaticLoadObject(
		UMaterial::StaticClass(), NULL,
		TEXT("/Engine/EditorMaterials/WidgetVertexColorMaterial.WidgetVertexColorMaterial"), NULL, LOAD_None, NULL);

	GridMaterial = (UMaterial*)StaticLoadObject(
		UMaterial::StaticClass(), NULL,
		TEXT("/Engine/EditorMaterials/WidgetGridVertexColorMaterial_Ma.WidgetGridVertexColorMaterial_Ma"), NULL,
		LOAD_None, NULL);
	if (!GridMaterial)
	{
		GridMaterial = TransparentVertexColorMaterial;
	}

	GizmoElementRoot->SetHoverMaterial(CurrentAxisMaterial);
	GizmoElementRoot->SetInteractMaterial(CurrentAxisMaterial);
}


void UTransformGizmo::Shutdown()
{
	ClearActiveTarget();
}

void UTransformGizmo::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bVisible && GizmoElementRoot && RenderAPI)
	{
		EToolContextCoordinateSystem Space = EToolContextCoordinateSystem::World;
		float Scale = 1.0f;

		if (TransformSource)
		{
			Space = TransformSource->GetGizmoCoordSystemSpace();
			Scale = TransformSource->GetGizmoScale();
		}

		FTransform LocalToWorldTransform = ActiveTarget->GetTransform();
		if (Space == EToolContextCoordinateSystem::World)
		{
			LocalToWorldTransform.SetRotation(FQuat::Identity);
		}
		LocalToWorldTransform.SetScale3D(FVector(Scale, Scale, Scale));

		UGizmoElementBase::FRenderTraversalState RenderState;
		RenderState.LocalToWorldTransform = LocalToWorldTransform;

		GizmoElementRoot->Render(RenderAPI, RenderState);
	}
}

void UTransformGizmo::UpdateMode()
{
	if (TransformSource && TransformSource->GetVisible())
	{
		EGizmoTransformMode NewMode = TransformSource->GetGizmoMode();
		EAxisList::Type NewAxisToDraw = TransformSource->GetGizmoAxisToDraw(NewMode);

		if (NewMode != CurrentMode)
		{
			EnableMode(CurrentMode, EAxisList::None);
			EnableMode(NewMode, NewAxisToDraw);

			CurrentMode = NewMode;
			CurrentAxisToDraw = NewAxisToDraw;
		}
		else if (NewAxisToDraw != CurrentAxisToDraw)
		{
			EnableMode(CurrentMode, NewAxisToDraw);
			CurrentAxisToDraw = NewAxisToDraw;
		}
	}
	else
	{
		EnableMode(CurrentMode, EAxisList::None);
		CurrentMode = EGizmoTransformMode::None;
	}
}

void UTransformGizmo::EnableMode(EGizmoTransformMode InMode, EAxisList::Type InAxisListToDraw)
{
	if (InMode == EGizmoTransformMode::Translate)
	{
		EnableTranslate(InAxisListToDraw);
	}
	else if (InMode == EGizmoTransformMode::Rotate)
	{
		EnableRotate(InAxisListToDraw);
	}
	else if (InMode == EGizmoTransformMode::Scale)
	{
		EnableScale(InAxisListToDraw);
	}
}

void UTransformGizmo::EnableTranslate(EAxisList::Type InAxisListToDraw)
{
	check(GizmoElementRoot);

	const bool bEnableX = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::X);
	const bool bEnableY = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Y);
	const bool bEnableZ = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Z);
	const bool bEnableAny = bEnableX || bEnableY || bEnableZ;

	if (bEnableX && TranslateXAxisElement == nullptr)
	{
		TranslateXAxisElement = MakeTranslateAxis(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), AxisMaterialX);
		GizmoElementRoot->Add(TranslateXAxisElement);
	}

	if (bEnableY && TranslateYAxisElement == nullptr)
	{
		TranslateYAxisElement = MakeTranslateAxis(FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), AxisMaterialY);
		GizmoElementRoot->Add(TranslateYAxisElement);
	}

	if (bEnableZ && TranslateZAxisElement == nullptr)
	{
		TranslateZAxisElement = MakeTranslateAxis(FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f), AxisMaterialZ);
		GizmoElementRoot->Add(TranslateZAxisElement);
	}

	if (bEnableAny && TranslateScreenSpaceElement == nullptr)
	{
		TranslateScreenSpaceElement = MakeTranslateScreenSpaceHandle();
		GizmoElementRoot->Add(TranslateScreenSpaceElement);
	}

	if (TranslateXAxisElement)
	{
		TranslateXAxisElement->SetEnabled(bEnableX);
	}

	if (TranslateYAxisElement)
	{
		TranslateYAxisElement->SetEnabled(bEnableY);
	}

	if (TranslateZAxisElement)
	{
		TranslateZAxisElement->SetEnabled(bEnableZ);
	}

	if (TranslateScreenSpaceElement)
	{
		TranslateScreenSpaceElement->SetEnabled(bEnableAny);
	}

	EnablePlanarObjects(bEnableX, bEnableY, bEnableZ);
}

void UTransformGizmo::EnablePlanarObjects(bool bEnableX, bool bEnableY, bool bEnableZ)
{
	check(GizmoElementRoot);

	const bool bEnableXY = bEnableX && bEnableY;
	const bool bEnableYZ = bEnableY && bEnableZ;
	const bool bEnableXZ = bEnableX && bEnableZ;

	const FVector XAxis(1.0f, 0.0f, 0.0f);
	const FVector YAxis(0.0f, 1.0f, 0.0f);
	const FVector ZAxis(0.0f, 0.0f, 1.0f);

	if (bEnableXY && PlanarXYElement == nullptr)
	{
		PlanarXYElement = MakePlanarHandle(XAxis, YAxis, ZAxis, TransparentVertexColorMaterial, AxisColorZ);
		GizmoElementRoot->Add(PlanarXYElement);
	}

	if (bEnableYZ && PlanarYZElement == nullptr)
	{
		PlanarYZElement = MakePlanarHandle(YAxis, ZAxis, XAxis, TransparentVertexColorMaterial, AxisColorX);
		GizmoElementRoot->Add(PlanarYZElement);
	}

	if (bEnableXZ && PlanarXZElement == nullptr)
	{
		PlanarXZElement = MakePlanarHandle(ZAxis, XAxis, YAxis, TransparentVertexColorMaterial, AxisColorY);
		GizmoElementRoot->Add(PlanarXZElement);
	}

	if (PlanarXYElement)
	{
		PlanarXYElement->SetEnabled(bEnableXY);
	}

	if (PlanarYZElement)
	{
		PlanarYZElement->SetEnabled(bEnableYZ);
	}

	if (PlanarXZElement)
	{
		PlanarXZElement->SetEnabled(bEnableXZ);
	}
}

void UTransformGizmo::EnableRotate(EAxisList::Type InAxisListToDraw)
{
	const bool bEnableX = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::X);
	const bool bEnableY = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Y);
	const bool bEnableZ = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Z);
	const bool bEnableAll = bEnableX && bEnableY && bEnableZ;

	const FVector XAxis(1.0f, 0.0f, 0.0f);
	const FVector YAxis(0.0f, 1.0f, 0.0f);
	const FVector ZAxis(0.0f, 0.0f, 1.0f);

	if (bEnableX && RotateXAxisElement == nullptr)
	{
		RotateXAxisElement = MakeRotateAxis(XAxis, YAxis, ZAxis, AxisMaterialX, CurrentAxisMaterial);
		GizmoElementRoot->Add(RotateXAxisElement);
	}

	if (bEnableY && RotateYAxisElement == nullptr)
	{
		RotateYAxisElement = MakeRotateAxis(YAxis, ZAxis, XAxis, AxisMaterialY, CurrentAxisMaterial);
		GizmoElementRoot->Add(RotateYAxisElement);
	}

	if (bEnableZ && RotateZAxisElement == nullptr)
	{
		RotateZAxisElement = MakeRotateAxis(ZAxis, XAxis, YAxis, AxisMaterialZ, CurrentAxisMaterial);
		GizmoElementRoot->Add(RotateZAxisElement);
	}

	if (bEnableAll)
	{
		if (RotateScreenSpaceElement == nullptr)
		{
			RotateScreenSpaceElement = MakeRotateCircleHandle(RotateScreenSpaceRadius, RotateScreenSpaceCircleColor, false);
			GizmoElementRoot->Add(RotateScreenSpaceElement);
		}

		if (RotateOuterCircleElement == nullptr)
		{
			RotateOuterCircleElement = MakeRotateCircleHandle(RotateOuterCircleRadius, RotateOuterCircleColor, false);
			GizmoElementRoot->Add(RotateOuterCircleElement);
		}

		if (RotateArcballOuterElement == nullptr)
		{
			RotateArcballOuterElement = MakeRotateCircleHandle(RotateArcballOuterRadius, RotateArcballCircleColor, false);
			GizmoElementRoot->Add(RotateArcballOuterElement);
		}

		if (RotateArcballInnerElement == nullptr)
		{
			RotateArcballInnerElement = MakeRotateCircleHandle(RotateArcballInnerRadius, RotateArcballCircleColor, true);
			GizmoElementRoot->Add(RotateArcballInnerElement);
		}
	}

	if (RotateXAxisElement)
	{
		RotateXAxisElement->SetEnabled(bEnableX);
	}

	if (RotateYAxisElement)
	{
		RotateYAxisElement->SetEnabled(bEnableY);
	}

	if (RotateZAxisElement)
	{
		RotateZAxisElement->SetEnabled(bEnableZ);
	}

	if (RotateScreenSpaceElement)
	{
		RotateScreenSpaceElement->SetEnabled(bEnableAll);
	}

	if (RotateOuterCircleElement)
	{
		RotateOuterCircleElement->SetEnabled(bEnableAll);
	}

	if (RotateArcballOuterElement)
	{
		RotateArcballOuterElement->SetEnabled(bEnableAll);
	}

	if (RotateArcballInnerElement)
	{ 
		RotateArcballInnerElement->SetEnabled(bEnableAll);
	}
}

void UTransformGizmo::EnableScale(EAxisList::Type InAxisListToDraw)
{
	check(GizmoElementRoot);

	bool bEnableX = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::X);
	bool bEnableY = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Y);
	bool bEnableZ = static_cast<uint8>(InAxisListToDraw) & static_cast<uint8>(EAxisList::Z);
	
	if (bEnableX && ScaleXAxisElement == nullptr)
	{
		ScaleXAxisElement = MakeScaleAxis(FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), AxisMaterialX);
		GizmoElementRoot->Add(ScaleXAxisElement);
	}

	if (bEnableY && ScaleYAxisElement == nullptr)
	{
		ScaleYAxisElement = MakeScaleAxis(FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f), AxisMaterialY);
		GizmoElementRoot->Add(ScaleYAxisElement);
	}

	if (bEnableZ && ScaleZAxisElement == nullptr)
	{
		ScaleZAxisElement = MakeScaleAxis(FVector(0.0f, 0.0f, 1.0f), FVector(1.0f, 0.0f, 0.0f), AxisMaterialZ);
		GizmoElementRoot->Add(ScaleZAxisElement);
	}

	if ((bEnableX || bEnableY || bEnableZ) && ScaleUniformElement == nullptr)
	{
		ScaleUniformElement = MakeUniformScaleHandle();
		GizmoElementRoot->Add(ScaleUniformElement);
	}

	if (ScaleXAxisElement)
	{
		ScaleXAxisElement->SetEnabled(bEnableX);
	}

	if (ScaleYAxisElement)
	{
		ScaleYAxisElement->SetEnabled(bEnableY);
	}

	if (ScaleZAxisElement)
	{
		ScaleZAxisElement->SetEnabled(bEnableZ);
	}

	if (ScaleUniformElement)
	{
		ScaleUniformElement->SetEnabled(bEnableX || bEnableY || bEnableZ);
	}

	EnablePlanarObjects(bEnableX, bEnableY, bEnableZ);
}

void UTransformGizmo::UpdateCameraAxisSource()
{
	FViewCameraState CameraState;
	GetGizmoManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
	if (CameraAxisSource != nullptr)
	{
		CameraAxisSource->Origin = ActiveTarget ? ActiveTarget->GetTransform().GetLocation() : FVector::ZeroVector;
		CameraAxisSource->Direction = -CameraState.Forward();
		CameraAxisSource->TangentX = CameraState.Right();
		CameraAxisSource->TangentY = CameraState.Up();
	}
}

void UTransformGizmo::Tick(float DeltaTime)
{
	UpdateMode();

	UpdateCameraAxisSource();
}

void UTransformGizmo::SetActiveTarget(UTransformProxy* Target, IToolContextTransactionProvider* TransactionProvider)
{
	if (ActiveTarget != nullptr)
	{
		ClearActiveTarget();
	}

	ActiveTarget = Target;

	// Set current mode to none, mode will be updated next Tick()
	CurrentMode = EGizmoTransformMode::None;

	if (!ActiveTarget)
	{
		return;
	}

	// This state target emits an explicit FChange that moves the GizmoActor root component during undo/redo.
	// It also opens/closes the Transaction that saves/restores the target object locations.
	if (TransactionProvider == nullptr)
	{
		TransactionProvider = GetGizmoManager();
	}

	CameraAxisSource = NewObject<UGizmoConstantFrameAxisSource>(this);
}


void UTransformGizmo::ReinitializeGizmoTransform(const FTransform& NewTransform)
{
	// @todo update gizmo objects here?

	// The underlying proxy has an existing way to reinitialize its transform without callbacks.
	TGuardValue<bool>(ActiveTarget->bSetPivotMode, true);
	ActiveTarget->SetTransform(NewTransform);
}


void UTransformGizmo::SetNewGizmoTransform(const FTransform& NewTransform)
{
	// @todo update gizmo objects here?

	check(ActiveTarget != nullptr);

	StateTarget->BeginUpdate();

	ActiveTarget->SetTransform(NewTransform);

	StateTarget->EndUpdate();
}


// @todo: This should either be named to "SetScale" or removed, since it can be done with ReinitializeGizmoTransform
void UTransformGizmo::SetNewChildScale(const FVector& NewChildScale)
{
	FTransform NewTransform = ActiveTarget->GetTransform();
	NewTransform.SetScale3D(NewChildScale);

	TGuardValue<bool>(ActiveTarget->bSetPivotMode, true);
	ActiveTarget->SetTransform(NewTransform);
}

void UTransformGizmo::SetVisibility(bool bVisibleIn)
{
	bVisible = bVisibleIn;
}

UGizmoElementArrow* UTransformGizmo::MakeTranslateAxis(const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial)
{
	UGizmoElementArrow* ArrowElement = NewObject<UGizmoElementArrow>();
	ArrowElement->SetHeadType(EGizmoElementArrowHeadType::Cone);
	ArrowElement->SetBase(InAxisDir * AxisLengthOffset);
	ArrowElement->SetDirection(InAxisDir);
	ArrowElement->SetSideDirection(InSideDir);
	ArrowElement->SetBodyLength(TranslateAxisLength);
	ArrowElement->SetBodyRadius(AxisRadius);
	ArrowElement->SetHeadLength(TranslateAxisConeHeight);
	ArrowElement->SetHeadRadius(TranslateAxisConeRadius);
	ArrowElement->SetNumSides(32);
	ArrowElement->SetMaterial(InMaterial);
	ArrowElement->SetViewDependentType(EGizmoElementViewDependentType::Axis);
	ArrowElement->SetViewDependentAxis(InAxisDir);
	return ArrowElement;
}

UGizmoElementArrow* UTransformGizmo::MakeScaleAxis(const FVector& InAxisDir, const FVector& InSideDir, UMaterialInterface* InMaterial)
{
	UGizmoElementArrow* ArrowElement = NewObject<UGizmoElementArrow>();
	ArrowElement->SetHeadType(EGizmoElementArrowHeadType::Cube);
	ArrowElement->SetBase(InAxisDir * AxisLengthOffset);
	ArrowElement->SetDirection(InAxisDir);
	ArrowElement->SetSideDirection(InSideDir);
	ArrowElement->SetBodyLength(TranslateAxisLength);
	ArrowElement->SetBodyRadius(AxisRadius);
	ArrowElement->SetHeadLength(ScaleAxisCubeDim);
	ArrowElement->SetNumSides(32);
	ArrowElement->SetMaterial(InMaterial);
	ArrowElement->SetViewDependentType(EGizmoElementViewDependentType::Axis);
	ArrowElement->SetViewDependentAxis(InAxisDir);
	return ArrowElement;
}

UGizmoElementBox* UTransformGizmo::MakeUniformScaleHandle()
{
	UGizmoElementBox* BoxElement = NewObject<UGizmoElementBox>();
	BoxElement->SetCenter(FVector::ZeroVector);
	BoxElement->SetUpDirection(FVector::UpVector);
	BoxElement->SetSideDirection(FVector::RightVector);
	BoxElement->SetDimensions(FVector(ScaleAxisCubeDim, ScaleAxisCubeDim, ScaleAxisCubeDim));
	BoxElement->SetMaterial(GreyMaterial);
	return BoxElement;
}

UGizmoElementRectangle* UTransformGizmo::MakePlanarHandle(const FVector& InUpDirection, const FVector& InSideDirection, const FVector& InPlaneNormal,
	UMaterialInterface* InMaterial, const FLinearColor& InVertexColor)
{
	FVector PlanarHandleCenter = (InUpDirection + InSideDirection) * PlanarHandleOffset;

	FColor LineColor = InVertexColor.ToFColor(false);
	FColor VertexColor = LineColor;
	VertexColor.A = LargeOuterAlpha;

	UGizmoElementRectangle* RectangleElement = NewObject<UGizmoElementRectangle>();
	RectangleElement->SetUpDirection(InUpDirection);
	RectangleElement->SetSideDirection(InSideDirection);
	RectangleElement->SetCenter(PlanarHandleCenter);
	RectangleElement->SetHeight(PlanarHandleSize);
	RectangleElement->SetWidth(PlanarHandleSize);
	RectangleElement->SetMaterial(InMaterial);
	RectangleElement->SetVertexColor(VertexColor);
	RectangleElement->SetLineColor(LineColor);
	RectangleElement->SetDrawLine(true);
	RectangleElement->SetDrawMesh(true);
	RectangleElement->SetHitMesh(true);
	RectangleElement->SetViewDependentType(EGizmoElementViewDependentType::Plane);
	RectangleElement->SetViewDependentAxis(InPlaneNormal);
	return RectangleElement;
}

UGizmoElementRectangle* UTransformGizmo::MakeTranslateScreenSpaceHandle()
{
	UGizmoElementRectangle* RectangleElement = NewObject<UGizmoElementRectangle>();
	RectangleElement->SetUpDirection(FVector::UpVector);
	RectangleElement->SetSideDirection(FVector::RightVector);
	RectangleElement->SetCenter(FVector::ZeroVector);
	RectangleElement->SetHeight(TranslateScreenSpaceHandleSize);
	RectangleElement->SetWidth(TranslateScreenSpaceHandleSize);
	RectangleElement->SetScreenSpace(true);
	RectangleElement->SetMaterial(TransparentVertexColorMaterial);
	RectangleElement->SetLineColor(ScreenSpaceColor);
	RectangleElement->SetHitMesh(true);
	RectangleElement->SetDrawMesh(false);
	RectangleElement->SetDrawLine(true);
	return RectangleElement;
}

UGizmoElementTorus* UTransformGizmo::MakeRotateAxis(const FVector& Normal, const FVector& TorusAxis0, const FVector& TorusAxis1,
	UMaterialInterface* InMaterial, UMaterialInterface* InCurrentMaterial)
{
	UGizmoElementTorus* RotateAxisElement = NewObject<UGizmoElementTorus>();
	RotateAxisElement->SetCenter(FVector::ZeroVector);
	RotateAxisElement->SetOuterRadius(UTransformGizmo::RotateAxisOuterRadius);
	RotateAxisElement->SetOuterSegments(UTransformGizmo::RotateAxisOuterSegments);
	RotateAxisElement->SetInnerRadius(UTransformGizmo::RotateAxisInnerRadius);
	RotateAxisElement->SetInnerSlices(UTransformGizmo::RotateAxisInnerSlices);
	RotateAxisElement->SetNormal(Normal);
	RotateAxisElement->SetBeginAxis(TorusAxis0);
	RotateAxisElement->SetPartial(true);
	RotateAxisElement->SetAngle(PI);
	RotateAxisElement->SetViewDependentType(EGizmoElementViewDependentType::Plane);
	RotateAxisElement->SetViewDependentAxis(Normal);
	RotateAxisElement->SetViewAlignType(EGizmoElementViewAlignType::Axial);
	RotateAxisElement->SetViewAlignAxis(Normal);
	RotateAxisElement->SetViewAlignNormal(TorusAxis1);
	RotateAxisElement->SetMaterial(InMaterial);
	return RotateAxisElement;
}

UGizmoElementCircle* UTransformGizmo::MakeRotateCircleHandle(float InRadius, const FLinearColor& InColor, float bFill)
{
	UGizmoElementCircle* CircleElement = NewObject<UGizmoElementCircle>();
	CircleElement->SetCenter(FVector::ZeroVector);
	CircleElement->SetRadius(InRadius);
	CircleElement->SetNormal(-FVector::ForwardVector);
	CircleElement->SetLineColor(InColor);
	CircleElement->SetViewAlignType(EGizmoElementViewAlignType::PointOnly);
	CircleElement->SetViewAlignNormal(-FVector::ForwardVector);

	if (bFill)
	{
		CircleElement->SetVertexColor(InColor.ToFColor(true));
		CircleElement->SetMaterial(WhiteMaterial);
	}
	else
	{
		CircleElement->SetDrawLine(true);
		CircleElement->SetHitLine(true);
		CircleElement->SetDrawMesh(false);
		CircleElement->SetHitMesh(false);
	}

	return CircleElement;
}


void UTransformGizmo::ClearActiveTarget()
{
	StateTarget = nullptr;
	ActiveTarget = nullptr;
}


bool UTransformGizmo::PositionSnapFunction(const FVector& WorldPosition, FVector& SnappedPositionOut) const
{
	SnappedPositionOut = WorldPosition;
#if 0
	// only snap if we want snapping obvs
	if (bSnapToWorldGrid == false)
	{
		return false;
	}

	// only snap to world grid when using world axes
	if (GetGizmoManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() != EToolContextCoordinateSystem::World)
	{
		return false;
	}

	FSceneSnapQueryRequest Request;
	Request.RequestType = ESceneSnapQueryType::Position;
	Request.TargetTypes = ESceneSnapQueryTargetType::Grid;
	Request.Position = WorldPosition;
	if ( bGridSizeIsExplicit )
	{
		Request.GridSize = ExplicitGridSize;
	}
	TArray<FSceneSnapQueryResult> Results;
	if (GetGizmoManager()->GetContextQueriesAPI()->ExecuteSceneSnapQuery(Request, Results))
	{
		SnappedPositionOut = Results[0].Position;
		return true;
	};
#endif
	return false;
}


FQuat UTransformGizmo::RotationSnapFunction(const FQuat& DeltaRotation) const
{
	FQuat SnappedDeltaRotation = DeltaRotation;
#if 0
	// only snap if we want snapping 
	if (bSnapToWorldRotGrid)
	{
		FSceneSnapQueryRequest Request;
		Request.RequestType   = ESceneSnapQueryType::Rotation;
		Request.TargetTypes   = ESceneSnapQueryTargetType::Grid;
		Request.DeltaRotation = DeltaRotation;
		if ( bRotationGridSizeIsExplicit )
		{
			Request.RotGridSize = ExplicitRotationGridSize;
		}
		TArray<FSceneSnapQueryResult> Results;
		if (GetGizmoManager()->GetContextQueriesAPI()->ExecuteSceneSnapQuery(Request, Results))
		{
			SnappedDeltaRotation = Results[0].DeltaRotation;
		};
	}
#endif	
	return SnappedDeltaRotation;
}

#undef LOCTEXT_NAMESPACE
