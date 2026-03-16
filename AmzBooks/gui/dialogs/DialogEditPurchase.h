#ifndef DIALOGEDITPURCHASE_H
#define DIALOGEDITPURCHASE_H

#include <QDialog>
#include <QList>
#include "books/PurchaseInvoiceManager.h"

namespace Ui {
class DialogEditPurchase;
}

class BookAccountPurchaseTable;
class CurrencyRateManager;
class QComboBox;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

class DialogEditPurchase : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditPurchase(const PurchaseInformation &info,
                                const QString &companyCurrency,
                                const BookAccountPurchaseTable *purchaseTable = nullptr,
                                const CurrencyRateManager *currencyRateManager = nullptr,
                                QWidget *parent = nullptr);
    ~DialogEditPurchase();

    PurchaseInformation getInfo() const;

private slots:
    void _onAccepted();

private:
    struct VatRow {
        QWidget     *rowWidget          = nullptr;
        QComboBox   *comboCountry       = nullptr;
        QLineEdit   *editRate           = nullptr;
        QLineEdit   *editAmount         = nullptr;
        QComboBox   *comboCurrency      = nullptr;
        // Company-currency equivalent of the VAT amount (dual-amount token support).
        // Shown as an optional second amount field. When non-empty and vatCurrency
        // differs from companyCurrency, a dual-amount token is emitted on save.
        QLineEdit   *editAmountCompany  = nullptr;
        QPushButton *btnRemove          = nullptr;
    };

    Ui::DialogEditPurchase *ui;
    PurchaseInformation m_info;
    QString m_companyCurrency;
    const BookAccountPurchaseTable *m_purchaseTable       = nullptr;
    const CurrencyRateManager      *m_currencyRateManager = nullptr;
    QVBoxLayout *m_vatLayout = nullptr;
    QList<VatRow> m_vatRows;

    void _setupCurrencies(const QString &companyCurrency, const QString &invoiceCurrency);
    void _addVatRow(const QString &country       = {},
                    const QString &rate          = {},
                    const QString &amount        = {},
                    const QString &currency      = {},
                    const QString &amountCompany = {});
    void _removeVatRow(QWidget *rowWidget);
    QComboBox *_makeCountryCombo(const QString &selected) const;
    QComboBox *_makeVatCurrencyCombo(const QString &selected) const;
};

#endif // DIALOGEDITPURCHASE_H
