#ifndef REVITESTDEVICE_H
#define REVITESTDEVICE_H

#include <QObject>

namespace shv { namespace chainpack { class RpcMessage; class RpcValue; }}
namespace shv { namespace iotqt { namespace node { class ShvNodeTree; }}}

class RevitestDevice : public QObject
{
	Q_OBJECT
public:
	explicit RevitestDevice(QObject *parent = nullptr);

	void onRpcMessageReceived(const shv::chainpack::RpcMessage &msg);
	Q_SIGNAL void sendRpcMessage(const shv::chainpack::RpcMessage &msg);
private:
	void onLublicatorPropertyValueChanged(const std::string &property_name, const shv::chainpack::RpcValue &new_val);
	void createDevices();
private:
	static constexpr size_t LUB_CNT = 27;
	//Lublicator m_lublicators[LUB_CNT];
	shv::iotqt::node::ShvNodeTree *m_devices = nullptr;
};

#endif // REVITESTDEVICE_H
