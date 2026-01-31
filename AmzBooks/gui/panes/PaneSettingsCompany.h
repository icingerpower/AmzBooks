#ifndef PANESETTINGSCOMPANY_H
#define PANESETTINGSCOMPANY_H

#include <QWidget>

namespace Ui {
class PaneSettingsCompany;
}

class PaneSettingsCompany : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsCompany(QWidget *parent = nullptr);
    ~PaneSettingsCompany();

    bool hasVatNumberCompanyCountry() const;
    int countVatNumbers() const;
    int countAddresses() const;

public slots:
    void addVatNumbers();
    void removeVatNumbers();
    void addAddress();
    void removeAddress();

private:
    Ui::PaneSettingsCompany *ui;
    void _connectSlots();
};

#endif // PANESETTINGSCOMPANY_H
