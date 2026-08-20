// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace AILOD::LogSchema
{
	struct FFieldDefinition
	{
		const TCHAR* Name;
		const TCHAR* Type;
	};

	inline constexpr TCHAR InitialPopulationManifestFile[] = TEXT("initial_population_manifest.json");
	inline constexpr TCHAR EarthquakeDamageListFile[] = TEXT("earthquake_damage_list.json");
	inline constexpr TCHAR PersistentTestPoolFile[] = TEXT("persistent_test_pool.json");
	inline constexpr TCHAR RunManifestFile[] = TEXT("run_manifest.json");
	inline constexpr TCHAR KingdomTimeseriesFile[] = TEXT("kingdom_timeseries.csv");
	inline constexpr TCHAR CohortTimeseriesFile[] = TEXT("cohort_timeseries.csv");
	inline constexpr TCHAR NPCSnapshotsFile[] = TEXT("npc_snapshots.csv");
	inline constexpr TCHAR SimulationEventsFile[] = TEXT("simulation_events.jsonl");
	inline constexpr TCHAR LODTransitionsFile[] = TEXT("lod_transitions.jsonl");
	inline constexpr TCHAR LedgerTransactionsFile[] = TEXT("ledger_transactions.jsonl");
	inline constexpr TCHAR PerformanceFile[] = TEXT("performance_1s.csv");
	inline constexpr TCHAR MetricsSummaryFile[] = TEXT("metrics_summary.csv");

	inline constexpr FFieldDefinition PreRunCommonFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") },
		{ TEXT("spec_version"), TEXT("string") },
		{ TEXT("seed"), TEXT("int32") },
		{ TEXT("config_hash"), TEXT("sha1") }
	};

	inline constexpr FFieldDefinition CommonFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") },
		{ TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") },
		{ TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") },
		{ TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }
	};

	inline constexpr FFieldDefinition InitialPopulationFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("spec_version"), TEXT("string") },
		{ TEXT("seed"), TEXT("int32") }, { TEXT("config_hash"), TEXT("sha1") },
		{ TEXT("population_per_kingdom"), TEXT("int32") }, { TEXT("residents"), TEXT("array") },
		{ TEXT("resident_id"), TEXT("int64") }, { TEXT("home_id"), TEXT("int64") },
		{ TEXT("persistent_id"), TEXT("int64") }, { TEXT("name"), TEXT("string") },
		{ TEXT("kingdom"), TEXT("enum") }, { TEXT("profession"), TEXT("enum") },
		{ TEXT("income_band"), TEXT("enum") }, { TEXT("cash"), TEXT("int32") },
		{ TEXT("repair_credit"), TEXT("int32") }, { TEXT("inventory_wood"), TEXT("int32") },
		{ TEXT("home_state"), TEXT("enum") }, { TEXT("event_id"), TEXT("int64") },
		{ TEXT("arrive_id"), TEXT("int64") }
	};

	inline constexpr FFieldDefinition EarthquakeDamageFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("spec_version"), TEXT("string") },
		{ TEXT("seed"), TEXT("int32") }, { TEXT("config_hash"), TEXT("sha1") },
		{ TEXT("damaged_residents"), TEXT("array") }, { TEXT("resident_id"), TEXT("int64") },
		{ TEXT("home_id"), TEXT("int64") }, { TEXT("profession"), TEXT("enum") },
		{ TEXT("income_band"), TEXT("enum") }
	};

	inline constexpr FFieldDefinition PersistentTestPoolFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("spec_version"), TEXT("string") },
		{ TEXT("seed"), TEXT("int32") }, { TEXT("config_hash"), TEXT("sha1") },
		{ TEXT("persistent_residents"), TEXT("array") }, { TEXT("resident_id"), TEXT("int64") },
		{ TEXT("home_id"), TEXT("int64") }, { TEXT("persistent_id"), TEXT("int64") },
		{ TEXT("name"), TEXT("string") }, { TEXT("kingdom"), TEXT("enum") },
		{ TEXT("profession"), TEXT("enum") }, { TEXT("income_band"), TEXT("enum") },
		{ TEXT("day7"), TEXT("bool") }, { TEXT("day30"), TEXT("bool") },
		{ TEXT("day45"), TEXT("bool") }
	};

	inline constexpr FFieldDefinition RunManifestFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("spec_version"), TEXT("string") },
		{ TEXT("config_hash"), TEXT("sha1") }, { TEXT("population_manifest_sha256"), TEXT("sha256") },
		{ TEXT("damage_list_sha256"), TEXT("sha256") }, { TEXT("persistent_pool_sha256"), TEXT("sha256") },
		{ TEXT("git_commit"), TEXT("string") }, { TEXT("ue_version"), TEXT("string") },
		{ TEXT("build_type"), TEXT("string") }, { TEXT("hardware"), TEXT("string") },
		{ TEXT("log_mode"), TEXT("string") },
		{ TEXT("start_time"), TEXT("string") }, { TEXT("end_time"), TEXT("string") },
		{ TEXT("valid"), TEXT("bool") },
		{ TEXT("proposed_model_version"), TEXT("string") },
		{ TEXT("authoritative_model_version"), TEXT("string") },
		{ TEXT("authority_mode"), TEXT("string") },
		{ TEXT("joint_state_version"), TEXT("string") },
		{ TEXT("claim_allocation_version"), TEXT("string") },
		{ TEXT("capsule_version"), TEXT("string") },
		{ TEXT("valid_for_formal_experiment"), TEXT("bool") }
	};

	inline constexpr FFieldDefinition KingdomTimeseriesFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("kingdom"), TEXT("enum") },
		{ TEXT("forest_wood"), TEXT("double") }, { TEXT("market_wood_available"), TEXT("double") },
		{ TEXT("market_wood_reserved"), TEXT("double") }, { TEXT("wood_in_transit"), TEXT("double") },
		{ TEXT("wood_embedded_in_repairs"), TEXT("double") }, { TEXT("wood_in_repaired_homes"), TEXT("double") },
		{ TEXT("treasury_available"), TEXT("int64") }, { TEXT("treasury_reserved"), TEXT("int64") },
		{ TEXT("market_coin"), TEXT("int64") }, { TEXT("wood_price"), TEXT("double") },
		{ TEXT("healthy"), TEXT("int32") }, { TEXT("damaged_waiting"), TEXT("int32") },
		{ TEXT("under_repair"), TEXT("int32") }, { TEXT("repaired"), TEXT("int32") },
		{ TEXT("policy_state"), TEXT("string") }
	};

	inline constexpr FFieldDefinition CohortTimeseriesFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("cohort_key"), TEXT("string") },
		{ TEXT("count"), TEXT("int32") }, { TEXT("cash_sum"), TEXT("int64") },
		{ TEXT("cash_squared_sum"), TEXT("int64") }, { TEXT("repair_credit_sum"), TEXT("int64") },
		{ TEXT("wood_count_0"), TEXT("int32") }, { TEXT("wood_count_1"), TEXT("int32") },
		{ TEXT("wood_count_2"), TEXT("int32") }, { TEXT("wood_count_3"), TEXT("int32") },
		{ TEXT("wood_count_4_plus"), TEXT("int32") }, { TEXT("macro_intent"), TEXT("enum") }
	};

	inline constexpr FFieldDefinition NPCSnapshotFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("persistent_id"), TEXT("int64") },
		{ TEXT("name"), TEXT("string") }, { TEXT("home_id"), TEXT("int64") },
		{ TEXT("kingdom"), TEXT("enum") }, { TEXT("profession"), TEXT("enum") },
		{ TEXT("income_band"), TEXT("enum") }, { TEXT("money"), TEXT("int32") },
		{ TEXT("repair_credit"), TEXT("int32") }, { TEXT("inventory_wood"), TEXT("int32") },
		{ TEXT("home_state"), TEXT("enum") }, { TEXT("current_goal"), TEXT("enum") },
		{ TEXT("last_completed_action"), TEXT("enum") }, { TEXT("macro_intent"), TEXT("enum") },
		{ TEXT("event_id"), TEXT("int64") }, { TEXT("reservation_id"), TEXT("int64") },
		{ TEXT("remaining_work_hours"), TEXT("double") }, { TEXT("first_action"), TEXT("enum") }
	};

	inline constexpr FFieldDefinition SimulationEventFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("event_id"), TEXT("int64") },
		{ TEXT("arrive_id"), TEXT("int64") }, { TEXT("parent_event_id"), TEXT("int64") },
		{ TEXT("type"), TEXT("enum") }, { TEXT("owner"), TEXT("string") },
		{ TEXT("start_time"), TEXT("string") }, { TEXT("end_time"), TEXT("string") },
		{ TEXT("participants"), TEXT("int32") }, { TEXT("cause"), TEXT("string") },
		{ TEXT("policy_id"), TEXT("int64") }
	};

	inline constexpr FFieldDefinition LODTransitionFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("persistent_id"), TEXT("int64") },
		{ TEXT("from"), TEXT("enum") }, { TEXT("to"), TEXT("enum") },
		{ TEXT("requested_time"), TEXT("string") }, { TEXT("committed_time"), TEXT("string") },
		{ TEXT("latency_ms"), TEXT("double") }, { TEXT("bucket"), TEXT("string") },
		{ TEXT("arrive_id"), TEXT("int64") }, { TEXT("result"), TEXT("enum") }
	};

	inline constexpr FFieldDefinition LedgerTransactionFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("transaction_id"), TEXT("int64") },
		{ TEXT("idempotency_key"), TEXT("string") }, { TEXT("event_id"), TEXT("int64") },
		{ TEXT("arrive_id"), TEXT("int64") }, { TEXT("resource"), TEXT("enum") },
		{ TEXT("source"), TEXT("string") }, { TEXT("destination"), TEXT("string") },
		{ TEXT("quantity"), TEXT("double") }, { TEXT("boundary_flag"), TEXT("bool") },
		{ TEXT("policy_id"), TEXT("int64") }
	};

	inline constexpr FFieldDefinition PerformanceFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("ai_cpu_ms"), TEXT("double") },
		{ TEXT("macro_cpu_ms"), TEXT("double") }, { TEXT("micro_cpu_ms"), TEXT("double") },
		{ TEXT("transition_cpu_ms"), TEXT("double") }, { TEXT("memory_mb"), TEXT("double") },
		{ TEXT("active_count"), TEXT("int32") }, { TEXT("queue_length"), TEXT("int32") }
	};

	inline constexpr FFieldDefinition MetricsSummaryFields[] =
	{
		{ TEXT("schema_version"), TEXT("string") }, { TEXT("experiment_id"), TEXT("string") },
		{ TEXT("run_id"), TEXT("string") }, { TEXT("method"), TEXT("string") },
		{ TEXT("scenario"), TEXT("string") }, { TEXT("seed"), TEXT("int32") },
		{ TEXT("game_time"), TEXT("string") }, { TEXT("metric_name"), TEXT("string") },
		{ TEXT("metric_value"), TEXT("double") }, { TEXT("scale"), TEXT("string") },
		{ TEXT("oracle_pair_run_id"), TEXT("string") }, { TEXT("ci_input"), TEXT("string") }
	};
}
