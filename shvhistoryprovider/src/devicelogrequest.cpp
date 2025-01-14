#include "application.h"
#include "appclioptions.h"
#include "devicemonitor.h"
#include "devicelogrequest.h"
#include "logdir.h"

#include <shv/core/utils/shvjournalfilereader.h>
#include <shv/core/utils/shvjournalfilewriter.h>
#include <shv/core/utils/shvlogfilereader.h>
#include <shv/core/utils/shvmemoryjournal.h>
#include <shv/coreqt/exception.h>
#include <shv/coreqt/log.h>
#include <shv/iotqt/rpc/rpcresponsecallback.h>

namespace cp = shv::chainpack;
using namespace shv::core::utils;

DeviceLogRequest::DeviceLogRequest(const QString &site_path, const QDateTime &since, const QDateTime &until, QObject *parent)
	: Super(parent)
	, m_sitePath(site_path)
	, m_since(since)
	, m_until(until)
	, m_logDir(site_path)
	, m_askElesys(true)
	, m_appendLog(false)
{
	m_askElesys = Application::instance()->deviceMonitor()->isElesysDevice(m_sitePath);
}

void DeviceLogRequest::getChunk()
{
	try {
		m_appendLog = false;
		if (m_since.isValid()) {
			LogDir log_dir(m_sitePath);
			QStringList all_files = log_dir.findFiles(m_since.addMSecs(-1), m_since);
			if (all_files.count() == 1) {
				std::ifstream in_file;
				in_file.open(all_files[0].toStdString(), std::ios::in | std::ios::binary);
				if (in_file) {
					cp::RpcValue::MetaData metadata;
					cp::ChainPackReader(in_file).read(metadata);
					int orig_record_count = metadata.value("recordCount").toInt();
					cp::RpcValue orig_until_cp = metadata.value("until");
					if (!orig_until_cp.isDateTime()) {
						SHV_QT_EXCEPTION("Missing until in file " + all_files[0]);
					}
					int64_t orig_until = orig_until_cp.toDateTime().msecsSinceEpoch();
					m_appendLog = (orig_until == m_since.toMSecsSinceEpoch() && orig_record_count < Application::CHUNK_RECORD_COUNT);
				}
			}
		}

		auto *conn = Application::instance()->deviceConnection();
		int rq_id = conn->nextRequestId();
		shv::iotqt::rpc::RpcResponseCallBack *cb = new shv::iotqt::rpc::RpcResponseCallBack(conn, rq_id, this);
		connect(cb, &shv::iotqt::rpc::RpcResponseCallBack::finished, this, &DeviceLogRequest::onChunkReceived);
		cb->start(2 * 60 * 1000);
		if (m_askElesys) {
			if (m_since.isValid() && m_since.date().daysTo(QDate::currentDate()) < 3) {
				m_askElesys = false;
			}
		}
		if (m_askElesys && m_sitePath.startsWith("test/") && !Application::instance()->cliOptions()->test()) {
			m_askElesys = false;
		}
		QString path;
		cp::RpcValue params;
		if (Application::instance()->deviceMonitor()->isPushLogDevice(m_sitePath)) {
			QString sync_log_broker = Application::instance()->deviceMonitor()->syncLogBroker(m_sitePath);
			if (sync_log_broker.isEmpty()) {
				shvError() << "cannot get log for push log device" << m_sitePath << "because source HP is not set";
				Q_EMIT finished(false);
				return;
			}
			path = "history@" + sync_log_broker + ">:/shv/" + m_sitePath;
			params = logParams(!m_appendLog).toRpcValue();
		}
		else if (m_askElesys) {
			path = m_sitePath;
			if (Application::instance()->cliOptions()->test()) {
				if (path.startsWith("test/")) {
					path = path.mid(5);
				}
			}
			cp::RpcValue::Map param_map;
			param_map["shvPath"] = cp::RpcValue::fromValue(path);
			param_map["logParams"] = logParams(!m_appendLog).toRpcValue();

			path = QString::fromStdString(Application::instance()->cliOptions()->elesysPath());
			params = param_map;
		}
		else {
			path = "shv/" + m_sitePath;
			params = logParams(!m_appendLog).toRpcValue();
		}

		conn->callShvMethod(rq_id, path.toStdString(), cp::Rpc::METH_GET_LOG, params);
	}
	catch (const std::exception &ex) {
		shvError() << "error on get chunk" << ex.what();
		Q_EMIT finished(false);
	}
}

void DeviceLogRequest::onChunkReceived(const shv::chainpack::RpcResponse &response)
{
	try {
		if (!response.isValid()) {
			SHV_EXCEPTION("invalid response");
		}
		if (response.isError()) {
			SHV_EXCEPTION((m_askElesys ? "elesys error: " : "device error: ") + m_sitePath.toStdString() +
						  " " + response.error().message());
		}

		const cp::RpcValue &result = response.result();
		ShvMemoryJournal log;
		log.loadLog(result);

		cp::RpcValue meta_since = result.metaValue("since");
		cp::RpcValue meta_until = result.metaValue("until");

		if (meta_until.isNull()) {
			if (log.entries().size() == 0) {   //no data obtained from device, let it for next time
				Q_EMIT finished(true);
				return;
			}
			meta_until = log.entries()[log.size() - 1].dateTime();
		}

		if (m_since.isValid() && m_until.isValid() && log.entries().size() == 0) {
			log.setSince(cp::RpcValue::fromValue(m_since));
			log.append(ShvJournalEntry{
						   ShvJournalEntry::PATH_DATA_MISSING,
						   ShvJournalEntry::DATA_MISSING_NOT_EXISTS,
						   ShvJournalEntry::DOMAIN_SHV_SYSTEM,
						   ShvJournalEntry::NO_SHORT_TIME,
						   ShvJournalEntry::NO_VALUE_FLAGS,
						   m_since.toMSecsSinceEpoch()
					   });
			log.append(ShvJournalEntry{
						   ShvJournalEntry::PATH_DATA_MISSING,
						   "",
						   ShvJournalEntry::DOMAIN_SHV_SYSTEM,
						   ShvJournalEntry::NO_SHORT_TIME,
						   ShvJournalEntry::NO_VALUE_FLAGS,
						   m_until.toMSecsSinceEpoch() - 1
					   });
		}

		bool is_finished = static_cast<int>(log.size()) < Application::CHUNK_RECORD_COUNT;

		if (!meta_since.isDateTime() || !meta_until.isDateTime()) {
			SHV_QT_EXCEPTION("Received invalid log from " + m_sitePath + ", missing since or until");
		}
		QDateTime until = rpcvalue_cast<QDateTime>(meta_until);

		while (log.size() > 0 && log.entries()[log.size() - 1].epochMsec == until.toMSecsSinceEpoch()) {
			log.removeLastEntry();
		}

		if (is_finished && m_askElesys) {
			m_askElesys = false;
			is_finished = false;
		}
		shvInfo() << "received log for" << m_sitePath << "with" << log.size() << "records"
				  << "since" << meta_since.toCpon()
				  << "until" << meta_until.toCpon();
		if (log.size()) {
			if (m_appendLog) {
				appendToPreviousFile(log, until);
				if (!m_since.isValid() && log.size() == 0) {
					fixFirstLogFile();
				}
			}
			else {
				if (Application::instance()->deviceMonitor()->isElesysDevice(m_sitePath) || !log.hasSnapshot()) {
					prependPreviousFile(log);  //predradime minuly soubor aby se prehral do snapshotu
				}
				saveToNewFile(log, until);
			}
			if (m_logDir.existsDirtyLog()) {
				trimDirtyLog(until);
			}
		}
		m_since = until;

		if (is_finished) {
			Q_EMIT finished(true);
			return;
		}
		getChunk();
	}
	catch (const shv::core::Exception &e) {
		shvError() << e.message();
		Q_EMIT finished(false);
	}
}

void DeviceLogRequest::exec()
{
	getChunk();
}

ShvGetLogParams DeviceLogRequest::logParams(bool with_snapshot) const
{
	ShvGetLogParams params;
	params.since = cp::RpcValue::fromValue(m_since);
	params.until = cp::RpcValue::fromValue(m_until);
	params.recordCountLimit = Application::CHUNK_RECORD_COUNT;
	params.withSnapshot = with_snapshot;
	params.withTypeInfo = true;
	return params;
}

void DeviceLogRequest::appendToPreviousFile(ShvMemoryJournal &log, const QDateTime &until)
{
	LogDir log_dir(m_sitePath);
	QStringList all_files = log_dir.findFiles(m_since.addMSecs(-1), m_since);
	std::ifstream in_file;
	in_file.open(all_files[0].toStdString(), std::ios::in | std::ios::binary);
	cp::ChainPackReader log_reader(in_file);
	std::string error;
	cp::RpcValue orig_log = log_reader.read(&error);
	cp::RpcValue hp_metadata = orig_log.metaValue("HP");
	if (!error.empty() || !orig_log.isList()) {
		QFile(all_files[0]).remove();
		shvError() << "error on parsing log file" << all_files[0] << "deleting it (" << error << ")";
		SHV_QT_EXCEPTION("Cannot parse file " + all_files[0]);
	}

	ShvMemoryJournal joined_log;
	joined_log.loadLog(orig_log);

	for (const auto &entry : log.entries()) {
		joined_log.append(entry);
	}
	QString temp_filename = all_files[0];
	temp_filename.replace(".chp", ".tmp");

	QFile temp_file(temp_filename);
	if (!temp_file.open(QFile::WriteOnly | QFile::Truncate)) {
		SHV_QT_EXCEPTION("Cannot open file " + temp_file.fileName());
	}
	ShvGetLogParams joined_params;
	joined_params.recordCountLimit = 2 * Application::CHUNK_RECORD_COUNT;
	joined_params.withSnapshot = true;
	joined_params.withTypeInfo = true;

	cp::RpcValue result = joined_log.getLog(joined_params);
	result.setMetaValue("until", cp::RpcValue::fromValue(until));
	if (hp_metadata.isMap()) {
		result.setMetaValue("HP", hp_metadata);
	}
	temp_file.write(QByteArray::fromStdString(result.toChainPack()));
	temp_file.close();

	QFile old_log_file(all_files[0]);
	if (!old_log_file.remove()) {
		SHV_QT_EXCEPTION("Cannot remove file " + all_files[0]);
	}
	if (!temp_file.rename(old_log_file.fileName())) {
		SHV_QT_EXCEPTION("Cannot rename file " + temp_file.fileName());
	}
}

void DeviceLogRequest::prependPreviousFile(ShvMemoryJournal &log)
{
	LogDir log_dir(m_sitePath);
	QStringList all_files = log_dir.findFiles(m_since.addMSecs(-1), m_since);
	if (all_files.count() == 1) {
		ShvMemoryJournal new_log;
		new_log.setTypeInfo(log.typeInfo());
		ShvLogFileReader previous_log(all_files[0].toStdString());
		while (previous_log.next()) {
			new_log.append(previous_log.entry());
		}
		for (const auto &entry : log.entries()) {
			new_log.append(entry);
		}
		log = new_log;
	}
}

void DeviceLogRequest::saveToNewFile(ShvMemoryJournal &log, const QDateTime &until)
{
	cp::RpcValue log_cp = log.getLog(logParams());

	QDateTime since;
	if (m_since.isValid()) {
		since = m_since;
	}
	else {
		since = rpcvalue_cast<QDateTime>(log_cp.metaValue("since"));
		log_cp.setMetaValue("HP", cp::RpcValue::Map{{ "firstLog", true }});
	}
	log_cp.setMetaValue("until", cp::RpcValue::fromValue(until));
	QFile file(LogDir(m_sitePath).filePath(since));
	if (!file.open(QFile::WriteOnly)) {
		SHV_QT_EXCEPTION("Cannot open file " + file.fileName());
	}
	file.write(QByteArray::fromStdString(log_cp.toChainPack()));
	file.close();
}

void DeviceLogRequest::trimDirtyLog(const QDateTime &until)
{

	qint64 until_msec = until.toMSecsSinceEpoch();
	QString temp_filename = "dirty.tmp";
	ShvJournalFileReader dirty_reader(m_logDir.dirtyLogPath().toStdString());
	if (m_logDir.exists(temp_filename)) {
		if (!m_logDir.remove(temp_filename)) {
			SHV_QT_EXCEPTION("cannot remove file " + m_logDir.absoluteFilePath(temp_filename));
		}
	}

	if (dirty_reader.next() && dirty_reader.entry().epochMsec < until_msec) {
		ShvJournalFileWriter dirty_writer(m_logDir.absoluteFilePath(temp_filename).toStdString());
		dirty_writer.append(ShvJournalEntry{
								ShvJournalEntry::PATH_DATA_DIRTY,
								true,
								ShvJournalEntry::DOMAIN_SHV_SYSTEM,
								ShvJournalEntry::NO_SHORT_TIME,
								ShvJournalEntry::NO_VALUE_FLAGS,
								until_msec
							});
		int64_t old_start_ts = 0LL;
		while (dirty_reader.next()) {
			const ShvJournalEntry &entry = dirty_reader.entry();
			if (!old_start_ts) {
				old_start_ts = entry.epochMsec;
			}
			if (entry.epochMsec > until_msec) {
				if (entry.path != ShvJournalEntry::PATH_DATA_MISSING) {
					dirty_writer.append(entry);
				}
			}
		}
		if (!m_logDir.remove(m_logDir.dirtyLogName())) {
			SHV_QT_EXCEPTION("Cannot remove file " + m_logDir.dirtyLogPath());
		}
		if (!m_logDir.rename(temp_filename, m_logDir.dirtyLogName())) {
			SHV_QT_EXCEPTION("Cannot rename file " + m_logDir.absoluteFilePath(temp_filename));
		}
		auto *conn = Application::instance()->deviceConnection();
		if (until_msec != old_start_ts && conn->state() == shv::iotqt::rpc::DeviceConnection::State::BrokerConnected) {
			conn->sendShvSignal((m_sitePath + "/" + Application::DIRTY_LOG_NODE + "/" + Application::START_TS_NODE).toStdString(),
								cp::Rpc::SIG_VAL_CHANGED, cp::RpcValue::DateTime::fromMSecsSinceEpoch(until_msec));
		}
	}
}

void DeviceLogRequest::fixFirstLogFile()
{
	QStringList log_files = m_logDir.findFiles(QDateTime(), QDateTime());
	if (log_files.count()) {
		QFile first_file(log_files[0]);
		if (first_file.open(QFile::ReadOnly)) {
			cp::RpcValue log = cp::RpcValue::fromChainPack(first_file.readAll().toStdString());
			log.setMetaValue("HP", cp::RpcValue::Map{{ "firstLog", true }});
			first_file.close();
			if (first_file.open(QFile::WriteOnly | QFile::Truncate)) {
				first_file.write(QByteArray::fromStdString(log.toChainPack()));
			}
		}
	}
}
