#pragma once

#include <QCoreApplication>
#include <shv/iotqt/rpc/clientconnection.h>

class AppCliOptions;

class Application : public QCoreApplication
{
	Q_OBJECT
	using Super = QCoreApplication;

public:
	Application(int &argc, char **argv, AppCliOptions* cli_opts);

private:
	void onShvStateChanged();

	AppCliOptions *m_cliOptions;
	shv::iotqt::rpc::ClientConnection *m_rpcConnection;
	int m_status;
};
