#include <iostream>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "mockrpcconnection.h"
#include "pretty_printers.h"
#include <doctest/doctest.h>

#include "src/appclioptions.h"
#include "src/historyapp.h"
#include "src/sitenode.h"
#include "src/valuecachenode.h"
#include "tests/sites.h"
#include "tests/utils.h"

namespace cp = shv::chainpack;
using cp::RpcValue;

QQueue<std::function<CallNext(MockRpcConnection*)>> setup_test()
{
	auto expected_path_change = std::make_shared<std::tuple<std::string, RpcValue>>();

	QQueue<std::function<CallNext(MockRpcConnection*)>> res;
	std::string cache_dir_path = "eyas/opc";
	enqueue(res, [=] (MockRpcConnection* mock) {
		HistoryApp::instance()->cliOptions()->setSiteOnlineStatusInterval(3);
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
		EXPECT_SUBSCRIPTION_YIELD("shv/eyas/opc", "chng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD("shv/eyas/opc", "cmdlog");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION_YIELD("shv/eyas/with_app_history", "chng");
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SUBSCRIPTION("shv/eyas/with_app_history", "cmdlog");

		auto node = HistoryApp::instance()->valueCacheNode();
		QObject::connect(node, &ValueCacheNode::valueChanged, node, [expected_path_change] (const std::string& path, const RpcValue& value){
			REQUIRE(expected_path_change != nullptr);
			const auto &[expected_path, expected_value] = *expected_path_change;
			CAPTURE(path);
			CAPTURE(value);
			REQUIRE(path == expected_path);
			REQUIRE(value == expected_value);
			*expected_path_change = {};
		});

		*expected_path_change = {"shv/eyas/opc/cached_status", RpcValue{"changed_value"}};
		NOTIFY_YIELD("shv/eyas/opc/cached_status", "chng", std::string{"changed_value"});
	});
	enqueue(res, [=] (MockRpcConnection* mock) {
		EXPECT_SIGNAL("eyas/opc", "onlinestatuschng", 2);
		return CallNext::Yes;
	});

	DOCTEST_SUBCASE("value is in cache")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			REQUEST_YIELD("_valuecache", "get", std::string{"shv/eyas/opc/cached_status"});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("_valuecache", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("changed_value");

			// mntchng removes it from the cache.
			NOTIFY_YIELD("shv/eyas", "mntchng", true);
		});

		// Have to handle the log resets here.
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/opc/.app/shvjournal", "lsfiles");
			RESPOND_YIELD(RpcValue::List{});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/with_app_history/.app/history", "lsfiles");
			RESPOND_YIELD(RpcValue::List{});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("shv/eyas/opc", "logReset");
			EXPECT_SIGNAL("shv/eyas/with_app_history", "logReset");
		});

		// Now it shouldn't be cached anymore.
		enqueue(res, [=] (MockRpcConnection* mock) {
			REQUEST_YIELD("_valuecache", "get", std::string{"shv/eyas/opc/cached_status"});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("_valuecache", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/opc/cached_status", "get");
			*expected_path_change = {"shv/eyas/opc/cached_status", RpcValue{"new_value"}};
			RESPOND_YIELD("new_value");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("new_value");

			// mntchng somewhere else doesn't remove the cache.
			NOTIFY_YIELD("shv/eyas/with_app_history", "mntchng", true);
		});

		// Have to handle the log resets here.
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/with_app_history/.app/history", "lsfiles");
			RESPOND_YIELD(RpcValue::List{});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("shv/eyas/with_app_history", "logReset");
			REQUEST_YIELD("_valuecache", "get", std::string{"shv/eyas/opc/cached_status"});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("_valuecache", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("new_value");
		});
	}

	DOCTEST_SUBCASE("value is not in cache")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			REQUEST_YIELD("_valuecache", "get", std::string{"shv/eyas/opc/not_cached"});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("_valuecache", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/opc/not_cached", "get");
			*expected_path_change = {"shv/eyas/opc/not_cached", RpcValue{"value_for_not_cached"}};
			RESPOND_YIELD("value_for_not_cached");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("value_for_not_cached");
			// Now it should be cached.
			REQUEST_YIELD("_valuecache", "get", std::string{"shv/eyas/opc/not_cached"});
		});

		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("_valuecache", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("value_for_not_cached");
		});
	}

	DOCTEST_SUBCASE("site online status")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			REQUIRE(HistoryApp::instance()->siteNode("eyas/with_app_history")->onlineStatus() == SiteNode::OnlineStatus::Unknown);
			REQUEST_YIELD("_valuecache", "get", std::string{"shv/eyas/with_app_history/not_cached"});
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("_valuecache", "cmdlog");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/with_app_history/not_cached", "get");
			*expected_path_change = {"shv/eyas/with_app_history/not_cached", RpcValue{"value_for_not_cached"}};
			RESPOND_YIELD("value_for_not_cached");
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("eyas/with_app_history", "onlinestatuschng", 2);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_RESPONSE("value_for_not_cached");
			REQUIRE(HistoryApp::instance()->siteNode("eyas/with_app_history")->onlineStatus() == SiteNode::OnlineStatus::Online);
			DRIVER_WAIT(4000);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/opc", "dir", "dir");
			RESPOND_TIMEOUT_YIELD();
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_REQUEST("shv/eyas/with_app_history", "dir", "dir");
			RESPOND_YIELD(true);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("eyas/opc", "onlinestatuschng", 1);
			REQUIRE(HistoryApp::instance()->siteNode("eyas/opc")->onlineStatus() == SiteNode::OnlineStatus::Offline);
			REQUIRE(HistoryApp::instance()->siteNode("eyas/with_app_history")->onlineStatus() == SiteNode::OnlineStatus::Online);
		});
		enqueue(res, [=] (MockRpcConnection* mock) {
			EXPECT_SIGNAL("eyas/opc", "alarmmod");
		});
	}
	return res;
}

TEST_HISTORYPROVIDER_MAIN("value_cache")
