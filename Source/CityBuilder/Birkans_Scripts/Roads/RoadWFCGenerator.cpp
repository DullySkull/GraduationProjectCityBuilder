#include "RoadWFCGenerator.h"
#include "DrawDebugHelpers.h"
#include "TimerManager.h"

ARoadWFCGenerator::ARoadWFCGenerator()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("PCGComponent"));
	PCGBoundsBox = CreateDefaultSubobject<UBoxComponent>(TEXT("PCGBoundsBox"));
	PCGBoundsBox->SetupAttachment(Root);
	PCGBoundsBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PCGBoundsBox->SetBoxExtent(FVector(1500.f, 1500.f, 500.f));

	Tags.AddUnique(FName("WFCRoadGenerator"));
}

void ARoadWFCGenerator::BeginPlay()
{
	Super::BeginPlay();

	if (bGenerateOnBeginPlay)
	{
		GenerateRoadsInternal(false);

		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this]()
		{
			RefreshPCG();
		}, 0.1f, false);
	}
}

void ARoadWFCGenerator::GenerateRoads()
{
	GenerateRoadsInternal(true);
}

void ARoadWFCGenerator::GenerateRoadsInternal(bool bShouldRefreshPCG)
{
	ClearRoad();

	RoadResults.Empty();
	Grid.Empty();
	TileRules.Empty();
	RoadMask.Empty();

	if (Width <= 0 || Height <= 0 || CellSize <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoadWFCGenerator: Width, Height and CellSize must be valid."));
		return;
	}

	CreateTileRules();
	CreateGrid();
	SeedRoadSkeleton();
	ConvertRoadMaskToCollapsedGrid();
	BuildResults();

	if (PCGBoundsBox)
	{
		const float HalfW = (Width * CellSize) * 0.5f;
		const float HalfH = (Height * CellSize) * 0.5f;
		PCGBoundsBox->SetBoxExtent(FVector(HalfW, HalfH, 500.f));
		PCGBoundsBox->SetRelativeLocation(FVector(HalfW, HalfH, 0.f));
	}

	if (bShouldRefreshPCG)
	{
		RefreshPCG();
	}

	UE_LOG(LogTemp, Warning, TEXT("RoadWFCGenerator: Finished generating %d cells, %d road cells."),
		RoadResults.Num(), GetOnlyRoadCells().Num());
}

void ARoadWFCGenerator::ClearRoads()
{
	ClearRoad();

	RoadResults.Empty();
	Grid.Empty();
	TileRules.Empty();
	RoadMask.Empty();

	if (PCGComponent)
	{
		PCGComponent->CleanupLocalImmediate(true);
	}

#if WITH_EDITOR
	if (GetWorld())
	{
		FlushPersistentDebugLines(GetWorld());
	}
#endif
}

void ARoadWFCGenerator::RefreshPCG()
{
	if (!PCGComponent)
	{
		return;
	}

	if (bUseImmediatePCGCleanup)
	{
		PCGComponent->CleanupLocalImmediate(true);
	}
	else
	{
		PCGComponent->CleanupLocal(true);
	}

	PCGComponent->GenerateLocal(false);
}

const TArray<FRoadCellResult>& ARoadWFCGenerator::GetRoadResults() const
{
	return RoadResults;
}

TArray<FRoadCellResult> ARoadWFCGenerator::GetOnlyRoadCells() const
{
	TArray<FRoadCellResult> OutRoads;

	for (const FRoadCellResult& Cell : RoadResults)
	{
		if (Cell.bIsRoad)
		{
			OutRoads.Add(Cell);
		}
	}

	return OutRoads;
}

TArray<FVector> ARoadWFCGenerator::GetRoadPoints() const
{
	TArray<FVector> Points;

	for (const FRoadCellResult& Cell : RoadResults)
	{
		if (Cell.bIsRoad)
		{
			Points.Add(Cell.WorldLocation);
		}
	}

	return Points;
}

TArray<FTransform> ARoadWFCGenerator::GetRoadTransforms() const
{
	TArray<FTransform> Transforms;

	for (const FRoadCellResult& Cell : RoadResults)
	{
		if (Cell.bIsRoad)
		{
			Transforms.Add(Cell.WorldTransform);
		}
	}

	return Transforms;
}

TArray<FRoadCellResult> ARoadWFCGenerator::GetRoadCellsByType(ERoadType InRoadType) const
{
	TArray<FRoadCellResult> OutCells;

	for (const FRoadCellResult& Cell : RoadResults)
	{
		if (Cell.bIsRoad && Cell.RoadType == InRoadType)
		{
			OutCells.Add(Cell);
		}
	}

	return OutCells;
}

TArray<FTransform> ARoadWFCGenerator::GetRoadTransformsByType(ERoadType InRoadType) const
{
	TArray<FTransform> OutTransforms;

	for (const FRoadCellResult& Cell : RoadResults)
	{
		if (Cell.bIsRoad && Cell.RoadType == InRoadType)
		{
			OutTransforms.Add(Cell.WorldTransform);
		}
	}

	return OutTransforms;
}

void ARoadWFCGenerator::CreateTileRules()
{
	// TODO: Make this into Data Assets.
	TileRules.Empty();

	FRoadTile Tile;

	Tile = FRoadTile();
	Tile.Id = 0;
	Tile.Name = TEXT("Empty");
	Tile.bRoad = false;
	Tile.RoadType = ERoadType::Empty;
	Tile.Rotation = 0;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 1;
	Tile.Name = TEXT("DeadEnd_N");
	Tile.bIsNorth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::DeadEnd;
	Tile.Rotation = -90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 2;
	Tile.Name = TEXT("DeadEnd_E");
	Tile.bIsEast = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::DeadEnd;
	Tile.Rotation = 180;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 3;
	Tile.Name = TEXT("DeadEnd_S");
	Tile.bIsSouth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::DeadEnd;
	Tile.Rotation = 90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 4;
	Tile.Name = TEXT("DeadEnd_W");
	Tile.bIsWest = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::DeadEnd;
	Tile.Rotation = 0;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 5;
	Tile.Name = TEXT("Straight_NS");
	Tile.bIsNorth = true;
	Tile.bIsSouth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Straight;
	Tile.Rotation = 90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 6;
	Tile.Name = TEXT("Straight_EW");
	Tile.bIsWest = true;
	Tile.bIsEast = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Straight;
	Tile.Rotation = 0;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 7;
	Tile.Name = TEXT("Corner_NE");
	Tile.bIsNorth = true;
	Tile.bIsEast = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Corner;
	Tile.Rotation = 0;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 8;
	Tile.Name = TEXT("Corner_ES");
	Tile.bIsEast = true;
	Tile.bIsSouth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Corner;
	Tile.Rotation = -90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 9;
	Tile.Name = TEXT("Corner_SW");
	Tile.bIsSouth = true;
	Tile.bIsWest = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Corner;
	Tile.Rotation = 180;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 10;
	Tile.Name = TEXT("Corner_WN");
	Tile.bIsWest = true;
	Tile.bIsNorth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Corner;
	Tile.Rotation = 90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 11;
	Tile.Name = TEXT("T_NWE");
	Tile.bIsNorth = true;
	Tile.bIsWest = true;
	Tile.bIsEast = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::TJunction;
	Tile.Rotation = 90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 12;
	Tile.Name = TEXT("T_NES");
	Tile.bIsNorth = true;
	Tile.bIsEast = true;
	Tile.bIsSouth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::TJunction;
	Tile.Rotation = 0;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 13;
	Tile.Name = TEXT("T_ESW");
	Tile.bIsEast = true;
	Tile.bIsSouth = true;
	Tile.bIsWest = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::TJunction;
	Tile.Rotation = -90;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 14;
	Tile.Name = TEXT("T_SWN");
	Tile.bIsSouth = true;
	Tile.bIsWest = true;
	Tile.bIsNorth = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::TJunction;
	Tile.Rotation = 180;
	TileRules.Add(Tile);

	Tile = FRoadTile();
	Tile.Id = 15;
	Tile.Name = TEXT("Cross");
	Tile.bIsNorth = true;
	Tile.bIsSouth = true;
	Tile.bIsWest = true;
	Tile.bIsEast = true;
	Tile.bRoad = true;
	Tile.RoadType = ERoadType::Cross;
	Tile.Rotation = 0;
	TileRules.Add(Tile);
}

void ARoadWFCGenerator::CreateGrid()
{
	Grid.SetNum(Width * Height);
	RoadMask.Init(false, Width * Height);

	TArray<int32> AllTileIds;
	AllTileIds.Reserve(TileRules.Num());

	for (const FRoadTile& Tile : TileRules)
	{
		AllTileIds.Add(Tile.Id);
	}

	for (FWFCell& Cell : Grid)
	{
		Cell.bCollapsed = false;
		Cell.ChosenTile = -1;
		Cell.Tiles = AllTileIds;
	}
}

TArray<int32> ARoadWFCGenerator::BuildAxisRoadPositions(int32 Size, FRandomStream& Stream) const
{
	TArray<int32> Positions;

	if (Size < 6)
	{
		return Positions;
	}

	const int32 StartMin = bRoadsTouchBorder ? 0 : 2;
	const int32 StartMax = bRoadsTouchBorder ? 1 : 4;
	int32 Current = Stream.RandRange(StartMin, FMath::Min(StartMax, Size - 1));

	while (Current < Size)
	{
		if (bRoadsTouchBorder || (Current > 0 && Current < Size - 1))
		{
			Positions.Add(Current);
		}

		const int32 Step = Stream.RandRange(MinBlockSize, MaxBlockSize);
		Current += FMath::Max(2, Step);
	}

	return Positions;
}

bool ARoadWFCGenerator::IsRoadMask(int32 X, int32 Y) const
{
	if (!IsValidCoord(X, Y) || !RoadMask.IsValidIndex(GetIndex(X, Y)))
	{
		return false;
	}

	return RoadMask[GetIndex(X, Y)];
}

void ARoadWFCGenerator::SetRoadMask(int32 X, int32 Y, bool bValue)
{
	if (!IsValidCoord(X, Y))
	{
		return;
	}

	RoadMask[GetIndex(X, Y)] = bValue;
}

void ARoadWFCGenerator::CarveHorizontal(int32 Y)
{
	for (int32 X = 0; X < Width; ++X)
	{
		if (!bRoadsTouchBorder && (X == 0 || X == Width - 1 || Y == 0 || Y == Height - 1))
		{
			continue;
		}

		SetRoadMask(X, Y, true);
	}
}

void ARoadWFCGenerator::CarveVertical(int32 X)
{
	for (int32 Y = 0; Y < Height; ++Y)
	{
		if (!bRoadsTouchBorder && (X == 0 || X == Width - 1 || Y == 0 || Y == Height - 1))
		{
			continue;
		}

		SetRoadMask(X, Y, true);
	}
}

void ARoadWFCGenerator::CarveSideStreet(int32 StartX, int32 StartY, int32 DirX, int32 DirY, int32 Length)
{
	int32 X = StartX;
	int32 Y = StartY;

	for (int32 i = 0; i < Length; ++i)
	{
		X += DirX;
		Y += DirY;

		if (!IsValidCoord(X, Y))
		{
			break;
		}

		if (!bRoadsTouchBorder && (X == 0 || X == Width - 1 || Y == 0 || Y == Height - 1))
		{
			break;
		}

		SetRoadMask(X, Y, true);

		if (i > 0)
		{
			const bool bConnectedNorth = IsRoadMask(X, Y + 1) && !(DirX == 0 && DirY == 1);
			const bool bConnectedSouth = IsRoadMask(X, Y - 1) && !(DirX == 0 && DirY == -1);
			const bool bConnectedEast = IsRoadMask(X + 1, Y) && !(DirX == 1 && DirY == 0);
			const bool bConnectedWest = IsRoadMask(X - 1, Y) && !(DirX == -1 && DirY == 0);

			if (bConnectedNorth || bConnectedSouth || bConnectedEast || bConnectedWest)
			{
				break;
			}
		}
	}
}

void ARoadWFCGenerator::SeedRoadSkeleton()
{
	FRandomStream Stream(RandomSeed);

	const TArray<int32> VerticalRoads = BuildAxisRoadPositions(Width, Stream);
	const TArray<int32> HorizontalRoads = BuildAxisRoadPositions(Height, Stream);

	for (int32 X : VerticalRoads)
	{
		CarveVertical(X);
	}

	for (int32 Y : HorizontalRoads)
	{
		CarveHorizontal(Y);
	}

	for (int32 X : VerticalRoads)
	{
		for (int32 Y = 1; Y < Height - 1; ++Y)
		{
			if (!IsRoadMask(X, Y))
			{
				continue;
			}

			if (IsRoadMask(X - 1, Y) || IsRoadMask(X + 1, Y))
			{
				continue;
			}

			if (Stream.FRand() <= SideStreetChance)
			{
				const int32 Length = Stream.RandRange(MinSideStreetLength, MaxSideStreetLength);
				const int32 DirX = Stream.RandRange(0, 1) == 0 ? 1 : -1;
				CarveSideStreet(X, Y, DirX, 0, Length);
			}
		}
	}

	for (int32 Y : HorizontalRoads)
	{
		for (int32 X = 1; X < Width - 1; ++X)
		{
			if (!IsRoadMask(X, Y))
			{
				continue;
			}

			if (IsRoadMask(X, Y - 1) || IsRoadMask(X, Y + 1))
			{
				continue;
			}

			if (Stream.FRand() <= SideStreetChance)
			{
				const int32 Length = Stream.RandRange(MinSideStreetLength, MaxSideStreetLength);
				const int32 DirY = Stream.RandRange(0, 1) == 0 ? -1 : 1;
				CarveSideStreet(X, Y, 0, DirY, Length);
			}
		}
	}

	if (!bAllowDeadEnds)
	{
		TArray<bool> NewMask = RoadMask;

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (!IsRoadMask(X, Y))
				{
					continue;
				}

				int32 Connections = 0;
				Connections += IsRoadMask(X, Y + 1) ? 1 : 0;
				Connections += IsRoadMask(X, Y - 1) ? 1 : 0;
				Connections += IsRoadMask(X + 1, Y) ? 1 : 0;
				Connections += IsRoadMask(X - 1, Y) ? 1 : 0;

				if (Connections <= 1)
				{
					NewMask[GetIndex(X, Y)] = false;
				}
			}
		}

		RoadMask = MoveTemp(NewMask);
	}
}

int32 ARoadWFCGenerator::FindTileIdByConnections(bool bNorth, bool bSouth, bool bWest, bool bEast) const
{
	for (const FRoadTile& Tile : TileRules)
	{
		if (Tile.bRoad &&
			Tile.bIsNorth == bNorth &&
			Tile.bIsSouth == bSouth &&
			Tile.bIsWest == bWest &&
			Tile.bIsEast == bEast)
		{
			return Tile.Id;
		}
	}

	return 0;
}

void ARoadWFCGenerator::ConvertRoadMaskToCollapsedGrid()
{
	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = GetIndex(X, Y);
			FWFCell& Cell = Grid[Index];

			Cell.bCollapsed = true;
			Cell.Tiles.Empty();

			if (!IsRoadMask(X, Y))
			{
				Cell.ChosenTile = 0;
				Cell.Tiles.Add(0);
				continue;
			}

			const bool bNorth = IsRoadMask(X, Y + 1);
			const bool bSouth = IsRoadMask(X, Y - 1);
			const bool bWest = IsRoadMask(X - 1, Y);
			const bool bEast = IsRoadMask(X + 1, Y);

			const int32 TileId = FindTileIdByConnections(bNorth, bSouth, bWest, bEast);
			Cell.ChosenTile = TileId;
			Cell.Tiles.Add(TileId);
		}
	}
}

void ARoadWFCGenerator::BuildResults()
{
	RoadResults.Empty();
	RoadResults.Reserve(Width * Height);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = GetIndex(X, Y);
			if (!Grid.IsValidIndex(Index))
			{
				continue;
			}

			const FWFCell& Cell = Grid[Index];
			if (Cell.Tiles.Num() == 0)
			{
				continue;
			}

			const FRoadTile* Tile = GetTileById(Cell.Tiles[0]);
			if (!Tile)
			{
				continue;
			}

			FRoadCellResult Result;
			Result.GridCoord = FWFCGridCoord(X, Y);
			Result.TileId = Tile->Id;
			Result.TileName = Tile->Name;
			Result.RotationDegrees = Tile->Rotation;
			Result.bIsRoad = Tile->bRoad;
			Result.RoadType = Tile->RoadType;
			Result.WorldLocation = GetWorldPosition(X, Y);
			Result.WorldTransform = FTransform(
				FRotator(0.0f, static_cast<float>(Tile->Rotation), 0.0f),
				Result.WorldLocation,
				FVector(1.0f, 1.0f, 1.0f));

			RoadResults.Add(Result);
		}
	}

#if WITH_EDITOR
	for (const FRoadCellResult& Result : RoadResults)
	{
		if (!Result.bIsRoad)
		{
			continue;
		}

		FColor Color = FColor::Red;
		if (Result.RoadType == ERoadType::DeadEnd) Color = FColor::Cyan;
		else if (Result.RoadType == ERoadType::Straight) Color = FColor::Green;
		else if (Result.RoadType == ERoadType::Corner) Color = FColor::Blue;
		else if (Result.RoadType == ERoadType::TJunction) Color = FColor::Magenta;
		else if (Result.RoadType == ERoadType::Cross) Color = FColor::Yellow;

		DrawDebugSphere(GetWorld(), Result.WorldLocation, 50.0f, 8, Color, false, 5.0f);
		DrawDebugDirectionalArrow(
			GetWorld(),
			Result.WorldLocation,
			Result.WorldLocation + Result.WorldTransform.GetRotation().GetForwardVector() * 200.0f,
			50.0f,
			Color,
			false,
			5.0f);
	}
#endif
}

void ARoadWFCGenerator::ClearRoad()
{
	if (!PCGComponent)
	{
		return;
	}

	if (bUseImmediatePCGCleanup)
	{
		PCGComponent->CleanupLocalImmediate(true);
	}
	else
	{
		PCGComponent->CleanupLocal(true);
	}
}

int32 ARoadWFCGenerator::GetIndex(int32 X, int32 Y) const
{
	return Y * Width + X;
}

bool ARoadWFCGenerator::IsValidCoord(int32 X, int32 Y) const
{
	return X >= 0 && X < Width && Y >= 0 && Y < Height;
}

FVector ARoadWFCGenerator::GetWorldPosition(int32 X, int32 Y) const
{
	return GetActorLocation() + FVector(X * CellSize, Y * CellSize, 0.0f);
}

const FRoadTile* ARoadWFCGenerator::GetTileById(int32 TileId) const
{
	for (const FRoadTile& Tile : TileRules)
	{
		if (Tile.Id == TileId)
		{
			return &Tile;
		}
	}

	return nullptr;
}