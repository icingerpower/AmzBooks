#include "RecordListEditor.h"
#include "RecordListTable.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableView>
#include <QHeaderView>

#include "orders/AbstractImporter.h"

RecordListEditor::RecordListEditor(AbstractImporter *importer, const QString &paramKey,
                                   QWidget *parent)
    : QWidget(parent)
{
    QString title = paramKey;
    if (importer) {
        const auto &params = importer->getLoadedParamValues();
        if (params.contains(paramKey) && !params[paramKey].label.isEmpty()) {
            title = params[paramKey].label;
        }
    }

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *label = new QLabel(title, this);
    layout->addWidget(label);

    m_model = new RecordListTable(importer, paramKey, this);
    connect(m_model, &RecordListTable::exceptionOccurred,
            this, &RecordListEditor::exceptionOccurred);

    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_view);

    auto *buttons = new QHBoxLayout();
    auto *addButton = new QPushButton(tr("Add"), this);
    auto *removeButton = new QPushButton(tr("Remove"), this);
    buttons->addWidget(addButton);
    buttons->addWidget(removeButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(addButton, &QPushButton::clicked, this, &RecordListEditor::_onAdd);
    connect(removeButton, &QPushButton::clicked, this, &RecordListEditor::_onRemove);
}

void RecordListEditor::_onAdd()
{
    m_model->addRow();
}

void RecordListEditor::_onRemove()
{
    const QModelIndex current = m_view->currentIndex();
    if (current.isValid()) {
        m_model->removeRow(current.row());
    }
}
