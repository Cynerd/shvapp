#pragma once

#include <shv/iotqt/client/consoleapplication.h>

class AppCliOptions;
class Revitest;

namespace shv { namespace iotqt { class ShvNodeTree; }}

class RevitestApp : public shv::iotqt::client::ConsoleApplication
{
	Q_OBJECT
private:
	using Super = shv::iotqt::client::ConsoleApplication;
public:
	RevitestApp(int &argc, char **argv, AppCliOptions* cli_opts);
	~RevitestApp() Q_DECL_OVERRIDE;
private:
	//void createDevices();
private:
	Revitest *m_revitest;
};

