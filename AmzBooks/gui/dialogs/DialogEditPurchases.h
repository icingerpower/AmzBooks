#ifndef DIALOGEDITPURCHASES_H
#define DIALOGEDITPURCHASES_H

#include <QDialog>
#include <QList>
#include <QStringList>
#include "books/PurchaseInvoiceManager.h"

namespace Ui {
class DialogEditPurchases;
}

class QDateEdit;
class QLineEdit;
class QComboBox;
class QCheckBox;
class BookAccountPurchaseTable;
class CurrencyRateManager;

class DialogEditPurchases : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditPurchases(const BookAccountPurchaseTable *purchaseTable,
                                 const QStringList &filePaths,
                                 const QString &companyCurrency,
                                 const CurrencyRateManager *currencyRateManager,
                                 QWidget *parent = nullptr);
    ~DialogEditPurchases();

    QList<PurchaseInformation> getInfos() const;

private slots:
    void _onAccepted();

private:
    struct RowData {
        PurchaseInformation originalInfo;
        QDateEdit  *dateEdit              = nullptr;
        QLineEdit  *editAccount           = nullptr;
        QLineEdit  *editLabel             = nullptr;
        QLineEdit  *editSupplier          = nullptr;
        QLineEdit  *editAmount            = nullptr;
        QComboBox  *comboCurrency         = nullptr;
        QLineEdit  *editVatAmount         = nullptr;
        // Company-currency equivalent of the VAT amount (only for dual-amount tokens
        // where vatCurrency differs from the company currency). Leave empty to fall
        // back to single-amount token encoding.
        QLineEdit  *editVatAmountCompany  = nullptr;
        QLineEdit  *editVatRate           = nullptr;
        QComboBox  *comboVatCurrency      = nullptr;
        QComboBox  *comboVatCountry       = nullptr;
        QComboBox  *comboCountryFrom      = nullptr;
        QComboBox  *comboCountryTo        = nullptr;
        QCheckBox  *checkInventory        = nullptr;
        QCheckBox  *checkDdp              = nullptr;
    };

    static constexpr int COL_FILE                 = 0;
    static constexpr int COL_DATE                 = 1;
    static constexpr int COL_ACCOUNT              = 2;
    static constexpr int COL_LABEL                = 3;
    static constexpr int COL_SUPPLIER             = 4;
    static constexpr int COL_AMOUNT               = 5;
    static constexpr int COL_CURRENCY             = 6;
    static constexpr int COL_VAT_AMOUNT           = 7;
    static constexpr int COL_VAT_AMOUNT_COMPANY   = 8;
    static constexpr int COL_VAT_RATE             = 9;
    static constexpr int COL_VAT_CURRENCY         = 10;
    static constexpr int COL_VAT_COUNTRY          = 11;
    static constexpr int COL_COUNTRY_FROM         = 12;
    static constexpr int COL_COUNTRY_TO           = 13;
    static constexpr int COL_INVENTORY            = 14;
    static constexpr int COL_DDP                  = 15;
    static constexpr int COL_STATUS               = 16;

    Ui::DialogEditPurchases *ui;
    const BookAccountPurchaseTable *m_purchaseTable;
    const CurrencyRateManager *m_currencyRateManager;
    QStringList m_filePaths;
    QString m_companyCurrency;
    QList<RowData> m_rows;

    void _setupTable();
    void _populateTable();
    QComboBox *_makeCurrencyCombo(const QString &invoiceCurrency) const;
    QComboBox *_makeVatCountryCombo(const QString &vatCountry) const;
    QComboBox *_makeCountryCodeCombo(const QString &selected) const;
    static QWidget *_makeCenteredCheckbox(QCheckBox *cb);
    bool _validateAll();
};

#endif // DIALOGEDITPURCHASES_H
