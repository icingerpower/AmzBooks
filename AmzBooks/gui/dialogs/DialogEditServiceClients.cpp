#include "DialogEditServiceClients.h"
#include "ui_DialogEditServiceClients.h"
#include "books/ServiceClientManager.h"
#include "DialogAddServiceClient.h"
#include <QMessageBox>
#include <QComboBox>
#include <QStyleOptionButton>
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QStyledItemDelegate>

// ─── Delegate ────────────────────────────────────────────────────────────────

class ServiceClientDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override
    {
        switch (index.column()) {
        case ServiceClientManager::ColCountry: {
            auto *cb = new QComboBox(parent);
            cb->addItems({"US","CA","CN","FR","DE","AT","BE","BG","CY","CZ","DK","EE","ES","FI",
                          "GR","HR","HU","IE","IT","LT","LU","LV","MT","NL","PL","PT","RO","SE","SI","SK"});
            return cb;
        }
        case ServiceClientManager::ColCurrency: {
            auto *cb = new QComboBox(parent);
            cb->addItems({"EUR","USD","GBP","CHF","CAD","AUD","JPY",
                          "SEK","NOK","DKK","PLN","CZK","HUF"});
            return cb;
        }
        case ServiceClientManager::ColPaymentType: {
            auto *cb = new QComboBox(parent);
            cb->addItems({tr("Instant"), tr("After X Days"), tr("End of Next Month")});
            return cb;
        }
        case ServiceClientManager::ColVatOnPayment:
            return nullptr; // toggled directly via editorEvent
        default:
            return QStyledItemDelegate::createEditor(parent, option, index);
        }
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *cb = qobject_cast<QComboBox *>(editor);
        if (!cb) { QStyledItemDelegate::setEditorData(editor, index); return; }

        switch (index.column()) {
        case ServiceClientManager::ColCountry:
        case ServiceClientManager::ColCurrency: {
            int i = cb->findText(index.data(Qt::EditRole).toString());
            if (i >= 0) cb->setCurrentIndex(i);
            break;
        }
        case ServiceClientManager::ColPaymentType: {
            bool ok;
            int v = index.data(Qt::EditRole).toString().toInt(&ok);
            if (ok && v >= 0 && v < cb->count()) cb->setCurrentIndex(v);
            break;
        }
        default:
            QStyledItemDelegate::setEditorData(editor, index);
        }
    }

    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *cb = qobject_cast<QComboBox *>(editor);
        if (!cb) { QStyledItemDelegate::setModelData(editor, model, index); return; }

        switch (index.column()) {
        case ServiceClientManager::ColCountry:
        case ServiceClientManager::ColCurrency:
            model->setData(index, cb->currentText(), Qt::EditRole);
            break;
        case ServiceClientManager::ColPaymentType:
            model->setData(index, QString::number(cb->currentIndex()), Qt::EditRole);
            break;
        default:
            QStyledItemDelegate::setModelData(editor, model, index);
        }
    }

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option,
                              const QModelIndex &) const override
    {
        editor->setGeometry(option.rect);
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        if (index.column() == ServiceClientManager::ColVatOnPayment) {
            if (option.state & QStyle::State_Selected)
                painter->fillRect(option.rect, option.palette.highlight());

            bool checked = index.data(Qt::EditRole).toString() == QLatin1String("1");
            QStyleOptionButton opt;
            opt.state = QStyle::State_Enabled | (checked ? QStyle::State_On : QStyle::State_Off);
            const int sz = 16;
            int x = option.rect.x() + (option.rect.width()  - sz) / 2;
            int y = option.rect.y() + (option.rect.height() - sz) / 2;
            opt.rect = QRect(x, y, sz, sz);
            QApplication::style()->drawControl(QStyle::CE_CheckBox, &opt, painter);
            return;
        }
        QStyledItemDelegate::paint(painter, option, index);
    }

    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override
    {
        if (index.column() == ServiceClientManager::ColVatOnPayment) {
            if (event->type() == QEvent::MouseButtonRelease) {
                auto *me = static_cast<QMouseEvent *>(event);
                if (option.rect.contains(me->pos())) {
                    bool cur = index.data(Qt::EditRole).toString() == QLatin1String("1");
                    model->setData(index, cur ? QStringLiteral("0") : QStringLiteral("1"), Qt::EditRole);
                    return true;
                }
            } else if (event->type() == QEvent::KeyPress) {
                auto *ke = static_cast<QKeyEvent *>(event);
                if (ke->key() == Qt::Key_Space) {
                    bool cur = index.data(Qt::EditRole).toString() == QLatin1String("1");
                    model->setData(index, cur ? QStringLiteral("0") : QStringLiteral("1"), Qt::EditRole);
                    return true;
                }
            }
        }
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
};

// ─────────────────────────────────────────────────────────────────────────────

DialogEditServiceClients::DialogEditServiceClients(ServiceClientManager *clientManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogEditServiceClients),
    m_clientManager(clientManager)
{
    ui->setupUi(this);

    ui->tableView->setModel(m_clientManager);
    ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->tableView->setAlternatingRowColors(true);
    ui->tableView->horizontalHeader()->setStretchLastSection(true);
    ui->tableView->setItemDelegate(new ServiceClientDelegate(ui->tableView));

    _setupConnections();
}

DialogEditServiceClients::~DialogEditServiceClients()
{
    delete ui;
}

void DialogEditServiceClients::_setupConnections()
{
    connect(ui->buttonAdd, &QPushButton::clicked, this, &DialogEditServiceClients::addClient);
    connect(ui->buttonRemove, &QPushButton::clicked, this, &DialogEditServiceClients::removeClient);
    connect(ui->buttonClose, &QPushButton::clicked, this, &DialogEditServiceClients::accept);
}

void DialogEditServiceClients::addClient()
{
    DialogAddServiceClient dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    if (dialog.getClientName().isEmpty())
        return;

    m_clientManager->addClient(
        dialog.getClientName(),
        dialog.getServiceLabel(),
        dialog.getCountry(),
        dialog.getVatNumber(),
        dialog.getCurrency(),
        dialog.getPaymentType(),
        dialog.getPaymentDays(),
        dialog.getStreet1(),
        dialog.getStreet2(),
        dialog.getPostalCode(),
        dialog.getCity(),
        dialog.getAccountSale7(),
        dialog.getAccountVat(),
        dialog.getAccount(),
        dialog.getVatOnPayment()
    );
}

void DialogEditServiceClients::removeClient()
{
    QModelIndexList selection = ui->tableView->selectionModel()->selectedRows();
    if (selection.isEmpty()) {
        QMessageBox::warning(this, tr("No Selection"), tr("Please select a client to remove."));
        return;
    }

    int row = selection.first().row();
    m_clientManager->removeClient(row);
}
