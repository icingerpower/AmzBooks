#include "DialogAddSaleService.h"
#include "ui_DialogAddSaleService.h"
#include "books/ServiceClientManager.h"
#include <QDialogButtonBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QHeaderView>

// Column indices in the articles table
static constexpr int COL_DESCRIPTION = 0;
static constexpr int COL_UNIT_PRICE  = 1;
static constexpr int COL_QUANTITY    = 2;
static constexpr int COL_TOTAL       = 3;

DialogAddSaleService::DialogAddSaleService(ServiceClientManager *clientManager, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogAddSaleService),
    m_clientManager(clientManager)
{
    ui->setupUi(this);

    ui->comboBoxClient->setModel(m_clientManager);
    ui->comboBoxClient->setModelColumn(ServiceClientManager::ColClientName);

    ui->comboBoxPaymentTerm->addItems(ServiceClientManager::paymentTypeLabels());
    ui->comboBoxPaymentTerm->setCurrentIndex(static_cast<int>(PaymentType::EndOfNextMonth));

    ui->dateEdit->setDate(QDate::currentDate());

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);

    _setupTable();
    _setupConnections();
    _updateCurrency();
}

DialogAddSaleService::~DialogAddSaleService()
{
    delete ui;
}

void DialogAddSaleService::_setupTable()
{
    auto *t = ui->tableArticles;
    t->setColumnCount(4);
    t->setHorizontalHeaderLabels({tr("Description"), tr("Unit Price (TTC)"), tr("Qty"), tr("Total (TTC)")});
    t->horizontalHeader()->setSectionResizeMode(COL_DESCRIPTION, QHeaderView::Stretch);
    t->horizontalHeader()->setSectionResizeMode(COL_UNIT_PRICE,  QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(COL_QUANTITY,    QHeaderView::ResizeToContents);
    t->horizontalHeader()->setSectionResizeMode(COL_TOTAL,       QHeaderView::ResizeToContents);
    t->verticalHeader()->setVisible(false);

    _addArticleRow();
}

void DialogAddSaleService::_addArticleRow(const QString &title, double unitPrice, double qty)
{
    auto *t = ui->tableArticles;
    const int row = t->rowCount();
    t->insertRow(row);

    // Description — plain editable cell
    t->setItem(row, COL_DESCRIPTION, new QTableWidgetItem(title));

    // Unit price spin box
    auto *priceBox = new QDoubleSpinBox;
    priceBox->setRange(0.01, 9'999'999.99);
    priceBox->setDecimals(2);
    priceBox->setValue(unitPrice);
    priceBox->setSuffix(QString(" %1").arg(getCurrency()));
    connect(priceBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DialogAddSaleService::_onTableDataChanged);
    t->setCellWidget(row, COL_UNIT_PRICE, priceBox);

    // Quantity spin box — 1 decimal, minimum 0.1
    auto *qtyBox = new QDoubleSpinBox;
    qtyBox->setRange(0.1, 9999.9);
    qtyBox->setDecimals(1);
    qtyBox->setSingleStep(0.1);
    qtyBox->setValue(qty > 0.0 ? qty : 1.0);
    connect(qtyBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DialogAddSaleService::_onTableDataChanged);
    t->setCellWidget(row, COL_QUANTITY, qtyBox);

    // Total — read-only computed cell
    auto *totalItem = new QTableWidgetItem;
    totalItem->setFlags(totalItem->flags() & ~Qt::ItemIsEditable);
    t->setItem(row, COL_TOTAL, totalItem);

    _onTableDataChanged();
}

void DialogAddSaleService::_setupConnections()
{
    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &DialogAddSaleService::accept);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &DialogAddSaleService::reject);

    connect(ui->comboBoxClient, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogAddSaleService::_updateCurrency);

    connect(ui->lineEditReference, &QLineEdit::textChanged,
            this, &DialogAddSaleService::_updateOkButton);

    connect(ui->comboBoxPaymentTerm, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DialogAddSaleService::_updatePaymentDays);

    connect(ui->tableArticles, &QTableWidget::itemChanged,
            this, &DialogAddSaleService::_onTableDataChanged);

    connect(ui->pushButtonAddArticle,    &QPushButton::clicked, this, &DialogAddSaleService::_addArticle);
    connect(ui->pushButtonRemoveArticle, &QPushButton::clicked, this, &DialogAddSaleService::_removeArticle);
}

void DialogAddSaleService::_updatePaymentDays()
{
    bool afterXDays = (ui->comboBoxPaymentTerm->currentIndex() == static_cast<int>(PaymentType::AfterXDays));
    ui->spinBoxPaymentDays->setEnabled(afterXDays);
}

void DialogAddSaleService::_updateCurrency()
{
    const QString currency = getCurrency();
    auto *t = ui->tableArticles;
    for (int row = 0; row < t->rowCount(); ++row) {
        if (auto *priceBox = qobject_cast<QDoubleSpinBox *>(t->cellWidget(row, COL_UNIT_PRICE)))
            priceBox->setSuffix(QString(" %1").arg(currency));
    }
    _onTableDataChanged();
}

void DialogAddSaleService::_onTableDataChanged()
{
    _updateTotal();
    _updateOkButton();
}

void DialogAddSaleService::_updateTotal()
{
    auto *t = ui->tableArticles;
    double grand = 0.0;
    for (int row = 0; row < t->rowCount(); ++row) {
        auto *priceBox = qobject_cast<QDoubleSpinBox *>(t->cellWidget(row, COL_UNIT_PRICE));
        auto *qtyBox   = qobject_cast<QDoubleSpinBox *>(t->cellWidget(row, COL_QUANTITY));
        if (!priceBox || !qtyBox) continue;
        const double total = priceBox->value() * qtyBox->value();
        grand += total;
        if (auto *item = t->item(row, COL_TOTAL))
            item->setText(QString::number(total, 'f', 2));
    }
    ui->labelTotal->setText(tr("Total: %1 %2").arg(grand, 0, 'f', 2).arg(getCurrency()));
}

void DialogAddSaleService::_updateOkButton()
{
    const bool valid = !ui->lineEditReference->text().trimmed().isEmpty()
                    && !getLineItems().isEmpty();
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

void DialogAddSaleService::_addArticle()
{
    _addArticleRow();
}

void DialogAddSaleService::_removeArticle()
{
    auto *t = ui->tableArticles;
    if (t->rowCount() <= 1) return; // keep at least one row
    int row = t->currentRow();
    if (row < 0) row = t->rowCount() - 1;
    t->removeRow(row);
    _onTableDataChanged();
}

// ── Setters ──────────────────────────────────────────────────────────────────

void DialogAddSaleService::setDate(const QDate &date)
{
    ui->dateEdit->setDate(date);
}

void DialogAddSaleService::setReference(const QString &ref)
{
    ui->lineEditReference->setText(ref);
}

void DialogAddSaleService::setClientByServiceLabel(const QString &label)
{
    for (int i = 0; i < m_clientManager->rowCount(); ++i) {
        if (m_clientManager->getServiceLabel(i) == label) {
            ui->comboBoxClient->setCurrentIndex(i);
            return;
        }
    }
}

void DialogAddSaleService::setVatOnPayment(bool vop)
{
    ui->checkBoxVatOnPayment->setChecked(vop);
}

void DialogAddSaleService::setPaymentTermFromString(const QString &term)
{
    if (term.startsWith(QStringLiteral("After ")) && term.endsWith(QStringLiteral(" days"))) {
        const QString daysStr = term.mid(6, term.length() - 11);
        bool ok = false;
        const int days = daysStr.toInt(&ok);
        if (ok && days > 0) {
            ui->comboBoxPaymentTerm->setCurrentIndex(static_cast<int>(PaymentType::AfterXDays));
            ui->spinBoxPaymentDays->setValue(days);
            return;
        }
    }
    const PaymentType type = ServiceClientManager::paymentTypeFromLabel(term);
    ui->comboBoxPaymentTerm->setCurrentIndex(static_cast<int>(type));
}

void DialogAddSaleService::setLineItems(const QList<ServiceSalesBooksTable::SaleLineItemInput> &items)
{
    auto *t = ui->tableArticles;
    while (t->rowCount() > 0) {
        t->removeRow(0);
    }
    for (const auto &item : items) {
        _addArticleRow(item.title, item.unitPriceTaxed, item.quantity);
    }
    if (t->rowCount() == 0) {
        _addArticleRow();
    }
}

void DialogAddSaleService::setFirstArticleUnitPrice(double price)
{
    auto *t = ui->tableArticles;
    if (t->rowCount() == 0) return;
    if (auto *priceBox = qobject_cast<QDoubleSpinBox *>(t->cellWidget(0, COL_UNIT_PRICE)))
        priceBox->setValue(price);
}

// ── Getters ──────────────────────────────────────────────────────────────────

QString DialogAddSaleService::getSelectedClientName() const
{
    return ui->comboBoxClient->currentText();
}

int DialogAddSaleService::getSelectedClientRow() const
{
    return ui->comboBoxClient->currentIndex();
}

QDate DialogAddSaleService::getDate() const
{
    return ui->dateEdit->date();
}

QString DialogAddSaleService::getInvoiceId() const
{
    return ui->lineEditReference->text();
}

QString DialogAddSaleService::getCurrency() const
{
    const int row = ui->comboBoxClient->currentIndex();
    if (row >= 0)
        return m_clientManager->getCurrency(row);
    return QString();
}

QString DialogAddSaleService::getAccount() const
{
    const int row = ui->comboBoxClient->currentIndex();
    if (row >= 0)
        return m_clientManager->getAccount(row);
    return QString();
}

PaymentType DialogAddSaleService::getPaymentType() const
{
    return static_cast<PaymentType>(ui->comboBoxPaymentTerm->currentIndex());
}

int DialogAddSaleService::getPaymentDays() const
{
    return ui->spinBoxPaymentDays->value();
}

bool DialogAddSaleService::getVatOnPayment() const
{
    return ui->checkBoxVatOnPayment->isChecked();
}

QList<ServiceSalesBooksTable::SaleLineItemInput> DialogAddSaleService::getLineItems() const
{
    QList<ServiceSalesBooksTable::SaleLineItemInput> result;
    auto *t = ui->tableArticles;
    for (int row = 0; row < t->rowCount(); ++row) {
        const auto *descItem = t->item(row, COL_DESCRIPTION);
        const auto *priceBox = qobject_cast<QDoubleSpinBox *>(t->cellWidget(row, COL_UNIT_PRICE));
        const auto *qtyBox   = qobject_cast<QDoubleSpinBox *>(t->cellWidget(row, COL_QUANTITY));
        if (!descItem || !priceBox || !qtyBox) continue;
        const QString title = descItem->text().trimmed();
        if (title.isEmpty() || priceBox->value() <= 0.0 || qtyBox->value() <= 0.0) continue;
        result.append({title, priceBox->value(), qtyBox->value()});
    }
    return result;
}
