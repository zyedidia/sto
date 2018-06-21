#include "clp.h"

#include "DB_profiler.hh"
#include "PlatformFeatures.hh"
#include "TART_bench.hh"

double db_params::constants::processor_tsc_frequency;

enum { opt_nthrs = 1, opt_time };

struct cmd_params {
    int num_threads;
    double time_limit;

    cmd_params() : num_threads(1), time_limit(10.0) {}
};

static const Clp_Option options[] = {
    { "nthreads", 't', opt_nthrs, Clp_ValInt, Clp_Optional },
    { "time", 'l', opt_time, Clp_ValDouble, Clp_Optional },
};

int main(int argc, const char * const *argv) {
    typedef db_params::db_default_params params;
    typedef tart_bench::tart_runner<params> r_type_nopred;
    typedef tart_bench::tart_db<params> db_type_nopred;

    using tart_bench::initialize_db;

    cmd_params p;

    Clp_Parser *clp = Clp_NewParser(argc, argv, arraysize(options), options);
    int ret_code = 0;
    int opt;
    bool clp_stop = false;
    while (!clp_stop && ((opt = Clp_Next(clp)) != Clp_Done)) {
        switch (opt) {
        case opt_nthrs:
            p.num_threads = clp->val.i;
            break;
        case opt_time:
            p.time_limit = clp->val.d;
            break;
        default:
            ret_code = 1;
            clp_stop = true;
            break;
        }
    }
    Clp_DeleteParser(clp);
    if (ret_code != 0)
        return ret_code;

    auto freq = determine_cpu_freq();
    if (freq ==  0.0)
        return -1;
    db_params::constants::processor_tsc_frequency = freq;

    db_type_nopred db_nopred;
    const size_t db_size = 256;
    initialize_db(db_nopred, db_size);

    bench::db_profiler prof(false/*don't spawn perf*/);
    auto nthreads = p.num_threads;
    auto time_limit = p.time_limit;
    std::cout << "Number of threads: " << nthreads << std::endl;

    r_type_nopred r_nopred(nthreads, time_limit, db_nopred);

    size_t ncommits;

    pthread_t advancer;
    pthread_create(&advancer, NULL, Transaction::epoch_advancer, NULL);
    pthread_detach(advancer);

    prof.start(Profiler::perf_mode::record);
    ncommits = r_nopred.run();
    prof.finish(ncommits);

    auto counters = Transaction::txp_counters_combined();
    auto ndreq = counters.p(txp_rcu_del_req);
    auto ndareq = counters.p(txp_rcu_delarr_req);
    auto nfreq = counters.p(txp_rcu_free_req);
    auto ndimpl = counters.p(txp_rcu_del_impl);
    auto ndaimpl = counters.p(txp_rcu_delarr_impl);
    auto nfimpl = counters.p(txp_rcu_free_impl);

    auto total_reqs = ndreq + ndareq + nfreq;
    auto total_impls = ndimpl + ndaimpl + nfimpl;

    printf("STO profile counters: %d\n", STO_PROFILE_COUNTERS);
    printf("RCU dealloc requests: %llu\n", total_reqs);
    printf("dealloc reqs/commit:  %.2lf\n", 1. * total_reqs / ncommits);
    printf("RCU dealloc calls:    %llu\n", total_impls);
    printf("dealloc calls/commit: %.2lf\n", 1. * total_impls / ncommits);

    return 0;
}
