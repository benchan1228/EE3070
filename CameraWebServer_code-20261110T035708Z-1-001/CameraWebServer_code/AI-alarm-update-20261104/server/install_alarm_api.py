"""Run in the dashboard folder. Preserves main.py and does not touch AI data."""
from datetime import datetime
from pathlib import Path

root = Path(__file__).resolve().parent
main = root / "main.py"
text = main.read_text(encoding="utf-8")
if "install_device_alarm_api(app," in text:
    print("Alarm API already installed.")
else:
    markers = ['if __name__ == "__main__":', "if __name__=='__main__':",
               "if __name__ == '__main__':", 'if __name__=="__main__":']
    marker = next((m for m in markers if m in text), None)
    if not marker:
        raise SystemExit("No main entry point found. No files changed; inspect main.py first.")
    insert = ("from device_alarm_api import install_device_alarm_api\n"
              "from store import db as alarm_db\n"
              "install_device_alarm_api(app, alarm_db)\n\n")
    updated = text.replace(marker, insert + marker, 1)
    compile(updated, str(main), "exec")
    backup = main.with_name("main.py.before-alarm-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    backup.write_text(text, encoding="utf-8")
    main.write_text(updated, encoding="utf-8")
    print("Installed event status API. Restart dashboard. Backup:", backup.name)
