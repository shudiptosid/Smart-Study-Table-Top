# Smart Study Table

An Arduino-based intelligent study table system that helps students stay focused by managing study sessions with automatic presence detection, phone monitoring, and timer functionality.

## Features

- **Presence Detection**: Automatically turns on/off LED lighting based on student presence using ultrasonic sensor
- **Study Timer**: Configurable countdown timer (1-600 minutes) with rotary encoder
- **Phone Management**: Detects phone placement and monitors phone presence during study sessions
- **Alert System**: Active buzzer alerts when phone is removed during countdown
- **Real-Time Clock**: Displays current date and time with DS3231 RTC module
- **OLED Display**: 128x64 SH1106 display showing timer, status, and system information
- **State Machine**: Intuitive workflow from idle to timer setting to countdown

## Hardware Components

| Component      | Pin/Interface         | Description                      |
| -------------- | --------------------- | -------------------------------- |
| SH1106 OLED    | I2C (A4/A5)           | 128x64 display                   |
| DS3231 RTC     | I2C (A4/A5)           | Real-time clock module           |
| HC-SR04        | Trig: 8, Echo: 9      | Ultrasonic distance sensor       |
| Rotary Encoder | A: 2, B: 3, Button: 4 | Timer control and navigation     |
| IR Sensor      | Pin 6                 | Phone detection                  |
| Buzzer         | Pin 5                 | Active/passive buzzer for alerts |
| Relay Module   | Pin 7                 | LED light control                |

## Wiring Diagram

```
Arduino UNO Connections:
- A4 (SDA) → OLED & RTC SDA
- A5 (SCL) → OLED & RTC SCL
- Pin 2 → Encoder A (with interrupt)
- Pin 3 → Encoder B (with interrupt)
- Pin 4 → Encoder Button
- Pin 5 → Buzzer
- Pin 6 → IR Sensor
- Pin 7 → Relay Module
- Pin 8 → HC-SR04 Trig
- Pin 9 → HC-SR04 Echo
- 5V & GND → Power all modules
```

## Software Dependencies

Install these libraries via Arduino Library Manager:

- **U8g2** by oliver - OLED display driver
- **RTClib** by Adafruit - Real-time clock interface
- **Wire** - I2C communication (built-in)

## Configuration

### Buzzer Type

```cpp
#define ACTIVE_BUZZER 1  // Set to 1 for active, 0 for passive buzzer
```

### IR Sensor Pull-up

```cpp
#define USE_PULLUP_FOR_IR true  // Enable internal pull-up resistor
```

### Thresholds

```cpp
const int presenceThreshold = 100;  // Distance in cm for presence detection
const unsigned long LONG_PRESS_MS = 2000;  // Long press duration for RTC set
```

## How to Use

### Basic Operation

1. **IDLE State**: System displays current time and distance

   - Press encoder button to enter timer setup

2. **SET TIMER State**: Adjust study duration

   - Rotate encoder to change minutes (1-600 min)
   - 4 encoder clicks = 1 minute change
   - Press button to confirm

3. **WAIT PHONE State**: Place phone to begin

   - IR sensor detects phone placement
   - Countdown starts automatically when phone detected

4. **COUNTDOWN State**: Study session in progress
   - Timer counts down on display
   - LED stays on if student present
   - Buzzer alerts if phone removed
   - Press button to cancel

### RTC Time Setting

#### Method 1: Serial Commands

Open Serial Monitor at 115200 baud:

```
SETNOW                     // Set to compile time
SET 2025 12 12 14 30 00   // Set to specific date/time (YYYY MM DD HH MM SS)
READ                       // Read current RTC time
```

#### Method 2: Long Press

Hold encoder button for 2+ seconds to set RTC to compile time

## Operation States

```
IDLE → SET_TIMER → WAIT_PHONE → COUNTDOWN → IDLE
  ↑                                  ↓
  └──────────── ABORTED ←───────────┘
```

- **IDLE**: Waiting for user input
- **SET_TIMER**: Adjusting countdown duration
- **WAIT_PHONE**: Waiting for phone to be placed
- **COUNTDOWN**: Active study session
- **ABORTED**: Cancelled by user (returns to IDLE)

## Display Information

The OLED shows:

- Line 1: Current date and time (DD-MM-YYYY HH:MM:SS)
- Line 2: Distance measurement and LED status
- Line 3+: Current state and relevant information
  - Timer setting value
  - Countdown remaining time (MM:SS)
  - Phone detection status
  - Instruction prompts

## Technical Details

### Timing System

- **Non-blocking**: Uses `millis()` for all timing operations
- **Countdown precision**: Uses end timestamp (`countdownEndMillis`) for accurate timing
- **Update intervals**:
  - Display: 250ms
  - Ultrasonic: 200ms
  - Phone check: 150ms
  - Buzzer toggle: 220ms

### Encoder Handling

- Interrupt-driven reading on pins 2 and 3
- Quadrature decoding for direction detection
- Debounced button with 50ms delay
- Long press detection (2000ms threshold)

### Presence Detection

- Range: 0-1000cm (practical: up to 400cm)
- Threshold: 100cm (configurable)
- Timeout: 30ms for ultrasonic pulse
- LED automatically controlled based on presence

## Troubleshooting

| Issue                  | Solution                                                 |
| ---------------------- | -------------------------------------------------------- |
| "Couldn't find RTC"    | Check I2C wiring (SDA→A4, SCL→A5) and power              |
| Display not working    | Verify I2C address matches SH1106 and connections        |
| Encoder not responding | Check pull-up resistors and pin connections (2, 3, 4)    |
| Buzzer not sounding    | Verify ACTIVE_BUZZER setting matches hardware            |
| Phone not detected     | Adjust IR sensor position and check PHONE_DETECTED_STATE |
| Incorrect distance     | Ensure HC-SR04 has clear line of sight, check wiring     |

## Serial Commands

| Command  | Parameters          | Description                   |
| -------- | ------------------- | ----------------------------- |
| `SETNOW` | None                | Set RTC to compile timestamp  |
| `SET`    | YYYY MM DD HH MM SS | Set RTC to specific date/time |
| `READ`   | None                | Display current RTC date/time |

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Author

**Shudipto Gain**

## Acknowledgments

- U8g2 library by oliver
- RTClib by Adafruit
- Arduino community

## Version History

- **v1.0** (2025): Initial release
  - Full state machine implementation
  - Robust countdown timing
  - Encoder with long-press RTC setting
  - Non-blocking operation
  - Serial command interface

## Future Enhancements

Potential improvements:

- WiFi connectivity for remote monitoring
- Data logging to SD card
- Multiple user profiles
- Adjustable buzzer patterns
- Mobile app integration
- Study statistics tracking
