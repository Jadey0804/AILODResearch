// Copyright Epic Games, Inc. All Rights Reserved.

#include "AILODIndividualSimulation.h"

#include "AILODDomainRules.h"
#include "AILODPhase0Manifest.h"
#include "Containers/StringConv.h"
#include "Misc/SecureHash.h"

namespace AILOD
{
	namespace
	{
		using namespace DomainRules;

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
			int32 ResidentIndex = INDEX_NONE;
			FArriveID ArriveID = 0;
		};

		struct FPlannedRequest
		{
			int32 ResidentIndex = INDEX_NONE;
			EIndividualAction Action = EIndividualAction::None;
			uint64 OrderKey = 0;
			FArriveID ArriveID = 0;
		};

		const TCHAR* ResourceName(const ESimulationResource Resource)
		{
			return Resource == ESimulationResource::Wood ? TEXT("Wood") : TEXT("Coin");
		}

		FString ResidentAccount(const FResidentID ResidentID, const TCHAR* StockName)
		{
			return FString::Printf(TEXT("Resident.%lld.%s"), ResidentID, StockName);
		}

		FString ResidentOwner(const FResidentID ResidentID)
		{
			return FString::Printf(TEXT("Resident:%lld"), ResidentID);
		}

		class FOracleSimulation
		{
		public:
			FOracleSimulation(const FPhase0Config& InConfig, const EStage2Scenario InScenario)
				: Config(InConfig)
				, Scenario(InScenario)
				, Clock(FSimulationTime::FromDays(-7))
			{
			}

			bool Run(FStage3OracleRunResult& OutResult, FString& OutError)
			{
				if (Config.PopulationPerKingdom != 100)
				{
					OutError = TEXT("Detailed Individual Oracle is frozen to 200 total residents.");
					return false;
				}
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
					if (StepEnd.Minutes > 0)
					{
						Snapshots.Add(BuildSnapshot(EKingdom::A, StepEnd));
						Snapshots.Add(BuildSnapshot(EKingdom::B, StepEnd));
					}
				}

				RefreshStocks(EKingdom::A);
				RefreshStocks(EKingdom::B);
				const FConservationAudit FinalAudit = AuditConservation(BuildPopulationState(), Ledger, EventStore);
				const double CoinResidual = Ledger.ComputeResidual(ESimulationResource::Coin);
				if (!FinalAudit.IsHardErrorFree() || !FMath::IsNearlyZero(CoinResidual, 1.e-6))
				{
					OutError = TEXT("Stage 3 final conservation audit failed.");
					return false;
				}

				OutResult = {};
				OutResult.Seed = Config.Seed;
				OutResult.ConfigHash = PopulationManifest.ConfigHash;
				OutResult.Scenario = Scenario;
				OutResult.FinalTime = Clock.Now();
				OutResult.WarmupHourSteps = 7 * static_cast<int32>(HoursPerDay);
				OutResult.FormalHourSteps = 60 * static_cast<int32>(HoursPerDay);
				OutResult.Residents = Residents;
				OutResult.KingdomAStocks = KingdomAStocks;
				OutResult.KingdomBStocks = KingdomBStocks;
				OutResult.Audit = FinalAudit;
				OutResult.CoinResidual = CoinResidual;
				OutResult.InitialBalances = InitialBalances;
				OutResult.FinalBalances = Ledger.GetBalances();
				OutResult.Transactions = Ledger.GetTransactions();
				OutResult.ActionTrace = MoveTemp(ActionTrace);
				OutResult.Snapshots = MoveTemp(Snapshots);
				OutResult.AidPaidCount = AidPaidCount;
				OutResult.AdditionalImportedWood = AdditionalImportedWood;

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
				if (!FPhase0ManifestGenerator::Generate(
					Config,
					PopulationManifest,
					DamageList,
					PersistentPool,
					OutError))
				{
					return false;
				}

				Residents.Reserve(PopulationManifest.Residents.Num());
				for (const FInitialResidentRecord& Initial : PopulationManifest.Residents)
				{
					FOracleResidentState& Resident = Residents.AddDefaulted_GetRef();
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
					Resident.LastUpdateTime = Clock.Now();
					Resident.Representation = EResidentRepresentation::ActiveMicro;
				}
				for (const FEarthquakeDamageRecord& Damage : DamageList.DamagedResidents)
				{
					DamagedResidentIDs.Add(Damage.ResidentID);
				}

				if (!InitializeKingdomLedger(EKingdom::A, OutError)
					|| !InitializeKingdomLedger(EKingdom::B, OutError))
				{
					return false;
				}
				for (const FOracleResidentState& Resident : Residents)
				{
					if (!Ledger.InitializeAccount(
						ESimulationResource::Coin,
						ResidentAccount(Resident.ResidentID, TEXT("Cash")),
						Resident.Cash,
						OutError)
						|| !Ledger.InitializeAccount(
							ESimulationResource::Coin,
							ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")),
							Resident.RepairCredit,
							OutError)
						|| !Ledger.InitializeAccount(
							ESimulationResource::Wood,
							ResidentAccount(Resident.ResidentID, TEXT("Wood")),
							Resident.InventoryWood,
							OutError))
					{
						return false;
					}
				}

				Ledger.SealInitialState();
				InitialBalances = Ledger.GetBalances();
				ImportBudgetRemainingCoin = Config.PopulationPerKingdom;
				RefreshStocks(EKingdom::A);
				RefreshStocks(EKingdom::B);
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
					{ ESimulationResource::Wood, TEXT("WoodEmbeddedInRepairs"), 0.0 },
					{ ESimulationResource::Wood, TEXT("WoodInRepairedHomes"), 0.0 },
					{ ESimulationResource::Coin, TEXT("TreasuryAvailable"), 5.0 * N },
					{ ESimulationResource::Coin, TEXT("TreasuryReserved"), 0.0 },
					{ ESimulationResource::Coin, TEXT("MarketCoin"), 0.0 }
				};

				for (const auto& Account : Accounts)
				{
					if (!Ledger.InitializeAccount(
						Account.Resource,
						MakeKingdomAccount(Kingdom, Account.StockName),
						Account.Quantity,
						OutError))
					{
						return false;
					}
				}
				return true;
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

				if (!ApplyForestGrowth(EKingdom::A, Time, OutError)
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
				RefreshStocks(EKingdom::A);
				RefreshStocks(EKingdom::B);
				if (!PlanAndStartActions(Time, OutError))
				{
					return false;
				}

				RefreshStocks(EKingdom::A);
				RefreshStocks(EKingdom::B);
				const FConservationAudit HourAudit = AuditConservation(BuildPopulationState(), Ledger, EventStore);
				if (!HourAudit.IsHardErrorFree()
					|| !FMath::IsNearlyZero(Ledger.ComputeResidual(ESimulationResource::Coin), 1.e-6))
				{
					OutError = FString::Printf(TEXT("Stage 3 conservation audit failed at %s."), *Time.ToString());
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
					if (FImportBatch* Batch = ImportBatches.Find(Due.EventID))
					{
						if (!CompleteImport(*Batch, Time, OutError))
						{
							return false;
						}
						ImportBatches.Remove(Due.EventID);
						continue;
					}

					const FSimulationEventRecord* EventRecord = EventStore.Find(Due.EventID);
					if (EventRecord == nullptr
						|| EventRecord->Event.ResidentID <= 0
						|| EventRecord->Event.ActionCode <= static_cast<int32>(EIndividualAction::None))
					{
						OutError = TEXT("Scheduled Stage 3 event has no action or import context.");
						return false;
					}
					const FSimulationEventRequest ContextCopy = EventRecord->Event;
					if (!CompleteAction(Due.EventID, Due.ArriveID, ContextCopy, Time, OutError))
					{
						return false;
					}
				}
				return true;
			}

			bool CompleteImport(FImportBatch& Batch, const FSimulationTime Time, FString& OutError)
			{
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(EKingdom::A, TEXT("WoodInTransit")),
					MakeKingdomAccount(EKingdom::A, TEXT("MarketWoodAvailable")),
					Batch.WoodQuantity,
					false,
					FString::Printf(TEXT("STATE-IMPORT-ARRIVE-%lld"), Batch.EventID),
					Batch.EventID,
					Batch.ArriveID,
					StateImportPolicyID,
					OutError)
					&& Reservations.CommitReservation(
						Batch.ReservationID,
						ExternalBoundaryAccount,
						FString::Printf(TEXT("STATE-IMPORT-PAY-%lld"), Batch.EventID),
						Time,
						Ledger,
						OutError)
					&& EventStore.CompleteEvent(Batch.EventID, OutError);
			}

			bool CompleteAction(
				const FEventID EventID,
				const FArriveID ArriveID,
				const FSimulationEventRequest& Context,
				const FSimulationTime Time,
				FString& OutError)
			{
				const int32 ResidentIndex = static_cast<int32>(Context.ResidentID - 1);
				if (!Residents.IsValidIndex(ResidentIndex)
					|| Residents[ResidentIndex].ResidentID != Context.ResidentID)
				{
					OutError = TEXT("Action completion references an invalid resident.");
					return false;
				}
				FOracleResidentState& Resident = Residents[ResidentIndex];
				const EIndividualAction Action = static_cast<EIndividualAction>(Context.ActionCode);

				switch (Action)
				{
				case EIndividualAction::Work:
				{
					const int32 Income = Resident.IncomeBand == EIncomeBand::Low ? 1 : 2;
					if (!SubmitFlow(
						Time,
						ESimulationResource::Coin,
						ExternalBoundaryAccount,
						ResidentAccount(Resident.ResidentID, TEXT("Cash")),
						Income,
						true,
						FString::Printf(TEXT("WORK-INCOME-%lld"), EventID),
						EventID,
						ArriveID,
						0,
						OutError))
					{
						return false;
					}
					RefreshResidentResourceView(Resident);
					break;
				}

				case EIndividualAction::BuyWood:
					if (!Reservations.CommitReservation(
						Context.ReservationID,
						ResidentAccount(Resident.ResidentID, TEXT("Wood")),
						FString::Printf(TEXT("BUY-WOOD-DELIVER-%lld"), EventID),
						Time,
						Ledger,
						OutError))
					{
						return false;
					}
					RefreshResidentResourceView(Resident);
					break;

				case EIndividualAction::ChopWood:
					if (!SubmitFlow(
						Time,
						ESimulationResource::Wood,
						MakeKingdomAccount(Resident.Kingdom, TEXT("ForestWood")),
						ResidentAccount(Resident.ResidentID, TEXT("Wood")),
						Context.WoodQuantity,
						false,
						FString::Printf(TEXT("CHOP-WOOD-%lld"), EventID),
						EventID,
						ArriveID,
						IsHarvestCapActive(Time, Resident.Kingdom) ? HarvestCapPolicyID : 0,
						OutError))
					{
						return false;
					}
					RefreshResidentResourceView(Resident);
					break;

				case EIndividualAction::ContinueRepair:
					if (!SubmitFlow(
						Time,
						ESimulationResource::Wood,
						MakeKingdomAccount(Resident.Kingdom, TEXT("WoodEmbeddedInRepairs")),
						MakeKingdomAccount(Resident.Kingdom, TEXT("WoodInRepairedHomes")),
						RepairWoodPerHome,
						false,
						FString::Printf(TEXT("REPAIR-COMPLETE-%lld"), EventID),
						EventID,
						ArriveID,
						0,
						OutError))
					{
						return false;
					}
					Resident.HomeState = EHomeState::Repaired;
					break;

				case EIndividualAction::Routine:
				case EIndividualAction::Wait:
					break;

				default:
					OutError = TEXT("Unsupported action completion.");
					return false;
				}

				if (!EventStore.CompleteEvent(EventID, OutError))
				{
					return false;
				}
				Resident.LastCompletedAction = Action;
				Resident.CurrentAction = EIndividualAction::None;
				Resident.ActiveEventID = 0;
				Resident.ActiveArriveID = 0;
				Resident.ActiveReservationID = 0;
				Resident.ActionEndTime = Time;
				ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, Action, EventID, ArriveID, false });
				Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
				Resident.MacroIntent = Resident.CurrentGoal == EIndividualGoal::RoutineLife
					? EMacroIntent::Routine
					: EMacroIntent::Wait;
				Resident.LastUpdateTime = Time;
				++Resident.Version;
				return true;
			}

			bool ApplyEarthquake(const FSimulationTime Time, FString& OutError)
			{
				for (FOracleResidentState& Resident : Residents)
				{
					if (DamagedResidentIDs.Contains(Resident.ResidentID))
					{
						Resident.HomeState = EHomeState::DamagedWaiting;
						Resident.CurrentGoal = EIndividualGoal::RestoreHome;
						Resident.LastUpdateTime = Time;
						++Resident.Version;
					}
				}
				FEventID EventID = 0;
				return CreateInstantEvent(
					TEXT("EarthquakeDamage"),
					Time,
					DamageList.DamagedResidents.Num(),
					0,
					Scheduler.IssueArriveID(),
					EventID,
					OutError);
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
				RefreshStocks(EKingdom::A);
				const double WaitingHomes = GetResidentHomeStateCount(EKingdom::A, EHomeState::DamagedWaiting);
				const double ExpectedRepairWoodUse = FMath::Min(
					WaitingHomes,
					RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom * 3.0) * RepairWoodPerHome;
				const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
				const double StockGapPerDay = FMath::Max(
					0.0,
					(TargetMarketWood - KingdomAStocks.MarketWoodAvailable - KingdomAStocks.WoodInTransit) / 3.0);
				const double DesiredImport = ExpectedRepairWoodUse + StockGapPerDay;
				const double DailyCap = StateImportDailyCapPerPerson * Config.PopulationPerKingdom;
				const double AffordableByPolicy = ImportBudgetRemainingCoin / StateImportPrice;
				const double AffordableByTreasury = KingdomAStocks.TreasuryAvailable / StateImportPrice;
				double OrderQuantity = FMath::Min(
					FMath::Min(DesiredImport, DailyCap),
					FMath::Min(AffordableByPolicy, AffordableByTreasury));
				if (OrderQuantity <= UE_DOUBLE_SMALL_NUMBER)
				{
					return true;
				}

				int64 CoinCost = FMath::CeilToInt64(OrderQuantity * StateImportPrice);
				if (CoinCost > ImportBudgetRemainingCoin || CoinCost > KingdomAStocks.TreasuryAvailable)
				{
					const int64 CoinLimit = FMath::Min(ImportBudgetRemainingCoin, KingdomAStocks.TreasuryAvailable);
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
				AdditionalImportedWood += OrderQuantity;
				return true;
			}

			bool CalculateAidEligibility(const FSimulationTime Time, FString& OutError)
			{
				AidRequests.Reset();
				const int64 RequiredCoins = PaymentCoins(static_cast<int32>(RepairWoodPerHome), KingdomAStocks.WoodPrice);
				TArray<FPlannedRequest> Eligible;
				for (int32 ResidentIndex = 0; ResidentIndex < Residents.Num(); ++ResidentIndex)
				{
					const FOracleResidentState& Resident = Residents[ResidentIndex];
					if (Resident.Kingdom != EKingdom::A
						|| Resident.IncomeBand != EIncomeBand::Low
						|| Resident.HomeState != EHomeState::DamagedWaiting
						|| Resident.bAidReceived
						|| Resident.Cash + Resident.RepairCredit >= RequiredCoins)
					{
						continue;
					}
					Eligible.Add({
						ResidentIndex,
						EIndividualAction::None,
						CompetitionOrderKey(Config.Seed, Time.Minutes, Resident.ResidentID, 0xA1D00001ull),
						0 });
				}

				Eligible.Sort([this](const FPlannedRequest& Left, const FPlannedRequest& Right)
				{
					if (Left.OrderKey != Right.OrderKey)
					{
						return Left.OrderKey < Right.OrderKey;
					}
					return Residents[Left.ResidentIndex].ResidentID < Residents[Right.ResidentIndex].ResidentID;
				});
				for (const FPlannedRequest& Request : Eligible)
				{
					AidRequests.Add({ Request.ResidentIndex, Scheduler.IssueArriveID() });
				}

				FEventID EventID = 0;
				return CreateInstantEvent(
					TEXT("RepairAidEligibility"),
					Time,
					AidRequests.Num(),
					RepairAidPolicyID,
					Scheduler.IssueArriveID(),
					EventID,
					OutError);
			}

			bool PayRepairAid(const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(EKingdom::A);
				const int64 PolicyBudget = FMath::FloorToInt64(0.40 * Config.PopulationPerKingdom);
				const int64 SpendableCoin = FMath::Min(PolicyBudget, KingdomAStocks.TreasuryAvailable);
				int32 RemainingPaidCount = FMath::Min(
					AidRequests.Num(),
					static_cast<int32>(SpendableCoin / static_cast<int64>(RepairAidPerHome)));

				FEventID EventID = 0;
				const FArriveID MarkerArriveID = AidRequests.Num() > 0
					? AidRequests[0].ArriveID
					: Scheduler.IssueArriveID();
				if (!CreateInstantEvent(
					TEXT("RepairAidPayment"),
					Time,
					RemainingPaidCount,
					RepairAidPolicyID,
					MarkerArriveID,
					EventID,
					OutError))
				{
					return false;
				}

				AidRequests.Sort([](const FAidRequest& Left, const FAidRequest& Right)
				{
					return Left.ArriveID < Right.ArriveID;
				});
				for (const FAidRequest& Request : AidRequests)
				{
					if (RemainingPaidCount <= 0)
					{
						break;
					}
					FOracleResidentState& Resident = Residents[Request.ResidentIndex];
					if (!SubmitFlow(
						Time,
						ESimulationResource::Coin,
						MakeKingdomAccount(EKingdom::A, TEXT("TreasuryAvailable")),
						ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")),
						RepairAidPerHome,
						false,
						FString::Printf(TEXT("REPAIR-AID-%lld"), Resident.ResidentID),
						EventID,
						Request.ArriveID,
						RepairAidPolicyID,
						OutError))
					{
						return false;
					}
					RefreshResidentResourceView(Resident);
					Resident.bAidReceived = true;
					Resident.LastUpdateTime = Time;
					++Resident.Version;
					++AidPaidCount;
					--RemainingPaidCount;
				}
				return true;
			}

			bool ApplyForestGrowth(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(Kingdom);
				const FKingdomStocks& Stocks = GetStocks(Kingdom);
				const double Growth = FMath::Max(
					0.0,
					ForestGrowthRatePerDay * Stocks.ForestWood
						* (1.0 - Stocks.ForestWood / Stocks.ForestCapacity) / HoursPerGameDay);
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(Kingdom, TEXT("ForestWood")),
					Growth,
					true,
					FString::Printf(TEXT("FOREST-GROWTH-%s-M%lld"), Kingdom == EKingdom::A ? TEXT("A") : TEXT("B"), Time.Minutes),
					0,
					0,
					0,
					OutError);
			}

			bool ApplyBaselineImport(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
			{
				const double Quantity = BaselineImportPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay;
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					ExternalBoundaryAccount,
					MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
					Quantity,
					true,
					FString::Printf(TEXT("BASELINE-IMPORT-%s-M%lld"), Kingdom == EKingdom::A ? TEXT("A") : TEXT("B"), Time.Minutes),
					0,
					0,
					0,
					OutError);
			}

			bool ApplyCommercialHarvest(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(Kingdom);
				FKingdomStocks& Stocks = GetStocks(Kingdom);
				const double Desired = BaselineHarvestPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay;
				double Allowance = TNumericLimits<double>::Max();
				FPolicyID PolicyID = 0;
				if (IsHarvestCapActive(Time, Kingdom))
				{
					EnsureHarvestAllowanceDay(Time);
					Allowance = HarvestAllowanceRemaining;
					PolicyID = HarvestCapPolicyID;
				}

				const double Quantity = FMath::Min(Desired, FMath::Min(Allowance, Stocks.ForestWood));
				if (Quantity <= UE_DOUBLE_SMALL_NUMBER)
				{
					return true;
				}
				if (!SubmitFlow(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom, TEXT("ForestWood")),
					MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
					Quantity,
					false,
					FString::Printf(TEXT("COMMERCIAL-HARVEST-%s-M%lld"), Kingdom == EKingdom::A ? TEXT("A") : TEXT("B"), Time.Minutes),
					0,
					0,
					PolicyID,
					OutError))
				{
					return false;
				}
				if (PolicyID == HarvestCapPolicyID)
				{
					HarvestAllowanceRemaining = FMath::Max(0.0, HarvestAllowanceRemaining - Quantity);
				}
				return true;
			}

			bool ApplyRoutineConsumption(const EKingdom Kingdom, const FSimulationTime Time, FString& OutError)
			{
				RefreshStocks(Kingdom);
				const FKingdomStocks& Stocks = GetStocks(Kingdom);
				const double Desired = RoutineConsumptionPerPersonPerDay * Config.PopulationPerKingdom / HoursPerGameDay;
				const double Quantity = FMath::Min(Desired, Stocks.MarketWoodAvailable);
				if (Quantity <= UE_DOUBLE_SMALL_NUMBER)
				{
					return true;
				}
				return SubmitFlow(
					Time,
					ESimulationResource::Wood,
					MakeKingdomAccount(Kingdom, TEXT("MarketWoodAvailable")),
					ExternalBoundaryAccount,
					Quantity,
					true,
					FString::Printf(TEXT("ROUTINE-CONSUMPTION-%s-M%lld"), Kingdom == EKingdom::A ? TEXT("A") : TEXT("B"), Time.Minutes),
					0,
					0,
					0,
					OutError);
			}

			void UpdateWoodPrice(const EKingdom Kingdom)
			{
				RefreshStocks(Kingdom);
				FKingdomStocks& Stocks = GetStocks(Kingdom);
				const double TargetMarketWood = 2.0 * Config.PopulationPerKingdom;
				const double PriceTarget = FMath::Clamp(
					FMath::Sqrt(TargetMarketWood / FMath::Max(Stocks.MarketWoodAvailable, UE_DOUBLE_SMALL_NUMBER)),
					0.5,
					3.0);
				Stocks.WoodPrice += (PriceTarget - Stocks.WoodPrice) / HoursPerGameDay;
			}

			bool PlanAndStartActions(const FSimulationTime Time, FString& OutError)
			{
				TArray<FPlannedRequest> Requests;
				for (int32 ResidentIndex = 0; ResidentIndex < Residents.Num(); ++ResidentIndex)
				{
					FOracleResidentState& Resident = Residents[ResidentIndex];
					if (Resident.ActiveEventID != 0)
					{
						continue;
					}

					Resident.CurrentGoal = FIndividualDomain::SelectGoal(Resident);
					const FKingdomStocks& Stocks = GetStocks(Resident.Kingdom);
					FIndividualWorldFacts World;
					World.MarketWoodAvailable = Stocks.MarketWoodAvailable;
					World.ForestWood = Stocks.ForestWood;
					World.HarvestAllowance = GetHarvestAllowance(Time, Resident.Kingdom);
					World.WoodPrice = Stocks.WoodPrice;
					const FIndividualPlan Plan = FIndividualDomain::BuildPlan(Resident, World);
					const EIndividualAction Action = Plan.Actions.Num() > 0 ? Plan.Actions[0] : EIndividualAction::Wait;
					Requests.Add({
						ResidentIndex,
						Action,
						CompetitionOrderKey(
							Config.Seed,
							Time.Minutes,
							Resident.ResidentID,
							static_cast<uint64>(Action)),
						0 });
				}

				Requests.Sort([this](const FPlannedRequest& Left, const FPlannedRequest& Right)
				{
					if (Left.OrderKey != Right.OrderKey)
					{
						return Left.OrderKey < Right.OrderKey;
					}
					return Residents[Left.ResidentIndex].ResidentID < Residents[Right.ResidentIndex].ResidentID;
				});
				for (FPlannedRequest& Request : Requests)
				{
					Request.ArriveID = Scheduler.IssueArriveID();
				}
				for (const FPlannedRequest& Request : Requests)
				{
					if (!StartRequestedAction(Request, Time, OutError))
					{
						return false;
					}
				}
				return true;
			}

			bool StartRequestedAction(
				const FPlannedRequest& Request,
				const FSimulationTime Time,
				FString& OutError)
			{
				FOracleResidentState& Resident = Residents[Request.ResidentIndex];
				switch (Request.Action)
				{
				case EIndividualAction::Routine:
					return StartTimedAction(Request, Time, 8 * MinutesPerHour, 0, 0, OutError);
				case EIndividualAction::Work:
					return StartTimedAction(Request, Time, MinutesPerDay, 0, 0, OutError);
				case EIndividualAction::Wait:
					return StartTimedAction(Request, Time, 6 * MinutesPerHour, 0, 0, OutError);

				case EIndividualAction::BuyWood:
					return StartBuyWood(Request, Time, OutError);

				case EIndividualAction::ChopWood:
					return StartChopWood(Request, Time, OutError);

				case EIndividualAction::StartRepair:
					return StartRepair(Request, Time, OutError);

				case EIndividualAction::ContinueRepair:
					if (Resident.ActiveEventID != 0)
					{
						return true;
					}
					return StartFallbackWait(Request, Time, OutError);

				case EIndividualAction::None:
				default:
					return StartFallbackWait(Request, Time, OutError);
				}
			}

			bool StartBuyWood(const FPlannedRequest& Request, const FSimulationTime Time, FString& OutError)
			{
				FOracleResidentState& Resident = Residents[Request.ResidentIndex];
				RefreshStocks(Resident.Kingdom);
				const FKingdomStocks& Stocks = GetStocks(Resident.Kingdom);
				const int32 MissingWood = FMath::Max(0, static_cast<int32>(RepairWoodPerHome) - Resident.InventoryWood);
				const int64 Cost = PaymentCoins(MissingWood, Stocks.WoodPrice);
				if (MissingWood <= 0
					|| Stocks.MarketWoodAvailable + UE_DOUBLE_SMALL_NUMBER < MissingWood
					|| Resident.Cash + Resident.RepairCredit < Cost)
				{
					ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, EIndividualAction::BuyWood, 0, Request.ArriveID, false });
					return StartFallbackWait(Request, Time, OutError);
				}

				FSimulationEventRequest EventRequest;
				EventRequest.Type = ToString(EIndividualAction::BuyWood);
				EventRequest.Owner = ResidentOwner(Resident.ResidentID);
				EventRequest.ResidentID = Resident.ResidentID;
				EventRequest.ActionCode = static_cast<int32>(EIndividualAction::BuyWood);
				EventRequest.WoodQuantity = MissingWood;
				EventRequest.StartTime = Time;
				EventRequest.EndTime = FSimulationTime::FromMinutes(Time.Minutes + MinutesPerHour);
				EventRequest.ArriveID = Request.ArriveID;
				EventRequest.ParticipantCount = 1;
				EventRequest.Cause = ToString(Resident.CurrentGoal);
				FEventID EventID = 0;
				if (!EventStore.CreateEvent(EventRequest, EventID, OutError))
				{
					return false;
				}

				FReservationRequest ReservationRequest;
				ReservationRequest.IdempotencyKey = FString::Printf(TEXT("BUY-WOOD-RESERVE-%lld"), EventID);
				ReservationRequest.GameTime = Time;
				ReservationRequest.Resource = ESimulationResource::Wood;
				ReservationRequest.SourceAccount = MakeKingdomAccount(Resident.Kingdom, TEXT("MarketWoodAvailable"));
				ReservationRequest.ReservedAccount = MakeKingdomAccount(Resident.Kingdom, TEXT("MarketWoodReserved"));
				ReservationRequest.Quantity = MissingWood;
				ReservationRequest.EventID = EventID;
				ReservationRequest.ArriveID = Request.ArriveID;
				FReservationID ReservationID = 0;
				if (!Reservations.CreateReservation(ReservationRequest, Ledger, ReservationID, OutError)
					|| !EventStore.SetReservationID(EventID, ReservationID, OutError))
				{
					return false;
				}

				const int32 CreditPayment = FMath::Min(Resident.RepairCredit, static_cast<int32>(Cost));
				const int32 CashPayment = static_cast<int32>(Cost) - CreditPayment;
				if (CreditPayment > 0
					&& !SubmitFlow(
						Time,
						ESimulationResource::Coin,
						ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")),
						MakeKingdomAccount(Resident.Kingdom, TEXT("MarketCoin")),
						CreditPayment,
						false,
						FString::Printf(TEXT("BUY-WOOD-CREDIT-%lld"), EventID),
						EventID,
						Request.ArriveID,
						0,
						OutError))
				{
					return false;
				}
				if (CashPayment > 0
					&& !SubmitFlow(
						Time,
						ESimulationResource::Coin,
						ResidentAccount(Resident.ResidentID, TEXT("Cash")),
						MakeKingdomAccount(Resident.Kingdom, TEXT("MarketCoin")),
						CashPayment,
						false,
						FString::Printf(TEXT("BUY-WOOD-CASH-%lld"), EventID),
						EventID,
						Request.ArriveID,
						0,
						OutError))
				{
					return false;
				}

				RefreshResidentResourceView(Resident);
				Resident.CurrentAction = EIndividualAction::BuyWood;
				Resident.MacroIntent = ToMacroIntent(EIndividualAction::BuyWood);
				Resident.ActiveEventID = EventID;
				Resident.ActiveArriveID = Request.ArriveID;
				Resident.ActiveReservationID = ReservationID;
				Resident.ActionStartTime = Time;
				Resident.ActionEndTime = EventRequest.EndTime;
				Resident.LastUpdateTime = Time;
				++Resident.Version;
				ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, EIndividualAction::BuyWood, EventID, Request.ArriveID, true });
				return Scheduler.Schedule({ EventID, Request.ArriveID, EventRequest.EndTime }, Time, OutError);
			}

			bool StartChopWood(const FPlannedRequest& Request, const FSimulationTime Time, FString& OutError)
			{
				FOracleResidentState& Resident = Residents[Request.ResidentIndex];
				RefreshStocks(Resident.Kingdom);
				const int32 MissingWood = FMath::Max(0, static_cast<int32>(RepairWoodPerHome) - Resident.InventoryWood);
				const double Allowance = GetHarvestAllowance(Time, Resident.Kingdom);
				if (Resident.Profession != EProfession::Logger
					|| MissingWood <= 0
					|| GetStocks(Resident.Kingdom).ForestWood + UE_DOUBLE_SMALL_NUMBER < MissingWood
					|| Allowance + UE_DOUBLE_SMALL_NUMBER < MissingWood)
				{
					ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, EIndividualAction::ChopWood, 0, Request.ArriveID, false });
					return StartFallbackWait(Request, Time, OutError);
				}

				if (IsHarvestCapActive(Time, Resident.Kingdom))
				{
					HarvestAllowanceRemaining -= MissingWood;
				}
				return StartTimedAction(Request, Time, MinutesPerDay, MissingWood, 0, OutError);
			}

			bool StartRepair(const FPlannedRequest& Request, const FSimulationTime Time, FString& OutError)
			{
				FOracleResidentState& Resident = Residents[Request.ResidentIndex];
				EnsureRepairCapacityDay(Time);
				if (Resident.HomeState != EHomeState::DamagedWaiting
					|| Resident.InventoryWood < static_cast<int32>(RepairWoodPerHome)
					|| RepairStartsRemaining <= 0)
				{
					ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, EIndividualAction::StartRepair, 0, Request.ArriveID, false });
					return StartFallbackWait(Request, Time, OutError);
				}

				FSimulationEventRequest EventRequest;
				EventRequest.Type = TEXT("Repair");
				EventRequest.Owner = ResidentOwner(Resident.ResidentID);
				EventRequest.ResidentID = Resident.ResidentID;
				EventRequest.ActionCode = static_cast<int32>(EIndividualAction::ContinueRepair);
				EventRequest.WoodQuantity = static_cast<int32>(RepairWoodPerHome);
				EventRequest.StartTime = Time;
				EventRequest.EndTime = FSimulationTime::FromMinutes(Time.Minutes + 2 * MinutesPerDay);
				EventRequest.ArriveID = Request.ArriveID;
				EventRequest.ParticipantCount = 1;
				EventRequest.Cause = ToString(Resident.CurrentGoal);
				FEventID EventID = 0;
				if (!EventStore.CreateEvent(EventRequest, EventID, OutError)
					|| !SubmitFlow(
						Time,
						ESimulationResource::Wood,
						ResidentAccount(Resident.ResidentID, TEXT("Wood")),
						MakeKingdomAccount(Resident.Kingdom, TEXT("WoodEmbeddedInRepairs")),
						RepairWoodPerHome,
						false,
						FString::Printf(TEXT("REPAIR-START-%lld"), EventID),
						EventID,
						Request.ArriveID,
						0,
						OutError))
				{
					return false;
				}

				--RepairStartsRemaining;
				RefreshResidentResourceView(Resident);
				Resident.HomeState = EHomeState::UnderRepair;
				Resident.CurrentAction = EIndividualAction::ContinueRepair;
				Resident.MacroIntent = ToMacroIntent(EIndividualAction::ContinueRepair);
				Resident.ActiveEventID = EventID;
				Resident.ActiveArriveID = Request.ArriveID;
				Resident.ActionStartTime = Time;
				Resident.ActionEndTime = EventRequest.EndTime;
				Resident.LastUpdateTime = Time;
				++Resident.Version;
				ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, EIndividualAction::StartRepair, EventID, Request.ArriveID, true });
				return Scheduler.Schedule({ EventID, Request.ArriveID, EventRequest.EndTime }, Time, OutError);
			}

			bool StartTimedAction(
				const FPlannedRequest& Request,
				const FSimulationTime Time,
				const int64 DurationMinutes,
				const int32 WoodQuantity,
				const FReservationID ReservationID,
				FString& OutError)
			{
				FOracleResidentState& Resident = Residents[Request.ResidentIndex];
				FSimulationEventRequest EventRequest;
				EventRequest.Type = ToString(Request.Action);
				EventRequest.Owner = ResidentOwner(Resident.ResidentID);
				EventRequest.ResidentID = Resident.ResidentID;
				EventRequest.ActionCode = static_cast<int32>(Request.Action);
				EventRequest.WoodQuantity = WoodQuantity;
				EventRequest.ReservationID = ReservationID;
				EventRequest.StartTime = Time;
				EventRequest.EndTime = FSimulationTime::FromMinutes(Time.Minutes + DurationMinutes);
				EventRequest.ArriveID = Request.ArriveID;
				EventRequest.ParticipantCount = 1;
				EventRequest.Cause = ToString(Resident.CurrentGoal);
				FEventID EventID = 0;
				if (!EventStore.CreateEvent(EventRequest, EventID, OutError))
				{
					return false;
				}

				Resident.CurrentAction = Request.Action;
				Resident.MacroIntent = ToMacroIntent(Request.Action);
				Resident.ActiveEventID = EventID;
				Resident.ActiveArriveID = Request.ArriveID;
				Resident.ActiveReservationID = ReservationID;
				Resident.ActionStartTime = Time;
				Resident.ActionEndTime = EventRequest.EndTime;
				Resident.LastUpdateTime = Time;
				++Resident.Version;
				ActionTrace.Add({ Time, Resident.ResidentID, Resident.CurrentGoal, Request.Action, EventID, Request.ArriveID, true });
				return Scheduler.Schedule({ EventID, Request.ArriveID, EventRequest.EndTime }, Time, OutError);
			}

			bool StartFallbackWait(const FPlannedRequest& FailedRequest, const FSimulationTime Time, FString& OutError)
			{
				FPlannedRequest WaitRequest = FailedRequest;
				WaitRequest.Action = EIndividualAction::Wait;
				WaitRequest.ArriveID = Scheduler.IssueArriveID();
				return StartTimedAction(WaitRequest, Time, 6 * MinutesPerHour, 0, 0, OutError);
			}

			bool SubmitFlow(
				const FSimulationTime Time,
				const ESimulationResource Resource,
				const FString& Source,
				const FString& Destination,
				const double Quantity,
				const bool bBoundaryFlow,
				const FIdempotencyKey& IdempotencyKey,
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
				const FArriveID ArriveID,
				FEventID& OutEventID,
				FString& OutError)
			{
				FSimulationEventRequest Request;
				Request.Type = Type;
				Request.Owner = TEXT("Macro:A");
				Request.StartTime = Time;
				Request.EndTime = Time;
				Request.ArriveID = ArriveID;
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
				return CreateInstantEvent(
					Type,
					Time,
					0,
					PolicyID,
					Scheduler.IssueArriveID(),
					EventID,
					OutError);
			}

			bool IsHarvestCapActive(const FSimulationTime Time, const EKingdom Kingdom) const
			{
				return Kingdom == EKingdom::A
					&& Scenario == EStage2Scenario::HarvestCap
					&& Time.Minutes >= FSimulationTime::FromDays(3).Minutes
					&& Time.Minutes < FSimulationTime::FromDays(30).Minutes;
			}

			void EnsureHarvestAllowanceDay(const FSimulationTime Time)
			{
				const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
				if (HarvestAllowanceDay != Day)
				{
					HarvestAllowanceDay = Day;
					HarvestAllowanceRemaining = HarvestCapPerPersonPerDay * Config.PopulationPerKingdom;
				}
			}

			double GetHarvestAllowance(const FSimulationTime Time, const EKingdom Kingdom)
			{
				if (!IsHarvestCapActive(Time, Kingdom))
				{
					return TNumericLimits<double>::Max();
				}
				EnsureHarvestAllowanceDay(Time);
				return HarvestAllowanceRemaining;
			}

			void EnsureRepairCapacityDay(const FSimulationTime Time)
			{
				const int32 Day = static_cast<int32>(Time.Minutes / MinutesPerDay);
				if (RepairCapacityDay != Day)
				{
					RepairCapacityDay = Day;
					RepairStartsRemaining = FMath::FloorToInt(
						RepairStartCapacityPerPersonPerDay * Config.PopulationPerKingdom);
				}
			}

			void RefreshStocks(const EKingdom Kingdom)
			{
				FKingdomStocks& Stocks = GetStocks(Kingdom);
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
				Stocks.ResidentInventoryWood = 0.0;
				Stocks.ResidentRepairCredit = 0;
				for (const FOracleResidentState& Resident : Residents)
				{
					if (Resident.Kingdom == Kingdom)
					{
						Stocks.ResidentInventoryWood += Resident.InventoryWood;
						Stocks.ResidentRepairCredit += Resident.RepairCredit;
					}
				}
			}

			void RefreshResidentResourceView(FOracleResidentState& Resident)
			{
				Resident.Cash = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
					ESimulationResource::Coin,
					ResidentAccount(Resident.ResidentID, TEXT("Cash")))));
				Resident.RepairCredit = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
					ESimulationResource::Coin,
					ResidentAccount(Resident.ResidentID, TEXT("RepairCredit")))));
				Resident.InventoryWood = static_cast<int32>(FMath::RoundToInt64(Ledger.GetBalance(
					ESimulationResource::Wood,
					ResidentAccount(Resident.ResidentID, TEXT("Wood")))));
			}

			FKingdomStocks& GetStocks(const EKingdom Kingdom)
			{
				return Kingdom == EKingdom::A ? KingdomAStocks : KingdomBStocks;
			}

			const FKingdomStocks& GetStocks(const EKingdom Kingdom) const
			{
				return Kingdom == EKingdom::A ? KingdomAStocks : KingdomBStocks;
			}

			int32 GetResidentHomeStateCount(const EKingdom Kingdom, const EHomeState HomeState) const
			{
				int32 Count = 0;
				for (const FOracleResidentState& Resident : Residents)
				{
					if (Resident.Kingdom == Kingdom && Resident.HomeState == HomeState)
					{
						++Count;
					}
				}
				return Count;
			}

			FKingdomSnapshot BuildSnapshot(const EKingdom Kingdom, const FSimulationTime Time) const
			{
				FKingdomSnapshot Snapshot;
				Snapshot.GameTime = Time;
				Snapshot.Kingdom = Kingdom;
				Snapshot.Stocks = GetStocks(Kingdom);
				Snapshot.Healthy = GetResidentHomeStateCount(Kingdom, EHomeState::Healthy);
				Snapshot.DamagedWaiting = GetResidentHomeStateCount(Kingdom, EHomeState::DamagedWaiting);
				Snapshot.UnderRepair = GetResidentHomeStateCount(Kingdom, EHomeState::UnderRepair);
				Snapshot.Repaired = GetResidentHomeStateCount(Kingdom, EHomeState::Repaired);
				Snapshot.LedgerTransactionCount = Ledger.GetTransactions().Num();
				return Snapshot;
			}

			FPopulationState BuildPopulationState() const
			{
				FPopulationState Population;
				Population.Total = Residents.Num();
				Population.ActiveMicro = Residents.Num();
				return Population;
			}

			FPhase0Config Config;
			EStage2Scenario Scenario = EStage2Scenario::None;
			FInitialPopulationManifest PopulationManifest;
			FEarthquakeDamageList DamageList;
			FPersistentTestPool PersistentPool;
			TSet<FResidentID> DamagedResidentIDs;
			TArray<FOracleResidentState> Residents;
			FKingdomStocks KingdomAStocks;
			FKingdomStocks KingdomBStocks;
			FSimulationClock Clock;
			FSimulationScheduler Scheduler;
			FResourceLedger Ledger;
			FReservationStore Reservations;
			FSimulationEventStore EventStore;
			TMap<FEventID, FImportBatch> ImportBatches;
			TArray<FAidRequest> AidRequests;
			TArray<FIndividualActionTrace> ActionTrace;
			TArray<FKingdomSnapshot> Snapshots;
			TMap<FResourceAccountKey, double> InitialBalances;
			int64 ImportBudgetRemainingCoin = 0;
			int32 AidPaidCount = 0;
			double AdditionalImportedWood = 0.0;
			int32 HarvestAllowanceDay = INDEX_NONE;
			double HarvestAllowanceRemaining = 0.0;
			int32 RepairCapacityDay = INDEX_NONE;
			int32 RepairStartsRemaining = 0;
		};
	}

	const TCHAR* ToString(const EIndividualGoal Goal)
	{
		switch (Goal)
		{
		case EIndividualGoal::RestoreHome: return TEXT("RestoreHome");
		case EIndividualGoal::RoutineLife: return TEXT("RoutineLife");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* ToString(const EIndividualAction Action)
	{
		switch (Action)
		{
		case EIndividualAction::None: return TEXT("None");
		case EIndividualAction::Routine: return TEXT("Routine");
		case EIndividualAction::Work: return TEXT("Work");
		case EIndividualAction::BuyWood: return TEXT("BuyWood");
		case EIndividualAction::ChopWood: return TEXT("ChopWood");
		case EIndividualAction::StartRepair: return TEXT("StartRepair");
		case EIndividualAction::ContinueRepair: return TEXT("ContinueRepair");
		case EIndividualAction::Wait: return TEXT("Wait");
		default: return TEXT("Unknown");
		}
	}

	EMacroIntent ToMacroIntent(const EIndividualAction Action)
	{
		switch (Action)
		{
		case EIndividualAction::Routine: return EMacroIntent::Routine;
		case EIndividualAction::Work: return EMacroIntent::Work;
		case EIndividualAction::BuyWood: return EMacroIntent::BuyWood;
		case EIndividualAction::ChopWood: return EMacroIntent::ChopWood;
		case EIndividualAction::StartRepair:
		case EIndividualAction::ContinueRepair: return EMacroIntent::Repair;
		case EIndividualAction::None:
		case EIndividualAction::Wait:
		default: return EMacroIntent::Wait;
		}
	}

	EIndividualGoal FIndividualDomain::SelectGoal(const FOracleResidentState& Resident)
	{
		return Resident.HomeState == EHomeState::DamagedWaiting
			|| Resident.HomeState == EHomeState::UnderRepair
			? EIndividualGoal::RestoreHome
			: EIndividualGoal::RoutineLife;
	}

	FIndividualPlan FIndividualDomain::BuildPlan(
		const FOracleResidentState& Resident,
		const FIndividualWorldFacts& World)
	{
		FIndividualPlan Plan;
		Plan.Goal = SelectGoal(Resident);
		if (Plan.Goal == EIndividualGoal::RoutineLife)
		{
			Plan.Actions.Add(EIndividualAction::Routine);
			return Plan;
		}

		struct FSearchState
		{
			int32 Cash = 0;
			int32 RepairCredit = 0;
			int32 Wood = 0;
			EHomeState HomeState = EHomeState::Healthy;
		};
		struct FSearchNode
		{
			FSearchState State;
			int64 CostMinutes = 0;
			TArray<EIndividualAction> Actions;
		};

		auto StateKey = [](const FSearchState& State)
		{
			return FString::Printf(
				TEXT("%d|%d|%d|%d"),
				State.Cash,
				State.RepairCredit,
				State.Wood,
				static_cast<int32>(State.HomeState));
		};
		auto IsNodeEarlier = [](const FSearchNode& Left, const FSearchNode& Right)
		{
			if (Left.CostMinutes != Right.CostMinutes)
			{
				return Left.CostMinutes < Right.CostMinutes;
			}
			const int32 CommonLength = FMath::Min(Left.Actions.Num(), Right.Actions.Num());
			for (int32 Index = 0; Index < CommonLength; ++Index)
			{
				if (Left.Actions[Index] != Right.Actions[Index])
				{
					return static_cast<uint8>(Left.Actions[Index]) < static_cast<uint8>(Right.Actions[Index]);
				}
			}
			return Left.Actions.Num() < Right.Actions.Num();
		};
		auto TryApply = [&Resident, &World](
			const FSearchState& State,
			const EIndividualAction Action,
			FSearchState& OutState,
			int64& OutCostMinutes)
		{
			OutState = State;
			OutCostMinutes = 0;
			const int32 MissingWood = FMath::Max(0, static_cast<int32>(RepairWoodPerHome) - State.Wood);
			const int64 Cost = PaymentCoins(MissingWood, World.WoodPrice);
			switch (Action)
			{
			case EIndividualAction::Work:
				if (State.HomeState != EHomeState::DamagedWaiting
					|| State.Cash + State.RepairCredit >= Cost)
				{
					return false;
				}
				OutState.Cash += Resident.IncomeBand == EIncomeBand::Low ? 1 : 2;
				OutCostMinutes = MinutesPerDay;
				return true;

			case EIndividualAction::BuyWood:
				if (State.HomeState != EHomeState::DamagedWaiting
					|| MissingWood <= 0
					|| World.MarketWoodAvailable + UE_DOUBLE_SMALL_NUMBER < MissingWood
					|| State.Cash + State.RepairCredit < Cost)
				{
					return false;
				}
				{
					const int32 CreditPayment = FMath::Min(State.RepairCredit, static_cast<int32>(Cost));
					OutState.RepairCredit -= CreditPayment;
					OutState.Cash -= static_cast<int32>(Cost) - CreditPayment;
					OutState.Wood += MissingWood;
					OutCostMinutes = MinutesPerHour;
					return true;
				}

			case EIndividualAction::ChopWood:
				if (State.HomeState != EHomeState::DamagedWaiting
					|| Resident.Profession != EProfession::Logger
					|| MissingWood <= 0
					|| World.ForestWood + UE_DOUBLE_SMALL_NUMBER < MissingWood
					|| World.HarvestAllowance + UE_DOUBLE_SMALL_NUMBER < MissingWood)
				{
					return false;
				}
				OutState.Wood += MissingWood;
				OutCostMinutes = MinutesPerDay;
				return true;

			case EIndividualAction::StartRepair:
				if (State.HomeState != EHomeState::DamagedWaiting
					|| State.Wood < static_cast<int32>(RepairWoodPerHome))
				{
					return false;
				}
				OutState.Wood -= static_cast<int32>(RepairWoodPerHome);
				OutState.HomeState = EHomeState::UnderRepair;
				return true;

			case EIndividualAction::ContinueRepair:
				if (State.HomeState != EHomeState::UnderRepair)
				{
					return false;
				}
				OutState.HomeState = EHomeState::Repaired;
				OutCostMinutes = 2 * MinutesPerDay;
				return true;

			default:
				return false;
			}
		};

		TArray<FSearchNode> Frontier;
		Frontier.Add({
			{ Resident.Cash, Resident.RepairCredit, Resident.InventoryWood, Resident.HomeState },
			0,
			{} });
		TSet<FString> Visited;
		const EIndividualAction SearchActions[] =
		{
			EIndividualAction::Work,
			EIndividualAction::BuyWood,
			EIndividualAction::ChopWood,
			EIndividualAction::StartRepair,
			EIndividualAction::ContinueRepair
		};

		while (Frontier.Num() > 0)
		{
			Frontier.Sort(IsNodeEarlier);
			FSearchNode Node = MoveTemp(Frontier[0]);
			Frontier.RemoveAt(0, 1, EAllowShrinking::No);
			const FString Key = StateKey(Node.State);
			if (Visited.Contains(Key))
			{
				continue;
			}
			Visited.Add(Key);

			if (Node.State.HomeState == EHomeState::Repaired)
			{
				Plan.Actions = MoveTemp(Node.Actions);
				return Plan;
			}

			for (const EIndividualAction Action : SearchActions)
			{
				FSearchState NextState;
				int64 ActionCost = 0;
				if (!TryApply(Node.State, Action, NextState, ActionCost))
				{
					continue;
				}
				FSearchNode& Next = Frontier.AddDefaulted_GetRef();
				Next.State = NextState;
				Next.CostMinutes = Node.CostMinutes + ActionCost;
				Next.Actions = Node.Actions;
				Next.Actions.Add(Action);
			}
		}

		Plan.Actions.Add(EIndividualAction::Wait);
		return Plan;
	}

	int32 FStage3OracleRunResult::GetActionCount(
		const EIndividualAction Action,
		const bool bStartedOnly) const
	{
		int32 Count = 0;
		for (const FIndividualActionTrace& Trace : ActionTrace)
		{
			if (Trace.Action == Action && (!bStartedOnly || Trace.bStarted))
			{
				++Count;
			}
		}
		return Count;
	}

	int32 FStage3OracleRunResult::GetHomeStateCount(
		const EKingdom Kingdom,
		const EHomeState HomeState) const
	{
		int32 Count = 0;
		for (const FOracleResidentState& Resident : Residents)
		{
			if (Resident.Kingdom == Kingdom && Resident.HomeState == HomeState)
			{
				++Count;
			}
		}
		return Count;
	}

	bool FIndividualOracleRunner::Run(
		const FPhase0Config& Config,
		const EStage2Scenario Scenario,
		FStage3OracleRunResult& OutResult,
		FString& OutError)
	{
		FOracleSimulation Simulation(Config, Scenario);
		return Simulation.Run(OutResult, OutError);
	}

	FString FIndividualOracleRunner::SerializeLedgerTrace(
		const FStage3OracleRunResult& Result,
		const FString& ExperimentID,
		const FString& RunID)
	{
		FString Output;
		for (const FLedgerTransaction& Transaction : Result.Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			Output += FString::Printf(
				TEXT("{\"schema_version\":\"%s\",\"experiment_id\":\"%s\",\"run_id\":\"%s\",\"method\":\"Oracle\",\"scenario\":\"%s\",\"seed\":%d,\"game_time\":\"%s\",\"transaction_id\":%lld,\"idempotency_key\":\"%s\",\"event_id\":%lld,\"arrive_id\":%lld,\"resource\":\"%s\",\"source\":\"%s\",\"destination\":\"%s\",\"quantity\":%.9f,\"boundary_flag\":%s,\"policy_id\":%lld}\n"),
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
				ResourceName(Transfer.Resource),
				*Transfer.Source,
				*Transfer.Destination,
				Transfer.Quantity,
				Transfer.bBoundaryFlow ? TEXT("true") : TEXT("false"),
				Transfer.PolicyID);
		}
		return Output;
	}

	FString FIndividualOracleRunner::BuildDeterministicDigest(const FStage3OracleRunResult& Result)
	{
		FString Canonical = FString::Printf(
			TEXT("Seed=%d|Scenario=%s|Final=%lld|Aid=%d|Import=%.9f|"),
			Result.Seed,
			ToString(Result.Scenario),
			Result.FinalTime.Minutes,
			Result.AidPaidCount,
			Result.AdditionalImportedWood);
		for (const FOracleResidentState& Resident : Result.Residents)
		{
			Canonical += FString::Printf(
				TEXT("R=%lld,%d,%d,%d,%d,%d,%d,%d,%lld,%lld|"),
				Resident.ResidentID,
				static_cast<int32>(Resident.Kingdom),
				Resident.Cash,
				Resident.RepairCredit,
				Resident.InventoryWood,
				static_cast<int32>(Resident.HomeState),
				static_cast<int32>(Resident.CurrentGoal),
				static_cast<int32>(Resident.CurrentAction),
				Resident.ActiveEventID,
				Resident.ActiveArriveID);
		}
		for (const FLedgerTransaction& Transaction : Result.Transactions)
		{
			const FLedgerTransferRequest& Transfer = Transaction.Transfer;
			Canonical += FString::Printf(
				TEXT("T=%lld,%s,%lld,%d,%s,%s,%.9f,%d,%lld,%lld,%lld|"),
				Transaction.TransactionID,
				*Transfer.IdempotencyKey,
				Transfer.GameTime.Minutes,
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
