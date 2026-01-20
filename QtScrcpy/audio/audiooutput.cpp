#include <QTcpSocket>
#include <QHostAddress>
#include <QAudioOutput>
#include <QTime>
#include <QElapsedTimer>
#include <QMutexLocker>
#include <QCoreApplication>

#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
#include <QAudioSink>
#include <QAudioDevice>
#include <QMediaDevices>
#endif

#include "audiooutput.h"

AudioOutput::AudioOutput(QObject *parent)
    : QObject(parent)
{
    m_running = false;
    m_isRestarting = false;
    
    // Set Qt application properties for PulseAudio
    QCoreApplication::setApplicationName("QtScrcpy");
    QCoreApplication::setOrganizationName("QtScrcpy");
    
    qInfo() << "AudioOutput::Constructor - Application properties set:";
    qInfo() << "  ApplicationName:" << QCoreApplication::applicationName();
    qInfo() << "  OrganizationName:" << QCoreApplication::organizationName();
    
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    m_audioOutput = nullptr;
#else
    m_audioSink = nullptr;
#endif
    connect(&m_sndcpy, &QProcess::readyReadStandardOutput, this, [this]() {
        qInfo() << QString("AudioOutput::") << QString(m_sndcpy.readAllStandardOutput());
    });
    connect(&m_sndcpy, &QProcess::readyReadStandardError, this, [this]() {
        qInfo() << QString("AudioOutput::") << QString(m_sndcpy.readAllStandardError());
    });
}

AudioOutput::~AudioOutput()
{
    if (QProcess::NotRunning != m_sndcpy.state()) {
        m_sndcpy.kill();
    }
    stop();
}

bool AudioOutput::start(const QString& serial, int port)
{
    if (m_running) {
        stop();
    }

    m_lastSerial = serial;
    m_lastPort = port;
    m_failureCount.storeRelaxed(0);

    QElapsedTimer timeConsumeCount;
    timeConsumeCount.start();
    bool ret = runSndcpyProcess(serial, port);
    qInfo() << "AudioOutput::run sndcpy cost:" << timeConsumeCount.elapsed() << "milliseconds";
    if (!ret) {
        handleFailure();
        return ret;
    }

    startAudioOutput();
    startRecvData(port);

    m_running = true;
    return true;
}

void AudioOutput::stop()
{
    if (!m_running) {
        return;
    }
    m_running = false;

    stopRecvData();
    stopAudioOutput();
}

void AudioOutput::installonly(const QString &serial, int port)
{
    runSndcpyProcess(serial, port, false);
}

void AudioOutput::setVolume(qreal volume)
{
    m_pendingVolume = volume;
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (m_audioOutput) {
        m_audioOutput->setVolume(volume);
    }
#else
    if (m_audioSink) {
        m_audioSink->setVolume(volume);
    }
#endif
}

bool AudioOutput::runSndcpyProcess(const QString &serial, int port, bool wait)
{
    if (QProcess::NotRunning != m_sndcpy.state()) {
        m_sndcpy.kill();
    }

#ifdef Q_OS_WIN32
    QStringList params{serial, QString::number(port)};
    m_sndcpy.start("sndcpy.bat", params);
#else
    QStringList params{"sndcpy.sh", serial, QString::number(port)};
    m_sndcpy.start("bash", params);
#endif

    if (!wait) {
        return true;
    }

    if (!m_sndcpy.waitForStarted()) {
        qWarning() << "AudioOutput::start sndcpy process failed";
        return false;
    }
    if (!m_sndcpy.waitForFinished()) {
        qWarning() << "AudioOutput::sndcpy process crashed";
        return false;
    }

    return true;
}

void AudioOutput::startAudioOutput()
{
    qInfo() << "AudioOutput::startAudioOutput() - Creating audio device";
    qInfo() << "  Current PID:" << QCoreApplication::applicationPid();
    qInfo() << "  Object address:" << (void*)this;
    
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (m_audioOutput) {
        qInfo() << "AudioOutput::startAudioOutput() - Audio output already exists";
        return;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleSize(16);
    format.setCodec("audio/pcm");
    format.setByteOrder(QAudioFormat::LittleEndian);
    format.setSampleType(QAudioFormat::SignedInt);
    QAudioDeviceInfo info(QAudioDeviceInfo::defaultOutputDevice());

    if (!info.isFormatSupported(format)) {
        qWarning() << "AudioOutput::audio format not supported, cannot play audio.";
        handleFailure();
        return;
    }

    qInfo() << "AudioOutput::Creating QAudioOutput with format:" << format;
    m_audioOutput = new QAudioOutput(format, this);
    m_audioOutput->setObjectName("QtScrcpy");
    qInfo() << "AudioOutput::QAudioOutput created - ObjectName:" << m_audioOutput->objectName();
    qInfo() << "AudioOutput::QAudioOutput address:" << (void*)m_audioOutput;
    
    connect(m_audioOutput, &QAudioOutput::stateChanged, this, [this](QAudio::State state) {
        qInfo() << "AudioOutput::audio state changed:" << state;
        if (state == QAudio::StoppedState && m_running) {
            handleFailure();
        }
    });
    m_audioOutput->setBufferSize(48000*2*15/1000 * 20);
    qInfo() << "AudioOutput::Starting audio output device...";
    m_outputDevice = m_audioOutput->start();
    qInfo() << "AudioOutput::Audio output device started:" << (void*)m_outputDevice;
    
    // Apply pending volume if it was set before audio device was created
    if (m_pendingVolume >= 0.0) {
        m_audioOutput->setVolume(m_pendingVolume);
        qInfo() << "AudioOutput::Applied pending volume:" << m_pendingVolume;
    }
#else
    if (m_audioSink) {
        qInfo() << "AudioOutput::startAudioOutput() - Audio sink already exists";
        return;
    }

    QAudioFormat format;
    format.setSampleRate(48000);
    format.setChannelCount(2);
    format.setSampleFormat(QAudioFormat::Int16);
    QAudioDevice defaultDevice = QMediaDevices::defaultAudioOutput();
    if (!defaultDevice.isFormatSupported(format)) {
        qWarning() << "AudioOutput::audio format not supported, cannot play audio.";
        handleFailure();
        return;
    }
    
    qInfo() << "AudioOutput::Creating QAudioSink with format:" << format;
    m_audioSink = new QAudioSink(defaultDevice, format, this);
    m_audioSink->setObjectName("QtScrcpy");
    qInfo() << "AudioOutput::QAudioSink created - ObjectName:" << m_audioSink->objectName();
    qInfo() << "AudioOutput::QAudioSink address:" << (void*)m_audioSink;
    
    qInfo() << "AudioOutput::Starting audio sink device...";
    m_outputDevice = m_audioSink->start();
    qInfo() << "AudioOutput::Audio sink device started:" << (void*)m_outputDevice;
    
    if (!m_outputDevice) {
        qWarning() << "AudioOutput::audio output device not available, cannot play audio.";
        delete m_audioSink;
        m_audioSink = nullptr;
        handleFailure();
        return;
    }
    
    // Apply pending volume if it was set before audio device was created
    if (m_pendingVolume >= 0.0) {
        m_audioSink->setVolume(m_pendingVolume);
        qInfo() << "AudioOutput::Applied pending volume:" << m_pendingVolume;
    }
#endif
}

void AudioOutput::stopAudioOutput()
{
    qInfo() << "AudioOutput::stopAudioOutput() - Stopping audio device";
#if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
    if (m_audioOutput) {
        qInfo() << "AudioOutput::Stopping QAudioOutput:" << (void*)m_audioOutput;
        m_audioOutput->stop();
        delete m_audioOutput;
        m_audioOutput = nullptr;
        qInfo() << "AudioOutput::QAudioOutput deleted";
    }
#else
    if (m_audioSink) {
        qInfo() << "AudioOutput::Stopping QAudioSink:" << (void*)m_audioSink;
        m_audioSink->stop();
        delete m_audioSink;
        m_audioSink = nullptr;
        qInfo() << "AudioOutput::QAudioSink deleted";
    }
#endif
    m_outputDevice = nullptr;
    qInfo() << "AudioOutput::stopAudioOutput() - Complete";
}

void AudioOutput::startRecvData(int port)
{
    if (m_workerThread.isRunning()) {
        stopRecvData();
    }

    auto audioSocket = new QTcpSocket();
    audioSocket->moveToThread(&m_workerThread);
    connect(&m_workerThread, &QThread::finished, audioSocket, &QObject::deleteLater);

    connect(this, &AudioOutput::connectTo, audioSocket, [this, audioSocket](int port) {
        audioSocket->connectToHost(QHostAddress::LocalHost, port);
        if (!audioSocket->waitForConnected(500)) {
            qWarning("AudioOutput::audio socket connect failed");
            handleFailure();
            return;
        }
        qInfo("AudioOutput::audio socket connect success");
        m_failureCount.storeRelaxed(0);
    });
    connect(audioSocket, &QIODevice::readyRead, audioSocket, [this, audioSocket]() {
        qint64 recv = audioSocket->bytesAvailable();

        if (!m_outputDevice || recv <= 0) {
            return;
        }
        
        // Validate audio frame size (16-bit stereo at 48kHz)
        const int frameSize = 4; // 2 channels * 2 bytes per sample
        qint64 validSize = (recv / frameSize) * frameSize;
        
        if (validSize <= 0) {
            return;
        }

        if (m_buffer.capacity() < validSize) {
            m_buffer.reserve(validSize);
        }

        qint64 count = audioSocket->read(m_buffer.data(), validSize);
        if (count > 0 && (count % frameSize) == 0) {
            qint64 written = m_outputDevice->write(m_buffer.data(), count);
            if (written != count) {
                qWarning() << "AudioOutput::write failed, expected:" << count << "written:" << written;
                handleFailure();
            }
        }
    });
    connect(audioSocket, &QTcpSocket::stateChanged, audioSocket, [](QAbstractSocket::SocketState state) {
        qInfo() << "AudioOutput::audio socket state changed:" << state;
    });
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    connect(audioSocket, &QTcpSocket::errorOccurred, audioSocket, [this](QAbstractSocket::SocketError error) {
        qInfo() << "AudioOutput::audio socket error occurred:" << error;
        handleFailure();
    });
#else
    connect(audioSocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), audioSocket, [this](QAbstractSocket::SocketError error) {
        qInfo() << "AudioOutput::audio socket error occurred:" << error;
        handleFailure();
    });
#endif

    m_workerThread.start();
    emit connectTo(port);
}

void AudioOutput::stopRecvData()
{
    if (!m_workerThread.isRunning()) {
        return;
    }

    m_workerThread.quit();
    m_workerThread.wait();
}

void AudioOutput::handleFailure()
{
    int failures = m_failureCount.fetchAndAddRelaxed(1) + 1;
    qWarning() << "AudioOutput::failure count:" << failures;
    
    if (failures > 10) {
        qWarning() << "AudioOutput::failure limit exceeded, stopping audio permanently";
        stop();
        m_failureCount.storeRelaxed(0);
    }
}

void AudioOutput::restartAudio()
{
    QMutexLocker locker(&m_restartMutex);
    
    if (m_isRestarting) {
        return;
    }
    
    m_isRestarting = true;
    qInfo() << "AudioOutput::restarting audio service";
    
    stop();
    
    if (!m_lastSerial.isEmpty() && m_lastPort > 0) {
        QThread::msleep(500);
        start(m_lastSerial, m_lastPort);
    }
    
    m_isRestarting = false;
}
