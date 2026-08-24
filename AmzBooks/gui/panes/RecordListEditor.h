#ifndef RECORDLISTEDITOR_H
#define RECORDLISTEDITOR_H

#include <QWidget>

class AbstractImporter;
class RecordListTable;
class QTableView;

// Reusable editor for a RecordList parameter: a titled table with Add / Remove
// buttons. Owns its own RecordListTable model and re-emits its exceptions.
class RecordListEditor : public QWidget
{
    Q_OBJECT

public:
    explicit RecordListEditor(AbstractImporter *importer, const QString &paramKey,
                              QWidget *parent = nullptr);

signals:
    void exceptionOccurred(const QString &title, const QString &message);

private:
    RecordListTable *m_model = nullptr;
    QTableView *m_view = nullptr;

    void _onAdd();
    void _onRemove();
};

#endif // RECORDLISTEDITOR_H
