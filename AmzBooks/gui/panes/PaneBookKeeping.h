#ifndef PANEBOOKKEEPING_H
#define PANEBOOKKEEPING_H

#include <QWidget>

class BooksConnections;

namespace Ui {
class PaneBookKeeping;
}

class AbstractBooksTableBank;
class AbstractBooksTable;
class EntrySelfTable;
class PurchaseInvoiceTable;
class PurchaseAmzPaymentsTable;
#include <QCoroTask>

class PaneBookKeeping : public QWidget
{
    Q_OBJECT

public:
    explicit PaneBookKeeping(QWidget *parent = nullptr);
    ~PaneBookKeeping();

private:
    Ui::PaneBookKeeping *ui;
    void _createBanks();
    void _createBooksTables();
    void _initYears();
    void _connectSlots();
    void _setSubButtonsEnabled(bool enable);
    void _updateServiceButtonsEnabled();
    EntrySelfTable *getSeflEntryTable() const;
    PurchaseInvoiceTable *getPurchaseInvoiceTable() const;
    PurchaseAmzPaymentsTable *getAmzPaymentsTable() const;
    QList<AbstractBooksTable *> getAllBookTables() const;
    QList<AbstractBooksTableBank *> getAllBankTables() const;
    QList<AbstractBooksTable *> getAllNonBankTables() const;
    AbstractBooksTableBank *getVisibleBankTable() const;

public slots:
    void loadYearSelected();
    void generateBookKeeping();
    void unselectAll();
    void associate();
    void dissociate();

    void selfEntryAdd();
    void selfEntryRemove();

    void purchaseAdd();
    void purchaseAddMany();
    void purchaseRemove();

    void bankAdd();
    void bankRemove();

    void serviceAddSale();
    void serviceRemoveSale();
    void serviceEditClients();
    void serviceCreateFromSelection();

    void amzPaymentAdd();
    void amzPaymentAddMany();
    void amzPaymentRemove();

private:
    BooksConnections *m_booksConnections;
    class OrderManager *m_orderManager;
    QCoro::Task<> generateBookKeepingAsync();
};

#endif // PANEBOOKKEEPING_H
