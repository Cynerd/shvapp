#include "application.h"
#include "appclioptions.h"

#include <shv/core/utils.h>
#include <shv/coreqt/log.h>

#include <iostream>

int main(int argc, char *argv[])
{
	QCoreApplication::setOrganizationName("Elektroline");
	QCoreApplication::setOrganizationDomain("elektroline.cz");
	QCoreApplication::setApplicationName("shvhistoryprovider");
	QCoreApplication::setApplicationVersion("2.4.24");

	NecroLog::registerTopic("SanitizerTimes", "");
	std::vector<std::string> shv_args = NecroLog::setCLIOptions(argc, argv);

	int ret = 0;

	AppCliOptions cli_opts;
	cli_opts.parse(shv_args);
	if (cli_opts.isParseError()) {
		for (const std::string &err : cli_opts.parseErrors())
			shvError() << err;
		return EXIT_FAILURE;
	}
	if (cli_opts.isAppBreak()) {
		if (cli_opts.isHelp()) {
			cli_opts.printHelp(std::cout);
		}
		return EXIT_SUCCESS;
	}
	if (cli_opts.isVersion()) {
		shvInfo().nospace() << QCoreApplication::applicationName() << ": " << QCoreApplication::applicationVersion();
		#ifdef GIT_COMMIT
			shvInfo() << "GIT commit:" << SHV_EXPAND_AND_QUOTE(GIT_COMMIT);
		#endif
		return EXIT_SUCCESS;
	}

	for (const std::string &s : cli_opts.unusedArguments())
		shvWarning() << "Undefined argument:" << s;

	if (!cli_opts.loadConfigFile()) {
		return EXIT_FAILURE;
	}

	shvInfo() << "======================================================================================";
	shvInfo() << "Starting shvhistoryprovider, PID:" << QCoreApplication::applicationPid() << "version:" << QCoreApplication::applicationVersion();
#ifdef GIT_COMMIT
	shvInfo() << "GIT commit:" << SHV_EXPAND_AND_QUOTE(GIT_COMMIT);
#endif
	shvInfo() << QDateTime::currentDateTime().toString(Qt::ISODate).toStdString() << "UTC:" << QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
	shvInfo() << "======================================================================================";
	shvInfo() << "Log tresholds:" << NecroLog::tresholdsLogInfo();
	shvInfo() << "--------------------------------------------------------------------------------------";

	Application a(argc, argv, &cli_opts);
	shvInfo() << "starting main thread event loop";
	ret = a.exec();
	shvInfo() << "main event loop exit code:" << ret;
	shvInfo() << "bye ...";

	return ret;
}
