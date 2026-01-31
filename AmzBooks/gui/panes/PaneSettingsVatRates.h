#ifndef PANESETTINGSVATRATES_H
#define PANESETTINGSVATRATES_H

#include <QWidget>

namespace Ui {
class PaneSettingsVatRates;
}

class PaneSettingsVatRates : public QWidget
{
    Q_OBJECT

public:
    explicit PaneSettingsVatRates(QWidget *parent = nullptr);
    ~PaneSettingsVatRates();

public slots:
    void addRate();
    void removeRate();

private:
    Ui::PaneSettingsVatRates *ui;
    void _connectSlots();
};

#endif // PANESETTINGSVATRATES_H
