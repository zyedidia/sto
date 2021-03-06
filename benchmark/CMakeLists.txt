add_library(db_index DB_index.cc DB_index.hh)

set(COMMON_HEADERS ../lib/sampling.hh)

add_executable(tpcc_bench TPCC_bench.cc TPCC_structs.hh DB_structs.hh DB_params.hh DB_profiler.hh ${COMMON_HEADERS})
add_executable(ycsb_bench YCSB_bench.cc YCSB_structs.hh DB_structs.hh DB_params.hh DB_profiler.hh ${COMMON_HEADERS})
add_executable(micro_bench MicroBenchmarks.cc Micro_structs.hh ${COMMON_HEADERS})
add_executable(pred_bench Predicate_bench.cc Predicate_bench.hh ${COMMON_HEADERS})
add_executable(wiki_bench Wikipedia_bench.cc Wikipedia_data.cc Wikipedia_bench.hh Wikipedia_txns.hh Wikipedia_structs.hh Wikipedia_loader.hh ${COMMON_HEADERS} Wikipedia_selectors.hh)
add_executable(voter_bench Voter_txns.hh Voter_structs.hh Voter_bench.hh Voter_bench.cc Voter_data.cc ${COMMON_HEADERS})
add_executable(tart_bench TART_bench.hh TART_bench.cc ${COMMON_HEADERS})

target_link_libraries(tpcc_bench db_index sto clp masstree json dprint xxhash ${PLATFORM_LIBRARIES})
target_link_libraries(ycsb_bench db_index sto clp masstree json dprint xxhash ${PLATFORM_LIBRARIES})
target_link_libraries(micro_bench db_index sto clp masstree json dprint ${PLATFORM_LIBRARIES})
target_link_libraries(pred_bench db_index sto clp masstree json dprint ${PLATFORM_LIBRARIES})
target_link_libraries(wiki_bench db_index sto clp masstree json dprint ${PLATFORM_LIBRARIES})
target_link_libraries(voter_bench db_index sto clp masstree json dprint ${PLATFORM_LIBRARIES})
target_link_libraries(tart_bench db_index sto clp masstree json dprint ${PLATFORM_LIBRARIES})
