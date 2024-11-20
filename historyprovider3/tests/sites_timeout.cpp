#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "mockrpcconnection.h"
#include "pretty_printers.h"
#include <doctest/doctest.h>

#include "src/appclioptions.h"
#include "src/historyapp.h"
#include "tests/sites.h"
#include "tests/utils.h"

#include <shv/core/utils/shvjournalentry.h>
#include <shv/core/utils/shvmemoryjournal.h>

#include <QDir>

namespace cp = shv::chainpack;
using cp::RpcValue;
#define MESSAGE_HANDLER enqueue(res, [] (MockRpcConnection* mock)

QQueue<std::function<CallNext(MockRpcConnection*)>> setup_test()
{
	QQueue<std::function<CallNext(MockRpcConnection*)>> res;
	DOCTEST_SUBCASE("sites request times out")
	{
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("sites", "getSites", RpcValue());
			mock->m_messageQueue.dequeue();
			DRIVER_WAIT(10000);
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("sites", "getSites", RpcValue());
			RESPOND_YIELD(mock_sites::some_site); // Respond with empty sites to avoid leaks.
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("some_site");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
		});
	}

	DOCTEST_SUBCASE("manual request to reload sites")
	{
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("sites", "getSites", RpcValue());
			RESPOND_YIELD(mock_sites::some_site);
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("some_site");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			REQUEST_YIELD("", "reloadSites");
		});

		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("sites", "getSites", RpcValue());
			return CallNext::Yes;
		});

		DOCTEST_SUBCASE("normal operation")
		{
			enqueue(res, [] (MockRpcConnection* mock) {
				RESPOND_YIELD(mock_sites::some_site);
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				DISABLE_TYPEINFO("some_site");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
		}

		DOCTEST_SUBCASE("reloadSites while already reloading produces error")
		{
			enqueue(res, [] (MockRpcConnection* mock) {
				REQUEST_YIELD("", "reloadSites");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				RESPOND_YIELD(mock_sites::some_site);
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_ERROR("MethodCallException: Sites are already being reloaded.");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				DISABLE_TYPEINFO("some_site");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
				return CallNext::Yes;
			});
		}

		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("Sites reloaded.");
		});
	}

	DOCTEST_SUBCASE("reload sites while legacy getlog is in progress")
	{
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("sites", "getSites", RpcValue());
			RESPOND_YIELD(mock_sites::legacy_hp);
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("legacy");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv", "mntchng");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/legacy", "chng");
		  });
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/legacy", "cmdlog");
			REQUEST_YIELD("_shvjournal", "syncLog", synclog_wait("shv/legacy"));
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/legacy", "getLog");
			// Let's not give any response, so that the legacy synclog is still in progress when use reloadSites
			mock->m_messageQueue.dequeue();
			REQUEST_YIELD("", "reloadSites");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_RESPONSE(R"(["shv/legacy"])"_cpon);
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_REQUEST("sites", "getSites", RpcValue());
			RESPOND_YIELD(mock_sites::legacy_hp);
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			DISABLE_TYPEINFO("legacy");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv", "mntchng");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/legacy", "chng");
		  });
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_SUBSCRIPTION("shv/legacy", "cmdlog");
		});
		enqueue(res, [] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("Sites reloaded.");
		});
	}

	return res;
}

TEST_HISTORYPROVIDER_MAIN("sites_timeout")
