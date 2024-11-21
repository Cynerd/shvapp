#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "mockrpcconnection.h"
#include "pretty_printers.h"
#include <doctest/doctest.h>

#include "src/appclioptions.h"
#include "src/sitenode.h"
#include "src/historyapp.h"
#include "tests/sites.h"
#include "tests/utils.h"

#include <shv/core/utils/shvlogrpcvaluereader.h>
using Severity = shv::core::utils::ShvAlarm::Severity;

shv::core::utils::ShvAlarm make_alarm(const std::string& path, shv::core::utils::ShvAlarm::Severity severity = shv::core::utils::ShvAlarm::Severity::Error, bool is_active = true)
{
	return shv::core::utils::ShvAlarm{
		.path = path,
		.isActive = is_active,
		.description = "Alarm 1",
		.label = "Alarm 1 label",
		.level = 0,
		.severity = severity,
	};
}

QQueue<std::function<CallNext(MockRpcConnection*)>> setup_test()
{
	QQueue<std::function<CallNext(MockRpcConnection*)>> res;

	DOCTEST_SUBCASE("no typeinfo")
	{
		enqueue(res, [=] (MockRpcConnection* mock) {
			SEND_SITES_YIELD(mock_sites::some_site);
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
		});
	}

	DOCTEST_SUBCASE("with typeinfo")
	{
		DOCTEST_SUBCASE("one device, one alarm") {
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_SITES_YIELD(mock_sites::some_site);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_site");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO("some_site", mock_typeinfo::one_device);
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				return CallNext::Yes;
			});

			DOCTEST_SUBCASE("alarm on") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status/some_alarm_name"),
					});
				});
			}

			DOCTEST_SUBCASE("alarm off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY("shv/some_site/one/status", "chng", 0);
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm on and off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status/some_alarm_name"),
					});

					NOTIFY_YIELD("shv/some_site/one/status", "chng", 0);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Invalid));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}
		}
		DOCTEST_SUBCASE("one device, two alarms") {
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_SITES_YIELD(mock_sites::some_site);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_site");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO("some_site", mock_typeinfo::two_alarms);
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				return CallNext::Yes;
			});

			DOCTEST_SUBCASE("alarm 1 on") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status1", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status1/some_alarm_name"),
					});
				});
			}

			DOCTEST_SUBCASE("alarm 1 off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY("shv/some_site/one/status1", "chng", 0);
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm 1 on and off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status1", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status1/some_alarm_name"),
					});

					NOTIFY_YIELD("shv/some_site/one/status1", "chng", 0);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Invalid));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm 2 on") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status2", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status2/some_alarm_name"),
					});
				});
			}

			DOCTEST_SUBCASE("alarm 2 off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY("shv/some_site/one/status2", "chng", 0);
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm 2 on and off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status2", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status2/some_alarm_name"),
					});

					NOTIFY_YIELD("shv/some_site/one/status2", "chng", 0);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Invalid));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}
			DOCTEST_SUBCASE("both alarm 1 and 2 on and then 1 off") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status1", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status1/some_alarm_name"),
					});
					NOTIFY_YIELD("shv/some_site/one/status2", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status1/some_alarm_name"),
						make_alarm("one/status2/some_alarm_name"),
					});
					NOTIFY_YIELD("shv/some_site/one/status1", "chng", 0);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status2/some_alarm_name"),
					});
				});
			}
		}

		DOCTEST_SUBCASE("two devices, each has one alarm") {
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_SITES_YIELD(mock_sites::some_site);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_site");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO("some_site", mock_typeinfo::two_device);
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				return CallNext::Yes;
			});

			DOCTEST_SUBCASE("alarm on device 1") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status/some_alarm_name"),
					});
				});
			}

			DOCTEST_SUBCASE("alarm off device 1") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY("shv/some_site/one/status", "chng", 0);
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm on and off 1") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/one/status", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("one/status/some_alarm_name"),
					});

					NOTIFY_YIELD("shv/some_site/one/status", "chng", 0);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Invalid));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm on device 2") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/two/status", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm("two/status/some_alarm_name"),
					});
				});
			}

			DOCTEST_SUBCASE("alarm off device 2") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY("shv/some_site/two/status", "chng", 0);
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}

			DOCTEST_SUBCASE("alarm on and off 2") {
				enqueue(res, [=] (MockRpcConnection* mock) {
					NOTIFY_YIELD("shv/some_site/two/status", "chng", 1);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
						make_alarm( "two/status/some_alarm_name")
					});

					NOTIFY_YIELD("shv/some_site/two/status", "chng", 0);
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site", "alarmmod");
				});
				enqueue(res, [=] (MockRpcConnection* mock) {
					EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Invalid));
					REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				});
			}
		}

		DOCTEST_SUBCASE("severity sorting") {
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_SITES_YIELD(mock_sites::some_site);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_site");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO("some_site", mock_typeinfo::different_severity);
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{});
				NOTIFY_YIELD("shv/some_site/one/status1", "chng", 1);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site", "alarmmod");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Warning));
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
					make_alarm("one/status1/alarm_warning", shv::core::utils::ShvAlarm::Severity::Warning),
				});
				NOTIFY_YIELD("shv/some_site/one/status2", "chng", 1);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site", "alarmmod");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
					make_alarm("one/status2/alarm_error"),
					make_alarm("one/status1/alarm_warning", shv::core::utils::ShvAlarm::Severity::Warning),
				});
			});
		}

		DOCTEST_SUBCASE("alarms are loaded from snapshot") {
			const auto snapshot = R"(2022-07-07T18:06:17.784Z	809781	one/status	1		chng	2	)";
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files("some_site", {
					{"2022-07-06T18-06-15-000.log2", snapshot}
				});
				SEND_SITES_YIELD(mock_sites::some_site);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_site");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO_YIELD("some_site", mock_typeinfo::one_device);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site", "alarmmod");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
					make_alarm("one/status/some_alarm_name")
				});
			});
		}

		DOCTEST_SUBCASE("alarmLog") {
			const auto snapshot =
				R"(2022-07-07T18:06:17.784Z	809781	one/status	1		chng	2	)""\n"
				R"(2022-07-07T18:06:20.784Z	809781	one/status	0		chng	2	)""\n"
				R"(2022-07-07T18:06:23.784Z	809781	one/status	1		chng	2	)""\n"
				;
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files("some_site", {
					{"2022-07-06T18-06-15-000.log2", snapshot}
				});
				SEND_SITES_YIELD(mock_sites::some_site);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_site");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_site", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_site", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO_YIELD("some_site", mock_typeinfo::one_device);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site", "alarmmod");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_site:overallAlarm", "chng", static_cast<int>(Severity::Error));
				REQUIRE(HistoryApp::instance()->siteNode("some_site")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
					make_alarm("one/status/some_alarm_name")
				});

				REQUEST_YIELD("some_site", "alarmLog", RpcValue::Map{
					{"since", RpcValue::DateTime::fromUtcString("2022-07-07T18:06:17.784Z")},
					{"until", RpcValue::DateTime::fromUtcString("2022-07-07T18:06:30.784Z")}
				});
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				auto expected = RpcValue::Map{
						{"some_site", RpcValue::Map {
							{"snapshot", RpcValue::List{
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name"), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:17.784Z")}.toRpcValue() }},
							{"events", RpcValue::List{
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name", Severity::Error, false), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:20.784Z")}.toRpcValue(),
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name", Severity::Error, true), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:23.784Z")}.toRpcValue()
							}}
						}}
				};
				EXPECT_RESPONSE(expected);
			});
		}

		DOCTEST_SUBCASE("aggregate alarm log") {
			const auto snapshot =
				R"(2022-07-07T18:06:17.784Z	809781	one/status	1		chng	2	)""\n"
				R"(2022-07-07T18:06:20.784Z	809781	one/status	0		chng	2	)""\n"
				R"(2022-07-07T18:06:23.784Z	809781	one/status	1		chng	2	)""\n"
				;
			enqueue(res, [=] (MockRpcConnection* mock) {
				create_dummy_cache_files("some_node/site_one", {
					{"2022-07-06T18-06-15-000.log2", snapshot}
				});
				create_dummy_cache_files("some_node/site_two", {
					{"2022-07-06T18-06-15-000.log2", snapshot}
				});
				SEND_SITES_YIELD(mock_sites::two_sites_inside_some_node);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_node/site_one");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				ENABLE_TYPEINFO("some_node/site_two");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv", "mntchng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_node/site_one", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_node/site_one", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION_YIELD("shv/some_node/site_two", "chng");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SUBSCRIPTION("shv/some_node/site_two", "cmdlog");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO_YIELD("some_node/site_one", mock_typeinfo::one_device);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				SEND_TYPEINFO_YIELD("some_node/site_two", mock_typeinfo::one_device);
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_node/site_one", "alarmmod");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_node/site_one:overallAlarm", "chng", static_cast<int>(Severity::Error));
				REQUIRE(HistoryApp::instance()->siteNode("some_node/site_one")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
					make_alarm("one/status/some_alarm_name")
				});
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_node/site_two", "alarmmod");
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				EXPECT_SIGNAL("some_node/site_two:overallAlarm", "chng", static_cast<int>(Severity::Error));
				REQUIRE(HistoryApp::instance()->siteNode("some_node/site_two")->alarms() == std::vector<shv::core::utils::ShvAlarm>{
					make_alarm("one/status/some_alarm_name")
				});

				REQUEST_YIELD("some_node", "alarmLog", RpcValue::Map{
					{"since", RpcValue::DateTime::fromUtcString("2022-07-07T18:06:17.784Z")},
					{"until", RpcValue::DateTime::fromUtcString("2022-07-07T18:06:30.784Z")}
				});
			});
			enqueue(res, [=] (MockRpcConnection* mock) {
				auto expected = RpcValue::Map{
					RpcValue::Map {
						{"some_node/site_one", RpcValue::Map {
							{"snapshot", RpcValue::List{
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name"), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:17.784Z")}.toRpcValue(),
							}},
							{"events", RpcValue::List{
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name", Severity::Error, false), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:20.784Z")}.toRpcValue(),
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name", Severity::Error, true), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:23.784Z")}.toRpcValue(),
							}}
						}},
						{"some_node/site_two", RpcValue::Map {
							{"snapshot", RpcValue::List{
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name"), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:17.784Z")}.toRpcValue(),
							}},
							{"events", RpcValue::List{
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name", Severity::Error, false), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:20.784Z")}.toRpcValue(),
								SiteNode::AlarmWithTimestamp{make_alarm("one/status/some_alarm_name", Severity::Error, true), RpcValue::DateTime::fromUtcString("2022-07-07T18:06:23.784Z")}.toRpcValue(),
							}}
						}}
					}
				};
				EXPECT_RESPONSE(expected);
			});
		}
	}
	return res;
}

TEST_HISTORYPROVIDER_MAIN("alarms")
