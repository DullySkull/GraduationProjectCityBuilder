#pragma once

#include "CoreMinimal.h"
#include "InteractiveTool.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolManager.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "Components/SplineComponent.h"
#include "Engine/StaticMesh.h"
#include "RoadBuildingTool.generated.h"

UCLASS()
class CITYBUILDTOOL_API ARoadSegmentActor : public AActor
{
	GENERATED_BODY()

public:
	ARoadSegmentActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Road")
	USplineComponent* SplineComponent;

	TArray<FVector> SplinePoints;

	void AddPoint(const FVector& WorldPoint);
	void UpdateLastPoint(const FVector& WorldPoint);
	void FinaliseRoad();

	bool bIsClosed = false;
};

UENUM(BlueprintType)
enum class ERoadIntersectionType : uint8
{
	None UMETA(DisplayName = "None"),
	DeadEnd UMETA(DisplayName = "Dead End / Cap"),
	Corner UMETA(DisplayName = "Corner / Bend"),
	TJunction UMETA(DisplayName = "T-Junction"),
	Crossroads UMETA(DisplayName = "Crossroads (4-way)"),
};

UCLASS()
class CITYBUILDTOOL_API ARoadIntersectionActor : public AActor
{
	GENERATED_BODY()

public:
	ARoadIntersectionActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	UStaticMeshComponent* MeshComponent;

	ERoadIntersectionType IntersectionType = ERoadIntersectionType::None;
	int32 ConnectionCount = 0;

	void SetMesh(UStaticMesh* Mesh);
};

UCLASS(Transient)
class CITYBUILDTOOL_API URoadBuildingToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = "Road Meshes")
	UStaticMesh* StraightRoadMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Intersection Meshes")
	UStaticMesh* DeadEndMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Intersection Meshes")
	UStaticMesh* CornerMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Intersection Meshes")
	UStaticMesh* TJunctionMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Intersection Meshes")
	UStaticMesh* CrossroadsMesh = nullptr;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "-5000", ClampMax = "5000"))
	float HeightOffset = 0.f;

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "1", ClampMax = "500"))
	float SnapRadius = 50.f;

	UPROPERTY(EditAnywhere, Category = "Options")
	FVector MeshScale = FVector(1.f, 1.f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "1"))
	float RoadMeshBaseLength = 500.f;

	// How strict the tool should be before treating a 2-connection node as a real 90 degree corner.
	// Lower = stricter. Around 0.25 to 0.35 works well.
	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CornerPerpendicularDotTolerance = 0.35f;

	// If two directions are almost opposite, treat as straight and do not place a corner.
	UPROPERTY(EditAnywhere, Category = "Options", meta = (ClampMin = "-1.0", ClampMax = "0.0"))
	float StraightOppositeDotThreshold = -0.85f;
};

struct FRoadNode
{
	FVector Location;
	TArray<int32> ConnectedNodes;
	ARoadIntersectionActor* IntersectionActor = nullptr;
};

UCLASS()
class CITYBUILDTOOL_API URoadBuildingTool : public UInteractiveTool,
                                            public IClickBehaviorTarget,
                                            public IHoverBehaviorTarget
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Shutdown(EToolShutdownType ShutdownType) override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;
	virtual void OnTick(float DeltaTime) override;

	virtual FInputRayHit IsHitByClick(const FInputDeviceRay& ClickPos) override;
	virtual void OnClicked(const FInputDeviceRay& ClickPos) override;

	virtual FInputRayHit BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos) override;
	virtual void OnBeginHover(const FInputDeviceRay& DevicePos) override;
	virtual bool OnUpdateHover(const FInputDeviceRay& DevicePos) override;
	virtual void OnEndHover() override;

	void OnDoubleClicked(const FInputDeviceRay& ClickPos);
	void AdjustHeight(float Delta);
	void CancelCurrentRoad();

private:
	enum ERoadDirMask : uint8
	{
		Dir_None = 0,
		Dir_North = 1 << 0, // +X
		Dir_East = 1 << 1, // +Y
		Dir_South = 1 << 2, // -X
		Dir_West = 1 << 3 // -Y
	};

	UPROPERTY()
	URoadBuildingToolProperties* Properties = nullptr;

	bool bIsDrawing = false;
	FVector CurrentMousePoint = FVector::ZeroVector;
	ARoadSegmentActor* ActiveRoad = nullptr;

	double LastClickTime = 0.0;
	static constexpr double DoubleClickThresholdSec = 0.35;

	TArray<FRoadNode> RoadNodes;
	TArray<ARoadSegmentActor*> AllRoads;

	FVector TraceToFloor(const FRay& WorldRay) const;
	int32 FindNearbyNode(const FVector& Location) const;
	int32 GetOrCreateNode(const FVector& Location);
	void ConnectNodes(int32 A, int32 B);
	void UpdateIntersectionActor(int32 NodeIndex);
	void CheckSplineIntersections(const FVector& NewPoint, int32 NewNodeIndex);

	uint8 BuildDirectionMask(int32 NodeIndex) const;
	int32 CountBits(uint8 Mask) const;
	bool IsStraightMask(uint8 Mask) const;
	bool IsValidCornerMask(uint8 Mask) const;

	ERoadIntersectionType ClassifyNodeFromMask(int32 NodeIndex, uint8 Mask) const;
	UStaticMesh* MeshForType(ERoadIntersectionType Type) const;
	float RotationFromMask(uint8 Mask, ERoadIntersectionType Type) const;

	void SpawnRoadSegmentMesh(const FVector& A, const FVector& B);
	void DrawPreviewLine(IToolsContextRenderAPI* RenderAPI) const;
	void CommitNode(const FVector& WorldLocation);
};

UCLASS()
class CITYBUILDTOOL_API URoadBuildingToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override { return true; }

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override
	{
		return NewObject<URoadBuildingTool>(SceneState.ToolManager);
	}
};
