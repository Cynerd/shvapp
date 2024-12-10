#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "mockrpcconnection.h"
#include "pretty_printers.h"
#include <doctest/doctest.h>

#include "src/appclioptions.h"
#include "src/historyapp.h"
#include "tests/sites.h"
#include "tests/utils.h"

QQueue<std::function<CallNext(MockRpcConnection*)>> setup_test()
{
	QQueue<std::function<CallNext(MockRpcConnection*)>> res;

	// We want the test to download everything regardless of age. Ten years ought to be enough.
	enqueue(res, [=] (MockRpcConnection* mock) {
		HistoryApp::instance()->cliOptions()->setCacheInitMaxAge(60 /*seconds*/ * 60 /*minutes*/ * 24 /*hours*/ * 365 /*days*/ * 10 /*years*/);
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
		return CallNext::Yes;
	});

	auto expected_cache_contents = std::make_shared<RpcValue::List>();
	DOCTEST_SUBCASE("standard operation")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			create_dummy_cache_files("one", {
				{"2022-07-06T18-06-15-000.log2", dummy_logfile},
				{"dirtylog", dummy_logfile2}
			});
			create_dummy_cache_files("two", {
				{"2022-07-06T18-06-15-000.log2", dummy_logfile},
				{"dirtylog", dummy_logfile2}
			});

			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "2022-07-06T18-06-15-000.log2", 308UL },
				RpcValue::List{ "dirtylog", 249UL }
			}});

			REQUIRE(get_cache_contents("one") == *expected_cache_contents);
			REQUIRE(get_cache_contents("two") == *expected_cache_contents);
			REQUEST_YIELD("_shvjournal", "syncLog", synclog_wait("shv/"));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/one/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile2.size() }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/two/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD(RpcValue::List());
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API("shv/one/.app/shvjournal/2022-07-07T18-06-15-557.log2");
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/one/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, dummy_logfile2.size()));
			RESPOND_YIELD(make_read_response(dummy_logfile2));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/one", "shv/two"])"_cpon);

			// two is left intact
			REQUIRE(get_cache_contents("two") == *expected_cache_contents);

			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "2022-07-06T18-06-15-000.log2", 308UL },
				RpcValue::List{ "2022-07-07T18-06-15-557.log2", 249UL },
				RpcValue::List{ "dirtylog", 83UL }
			}});

			REQUIRE(get_cache_contents("one") == *expected_cache_contents);
		});
	}

	DOCTEST_SUBCASE("file to trim from is empty")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			create_dummy_cache_files("one", {
				{"dirtylog", dummy_logfile2}
			});

			REQUEST_YIELD("_shvjournal", "syncLog", synclog_wait("shv/"));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/one/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", 0L }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/two/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD(RpcValue::List());
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API("shv/one/.app/shvjournal/2022-07-07T18-06-15-557.log2");
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/one", "shv/two"])"_cpon);
		});
	}

	DOCTEST_SUBCASE("file to trim from only has one entry")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			create_dummy_cache_files("one", {
				{"dirtylog", dummy_logfile2}
			});

			REQUEST_YIELD("_shvjournal", "syncLog", synclog_wait("shv/"));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/one/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", logfile_one_entry.size() }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/two/.app/shvjournal", "lsfiles", ls_size_true);
			RESPOND_YIELD(RpcValue::List());
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API("shv/one/.app/shvjournal/2022-07-07T18-06-15-557.log2");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/one/.app/shvjournal/2022-07-07T18-06-15-557.log2", "read", read_offset_with_size(0, logfile_one_entry.size()));
			RESPOND_YIELD(make_read_response(logfile_one_entry));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/one", "shv/two"])"_cpon);
			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "2022-07-07T18-06-15-557.log2", 79UL },
				RpcValue::List{ "dirtylog", 231UL }
			}});

			REQUIRE(get_cache_contents("one") == *expected_cache_contents);
		});
	}

	return res;
}

TEST_HISTORYPROVIDER_MAIN("dirtylog_trimming")
