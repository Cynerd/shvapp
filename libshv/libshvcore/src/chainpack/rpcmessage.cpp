#include "rpcmessage.h"
#include "chainpackprotocol.h"
#include "metatypes.h"

#include <cassert>

namespace shv {
namespace core {
namespace chainpack {

RpcMessage::RpcMessage(const RpcValue &val)
{
	assert(val.isIMap());
	m_value = val;
}

bool RpcMessage::hasKey(RpcValue::UInt key) const
{
	return m_value.toIMap().count(key);
}

RpcValue RpcMessage::value(RpcValue::UInt key) const
{
	return m_value.at(key);
}

void RpcMessage::setValue(RpcValue::UInt key, const RpcValue &val)
{
	assert(key >= Key::Method && key < Key::MAX_KEY);
	checkMetaValues();
	m_value.set(key, val);
}

void RpcMessage::setMetaValue(RpcValue::UInt key, const RpcValue &val)
{
	checkMetaValues();
	m_value.setMetaValue(key, val);
}

RpcValue::UInt RpcMessage::id() const
{
	return m_value.metaData().value(MetaTypes::Global::ChainPackRpcMessage::Tag::RequestId).toUInt();
}

void RpcMessage::setId(RpcValue::UInt id)
{
	checkMetaValues();
	checkRpcTypeMetaValue();
	setMetaValue(MetaTypes::Global::ChainPackRpcMessage::Tag::RequestId, id);
}

bool RpcMessage::isValid() const
{
	return m_value.isValid();
}

bool RpcMessage::isRequest() const
{
	return rpcType() == MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Request;
}

bool RpcMessage::isNotify() const
{
	return rpcType() == MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Notify;
}

bool RpcMessage::isResponse() const
{
	return rpcType() == MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Response;
}

int RpcMessage::write(std::ostream &out) const
{
	assert(m_value.isValid());
	assert(rpcType() != MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Undefined);
	return ChainPackProtocol::write(out, m_value);
}

MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Enum RpcMessage::rpcType() const
{
	RpcValue::UInt rpc_id = id();
	bool has_method = hasKey(Key::Method);
	if(has_method)
		return (rpc_id > 0)? MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Request: MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Notify;
	if(hasKey(Key::Result) || hasKey(Key::Error))
		return MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Response;
	return MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Undefined;
}

void RpcMessage::checkMetaValues()
{
	if(!m_value.isValid()) {
		m_value = RpcValue::IMap();
		/// not needed, Global is default name space
		//setMetaValue(MetaTypes::Tag::MetaTypeNameSpaceId, MetaTypes::Global::Value);
		/// not needed, ChainPackRpcMessage is default metatype
		//setMetaValue(MetaTypes::Tag::MetaTypeId, MetaTypes::Global::ChainPackRpcMessage::Value);
	}
}

void RpcMessage::checkRpcTypeMetaValue()
{
	MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Enum rpc_type = isResponse()? MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Response: isNotify()? MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Notify: MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType::Request;
	setMetaValue(MetaTypes::Global::ChainPackRpcMessage::Tag::RpcCallType, rpc_type);
}

RpcValue::String RpcRequest::method() const
{
	return value(Key::Method).toString();
}

RpcRequest &RpcRequest::setMethod(RpcValue::String &&met)
{
	setValue(Key::Method, RpcValue{std::move(met)});
	checkRpcTypeMetaValue();
	return *this;
}

RpcValue RpcRequest::params() const
{
	return value(Key::Params);
}

RpcRequest& RpcRequest::setParams(const RpcValue& p)
{
	setValue(Key::Params, p);
	return *this;
}

RpcResponse::Error RpcResponse::error() const
{
	return Error{value(Key::Error).toIMap()};
}

RpcResponse &RpcResponse::setError(RpcResponse::Error err)
{
	setValue(Key::Error, std::move(err));
	checkRpcTypeMetaValue();
	return *this;
}

RpcValue RpcResponse::result() const
{
	return value(Key::Result);
}

RpcResponse& RpcResponse::setResult(const RpcValue& res)
{
	setValue(Key::Result, res);
	checkRpcTypeMetaValue();
	return *this;
}

RpcResponse::Error::ErrorType RpcResponse::Error::code() const
{
	auto iter = find(KeyCode);
	return (iter == end()) ? NoError : (ErrorType)(iter->second.toUInt());
}

RpcResponse::Error& RpcResponse::Error::setCode(ErrorType c)
{
	(*this)[KeyCode] = RpcValue{(RpcValue::UInt)c};
	return *this;
}

RpcResponse::Error& RpcResponse::Error::setMessage(RpcValue::String &&mess)
{
	(*this)[KeyMessage] = RpcValue{std::move(mess)};
	return *this;
}

RpcValue::String RpcResponse::Error::message() const
{
	auto iter = find(KeyMessage);
	return (iter == end()) ? RpcValue::String{} : iter->second.toString();
}

std::string RpcMessage::toStdString() const
{
	return m_value.toStdString();
}

} // namespace chainpackrpc
}
} // namespace shv
