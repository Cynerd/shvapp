#ifndef SHVJOURNALNODE_H
#define SHVJOURNALNODE_H

#include "logtype.h"

#include <shv/iotqt/node/shvnode.h>
#include <shv/iotqt/node/localfsnode.h>

#include <set>

struct SlaveHpInfo {
	LogType log_type;
	std::string shv_path;
	QString cache_dir_path;
};

class ShvJournalNode : public shv::iotqt::node::LocalFSNode
{
	Q_OBJECT

	using Super = shv::iotqt::node::LocalFSNode;

public:
	ShvJournalNode(const std::vector<SlaveHpInfo>& slave_hps, const std::set<std::string>& leaf_nodes, ShvNode* parent = nullptr);

	size_t methodCount(const StringViewList& shv_path) override;
	const shv::chainpack::MetaMethod* metaMethod(const StringViewList& shv_path, size_t ix) override;
	shv::chainpack::RpcValue callMethodRq(const shv::chainpack::RpcRequest &rq) override;
	void trimDirtyLog(const QString& slave_hp_path, const QString& cache_dir_path);
	void syncLog(const std::string& shv_path, const std::function<void(const shv::chainpack::RpcValue::List&)> site_list_cb, const std::function<void()> success_cb);

	const QString& cacheDirPath() const;
	const std::vector<SlaveHpInfo>& slaveHps() const;
	const std::set<std::string>& leafNodes() const;
	QMap<QString, bool>& syncInProgress();
	const shv::chainpack::RpcValue::Map& syncInfo();
	void updateSyncStatus(const QString& shv_path, const std::string& status);

private:
	void onRpcMessageReceived(const shv::chainpack::RpcMessage &msg);

	std::vector<SlaveHpInfo> m_slaveHps;
	std::set<std::string> m_leafNodes;
	QString m_remoteLogShvPath;
	QString m_cacheDirPath;
	QMap<QString, bool> m_syncInProgress;

	/* Format of m_syncInfo:
	 * {
	 * 		[site]: DataValue
	 * 			timetamp: DateTime
	 * 			status: string
	 * 		}
	 * }
	 */
	shv::chainpack::RpcValue::Map m_syncInfo;
};
#endif /*SHVJOURNALNODE_H*/
