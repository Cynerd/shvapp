#pragma once

#include <shv/iotqt/node/shvnode.h>

namespace rpc { class TcpServer; class ServerConnection; class MasterBrokerConnection; }

class ClientShvNode : public shv::iotqt::node::ShvNode
{
	Q_OBJECT
private:
	using Super = shv::iotqt::node::ShvNode;
public:
	ClientShvNode(rpc::ServerConnection *conn, shv::iotqt::node::ShvNode *parent = nullptr);
	~ClientShvNode() override;

	rpc::ServerConnection * connection() const {return m_connections.value(0);}

	void addConnection(rpc::ServerConnection *conn);
	void removeConnection(rpc::ServerConnection *conn);

	void handleRawRpcRequest(shv::chainpack::RpcValue::MetaData &&meta, std::string &&data) override;
	shv::chainpack::RpcValue hasChildren(const StringViewList &shv_path) override;
private:
	QList<rpc::ServerConnection *> m_connections;
};

class MasterBrokerShvNode : public shv::iotqt::node::ShvNode
{
	Q_OBJECT
private:
	using Super = shv::iotqt::node::ShvNode;
public:
	MasterBrokerShvNode(rpc::MasterBrokerConnection *conn, shv::iotqt::node::ShvNode *parent = nullptr);
	~MasterBrokerShvNode() override;

	rpc::MasterBrokerConnection * connection() const {return m_connections.value(0);}

	void addConnection(rpc::MasterBrokerConnection *conn);
	void removeConnection(rpc::MasterBrokerConnection *conn);

	void handleRawRpcRequest(shv::chainpack::RpcValue::MetaData &&meta, std::string &&data) override;
	shv::chainpack::RpcValue hasChildren(const StringViewList &shv_path) override {Q_UNUSED(shv_path) return false;}
private:
	QList<rpc::MasterBrokerConnection *> m_connections;
};
