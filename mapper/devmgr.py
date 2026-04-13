#!/usr/bin/env python3
# Interactive device manager for test mapper
# Arrow keys to navigate, Enter to toggle, q to quit

import os, json, sys, tty, termios, glob, signal

CONFIG = "/root/mapper-test/mapper_devices.json"
PID_FILE = "/root/mapper-test/mapper.pid"

def get_key():
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        ch = sys.stdin.read(1)
        if ch == '\x03':  # Ctrl+C
            raise KeyboardInterrupt
        if ch == '\x1b':
            ch2 = sys.stdin.read(2)
            if ch2 == '[A': return 'UP'
            if ch2 == '[B': return 'DOWN'
        return ch
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)

def discover():
    """Parse /proc/bus/input/devices and return mouse+keyboard devices"""
    devices = []
    name = handlers = ev = ""
    with open("/proc/bus/input/devices") as f:
        for line in f:
            line = line.strip()
            if line.startswith("N: Name="):
                name = line.split('"')[1]
            elif line.startswith("H: Handlers="):
                handlers = line.split("=",1)[1]
            elif line.startswith("B: EV="):
                ev = int(line.split("=")[1], 16)
            elif line == "":
                node = next((h for h in handlers.split() if h.startswith("event")), None)
                if node:
                    has_key = bool(ev & 0x2)
                    has_rel = bool(ev & 0x4)
                    is_mouse = has_key and has_rel
                    is_kbd   = has_key and not has_rel and "leds" in handlers
                    if is_mouse or is_kbd:
                        devices.append({
                            "path": f"/dev/input/{node}",
                            "name": name,
                            "type": "mouse" if is_mouse else "keyboard"
                        })
                name = handlers = ""
                ev = 0
    return devices

def load_config():
    if not os.path.exists(CONFIG):
        return {"keyboards": [], "mice": []}
    with open(CONFIG) as f:
        return json.load(f)

def save_config(cfg):
    with open(CONFIG, "w") as f:
        json.dump(cfg, f, indent=2)

def reload_mapper():
    try:
        pid = int(open(PID_FILE).read().strip())
        os.kill(pid, 12)  # SIGUSR2
        return True
    except:
        return False

def is_enabled(cfg, path):
    for d in cfg["keyboards"] + cfg["mice"]:
        if d["path"] == path:
            return d["enabled"]
    return False  # not in config = disabled

def toggle(cfg, dev):
    lst = cfg["mice"] if dev["type"] == "mouse" else cfg["keyboards"]
    for d in lst:
        if d["path"] == dev["path"]:
            d["enabled"] = not d["enabled"]
            return
    # not in config yet — add as enabled
    lst.append({"path": dev["path"], "name": dev["name"], "enabled": True})

def draw(devices, cfg, cursor):
    os.system("clear")
    mapper_alive = os.path.exists(PID_FILE) and os.path.exists(f"/proc/{open(PID_FILE).read().strip()}")
    print(f"  GameKeyMapper — Device Manager  [mapper: {'● RUNNING' if mapper_alive else '○ STOPPED'}]")
    print(f"  ↑↓ navigate  Enter=toggle  r=reload  q=quit\n")
    for i, dev in enumerate(devices):
        enabled = is_enabled(cfg, dev["path"])
        status = "\033[32m[ON] \033[0m" if enabled else "\033[31m[OFF]\033[0m"
        tag = f"[{dev['type'][:3].upper()}]"
        prefix = "  \033[7m" if i == cursor else "  "
        suffix = "\033[0m" if i == cursor else ""
        print(f"{prefix}{status} {tag} {dev['name']}  {dev['path']}{suffix}")
    print()

def main():
    devices = discover()
    if not devices:
        print("No input devices found."); return
    cfg = load_config()
    cursor = 0

    while True:
        draw(devices, cfg, cursor)
        k = get_key()
        if k == 'q':
            break
        elif k == 'UP':
            cursor = (cursor - 1) % len(devices)
        elif k == 'DOWN':
            cursor = (cursor + 1) % len(devices)
        elif k == '\r' or k == '\n':
            toggle(cfg, devices[cursor])
            save_config(cfg)
            reloaded = reload_mapper()
            cfg = load_config()  # re-read to reflect saved state
        elif k == 'r':
            reload_mapper()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nBye.")
        sys.exit(0)
