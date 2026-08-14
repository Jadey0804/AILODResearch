// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODStructuredMacro.h"

#include "AILODDomainRules.h"
#include "AILODPhase0Types.h"
#include "Containers/StringConv.h"
#include "Misc/SecureHash.h"

namespace AILOD
{
	namespace
	{
		using namespace DomainRules;

		const TCHAR* ToString(const ESimulationResource Resource)
		{
			return Resource == ESimulationResource::Wood ? TEXT("Wood") : TEXT("Coin");
		}

		int32 CohortOrder(const FCohortKey& Key)
		{
			int32 Value = static_cast<int32>(Key.Kingdom);
			Value = Value * 2 + static_cast<int32>(Key.Profession);
			Value = Value * 2 + static_cast<int32>(Key.IncomeBand);
			Value = Value * 4 + static_cast<int32>(Key.HomeState);
			return Value * 6 + static_cast<int32>(Key.MacroIntent);
		}

		struct FImportBatch
		{
			FEventID EventID = 0;
			FReservationID ReservationID = 0;
			FArriveID ArriveID = 0;
			double WoodQuantity = 0.0;
			int64 CoinCost = 0;
		};

		struct FAidRequest
		{
			FCohortKey Key;
			int32 EligibleCount = 0;
			FArriveID ArriveID = 0;
		};

		class FStage2Simulation
		{
		public:
			FStage2Simulation(const FPhase0Config& InConfig, const EStage2Scenario InScenario)
				: Config(InConfig)
				, Scenario(InScenario)
				, Clock(FSimulationTime::FromDays(-7))
			{
				KingdomA.Kingdom = EKingdom::A;
				KingdomB.Kingdom = EKingdom::B;
			}

			bool Run(FStage2RunResult& OutResult, FString& OutError)
			{
				if (!Initialize(OutError))
				{
					return false;
				}

				const int32 TotalHourSteps = 67 * static_cast<int32>(HoursPerDay);
				for (int32 Step = 0; Step < TotalHourSteps; ++Step)
				{
					const FSimulationTime StepStart = Clock.Now();
					if (!ProcessHour(StepStart, OutError))
					{
						return false;
					}

					const FSimulationTime StepEnd = FSimulationTime::FromMinutes(StepStart.Minutes + MinutesPerHour);
					if (!Clock.AdvanceTo(StepEnd, OutError))
					{
						return false;
					}
					UpdateCohortTimes(StepEnd);
					if (StepEnd.Minutes > 0)
					{
						Snapshots.Add(BuildSnapshot(KingdomA, StepEnd));
						Snapshots.Add(BuildSnapshot(KingdomB, StepEnd));
					}
				}

				if (Scheduler.NumPending() != 0)
				{
					OutError = TEXT("Stage 2 ended with scheduled events still pending.");
					return false;
				}

				RefreshStocks(KingdomA);
				RefreshStocks(KingdomB);
				const FPopulationState Population = BuildPopulationState();
				const FConservationAudit FinalAudit = AuditConservation(Population, Ledger, EventStore);
				if (!FinalAudit.IsHardErrorFree())
				{
					OutError = TEXT("Stage 2 final conservation audit failed.");
					return false;
				}

				OutResult = {};
				OutResult.Seed = Config.Seed;
				OutResult.ConfigHash = PopulationManifest.ConfigHash;
				OutResult.Scenario = Scenario;
				OutResult.FinalTime = Clock.Now();
				OutResult.WarmupHourSteps = 7 * static_cast<int32>(HoursPerDay);
				OutResult.FormalHourSteps = 60 * static_cast<int32>(HoursPerDay);
				OutResult.KingdomA = KingdomA;
				OutResult.KingdomB = KingdomB;
				OutResult.Audit = FinalAudit;
				OutResult.InitialBalances = InitialBalances;
				OutResult.FinalBalances = Ledger.GetBalances();
				OutResult.Transactions = Ledger.GetTransactions();
				OutResult.Snapshots = MoveTemp(Snapshots);

				for (const TPair<FEventID, FSimulationEventRecord>& Pair : EventStore.GetEvents())
				{
					OutResult.Events.Add(Pair.Value);
				}
				OutResult.Events.Sort([](const FSimulationEventRecord& Left, const FSimulationEventRecord& Right)
				{
					return Left.EventID < Right.EventID;
				});

				OutError.Reset();
				return true;
			}

		private:
			bool Initialize(FString& OutError)
			{
				if (!FPhase0ManifestGenerator::Generate(Config, PopulationManifest, DamageList, PersistentPool, OutError))
				{
					return false;
				}

				KingdomA.Population = Config.PopulationPerKingdom;
				KingdomB.Population = Config.PopulationPerKingdom;
				BuildInitialCohorts(FSimulationTime::FromDays(-7));

				for (const FEarthquakeDamageRecord& Damage : DamageList.DamagedResidents)
				{
					DamagedResidentIDs.Add(Damage.ResidentID);
				}

				if (!InitializeKingdomLedger(EKingdom::A, OutError)
					|| !InitializeKingdomLedger(EKingdom::B, OutError))
				{
					return false;
				}
				Ledger.SealInitialState();
				InitialBalances = Ledger.GetBalances();
				RefreshStocks(KingdomA);
				RefreshStocks(KingdomB);
				ImportBudgetRemainingCoin = Config.PopulationPerKingdom;
				return true;
			}

			bool InitializeKingdomLedger(const EKingdom Kingdom, FString& OutError)
			{
				const double N = Config.PopulationPerKingdom;
				const struct
				{
					ESimulationResource Resource;
					const TCHAR* StockName;
					double Quantity;
				} Accounts[] =
				{
					{ ESimulationResource::Wood, TEXT("ForestWood"), 16.0 * N },
					{ ESimulationResource::Wood, TEXT("MarketWoodAvailable"), 2.0 * N },
					{ ESimulationResource::Wood, TEXT("MarketWoodReserved"), 0.0 },
					{ ESimulationResource::Wood, TEXT("WoodInTransit"), 0.0 },
					{ ESimulationResource::Wood, TEXT("ResidentInventoryWood"), 0.0 },
					{ ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs"), 0.0 },
					{ ESimulationResource::Wood, TEXT("WoodInRepairedHomes"), 0.0 },
					{ ESimulationResource::Coin, TEXT("TreasuryAvailable"), 5.0 * N },
					{ ESimulationResource::Coin, TEXT("TreasuryReserved"), 0.0 },
					{ ESimulationResource::Coin, TEXT("MarketCoin"), 0.0 },
					{ ESimulationResource::Coin, TEXT("ResidentRepairCredit"), 0.0 }
				};

				for (const auto& Account : Accounts)
				{
					if (!Ledger.InitializeAccount(Account.Resource, MakeKingdomAccount(Kingdom, Account.StockName), Account.Quantity, OutError))
					{
						return false;
					}
				}
				return true;
			}

			void BuildInitialCohorts(const FSimulationTime Time)
			{
				KingdomA.Cohorts.Reset();
				KingdomB.Cohorts.Reset();
				for (const FInitialResidentRecord& Resident : PopulationManifest.Residents)
				{
					AddResidentToCohort(Resident, EHomeState::Healthy, Time);
				}
			}

			void AddResidentToCohort(
				const FInitialResidentRecord& Resident,
				const EHomeState HomeState,
				const FSimulationTime Time)
			{
				FKingdomState& Kingdom = Resident.Kingdom == EKingdom::A ? KingdomA : KingdomB;
				const FCohortKey Key{
					Resident.Kingdom,
					Resident.Profession,
					Resident.IncomeBand,
					HomeState,
					EMacroIntent::Routine
				};
				FCohortBucket& Bucket = Kingdom.Cohorts.FindOrAdd(Key);
				++Bucket.PopulationCount;
				Bucket.CashSum += Resident.Cash;
				Bucket.CashSquaredSum += static_cast<int64>(Resident.Cash) * Resident.Cash;
				Bucket.RepairCreditSum += Resident.RepairCredit;
				const int32 WoodBin = FMath::Clamp(Resident.InventoryWood, 0, 4);
				++Bucket.WoodCounts[WoodBin];
				Bucket.LastUpdateTime = Time;
				Bucket.RNGStreamKey = HashCombine(::GetTypeHash(Config.Seed), GetTypeHash(Key));
			}

			bool ProcessHour(const FSimulationTime Time, FString& OutError)
			{
				if (!ProcessDueEvents(Time, OutError))
				{
					return false;
				}

				if (Time.Minutes == 0 && !ApplyEarthquake(Time, OutError))
				{
					return false;
				}

				if (Time.Minutes >= 0 && Time.Minutes % MinutesPerDay == 0
					&& !ProcessDailyPolicy(Time, OutError))
				{
					return false;
				}

				if (!ApplyForestGrowth(KingdomA, Time, OutError)
					|| !ApplyForestGrowth(KingdomB, Time, OutError)
					|| !ApplyBaselineImport(KingdomA, Time, OutError)
					|| !ApplyBaselineImport(KingdomB, Time, OutError)
					|| !ApplyCommercialHarvest(KingdomA, Time, OutError)
					|| !ApplyCommercialHarvest(KingdomB, Time, OutError)
					|| !ApplyRoutineConsumption(KingdomA, Time, OutError)
					|| !ApplyRoutineConsumption(KingdomB, Time, OutError))
				{
					return false;
				}

				UpdateWoodPrice(KingdomA);
				UpdateWoodPrice(KingdomB);
				RefreshStocks(KingdomA);
				RefreshStocks(KingdomB);

				const FConservationAudit HourAudit = AuditConservation(BuildPopulationState(), Ledger, EventStore);
				if (!HourAudit.IsHardErrorFree())
				{
					OutError = FString::Printf(TEXT("Conservation audit failed at %s."), *Time.ToString());
					return false;
				}
				return true;
			}

			bool ProcessDueEvents(const FSimulationTime Time, FString& OutError)
			{
				TArray<FScheduledEvent> DueEvents;
				Scheduler.PopDueThrough(Time, DueEvents);
				for (const FScheduledEvent& Due : DueEvents)
				{
					FImportBatch* Batch = ImportBatches.Find(Due.EventID);
					if (Batch == nullptr)
					{
						OutError = TEXT("Scheduled Stage 2 event has no import batch.");
						return false;
					}

					if (!SubmitFlow(
						Time,
						ESimulationResource::Wood,
						MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit")),
						MakeKingdomAccount(EKingdom::A, TEXT("MarketWoodAvailable")),
						Batch->WoodQuantity,
						false,
						FString::Printf(TEXT("STATE-IMPORT-ARRIVE-%lld"), Batch->EventID),
						Batch->EventID,
						Batch->ArriveID,
						StateImportPolicyID,
						OutError))
					{
						return false;
					}

					if (!Reservations.CommitReservation(
						Batch->ReservationID,
						ExternalBoundaryAccount,
						FString::Printf(TEXT("STATE-IMPORT-PAY-%lld"), Batch->EventID),
						Time,
						Ledger,
						OutError)
						|| !EventStore.CompleteEvent(Batch->EventID, OutError))
					{
						return false;
					}
					ImportBatches.Remove(Batch->EventID);
				}
				return true;
			}

			bool ApplyEarthquake(const FSimulationTime Time, FString& OutError)
			{
				KingdomA.Cohorts.Reset();
				KingdomB.Cohorts.Reset();
				for (const FInitialResidentRecord& Resident : PopulationManifest.Residents)
				{
					const EHomeState HomeState = DamagedResidentIDs.Contains(Resident.ResidentID)
						? EHomeState::DamagedWaiting
						: EHomeState::Healthy;
					AddResidentToCohort(Resident, HomeState, Time);
				}

				FEventID EventID = 0;
				if (!CreateInstantEvent(TEXT("EarthquakeDamage"), Time, DamageList.DamagedResidents.Num(), 0, EventID, OutError))
				{
					return false;
				}
				for (TPair<FCohortKey, FCohortBucket>& Pair : KingdomA.Cohorts)
				{
					if (Pair.Key.HomeState == EHomeState::DamagedWaiting)
					{
						Pair.Value.EventBatchRefs.Add(EventID);
					}
				}
				return true;
			}

			bool ProcessDailyPolicy(const FSimulationTime Time, FString& OutError)
			{
				const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
				switch (Scenario)
				{
				case EStage2Scenario::HarvestCap:
					if (Day == 2 && !CreatePolicyMarker(TEXT("HarvestCapAnnounced"), Time, HarvestCapPolicyID, OutError))
					{
						return false;
					}
					if (Day == 3 && !CreatePolicyMarker(TEXT("HarvestCapActive"), Time, HarvestCapPolicyID, OutError))
					{
						return false;
					}
					if (Day == 30 && !CreatePolicyMarker(TEXT("HarvestCapEnded"), Time, HarvestCapPolicyID, OutError))
					{
						return false;
					}
					break;

				case EStage2Scenario::StateImport:
					if (Day == 2 && !CreatePolicyMarker(TEXT("StateImportAnnounced"), Time, StateImportPolicyID, OutError))
					{
						return false;
					}
					if (Day >= 2 && Day <= 14 && !PlaceStateImportOrder(Time, OutError))
					{
						return false;
					}
					break;

				case EStage2Scenario::RepairAid:
					if (Day == 2 && !CalculateAidEligibility(Time, OutError))
					{
						return false;
					}
					if (Day == 3 && !PayRepairAid(Time, OutError))
					{
						return false;
					}
					break;

				case EStage2Scenario::None:
				default:
					break;
				}
				return true;
			}

			bool PlaceStateImportOrder(const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(KingdomA);
				const double WaitingHomes = GetHomeStateCount(KingdomA, EHomeState::DamagedWaiting);
				const double ExpectedRepairWoodUse = FMath::Min(
					WaitingHomes,
					RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom * 3.0) * RepairWoodPerHome;
				const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
				const double StockGapPerDay = FMath::Max(
					0.0,
					(TargetMarketWood - KingdomA.Stocks.MarketWoodAvailable - KingdomA.Stocks.WoodInTransit) / 3.0);
				const double DesiredImport = ExpectedRepairWoodUse + StockGapPerDay;
				const double DailyCap = StateImportDailyCapPerPerson * Config.PopulationPerKingdom;
				const double AffordableByPolicy = ImportBudgetRemainingCoin / StateImportPrice;
				const double AffordableByTreasury = KingdomA.Stocks.TreasuryAvailable / StateImportPrice;
				double OrderQuantity = FMath::Min(
					FMath::Min(DesiredImport, DailyCap),
					FMath::Min(AffordableByPolicy, AffordableByTreasury));
				if (OrderQuantity <= UE_DOUBLE_SMALL_NUMBER)
				{
					return true;
				}

				int64 CoinCost = FMath::CeilToInt64(OrderQuantity * StateImportPrice);
				if (CoinCost > ImportBudgetRemainingCoin || CoinCost > KingdomA.Stocks.TreasuryAvailable)
				{
					const int64 CoinLimit = FMath::Min(ImportBudgetRemainingCoin, KingdomA.Stocks.TreasuryAvailable);
					OrderQuantity = CoinLimit / StateImportPrice;
					CoinCost = FMath::CeilToInt64(OrderQuantity * StateImportPrice);
				}

				const FArriveID ArriveID = Scheduler.IssueArriveID();
				FSimulationEventRequest EventRequest;
				EventRequest.Type = TEXT("StateImport");
				EventRequest.Owner = TEXT("Macro:A");
				EventRequest.StartTime = Time;
				EventRequest.EndTime = FSimulationTime::FromMinutes(Time.Minutes + 3 * MinutesPerDay);
				EventRequest.ArriveID = ArriveID;
				EventRequest.ParticipantCount = 1;
				EventRequest.Cause = TEXT("StateImportPolicy");
				EventRequest.PolicyID = StateImportPolicyID;
				FEventID EventID = 0;
				if (!EventStore.CreateEvent(EventRequest, EventID, OutError))
				{
					return false;
				}

				FReservationRequest ReservationRequest;
				ReservationRequest.IdempotencyKey = FString::Printf(TEXT("STATE-IMPORT-RESERVE-%lld"), EventID);
				ReservationRequest.GameTime = Time;
				ReservationRequest.Resource = ESimulationResource::Coin;
				ReservationRequest.SourceAccount = MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable"));
				ReservationRequest.ReservedAccount = MakeKingdomAccount(EKingdom::A, TEXT("TreasuryReserved"));
				ReservationRequest.Quantity = CoinCost;
				ReservationRequest.EventID = EventID;
				ReservationRequest.ArriveID = ArriveID;
				ReservationRequest.PolicyID = StateImportPolicyID;
				FReservationID ReservationID = 0;
				if (!Reservations.CreateReservation(ReservationRequest, Ledger, ReservationID, OutError)
					|| !EventStore.SetReservationID(EventID, ReservationID, OutError)
					|| !SubmitFlow(
						Time,
						ESimulationResource::Wood,
						ExternalBoundaryAccount,
						MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit")),
						OrderQuantity,
						true,
						FString::Printf(TEXT("STATE-IMPORT-WOOD-%lld"), EventID),
						EventID,
						ArriveID,
						StateImportPolicyID,
						OutError)
					|| !Scheduler.Schedule({ EventID, ArriveID, EventRequest.EndTime }, Time, OutError))
				{
					return false;
				}

				ImportBatches.Add(EventID, { EventID, ReservationID, ArriveID, OrderQuantity, CoinCost });
				ImportBudgetRemainingCoin -= CoinCost;
				KingdomA.AdditionalImportedWood += OrderQuantity;
				return true;
			}

			bool CalculateAidEligibility(const FSimulationTime Time, FString& OutError)
			{
				AidRequests.Reset();
				const int64 RequiredCoins = FMath::CeilToInt64(RepairWoodPerHome * KingdomA.Stocks.WoodPrice);
				TMap<FCohortKey, int32> EligibleByCohort;
				for (const FInitialResidentRecord& Resident : PopulationManifest.Residents)
				{
					if (Resident.Kingdom != EKingdom::A
						|| Resident.IncomeBand != EIncomeBand::Low
						|| !DamagedResidentIDs.Contains(Resident.ResidentID)
						|| Resident.Cash + Resident.RepairCredit >= RequiredCoins)
					{
						continue;
					}
					const FCohortKey Key{
						EKingdom::A,
						Resident.Profession,
						Resident.IncomeBand,
						EHomeState::DamagedWaiting,
						EMacroIntent::Routine
					};
					++EligibleByCohort.FindOrAdd(Key);
				}

				TArray<FCohortKey> Keys;
				EligibleByCohort.GetKeys(Keys);
				Keys.Sort([](const FCohortKey& Left, const FCohortKey& Right)
				{
					return CohortOrder(Left) < CohortOrder(Right);
				});

				int32 TotalEligible = 0;
				for (const FCohortKey& Key : Keys)
				{
					const int32 EligibleCount = EligibleByCohort[Key];
					FCohortBucket* Bucket = KingdomA.Cohorts.Find(Key);
					if (Bucket == nullptr)
					{
						OutError = TEXT("Repair Aid eligibility references a missing Cohort bucket.");
						return false;
					}
					Bucket->AidEligibleCount = EligibleCount;
					AidRequests.Add({ Key, EligibleCount, Scheduler.IssueArriveID() });
					TotalEligible += EligibleCount;
				}

				FEventID EventID = 0;
				if (!CreateInstantEvent(TEXT("RepairAidEligibility"), Time, TotalEligible, RepairAidPolicyID, EventID, OutError))
				{
					return false;
				}
				for (FAidRequest& Request : AidRequests)
				{
					KingdomA.Cohorts[Request.Key].EventBatchRefs.Add(EventID);
				}
				return true;
			}

			bool PayRepairAid(const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(KingdomA);
				int32 TotalEligible = 0;
				for (const FAidRequest& Request : AidRequests)
				{
					TotalEligible += Request.EligibleCount;
				}
				const int64 PolicyBudget = FMath::FloorToInt64(0.40 * Config.PopulationPerKingdom);
				const int64 SpendableCoin = FMath::Min(PolicyBudget, KingdomA.Stocks.TreasuryAvailable);
				int32 RemainingPaidCount = FMath::Min(TotalEligible, static_cast<int32>(SpendableCoin / static_cast<int64>(RepairAidPerHome)));

				FEventID EventID = 0;
				if (!CreateInstantEvent(TEXT("RepairAidPayment"), Time, RemainingPaidCount, RepairAidPolicyID, EventID, OutError))
				{
					return false;
				}

				AidRequests.Sort([](const FAidRequest& Left, const FAidRequest& Right)
				{
					return Left.ArriveID < Right.ArriveID;
				});
				for (const FAidRequest& Request : AidRequests)
				{
					const int32 PaidCount = FMath::Min(Request.EligibleCount, RemainingPaidCount);
					if (PaidCount <= 0)
					{
						break;
					}
					const int64 CoinQuantity = static_cast<int64>(PaidCount * RepairAidPerHome);
					if (!SubmitFlow(
						Time,
						ESimulationResource::Coin,
						MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable")),
						MakeKingdomAccount(EKingdom::A, TEXT("ResidentRepairCredit")),
						CoinQuantity,
						false,
						FString::Printf(TEXT("REPAIR-AID-%d-%d"), CohortOrder(Request.Key), PaidCount),
						EventID,
						Request.ArriveID,
						RepairAidPolicyID,
						OutError))
					{
						return false;
					}

					FCohortBucket& Bucket = KingdomA.Cohorts[Request.Key];
					Bucket.AidReceivedCount += PaidCount;
					Bucket.RepairCreditSum += CoinQuantity;
					Bucket.EventBatchRefs.AddUnique(EventID);
					KingdomA.AidPaidCount += PaidCount;
					RemainingPaidCount -= PaidCount;
				}
				return true;
			}

			bool ApplyForestGrowth(FKingdomState& Kingdom, const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(Kingdom);
				const double F = Kingdom.Stocks.ForestWood;
				const double K = Kingdom.Stocks.ForestCapacity;
				const double Growth = FMath::Max(0.0, ForestGrowthRatePerDay * F * (1.0 - F / K) / HoursPerGameDay);
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(Kingdom.Kingdom, TEXT("ForestWood")),
					Growth,
					true,
					FlowKey(Time, Kingdom.Kingdom, TEXT("FOREST-GROWTH")),
					0,
					Scheduler.IssueArriveID(),
					0,
					OutError);
			}

			bool ApplyBaselineImport(FKingdomState& Kingdom, const FSimulationTime Time, FString& OutError)
			{
				const double Quantity = BaselineImportPerPersonPerDay * Kingdom.Population / HoursPerGameDay;
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(Kingdom.Kingdom, TEXT("MarketWoodAvailable")),
					Quantity,
					true,
					FlowKey(Time, Kingdom.Kingdom, TEXT("BASELINE-IMPORT")),
					0,
					Scheduler.IssueArriveID(),
					0,
					OutError);
			}

			bool ApplyCommercialHarvest(FKingdomState& Kingdom, const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(Kingdom);
				const double Desired = BaselineHarvestPerPersonPerDay * Kingdom.Population / HoursPerGameDay;
				double Allowance = Desired;
				FPolicyID PolicyID = 0;
				if (Kingdom.Kingdom == EKingdom::A
					&& Scenario == EStage2Scenario::HarvestCap
					&& Time.Minutes >= 3 * MinutesPerDay
					&& Time.Minutes < 30 * MinutesPerDay)
				{
					const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
					if (HarvestAllowanceDay != Day)
					{
						HarvestAllowanceDay = Day;
						HarvestAllowanceRemaining = HarvestCapPerPersonPerDay * Kingdom.Population;
					}
					Allowance = HarvestAllowanceRemaining;
					PolicyID = HarvestCapPolicyID;
				}

				const double Quantity = FMath::Min3(Desired, Allowance, Kingdom.Stocks.ForestWood);
				if (Quantity <= UE_DOUBLE_SMALL_NUMBER)
				{
					return true;
				}
				if (!SubmitFlow(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom.Kingdom, TEXT("ForestWood")),
					MakeKingdomAccount(Kingdom.Kingdom, TEXT("MarketWoodAvailable")),
					Quantity,
					false,
					FlowKey(Time, Kingdom.Kingdom, TEXT("COMMERCIAL-HARVEST")),
					0,
					Scheduler.IssueArriveID(),
					PolicyID,
					OutError))
				{
					return false;
				}
				if (PolicyID == HarvestCapPolicyID)
				{
					HarvestAllowanceRemaining = FMath::Max(0.0, HarvestAllowanceRemaining - Quantity);
				}
				Kingdom.CommercialHarvestedWood += Quantity;
				return true;
			}

			bool ApplyRoutineConsumption(FKingdomState& Kingdom, const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(Kingdom);
				const double Desired = RoutineConsumptionPerPersonPerDay * Kingdom.Population / HoursPerGameDay;
				const double Actual = FMath::Min(Desired, Kingdom.Stocks.MarketWoodAvailable);
				Kingdom.UnmetRoutineConsumption += Desired - Actual;
				if (Actual <= UE_DOUBLE_SMALL_NUMBER)
				{
					return true;
				}
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom.Kingdom, TEXT("MarketWoodAvailable")),
					ExternalBoundaryAccount,
					Actual,
					true,
					FlowKey(Time, Kingdom.Kingdom, TEXT("ROUTINE-CONSUMPTION")),
					0,
					Scheduler.IssueArriveID(),
					0,
					OutError);
			}

			void UpdateWoodPrice(FKingdomState& Kingdom)
			{
				RefreshStocks(Kingdom);
				const double TargetMarketWood = 2.0 * Kingdom.Population;
				const double PriceTarget = FMath::Clamp(
					FMath::Sqrt(TargetMarketWood / FMath::Max(Kingdom.Stocks.MarketWoodAvailable, UE_DOUBLE_SMALL_NUMBER)),
					0.5,
					3.0);
				Kingdom.Stocks.WoodPrice += (PriceTarget - Kingdom.Stocks.WoodPrice) / HoursPerGameDay;
			}

			bool SubmitFlow(
				const FSimulationTime Time,
				const ESimulationResource Resource,
				const FString& Source,
				const FString& Destination,
				const double Quantity,
				const bool bBoundaryFlow,
				const FString& IdempotencyKey,
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
				Request.IdempotencyKey = IdempotencyKey;
				Request.GameTime = Time;
				Request.Resource = Resource;
				Request.Source = Source;
				Request.Destination = Destination;
				Request.Quantity = Quantity;
				Request.bBoundaryFlow = bBoundaryFlow;
				Request.EventID = EventID;
				Request.ArriveID = ArriveID;
				Request.PolicyID = PolicyID;
				FTransactionID TransactionID = 0;
				return Ledger.SubmitTransfer(Request, TransactionID, OutError);
			}

			bool CreateInstantEvent(
				const TCHAR* Type,
				const FSimulationTime Time,
				const int32 Participants,
				const FPolicyID PolicyID,
				FEventID& OutEventID,
				FString& OutError)
			{
				FSimulationEventRequest Request;
				Request.Type = Type;
				Request.Owner = TEXT("Macro:A");
				Request.StartTime = Time;
				Request.EndTime = Time;
				Request.ArriveID = Scheduler.IssueArriveID();
				Request.ParticipantCount = Participants;
				Request.Cause = Type;
				Request.PolicyID = PolicyID;
				return EventStore.CreateEvent(Request, OutEventID, OutError)
					&& EventStore.CompleteEvent(OutEventID, OutError);
			}

			bool CreatePolicyMarker(
				const TCHAR* Type,
				const FSimulationTime Time,
				const FPolicyID PolicyID,
				FString& OutError)
			{
				FEventID EventID = 0;
				return CreateInstantEvent(Type, Time, 0, PolicyID, EventID, OutError);
			}

			void RefreshStocks(FKingdomState& Kingdom)
			{
				Kingdom.Stocks.ForestCapacity = 20.0 * Kingdom.Population;
				Kingdom.Stocks.ForestWood = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("ForestWood")));
				Kingdom.Stocks.MarketWoodAvailable = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("MarketWoodAvailable")));
				Kingdom.Stocks.MarketWoodReserved = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("MarketWoodReserved")));
				Kingdom.Stocks.WoodInTransit = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("WoodInTransit")));
				Kingdom.Stocks.ResidentInventoryWood = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("ResidentInventoryWood")));
				Kingdom.Stocks.WoodEmbeddedInRepairs = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("WoodEmbeddedInRepairs")));
				Kingdom.Stocks.WoodInRepairedHomes = Ledger.GetBalance(ESimulationResource::Wood, MakeKingdomAccount(Kingdom.Kingdom, TEXT("WoodInRepairedHomes")));
				Kingdom.Stocks.TreasuryAvailable = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom.Kingdom, TEXT("TreasuryAvailable"))));
				Kingdom.Stocks.TreasuryReserved = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom.Kingdom, TEXT("TreasuryReserved"))));
				Kingdom.Stocks.MarketCoin = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom.Kingdom, TEXT("MarketCoin"))));
				Kingdom.Stocks.ResidentRepairCredit = FMath::RoundToInt64(Ledger.GetBalance(ESimulationResource::Coin, MakeKingdomAccount(Kingdom.Kingdom, TEXT("ResidentRepairCredit"))));
			}

			void UpdateCohortTimes(const FSimulationTime Time)
			{
				for (TPair<FCohortKey, FCohortBucket>& Pair : KingdomA.Cohorts)
				{
					Pair.Value.LastUpdateTime = Time;
				}
				for (TPair<FCohortKey, FCohortBucket>& Pair : KingdomB.Cohorts)
				{
					Pair.Value.LastUpdateTime = Time;
				}
			}

			FPopulationState BuildPopulationState() const
			{
				FPopulationState Population;
				Population.Total = Config.PopulationPerKingdom * 2;
				for (const TPair<FCohortKey, FCohortBucket>& Pair : KingdomA.Cohorts)
				{
					Population.Anonymous += Pair.Value.PopulationCount;
				}
				for (const TPair<FCohortKey, FCohortBucket>& Pair : KingdomB.Cohorts)
				{
					Population.Anonymous += Pair.Value.PopulationCount;
				}
				return Population;
			}

			FKingdomSnapshot BuildSnapshot(FKingdomState& Kingdom, const FSimulationTime Time)
			{
				RefreshStocks(Kingdom);
				FKingdomSnapshot Snapshot;
				Snapshot.GameTime = Time;
				Snapshot.Kingdom = Kingdom.Kingdom;
				Snapshot.Stocks = Kingdom.Stocks;
				Snapshot.Healthy = GetHomeStateCount(Kingdom, EHomeState::Healthy);
				Snapshot.DamagedWaiting = GetHomeStateCount(Kingdom, EHomeState::DamagedWaiting);
				Snapshot.UnderRepair = GetHomeStateCount(Kingdom, EHomeState::UnderRepair);
				Snapshot.Repaired = GetHomeStateCount(Kingdom, EHomeState::Repaired);
				Snapshot.LedgerTransactionCount = Ledger.GetTransactions().Num();
				return Snapshot;
			}

			static FString FlowKey(const FSimulationTime Time, const EKingdom Kingdom, const TCHAR* Flow)
			{
				return FString::Printf(TEXT("%s-%c-M%lld"), Flow, Kingdom == EKingdom::A ? TEXT('A') : TEXT('B'), Time.Minutes);
			}

			FPhase0Config Config;
			EStage2Scenario Scenario;
			FInitialPopulationManifest PopulationManifest;
			FEarthquakeDamageList DamageList;
			FPersistentTestPool PersistentPool;
			TSet<FResidentID> DamagedResidentIDs;
			FKingdomState KingdomA;
			FKingdomState KingdomB;
			FSimulationClock Clock;
			FSimulationScheduler Scheduler;
			FResourceLedger Ledger;
			FReservationStore Reservations;
			FSimulationEventStore EventStore;
			TMap<FEventID, FImportBatch> ImportBatches;
			TArray<FAidRequest> AidRequests;
			TArray<FKingdomSnapshot> Snapshots;
			TMap<FResourceAccountKey, double> InitialBalances;
			int64 ImportBudgetRemainingCoin = 0;
			int32 HarvestAllowanceDay = INDEX_NONE;
			double HarvestAllowanceRemaining = 0.0;
		};
	}

	const TCHAR* ToString(const EStage2Scenario Scenario)
	{
		switch (Scenario)
		{
		case EStage2Scenario::HarvestCap: return TEXT("HarvestCap");
		case EStage2Scenario::StateImport: return TEXT("StateImport");
		case EStage2Scenario::RepairAid: return TEXT("RepairAid");
		case EStage2Scenario::None:
		default: return TEXT("None");
		}
	}

	FString MakeKingdomAccount(const EKingdom Kingdom, const TCHAR* StockName)
	{
		return FString::Printf(TEXT("%c.%s"), Kingdom == EKingdom::A ? TEXT('A') : TEXT('B'), StockName);
	}

	int32 GetHomeStateCount(const FKingdomState& Kingdom, const EHomeState HomeState)
	{
		int32 Count = 0;
		for (const TPair<FCohortKey, FCohortBucket>& Pair : Kingdom.Cohorts)
		{
			if (Pair.Key.HomeState == HomeState)
			{
				Count += Pair.Value.PopulationCount;
			}
		}
		return Count;
	}

	int32 GetCohortPopulation(const FStage2RunResult& Result)
	{
		int32 Count = 0;
		for (const TPair<FCohortKey, FCohortBucket>& Pair : Result.KingdomA.Cohorts)
		{
			Count += Pair.Value.PopulationCount;
		}
		for (const TPair<FCohortKey, FCohortBucket>& Pair : Result.KingdomB.Cohorts)
		{
			Count += Pair.Value.PopulationCount;
		}
		return Count;
	}

	bool FStructuredMacroRunner::Run(
		const FPhase0Config& Config,
		const EStage2Scenario Scenario,
		FStage2RunResult& OutResult,
		FString& OutError)
	{
		FStage2Simulation Simulation(Config, Scenario);
		return Simulation.Run(OutResult, OutError);
	}

	FString FStructuredMacroRunner::SerializeLedgerTrace(
		const FStage2RunResult& Result,
		const FString& ExperimentID,
		const FString& RunID)
	{
		FString Output;
		for (const FLedgerTransaction& Transaction : Result.Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			Output += FString::Printf(
				TEXT("{\"schema_version\":\"%s\",\"experiment_id\":\"%s\",\"run_id\":\"%s\",\"method\":\"Proposed\",\"scenario\":\"%s\",\"seed\":%d,\"game_time\":\"%s\",\"transaction_id\":%lld,\"idempotency_key\":\"%s\",\"event_id\":%lld,\"arrive_id\":%lld,\"resource\":\"%s\",\"source\":\"%s\",\"destination\":\"%s\",\"quantity\":%.9f,\"boundary_flag\":%s,\"policy_id\":%lld}\n"),
				SchemaVersion,
				*ExperimentID,
				*RunID,
				ToString(Result.Scenario),
				Result.Seed,
				*Transfer.GameTime.ToString(),
				Transaction.TransactionID,
				*Transfer.IdempotencyKey,
				Transfer.EventID,
				Transfer.ArriveID,
				ToString(Transfer.Resource),
				*Transfer.Source,
				*Transfer.Destination,
				Transfer.Quantity,
				Transfer.bBoundaryFlow ? TEXT("true") : TEXT("false"),
				Transfer.PolicyID);
		}
		return Output;
	}

	FString FStructuredMacroRunner::BuildDeterministicDigest(const FStage2RunResult& Result)
	{
		FString Canonical = FString::Printf(
			TEXT("seed=%d|config=%s|scenario=%s|final=%lld|pop=%d|wood=%.12f|dup=%d"),
			Result.Seed,
			*Result.ConfigHash,
			ToString(Result.Scenario),
			Result.FinalTime.Minutes,
			Result.Audit.PopulationResidual,
			Result.Audit.WoodResidual,
			Result.Audit.DuplicateTransactionCount);

		TArray<FResourceAccountKey> BalanceKeys;
		Result.FinalBalances.GetKeys(BalanceKeys);
		BalanceKeys.Sort([](const FResourceAccountKey& Left, const FResourceAccountKey& Right)
		{
			if (Left.Resource != Right.Resource)
			{
				return static_cast<uint8>(Left.Resource) < static_cast<uint8>(Right.Resource);
			}
			return Left.Account < Right.Account;
		});
		for (const FResourceAccountKey& Key : BalanceKeys)
		{
			Canonical += FString::Printf(TEXT("|B:%d:%s:%.12f"), static_cast<int32>(Key.Resource), *Key.Account, Result.FinalBalances[Key]);
		}

		auto AppendCohorts = [&Canonical](const FKingdomState& Kingdom)
		{
			TArray<FCohortKey> Keys;
			Kingdom.Cohorts.GetKeys(Keys);
			Keys.Sort([](const FCohortKey& Left, const FCohortKey& Right)
			{
				return CohortOrder(Left) < CohortOrder(Right);
			});
			for (const FCohortKey& Key : Keys)
			{
				const FCohortBucket& Bucket = Kingdom.Cohorts[Key];
				Canonical += FString::Printf(
					TEXT("|C:%d:%d:%d:%d:%d:%d:%lld:%lld:%d:%d"),
					static_cast<int32>(Key.Kingdom),
					static_cast<int32>(Key.Profession),
					static_cast<int32>(Key.IncomeBand),
					static_cast<int32>(Key.HomeState),
					static_cast<int32>(Key.MacroIntent),
					Bucket.PopulationCount,
					Bucket.CashSum,
					Bucket.RepairCreditSum,
					Bucket.AidEligibleCount,
					Bucket.AidReceivedCount);
			}
		};
		AppendCohorts(Result.KingdomA);
		AppendCohorts(Result.KingdomB);

		for (const FLedgerTransaction& Transaction : Result.Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			Canonical += FString::Printf(
				TEXT("|T:%lld:%lld:%s:%d:%s:%s:%.12f:%d:%lld:%lld:%lld"),
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
