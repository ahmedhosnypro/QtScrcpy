// Optimization: Pre-allocate control messages for common keys
class OptimizedController : public Controller {
private:
    QHash<int, ControlMsg*> m_preAllocatedMsgs;
    
public:
    OptimizedController() {
        // Pre-allocate messages for common keys
        preAllocateCommonKeys();
    }
    
    void preAllocateCommonKeys() {
        // Pre-create messages for WASD, arrow keys, etc.
        QList<AndroidKeycode> commonKeys = {
            AKEYCODE_W, AKEYCODE_A, AKEYCODE_S, AKEYCODE_D,
            AKEYCODE_DPAD_UP, AKEYCODE_DPAD_DOWN, 
            AKEYCODE_DPAD_LEFT, AKEYCODE_DPAD_RIGHT
        };
        
        for (auto key : commonKeys) {
            ControlMsg* downMsg = new ControlMsg(ControlMsg::CMT_INJECT_KEYCODE);
            downMsg->setInjectKeycodeMsgData(AKEY_EVENT_ACTION_DOWN, key, 0, AMETA_NONE);
            m_preAllocatedMsgs[key] = downMsg;
        }
    }
    
    // Fast path for pre-allocated keys
    void postKeyCodeClickFast(AndroidKeycode keycode) {
        if (m_preAllocatedMsgs.contains(keycode)) {
            // Reuse pre-allocated message
            sendControl(m_preAllocatedMsgs[keycode]->serializeData());
        } else {
            // Fallback to normal path
            postKeyCodeClick(keycode);
        }
    }
};
