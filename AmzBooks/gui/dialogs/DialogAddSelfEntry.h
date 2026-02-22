#ifndef DIALOGADDSELFENTRY_H
#define DIALOGADDSELFENTRY_H

#include <QDialog>

namespace Ui {
class DialogAddSelfEntry;
}

class DialogAddSelfEntry : public QDialog
{
    Q_OBJECT

public:
    explicit DialogAddSelfEntry(QWidget *parent = nullptr);
    ~DialogAddSelfEntry();

    QString getName() const;
    QString getAccount() const;

private slots:
    void _onAccepted();

private:
    Ui::DialogAddSelfEntry *ui;
};

#endif // DIALOGADDSELFENTRY_H
