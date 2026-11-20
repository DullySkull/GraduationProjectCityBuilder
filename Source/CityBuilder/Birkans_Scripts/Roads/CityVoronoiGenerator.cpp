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

	/** Keeps the part of a convex polygon where Dot(P - Origin, Normal) <= 0. */
	static TArray<FVector2d> ClipHalfPlane(const TArray<FVector2d>& Poly, const FVector2d& Origin, const FVector2d& Normal)
	{
		TArray<FVector2d> Out;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2d& A = Poly[i];
			const FVector2d& B = Poly[(i + 1) % Poly.Num()];
			const double DA = (A - Origin).Dot(Normal);
			const double DB = (B - Origin).Dot(Normal);
			if (DA <= 0.0)
			{
				Out.Add(A);
			}
			if ((DA < 0.0 && DB > 0.0) || (DA > 0.0 && DB < 0.0))
			{
				Out.Add(A + (B - A) * (DA / (DA - DB)));
			}
		}
		return Out;
	}

	/** Point inside a counter-clockwise convex polygon. */
	static bool InsideConvex(const TArray<FVector2d>& Poly, const FVector2d& P)
	{
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2d E = Poly[(i + 1) % Poly.Num()] - Poly[i];
			const FVector2d D = P - Poly[i];
			if (E.X * D.Y - E.Y * D.X < -1.0)
			{
				return false;
			}
		}
		return true;
	}

	/** Direction of the longest edge; lots and buildings line up with it, which usually means with the street. */
	static FVector2d LongestEdgeDir(const TArray<FVector2d>& Poly)
	{
		FVector2d Best(1.0, 0.0);
		double BestLength = -1.0;
		for (int32 i = 0; i < Poly.Num(); ++i)
		{
			const FVector2d E = Poly[(i + 1) % Poly.Num()] - Poly[i];
			if (E.SizeSquared() > BestLength)
			{
				BestLength = E.SizeSquared();
				Best = E;
			}
		}
		return Best.GetSafeNormal();
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TreeFinder(TEXT("/Game/Downtown_West/Assets/foliage/tree_oak_a/sm_tree_oak_a.sm_tree_oak_a"));
	TreeMesh = TreeFinder.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SidewalkFinder(TEXT("/Game/Downtown_West/Materials/Ground_Shared/MI_Bricks_Ground_A_WS.MI_Bricks_Ground_A_WS"));
	SidewalkMaterial = SidewalkFinder.Object;
	RoadMesh = RoadFinder.Object;
	IntersectionMesh = PlateFinder.Object;
	RoofMesh = RoofFinder.Object;
	SidewalkMesh = RoofFinder.Object;

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
	SidewalkStrips = nullptr;
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
	if (FlowStrength > 0.0f)
	{
		// Tensor-field style: sites are dropped along streamlines of a smooth, curving direction field, so blocks stretch
		// along the flow and streets follow it (like a city shaped by a river or coastline).
		const double F1 = Rand.FRandRange(0.0f, UE_TWO_PI), F2 = Rand.FRandRange(0.0f, UE_TWO_PI), F3 = Rand.FRandRange(0.0f, UE_TWO_PI);
		auto FlowAngle = [&](const FVector2d& P)
		{
			const double L = FMath::Max(1000.0, (double)FlowScale);
			return FMath::DegreesToRadians(FlowDirection) + FlowCurl * FlowStrength *
				(FMath::Sin(P.X / L + F1) * FMath::Cos(P.Y / L * 0.8 + F2) + 0.5 * FMath::Sin((P.X - P.Y) / (0.6 * L) + F3));
		};
		auto TooClose = [&Sites](const FVector2d& P, double MinDistance)
		{
			for (const FVector2d& S : Sites)
			{
				if (FVector2d::DistSquared(S, P) < MinDistance * MinDistance)
				{
					return true;
				}
			}
			return false;
		};

		const double Along = BlockSize * FMath::Max(1.0f, FlowElongation);
		const double A0 = FlowAngle(FVector2d::ZeroVector);
		const FVector2d Dir0(FMath::Cos(A0), FMath::Sin(A0));
		const FVector2d Perp0(-Dir0.Y, Dir0.X);
		const int32 NumLines = FMath::CeilToInt(CityRadius * 1.4 / BlockSize);
		for (int32 Line = -NumLines; Line <= NumLines; ++Line)
		{
			const FVector2d Start = Perp0 * (Line * BlockSize) + Dir0 * (Rand.FRandRange(-0.5f, 0.5f) * Along);
			for (const double Direction : { 1.0, -1.0 })
			{
				FVector2d P = Start;
				for (int32 StepIndex = 0; StepIndex < 200 && P.Size() < CityRadius * 1.4; ++StepIndex)
				{
					const double Density = 1.0 - DowntownDensity * 0.5 * (1.0 - FMath::Clamp(P.Size() / CityRadius, 0.0, 1.0));
					const FVector2d Q = P + FVector2d(Rand.FRandRange(-1.0f, 1.0f), Rand.FRandRange(-1.0f, 1.0f)) * (Organic * 0.25 * BlockSize);
					if (FMath::Abs(Q.X) < Half.X && FMath::Abs(Q.Y) < Half.Y && !TooClose(Q, 0.55 * BlockSize * Density))
					{
						Sites.Add(Q);
					}
					const double Angle = FlowAngle(P);
					P += FVector2d(FMath::Cos(Angle), FMath::Sin(Angle)) * (Direction * Along * Density);
				}
			}
		}
	}
	else
	{
		for (int32 Y = 0; Y < NumY; ++Y)
		{
			for (int32 X = 0; X < NumX; ++X)
			{
				const FVector2d Jitter(Rand.FRandRange(-0.5f, 0.5f) * Step.X, Rand.FRandRange(-0.5f, 0.5f) * Step.Y);
				Sites.Add(-Half + FVector2d((X + 0.5) * Step.X, (Y + 0.5) * Step.Y) + Jitter * Organic);
			}
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

	// 3b. Noise-based road thinning: drop streets in the outskirts so neighbouring blocks merge into bigger plots.
	// A random spanning tree always stays (the network never splits) and no intersection drops below two streets (no dead ends).
	if (RoadThinning > 0.0f)
	{
		TArray<FIntPoint> EdgeList = Edges.Array();
		for (int32 i = EdgeList.Num() - 1; i > 0; --i)
		{
			EdgeList.Swap(i, Rand.RandRange(0, i));
		}
		TArray<int32> Parent;
		Parent.SetNum(Nodes.Num());
		for (int32 i = 0; i < Parent.Num(); ++i)
		{
			Parent[i] = i;
		}
		auto FindRoot = [&Parent](int32 X)
		{
			while (Parent[X] != X)
			{
				Parent[X] = Parent[Parent[X]];
				X = Parent[X];
			}
			return X;
		};
		TSet<FIntPoint> SpanningTree;
		TArray<int32> Degree;
		Degree.Init(0, Nodes.Num());
		for (const FIntPoint& Edge : EdgeList)
		{
			++Degree[Edge.X];
			++Degree[Edge.Y];
			const int32 RootA = FindRoot(Edge.X);
			const int32 RootB = FindRoot(Edge.Y);
			if (RootA != RootB)
			{
				Parent[RootA] = RootB;
				SpanningTree.Add(Edge);
			}
		}

		const double N1 = Rand.FRandRange(0.0f, UE_TWO_PI), N2 = Rand.FRandRange(0.0f, UE_TWO_PI);
		const double NoiseScale = BlockSize * 3.0;
		for (const FIntPoint& Edge : EdgeList)
		{
			if (SpanningTree.Contains(Edge) || Degree[Edge.X] <= 2 || Degree[Edge.Y] <= 2)
			{
				continue;
			}
			const FVector2d Mid = (Nodes[Edge.X] + Nodes[Edge.Y]) * 0.5;
			const double Outskirts = FMath::SmoothStep((double)ThinningInnerRadius, 1.0, Mid.Size() / CityRadius); // 0 downtown, 1 at the edge
			const double Noise = 0.5 + 0.5 * FMath::Sin(Mid.X / NoiseScale + N1) * FMath::Cos(Mid.Y / NoiseScale + N2);
			if (Rand.FRand() < RoadThinning * Outskirts * (0.4 + 1.2 * Noise))
			{
				Edges.Remove(Edge);
				--Degree[Edge.X];
				--Degree[Edge.Y];
			}
		}
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

		// Keep clear of the street, including how far it curves into this block. Where thinning removed the street,
		// the block reaches (almost) to its neighbour, so the two read as one bigger plot.
		TArray<double> EdgeInset;
		TArray<bool> EdgeHasRoad;
		for (int32 i = 0; i < Ring.Num(); ++i)
		{
			const int32 U = Ring[i];
			const int32 V = Ring[(i + 1) % Ring.Num()];
			const FIntPoint Key(FMath::Min(U, V), FMath::Max(U, V));
			const bool bRoad = Edges.Contains(Key);
			const double* Bulge = EdgeBulge.Find(Key);
			EdgeHasRoad.Add(bRoad);
			EdgeInset.Add(bRoad ? RoadWidth * 0.5 + BlockMargin + (Bulge ? *Bulge : 0.0) : 100.0);
		}

		const TArray<FVector2d> Inner = Inset(Poly, EdgeInset);
		if (Inner.Num() < 3)
		{
			continue;
		}

		// Raised sidewalk strip between the road and the block on every street side.
		if (bGenerateSidewalks && SidewalkMesh)
		{
			const FVector Extent = SidewalkMesh->GetBounds().BoxExtent;
			for (int32 i = 0; i < Inner.Num(); ++i)
			{
				if (!EdgeHasRoad[i])
				{
					continue;
				}
				const FVector2d A = Inner[i];
				const FVector2d B = Inner[(i + 1) % Inner.Num()];
				const double Length = FVector2d::Distance(A, B);
				const double Width = EdgeInset[i] - RoadWidth * 0.5 + 100.0; // overlap the road edge slightly
				if (Length < 1.0 || Width <= 0.0)
				{
					continue;
				}
				const FVector2d T = (B - A) / Length;
				const FVector2d N(-T.Y, T.X);
				const FVector2d Center = (A + B) * 0.5 - N * (Width * 0.5);
				if (!SidewalkStrips)
				{
					SidewalkStrips = AddGenerated<UHierarchicalInstancedStaticMeshComponent>();
					SidewalkStrips->SetMobility(EComponentMobility::Static);
					SidewalkStrips->bAutoRebuildTreeOnInstanceChanges = false;
					SidewalkStrips->SetStaticMesh(SidewalkMesh);
					SidewalkStrips->SetCastShadow(false);
					if (SidewalkMaterial)
					{
						SidewalkStrips->SetMaterial(0, SidewalkMaterial);
					}
					SidewalkStrips->RegisterComponent();
				}
				SidewalkStrips->AddInstance(FTransform(FRotator(0.0, FMath::RadiansToDegrees(FMath::Atan2(T.Y, T.X)), 0.0), To3D(Center, SidewalkHeight * 0.5),
					FVector((Length + Width) / (Extent.X * 2.0), Width / (Extent.Y * 2.0), SidewalkHeight / (Extent.Z * 2.0))));
			}
		}

		const float Roll = Rand.FRand();
		const FName Type = Roll < ParkChance ? ParkTag : (Roll < ParkChance + ParkingChance ? ParkingTag : BuildingTag);
		AddBlock(Inner, Type);

		// Park walking loop: two crowd splines that meet head-to-tail at edge midpoints, so ZoneGraph links them into a loop.
		// Tagged for Smart Objects so pedestrians find the PCG benches placed beside it.
		if (Type == ParkTag && bGenerateParkPaths && bGenerateZoneLanes)
		{
			TArray<double> PathInset;
			PathInset.Init(ParkPathInset, Inner.Num());
			const TArray<FVector2d> Path = Inset(Inner, PathInset);
			const int32 Num = Path.Num();
			if (Num >= 3)
			{
				auto EdgeDir = [&Path, Num](int32 i) { return (Path[(i + 1) % Num] - Path[i]).GetSafeNormal(); };
				auto EdgeMid = [&Path, Num](int32 i) { return (Path[i] + Path[(i + 1) % Num]) * 0.5; };
				auto Junction = [&](int32 Edge)
				{
					FZoneShapePoint Point = MakePoint(To3D(EdgeMid(Edge), LaneHeight), To3D(EdgeDir(Edge)), FZoneShapePointType::Bezier);
					Point.TangentLength = 300.0f;
					return Point;
				};
				const int32 Opposite = Num / 2;
				TArray<FZoneShapePoint> First = { Junction(0) };
				for (int32 i = 1; i <= Opposite; ++i)
				{
					First.Add(MakePoint(To3D(Path[i], LaneHeight), To3D(EdgeDir(i)), FZoneShapePointType::AutoBezier));
				}
				First.Add(Junction(Opposite));
				TArray<FZoneShapePoint> Second = { Junction(Opposite) };
				for (int32 i = Opposite + 1; i <= Num; ++i)
				{
					Second.Add(MakePoint(To3D(Path[i % Num], LaneHeight), To3D(EdgeDir(i % Num)), FZoneShapePointType::AutoBezier));
				}
				Second.Add(Junction(0));
				AddZoneShape(FZoneShapeType::Spline, CrowdLaneProfile, ParkLaneTags, MoveTemp(First));
				AddZoneShape(FZoneShapeType::Spline, CrowdLaneProfile, ParkLaneTags, MoveTemp(Second));
			}
		}
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
	for (UHierarchicalInstancedStaticMeshComponent* Extra : { LumenProxies.Get(), SidewalkStrips.Get() })
	{
		if (Extra)
		{
			Extra->BuildTreeIfOutdated(true, true);
		}
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
		if (Mesh == TreeMesh)
		{
			// Same cost settings as the PCG park trees: swaying leaves must not re-render the shadow cache every frame.
			Instances->ShadowCacheInvalidationBehavior = EShadowCacheInvalidationBehavior::Rigid;
			Instances->WorldPositionOffsetDisableDistance = 2500;
			Instances->bVisibleInRayTracing = false;
			Instances->bAffectDistanceFieldLighting = false;
			Instances->bAffectDynamicIndirectLighting = false;
		}
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

	// 1. Plot subdivision: split the block across its longest extent until every lot is small enough.
	TArray<TArray<FVector2d>> Lots;
	TFunction<void(const TArray<FVector2d>&, int32)> Split = [&](const TArray<FVector2d>& Poly, int32 Depth)
	{
		if (Poly.Num() < 3)
		{
			return;
		}
		const FVector2d U = LongestEdgeDir(Poly);
		const FVector2d V(-U.Y, U.X);
		double UMin = DBL_MAX, UMax = -DBL_MAX, VMin = DBL_MAX, VMax = -DBL_MAX;
		for (const FVector2d& P : Poly)
		{
			UMin = FMath::Min(UMin, P.Dot(U)); UMax = FMath::Max(UMax, P.Dot(U));
			VMin = FMath::Min(VMin, P.Dot(V)); VMax = FMath::Max(VMax, P.Dot(V));
		}
		const double LenU = UMax - UMin;
		const double LenV = VMax - VMin;
		const double MaxLot = Rand.FRandRange(LotSize.X, FMath::Max(LotSize.X, LotSize.Y));
		if (Depth > 10 || (LenU <= MaxLot && LenV <= MaxLot))
		{
			Lots.Add(Poly);
			return;
		}
		const bool bCutU = LenU >= LenV;
		const FVector2d Axis = bCutU ? U : V;
		const double Cut = (bCutU ? UMin : VMin) + (bCutU ? LenU : LenV) * Rand.FRandRange(0.35f, 0.65f);
		Split(ClipHalfPlane(Poly, Axis * Cut, Axis), Depth + 1);
		Split(ClipHalfPlane(Poly, Axis * Cut, -Axis), Depth + 1);
	};
	Split(Block, 0);

	const double CityRadius = 0.5 * FMath::Min(CitySize.X, CitySize.Y);
	auto To3D = [](const FVector2d& P, double Z) { return FVector(P.X, P.Y, Z); };

	for (const TArray<FVector2d>& Lot : Lots)
	{
		// 2. Largest lot-aligned rectangle that fits inside the (convex) lot, minus a random gap to the neighbours.
		const FVector2d U = LongestEdgeDir(Lot);
		const FVector2d V(-U.Y, U.X);
		const FVector2d C = Centroid(Lot);
		double UMin = DBL_MAX, UMax = -DBL_MAX, VMin = DBL_MAX, VMax = -DBL_MAX;
		for (const FVector2d& P : Lot)
		{
			UMin = FMath::Min(UMin, (P - C).Dot(U)); UMax = FMath::Max(UMax, (P - C).Dot(U));
			VMin = FMath::Min(VMin, (P - C).Dot(V)); VMax = FMath::Max(VMax, (P - C).Dot(V));
		}
		const FVector2d Mid = C + U * ((UMin + UMax) * 0.5) + V * ((VMin + VMax) * 0.5);
		double HalfU = (UMax - UMin) * 0.5;
		double HalfV = (VMax - VMin) * 0.5;
		double Scale = 1.0;
		for (; Scale > 0.3; Scale -= 0.05)
		{
			const double HU = HalfU * Scale, HV = HalfV * Scale;
			if (InsideConvex(Lot, Mid + U * HU + V * HV) && InsideConvex(Lot, Mid - U * HU + V * HV) &&
				InsideConvex(Lot, Mid + U * HU - V * HV) && InsideConvex(Lot, Mid - U * HU - V * HV))
			{
				break;
			}
		}
		const double Gap = Rand.FRandRange(LotGap.X, FMath::Max(LotGap.X, LotGap.Y));
		HalfU = HalfU * Scale - Gap * 0.5;
		HalfV = HalfV * Scale - Gap * 0.5;

		// Some lots stay open: a small green patch with a few trees.
		if (Rand.FRand() < GreenLotChance)
		{
			if (TreeMesh)
			{
				const int32 NumTrees = Rand.RandRange(2, 5);
				for (int32 i = 0; i < NumTrees; ++i)
				{
					const FVector2d P = Mid + U * Rand.FRandRange(-HalfU, HalfU) * 0.8 + V * Rand.FRandRange(-HalfV, HalfV) * 0.8;
					const float TreeScale = Rand.FRandRange(2.5f, 3.5f);
					AddInstance(TreeMesh, FTransform(FRotator(0.0, Rand.FRandRange(0.0f, 360.0f), 0.0), To3D(P, 0.0), FVector(TreeScale)));
				}
			}
			continue;
		}

		const FCityBuildingStyle& S = PickStyle();
		const int32 WMods = FMath::FloorToInt(2.0 * HalfU / S.ModuleWidth);
		const int32 DMods = FMath::FloorToInt(2.0 * HalfV / S.ModuleWidth);
		if (WMods < 2 || DMods < 2)
		{
			continue;
		}

		// Height: mostly mid-rise, taller towards downtown.
		const double Downtown = 1.0 - FMath::Clamp(Mid.Size() / CityRadius, 0.0, 1.0);
		const double Alpha = FMath::Lerp((double)Rand.FRand(), Downtown * (0.6 + 0.4 * Rand.FRand()), (double)DowntownStrength);
		const int32 Floors = FMath::Max(1, FMath::RoundToInt(FMath::Lerp((double)MinFloors, (double)FMath::Max(MinFloors, MaxFloors), Alpha * Alpha)));

		const FVector2d Origin = Mid - U * (WMods * S.ModuleWidth * 0.5) - V * (DMods * S.ModuleWidth * 0.5);
		AddBuildingBox(S, Origin, U, V, WMods, DMods, Floors);
	}
}

void ACityVoronoiGenerator::AddBuildingBox(const FCityBuildingStyle& S, const FVector2d& Origin, const FVector2d& T, const FVector2d& N, int32 WMods, int32 DMods, int32 Floors)
{
	auto Yaw = [](const FVector2d& Dir) { return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)); };
	auto To3D = [](const FVector2d& P, double Z) { return FVector(P.X, P.Y, Z); };

	const double W = WMods * S.ModuleWidth;
	const double D = DMods * S.ModuleWidth;
	const FVector2d Middle = Origin + T * (W * 0.5) + N * (D * 0.5);
	const double TopZ = S.GroundFloorHeight + (Floors - 1) * S.FloorHeight;

	// The box spans Origin + [0, W] * T + [0, D] * N; every side faces outwards.
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

	if (!RoofMesh)
	{
		return;
	}
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
			// Off-screen reflections fall back to these boxes; the default white material made windows flash white.
			UMaterialInterface* ProxyMaterial = LumenProxyMaterial ? LumenProxyMaterial.Get() : S.WallMesh->GetMaterial(0);
			if (ProxyMaterial)
			{
				LumenProxies->SetMaterial(0, ProxyMaterial);
			}
			LumenProxies->RegisterComponent();
		}
		LumenProxies->AddInstance(FTransform(FRotator(0.0, Yaw(T), 0.0), To3D(Middle, TopZ * 0.5),
			FVector(W / (Extent.X * 2.0), D / (Extent.Y * 2.0), TopZ / (Extent.Z * 2.0))), false);
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
