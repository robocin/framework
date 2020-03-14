#ifndef COMBINEDLOGWRITER_H
#define COMBINEDLOGWRITER_H

#include "protobuf/robot.pb.h"
#include "protobuf/status.h"

#include <QString>
#include <QObject>
#include <QList>

class LogFileWriter;
class BacklogWriter;
class QThread;
class QDateTime;
class QLabel;
class StatusSource;

class CombinedLogWriter : public QObject
{
    Q_OBJECT
public:
    CombinedLogWriter(bool replay, int backlogLength);
    ~CombinedLogWriter();
    CombinedLogWriter(const CombinedLogWriter&) = delete;
    CombinedLogWriter& operator=(const CombinedLogWriter&) = delete;
    std::shared_ptr<StatusSource> makeStatusSource();
    QList<Status> getBacklogStatus(int lastNPackets);
    Status getTeamStatus();
    static QString dateTimeToString(const QDateTime & dt);

signals:
    void setRecordButton(bool on);
    void enableRecordButton(bool enable);
    void enableBacklogButton(bool enable);
    void saveBacklogFile(QString filename, const Status &status, bool processEvents);
    void gotStatusForRecording(const Status &status);
    void gotStatusForBacklog(const Status &status);
    void changeLogTimeLabel(QString text);
    void showLogTimeLabel(bool show);
    void resetBacklog();
    void disableSkipping(bool disable);

public slots:
    void handleStatus(const Status &status);
    void enableLogging(bool enable); // enables or disables both record and backlog
    void backLogButtonClicked();
    void recordButtonToggled(bool enabled);
    void useLogfileLocation(bool enabled);

private:
    QString createLogFilename() const;
    void startLogfile();

private:
    bool m_isReplay;
    bool m_useSettingLocation = false;
    BacklogWriter *m_backlogWriter;
    QThread *m_backlogThread;
    LogFileWriter *m_logFile;
    QThread *m_logFileThread;

    robot::Team m_yellowTeam;
    robot::Team m_blueTeam;
    QString m_yellowTeamName;
    QString m_blueTeamName;

    qint64 m_lastTime;
    qint64 m_logStartTime;
    QString m_lastLogTimeLabel;

    bool m_isLoggingEnabled;
    bool m_isRecording;
};

#endif // COMBINEDLOGWRITER_H
