#ifndef PANEBOOKKEEPING_H
#define PANEBOOKKEEPING_H

#include <QWidget>

class BooksConnections;

namespace Ui {
class PaneBookKeeping;
}

class AbstractBooksTableBank;

class PaneBookKeeping : public QWidget
{
    Q_OBJECT

public:
    explicit PaneBookKeeping(QWidget *parent = nullptr);
    ~PaneBookKeeping();

private:
    Ui::PaneBookKeeping *ui;
    void _createBanks();
    void _initYears();
    void _connectSlots();
    void _setSubButtonsEnabled(bool enable);
    AbstractBooksTableBank *getVisibleBankTable() const;

public slots:
    void loadYearSelected();
    void generateBookKeeping();
    void unselectAll();
    void associate();
    void dissociate();

    void bankAdd();
    void bankRemove();

private:
    BooksConnections *m_booksConnections;
};

#endif // PANEBOOKKEEPING_H
