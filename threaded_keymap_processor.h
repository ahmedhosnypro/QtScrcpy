#pragma once
#include <QThread>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include "keymap.h"

class KeymapWorkerThread : public QThread {
    Q_OBJECT
public:
    struct KeyEvent {
        int key;
        KeyMap::KeyMapType type;
        QPointF pos;
        bool pressed;
    };

    KeymapWorkerThread(int keyCode, std::function<void(const QByteArray&)> sendFunc)
        : m_keyCode(keyCode), m_sendFunction(sendFunc) {}

    void processKey(const KeyEvent& event) {
        QMutexLocker locker(&m_mutex);
        m_eventQueue.enqueue(event);
        m_condition.wakeOne();
    }

protected:
    void run() override {
        while (!isInterruptionRequested()) {
            QMutexLocker locker(&m_mutex);
            if (m_eventQueue.isEmpty()) {
                m_condition.wait(&m_mutex);
                continue;
            }
            
            KeyEvent event = m_eventQueue.dequeue();
            locker.unlock();
            
            // Process based on keymap type
            switch (event.type) {
                case KeyMap::KMT_CLICK:
                    processClick(event);
                    break;
                case KeyMap::KMT_DRAG:
                    processDrag(event);
                    break;
                case KeyMap::KMT_STEER_WHEEL:
                    processSteerWheel(event);
                    break;
            }
        }
    }

private:
    void processClick(const KeyEvent& event) {
        // Generate touch event and send immediately
        QByteArray data = generateTouchData(event.pos, event.pressed);
        m_sendFunction(data);
    }

    void processDrag(const KeyEvent& event) {
        // Handle drag sequence in this thread
        // No blocking of main thread
    }

    void processSteerWheel(const KeyEvent& event) {
        // Handle continuous movement
    }

    QByteArray generateTouchData(QPointF pos, bool pressed);

    int m_keyCode;
    std::function<void(const QByteArray&)> m_sendFunction;
    QQueue<KeyEvent> m_eventQueue;
    QMutex m_mutex;
    QWaitCondition m_condition;
};

class ThreadedKeymapProcessor {
public:
    void initializeForKeymap(const QString& keymapJson) {
        // Parse keymap and create one thread per configured key
        QJsonDocument doc = QJsonDocument::fromJson(keymapJson.toUtf8());
        auto nodes = doc.object()["keyMapNodes"].toArray();
        
        for (const auto& node : nodes) {
            int key = getKeyFromNode(node.toObject());
            m_keyThreads[key] = new KeymapWorkerThread(key, m_sendFunction);
            m_keyThreads[key]->start();
        }
    }

    void processKeyEvent(int key, bool pressed, QPointF pos) {
        if (m_keyThreads.contains(key)) {
            KeymapWorkerThread::KeyEvent event{key, getKeyType(key), pos, pressed};
            m_keyThreads[key]->processKey(event);
        }
    }

private:
    QHash<int, KeymapWorkerThread*> m_keyThreads;
    std::function<void(const QByteArray&)> m_sendFunction;
};
