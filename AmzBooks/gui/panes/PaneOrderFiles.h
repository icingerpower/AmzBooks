#ifndef PANEORDERFILES_H
#define PANEORDERFILES_H

#include <QWidget>
#include <QMap>
#include <QDate>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QValueAxis>

namespace Ui {
class PaneOrderFiles;
}

class PaneOrderFiles : public QWidget
{
    Q_OBJECT

public:
    explicit PaneOrderFiles(QWidget *parent = nullptr);
    ~PaneOrderFiles();

public slots:
    void importFile();
    void removeFile();

private:
    Ui::PaneOrderFiles *ui;
    class FileImportersTable *m_importersTable;
    class ParamsTable *m_paramsModel = nullptr;
    class QFileSystemModel *m_fileSystemModel;
    QMap<QString, class ActivityTable*> m_activityModels;

    void _connectSlots();
    void onImporterSelected(const QModelIndex &current, const QModelIndex &previous);
    void updateChart();

    QMap<QString, QMap<QDate, int>> m_ordersData;
};

#endif // PANEORDERFILES_H
