#ifndef AUDIOOUTPUT_H
#define AUDIOOUTPUT_H

#include <QThread>
#include <QProcess>
#include <QPointer>
#include <QVector>
#include <QAtomicInt>
#include <QMutex>

class QAudioSink;
class QAudioOutput;
class QIODevice;
class AudioOutput : public QObject
{
    Q_OBJECT
public:
    explicit AudioOutput(QObject *parent = nullptr);
    ~AudioOutput();

    bool start(const QString& serial, int port);
    void stop();
    void installonly(const QString& serial, int port);
    void setVolume(qreal volume);

private:
    bool runSndcpyProcess(const QString& serial, int port, bool wait = true);
    void startAudioOutput();
    void stopAudioOutput();
    void startRecvData(int port);
    void stopRecvData();
    void handleFailure();
    void restartAudio();

signals:
    void connectTo(int port);

private:
    QPointer<QIODevice> m_outputDevice;
    QThread m_workerThread;
    QProcess m_sndcpy;
    QVector<char> m_buffer;
    bool m_running = false;
    QAtomicInt m_failureCount{0};
    QMutex m_restartMutex;
    bool m_isRestarting = false;
    QString m_lastSerial;
    int m_lastPort = 0;
    qreal m_pendingVolume = -1.0; // -1 means not set yet
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    QAudioOutput* m_audioOutput = nullptr;
#else
    QAudioSink *m_audioSink = nullptr;
#endif
};

#endif // AUDIOOUTPUT_H
