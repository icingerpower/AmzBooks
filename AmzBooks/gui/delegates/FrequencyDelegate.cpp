#include <QComboBox>

#include "books/PurchaseControlTable.h"
#include "FrequencyDelegate.h"

FrequencyDelegate::FrequencyDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *FrequencyDelegate::createEditor(QWidget *parent,
                                         const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    auto *combo = new QComboBox(parent);
    // Populate with translated display texts; the matching code is stored
    // in each item's UserData so we can convert back in setModelData().
    const QStringList codes = PurchaseControlTable::allFrequencyCodes();
    for (const auto &code : codes) {
        combo->addItem(PurchaseControlTable::frequencyDisplayText(code), code);
    }
    return combo;
}

void FrequencyDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    // Qt::EditRole returns the stable code stored in the model.
    const QString code = index.data(Qt::EditRole).toString();
    auto *combo = static_cast<QComboBox *>(editor);
    // Find the item whose UserData matches the code.
    const int idx = combo->findData(code);
    if (idx != -1) {
        combo->setCurrentIndex(idx);
    }
}

void FrequencyDelegate::setModelData(QWidget *editor,
                                     QAbstractItemModel *model,
                                     const QModelIndex &index) const
{
    const auto *combo = static_cast<QComboBox *>(editor);
    // Save the stable code (UserData), not the display text.
    const QString code = combo->currentData().toString();
    model->setData(index, code, Qt::EditRole);
}

void FrequencyDelegate::updateEditorGeometry(QWidget *editor,
                                             const QStyleOptionViewItem &option,
                                             const QModelIndex &index) const
{
    Q_UNUSED(index);
    editor->setGeometry(option.rect);
}
