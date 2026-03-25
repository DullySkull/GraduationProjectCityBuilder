#include "RoadBuildingTool.h"
#include "InteractiveToolManager.h"
#include "ToolContextInterfaces.h"
#include "SceneManagement.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Engine/World.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SplineComponent.h"
#include "EngineUtils.h"

ARoadSegmentActor::ARoadSegmentActor()
{
	SplineComponent = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = SplineComponent;
	SplineComponent->ClearSplinePoints(false);
}

void ARoadSegmentActor::AddPoint(const FVector& WorldPoint)
{
	SplinePoints.Add(WorldPoint);

	FSplinePoint SP;
	SP.InputKey = static_cast<float>(SplinePoints.Num() - 1);
	SP.Position = WorldPoint;
	SP.Type = ESplinePointType::Linear;
	SplineComponent->AddPoint(SP, true);
}

void ARoadSegmentActor::UpdateLastPoint(const FVector& WorldPoint)
{
	if (SplinePoints.Num() == 0)
	{
		return;
	}

	const int32 Last = SplinePoints.Num() - 1;
	SplinePoints[Last] = WorldPoint;
	SplineComponent->SetLocationAtSplinePoint(Last, WorldPoint, ESplineCoordinateSpace::World, true);
}

void ARoadSegmentActor::FinaliseRoad()
{
	Tags.AddUnique(FName("Road"));
	SplineComponent->UpdateSpline();
}

ARoadIntersectionActor::ARoadIntersectionActor()
{
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = MeshComponent;
}

void ARoadIntersectionActor::SetMesh(UStaticMesh* Mesh)
{
	if (MeshComponent && Mesh)
	{
		MeshComponent->SetStaticMesh(Mesh);
	}
}

void URoadBuildingTool::Setup()
{
	UInteractiveTool::Setup();

	Properties = NewObject<URoadBuildingToolProperties>(this);
	AddToolPropertySource(Properties);

	USingleClickInputBehavior* ClickBehavior = NewObject<USingleClickInputBehavior>(this);
	ClickBehavior->Initialize(this);
	AddInputBehavior(ClickBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	bIsDrawing = false;
	CurrentMousePoint = FVector::ZeroVector;
	ActiveRoad = nullptr;
	LastClickTime = 0.0;
}

void URoadBuildingTool::Shutdown(EToolShutdownType ShutdownType)
{
	CancelCurrentRoad();
	UInteractiveTool::Shutdown(ShutdownType);
}

void URoadBuildingTool::OnTick(float DeltaTime)
{
}

void URoadBuildingTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (bIsDrawing && ActiveRoad)
	{
		DrawPreviewLine(RenderAPI);
	}
}

void URoadBuildingTool::DrawPreviewLine(IToolsContextRenderAPI* RenderAPI) const
{
	if (!RenderAPI || !ActiveRoad)
	{
		return;
	}

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	const TArray<FVector>& Pts = ActiveRoad->SplinePoints;
	if (Pts.Num() == 0)
	{
		return;
	}

	const FVector AdjustedCursor = CurrentMousePoint + FVector(0, 0, Properties->HeightOffset);

	for (int32 i = 0; i < Pts.Num() - 1; ++i)
	{
		PDI->DrawLine(Pts[i], Pts[i + 1], FLinearColor::Green, SDPG_Foreground, 10.f);
	}

	PDI->DrawLine(Pts.Last(), AdjustedCursor, FLinearColor::Yellow, SDPG_Foreground, 10.f);

	const int32 Near = FindNearbyNode(AdjustedCursor);
	if (Near != INDEX_NONE)
	{
		DrawWireSphere(PDI, RoadNodes[Near].Location, FLinearColor::Red, Properties->SnapRadius, 16, SDPG_Foreground);
	}
}

FInputRayHit URoadBuildingTool::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	return FInputRayHit(0.f);
}

void URoadBuildingTool::OnClicked(const FInputDeviceRay& ClickPos)
{
	const double Now = FPlatformTime::Seconds();
	const bool bDoubleClick = (Now - LastClickTime) < DoubleClickThresholdSec;
	LastClickTime = Now;

	if (bDoubleClick)
	{
		OnDoubleClicked(ClickPos);
		return;
	}

	FVector HitLocation = TraceToFloor(ClickPos.WorldRay);
	if (HitLocation.IsZero())
	{
		return;
	}

	HitLocation.Z += Properties->HeightOffset;
	CommitNode(HitLocation);
}

void URoadBuildingTool::OnDoubleClicked(const FInputDeviceRay& ClickPos)
{
	if (!bIsDrawing || !ActiveRoad)
	{
		return;
	}

	if (ActiveRoad->SplinePoints.Num() >= 2)
	{
		ActiveRoad->FinaliseRoad();
	}
	else
	{
		ActiveRoad->Destroy();
		AllRoads.RemoveSingle(ActiveRoad);
	}

	ActiveRoad = nullptr;
	bIsDrawing = false;
}

FInputRayHit URoadBuildingTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	return FInputRayHit(0.f);
}

void URoadBuildingTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	CurrentMousePoint = TraceToFloor(DevicePos.WorldRay);
}

bool URoadBuildingTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	CurrentMousePoint = TraceToFloor(DevicePos.WorldRay);
	return true;
}

void URoadBuildingTool::OnEndHover()
{
}

void URoadBuildingTool::CancelCurrentRoad()
{
	if (bIsDrawing && ActiveRoad)
	{
		ActiveRoad->Destroy();
		AllRoads.RemoveSingle(ActiveRoad);
		ActiveRoad = nullptr;
	}

	bIsDrawing = false;
}

void URoadBuildingTool::AdjustHeight(float Delta)
{
	if (Properties)
	{
		Properties->HeightOffset += Delta;
	}
}

void URoadBuildingTool::CommitNode(const FVector& WorldLocation)
{
	UWorld* World = GetToolManager()->GetWorld();
	if (!World)
	{
		return;
	}

	if (!bIsDrawing)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ActiveRoad = World->SpawnActor<ARoadSegmentActor>(ARoadSegmentActor::StaticClass(), FTransform::Identity,
		                                                  Params);
		if (!ActiveRoad)
		{
			return;
		}

		AllRoads.Add(ActiveRoad);

		const int32 Existing = FindNearbyNode(WorldLocation);
		const FVector StartPt = (Existing != INDEX_NONE) ? RoadNodes[Existing].Location : WorldLocation;

		ActiveRoad->AddPoint(StartPt);
		bIsDrawing = true;
	}
	else
	{
		const int32 NearNode = FindNearbyNode(WorldLocation);
		const FVector NewPt = (NearNode != INDEX_NONE) ? RoadNodes[NearNode].Location : WorldLocation;
		const FVector PrevPt = ActiveRoad->SplinePoints.Last();

		const int32 NodeA = GetOrCreateNode(PrevPt);
		const int32 NodeB = GetOrCreateNode(NewPt);

		if (NodeA != NodeB)
		{
			ConnectNodes(NodeA, NodeB);
			SpawnRoadSegmentMesh(PrevPt, NewPt);
		}

		CheckSplineIntersections(NewPt, NodeB);

		ActiveRoad->AddPoint(NewPt);
		CurrentMousePoint = NewPt;

		if (NearNode != INDEX_NONE)
		{
			ActiveRoad->FinaliseRoad();
			ActiveRoad = nullptr;
			bIsDrawing = false;
		}
	}
}

void URoadBuildingTool::CheckSplineIntersections(const FVector& NewPoint, int32 NewNodeIndex)
{
	UWorld* World = GetToolManager()->GetWorld();
	if (!World)
	{
		return;
	}

	const float SnapSq = FMath::Square(Properties->SnapRadius);

	for (TActorIterator<ARoadSegmentActor> It(World); It; ++It)
	{
		ARoadSegmentActor* OtherRoad = *It;

		if (!OtherRoad || OtherRoad == ActiveRoad)
		{
			continue;
		}

		if (!OtherRoad->ActorHasTag(FName("Road")))
		{
			continue;
		}

		USplineComponent* Spline = OtherRoad->SplineComponent;
		if (!Spline)
		{
			continue;
		}

		const int32 NumPoints = Spline->GetNumberOfSplinePoints();
		for (int32 i = 0; i < NumPoints; ++i)
		{
			const FVector SplinePt = Spline->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);

			if (FVector::DistSquared(SplinePt, NewPoint) <= SnapSq)
			{
				const int32 ExistingNode = GetOrCreateNode(SplinePt);

				if (ExistingNode != NewNodeIndex)
				{
					ConnectNodes(NewNodeIndex, ExistingNode);
				}
				break;
			}
		}

		const float SplineLength = Spline->GetSplineLength();
		if (SplineLength <= 0.f)
		{
			continue;
		}

		const float StepSize = FMath::Max(Properties->SnapRadius * 0.5f, 1.f);
		const int32 Steps = FMath::Max(1, FMath::FloorToInt(SplineLength / StepSize));

		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			const float Distance = (static_cast<float>(Step) / static_cast<float>(Steps)) * SplineLength;
			const FVector ClosestPt = Spline->GetLocationAtDistanceAlongSpline(Distance, ESplineCoordinateSpace::World);

			if (FVector::DistSquared(ClosestPt, NewPoint) <= SnapSq)
			{
				const int32 MidNode = GetOrCreateNode(ClosestPt);

				if (MidNode != NewNodeIndex)
				{
					ConnectNodes(NewNodeIndex, MidNode);
				}
				break;
			}
		}
	}
}

FVector URoadBuildingTool::TraceToFloor(const FRay& WorldRay) const
{
	UWorld* World = GetToolManager()->GetWorld();
	if (!World)
	{
		return FVector::ZeroVector;
	}

	const FVector Start = WorldRay.Origin;
	const FVector End = Start + WorldRay.Direction * 100000.f;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.bTraceComplex = true;

	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		return Hit.Location;
	}

	return FVector::ZeroVector;
}

int32 URoadBuildingTool::FindNearbyNode(const FVector& Location) const
{
	const float SnapSq = FMath::Square(Properties ? Properties->SnapRadius : 50.f);

	for (int32 i = 0; i < RoadNodes.Num(); ++i)
	{
		if (FVector::DistSquared(RoadNodes[i].Location, Location) <= SnapSq)
		{
			return i;
		}
	}

	return INDEX_NONE;
}

int32 URoadBuildingTool::GetOrCreateNode(const FVector& Location)
{
	const int32 Existing = FindNearbyNode(Location);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	FRoadNode Node;
	Node.Location = Location;
	RoadNodes.Add(Node);
	return RoadNodes.Num() - 1;
}

void URoadBuildingTool::ConnectNodes(int32 A, int32 B)
{
	if (!RoadNodes.IsValidIndex(A) || !RoadNodes.IsValidIndex(B) || A == B)
	{
		return;
	}

	if (!RoadNodes[A].ConnectedNodes.Contains(B))
	{
		RoadNodes[A].ConnectedNodes.Add(B);
	}

	if (!RoadNodes[B].ConnectedNodes.Contains(A))
	{
		RoadNodes[B].ConnectedNodes.Add(A);
	}

	UpdateIntersectionActor(A);
	UpdateIntersectionActor(B);
}

int32 URoadBuildingTool::CountBits(uint8 Mask) const
{
	int32 Count = 0;
	for (int32 i = 0; i < 4; ++i)
	{
		if ((Mask & (1 << i)) != 0)
		{
			++Count;
		}
	}
	return Count;
}

uint8 URoadBuildingTool::BuildDirectionMask(int32 NodeIndex) const
{
	if (!RoadNodes.IsValidIndex(NodeIndex))
	{
		return Dir_None;
	}

	uint8 Mask = Dir_None;
	const FRoadNode& Node = RoadNodes[NodeIndex];

	for (const int32 ConnectedIndex : Node.ConnectedNodes)
	{
		if (!RoadNodes.IsValidIndex(ConnectedIndex))
		{
			continue;
		}

		const FVector Delta = RoadNodes[ConnectedIndex].Location - Node.Location;
		if (Delta.IsNearlyZero())
		{
			continue;
		}

		if (FMath::Abs(Delta.X) >= FMath::Abs(Delta.Y))
		{
			Mask |= (Delta.X >= 0.f) ? Dir_North : Dir_South;
		}
		else
		{
			Mask |= (Delta.Y >= 0.f) ? Dir_East : Dir_West;
		}
	}

	return Mask;
}

bool URoadBuildingTool::IsStraightMask(uint8 Mask) const
{
	const bool bNS = ((Mask & Dir_North) && (Mask & Dir_South) && !(Mask & Dir_East) && !(Mask & Dir_West));
	const bool bEW = ((Mask & Dir_East) && (Mask & Dir_West) && !(Mask & Dir_North) && !(Mask & Dir_South));
	return bNS || bEW;
}

bool URoadBuildingTool::IsValidCornerMask(uint8 Mask) const
{
	const bool bNE = ((Mask & Dir_North) && (Mask & Dir_East) && !(Mask & Dir_South) && !(Mask & Dir_West));
	const bool bES = ((Mask & Dir_East) && (Mask & Dir_South) && !(Mask & Dir_North) && !(Mask & Dir_West));
	const bool bSW = ((Mask & Dir_South) && (Mask & Dir_West) && !(Mask & Dir_North) && !(Mask & Dir_East));
	const bool bWN = ((Mask & Dir_West) && (Mask & Dir_North) && !(Mask & Dir_East) && !(Mask & Dir_South));

	if (!(bNE || bES || bSW || bWN))
	{
		return false;
	}

	return true;
}

ERoadIntersectionType URoadBuildingTool::ClassifyNodeFromMask(int32 NodeIndex, uint8 Mask) const
{
	if (!RoadNodes.IsValidIndex(NodeIndex))
	{
		return ERoadIntersectionType::None;
	}

	const FRoadNode& Node = RoadNodes[NodeIndex];
	const int32 ConnectionCount = Node.ConnectedNodes.Num();
	const int32 BitCount = CountBits(Mask);

	if (ConnectionCount <= 0 || BitCount <= 0)
	{
		return ERoadIntersectionType::None;
	}

	if (ConnectionCount == 1 || BitCount == 1)
	{
		return ERoadIntersectionType::DeadEnd;
	}

	if (ConnectionCount == 2)
	{
		if (Node.ConnectedNodes.Num() < 2)
		{
			return ERoadIntersectionType::None;
		}

		const FVector DirA = (RoadNodes[Node.ConnectedNodes[0]].Location - Node.Location).GetSafeNormal2D();
		const FVector DirB = (RoadNodes[Node.ConnectedNodes[1]].Location - Node.Location).GetSafeNormal2D();
		const float Dot = FVector::DotProduct(DirA, DirB);

		if (Dot <= Properties->StraightOppositeDotThreshold)
		{
			return ERoadIntersectionType::None;
		}

		if (FMath::Abs(Dot) <= Properties->CornerPerpendicularDotTolerance && IsValidCornerMask(Mask))
		{
			return ERoadIntersectionType::Corner;
		}

		return ERoadIntersectionType::None;
	}

	if (ConnectionCount == 3 || BitCount == 3)
	{
		return ERoadIntersectionType::TJunction;
	}

	if (ConnectionCount >= 4 || BitCount >= 4)
	{
		return ERoadIntersectionType::Crossroads;
	}

	return ERoadIntersectionType::None;
}

UStaticMesh* URoadBuildingTool::MeshForType(ERoadIntersectionType Type) const
{
	if (!Properties)
	{
		return nullptr;
	}

	switch (Type)
	{
	case ERoadIntersectionType::DeadEnd: return Properties->DeadEndMesh;
	case ERoadIntersectionType::Corner: return Properties->CornerMesh;
	case ERoadIntersectionType::TJunction: return Properties->TJunctionMesh;
	case ERoadIntersectionType::Crossroads: return Properties->CrossroadsMesh;
	default: return nullptr;
	}
}

float URoadBuildingTool::RotationFromMask(uint8 Mask, ERoadIntersectionType Type) const
{
	switch (Type)
	{
	case ERoadIntersectionType::DeadEnd:
		if (Mask == Dir_North) return -90.f;
		if (Mask == Dir_East) return 180.f;
		if (Mask == Dir_South) return 90.f;
		if (Mask == Dir_West) return 0.f;
		break;

	case ERoadIntersectionType::Corner:
		if (Mask == (Dir_North | Dir_East)) return 0.f; // Corner_NE
		if (Mask == (Dir_East | Dir_South)) return -90.f; // Corner_ES
		if (Mask == (Dir_South | Dir_West)) return 180.f; // Corner_SW
		if (Mask == (Dir_West | Dir_North)) return 90.f; // Corner_WN
		break;

	case ERoadIntersectionType::TJunction:
		if (Mask == (Dir_North | Dir_West | Dir_East)) return 90.f; // T_NWE
		if (Mask == (Dir_North | Dir_East | Dir_South)) return 0.f; // T_NES
		if (Mask == (Dir_East | Dir_South | Dir_West)) return -90.f; // T_ESW
		if (Mask == (Dir_South | Dir_West | Dir_North)) return 180.f; // T_SWN
		break;

	case ERoadIntersectionType::Crossroads:
		return 0.f;

	default:
		break;
	}

	return 0.f;
}

void URoadBuildingTool::UpdateIntersectionActor(int32 NodeIndex)
{
	if (!RoadNodes.IsValidIndex(NodeIndex))
	{
		return;
	}

	FRoadNode& Node = RoadNodes[NodeIndex];

	UWorld* World = GetToolManager()->GetWorld();
	if (!World)
	{
		return;
	}

	if (Node.IntersectionActor)
	{
		Node.IntersectionActor->Destroy();
		Node.IntersectionActor = nullptr;
	}

	const uint8 Mask = BuildDirectionMask(NodeIndex);
	const ERoadIntersectionType Type = ClassifyNodeFromMask(NodeIndex, Mask);
	if (Type == ERoadIntersectionType::None)
	{
		return;
	}

	UStaticMesh* Mesh = MeshForType(Type);
	if (!Mesh)
	{
		return;
	}

	const float Yaw = RotationFromMask(Mask, Type);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ARoadIntersectionActor* Actor = World->SpawnActor<ARoadIntersectionActor>(
		ARoadIntersectionActor::StaticClass(),
		Node.Location,
		FRotator(0.f, Yaw, 0.f),
		Params);

	if (Actor)
	{
		Actor->SetMesh(Mesh);
		Actor->IntersectionType = Type;
		Actor->ConnectionCount = Node.ConnectedNodes.Num();

		if (Properties)
		{
			Actor->MeshComponent->SetWorldScale3D(Properties->MeshScale);
		}

		Node.IntersectionActor = Actor;
	}
}

void URoadBuildingTool::SpawnRoadSegmentMesh(const FVector& A, const FVector& B)
{
	if (!Properties || !Properties->StraightRoadMesh)
	{
		return;
	}

	UWorld* World = GetToolManager()->GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Mid = (A + B) * 0.5f;
	const FVector Dir = (B - A).GetSafeNormal();
	const FRotator Rot = Dir.Rotation();
	const float Length = FVector::Dist(A, B);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AStaticMeshActor* RoadMesh = World->SpawnActor<AStaticMeshActor>(
		AStaticMeshActor::StaticClass(),
		Mid,
		Rot,
		Params);

	if (RoadMesh)
	{
		UStaticMeshComponent* SMC = RoadMesh->GetStaticMeshComponent();
		SMC->SetStaticMesh(Properties->StraightRoadMesh);
		SMC->SetMobility(EComponentMobility::Movable);

		const FBox MeshBounds = Properties->StraightRoadMesh->GetBoundingBox();
		const float MeshNativeLength = FMath::Max(MeshBounds.GetSize().X, 1.f);

		FVector Scale = Properties->MeshScale;
		Scale.X = (Length / MeshNativeLength) * Properties->MeshScale.X;
		SMC->SetWorldScale3D(Scale);

		RoadMesh->Tags.AddUnique(FName("Road"));
		RoadMesh->SetActorLabel(TEXT("RoadSegment"));
	}
}
