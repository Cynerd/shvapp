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
	std::string cache_dir_path = "fin/hel/tram/hel002";
	std::string master_shv_journal_path = "_shvjournal";
	std::string slave_shv_journal_path = "shv/fin/hel/tram/hel002/.local/history/_shvjournal";
	enqueue(res, [=] (MockRpcConnection* mock) {
		HistoryApp::instance()->cliOptions()->setCacheInitMaxAge(60 /*seconds*/ * 60 /*minutes*/ * 24 /*hours*/ * 365 /*days*/ * 10 /*years*/);
		SEND_SITES_YIELD(mock_sites::fin_master_broker);
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		DISABLE_TYPEINFO("fin/hel/tram/hel002/eyas/opc");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD("shv/fin/hel/tram/hel002", "chng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION("shv/fin/hel/tram/hel002", "cmdlog");
		REQUEST_YIELD(master_shv_journal_path, "syncLog", synclog_wait("shv/fin/hel/tram/hel002"));
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_REQUEST(slave_shv_journal_path, "lsfiles", ls_size_true);
		return CallNext::Yes;
	});

	auto expected_cache_contents = std::make_shared<RpcValue::List>();
	DOCTEST_SUBCASE("Remote - has files, local - empty")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			create_dummy_cache_files(cache_dir_path, {});
			// NOTE: I'm only really testing whether historyprovider correctly enters the .local broker.
			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "eyas/opc/2022-07-07T18-06-15-557.log2", dummy_logfile.size() }
			}});
			RESPOND_YIELD((RpcValue::List{{
				{ "eyas", "d", 0 }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "opc", "d", 0 }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/opc"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
			}}));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API(join(slave_shv_journal_path, "eyas/opc/2022-07-07T18-06-15-557.log2"));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/opc/2022-07-07T18-06-15-557.log2"), "read", read_offset_with_size(0, dummy_logfile.size()));
			RESPOND_YIELD(make_read_response(dummy_logfile));
		});
	}

	DOCTEST_SUBCASE("more sites and one file download fails")
	{
		// Should skip all the files from the failed site.
		enqueue(res, [=] (MockRpcConnection* mock) {
			create_dummy_cache_files(cache_dir_path, {});
			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "eyas/app/2022-07-07T18-06-15-557.log2", dummy_logfile.size() },
			}});
			RESPOND_YIELD((RpcValue::List{{
				{ "eyas", "d", 0 }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{
				{{ "opc", "d", 0 }},
				{{ "app", "d", 0 }}
			}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/opc"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }},
				{{ "2022-07-08T18-06-15-557.log2", "f", dummy_logfile.size() }}
			}}));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/app"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile.size() }
			}}));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API(join(slave_shv_journal_path, "eyas/opc/2022-07-07T18-06-15-557.log2"));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/opc/2022-07-07T18-06-15-557.log2"), "read", read_offset_with_size(0, dummy_logfile.size()));
			RESPOND_ERROR_YIELD("Couldn't send this file.");
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/app/2022-07-07T18-06-15-557.log2"), "read", read_offset_with_size(0, dummy_logfile.size()));
			RESPOND_YIELD(make_read_response(dummy_logfile));
		});
	}

	DOCTEST_SUBCASE("trimming dirtylog")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			create_dummy_cache_files(cache_dir_path, {
				{"eyas/opc/2022-07-06T18-06-15-000.log2", dummy_logfile},
				{"eyas/opc/dirtylog", dummy_logfile2},
				{"eyas/app/2022-07-06T18-06-15-000.log2", dummy_logfile},
				{"eyas/app/dirtylog", dummy_logfile2}
			});

			*expected_cache_contents = RpcValue::List({{
				RpcValue::List{ "eyas/app/2022-07-06T18-06-15-000.log2", 308UL },
				RpcValue::List{ "eyas/app/2022-07-07T18-06-15-557.log2", 249UL },
				RpcValue::List{ "eyas/app/dirtylog", 83UL },
				RpcValue::List{ "eyas/opc/2022-07-06T18-06-15-000.log2", 308UL },
				RpcValue::List{ "eyas/opc/2022-07-07T18-06-15-557.log2", 249UL },
				RpcValue::List{ "eyas/opc/dirtylog", 83UL }
			}});

			RESPOND_YIELD((RpcValue::List{{
				{ "eyas", "d", 0 }
			}}));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{
				{{ "opc", "d", 0 }},
				{{ "app", "d", 0 }}
			}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/opc"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile2.size() }
			}}));
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/app"), "lsfiles", ls_size_true);
			RESPOND_YIELD((RpcValue::List{{
				{ "2022-07-07T18-06-15-557.log2", "f", dummy_logfile2.size() }
			}}));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			ENABLE_MAP_FILE_API(join(slave_shv_journal_path, "eyas/opc/2022-07-07T18-06-15-557.log2"));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/opc/2022-07-07T18-06-15-557.log2"), "read", read_offset_with_size(0, dummy_logfile2.size()));
			RESPOND_YIELD(make_read_response(dummy_logfile2));
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST(join(slave_shv_journal_path, "eyas/app/2022-07-07T18-06-15-557.log2"), "read", read_offset_with_size(0, dummy_logfile2.size()));
			RESPOND_YIELD(make_read_response(dummy_logfile2));
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
				NOTIFY("shv/fin/hel/tram/hel002/eyas/opc/power-on", "chng", true);
				NOTIFY("shv/fin/hel/tram/hel002/eyas/opc/power-on", "chng", false);

				*expected_cache_contents = RpcValue::List({{
					RpcValue::List{ "eyas/opc/dirtylog", 99UL }
				}});
				return CallNext::Yes;
			});
		}

		DOCTEST_SUBCASE("HP discards events from site nodes it doesn't know")
		{
			enqueue(res, [=] (MockRpcConnection* mock) {
				NOTIFY("shv/fin/hel/tram/hel002/some/unknown/leaf", "chng", true);

				*expected_cache_contents = RpcValue::List();
				return CallNext::Yes;
			});
		}
	}

	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_RESPONSE(R"(["shv/fin/hel/tram/hel002"])"_cpon);
		REQUIRE(get_cache_contents(cache_dir_path) == *expected_cache_contents);
	});

	return res;
}

TEST_HISTORYPROVIDER_MAIN("slave_hp")
