#pragma once

#include <Core/BackgroundSchedulePool.h>
#include <DataTypes/DataTypeInterval.h>
#include <Interpreters/InterpreterSelectQuery.h>
#include <Storages/IStorage.h>
#include <Poco/Logger.h>
#include <base/shared_ptr_helper.h>

#include <mutex>

namespace DB
{
class IAST;
class WindowViewSource;
using ASTPtr = std::shared_ptr<IAST>;

class StorageWindowView final : public shared_ptr_helper<StorageWindowView>, public IStorage, WithContext
{
    friend struct shared_ptr_helper<StorageWindowView>;
    friend class TimestampTransformation;
    friend class WindowViewSource;
    friend class WatermarkTransform;

public:
    ~StorageWindowView() override;
    String getName() const override { return "WindowView"; }

    bool isView() const override { return true; }
    bool supportsSampling() const override { return true; }
    bool supportsFinal() const override { return true; }

    void checkTableCanBeDropped() const override;

    void dropInnerTableIfAny(bool no_delay, ContextPtr context) override;

    void drop() override;

    void truncate(const ASTPtr &, const StorageMetadataPtr &, ContextPtr, TableExclusiveLockHolder &) override;

    bool optimize(
        const ASTPtr & query,
        const StorageMetadataPtr & metadata_snapshot,
        const ASTPtr & partition,
        bool final,
        bool deduplicate,
        const Names & deduplicate_by_columns,
        ContextPtr context) override;

    void startup() override;
    void shutdown() override;

    Pipe watch(
        const Names & column_names,
        const SelectQueryInfo & query_info,
        ContextPtr context,
        QueryProcessingStage::Enum & processed_stage,
        size_t max_block_size,
        unsigned num_streams) override;

    std::pair<BlocksPtr, Block> getNewBlocks(UInt32 watermark);

    static void writeIntoWindowView(StorageWindowView & window_view, const Block & block, ContextPtr context);

    ASTPtr getMergeableQuery() const { return mergeable_query->clone(); }

private:
    Poco::Logger * log;

    ASTPtr mergeable_query;
    ASTPtr final_query;

    ContextMutablePtr window_view_context;
    bool is_proctime{true};
    bool is_time_column_func_now;
    bool is_tumble; // false if is hop
    std::atomic<bool> shutdown_called{false};
    bool has_inner_table{true};
    mutable Block sample_block;
    mutable Block mergeable_header;
    UInt64 clean_interval_ms;
    const DateLUTImpl * time_zone = nullptr;
    UInt32 max_timestamp = 0;
    UInt32 max_watermark = 0; // next watermark to fire
    UInt32 max_fired_watermark = 0;
    bool is_watermark_strictly_ascending{false};
    bool is_watermark_ascending{false};
    bool is_watermark_bounded{false};
    bool allowed_lateness{false};
    UInt32 next_fire_signal;
    std::deque<UInt32> fire_signal;
    std::list<std::weak_ptr<WindowViewSource>> watch_streams;
    std::condition_variable_any fire_signal_condition;
    std::condition_variable fire_condition;

    /// Mutex for the blocks and ready condition
    std::mutex mutex;
    std::mutex flush_table_mutex;
    std::shared_mutex fire_signal_mutex;
    mutable std::mutex sample_block_lock; /// Mutex to protect access to sample block and inner_blocks_query

    IntervalKind::Kind window_kind;
    IntervalKind::Kind hop_kind;
    IntervalKind::Kind watermark_kind;
    IntervalKind::Kind lateness_kind;
    Int64 window_num_units;
    Int64 hop_num_units;
    Int64 slice_num_units;
    Int64 watermark_num_units = 0;
    Int64 lateness_num_units = 0;
    String window_id_name;
    String window_id_alias;
    String window_column_name;
    String timestamp_column_name;

    StorageID select_table_id = StorageID::createEmpty();
    StorageID target_table_id = StorageID::createEmpty();
    StorageID inner_table_id = StorageID::createEmpty();
    mutable StoragePtr parent_storage;
    mutable StoragePtr inner_storage;
    mutable StoragePtr target_storage;

    BackgroundSchedulePool::TaskHolder clean_cache_task;
    BackgroundSchedulePool::TaskHolder fire_task;

    ASTPtr innerQueryParser(ASTSelectQuery & inner_query);
    void eventTimeParser(const ASTCreateQuery & query);

    std::shared_ptr<ASTCreateQuery> getInnerTableCreateQuery(
        const ASTPtr & inner_query, ASTStorage * storage, const String & database_name, const String & table_name);
    UInt32 getCleanupBound();
    ASTPtr getCleanupQuery();

    UInt32 getWindowLowerBound(UInt32 time_sec);
    UInt32 getWindowUpperBound(UInt32 time_sec);

    void fire(UInt32 watermark);
    void cleanup();
    void threadFuncCleanup();
    void threadFuncFireProc();
    void threadFuncFireEvent();
    void addFireSignal(std::set<UInt32> & signals);
    void updateMaxWatermark(UInt32 watermark);
    void updateMaxTimestamp(UInt32 timestamp);

    ASTPtr getFinalQuery() const { return final_query->clone(); }
    ASTPtr getFetchColumnQuery(UInt32 w_start, UInt32 w_end) const;

    StoragePtr getParentStorage() const;

    StoragePtr getInnerStorage() const;

    StoragePtr getTargetStorage() const;

    Block & getHeader() const;

    StorageWindowView(
        const StorageID & table_id_,
        ContextPtr context_,
        const ASTCreateQuery & query,
        const ColumnsDescription & columns,
        bool attach_);
};
}
