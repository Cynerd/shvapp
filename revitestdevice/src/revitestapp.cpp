#include "revitestapp.h"
#include "appclioptions.h"
#include "lublicator.h"

#include <shv/iotqt/client/connection.h>
#include <shv/iotqt/shvnodetree.h>
#include <shv/core/log.h>
#include <shv/chainpack/rpcdriver.h>

RevitestApp::RevitestApp(int &argc, char **argv, AppCliOptions* cli_opts)
	: Super(argc, argv, cli_opts)
{
	m_clientConnection->setProtocolVersion(shv::chainpack::RpcDriver::ProtocolVersion::Cpon);
	m_clientConnection->setProfile("revitest");
	m_clientConnection->setDeviceId("/test/shv/eu/pl/lublin/odpojovace");
	createDevices();
}

RevitestApp::~RevitestApp()
{
	shvInfo() << "destroying shv agent application";
}

void RevitestApp::createDevices()
{
	m_devices = new ShvNodeTree(this);
	static constexpr size_t LUB_CNT = 27;
	for (size_t i = 0; i < LUB_CNT; ++i) {
		auto *nd = new Lublicator(m_devices);
		nd->setNodeName(std::to_string(i+1));
	}
}