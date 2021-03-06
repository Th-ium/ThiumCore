// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#pragma once

#include "catchup/CatchupConfiguration.h"
#include "catchup/VerifyLedgerChainWork.h"
#include "history/HistoryArchive.h"
#include "historywork/GetHistoryArchiveStateWork.h"
#include "ledger/LedgerRange.h"
#include "work/Work.h"
#include "work/WorkSequence.h"

namespace stellar
{

class HistoryManager;
class Bucket;
class TmpDir;
struct LedgerRange;
struct CheckpointRange;

// Range required to do a catchup.
//
// For initial catchup (after new-db) we have lastClosedLedger ==
// LedgerManager.GENESIS_LEDGER_SEQ. In that case CatchupConfiguration::count()
// and CatchupConfiguration::toLedger() are taken into consideration. Depending
// on all of those values only one of "apply buckets" and "apply transactions"
// can be executed, or both of them. The values are calculated in such a way
// that transactions from at least count() ledgers are available in txhistory
// table.
//
// If mApplyBuckets is true, this catchup requires downloading and applying
// buckets for the getBucketApplyLedger() (which is equal to
// mApplyLedgers.mFirst - 1).
//
// Then all ledgers in range mApplyLedgers are downloaded and applied.
//
struct CatchupRange final
{
    struct Ledgers
    {
        uint32_t const mFirst;
        uint32_t const mCount;
    };

    Ledgers mLedgers;
    bool const mApplyBuckets;

    /**
     * Preconditions:
     * * lastClosedLedger > 0
     * * configuration.toLedger() > lastClosedLedger
     * * configuration.toLedger() != CatchupConfiguration::CURRENT
     */
    explicit CatchupRange(uint32_t lastClosedLedger,
                          CatchupConfiguration const& configuration,
                          HistoryManager const& historyManager);

    bool
    applyLedgers() const
    {
        return mLedgers.mCount > 0;
    }

    uint32_t getLast() const;
    uint32_t getBucketApplyLedger() const;
};
using WorkSeqPtr = std::shared_ptr<WorkSequence>;

// CatchupWork does all the neccessary work to perform any type of catchup.
// It accepts CatchupConfiguration structure to know from which ledger to which
// one do the catchup and if it involves only applying ledgers or ledgers and
// buckets.
//
// First thing it does is to get a history state which allows to calculate
// proper destination ledger (in case CatchupConfiguration::CURRENT) was used
// and to get list of buckets that should be in database on that ledger.
//
// Next step is downloading and verifying ledgers (if verifyMode is set to
// VERIFY_BUFFERED_LEDGERS it can also verify against ledgers currently
// buffered in LedgerManager).
//
// Then, depending on configuration, it can download, verify and apply buckets
// (as in MINIMAL and RECENT catchups), and then download and apply
// transactions (as in COMPLETE and RECENT catchups).
//
// After that, catchup is done and node can replay buffered ledgers and take
// part in consensus protocol.

class CatchupWork : public Work
{
  protected:
    HistoryArchiveState mLocalState;
    std::unique_ptr<TmpDir> mDownloadDir;
    std::map<std::string, std::shared_ptr<Bucket>> mBuckets;

    void doReset() override;
    BasicWork::State doWork() override;
    void onFailureRaise() override;
    void onSuccess() override;

  public:
    // Resume application when publish queue shrinks down to this many
    // checkpoints
    static uint32_t const PUBLISH_QUEUE_UNBLOCK_APPLICATION;

    // Allow at most this many checkpoints in the publish queue while catching
    // up. If the queue grows too big, ApplyCheckpointWork will wait until
    // enough snapshots were published, and unblock itself.
    static uint32_t const PUBLISH_QUEUE_MAX_SIZE;

    CatchupWork(Application& app, CatchupConfiguration catchupConfiguration,
                std::shared_ptr<HistoryArchive> archive = nullptr);
    virtual ~CatchupWork();
    std::string getStatus() const override;

  private:
    LedgerNumHashPair mLastClosedLedgerHashPair;
    CatchupConfiguration const mCatchupConfiguration;
    LedgerHeaderHistoryEntry mVerifiedLedgerRangeStart;
    LedgerHeaderHistoryEntry mLastApplied;
    std::shared_ptr<HistoryArchive> mArchive;
    bool mBucketsAppliedEmitted{false};
    bool mTransactionsVerifyEmitted{false};

    std::shared_ptr<GetHistoryArchiveStateWork> mGetHistoryArchiveStateWork;
    std::shared_ptr<GetHistoryArchiveStateWork> mGetBucketStateWork;

    WorkSeqPtr mDownloadVerifyLedgersSeq;
    std::shared_ptr<VerifyLedgerChainWork> mVerifyLedgers;
    std::shared_ptr<Work> mVerifyTxResults;
    WorkSeqPtr mBucketVerifyApplySeq;
    std::shared_ptr<Work> mTransactionsVerifyApplySeq;
    std::shared_ptr<BasicWork> mApplyBufferedLedgersWork;
    WorkSeqPtr mCatchupSeq;

    std::shared_ptr<BasicWork> mCurrentWork;

    bool hasAnyLedgersToCatchupTo() const;
    bool alreadyHaveBucketsHistoryArchiveState(uint32_t atCheckpoint) const;
    void assertBucketState();

    void downloadVerifyLedgerChain(CatchupRange const& catchupRange,
                                   LedgerNumHashPair rangeEnd);
    WorkSeqPtr downloadApplyBuckets();
    void downloadApplyTransactions(CatchupRange const& catchupRange);
    void downloadVerifyTxResults(CatchupRange const& catchupRange);
    BasicWork::State runCatchupStep();
};
}
