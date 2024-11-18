#include "historyapp.h"
#include "appclioptions.h"
#include "valuecachenode.h"
#include "utils.h"
#include "src/shvjournalnode.h"
#include "src/sitenode.h"

#include <shv/iotqt/rpc/rpccall.h>
#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/iotqt/node/shvnodetree.h>
#include <shv/iotqt/node/localfsnode.h>
#include <shv/coreqt/log.h>
#include <shv/coreqt/utils.h>
#include <shv/chainpack/tunnelctl.h>
#include <shv/chainpack/metamethod.h>
#include <shv/chainpack/cponreader.h>

#include <shv/core/stringview.h>

#include <QtConcurrentRun>

#include <QDirIterator>
#include <QTimer>

#include <QFuture>
#include <QPromise>

#include <set>

using namespace std;
namespace cp = shv::chainpack;
namespace si = shv::iotqt;

namespace {
const auto METH_GET_VERSION = "version";
const auto METH_GIT_COMMIT = "gitCommit";
const auto METH_SHV_VERSION = "shvVersion";
const auto METH_SHV_GIT_COMMIT = "shvGitCommit";
const auto METH_UPTIME = "uptime";
const auto METH_RELOAD_SITES = "reloadSites";
const std::vector<cp::MetaMethod> meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
	{cp::Rpc::METH_APP_NAME, cp::MetaMethod::Flag::IsGetter, {}, "RpcValue", cp::AccessLevel::Browse},
	{cp::Rpc::METH_DEVICE_ID, cp::MetaMethod::Flag::IsGetter, {}, "RpcValue", cp::AccessLevel::Browse},
	{cp::Rpc::METH_DEVICE_TYPE, cp::MetaMethod::Flag::IsGetter, {}, "RpcValue", cp::AccessLevel::Browse},
	{METH_GET_VERSION, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{METH_GIT_COMMIT, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{METH_SHV_VERSION, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{METH_SHV_GIT_COMMIT, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{METH_UPTIME, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read },
	{METH_RELOAD_SITES, cp::MetaMethod::Flag::None, {}, "Bool", cp::AccessLevel::Write },
};
}

size_t AppRootNode::methodCount(const StringViewList& shv_path)
{
	if (shv_path.empty()) {
		return meta_methods.size();
	}
	return 0;
}

const cp::MetaMethod* AppRootNode::metaMethod(const StringViewList& shv_path, size_t ix)
{
	if (shv_path.empty()) {
		if (meta_methods.size() <= ix) {
			SHV_EXCEPTION("Invalid method index: " + std::to_string(ix) + " of: " + std::to_string(meta_methods.size()));
		}
		return &(meta_methods[ix]);
	}
	return nullptr;
}

cp::RpcValue AppRootNode::callMethod(const StringViewList& shv_path, const std::string& method, const cp::RpcValue& params, const cp::RpcValue& user_id)
{
	if (shv_path.empty()) {
		if (method == cp::Rpc::METH_APP_NAME) {
			return QCoreApplication::applicationName().toStdString();
		}

		if (method == METH_GET_VERSION) {
			return QCoreApplication::applicationVersion().toStdString();
		}

		if (method == METH_UPTIME) {
			return HistoryApp::instance()->uptime().toStdString();
		}


		if(method == METH_GIT_COMMIT) {
#ifdef GIT_COMMIT
			return SHV_EXPAND_AND_QUOTE(GIT_COMMIT);
#else
			return "N/A";
#endif
		}

		if (method == METH_SHV_GIT_COMMIT) {
#ifdef SHV_GIT_COMMIT
			return SHV_EXPAND_AND_QUOTE(SHV_GIT_COMMIT);
#else
			return "N/A";
#endif
		}

		if (method == METH_SHV_VERSION) {
#ifdef SHV_VERSION
			return SHV_EXPAND_AND_QUOTE(SHV_VERSION);
#else
			return "N/A";
#endif
		}
	}
	return Super::callMethod(shv_path, method, params, user_id);
}

SiteNode* HistoryApp::siteNode(const std::string& path)
{
	auto it = std::ranges::find(m_siteNodes, path, [] (const auto& node) { return node->shvPath().asString(); });
	if (it == m_siteNodes.end()) {
		throw std::logic_error("SiteNode " + path + " not found");
	}

	return *it;
}

QFuture<void> HistoryApp::reloadSites()
{
	QElapsedTimer reload_duration;
	reload_duration.start();
	if (m_loadingSites) {
		return QtFuture::makeExceptionalFuture(std::make_exception_ptr(std::runtime_error("Sites are already being reloaded.")));
	}
	m_loadingSites = true;
	deinitializeShvTree();
	if (m_isBrokerConnected) {
		return initializeShvTree().then([this, reload_duration] {
			auto elapsed_ms = reload_duration.elapsed();
			if (elapsed_ms > 5000) {
				shvWarning().nospace() << "Loading sites took more than 5 seconds! (" << elapsed_ms << "ms)";
			}
			m_loadingSites = false;
		});
	}

	m_loadingSites = false;
	return QtFuture::makeReadyFuture();
}

cp::RpcValue AppRootNode::callMethodRq(const cp::RpcRequest& rq)
{
	if (rq.shvPath().asString().empty()) {
		if (rq.method() == cp::Rpc::METH_DEVICE_ID) {
			HistoryApp* app = HistoryApp::instance();
			const cp::RpcValue opts = app->rpcConnection()->connectionOptions();
			const cp::RpcValue::Map &dev = opts.asMap().valref(cp::Rpc::KEY_DEVICE).asMap();
			return dev.value(cp::Rpc::KEY_DEVICE_ID).toString();
		}

		if (rq.method() == cp::Rpc::METH_DEVICE_TYPE) {
			return "HistoryProvider";
		}

		if (rq.method() == METH_RELOAD_SITES) {
			HistoryApp::instance()->reloadSites().then([rq] {
				auto response = rq.makeResponse();
				response.setResult("Sites reloaded.");
				HistoryApp::instance()->rpcConnection()->sendRpcMessage(response);
			}).onFailed([rq] (std::exception& err) {
				auto error = cp::RpcResponse::Error::create(cp::RpcResponse::Error::MethodCallException, err.what());
				auto response = rq.makeResponse();
				response.setError(error);
				HistoryApp::instance()->rpcConnection()->sendRpcMessage(shv::chainpack::RpcMessage(response));
			});
			return {};
		}
	}
	return Super::callMethodRq(rq);
}


namespace {
enum class SlaveFound {
	Yes,
	No
};

const cp::MetaMethod ALARM_LOG_METHOD{SiteNode::M_ALARM_LOG,  cp::MetaMethod::Flag::None, "Map", "List", cp::AccessLevel::Read, {}, "Desc"};
class AggregateNode : public shv::iotqt::node::ShvNode {
	Q_OBJECT

public:
	using shv::iotqt::node::ShvNode::ShvNode;
	size_t methodCount(const StringViewList& shv_path) override
	{
		return ShvNode::methodCount(shv_path) + 1;
	}

	const cp::MetaMethod* metaMethod(const StringViewList& shv_path, size_t index) override
	{
		if (index >= ShvNode::methodCount(shv_path)) {
			return &ALARM_LOG_METHOD;
		}

		return ShvNode::metaMethod(shv_path, index);
	}

	shv::chainpack::RpcValue callMethodRq(const shv::chainpack::RpcRequest &rq) override
	{
		if (rq.method() == SiteNode::M_ALARM_LOG) {
			QtConcurrent::run([this, rq] {
				const auto site_nodes = this->findChildren<SiteNode*>();
				AlarmLog res_log;
				for (const auto& site_node : site_nodes) {
					AlarmLog log;
					try {
						log = site_node->alarmLog(rq.params());
					} catch (shv::core::Exception& ex) {
						auto resp = rq.makeResponse();
						resp.setError(shv::chainpack::RpcError(ex.what(), shv::chainpack::RpcError::ErrorCode::MethodCallException));
						return resp;
					} catch (std::exception& ex) {
						shvError() << "Unexpected error when getting alarm log for" << site_node->shvPath() << "-" << ex.what();
						auto resp = rq.makeResponse();
						resp.setError(shv::chainpack::RpcError("Internal error when loading alarm log", shv::chainpack::RpcError::ErrorCode::InternalError));
						return resp;
					}
					auto add_path_prefix = [site_node] (auto& alarms) {
						for (auto& alarm_with_ts : alarms) {
							alarm_with_ts.alarm.path = shv::core::utils::joinPath(site_node->shvPath().asString(), alarm_with_ts.alarm.path);
						}
					};
					add_path_prefix(log.snapshot);
					add_path_prefix(log.events);
					std::ranges::copy(log.snapshot, std::back_inserter(res_log.snapshot));
					std::ranges::copy(log.events, std::back_inserter(res_log.events));
				}

				std::ranges::sort(res_log.events, [] (const auto& a, const auto& b) {
					return a.timestamp < b.timestamp;
				});

				auto resp = rq.makeResponse();
				resp.setResult(res_log.toRpcValue());
				return resp;
			}).then(this, [this] (const QFuture<shv::chainpack::RpcResponse> & resp) {
				shvDebug() << "Aggregate get log on" << shvPath() << "done";
				HistoryApp::instance()->rpcConnection()->sendRpcMessage(resp.result());
			});

			return {};
		}

		return shv::iotqt::node::ShvNode::callMethodRq(rq);
	}
};

void createTree(shv::iotqt::node::ShvNode* parent_node, const cp::RpcValue::Map& tree, const QString& node_name, std::string journal_cache_dir, std::vector<SlaveHpInfo>& slave_hps, std::set<std::string>& site_nodes, SlaveFound slave_found)
{
	shv::iotqt::node::ShvNode* node;
	// We don't want a "shv" directory in out directory tree.
	if (node_name != "shv") {
		journal_cache_dir = shv::core::utils::joinPath(journal_cache_dir, node_name.toStdString());
	}

	// We don't want to create an "shv" node, we'll use the root node directly (parent_node is the root node here).
	if (node_name == "shv") {
		node = parent_node;
	} else if (auto meta_node = tree.value("_meta").asMap(); (meta_node.hasKey("HP") || meta_node.hasKey("HP3"))) {
		auto hp_node = meta_node.value("HP", meta_node.value("HP3")).asMap();
		auto log_type =
			hp_node.value("pushLog").toBool() ? LogType::PushLog :
			meta_node.hasKey("HP") ? LogType::Legacy :
			LogType::Normal;

		bool is_site = meta_node.hasKey("HP") || meta_node.value("HP3").asMap().value("type", "device").asString() == "device";
		std::string site_sync_path;
		if (is_site) {
			site_sync_path = meta_node.value("HP3").asMap().value("syncPath", ".app/history").asString();
			node = new SiteNode(node_name.toStdString(), journal_cache_dir, log_type, parent_node);
		} else {
			node = new AggregateNode(node_name.toStdString(), parent_node);
		}

		auto log_source_shv_path = shv::core::utils::joinPath(std::string{"shv"}, node->shvPath().asString());
		if (is_site) {
			site_nodes.insert(log_source_shv_path);
		}

		if (slave_found != SlaveFound::Yes) {
			slave_found = SlaveFound::Yes;
			slave_hps.push_back(SlaveHpInfo {
				.log_type = log_type,
				.shv_path = log_source_shv_path,
				.site_sync_path = site_sync_path,
				.cache_dir_path = QString::fromStdString(journal_cache_dir)
			});
		}

	} else {
		node = new AggregateNode(node_name.toStdString(), parent_node);
	}

	for (const auto& [k, v] : tree) {
		if (k == "_meta") {
			continue;
		}

		if (v.type() == cp::RpcValue::Type::Map) {
			createTree(node, v.asMap(), QString::fromStdString(k), journal_cache_dir, slave_hps, site_nodes, slave_found);
		}
	}
}

auto parse_size_option(const std::string& size_option, bool* success)
{
	*success = true;

	QString limit = QString::fromStdString(size_option);
	char suffix = 0;
	if (limit.endsWith('k') || limit.endsWith('M') || limit.endsWith('G')) {
		suffix = limit[limit.length() - 1].toLatin1();
		limit = limit.left(limit.length() - 1);
	}

	auto cache_size_limit_bytes = limit.toLongLong(success);
	if (!success) {
		shvError() << "Invalid cacheSizeLimit" << size_option;
		return 0LL;
	}

	if (suffix) {
		switch (suffix) {
		case 'k':
			cache_size_limit_bytes *= 1024;
			break;
		case 'M':
			cache_size_limit_bytes *= 1024;
			cache_size_limit_bytes *= 1024;
			break;
		case 'G':
			cache_size_limit_bytes *= 1024;
			cache_size_limit_bytes *= 1024;
			cache_size_limit_bytes *= 1024;
			break;
		default:
			*success = false;
			shvError() << "Invalid cacheSizeLimit suffix" << suffix;
			break;
		}
	}

	return cache_size_limit_bytes;
}
}

HistoryApp::HistoryApp(int& argc, char** argv, AppCliOptions* cli_opts, shv::iotqt::rpc::DeviceConnection* rpc_connection)
	: Super(argc, argv)
	  , m_rpcConnection(rpc_connection)
	  , m_cliOptions(cli_opts)
{
	m_rpcConnection->setParent(this);

	if (!cli_opts->user_isset()) {
		cli_opts->setUser("iot");
	}

	if (!cli_opts->password_isset()) {
		cli_opts->setPassword("lub42DUB");
	}

	bool success;
	auto size_limit_bytes = parse_size_option(cli_opts->journalCacheSizeLimit(), &success);

	if (!success) {
		auto default_limit_bytes = cli_opts->option("app.cacheSizeLimit").defaultValue().asString();
		shvError() << "Invalid cacheSizeLimit:" << cli_opts->journalCacheSizeLimit() << "using the default:" << default_limit_bytes;
		size_limit_bytes = parse_size_option(default_limit_bytes, &success);
	}

	shvInfo() << "Cache size limit:" << size_limit_bytes << "bytes";
	shvInfo() << "Sanitizer interval:" << cli_opts->journalSanitizerInterval() << "seconds";
	shvInfo() << "Log max age:" << cli_opts->logMaxAge() << "seconds";

	m_rpcConnection->setCliOptions(cli_opts);
	m_totalCacheSizeLimit = size_limit_bytes;

	connect(m_rpcConnection, &si::rpc::ClientConnection::brokerConnectedChanged, this, &HistoryApp::onBrokerConnectedChanged);
	connect(m_rpcConnection, &si::rpc::ClientConnection::rpcMessageReceived, this, &HistoryApp::onRpcMessageReceived);

	QTimer::singleShot(0, m_rpcConnection, &si::rpc::ClientConnection::open);
	m_uptime.start();
}

HistoryApp::~HistoryApp()
{
	shvInfo() << "destroying history provider application";
}

HistoryApp* HistoryApp::instance()
{
	return qobject_cast<HistoryApp*>(QCoreApplication::instance());
}

QFuture<void> HistoryApp::initializeShvTree()
{
	m_root = new AppRootNode();
	m_shvTree = new si::node::ShvNodeTree(m_root, this);
	m_valueCacheNode = new ValueCacheNode(m_root);
	connect(m_shvTree->root(), &si::node::ShvRootNode::sendRpcMessage, m_rpcConnection, &si::rpc::ClientConnection::sendRpcMessage);

	auto call = shv::iotqt::rpc::RpcCall::create(HistoryApp::instance()->rpcConnection())
		->setShvPath("sites")
		->setMethod("getSites");

	QPromise<void> promise;
	promise.start();
	auto future = promise.future();
	connect(call, &shv::iotqt::rpc::RpcCall::maybeResult, this, [this, promise = std::move(promise)] (const auto& result, const auto& error) mutable {
		if (error.isValid()) {
			shvError() << "Couldn't retrieve sites:" << error.message() << ", trying again";
			QTimer::singleShot(3000, this, [this, promise = std::move(promise)] () mutable {
				initializeShvTree().then([promise =std::move(promise)] () mutable {
					promise.finish();
				});
			});
			return;
		}

		std::vector<SlaveHpInfo> slave_hps;
		std::set<std::string> site_nodes;
		createTree(m_root, result.asMap(), "shv", cliOptions()->journalCacheRoot(), slave_hps, site_nodes, SlaveFound::No);

		auto conn = HistoryApp::instance()->rpcConnection();
		conn->callMethodSubscribe("shv", shv::chainpack::Rpc::SIG_MOUNTED_CHANGED);
		for (const auto& it : slave_hps) {
			if (it.log_type != LogType::PushLog) {
				conn->callMethodSubscribe(it.shv_path, shv::chainpack::Rpc::SIG_VAL_CHANGED);
				conn->callMethodSubscribe(it.shv_path, shv::chainpack::Rpc::SIG_COMMAND_LOGGED);
			}
		}

		m_shvJournalNode = new ShvJournalNode(slave_hps, site_nodes, m_root);

		m_siteNodes = m_shvTree->findChildren<SiteNode*>();
		if (!m_siteNodes.empty()) {
			m_sanitizerTimer = new QTimer(this);
			connect(m_sanitizerTimer, &QTimer::timeout, m_shvJournalNode, &ShvJournalNode::sanitizeSize);
			m_sanitizerTimer->start(m_cliOptions->journalSanitizerInterval() * 1000);
		}

		promise.finish();
	});
	call->start();

	return future;
}

void HistoryApp::deinitializeShvTree()
{
	delete m_sanitizerTimer;
	m_sanitizerTimer = nullptr;
	m_siteNodes.clear();
	delete m_shvTree;
	m_shvTree = nullptr;
}

void HistoryApp::onBrokerConnectedChanged(bool is_connected)
{
	m_isBrokerConnected = is_connected;
	reloadSites();
}

void HistoryApp::onRpcMessageReceived(const cp::RpcMessage& msg)
{
	shvLogFuncFrame() << msg.toCpon();
	if (msg.isRequest()) {
		cp::RpcRequest rq(msg);
		shvDebug() << "RPC request received:" << rq.toPrettyString();
		if (m_shvTree->root()) {
			m_shvTree->root()->handleRpcRequest(rq);
		}
	} else if (msg.isResponse()) {
		cp::RpcResponse rp(msg);
		shvDebug() << "RPC response received:" << rp.toPrettyString();
	} else if (msg.isSignal()) {
		cp::RpcSignal nt(msg);
		shvDebug() << "RPC notify received:" << nt.toPrettyString();
	}
}

QString HistoryApp::uptime() const
{
	qint64 elapsed = m_uptime.elapsed();
	int ms = static_cast<int>(elapsed) % 1000;
	elapsed /= 1000;
	int sec = static_cast<int>(elapsed) % 60;
	elapsed /= 60;
	int min = static_cast<int>(elapsed) % 60;
	elapsed /= 60;
	int hour = static_cast<int>(elapsed) % 24;
	int day = static_cast<int>(elapsed) / 24;
	return QString("%1 day(s) %2:%3:%4.%5")
		.arg(day)
		.arg(hour, 2, 10, QChar('0'))
		.arg(min, 2, 10, QChar('0'))
		.arg(sec, 2, 10, QChar('0'))
		.arg(ms, 3, 10, QChar('0'));
}

#include "historyapp.moc"
