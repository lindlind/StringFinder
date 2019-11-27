#include "mainwindow.h"
#include "ui_mainwindow.h"

std::map <QString, std::vector <QFile*> > MainWindow::filesByTrigrams, MainWindow::smallFiles;
QFutureWatcher<void> MainWindow::watcher, MainWindow::preparingWatcher;
std::vector <std::vector <QFile*> > MainWindow::threadFiles;
std::vector <QFile*> MainWindow::allFiles;
QMutex MainWindow::mutex;
qint64 MainWindow::checkedFilesSize, MainWindow::sumSize;
int MainWindow::threadsNumber, MainWindow::shift;
bool MainWindow::threadsWasCancelled, MainWindow::changeWasDetected;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new
Ui::MainWindow)
{
    ui->setupUi(this);
    threadsNumber = QThread::idealThreadCount();
    threadsWasCancelled = false;
    connect(&preparingWatcher, SIGNAL(finished()), this, SLOT(search()));
    connect(this, SIGNAL(openFinalWindow()), &finalWindow, SLOT(showWindow()));
    connect(&finalWindow, SIGNAL(closeWindow()), this, SLOT(closeFinalWindow()));
    connect(finalWindow.detector, SIGNAL(fileChanged(QString)), this, SLOT(changeWasFound()));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_checkBox_Threads_clicked() {
    if (ui->checkBox_Threads->isChecked()) {
        threadsNumber = QThread::idealThreadCount();
        ui->statusBar->showMessage("Установлено потоков: " + QString::number(threadsNumber) + ".");
    } else {
        threadsNumber = 1;
        ui->statusBar->showMessage("Программа будет использовать 1 поток.");
    }
}

void MainWindow::on_pushButton_Browse_clicked() {
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    "Select Directory for Scanning",
                                                    QString(),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    ui->lineEdit_Directory->setText(dir);
    ui->statusBar->showMessage("Директория выбрана.");
}

void MainWindow::on_pushButton_Start_clicked() {

    const QFileInfo directory(ui->lineEdit_Directory->text());
    if (!directory.exists() || !directory.isDir()) {
        ui->statusBar->showMessage("Неверный путь.");
        QMessageBox::warning(this, "Ошибка!", "Неправильно указана путь до папки.\nПроверьте правильность и измените путь,\nили воспользуйтесь кнопкой \"Выбрать...\"");
        return;
    }
    if (directory.isSymLink()) {
        ui->statusBar->showMessage("Неверный путь.");
        QMessageBox::warning(this, "Ошибка!", "Папка является ярлыком.\nИзмените путь или воспользуйтесь кнопкой \"Выбрать...\"");
        return;
    }

    changeWasDetected = false;
    FinalWindow::changeDetected = false;
    ui->statusBar->showMessage("Идет индексация.");
    ui->checkBox_Threads->setEnabled(false);
    ui->pushButton_Browse->setEnabled(false);
    ui->pushButton_Start->setEnabled(false);
    ui->lineEdit_Directory->setEnabled(false);
    preparingWatcher.setFuture(QtConcurrent::run(&MainWindow::distributeFilesOnThreads, ui->lineEdit_Directory->text()));
}

void MainWindow::distributeFilesOnThreads(QString dir) {
    getAllFiles(dir);
    if (allFiles.size() == 0) {
        return;
    }
    deleteUnreadableFiles();

    sumSize = 0;
    threadFiles.resize(threadsNumber);
    QVector <int> threadsSizes(threadsNumber,0);
    for (int j = 0; j < (int)allFiles.size(); j++) {
        QString fileName = allFiles[j]->fileName();
        FinalWindow::detector->addPath(fileName);
        sumSize += allFiles[j]->size();
        int minInd = 0;
        qint64 minSize = threadsSizes[0];
        for (int i = 1; i < threadsNumber; i++) {
            if (threadsSizes[i] < minSize) {
                minInd = i;
                minSize = threadsSizes[i];
            }
        }
        threadFiles[minInd].push_back(allFiles[j]);
        threadsSizes[minInd] += allFiles[j]->size();
    }
    for (int i = 0; i < threadsNumber; i++) {
        std::reverse(threadFiles[i].begin(),threadFiles[i].end());
    }

    shift = 0;
    for (int i = 22; i < 64; i++) {
        if ((sumSize>>i) > 0) {
            shift++;
        } else {
            break;
        }
    }
    checkedFilesSize = 0;
}

void MainWindow::getAllFiles(QString dir) {
    QDir directory(dir);
    QStringList files = directory.entryList(QDir::Files | QDir::NoSymLinks, QDir::NoSort);
    foreach (auto file, files) {
        allFiles.push_back(new QFile(dir + "/" + file));
    }

    QStringList dirs = directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks, QDir::NoSort);
    foreach (QString currentDirectory, dirs) {
        getAllFiles(dir + "/" + currentDirectory);
    }
}

void MainWindow::deleteUnreadableFiles() {
    std::sort(allFiles.begin(), allFiles.end(), [] (QFile* const &a, QFile* const &b) { return (a->size() > b->size()); });

    std::vector <QFile*> filesWithoutUnreadable;
    for (int i = 0; i < (int)allFiles.size(); i++) {
        if (allFiles[i]->open(QFile::ReadOnly)) {
            allFiles[i]->close();
            filesWithoutUnreadable.push_back(allFiles[i]);
        }
    }
    allFiles.swap(filesWithoutUnreadable);
}

void MainWindow:: setFlag() {
    threadsWasCancelled = true;
}

void MainWindow::findTrigrams(std::vector <QFile*> &files) {
    std::map <QString, std::vector <QFile*> > localFilesByTrigrams, localSmallFiles;
    qint64 unviewedSize = 0;

    for (int j = 0; j < (int)files.size(); j++) {
        if (threadsWasCancelled) {
            return;
        }

        if(!(*files[j]).open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        std::set<QString> trigramsInFile;
        QTextStream inputStream(files[j]);
        bool ok = true;

        QChar first, second, third, curChar;

        while(!inputStream.atEnd()) {

            if(threadsWasCancelled) {

                files[j]->close();
                return;
            }

            inputStream >> curChar;

            if(inputStream.status() != QTextStream::Ok || curChar.isNonCharacter()) {
                ok = false;
                break;
            }

            first = second;
            second = third;
            third = curChar;

            if(!first.isNull()) {
                QString curTrigram = (QString)"" + first + second + third;
                trigramsInFile.insert(curTrigram);
            }
        }
        files[j]->close();

        if (ok && trigramsInFile.empty()) {
            if(!(*files[j]).open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            QTextStream inputStream(files[j]);
            QString text = "";
            QChar curChar;

            while(!inputStream.atEnd()) {
                inputStream >> curChar;
                text += curChar;
            }

            localSmallFiles[text].push_back(files[j]);

            files[j]->close();
        }

        unviewedSize += files[j]->size();

        if(threadsWasCancelled) {
            return;
        }

        if (unviewedSize > (sumSize>>10)) {
            mutex.lock();
            checkedFilesSize += unviewedSize;
            unviewedSize = 0;
            emit watcher.progressValueChanged(checkedFilesSize>>shift);
            mutex.unlock();
        }

        if(ok) {
            for(const QString &trigram : trigramsInFile) {
                localFilesByTrigrams[trigram].push_back(files[j]);
            }
        }

    }

    if (threadsWasCancelled) {
        return;
    }

    mutex.lock();
    checkedFilesSize += unviewedSize;
    emit watcher.progressValueChanged(checkedFilesSize>>shift);
    for (auto& cur : localFilesByTrigrams) {
        std::sort(cur.second.begin(), cur.second.end(), [] (QFile* const& a, QFile* const& b) {return (b - a > 0);});
        std::copy(cur.second.begin(), cur.second.end(), std::back_inserter(filesByTrigrams[cur.first]));
    }
    for (auto& cur : localSmallFiles) {
        std::sort(cur.second.begin(), cur.second.end(), [] (QFile* const& a, QFile* const& b) {return (b - a > 0);});
        std::copy(cur.second.begin(), cur.second.end(), std::back_inserter(smallFiles[cur.first]));
    }
    mutex.unlock();
}

void MainWindow::search() {

    ui->statusBar->showMessage("Сохраняем триграммы.");

    QProgressDialog LoadingWindow;
    if (sumSize > (1 << 19)) {
        LoadingWindow.setLabelText("Идет поиск триграмм.\nПожалуйста, подождите...");
        LoadingWindow.setMinimumSize(300, 150);
        LoadingWindow.setMaximumSize(400, 250);

        connect(&LoadingWindow, SIGNAL(canceled()), &watcher, SLOT(cancel()));
        connect(&LoadingWindow, SIGNAL(canceled()), this, SLOT(setFlag()));
        connect(&watcher, SIGNAL(progressValueChanged(int)), &LoadingWindow, SLOT(setValue(int)));
    }
    watcher.setFuture(QtConcurrent::map(threadFiles,&MainWindow::findTrigrams));

    if (sumSize > (1 << 19)) {
        LoadingWindow.setRange(0, sumSize>>shift);
        LoadingWindow.exec();
    }

    watcher.waitForFinished();
    LoadingWindow.reset();
    LoadingWindow.hide();
    if (!changeWasDetected) {
        if (watcher.isCanceled()) {
            filesByTrigrams.clear();
            smallFiles.clear();
            threadFiles.clear();
            allFiles.clear();
            checkedFilesSize = 0;
            sumSize = 0;
            QMessageBox::warning(this, "ОТМЕНА", "Поиск отменен.");
            ui->statusBar->showMessage("Поиск отменен.");
        } else if (filesByTrigrams.size() == 0) {
            filesByTrigrams.clear();
            smallFiles.clear();
            threadFiles.clear();
            allFiles.clear();
            checkedFilesSize = 0;
            sumSize = 0;
            QMessageBox::warning(this, "Удача!", "Нет одинаковых файлов");
            ui->statusBar->showMessage("Нет одинаковых файлов");
        } else {
            /*for (auto& cur : filesByHash) {
                if (cur.second.size() > 1) {
                    finalWindow.files.push_back(cur.second);
                }
            }
            if (finalWindow.files.size() == 0) {
                threadFiles.clear();
                allFiles.clear();
                checkedFilesSize = 0;
                sumSize = 0;
                QMessageBox::warning(this, "Удача!", "Нет одинаковых файлов");
                ui->statusBar->showMessage("Нет одинаковых файлов");
            } else {*/
                hide();
                finalWindow.filesByTrigrams = &filesByTrigrams;
                finalWindow.smallFiles = &smallFiles;
                finalWindow.threadsNumber = threadsNumber;
                finalWindow.globalDir = ui->lineEdit_Directory->text();
                emit this->openFinalWindow();
                QMessageBox::about(this, "OK", "Поиск завершен.");
                ui->statusBar->showMessage("Поиск завершен.");
            //}
        }
    }
    threadsWasCancelled = false;
    ui->checkBox_Threads->setEnabled(true);
    ui->pushButton_Browse->setEnabled(true);
    ui->pushButton_Start->setEnabled(true);
    ui->lineEdit_Directory->setEnabled(true);
}

void MainWindow::changeWasFound() {
    if (finalWindow.isHidden() && !FinalWindow::changeDetected) {
        changeWasDetected = true;
        QMessageBox::warning(this, "Что-то изменилось!", "Требуется переиндексировать файлы. Выберите директорию заново.");
        setFlag();
    }
}

void MainWindow::closeFinalWindow() {
    filesByTrigrams.clear();
    smallFiles.clear();
    threadFiles.clear();
    allFiles.clear();
    checkedFilesSize = 0;
    sumSize = 0;
    ui->lineEdit_Directory->setText("");
    ui->statusBar->showMessage("");
    show();
}
