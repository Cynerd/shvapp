#include "application.h"
#include "appclioptions.h"
#include "devicemonitor.h"
#include "getlogmerge.h"
#include "rootnode.h"
#include "siteitem.h"
#include "logdir.h"
#include "logsanitizer.h"

#include <shv/chainpack/metamethod.h>
#include <shv/coreqt/log.h>
#include <shv/coreqt/exception.h>
#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/iotqt/node/shvnodetree.h>
#include <shv/iotqt/rpc/rpc.h>
#include <shv/core/utils/shvjournalfilereader.h>

#include <QElapsedTimer>

namespace cp = shv::chainpack;
using namespace shv::core::utils;

static const char METH_GET_VERSION[] = "version";
static const char METH_RELOAD_SITES[] = "reloadSites";
static const char METH_GET_UPTIME[] = "uptime";
static const char METH_GET_LOGVERBOSITY[] = "logVerbosity";
static const char METH_SET_LOGVERBOSITY[] = "setLogVerbosity";
static const char METH_TRIM_DIRTY_LOG[] = "trim";
static const char METH_SANITIZE_LOG_CACHE[] = "sanitizeLogCache";
static const char METH_CHECK_LOG_CACHE[] = "checkLogCache";

static std::vector<cp::MetaMethod> root_meta_methods {
	{ cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_DEVICE_ID, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
	{ cp::Rpc::METH_APP_NAME, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
    { METH_GET_VERSION, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
	{ METH_RELOAD_SITES, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_COMMAND },
	{ METH_GET_UPTIME, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
	{ METH_GET_LOGVERBOSITY, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::IsGetter, cp::Rpc::ROLE_READ },
	{ METH_SET_LOGVERBOSITY, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::IsSetter, cp::Rpc::ROLE_COMMAND },
};

static std::vector<cp::MetaMethod> branch_meta_methods {
	{ cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_GET_LOG, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
};

static std::vector<cp::MetaMethod> leaf_meta_methods {
	{ cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_GET_LOG, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
	{ METH_SANITIZE_LOG_CACHE, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_SERVICE },
	{ METH_CHECK_LOG_CACHE, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_SERVICE },
};

static std::vector<cp::MetaMethod> dirty_log_meta_methods {
	{ cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ METH_TRIM_DIRTY_LOG, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_READ },
};

static std::vector<cp::MetaMethod> start_ts_meta_methods {
	{ cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::Flag::None, cp::Rpc::ROLE_BROWSE },
	{ cp::Rpc::METH_GET, cp::MetaMethod::Signature::RetVoid, cp::MetaMethod::Flag::IsGetter, cp::Rpc::ROLE_READ },
	{ cp::Rpc::SIG_VAL_CHANGED, cp::MetaMethod::Signature::VoidParam, cp::MetaMethod::Flag::IsSignal, cp::Rpc::ROLE_READ },
};

RootNode::RootNode(QObject *parent)
	: Super(parent)
{
}

const std::vector<shv::chainpack::MetaMethod> &RootNode::metaMethods(const shv::iotqt::node::ShvNode::StringViewList &shv_path)
{
	if (shv_path.empty()) {
		return root_meta_methods;
	}
	else {
		if (shv_path[shv_path.size() - 1] == Application::DIRTY_LOG_NODE) {
			return dirty_log_meta_methods;
		}
		else if (shv_path[shv_path.size() - 1] == Application::START_TS_NODE) {
			return start_ts_meta_methods;
		}
		else if (!Application::instance()->deviceMonitor()->monitoredDevices().contains(QString::fromStdString(shv_path.join('/')))) {
			return branch_meta_methods;
		}
		return leaf_meta_methods;
	}
}

size_t RootNode::methodCount(const StringViewList &shv_path)
{
	return metaMethods(shv_path).size();
}

const shv::chainpack::MetaMethod *RootNode::metaMethod(const StringViewList &shv_path, size_t ix)
{
	const std::vector<cp::MetaMethod> &meta_methods = metaMethods(shv_path);
	if (meta_methods.size() <= ix) {
		SHV_EXCEPTION("Invalid method index: " + std::to_string(ix) + " of: " + std::to_string(meta_methods.size()));
	}
	return &(meta_methods[ix]);
}

shv::chainpack::RpcValue RootNode::ls(const shv::core::StringViewList &shv_path, const shv::chainpack::RpcValue &params)
{
	Q_UNUSED(params);
	return ls(shv_path, 0, Application::instance()->deviceMonitor()->sites());
}

cp::RpcValue RootNode::ls(const shv::core::StringViewList &shv_path, size_t index, const SiteItem *site_item)
{
	if (shv_path.size() - index == 0) {
		cp::RpcValue::List items;
		for (int i = 0; i < site_item->children().count(); ++i) {
			items.push_back(cp::RpcValue::fromValue(site_item->children()[i]->objectName()));
		}
		if (site_item->children().count() == 0) {
			items.push_back(Application::DIRTY_LOG_NODE);
		}
		return std::move(items);
	}
	QString key = QString::fromStdString(shv_path[index].toString());
	SiteItem *child = site_item->findChild<SiteItem*>(key);
	if (child) {
		return ls(shv_path, index + 1, child);
	}
	if (index + 1 == shv_path.size() && key == Application::DIRTY_LOG_NODE) {
		cp::RpcValue::List items;
		items.push_back(Application::START_TS_NODE);
		return std::move(items);
	}
	return cp::RpcValue::List();
}

void RootNode::trimDirtyLog(const QString &shv_path)
{
	Application::instance()->logSanitizer()->trimDirtyLog(shv_path);
}

shv::chainpack::RpcValue RootNode::getStartTS(const QString &shv_path)
{
	LogDir log_dir(shv_path);
	ShvLogHeader header;
	ShvJournalFileReader dirty_log(log_dir.dirtyLogPath().toStdString(), &header);
	if (dirty_log.next()) {
		return dirty_log.entry().dateTime();
	}
	return cp::RpcValue(nullptr);
}

void RootNode::onRpcMessageReceived(const shv::chainpack::RpcMessage &msg)
{
	if (msg.isRequest()) {
		cp::RpcRequest rq(msg);
		cp::RpcResponse resp = cp::RpcResponse::forRequest(rq.metaData());
		try {
			rq.setShvPath(rq.shvPath().toString());
			shv::chainpack::RpcValue result = processRpcRequest(rq);
			if (result.isValid()) {
				resp.setResult(result);
			}
			else {
				return;
			}
		}
		catch (shv::core::Exception &e) {
			resp.setError(cp::RpcResponse::Error::create(cp::RpcResponse::Error::MethodCallException, e.message()));
		}
		if (resp.requestId().toInt() > 0) { // RPC calls with requestID == 0 does not expect response
			sendRpcMessage(resp);
		}
	}
}

shv::chainpack::RpcValue RootNode::callMethod(const shv::iotqt::node::ShvNode::StringViewList &shv_path, const std::string &method, const shv::chainpack::RpcValue &params)
{
	if (method == cp::Rpc::METH_APP_NAME) {
		return Application::instance()->applicationName().toStdString();
	}
	if (method == cp::Rpc::METH_DEVICE_ID) {
		return Application::instance()->cliOptions()->deviceId();
	}
	else if (method == METH_GET_VERSION) {
		return Application::instance()->applicationVersion().toStdString();
	}
	else if (method == METH_GET_UPTIME) {
		return Application::instance()->uptime().toStdString();
	}
	else if (method == cp::Rpc::METH_GET_LOG) {
		return getLog(QString::fromStdString(shv_path.join('/')), params);
	}
	else if (method == METH_RELOAD_SITES) {
		Application::instance()->deviceMonitor()->downloadSites();
		return true;
	}
	else if (method == METH_SANITIZE_LOG_CACHE) {
		return Application::instance()->logSanitizer()->sanitizeLogCache(QString::fromStdString(shv_path.join('/')), CheckLogTask::CheckType::CheckDirtyLogState);
	}
	else if (method == METH_CHECK_LOG_CACHE) {
		CacheState info = CheckLogTask::checkLogCache(QString::fromStdString(shv_path.join('/')));
		cp::RpcValue::Map res;
		cp::RpcValue::List res_errs;
		for (const auto &err : info.errors) {
			cp::RpcValue::Map res_err;
			res_err["type"] = CacheError::typeToString(err.type);
			res_err["fileName"] = err.fileName.toStdString();
			res_err["description"] = err.description.toStdString();
			res_errs.push_back(res_err);
		}
		res["errors"] = res_errs;
		res["recordCount"] = info.recordCount;
		res["fileCount"] = info.fileCount;
		res["since"] = cp::RpcValue::fromValue(info.since);
		res["until"] = cp::RpcValue::fromValue(info.until);
		return res;
	}
	else if (method == METH_TRIM_DIRTY_LOG && shv_path.size() && shv_path.value(-1) == Application::DIRTY_LOG_NODE) {
		shv::iotqt::node::ShvNode::StringViewList path = shv_path.mid(0, shv_path.size() - 1);
		QString shv_path = QString::fromStdString(path.join('/'));
		trimDirtyLog(shv_path);
		return true;
	}
	else if (method == cp::Rpc::METH_GET && shv_path.size() > 2 && shv_path.value(-1) == Application::START_TS_NODE) {
		shv::iotqt::node::ShvNode::StringViewList path = shv_path.mid(0, shv_path.size() - 2);
		QString shv_path = QString::fromStdString(path.join('/'));
		return getStartTS(shv_path);
	}
	else if (method == METH_GET_LOGVERBOSITY) {
		return NecroLog::topicsLogTresholds();
	}
	else if (method == METH_SET_LOGVERBOSITY) {
		const std::string &s = params.toString();
		NecroLog::setTopicsLogTresholds(s);
		return true;
	}

	return Super::callMethod(shv_path, method, params);
}

cp::RpcValue RootNode::getLog(const QString &shv_path, const shv::chainpack::RpcValue &params) const
{
	shv::core::utils::ShvGetLogParams log_params;
	if (params.isValid()) {
		if (!params.isMap()) {
			SHV_QT_EXCEPTION("Bad param format");
		}
		log_params = params;
	}

	if (log_params.since.isValid() && log_params.until.isValid() && log_params.since.toDateTime() > log_params.until.toDateTime()) {
		SHV_QT_EXCEPTION("since is after until");
	}
	static int static_request_no = 0;
	int request_no = static_request_no++;
	QElapsedTimer tm;
	tm.start();
	shvMessage() << "got request" << request_no << "for log" << shv_path << "with params:\n" << log_params.toRpcValue().toCpon("    ");
	GetLogMerge request(shv_path, log_params);
	try {
		shv::chainpack::RpcValue result = request.getLog();
		shvMessage() << "request number" << request_no << "finished in" << tm.elapsed() << "ms with" << result.toList().size() << "records";
		return result;
	}
	catch (const shv::core::Exception &e) {
		shvMessage() << "request number" << request_no << "finished in" << tm.elapsed() << "ms with error" << e.message();
		throw;
	}
}