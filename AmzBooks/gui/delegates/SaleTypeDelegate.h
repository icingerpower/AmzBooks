#ifndef SALETYPEDELEGATE_H
#define SALETYPEDELEGATE_H

#include <QStyledItemDelegate>

// Delegate for the Sale type column of SaleControlTable.
// Presents a QComboBox with translated sale-type labels.
// The model stores stable codes (Qt::EditRole); display text is translated
// at runtime so language changes work correctly without touching the CSV.
class SaleTypeDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit SaleTypeDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;
};

#endif // SALETYPEDELEGATE_H
