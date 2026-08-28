// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AILODPhase0Types.h"

namespace AILOD
{
	inline constexpr int64 MinutesPerHour = 60;
	inline constexpr int64 HoursPerDay = 24;
	inline constexpr int64 MinutesPerDay = MinutesPerHour * HoursPerDay;
	inline constexpr TCHAR ExternalBoundaryAccount[] = TEXT("ExternalBoundary");

	struct FSimulationTime
	{
		int64 Minutes = 0;

		static FSimulationTime FromMinutes(int64 Value);
		static FSimulationTime FromHours(int64 Value);
		static FSimulationTime FromDays(int64 Value);
		FString ToString() const;

		bool operator==(const FSimulationTime& Other) const { return Minutes == Other.Minutes; }
		bool operator<(const FSimulationTime& Other) const { return Minutes < Other.Minutes; }
		bool operator<=(const FSimulationTime& Other) const { return Minutes <= Other.Minutes; }
	};

	class FSimulationClock
	{
	public:
		explicit FSimulationClock(FSimulationTime StartTime = {});

		const FSimulationTime& Now() const { return CurrentTime; }
		bool AdvanceTo(FSimulationTime TargetTime, FString& OutError);

	private:
		FSimulationTime CurrentTime;
	};

	struct FScheduledEvent
	{
		FEventID EventID = 0;
		FArriveID ArriveID = 0;
		FSimulationTime ExecuteAt;
	};

	class FSimulationScheduler
	{
	public:
		FArriveID IssueArriveID();
		bool Schedule(const FScheduledEvent& Event, FSimulationTime CurrentTime, FString& OutError);
		bool RemovePending(FEventID EventID, FScheduledEvent& OutRemoved, FString& OutError);
		void PopDueThrough(FSimulationTime Time, TArray<FScheduledEvent>& OutEvents);
		int32 NumPending() const { return PendingEvents.Num(); }
		FArriveID GetNextArriveID() const { return NextArriveID; }
		const TArray<FScheduledEvent>& GetPendingEvents() const { return PendingEvents; }
		uint64 GetTrackedAllocatedSize() const;

	private:
		FArriveID NextArriveID = 1;
		TArray<FScheduledEvent> PendingEvents;
	};

	enum class ESimulationResource : uint8
	{
		Wood,
		Coin
	};

	struct FResourceAccountKey
	{
		ESimulationResource Resource = ESimulationResource::Wood;
		FString Account;

		bool operator==(const FResourceAccountKey& Other) const
		{
			return Resource == Other.Resource && Account == Other.Account;
		}
	};

	FORCEINLINE uint32 GetTypeHash(const FResourceAccountKey& Key)
	{
		return HashCombine(::GetTypeHash(static_cast<uint8>(Key.Resource)), FCrc::StrCrc32(*Key.Account));
	}

	struct FLedgerTransferRequest
	{
		FIdempotencyKey IdempotencyKey;
		FSimulationTime GameTime;
		ESimulationResource Resource = ESimulationResource::Wood;
		FString Source;
		FString Destination;
		double Quantity = 0.0;
		bool bBoundaryFlow = false;
		FEventID EventID = 0;
		FArriveID ArriveID = 0;
		FPolicyID PolicyID = 0;
	};

	struct FLedgerTransaction
	{
		FTransactionID TransactionID = 0;
		FLedgerTransferRequest Transfer;
	};

	class FResourceLedger
	{
	public:
		bool InitializeAccount(ESimulationResource Resource, const FString& Account, double Quantity, FString& OutError);
		void SealInitialState();
		bool SubmitTransfer(const FLedgerTransferRequest& Request, FTransactionID& OutTransactionID, FString& OutError);
		bool RemoveZeroBalanceAccount(ESimulationResource Resource, const FString& Account, FString& OutError);

		double GetBalance(ESimulationResource Resource, const FString& Account) const;
		double ComputeResidual(ESimulationResource Resource) const;
		int32 CountNegativeStocks() const;
		int32 GetDuplicateTransactionCount() const { return DuplicateTransactionCount; }
		bool HasCommittedIdempotencyKey(const FIdempotencyKey& Key) const
		{
			return CommittedIdempotencyKeys.Contains(Key);
		}
		const TArray<FLedgerTransaction>& GetTransactions() const { return Transactions; }
		const TMap<FResourceAccountKey, double>& GetBalances() const { return Balances; }
		uint64 GetTrackedAllocatedSize() const;

	private:
		static int32 ResourceIndex(ESimulationResource Resource);
		double GetCurrentTotal(ESimulationResource Resource) const;

		bool bInitialStateSealed = false;
		FTransactionID NextTransactionID = 1;
		int32 DuplicateTransactionCount = 0;
		double InitialTotals[2] = {};
		double BoundaryInTotals[2] = {};
		double BoundaryOutTotals[2] = {};
		TMap<FResourceAccountKey, double> Balances;
		TSet<FIdempotencyKey> CommittedIdempotencyKeys;
		TArray<FLedgerTransaction> Transactions;
	};

	enum class EReservationState : uint8
	{
		Active,
		Committed,
		Released,
		Merged
	};

	struct FReservationRequest
	{
		FIdempotencyKey IdempotencyKey;
		FSimulationTime GameTime;
		ESimulationResource Resource = ESimulationResource::Wood;
		FString SourceAccount;
		FString ReservedAccount;
		double Quantity = 0.0;
		FEventID EventID = 0;
		FArriveID ArriveID = 0;
		FPolicyID PolicyID = 0;
	};

	struct FReservationRecord
	{
		FReservationID ReservationID = 0;
		FReservationRequest Request;
		EReservationState State = EReservationState::Active;
	};

	class FReservationStore
	{
	public:
		bool CreateReservation(const FReservationRequest& Request, FResourceLedger& Ledger, FReservationID& OutReservationID, FString& OutError);
		bool CommitReservation(FReservationID ReservationID, const FString& DestinationAccount, const FIdempotencyKey& IdempotencyKey, FSimulationTime GameTime, FResourceLedger& Ledger, FString& OutError);
		bool ReleaseReservation(FReservationID ReservationID, const FIdempotencyKey& IdempotencyKey, FSimulationTime GameTime, FResourceLedger& Ledger, FString& OutError);
		bool SplitReservation(FReservationID ParentReservationID, FEventID ChildEventID, FArriveID ChildArriveID, double Quantity, FReservationID& OutChildReservationID, FString& OutError);
		bool MergeReservations(FReservationID TargetReservationID, FReservationID SourceReservationID, FString& OutError);
		const FReservationRecord* Find(FReservationID ReservationID) const;
		const TMap<FReservationID, FReservationRecord>& GetReservations() const { return Reservations; }
		uint64 GetTrackedAllocatedSize() const;

	private:
		FReservationID NextReservationID = 1;
		TMap<FReservationID, FReservationRecord> Reservations;
	};

	enum class ESimulationEventState : uint8
	{
		Pending,
		Completed
	};

	struct FSimulationEventRequest
	{
		FString Type;
		FString Owner;
		FResidentID ResidentID = 0;
		int32 ActionCode = 0;
		int32 WoodQuantity = 0;
		FSimulationTime StartTime;
		FSimulationTime EndTime;
		FReservationID ReservationID = 0;
		FArriveID ArriveID = 0;
		FEventID ParentEventID = 0;
		int32 ParticipantCount = 0;
		FString Cause;
		FPolicyID PolicyID = 0;
	};

	struct FSimulationEventRecord
	{
		FEventID EventID = 0;
		FSimulationEventRequest Event;
		ESimulationEventState State = ESimulationEventState::Pending;
	};

	class FSimulationEventStore
	{
	public:
		bool CreateEvent(const FSimulationEventRequest& Request, FEventID& OutEventID, FString& OutError);
		bool TransferOwner(FEventID EventID, const FString& ExpectedOwner, const FString& NewOwner, FString& OutError);
		bool ConvertPendingEventToAggregate(FEventID EventID, const FString& ExpectedOwner, const FString& NewOwner, FString& OutError);
		bool ConvertPendingEventToIndividual(FEventID EventID, const FString& ExpectedOwner, const FString& NewOwner, FResidentID ResidentID, FString& OutError);
		bool SetPendingParticipantCount(FEventID EventID, int32 ParticipantCount, FString& OutError);
		bool SetReservationID(FEventID EventID, FReservationID ReservationID, FString& OutError);
		bool CompleteEvent(FEventID EventID, FString& OutError);
		bool RemoveCompletedEvent(FEventID EventID, FString& OutError);
		bool RemovePendingEvent(FEventID EventID, FString& OutError);
		const FSimulationEventRecord* Find(FEventID EventID) const;
		const TMap<FEventID, FSimulationEventRecord>& GetEvents() const { return Events; }
		int32 GetOwnerConflictCount() const { return OwnerConflictCount; }
		int32 GetDuplicateCompletionCount() const { return DuplicateCompletionCount; }
		uint64 GetTrackedAllocatedSize() const;

	private:
		FEventID NextEventID = 1;
		int32 OwnerConflictCount = 0;
		int32 DuplicateCompletionCount = 0;
		TMap<FEventID, FSimulationEventRecord> Events;
	};

	struct FPopulationState
	{
		int32 Total = 0;
		int32 Anonymous = 0;
		int32 PersistentMacro = 0;
		int32 ActiveMicro = 0;
	};

	struct FConservationAudit
	{
		int32 PopulationResidual = 0;
		double WoodResidual = 0.0;
		int32 NegativeStockCount = 0;
		int32 DuplicateTransactionCount = 0;
		int32 EventOwnerConflictCount = 0;
		int32 DuplicateCompletionCount = 0;

		bool IsHardErrorFree() const;
	};

	FConservationAudit AuditConservation(
		const FPopulationState& Population,
		const FResourceLedger& Ledger,
		const FSimulationEventStore& EventStore);

	struct FEmptyScenarioResult
	{
		FSimulationTime FinalTime;
		int32 HourSteps = 0;
		FPopulationState Population;
		FConservationAudit Audit;
	};

	class FEmptyScenarioRunner
	{
	public:
		static bool Run60Days(int32 PopulationPerKingdom, FEmptyScenarioResult& OutResult, FString& OutError);
	};
}
