#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QVariantMap>
#include <QVector>

// A read-only list model whose rows are QVariantMaps; the role names are the
// map keys. Subclasses only decide the key set and how a JSON row becomes a
// map. QML reads a row with get(i) (used by the cover) or through the
// delegate's roles.
class MapListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    explicit MapListModel(const QStringList &keys, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setRows(const QVector<QVariantMap> &rows);
    void clear();

    Q_INVOKABLE QVariantMap get(int row) const;
    // Row of the entry whose "id" role equals id, or -1.
    Q_INVOKABLE int indexOfId(const QString &id) const;

signals:
    void countChanged();

private:
    QStringList m_keys;
    QVector<QVariantMap> m_rows;
};
