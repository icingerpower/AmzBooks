#ifndef DIALOGEDITCSVPURCHASES_H
#define DIALOGEDITCSVPURCHASES_H

#include <QDialog>
#include <QDir>

namespace Ui {
class DialogEditCsvPurchases;
}

class PurchaseFileSettingsTree;

class DialogEditCsvPurchases : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditCsvPurchases(
            const QDir &workingDir, QWidget *parent = nullptr);
    ~DialogEditCsvPurchases();

private slots:
    void addCandidate();
    void removeCandidate();

    void _connectSlots();

private:
    Ui::DialogEditCsvPurchases *ui;
    PurchaseFileSettingsTree *m_model;
};

#endif // DIALOGEDITCSVPURCHASES_H
