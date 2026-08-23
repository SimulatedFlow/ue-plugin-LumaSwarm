// Copyright 2026 Silvan Teufel. All Rights Reserved.

#include "LumaSwarmSettings.h"

ULumaSwarmSettings::ULumaSwarmSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("LumaSwarm");
}

FName ULumaSwarmSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

FName ULumaSwarmSettings::GetSectionName() const
{
	return TEXT("LumaSwarm");
}

const ULumaSwarmSettings& ULumaSwarmSettings::Get()
{
	const ULumaSwarmSettings* Settings = GetDefault<ULumaSwarmSettings>();
	check(Settings);
	return *Settings;
}
