#include "application.h"
#include "devicemonitor.h"
#include "getlogmerge.h"
#include "logdir.h"
#include "logdirreader.h"
#include "siteitem.h"

#include <QDateTime>

#include <shv/core/utils/shvjournalfilereader.h>
#include <shv/core/utils/shvlogfilereader.h>
#include <shv/coreqt/exception.h>
#include <shv/coreqt/log.h>
#include <shv/iotqt/rpc/rpc.h>

namespace cp = shv::chainpack;
using namespace shv::core::utils;

GetLogMerge::GetLogMerge(const QString &shv_path, const shv::core::utils::ShvGetLogParams &log_params)
	: m_shvPath(shv_path)
	, m_logParams(log_params)
{
	const SiteItem *site_item = Application::instance()->deviceMonitor()->sites()->itemByShvPath(shv_path);
	if (!site_item) {
		SHV_EXCEPTION("Invalid shv_path");
	}
	if (qobject_cast<const SitesHPDevice *>(site_item)) {
		m_shvPaths << m_shvPath;
	}
	else {
		for (const SitesHPDevice *device : site_item->findChildren<const SitesHPDevice*>()) {
			m_shvPaths << device->shvPath();
		}
	}
}

shv::chainpack::RpcValue GetLogMerge::getLog()
{
	QDateTime since = rpcvalue_cast<QDateTime>(m_logParams.since);
	QDateTime until = rpcvalue_cast<QDateTime>(m_logParams.until);

	int64_t first_record_since = 0LL;

	struct ReaderInfo {
		bool used = false;
		bool exhausted = false;
	};

	shv::core::utils::ShvGetLogParams first_step_params;
	shv::core::utils::ShvGetLogParams final_params;
	if (!m_logParams.pathPattern.empty()) {
		first_step_params.pathPattern = m_logParams.pathPattern;
		first_step_params.pathPatternType = m_logParams.pathPatternType;
	}
	if (since.isValid()) {
		if (m_logParams.withSnapshot) {
			final_params.since = m_logParams.since;
		}
		else {
			first_step_params.since = m_logParams.since;
		}
	}
	shv::core::utils::ShvLogFilter first_step_filter(first_step_params);
	shv::core::utils::ShvLogFilter final_filter(final_params);

	QVector<LogDirReader*> readers;
	QVector<ReaderInfo> reader_infos;
	for (const QString &shv_path : m_shvPaths) {
		int prefix_length = shv_path.length() - m_shvPath.length();
		LogDirReader *reader = new LogDirReader(shv_path, prefix_length, since, until, m_logParams.withSnapshot);
		if (reader->next()) {
			readers << reader;
			reader_infos << ReaderInfo();
		}
		else {
			delete reader;
		}
	}

	int64_t until_msecs = until.isNull() ? std::numeric_limits<int64_t>::max() : until.toMSecsSinceEpoch();

	int record_count = 0;
	int usable_readers = readers.count();
	while (usable_readers) {
		int64_t oldest = std::numeric_limits<int64_t>::max();
		int oldest_index = -1;
		for (int i = 0; i < readers.count(); ++i) {
			if (!reader_infos[i].exhausted && readers[i]->entry().epochMsec < oldest) {
				oldest = readers[i]->entry().epochMsec;
				oldest_index = i;
			}
		}
		LogDirReader *reader = readers[oldest_index];
		ShvJournalEntry entry = reader->entry();
		if (entry.epochMsec >= until_msecs) {
			break;
		}
		entry.path = reader->pathPrefix() + entry.path;
		if (first_step_filter.match(entry)) {
			if (!first_record_since) {
				first_record_since = entry.epochMsec;
			}
			m_mergedLog.append(entry);
			if (final_filter.match(entry)) {
				++record_count;
				if (record_count > m_logParams.recordCountLimit) {
					break;
				}
			}
		}
		reader_infos[oldest_index].used = true;
		if (!reader->next()) {
			reader_infos[oldest_index].exhausted = true;
			--usable_readers;
		}
	}
	cp::RpcValue::Map dirty_log_info;
	for (int i = 0; i < readers.count(); ++i) {
		if (reader_infos[i].used) {
			m_mergedLog.setTypeInfo(readers[i]->typeInfo(), readers[i]->pathPrefix());
			if (readers[i]->dirtyLogBegin()) {
				dirty_log_info[readers[i]->pathPrefix()] = cp::RpcValue::Map{{ "startTS", cp::RpcValue::DateTime::fromMSecsSinceEpoch(readers[i]->dirtyLogBegin()) }};
			}
		}
	}
	qDeleteAll(readers);

	ShvGetLogParams params = m_logParams;
	params.pathPattern = std::string();
	cp::RpcValue res = m_mergedLog.getLog(params);
	if (!since.isNull() && first_record_since && first_record_since > since.toMSecsSinceEpoch()) {
		res.setMetaValue("since", cp::RpcValue::DateTime::fromMSecsSinceEpoch(first_record_since));
	}
	res.setMetaValue("dirtyLog", dirty_log_info);
	return res;
}
