#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "mockrpcconnection.h"
#include "pretty_printers.h"
#include <doctest/doctest.h>

#include "src/appclioptions.h"
#include "src/historyapp.h"
#include "src/shvjournalnode.h"
#include "tests/sites.h"
#include "tests/utils.h"

namespace {
auto assert_sync_info_equal(const RpcValue::Map& a_map, const RpcValue& b)
{
	const auto& b_map = b.asMap();
	REQUIRE(a_map.keys() == b_map.keys());
	for (const auto& kv : a_map) {
		CAPTURE(kv.first);
		REQUIRE(b_map.value(kv.first).asMap().value("status") == kv.second.asMap().value("status"));
	}
}
}

// TODO: add proper testing for file contents (not just sizes)
QQueue<std::function<CallNext(MockRpcConnection*)>> setup_test()
{
	QQueue<std::function<CallNext(MockRpcConnection*)>> res;
	std::string shv_path = "shv/eyas/opc";
	std::string cache_dir_path = "eyas/opc";
	enqueue(res, [=] (MockRpcConnection* mock) {
		// We want the test to download everything regardless of age. Ten years ought to be enough.
		HistoryApp::instance()->cliOptions()->setCacheInitMaxAge(60 /*seconds*/ * 60 /*minutes*/ * 24 /*hours*/ * 365 /*days*/ * 10 /*years*/);
		SEND_SITES_YIELD(mock_sites::fin_slave_broker);
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		DISABLE_TYPEINFO(cache_dir_path);
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		DISABLE_TYPEINFO("eyas/with_app_history");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD(shv_path, "chng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION(shv_path, "cmdlog");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD("shv/eyas/with_app_history", "chng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION("shv/eyas/with_app_history", "cmdlog");
		assert_sync_info_equal(HistoryApp::instance()->shvJournalNode()->syncInfo(), R"({
			"shv/eyas/opc": {"status": ["Unknown"]},
			"shv/eyas/with_app_history": {"status": ["Unknown"]}
		})"_cpon);
		REQUEST_YIELD("_shvjournal", "syncLog", synclog_wait("shv/eyas/opc"));
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		assert_sync_info_equal(HistoryApp::instance()->shvJournalNode()->syncInfo(), R"({
			"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization"]},
			"shv/eyas/with_app_history": {"status": ["Unknown"]}
		})"_cpon);
		EXPECT_REQUEST(join(shv_path, "/.app/shvjournal"), "lsfiles", ls_size_true);
		return CallNext::Yes;
	});

	DOCTEST_SUBCASE("syncLog")
	{
		auto expected_cache_contents = std::make_shared<RpcValue::List>();
		auto expected_sync_info = std::make_shared<RpcValue>();
		DOCTEST_SUBCASE("Remote and local - empty")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_sync_info = R"({
					"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})"_cpon;
				RESPOND_YIELD(RpcValue::List());
			});
		}

		DOCTEST_SUBCASE("Remote - has files, local - empty")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", dummy_logfile.size() }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 308",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: successfully synced",
						"Syncing done"
					]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2");
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, dummy_logfile.size()));
				RESPOND_YIELD(make_read_response(dummy_logfile));
			});
		}

		DOCTEST_SUBCASE("Multiple files without chronological order")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", dummy_logfile.size() },
					RpcValue::List{ "2022-07-08T18-06-15-557.log2", dummy_logfile.size() }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-08T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 308",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: successfully synced",
						"shv/eyas/opc/.app/shvjournal/2022-07-08T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-08T18-06-15-557.log2: got chunk of size: 308",
						"shv/eyas/opc/.app/shvjournal/2022-07-08T18-06-15-557.log2: successfully synced",
						"Syncing done"
					]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				RESPOND_YIELD((RpcValue::List({
					{{ "2022-07-08T18-06-15-557.log2", "f", dummy_logfile.size() }},
					{{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }}
				})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2");
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, dummy_logfile.size()));
				RESPOND_YIELD(make_read_response(dummy_logfile));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-08T18-06-15-557.log2", "read", read_offset_with_size(0, dummy_logfile.size()));
				RESPOND_YIELD(make_read_response(dummy_logfile));
			});
		}

		DOCTEST_SUBCASE("Remote - has files and subdirectories, local - empty")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "subdir/2022-07-07T18-06-15-557.log2", dummy_logfile.size() }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/subdir/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/subdir/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/subdir/2022-07-07T18-06-15-557.log2: got chunk of size: 308",
						"shv/eyas/opc/.app/shvjournal/subdir/2022-07-07T18-06-15-557.log2: successfully synced",
						"Syncing done"
					]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;

				RESPOND_YIELD((RpcValue::List({{
					{ "subdir", "d", 0 }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST(join(shv_path, "/.app/shvjournal/subdir"), "lsfiles", ls_size_true);

				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/subdir/2022-07-07T18-06-15-557.log2");
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/subdir/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, dummy_logfile.size()));
				RESPOND_YIELD(make_read_response(dummy_logfile));
			});
		}

		DOCTEST_SUBCASE("Remote - has one empty file and one non-empty file, local - empty")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-05T18-06-15-557.log2", 0 },
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", dummy_logfile.size() }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-05T18-06-15-557.log2: will sync (remote size: 0 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-05T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-05T18-06-15-557.log2: is an empty file, skipping read(), and creating it locally",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 308",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: successfully synced",
						"Syncing done"]}
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				RESPOND_YIELD((RpcValue::List({{
					RpcValue::List{ "2022-07-05T18-06-15-557.log2", "f", 0ULL },
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				// The empty file is skipped, but it is still used to detect the API.
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-05T18-06-15-557.log2");
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, dummy_logfile.size()));
				RESPOND_YIELD(make_read_response(dummy_logfile));
			});
		}

		DOCTEST_SUBCASE("dirty log")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				// Respond to the initial `lsfiles` request.
				RESPOND_YIELD(RpcValue::List());
			});

			DOCTEST_SUBCASE("HP accepts events and puts them into the log")
			{
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/eyas/opc/power-on", "chng", true);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY("shv/eyas/opc/power-on", "chng", false);
					NOTIFY("shv/eyas/opc/run-command", "cmdlog", false);

					*expected_cache_contents = RpcValue::List({{
						RpcValue::List{ "dirtylog", 152UL }
					}});
					*expected_sync_info = R"({
						"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]}
						"shv/eyas/with_app_history": {"status": ["Unknown"]}
					})"_cpon;
					return CallNext::Yes;
				});
			}
		}

		DOCTEST_SUBCASE("Don't download files older than we already have")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", 0UL },
					RpcValue::List{ "2022-07-07T18-06-15-558.log2", 0UL }
				}});
				*expected_sync_info = R"({
					"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]}
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})"_cpon;
				create_dummy_cache_files(cache_dir_path, {
					{"2022-07-07T18-06-15-557.log2", ""},
					{"2022-07-07T18-06-15-558.log2", ""}
				});
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-000.log2", "f", dummy_logfile.size() }
				}})));
			});
		}

		DOCTEST_SUBCASE("Files with same size remote/local size aren't synced")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", 0UL },
					RpcValue::List{ "2022-07-07T18-06-15-558.log2", 0UL }
				}});
				*expected_sync_info = R"({
					"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-558.log2: up-to-date", "Syncing done"]}
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})"_cpon;
				create_dummy_cache_files(cache_dir_path, {
					{"2022-07-07T18-06-15-557.log2", ""},
					{"2022-07-07T18-06-15-558.log2", ""}
				});
				RESPOND_YIELD((RpcValue::List({{
					{{ "2022-07-07T18-06-15-557.log2", "f", 0UL }},
					{{ "2022-07-07T18-06-15-558.log2", "f", 0UL }}
				}})));
			});
		}

		DOCTEST_SUBCASE("Don't download files older than one month")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				HistoryApp::instance()->cliOptions()->setCacheInitMaxAge(60 /*seconds*/ * 60 /*minutes*/ * 24 /*hours*/ * 30 /*days*/);
				*expected_sync_info = R"({
					"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]}
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})"_cpon;
				create_dummy_cache_files(cache_dir_path, {});
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-000.log2", "f", dummy_logfile.size() }
				}})));
			});
		}

		DOCTEST_SUBCASE("Download files older than cache init threshold if there's a old enough dirtylog entry")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				HistoryApp::instance()->cliOptions()->setCacheInitMaxAge(60 /*seconds*/ * 60 /*minutes*/ * 24 /*hours*/ * 30 /*days*/);
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-000.log2", dummy_logfile.size() },
					RpcValue::List{ "dirtylog", 0UL }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-000.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-000.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-000.log2: got chunk of size: 308",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-000.log2: successfully synced",
						"Syncing done"
					]}
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				create_dummy_cache_files(cache_dir_path, {
					{"dirtylog", "2022-07-07T18-06-14.000Z	809781	zone1/zone/Zone1/plcDisconnected	false		chng	2	\n"}
				});
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-000.log2", "f", dummy_logfile.size() }
				}})));
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-000.log2");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-000.log2", "read", read_offset_with_size(0, dummy_logfile.size()));
				RESPOND_YIELD(make_read_response(dummy_logfile));
			});
		}

		DOCTEST_SUBCASE("Chunk downloading")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", very_large_log_file.size() }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 4000000 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 128000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 32000",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: successfully synced",
						"Syncing done"
					]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-557.log2", "f", very_large_log_file.size() }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2");
			});

			for (auto i = 0; i < 31; i++) {
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(i * 128000, 128000));
					RESPOND_YIELD(make_read_response(very_large_log_file, i * 128000, 128000));
				});
			}

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(31 * 128000, 32000));
				RESPOND_YIELD(make_read_response(very_large_log_file, 31 * 128000, 32000));
			});
		}

		DOCTEST_SUBCASE("Remote sends less data")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "2022-07-07T18-06-15-557.log2", dummy_logfile.size() }
				}});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 150",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 158",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: successfully synced",
						"Syncing done"
					]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2");
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, 308));
				RESPOND_YIELD(make_read_response(dummy_logfile, 0, 150));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(150, 158));
				RESPOND_YIELD(make_read_response(dummy_logfile, 150, 158));
			});
		}

		DOCTEST_SUBCASE("Remote sends more data")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_cache_contents = RpcValue::List({
					// The file won't get saved.
				});
				*expected_sync_info = R"EOF({
					"shv/eyas/opc": {"status": [
						"Syncing shv/eyas/opc via file synchronization",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
						"shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2: got invalid offset or more data than requested, got offset: 0 expected offset: 0 got size: 4000000 expected size: 308",
						"Skipping all files from shv/eyas/opc/.app/shvjournal/ because shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2 had an expected offset and/or size",
						"Syncing done"
					]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})EOF"_cpon;
				RESPOND_YIELD((RpcValue::List({{
					{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
				}})));
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_MAP_FILE_API("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2");
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, 308));
				RESPOND_YIELD(make_read_response(very_large_log_file));
			});
		}

		DOCTEST_SUBCASE("Don't synclog when already in progress")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files(cache_dir_path, {});
				*expected_sync_info = R"({
					"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]},
					"shv/eyas/with_app_history": {"status": ["Unknown"]}
				})"_cpon;
				REQUEST("_shvjournal", "syncLog", synclog_wait("shv/eyas/opc"));
				RESPOND_YIELD(RpcValue::List());
			});
		}

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/eyas/opc"])"_cpon);
			REQUIRE(get_cache_contents(cache_dir_path) == *expected_cache_contents);
			assert_sync_info_equal(HistoryApp::instance()->shvJournalNode()->syncInfo(), *expected_sync_info);
		});
	}

	DOCTEST_SUBCASE(".app/history")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			// Respond to the initial syncLog request.
			RESPOND_YIELD(RpcValue::List());
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/eyas/opc"])"_cpon);
			REQUEST_YIELD("_shvjournal", "syncLog", synclog_wait("shv/eyas/with_app_history"));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			assert_sync_info_equal(HistoryApp::instance()->shvJournalNode()->syncInfo(), R"({
				"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]},
				"shv/eyas/with_app_history": {"status": ["Syncing shv/eyas/with_app_history via file synchronization"]}
			})"_cpon);
			EXPECT_REQUEST("shv/eyas/with_app_history/.app/history", "lsfiles", ls_size_true);
			RESPOND_YIELD(RpcValue::List());
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/eyas/with_app_history"])"_cpon);
			assert_sync_info_equal(HistoryApp::instance()->shvJournalNode()->syncInfo(), R"({
				"shv/eyas/opc": {"status": ["Syncing shv/eyas/opc via file synchronization", "Syncing done"]},
				"shv/eyas/with_app_history": {"status": ["Syncing shv/eyas/with_app_history via file synchronization", "Syncing done"]}
			})"_cpon);
		});
	}

	DOCTEST_SUBCASE("periodic syncLog")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			// Respond to the initial syncLog request.
			RESPOND_YIELD(RpcValue::List());
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/eyas/opc"])"_cpon);
			return CallNext::Yes;
		});

		DOCTEST_SUBCASE("dirty log too old")
		{
			enqueue(res, [=] (MockRpcConnection*) {
				create_dummy_cache_files(cache_dir_path, {
					// Make a 100 second old entry.
					{"dirtylog", QDateTime::currentDateTimeUtc().addSecs(-100).toString(Qt::DateFormat::ISODate).toStdString() + "	809781	zone1/zone/Zone1/plcDisconnected	false		chng	2	\n"}
				});
				return CallNext::Yes;
			});

			DOCTEST_SUBCASE("synclog should not trigger")
			{
				enqueue(res, [=] (MockRpcConnection* mock) {
					HistoryApp::instance()->cliOptions()->setLogMaxAge(1000);
					NOTIFY_YIELD("shv/eyas/opc/power-on", "chng", true); // Send an event so that HP checks for dirty log age.
					// Nothing happens afterwards, since max age is >10
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("eyas/opc", "onlinestatuschng", 2);
				});
			}
		}

		DOCTEST_SUBCASE("device mounted")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				NOTIFY_YIELD(shv_path, "mntchng", true);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST(join(shv_path, "/.app/shvjournal"), "lsfiles", ls_size_true);
				RESPOND_YIELD(RpcValue::List()); // We only test if the syncLog triggers.
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL(shv_path, "logReset", RpcValue());
			});
		}

		DOCTEST_SUBCASE("device unmounted")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				NOTIFY(shv_path, "mntchng", false);
				// Sync shouldn't trigger.
				DRIVER_WAIT(100);
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				CAPTURE(mock->m_messageQueue.head());
				REQUIRE(mock->m_messageQueue.empty());
			});
		}

		DOCTEST_SUBCASE("parent path of device mounted")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				NOTIFY_YIELD("shv", "mntchng", true);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST(join(shv_path, "/.app/shvjournal"), "lsfiles", ls_size_true);
				RESPOND_YIELD(RpcValue::List()); // We only test if the syncLog triggers.
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/eyas/with_app_history/.app/history", "lsfiles", ls_size_true);
				RESPOND_YIELD(RpcValue::List()); // We only test if the syncLog triggers.
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL(shv_path, "logReset", RpcValue());
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("shv/eyas/with_app_history", "logReset", RpcValue());
			});
		}

		DOCTEST_SUBCASE("parent path of more than one device mounted")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				mock->setBrokerConnected(false);
				QTimer::singleShot(0, mock, [mock] {mock->setBrokerConnected(true);});
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_SITES_YIELD(mock_sites::two_devices);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				DISABLE_TYPEINFO("one");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				DISABLE_TYPEINFO("two");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/one", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/one", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/two", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/two", "cmdlog");
				NOTIFY_YIELD("shv", "mntchng", true);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST(join("shv/one", "/.app/shvjournal"), "lsfiles", ls_size_true);
				RESPOND_YIELD(RpcValue::List()); // We only test if the syncLog triggers.
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST(join("shv/two", "/.app/shvjournal"), "lsfiles", ls_size_true);
				RESPOND_YIELD(RpcValue::List()); // We only test if the syncLog triggers.
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("shv/one", "logReset", RpcValue());
			});
			// I can only handle one message every coroutine run, which means that I have to handle add a sseparate
			// lambda here.
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("shv/two", "logReset", RpcValue());
			});
		}

	}
	DOCTEST_SUBCASE("syncLog/syncInfo on specific site subtree")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			// Respond to the initial syncLog request.
			RESPOND_YIELD(RpcValue::List());
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/eyas/opc"])"_cpon);
			return CallNext::Yes;
		});

		auto expected_cache_contents = std::make_shared<RpcValue::List>();
		enqueue(res, [=] (MockRpcConnection* mock) {
			mock->setBrokerConnected(false);
			QTimer::singleShot(0, mock, [mock] {mock->setBrokerConnected(true);});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			SEND_SITES_YIELD(mock_sites::even_more_devices);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("one");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("two");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/mil014atm", "chng");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/mil014atm", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/one", "chng");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/one", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/two", "chng");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/two", "cmdlog");
			return CallNext::Yes;
		});

		DOCTEST_SUBCASE("just one site")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				REQUEST_YIELD("_shvjournal", "syncLog", R"({"shvPath": "shv/two", "waitForFinished": true})"_cpon);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/two/.app/shvjournal", "lsfiles", ls_size_true);
				create_dummy_cache_files(cache_dir_path, {});
				RESPOND_YIELD(RpcValue::List());
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_RESPONSE(R"(["shv/two"])"_cpon);
				REQUIRE(get_cache_contents(cache_dir_path) == *expected_cache_contents);
			});
		}

		DOCTEST_SUBCASE("just one which is a slave HP")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				REQUEST_YIELD("_shvjournal", "syncLog", R"({"shvPath": "shv/mil014atm", "waitForFinished": true})"_cpon);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/mil014atm/.local/history/_shvjournal", "lsfiles", ls_size_true);
				create_dummy_cache_files(cache_dir_path, {});
				RESPOND_YIELD(RpcValue::List());
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_RESPONSE(R"(["shv/mil014atm"])"_cpon);
				REQUIRE(get_cache_contents(cache_dir_path) == *expected_cache_contents);
			});
		}

		DOCTEST_SUBCASE("shv subtree")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				REQUEST_YIELD("_shvjournal", "syncLog", R"({"shvPath": "shv", "waitForFinished": true})"_cpon);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/mil014atm/.local/history/_shvjournal", "lsfiles", ls_size_true);
				create_dummy_cache_files(cache_dir_path, {});
				RESPOND_YIELD(RpcValue::List());
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/one/.app/shvjournal", "lsfiles", ls_size_true);
				create_dummy_cache_files(cache_dir_path, {});
				RESPOND_YIELD(RpcValue::List());
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_REQUEST("shv/two/.app/shvjournal", "lsfiles", ls_size_true);
				create_dummy_cache_files(cache_dir_path, {});
				RESPOND_YIELD(RpcValue::List());
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_RESPONSE(R"(["shv/mil014atm", "shv/one", "shv/two"])"_cpon);
				REQUIRE(get_cache_contents(cache_dir_path) == *expected_cache_contents);
			});
		}

		DOCTEST_SUBCASE("syncInfo")
		{
			std::string filter;
			std::vector<std::string> expected_keys;
			DOCTEST_SUBCASE("shv")
			{
				filter = "shv";
				expected_keys = {
					"shv/mil014atm",
					"shv/one",
					"shv/two",
				};
			}

			DOCTEST_SUBCASE("shv/one")
			{
				filter = "shv/one";
				expected_keys = {
					"shv/one",
				};
			}

			DOCTEST_SUBCASE("shv/one")
			{
				filter = "shv/one";
				expected_keys = {
					"shv/one",
				};
			}

			enqueue(res, [=] (MockRpcConnection* mock) {
				REQUEST_YIELD("_shvjournal", "syncInfo", filter);
			});

			enqueue(res, [=] (MockRpcConnection* mock) {
				REQUIRE(shv::chainpack::RpcResponse(mock->m_messageQueue.head()).result().asMap().keys() == expected_keys);
				mock->m_messageQueue.dequeue();
			});
		}
	}

	DOCTEST_SUBCASE("custom fileChunkLimit")
	{
		auto expected_sync_info = std::make_shared<RpcValue>();
		enqueue(res, [=] (MockRpcConnection* mock) {
			// Respond to the initial syncLog request.
			RESPOND_YIELD(RpcValue::List());
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/eyas/opc"])"_cpon);
			return CallNext::Yes;
		});

		auto expected_cache_contents = std::make_shared<RpcValue::List>();
		enqueue(res, [=] (MockRpcConnection* mock) {
			mock->setBrokerConnected(false);
			QTimer::singleShot(0, mock, [mock] {mock->setBrokerConnected(true);});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			SEND_SITES_YIELD(mock_sites::site_with_custom_chunk_limit);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("some_site");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			REQUEST_YIELD("_shvjournal", "syncLog", R"({"shvPath": "shv/some_site", "waitForFinished": true})"_cpon);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "2022-07-07T18-06-15-557.log2", dummy_logfile.size() }
			}});
			*expected_sync_info = R"EOF({
				"shv/some_site": {"status": [
					"Syncing shv/some_site via file synchronization",
					"shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2: will sync (remote size: 308 local size: <doesn't exist>)",
					"shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2: starting to sync",
					"shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 149",
					"shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 149",
					"shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2: got chunk of size: 10",
					"shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2: successfully synced",
					"Syncing done"
				]}
			})EOF"_cpon;
			EXPECT_REQUEST("shv/some_site/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List({{
				{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
			}})));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API("shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2");
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, 149));
			RESPOND_YIELD(make_read_response(dummy_logfile, 0, 149));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(149, 149));
			RESPOND_YIELD(make_read_response(dummy_logfile, 149, 149));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/some_site/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(298, 10));
			RESPOND_YIELD(make_read_response(dummy_logfile, 298, 10));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/some_site"])"_cpon);
			REQUIRE(get_cache_contents("some_site") == *expected_cache_contents);
			assert_sync_info_equal(HistoryApp::instance()->shvJournalNode()->syncInfo(), *expected_sync_info);
		});
	}
	return res;
}

TEST_HISTORYPROVIDER_MAIN("synclog")
