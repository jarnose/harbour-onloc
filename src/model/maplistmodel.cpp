#include "maplistmodel.h"

MapListModel::MapListModel(const QStringList &keys, QObject *parent)
    : QAbstractListModel(parent)
    , m_keys(keys)
{
}

int MapListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant MapListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const int k = role - Qt::UserRole - 1;
    if (k < 0 || k >= m_keys.size())
        return QVariant();
    return m_rows.at(index.row()).value(m_keys.at(k));
}

QHash<int, QByteArray> MapListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    for (int i = 0; i < m_keys.size(); ++i)
        roles.insert(Qt::UserRole + 1 + i, m_keys.at(i).toUtf8());
    return roles;
}

void MapListModel::setRows(const QVector<QVariantMap> &rows)
{
    const int before = m_rows.size();
    beginResetModel();
    m_rows = rows;
    endResetModel();
    if (before != m_rows.size())
        emit countChanged();
}

void MapListModel::clear()
{
    setRows({});
}

QVariantMap MapListModel::get(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QVariantMap();
}

int MapListModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).value(QStringLiteral("id")).toString() == id)
            return i;
    }
    return -1;
}
