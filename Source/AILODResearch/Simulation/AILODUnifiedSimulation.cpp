// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODUnifiedSimulation.h"

#include "AILODDomainRules.h"
#include "AILODPhase0Manifest.h"
#include "HAL/PlatformTime.h"
#include "Misc/SecureHash.h"

namespace AILOD
{
	namespace
	{
		using namespace DomainRules;

		int32 KingdomIndex(const EKingdom Kingdom)
		{
			return Kingdom == EKingdom::A ? 0 : 1;
		}

		int32 HomeStateIndex(const EHomeState State)
		{
			return static_cast<int32>(State);
		}

		FString ResidentAccount(const FResidentID ResidentID, const TCHAR* Stock)
		{
			return FString::Printf(TEXT("Resident.%lld.%s"), ResidentID, Stock);
		}

		FString SimpleAccount(const EKingdom Kingdom, const TCHAR* Stock)
		{
			return FString::Printf(TEXT("Simple.%s.%s"), Kingdom == EKingdom::A ? TEXT("A") : TEXT("B"), Stock);
		}

		FString SimpleMicroAccount(const FResidentID ResidentID, const TCHAR* Stock)
		{
			return FString::Printf(TEXT("Simple.Active.%lld.%s"), ResidentID, Stock);
		}

		FString ResidentOwner(const FResidentID ResidentID)
		{
			return FString::Printf(TEXT("Resident:%lld"), ResidentID);
		}

		struct FUnifiedImportBatch
		{
			FReservationID ReservationID = 0;
			double WoodQuantity = 0.0;
		};

		struct FSimpleKingdomState
		{
			int32 HomeStates[4] = {};
			int32 BusyWaitingCount = 0;
		};

		struct FSimpleDelayedAction
		{
			FEventID EventID = 0;
			FSimulationTime ExecuteAt;
			FArriveID ArriveID = 0;
			EKingdom Kingdom = EKingdom::A;
			EIndividualAction Action = EIndividualAction::None;
			int32 ParticipantCount = 0;
			int32 WoodQuantity = 0;
			int64 CoinQuantity = 0;
			FReservationID ReservationID = 0;
			bool bCountsAsBusyWaiting = false;
		};

		struct FActionCandidate
		{
			int32 ResidentIndex = INDEX_NONE;
			EIndividualAction Action = EIndividualAction::None;
			FUnifiedCompetitionScope Scope;
			int32 Quantity = 1;
			uint64 OrderKey = 0;
			FArriveID ArriveID = 0;
			bool bActiveMicro = false;
			bool bCohortApproximation = false;
		};

		struct FMutationFingerprint
		{
			int32 TransactionCount = 0;
			int32 EventCount = 0;
			int32 ScheduledCount = 0;
			int32 ActiveReservationCount = 0;
			int32 ImportBatchCount = 0;
			FEventID ResidentEventID = 0;
			FArriveID ResidentArriveID = 0;
			FReservationID ResidentReservationID = 0;
			int32 ResidentCash = 0;
			int32 ResidentCredit = 0;
			int32 ResidentWood = 0;

			bool operator==(const FMutationFingerprint& Other) const
			{
				return TransactionCount == Other.TransactionCount
					&& EventCount == Other.EventCount
					&& ScheduledCount == Other.ScheduledCount
					&& ActiveReservationCount == Other.ActiveReservationCount
					&& ImportBatchCount == Other.ImportBatchCount
					&& ResidentEventID == Other.ResidentEventID
					&& ResidentArriveID == Other.ResidentArriveID
					&& ResidentReservationID == Other.ResidentReservationID
					&& ResidentCash == Other.ResidentCash
					&& ResidentCredit == Other.ResidentCredit
					&& ResidentWood == Other.ResidentWood;
			}
		};

		enum class EBackendPopulationRepresentation : uint8
		{
			PersistentResidents,
			AggregateKingdom
		};

		enum class EBackendPlanningGranularity : uint8
		{
			Individual,
			Cohort,
			Aggregate
		};

		enum class EBackendActivationBridge : uint8
		{
			PersistentCoreState,
			ReconstructedMicro
		};

		class ISimulationBackend
		{
		public:
			virtual ~ISimulationBackend() = default;
			virtual EUnifiedSimulationMethod GetMethod() const = 0;
			virtual EBackendPopulationRepresentation GetPopulationRepresentation() const = 0;
			virtual EBackendPlanningGranularity GetPlanningGranularity() const = 0;
			virtual EBackendActivationBridge GetActivationBridge() const = 0;
			virtual bool ValidatePopulation(int32 PopulationPerKingdom, FString& OutError) const = 0;
		};

		struct FSimulationBackendProfile
		{
			EUnifiedSimulationMethod Method = EUnifiedSimulationMethod::Oracle;
			EBackendPopulationRepresentation PopulationRepresentation = EBackendPopulationRepresentation::PersistentResidents;
			EBackendPlanningGranularity PlanningGranularity = EBackendPlanningGranularity::Individual;
			EBackendActivationBridge ActivationBridge = EBackendActivationBridge::PersistentCoreState;
			int32 RequiredTotalPopulation = 0;
		};

		class FConfiguredSimulationBackend final : public ISimulationBackend
		{
		public:
			explicit FConfiguredSimulationBackend(const FSimulationBackendProfile& InProfile)
				: Profile(InProfile)
			{
			}

			virtual EUnifiedSimulationMethod GetMethod() const override { return Profile.Method; }
			virtual EBackendPopulationRepresentation GetPopulationRepresentation() const override { return Profile.PopulationRepresentation; }
			virtual EBackendPlanningGranularity GetPlanningGranularity() const override { return Profile.PlanningGranularity; }
			virtual EBackendActivationBridge GetActivationBridge() const override { return Profile.ActivationBridge; }

			virtual bool ValidatePopulation(const int32 PopulationPerKingdom, FString& OutError) const override
			{
				if (Profile.RequiredTotalPopulation > 0
					&& 2 * PopulationPerKingdom != Profile.RequiredTotalPopulation)
				{
					OutError = TEXT("The Detailed Individual Oracle is frozen to 200 total residents.");
					return false;
				}
				return true;
			}

		private:
			FSimulationBackendProfile Profile;
		};

		TUniquePtr<ISimulationBackend> CreateSimulationBackend(const EUnifiedSimulationMethod Method)
		{
			FSimulationBackendProfile Profile;
			Profile.Method = Method;
			switch (Method)
			{
			case EUnifiedSimulationMethod::Oracle:
				Profile.RequiredTotalPopulation = 200;
				break;
			case EUnifiedSimulationMethod::Proposed:
				Profile.PlanningGranularity = EBackendPlanningGranularity::Cohort;
				break;
			case EUnifiedSimulationMethod::PerAgent:
				break;
			case EUnifiedSimulationMethod::Simple:
				Profile.PopulationRepresentation = EBackendPopulationRepresentation::AggregateKingdom;
				Profile.PlanningGranularity = EBackendPlanningGranularity::Aggregate;
				Profile.ActivationBridge = EBackendActivationBridge::ReconstructedMicro;
				break;
			default:
				checkNoEntry();
				return nullptr;
			}
			return MakeUnique<FConfiguredSimulationBackend>(Profile);
		}

		class FUnifiedRuntime
		{
		public:
			FUnifiedRuntime(
				const FPhase0Config& InConfig,
				const EUnifiedSimulationMethod InMethod,
				const EStage2Scenario InScenario,
				const FUnifiedRunOptions& InOptions)
				: Config(InConfig)
				, Backend(CreateSimulationBackend(InMethod))
				, Scenario(InScenario)
				, Options(InOptions)
				, Clock(FSimulationTime::FromDays(-7))
			{
			}

			bool Initialize(FString& OutError);
			bool StepHour(FString& OutError);
			bool Finalize(FUnifiedRunResult& OutResult, FString& OutError);
			bool IsComplete() const { return Clock.Now() == FSimulationTime::FromDays(60); }
			FSimulationTime GetCurrentTime() const { return Clock.Now(); }
			const FUnifiedStepMeasurement& GetLastStepMeasurement() const { return LastStepMeasurement; }

		private:
			bool InitializeLedger(FString& OutError);
			bool BuildDay14ActivationSample(FString& OutError);
			bool ProcessHour(FSimulationTime Time, FString& OutError);
			bool AdvanceChronologically(FSimulationTime Target, FString& OutError);
			bool CompleteScheduledEvent(const FScheduledEvent& Due, FString& OutError);
			bool CompleteResidentAction(const FSimulationEventRecord& Event, FSimulationTime Time, FString& OutError);
			bool CompleteImport(const FSimulationEventRecord& Event, FSimulationTime Time, FString& OutError);
			bool CompleteSimpleAction(const FSimpleDelayedAction& Action, FString& OutError);

			bool ApplyActivationTrace(FSimulationTime Time, FString& OutError);
			bool ActivateResident(FResidentID ResidentID, FSimulationTime Time, FString& OutError);
			bool DeactivateResident(FResidentID ResidentID, FSimulationTime Time, FString& OutError);
			bool ActivateSimpleResident(FResidentID ResidentID, FSimulationTime Time, FString& OutError);
			bool DeactivateSimpleResident(FResidentID ResidentID, FSimulationTime Time, FString& OutError);
			void RecordActivation(FResidentCoreState& Resident, FSimulationTime Time, bool bSimpleReconstructed);
			void RecordFirstAction(FResidentID ResidentID, EIndividualAction Action, bool bContinuedCommittedEvent);
			bool ApplyEarthquake(FSimulationTime Time, FString& OutError);
			bool ApplyPolicies(FSimulationTime Time, FString& OutError);
			bool PlaceStateImportOrder(FSimulationTime Time, FString& OutError);
			bool CalculateAidEligibility(FSimulationTime Time, FString& OutError);
			bool PayRepairAid(FSimulationTime Time, FString& OutError);
			bool CreateInstantEvent(const TCHAR* Type, FSimulationTime Time, int32 Participants, FPolicyID PolicyID, FString& OutError);
			bool CreateEvent(const FSimulationEventRequest& Request, FEventID& OutEventID, FString& OutError);

			bool ApplyForestGrowth(EKingdom Kingdom, FSimulationTime Time, FString& OutError);
			bool ApplyBaselineImport(EKingdom Kingdom, FSimulationTime Time, FString& OutError);
			bool ApplyCommercialHarvest(EKingdom Kingdom, FSimulationTime Time, FString& OutError);
			bool ApplyRoutineConsumption(EKingdom Kingdom, FSimulationTime Time, FString& OutError);
			void UpdateWoodPrice(EKingdom Kingdom);
			void EnsureHarvestDay(EKingdom Kingdom, FSimulationTime Time);
			void EnsureRepairDay(EKingdom Kingdom, FSimulationTime Time);
			bool IsHarvestCapActive(EKingdom Kingdom, FSimulationTime Time) const;
			double HarvestAllowance(EKingdom Kingdom, FSimulationTime Time);

			bool PlanDetailedResidents(FSimulationTime Time, FString& OutError);
			bool PlanSimple(FSimulationTime Time, FString& OutError);
			bool ResolveAndCommitCandidates(FSimulationTime Time, TArray<FActionCandidate>& Candidates, FString& OutError);
			bool StartCandidate(const FActionCandidate& Candidate, FSimulationTime Time, FString& OutError);
			bool StartTimedAction(const FActionCandidate& Candidate, FSimulationTime Time, int64 Duration, int32 WoodQuantity, FReservationID ReservationID, FString& OutError);
			bool StartBuyWood(const FActionCandidate& Candidate, FSimulationTime Time, FString& OutError);
			bool StartChopWood(const FActionCandidate& Candidate, FSimulationTime Time, FString& OutError);
			bool StartRepair(const FActionCandidate& Candidate, FSimulationTime Time, FString& OutError);
			bool StartFallbackWait(const FActionCandidate& Candidate, FSimulationTime Time, FString& OutError);
			bool ConsumeFaultInjection(EUnifiedFaultInjectionPoint Point, int32 ResidentIndex);
			FMutationFingerprint CaptureMutationFingerprint(int32 ResidentIndex) const;

			bool Submit(
				FSimulationTime Time,
				ESimulationResource Resource,
				const FString& Source,
				const FString& Destination,
				double Quantity,
				bool bBoundary,
				const FString& Key,
				FEventID EventID,
				FArriveID ArriveID,
				FPolicyID PolicyID,
				FString& OutError);
			void SyncResident(FResidentCoreState& Resident);
			void RecordLODTransition(const FResidentCoreState& Resident, EResidentRepresentation From, EResidentRepresentation To, FSimulationTime Time);
			void PublishReadOnlyObservations(FSimulationTime GameTime, FSimulationTime ProcessedTime);
			void BuildCohortObservations(FSimulationTime GameTime, TArray<FUnifiedCohortObservation>& OutObservations) const;
			FString PolicyStateAt(FSimulationTime ProcessedTime) const;
			FString ResidentLedgerAccount(FResidentID ResidentID, const TCHAR* Stock) const;
			FResidentCoreState* FindResident(FResidentID ResidentID);
			const FResidentCoreState* FindResident(FResidentID ResidentID) const;
			const FInitialResidentRecord* FindInitialResident(FResidentID ResidentID) const;
			FKingdomStocks ReadStocks(EKingdom Kingdom, bool bIncludeResidentTotals, bool bCountDiagnostics = true);
			int32 CountHomes(EKingdom Kingdom, EHomeState State) const;
			FPopulationState PopulationState() const;
			bool AuditHour(FString& OutError);
			FKingdomSnapshot BuildKingdomSnapshot(FSimulationTime Time, EKingdom Kingdom, bool bCountDiagnostics);
			void AddSnapshot(FSimulationTime Time);
			void FillResult(FUnifiedRunResult& OutResult);

			FPhase0Config Config;
			TUniquePtr<ISimulationBackend> Backend;
			EStage2Scenario Scenario = EStage2Scenario::None;
			FUnifiedRunOptions Options;
			FInitialPopulationManifest PopulationManifest;
			FEarthquakeDamageList DamageList;
			FPersistentTestPool ContinuitySample;
			TArray<FResidentCoreState> Residents;
			TMap<FResidentID, int32> ResidentIndices;
			TMap<FResidentID, int32> InitialResidentIndices;
			TSet<FResidentID> ActiveResidents;
			TArray<FResidentID> Day14ActivationResidents;
			TMap<FResidentID, int32> PendingFirstActionObservations;
			TArray<FUnifiedActivationObservation> ActivationObservations;
			TMap<int32, FUnifiedNPCObservation> FinalizedNPCObservations;
			TArray<FLODTransitionRecord> LODTransitions;
			TArray<FSimulationEventRecord> PendingEventObservations;
			int32 PublishedTransactionCount = 0;
			int32 PublishedLODTransitionCount = 0;
			int32 PublishedActivationObservationCount = 0;
			FSimpleKingdomState SimpleStates[2];
			TMap<FEventID, FSimpleDelayedAction> SimpleActions;
			TArray<int32> AidEligibleResidentIndices;
			TMap<int32, FArriveID> AidArriveIDs;
			int32 SimpleAidEligibleCount = 0;
			FSimulationClock Clock;
			FSimulationScheduler Scheduler;
			FResourceLedger Ledger;
			FReservationStore Reservations;
			FSimulationEventStore EventStore;
			TMap<FEventID, FUnifiedImportBatch> ImportBatches;
			double WoodPrices[2] = { 1.0, 1.0 };
			int64 ImportBudgetRemaining = 0;
			int32 HarvestDays[2] = { TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min() };
			double HarvestRemaining[2] = {};
			int32 RepairDays[2] = { TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min() };
			int32 RepairRemaining[2] = {};
			bool bEarthquakeApplied = false;
			FUnifiedRunDiagnostics Diagnostics;
			TArray<FKingdomSnapshot> Snapshots;
			FConservationAudit LastAudit;
			double LastCoinResidual = 0.0;
			int32 CoreLedgerMismatchCount = 0;
			int32 EventReferenceErrorCount = 0;
			int32 ActiveCapViolationCount = 0;
			int32 ReservationErrorCount = 0;
			int32 TaskResetCount = 0;
			bool bFaultInjectionConsumed = false;
			FUnifiedStepMeasurement LastStepMeasurement;
			FUnifiedCostBreakdown CostBreakdown;
			double LastDetailedActivePlanningMs = 0.0;
		};

		bool FUnifiedRuntime::Initialize(FString& OutError)
		{
			const double InitializeStart = FPlatformTime::Seconds();
			if (Config.PopulationPerKingdom <= 0)
			{
				OutError = TEXT("Unified runtime requires a positive per-kingdom population.");
				return false;
			}
			if (!Backend || !Backend->ValidatePopulation(Config.PopulationPerKingdom, OutError))
			{
				return false;
			}
			if (!FPhase0ManifestGenerator::Generate(
				Config,
				PopulationManifest,
				DamageList,
				ContinuitySample,
				OutError))
			{
				return false;
			}
			for (int32 Index = 0; Index < PopulationManifest.Residents.Num(); ++Index)
			{
				InitialResidentIndices.Add(PopulationManifest.Residents[Index].ResidentID, Index);
			}
			if (!BuildDay14ActivationSample(OutError))
			{
				return false;
			}

			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				for (int32 Index = 0; Index < 2; ++Index)
				{
					SimpleStates[Index].HomeStates[HomeStateIndex(EHomeState::Healthy)] = Config.PopulationPerKingdom;
				}
			}
			else
			{
				Residents.Reserve(PopulationManifest.Residents.Num());
				for (const FInitialResidentRecord& Initial : PopulationManifest.Residents)
				{
					FResidentCoreState& Resident = Residents.AddDefaulted_GetRef();
					Resident.ResidentID = Initial.ResidentID;
					Resident.HomeID = Initial.HomeID;
					Resident.PersistentID = Initial.PersistentID;
					Resident.Name = Initial.Name;
					Resident.Kingdom = Initial.Kingdom;
					Resident.Profession = Initial.Profession;
					Resident.IncomeBand = Initial.IncomeBand;
					Resident.Cash = Initial.Cash;
					Resident.RepairCredit = Initial.RepairCredit;
					Resident.InventoryWood = Initial.InventoryWood;
					Resident.HomeState = Initial.HomeState;
					Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
					Resident.LastUpdateTime = Clock.Now();
					Resident.RNGStreamKey = static_cast<uint32>(Mix64(
						Mix64(static_cast<uint64>(static_cast<uint32>(Config.Seed)))
						^ Mix64(static_cast<uint64>(Resident.ResidentID))));
					ResidentIndices.Add(Resident.ResidentID, Residents.Num() - 1);
				}
			}

			ImportBudgetRemaining = Config.PopulationPerKingdom;
			if (!InitializeLedger(OutError))
			{
				return false;
			}
			const double AuditStart = FPlatformTime::Seconds();
			if (!AuditHour(OutError))
			{
				return false;
			}
			const double InitialAuditMs = (FPlatformTime::Seconds() - AuditStart) * 1000.0;
			CostBreakdown.AuditCpuMs += InitialAuditMs;
			CostBreakdown.InitializeCpuMs = FMath::Max(
				0.0,
				(FPlatformTime::Seconds() - InitializeStart) * 1000.0 - InitialAuditMs);
			return true;
		}

		bool FUnifiedRuntime::BuildDay14ActivationSample(FString& OutError)
		{
			TSet<FResidentID> ContinuityIDs;
			for (const FPersistentTestRecord& Record : ContinuitySample.Residents)
			{
				ContinuityIDs.Add(Record.ResidentID);
			}

			struct FStratum
			{
				EKingdom Kingdom = EKingdom::A;
				EProfession Profession = EProfession::Worker;
				EIncomeBand IncomeBand = EIncomeBand::Low;
				int32 Count = 0;
			};
			const FStratum Strata[] =
			{
				{ EKingdom::A, EProfession::Logger, EIncomeBand::Low, 1 },
				{ EKingdom::A, EProfession::Logger, EIncomeBand::NonLow, 1 },
				{ EKingdom::A, EProfession::Worker, EIncomeBand::Low, 6 },
				{ EKingdom::A, EProfession::Worker, EIncomeBand::NonLow, 2 },
				{ EKingdom::B, EProfession::Logger, EIncomeBand::Low, 1 },
				{ EKingdom::B, EProfession::Logger, EIncomeBand::NonLow, 1 },
				{ EKingdom::B, EProfession::Worker, EIncomeBand::Low, 6 },
				{ EKingdom::B, EProfession::Worker, EIncomeBand::NonLow, 2 }
			};

			for (const FStratum& Stratum : Strata)
			{
				TArray<const FInitialResidentRecord*> Candidates;
				for (const FInitialResidentRecord& Resident : PopulationManifest.Residents)
				{
					if (Resident.Kingdom == Stratum.Kingdom
						&& Resident.Profession == Stratum.Profession
						&& Resident.IncomeBand == Stratum.IncomeBand
						&& !ContinuityIDs.Contains(Resident.ResidentID))
					{
						Candidates.Add(&Resident);
					}
				}
				Candidates.Sort([this](const FInitialResidentRecord& Left, const FInitialResidentRecord& Right)
				{
					const uint64 LeftKey = CompetitionOrderKey(Config.Seed, FSimulationTime::FromDays(14).Minutes, Left.ResidentID, 0xD14ull);
					const uint64 RightKey = CompetitionOrderKey(Config.Seed, FSimulationTime::FromDays(14).Minutes, Right.ResidentID, 0xD14ull);
					return LeftKey != RightKey ? LeftKey < RightKey : Left.ResidentID < Right.ResidentID;
				});
				if (Candidates.Num() < Stratum.Count)
				{
					OutError = TEXT("The manifest cannot supply the frozen Day 14 stratified activation sample.");
					return false;
				}
				for (int32 Index = 0; Index < Stratum.Count; ++Index)
				{
					Day14ActivationResidents.Add(Candidates[Index]->ResidentID);
				}
			}
			Day14ActivationResidents.Sort();
			if (Day14ActivationResidents.Num() != 20)
			{
				OutError = TEXT("The frozen Day 14 activation sample must contain exactly 20 residents.");
				return false;
			}
			OutError.Reset();
			return true;
		}

		bool FUnifiedRuntime::InitializeLedger(FString& OutError)
		{
			for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
			{
				const double N = Config.PopulationPerKingdom;
				if (!Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("ForestWood")), 16.0 * N, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("ForestWoodReserved")), 0.0, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")), 2.0 * N, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("MarketWoodReserved")), 0.0, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("WoodInTransit")), 0.0, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("WoodEmbeddedInRepairs")), 0.0, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("WoodInRepairedHomes")), 0.0, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Coin, MakeKingdomAccount(Kingdom, TEXT("TreasuryAvailable")), 5.0 * N, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Coin, MakeKingdomAccount(Kingdom, TEXT("TreasuryReserved")), 0.0, OutError)
					|| !Ledger.InitializeAccount(ESimulationResource::Coin, MakeKingdomAccount(Kingdom, TEXT("MarketCoin")), 0.0, OutError))
				{
					return false;
				}
			}

			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
				{
					int64 Cash = 0;
					for (const FInitialResidentRecord& Initial : PopulationManifest.Residents)
					{
						if (Initial.Kingdom == Kingdom)
						{
							Cash += Initial.Cash;
						}
					}
					if (!Ledger.InitializeAccount(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("Cash")), Cash, OutError)
						|| !Ledger.InitializeAccount(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("RepairCredit")), 0.0, OutError)
						|| !Ledger.InitializeAccount(ESimulationResource::Wood, SimpleAccount(Kingdom, TEXT("Wood")), 0.0, OutError))
					{
						return false;
					}
				}

				TSet<FResidentID> TraceResidentIDs;
				for (const FPersistentTestRecord& Record : ContinuitySample.Residents)
				{
					TraceResidentIDs.Add(Record.ResidentID);
				}
				for (const FResidentID ResidentID : Day14ActivationResidents)
				{
					TraceResidentIDs.Add(ResidentID);
				}
				for (const FResidentID ResidentID : TraceResidentIDs)
				{
					if (!Ledger.InitializeAccount(ESimulationResource::Coin, SimpleMicroAccount(ResidentID, TEXT("Cash")), 0.0, OutError)
						|| !Ledger.InitializeAccount(ESimulationResource::Coin, SimpleMicroAccount(ResidentID, TEXT("RepairCredit")), 0.0, OutError)
						|| !Ledger.InitializeAccount(ESimulationResource::Wood, SimpleMicroAccount(ResidentID, TEXT("Wood")), 0.0, OutError))
					{
						return false;
					}
				}
			}
			else
			{
				for (const FResidentCoreState& Resident : Residents)
				{
					if (!Ledger.InitializeAccount(ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("Cash")), Resident.Cash, OutError)
						|| !Ledger.InitializeAccount(ESimulationResource::Coin, ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")), Resident.RepairCredit, OutError)
						|| !Ledger.InitializeAccount(ESimulationResource::Wood, ResidentAccount(Resident.ResidentID, TEXT("Wood")), Resident.InventoryWood, OutError))
					{
						return false;
					}
				}
			}

			Ledger.SealInitialState();
			return true;
		}

		bool FUnifiedRuntime::StepHour(FString& OutError)
		{
			if (Clock.Now().Minutes >= FSimulationTime::FromDays(60).Minutes)
			{
				OutError = TEXT("Unified runtime is already at D60T00:00.");
				return false;
			}

			LastStepMeasurement = {};
			LastDetailedActivePlanningMs = 0.0;
			const double StepStart = FPlatformTime::Seconds();
			const FSimulationTime Time = Clock.Now();
			if (!ProcessHour(Time, OutError))
			{
				return false;
			}

			const FSimulationTime StepEnd = FSimulationTime::FromMinutes(Time.Minutes + MinutesPerHour);
			if (!AdvanceChronologically(StepEnd, OutError))
			{
				return false;
			}
			const double TransitionStart = FPlatformTime::Seconds();
			if (!ApplyActivationTrace(StepEnd, OutError))
			{
				return false;
			}
			LastStepMeasurement.TransitionCpuMs = (FPlatformTime::Seconds() - TransitionStart) * 1000.0;
			if (Options.Mode != EUnifiedRunMode::Performance)
			{
				const double AuditStart = FPlatformTime::Seconds();
				if (!AuditHour(OutError))
				{
					return false;
				}
				LastStepMeasurement.AuditCpuMs = (FPlatformTime::Seconds() - AuditStart) * 1000.0;
			}
			if (Options.Mode != EUnifiedRunMode::Performance && Options.bRecordSnapshots && StepEnd.Minutes > 0)
			{
				const double SnapshotStart = FPlatformTime::Seconds();
				AddSnapshot(StepEnd);
				LastStepMeasurement.SnapshotCpuMs = (FPlatformTime::Seconds() - SnapshotStart) * 1000.0;
			}
			if (Options.Observer != nullptr || Options.EventSink != nullptr)
			{
				const double ObserverStart = FPlatformTime::Seconds();
				PublishReadOnlyObservations(StepEnd, Time);
				LastStepMeasurement.ObserverCpuMs = (FPlatformTime::Seconds() - ObserverStart) * 1000.0;
			}
			LastStepMeasurement.GameTime = StepEnd;
			LastStepMeasurement.ActiveCount = ActiveResidents.Num();
			LastStepMeasurement.QueueLength = Scheduler.GetPendingEvents().Num();
			const double StepCpuMs = (FPlatformTime::Seconds() - StepStart) * 1000.0;
			LastStepMeasurement.ProductionCpuMs = FMath::Max(
				0.0,
				StepCpuMs - LastStepMeasurement.ValidationCpuMs - LastStepMeasurement.AuditCpuMs
					- LastStepMeasurement.SnapshotCpuMs - LastStepMeasurement.ObserverCpuMs);
			CostBreakdown.ProductionCpuMs += LastStepMeasurement.ProductionCpuMs;
			CostBreakdown.MacroCpuMs += LastStepMeasurement.MacroCpuMs;
			CostBreakdown.MicroCpuMs += LastStepMeasurement.MicroCpuMs;
			CostBreakdown.TransitionCpuMs += LastStepMeasurement.TransitionCpuMs;
			CostBreakdown.ValidationCpuMs += LastStepMeasurement.ValidationCpuMs;
			CostBreakdown.AuditCpuMs += LastStepMeasurement.AuditCpuMs;
			CostBreakdown.SnapshotCpuMs += LastStepMeasurement.SnapshotCpuMs;
			CostBreakdown.ObserverCpuMs += LastStepMeasurement.ObserverCpuMs;
			OutError.Reset();
			return true;
		}

		bool FUnifiedRuntime::Finalize(FUnifiedRunResult& OutResult, FString& OutError)
		{
			const double FinalizeStart = FPlatformTime::Seconds();
			double FinalAuditMs = 0.0;
			if (!IsComplete())
			{
				OutError = TEXT("Unified runtime can only finalize at D60T00:00.");
				return false;
			}
			if (Options.Mode == EUnifiedRunMode::Performance)
			{
				const double AuditStart = FPlatformTime::Seconds();
				if (!AuditHour(OutError))
				{
					return false;
				}
				FinalAuditMs = (FPlatformTime::Seconds() - AuditStart) * 1000.0;
				CostBreakdown.AuditCpuMs += FinalAuditMs;
			}

			FillResult(OutResult);
			CostBreakdown.FinalizeCpuMs = FMath::Max(0.0, (FPlatformTime::Seconds() - FinalizeStart) * 1000.0 - FinalAuditMs);
			OutResult.CostBreakdown = CostBreakdown;
			if (!OutResult.IsHardErrorFree())
			{
				OutError = TEXT("Unified runtime final hard-error gate failed.");
				return false;
			}
			OutError.Reset();
			return true;
		}

		bool FUnifiedRuntime::Submit(
			const FSimulationTime Time,
			const ESimulationResource Resource,
			const FString& Source,
			const FString& Destination,
			const double Quantity,
			const bool bBoundary,
			const FString& Key,
			const FEventID EventID,
			const FArriveID ArriveID,
			const FPolicyID PolicyID,
			FString& OutError)
		{
			if (Quantity <= UE_DOUBLE_SMALL_NUMBER)
			{
				return true;
			}
			FLedgerTransferRequest Request;
			Request.IdempotencyKey = Key;
			Request.GameTime = Time;
			Request.Resource = Resource;
			Request.Source = Source;
			Request.Destination = Destination;
			Request.Quantity = Quantity;
			Request.bBoundaryFlow = bBoundary;
			Request.EventID = EventID;
			Request.ArriveID = ArriveID;
			Request.PolicyID = PolicyID;
			FTransactionID TransactionID = 0;
			return Ledger.SubmitTransfer(Request, TransactionID, OutError);
		}

		bool FUnifiedRuntime::CreateEvent(
			const FSimulationEventRequest& Request,
			FEventID& OutEventID,
			FString& OutError)
		{
			if (!EventStore.CreateEvent(Request, OutEventID, OutError))
			{
				return false;
			}
			if (Options.EventSink != nullptr)
			{
				PendingEventObservations.Add(*EventStore.Find(OutEventID));
			}
			return true;
		}

		FString FUnifiedRuntime::ResidentLedgerAccount(const FResidentID ResidentID, const TCHAR* Stock) const
		{
			return Backend->GetActivationBridge() == EBackendActivationBridge::ReconstructedMicro
				? SimpleMicroAccount(ResidentID, Stock)
				: ResidentAccount(ResidentID, Stock);
		}

		FResidentCoreState* FUnifiedRuntime::FindResident(const FResidentID ResidentID)
		{
			const int32* Index = ResidentIndices.Find(ResidentID);
			return Index != nullptr && Residents.IsValidIndex(*Index) ? &Residents[*Index] : nullptr;
		}

		const FResidentCoreState* FUnifiedRuntime::FindResident(const FResidentID ResidentID) const
		{
			const int32* Index = ResidentIndices.Find(ResidentID);
			return Index != nullptr && Residents.IsValidIndex(*Index) ? &Residents[*Index] : nullptr;
		}

		const FInitialResidentRecord* FUnifiedRuntime::FindInitialResident(const FResidentID ResidentID) const
		{
			const int32* Index = InitialResidentIndices.Find(ResidentID);
			return Index != nullptr && PopulationManifest.Residents.IsValidIndex(*Index)
				? &PopulationManifest.Residents[*Index]
				: nullptr;
		}

		void FUnifiedRuntime::SyncResident(FResidentCoreState& Resident)
		{
			Resident.Cash = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
				ESimulationResource::Coin,
				ResidentLedgerAccount(Resident.ResidentID, TEXT("Cash")))));
			Resident.RepairCredit = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
				ESimulationResource::Coin,
				ResidentLedgerAccount(Resident.ResidentID, TEXT("RepairCredit")))));
			Resident.InventoryWood = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
				ESimulationResource::Wood,
				ResidentLedgerAccount(Resident.ResidentID, TEXT("Wood")))));
			Diagnostics.LedgerQueryCount += 3;
		}

		FMutationFingerprint FUnifiedRuntime::CaptureMutationFingerprint(const int32 ResidentIndex) const
		{
			FMutationFingerprint Fingerprint;
			Fingerprint.TransactionCount = Ledger.GetTransactions().Num();
			Fingerprint.EventCount = EventStore.GetEvents().Num();
			Fingerprint.ScheduledCount = Scheduler.NumPending();
			Fingerprint.ImportBatchCount = ImportBatches.Num();
			for (const TPair<FReservationID, FReservationRecord>& Pair : Reservations.GetReservations())
			{
				Fingerprint.ActiveReservationCount += Pair.Value.State == EReservationState::Active ? 1 : 0;
			}
			if (Residents.IsValidIndex(ResidentIndex))
			{
				const FResidentCoreState& Resident = Residents[ResidentIndex];
				Fingerprint.ResidentEventID = Resident.ActiveEventID;
				Fingerprint.ResidentArriveID = Resident.ActiveArriveID;
				Fingerprint.ResidentReservationID = Resident.ActiveReservationID;
				Fingerprint.ResidentCash = Resident.Cash;
				Fingerprint.ResidentCredit = Resident.RepairCredit;
				Fingerprint.ResidentWood = Resident.InventoryWood;
			}
			return Fingerprint;
		}

		bool FUnifiedRuntime::ConsumeFaultInjection(
			const EUnifiedFaultInjectionPoint Point,
			const int32 ResidentIndex)
		{
			if (bFaultInjectionConsumed || Options.FaultInjection != Point)
			{
				return false;
			}
			const FMutationFingerprint Before = CaptureMutationFingerprint(ResidentIndex);
			bFaultInjectionConsumed = true;
			++Diagnostics.FaultInjectionCount;
			if (!(Before == CaptureMutationFingerprint(ResidentIndex)))
			{
				++Diagnostics.RejectedActionResidueCount;
			}
			return true;
		}

		FKingdomStocks FUnifiedRuntime::ReadStocks(
			const EKingdom Kingdom,
			const bool bIncludeResidentTotals,
			const bool bCountDiagnostics)
		{
			FKingdomStocks Stocks;
			Stocks.ForestCapacity = 20.0 * Config.PopulationPerKingdom;
			Stocks.ForestWood = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("ForestWood")));
			Stocks.MarketWoodAvailable = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")));
			Stocks.MarketWoodReserved = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("MarketWoodReserved")));
			Stocks.WoodInTransit = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("WoodInTransit")));
			Stocks.WoodEmbeddedInRepairs = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("WoodEmbeddedInRepairs")));
			Stocks.WoodInRepairedHomes = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("WoodInRepairedHomes")));
			Stocks.TreasuryAvailable = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom, TEXT("TreasuryAvailable"))));
			Stocks.TreasuryReserved = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom, TEXT("TreasuryReserved"))));
			Stocks.MarketCoin = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom, TEXT("MarketCoin"))));
			Stocks.WoodPrice = WoodPrices[KingdomIndex(Kingdom)];
			Diagnostics.LedgerQueryCount += bCountDiagnostics ? 9 : 0;

			if (bIncludeResidentTotals)
			{
				if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
				{
					Stocks.ResidentInventoryWood = Ledger.GetBalance(ESimulationResource::Wood, SimpleAccount(Kingdom, TEXT("Wood")));
					Stocks.ResidentRepairCredit = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("RepairCredit"))));
					Diagnostics.LedgerQueryCount += bCountDiagnostics ? 2 : 0;
					for (const FResidentCoreState& Resident : Residents)
					{
						if (Resident.Kingdom == Kingdom)
						{
							Stocks.ResidentInventoryWood += Resident.InventoryWood;
							Stocks.ResidentRepairCredit += Resident.RepairCredit;
						}
					}
				}
				else
				{
					for (const FResidentCoreState& Resident : Residents)
					{
						if (Resident.Kingdom == Kingdom)
						{
							Stocks.ResidentInventoryWood += Resident.InventoryWood;
							Stocks.ResidentRepairCredit += Resident.RepairCredit;
						}
					}
				}
			}
			return Stocks;
		}

		int32 FUnifiedRuntime::CountHomes(const EKingdom Kingdom, const EHomeState State) const
		{
			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				int32 Count = SimpleStates[KingdomIndex(Kingdom)].HomeStates[HomeStateIndex(State)];
				for (const FResidentCoreState& Resident : Residents)
				{
					Count += Resident.Kingdom == Kingdom && Resident.HomeState == State ? 1 : 0;
				}
				return Count;
			}
			int32 Count = 0;
			for (const FResidentCoreState& Resident : Residents)
			{
				Count += Resident.Kingdom == Kingdom && Resident.HomeState == State ? 1 : 0;
			}
			return Count;
		}

		FPopulationState FUnifiedRuntime::PopulationState() const
		{
			FPopulationState Population;
			Population.Total = 2 * Config.PopulationPerKingdom;
			Population.ActiveMicro = ActiveResidents.Num();
			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				Population.Anonymous = Population.Total - Population.ActiveMicro;
			}
			else
			{
				Population.PersistentMacro = Population.Total - Population.ActiveMicro;
			}
			return Population;
		}

		bool FUnifiedRuntime::AuditHour(FString& OutError)
		{
			++Diagnostics.FullAuditCount;
			Diagnostics.AuditResidentVisitCount += Residents.Num();
			LastAudit = AuditConservation(PopulationState(), Ledger, EventStore);
			LastCoinResidual = Ledger.ComputeResidual(ESimulationResource::Coin);
			if (ActiveResidents.Num() > ActiveMicroCap)
			{
				++ActiveCapViolationCount;
			}

			TSet<FEventID> ReferencedResidentEvents;
			if (Backend->GetPopulationRepresentation() != EBackendPopulationRepresentation::AggregateKingdom || Residents.Num() > 0)
			{
				for (const FResidentCoreState& Resident : Residents)
				{
					const int32 Cash = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, ResidentLedgerAccount(Resident.ResidentID, TEXT("Cash")))));
					const int32 Credit = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, ResidentLedgerAccount(Resident.ResidentID, TEXT("RepairCredit")))));
					const int32 Wood = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Wood, ResidentLedgerAccount(Resident.ResidentID, TEXT("Wood")))));
					Diagnostics.LedgerQueryCount += 3;
					if (Cash != Resident.Cash || Credit != Resident.RepairCredit || Wood != Resident.InventoryWood)
					{
						++CoreLedgerMismatchCount;
					}
					if (Resident.ActiveEventID != 0)
					{
						const FSimulationEventRecord* Event = EventStore.Find(Resident.ActiveEventID);
						if (Event == nullptr
							|| Event->State != ESimulationEventState::Pending
							|| Event->Event.ResidentID != Resident.ResidentID
							|| Event->Event.Owner != ResidentOwner(Resident.ResidentID)
							|| Event->Event.ArriveID != Resident.ActiveArriveID)
						{
							++EventReferenceErrorCount;
						}
						else
						{
							ReferencedResidentEvents.Add(Resident.ActiveEventID);
						}
					}
				}
			}

			for (const TPair<FEventID, FSimulationEventRecord>& Pair : EventStore.GetEvents())
			{
				if (Pair.Value.State == ESimulationEventState::Pending
					&& Pair.Value.Event.ResidentID != 0
					&& !ReferencedResidentEvents.Contains(Pair.Key))
				{
					++EventReferenceErrorCount;
				}
			}
			for (const TPair<FReservationID, FReservationRecord>& Pair : Reservations.GetReservations())
			{
				if (Pair.Value.State != EReservationState::Active)
				{
					continue;
				}
				bool bReferenced = false;
				if (Pair.Value.Request.EventID != 0)
				{
					const FSimulationEventRecord* Event = EventStore.Find(Pair.Value.Request.EventID);
					bReferenced = Event != nullptr && Event->State == ESimulationEventState::Pending;
				}
				else
				{
					bReferenced = SimpleActions.Contains(Pair.Value.Request.EventID);
				}
				if (!bReferenced)
				{
					++ReservationErrorCount;
				}
			}

			if (!LastAudit.IsHardErrorFree()
				|| !FMath::IsNearlyZero(LastCoinResidual, 1.e-6)
				|| CoreLedgerMismatchCount != 0
				|| EventReferenceErrorCount != 0
				|| ActiveCapViolationCount != 0
				|| ReservationErrorCount != 0
				|| TaskResetCount != 0)
			{
				OutError = FString::Printf(
					TEXT("Unified runtime audit failed at %s: Population=%d Wood=%.12g Negative=%d DuplicateTx=%d Owner=%d DuplicateCompletion=%d Coin=%.12g CoreLedger=%d EventReference=%d ActiveCap=%d Reservation=%d TaskReset=%d."),
					*Clock.Now().ToString(),
					LastAudit.PopulationResidual,
					LastAudit.WoodResidual,
					LastAudit.NegativeStockCount,
					LastAudit.DuplicateTransactionCount,
					LastAudit.EventOwnerConflictCount,
					LastAudit.DuplicateCompletionCount,
					LastCoinResidual,
					CoreLedgerMismatchCount,
					EventReferenceErrorCount,
					ActiveCapViolationCount,
					ReservationErrorCount,
					TaskResetCount);
				return false;
			}
			OutError.Reset();
			return true;
		}

		FKingdomSnapshot FUnifiedRuntime::BuildKingdomSnapshot(
			const FSimulationTime Time,
			const EKingdom Kingdom,
			const bool bCountDiagnostics)
		{
			FKingdomSnapshot Snapshot;
			Snapshot.GameTime = Time;
			Snapshot.Kingdom = Kingdom;
			Snapshot.Stocks = ReadStocks(Kingdom, true, bCountDiagnostics);
			Snapshot.Healthy = CountHomes(Kingdom, EHomeState::Healthy);
			Snapshot.DamagedWaiting = CountHomes(Kingdom, EHomeState::DamagedWaiting);
			Snapshot.UnderRepair = CountHomes(Kingdom, EHomeState::UnderRepair);
			Snapshot.Repaired = CountHomes(Kingdom, EHomeState::Repaired);
			Snapshot.LedgerTransactionCount = Ledger.GetTransactions().Num();
			return Snapshot;
		}

		void FUnifiedRuntime::AddSnapshot(const FSimulationTime Time)
		{
			Diagnostics.SnapshotResidentVisitCount += 10ll * Residents.Num();
			for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
			{
				Snapshots.Add(BuildKingdomSnapshot(Time, Kingdom, true));
			}
		}

		FString FUnifiedRuntime::PolicyStateAt(const FSimulationTime ProcessedTime) const
		{
			const int64 Day2 = FSimulationTime::FromDays(2).Minutes;
			const int64 Day3 = FSimulationTime::FromDays(3).Minutes;
			switch (Scenario)
			{
			case EStage2Scenario::HarvestCap:
				if (ProcessedTime.Minutes >= FSimulationTime::FromDays(30).Minutes) return TEXT("Ended");
				if (ProcessedTime.Minutes >= Day3) return TEXT("Active");
				return ProcessedTime.Minutes >= Day2 ? TEXT("Announced") : TEXT("Inactive");
			case EStage2Scenario::StateImport:
				if (ProcessedTime.Minutes >= FSimulationTime::FromDays(15).Minutes) return TEXT("Ended");
				return ProcessedTime.Minutes >= Day2 ? TEXT("Active") : TEXT("Inactive");
			case EStage2Scenario::RepairAid:
				if (ProcessedTime.Minutes >= Day3) return TEXT("Paid");
				return ProcessedTime.Minutes >= Day2 ? TEXT("EligibilityFrozen") : TEXT("Inactive");
			case EStage2Scenario::None:
			default:
				return TEXT("None");
			}
		}

		void FUnifiedRuntime::BuildCohortObservations(
			const FSimulationTime GameTime,
			TArray<FUnifiedCohortObservation>& OutObservations) const
		{
			TMap<FString, FUnifiedCohortObservation> Buckets;
			auto AddResident = [&Buckets, GameTime](const FResidentCoreState& Resident)
			{
				const FString Key = FString::Printf(
					TEXT("K=%d|P=%d|I=%d|H=%d|M=%d"),
					static_cast<int32>(Resident.Kingdom),
					static_cast<int32>(Resident.Profession),
					static_cast<int32>(Resident.IncomeBand),
					static_cast<int32>(Resident.HomeState),
					static_cast<int32>(Resident.MacroIntent));
				FUnifiedCohortObservation& Bucket = Buckets.FindOrAdd(Key);
				Bucket.GameTime = GameTime;
				Bucket.CohortKey = Key;
				Bucket.MacroIntent = Resident.MacroIntent;
				++Bucket.Count;
				Bucket.CashSum += Resident.Cash;
				Bucket.CashSquaredSum += static_cast<int64>(Resident.Cash) * Resident.Cash;
				Bucket.RepairCreditSum += Resident.RepairCredit;
				++Bucket.WoodCounts[FMath::Clamp(Resident.InventoryWood, 0, 4)];
			};

			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
				{
					const FSimpleKingdomState& State = SimpleStates[KingdomIndex(Kingdom)];
					int32 Count = 0;
					for (const int32 HomeCount : State.HomeStates)
					{
						Count += HomeCount;
					}
					if (Count <= 0)
					{
						continue;
					}
					const FString Key = FString::Printf(TEXT("K=%d|Aggregate"), static_cast<int32>(Kingdom));
					FUnifiedCohortObservation& Bucket = Buckets.FindOrAdd(Key);
					Bucket.GameTime = GameTime;
					Bucket.CohortKey = Key;
					Bucket.Count = Count;
					Bucket.CashSum = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("Cash"))));
					const double MeanCash = static_cast<double>(Bucket.CashSum) / Count;
					Bucket.CashSquaredSum = FMath::RoundToInt64(MeanCash * MeanCash * Count);
					Bucket.RepairCreditSum = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("RepairCredit"))));
					const double WoodSum = Ledger.GetBalance(ESimulationResource::Wood, SimpleAccount(Kingdom, TEXT("Wood")));
					const int32 MeanWoodBin = FMath::Clamp(FMath::RoundToInt(WoodSum / Count), 0, 4);
					Bucket.WoodCounts[MeanWoodBin] = Count;
					Bucket.MacroIntent = EMacroIntent::Routine;
				}
			}

			for (const FResidentCoreState& Resident : Residents)
			{
				AddResident(Resident);
			}

			TArray<FString> Keys;
			Buckets.GetKeys(Keys);
			Keys.Sort();
			OutObservations.Reserve(Keys.Num());
			for (const FString& Key : Keys)
			{
				OutObservations.Add(Buckets.FindChecked(Key));
			}
		}

		void FUnifiedRuntime::PublishReadOnlyObservations(
			const FSimulationTime GameTime,
			const FSimulationTime ProcessedTime)
		{
			if (Options.EventSink != nullptr)
			{
				for (const FSimulationEventRecord& Event : PendingEventObservations)
				{
					Options.EventSink->OnEventCommitted(Event);
				}
				PendingEventObservations.Reset();

				const TArray<FLedgerTransaction>& Transactions = Ledger.GetTransactions();
				for (; PublishedTransactionCount < Transactions.Num(); ++PublishedTransactionCount)
				{
					Options.EventSink->OnTransactionCommitted(Transactions[PublishedTransactionCount]);
				}
				for (; PublishedLODTransitionCount < LODTransitions.Num(); ++PublishedLODTransitionCount)
				{
					Options.EventSink->OnLODTransitionCommitted(LODTransitions[PublishedLODTransitionCount]);
				}
			}

			if (Options.Observer != nullptr || Options.EventSink != nullptr)
			{
				while (const FUnifiedNPCObservation* NPC = FinalizedNPCObservations.Find(PublishedActivationObservationCount))
				{
					if (Options.EventSink != nullptr)
					{
						Options.EventSink->OnActivationObserved(ActivationObservations[PublishedActivationObservationCount]);
					}
					if (Options.Observer != nullptr)
					{
						Options.Observer->OnNPCSnapshot(*NPC);
					}
					FinalizedNPCObservations.Remove(PublishedActivationObservationCount);
					++PublishedActivationObservationCount;
				}
			}

			if (Options.Observer != nullptr)
			{
				FUnifiedHourObservation Observation;
				Observation.GameTime = GameTime;
				Observation.KingdomA = BuildKingdomSnapshot(GameTime, EKingdom::A, false);
				Observation.KingdomB = BuildKingdomSnapshot(GameTime, EKingdom::B, false);
				Observation.PolicyState = PolicyStateAt(ProcessedTime);
				if (GameTime.Minutes % (6 * MinutesPerHour) == 0)
				{
					BuildCohortObservations(GameTime, Observation.Cohorts);
				}
				Options.Observer->OnHourCompleted(Observation);
			}
		}

		void FUnifiedRuntime::FillResult(FUnifiedRunResult& OutResult)
		{
			OutResult = {};
			OutResult.Method = Backend->GetMethod();
			OutResult.Scenario = Scenario;
			OutResult.Seed = Config.Seed;
			OutResult.PopulationPerKingdom = Config.PopulationPerKingdom;
			OutResult.Mode = Options.Mode;
			OutResult.bRetainCompletedEvents = Options.bRetainCompletedEvents;
			OutResult.bRecordSnapshots = Options.bRecordSnapshots;
			OutResult.bVerifyCohortApproximation = Options.bVerifyCohortApproximation;
			OutResult.FaultInjection = Options.FaultInjection;
			OutResult.ConfigHash = PopulationManifest.ConfigHash;
			OutResult.FinalTime = Clock.Now();
			OutResult.WarmupHourSteps = 7 * static_cast<int32>(HoursPerDay);
			OutResult.FormalHourSteps = 60 * static_cast<int32>(HoursPerDay);
			OutResult.KingdomAStocks = ReadStocks(EKingdom::A, true);
			OutResult.KingdomBStocks = ReadStocks(EKingdom::B, true);
			for (int32 State = 0; State < 4; ++State)
			{
				OutResult.KingdomAHomeStates[State] = CountHomes(EKingdom::A, static_cast<EHomeState>(State));
				OutResult.KingdomBHomeStates[State] = CountHomes(EKingdom::B, static_cast<EHomeState>(State));
			}
			if (Backend->GetPopulationRepresentation() != EBackendPopulationRepresentation::AggregateKingdom)
			{
				OutResult.Residents = Residents;
			}
			OutResult.Audit = LastAudit;
			OutResult.CoinResidual = LastCoinResidual;
			OutResult.CoreLedgerMismatchCount = CoreLedgerMismatchCount;
			OutResult.EventReferenceErrorCount = EventReferenceErrorCount;
			OutResult.ActiveCapViolationCount = ActiveCapViolationCount;
			OutResult.ReservationErrorCount = ReservationErrorCount;
			OutResult.TaskResetCount = TaskResetCount;
			OutResult.SimpleIndividualCoreStateCount = Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom ? Residents.Num() : 0;
			for (const FScheduledEvent& Pending : Scheduler.GetPendingEvents())
			{
				OutResult.PendingEventsAtOrBeforeEnd += Pending.ExecuteAt <= Clock.Now() ? 1 : 0;
			}
			Diagnostics.TransactionCount = Ledger.GetTransactions().Num();
			OutResult.Diagnostics = Diagnostics;
			OutResult.CostBreakdown = CostBreakdown;
			OutResult.Transactions = Ledger.GetTransactions();
			for (const TPair<FEventID, FSimulationEventRecord>& Pair : EventStore.GetEvents())
			{
				OutResult.Events.Add(Pair.Value);
			}
			OutResult.Events.Sort([](const FSimulationEventRecord& Left, const FSimulationEventRecord& Right)
			{
				return Left.EventID < Right.EventID;
			});
			OutResult.Snapshots = MoveTemp(Snapshots);
			OutResult.LODTransitions = LODTransitions;
			OutResult.ActivationObservations = ActivationObservations;
		}

		bool FUnifiedRuntime::ProcessHour(const FSimulationTime Time, FString& OutError)
		{
			if (Time.Minutes == 0 && !bEarthquakeApplied && !ApplyEarthquake(Time, OutError))
			{
				return false;
			}
			if (!ApplyPolicies(Time, OutError)
				|| !ApplyForestGrowth(EKingdom::A, Time, OutError)
				|| !ApplyForestGrowth(EKingdom::B, Time, OutError)
				|| !ApplyBaselineImport(EKingdom::A, Time, OutError)
				|| !ApplyBaselineImport(EKingdom::B, Time, OutError)
				|| !ApplyCommercialHarvest(EKingdom::A, Time, OutError)
				|| !ApplyCommercialHarvest(EKingdom::B, Time, OutError)
				|| !ApplyRoutineConsumption(EKingdom::A, Time, OutError)
				|| !ApplyRoutineConsumption(EKingdom::B, Time, OutError))
			{
				return false;
			}
			UpdateWoodPrice(EKingdom::A);
			UpdateWoodPrice(EKingdom::B);

			if (Backend->GetPlanningGranularity() == EBackendPlanningGranularity::Aggregate)
			{
				if (Residents.Num() > 0)
				{
					const double ValidationBefore = LastStepMeasurement.ValidationCpuMs;
					const double MicroStart = FPlatformTime::Seconds();
					if (!PlanDetailedResidents(Time, OutError))
					{
						return false;
					}
					LastStepMeasurement.MicroCpuMs += FMath::Max(
						0.0,
						(FPlatformTime::Seconds() - MicroStart) * 1000.0
							- (LastStepMeasurement.ValidationCpuMs - ValidationBefore));
				}
				const double MacroStart = FPlatformTime::Seconds();
				const bool bResult = PlanSimple(Time, OutError);
				LastStepMeasurement.MacroCpuMs += (FPlatformTime::Seconds() - MacroStart) * 1000.0;
				return bResult;
			}
			const double ValidationBefore = LastStepMeasurement.ValidationCpuMs;
			const double DetailedStart = FPlatformTime::Seconds();
			if (!PlanDetailedResidents(Time, OutError))
			{
				return false;
			}
			const double DetailedMs = FMath::Max(
				0.0,
				(FPlatformTime::Seconds() - DetailedStart) * 1000.0
					- (LastStepMeasurement.ValidationCpuMs - ValidationBefore));
			if (Backend->GetPlanningGranularity() == EBackendPlanningGranularity::Cohort)
			{
				LastStepMeasurement.MicroCpuMs += LastDetailedActivePlanningMs;
				LastStepMeasurement.MacroCpuMs += FMath::Max(0.0, DetailedMs - LastDetailedActivePlanningMs);
			}
			else
			{
				LastStepMeasurement.MicroCpuMs += DetailedMs;
			}
			return true;
		}

		bool FUnifiedRuntime::ApplyActivationTrace(const FSimulationTime Time, FString& OutError)
		{
			bool bActivate = false;
			bool bDeactivate = false;
			int32 TraceDay = INDEX_NONE;
			if (Time.Minutes == FSimulationTime::FromDays(7).Minutes)
			{
				bActivate = true;
				TraceDay = 7;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(8).Minutes)
			{
				bDeactivate = true;
				TraceDay = 7;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(14).Minutes)
			{
				bActivate = true;
				TraceDay = 14;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(15).Minutes)
			{
				bDeactivate = true;
				TraceDay = 14;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(30).Minutes)
			{
				bActivate = true;
				TraceDay = 30;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(31).Minutes)
			{
				bDeactivate = true;
				TraceDay = 30;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(45).Minutes)
			{
				bActivate = true;
				TraceDay = 45;
			}
			else if (Time.Minutes == FSimulationTime::FromDays(46).Minutes)
			{
				bDeactivate = true;
				TraceDay = 45;
			}

			if (!bActivate && !bDeactivate)
			{
				return true;
			}

			TArray<FResidentID> TraceResidents;
			if (TraceDay == 14)
			{
				TraceResidents = Day14ActivationResidents;
			}
			else
			{
				for (const FPersistentTestRecord& Record : ContinuitySample.Residents)
				{
					const bool bInTrace = TraceDay == 7 ? Record.bDay7 : TraceDay == 30 ? Record.bDay30 : Record.bDay45;
					if (bInTrace)
					{
						TraceResidents.Add(Record.ResidentID);
					}
				}
			}
			TraceResidents.Sort();
			for (const FResidentID ResidentID : TraceResidents)
			{
				++Diagnostics.ActivationRequestCount;
				if (bActivate && !ActivateResident(ResidentID, Time, OutError))
				{
					return false;
				}
				if (bDeactivate && !DeactivateResident(ResidentID, Time, OutError))
				{
					return false;
				}
				if (TraceDay == 14)
				{
					Diagnostics.Day14ActivationCount += bActivate ? 1 : 0;
					Diagnostics.Day14DeactivationCount += bDeactivate ? 1 : 0;
				}
			}
			Diagnostics.MaxActiveMicro = FMath::Max(Diagnostics.MaxActiveMicro, ActiveResidents.Num());
			return true;
		}

		void FUnifiedRuntime::RecordActivation(
			FResidentCoreState& Resident,
			const FSimulationTime Time,
			const bool bSimpleReconstructed)
		{
			FUnifiedActivationObservation& Observation = ActivationObservations.AddDefaulted_GetRef();
			Observation.ResidentID = Resident.ResidentID;
			Observation.ActivationTime = Time;
			Observation.bSimpleReconstructed = bSimpleReconstructed;
			PendingFirstActionObservations.Add(Resident.ResidentID, ActivationObservations.Num() - 1);
			if (Resident.ActiveEventID != 0)
			{
				RecordFirstAction(Resident.ResidentID, Resident.CurrentAction, true);
			}
		}

		void FUnifiedRuntime::RecordFirstAction(
			const FResidentID ResidentID,
			const EIndividualAction Action,
			const bool bContinuedCommittedEvent)
		{
			const int32* ObservationIndex = PendingFirstActionObservations.Find(ResidentID);
			if (ObservationIndex == nullptr || !ActivationObservations.IsValidIndex(*ObservationIndex))
			{
				return;
			}
			FUnifiedActivationObservation& Observation = ActivationObservations[*ObservationIndex];
			Observation.FirstAction = Action;
			Observation.bContinuedCommittedEvent = bContinuedCommittedEvent;
			if (Options.Observer != nullptr || Options.EventSink != nullptr)
			{
				const FResidentCoreState* Resident = FindResident(ResidentID);
				if (Resident != nullptr)
				{
					FUnifiedNPCObservation NPC;
					NPC.GameTime = Observation.ActivationTime;
					NPC.Resident = *Resident;
					NPC.FirstAction = Action;
					FinalizedNPCObservations.Add(*ObservationIndex, MoveTemp(NPC));
				}
			}
			PendingFirstActionObservations.Remove(ResidentID);
			++Diagnostics.FirstActionCount;
		}

		void FUnifiedRuntime::RecordLODTransition(
			const FResidentCoreState& Resident,
			const EResidentRepresentation From,
			const EResidentRepresentation To,
			const FSimulationTime Time)
		{
			FLODTransitionRecord& Transition = LODTransitions.AddDefaulted_GetRef();
			Transition.PersistentID = Resident.PersistentID;
			Transition.From = From;
			Transition.To = To;
			Transition.RequestedTime = Time;
			Transition.CommittedTime = Time;
			Transition.ArriveID = Resident.ActiveArriveID;
			Transition.Bucket = FString::Printf(
				TEXT("K=%d|P=%d|I=%d|H=%d|M=%d"),
				static_cast<int32>(Resident.Kingdom),
				static_cast<int32>(Resident.Profession),
				static_cast<int32>(Resident.IncomeBand),
				static_cast<int32>(Resident.HomeState),
				static_cast<int32>(Resident.MacroIntent));
			Transition.Result = ELODTransitionResult::Committed;
		}

		bool FUnifiedRuntime::ActivateResident(
			const FResidentID ResidentID,
			const FSimulationTime Time,
			FString& OutError)
		{
			if (ActiveResidents.Contains(ResidentID) || ActiveResidents.Num() >= ActiveMicroCap)
			{
				OutError = TEXT("Fixed Activation Trace contains a duplicate request or exceeds the global Active Micro cap.");
				return false;
			}
			if (Backend->GetActivationBridge() == EBackendActivationBridge::ReconstructedMicro)
			{
				if (!ActivateSimpleResident(ResidentID, Time, OutError))
				{
					return false;
				}
				RecordLODTransition(
					*FindResident(ResidentID),
					EResidentRepresentation::CohortManaged,
					EResidentRepresentation::ActiveMicro,
					Time);
				return true;
			}

			FResidentCoreState* Resident = FindResident(ResidentID);
			if (Resident == nullptr)
			{
				OutError = TEXT("Fixed Activation Trace references an unknown resident.");
				return false;
			}
			const FEventID EventID = Resident->ActiveEventID;
			const FArriveID ArriveID = Resident->ActiveArriveID;
			const FReservationID ReservationID = Resident->ActiveReservationID;
			const FSimulationTime StartTime = Resident->ActionStartTime;
			const FSimulationTime EndTime = Resident->ActionEndTime;
			const EResidentRepresentation From = Resident->Representation;
			Resident->Representation = EResidentRepresentation::ActiveMicro;
			ActiveResidents.Add(ResidentID);
			if (Resident->ActiveEventID != EventID
				|| Resident->ActiveArriveID != ArriveID
				|| Resident->ActiveReservationID != ReservationID
				|| !(Resident->ActionStartTime == StartTime)
				|| !(Resident->ActionEndTime == EndTime))
			{
				++TaskResetCount;
			}
			RecordActivation(*Resident, Time, false);
			RecordLODTransition(*Resident, From, EResidentRepresentation::ActiveMicro, Time);
			return true;
		}

		bool FUnifiedRuntime::DeactivateResident(
			const FResidentID ResidentID,
			const FSimulationTime Time,
			FString& OutError)
		{
			if (!ActiveResidents.Contains(ResidentID))
			{
				OutError = TEXT("Fixed Activation Trace attempted to deactivate a resident who is not active.");
				return false;
			}
			if (Backend->GetActivationBridge() == EBackendActivationBridge::ReconstructedMicro)
			{
				const FResidentCoreState* ActiveResident = FindResident(ResidentID);
				if (ActiveResident == nullptr)
				{
					return DeactivateSimpleResident(ResidentID, Time, OutError);
				}
				const FResidentCoreState Before = *ActiveResident;
				if (!DeactivateSimpleResident(ResidentID, Time, OutError))
				{
					return false;
				}
				RecordLODTransition(Before, EResidentRepresentation::ActiveMicro, EResidentRepresentation::CohortManaged, Time);
				return true;
			}

			FResidentCoreState* Resident = FindResident(ResidentID);
			if (Resident == nullptr)
			{
				OutError = TEXT("Fixed Activation Trace cannot find the resident being deactivated.");
				return false;
			}
			const FEventID EventID = Resident->ActiveEventID;
			const FArriveID ArriveID = Resident->ActiveArriveID;
			const FReservationID ReservationID = Resident->ActiveReservationID;
			const FSimulationTime StartTime = Resident->ActionStartTime;
			const FSimulationTime EndTime = Resident->ActionEndTime;
			const EResidentRepresentation From = Resident->Representation;
			Resident->Representation = EResidentRepresentation::CohortManaged;
			ActiveResidents.Remove(ResidentID);
			if (Resident->ActiveEventID != EventID
				|| Resident->ActiveArriveID != ArriveID
				|| Resident->ActiveReservationID != ReservationID
				|| !(Resident->ActionStartTime == StartTime)
				|| !(Resident->ActionEndTime == EndTime))
			{
				++TaskResetCount;
			}
			if (PendingFirstActionObservations.Contains(ResidentID))
			{
				RecordFirstAction(ResidentID, Resident->CurrentAction == EIndividualAction::None ? EIndividualAction::Wait : Resident->CurrentAction, Resident->ActiveEventID != 0);
			}
			RecordLODTransition(*Resident, From, EResidentRepresentation::CohortManaged, Time);
			return true;
		}

		bool FUnifiedRuntime::ActivateSimpleResident(
			const FResidentID ResidentID,
			const FSimulationTime Time,
			FString& OutError)
		{
			const FInitialResidentRecord* Initial = FindInitialResident(ResidentID);
			if (Initial == nullptr || FindResident(ResidentID) != nullptr)
			{
				OutError = TEXT("Simple activation cannot resolve a unique manifest identity.");
				return false;
			}

			FSimpleKingdomState& Aggregate = SimpleStates[KingdomIndex(Initial->Kingdom)];
			int32 AggregatePopulation = 0;
			for (const int32 Count : Aggregate.HomeStates)
			{
				AggregatePopulation += Count;
			}
			if (AggregatePopulation <= 0)
			{
				OutError = TEXT("Simple activation has no aggregate resident to reconstruct.");
				return false;
			}
			int32 ReconstructableCounts[4] =
			{
				Aggregate.HomeStates[HomeStateIndex(EHomeState::Healthy)],
				FMath::Max(0, Aggregate.HomeStates[HomeStateIndex(EHomeState::DamagedWaiting)] - Aggregate.BusyWaitingCount),
				0,
				Aggregate.HomeStates[HomeStateIndex(EHomeState::Repaired)]
			};
			const int32 ReconstructablePopulation = ReconstructableCounts[0]
				+ ReconstructableCounts[1]
				+ ReconstructableCounts[2]
				+ ReconstructableCounts[3];
			if (ReconstructablePopulation <= 0)
			{
				OutError = TEXT("Simple activation cannot split a resident from an aggregate committed event.");
				return false;
			}
			int32 Rank = static_cast<int32>(CompetitionOrderKey(Config.Seed, Time.Minutes, ResidentID, 0x51A1ull) % static_cast<uint64>(ReconstructablePopulation));
			EHomeState ReconstructedHomeState = EHomeState::Healthy;
			for (int32 StateIndex = 0; StateIndex < 4; ++StateIndex)
			{
				if (Rank < ReconstructableCounts[StateIndex])
				{
					ReconstructedHomeState = static_cast<EHomeState>(StateIndex);
					break;
				}
				Rank -= ReconstructableCounts[StateIndex];
			}

			const int32 Cash = FMath::FloorToInt(Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Initial->Kingdom, TEXT("Cash"))) / AggregatePopulation);
			const int32 Credit = FMath::FloorToInt(Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Initial->Kingdom, TEXT("RepairCredit"))) / AggregatePopulation);
			const int32 Wood = FMath::FloorToInt(Ledger.GetBalance(ESimulationResource::Wood, SimpleAccount(Initial->Kingdom, TEXT("Wood"))) / AggregatePopulation);
			Diagnostics.LedgerQueryCount += 3;
			if (!Submit(Time, ESimulationResource::Coin, SimpleAccount(Initial->Kingdom, TEXT("Cash")), SimpleMicroAccount(ResidentID, TEXT("Cash")), Cash, false, FString::Printf(TEXT("SIMPLE-ACTIVATE-CASH-%lld-M%lld"), ResidentID, Time.Minutes), 0, 0, 0, OutError)
				|| !Submit(Time, ESimulationResource::Coin, SimpleAccount(Initial->Kingdom, TEXT("RepairCredit")), SimpleMicroAccount(ResidentID, TEXT("RepairCredit")), Credit, false, FString::Printf(TEXT("SIMPLE-ACTIVATE-CREDIT-%lld-M%lld"), ResidentID, Time.Minutes), 0, 0, 0, OutError)
				|| !Submit(Time, ESimulationResource::Wood, SimpleAccount(Initial->Kingdom, TEXT("Wood")), SimpleMicroAccount(ResidentID, TEXT("Wood")), Wood, false, FString::Printf(TEXT("SIMPLE-ACTIVATE-WOOD-%lld-M%lld"), ResidentID, Time.Minutes), 0, 0, 0, OutError))
			{
				return false;
			}

			--Aggregate.HomeStates[HomeStateIndex(ReconstructedHomeState)];
			FResidentCoreState& Resident = Residents.AddDefaulted_GetRef();
			Resident.ResidentID = Initial->ResidentID;
			Resident.HomeID = Initial->HomeID;
			Resident.PersistentID = Initial->PersistentID;
			Resident.Name = Initial->Name;
			Resident.Kingdom = Initial->Kingdom;
			Resident.Profession = Initial->Profession;
			Resident.IncomeBand = Initial->IncomeBand;
			Resident.Cash = Cash;
			Resident.RepairCredit = Credit;
			Resident.InventoryWood = Wood;
			Resident.HomeState = ReconstructedHomeState;
			Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
			Resident.MacroIntent = Resident.CurrentGoal == EIndividualGoal::RoutineLife ? EMacroIntent::Routine : EMacroIntent::Wait;
			Resident.LastUpdateTime = Time;
			Resident.RNGStreamKey = static_cast<uint32>(Mix64(Mix64(static_cast<uint64>(static_cast<uint32>(Config.Seed))) ^ Mix64(static_cast<uint64>(ResidentID))));
			Resident.Representation = EResidentRepresentation::ActiveMicro;
			ResidentIndices.Add(ResidentID, Residents.Num() - 1);
			ActiveResidents.Add(ResidentID);
			++Diagnostics.SimpleMicroReconstructionCount;
			RecordActivation(Resident, Time, true);
			return true;
		}

		bool FUnifiedRuntime::DeactivateSimpleResident(
			const FResidentID ResidentID,
			const FSimulationTime Time,
			FString& OutError)
		{
			const int32* FoundIndex = ResidentIndices.Find(ResidentID);
			if (FoundIndex == nullptr || !Residents.IsValidIndex(*FoundIndex))
			{
				OutError = TEXT("Simple deactivation cannot find its temporary CoreState.");
				return false;
			}
			const int32 ResidentIndex = *FoundIndex;
			FResidentCoreState& Resident = Residents[ResidentIndex];
			SyncResident(Resident);
			FSimpleKingdomState& Aggregate = SimpleStates[KingdomIndex(Resident.Kingdom)];
			bool bCountsAsBusyWaiting = false;

			if (Resident.ActiveEventID != 0)
			{
				const FSimulationEventRecord* Stored = EventStore.Find(Resident.ActiveEventID);
				if (Stored == nullptr || Stored->State != ESimulationEventState::Pending)
				{
					OutError = TEXT("Simple deactivation cannot convert a missing committed event.");
					return false;
				}
				const FSimulationEventRecord Before = *Stored;
				FSimpleDelayedAction Delayed;
				Delayed.EventID = Before.EventID;
				Delayed.ExecuteAt = Before.Event.EndTime;
				Delayed.ArriveID = Before.Event.ArriveID;
				Delayed.Kingdom = Resident.Kingdom;
				Delayed.Action = Resident.CurrentAction;
				Delayed.ParticipantCount = 1;
				Delayed.WoodQuantity = Before.Event.WoodQuantity;
				Delayed.CoinQuantity = Resident.CurrentAction == EIndividualAction::Work ? FIndividualDomain::GetWorkIncome(Resident.IncomeBand) : 0;
				Delayed.ReservationID = Before.Event.ReservationID;
				bCountsAsBusyWaiting = Resident.HomeState == EHomeState::DamagedWaiting
					&& (Resident.CurrentAction == EIndividualAction::Work
						|| Resident.CurrentAction == EIndividualAction::BuyWood
						|| Resident.CurrentAction == EIndividualAction::ChopWood
						|| Resident.CurrentAction == EIndividualAction::Wait);
				Delayed.bCountsAsBusyWaiting = bCountsAsBusyWaiting;
				const FString AggregateOwner = Resident.Kingdom == EKingdom::A ? TEXT("Simple:A") : TEXT("Simple:B");
				if (!EventStore.ConvertPendingEventToAggregate(Before.EventID, ResidentOwner(ResidentID), AggregateOwner, OutError))
				{
					return false;
				}
				const FSimulationEventRecord* Converted = EventStore.Find(Before.EventID);
				if (Converted == nullptr
					|| Converted->Event.ArriveID != Before.Event.ArriveID
					|| Converted->Event.ReservationID != Before.Event.ReservationID
					|| !(Converted->Event.StartTime == Before.Event.StartTime)
					|| !(Converted->Event.EndTime == Before.Event.EndTime))
				{
					++TaskResetCount;
				}
				SimpleActions.Add(Before.EventID, Delayed);
			}

			++Aggregate.HomeStates[HomeStateIndex(Resident.HomeState)];
			Aggregate.BusyWaitingCount += bCountsAsBusyWaiting ? 1 : 0;
			if (!Submit(Time, ESimulationResource::Coin, SimpleMicroAccount(ResidentID, TEXT("Cash")), SimpleAccount(Resident.Kingdom, TEXT("Cash")), Resident.Cash, false, FString::Printf(TEXT("SIMPLE-DEACTIVATE-CASH-%lld-M%lld"), ResidentID, Time.Minutes), Resident.ActiveEventID, Resident.ActiveArriveID, 0, OutError)
				|| !Submit(Time, ESimulationResource::Coin, SimpleMicroAccount(ResidentID, TEXT("RepairCredit")), SimpleAccount(Resident.Kingdom, TEXT("RepairCredit")), Resident.RepairCredit, false, FString::Printf(TEXT("SIMPLE-DEACTIVATE-CREDIT-%lld-M%lld"), ResidentID, Time.Minutes), Resident.ActiveEventID, Resident.ActiveArriveID, 0, OutError)
				|| !Submit(Time, ESimulationResource::Wood, SimpleMicroAccount(ResidentID, TEXT("Wood")), SimpleAccount(Resident.Kingdom, TEXT("Wood")), Resident.InventoryWood, false, FString::Printf(TEXT("SIMPLE-DEACTIVATE-WOOD-%lld-M%lld"), ResidentID, Time.Minutes), Resident.ActiveEventID, Resident.ActiveArriveID, 0, OutError))
			{
				return false;
			}
			if (PendingFirstActionObservations.Contains(ResidentID))
			{
				RecordFirstAction(ResidentID, Resident.CurrentAction == EIndividualAction::None ? EIndividualAction::Wait : Resident.CurrentAction, Resident.ActiveEventID != 0);
			}

			ActiveResidents.Remove(ResidentID);
			ResidentIndices.Remove(ResidentID);
			Residents.RemoveAtSwap(ResidentIndex, 1, EAllowShrinking::No);
			if (Residents.IsValidIndex(ResidentIndex))
			{
				ResidentIndices.FindOrAdd(Residents[ResidentIndex].ResidentID) = ResidentIndex;
			}
			++Diagnostics.SimpleMicroWritebackCount;
			return true;
		}

		bool FUnifiedRuntime::ApplyEarthquake(const FSimulationTime Time, FString& OutError)
		{
			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				FSimpleKingdomState& State = SimpleStates[KingdomIndex(EKingdom::A)];
				const int32 Damaged = DamageList.DamagedResidents.Num();
				State.HomeStates[HomeStateIndex(EHomeState::Healthy)] -= Damaged;
				State.HomeStates[HomeStateIndex(EHomeState::DamagedWaiting)] += Damaged;
			}
			else
			{
				for (const FEarthquakeDamageRecord& Damage : DamageList.DamagedResidents)
				{
					FResidentCoreState* Resident = FindResident(Damage.ResidentID);
					if (Resident == nullptr)
					{
						OutError = TEXT("Earthquake Damage List references an unknown resident.");
						return false;
					}
					Resident->HomeState = EHomeState::DamagedWaiting;
					Resident->CurrentGoal = EIndividualGoal::RestoreHome;
					Resident->MacroIntent = EMacroIntent::Wait;
					Resident->LastUpdateTime = Time;
					++Resident->Version;
				}
			}
			bEarthquakeApplied = true;
			return CreateInstantEvent(TEXT("EarthquakeDamage"), Time, DamageList.DamagedResidents.Num(), 0, OutError);
		}

		bool FUnifiedRuntime::ApplyPolicies(const FSimulationTime Time, FString& OutError)
		{
			if (Time.Minutes < 0 || Time.Minutes % MinutesPerDay != 0)
			{
				return true;
			}
			const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
			switch (Scenario)
			{
			case EStage2Scenario::HarvestCap:
				if (Day == 2 && !CreateInstantEvent(TEXT("HarvestCapAnnounced"), Time, 1, HarvestCapPolicyID, OutError)) return false;
				if (Day == 3 && !CreateInstantEvent(TEXT("HarvestCapActive"), Time, 1, HarvestCapPolicyID, OutError)) return false;
				if (Day == 30 && !CreateInstantEvent(TEXT("HarvestCapEnded"), Time, 1, HarvestCapPolicyID, OutError)) return false;
				break;
			case EStage2Scenario::StateImport:
				if (Day == 2 && !CreateInstantEvent(TEXT("StateImportAnnounced"), Time, 1, StateImportPolicyID, OutError)) return false;
				if (Day >= 2 && Day <= 14 && !PlaceStateImportOrder(Time, OutError)) return false;
				break;
			case EStage2Scenario::RepairAid:
				if (Day == 2 && !CalculateAidEligibility(Time, OutError)) return false;
				if (Day == 3 && !PayRepairAid(Time, OutError)) return false;
				break;
			case EStage2Scenario::None:
			default:
				break;
			}
			return true;
		}

		bool FUnifiedRuntime::CreateInstantEvent(
			const TCHAR* Type,
			const FSimulationTime Time,
			const int32 Participants,
			const FPolicyID PolicyID,
			FString& OutError)
		{
			FSimulationEventRequest Request;
			Request.Type = Type;
			Request.Owner = TEXT("UnifiedRuntime");
			Request.StartTime = Time;
			Request.EndTime = Time;
			Request.ArriveID = Scheduler.IssueArriveID();
			Request.ParticipantCount = FMath::Max(1, Participants);
			Request.PolicyID = PolicyID;
			FEventID EventID = 0;
			if (!CreateEvent(Request, EventID, OutError)
				|| !EventStore.CompleteEvent(EventID, OutError))
			{
				return false;
			}
			++Diagnostics.EventCount;
			if (!Options.bRetainCompletedEvents && !EventStore.RemoveCompletedEvent(EventID, OutError))
			{
				return false;
			}
			return true;
		}

		bool FUnifiedRuntime::PlaceStateImportOrder(const FSimulationTime Time, FString& OutError)
		{
			const FKingdomStocks Stocks = ReadStocks(EKingdom::A, false);
			const double WaitingHomes = CountHomes(EKingdom::A, EHomeState::DamagedWaiting);
			const double ExpectedRepairWoodUse = FMath::Min(
				WaitingHomes,
				RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom * 3.0) * RepairWoodPerHome;
			const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
			const double StockGapPerDay = FMath::Max(0.0, (TargetMarketWood - Stocks.MarketWoodAvailable - Stocks.WoodInTransit) / 3.0);
			double Quantity = FMath::Min(
				ExpectedRepairWoodUse + StockGapPerDay,
				StateImportDailyCapPerPerson * Config.PopulationPerKingdom);
			Quantity = FMath::Min(Quantity, ImportBudgetRemaining / StateImportPrice);
			Quantity = FMath::Min(Quantity, Stocks.TreasuryAvailable / StateImportPrice);
			if (Quantity <= UE_DOUBLE_SMALL_NUMBER)
			{
				return true;
			}
			int64 Cost = FMath::CeilToInt64(Quantity * StateImportPrice);
			if (Cost > ImportBudgetRemaining || Cost > Stocks.TreasuryAvailable)
			{
				const int64 CoinLimit = FMath::Min(ImportBudgetRemaining, Stocks.TreasuryAvailable);
				Quantity = CoinLimit / StateImportPrice;
				Cost = FMath::CeilToInt64(Quantity * StateImportPrice);
			}
			if (ConsumeFaultInjection(EUnifiedFaultInjectionPoint::StateImportPreflight, INDEX_NONE))
			{
				return true;
			}

			const FArriveID ArriveID = Scheduler.IssueArriveID();
			FSimulationEventRequest Event;
			Event.Type = TEXT("StateImport");
			Event.Owner = TEXT("UnifiedRuntime:A");
			Event.StartTime = Time;
			Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + 3 * MinutesPerDay);
			Event.ArriveID = ArriveID;
			Event.ParticipantCount = 1;
			Event.PolicyID = StateImportPolicyID;
			FEventID EventID = 0;
			if (!CreateEvent(Event, EventID, OutError))
			{
				return false;
			}
			++Diagnostics.EventCount;

			FReservationRequest Reservation;
			Reservation.IdempotencyKey = FString::Printf(TEXT("UNIFIED-IMPORT-RESERVE-%lld"), EventID);
			Reservation.GameTime = Time;
			Reservation.Resource = ESimulationResource::Coin;
			Reservation.SourceAccount = MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable"));
			Reservation.ReservedAccount = MakeKingdomAccount(EKingdom::A, TEXT("TreasuryReserved"));
			Reservation.Quantity = Cost;
			Reservation.EventID = EventID;
			Reservation.ArriveID = ArriveID;
			Reservation.PolicyID = StateImportPolicyID;
			FReservationID ReservationID = 0;
			if (!Reservations.CreateReservation(Reservation, Ledger, ReservationID, OutError)
				|| !EventStore.SetReservationID(EventID, ReservationID, OutError)
				|| !Submit(
					Time,
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit")),
					Quantity,
					true,
					FString::Printf(TEXT("UNIFIED-IMPORT-WOOD-%lld"), EventID),
					EventID,
					ArriveID,
					StateImportPolicyID,
					OutError)
				|| !Scheduler.Schedule({ EventID, ArriveID, Event.EndTime }, Time, OutError))
			{
				return false;
			}
			ImportBatches.Add(EventID, { ReservationID, Quantity });
			ImportBudgetRemaining -= Cost;
			return true;
		}

		bool FUnifiedRuntime::CalculateAidEligibility(const FSimulationTime Time, FString& OutError)
		{
			AidEligibleResidentIndices.Reset();
			AidArriveIDs.Reset();
			SimpleAidEligibleCount = 0;
			const int64 Required = PaymentCoins(static_cast<int32>(RepairWoodPerHome), WoodPrices[KingdomIndex(EKingdom::A)]);
			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				const FSimpleKingdomState& State = SimpleStates[KingdomIndex(EKingdom::A)];
				const double MeanCash = Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(EKingdom::A, TEXT("Cash"))) / Config.PopulationPerKingdom;
				const double MeanCredit = Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(EKingdom::A, TEXT("RepairCredit"))) / Config.PopulationPerKingdom;
				Diagnostics.LedgerQueryCount += 2;
				SimpleAidEligibleCount = MeanCash + MeanCredit < Required
					? FMath::FloorToInt(State.HomeStates[HomeStateIndex(EHomeState::DamagedWaiting)] * 0.70)
					: 0;
			}
			else
			{
				TArray<TPair<uint64, int32>> Ordered;
				for (int32 Index = 0; Index < Residents.Num(); ++Index)
				{
					const FResidentCoreState& Resident = Residents[Index];
					if (Resident.Kingdom == EKingdom::A
						&& Resident.IncomeBand == EIncomeBand::Low
						&& Resident.HomeState == EHomeState::DamagedWaiting
						&& !Resident.bAidReceived
						&& Resident.Cash + Resident.RepairCredit < Required)
					{
						Ordered.Add({ CompetitionOrderKey(Config.Seed, Time.Minutes, Resident.ResidentID, 0xA1D00001ull), Index });
					}
				}
				Ordered.Sort([](const TPair<uint64, int32>& Left, const TPair<uint64, int32>& Right)
				{
					return Left.Key == Right.Key ? Left.Value < Right.Value : Left.Key < Right.Key;
				});
				for (const TPair<uint64, int32>& Entry : Ordered)
				{
					AidEligibleResidentIndices.Add(Entry.Value);
					AidArriveIDs.Add(Entry.Value, Scheduler.IssueArriveID());
				}
			}
			return CreateInstantEvent(
				TEXT("RepairAidEligibility"),
				Time,
				Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom ? SimpleAidEligibleCount : AidEligibleResidentIndices.Num(),
				RepairAidPolicyID,
				OutError);
		}

		bool FUnifiedRuntime::PayRepairAid(const FSimulationTime Time, FString& OutError)
		{
			const int64 Treasury = FMath::RoundToInt64(Ledger.GetBalance(
				ESimulationResource::Coin,
				MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable"))));
			++Diagnostics.LedgerQueryCount;
			const int64 Budget = FMath::Min<int64>(FMath::FloorToInt64(0.40 * Config.PopulationPerKingdom), Treasury);
			int32 PaidCount = FMath::Min(
				Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom ? SimpleAidEligibleCount : AidEligibleResidentIndices.Num(),
				static_cast<int32>(Budget / static_cast<int64>(RepairAidPerHome)));
			if (!CreateInstantEvent(TEXT("RepairAidPayment"), Time, PaidCount, RepairAidPolicyID, OutError))
			{
				return false;
			}
			if (Backend->GetPopulationRepresentation() == EBackendPopulationRepresentation::AggregateKingdom)
			{
				return PaidCount <= 0 || Submit(
					Time,
					ESimulationResource::Coin,
					MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable")),
					SimpleAccount(EKingdom::A, TEXT("RepairCredit")),
					PaidCount * RepairAidPerHome,
					false,
					TEXT("UNIFIED-SIMPLE-REPAIR-AID"),
					0,
					Scheduler.IssueArriveID(),
					RepairAidPolicyID,
					OutError);
			}

			for (const int32 ResidentIndex : AidEligibleResidentIndices)
			{
				if (PaidCount <= 0)
				{
					break;
				}
				FResidentCoreState& Resident = Residents[ResidentIndex];
				const FArriveID ArriveID = AidArriveIDs.FindRef(ResidentIndex);
				if (!Submit(
					Time,
					ESimulationResource::Coin,
					MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable")),
					ResidentLedgerAccount(Resident.ResidentID, TEXT("RepairCredit")),
					RepairAidPerHome,
					false,
					FString::Printf(TEXT("UNIFIED-REPAIR-AID-%lld"), Resident.ResidentID),
					0,
					ArriveID,
					RepairAidPolicyID,
					OutError))
				{
					return false;
				}
				SyncResident(Resident);
				Resident.bAidReceived = true;
				Resident.LastUpdateTime = Time;
				++Resident.Version;
				--PaidCount;
			}
			return true;
		}

		bool FUnifiedRuntime::IsHarvestCapActive(const EKingdom Kingdom, const FSimulationTime Time) const
		{
			return Kingdom == EKingdom::A
				&& Scenario == EStage2Scenario::HarvestCap
				&& Time.Minutes >= FSimulationTime::FromDays(3).Minutes
				&& Time.Minutes < FSimulationTime::FromDays(30).Minutes;
		}

		void FUnifiedRuntime::EnsureHarvestDay(const EKingdom Kingdom, const FSimulationTime Time)
		{
			const int32 Index = KingdomIndex(Kingdom);
			const int32 Day = static_cast<int32>(FMath::FloorToDouble(static_cast<double>(Time.Minutes) / MinutesPerDay));
			if (HarvestDays[Index] != Day)
			{
				HarvestDays[Index] = Day;
				HarvestRemaining[Index] = IsHarvestCapActive(Kingdom, Time)
					? HarvestCapPerPersonPerDay * Config.PopulationPerKingdom
					: TNumericLimits<double>::Max();
			}
		}

		double FUnifiedRuntime::HarvestAllowance(const EKingdom Kingdom, const FSimulationTime Time)
		{
			EnsureHarvestDay(Kingdom, Time);
			return HarvestRemaining[KingdomIndex(Kingdom)];
		}

		void FUnifiedRuntime::EnsureRepairDay(const EKingdom Kingdom, const FSimulationTime Time)
		{
			const int32 Index = KingdomIndex(Kingdom);
			const int32 Day = static_cast<int32>(FMath::FloorToDouble(static_cast<double>(Time.Minutes) / MinutesPerDay));
			if (RepairDays[Index] != Day)
			{
				RepairDays[Index] = Day;
				RepairRemaining[Index] = FMath::FloorToInt(RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom);
			}
		}

		bool FUnifiedRuntime::ApplyForestGrowth(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
		{
			const FKingdomStocks Stocks = ReadStocks(Kingdom, false);
			const double Growth = FMath::Max(
				0.0,
				ForestGrowthRatePerDay * Stocks.ForestWood * (1.0 - Stocks.ForestWood / Stocks.ForestCapacity) / HoursPerGameDay);
			return Submit(
				Time,
				ESimulationResource::Wood,
				ExternalBoundaryAccount,
				MakeKingdomAccount(Kingdom, TEXT("ForestWood")),
				Growth,
				true,
				FString::Printf(TEXT("UNIFIED-GROWTH-%d-M%lld"), KingdomIndex(Kingdom), Time.Minutes),
				0,
				0,
				0,
				OutError);
		}

		bool FUnifiedRuntime::ApplyBaselineImport(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
		{
			return Submit(
				Time,
				ESimulationResource::Wood,
				ExternalBoundaryAccount,
				MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
				BaselineImportPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay,
				true,
				FString::Printf(TEXT("UNIFIED-BASELINE-IMPORT-%d-M%lld"), KingdomIndex(Kingdom), Time.Minutes),
				0,
				0,
				0,
				OutError);
		}

		bool FUnifiedRuntime::ApplyCommercialHarvest(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
		{
			const FKingdomStocks Stocks = ReadStocks(Kingdom, false);
			const double Allowance = HarvestAllowance(Kingdom, Time);
			const double Desired = BaselineHarvestPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay;
			const double Quantity = FMath::Min(Desired, FMath::Min(Allowance, Stocks.ForestWood));
			if (!Submit(
				Time,
				ESimulationResource::Wood,
				MakeKingdomAccount(Kingdom, TEXT("ForestWood")),
				MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
				Quantity,
				false,
				FString::Printf(TEXT("UNIFIED-HARVEST-%d-M%lld"), KingdomIndex(Kingdom), Time.Minutes),
				0,
				0,
				IsHarvestCapActive(Kingdom, Time) ? HarvestCapPolicyID : 0,
				OutError))
			{
				return false;
			}
			if (IsHarvestCapActive(Kingdom, Time))
			{
				HarvestRemaining[KingdomIndex(Kingdom)] = FMath::Max(0.0, Allowance - Quantity);
			}
			return true;
		}

		bool FUnifiedRuntime::ApplyRoutineConsumption(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
		{
			const FKingdomStocks Stocks = ReadStocks(Kingdom, false);
			const double Desired = RoutineConsumptionPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay;
			const double Quantity = FMath::Min(Desired, Stocks.MarketWoodAvailable);
			return Submit(
				Time,
				ESimulationResource::Wood,
				MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
				ExternalBoundaryAccount,
				Quantity,
				true,
				FString::Printf(TEXT("UNIFIED-CONSUMPTION-%d-M%lld"), KingdomIndex(Kingdom), Time.Minutes),
				0,
				0,
				0,
				OutError);
		}

		void FUnifiedRuntime::UpdateWoodPrice(const EKingdom Kingdom)
		{
			const FKingdomStocks Stocks = ReadStocks(Kingdom, false);
			const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
			const double Target = FMath::Clamp(
				FMath::Sqrt(TargetMarketWood / FMath::Max(Stocks.MarketWoodAvailable, UE_DOUBLE_SMALL_NUMBER)),
				0.5,
				3.0);
			const int32 Index = KingdomIndex(Kingdom);
			WoodPrices[Index] += (Target - WoodPrices[Index]) / HoursPerGameDay;
		}

		bool FUnifiedRuntime::PlanDetailedResidents(const FSimulationTime Time, FString& OutError)
		{
			TArray<FActionCandidate> Candidates;
			++Diagnostics.FullPopulationScanCount;
			FIndividualWorldFacts WorldByKingdom[2];
			for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
			{
				const FKingdomStocks Stocks = ReadStocks(Kingdom, false);
				FIndividualWorldFacts& World = WorldByKingdom[KingdomIndex(Kingdom)];
				World.MarketWoodAvailable = Stocks.MarketWoodAvailable;
				World.ForestWood = Stocks.ForestWood;
				World.HarvestAllowance = HarvestAllowance(Kingdom, Time);
				World.WoodPrice = Stocks.WoodPrice;
			}

			auto AddCandidate = [this, &Candidates, Time](
				const int32 ResidentIndex,
				const EIndividualAction Action,
				const bool bCohortApproximation = false)
			{
				const FResidentCoreState& Resident = Residents[ResidentIndex];
				FActionCandidate Candidate;
				Candidate.ResidentIndex = ResidentIndex;
				Candidate.Action = Action;
				Candidate.bActiveMicro = ActiveResidents.Contains(Resident.ResidentID);
				Candidate.bCohortApproximation = bCohortApproximation;
				Candidate.Scope.Kingdom = Resident.Kingdom;
				Candidate.OrderKey = CompetitionOrderKey(
					Config.Seed,
					Time.Minutes,
					Resident.ResidentID,
					static_cast<uint64>(Action));
				const int32 MissingWood = FMath::Max(0, static_cast<int32>(RepairWoodPerHome) - Resident.InventoryWood);
				switch (Action)
				{
				case EIndividualAction::BuyWood:
					Candidate.Scope.Resource = EUnifiedCompetitionResource::MarketWood;
					Candidate.Scope.Window = static_cast<int32>(Time.Minutes / MinutesPerHour);
					Candidate.Quantity = FMath::Max(1, MissingWood);
					break;
				case EIndividualAction::ChopWood:
					Candidate.Scope.Resource = EUnifiedCompetitionResource::ForestWood;
					Candidate.Scope.Window = static_cast<int32>(Time.Minutes / MinutesPerHour);
					Candidate.Quantity = FMath::Max(1, MissingWood);
					break;
				case EIndividualAction::StartRepair:
					Candidate.Scope.Resource = EUnifiedCompetitionResource::RepairCapacity;
					Candidate.Scope.Window = static_cast<int32>(Time.Minutes / MinutesPerDay);
					Candidate.Quantity = 1;
					break;
				default:
					Candidate.Scope.Resource = EUnifiedCompetitionResource::None;
					Candidate.Scope.Window = static_cast<int32>(Time.Minutes / MinutesPerHour);
					Candidate.Quantity = 1;
					break;
				}
				Candidates.Add(Candidate);
				if (Candidate.bActiveMicro)
				{
					RecordFirstAction(Resident.ResidentID, Action, false);
				}
			};

			if (Backend->GetPlanningGranularity() == EBackendPlanningGranularity::Cohort)
			{
				TMap<FString, TArray<int32>> CohortGroups;
				for (int32 ResidentIndex = 0; ResidentIndex < Residents.Num(); ++ResidentIndex)
				{
					FResidentCoreState& Resident = Residents[ResidentIndex];
					if (Resident.ActiveEventID != 0)
					{
						continue;
					}
					Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
					if (ActiveResidents.Contains(Resident.ResidentID))
					{
						const FIndividualWorldFacts& World = WorldByKingdom[KingdomIndex(Resident.Kingdom)];
						const double ActivePlanningStart = FPlatformTime::Seconds();
						const FIndividualPlan Plan = FIndividualDomain::BuildPlan(Resident, World);
						LastDetailedActivePlanningMs += (FPlatformTime::Seconds() - ActivePlanningStart) * 1000.0;
						const EIndividualAction Action = Plan.Actions.Num() > 0 ? Plan.Actions[0] : EIndividualAction::Wait;
						++Diagnostics.PlanningEvaluationCount;
						++Diagnostics.ActiveMicroPlanningEvaluationCount;
						AddCandidate(ResidentIndex, Action);
						continue;
					}

					const int32 PurchasingPower = Resident.Cash + Resident.RepairCredit;
					const int32 PurchasingPowerBand = PurchasingPower < 4 ? 0 : PurchasingPower < 8 ? 1 : 2;
					const int32 WoodBand = Resident.InventoryWood <= 0 ? 0 : Resident.InventoryWood < static_cast<int32>(RepairWoodPerHome) ? 1 : 2;
					const FString Key = FString::Printf(
						TEXT("%d|%d|%d|%d|%d|%d|%d"),
						static_cast<int32>(Resident.Kingdom),
						static_cast<int32>(Resident.Profession),
						static_cast<int32>(Resident.IncomeBand),
						static_cast<int32>(Resident.HomeState),
						static_cast<int32>(Resident.MacroIntent),
						PurchasingPowerBand,
						WoodBand);
					CohortGroups.FindOrAdd(Key).Add(ResidentIndex);
				}

				TArray<FString> Keys;
				CohortGroups.GetKeys(Keys);
				Keys.Sort();
				for (const FString& Key : Keys)
				{
					const TArray<int32>& Group = CohortGroups.FindChecked(Key);
					if (Group.Num() == 0)
					{
						continue;
					}
					FResidentCoreState Representative = Residents[Group[0]];
					int64 CashSum = 0;
					int64 CreditSum = 0;
					int64 WoodSum = 0;
					for (const int32 ResidentIndex : Group)
					{
						CashSum += Residents[ResidentIndex].Cash;
						CreditSum += Residents[ResidentIndex].RepairCredit;
						WoodSum += Residents[ResidentIndex].InventoryWood;
					}
					Representative.ResidentID = 0;
					Representative.PersistentID = 0;
					Representative.Name.Reset();
					Representative.Cash = FMath::RoundToInt(static_cast<double>(CashSum) / Group.Num());
					Representative.RepairCredit = FMath::RoundToInt(static_cast<double>(CreditSum) / Group.Num());
					Representative.InventoryWood = FMath::RoundToInt(static_cast<double>(WoodSum) / Group.Num());
					Representative.CurrentGoal = FIndividualDomain::SelectGoal(Representative);
					const FIndividualWorldFacts& World = WorldByKingdom[KingdomIndex(Representative.Kingdom)];
					const FIndividualPlan RepresentativePlan = FIndividualDomain::BuildPlan(Representative, World);
					const EIndividualAction Action = RepresentativePlan.Actions.Num() > 0 ? RepresentativePlan.Actions[0] : EIndividualAction::Wait;
					++Diagnostics.PlanningEvaluationCount;
					++Diagnostics.CohortPlanningEvaluationCount;
					for (const int32 ResidentIndex : Group)
					{
						if (Options.Mode == EUnifiedRunMode::Validation && Options.bVerifyCohortApproximation)
						{
							const double ValidationStart = FPlatformTime::Seconds();
							const FIndividualPlan MemberPlan = FIndividualDomain::BuildPlan(Residents[ResidentIndex], World);
							const EIndividualAction MemberAction = MemberPlan.Actions.Num() > 0 ? MemberPlan.Actions[0] : EIndividualAction::Wait;
							++Diagnostics.ValidationPlanningEvaluationCount;
							if (MemberPlan.Goal != RepresentativePlan.Goal || MemberAction != Action)
							{
								++Diagnostics.CohortDecisionDisagreementCount;
							}
							LastStepMeasurement.ValidationCpuMs += (FPlatformTime::Seconds() - ValidationStart) * 1000.0;
						}
						AddCandidate(ResidentIndex, Action, true);
					}
				}
			}
			else
			{
				for (int32 ResidentIndex = 0; ResidentIndex < Residents.Num(); ++ResidentIndex)
				{
					FResidentCoreState& Resident = Residents[ResidentIndex];
					if (Resident.ActiveEventID != 0)
					{
						continue;
					}
					Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
					const FIndividualWorldFacts& World = WorldByKingdom[KingdomIndex(Resident.Kingdom)];
					const FIndividualPlan Plan = FIndividualDomain::BuildPlan(Resident, World);
					const EIndividualAction Action = Plan.Actions.Num() > 0 ? Plan.Actions[0] : EIndividualAction::Wait;
					++Diagnostics.PlanningEvaluationCount;
					if (ActiveResidents.Contains(Resident.ResidentID))
					{
						++Diagnostics.ActiveMicroPlanningEvaluationCount;
					}
					AddCandidate(ResidentIndex, Action);
				}
			}

			Diagnostics.CandidateCount += Candidates.Num();
			return ResolveAndCommitCandidates(Time, Candidates, OutError);
		}

		bool FUnifiedRuntime::ResolveAndCommitCandidates(
			const FSimulationTime Time,
			TArray<FActionCandidate>& Candidates,
			FString& OutError)
		{
			Candidates.Sort([this](const FActionCandidate& Left, const FActionCandidate& Right)
			{
				if (Left.OrderKey != Right.OrderKey)
				{
					return Left.OrderKey < Right.OrderKey;
				}
				return Residents[Left.ResidentIndex].ResidentID < Residents[Right.ResidentIndex].ResidentID;
			});

			TSet<FUnifiedCompetitionScope> Scopes;
			TMap<FUnifiedCompetitionScope, uint8> RepresentationFlags;
			for (FActionCandidate& Candidate : Candidates)
			{
				Candidate.ArriveID = Scheduler.IssueArriveID();
				if (Candidate.Scope.Resource != EUnifiedCompetitionResource::None)
				{
					Scopes.Add(Candidate.Scope);
					uint8& Flags = RepresentationFlags.FindOrAdd(Candidate.Scope);
					Flags |= Candidate.bActiveMicro ? 0x2 : 0x1;
				}
			}
			Diagnostics.CompetitionScopeCount += Scopes.Num();
			for (const TPair<FUnifiedCompetitionScope, uint8>& Pair : RepresentationFlags)
			{
				Diagnostics.MixedRepresentationCompetitionCount += Pair.Value == 0x3 ? 1 : 0;
			}

			for (const FActionCandidate& Candidate : Candidates)
			{
				const FResidentCoreState& Resident = Residents[Candidate.ResidentIndex];
				bool bWon = true;
				switch (Candidate.Scope.Resource)
				{
				case EUnifiedCompetitionResource::MarketWood:
					bWon = Ledger.GetBalance(
						ESimulationResource::Wood,
						MakeKingdomAccount(Resident.Kingdom, TEXT("MarketWoodAvailable"))) + UE_DOUBLE_SMALL_NUMBER >= Candidate.Quantity;
					++Diagnostics.LedgerQueryCount;
					break;
				case EUnifiedCompetitionResource::ForestWood:
					bWon = Ledger.GetBalance(
						ESimulationResource::Wood,
						MakeKingdomAccount(Resident.Kingdom, TEXT("ForestWood"))) + UE_DOUBLE_SMALL_NUMBER >= Candidate.Quantity
						&& HarvestAllowance(Resident.Kingdom, Time) + UE_DOUBLE_SMALL_NUMBER >= Candidate.Quantity;
					++Diagnostics.LedgerQueryCount;
					break;
				case EUnifiedCompetitionResource::RepairCapacity:
					EnsureRepairDay(Resident.Kingdom, Time);
					bWon = RepairRemaining[KingdomIndex(Resident.Kingdom)] > 0;
					break;
				case EUnifiedCompetitionResource::None:
				default:
					break;
				}

				if (bWon)
				{
					if (!StartCandidate(Candidate, Time, OutError))
					{
						return false;
					}
				}
				else if (!StartFallbackWait(Candidate, Time, OutError))
				{
					return false;
				}
			}
			return true;
		}

		bool FUnifiedRuntime::StartCandidate(const FActionCandidate& Candidate, const FSimulationTime Time, FString& OutError)
		{
			switch (Candidate.Action)
			{
			case EIndividualAction::Routine:
				return StartTimedAction(Candidate, Time, FIndividualDomain::GetActionDuration(Candidate.Action), 0, 0, OutError);
			case EIndividualAction::Work:
				return StartTimedAction(Candidate, Time, FIndividualDomain::GetActionDuration(Candidate.Action), 0, 0, OutError);
			case EIndividualAction::Wait:
				return StartTimedAction(Candidate, Time, FIndividualDomain::GetActionDuration(Candidate.Action), 0, 0, OutError);
			case EIndividualAction::BuyWood:
				return StartBuyWood(Candidate, Time, OutError);
			case EIndividualAction::ChopWood:
				return StartChopWood(Candidate, Time, OutError);
			case EIndividualAction::StartRepair:
				return StartRepair(Candidate, Time, OutError);
			case EIndividualAction::ContinueRepair:
				return true;
			case EIndividualAction::None:
			default:
				return StartFallbackWait(Candidate, Time, OutError);
			}
		}

		bool FUnifiedRuntime::StartTimedAction(
			const FActionCandidate& Candidate,
			const FSimulationTime Time,
			const int64 Duration,
			const int32 WoodQuantity,
			const FReservationID ReservationID,
			FString& OutError)
		{
			FResidentCoreState& Resident = Residents[Candidate.ResidentIndex];
			FIndividualWorldFacts World;
			World.WoodPrice = WoodPrices[KingdomIndex(Resident.Kingdom)];
			const FIndividualActionEvaluation Evaluation = FIndividualDomain::EvaluateAction(
				Candidate.Action,
				{ Resident.Cash, Resident.RepairCredit, Resident.InventoryWood, Resident.HomeState },
				Resident.Profession,
				Resident.IncomeBand,
				World);
			if (Resident.ActiveEventID != 0 || Duration <= 0 || !Evaluation.bApplicable)
			{
				return Candidate.Action == EIndividualAction::Wait
					? false
					: StartFallbackWait(Candidate, Time, OutError);
			}
			FSimulationEventRequest Event;
			Event.Type = ToString(Candidate.Action);
			Event.Owner = ResidentOwner(Resident.ResidentID);
			Event.ResidentID = Resident.ResidentID;
			Event.ActionCode = static_cast<int32>(Candidate.Action);
			Event.WoodQuantity = WoodQuantity;
			Event.StartTime = Time;
			Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + Duration);
			Event.ReservationID = ReservationID;
			Event.ArriveID = Candidate.ArriveID;
			Event.ParticipantCount = 1;
			Event.Cause = ToString(Resident.CurrentGoal);
			FEventID EventID = 0;
			if (!CreateEvent(Event, EventID, OutError)
				|| !Scheduler.Schedule({ EventID, Candidate.ArriveID, Event.EndTime }, Time, OutError))
			{
				return false;
			}
			++Diagnostics.EventCount;
			Resident.CurrentAction = Candidate.Action;
			Resident.MacroIntent = ToMacroIntent(Candidate.Action);
			Resident.ActiveEventID = EventID;
			Resident.ActiveArriveID = Candidate.ArriveID;
			Resident.ActiveReservationID = ReservationID;
			Resident.ActionStartTime = Time;
			Resident.ActionEndTime = Event.EndTime;
			Resident.LastUpdateTime = Time;
			++Resident.Version;
			return true;
		}

		bool FUnifiedRuntime::StartBuyWood(const FActionCandidate& Candidate, const FSimulationTime Time, FString& OutError)
		{
			FResidentCoreState& Resident = Residents[Candidate.ResidentIndex];
			const double Available = Ledger.GetBalance(
				ESimulationResource::Wood,
				MakeKingdomAccount(Resident.Kingdom, TEXT("MarketWoodAvailable")));
			++Diagnostics.LedgerQueryCount;
			FIndividualWorldFacts World;
			World.MarketWoodAvailable = Available;
			World.WoodPrice = WoodPrices[KingdomIndex(Resident.Kingdom)];
			const FIndividualActionEvaluation Evaluation = FIndividualDomain::EvaluateAction(
				EIndividualAction::BuyWood,
				{ Resident.Cash, Resident.RepairCredit, Resident.InventoryWood, Resident.HomeState },
				Resident.Profession,
				Resident.IncomeBand,
				World);
			const int32 Quantity = Evaluation.WoodQuantity;
			const int64 Cost = Evaluation.CoinCost;
			if (Resident.ActiveEventID != 0 || !Evaluation.bApplicable)
			{
				return StartFallbackWait(Candidate, Time, OutError);
			}
			if (ConsumeFaultInjection(EUnifiedFaultInjectionPoint::BuyWoodPreflight, Candidate.ResidentIndex))
			{
				return StartFallbackWait(Candidate, Time, OutError);
			}

			FSimulationEventRequest Event;
			Event.Type = ToString(EIndividualAction::BuyWood);
			Event.Owner = ResidentOwner(Resident.ResidentID);
			Event.ResidentID = Resident.ResidentID;
			Event.ActionCode = static_cast<int32>(EIndividualAction::BuyWood);
			Event.WoodQuantity = Quantity;
			Event.StartTime = Time;
			Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + FIndividualDomain::GetActionDuration(EIndividualAction::BuyWood));
			Event.ArriveID = Candidate.ArriveID;
			Event.ParticipantCount = 1;
			Event.Cause = ToString(Resident.CurrentGoal);
			FEventID EventID = 0;
			if (!CreateEvent(Event, EventID, OutError))
			{
				return false;
			}
			++Diagnostics.EventCount;

			FReservationRequest Reservation;
			Reservation.IdempotencyKey = FString::Printf(TEXT("UNIFIED-BUY-RESERVE-%lld"), EventID);
			Reservation.GameTime = Time;
			Reservation.Resource = ESimulationResource::Wood;
			Reservation.SourceAccount = MakeKingdomAccount(Resident.Kingdom, TEXT("MarketWoodAvailable"));
			Reservation.ReservedAccount = MakeKingdomAccount(Resident.Kingdom, TEXT("MarketWoodReserved"));
			Reservation.Quantity = Quantity;
			Reservation.EventID = EventID;
			Reservation.ArriveID = Candidate.ArriveID;
			FReservationID ReservationID = 0;
			if (!Reservations.CreateReservation(Reservation, Ledger, ReservationID, OutError)
				|| !EventStore.SetReservationID(EventID, ReservationID, OutError))
			{
				return false;
			}

			const int32 CreditPayment = FMath::Min(Resident.RepairCredit, static_cast<int32>(Cost));
			const int32 CashPayment = static_cast<int32>(Cost) - CreditPayment;
			if ((CreditPayment > 0 && !Submit(
					Time,
					ESimulationResource::Coin,
					ResidentLedgerAccount(Resident.ResidentID, TEXT("RepairCredit")),
					MakeKingdomAccount(Resident.Kingdom, TEXT("MarketCoin")),
					CreditPayment,
					false,
					FString::Printf(TEXT("UNIFIED-BUY-CREDIT-%lld"), EventID),
					EventID,
					Candidate.ArriveID,
					0,
					OutError))
				|| (CashPayment > 0 && !Submit(
					Time,
					ESimulationResource::Coin,
					ResidentLedgerAccount(Resident.ResidentID, TEXT("Cash")),
					MakeKingdomAccount(Resident.Kingdom, TEXT("MarketCoin")),
					CashPayment,
					false,
					FString::Printf(TEXT("UNIFIED-BUY-CASH-%lld"), EventID),
					EventID,
					Candidate.ArriveID,
					0,
					OutError))
				|| !Scheduler.Schedule({ EventID, Candidate.ArriveID, Event.EndTime }, Time, OutError))
			{
				return false;
			}
			SyncResident(Resident);
			Resident.CurrentAction = EIndividualAction::BuyWood;
			Resident.MacroIntent = EMacroIntent::BuyWood;
			Resident.ActiveEventID = EventID;
			Resident.ActiveArriveID = Candidate.ArriveID;
			Resident.ActiveReservationID = ReservationID;
			Resident.ActionStartTime = Time;
			Resident.ActionEndTime = Event.EndTime;
			Resident.LastUpdateTime = Time;
			++Resident.Version;
			return true;
		}

		bool FUnifiedRuntime::StartChopWood(const FActionCandidate& Candidate, const FSimulationTime Time, FString& OutError)
		{
			FResidentCoreState& Resident = Residents[Candidate.ResidentIndex];
			const double Available = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Resident.Kingdom, TEXT("ForestWood")));
			++Diagnostics.LedgerQueryCount;
			FIndividualWorldFacts World;
			World.ForestWood = Available;
			World.HarvestAllowance = HarvestAllowance(Resident.Kingdom, Time);
			World.WoodPrice = WoodPrices[KingdomIndex(Resident.Kingdom)];
			const FIndividualActionEvaluation Evaluation = FIndividualDomain::EvaluateAction(
				EIndividualAction::ChopWood,
				{ Resident.Cash, Resident.RepairCredit, Resident.InventoryWood, Resident.HomeState },
				Resident.Profession,
				Resident.IncomeBand,
				World);
			const int32 Quantity = Evaluation.WoodQuantity;
			if (Resident.ActiveEventID != 0 || !Evaluation.bApplicable)
			{
				return StartFallbackWait(Candidate, Time, OutError);
			}
			if (ConsumeFaultInjection(EUnifiedFaultInjectionPoint::ChopWoodPreflight, Candidate.ResidentIndex))
			{
				return StartFallbackWait(Candidate, Time, OutError);
			}

			FSimulationEventRequest Event;
			Event.Type = ToString(EIndividualAction::ChopWood);
			Event.Owner = ResidentOwner(Resident.ResidentID);
			Event.ResidentID = Resident.ResidentID;
			Event.ActionCode = static_cast<int32>(EIndividualAction::ChopWood);
			Event.WoodQuantity = Quantity;
			Event.StartTime = Time;
			Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + FIndividualDomain::GetActionDuration(EIndividualAction::ChopWood));
			Event.ArriveID = Candidate.ArriveID;
			Event.ParticipantCount = 1;
			Event.Cause = ToString(Resident.CurrentGoal);
			Event.PolicyID = IsHarvestCapActive(Resident.Kingdom, Time) ? HarvestCapPolicyID : 0;
			FEventID EventID = 0;
			if (!CreateEvent(Event, EventID, OutError))
			{
				return false;
			}
			++Diagnostics.EventCount;

			FReservationRequest Reservation;
			Reservation.IdempotencyKey = FString::Printf(TEXT("UNIFIED-CHOP-RESERVE-%lld"), EventID);
			Reservation.GameTime = Time;
			Reservation.Resource = ESimulationResource::Wood;
			Reservation.SourceAccount = MakeKingdomAccount(Resident.Kingdom, TEXT("ForestWood"));
			Reservation.ReservedAccount = MakeKingdomAccount(Resident.Kingdom, TEXT("ForestWoodReserved"));
			Reservation.Quantity = Quantity;
			Reservation.EventID = EventID;
			Reservation.ArriveID = Candidate.ArriveID;
			Reservation.PolicyID = Event.PolicyID;
			FReservationID ReservationID = 0;
			if (!Reservations.CreateReservation(Reservation, Ledger, ReservationID, OutError)
				|| !EventStore.SetReservationID(EventID, ReservationID, OutError)
				|| !Scheduler.Schedule({ EventID, Candidate.ArriveID, Event.EndTime }, Time, OutError))
			{
				return false;
			}
			if (IsHarvestCapActive(Resident.Kingdom, Time))
			{
				HarvestRemaining[KingdomIndex(Resident.Kingdom)] -= Quantity;
			}
			Resident.CurrentAction = EIndividualAction::ChopWood;
			Resident.MacroIntent = EMacroIntent::ChopWood;
			Resident.ActiveEventID = EventID;
			Resident.ActiveArriveID = Candidate.ArriveID;
			Resident.ActiveReservationID = ReservationID;
			Resident.ActionStartTime = Time;
			Resident.ActionEndTime = Event.EndTime;
			Resident.LastUpdateTime = Time;
			++Resident.Version;
			return true;
		}

		bool FUnifiedRuntime::StartRepair(const FActionCandidate& Candidate, const FSimulationTime Time, FString& OutError)
		{
			FResidentCoreState& Resident = Residents[Candidate.ResidentIndex];
			EnsureRepairDay(Resident.Kingdom, Time);
			FIndividualWorldFacts World;
			World.WoodPrice = WoodPrices[KingdomIndex(Resident.Kingdom)];
			const FIndividualActionEvaluation Evaluation = FIndividualDomain::EvaluateAction(
				EIndividualAction::StartRepair,
				{ Resident.Cash, Resident.RepairCredit, Resident.InventoryWood, Resident.HomeState },
				Resident.Profession,
				Resident.IncomeBand,
				World);
			if (Resident.ActiveEventID != 0
				|| !Evaluation.bApplicable
				|| RepairRemaining[KingdomIndex(Resident.Kingdom)] <= 0)
			{
				return StartFallbackWait(Candidate, Time, OutError);
			}
			if (ConsumeFaultInjection(EUnifiedFaultInjectionPoint::StartRepairPreflight, Candidate.ResidentIndex))
			{
				return StartFallbackWait(Candidate, Time, OutError);
			}

			FSimulationEventRequest Event;
			Event.Type = TEXT("Repair");
			Event.Owner = ResidentOwner(Resident.ResidentID);
			Event.ResidentID = Resident.ResidentID;
			Event.ActionCode = static_cast<int32>(EIndividualAction::ContinueRepair);
			Event.WoodQuantity = static_cast<int32>(RepairWoodPerHome);
			Event.StartTime = Time;
			Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + FIndividualDomain::GetActionDuration(EIndividualAction::ContinueRepair));
			Event.ArriveID = Candidate.ArriveID;
			Event.ParticipantCount = 1;
			Event.Cause = ToString(EIndividualGoal::RestoreHome);
			FEventID EventID = 0;
			if (!CreateEvent(Event, EventID, OutError))
			{
				return false;
			}
			++Diagnostics.EventCount;
			if (!Submit(
					Time,
					ESimulationResource::Wood,
					ResidentLedgerAccount(Resident.ResidentID, TEXT("Wood")),
					MakeKingdomAccount(Resident.Kingdom, TEXT("WoodEmbeddedInRepairs")),
					RepairWoodPerHome,
					false,
					FString::Printf(TEXT("UNIFIED-REPAIR-START-%lld"), EventID),
					EventID,
					Candidate.ArriveID,
					0,
					OutError)
				|| !Scheduler.Schedule({ EventID, Candidate.ArriveID, Event.EndTime }, Time, OutError))
			{
				return false;
			}
			--RepairRemaining[KingdomIndex(Resident.Kingdom)];
			SyncResident(Resident);
			Resident.HomeState = EHomeState::UnderRepair;
			Resident.CurrentAction = EIndividualAction::ContinueRepair;
			Resident.MacroIntent = EMacroIntent::Repair;
			Resident.ActiveEventID = EventID;
			Resident.ActiveArriveID = Candidate.ArriveID;
			Resident.ActiveReservationID = 0;
			Resident.ActionStartTime = Time;
			Resident.ActionEndTime = Event.EndTime;
			Resident.LastUpdateTime = Time;
			++Resident.Version;
			return true;
		}

		bool FUnifiedRuntime::StartFallbackWait(const FActionCandidate& Candidate, const FSimulationTime Time, FString& OutError)
		{
			if (Candidate.bCohortApproximation && Candidate.Action != EIndividualAction::Wait)
			{
				++Diagnostics.CohortAllocationFallbackCount;
			}
			FActionCandidate Wait = Candidate;
			Wait.Action = EIndividualAction::Wait;
			Wait.ArriveID = Scheduler.IssueArriveID();
			return StartTimedAction(Wait, Time, FIndividualDomain::GetActionDuration(EIndividualAction::Wait), 0, 0, OutError);
		}

		bool FUnifiedRuntime::AdvanceChronologically(const FSimulationTime Target, FString& OutError)
		{
			TArray<FScheduledEvent> DueEvents;
			Scheduler.PopDueThrough(Target, DueEvents);
			for (const FScheduledEvent& Due : DueEvents)
			{
				if (!Clock.AdvanceTo(Due.ExecuteAt, OutError)
					|| !CompleteScheduledEvent(Due, OutError))
				{
					return false;
				}
			}
			return Clock.AdvanceTo(Target, OutError);
		}

		bool FUnifiedRuntime::CompleteScheduledEvent(const FScheduledEvent& Due, FString& OutError)
		{
			const FSimulationEventRecord* Stored = EventStore.Find(Due.EventID);
			if (Stored == nullptr
				|| Stored->State != ESimulationEventState::Pending
				|| Stored->Event.ArriveID != Due.ArriveID
				|| !(Stored->Event.EndTime == Due.ExecuteAt))
			{
				OutError = TEXT("Scheduled event does not match authoritative event state.");
				return false;
			}
			if (ImportBatches.Contains(Due.EventID))
			{
				return CompleteImport(*Stored, Due.ExecuteAt, OutError);
			}
			if (const FSimpleDelayedAction* Simple = SimpleActions.Find(Due.EventID))
			{
				const FSimpleDelayedAction Copy = *Simple;
				return CompleteSimpleAction(Copy, OutError);
			}
			return CompleteResidentAction(*Stored, Due.ExecuteAt, OutError);
		}

		bool FUnifiedRuntime::CompleteImport(
			const FSimulationEventRecord& Event,
			const FSimulationTime Time,
			FString& OutError)
		{
			const FUnifiedImportBatch* Found = ImportBatches.Find(Event.EventID);
			if (Found == nullptr)
			{
				OutError = TEXT("State import completion has no batch context.");
				return false;
			}
			const FUnifiedImportBatch Batch = *Found;
			const FReservationRecord* Reservation = Reservations.Find(Batch.ReservationID);
			if (Reservation == nullptr
				|| Reservation->State != EReservationState::Active
				|| Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit"))) + UE_DOUBLE_SMALL_NUMBER < Batch.WoodQuantity)
			{
				OutError = TEXT("State import cannot be completed atomically.");
				return false;
			}
			++Diagnostics.LedgerQueryCount;
			if (!Submit(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit")),
					MakeKingdomAccount(EKingdom::A, TEXT("MarketWoodAvailable")),
					Batch.WoodQuantity,
					false,
					FString::Printf(TEXT("UNIFIED-IMPORT-ARRIVE-%lld"), Event.EventID),
					Event.EventID,
					Event.Event.ArriveID,
					StateImportPolicyID,
					OutError)
				|| !Reservations.CommitReservation(
					Batch.ReservationID,
					ExternalBoundaryAccount,
					FString::Printf(TEXT("UNIFIED-IMPORT-PAY-%lld"), Event.EventID),
					Time,
					Ledger,
					OutError)
				|| !EventStore.CompleteEvent(Event.EventID, OutError))
			{
				return false;
			}
			ImportBatches.Remove(Event.EventID);
			return Options.bRetainCompletedEvents || EventStore.RemoveCompletedEvent(Event.EventID, OutError);
		}

		bool FUnifiedRuntime::CompleteResidentAction(
			const FSimulationEventRecord& Event,
			const FSimulationTime Time,
			FString& OutError)
		{
			FResidentCoreState* Resident = FindResident(Event.Event.ResidentID);
			if (Resident == nullptr
				|| Resident->ActiveEventID != Event.EventID
				|| Resident->ActiveArriveID != Event.Event.ArriveID)
			{
				OutError = TEXT("Resident action completion has stale or invalid ownership.");
				return false;
			}
			const EIndividualAction Action = static_cast<EIndividualAction>(Event.Event.ActionCode);
			if (Action == EIndividualAction::BuyWood || Action == EIndividualAction::ChopWood)
			{
				const FReservationRecord* Reservation = Reservations.Find(Event.Event.ReservationID);
				if (Reservation == nullptr || Reservation->State != EReservationState::Active)
				{
					OutError = TEXT("Resident action completion requires an active reservation.");
					return false;
				}
			}
			if (Action == EIndividualAction::ContinueRepair
				&& Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Resident->Kingdom, TEXT("WoodEmbeddedInRepairs"))) + UE_DOUBLE_SMALL_NUMBER < RepairWoodPerHome)
			{
				OutError = TEXT("Repair completion has insufficient embedded wood.");
				return false;
			}

			switch (Action)
			{
			case EIndividualAction::Work:
				if (!Submit(
					Time,
					ESimulationResource::Coin,
					ExternalBoundaryAccount,
					ResidentLedgerAccount(Resident->ResidentID, TEXT("Cash")),
					FIndividualDomain::GetWorkIncome(Resident->IncomeBand),
					true,
					FString::Printf(TEXT("UNIFIED-WORK-INCOME-%lld"), Event.EventID),
					Event.EventID,
					Event.Event.ArriveID,
					0,
					OutError))
				{
					return false;
				}
				break;

			case EIndividualAction::BuyWood:
			case EIndividualAction::ChopWood:
				if (!Reservations.CommitReservation(
					Event.Event.ReservationID,
					ResidentLedgerAccount(Resident->ResidentID, TEXT("Wood")),
					FString::Printf(TEXT("UNIFIED-WOOD-DELIVER-%lld"), Event.EventID),
					Time,
					Ledger,
					OutError))
				{
					return false;
				}
				break;

			case EIndividualAction::ContinueRepair:
				if (!Submit(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(Resident->Kingdom, TEXT("WoodEmbeddedInRepairs")),
					MakeKingdomAccount(Resident->Kingdom, TEXT("WoodInRepairedHomes")),
					RepairWoodPerHome,
					false,
					FString::Printf(TEXT("UNIFIED-REPAIR-COMPLETE-%lld"), Event.EventID),
					Event.EventID,
					Event.Event.ArriveID,
					0,
					OutError))
				{
					return false;
				}
				Resident->HomeState = EHomeState::Repaired;
				break;

			case EIndividualAction::Routine:
			case EIndividualAction::Wait:
				break;

			default:
				OutError = TEXT("Unsupported unified resident action completion.");
				return false;
			}

			if (!EventStore.CompleteEvent(Event.EventID, OutError))
			{
				return false;
			}
			SyncResident(*Resident);
			Resident->LastCompletedAction = Action;
			Resident->CurrentAction = EIndividualAction::None;
			Resident->ActiveEventID = 0;
			Resident->ActiveArriveID = 0;
			Resident->ActiveReservationID = 0;
			Resident->ActionEndTime = Time;
			Resident->CurrentGoal = FIndividualDomain::SelectGoal(*Resident);
			Resident->MacroIntent = Resident->CurrentGoal == EIndividualGoal::RoutineLife
				? EMacroIntent::Routine
				: EMacroIntent::Wait;
			Resident->LastUpdateTime = Time;
			++Resident->Version;
			return Options.bRetainCompletedEvents || EventStore.RemoveCompletedEvent(Event.EventID, OutError);
		}

		bool FUnifiedRuntime::PlanSimple(const FSimulationTime Time, FString& OutError)
		{
			for (const EKingdom Kingdom : { EKingdom::A, EKingdom::B })
			{
				const int32 Index = KingdomIndex(Kingdom);
				FSimpleKingdomState& State = SimpleStates[Index];
				EnsureRepairDay(Kingdom, Time);
				int32 FreeWaiting = FMath::Max(
					0,
					State.HomeStates[HomeStateIndex(EHomeState::DamagedWaiting)] - State.BusyWaitingCount);
				if (FreeWaiting <= 0)
				{
					continue;
				}

				auto ScheduleBatch = [this, Kingdom, Time, &OutError](
					const EIndividualAction Action,
					const int32 Participants,
					const int32 WoodQuantity,
					const int64 CoinQuantity,
					const FReservationID ReservationID,
					const FEventID ExistingEventID,
					const FArriveID ExistingArriveID,
					const int64 Duration) -> bool
				{
					FEventID EventID = ExistingEventID;
					const FArriveID ArriveID = ExistingArriveID > 0 ? ExistingArriveID : Scheduler.IssueArriveID();
					if (EventID == 0)
					{
						FSimulationEventRequest Event;
						Event.Type = Action == EIndividualAction::ContinueRepair ? TEXT("Repair") : ToString(Action);
						Event.Owner = Kingdom == EKingdom::A ? TEXT("Simple:A") : TEXT("Simple:B");
						Event.ActionCode = static_cast<int32>(Action);
						Event.WoodQuantity = WoodQuantity;
						Event.StartTime = Time;
						Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + Duration);
						Event.ReservationID = ReservationID;
						Event.ArriveID = ArriveID;
						Event.ParticipantCount = Participants;
						Event.Cause = TEXT("SimpleAverageState");
						if (!CreateEvent(Event, EventID, OutError))
						{
							return false;
						}
						++Diagnostics.EventCount;
					}

					FSimpleDelayedAction Delayed;
					Delayed.EventID = EventID;
					Delayed.ExecuteAt = FSimulationTime::FromMinutes(Time.Minutes + Duration);
					Delayed.ArriveID = ArriveID;
					Delayed.Kingdom = Kingdom;
					Delayed.Action = Action;
					Delayed.ParticipantCount = Participants;
					Delayed.WoodQuantity = WoodQuantity;
					Delayed.CoinQuantity = CoinQuantity;
					Delayed.ReservationID = ReservationID;
					Delayed.bCountsAsBusyWaiting = Action == EIndividualAction::BuyWood
						|| Action == EIndividualAction::ChopWood
						|| Action == EIndividualAction::Work;
					SimpleActions.Add(EventID, Delayed);
					return Scheduler.Schedule({ EventID, ArriveID, Delayed.ExecuteAt }, Time, OutError);
				};

				const int32 RepairCount = FMath::Min3(
					FreeWaiting,
					RepairRemaining[Index],
					static_cast<int32>(FMath::FloorToInt64(Ledger.GetBalance(
						ESimulationResource::Wood,
						SimpleAccount(Kingdom, TEXT("Wood"))) / RepairWoodPerHome)));
				++Diagnostics.LedgerQueryCount;
				if (RepairCount > 0)
				{
					const int32 Wood = RepairCount * static_cast<int32>(RepairWoodPerHome);
					const FArriveID ArriveID = Scheduler.IssueArriveID();
					if (!Submit(
						Time,
						ESimulationResource::Wood,
						SimpleAccount(Kingdom, TEXT("Wood")),
						MakeKingdomAccount(Kingdom, TEXT("WoodEmbeddedInRepairs")),
						Wood,
						false,
						FString::Printf(TEXT("UNIFIED-SIMPLE-REPAIR-START-%d-M%lld"), Index, Time.Minutes),
						0,
						ArriveID,
						0,
						OutError)
						|| !ScheduleBatch(EIndividualAction::ContinueRepair, RepairCount, Wood, 0, 0, 0, ArriveID, FIndividualDomain::GetActionDuration(EIndividualAction::ContinueRepair)))
					{
						return false;
					}
					State.HomeStates[HomeStateIndex(EHomeState::DamagedWaiting)] -= RepairCount;
					State.HomeStates[HomeStateIndex(EHomeState::UnderRepair)] += RepairCount;
					RepairRemaining[Index] -= RepairCount;
					FreeWaiting -= RepairCount;
				}

				if (FreeWaiting <= 0)
				{
					continue;
				}
				const double Cash = Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("Cash")));
				const double Credit = Ledger.GetBalance(ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("RepairCredit")));
				const double MarketWood = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")));
				Diagnostics.LedgerQueryCount += 3;
				const int64 CostPerHome = PaymentCoins(static_cast<int32>(RepairWoodPerHome), WoodPrices[Index]);
				int32 AggregatePopulation = 0;
				for (const int32 Count : State.HomeStates)
				{
					AggregatePopulation += Count;
				}
				const bool bMeanCanBuy = AggregatePopulation > 0 && (Cash + Credit) / AggregatePopulation >= CostPerHome;
				int32 BuyCount = bMeanCanBuy
					? FMath::Min3(
						FreeWaiting,
						static_cast<int32>(FMath::FloorToInt64(MarketWood / RepairWoodPerHome)),
						static_cast<int32>(FMath::FloorToInt64((Cash + Credit) / CostPerHome)))
					: 0;
				if (BuyCount > 0)
				{
					const FArriveID ArriveID = Scheduler.IssueArriveID();
					FSimulationEventRequest Event;
					Event.Type = ToString(EIndividualAction::BuyWood);
					Event.Owner = Kingdom == EKingdom::A ? TEXT("Simple:A") : TEXT("Simple:B");
					Event.ActionCode = static_cast<int32>(EIndividualAction::BuyWood);
					Event.WoodQuantity = BuyCount * static_cast<int32>(RepairWoodPerHome);
					Event.StartTime = Time;
					Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + FIndividualDomain::GetActionDuration(EIndividualAction::BuyWood));
					Event.ArriveID = ArriveID;
					Event.ParticipantCount = BuyCount;
					Event.Cause = TEXT("SimpleAverageState");
					FEventID EventID = 0;
					if (!CreateEvent(Event, EventID, OutError))
					{
						return false;
					}
					++Diagnostics.EventCount;

					FReservationRequest Reservation;
					Reservation.IdempotencyKey = FString::Printf(TEXT("UNIFIED-SIMPLE-BUY-RESERVE-%lld"), EventID);
					Reservation.GameTime = Time;
					Reservation.Resource = ESimulationResource::Wood;
					Reservation.SourceAccount = MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable"));
					Reservation.ReservedAccount = MakeKingdomAccount(Kingdom, TEXT("MarketWoodReserved"));
					Reservation.Quantity = Event.WoodQuantity;
					Reservation.EventID = EventID;
					Reservation.ArriveID = ArriveID;
					FReservationID ReservationID = 0;
					const int64 TotalCost = BuyCount * CostPerHome;
					const int64 CreditPayment = FMath::Min<int64>(FMath::FloorToInt64(Credit), TotalCost);
					const int64 CashPayment = TotalCost - CreditPayment;
					if (!Reservations.CreateReservation(Reservation, Ledger, ReservationID, OutError)
						|| !EventStore.SetReservationID(EventID, ReservationID, OutError)
						|| !Submit(Time, ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("RepairCredit")), MakeKingdomAccount(Kingdom, TEXT("MarketCoin")), CreditPayment, false, FString::Printf(TEXT("UNIFIED-SIMPLE-BUY-CREDIT-%lld"), EventID), EventID, ArriveID, 0, OutError)
						|| !Submit(Time, ESimulationResource::Coin, SimpleAccount(Kingdom, TEXT("Cash")), MakeKingdomAccount(Kingdom, TEXT("MarketCoin")), CashPayment, false, FString::Printf(TEXT("UNIFIED-SIMPLE-BUY-CASH-%lld"), EventID), EventID, ArriveID, 0, OutError)
						|| !ScheduleBatch(EIndividualAction::BuyWood, BuyCount, Event.WoodQuantity, 0, ReservationID, EventID, ArriveID, FIndividualDomain::GetActionDuration(EIndividualAction::BuyWood)))
					{
						return false;
					}
					State.BusyWaitingCount += BuyCount;
					FreeWaiting -= BuyCount;
				}

				if (FreeWaiting <= 0)
				{
					continue;
				}
				const double ForestWood = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom, TEXT("ForestWood")));
				++Diagnostics.LedgerQueryCount;
				int32 ChopCount = FMath::Min3(
					static_cast<int32>(FMath::FloorToInt(FreeWaiting * 0.20)),
					static_cast<int32>(FMath::FloorToInt64(ForestWood / RepairWoodPerHome)),
					static_cast<int32>(FMath::FloorToInt64(HarvestAllowance(Kingdom, Time) / RepairWoodPerHome)));
				if (ChopCount > 0)
				{
					const FArriveID ArriveID = Scheduler.IssueArriveID();
					FSimulationEventRequest Event;
					Event.Type = ToString(EIndividualAction::ChopWood);
					Event.Owner = Kingdom == EKingdom::A ? TEXT("Simple:A") : TEXT("Simple:B");
					Event.ActionCode = static_cast<int32>(EIndividualAction::ChopWood);
					Event.WoodQuantity = ChopCount * static_cast<int32>(RepairWoodPerHome);
					Event.StartTime = Time;
					Event.EndTime = FSimulationTime::FromMinutes(Time.Minutes + FIndividualDomain::GetActionDuration(EIndividualAction::ChopWood));
					Event.ArriveID = ArriveID;
					Event.ParticipantCount = ChopCount;
					Event.Cause = TEXT("SimpleAverageState");
					Event.PolicyID = IsHarvestCapActive(Kingdom, Time) ? HarvestCapPolicyID : 0;
					FEventID EventID = 0;
					if (!CreateEvent(Event, EventID, OutError))
					{
						return false;
					}
					++Diagnostics.EventCount;
					FReservationRequest Reservation;
					Reservation.IdempotencyKey = FString::Printf(TEXT("UNIFIED-SIMPLE-CHOP-RESERVE-%lld"), EventID);
					Reservation.GameTime = Time;
					Reservation.Resource = ESimulationResource::Wood;
					Reservation.SourceAccount = MakeKingdomAccount(Kingdom, TEXT("ForestWood"));
					Reservation.ReservedAccount = MakeKingdomAccount(Kingdom, TEXT("ForestWoodReserved"));
					Reservation.Quantity = Event.WoodQuantity;
					Reservation.EventID = EventID;
					Reservation.ArriveID = ArriveID;
					Reservation.PolicyID = Event.PolicyID;
					FReservationID ReservationID = 0;
					if (!Reservations.CreateReservation(Reservation, Ledger, ReservationID, OutError)
						|| !EventStore.SetReservationID(EventID, ReservationID, OutError)
						|| !ScheduleBatch(EIndividualAction::ChopWood, ChopCount, Event.WoodQuantity, 0, ReservationID, EventID, ArriveID, FIndividualDomain::GetActionDuration(EIndividualAction::ChopWood)))
					{
						return false;
					}
					if (IsHarvestCapActive(Kingdom, Time))
					{
						HarvestRemaining[Index] -= Event.WoodQuantity;
					}
					State.BusyWaitingCount += ChopCount;
					FreeWaiting -= ChopCount;
				}

				if (FreeWaiting > 0)
				{
					const int32 LowIncome = FMath::FloorToInt(FreeWaiting * 0.70);
					const int64 Income = static_cast<int64>(LowIncome) * FIndividualDomain::GetWorkIncome(EIncomeBand::Low)
						+ static_cast<int64>(FreeWaiting - LowIncome) * FIndividualDomain::GetWorkIncome(EIncomeBand::NonLow);
					if (!ScheduleBatch(EIndividualAction::Work, FreeWaiting, 0, Income, 0, 0, 0, FIndividualDomain::GetActionDuration(EIndividualAction::Work)))
					{
						return false;
					}
					State.BusyWaitingCount += FreeWaiting;
				}
			}
			return true;
		}

		bool FUnifiedRuntime::CompleteSimpleAction(const FSimpleDelayedAction& Action, FString& OutError)
		{
			const FSimulationEventRecord* Event = EventStore.Find(Action.EventID);
			if (Event == nullptr || Event->State != ESimulationEventState::Pending)
			{
				OutError = TEXT("Simple delayed action has no pending event.");
				return false;
			}
			FSimpleKingdomState& State = SimpleStates[KingdomIndex(Action.Kingdom)];
			switch (Action.Action)
			{
			case EIndividualAction::BuyWood:
			case EIndividualAction::ChopWood:
				if (!Reservations.CommitReservation(
					Action.ReservationID,
					SimpleAccount(Action.Kingdom, TEXT("Wood")),
					FString::Printf(TEXT("UNIFIED-SIMPLE-WOOD-DELIVER-%lld"), Action.EventID),
					Action.ExecuteAt,
					Ledger,
					OutError))
				{
					return false;
				}
				State.BusyWaitingCount -= Action.bCountsAsBusyWaiting ? Action.ParticipantCount : 0;
				break;

			case EIndividualAction::Work:
				if (!Submit(
					Action.ExecuteAt,
					ESimulationResource::Coin,
					ExternalBoundaryAccount,
					SimpleAccount(Action.Kingdom, TEXT("Cash")),
					Action.CoinQuantity,
					true,
					FString::Printf(TEXT("UNIFIED-SIMPLE-WORK-INCOME-%lld"), Action.EventID),
					Action.EventID,
					Action.ArriveID,
					0,
					OutError))
				{
					return false;
				}
				State.BusyWaitingCount -= Action.bCountsAsBusyWaiting ? Action.ParticipantCount : 0;
				break;

			case EIndividualAction::ContinueRepair:
				if (!Submit(
					Action.ExecuteAt,
					ESimulationResource::Wood,
					MakeKingdomAccount(Action.Kingdom, TEXT("WoodEmbeddedInRepairs")),
					MakeKingdomAccount(Action.Kingdom, TEXT("WoodInRepairedHomes")),
					Action.WoodQuantity,
					false,
					FString::Printf(TEXT("UNIFIED-SIMPLE-REPAIR-COMPLETE-%lld"), Action.EventID),
					Action.EventID,
					Action.ArriveID,
					0,
					OutError))
				{
					return false;
				}
				State.HomeStates[HomeStateIndex(EHomeState::UnderRepair)] -= Action.ParticipantCount;
				State.HomeStates[HomeStateIndex(EHomeState::Repaired)] += Action.ParticipantCount;
				break;

			case EIndividualAction::Routine:
			case EIndividualAction::Wait:
				State.BusyWaitingCount -= Action.bCountsAsBusyWaiting ? Action.ParticipantCount : 0;
				break;

			default:
				OutError = TEXT("Unsupported Simple delayed action.");
				return false;
			}

			if (State.BusyWaitingCount < 0 || !EventStore.CompleteEvent(Action.EventID, OutError))
			{
				OutError = State.BusyWaitingCount < 0 ? TEXT("Simple busy population became negative.") : OutError;
				return false;
			}
			SimpleActions.Remove(Action.EventID);
			return Options.bRetainCompletedEvents || EventStore.RemoveCompletedEvent(Action.EventID, OutError);
		}
	}

	class FUnifiedSimulationSession::FImpl
	{
	public:
		FImpl(
			const FPhase0Config& Config,
			const EUnifiedSimulationMethod Method,
			const EStage2Scenario Scenario,
			const FUnifiedRunOptions& Options)
			: Runtime(Config, Method, Scenario, Options)
		{
		}

		bool Initialize(FString& OutError)
		{
			if (State != EState::Created)
			{
				OutError = TEXT("Unified simulation session can only initialize once.");
				return false;
			}
			if (!Runtime.Initialize(OutError))
			{
				State = EState::Failed;
				return false;
			}
			State = EState::Running;
			OutError.Reset();
			return true;
		}

		bool StepHour(FString& OutError)
		{
			if (State != EState::Running)
			{
				OutError = TEXT("Unified simulation session is not ready to step.");
				return false;
			}
			if (!Runtime.StepHour(OutError))
			{
				State = EState::Failed;
				return false;
			}
			++CompletedHourSteps;
			if (Runtime.IsComplete())
			{
				State = EState::Complete;
			}
			OutError.Reset();
			return true;
		}

		bool Finalize(FUnifiedRunResult& OutResult, FString& OutError)
		{
			if (State != EState::Complete)
			{
				OutError = TEXT("Unified simulation session can only finalize after reaching D60T00:00.");
				return false;
			}
			if (!Runtime.Finalize(OutResult, OutError))
			{
				State = EState::Failed;
				return false;
			}
			State = EState::Finalized;
			OutError.Reset();
			return true;
		}

		bool IsComplete() const
		{
			return State == EState::Complete || State == EState::Finalized;
		}

		FSimulationTime GetCurrentTime() const
		{
			return Runtime.GetCurrentTime();
		}

		int32 GetCompletedHourSteps() const
		{
			return CompletedHourSteps;
		}

		const FUnifiedStepMeasurement& GetLastStepMeasurement() const
		{
			return Runtime.GetLastStepMeasurement();
		}

	private:
		enum class EState : uint8
		{
			Created,
			Running,
			Complete,
			Finalized,
			Failed
		};

		FUnifiedRuntime Runtime;
		EState State = EState::Created;
		int32 CompletedHourSteps = 0;
	};

	FUnifiedSimulationSession::FUnifiedSimulationSession(
		const FPhase0Config& Config,
		const EUnifiedSimulationMethod Method,
		const EStage2Scenario Scenario,
		const FUnifiedRunOptions& Options)
		: Impl(MakeUnique<FImpl>(Config, Method, Scenario, Options))
	{
	}

	FUnifiedSimulationSession::~FUnifiedSimulationSession() = default;

	bool FUnifiedSimulationSession::Initialize(FString& OutError)
	{
		return Impl->Initialize(OutError);
	}

	bool FUnifiedSimulationSession::StepHour(FString& OutError)
	{
		return Impl->StepHour(OutError);
	}

	bool FUnifiedSimulationSession::Finalize(FUnifiedRunResult& OutResult, FString& OutError)
	{
		return Impl->Finalize(OutResult, OutError);
	}

	bool FUnifiedSimulationSession::IsComplete() const
	{
		return Impl->IsComplete();
	}

	FSimulationTime FUnifiedSimulationSession::GetCurrentTime() const
	{
		return Impl->GetCurrentTime();
	}

	int32 FUnifiedSimulationSession::GetCompletedHourSteps() const
	{
		return Impl->GetCompletedHourSteps();
	}

	const FUnifiedStepMeasurement& FUnifiedSimulationSession::GetLastStepMeasurement() const
	{
		return Impl->GetLastStepMeasurement();
	}

	const TCHAR* ToString(const EUnifiedSimulationMethod Method)
	{
		switch (Method)
		{
		case EUnifiedSimulationMethod::Oracle: return TEXT("Oracle");
		case EUnifiedSimulationMethod::Proposed: return TEXT("Proposed");
		case EUnifiedSimulationMethod::PerAgent: return TEXT("PerAgent");
		case EUnifiedSimulationMethod::Simple: return TEXT("Simple");
		default: return TEXT("Unknown");
		}
	}

	bool FUnifiedRunResult::IsHardErrorFree() const
	{
		return Audit.IsHardErrorFree()
			&& FMath::IsNearlyZero(CoinResidual, 1.e-6)
			&& CoreLedgerMismatchCount == 0
			&& EventReferenceErrorCount == 0
			&& ActiveCapViolationCount == 0
			&& ReservationErrorCount == 0
			&& TaskResetCount == 0
			&& PendingEventsAtOrBeforeEnd == 0
			&& Diagnostics.RejectedActionResidueCount == 0
			&& Diagnostics.FirstActionCount == ActivationObservations.Num()
			&& (Method != EUnifiedSimulationMethod::Simple || SimpleIndividualCoreStateCount == 0);
	}

	int32 FUnifiedRunResult::GetHomeStateCount(const EKingdom Kingdom, const EHomeState HomeState) const
	{
		const int32 Index = static_cast<int32>(HomeState);
		return Index >= 0 && Index < 4
			? (Kingdom == EKingdom::A ? KingdomAHomeStates[Index] : KingdomBHomeStates[Index])
			: 0;
	}

	bool FUnifiedSimulationRunner::Run(
		const FPhase0Config& Config,
		const EUnifiedSimulationMethod Method,
		const EStage2Scenario Scenario,
		const FUnifiedRunOptions& Options,
		FUnifiedRunResult& OutResult,
		FString& OutError)
	{
		FUnifiedSimulationSession Session(Config, Method, Scenario, Options);
		if (!Session.Initialize(OutError))
		{
			return false;
		}
		while (!Session.IsComplete())
		{
			if (!Session.StepHour(OutError))
			{
				return false;
			}
		}
		return Session.Finalize(OutResult, OutError);
	}

	FString FUnifiedSimulationRunner::BuildDeterministicDigest(const FUnifiedRunResult& Result)
	{
		FString Canonical = FString::Printf(
			TEXT("Method=%s|Seed=%d|Config=%s|Scenario=%s|Final=%lld|A=%.9f,%.9f,%.9f,%d,%d,%d,%d|B=%.9f,%.9f,%.9f,%d,%d,%d,%d|"),
			ToString(Result.Method),
			Result.Seed,
			*Result.ConfigHash,
			ToString(Result.Scenario),
			Result.FinalTime.Minutes,
			Result.KingdomAStocks.ForestWood,
			Result.KingdomAStocks.MarketWoodAvailable,
			Result.KingdomAStocks.WoodInRepairedHomes,
			Result.KingdomAHomeStates[0],
			Result.KingdomAHomeStates[1],
			Result.KingdomAHomeStates[2],
			Result.KingdomAHomeStates[3],
			Result.KingdomBStocks.ForestWood,
			Result.KingdomBStocks.MarketWoodAvailable,
			Result.KingdomBStocks.WoodInRepairedHomes,
			Result.KingdomBHomeStates[0],
			Result.KingdomBHomeStates[1],
			Result.KingdomBHomeStates[2],
			Result.KingdomBHomeStates[3]);
		for (const FResidentCoreState& Resident : Result.Residents)
		{
			Canonical += FString::Printf(
				TEXT("R=%lld,%d,%d,%d,%d,%d,%d,%lld,%lld|"),
				Resident.ResidentID,
				static_cast<int32>(Resident.Kingdom),
				Resident.Cash,
				Resident.RepairCredit,
				Resident.InventoryWood,
				static_cast<int32>(Resident.HomeState),
				static_cast<int32>(Resident.CurrentAction),
				Resident.ActiveEventID,
				Resident.ActiveArriveID);
		}
		for (const FUnifiedActivationObservation& Observation : Result.ActivationObservations)
		{
			Canonical += FString::Printf(
				TEXT("A=%lld,%lld,%d,%d,%d|"),
				Observation.ResidentID,
				Observation.ActivationTime.Minutes,
				static_cast<int32>(Observation.FirstAction),
				Observation.bSimpleReconstructed ? 1 : 0,
				Observation.bContinuedCommittedEvent ? 1 : 0);
		}
		for (const FLedgerTransaction& Transaction : Result.Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			Canonical += FString::Printf(
				TEXT("T=%lld,%lld,%s,%d,%s,%s,%.9f,%d,%lld,%lld,%lld|"),
				Transaction.TransactionID,
				Transfer.GameTime.Minutes,
				*Transfer.IdempotencyKey,
				static_cast<int32>(Transfer.Resource),
				*Transfer.Source,
				*Transfer.Destination,
				Transfer.Quantity,
				Transfer.bBoundaryFlow ? 1 : 0,
				Transfer.EventID,
				Transfer.ArriveID,
				Transfer.PolicyID);
		}
		FTCHARToUTF8 Utf8(*Canonical);
		return FSHA1::HashBuffer(Utf8.Get(), static_cast<uint64>(Utf8.Length())).ToString();
	}
}
