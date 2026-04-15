#include <QComboBox>

#include "books/SaleControlTable.h"
#include "SaleTypeDelegate.h"

SaleTypeDelegate::SaleTypeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *SaleTypeDelegate::createEditor(QWidget *parent,
                                        const QStyleOptionViewItem &option,
                                        const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);

    auto *combo = new QComboBox(parent);
    // Populate with translated display texts; the matching code is stored
    // in each item's UserData so we can convert back in setModelData().
    const QStringList codes = SaleControlTable::allSaleTypeCodes();
    for (const auto &code : codes) {
        combo->addItem(SaleControlTable::saleTypeDisplayText(code), code);
    }
    return combo;
}

void SaleTypeDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    const QString code = index.data(Qt::EditRole).toString();
    auto *combo = static_cast<QComboBox *>(editor);
    const int idx = combo->findData(code);
    if (idx != -1) {
        combo->setCurrentIndex(idx);
    }
}

void SaleTypeDelegate::setModelData(QWidget *editor,
                                    QAbstractItemModel *model,
                                    const QModelIndex &index) const
{
    const auto *combo = static_cast<QComboBox *>(editor);
    const QString code = combo->currentData().toString();
    model->setData(index, code, Qt::EditRole);
}

void SaleTypeDelegate::updateEditorGeometry(QWidget *editor,
                                            const QStyleOptionViewItem &option,
                                            const QModelIndex &index) const
{
    Q_UNUSED(index);
    editor->setGeometry(option.rect);
}
