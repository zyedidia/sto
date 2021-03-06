#include <thread>

#include "DB_index.hh"
#include "TART_index.hh"
#include "TBox.hh"
#include "TCounter.hh"
#include "DB_params.hh"

namespace tart_bench {

typedef std::pair<const char*, size_t> tart_key;
typedef uintptr_t tart_row;

struct oi_value {
    enum class NamedColumn : int { val = 0 };
    uintptr_t val;

    oi_value(uintptr_t v) {
        val = v;
    }
};

struct oi_key {
    uint64_t key;
    oi_key(uint64_t k) {
        key = k;
    }
    bool operator==(const oi_key& other) const {
        return (key == other.key);
    }
    bool operator!=(const oi_key& other) const {
        return !(*this == other);
    }
    operator lcdf::Str() const {
        return lcdf::Str((const char *)this, sizeof(*this));
    }
};

template <typename DBParams>
class tart_db {
public:
    template <typename K, typename V>
    using TIndex = bench::ordered_index<K, V, DBParams>;
    typedef TIndex<oi_key, oi_value> table_type;

    table_type& table() {
        return tbl_;
    }
    size_t size() const {
        return size_;
    }
    size_t& size() {
        return size_;
    }
private:
    size_t size_;
    table_type tbl_;
};

template <typename DBParams>
void initialize_db(tart_db<DBParams>& db, size_t db_size) {
    //db.table().thread_init();
    for (size_t i = 0; i < db_size; i++)
        db.table().nontrans_put(oi_key(i), new oi_value{rand()});
    db.size() = db_size;
}

template <typename DBParams>
class tart_runner {
public:
    using RowAccess = bench::RowAccess;
    void run_txn(size_t key) {
        TRANSACTION_E {
            bool success, found;
            oi_value* value;
            std::tie(success, found, std::ignore, value) = db.table().select_row(oi_key(key), RowAccess::None);
            if (success) {
                if (found) {
                  std::tie(success, found) = db.table().delete_row(oi_key(key));
                } else {
                  std::tie(success, found) = db.table().insert_row(oi_key(key), *Sto::tx_alloc<tart_row>());
                }
            }
        } RETRY_E(true);
    }

    // returns the total number of transactions committed
    size_t run() {
        std::vector<std::thread> thrs;
        std::vector<size_t> txn_cnts((size_t)num_runners, 0);
        for (int i = 0; i < num_runners; ++i)
            thrs.emplace_back(
                &tart_runner::runner_thread, this, i, std::ref(txn_cnts[i]));
        for (auto& t : thrs)
            t.join();
        size_t combined_txn_count = 0;
        for (auto c : txn_cnts)
            combined_txn_count += c;
        return combined_txn_count;
    }

    tart_runner(int nthreads, double time_limit, tart_db<DBParams>& database)
        : num_runners(nthreads), tsc_elapse_limit(), db(database) {
        using db_params::constants;
        tsc_elapse_limit = (uint64_t)(time_limit * constants::processor_tsc_frequency * constants::billion);
    }

private:
    void runner_thread(int runner_id, size_t& committed_txns) {
        ::TThread::set_id(runner_id);
        //db.table().thread_init();
        std::mt19937 gen(runner_id);
        std::uniform_int_distribution<uint64_t> dist(0, db.size() - 1);

        size_t thread_txn_count = 0;
        auto tsc_begin = read_tsc();
        size_t key = dist(gen) % db.size();
        while (true) {
            run_txn(key);
            key = (key + 1) % db.size();
            ++thread_txn_count;
            if ((thread_txn_count & 0xfful) == 0) {
                if (read_tsc() - tsc_begin >= tsc_elapse_limit)
                    break;
            }
        }

        committed_txns = thread_txn_count;
    }

    int num_runners;
    uint64_t tsc_elapse_limit;
    tart_db<DBParams> db;
};

};
