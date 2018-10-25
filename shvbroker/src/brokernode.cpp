#include "brokernode.h"

#include "brokerapp.h"

#include <shv/chainpack/metamethod.h>
#include <shv/chainpack/rpcmessage.h>
//#include <shv/chainpack/rpcdriver.h>
#include <shv/core/exception.h>
#include <shv/core/stringview.h>
#include <shv/core/log.h>

#include <QTimer>

namespace cp = shv::chainpack;

static const char M_RESTART[] = "restart";

BrokerNode::BrokerNode(shv::iotqt::node::ShvNode *parent)
	: Super(parent)
{
}

shv::chainpack::RpcValue BrokerNode::processRpcRequest(const shv::chainpack::RpcRequest &rq)
{
	const cp::RpcValue::String &shv_path = rq.shvPath().toString();
	//StringViewList shv_path = StringView(path).split('/');
	if(shv_path.empty()) {
		const cp::RpcValue::String method = rq.method().toString();
		if(method == cp::Rpc::METH_SUBSCRIBE) {
			const shv::chainpack::RpcValue parms = rq.params();
			const shv::chainpack::RpcValue::Map &pm = parms.toMap();
			std::string path = pm.value(cp::Rpc::PAR_PATH).toString();
			std::string method = pm.value(cp::Rpc::PAR_METHOD).toString();
			shv::chainpack::RpcRequest rq2 = rq;
			int client_id = rq2.popCallerId();
			BrokerApp::instance()->createSubscription(client_id, path, method);
			return true;
		}
	}
	return Super::processRpcRequest(rq);
}

shv::iotqt::node::ShvNode::StringList BrokerNode::childNames(const StringViewList &shv_path)
{
	Q_UNUSED(shv_path)
	return shv::iotqt::node::ShvNode::StringList{};
}

static std::vector<cp::MetaMethod> meta_methods {
	{cp::Rpc::METH_DIR, cp::MetaMethod::Signature::RetParam, 0, cp::MetaMethod::AccessLevel::Read},
	{cp::Rpc::METH_LS, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::AccessLevel::Read},
	{cp::Rpc::METH_PING, cp::MetaMethod::Signature::VoidVoid, cp::MetaMethod::AccessLevel::Host},
	{cp::Rpc::METH_ECHO, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::AccessLevel::Host},
	{cp::Rpc::METH_SUBSCRIBE, cp::MetaMethod::Signature::RetParam, cp::MetaMethod::AccessLevel::Read},
	{M_RESTART, cp::MetaMethod::Signature::VoidVoid, 0, cp::MetaMethod::AccessLevel::Service},
};

size_t BrokerNode::methodCount(const StringViewList &shv_path)
{
	Q_UNUSED(shv_path)
	return meta_methods.size();
}

const cp::MetaMethod *BrokerNode::metaMethod(const StringViewList &shv_path, size_t ix)
{
	Q_UNUSED(shv_path)
	if(meta_methods.size() <= ix)
		SHV_EXCEPTION("Invalid method index: " + std::to_string(ix) + " of: " + std::to_string(meta_methods.size()));
	return &(meta_methods[ix]);
}

shv::chainpack::RpcValue BrokerNode::callMethod(const StringViewList &shv_path, const std::string &method, const shv::chainpack::RpcValue &params)
{
	if(shv_path.empty()) {
		if(method == cp::Rpc::METH_PING) {
			return true;
		}
		if(method == cp::Rpc::METH_ECHO) {
			return params.isValid()? params: nullptr;
		}
		if(method == M_RESTART) {
			shvInfo() << "Server restart requested via RPC.";
			QTimer::singleShot(500, BrokerApp::instance(), &BrokerApp::quit);
			return true;
		}
	}
	return Super::callMethod(shv_path, method, params);
}
