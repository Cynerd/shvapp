﻿#include "serverconnection.h"
#include "../theapp.h"

#include <shv/coreqt/chainpack/rpcconnection.h>
#include <shv/coreqt/log.h>

#include <shv/core/chainpack/chainpackprotocol.h>
#include <shv/core/chainpack/rpcmessage.h>

#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>
#include <QCryptographicHash>

#include <ctime>

#define logRpc() shvCDebug("rpc")

namespace cp = shv::core::chainpack;

namespace rpc {

ServerConnection::ServerConnection(QTcpSocket *socket, QObject *parent)
	: Super(parent)
	, m_socket(socket)
{
	std::srand(std::time(nullptr));
	socket->setParent(nullptr);
	connect(m_socket, &QTcpSocket::disconnected, this, &ServerConnection::deleteLater);
	{
		m_rpcConnection = new shv::coreqt::chainpack::RpcConnection(this);
		m_rpcConnection->setSocket(m_socket);
		connect(m_rpcConnection, &shv::coreqt::chainpack::RpcConnection::messageReceived, this, &ServerConnection::processRpcMessage);
	}
	sendHello();
}

ServerConnection::~ServerConnection()
{
	shvInfo() << "Agent disconnected:" << agentName();
}

QString ServerConnection::peerAddress() const
{
	return m_socket->peerAddress().toString();
}

shv::coreqt::chainpack::RpcConnection *ServerConnection::rpcConnection()
{
	return m_rpcConnection;
}

void ServerConnection::sendHello()
{
	setAgentName(QStringLiteral("%1:%2").arg(peerAddress()).arg(m_socket->peerPort()));
	shvInfo() << "sending hello to:" << agentName();
	m_pendingAuthChallenge = std::to_string(std::rand());
	cp::RpcValue::Map params {
		{"protocol", cp::RpcValue::Map{{"version", 1}}},
		{"challenge", m_pendingAuthChallenge}
	};
	QTimer::singleShot(3000, this, [this]() {
		if(m_helloRequestId > 0 || !m_pendingAuthChallenge.empty()) {
			shvError() << "HELLO request client time out! Dropping client connection." << agentName();
			this->deleteLater();
		}
	});
	m_helloRequestId = rpcConnection()->callMethodASync("hello", params);
}

std::string ServerConnection::passwordHash(const std::string &user)
{
	if(user == "iot")
		return std::string();

	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha1);
	hash.addData(user.c_str(), user.length());
	QByteArray sha1 = hash.result().toHex();
	return std::string(sha1.constData(), sha1.length());
}

void ServerConnection::processRpcMessage(const cp::RpcMessage &msg)
{
	logRpc() << msg.toStdString();
	if(m_helloRequestId > 0 || !m_pendingAuthChallenge.empty()) {
		if(msg.isResponse()) {
			cp::RpcResponse resp(msg);
			if(resp.isError()) {
				shvError() << "HELLO request error response!" << resp.error().toString() << "Dropping client connection." << agentName();
				this->deleteLater();
			}
			else {
				if(resp.id() == m_helloRequestId) {
					cp::RpcValue::Map result = resp.result().toMap();
					const cp::RpcValue::Map login = result.value("login").toMap();
					const cp::RpcValue::String user = login.value("user").toString();

					std::string password_hash = passwordHash(user);
					bool password_ok = password_hash.empty();
					if(!password_ok) {
						std::string challenge_sha1 = result.value("challenge").toString();
						std::string challenge = m_pendingAuthChallenge + passwordHash(user);
						QCryptographicHash hash(QCryptographicHash::Algorithm::Sha1);
						hash.addData(challenge.c_str(), challenge.length());
						std::string sha1 = std::string(hash.result().toHex().constData());
						shvInfo() << challenge_sha1 << "vs" << sha1;
						password_ok = (challenge_sha1 == sha1);
					}
					if(password_ok) {
						m_helloRequestId = 0;
						m_pendingAuthChallenge.clear();
						shvInfo() << "Agent logged in user:" << user << "from:" << agentName();
					}
					else {
						shvError() << "Invalid autentication for user:" << user << "at:" << agentName();
						this->deleteLater();
					}
				}
				else {
					shvError() << "HELLO response invalid request id! Dropping client connection." << agentName();
					this->deleteLater();
				}
			}
		}
		else {
			shvError() << "HELLO request invalid response! Dropping client connection." << agentName();
			this->deleteLater();
		}
		return;
	}
	if(msg.isRequest()) {
		cp::RpcRequest rq(msg);
		shvInfo() << "RPC request received:" << rq.toStdString();
	}
	else if(msg.isNotify()) {
		cp::RpcNotify ntf(msg);
		shvInfo() << "RPC notify received:" << ntf.toStdString();
	}
}

} // namespace rpc


