// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityBuildToolEditorMode.h"
#include "CityBuildToolEditorModeToolkit.h"
#include "InteractiveToolManager.h"
#include "CityBuildToolEditorModeCommands.h"
#include "Modules/ModuleManager.h"


//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
// AddYourTool Step 1 - include the header file for your Tools here
//////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////// 
#include "CityBuildTool/Tools/BuildingPlacementToolBuilder.h"
#include "CityBuildTool/Tools/RoadBuildingTool.h"
// step 2: register a ToolBuilder in FCityBuildToolEditorMode::Enter() below


#define LOCTEXT_NAMESPACE "CityBuildToolEditorMode"

const FEditorModeID UCityBuildToolEditorMode::EM_CityBuildToolEditorModeId = TEXT("EM_CityBuildToolEditorMode");

FString UCityBuildToolEditorMode::SimpleToolName = TEXT("CityBuildTool_ActorInfoTool");
FString UCityBuildToolEditorMode::InteractiveToolName = TEXT("CityBuildTool_MeasureDistanceTool");


UCityBuildToolEditorMode::UCityBuildToolEditorMode()
{
	FModuleManager::Get().LoadModule("EditorStyle");

	// appearance and icon in the editing mode ribbon can be customized here
	Info = FEditorModeInfo(UCityBuildToolEditorMode::EM_CityBuildToolEditorModeId,
	                       LOCTEXT("ModeName", "CityBuildTool"),
	                       FSlateIcon(),
	                       true);
}


UCityBuildToolEditorMode::~UCityBuildToolEditorMode()
{
}


void UCityBuildToolEditorMode::ActorSelectionChangeNotify()
{
}

void UCityBuildToolEditorMode::Enter()
{
	UEdMode::Enter();

	//////////////////////////////////////////////////////////////////////////
	//////////////////////////////////////////////////////////////////////////
	// AddYourTool Step 2 - register the ToolBuilders for your Tools here.
	// The string name you pass to the ToolManager is used to select/activate your ToolBuilder later.
	//////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////// 
	const FCityBuildToolEditorModeCommands& ModeCommands = FCityBuildToolEditorModeCommands::Get();

	// Register the Road Tool
	RegisterTool(FCityBuildToolEditorModeCommands::Get().BuildRoad,
	             TEXT("RoadTool"), NewObject<URoadBuildingToolBuilder>(this));

	// Register the Building Tool
	RegisterTool(FCityBuildToolEditorModeCommands::Get().BuildBuilding,
	             TEXT("BuildingTool"), NewObject<UBuildingPlacementToolBuilder>(this));

	GetToolManager()->SelectActiveToolType(EToolSide::Left, TEXT("BuildRoadTool"));
}

void UCityBuildToolEditorMode::Exit()
{
	URoadBuildingTool* RoadTool = Cast<URoadBuildingTool>(GetToolManager()->GetActiveTool(EToolSide::Left));
	if (RoadTool)
	{
		RoadTool->CancelCurrentRoad();
	}

	UEdMode::Exit();
}

void UCityBuildToolEditorMode::CreateToolkit()
{
	Toolkit = MakeShareable(new FCityBuildToolEditorModeToolkit);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> UCityBuildToolEditorMode::GetModeCommands() const
{
	return FCityBuildToolEditorModeCommands::Get().GetCommands();
}

void UCityBuildToolEditorMode::OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
	// Nothing extra needed – the tool registers its own input behaviors in Setup()
}

void UCityBuildToolEditorMode::OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool)
{
}

#undef LOCTEXT_NAMESPACE
