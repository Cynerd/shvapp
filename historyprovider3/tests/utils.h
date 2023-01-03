#pragma once
#include "test_vars.h"

#include <shv/core/utils/shvjournalentry.h>
#include <shv/core/utils/shvmemoryjournal.h>

#include <QDir>
#include <QDirIterator>

namespace cp = shv::chainpack;
using cp::RpcValue;
using namespace std::string_literals;

struct DummyFileInfo {
	QString fileName;
	std::string content;
};
QDir get_site_cache_dir(const std::string& site_path);
void remove_cache_contents(const std::string& site_path);
RpcValue::List get_cache_contents(const std::string& site_path);
void create_dummy_cache_files(const std::string& site_path, const std::vector<DummyFileInfo>& files);
std::string join(const std::string& a, const std::string& b);


const auto dummy_logfile = R"(2022-07-07T18:06:15.557Z	809779	APP_START	true		SHV_SYS	0	
2022-07-07T18:06:17.784Z	809781	zone1/system/sig/plcDisconnected	false		chng	2	
2022-07-07T18:06:17.784Z	809781	zone1/zone/Zone1/plcDisconnected	false		chng	2	
2022-07-07T18:06:17.869Z	809781	zone1/pme/TSH1-1/switchRightCounterPermanent	0u		chng	2	
)"s;

const auto dummy_logfile2 = R"(2022-07-07T18:06:17.872Z	809781	zone1/system/sig/plcDisconnected	false		chng	2	
2022-07-07T18:06:17.874Z	809781	zone1/zone/Zone1/plcDisconnected	false		chng	2	
2022-07-07T18:06:17.880Z	809781	zone1/pme/TSH1-1/switchRightCounterPermanent	0u		chng	2	
)"s;

const auto dummy_logfile3 = R"(2022-07-07T18:06:20.900Z	809781	zone1/system/sig/plcDisconnected	false		chng	2	
)"s;

const auto logfile_one_entry = R"(2022-07-07T18:06:17.872Z	809781	zone1/system/sig/plcDisconnected	false		chng	2	)"s;

const auto dummy_pushlog = RpcValue::fromCpon(R"(
<
  "dateTime":d"2022-09-15T13:30:04.293Z",
  "device":{"id":"historyprovider"},
  "fields":[
    {"name":"timestamp"},
    {"name":"path"},
    {"name":"value"},
    {"name":"shortTime"},
    {"name":"domain"},
    {"name":"valueFlags"},
    {"name":"userId"}
  ],
  "logParams":{"recordCountLimit":1000, "until":d"2022-07-07T18:06:17.870Z", "withPathsDict":true, "withSnapshot":false, "withTypeInfo":false},
  "logVersion":2,
  "pathsDict":i{1:"APP_START", 2:"zone1/system/sig/plcDisconnected", 3:"zone1/zone/Zone1/plcDisconnected", 4:"zone1/pme/TSH1-1/switchRightCounterPermanent"},
  "recordCount":4,
  "recordCountLimit":1000,
  "recordCountLimitHit":false,
  "since":d"2022-07-07T18:06:15.557Z",
  "until":d"2022-07-07T18:06:15.557Z",
  "withPathsDict":true,
  "withSnapshot":false
>[
  [d"2022-07-07T18:06:15.557Z", 1, true, null, "SHV_SYS", 0u, null],
]
)");

const auto dummy_pushlog3 = RpcValue::fromCpon(R"(
<
  "dateTime":d"2022-09-15T13:30:04.293Z",
  "device":{"id":"historyprovider"},
  "fields":[
    {"name":"timestamp"},
    {"name":"path"},
    {"name":"value"},
    {"name":"shortTime"},
    {"name":"domain"},
    {"name":"valueFlags"},
    {"name":"userId"}
  ],
  "logParams":{"recordCountLimit":1000, "until":d"2022-07-07T18:06:17.870Z", "withPathsDict":true, "withSnapshot":false, "withTypeInfo":false},
  "logVersion":2,
  "pathsDict":i{1:"APP_START", 2:"zone1/system/sig/plcDisconnected", 3:"zone1/zone/Zone1/plcDisconnected", 4:"zone1/pme/TSH1-1/switchRightCounterPermanent"},
  "recordCount":4,
  "recordCountLimit":1000,
  "recordCountLimitHit":false,
  "since":d"2022-07-07T18:06:15.557Z",
  "until":d"2022-07-07T18:06:15.557Z",
  "withPathsDict":true,
  "withSnapshot":false
>[
  [d"2022-07-07T18:06:15.557Z", 2, true, null, "SHV_SYS", 0u, null],
]
)");

const auto dummy_pushlog2 = RpcValue::fromCpon(R"(
<
  "dateTime":d"2022-09-15T13:30:04.293Z",
  "device":{"id":"historyprovider"},
  "fields":[
    {"name":"timestamp"},
    {"name":"path"},
    {"name":"value"},
    {"name":"shortTime"},
    {"name":"domain"},
    {"name":"valueFlags"},
    {"name":"userId"}
  ],
  "logParams":{"recordCountLimit":1000, "until":d"2022-07-07T18:06:17.870Z", "withPathsDict":true, "withSnapshot":false, "withTypeInfo":false},
  "logVersion":2,
  "pathsDict":i{1:"APP_START", 2:"zone1/system/sig/plcDisconnected", 3:"zone1/zone/Zone1/plcDisconnected", 4:"zone1/pme/TSH1-1/switchRightCounterPermanent"},
  "recordCount":4,
  "recordCountLimit":1000,
  "recordCountLimitHit":false,
  "since":d"2022-07-07T18:06:15.554Z",
  "until":d"2022-07-07T18:06:15.554Z",
  "withPathsDict":true,
  "withSnapshot":false
>[
  [d"2022-07-07T18:06:15.554Z", 1, true, null, "SHV_SYS", 0u, null],
]
)");

const auto dummy_getlog_response = RpcValue::fromCpon(R"(
<
  "dateTime":d"2022-09-15T13:30:04.293Z",
  "device":{"id":"historyprovider"},
  "fields":[
    {"name":"timestamp"},
    {"name":"path"},
    {"name":"value"},
    {"name":"shortTime"},
    {"name":"domain"},
    {"name":"valueFlags"},
    {"name":"userId"}
  ],
  "logParams":{"recordCountLimit":1000, "until":d"2022-07-07T18:06:17.870Z", "withPathsDict":true, "withSnapshot":false, "withTypeInfo":false},
  "logVersion":2,
  "pathsDict":i{1:"APP_START", 2:"zone1/system/sig/plcDisconnected", 3:"zone1/zone/Zone1/plcDisconnected", 4:"zone1/pme/TSH1-1/switchRightCounterPermanent"},
  "recordCount":4,
  "recordCountLimit":1000,
  "recordCountLimitHit":false,
  "since":d"2022-07-07T18:06:15.557Z",
  "until":d"2022-07-07T18:06:17.870Z",
  "withPathsDict":true,
  "withSnapshot":false
>[
  [d"2022-07-07T18:06:15.557Z", 1, true, null, "SHV_SYS", 0u, null],
  [d"2022-07-07T18:06:17.784Z", 2, false, null, null, 2u, null],
  [d"2022-07-07T18:06:17.784Z", 3, false, null, null, 2u, null],
  [d"2022-07-07T18:06:17.869Z", 4, 0u, null, null, 2u, null]
]
)");

const auto five_thousand_records_getlog_response = RpcValue::fromCpon((R"(
<
  "dateTime":d"2022-09-15T13:30:04.293Z",
  "device":{"id":"historyprovider"},
  "fields":[
    {"name":"timestamp"},
    {"name":"path"},
    {"name":"value"},
    {"name":"shortTime"},
    {"name":"domain"},
    {"name":"valueFlags"},
    {"name":"userId"}
  ],
  "logParams":{"recordCountLimit":1000, "until":d"2022-07-07T18:06:17.870Z", "withPathsDict":true, "withSnapshot":false, "withTypeInfo":false},
  "logVersion":2,
  "pathsDict":i{1:"APP_START", 2:"zone1/system/sig/plcDisconnected", 3:"zone1/zone/Zone1/plcDisconnected", 4:"zone1/pme/TSH1-1/switchRightCounterPermanent"},
  "recordCount":5000,
  "recordCountLimit":1000,
  "recordCountLimitHit":false,
  "since":d"2022-07-07T18:06:15.557Z",
  "until":d"2022-07-07T18:06:17.870Z",
  "withPathsDict":true,
  "withSnapshot":false
>[
  )" + QString(R"([d"2022-07-07T18:06:15.557Z", 1, true, null, "SHV_SYS", 0u, null],
)").repeated(5000) + R"(]
)").toStdString());

const auto ls_size_true = RpcValue::fromCpon(R"({"size": true})");
const auto read_offset_0 = RpcValue::fromCpon(R"({"offset":0})");

#define TEST_HISTORYPROVIDER_MAIN(test_name)                                           \
	DOCTEST_TEST_CASE(test_name)                                                       \
	{                                                                                  \
		NecroLog::registerTopic("MockRpcConnection", "");                              \
		NecroLog::setTopicsLogTresholds(":D");                                         \
		QCoreApplication::setApplicationName("historyprovider tester");                \
		int argc = 0;                                                                  \
		char *argv[] = { nullptr };                                                    \
		AppCliOptions cli_opts;                                                        \
                                                                                       \
		cli_opts.setJournalCacheRoot(join(TESTS_DIR, test_name));                      \
		QDir(QString::fromStdString(cli_opts.journalCacheRoot())).removeRecursively(); \
		HistoryApp app(argc, argv, &cli_opts, new MockRpcConnection());                \
		app.exec();                                                                    \
	}