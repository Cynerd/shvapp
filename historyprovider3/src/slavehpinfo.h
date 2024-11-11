#pragma once

#include "logtype.h"
#include <QString>
#include <string>

struct SlaveHpInfo {
	LogType log_type;
	std::string shv_path;
	std::string site_sync_path;
	QString cache_dir_path;
};
