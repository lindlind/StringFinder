#ifndef MAINWINDOW_H
#define MAINWINDOW_H

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
#include "finalwindow.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    static bool threadsWasCancelled, changeWasDetected;
    static int threadsNumber, shift;
    static qint64 checkedFilesSize, sumSize;
    static QMutex mutex;
    static std::vector <QFile*> allFiles;
    static QFutureWatcher<void> watcher, preparingWatcher;
    static std::vector <std::vector <QFile*> > threadFiles;
    static std::map <QString, std::vector <QFile*> > filesByTrigrams, smallFiles;

    static void getAllFiles(QString directory);
    static void deleteUnreadableFiles();
    static void distributeFilesOnThreads(QString dir);
    static void findTrigrams(std::vector <QFile*> &files);

    FinalWindow finalWindow;

private slots:
    void on_checkBox_Threads_clicked();

    void on_pushButton_Browse_clicked();

    void on_pushButton_Start_clicked();

    void search();

    void setFlag();

    void changeWasFound();

    void closeFinalWindow();

signals:
    void openFinalWindow();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
