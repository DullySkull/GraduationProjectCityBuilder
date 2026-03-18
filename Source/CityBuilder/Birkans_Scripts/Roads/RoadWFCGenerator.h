#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WFCData.h"
#include "PCGComponent.h"
#include "Components/BoxComponent.h"
#include "RoadWFCGenerator.generated.h"

UCLASS()
class CITYBUILDER_API ARoadWFCGenerator : public AActor
{
	GENERATED_BODY()

public:
	ARoadWFCGenerator();

	virtual void BeginPlay() override;

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "WFC")
	void GenerateRoads();

	UFUNCTION(CallInEditor, BlueprintCallable, Category = "WFC")
	void ClearRoads();

	UFUNCTION(BlueprintCallable, Category = "WFC")
	const TArray<FRoadCellResult>& GetRoadResults() const;

	UFUNCTION(BlueprintCallable, Category = "WFC")
	TArray<FRoadCellResult> GetOnlyRoadCells() const;

	UFUNCTION(BlueprintCallable, Category = "WFC")
	TArray<FVector> GetRoadPoints() const;

	UFUNCTION(BlueprintCallable, Category = "WFC")
	TArray<FTransform> GetRoadTransforms() const;

	UFUNCTION(BlueprintCallable, Category = "WFC")
	TArray<FRoadCellResult> GetRoadCellsByType(ERoadType InRoadType) const;

	UFUNCTION(BlueprintCallable, Category = "WFC")
	TArray<FTransform> GetRoadTransformsByType(ERoadType InRoadType) const;

	UFUNCTION(BlueprintCallable, Category = "WFC")
	void RefreshPCG();

protected:
	// Components
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UPCGComponent* PCGComponent;
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBoxComponent* PCGBoundsBox;

	// WFC Settings
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Width = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Height = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	float CellSize = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 RandomSeed = 67;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bGenerateOnBeginPlay = true;

	// City layout settings
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	int32 MinBlockSize = 4;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	int32 MaxBlockSize = 7;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	float SideStreetChance = 0.20f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	int32 MinSideStreetLength = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	int32 MaxSideStreetLength = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	bool bAllowDeadEnds = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "City Layout")
	bool bRoadsTouchBorder = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PCG")
	bool bUseImmediatePCGCleanup = true;

	UPROPERTY(BlueprintReadOnly, Category = "WFC")
	TArray<FRoadCellResult> RoadResults;
	
	TArray<FRoadTile> TileRules;
	TArray<FWFCell> Grid;
	TArray<bool> RoadMask;

	// Main flow
	void GenerateRoadsInternal(bool bShouldRefreshPCG);
	void CreateTileRules();
	void CreateGrid();
	void SeedRoadSkeleton();
	void BuildResults();
	void ClearRoad();

	// Grid helpers
	int32 GetIndex(int32 X, int32 Y) const;
	bool IsValidCoord(int32 X, int32 Y) const;
	FVector GetWorldPosition(int32 X, int32 Y) const;

	// Road Masks
	bool IsRoadMask(int32 X, int32 Y) const;
	void SetRoadMask(int32 X, int32 Y, bool bValue = true);
	void CarveHorizontal(int32 Y);
	void CarveVertical(int32 X);
	void CarveSideStreet(int32 StartX, int32 StartY, int32 DirX, int32 DirY, int32 Length);
	TArray<int32> BuildAxisRoadPositions(int32 Size, FRandomStream& Stream) const;
	void ConvertRoadMaskToCollapsedGrid();

	int32 FindTileIdByConnections(bool bNorth, bool bSouth, bool bWest, bool bEast) const;
	const FRoadTile* GetTileById(int32 TileId) const;
};
