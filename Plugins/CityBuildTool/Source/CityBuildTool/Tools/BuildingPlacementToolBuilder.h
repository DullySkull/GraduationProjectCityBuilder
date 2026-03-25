#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"
#include "InteractiveToolManager.h"
#include "BuildingPlacementToolBuilder.generated.h"

UCLASS()
class CITYBUILDTOOL_API UBuildingPlacementTool : public UInteractiveTool
{
	GENERATED_BODY()

public:
	virtual void Setup() override;
	virtual void Render(IToolsContextRenderAPI* RenderAPI) override;

	UPROPERTY()
	UStaticMesh* SelectedBuildingMesh;
};

UCLASS()
class CITYBUILDTOOL_API UBuildingPlacementToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override { return true; }

	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override
	{
		return NewObject<UBuildingPlacementTool>(SceneState.ToolManager);
	}
};
