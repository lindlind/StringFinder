#include "finalwindow.h"
#include "ui_finalwindow.h"

QFutureWatcher<void> FinalWindow::watcher, FinalWindow::preparingWatcher;
std::map <QString, std::vector <QFile*> >* FinalWindow::filesByTrigrams, *FinalWindow::smallFiles;
std::vector <std::vector <QFile*> > FinalWindow::threadFiles;
std::vector<QFile*> FinalWindow::potentialFiles, FinalWindow::requestedFiles;
qint64 FinalWindow::checkedFilesSize, FinalWindow::sumSize;
QString FinalWindow::requestString;
QMutex FinalWindow::mutex;
int FinalWindow::threadsNumber, FinalWindow::shift;
bool FinalWindow::threadsWasCancelled, FinalWindow::changeDetected;
QFileSystemWatcher* FinalWindow::detector;

FinalWindow::FinalWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::FinalWindow)
{
    detector = new QFileSystemWatcher(this);
    ui->setupUi(this);
    connect(&preparingWatcher, SIGNAL(finished()), this, SLOT(search()));
    connect(detector, SIGNAL(fileChanged(QString)), this, SLOT(changeWasFound()));
}

FinalWindow::~FinalWindow()
{
    delete ui;
}

void FinalWindow::splitByTrigrams(QString str) {
    std::set <QString> trigramsSubstr, smallFilesSubstr;
    potentialFiles.clear();
    if (str.size() >= 3) {
        for (int i = 0; i < str.size() - 2; i++) {
            trigramsSubstr.insert((QString)"" + str[i] + str[i+1] + str[i+2]);
        }
        std::set<QString>::iterator it = trigramsSubstr.begin();
        potentialFiles = (*filesByTrigrams)[*it];
        it++;
        while (it != trigramsSubstr.end()) {
            std::vector <QFile*> curFiles = (*filesByTrigrams)[*it], buf;
            int i = 0, j = 0;
            while (i < curFiles.size() && j < potentialFiles.size()) {
                if (curFiles[i] == potentialFiles[j]) {
                    buf.push_back(curFiles[i]);
                    i++, j++;
                    continue;
                }
                if (curFiles[i] < potentialFiles[j]) {
                    i++;
                } else {
                    j++;
                }
            }
            potentialFiles.swap(buf);
            it++;
        }
    } else {
        std::function <void(std::map <QString, std::vector <QFile*> >*, std::set <QString>&)>
                selectionSubstr = [&] (std::map <QString, std::vector <QFile*> >* mp, std::set <QString>& substrs) {
            for (auto& p : (*mp)) {
                QString trigram = p.first;
                for (int i = 0; i < (int)trigram.size() - (int)str.size(); i++) {
                    bool ok = true;
                    for (int j = 0; j < str.size(); j++) {
                        ok = (ok && (trigram[i + j] == str[j]));
                    }
                    if (ok) {
                        substrs.insert(trigram);
                        continue;
                    }
                }
            }
        };
        selectionSubstr(filesByTrigrams, trigramsSubstr);
        selectionSubstr(smallFiles, smallFilesSubstr);

        std::function <void(std::map <QString, std::vector <QFile*> >*, std::set <QString>&)>
                selectionFiles = [&] (std::map <QString, std::vector <QFile*> >* mp, std::set <QString>& substrs) {
            std::set<QString>::iterator it = substrs.begin();
            while (it != substrs.end()) {
                std::vector <QFile*> curFiles = (*mp)[*it], buf;
                int i = 0, j = 0;
                while (i < curFiles.size() && j < potentialFiles.size()) {
                    if (curFiles[i] == potentialFiles[j]) {
                        buf.push_back(curFiles[i]);
                        i++, j++;
                        continue;
                    }
                    if (curFiles[i] < potentialFiles[j]) {
                        buf.push_back(curFiles[i]);
                        i++;
                    } else {
                        buf.push_back(potentialFiles[j]);
                        j++;
                    }
                }
                for (; i < curFiles.size(); i++) {
                    buf.push_back(curFiles[i]);
                }
                for (; j < potentialFiles.size(); j++) {
                    buf.push_back(potentialFiles[j]);
                }
                potentialFiles.swap(buf);
                it++;
            }
        };

        selectionFiles(filesByTrigrams, trigramsSubstr);
        selectionFiles(smallFiles, smallFilesSubstr);
    }
    distributeFilesOnThreads();
}

void FinalWindow::on_pushButton_browse_clicked()
{
    changeDetected = false;
    ui->listWidget_files->clear();
    requestedFiles.clear();
    if (ui->lineEdit_substring->text().size() < 1) {
        ui->statusbar->showMessage("Слишком короткая строка.");
        QMessageBox::warning(this, "Слишком короткая строка!", "Пустая строка запрещена для поиска.");
        return;
    }
    if (ui->lineEdit_substring->text().size() > 1000) {
        ui->statusbar->showMessage("Слишком длинная строка.");
        QMessageBox::warning(this, "Слишком длинная строка!", "Строка должна иметь не более 1000 символов.");
        return;
    }
    ui->statusbar->showMessage("Идет поиск триграмм в файлах.");
    ui->lineEdit_substring->setEnabled(false);
    ui->pushButton_browse->setEnabled(false);
    ui->pushButton_closeFinalWindow->setEnabled(false);
    requestString = ui->lineEdit_substring->text();
    preparingWatcher.setFuture(QtConcurrent::run(&FinalWindow::splitByTrigrams, requestString));
}

void FinalWindow::distributeFilesOnThreads() {
    sumSize = 0;
    threadFiles.assign(threadsNumber, {});
    QVector <int> threadsSizes(threadsNumber,0);
    for (int j = 0; j < (int)potentialFiles.size(); j++) {
        sumSize += potentialFiles[j]->size();
        int minInd = 0;
        qint64 minSize = threadsSizes[0];
        for (int i = 1; i < threadsNumber; i++) {
            if (threadsSizes[i] < minSize) {
                minInd = i;
                minSize = threadsSizes[i];
            }
        }
        threadFiles[minInd].push_back(potentialFiles[j]);
        threadsSizes[minInd] += potentialFiles[j]->size();
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

void FinalWindow:: setFlag() {
    threadsWasCancelled = true;
}

void FinalWindow::findText(std::vector <QFile*> &files) {
    std::vector<QFile*> localRequestedFiles;

    for (int j = 0; j < (int)files.size(); j++) {
        if (threadsWasCancelled) {
            return;
        }

        if(!(*files[j]).open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        QTextStream inputStream(files[j]);
        int pos = 0;
        QString buf = "";
        bool found = false;

        while(!found && !inputStream.atEnd() && buf.size() != requestString.size()) {
            if(threadsWasCancelled) {
                files[j]->close();
                return;
            }
            QChar curChar;
            inputStream >> curChar;
            ++pos;
            buf += curChar;
            if(buf == requestString) {
                localRequestedFiles.push_back(files[j]);
                found = true;
            }
        }
        while(!found && !inputStream.atEnd()) {
            if(threadsWasCancelled) {
                files[j]->close();
                return;
            }
            QChar curChar;
            inputStream >> curChar;
            ++pos;

            for(int i = 0; i < buf.size() - 1; ++i) {
                buf[i] = buf[i + 1];
            }

            buf[buf.size() - 1] = curChar;

            if(buf == requestString) {
                localRequestedFiles.push_back(files[j]);
                found = true;
            }
        }

        files[j]->close();

        if(threadsWasCancelled) {
            return;
        }

        mutex.lock();
        checkedFilesSize += files[j]->size();
        emit watcher.progressValueChanged(checkedFilesSize>>shift);
        mutex.unlock();
    }

    if (threadsWasCancelled) {
        return;
    }

    mutex.lock();
    std::move(localRequestedFiles.begin(), localRequestedFiles.end(), std::back_inserter(requestedFiles));
    mutex.unlock();
}


void FinalWindow::search() {
    ui->statusbar->showMessage("Идет поиск текста в файлах.");

    QProgressDialog LoadingWindow;
    if (sumSize > (1 << 19)) {
        LoadingWindow.setLabelText("Идет поиск текста в файлах.\nПожалуйста, подождите...");
        LoadingWindow.setMinimumSize(300, 150);
        LoadingWindow.setMaximumSize(400, 250);

        connect(&LoadingWindow, SIGNAL(canceled()), &watcher, SLOT(cancel()));
        connect(&LoadingWindow, SIGNAL(canceled()), this, SLOT(setFlag()));
        connect(&watcher, SIGNAL(progressValueChanged(int)), &LoadingWindow, SLOT(setValue(int)));
    }
    watcher.setFuture(QtConcurrent::map(threadFiles,&FinalWindow::findText));

    if (sumSize > (1 << 19)) {
        LoadingWindow.setRange(0, sumSize>>shift);
        LoadingWindow.exec();
    }

     watcher.waitForFinished();
     LoadingWindow.reset();
     LoadingWindow.hide();
     if (!changeDetected) {
         if (watcher.isCanceled()) {
             filesByTrigrams->clear();
             smallFiles->clear();
             threadFiles.clear();
             potentialFiles.clear();
             checkedFilesSize = 0;
             sumSize = 0;
             QMessageBox::warning(this, "ОТМЕНА", "Поиск отменен.");
             ui->statusbar->showMessage("Поиск отменен.");
         } else {
             if (requestedFiles.size() == 0) {
                 ui->statusbar->showMessage("Нет файлов, содержащих такую строку.");
             } else {
                 for (QFile* file : requestedFiles) {
                     QString fileName = file->fileName();
                     fileName = fileName.mid(globalDir.size() + 1, fileName.size() - globalDir.size() - 1);
                     ui->listWidget_files->addItem(fileName);
                 }
                 ui->statusbar->showMessage("Поиск завершен.");
            }
         }
     }
     threadsWasCancelled = false;
     ui->lineEdit_substring->setEnabled(true);
     ui->pushButton_browse->setEnabled(true);
     ui->pushButton_closeFinalWindow->setEnabled(true);
}

void FinalWindow::changeWasFound() {
    if (!this->isHidden()) {
        changeDetected = true;
        QMessageBox::warning(this, "Что-то изменилось!", "Требуется переиндексировать файлы. Выберите директорию заново.");
        setFlag();
        on_pushButton_closeFinalWindow_clicked();
    }
}

void FinalWindow::showWindow() {
    show();
}

void FinalWindow::on_pushButton_closeFinalWindow_clicked()
{
    hide();
    ui->lineEdit_substring->setText("");
    ui->listWidget_files->clear();
    emit this->closeWindow();
}
