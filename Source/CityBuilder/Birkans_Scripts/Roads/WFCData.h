#pragma once

#include "CoreMinimal.h"
#include "WFCData.generated.h"

UENUM(BlueprintType)
enum class ERoadDirection : uint8
{
	North, East, South, West
};

USTRUCT(BlueprintType)
struct FRoadTile
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Id = -1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	FString Name;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bIsNorth = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bIsSouth = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bIsWest = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bIsEast = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Rotation = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bRoad = false;
	
	// Maybe switch state to give each case direction
};

USTRUCT(BlueprintType)
struct FWFCGridCoord
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 X = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 Y = 0;
	
	FWFCGridCoord() {};
	FWFCGridCoord(int32 InX, int32 InY) : X(InX), Y(InY) {};
	
	bool operator==(const FWFCGridCoord& Other) const
	{
		return X == Other.X && Y == Other.Y;
	}
};

USTRUCT(BlueprintType)
struct FWFCell
{
	GENERATED_BODY()

	bool bCollapsed = false;
	TArray<int32> Tiles;
	int32 ChosenTile = -1;
	int32 GetEntropy() const
	{
		return bCollapsed ? 0 : Tiles.Num();
	}
};

USTRUCT(BlueprintType)
struct FRoadCellResult
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	FWFCGridCoord GridCoord;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 TileId = -1;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	FString TileName = TEXT("");
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	int32 RotationDegrees = 0;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	bool bIsRoad = false;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WFC")
	FVector WorldLocation = FVector::ZeroVector;
};
