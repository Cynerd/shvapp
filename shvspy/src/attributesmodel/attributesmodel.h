#pragma once

#include <QAbstractTableModel>

#include <QPointer>

class ShvNodeItem;
//class ShvMetaMethod;

namespace shv { namespace chainpack { class RpcMessage; }}

class AttributesModel : public QAbstractTableModel
{
	Q_OBJECT
private:
	typedef QAbstractTableModel Super;
public:
	enum Columns {ColMethodName = 0, ColParams, ColResult, ColBtRun, ColCnt};
public:
	AttributesModel(QObject *parent = nullptr);
	~AttributesModel() Q_DECL_OVERRIDE;
public:
	int rowCount(const QModelIndex &parent) const override;
	int columnCount(const QModelIndex &parent) const override {Q_UNUSED(parent) return ColCnt;}
	Qt::ItemFlags flags(const QModelIndex &ix) const Q_DECL_OVERRIDE;
	QVariant data( const QModelIndex & index, int role = Qt::DisplayRole ) const Q_DECL_OVERRIDE;
	bool setData(const QModelIndex &ix, const QVariant &val, int role = Qt::EditRole) Q_DECL_OVERRIDE;
	QVariant headerData ( int section, Qt::Orientation o, int role = Qt::DisplayRole ) const Q_DECL_OVERRIDE;

	void load(ShvNodeItem *nd);
	void callMethod(int row);
private:
	//void onRpcMessageReceived(const shv::chainpack::RpcMessage &msg);
	void onMethodsLoaded();
	void onRpcMethodCallFinished(int method_ix);
	void loadRow(int method_ix);
	void loadRows();
	void emitRowChanged(int row_ix);
	void callGet();
private:
	QPointer<ShvNodeItem> m_shvTreeNodeItem;
	using RowVals = QVector<QVariant>;
	QVector<RowVals> m_rows;
};