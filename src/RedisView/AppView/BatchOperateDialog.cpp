/**
* @file      BatchOperateDialog.cpp
* @brief     批量操作
* @author    王长春
* @date      2018-11-24
* @version   001
* @copyright Copyright (c) 2018
*/
#include "BatchOperateDialog.h"
#include "ui_batchoperatedialog.h"

BatchOperateDialog::BatchOperateDialog(RedisCluster *redisClient, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BatchOperateDialog)
{
    ui->setupUi(this);
    setWindowTitle(tr("批量操作"));
    _vTaskId.clear();
    _isRun = false;
    _workThread = nullptr;
    _redisCluster = redisClient;
    ui->_batchComboBox->addItem("Delete");
    // 生产环境有RedisTool可用,RedisView工具不在实现
    ui->_batchComboBox->addItem("Scan key");
    ui->_batchComboBox->addItem("Import from oracle");
    ui->_batchComboBox->addItem("Import from mysql");
    ui->_batchComboBox->addItem("Export to oracle");
    ui->_batchComboBox->addItem("Export to mysql");
    ui->_batchComboBox->addItem("Delete oracle key");
    ui->_batchComboBox->addItem("Delete mysql key");
    ui->_progressBar->setMinimum(0);
    ui->_progressBar->setMaximum(100);
    ui->_progressBar->setValue(0);
    QRegExp regx("[0-9]+$");
    QValidator *validator = new QRegExpValidator(regx, ui->_patternCountLineEdit);
    ui->_patternCountLineEdit->setValidator(validator);

    if(_redisCluster) {
        _isClusterMode = _redisCluster->getClusterMode();
        if(_isClusterMode) {
            _idbNums = 1;
        } else {
            if(!_redisCluster->getDbNum(_idbNums)) {
                _idbNums = 1;
                QMessageBox::information(this, tr("错误"), tr("获取DB个数失败!"));
            }
        }
        _vMasterClients = _redisCluster->getClients(true);
        for(int i = 0; i < _vMasterClients.size(); ++i) {
            _vMasterClients[i]._client = nullptr;
        }
    }

    _threadPool = QThreadPool::globalInstance();
    _threadPool->setMaxThreadCount(6);
    _threadPool->setExpiryTimeout(5000); //5s

    connect(ui->_batchComboBox, SIGNAL(currentTextChanged(const QString)), this,
            SLOT(changeOperate(const QString)));
    emit ui->_batchComboBox->currentTextChanged(ui->_batchComboBox->currentText());
}

BatchOperateDialog::~BatchOperateDialog()
{
    delete ui;
}

void BatchOperateDialog::changeOperate(const QString operate) {
    if(operate == "Delete") {
        ui->_patternCountLineEdit->setPlaceholderText("2");
        ui->_patternSeparatorLineEdit->setPlaceholderText("|");
        ui->_patternlineEdit->setPlaceholderText("a*|b*");
        ui->_textBrowser->setPlaceholderText(tr("删除以a或b开头的键"));
		ui->_textBrowser->clear();
    } else if(operate == "Scan key") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    } else if(operate == "Import from oracle") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    } else if(operate == "Export to oracle") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    } else if(operate == "Import from mysql") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    } else if(operate == "Export to mysql") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    } else if(operate == "Delete oracle key") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    } else if(operate == "Delete mysql key") {
        ui->_textBrowser->setText(tr("暂不实现此功能"));
    }
}

void BatchOperateDialog::recvData(const TaskMsg taskMsg) {
    if(taskMsg._taskid == THREAD_BATCH_DEL_KEY_TASK) {
        _TASK_LOCK.lockForWrite();
        _NowKeyNum += taskMsg._sequence;
        if(_KeyNum == 0) {
            _processValue = 100;
        } else {
            _processValue = _NowKeyNum * 100 / _KeyNum;
            _processValue = _processValue > 100 ? 100 : _processValue;
            _processValue = _processValue < 0 ? 0 : _processValue;
        }
        ui->_progressBar->setValue(_processValue);
        ui->_textBrowser->moveCursor(QTextCursor::End);
        QTextCursor cursor=ui->_textBrowser->textCursor();
        cursor.select(QTextCursor::BlockUnderCursor);
        cursor.removeSelectedText();
        ui->_textBrowser->append(QString("%1/%2").arg(_NowKeyNum).arg(_KeyNum));
        _TASK_LOCK.unlock();
    }
}

void BatchOperateDialog::runError(const int taskid, const QString & error) {
    if(taskid == THREAD_BATCH_DEL_KEY_TASK)
        QMessageBox::critical(this, tr("错误"), error);
}

void BatchOperateDialog::finishWork(const int taskid) {
    _vTaskId.removeOne(taskid);
    if(taskid == THREAD_BATCH_DEL_KEY_TASK) {
        if(_vTaskId.indexOf(taskid) == -1) {
            ui->_okPushButton->setEnabled(true);
            ui->_batchComboBox->setEnabled(true);
            _isRun = false;
            ui->_progressBar->setValue(ui->_progressBar->maximum());
            QDateTime nowDateTime = QDateTime::currentDateTime();
            ui->_textBrowser->append("");
            ui->_textBrowser->append(QString("[%1] %2,total num is %3...")
                                     .arg(nowDateTime.toString("yyyy-MM-dd HH:mm:ss"))
                                     .arg("End delete key").arg(_NowKeyNum));
        }
    }
}

void BatchOperateDialog::on__cancelPushButton_clicked()
{
    if(!_isRun)
        return;
    if(_operate == "Delete")
        emit cancelWork(THREAD_BATCH_DEL_KEY_TASK);
}

void BatchOperateDialog::on__okPushButton_clicked()
{
    if(_vTaskId.size() > 0) {
        QMessageBox::information(this, tr("错误"), tr("存在未完成任务,稍后重试!"));
        return;
    }

    if(!_redisCluster) {
        QMessageBox::information(this, tr("错误"), tr("客户端连接为空!"));
        return;
    }

    _operate = ui->_batchComboBox->currentText();
    if(_operate == "Delete") {
        _patternCount = ui->_patternCountLineEdit->text().toInt();
        if(_patternCount <= 0) {
            QMessageBox::information(this, tr("错误"), tr("键模式个数最小为1！"));
            return;
        }
        _patternSeparator = ui->_patternSeparatorLineEdit->text();
        if(_patternSeparator.isEmpty()) {
            QMessageBox::information(this, tr("错误"), tr("键模式分隔符不可为空！"));
            return;
        }
        _keyPattern = ui->_patternlineEdit->text();
        QStringList vKeyPattern = _keyPattern.split(_patternSeparator,QString::SkipEmptyParts);
        if(vKeyPattern.size() != _patternCount) {
            QMessageBox::information(this, tr("错误"), tr("键模式与个数不匹配！"));
            return;
        }

        _NowKeyNum = 0;
        if(!_redisCluster->dbsize(_KeyNum)) {
            QMessageBox::information(this, tr("错误"), tr("获取数据库键总数失败!"));
            return;
        }

        _vTaskId.clear();
        _isRun = true;
        ui->_textBrowser->clear();
        ui->_okPushButton->setEnabled(false);
        ui->_batchComboBox->setEnabled(false);
        QDateTime nowDateTime = QDateTime::currentDateTime();
        ui->_textBrowser->append(QString("[%1] %2")
                                 .arg(nowDateTime.toString("yyyy-MM-dd HH:mm:ss"))
                                 .arg("Begin delete key..."));
        ui->_textBrowser->append("");
        for(int i = 0; i < _vMasterClients.size(); ++i) {
            if(_isClusterMode) {
                _taskMsg = new TaskMsg();
                _taskMsg->_taskid = THREAD_BATCH_DEL_KEY_TASK;
                _vTaskId.push_back(_taskMsg->_taskid);
                _taskMsg->_sequence = i;
                _taskMsg->_host = _vMasterClients[i]._host;
                _taskMsg->_port = _vMasterClients[i]._port;
                _taskMsg->_passwd = _vMasterClients[i]._passwd;
                _taskMsg->_key = _patternSeparator;
                _taskMsg->_keyPattern = _keyPattern;
                _taskMsg->_dbIndex = 0;
                _workThread = new WorkThread(_taskMsg);

                connect(_workThread, &WorkThread::sendData, this, &BatchOperateDialog::recvData);
                connect(_workThread, &WorkThread::finishWork, this, &BatchOperateDialog::finishWork);
                connect(_workThread, &WorkThread::runError, this, &BatchOperateDialog::runError);
                connect(this, &BatchOperateDialog::cancelWork, _workThread, &WorkThread::cancelWork);

                _threadPool->start(_workThread);
                _taskMsg = nullptr;
                _workThread = nullptr;
            } else {
                for(int j = 0; j < _idbNums; ++j) {
                    _taskMsg = new TaskMsg();
                    _taskMsg->_taskid = THREAD_BATCH_DEL_KEY_TASK;
                    _vTaskId.push_back(_taskMsg->_taskid);
                    _taskMsg->_sequence = i;
                    _taskMsg->_host = _vMasterClients[i]._host;
                    _taskMsg->_port = _vMasterClients[i]._port;
                    _taskMsg->_passwd = _vMasterClients[i]._passwd;
                    _taskMsg->_key = _patternSeparator;
                    _taskMsg->_keyPattern = _keyPattern;
                    _taskMsg->_dbIndex = j;
                    _workThread = new WorkThread(_taskMsg);

                    connect(_workThread, &WorkThread::sendData, this, &BatchOperateDialog::recvData);
                    connect(_workThread, &WorkThread::finishWork, this, &BatchOperateDialog::finishWork);
                    connect(_workThread, &WorkThread::runError, this, &BatchOperateDialog::runError);
                    connect(this, &BatchOperateDialog::cancelWork, _workThread, &WorkThread::cancelWork);

                    _threadPool->start(_workThread);
                    _taskMsg = nullptr;
                    _workThread = nullptr;
                }
            }
        }
    } else if(_operate == "Scan key") {

    } else if(_operate == "Import from oracle") {

    } else if(_operate == "Export to oracle") {

    } else if(_operate == "Import from mysql") {

    } else if(_operate == "Export to mysql") {

    } else if(_operate == "Delete oracle key") {

    } else if(_operate == "Delete mysql key") {

    }
}

void BatchOperateDialog::on__exitPushButton_clicked()
{
    if(_isRun) {
        if(QMessageBox::Yes != QMessageBox::question(this, tr("确认"), tr("任务未结束，确定退出么？")))
            return;
        on__cancelPushButton_clicked();
    }
    accept();
}
