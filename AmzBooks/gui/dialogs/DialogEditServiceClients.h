#ifndef DIALOGEDITSERVICECLIENTS_H
#define DIALOGEDITSERVICECLIENTS_H

#include <QDialog>

class ServiceClientManager;
class QAbstractButton;

namespace Ui {
class DialogEditServiceClients;
}

class DialogEditServiceClients : public QDialog
{
    Q_OBJECT

public:
    explicit DialogEditServiceClients(ServiceClientManager *clientManager, QWidget *parent = nullptr);
    ~DialogEditServiceClients();

public slots:
    void addClient();
    void removeClient();

private:
    Ui::DialogEditServiceClients *ui;
    ServiceClientManager *m_clientManager;
    
    void _setupConnections();
};

#endif // DIALOGEDITSERVICECLIENTS_H
