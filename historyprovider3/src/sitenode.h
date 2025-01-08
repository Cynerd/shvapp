#pragma once

#include "logtype.h"

#include <shv/core/utils/shvalarm.h>
#include <shv/core/utils/shvgetlogparams.h>
#include <shv/core/utils/shvtypeinfo.h>
#include <shv/iotqt/node/shvnode.h>
#include <shv/iotqt/node/localfsnode.h>

struct AlarmLog;

class SiteNode : public shv::iotqt::node::ShvNode
{
	Q_OBJECT

	using Super = shv::iotqt::node::ShvNode;

public:
	static constexpr auto M_ALARM_LOG = "alarmLog";

	SiteNode(const std::string& node_id, const std::string& journal_cache_dir, const LogType log_type, ShvNode* parent = nullptr);

	size_t methodCount(const StringViewList& shv_path) override;
	const shv::chainpack::MetaMethod* metaMethod(const StringViewList& shv_path, size_t ix) override;
	shv::chainpack::RpcValue callMethod(const StringViewList& shv_path, const std::string& method, const shv::chainpack::RpcValue& params, const shv::chainpack::RpcValue& user_id) override;
	qint64 calculateCacheDirSize() const;

	struct AlarmWithTimestamp {
		shv::core::utils::ShvAlarm alarm;
		shv::chainpack::RpcValue::DateTime timestamp;
		bool stale;
		shv::chainpack::RpcValue toRpcValue() const;
	};

	std::vector<shv::core::utils::ShvAlarm> alarms() const;
	bool alarmIsStale(const std::string& path) const;

	enum class OnlineStatus {
		Unknown,
		Offline,
		Online,
	};
	OnlineStatus onlineStatus() const;

	AlarmLog alarmLog(const shv::chainpack::RpcValue& params);

private:
	void setOnlineStatus(const OnlineStatus online_status);
	shv::chainpack::RpcValue getLog(const shv::core::utils::ShvGetLogParams& get_log_params);

	std::string m_journalCacheDir;
	LogType m_logType;
	shv::chainpack::RpcValue::List m_pushLogDebugLog;
	std::variant<shv::core::utils::ShvTypeInfo, std::string> m_typeInfo = std::string{"typeInfo not yet initialized"};
	std::vector<AlarmWithTimestamp> m_alarms;
	OnlineStatus m_onlineStatus = OnlineStatus::Unknown;

	shv::core::utils::ShvAlarm::Severity m_overallAlarm = shv::core::utils::ShvAlarm::Severity::Invalid;
};

struct AlarmLog {
	std::string site;
	std::vector<SiteNode::AlarmWithTimestamp> snapshot;
	std::vector<SiteNode::AlarmWithTimestamp> events;

	shv::chainpack::RpcValue::Map toRpcValue() const
	{
		auto asList = [] (const auto& input) {
			shv::chainpack::RpcValue::List ret;
			std::ranges::transform(input, std::back_inserter(ret), &SiteNode::AlarmWithTimestamp::toRpcValue);
			return ret;
		};

		shv::chainpack::RpcValue::Map res{
			{site, shv::chainpack::RpcValue::Map {
				{"snapshot", asList(snapshot)},
				{"events", asList(events)},
			}}
		};
		return res;
	}
};

