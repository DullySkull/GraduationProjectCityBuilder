#include "CityVoronoiGenerator.h"

#include "Algo/Reverse.h"
#include "CompGeom/Delaunay2.h"
#include "Components/SplineComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "PCGComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "ZoneGraphSettings.h"
#include "ZoneShapeComponent.h"

namespace CityVoronoi
{
	static const FName BlockTag(TEXT("Block"));
	static const FName BuildingTag(TEXT("Block_Building"));
	static const FName ParkTag(TEXT("Block_Park"));
	static const FName ParkingTag(TEXT("Block_Parking"));
	static constexpr float LaneHeight = 25.0f; // just above the road surface

	static double SignedArea(const TArray<FVector2d>& Poly)
	{
		double Area = 0.0;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2d& A = Poly[i];
			const FVector2d& B = Poly[(i + 1) % Poly.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		return Area * 0.5;
	}

	static FVector2d Centroid(const TArray<FVector2d>& Poly)
	{
		FVector2d Sum(0.0, 0.0);
		for (const FVector2d& P : Poly)
		{
			Sum += P;
		}
		return Sum / FMath::Max(1, Poly.Num());
	}

	/**
	 * Shrinks a counter-clockwise convex polygon. EdgeDistance[i] is the inset of edge Poly[i] -> Poly[i + 1].
	 * Returns an empty array if it collapses.
	 */
	static TArray<FVector2d> Inset(const TArray<FVector2d>& Poly, const TArray<double>& EdgeDistance)
	{
		TArray<FVector2d> Result;
		const int32 Num = Poly.Num();
		double MaxDistance = 0.0;
		for (int32 i = 0; i < Num; ++i)
		{
			const FVector2d& Prev = Poly[(i + Num - 1) % Num];
			const FVector2d& Cur = Poly[i];
			const FVector2d& Next = Poly[(i + 1) % Num];
			const double D1 = EdgeDistance[(i + Num - 1) % Num];
			const double D2 = EdgeDistance[i];
			MaxDistance = FMath::Max(MaxDistance, D2);
			const FVector2d E1 = (Cur - Prev).GetSafeNormal();
			const FVector2d E2 = (Next - Cur).GetSafeNormal();
			const FVector2d N1(-E1.Y, E1.X); // left = inside for CCW
			const FVector2d N2(-E2.Y, E2.X);

			// Intersect the two offset edge lines.
			const FVector2d P1 = Prev + N1 * D1;
			const FVector2d P2 = Cur + N2 * D2;
			const double Cross = E1.X * E2.Y - E1.Y * E2.X;
			if (FMath::Abs(Cross) < 1e-4)
			{
				Result.Add(Cur + N1 * D1);
				continue;
			}
			const FVector2d D = P2 - P1;
			const double T = (D.X * E2.Y - D.Y * E2.X) / Cross;
			Result.Add(P1 + E1 * T);
		}

		// Reject flipped or tiny results (offset larger than the block).
		return SignedArea(Result) > MaxDistance * MaxDistance ? Result : TArray<FVector2d>();
	}

	/** Point on a quadratic Bezier curve. */
	static FVector2d Bezier(const FVector2d& A, const FVector2d& Control, const FVector2d& B, double T)
	{
		const double U = 1.0 - T;
		return A * (U * U) + Control * (2.0 * U * T) + B * (T * T);
	}
}

ACityVoronoiGenerator::ACityVoronoiGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
	PCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand; // generated in the editor and saved, never regenerated on play
	PCGComponent->SetIsPartitioned(true); // parks/parking go into World Partition grid cells that stream in and out

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RoadFinder(TEXT("/Game/Birkans_Folder/Art/StaticMesh/StraightRoad.StraightRoad"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlateFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RoofFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GroundFinder(TEXT("/Engine/BasicShapes/Plane.Plane"));
	GroundMesh = GroundFinder.Object;
	RoadMesh = RoadFinder.Object;
	IntersectionMesh = PlateFinder.Object;
	RoofMesh = RoofFinder.Object;

	// Default facade kits from Downtown West.
	auto Mesh = [](const TCHAR* Path) { ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Finder(Path); return Finder.Get(); };
	auto Style = [](UStaticMesh* Wall, UStaticMesh* Ground, UStaticMesh* Top, UStaticMesh* Corner, float Width, float GroundHeight)
	{
		FCityBuildingStyle S;
		S.WallMesh = Wall;
		S.GroundFloorMesh = Ground;
		S.TopTrimMesh = Top;
		S.CornerMesh = Corner;
		S.ModuleWidth = Width;
		S.GroundFloorHeight = GroundHeight;
		return S;
	};
	BuildingStyles = {
		Style(Mesh(TEXT("/Game/Downtown_West/Assets/building_d/SM_build_d_mod_wall_lvl2_window.SM_build_d_mod_wall_lvl2_window")),
			Mesh(TEXT("/Game/Downtown_West/Assets/building_d/SM_build_d_mod_lvl1_window.SM_build_d_mod_lvl1_window")),
			Mesh(TEXT("/Game/Downtown_West/Assets/building_d/SM_build_d_mod_trim_top_a.SM_build_d_mod_trim_top_a")),
			Mesh(TEXT("/Game/Downtown_West/Assets/building_d/SM_build_d_pillar_a.SM_build_d_pillar_a")), 500.0f, 450.0f),
		Style(Mesh(TEXT("/Game/Downtown_West/Assets/building_e/SM_build_e_mod_wall_lvl2_window.SM_build_e_mod_wall_lvl2_window")),
			nullptr,
			Mesh(TEXT("/Game/Downtown_West/Assets/building_e/SM_build_e_mod_trim_top_a.SM_build_e_mod_trim_top_a")),
			nullptr, 400.0f, 600.0f),
		Style(Mesh(TEXT("/Game/Downtown_West/Assets/building_b/SM_build_b_mod_lvl2_doublewindow.SM_build_b_mod_lvl2_doublewindow")),
			Mesh(TEXT("/Game/Downtown_West/Assets/building_b/SM_build_b_mod_lvl1_storefront_a_5m.SM_build_b_mod_lvl1_storefront_a_5m")),
			Mesh(TEXT("/Game/Downtown_West/Assets/building_b/SM_build_b_mod_top_trim.SM_build_b_mod_top_trim")),
			nullptr, 500.0f, 400.0f),
		Style(Mesh(TEXT("/Game/Downtown_West/Assets/building_f/SM_build_f_mod_lvl2_window_a.SM_build_f_mod_lvl2_window_a")),
			nullptr, nullptr,
			Mesh(TEXT("/Game/Downtown_West/Assets/building_f/SM_build_f_pillar_a.SM_build_f_pillar_a")), 600.0f, 600.0f),
	};

	for (const FZoneLaneProfile& Profile : GetDefault<UZoneGraphSettings>()->GetLaneProfiles())
	{
		const FString ProfileName = Profile.Name.ToString();
		if (!VehicleLaneProfile.ID.IsValid() && ProfileName.Contains(TEXT("Vehicle")))
		{
			VehicleLaneProfile = FZoneLaneProfileRef(Profile);
		}
		if (!CrowdLaneProfile.ID.IsValid() && ProfileName.Contains(TEXT("Crowd")))
		{
			CrowdLaneProfile = FZoneLaneProfileRef(Profile);
		}
	}
}

template <class T>
T* ACityVoronoiGenerator::AddGenerated()
{
	T* Component = NewObject<T>(this, NAME_None, RF_Transactional);
	Component->CreationMethod = EComponentCreationMethod::Instance; // saved with the level and copied into PIE
	if (USceneComponent* Scene = Cast<USceneComponent>(Component))
	{
		Scene->SetupAttachment(Root);
	}
	AddInstanceComponent(Component);
	GeneratedComponents.Add(Component);
	return Component;
}

#if WITH_EDITOR
void ACityVoronoiGenerator::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Skip slider drags (wait for release) and the toggle itself.
	const bool bIsToggle = PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ACityVoronoiGenerator, bAutoRegenerate);
	if (bAutoRegenerate && !bIsToggle && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive && GetWorld() && !GetWorld()->IsGameWorld())
	{
		GenerateCity();
	}
}
#endif

void ACityVoronoiGenerator::NewSeed()
{
	Seed = FMath::Rand();
	GenerateCity();
}

void ACityVoronoiGenerator::ClearCity()
{
	if (PCGComponent)
	{
		PCGComponent->CleanupLocalImmediate(true);
	}

	for (UActorComponent* Component : GeneratedComponents)
	{
		if (Component)
		{
			RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
	}
	GeneratedComponents.Reset();
	InstancedMeshes.Reset();
	LumenProxies = nullptr;
}

void ACityVoronoiGenerator::GenerateCity()
{
	using namespace CityVoronoi;

#if !UE_BUILD_SHIPPING
	// Self-check: a 100x100 square inset by 10 must become 80x80.
	ensure(FMath::IsNearlyEqual(SignedArea(Inset({ {0, 0}, {100, 0}, {100, 100}, {0, 100} }, { 10.0, 10.0, 10.0, 10.0 })), 6400.0, 1.0));
#endif

	ClearCity();
	Modify();

	FRandomStream Rand(Seed);
	const FVector2d Half(CitySize.X * 0.5, CitySize.Y * 0.5);
	const double CityRadius = FMath::Min(Half.X, Half.Y);

	// 1. Sites on a jittered grid (Organic = 0 gives square blocks) ...
	const int32 NumX = FMath::Max(2, FMath::RoundToInt(CitySize.X / BlockSize));
	const int32 NumY = FMath::Max(2, FMath::RoundToInt(CitySize.Y / BlockSize));
	const FVector2d Step(CitySize.X / NumX, CitySize.Y / NumY);
	TArray<FVector2d> Sites;
	for (int32 Y = 0; Y < NumY; ++Y)
	{
		for (int32 X = 0; X < NumX; ++X)
		{
			const FVector2d Jitter(Rand.FRandRange(-0.5f, 0.5f) * Step.X, Rand.FRandRange(-0.5f, 0.5f) * Step.Y);
			Sites.Add(-Half + FVector2d((X + 0.5) * Step.X, (Y + 0.5) * Step.Y) + Jitter * Organic);
		}
	}
	// ... plus extra sites clustered around the centre, so downtown blocks are smaller than the outskirts.
	const int32 NumExtra = FMath::RoundToInt(Sites.Num() * DowntownDensity);
	for (int32 i = 0; i < NumExtra; ++i)
	{
		const double Angle = Rand.FRandRange(0.0f, UE_TWO_PI);
		const double Distance = CityRadius * 0.7 * Rand.FRand(); // denser towards the centre, without collapsing into tiny blocks
		Sites.Add(FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance);
	}

	// Organic city edge: a noisy blob instead of the full rectangle.
	const double Phase1 = Rand.FRandRange(0.0f, UE_TWO_PI), Phase2 = Rand.FRandRange(0.0f, UE_TWO_PI), Phase3 = Rand.FRandRange(0.0f, UE_TWO_PI);
	auto IsInsideCity = [&](const FVector2d& P)
	{
		if (!bOrganicOutline)
		{
			return true;
		}
		const double Angle = FMath::Atan2(P.Y, P.X);
		const double Noise = 0.5 * FMath::Sin(2.0 * Angle + Phase1) + 0.3 * FMath::Sin(3.0 * Angle + Phase2) + 0.2 * FMath::Sin(5.0 * Angle + Phase3);
		return P.Size() < CityRadius * (0.85 + OutlineRoughness * 0.25 * Noise);
	};

	// 2. Voronoi cells, clipped to the city rectangle, with Lloyd relaxation.
	const UE::Geometry::FAxisAlignedBox2d Bounds(-Half, Half);
	TArray<TArray<FVector2d>> Cells;
	for (int32 Pass = 0; ; ++Pass)
	{
		Cells = UE::Geometry::FDelaunay2::ComputeVoronoiCells<double>(Sites, true, Bounds);
		if (Pass >= RelaxIterations || Cells.Num() != Sites.Num())
		{
			break;
		}
		for (int32 i = 0; i < Cells.Num(); ++i)
		{
			if (Cells[i].Num() >= 3)
			{
				Sites[i] = Centroid(Cells[i]);
			}
		}
	}

	if (Cells.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("CityVoronoiGenerator: Voronoi failed, try another seed."));
		return;
	}

	// 3. Weld cell corners into a street graph. Close corners merge so every street is longer than two intersections.
	const double Radius = RoadWidth * 0.65;
	const double WeldDistance = Radius * 2.0 + 300.0;
	TArray<FVector2d> Nodes;
	auto FindOrAddNode = [&Nodes, WeldDistance](const FVector2d& P)
	{
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (FVector2d::DistSquared(Nodes[i], P) < WeldDistance * WeldDistance)
			{
				return i;
			}
		}
		return Nodes.Add(P);
	};

	TArray<TArray<int32>> CellNodes;
	TSet<FIntPoint> Edges;
	for (int32 CellIndex = 0; CellIndex < Cells.Num(); ++CellIndex)
	{
		const TArray<FVector2d>& Cell = Cells[CellIndex];
		if (!IsInsideCity(Sites[CellIndex]))
		{
			continue;
		}
		TArray<int32> Ids;
		for (const FVector2d& Corner : Cell)
		{
			const int32 Id = FindOrAddNode(Corner);
			if (Ids.Num() == 0 || Ids.Last() != Id)
			{
				Ids.Add(Id);
			}
		}
		if (Ids.Num() > 1 && Ids[0] == Ids.Last())
		{
			Ids.Pop();
		}
		if (Ids.Num() < 3)
		{
			continue;
		}
		for (int32 i = 0; i < Ids.Num(); ++i)
		{
			const int32 A = Ids[i];
			const int32 B = Ids[(i + 1) % Ids.Num()];
			Edges.Add(FIntPoint(FMath::Min(A, B), FMath::Max(A, B)));
		}
		CellNodes.Add(MoveTemp(Ids));
	}

	auto To3D = [](const FVector2d& P, double Z = 0.0) { return FVector(P.X, P.Y, Z); };
	auto MakePoint = [](const FVector& Position, const FVector& Forward, FZoneShapePointType PointType)
	{
		FZoneShapePoint Point(Position);
		Point.Type = PointType;
		Point.SetRotationFromForwardAndUp(Forward, FVector::UpVector);
		return Point;
	};

	// 4. Streets bend along a quadratic curve. The bend is seeded per street, so both neighbouring blocks agree on it.
	TArray<TArray<FVector2d>> NodeDirs; // direction each street leaves an intersection
	NodeDirs.SetNum(Nodes.Num());
	TMap<FIntPoint, double> EdgeBulge;   // how far each street curves away from its straight line
	const double MeshLength = RoadMesh ? FMath::Max(100.0, RoadMesh->GetBounds().BoxExtent.X * 2.0) : 2000.0;

	for (const FIntPoint& Edge : Edges)
	{
		const FVector2d A = Nodes[Edge.X];
		const FVector2d B = Nodes[Edge.Y];
		const double Length = FVector2d::Distance(A, B);
		const FVector2d Chord = (B - A) / Length;
		const FVector2d Perp(-Chord.Y, Chord.X);
		FRandomStream EdgeRand(HashCombine(GetTypeHash(Seed), GetTypeHash(Edge)));
		const double Offset = EdgeRand.FRandRange(-1.0f, 1.0f) * StreetCurvature * 0.2 * Length;
		const FVector2d Control = (A + B) * 0.5 + Perp * Offset;
		EdgeBulge.Add(Edge, FMath::Abs(Offset) * 0.5); // a quadratic curve peaks at half its control offset

		const FVector2d DirA = (Control - A).GetSafeNormal();
		const FVector2d DirB = (Control - B).GetSafeNormal();
		NodeDirs[Edge.X].Add(DirA);
		NodeDirs[Edge.Y].Add(DirB);

		// Sample the curve between the two intersection plates.
		const FVector2d EntryA = A + DirA * Radius;
		const FVector2d EntryB = B + DirB * Radius;
		const int32 Segments = FMath::Max(1, FMath::RoundToInt(FVector2d::Distance(EntryA, EntryB) / MeshLength));
		TArray<FVector2d> Curve;
		for (int32 i = 0; i <= Segments; ++i)
		{
			Curve.Add(Bezier(EntryA, Control, EntryB, double(i) / Segments));
		}
		for (int32 i = 0; i < Segments; ++i)
		{
			AddRoad(To3D(Curve[i]), To3D(Curve[i + 1]));
		}

		if (bGenerateZoneLanes)
		{
			TArray<FZoneShapePoint> CarPoints;
			TArray<FZoneShapePoint> LeftWalk;
			TArray<FZoneShapePoint> RightWalk;
			for (int32 i = 0; i <= Segments; ++i)
			{
				const FVector2d Tangent = i == 0 ? DirA : (i == Segments ? -DirB : (Curve[i + 1] - Curve[i - 1]).GetSafeNormal());
				const FVector2d Side(-Tangent.Y, Tangent.X);
				// Inner points smooth the curve; the ends keep the exact entry direction so they connect to the intersections.
				const FZoneShapePointType Type = (i == 0 || i == Segments) ? FZoneShapePointType::Sharp : FZoneShapePointType::AutoBezier;
				CarPoints.Add(MakePoint(To3D(Curve[i], LaneHeight), To3D(Tangent), Type));
				LeftWalk.Add(MakePoint(To3D(Curve[i] + Side * SidewalkOffset, LaneHeight), To3D(Tangent), Type));
				RightWalk.Add(MakePoint(To3D(Curve[i] - Side * SidewalkOffset, LaneHeight), To3D(Tangent), Type));
			}
			AddZoneShape(FZoneShapeType::Spline, VehicleLaneProfile, RoadTags, MoveTemp(CarPoints));
			AddZoneShape(FZoneShapeType::Spline, CrowdLaneProfile, FZoneGraphTagMask(), MoveTemp(LeftWalk));
			AddZoneShape(FZoneShapeType::Spline, CrowdLaneProfile, FZoneGraphTagMask(), MoveTemp(RightWalk));
		}
	}

	// 5. Intersections: a plate plus a vehicle polygon (traffic lights when 3+ streets meet) and a crowd polygon (crosswalks).
	for (int32 NodeId = 0; NodeId < Nodes.Num(); ++NodeId)
	{
		TArray<FVector2d>& Dirs = NodeDirs[NodeId];
		if (Dirs.Num() == 0)
		{
			continue;
		}
		const FVector2d Center = Nodes[NodeId];
		AddIntersection(To3D(Center), Radius);

		if (!bGenerateZoneLanes || Dirs.Num() < 2)
		{
			continue;
		}

		// Counter-clockwise order keeps the polygon winding consistent.
		Dirs.Sort([](const FVector2d& L, const FVector2d& R) { return FMath::Atan2(L.Y, L.X) < FMath::Atan2(R.Y, R.X); });

		TArray<FZoneShapePoint> CarPoints;
		TArray<FZoneShapePoint> WalkPoints;
		for (const FVector2d& Dir2 : Dirs)
		{
			const FVector Dir = To3D(Dir2);
			const FVector Side(-Dir.Y, Dir.X, 0.0);
			const FVector Entry = To3D(Center, LaneHeight) + Dir * Radius;
			CarPoints.Add(MakePoint(Entry, -Dir, FZoneShapePointType::LaneProfile));
			WalkPoints.Add(MakePoint(Entry - Side * SidewalkOffset, -Dir, FZoneShapePointType::LaneProfile));
			WalkPoints.Add(MakePoint(Entry + Side * SidewalkOffset, -Dir, FZoneShapePointType::LaneProfile));
		}
		AddZoneShape(FZoneShapeType::Polygon, VehicleLaneProfile, Dirs.Num() >= 3 ? IntersectionTags : RoadTags, MoveTemp(CarPoints));
		AddZoneShape(FZoneShapeType::Polygon, CrowdLaneProfile, CrosswalkTags, MoveTemp(WalkPoints));
	}

	// 6. Blocks: the cell shrunk away from the streets, tagged with its district for the PCG graph.
	int32 NumBlocks = 0;
	for (const TArray<int32>& Ids : CellNodes)
	{
		TArray<int32> Ring = Ids;
		TArray<FVector2d> Poly;
		for (const int32 Id : Ring)
		{
			Poly.Add(Nodes[Id]);
		}
		if (SignedArea(Poly) < 0.0)
		{
			Algo::Reverse(Poly);
			Algo::Reverse(Ring);
		}

		// Keep clear of the street, including how far it curves into this block.
		TArray<double> EdgeInset;
		for (int32 i = 0; i < Ring.Num(); ++i)
		{
			const int32 U = Ring[i];
			const int32 V = Ring[(i + 1) % Ring.Num()];
			const double* Bulge = EdgeBulge.Find(FIntPoint(FMath::Min(U, V), FMath::Max(U, V)));
			EdgeInset.Add(RoadWidth * 0.5 + BlockMargin + (Bulge ? *Bulge : 0.0));
		}

		const TArray<FVector2d> Inner = Inset(Poly, EdgeInset);
		if (Inner.Num() < 3)
		{
			continue;
		}

		const float Roll = Rand.FRand();
		const FName Type = Roll < ParkChance ? ParkTag : (Roll < ParkChance + ParkingChance ? ParkingTag : BuildingTag);
		AddBlock(Inner, Type);
		if (Type == BuildingTag && bGenerateBuildings)
		{
			AddBuildings(Inner, Rand);
		}
		++NumBlocks;
	}

	for (const TPair<TObjectPtr<UStaticMesh>, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair : InstancedMeshes)
	{
		Pair.Value->BuildTreeIfOutdated(true, true);
	}
	if (LumenProxies)
	{
		LumenProxies->BuildTreeIfOutdated(true, true);
	}

	if (GroundMesh)
	{
		const FVector Extent = GroundMesh->GetBounds().BoxExtent;
		UStaticMeshComponent* Ground = AddGenerated<UStaticMeshComponent>();
		Ground->SetMobility(EComponentMobility::Static);
		Ground->SetStaticMesh(GroundMesh);
		Ground->SetCastShadow(false); // flat ground never shadows anything
		Ground->SetRelativeScale3D(FVector(CitySize.X / FMath::Max(1.0, Extent.X * 2.0), CitySize.Y / FMath::Max(1.0, Extent.Y * 2.0), 1.0));
		if (GroundMaterial)
		{
			Ground->SetMaterial(0, GroundMaterial);
		}
		Ground->RegisterComponent();
	}

	UE_LOG(LogTemp, Log, TEXT("CityVoronoiGenerator: %d intersections, %d streets, %d blocks."), Nodes.Num(), Edges.Num(), NumBlocks);

	if (PCGComponent)
	{
		PCGComponent->GenerateLocal(true);
	}
}

void ACityVoronoiGenerator::AddRoad(const FVector& Start, const FVector& End)
{
	if (!RoadMesh)
	{
		return;
	}

	// Streets are straight, so instanced pieces (stretched to fit) replace one spline mesh component per piece.
	const FBoxSphereBounds MeshBounds = RoadMesh->GetBounds();
	const double MeshLength = FMath::Max(100.0, MeshBounds.BoxExtent.X * 2.0);
	const FVector Delta = End - Start;
	const int32 Pieces = FMath::Max(1, FMath::RoundToInt(Delta.Size() / MeshLength));
	const FVector Step = Delta / Pieces;
	const FRotator Rotation = Delta.Rotation();
	const FVector Scale(Step.Size() / MeshLength, 1.0, 1.0);

	for (int32 i = 0; i < Pieces; ++i)
	{
		// Centre each piece on its segment (works for any pivot along X).
		const FVector Mid = Start + Step * (i + 0.5) - Rotation.RotateVector(FVector(MeshBounds.Origin.X * Scale.X, 0.0, 0.0));
		AddInstance(RoadMesh, FTransform(Rotation, Mid, Scale));
	}
}

void ACityVoronoiGenerator::AddIntersection(const FVector& Center, float Radius)
{
	if (!IntersectionMesh)
	{
		return;
	}

	const FBoxSphereBounds MeshBounds = IntersectionMesh->GetBounds();
	const double Width = FMath::Max(1.0, MeshBounds.BoxExtent.X * 2.0);
	const double Height = FMath::Max(1.0, MeshBounds.BoxExtent.Z * 2.0);
	const double RoadThickness = RoadMesh ? RoadMesh->GetBounds().BoxExtent.Z * 2.0 : 40.0;
	AddInstance(IntersectionMesh, FTransform(FRotator::ZeroRotator, Center, FVector(Radius * 2.0 / Width, Radius * 2.0 / Width, RoadThickness / Height)));
}

void ACityVoronoiGenerator::AddBlock(const TArray<FVector2d>& Polygon, FName TypeTag)
{
	USplineComponent* Outline = AddGenerated<USplineComponent>();
	Outline->ClearSplinePoints(false);
	for (const FVector2d& P : Polygon)
	{
		Outline->AddSplinePoint(FVector(P.X, P.Y, 0.0), ESplineCoordinateSpace::Local, false);
	}
	for (int32 i = 0; i < Polygon.Num(); ++i)
	{
		Outline->SetSplinePointType(i, ESplinePointType::Linear, false);
	}
	Outline->SetClosedLoop(true, false);
	Outline->UpdateSpline();
	Outline->ComponentTags = { CityVoronoi::BlockTag, TypeTag };
	Outline->bIsEditorOnly = true; // only the PCG graph reads it, in the editor
	Outline->RegisterComponent();
}

void ACityVoronoiGenerator::AddInstance(UStaticMesh* Mesh, const FTransform& Transform)
{
	if (!Mesh)
	{
		return;
	}

	TObjectPtr<UHierarchicalInstancedStaticMeshComponent>& Instances = InstancedMeshes.FindOrAdd(Mesh);
	if (!Instances)
	{
		Instances = AddGenerated<UHierarchicalInstancedStaticMeshComponent>();
		Instances->SetMobility(EComponentMobility::Static);
		Instances->bAutoRebuildTreeOnInstanceChanges = false; // built once after generation
		Instances->SetStaticMesh(Mesh);
		if (Mesh == RoofMesh && RoofMaterial)
		{
			Instances->SetMaterial(0, RoofMaterial);
		}
		if (Mesh == IntersectionMesh)
		{
			if (UMaterialInterface* Material = IntersectionMaterial ? IntersectionMaterial.Get() : (RoadMesh ? RoadMesh->GetMaterial(0) : nullptr))
			{
				Instances->SetMaterial(0, Material);
			}
		}
		const bool bIsDetail = BuildingStyles.ContainsByPredicate([Mesh](const FCityBuildingStyle& S) { return Mesh == S.TopTrimMesh || Mesh == S.CornerMesh; });
		if (DetailCullDistance > 0.0f && bIsDetail)
		{
			Instances->SetCullDistances(DetailCullDistance * 0.8f, DetailCullDistance); // trims and pillars only, walls stay visible
		}
		const bool bIsFacade = BuildingStyles.ContainsByPredicate([Mesh](const FCityBuildingStyle& S)
		{
			return Mesh == S.WallMesh || Mesh == S.GroundFloorMesh || Mesh == S.TopTrimMesh || Mesh == S.CornerMesh;
		});
		if (bIsFacade && bUseLumenBuildingProxies)
		{
			// The per-building proxy boxes stand in for these pieces in Lumen's surface cache and the distance field.
			Instances->bAffectDynamicIndirectLighting = false;
			Instances->bAffectDistanceFieldLighting = false;
		}
		Instances->RegisterComponent();
	}
	Instances->AddInstance(Transform, false);
}

void ACityVoronoiGenerator::AddBuildings(const TArray<FVector2d>& Block, FRandomStream& Rand)
{
	using namespace CityVoronoi;

	float TotalWeight = 0.0f;
	for (const FCityBuildingStyle& Style : BuildingStyles)
	{
		TotalWeight += Style.WallMesh ? Style.Weight : 0.0f;
	}
	if (TotalWeight <= 0.0f)
	{
		return;
	}
	auto PickStyle = [&]() -> const FCityBuildingStyle&
	{
		float Roll = Rand.FRand() * TotalWeight;
		for (const FCityBuildingStyle& Style : BuildingStyles)
		{
			if (Style.WallMesh && (Roll -= Style.Weight) <= 0.0f)
			{
				return Style;
			}
		}
		return BuildingStyles.Last();
	};

	auto Yaw = [](const FVector2d& Dir) { return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)); };
	auto To3D = [](const FVector2d& P, double Z) { return FVector(P.X, P.Y, Z); };

	const FVector2d Center = Centroid(Block);
	const double CityRadius = 0.5 * FMath::Min(CitySize.X, CitySize.Y);
	const int32 NumEdges = Block.Num();

	for (int32 Edge = 0; Edge < NumEdges; ++Edge)
	{
		const FVector2d A = Block[Edge];
		const FVector2d B = Block[(Edge + 1) % NumEdges];
		const double Length = FVector2d::Distance(A, B);
		if (Length < 1.0)
		{
			continue;
		}
		const FVector2d T = (B - A) / Length;     // along the street
		const FVector2d N(-T.Y, T.X);              // into the block (block polygons are counter-clockwise)
		const double MaxDepth = FVector2d::DotProduct(Center - A, N) * 0.9;

		// Leave the block corners free so buildings on neighbouring edges do not overlap.
		const double CornerGap = FMath::Min(MaxDepth, 1500.0);
		double Cursor = CornerGap;

		while (true)
		{
			const FCityBuildingStyle& S = PickStyle();
			const int32 WMods = Rand.RandRange(FMath::Max(1, WidthModules.X), FMath::Max(WidthModules.X, WidthModules.Y));
			const int32 DMods = FMath::Min(Rand.RandRange(FMath::Max(1, DepthModules.X), FMath::Max(DepthModules.X, DepthModules.Y)), FMath::FloorToInt(MaxDepth / S.ModuleWidth));
			const double W = WMods * S.ModuleWidth;
			const double D = DMods * S.ModuleWidth;
			if (DMods < 2 || Cursor + W > Length - CornerGap)
			{
				break;
			}

			// Mostly low-rise; towers cluster downtown when DowntownStrength is high.
			const FVector2d Origin = A + T * Cursor; // front-left corner on the street side
			const FVector2d Middle = Origin + T * (W * 0.5) + N * (D * 0.5);
			const double Downtown = 1.0 - FMath::Clamp(Middle.Size() / CityRadius, 0.0, 1.0);
			const double Alpha = FMath::Lerp((double)Rand.FRand(), Downtown * (0.6 + 0.4 * Rand.FRand()), (double)DowntownStrength);
			const int32 Floors = FMath::Max(1, FMath::RoundToInt(FMath::Lerp((double)MinFloors, (double)FMath::Max(MinFloors, MaxFloors), Alpha * Alpha)));
			const double TopZ = S.GroundFloorHeight + (Floors - 1) * S.FloorHeight;

			struct FSide { FVector2d Start; FVector2d Along; FVector2d Out; int32 Count; };
			const FSide Sides[] = {
				{ Origin,                 T,  -N, WMods },
				{ Origin + T * W,         N,   T, DMods },
				{ Origin + T * W + N * D, -T,  N, WMods },
				{ Origin + N * D,         -N, -T, DMods } };

			for (const FSide& Side : Sides)
			{
				const FRotator Rotation(0.0, Yaw(Side.Out), 0.0);
				for (int32 i = 0; i < Side.Count; ++i)
				{
					const FVector2d P = Side.Start + Side.Along * ((i + 0.5) * S.ModuleWidth);
					AddInstance(S.GroundFloorMesh ? S.GroundFloorMesh.Get() : S.WallMesh.Get(), FTransform(Rotation, To3D(P, 0.0)));
					for (int32 Floor = 1; Floor < Floors; ++Floor)
					{
						AddInstance(S.WallMesh, FTransform(Rotation, To3D(P, S.GroundFloorHeight + (Floor - 1) * S.FloorHeight)));
					}
					AddInstance(S.TopTrimMesh, FTransform(Rotation, To3D(P, TopZ)));
				}
				if (S.CornerMesh)
				{
					const double PillarHeight = FMath::Max(1.0, S.CornerMesh->GetBounds().BoxExtent.Z * 2.0);
					AddInstance(S.CornerMesh, FTransform(Rotation, To3D(Side.Start, 0.0), FVector(1.0, 1.0, TopZ / PillarHeight)));
				}
			}

			if (RoofMesh)
			{
				const FVector Extent = RoofMesh->GetBounds().BoxExtent;
				AddInstance(RoofMesh, FTransform(FRotator(0.0, Yaw(T), 0.0), To3D(Middle, TopZ - 10.0),
					FVector(W / (Extent.X * 2.0), D / (Extent.Y * 2.0), 20.0 / (Extent.Z * 2.0))));

				if (bUseLumenBuildingProxies)
				{
					if (!LumenProxies)
					{
						LumenProxies = AddGenerated<UHierarchicalInstancedStaticMeshComponent>();
						LumenProxies->SetMobility(EComponentMobility::Static);
						LumenProxies->bAutoRebuildTreeOnInstanceChanges = false;
						LumenProxies->SetStaticMesh(RoofMesh);
						LumenProxies->SetRenderInMainPass(false); // never drawn, only lit/traced
						LumenProxies->SetRenderInDepthPass(false);
						LumenProxies->SetCastShadow(false);      // the facades cast the real shadows
						LumenProxies->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						LumenProxies->RegisterComponent();
					}
					LumenProxies->AddInstance(FTransform(FRotator(0.0, Yaw(T), 0.0), To3D(Middle, TopZ * 0.5),
						FVector(W / (Extent.X * 2.0), D / (Extent.Y * 2.0), TopZ / (Extent.Z * 2.0))), false);
				}
			}

			Cursor += W + Rand.FRandRange(0.0f, 300.0f);
		}
	}
}

void ACityVoronoiGenerator::AddZoneShape(FZoneShapeType Type, const FZoneLaneProfileRef& Profile, FZoneGraphTagMask ShapeTags, TArray<FZoneShapePoint>&& Points)
{
	UZoneShapeComponent* Shape = AddGenerated<UZoneShapeComponent>();
	Shape->SetShapeType(Type);
	Shape->SetCommonLaneProfile(Profile);
	Shape->SetTags(ShapeTags);
	Shape->GetMutablePoints() = MoveTemp(Points);
	Shape->bIsEditorOnly = true; // lanes are baked into the level's ZoneGraphData; the game does not need the shapes
	Shape->SetVisibility(bShowLanesInEditor);
	Shape->UpdateShape();
	Shape->RegisterComponent();
}
