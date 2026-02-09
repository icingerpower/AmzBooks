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
    const EntrySelfTable *getSeflEntryTable() const;
    QList<const AbstractBooksTable *> getAllBookTables() const;
    QList<const AbstractBooksTableBank *> getAllBankTables() const;
    QList<const AbstractBooksTable *> getAllNonBankTables() const;
    AbstractBooksTableBank *getVisibleBankTable() const;

public slots:
    void loadYearSelected();
    void generateBookKeeping();
    void unselectAll();
    void associate();
    void dissociate();

    void purchaseAdd();
    void purchaseAddMany();
    void purchaseRemove();

    void bankAdd();
    void bankRemove();

    void serviceAddSale();
    void serviceRemoveSale();
    void serviceEditClients();
    void serviceCreateFromSelection();

private:
    BooksConnections *m_booksConnections;
    class OrderManager *m_orderManager;
    QCoro::Task<> generateBookKeepingAsync();
};

#endif // PANEBOOKKEEPING_H
