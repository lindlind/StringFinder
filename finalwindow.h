#ifndef FINALWINDOW_H
#define FINALWINDOW_H

#include <QMainWindow>
#include <QFileDialog>
#include <QProgressDialog>
#include <QMessageBox>
#include <QFile>
#include <QDir>
#include <QString>
#include <vector>
#include <QThread>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QCryptographicHash>
#include <set>
#include <functional>
#include <QFileSystemWatcher>

namespace Ui {
class FinalWindow;
}

class FinalWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit FinalWindow(QWidget *parent = 0);
    ~FinalWindow();

    static QFileSystemWatcher* detector;
    QString globalDir;
    static bool threadsWasCancelled, changeDetected;
    static int threadsNumber, shift;
    static qint64 checkedFilesSize, sumSize;
    static QMutex mutex;
    static QString requestString;
    static QFutureWatcher<void> watcher, preparingWatcher;
    static std::vector <QFile*> potentialFiles, requestedFiles;
    static std::vector <std::vector <QFile*> > threadFiles;
    static std::map <QString, std::vector <QFile*> > *filesByTrigrams, *smallFiles;

    static void findText(std::vector <QFile*> &files);
    static void distributeFilesOnThreads();
    static void splitByTrigrams(QString directory);

public slots:
    void showWindow();

private slots:
    void on_pushButton_closeFinalWindow_clicked();

    void on_pushButton_browse_clicked();

    void search();

    void setFlag();

    void changeWasFound();

signals:
    void closeWindow();

private:
    Ui::FinalWindow *ui;
};

#endif // FINALWINDOW_H
