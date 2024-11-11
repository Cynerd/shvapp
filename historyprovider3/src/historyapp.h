#pragma once

#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/iotqt/node/shvnode.h>

#include <QCoreApplication>
#include <QElapsedTimer>

#include <QFuture>

class AppCliOptions;
class QDir;
class QFileInfo;
class QTimer;

namespace shv { namespace chainpack { class RpcMessage; }}
namespace shv { namespace iotqt { namespace rpc { class DeviceConnection; }}}
namespace shv { namespace iotqt { namespace node { class ShvNodeTree; }}}

class SiteNode;
class ShvJournalNode;
class ValueCacheNode;

class AppRootNode : public shv::iotqt::node::ShvRootNode
{
	using Super = shv::iotqt::node::ShvRootNode;
public:
	explicit AppRootNode(QObject* parent = nullptr) : Super(parent) {}

	size_t methodCount(const StringViewList& shv_path) override;
	const shv::chainpack::MetaMethod* metaMethod(const StringViewList& shv_path, size_t ix) override;

	shv::chainpack::RpcValue callMethodRq(const shv::chainpack::RpcRequest& rq) override;
	shv::chainpack::RpcValue callMethod(const StringViewList& shv_path, const std::string& method, const shv::chainpack::RpcValue& params, const shv::chainpack::RpcValue& user_id) override;
};

class HistoryApp : public QCoreApplication
{
	Q_OBJECT
private:
	using Super = QCoreApplication;
public:
	HistoryApp(int& argc, char** argv, AppCliOptions* cli_opts, shv::iotqt::rpc::DeviceConnection* rpc_connection);
	~HistoryApp() Q_DECL_OVERRIDE;

	static HistoryApp* instance();
	shv::iotqt::rpc::DeviceConnection* rpcConnection() const {return m_rpcConnection;}
	int64_t totalCacheSizeLimit() const {return m_totalCacheSizeLimit;}
	void setTotalCacheSizeLimit(int64_t size_limit) {m_totalCacheSizeLimit = size_limit;}
	ShvJournalNode* shvJournalNode() {return m_shvJournalNode;}
	ValueCacheNode* valueCacheNode() {return m_valueCacheNode;}
	SiteNode* siteNode(const std::string& path);

	AppCliOptions* cliOptions() {return m_cliOptions;}

	QString uptime() const;
	QFuture<void> reloadSites();

private:
	void onBrokerConnectedChanged(bool is_connected);
	void onRpcMessageReceived(const shv::chainpack::RpcMessage& msg);
	QFuture<void> initializeShvTree();
	void deinitializeShvTree();

private:
	QElapsedTimer m_uptime;
	shv::iotqt::rpc::DeviceConnection* m_rpcConnection = nullptr;
	AppCliOptions* m_cliOptions;
	shv::iotqt::node::ShvNodeTree* m_shvTree = nullptr;
	AppRootNode* m_root = nullptr;
	bool m_isBrokerConnected = false;
	int64_t m_totalCacheSizeLimit;
	QList<SiteNode*> m_siteNodes;
	ShvJournalNode* m_shvJournalNode = nullptr;
	ValueCacheNode* m_valueCacheNode = nullptr;
	QTimer* m_sanitizerTimer = nullptr;
	bool m_loadingSites = false;
};
