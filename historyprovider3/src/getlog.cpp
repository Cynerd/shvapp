#include "getlog.h"

#include <shv/core/log.h>
#include <shv/core/utils/abstractshvjournal.h>
#include <shv/core/utils/patternmatcher.h>
#include <shv/core/utils/shvlogheader.h>

#include <functional>
#include <ranges>

#define logWGetLog() shvCWarning("GetLog")
#define logIGetLog() shvCInfo("GetLog")
#define logMGetLog() shvCMessage("GetLog")
#define logDGetLog() shvCDebug("GetLog")

namespace shv::core::utils {
using chainpack::RpcValue;

namespace {
[[nodiscard]] RpcValue as_shared_path(chainpack::RpcValue::Map& path_map, const std::string &path)
{
	RpcValue ret;
	if (auto it = path_map.find(path); it == path_map.end()) {
		ret = static_cast<int>(path_map.size());
		logDGetLog() << "Adding record to path cache:" << path << "-->" << ret.toCpon();
		path_map[path] = ret;
	} else {
		ret = it->second;
	}
	return ret;
}

struct GetLogContext {
	RpcValue::Map pathCache;
	ShvGetLogParams params;
};

enum class Status {
	Ok,
	RecordCountLimitHit
};

void append_log_entry(RpcValue::List& log, const ShvJournalEntry &e, GetLogContext& ctx)
{
	logDGetLog() << "\t append_log_entry:" << e.toRpcValueMap().toCpon();
	log.push_back(RpcValue::List{
		e.dateTime(),
		ctx.params.withPathsDict ? as_shared_path(ctx.pathCache, e.path) : e.path,
		e.value,
		e.shortTime == ShvJournalEntry::NO_SHORT_TIME ? RpcValue(nullptr) : RpcValue(e.shortTime),
		(e.domain.empty() || e.domain == ShvJournalEntry::DOMAIN_VAL_CHANGE) ? RpcValue(nullptr) : e.domain,
		e.valueFlags,
		e.userId.empty() ? RpcValue(nullptr) : RpcValue(e.userId),
	});
}

auto snapshot_to_entries(const ShvSnapshot& snapshot, const bool since_last, const int64_t params_since_msec, GetLogContext& ctx)
{
	RpcValue::List res;
	logMGetLog() << "\t writing snapshot, record count:" << snapshot.keyvals.size();
	if (!snapshot.keyvals.empty()) {
		auto since_res = params_since_msec;

		if (since_last) {
			for (const auto& kv : snapshot.keyvals) {
				since_res = std::max(kv.second.epochMsec, since_res);
			}
		}
		for (const auto& kv : snapshot.keyvals) {
			ShvJournalEntry e = kv.second;
			e.epochMsec = since_res;
			e.setSnapshotValue(true);
			// erase EVENT flag in the snapshot values,
			// they can trigger events during reply otherwise
			e.setSpontaneous(false);
			logDGetLog() << "\t writing SNAPSHOT entry:" << e.toRpcValueMap().toCpon();
			append_log_entry(res, e, ctx);
		}
	}

	return res;
}
}

[[nodiscard]] chainpack::RpcValue get_log(const std::vector<std::function<ShvJournalFileReader()>>& readers, const ShvGetLogParams& orig_params)
{
	logIGetLog() << "========================= getLog ==================";
	logIGetLog() << "params:" << orig_params.toRpcValue().toCpon();
	GetLogContext ctx;
	ctx.params = orig_params;

	// Asking for a snapshot without supplying `since` is invalid. We'll continue as if the user didn't want a snapshot.
	if (ctx.params.withSnapshot && !ctx.params.since.isValid()) {
		logWGetLog() << "Asking for a snapshot without `since` is invalid. No snapshot will be created.";
		ctx.params.withSnapshot = false;
	}

	ShvSnapshot snapshot;
	RpcValue::List result_log;
	PatternMatcher pattern_matcher(ctx.params);

	ShvLogHeader log_header;
	auto record_count_limit = std::min(ctx.params.recordCountLimit, AbstractShvJournal::DEFAULT_GET_LOG_RECORD_COUNT_LIMIT);

	log_header.setRecordCountLimit(record_count_limit);
	log_header.setWithSnapShot(ctx.params.withSnapshot);
	log_header.setWithPathsDict(ctx.params.withPathsDict);

	const auto params_since_msec =
		ctx.params.since.isDateTime() ? ctx.params.since.toDateTime().msecsSinceEpoch() : 0;

	const auto params_until_msec =
		ctx.params.until.isDateTime() ? ctx.params.until.toDateTime().msecsSinceEpoch() : std::numeric_limits<int64_t>::max();

	std::optional<ShvJournalEntry> last_entry;

	int record_count = 0;

	for (const auto& readerFn : readers) {
		auto reader = readerFn();
		while(reader.next()) {
			const auto& entry = reader.entry();
			last_entry = entry;

			if (!pattern_matcher.match(entry)) {
				logDGetLog() << "\t SKIPPING:" << entry.path << "because it doesn't match" << ctx.params.pathPattern;
				continue;
			}

			if (ctx.params.isSinceLast() || entry.epochMsec < params_since_msec) {
				logDGetLog() << "\t saving SNAPSHOT entry:" << entry.toRpcValueMap().toCpon();
				AbstractShvJournal::addToSnapshot(snapshot, entry);
			}

			if (entry.epochMsec >= params_until_msec) {
				goto exit_nested_loop;
			}

			if (entry.epochMsec >= params_since_msec && !ctx.params.isSinceLast()) {
				if ((static_cast<int>(snapshot.keyvals.size()) + record_count + 1) > ctx.params.recordCountLimit) {
					log_header.setRecordCountLimitHit(true);
					goto exit_nested_loop;
				}

				append_log_entry(result_log, entry, ctx);
				record_count++;
			}
		}
	}
exit_nested_loop:

	auto snapshot_entries = [&] {
		if (ctx.params.withSnapshot) {
			return snapshot_to_entries(snapshot, ctx.params.isSinceLast(), params_since_msec, ctx);
		}

		if (last_entry && ctx.params.isSinceLast()) {
			RpcValue::List res;
			append_log_entry(res, *last_entry, ctx);
			return res;
		}
		return RpcValue::List{};
	}();

	RpcValue::List result_entries = snapshot_entries;
	std::copy(result_log.begin(), result_log.end(), std::back_inserter(result_entries));

	if (result_entries.empty()) {
		log_header.setSince(ctx.params.since.isValid() ? ctx.params.since : ctx.params.until);
		log_header.setUntil(ctx.params.until.isValid() ? ctx.params.until : ctx.params.since);
	} else {
		log_header.setSince(ctx.params.since.isValid() && !ctx.params.isSinceLast() ? ctx.params.since : result_entries.front().asList().value(ShvLogHeader::Column::Timestamp));
		log_header.setUntil(log_header.recordCountLimitHit() || !ctx.params.until.isValid() || ctx.params.isSinceLast() ?
							result_entries.back().asList().value(ShvLogHeader::Column::Timestamp) :
							ctx.params.until);
	}

	logDGetLog() << "result since:" << log_header.sinceCRef().toCpon() << "result until:" << log_header.untilCRef().toCpon();

	log_header.setDateTime(RpcValue::DateTime::now());
	log_header.setLogParams(orig_params);
	log_header.setRecordCount(static_cast<int>(result_entries.size()));

	if (ctx.params.withPathsDict) {
		logMGetLog() << "Generating paths dict size:" << ctx.pathCache.size();
		log_header.setPathDict([&] {
			RpcValue::IMap path_dict;
			for(const auto &kv : ctx.pathCache) {
				path_dict[kv.second.toInt()] = kv.first;
			}
			return path_dict;
		}());
	}

	auto rpc_value_result = RpcValue{result_entries};
	rpc_value_result.setMetaData(log_header.toMetaData());

	return rpc_value_result;
}
}
