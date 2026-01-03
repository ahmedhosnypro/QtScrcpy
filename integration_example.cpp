// Integration into existing InputConvertGame class
class ThreadedInputConvertGame : public InputConvertGame {
private:
    ThreadedKeymapProcessor m_threadedProcessor;

public:
    ThreadedInputConvertGame(Controller *controller) : InputConvertGame(controller) {
        // Initialize threaded processor with send function
        m_threadedProcessor.setSendFunction([this](const QByteArray& data) {
            // Use existing controller's send mechanism
            if (m_controller) {
                m_controller->sendControlData(data);
            }
        });
    }

    void loadKeyMap(const QString &json) override {
        // Load keymap normally
        InputConvertGame::loadKeyMap(json);
        
        // Initialize threads for each configured key
        m_threadedProcessor.initializeForKeymap(json);
    }

    void keyEvent(const QKeyEvent *from, const QSize &frameSize, const QSize &showSize) override {
        if (!m_gameMap) {
            return InputConvertGame::keyEvent(from, frameSize, showSize);
        }

        // Check if this key has a dedicated thread
        const KeyMap::KeyMapNode &node = m_keyMap.getKeyMapNodeKey(from->key());
        if (node.type != KeyMap::KMT_INVALID) {
            // Send to dedicated thread for processing
            QPointF pos = calcFrameAbsolutePos(node.data.click.keyNode.pos);
            m_threadedProcessor.processKeyEvent(from->key(), 
                                              from->type() == QEvent::KeyPress, 
                                              pos);
            return;
        }

        // Fallback to normal processing
        InputConvertGame::keyEvent(from, frameSize, showSize);
    }
};
