// Copyright Epic Games, Inc. All Rights Reserved.

#include "CityBuildToolEditorModeCommands.h"
#include "CityBuildToolEditorMode.h"
#include "EditorStyleSet.h"

#define LOCTEXT_NAMESPACE "CityBuildToolEditorModeCommands"

FCityBuildToolEditorModeCommands::FCityBuildToolEditorModeCommands()
	: TCommands<FCityBuildToolEditorModeCommands>("CityBuildToolEditorMode",
		NSLOCTEXT("CityBuildToolEditorMode", "CityBuildToolEditorModeCommands", "CityBuildTool Editor Mode"),
		NAME_None,
		FAppStyle::GetAppStyleSetName())
{
}

void FCityBuildToolEditorModeCommands::RegisterCommands()
{
	TArray <TSharedPtr<FUICommandInfo>>& ToolCommands = Commands.FindOrAdd(NAME_Default);

	UI_COMMAND(BuildRoad, "Build Road", "Click to start drawing roads.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(BuildRoad);
	
	UI_COMMAND(BuildBuilding, "Build Building", "Click to place buildings.", EUserInterfaceActionType::ToggleButton, FInputChord());
	ToolCommands.Add(BuildBuilding);
}

TMap<FName, TArray<TSharedPtr<FUICommandInfo>>> FCityBuildToolEditorModeCommands::GetCommands()
{
	return FCityBuildToolEditorModeCommands::Get().Commands;
}

#undef LOCTEXT_NAMESPACE
