#include "shvjournalnode.h"
#include "historyapp.h"
#include "appclioptions.h"
#include "utils.h"

#include <shv/coreqt/log.h>
#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/iotqt/rpc/rpccall.h>
#include <shv/core/utils/shvjournalfilereader.h>
#include <shv/core/utils/shvjournalfilewriter.h>
#include <shv/core/utils/shvjournalentry.h>
#include <shv/core/utils/shvfilejournal.h>
#include <shv/core/utils/shvmemoryjournal.h>

#include <QDir>
#include <QDirIterator>
#include <QTimer>

#include <deque>

#define journalDebug() shvCDebug("historyjournal")
#define journalInfo() shvCInfo("historyjournal")
#define journalWarning() shvCWarning("historyjournal")
#define journalError() shvCError("historyjournal")

namespace cp = shv::chainpack;
namespace {
constexpr auto SYNCLOG_DESC = R"(syncLog - triggers a manual sync
With a string param, only the subtree signified by the string is synced.
syncLog also takes a map param in this format: {
	waitForFinished: bool // the method waits until the whole operation is finished and only then returns a response
	shvPath: string // the subtree to be synced
}

Returns: a list of all leaf sites that will be synced
)";

constexpr auto SYNCINFO_DESC = R"(syncInfo - returns info about sites' sync status
Optionally takes a string that filters the sites by prefix.

Returns: a map where they is the path of the site and the value is a map with a status string and a last updated timestamp.
)";

const auto M_LOG_SIZE_LIMIT = "logSizeLimit";
const auto M_TOTAL_LOG_SIZE = "totalLogSize";
const auto M_LOG_USAGE = "logUsage";
const auto M_SYNC_INFO = "syncInfo";
const auto M_SYNC_LOG = "syncLog";
const auto M_SANITIZE_LOG = "sanitizeLog";

const std::vector<cp::MetaMethod> methods {
	{M_LOG_SIZE_LIMIT, cp::MetaMethod::Flag::None, {}, "Int", cp::AccessLevel::Devel, {}, "Returns: log size limit."},
	{M_TOTAL_LOG_SIZE, cp::MetaMethod::Flag::None, {}, "Int", cp::AccessLevel::Devel, {}, "Returns: total size occupied by logs."},
	{M_LOG_USAGE, cp::MetaMethod::Flag::None, {}, "Decimal", cp::AccessLevel::Devel, {}, "Returns: percentage of space occupied by logs."},
	{M_SYNC_LOG, cp::MetaMethod::Flag::None, "String|Map", "List", cp::AccessLevel::Write, {}, SYNCLOG_DESC},
	{M_SYNC_INFO, cp::MetaMethod::Flag::None, "String", "Map", cp::AccessLevel::Read, {}, SYNCINFO_DESC},
	{M_SANITIZE_LOG, cp::MetaMethod::Flag::None, {}, "String", cp::AccessLevel::Devel},
};

const auto DIRTY_FILENAME = "dirtylog";

template <typename StringType>
std::string dirty_log_path(const StringType& cache_dir_path)
{
	return shv::coreqt::utils::joinPath<std::string>(cache_dir_path, DIRTY_FILENAME);
}

template <typename StringType>
std::string get_cache_dir_path(const QString& cache_root, const StringType& slave_hp_path)
{
	return shv::coreqt::utils::joinPath<std::string>(cache_root, slave_hp_path);
}

}

ShvJournalNode::ShvJournalNode(const std::vector<SlaveHpInfo>& slave_hps, const std::set<std::string>& site_nodes, ShvNode* parent)
	: Super(QString::fromStdString(HistoryApp::instance()->cliOptions()->journalCacheRoot()), "_shvjournal", parent)
	, m_slaveHps(slave_hps)
	, m_siteNodes(site_nodes)
	, m_cacheDirPath(QString::fromStdString(HistoryApp::instance()->cliOptions()->journalCacheRoot()))
{
	QDir(m_cacheDirPath).mkpath(".");
	auto conn = HistoryApp::instance()->rpcConnection();

	auto now = shv::chainpack::RpcValue::DateTime::now();
	for (const auto& slave_hp : m_slaveHps) {
		m_syncInfo.setValue(slave_hp.shv_path, shv::chainpack::RpcValue::Map {
			{"timestamp", now},
			{"status", shv::chainpack::RpcValue::List{"Unknown"}},
		});
	}

	connect(conn, &shv::iotqt::rpc::ClientConnection::rpcMessageReceived, this, &ShvJournalNode::onRpcMessageReceived);

	if (m_slaveHps.empty()) {
		return;
	}
	auto tmr = new QTimer(this);
	connect(tmr, &QTimer::timeout, this, [this, sync_iterator = m_slaveHps.begin(), dirtylog_age_cache = std::map<std::string, int64_t>{}] () mutable {
		if (sync_iterator == m_slaveHps.end()) {
			sync_iterator = m_slaveHps.begin();
		}

		auto current_slave_hp_path = sync_iterator->shv_path;
		auto current_slave_cache_dir_path = sync_iterator->cache_dir_path;
		if (dirtylog_age_cache.find(current_slave_hp_path) == dirtylog_age_cache.end()) {
			dirtylog_age_cache.emplace(current_slave_hp_path, 0);

			auto journal_dir_path = get_cache_dir_path(m_cacheDirPath, current_slave_cache_dir_path);
			if (QFile(QString::fromStdString(dirty_log_path(journal_dir_path))).exists()) {
				if (auto reader = shv::core::utils::ShvJournalFileReader(dirty_log_path(journal_dir_path)); reader.next()) {
					dirtylog_age_cache[current_slave_hp_path] = reader.entry().epochMsec;
				}
			}
		}

		if ((QDateTime::currentDateTime().toMSecsSinceEpoch() - dirtylog_age_cache[current_slave_hp_path]) > HistoryApp::instance()->cliOptions()->logMaxAge() * 1000) {
			HistoryApp::instance()->shvJournalNode()->syncLog(sync_iterator->shv_path, [] (const auto&) {}, [] {});
			dirtylog_age_cache.erase(current_slave_hp_path);
		}

		++sync_iterator;
	});
	tmr->start(HistoryApp::instance()->cliOptions()->syncIteratorInterval() * 1000);

	auto sanitizer_timer = new QTimer(this);
	connect(tmr, &QTimer::timeout, this, [this] {
		sanitizeSize();
	});
	sanitizer_timer->start(HistoryApp::instance()->cliOptions()->journalSanitizerInterval() * 1000);
}

#define sanitizerInfo() shvCInfo("sanitizer")
#define sanitizerDebug() shvCDebug("sanitizer")

namespace {
auto get_log_info(auto cache_dir_path)
{
	auto it = QDirIterator{cache_dir_path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories};
	struct {
		QList<QFileInfo> files;
		qint64 total_size = 0;
	} ret;
	while (it.hasNext()) {
		auto file_info = it.nextFileInfo();
		ret.total_size += file_info.size();
		if (file_info.fileName() == "dirtylog") {
			continue;
		}
		ret.files.emplace_back(file_info);
	}

	return ret;
}
}

void ShvJournalNode::sanitizeSize() const
{
	auto max_size = HistoryApp::instance()->totalCacheSizeLimit();
	auto log_info = get_log_info(m_cacheDirPath);

	sanitizerInfo() << "Current total size:" << log_info.total_size << "journalCacheSizeLimit:" << max_size;
	if (log_info.total_size <= max_size) {
		sanitizerDebug() << "Total size is within limits";
		return;
	}
	std::ranges::sort(log_info.files, [] (const auto& file_a, const auto& file_b) {
		return file_a.fileName() < file_b.fileName();
	});

	for (const auto& file : log_info.files) {
		auto info = QFileInfo{file};
		if (std::ranges::count(log_info.files, info.path(), &QFileInfo::path) < 2) {
			// Let's leave at least one file in each directory, so that we have a snapshot.
			continue;
		}
		auto size = info.size();
		log_info.total_size -= size;
		sanitizerDebug() << "Removing" << info.absoluteFilePath() << "size" << size;
		QFile(file.absoluteFilePath()).remove();
		if (log_info.total_size <= max_size) {
			break;
		}
	}
}

void ShvJournalNode::onRpcMessageReceived(const cp::RpcMessage &msg)
{
	if (msg.isSignal()) {
		cp::RpcSignal ntf(msg);
		auto path = ntf.shvPath().asString();
		auto method = ntf.method().asString();
		auto params = ntf.params();
		auto value = ntf.value();

		if (method == shv::chainpack::Rpc::SIG_VAL_CHANGED || method == shv::chainpack::Rpc::SIG_COMMAND_LOGGED) {
			auto it = std::find_if(m_slaveHps.begin(), m_slaveHps.end(), [&path] (const auto& slave_hp) {
				return path.starts_with(slave_hp.shv_path);
			});

			if (it != m_slaveHps.end()) {
				{
					if (path.at(it->shv_path.size()) != '/') {
						journalWarning() << "Discarding notification with a top-level node path. Offending path was:" << it->shv_path;
						return;
					}

					auto longest_prefix = shv::core::utils::findLongestPrefix(m_siteNodes, path);
					if (longest_prefix == m_siteNodes.end()) {
						// We'll discard events that come from a site node we don't know about.
						return;
					}
					// get rid of the shv prefix
					assert(longest_prefix->starts_with("shv/"));
					auto longest_prefix_without_shv = longest_prefix->substr(4);

					auto writer = shv::core::utils::ShvJournalFileWriter(dirty_log_path(get_cache_dir_path(m_cacheDirPath, longest_prefix_without_shv)));
					auto path_without_prefix = path.substr(longest_prefix->size() + 1 /* for the slash */);
					auto data_change = shv::chainpack::DataChange::fromRpcValue(ntf.params());

					auto entry = shv::core::utils::ShvJournalEntry(path_without_prefix, data_change.value()
																   , shv::core::utils::ShvJournalEntry::DOMAIN_VAL_CHANGE
																   , shv::core::utils::ShvJournalEntry::NO_SHORT_TIME
																   , 1 << shv::core::utils::ShvJournalEntry::ValueFlag::Provisional
																   , data_change.epochMSec());
					writer.append(entry);

				}
			}
		}

		if (method == shv::chainpack::Rpc::SIG_MOUNTED_CHANGED) {
			for (const auto& slave_hp : m_slaveHps) {
				if (slave_hp.shv_path.starts_with(path) &&
					slave_hp.log_type != LogType::PushLog &&
					!m_syncInProgress.value(QString::fromStdString(slave_hp.shv_path), false) &&
					params.toBool() == true) {
					journalInfo() << "mntchng on" << slave_hp.shv_path << "syncing its journal";
					syncLog(slave_hp.shv_path, [] (const auto&) {}, [signal_path = slave_hp.shv_path] () {
						HistoryApp::instance()->rpcConnection()->sendShvSignal(signal_path, "logReset");
					});
				}
			}
		}

	}
}

size_t ShvJournalNode::methodCount(const StringViewList& shv_path)
{
	if (!shv_path.empty()) {
		// We'll let LocalFSNode handle the child nodes.
		return Super::methodCount(shv_path);
	}

	return Super::methodCount(shv_path) + methods.size();
}

const cp::MetaMethod* ShvJournalNode::metaMethod(const StringViewList& shv_path, size_t index)
{
	if (!shv_path.empty()) {
		// We'll let LocalFSNode handle the child nodes.
		return Super::metaMethod(shv_path, index);
	}

	static auto super_method_count = Super::methodCount(shv_path);

	if (index >= super_method_count) {
		return &methods.at(index - super_method_count);
	}

	return Super::metaMethod(shv_path, index);
}

namespace {
enum class Overwrite {
	Yes,
	No
};

void do_write_entries_to_file(const QString& file_path, const std::vector<shv::core::utils::ShvJournalEntry>& entries, const Overwrite overwrite)
{
	journalDebug() << "Writing" << entries.size() << "entries to" << file_path;
	if (overwrite == Overwrite::Yes) {
		QFile(file_path).remove();
	}

	shv::core::utils::ShvJournalFileWriter writer(file_path.toStdString());
	for (const auto& entry : entries) {
		writer.append(entry);
	}
}

const auto DIRTYLOG_SIZE_THRESHOLD = 1024 * 1024 * 20; // 20 MiB
}

void ShvJournalNode::trimDirtyLog(const QString& cache_dir_path, const TrimLastMS trim_last_ms)
{
	journalInfo() << "Trimming dirty log for" << cache_dir_path;
	using shv::coreqt::Utils;
	auto journal_dir_path = cache_dir_path;
	QDir cache_dir(journal_dir_path);
	auto entries = cache_dir.entryList(QDir::NoDotAndDotDot | QDir::Files, QDir::Name | QDir::Reversed);

	if (entries.empty() || entries.at(0) != DIRTY_FILENAME) {
		journalDebug() << "No dirty log, nothing to trim";
		return;
	}

	if (entries.size() == 1) {
		journalDebug() << "No logs to use for trimming";
		return;
	}

	if (!shv::core::utils::ShvJournalFileReader(dirty_log_path(journal_dir_path)).next()) {
		// No entries in the dirty log file, nothing to change.
		journalDebug() << "Dirty log empty, nothing to trim";
		return;
	}

	journalDebug() << "File used for trimming" << cache_dir.filePath(entries.at(1));

	auto newest_file_entries = read_entries_from_file(cache_dir.filePath(entries.at(1)));
	int64_t newest_entry_msec = 0;
	if (!newest_file_entries.empty()) {
		newest_entry_msec = newest_file_entries.back().epochMsec;
	}

	if (trim_last_ms == TrimLastMS::Yes) {
		// It is possible that the newest logfile contains multiple entries with the same timestamp. Because it could
		// have been written (by shvagent) in between two events that happened in the same millisecond, we can't be
		// sure whether we have all events from the last millisecond. Because of that we will discard the last
		// millisecond from the synced log, and keep it in the dirty log.
		newest_file_entries.erase(std::find_if(newest_file_entries.begin(), newest_file_entries.end(), [newest_entry_msec] (const auto& entry) {
			return entry.epochMsec == newest_entry_msec;
		}), newest_file_entries.end());

		// If we discarded all the entries from the newest file (i.e. it only
		// contained data from the same timestamp), we'll just delete it. No
		// point in having empty files. After that we don't have to do anything
		// to the dirty log, because there's no data to trim.
		if (newest_file_entries.empty()) {
			QFile(cache_dir.filePath(entries.at(1))).remove();
			return;
		}

		do_write_entries_to_file(cache_dir.filePath(entries.at(1)), newest_file_entries, Overwrite::Yes);
	}

	// Now filter dirty log's newer events.
	std::vector<shv::core::utils::ShvJournalEntry> new_dirty_log_entries;
	{
		shv::core::utils::ShvJournalFileReader reader(dirty_log_path(journal_dir_path));
		while (reader.next()) {
			if (reader.entry().epochMsec >= newest_entry_msec) {
				new_dirty_log_entries.push_back(reader.entry());
			}
		}
	}

	do_write_entries_to_file(QString::fromStdString(dirty_log_path(journal_dir_path)), new_dirty_log_entries, Overwrite::Yes);
	if (auto dirtylog_size = QFile(QString::fromStdString(dirty_log_path(journal_dir_path))).size(); dirtylog_size > DIRTYLOG_SIZE_THRESHOLD) {
		journalWarning() << "Size of" << dirty_log_path(journal_dir_path) << "is too big after trimming:" << dirtylog_size;
	}
}

namespace {
const auto RECORD_COUNT_LIMIT = 5000;
const auto MAX_ENTRIES_PER_FILE = 50000;
const auto SYNCER_MEMORY_LIMIT = 1024 /*B*/ * 1024 /*kB*/ * 100 /*MB*/;

const auto LS_FILES_RESPONSE_FILENAME = 0;
const auto LS_FILES_RESPONSE_FILETYPE = 1;
const auto LS_FILES_RESPONSE_FILESIZE = 2;

void writeEntriesToFile(ShvJournalNode* node, const std::vector<shv::core::utils::ShvJournalEntry>& downloaded_entries, const QString& cache_dir_path, const std::optional<QString>& file_name_hint)
{
	journalInfo() << "Writing" << downloaded_entries.size() << "entries for" << cache_dir_path;
	if (downloaded_entries.empty()) {
		return;
	}

	QDir dir(cache_dir_path);

	auto file_name = dir.filePath([&] {
		if (file_name_hint) {
			return file_name_hint.value();
		}

		return QString::fromStdString(shv::core::utils::ShvJournalFileReader::msecToBaseFileName(downloaded_entries.front().epochMsec)) + ".log2";
	}());

	do_write_entries_to_file(file_name, downloaded_entries, Overwrite::No);
	node->trimDirtyLog(cache_dir_path, ShvJournalNode::TrimLastMS::Yes);
}
}

class LegacyFileSyncerImpl : public QObject {
	Q_OBJECT
public:
	LegacyFileSyncerImpl(ShvJournalNode* node, const QString& slave_hp_path, const QString& cache_dir_path)
		: QObject(node)
		, slave_hp_path(slave_hp_path)
		, cache_dir_path(cache_dir_path)
		, node(node)
	{
		auto msg = "Syncing " + slave_hp_path + " via legacy getLog";
		journalInfo() << msg;
		node->appendSyncStatus(slave_hp_path, msg.toStdString());
		using shv::coreqt::Utils;
		get_log_params.withSnapshot = true;
		get_log_params.recordCountLimit = RECORD_COUNT_LIMIT;
		get_log_params.since = shv::chainpack::RpcValue::DateTime::fromMSecsSinceEpoch(QDateTime::currentDateTime().addSecs(- HistoryApp::instance()->cliOptions()->cacheInitMaxAge()).toMSecsSinceEpoch());

		QDir cache_dir(cache_dir_path);
		auto entry_list = cache_dir.entryList(QDir::NoDotAndDotDot | QDir::Files, QDir::Name | QDir::Reversed);
		if (!entry_list.empty()) {
			QString newest_file_name;

			if (entry_list.at(0) != DIRTY_FILENAME) {
				newest_file_name = entry_list.at(0);
			} else if (entry_list.size() > 1) {
				newest_file_name = entry_list.at(1);
			}

			if (!newest_file_name.isEmpty()) {
				auto newest_file_entries = read_entries_from_file(cache_dir.filePath(newest_file_name));
				if (!newest_file_entries.empty()) {
					get_log_params.since = shv::chainpack::RpcValue::DateTime::fromMSecsSinceEpoch(newest_file_entries.back().dateTime().msecsSinceEpoch() + 1);
					journalDebug() << "Newest entry" << get_log_params.since << "for" << slave_hp_path;

					// No fancy algorithm for appending the files: we'll only append if the existing file can contain
					// the whole RECORD_COUNT_LIMIT records.
					if (newest_file_entries.size() + RECORD_COUNT_LIMIT < MAX_ENTRIES_PER_FILE) {
						newest_file_entry_count = newest_file_entries.size();
						file_name_hint = newest_file_name;
						get_log_params.withSnapshot = false;
					}
				}
			}
		}
		promise.start();
		syncNext();
	}

	QFuture<void> getFuture()
	{
		return promise.future();
	}
private:

	void finish()
	{
		writeEntriesToFile(node, downloaded_entries, cache_dir_path, file_name_hint);
		node->appendSyncStatus(slave_hp_path, "Syncing done");
		promise.finish();
		deleteLater();
	}
	void syncNext()
	{
		auto call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
			->setShvPath(slave_hp_path)
			->setMethod("getLog")
			->setParams(get_log_params.toRpcValue())
			->setTimeout(60000);
		call->setParent(node);
		call->start();
		connect(call, &shv::iotqt::rpc::RpcCall::maybeResult, [this] (const auto& result, const auto& error) {
			if (error.isValid()) {
				auto err = "Error retrieving logs via getLog for: " + slave_hp_path.toStdString() + " " + error.message();
				node->appendSyncStatus(slave_hp_path, err);
				finish();
				return;
			}

			get_log_params.withSnapshot = false;

			shv::core::utils::ShvMemoryJournal result_log;
			result_log.loadLog(result);
			if (result_log.isEmpty()) {
				finish();
				return;
			}

			const auto& remote_entries = result_log.entries();

			auto msg = "Loaded " + QString::number(remote_entries.size()) + " log entries for " + slave_hp_path;
			journalInfo() << msg;
			node->appendSyncStatus(slave_hp_path, msg.toStdString());
			auto newest_entry = remote_entries.back().epochMsec;
			for (const auto& entry : remote_entries) {
				// We're skipping all entries from the last millisecond, and retrieve it again, otherwise we can't be
				// sure if we got all from the same millisecond.
				if (entry.epochMsec == newest_entry) {
					break;
				}
				downloaded_entries.push_back(entry);
			}

			if (remote_entries.size() < RECORD_COUNT_LIMIT) {
				finish();
				return;
			}

			if (downloaded_entries.size() + newest_file_entry_count > MAX_ENTRIES_PER_FILE) {
				writeEntriesToFile(node, downloaded_entries, cache_dir_path, file_name_hint);
				downloaded_entries.clear();
				newest_file_entry_count = 0;
				file_name_hint.reset();
				get_log_params.withSnapshot = true;
			}
			get_log_params.since = remote_entries.back().dateTime();
			syncNext();
		});
	}

	QPromise<void> promise;
	QString slave_hp_path;
	QString cache_dir_path;
	ShvJournalNode* node;
	std::vector<shv::core::utils::ShvJournalEntry> downloaded_entries;
	size_t newest_file_entry_count = 0;
	std::optional<QString> file_name_hint;
	shv::core::utils::ShvGetLogParams get_log_params;
};

class FileSyncer : public QObject {
	Q_OBJECT
public:
	enum class SyncType {
		Device,
		HP3
	};

	FileSyncer(
		ShvJournalNode* node,
		const std::string& shv_path,
		const std::string& site_sync_path,
		const QString& cache_dir_path,
		const SyncType sync_type)
		: QObject(node)
		, m_node(node)
		, m_shvPath(QString::fromStdString(shv_path))
	{
		m_promise.start();
		auto msg = "Syncing " + shv_path + " via file synchronization";
		journalInfo() << msg;
		m_node->appendSyncStatus(m_shvPath, msg);
		auto shvjournal_suffix =
			sync_type == SyncType::Device ? site_sync_path : ".local/history/_shvjournal";
		auto shvjournal_shvpath = shv::coreqt::utils::joinPath(m_shvPath, shvjournal_suffix);

		impl_doSync(m_shvPath, cache_dir_path, shvjournal_shvpath, "");
	}

	void writeFiles()
	{
		QMapIterator iter(m_downloadedFiles);
		while (iter.hasNext()) {
			iter.next();
			QFile log_file(iter.key());
			QFileInfo(log_file).dir().mkpath(".");
			if (log_file.open(QFile::WriteOnly | QFile::Append)) {
				auto blob = iter.value().asBlob();
				journalDebug() << "Writing" << iter.key();
				log_file.write(reinterpret_cast<const char*>(blob.data()), static_cast<qsizetype>(blob.size()));
			} else {
				auto err = "Couldn't open " + iter.key() + " for writing";
				journalError() << err;
				m_node->appendSyncStatus(m_shvPath, err.toStdString());
			}

		}

		m_downloadedFiles.clear();
		current_memory_usage = 0;
	}

	void impl_doSync(const QString& slave_hp_path, const QString& cache_dir_path, const QString& shvjournal_shvpath, const QString& path_prefix)
	{
		auto call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
			->setShvPath(shvjournal_shvpath)
			->setMethod("lsfiles")
			->setParams(cp::RpcValue::Map{{"size", true}});
		call->setParent(m_node);
		call->start();
		m_counter++;

		connect(call, &shv::iotqt::rpc::RpcCall::maybeResult, this, [this, shvjournal_shvpath, cache_dir_path, slave_hp_path, path_prefix] (const auto& file_list, const auto& error) {
			auto decrementer = qScopeGuard([this, cache_dir_path, file_list, path_prefix] {
				m_counter--;
				if (!file_list.asList().empty()) {
					m_toTrim.push_back(QDir(cache_dir_path).filePath(path_prefix));
				}
				if (m_counter == 0) {
					downloadNext();
				}
			});
			if (error.isValid()) {
				auto err = "Couldn't retrieve filelist from: " + shvjournal_shvpath.toStdString() + " " + error.message();
				journalError() << err;
				m_node->appendSyncStatus(slave_hp_path, err);
				return;
			}

			journalDebug() << "Got filelist from" << shvjournal_shvpath;

			if (!file_list.asList().empty() && file_list.asList().front().asList().at(LS_FILES_RESPONSE_FILETYPE) == "d") {
				for (const auto& current_directory : file_list.asList()) {
					auto dir_name = QString::fromStdString(current_directory.asList().at(LS_FILES_RESPONSE_FILENAME).asString());
					impl_doSync(slave_hp_path, cache_dir_path, shv::coreqt::utils::joinPath(shvjournal_shvpath, dir_name), shv::coreqt::utils::joinPath(path_prefix, dir_name));
				}
				return;
			}

			QDir cache_dir(QDir(cache_dir_path).filePath(path_prefix));
			QString newest_file_name;
			int64_t newest_file_name_ms = QDateTime::currentDateTime().addSecs(- HistoryApp::instance()->cliOptions()->cacheInitMaxAge()).toMSecsSinceEpoch();
			auto entry_list = cache_dir.entryList(QDir::NoDotAndDotDot | QDir::Files, QDir::Name | QDir::Reversed);
			if (!entry_list.empty()) {
				if (entry_list.at(0) != DIRTY_FILENAME) {
					newest_file_name = entry_list.at(0);
					newest_file_name_ms = shv::core::utils::ShvJournalFileReader::fileNameToFileMsec(newest_file_name.toStdString());
				} else if (entry_list.size() > 1) {
					newest_file_name = entry_list.at(1);
					newest_file_name_ms = shv::core::utils::ShvJournalFileReader::fileNameToFileMsec(newest_file_name.toStdString());
				}
			}

			auto oldest_dirtylog_entry_ms = [&cache_dir_path] () -> int64_t {
				if (auto dirtylog_file = QFile(QString::fromStdString(dirty_log_path(cache_dir_path))); dirtylog_file.exists()) {
					auto reader = shv::core::utils::ShvJournalFileReader(dirty_log_path(cache_dir_path));
					if (reader.next()) {
						return reader.entry().epochMsec;
					}
				}

				return std::numeric_limits<int64_t>::max();
			}();

			// If there's an old enough dirtylog entry, use that as the threshold for the oldest files (rather than
			// what's in cacheInitMaxAge. This will fix issues where syncing didn't work for a file, and the remote
			// files got old enough.
			newest_file_name_ms = std::min(oldest_dirtylog_entry_ms, newest_file_name_ms);
			journalDebug() << "Newest file for" << shv::coreqt::utils::joinPath(slave_hp_path, path_prefix) << "is" << newest_file_name;

			for (const auto& current_file : file_list.asList()) {
				if (current_memory_usage > SYNCER_MEMORY_LIMIT) {
					writeFiles();
				};
				auto file_name = QString::fromStdString(current_file.asList().at(LS_FILES_RESPONSE_FILENAME).asString());
				if (file_name == DIRTY_FILENAME) {
					continue;
				}

				auto parsed_file_ms = shv::core::utils::ShvJournalFileReader::fileNameToFileMsec(file_name.toStdString(), false);
				if (parsed_file_ms == -1) {
					journalWarning() << "Skipping file" << file_name << "and any after it, because it has an invalid filename";
					break;
				}

				if (parsed_file_ms < newest_file_name_ms) {
					journalDebug() << "Skipping" << file_name << "because it's older than our newest file" << newest_file_name;
					continue;
				}

				auto full_file_name = QDir(cache_dir_path).filePath(shv::coreqt::utils::joinPath(path_prefix, file_name));
				QFile file(full_file_name);
				auto local_size = static_cast<int>(file.size());
				auto remote_size = current_file.asList().at(LS_FILES_RESPONSE_FILESIZE).toInt();

				auto sites_log_file = shv::coreqt::utils::joinPath(shvjournal_shvpath, file_name);
				{
					auto msg = sites_log_file;
					auto log_writer = qScopeGuard([&] {
						journalInfo() << msg;
						m_node->appendSyncStatus(slave_hp_path, msg.toStdString());
					});
					if (file.exists()) {
						if (local_size == remote_size) {
							msg += ": up-to-date";
							continue;
						}

						if (local_size > remote_size) {
							msg += ": WARNING - local size is larger than the remote size";
							continue;
						}
					}
					msg += QStringLiteral(": syncing (remote size: %1 local size: %2)").arg(QString::number(remote_size), (file.exists() ? QString::number(local_size) : "<doesn't exist>"));

				}

				journalDebug() << "Enqueuing" << sites_log_file << "offset:" << local_size;
				m_downloadQueue.push_back(DownloadJob{
					.slave_hp_path = slave_hp_path,
					.sites_log_file = sites_log_file,
					.remote_size = remote_size,
					.local_size = local_size,
					.full_file_name = full_file_name,
				});
			}

		});
	}

	QFuture<void> getFuture()
	{
		return m_promise.future();
	}

private:

	struct DownloadJob {
		QString slave_hp_path;
		QString sites_log_file;
		int remote_size;
		int local_size;
		QString full_file_name;
	};

	enum class FileReadParamApi {
		Map,
		List,
	};

	void downloadNext(std::optional<FileReadParamApi> file_read_param_api = {})
	{
		auto finish = [this] {
			writeFiles();

			for (const auto& dir_path : m_toTrim) {
				m_node->trimDirtyLog(dir_path, ShvJournalNode::TrimLastMS::No);
			}

			journalDebug() << "No more files to download for" << m_shvPath.toStdString();
			m_node->appendSyncStatus(m_shvPath, "Syncing done");
			m_promise.finish();
			deleteLater();
			return;
		};

		if (m_downloadQueue.empty()) {
			finish();
			return;
		}

		if (!file_read_param_api.has_value()) {
			journalDebug() << "Detecting file:read parm API for:" << m_shvPath.toStdString();
			auto call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
				->setShvPath(m_downloadQueue.front().sites_log_file)
				->setMethod("dir")
				->setParams("sha1");
			call->setParent(m_node);

			connect(call, &shv::iotqt::rpc::RpcCall::maybeResult, this, [this, finish] (const auto& dir_result, const auto& error) {
				if (error.isValid()) {
					journalWarning() << "Couldn't retrieve file:read param API for:" << m_shvPath.toStdString();
					finish();
					return;
				}

				downloadNext(dir_result.isIMap() ? FileReadParamApi::List : FileReadParamApi::Map);
			});
			call->start();
			return;
		}

		journalDebug() << "Downloading next file for" << m_shvPath.toStdString();
		auto next = m_downloadQueue.front();
		auto call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
			->setShvPath(next.sites_log_file)
			->setMethod("read")
			->setTimeout(60000);

		if (next.remote_size == 0) {
			auto msg = call->shvPath() + ": is an empty file, skipping read(), and creating it locally";
			journalWarning() << msg;
			m_node->appendSyncStatus(m_shvPath, msg);
			m_downloadedFiles.insert(next.full_file_name, shv::chainpack::RpcValue::Blob{});
			call->deleteLater();
			m_downloadQueue.pop_front();
			downloadNext(file_read_param_api);
			return;
		}

		QtFuture::connect(call, &shv::iotqt::rpc::RpcCall::maybeResult).then([this, next] (const std::tuple<shv::chainpack::RpcValue, shv::chainpack::RpcError>& result_or_error) {
			auto [result, retrieve_error] = result_or_error;
			auto site_path = std::filesystem::path{next.sites_log_file.toStdString()}.remove_filename();
			auto skip_rest = [this, site_path] {
				for (auto it = m_downloadQueue.begin(); it != m_downloadQueue.end(); /* nothing */) {
					if (auto shv_path = (*it).sites_log_file; shv_path.startsWith(site_path.c_str())) {
						journalDebug() << "Skipping" << shv_path;
						it = m_downloadQueue.erase(it);
					} else {
						++it;
					}
				}
			};
			auto msg = next.sites_log_file.toStdString() + ": ";
			if (retrieve_error.code() != shv::chainpack::RpcError::NoError) {
				msg += retrieve_error.message();
				journalError() << msg;
				m_node->appendSyncStatus(next.slave_hp_path, msg);
				msg = "Skipping all files from " + site_path.string() + " because " + next.sites_log_file.toStdString() + " download failed to finish";
				journalWarning() << msg;
				m_node->appendSyncStatus(next.slave_hp_path, msg);
				skip_rest();
			} else if (result.metaValue("offset").toInt() != next.local_size || result.metaValue("size").toInt() != next.remote_size - next.local_size) {
				msg += "got invalid size or offset, got offset: " + std::to_string(result.metaValue("offset").toInt()) + " expected offset: " + std::to_string(next.local_size) + " got size: " + std::to_string(result.metaValue("size").toInt()) + " expected size: " + std::to_string(next.remote_size - next.local_size);
				m_node->appendSyncStatus(next.slave_hp_path, msg);
				journalWarning() << msg;
				msg = "Skipping all files from " + site_path.string() + " because " + next.sites_log_file.toStdString() + " had an expected offset and/or size";
				m_node->appendSyncStatus(next.slave_hp_path, msg);
				journalWarning() << msg;
				skip_rest();
			} else {
				msg += "successfully synced";
				m_downloadedFiles.insert(next.full_file_name, result);
				m_node->appendSyncStatus(next.slave_hp_path, msg);
				journalInfo() << msg;
			}
			downloadNext();
		});

		switch (file_read_param_api.value()) {
		case FileReadParamApi::Map:
			call->setParams(cp::RpcValue::Map{{"offset", next.local_size}});
			break;
		case FileReadParamApi::List:
			call->setParams(cp::RpcValue::List{next.local_size, next.remote_size - next.local_size});
			break;
		}
		call->start();
		m_downloadQueue.pop_front();
	}

	ShvJournalNode* m_node;
	const QString m_shvPath; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
	int current_memory_usage = 0;

	std::deque<DownloadJob> m_downloadQueue;

	QMap<QString, cp::RpcValue> m_downloadedFiles;
	std::vector<QString> m_toTrim;
	QPromise<void> m_promise;
	int m_counter = 0;
};

void ShvJournalNode::syncLog(const std::string& shv_path, const std::function<void(const shv::chainpack::RpcValue::List&)> site_list_cb, const std::function<void()> success_cb)
{
	using shv::coreqt::Utils;
	QVector<QFuture<void>> all_synced;
	shv::chainpack::RpcValue::List sites_to_be_synced;
	for (const auto& slave_hp : m_slaveHps) {
		auto slave_hp_path_qstr = QString::fromStdString(slave_hp.shv_path);

		if (!shv_path.empty() && !slave_hp.shv_path.starts_with(shv_path)) {
			continue;
		}

		journalDebug() << "Found matching site to sync:" << slave_hp.shv_path;

		if (m_syncInProgress.value(slave_hp_path_qstr, false)) {
			journalInfo() << slave_hp_path_qstr << "is already being synced, skipping";
			continue;
		}

		m_syncInProgress[slave_hp_path_qstr] = true;
		QScopeGuard lock_guard([this, &slave_hp_path_qstr] {
			m_syncInProgress[slave_hp_path_qstr] = false;
		});

		// We know all the site nodes, so let's check what's our sync type.
		auto sync_type = m_siteNodes.contains(slave_hp.shv_path) ? FileSyncer::SyncType::Device : FileSyncer::SyncType::HP3;

		// We shouldn't sync pushlogs, if they're our directly connected device.
		if (sync_type == FileSyncer::SyncType::Device && slave_hp.log_type == LogType::PushLog) {
			continue;
		}
		sites_to_be_synced.push_back(slave_hp.shv_path);

		resetSyncStatus(slave_hp_path_qstr);
		if (sync_type == FileSyncer::SyncType::Device && slave_hp.log_type == LogType::Legacy) {
			all_synced.push_back((new LegacyFileSyncerImpl(this, slave_hp_path_qstr, slave_hp.cache_dir_path))->getFuture());
		} else {
			all_synced.push_back((new FileSyncer(this, slave_hp.shv_path, slave_hp.site_sync_path, slave_hp.cache_dir_path, sync_type))->getFuture());
		}
	}

	site_list_cb(sites_to_be_synced);
	if (!all_synced.empty()) {
		QtFuture::whenAll(all_synced.begin(), all_synced.end()).then([success_cb] (const auto&) {
			success_cb();
		});
	}
}

cp::RpcValue ShvJournalNode::callMethodRq(const cp::RpcRequest &rq)
{
	auto method = rq.method().asString();
	if (method == M_SYNC_LOG) {
		auto params = rq.params();

		auto shv_path = rq.params().asMap().value("shvPath", rq.params().asString());
		if (shv_path.asString().empty()) {
			auto response = rq.makeResponse();
			response.setError(shv::chainpack::RpcError::createMethodCallExceptionError("No path given."));
			HistoryApp::instance()->rpcConnection()->sendRpcMessage(response);
			return {};
		}

		journalInfo() << "Syncing shvjournal" << m_remoteLogShvPath;
		auto sites_resp = std::make_shared<shv::chainpack::RpcValue>();

		auto onSites = [sites_resp, params, rq, shv_path] (const shv::chainpack::RpcValue::List& sites) {
			if (!params.asMap().value("waitForFinished", false).toBool()) {
				auto response = rq.makeResponse();
				response.setResult(sites);
				HistoryApp::instance()->rpcConnection()->sendRpcMessage(response);
			} else {
				*sites_resp = sites;
			}
		};
		auto onSuccess = [sites_resp, params, rq] {
			if (params.asMap().value("waitForFinished", false).toBool()) {
				auto response = rq.makeResponse();
				response.setResult(*sites_resp);
				HistoryApp::instance()->rpcConnection()->sendRpcMessage(response);
			}
		};

		syncLog(params.isString() ? params.asString() : params.asMap().value("shvPath", "").asString(), onSites, onSuccess);

		return {};
	}

	if (method == M_SYNC_INFO) {
		auto sync_info = syncInfo();
		if (auto filter = rq.params().asString(); !filter.empty()) {
			for (auto it = sync_info.begin(); it != sync_info.end(); /*nothing*/) {
				if (!it->first.starts_with(filter)) {
					it = sync_info.erase(it);
					continue;
				}
				it++;
			}
		}
		return sync_info;
	}

	if (method == M_SANITIZE_LOG) {
		sanitizeSize();
		return "Cache sanitization done";
	}

	if (method == M_TOTAL_LOG_SIZE) {
		return get_log_info(m_cacheDirPath).total_size;
	}

	if (method == M_LOG_SIZE_LIMIT) {
		return HistoryApp::instance()->totalCacheSizeLimit();
	}

	if (method == M_LOG_USAGE) {
		return static_cast<double>(get_log_info(m_cacheDirPath).total_size) / static_cast<double>(HistoryApp::instance()->totalCacheSizeLimit()) * 100;
	}

	return Super::callMethodRq(rq);
}

const QString& ShvJournalNode::cacheDirPath() const
{
	return m_cacheDirPath;
}

QMap<QString, bool>& ShvJournalNode::syncInProgress()
{
	return m_syncInProgress;
}

void ShvJournalNode::resetSyncStatus(const QString& shv_path)
{
	m_syncInfo.setValue(shv_path.toStdString(), shv::chainpack::RpcValue::Map {
		{"status", shv::chainpack::RpcValue::List{}},
		{"timestamp", shv::chainpack::RpcValue::DateTime::now()}
	});
}

void ShvJournalNode::appendSyncStatus(const QString& shv_path, const std::string& status)
{
	m_syncInfo.at(shv_path.toStdString()).set("timestamp", shv::chainpack::RpcValue::DateTime::now());
	auto current_status = m_syncInfo.at(shv_path.toStdString()).asMap().value("status").asList();
	current_status.push_back(status);
	m_syncInfo.at(shv_path.toStdString()).set("status", current_status);
}

const shv::chainpack::RpcValue::Map& ShvJournalNode::syncInfo()
{
	return m_syncInfo;
}

const std::vector<SlaveHpInfo>& ShvJournalNode::slaveHps() const
{
	return m_slaveHps;
}

const std::set<std::string>& ShvJournalNode::siteNodes() const
{
	return m_siteNodes;
}

#include "shvjournalnode.moc"
