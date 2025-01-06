#include "appclioptions.h"
#include "sitenode.h"
#include "historyapp.h"
#include "valuecachenode.h"
#include "utils.h"

#include <shv/core/utils/getlog.h>
#include <shv/core/utils/shvjournalfilereader.h>
#include <shv/core/utils/shvjournalfilewriter.h>
#include <shv/core/utils/shvlogrpcvaluereader.h>
#include <shv/core/utils/shvfilejournal.h>
#include <shv/coreqt/log.h>
#include <shv/coreqt/rpc.h>
#include <shv/iotqt/rpc/rpccall.h>

#include <QDir>
#include <QDirIterator>
#include <QTimer>

#define journalDebug() shvCDebug("historyjournal")
#define journalWarning() shvCWarning("historyjournal")
#define journalInfo() shvCInfo("historyjournal")

namespace cp = shv::chainpack;
namespace {
const auto M_GET_LOG = "getLog";
const auto M_LOG_SIZE = "logSize";
const auto M_ONLINE_STATUS = "onlineStatus";
const auto M_ONLINE_STATUS_CHNG = "onlinestatuschng";
const std::vector<cp::MetaMethod> methods {
	cp::methods::DIR,
	cp::methods::LS,
	{M_GET_LOG, cp::MetaMethod::Flag::None, "RpcValue", "RpcValue", cp::AccessLevel::Read},
	{M_LOG_SIZE, cp::MetaMethod::Flag::IsGetter, {}, "UInt", cp::AccessLevel::Read},
	{M_ONLINE_STATUS, cp::MetaMethod::Flag::IsGetter, {}, "i[Unknown,Offline,Online]", cp::AccessLevel::Read, {{M_ONLINE_STATUS_CHNG}}},
};

const auto M_PUSH_LOG = "pushLog";
const auto M_PUSH_LOG_DEBUG_LOG = "pushLogDebugLog";
const std::vector<cp::MetaMethod> push_log_methods {
	{M_PUSH_LOG, cp::MetaMethod::Flag::None, "RpcValue", "RpcValue", cp::AccessLevel::Write},
	{M_PUSH_LOG_DEBUG_LOG, cp::MetaMethod::Flag::None, {}, "RpcValue", cp::AccessLevel::Devel},
};

const auto M_OVERALL_ALARM = "overallAlarm";
const auto M_ALARM_TABLE = "alarmTable";
const auto M_ALARM_MOD = "alarmmod";

const std::vector<cp::MetaMethod> alarm_methods {
	{M_ALARM_TABLE,  cp::MetaMethod::Flag::None, {}, "List", cp::AccessLevel::Read, {{M_ALARM_MOD}}},
	{SiteNode::M_ALARM_LOG,  cp::MetaMethod::Flag::None, "Map", "List", cp::AccessLevel::Read, {}, "Desc"},
	{M_OVERALL_ALARM, cp::MetaMethod::Flag::IsGetter, {}, "Int", cp::AccessLevel::Read, {{cp::Rpc::SIG_VAL_CHANGED}}},
};
}

std::vector<shv::core::utils::ShvAlarm> SiteNode::alarms() const
{
	std::vector<shv::core::utils::ShvAlarm> res;
	std::ranges::transform(m_alarms, std::back_inserter(res), std::identity{}, &AlarmWithTimestamp::alarm);
	return res;
}

SiteNode::OnlineStatus SiteNode::onlineStatus() const
{
	return m_onlineStatus;
}

shv::chainpack::RpcValue SiteNode::AlarmWithTimestamp::toRpcValue() const
{
	auto res = this->alarm.toRpcValue(true).asMap();
	res.emplace("timestamp", timestamp);
	return res;
}

auto get_changed_alarms(const auto& alarms, const auto& type_info, const auto& shv_path, const auto& value)
{
	std::string p_field_name;
	shv::core::utils::ShvPropertyDescr nd = std::get<shv::core::utils::ShvTypeInfo>(type_info).propertyDescriptionForPath(shv_path, &p_field_name);

	if (!p_field_name.empty()) { //skip bitfield paths generated in shvG2
		return std::vector<shv::core::utils::ShvAlarm>{};
	}

	std::vector<shv::core::utils::ShvAlarm> changed_alarms;
	for (const auto &alarm : shv::core::utils::ShvAlarm::checkAlarms(std::get<shv::core::utils::ShvTypeInfo>(type_info), shv_path, value)) {
		if ([&alarms, alarm] {
				if (!alarm.isActive) {
					// If the alarm is not active, we'll try to find a current active one with the same path.
					return std::ranges::find(alarms, alarm.path, [] (const auto& alarm_with_ts) {return alarm_with_ts.alarm.path;}) != alarms.end();
				}
				// If it is active, we'll look into whether there already is an identical one.
				return std::ranges::find(alarms, alarm, &SiteNode::AlarmWithTimestamp::alarm) == alarms.end();
			} ()) {
			changed_alarms.push_back(alarm);
		}
	}

	return changed_alarms;
}

auto update_alarms(auto& alarms, const auto& changed_alarms, const auto& timestamp)
{
	if (changed_alarms.empty()) {
		return;
	}

	for (const auto& changed_alarm : changed_alarms) {
		auto to_erase = std::ranges::remove_if(alarms, [&changed_alarm] (const auto& alarm_with_ts) {
			return alarm_with_ts.alarm.path == changed_alarm.path;
		});

		alarms.erase(to_erase.begin(), to_erase.end());

		if (changed_alarm.isActive) {
			alarms.emplace_back(SiteNode::AlarmWithTimestamp{
				.alarm = changed_alarm,
				.timestamp = timestamp
			});
		}
	}
}

void SiteNode::setOnlineStatus(const OnlineStatus online_status)
{
	if (online_status == m_onlineStatus) {
		return;
	}

	HistoryApp::instance()->rpcConnection()->sendShvSignal(shvPath().asString(), M_ONLINE_STATUS_CHNG, shv::chainpack::RpcValue::Int(online_status));
	m_onlineStatus = online_status;
}

SiteNode::SiteNode(const std::string& node_id, const std::string& journal_cache_dir, LogType log_type, ShvNode* parent)
	: Super(node_id, parent)
	, m_journalCacheDir(journal_cache_dir)
	, m_logType(log_type)
{
	QDir(QString::fromStdString(m_journalCacheDir)).mkpath(".");

	if (m_logType != LogType::PushLog) {
		QElapsedTimer alarm_load_timer;
		alarm_load_timer.start();
		const auto files_path = shv::core::utils::joinPath("sites", shvPath().asString(), "_files");
		auto* ls_call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
			->setShvPath(files_path)
			->setMethod("ls");
		journalDebug() << "Discovering typeInfo at" << files_path;
		connect(ls_call, &shv::iotqt::rpc::RpcCall::error, this, [this, ls_call, files_path] (const shv::chainpack::RpcError& ls_error) {
			ls_call->deleteLater();
			journalWarning() << "Couldn't discover typeInfo support for" << files_path << ls_error.toString();
			this->m_typeInfo.emplace<std::string>("Couldn't discover site files: " + ls_error.toString());
		});

		connect(ls_call, &shv::iotqt::rpc::RpcCall::result, this, [this, ls_call, files_path, alarm_load_timer] (const shv::chainpack::RpcValue& ls_result) {
			std::string type_info_path;
			ls_call->deleteLater();
			const auto& list = ls_result.asList();

			if (std::ranges::find(list, "typeInfo.cpon") != list.end()) {
				type_info_path = shv::core::utils::joinPath(files_path, "typeInfo.cpon");
			} else if (std::ranges::find(list, "nodesTree.cpon") != list.end()) {
				type_info_path = shv::core::utils::joinPath(files_path, "nodesTree.cpon");
			} else {
				journalDebug() << "No typeInfo at" << files_path;
				this->m_typeInfo.emplace<std::string>("This site doesn't support typeInfo.cpon");
				return;
			}

			journalDebug() << "Retrieving" << type_info_path;
			auto* read_call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
				->setShvPath(type_info_path)
				->setMethod("read")
				->setTimeout(60000);
			connect(read_call, &shv::iotqt::rpc::RpcCall::error, this, [this, read_call, type_info_path] (const shv::chainpack::RpcError& read_error) {
				read_call->deleteLater();
				journalDebug() << "Retrieving" << type_info_path << "failed:" << read_error.toString();
				this->m_typeInfo.emplace<std::string>("Couldn't retrieve typeInfo.cpon for this site: " + read_error.toString());
			});
			connect(read_call, &shv::iotqt::rpc::RpcCall::result, this, [this, read_call, type_info_path, alarm_load_timer] (const shv::chainpack::RpcValue& read_result) {
				if (!read_result.isBlob()) {
					auto msg = std::string{"Couldn't retrieve typeInfo.cpon for this site: sitesprovider didn't typeinfo response wasn't a blob, it was: "} + read_result.typeName();
					this->m_typeInfo.emplace<std::string>(msg);
					journalWarning() << "Couldn't retrieve typeinfo for" << type_info_path << msg;
					return;
				}

				read_call->deleteLater();
				journalDebug() << "Retrieved" << type_info_path << "successfully";
				std::string error;
				this->m_typeInfo.emplace<shv::core::utils::ShvTypeInfo>(shv::core::utils::ShvTypeInfo::fromRpcValue(shv::chainpack::RpcValue::fromCpon(read_result.toString(), &error)));
				if (!error.empty()) {
					this->m_typeInfo.emplace<std::string>("Couldn't retrieve typeInfo.cpon for this site: " + error);
					journalDebug() << "Couldn't parse typeinfo for" << type_info_path << error;
					return;
				}

				auto update_alarms_and_overall_alarm = [this] (const auto& shv_path, const auto& value, const auto& timestamp) {
					auto changed_alarms = get_changed_alarms(m_alarms, m_typeInfo, shv_path, value);
					if (changed_alarms.empty()) {
						return;
					}

					update_alarms(m_alarms, changed_alarms, timestamp);

					std::ranges::sort(m_alarms, std::less<shv::core::utils::ShvAlarm::Severity>{}, [] (const auto& alarm_with_ts) {return alarm_with_ts.alarm.severity;});
					HistoryApp::instance()->rpcConnection()->sendShvSignal(shvPath().asString(), M_ALARM_MOD);

					auto new_overall_alarm = m_alarms.empty() ? shv::core::utils::ShvAlarm::Severity::Invalid : m_alarms.front().alarm.severity;
					if (new_overall_alarm != m_overallAlarm) {
						m_overallAlarm = new_overall_alarm;
						HistoryApp::instance()->rpcConnection()->sendShvSignal(shvPath().asString() + ":" +  M_OVERALL_ALARM, cp::Rpc::SIG_VAL_CHANGED, static_cast<int>(m_overallAlarm));
					}
				};

				connect(HistoryApp::instance()->valueCacheNode(), &ValueCacheNode::valueChanged, this, [update_alarms_and_overall_alarm, node_path = shvPath().asString() + "/"] (const std::string& path, const shv::chainpack::RpcValue& value) {
					// Event paths come in full, so we need to strip the "shv/" prefix and the site prefix. What's left
					// is the device path (the one in typeInfo).
					assert(path.starts_with("shv/"));
					auto path_without_shv_prefix = path.substr(4);
					if (!path_without_shv_prefix.starts_with(node_path)) {
						return;
					}
					auto path_without_site_prefix = path_without_shv_prefix.substr(node_path.size());
					update_alarms_and_overall_alarm(path_without_site_prefix, value, cp::RpcValue::DateTime::now());
				});

				shv::core::utils::ShvGetLogParams params;
				params.since = "last";
				params.withSnapshot = true;
				auto snapshot = shv::core::utils::ShvLogRpcValueReader(getLog(params));
				auto now = cp::RpcValue::DateTime::now();
				while (snapshot.next()) {
					auto entry = snapshot.entry();
					update_alarms_and_overall_alarm(entry.path, entry.value, now);
				}
				auto elapsed_ms = alarm_load_timer.elapsed();
				if (elapsed_ms > 5000) {
					shvWarning().nospace() << "Initializing alarms for " << shvPath() << " took more than 5 seconds! (" << elapsed_ms << "ms)";
				}
			});
			read_call->start();
		});
		ls_call->start();
	}
	auto site_online_timer = new QTimer();
	connect(site_online_timer, &QTimer::timeout, this, [this, node_path = shvPath().asString()] {
		auto* dir_call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
			->setShvPath(shv::core::utils::joinPath("shv", node_path))
			->setMethod(shv::chainpack::Rpc::METH_DIR)
			->setParams(shv::chainpack::Rpc::METH_DIR);
		connect(dir_call, &shv::iotqt::rpc::RpcCall::maybeResult, this, [this] (const shv::chainpack::RpcValue& /*dir_result*/, const shv::chainpack::RpcError& dir_error) {
			if (dir_error.isValid()) {
				if ((dir_error.code() == cp::RpcError::ErrorCode::MethodCallTimeout) ||
					(dir_error.code() == cp::RpcError::ErrorCode::MethodNotFound)) {
					setOnlineStatus(OnlineStatus::Offline);
					return;
				}
			}

			setOnlineStatus(OnlineStatus::Online);
		});

		dir_call->start();
	});
	site_online_timer->start(HistoryApp::instance()->cliOptions()->siteOnlineStatusInterval() * 1000);

	connect(HistoryApp::instance()->valueCacheNode(), &ValueCacheNode::valueChanged, this, [this, node_path = shvPath().asString() + "/", site_online_timer] (const std::string& path, const shv::chainpack::RpcValue& /*value*/) {
		assert(path.starts_with("shv/"));
		auto path_without_shv_prefix = path.substr(4);
		if (!path_without_shv_prefix.starts_with(node_path)) {
			return;
		}
		setOnlineStatus(OnlineStatus::Online);
		site_online_timer->start();
	});

}

size_t SiteNode::methodCount(const StringViewList& shv_path)
{
	if (shv_path.empty()) {
		if (m_logType == LogType::PushLog) {
			return methods.size() + push_log_methods.size();
		}

		if (std::holds_alternative<shv::core::utils::ShvTypeInfo>(m_typeInfo)) {
			return methods.size() + alarm_methods.size();
		}

		return methods.size();
	}

	return Super::methodCount(shv_path);
}

const cp::MetaMethod* SiteNode::metaMethod(const StringViewList& shv_path, size_t index)
{
	if (shv_path.empty()) {
		if (index >= methods.size()) {
			if (m_logType == LogType::PushLog) {
				return &push_log_methods.at(index - methods.size());
			}
			return &alarm_methods.at(index - methods.size());
		}

		return &methods.at(index);
	}

	return Super::metaMethod(shv_path, index);
}

qint64 SiteNode::calculateCacheDirSize() const
{
	journalDebug() << "Calculating cache directory size";
	QDirIterator iter(QString::fromStdString(m_journalCacheDir), QDir::NoDotAndDotDot | QDir::Files, QDirIterator::Subdirectories);
	qint64 total_size = 0;
	while (iter.hasNext()) {
		QFile file(iter.next());
		total_size += file.size();
	}
	journalDebug() << "Cache directory size" << total_size;

	return total_size;
}

shv::chainpack::RpcValue SiteNode::getLog(const shv::core::utils::ShvGetLogParams& get_log_params)
{
	std::vector<std::function<shv::core::utils::ShvJournalFileReader()>> readers;
	auto journal_dir = QDir(QString::fromStdString(m_journalCacheDir));
	auto dir_files = journal_dir.entryList(QDir::Files, QDir::Name);
	auto newest_matching_file_it = [&get_log_params, &dir_files] {
		// If there's no since param, return everything.
		if ((!get_log_params.since.isDateTime() && !get_log_params.isSinceLast()) || dir_files.begin() == dir_files.end()) {
			return dir_files.begin();
		}

		// If the first file is the dirtylog, just return it. There are no other files.
		if (*dir_files.begin() == "dirtylog") {
			return dir_files.begin();
		}

		// For since == last, we need to take the dirtylog and the last file.
		// the previous condition would catch that.
		if (get_log_params.isSinceLast()) {
			// There is at least one file, let's look at it. If it's the dirty log, then there must be at least one
			// more file, because the previous condition checks whether there is only the dirty log.
			auto it = dir_files.end() - 1;
			if (*it != "dirtylog") {
				// There's no dirtylog, so we'll just return the last file.
				return it;
			}

			// Otherwise, there's a dirtylog and we'll return the last file AND the dirtylog.
			return dir_files.end() - 2;
		}

		// If the first file is newer than since, than just return it.
		auto since_param_ms = get_log_params.since.toDateTime().msecsSinceEpoch();
		if (shv::core::utils::ShvJournalFileReader::fileNameToFileMsec(dir_files.begin()->toStdString()) >= since_param_ms) {
			return dir_files.begin();
		}

		// Otherwise the first file is older than since.
		// Try to find files that are newer, but still older than since.
		auto last_matching_it = dir_files.begin();
		for (auto it = dir_files.begin(); it != dir_files.end(); ++it) {
			if (*it == "dirtylog" || shv::core::utils::ShvJournalFileReader::fileNameToFileMsec(it->toStdString()) >= since_param_ms) {
				break;
			}

			last_matching_it = it;
		}

		return last_matching_it;
	}();

	for (auto it = newest_matching_file_it; it != dir_files.end(); ++it) {
		journalDebug() << "Using" << *it << "for getLog at" << shvPath();
		readers.emplace_back([full_file_name = journal_dir.filePath(*it).toStdString()] {
			return shv::core::utils::ShvJournalFileReader(full_file_name);
		});
	}

	return shv::core::utils::getLog(readers, get_log_params, shv::chainpack::RpcValue::DateTime::now());
}

AlarmLog SiteNode::alarmLog(const shv::chainpack::RpcValue& params)
{
	if (std::holds_alternative<std::string>(m_typeInfo)) {
		return {};
	}

	if (!params.isMap()) {
		SHV_EXCEPTION("Expected a Map param");
	}

	if (!params.asMap().hasKey("since")) {
		SHV_EXCEPTION("Missing since param");
	}

	auto since = params.asMap().at("since");
	if (!since.isDateTime()) {
		SHV_EXCEPTION(std::string("Expected since param to be DateTime, got: ") + since.typeName());
	}

	if (!params.asMap().hasKey("until")) {
		SHV_EXCEPTION("Missing until param");
	}

	auto until = params.asMap().at("until");
	if (!until.isDateTime()) {
		SHV_EXCEPTION(std::string("Expected until param to be DateTime, got: ") + until.typeName());
	}

	shv::core::utils::ShvGetLogParams get_log_params;
	get_log_params.since = since.toDateTime();
	get_log_params.until = until.toDateTime();
	get_log_params.withSnapshot = true;
	auto log = shv::core::utils::ShvLogRpcValueReader(getLog(get_log_params));
	AlarmLog alarm_log{
		.site = shvPath().asString(),
		.snapshot = {},
		.events = {},
	};
	std::vector<SiteNode::AlarmWithTimestamp> current_snapshot;
	auto snapshot_saved = false;
	while (log.next()) {
		const auto& entry = log.entry();

		auto changed_alarms = get_changed_alarms(current_snapshot, m_typeInfo, entry.path, entry.value);
		if (!log.isInSnapshot()) {
			if (!snapshot_saved) {
				// Our snapshot is complete, so we'll save it now, because we'll keep updating it as we're building the events.
				alarm_log.snapshot = current_snapshot;
				snapshot_saved = true;
			}

			for (const auto& changed_alarm : changed_alarms) {
				alarm_log.events.emplace_back(AlarmWithTimestamp{
					.alarm = changed_alarm,
					.timestamp = entry.dateTime()
				});
			}
		}
		update_alarms(current_snapshot, changed_alarms, entry.dateTime());
	}

	return alarm_log;
}

shv::chainpack::RpcValue SiteNode::callMethod(const StringViewList& shv_path, const std::string& method, const shv::chainpack::RpcValue& params, const shv::chainpack::RpcValue& user_id)
{
	if (method == M_PUSH_LOG && m_logType == LogType::PushLog) {
		m_pushLogDebugLog.clear();
		#define log_pushlog(logger, msg) \
			logger << (msg); \
			m_pushLogDebugLog.emplace_back(shv::chainpack::RpcValue::List{shv::chainpack::RpcValue::DateTime::now(), (msg)});

		log_pushlog(journalInfo(), "Got pushLog at " + shvPath().asString());

		shv::core::utils::ShvLogRpcValueReader reader(params);
		QDir cache_dir(QString::fromStdString(m_journalCacheDir));
		auto remote_since_ms = reader.logHeader().sinceCRef().toDateTime().msecsSinceEpoch();
		int64_t local_newest_entry_ms = 0;
		std::string local_newest_entry_str;
		std::vector<std::string> local_newest_entry_paths;
		auto entries = cache_dir.entryList(QDir::NoDotAndDotDot | QDir::Files, QDir::Name | QDir::Reversed);
		if (!std::empty(entries)) {
			auto local_newest_log_file = entries.at(0);
			auto newest_file_entries = read_entries_from_file(cache_dir.filePath(local_newest_log_file));
			if (!newest_file_entries.empty()) {
				local_newest_entry_ms = newest_file_entries.back().dateTime().msecsSinceEpoch();
				local_newest_entry_str = newest_file_entries.back().dateTime().toIsoString();
				for (const auto& entry : newest_file_entries) {
					if (entry.epochMsec == local_newest_entry_ms) {
						local_newest_entry_paths.push_back(entry.path);
					}
				}
			}
		}

		auto file_path = cache_dir.filePath(QString::fromStdString(shv::core::utils::ShvJournalFileReader::msecToBaseFileName(remote_since_ms)) + ".log2");

		shv::core::utils::ShvJournalFileWriter writer(file_path.toStdString());
		int remote_entries_count = 0;
		int written_entries_count = 0;
		log_pushlog(journalDebug(), "Writing " + file_path.toStdString());
		while (reader.next()) {
			auto entry = reader.entry();
			remote_entries_count++;
			if (entry.epochMsec < local_newest_entry_ms) {
				log_pushlog(shvWarning(), "Rejecting push log entry for: " + shvPath().asString() + " with timestamp: " + entry.dateTime().toIsoString() + " because a newer one already exists: " + local_newest_entry_str);
				continue;
			}

			if (entry.epochMsec == local_newest_entry_ms && std::find(local_newest_entry_paths.begin(), local_newest_entry_paths.end(), entry.path) != local_newest_entry_paths.end()) {
				log_pushlog(shvWarning(), "Rejecting push log entry for: " + shvPath().asString() + " with timestamp: " + entry.dateTime().toIsoString() + " and path: " + entry.path + " because we already have an entry with this timestamp and path");
				continue;
			}

			writer.append(reader.entry());
			written_entries_count++;
		}

		log_pushlog(journalInfo(), "Got " + std::to_string(remote_entries_count) + " entries (" + std::to_string(remote_entries_count - written_entries_count) + " were rejected) from pushLog at " + shvPath().asString());

		// The output file is always created by the writer. If we didn't manage to write any entries, we'll remove the
		// file.
		if (auto file = QFile(file_path); file.size() == 0) {
			file.remove();
		}

		if (remote_entries_count == 0) {
			return shv::chainpack::RpcValue::Map {
				{"since", shv::chainpack::RpcValue(nullptr)},
				{"until", shv::chainpack::RpcValue(nullptr)},
				{"msg", "success"}
			};
		}

		return shv::chainpack::RpcValue::Map {
			{"since", reader.logHeader().since()},
			{"until", reader.logHeader().until()},
			{"msg", "success"}
		};
	}

	if (method == M_PUSH_LOG_DEBUG_LOG && m_logType == LogType::PushLog) {
		return m_pushLogDebugLog;
	}

	if (method == M_GET_LOG) {
		return getLog(shv::core::utils::ShvGetLogParams(params));
	}

	if (method == M_ALARM_TABLE) {
		if (std::holds_alternative<std::string>(m_typeInfo)) {
			return std::get<std::string>(m_typeInfo);
		}

		shv::chainpack::RpcValue::List ret;
		std::ranges::transform(m_alarms, std::back_inserter(ret), [] (const auto& alarm) { return alarm.toRpcValue(); });
		return ret;
	}

	if (method == M_ALARM_LOG) {
		if (std::holds_alternative<std::string>(m_typeInfo)) {
			return std::get<std::string>(m_typeInfo);
		}

		return alarmLog(params).toRpcValue();
	}

	if (method == M_OVERALL_ALARM) {
		return static_cast<int>(m_overallAlarm);
	}

	if (method == M_LOG_SIZE) {
		return shv::chainpack::RpcValue::Int(calculateCacheDirSize());
	}

	if (method == M_ONLINE_STATUS) {
		return shv::chainpack::RpcValue::Int(m_onlineStatus);
	}

	return Super::callMethod(shv_path, method, params, user_id);
}
