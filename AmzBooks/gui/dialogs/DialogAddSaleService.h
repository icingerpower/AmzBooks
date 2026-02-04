#ifndef DIALOGADDSALESERVICE_H
#define DIALOGADDSALESERVICE_H

#include <QDialog>
#include <QDate>

class ServiceClientManager;

namespace Ui {
class DialogAddSaleService;
}

class DialogAddSaleService : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddSaleService(ServiceClientManager *clientManager, QWidget *parent = nullptr);
    ~DialogAddSaleService();

    // Setters for pre-filling
    void setDate(const QDate &date);
    void setAmount(double amount);
    void setReference(const QString &ref);

    // Getters
    QString getSelectedClientName() const;
    QDate getDate() const;
    double getAmount() const;
    QString getInvoiceId() const;
    QString getCurrency() const; // From Client
    int getSelectedClientRow() const;

private:
    Ui::DialogAddSaleService *ui;
    ServiceClientManager *m_clientManager;
    
    void _setupConnections();
    void _updateCurrency();
};

#endif // DIALOGADDSALESERVICE_H
