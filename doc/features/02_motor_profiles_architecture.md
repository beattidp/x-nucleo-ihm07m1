# Feature Specification: Modular Motor Profiles Architecture

- **Status**: Implemented & Verified
- **Date**: 2026-10-17
- **Source Modules**: [`include/motor_profiles.h`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/include/motor_profiles.h), [`src/motor_profiles.c`](file:///home/beattidp/Documents/antigravity/x-nucleo-ihm07m1/src/motor_profiles.c)

---

## 1. Overview & Purpose
Different brushless DC motors vary widely in electrical characteristics, pole counts, nominal voltages, winding resistances, and operating speed limits. Hardcoding motor characteristics directly into driver logic prevents rapid evaluation across different motors.

The **Motor Profiles System** abstracts motor physical parameters into a structured, swappable table of configurations, allowing runtime switching between factory profiles or loading characterization measurements into a configurable user profile.

---

## 2. Profile Data Model

```c
typedef struct {
    const char* name;            /* Descriptive name of the motor */
    uint8_t     pole_pairs;      /* Number of rotor pole pairs (Poles / 2) */
    float       rated_voltage_v; /* Nominal bus voltage rating (V) */
    float       max_continuous_amps; /* Continuous thermal phase current limit (A) */
    float       hw_break_trip_amps;  /* Hardware overcurrent trip reference (A) */
    float       min_rpm;         /* Minimum stable open-loop running speed (RPM) */
    float       max_rpm;         /* Maximum rated speed (RPM) */
    float       base_duty_pct;   /* Starting stator flux duty cycle (%) */
    float       vf_slope_pct;    /* Dynamic Back-EMF compensation slope (% / 1000 RPM) */
    float       accel_slew_rpm_s;/* Maximum acceleration rate (RPM / s) */
    float       kv_rating;       /* Motor velocity constant (RPM / V) */
} MotorProfile_t;
```

---

## 3. Standard Profile Catalog

| Profile ID | Profile Name | Pole Pairs | Voltage | Speed Range | $Kv$ | Intended Application |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `0` | **Default Low-KV 7PP** | 7 (14 poles) | 12.0 V | 300 - 3000 RPM | 575 | General small drone / evaluation |
| `1` | **Flywheel 5010-360KV** | 14 (28 poles) | 24.0 V | 300 - 5000 RPM | 360 | High-inertia flywheel balancing |
| `2` | **High-Torque 170KV** | 14 (28 poles) | 24.0 V | 150 - 3000 RPM | 170 | Heavy gimbal / low-speed direct drive |
| `3` | **Custom / Characterized** | Dynamic | Dynamic | Dynamic | Measured | Saved automatically from Characterization |

---

## 4. API & Console Integration
- `void MotorProfiles_Init(void)`: Initializes default catalog.
- `bool MotorProfiles_Select(uint8_t index)`: Switches active profile and updates active driver parameters.
- `const MotorProfile_t* MotorProfiles_GetActive(void)`: Retrieves active configuration for control loops.
- `void MotorProfiles_SaveCustom(...)`: Updates Profile 3 with live measured values from characterization.
- **Console Hotkey**: Pressing `[P]` in the interactive ANSI dashboard cycles to the next available motor profile in real time.
