#ifndef LOGPROCESSOR_H
#define LOGPROCESSOR_H

#include <QThread>
#include <QList>
#include <QString>

class LogFileReader;
class Exchanger;

class LogProcessor : public QThread
{
    Q_OBJECT
public:
    enum Option {
        NoOptions = 0x0,
        CutHalt = 0x1,
        CutNonGame = 0x2,
        CutStop = 0x4,
        CutBallplacement = 0x8,
        CutSimulated = 0x10
    };
    Q_DECLARE_FLAGS(Options, Option)

    explicit LogProcessor(const QList<QString>& inputFiles, const QString& outputFile,
                          Options options, QObject *parent = 0);
    ~LogProcessor() override;
    LogProcessor(const LogProcessor&) = delete;
    LogProcessor& operator=(const LogProcessor&) = delete;

    void run() override;

signals:
    void progressUpdate(const QString& progress);
    void finished();
    void error(const QString &message);

private:
    qint64 filterLog(LogFileReader &reader, Exchanger *writer, Exchanger *dump, qint64 lastTime);
    void signalFrames(int currentFrame, int totalFrames) { emit progressUpdate(QString("Processed %1 of %2 frames").arg(currentFrame).arg(totalFrames)); }

    QList<QString> m_inputFiles;
    QString m_outputFile;
    Options m_options;

    int m_currentFrame;
    int m_totalFrames;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(LogProcessor::Options)

#endif // LOGPROCESSOR_H
