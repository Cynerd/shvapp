#include "nodes.h"
#include "gitpushtask.h"
#include "sitesproviderapp.h"
#include "appclioptions.h"

#include <shv/coreqt/log.h>
#include <shv/coreqt/exception.h>
#include <shv/iotqt/rpc/deviceconnection.h>
#include <shv/coreqt/rpc.h>
#include <shv/chainpack/utils.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

namespace cp = shv::chainpack;

using namespace std;

const QString TEMPLATE_DIR = QStringLiteral("_template");
const QString TARGET_DIR = QStringLiteral("_target");
const QString FILES_NODE = QStringLiteral("_files");
const QString META_NODE = QStringLiteral("_meta");
const QString META_FILE = QStringLiteral("_meta.json");

static const char METH_GIT_COMMIT[] = "gitCommit";
static const char METH_APP_VERSION[] = "version";
static const char METH_SHV_VERSION[] = "shvVersion";
static const char METH_SHV_GIT_COMMIT[] = "shvGitCommit";
static const char METH_GET_SITES[] = "getSites";
static const char METH_GET_SITES_TGZ_INFO[] = "getSitesTgzInfo";
static const char METH_GET_SITES_TGZ[] = "getSitesTgz";
static const char METH_RELOAD_SITES[] = "reloadSites";
static const char METH_SITES_SYNCED_BEFORE[] = "sitesSyncedBefore";
static const char METH_SITES_RELOADED[] = "reloaded";
static const char METH_FILE_READ[] = "read";
static const char METH_FILE_READ_COMPRESSED[] = "readCompressed";
static const char METH_FILE_HASH[] = "hash";
static const char METH_FILE_SIZE[] = "size";
static const char METH_FILE_SIZE_COMPRESSED[] = "sizeCompressed";
// static const char METH_GIT_PUSH[] = "addFilesToVersionControl";

static std::vector<cp::MetaMethod> root_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
	{cp::Rpc::METH_APP_NAME, cp::MetaMethod::Flag::None, {}, "String"},
	{ METH_APP_VERSION, cp::MetaMethod::Flag::IsGetter, {}, "String", shv::chainpack::AccessLevel::Read },
	{cp::Rpc::METH_DEVICE_ID, cp::MetaMethod::Flag::IsGetter, {}, "RpcValue"},
	{ cp::Rpc::METH_DEVICE_TYPE, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Browse},
	{ METH_GIT_COMMIT, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{ METH_SHV_VERSION, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{ METH_SHV_GIT_COMMIT, cp::MetaMethod::Flag::IsGetter, {}, "String", cp::AccessLevel::Read},
	{ METH_GET_SITES, cp::MetaMethod::Flag::None, {}, "Map", shv::chainpack::AccessLevel::Read},
	{ METH_GET_SITES_TGZ_INFO, cp::MetaMethod::Flag::None, {}, "RpcValue", shv::chainpack::AccessLevel::Read},
	{ METH_GET_SITES_TGZ, cp::MetaMethod::Flag::None, {}, "RpcValue", shv::chainpack::AccessLevel::Read},
};

const static std::vector<cp::MetaMethod> dir_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
	{ METH_GET_SITES, cp::MetaMethod::Flag::None, {}, "Map", shv::chainpack::AccessLevel::Read},
};

const static std::vector<cp::MetaMethod> device_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
	{ METH_GET_SITES, cp::MetaMethod::Flag::None, {}, "Map", shv::chainpack::AccessLevel::Read},
};

const static std::vector<cp::MetaMethod> meta_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
};

const static std::vector<cp::MetaMethod> file_dir_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
};

const static std::vector<cp::MetaMethod> device_file_dir_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
};

const static std::vector<cp::MetaMethod> file_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
	{METH_FILE_SIZE, cp::MetaMethod::Flag::LargeResultHint, "", "UInt", cp::AccessLevel::Browse},
	{METH_FILE_SIZE_COMPRESSED, cp::MetaMethod::Flag::None, "Map", "UInt", cp::AccessLevel::Browse},
	{METH_FILE_READ, cp::MetaMethod::Flag::LargeResultHint, "Map", "Blob", cp::AccessLevel::Read},
	{METH_FILE_READ_COMPRESSED, cp::MetaMethod::Flag::None, "Map", "Blob", cp::AccessLevel::Read},
	{METH_FILE_HASH, cp::MetaMethod::Flag::None, "Map", "String", cp::AccessLevel::Read},
};

const static std::vector<cp::MetaMethod> data_meta_methods {
	cp::methods::DIR,
	cp::methods::LS,
	{cp::Rpc::METH_GET, cp::MetaMethod::Flag::IsGetter, {}, "RpcValue", cp::AccessLevel::Read, {{cp::Rpc::SIG_VAL_CHANGED}}},
};

enum class CompressionType {
	Invalid,
	QCompress,
};

static CompressionType compression_type_from_string(const std::string &type_str, CompressionType default_type)
{
	if (type_str.empty()) {
		return default_type;
	}
	if (type_str == "qcompress") {
		return CompressionType::QCompress;
	}
	return CompressionType::Invalid;
}

AppRootNode::AppRootNode(QObject *parent)
	: Super(parent)
	, m_downloadSitesTimer(this)
{
	// if (QDir(QString::fromStdString(SitesProviderApp::instance()->cliOptions()->localSitesDir()) + "/.git").exists()) {
	// 	shvInfo() << "sites directory" << SitesProviderApp::instance()->cliOptions()->localSitesDir() << "identified as git repository";
	// 	root_meta_methods.push_back({ METH_GIT_PUSH, cp::MetaMethod::Flag::None, {}, "RpcValue", shv::chainpack::AccessLevel::Admin });
	// }
	if (SitesProviderApp::instance()->cliOptions()->syncSites()) {
		root_meta_methods.push_back({ METH_RELOAD_SITES, cp::MetaMethod::Flag::None, {}, "RpcValue", shv::chainpack::AccessLevel::Command});
		root_meta_methods.push_back({ METH_SITES_SYNCED_BEFORE, cp::MetaMethod::Flag::None, {}, "RpcValue", shv::chainpack::AccessLevel::Read, {{METH_SITES_RELOADED, "Null"}} });

		m_downloadSitesTimer.setInterval(30 * 60 * 1000);
		connect(&m_downloadSitesTimer, &QTimer::timeout, this, &AppRootNode::downloadSites);
		m_downloadSitesTimer.start();
		QTimer::singleShot(500, this, &AppRootNode::downloadSites);
	}
	else {
		QTimer::singleShot(500, this, &AppRootNode::updateSitesTgz);
	}
}

size_t AppRootNode::methodCount(const StringViewList &shv_path)
{
	return metaMethods(shv_path).size();
}

const cp::MetaMethod *AppRootNode::metaMethod(const StringViewList &shv_path, size_t ix)
{
	const std::vector<cp::MetaMethod> &meta_methods = metaMethods(shv_path);
	if (meta_methods.size() <= ix) {
		SHV_EXCEPTION("Invalid method index: " + std::to_string(ix) + " of: " + std::to_string(meta_methods.size()));
	}
	return &(meta_methods[ix]);
}

cp::RpcValue AppRootNode::callMethodRq(const cp::RpcRequest &rq)
{
	std::string method = rq.method().toString();
	auto qshv_path = rq.shvPath().to<QString>();
	if (method == cp::Rpc::METH_APP_NAME) {
		return QCoreApplication::instance()->applicationName().toStdString();
	}
	else if(method == METH_GIT_COMMIT) {
#ifdef GIT_COMMIT
		return SHV_EXPAND_AND_QUOTE(GIT_COMMIT);
#else
		return "N/A";
#endif
	}
	else if (method == METH_SHV_GIT_COMMIT) {
#ifdef SHV_GIT_COMMIT
		return SHV_EXPAND_AND_QUOTE(SHV_GIT_COMMIT);
#else
		return "N/A";
#endif
	}
	else if (method == METH_SHV_VERSION) {
#ifdef SHV_VERSION
		return SHV_EXPAND_AND_QUOTE(SHV_VERSION);
#else
		return "N/A";
#endif
	}
	else if (method == METH_APP_VERSION) {
		return QCoreApplication::instance()->applicationVersion().toStdString();
	}
	else if (method == METH_GET_SITES) {
		return getSites(rq.shvPath().to<QString>());
	}
	else if (method == METH_GET_SITES_TGZ_INFO) {
		return getSitesInfo();
	}
	else if (method == METH_GET_SITES_TGZ) {
		return getSitesTgz();
	}
	else if (method == METH_FILE_READ) {
		return readFile(rq.shvPath().to<QString>());
	}
	else if (method == METH_FILE_READ_COMPRESSED) {
		return readFileCompressed(rq);
	}
	else if (method == METH_FILE_SIZE) {
		return readFile(rq.shvPath().to<QString>()).asData().second;
	}
	else if (method == METH_FILE_SIZE_COMPRESSED) {
		return readFileCompressed(rq).asData().second;
	}
	else if (method == METH_FILE_HASH) {
		string bytes = readFile(qshv_path).toString();
		QCryptographicHash h(QCryptographicHash::Sha1);
#if QT_VERSION_MAJOR >= 6 && QT_VERSION_MINOR >= 3
		using byte_array_view_t = QByteArrayView;
#else
		using byte_array_view_t = QByteArray;
#endif
		h.addData(byte_array_view_t(bytes.data(), static_cast<int>(bytes.size())));
		return h.result().toHex().toStdString();
	}
	else if (method == cp::Rpc::METH_GET) {
		return metaValue(qshv_path);
	}
	else if (method == cp::Rpc::METH_DEVICE_ID) {
		SitesProviderApp *app = SitesProviderApp::instance();
		cp::RpcValue::Map opts = app->rpcConnection()->connectionOptions().asMap();;
		cp::RpcValue::Map dev = opts.value(cp::Rpc::KEY_DEVICE).asMap();
		return dev.value(cp::Rpc::KEY_DEVICE_ID).toString();
	}
	else if(method == cp::Rpc::METH_DEVICE_TYPE) {
		return "SitesProvider";
	}
	else if (method == METH_RELOAD_SITES) {
		if (!m_sitesSyncedBefore.isValid() || m_sitesSyncedBefore.elapsed() > 5000) {
			downloadSites();
		}
		return true;
	}
	else if (method == METH_SITES_SYNCED_BEFORE) {
		if(!m_sitesSyncedBefore.isValid())
			return nullptr;
		return static_cast<int>(m_sitesSyncedBefore.elapsed() / 1000);
	}
	// else if (method == METH_GIT_PUSH) {
	// 	GitPushTask *git_task = new GitPushTask(QString::fromStdString(rq.userId().asString()), this);
	// 	connect(git_task, &GitPushTask::finished, [this, rq, git_task](bool success) {
	// 		cp::RpcResponse resp = cp::RpcResponse::forRequest(rq);
	// 		if (success) {
	// 			resp.setResult(true);
	// 		}
	// 		else {
	// 			resp.setError(cp::RpcResponse::Error::create(cp::RpcResponse::Error::MethodCallException, git_task->error().toStdString()));
	// 		}
	// 		emitSendRpcMessage(resp);
	// 	});
	// 	git_task->start();
	// 	return cp::RpcValue();
	// }
	else if (method == cp::Rpc::METH_LS) {
		cp::RpcValue::List res;
		for (const QString &s : lsNode(qshv_path)) {
			res.push_back(s.toStdString());
		}
		return res;
	}
	return Super::callMethodRq(rq);
}

const std::vector<shv::chainpack::MetaMethod> &AppRootNode::metaMethods(const shv::iotqt::node::ShvNode::StringViewList &shv_path_list)
{
	if (shv_path_list.empty()) {
		return root_meta_methods;
	}
	QString shv_path = QString::fromStdString(shv_path_list.join('/'));
	cp::RpcValue meta = metaValue(shv_path);
	if (meta.isValid()) {
		if (meta.isMap()) {
			return meta_meta_methods;
		}
			return data_meta_methods;
		}
	else if (shv_path.contains('/' + FILES_NODE) || shv_path.startsWith(FILES_NODE + '/')) {
		if (isFile(shv_path)) {
			return file_meta_methods;
		}
			if (shv_path.endsWith('/' + FILES_NODE) && isDevice(shv_path.chopped(FILES_NODE.length() + 1))) {
				return device_file_dir_meta_methods;
			}
				return file_dir_meta_methods;
			}
	if (isDevice(shv_path)) {
		return device_meta_methods;
	}
	else {
		return dir_meta_methods;
	}
}

shv::chainpack::RpcValue AppRootNode::metaValue(const QString &shv_path)
{
	cp::RpcValue meta;
	qsizetype meta_ix = shv_path.indexOf('/' + META_NODE);
	if (meta_ix != -1 || shv_path.startsWith(META_NODE)) {
		QString path_rest = shv_path.mid(meta_ix + 1 + META_NODE.length());
		if (path_rest.isEmpty() || path_rest[0] == '/') {
			QFile meta_file(nodeMetaPath(meta_ix == -1 ? QString() : shv_path.mid(0, meta_ix)));
			if (meta_file.open(QFile::ReadOnly)) {
				QByteArray meta_content = meta_file.readAll();
				meta = cp::RpcValue::fromCpon(meta_content.toStdString());
				meta_file.close();
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
				QStringList meta_keys = path_rest.split('/', QString::SkipEmptyParts);
#else
				QStringList meta_keys = path_rest.split('/', Qt::SkipEmptyParts);
#endif
				for (int i = 0; i < meta_keys.count(); ++i) {
					meta = meta.at(meta_keys[i].toStdString());
				}
			}
		}
	}
	return meta;
}

QString AppRootNode::nodeMetaPath(const QString &shv_path) const
{
	return nodeLocalPath(shv_path) + "/" + META_FILE;
}

cp::RpcValue AppRootNode::getSites(const QString &shv_path)
{
	cp::RpcValue::Map res;
	QDir dir(nodeLocalPath(shv_path));
	for (const QFileInfo &info : dir.entryInfoList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot)) {
		if (info.fileName() == META_FILE) {
			QFile meta_file(info.filePath());
			if (meta_file.open(QFile::ReadOnly)) {
				std::string err;
				res[META_NODE.toStdString()] = cp::RpcValue::fromCpon(meta_file.readAll().toStdString(), &err);
				if (!err.empty()) {
					res.erase(res.find(META_NODE.toStdString()));
					shvWarning() << "Invalid json file" << meta_file.fileName() << err;
				}
			}
			else {
				shvWarning() << "Cannot open" << meta_file.fileName() << "for reading";
			}
		}
		else if (info.isDir() && !info.fileName().startsWith('_')) {
			res[info.fileName().toStdString()] = getSites(shv_path + "/" + info.fileName());
		}
	}
	return res;
}

shv::chainpack::RpcValue AppRootNode::getSitesInfo() const
{
	QFile f(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/sites.info");
	if (!f.open(QFile::ReadOnly)) {
		SHV_QT_EXCEPTION("Cannot open file " + f.fileName());
	}
	std::string err;
	cp::RpcValue info = cp::RpcValue::fromCpon(f.readAll().toStdString(), &err);
	if (!err.empty()) {
		SHV_EXCEPTION("Error parsing info file " + err);
	}
	return info;
}

shv::chainpack::RpcValue AppRootNode::getSitesTgz() const
{
	QFile f(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/sites.tgz");
	if (!f.open(QFile::ReadOnly)) {
		SHV_QT_EXCEPTION("Cannot open file " + f.fileName());
	}
	return cp::RpcValue::stringToBlob(f.readAll().toStdString());
}

void AppRootNode::updateSitesTgz()
{
	createSitesTgz([this](const QByteArray &data, const QString &error) {
		if (!error.isEmpty()) {
			shvError() << error;
			return;
		}
		saveSitesTgz(data);
	});
}

void AppRootNode::createSitesTgz(std::function<void(const QByteArray &, const QString &)> callback)
{
	auto *tar_process = new QProcess(this);
	QSharedPointer<QByteArray> data(new QByteArray);
	QSharedPointer<QByteArray> error(new QByteArray);

	connect(tar_process, &QProcess::readyReadStandardError, [tar_process, error]() {
		(*error) += tar_process->readAllStandardError();
	});
	connect(tar_process, &QProcess::readyReadStandardOutput, [tar_process, data]() {
		(*data) += tar_process->readAllStandardOutput();
	});
	connect(tar_process, &QProcess::errorOccurred, [tar_process, callback](QProcess::ProcessError error) {
		if (error == QProcess::ProcessError::FailedToStart) {
			tar_process->deleteLater();
			callback({}, "Cannot start tar " + tar_process->errorString());
		}
	});
#if QT_VERSION_MAJOR >= 6
	connect(tar_process, &QProcess::finished, [tar_process, data, error, callback](int exit_code, QProcess::ExitStatus exit_status) {
#else
	connect(tar_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), [tar_process, data, error, callback](int exit_code, QProcess::ExitStatus exit_status) {
#endif
		if (exit_status != QProcess::ExitStatus::NormalExit) {
			callback({}, "Tar crashed");
			return;
		}
		if (exit_code != 0) {
			callback({}, "Tar failed with status: " + QString::number(exit_code) + " " + (*error));
			return;
		}
		tar_process->deleteLater();
		callback((*data), {});
	});
	tar_process->start("tar", QStringList{ "--transform=s@^@sites/@", "-C", nodeLocalPath(), "-czhf", "-", "./" });
}

QStringList AppRootNode::lsNode(const QString &shv_path)
{
	QStringList ret;
	cp::RpcValue meta = metaValue(shv_path);
	if (meta.isValid()) {
		for (const std::string &meta_item : meta.asMap().keys()) {
			ret.push_back(QString::fromStdString(meta_item));
		}
	}
	else {
		for (const QString &dir_item : lsDir(shv_path)) {
			if (dir_item == META_FILE) {
				ret.push_back(META_NODE);
			}
			else {
				ret.push_back(dir_item);
			}
		}
	}
	return ret;
}

QStringList AppRootNode::lsDir(const QString &shv_path) const
{
	QStringList items;
	QString path = nodeLocalPath(shv_path);
	QFileInfo fi(path);
	if (fi.isDir()) {
		items = QDir(path).entryList(QDir::Filter::Dirs | QDir::Filter::Files | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name);
	}
	return items;
}

bool AppRootNode::isDevice(const QString &shv_path)
{
	QString file_path = nodeLocalPath(shv_path);
	QFileInfo fi(file_path);
	if (fi.isDir()) {
		QStringList entries = QDir(file_path).entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot);
		if ((entries.count() == 1 || (entries.count() == 2 && entries.contains(FILES_NODE))) && entries.contains(META_FILE)) {
			return metaValue(shv_path + '/' + META_NODE).asMap().hasKey("type");
		}
	}
	return false;
}

void AppRootNode::saveSitesTgz(const QByteArray &data) const
{
	QCryptographicHash hash(QCryptographicHash::Algorithm::Sha1);

	QDirIterator it(nodeLocalPath(), QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		QString dir = it.next();
		QFile file(dir);
		if (!file.open(QFile::ReadOnly)) {
			shvError() << "Cannot open file" << file.fileName();
			return;
		}
		hash.addData(file.readAll());
		file.close();
	}

	QString cache_path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
	if (!QFileInfo::exists(cache_path)) {
		QDir("/").mkpath(cache_path);
	}
	QFile tgz_file(cache_path + "/sites.tgz");
	if (!tgz_file.open(QFile::WriteOnly | QFile::Truncate)) {
		shvError() << "Cannot open file" << tgz_file.fileName();
		return;
	}
	tgz_file.write(data);
	tgz_file.close();

	QFile info_file(cache_path + "/sites.info");
	if (!info_file.open(QFile::WriteOnly | QFile::Truncate))	 {
		shvError() << "Cannot open file" << info_file.fileName();
		return;
	}
	info_file.write(QByteArray::fromStdString(
						cp::RpcValue(cp::RpcValue::Map {
							{ "size", data.length() },
							{ "sha1", hash.result().toHex().toStdString() }
						}).toCpon("  ")
						));
	info_file.close();
}

void AppRootNode::onSitesDownloaded()
{
	m_sitesSyncedBefore.start();
	if (SitesProviderApp::instance()->rpcConnection()->isBrokerConnected()) {
		cp::RpcSignal ntf;
		ntf.setMethod(METH_SITES_RELOADED);
		rootNode()->emitSendRpcMessage(ntf);
	}
	updateSitesTgz();
	Q_EMIT sitesDownloaded();
}

void AppRootNode::downloadSites()
{
	if (m_downloadingSites) {
		return;
	}
	m_downloadingSites = true;

	QDir("/tmp/sites").removeRecursively();

	auto *git = new QProcess(this);
	git->setWorkingDirectory("/tmp");
	QStringList arguments;
	arguments << "clone" << SitesProviderApp::instance()->remoteSitesUrl();
	connect(git, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, git](int exit_code, QProcess::ExitStatus exit_status) {
		git->deleteLater();
		if (exit_status == QProcess::ExitStatus::CrashExit) {
			shvError() << git->errorString();
		}
		else if (exit_code) {
			shvError() << git->readAllStandardError();
		}
		else {
			mergeSitesDirs(nodeLocalPath(), "/tmp/sites/sites");
			QDir("/tmp/sites").removeRecursively();
			onSitesDownloaded();
		}
		m_downloadingSites = false;
	});
	connect(git, &QProcess::errorOccurred, this, [this, git](QProcess::ProcessError error) {
		if (error == QProcess::FailedToStart) {
			git->deleteLater();
			shvError() << git->errorString();
			m_downloadingSites = false;
		}
	});
	git->start("git", arguments);
}

void AppRootNode::mergeSitesDirs(const QString &files_path, const QString &git_path)
{
	QDir files_dir(files_path);
	QDir git_dir(git_path);

	QFileInfoList files_entries = files_dir.entryInfoList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot);
	QFileInfoList git_entries = git_dir.entryInfoList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot);

	for (const QFileInfo &git_entry : git_entries) {
		if (git_entry.fileName() != ".git" && git_entry.fileName() != TEMPLATE_DIR && git_entry.fileName() != TARGET_DIR) {
			bool is_in_files = false;
			for (int i = 0; i < files_entries.count(); ++i) {
				const QFileInfo &files_entry = files_entries[i];
				if (git_entry.fileName() == files_entry.fileName()) {
					is_in_files = true;
					files_entries.removeAt(i);
					break;
				}
			}
			if (is_in_files) {
				if (git_entry.isDir()) {
					mergeSitesDirs(files_path + '/' + git_entry.fileName(), git_path + '/' + git_entry.fileName());
				}
				else {
					QFile::remove(files_dir.filePath(git_entry.fileName()));
					QFile::rename(git_entry.filePath(), files_dir.filePath(git_entry.fileName()));
				}
			}
			else {
				if (git_entry.isDir()) {
					files_dir.mkdir(git_entry.fileName());
					mergeSitesDirs(files_path + '/' + git_entry.fileName(), git_path + '/' + git_entry.fileName());
				}
				else {
					QFile::rename(git_entry.filePath(), files_dir.filePath(git_entry.fileName()));
				}
			}
		}
	}

	for (const QFileInfo &files_entry : files_entries) {
		if ((files_entry.isDir() && files_entry.fileName() != FILES_NODE) || files_entry.fileName() == META_FILE) {
			QDir(files_path + '/' + files_entry.fileName()).removeRecursively();
		}
	}
}

shv::chainpack::RpcValue AppRootNode::readFile(const QString &shv_path)
{
	QString filename = nodeLocalPath(shv_path);
	QFile f(filename);
	if (!f.open(QFile::ReadOnly)) {
		SHV_QT_EXCEPTION("Cannot open file " + filename);
	}
	QByteArray file_content = f.readAll();
	return cp::RpcValue::Blob(file_content.cbegin(), file_content.cend());
}

shv::chainpack::RpcValue AppRootNode::readFileCompressed(const shv::chainpack::RpcRequest &request)
{
	const std::string compression_type_str = request.params().asMap().value("compressionType").toString();
	const CompressionType compression_type = compression_type_from_string(compression_type_str, CompressionType::QCompress);
	if (compression_type == CompressionType::Invalid) {
		SHV_EXCEPTION("Invalid compress type: " + compression_type_str + " on path: " + request.shvPath().asString());
	}

	cp::RpcValue result;
	const auto blob = readFile(request.shvPath().to<QString>()).asData();
	if (compression_type == CompressionType::QCompress) {
		const auto compressed_blob = qCompress(QByteArray::fromRawData(blob.first, static_cast<int>(blob.second)));
		result = cp::RpcValue::Blob(compressed_blob.cbegin(), compressed_blob.cend());
	}

	return result;
}

QString AppRootNode::nodeFilesPath(const QString &shv_path) const
{
	return nodeLocalPath(shv_path) + '/' + FILES_NODE;
}

QString AppRootNode::nodeLocalPath(const QString &shv_path) const
{
	QString path = QString::fromStdString(SitesProviderApp::instance()->cliOptions()->localSitesDir());
	if (!path.endsWith('/') && !shv_path.startsWith('/') && !shv_path.isEmpty()) {
		path += '/';
	}
	return path + shv_path;
}

bool AppRootNode::isFile(const QString &shv_path) const
{
	QFileInfo fi(nodeLocalPath(shv_path));
	return fi.isFile();
}

bool AppRootNode::isDir(const QString &shv_path) const
{
	QFileInfo fi(nodeLocalPath(shv_path));
	return fi.isDir();
}
