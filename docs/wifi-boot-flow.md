# Wi-Fi boot flow

How `connect_wifi()` (`firmware/src/main.cpp`) decides what to try, and when the setup
wizard takes over. Source of truth is the code; this diagram is a read of it as of commit
`babce59`.

```mermaid
flowchart TD
    A["Boot"] --> B{"Credentials saved in NVS?"}

    B -- "no" --> C{"secrets.h built in?"}
    C -- "no" --> W
    C -- "yes" --> D["1 attempt: built-in network"]
    D -- "connected" --> E["Continue boot<br/>not saved to NVS"]
    D -- "failed" --> W

    B -- "yes" --> F["Candidate list: built-in (if any) + saved<br/>alternating, 6 attempts total"]
    F --> G{"Any attempt connects?"}
    G -- "yes" --> H["Continue boot<br/>saved candidate persisted again"]
    G -- "no, all 6 failed" --> W

    W["Setup wizard"] --> X{"Display available?"}
    X -- "no" --> Y["Setup access point<br/>BTClock-Setup / 192.168.4.1"]
    X -- "yes" --> Z["On-screen wizard:<br/>scan, pick network, enter password"]

    Z -- "connects" --> S["Save ssid/password to NVS"]
    Z -- "fails" --> Z2["FAILED screen, retry"]
    Z2 --> Z
    Z -- "unanswered 5 min" --> Y

    Y -- "credentials submitted and connect" --> S
    S --> E2["Continue boot"]
```

## Notes

* **No saved credentials = never set up.** One quick attempt at the built-in network (if the
  build has one), then straight to the wizard. There is no known-good network yet worth
  waiting for.
* **Saved credentials = patient.** Up to 6 attempts (~3 minutes), alternating between the
  built-in network and the saved one, so a router that boots slower than the device after a
  power cut is still waited for.
* **The built-in network is never written to NVS.** Only a network chosen through the wizard
  (or the headless portal) is persisted, and only once, in `g_ssid`/`g_pass` via
  `wifi_save_credentials()`.
* **Headless** (safe mode, or no display): no on-screen wizard, straight to the
  `BTClock-Setup` access point.
* **With a display:** the wizard reuses the same connect path (`try_candidate` /
  `wifi_start_reconnect_task`); if nobody answers it for 5 minutes, it hands over to the
  access point too, since a screen nobody can use must not be the only way in.
