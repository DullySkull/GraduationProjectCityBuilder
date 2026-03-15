#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WFCData.h"
#include "RoadWFCGenerator.generated.h"

UCLASS()
class CITYBUILDER_API ARoadWFCGenerator : public AActor
{
	GENERATED_BODY()

public:
	ARoadWFCGenerator();
	
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "WFC")
	void GenerateRoads();

	UFUNCTION(BlueprintCallable, Category = "WFC")
	const TArray<FRoadCellResult>& GetRoadResults() const;

protected:
	
	// Settings
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Width = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Height = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	float CellSize = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 RandomSeed = 67;

	UPROPERTY(BlueprintReadOnly, Category = "WFC")
	TArray<FRoadCellResult> RoadResults;
	
	TArray<FRoadTile> TileRules;
	
	TArray<FWFCell> Grid;

	// Helpers
	void CreateTileRules();
	void CreateGrid();
	void SolveGrid();
	void CollapseCell(int32 CellIndex);
	void Propagate(int32 StartCellIndex);
	void BuildResults();

	int32 GetIndex(int32 X, int32 Y) const;
	bool IsValidCoord(int32 X, int32 Y) const;
	FVector GetWorldPosition(int32 X, int32 Y) const;

	TArray<int32> GetNeighborIndices(int32 CellIndex) const;
	ERoadDirection GetDirectionBetween(int32 FromIndex, int32 ToIndex) const;

	const FRoadTile* GetTileById(int32 TileId) const;
	bool AreTilesCompatible(const FRoadTile& A, const FRoadTile& B, ERoadDirection Direction) const;

	int32 GetLowestEntropyCell() const;
	int32 ChooseRandomTile(const TArray<int32>& PossibleTiles, FRandomStream& Stream) const;
};
