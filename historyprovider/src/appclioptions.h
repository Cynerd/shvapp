#pragma once

#include <shv/iotqt/rpc/deviceappclioptions.h>

class AppCliOptions : public shv::iotqt::rpc::DeviceAppCliOptions
{
private:
	using Super = shv::iotqt::rpc::DeviceAppCliOptions;
public:
	AppCliOptions();
	CLIOPTION_GETTER_SETTER2(std::string, "app.journalCacheRoot", j, setJ, ournalCacheRoot)
	CLIOPTION_GETTER_SETTER2(std::string, "app.journalCacheSizeLimit", j, setJ, ournalCacheSizeLimit)
	CLIOPTION_GETTER_SETTER2(int, "app.journalSanitizerInterval", j, setJ, ournalSanitizerInterval)
};
