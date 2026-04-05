#ifndef PANEFIXVAT_H
#define PANEFIXVAT_H

#include <QWidget>

namespace Ui {
class PaneFixVat;
}

class PaneFixVat : public QWidget
{
    Q_OBJECT

public:
    explicit PaneFixVat(QWidget *parent = nullptr);
    ~PaneFixVat();

private slots:
    void onBrowseSummary();
    void onBrowseUpdate();
    void onFixVat();
    void onFixInventoryCost();

private:
    Ui::PaneFixVat *ui;

    void _connectSlots();
    // Returns the fixer currently selected in listFixers, or nullptr if none.
    const class AbstractVatFixer *_selectedFixer() const;
};

#endif // PANEFIXVAT_H
