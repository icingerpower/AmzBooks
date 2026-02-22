#ifndef DIALOGEDITPURCHASE_H
#define DIALOGEDITPURCHASE_H

#include <QDialog>
#include <QList>
#include "books/PurchaseInvoiceManager.h"

namespace Ui {
class DialogEditPurchase;
}

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
                                QWidget *parent = nullptr);
    ~DialogEditPurchase();

    PurchaseInformation getInfo() const;

private slots:
    void _onAccepted();

private:
    struct VatRow {
        QWidget     *rowWidget    = nullptr;
        QComboBox   *comboCountry = nullptr;
        QLineEdit   *editRate     = nullptr;
        QLineEdit   *editAmount   = nullptr;
        QComboBox   *comboCurrency= nullptr;
        QPushButton *btnRemove    = nullptr;
    };

    Ui::DialogEditPurchase *ui;
    PurchaseInformation m_info;
    QString m_companyCurrency;
    QVBoxLayout *m_vatLayout = nullptr;
    QList<VatRow> m_vatRows;

    void _setupCurrencies(const QString &companyCurrency, const QString &invoiceCurrency);
    void _addVatRow(const QString &country = {},
                    const QString &rate    = {},
                    const QString &amount  = {},
                    const QString &currency= {});
    void _removeVatRow(QWidget *rowWidget);
    QComboBox *_makeCountryCombo(const QString &selected) const;
    QComboBox *_makeVatCurrencyCombo(const QString &selected) const;
};

#endif // DIALOGEDITPURCHASE_H
