#ifndef FREQUENCYDELEGATE_H
#define FREQUENCYDELEGATE_H

#include <QStyledItemDelegate>

// Delegate for the Frequency column of PurchaseControlTable.
// Presents a QComboBox with translated month names (plus "All months").
// The model stores stable codes (Qt::EditRole); display text is translated
// at runtime so language changes work correctly without touching the CSV.
class FrequencyDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit FrequencyDelegate(QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;

    void setEditorData(QWidget *editor, const QModelIndex &index) const override;

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &index) const override;
};

#endif // FREQUENCYDELEGATE_H
