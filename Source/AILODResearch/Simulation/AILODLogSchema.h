// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AILOD::LogSchema
{
	inline constexpr TCHAR InitialPopulationManifestFile[] = TEXT("initial_population_manifest.json");
	inline constexpr TCHAR EarthquakeDamageListFile[] = TEXT("earthquake_damage_list.json");
	inline constexpr TCHAR RunManifestFile[] = TEXT("run_manifest.json");
	inline constexpr TCHAR KingdomTimeseriesFile[] = TEXT("kingdom_timeseries.csv");
	inline constexpr TCHAR CohortTimeseriesFile[] = TEXT("cohort_timeseries.csv");
	inline constexpr TCHAR NPCSnapshotsFile[] = TEXT("npc_snapshots.csv");
	inline constexpr TCHAR SimulationEventsFile[] = TEXT("simulation_events.jsonl");
	inline constexpr TCHAR LODTransitionsFile[] = TEXT("lod_transitions.jsonl");
	inline constexpr TCHAR LedgerTransactionsFile[] = TEXT("ledger_transactions.jsonl");
	inline constexpr TCHAR PerformanceFile[] = TEXT("performance_1s.csv");
	inline constexpr TCHAR MetricsSummaryFile[] = TEXT("metrics_summary.csv");

	inline constexpr const TCHAR* CommonFields[] =
	{
		TEXT("schema_version"),
		TEXT("experiment_id"),
		TEXT("run_id"),
		TEXT("method"),
		TEXT("scenario"),
		TEXT("seed"),
		TEXT("game_time")
	};
}
