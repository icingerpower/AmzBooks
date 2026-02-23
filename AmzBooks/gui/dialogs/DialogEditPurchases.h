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

class DialogEditPurchases : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditPurchases(const BookAccountPurchaseTable *purchaseTable,
                                 const QStringList &filePaths,
                                 const QString &companyCurrency,
                                 QWidget *parent = nullptr);
    ~DialogEditPurchases();

    QList<PurchaseInformation> getInfos() const;

private slots:
    void _onAccepted();

private:
    struct RowData {
        PurchaseInformation originalInfo;
        QDateEdit  *dateEdit          = nullptr;
        QLineEdit  *editAccount       = nullptr;
        QLineEdit  *editLabel         = nullptr;
        QLineEdit  *editSupplier      = nullptr;
        QLineEdit  *editAmount        = nullptr;
        QComboBox  *comboCurrency     = nullptr;
        QLineEdit  *editVatAmount     = nullptr;
        QComboBox  *comboVatCurrency  = nullptr;
        QComboBox  *comboVatCountry   = nullptr;
        QCheckBox  *checkInventory    = nullptr;
        QCheckBox  *checkDdp          = nullptr;
    };

    static constexpr int COL_FILE         = 0;
    static constexpr int COL_DATE         = 1;
    static constexpr int COL_ACCOUNT      = 2;
    static constexpr int COL_LABEL        = 3;
    static constexpr int COL_SUPPLIER     = 4;
    static constexpr int COL_AMOUNT       = 5;
    static constexpr int COL_CURRENCY     = 6;
    static constexpr int COL_VAT_AMOUNT   = 7;
    static constexpr int COL_VAT_CURRENCY = 8;
    static constexpr int COL_VAT_COUNTRY  = 9;
    static constexpr int COL_INVENTORY    = 10;
    static constexpr int COL_DDP          = 11;
    static constexpr int COL_STATUS       = 12;

    Ui::DialogEditPurchases *ui;
    const BookAccountPurchaseTable *m_purchaseTable;
    QStringList m_filePaths;
    QString m_companyCurrency;
    QList<RowData> m_rows;

    void _setupTable();
    void _populateTable();
    QComboBox *_makeCurrencyCombo(const QString &invoiceCurrency) const;
    QComboBox *_makeVatCountryCombo(const QString &vatCountry) const;
    static QWidget *_makeCenteredCheckbox(QCheckBox *cb);
    bool _validateAll();
};

#endif // DIALOGEDITPURCHASES_H
