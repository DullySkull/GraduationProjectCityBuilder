#include "RoadWFCGenerator.h"

ARoadWFCGenerator::ARoadWFCGenerator()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ARoadWFCGenerator::GenerateRoads()
{
	RoadResults.Empty();
	Grid.Empty();
	TileRules.Empty();

	if (Width <= 0 || Height <= 0 || CellSize <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("RoadWFCGenerator: Width, Height and CellSize must be valid."));
		return;
	}

	CreateTileRules();
	CreateGrid();
	SolveGrid();
	BuildResults();

	UE_LOG(LogTemp, Warning, TEXT("RoadWFCGenerator: Finished generating %d cells."), RoadResults.Num());
}

const TArray<FRoadCellResult>& ARoadWFCGenerator::GetRoadResults() const
{
	return RoadResults;
}

void ARoadWFCGenerator::CreateTileRules()
{
	TileRules.Empty();

	FRoadTile Tile;

	Tile.Id = 0;
	Tile.Name = TEXT("Empty");
	Tile.bIsNorth = false;
	Tile.bIsSouth = false;
	Tile.bIsWest = false;
	Tile.bIsEast = false;
	Tile.Rotation = 0;
	Tile.bRoad = false;
	TileRules.Add(Tile);

	Tile.Id = 1;
	Tile.Name = TEXT("Straight_NS");
	Tile.bIsNorth = true;
	Tile.bIsSouth = true;
	Tile.bIsWest = false;
	Tile.bIsEast = false;
	Tile.Rotation = 0;
	Tile.bRoad = true;
	TileRules.Add(Tile);

	Tile.Id = 2;
	Tile.Name = TEXT("Straight_EW");
	Tile.bIsNorth = false;
	Tile.bIsSouth = false;
	Tile.bIsWest = true;
	Tile.bIsEast = true;
	Tile.Rotation = 90;
	Tile.bRoad = true;
	TileRules.Add(Tile);

	Tile.Id = 3;
	Tile.Name = TEXT("Corner_NE");
	Tile.bIsNorth = true;
	Tile.bIsSouth = false;
	Tile.bIsWest = false;
	Tile.bIsEast = true;
	Tile.Rotation = 0;
	Tile.bRoad = true;
	TileRules.Add(Tile);

	Tile.Id = 4;
	Tile.Name = TEXT("Corner_ES");
	Tile.bIsNorth = false;
	Tile.bIsSouth = true;
	Tile.bIsWest = false;
	Tile.bIsEast = true;
	Tile.Rotation = 90;
	Tile.bRoad = true;
	TileRules.Add(Tile);

	Tile.Id = 5;
	Tile.Name = TEXT("Corner_SW");
	Tile.bIsNorth = false;
	Tile.bIsSouth = true;
	Tile.bIsWest = true;
	Tile.bIsEast = false;
	Tile.Rotation = 180;
	Tile.bRoad = true;
	TileRules.Add(Tile);

	Tile.Id = 6;
	Tile.Name = TEXT("Corner_WN");
	Tile.bIsNorth = true;
	Tile.bIsSouth = false;
	Tile.bIsWest = true;
	Tile.bIsEast = false;
	Tile.Rotation = 270;
	Tile.bRoad = true;
	TileRules.Add(Tile);

	Tile.Id = 7;
	Tile.Name = TEXT("Cross");
	Tile.bIsNorth = true;
	Tile.bIsSouth = true;
	Tile.bIsWest = true;
	Tile.bIsEast = true;
	Tile.Rotation = 0;
	Tile.bRoad = true;
	TileRules.Add(Tile);
}

void ARoadWFCGenerator::CreateGrid()
{
	Grid.SetNum(Width * Height);

	TArray<int32> AllTileIds;
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

void ARoadWFCGenerator::SolveGrid()
{
	FRandomStream Stream(RandomSeed);

	while (true)
	{
		const int32 CellIndex = GetLowestEntropyCell();
		if (CellIndex == INDEX_NONE)
		{
			break;
		}

		CollapseCell(CellIndex);
		Propagate(CellIndex);
	}
}

void ARoadWFCGenerator::CollapseCell(int32 CellIndex)
{
	if (!Grid.IsValidIndex(CellIndex))
	{
		return;
	}

	FRandomStream Stream(RandomSeed + CellIndex);

	FWFCell& Cell = Grid[CellIndex];
	if (Cell.Tiles.Num() == 0)
	{
		return;
	}

	const int32 Chosen = ChooseRandomTile(Cell.Tiles, Stream);
	Cell.bCollapsed = true;
	Cell.ChosenTile = Chosen;
	Cell.Tiles.Empty();
	Cell.Tiles.Add(Chosen);
}

void ARoadWFCGenerator::Propagate(int32 StartCellIndex)
{
	TArray<int32> OpenList;
	OpenList.Add(StartCellIndex);

	int32 CurrentIndex = 0;

	while (CurrentIndex < OpenList.Num())
	{
		const int32 SourceIndex = OpenList[CurrentIndex];
		CurrentIndex++;

		if (!Grid.IsValidIndex(SourceIndex))
		{
			continue;
		}

		const FWFCell& SourceCell = Grid[SourceIndex];
		if (SourceCell.Tiles.Num() == 0)
		{
			continue;
		}

		const TArray<int32> NeighborIndices = GetNeighborIndices(SourceIndex);

		for (int32 NeighborIndex : NeighborIndices)
		{
			if (!Grid.IsValidIndex(NeighborIndex))
			{
				continue;
			}

			FWFCell& NeighborCell = Grid[NeighborIndex];
			if (NeighborCell.Tiles.Num() == 0)
			{
				continue;
			}

			const ERoadDirection Direction = GetDirectionBetween(SourceIndex, NeighborIndex);

			TArray<int32> NewPossibleTiles;

			for (int32 NeighborTileId : NeighborCell.Tiles)
			{
				const FRoadTile* NeighborTile = GetTileById(NeighborTileId);
				if (!NeighborTile)
				{
					continue;
				}

				bool bValid = false;

				for (int32 SourceTileId : SourceCell.Tiles)
				{
					const FRoadTile* SourceTile = GetTileById(SourceTileId);
					if (!SourceTile)
					{
						continue;
					}

					if (AreTilesCompatible(*SourceTile, *NeighborTile, Direction))
					{
						bValid = true;
						break;
					}
				}

				if (bValid)
				{
					NewPossibleTiles.Add(NeighborTileId);
				}
			}

			if (NewPossibleTiles.Num() > 0 && NewPossibleTiles.Num() < NeighborCell.Tiles.Num())
			{
				NeighborCell.Tiles = NewPossibleTiles;

				if (NeighborCell.Tiles.Num() == 1)
				{
					NeighborCell.bCollapsed = true;
					NeighborCell.ChosenTile = NeighborCell.Tiles[0];
				}

				OpenList.Add(NeighborIndex);
			}
		}
	}
}

void ARoadWFCGenerator::BuildResults()
{
	RoadResults.Empty();

	for (int32 Y = 0; Y < Height; Y++)
	{
		for (int32 X = 0; X < Width; X++)
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
			Result.WorldLocation = GetWorldPosition(X, Y);

			RoadResults.Add(Result);
		}
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

TArray<int32> ARoadWFCGenerator::GetNeighborIndices(int32 CellIndex) const
{
	TArray<int32> Neighbors;

	const int32 X = CellIndex % Width;
	const int32 Y = CellIndex / Width;

	if (IsValidCoord(X, Y + 1))
	{
		Neighbors.Add(GetIndex(X, Y + 1));
	}

	if (IsValidCoord(X + 1, Y))
	{
		Neighbors.Add(GetIndex(X + 1, Y));
	}

	if (IsValidCoord(X, Y - 1))
	{
		Neighbors.Add(GetIndex(X, Y - 1));
	}

	if (IsValidCoord(X - 1, Y))
	{
		Neighbors.Add(GetIndex(X - 1, Y));
	}

	return Neighbors;
}

ERoadDirection ARoadWFCGenerator::GetDirectionBetween(int32 FromIndex, int32 ToIndex) const
{
	const int32 FromX = FromIndex % Width;
	const int32 FromY = FromIndex / Width;
	const int32 ToX = ToIndex % Width;
	const int32 ToY = ToIndex / Width;

	if (ToX == FromX && ToY == FromY + 1)
	{
		return ERoadDirection::North;
	}

	if (ToX == FromX + 1 && ToY == FromY)
	{
		return ERoadDirection::East;
	}

	if (ToX == FromX && ToY == FromY - 1)
	{
		return ERoadDirection::South;
	}

	return ERoadDirection::West;
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

bool ARoadWFCGenerator::AreTilesCompatible(const FRoadTile& A, const FRoadTile& B, ERoadDirection Direction) const
{
	switch (Direction)
	{
	case ERoadDirection::North:
		return A.bIsNorth == B.bIsSouth;

	case ERoadDirection::East:
		return A.bIsEast == B.bIsWest;

	case ERoadDirection::South:
		return A.bIsSouth == B.bIsNorth;

	case ERoadDirection::West:
		return A.bIsWest == B.bIsEast;

	default:
		return false;
	}
}

int32 ARoadWFCGenerator::GetLowestEntropyCell() const
{
	int32 BestIndex = INDEX_NONE;
	int32 BestEntropy = TNumericLimits<int32>::Max();

	for (int32 i = 0; i < Grid.Num(); i++)
	{
		const FWFCell& Cell = Grid[i];

		if (Cell.bCollapsed)
		{
			continue;
		}

		const int32 Entropy = Cell.GetEntropy();

		if (Entropy > 0 && Entropy < BestEntropy)
		{
			BestEntropy = Entropy;
			BestIndex = i;
		}
	}

	return BestIndex;
}

int32 ARoadWFCGenerator::ChooseRandomTile(const TArray<int32>& PossibleTiles, FRandomStream& Stream) const
{
	if (PossibleTiles.Num() == 0)
	{
		return -1;
	}

	const int32 RandomIndex = Stream.RandRange(0, PossibleTiles.Num() - 1);
	return PossibleTiles[RandomIndex];
}