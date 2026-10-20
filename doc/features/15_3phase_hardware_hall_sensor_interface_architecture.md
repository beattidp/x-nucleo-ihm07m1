# Feature 15: STM32 Hardware 3-Phase Hall Sensor Interface Architecture on TIM2

## 1. Overview & Objective
Following the relocation of the optical tachometer to Morpho **CN10 Pin 4 (`PC6`)** using cascaded **`TIM3` (Master) + `TIM4` (Slave)** (Feature 14), 32-bit hardware timer **`TIM2`** and motor shield connector **J3** (`PA15`, `PB3`, `PB10`) were completely freed.

This feature configures STM32 hardware **3-Phase Hall Sensor Interface Mode** on `TIM2` to prepare the evaluation platform for sensored BLDC motors, while preserving 100% backward compatibility with the current sensorless bare-rotor motor.

---

## 2. Hardware Architecture & Signal Routing

### 2.1 Connector J3 Pin Mapping (X-NUCLEO-IHM07M1 & STM32G474RE)

| Connector J3 Pin | Silk / Function | Shield Arduino Net | STM32 Pin | Timer Function | AF Mode | Electrical Characteristics |
| :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **Pin 1** | `+5V` | `+5V` | - | Power supply for sensor ICs | - | 5.0 V DC bus |
| **Pin 2** | `GND` | `GND` | - | Ground return | - | Ground |
| **Pin 3** | `H1` / `A+` | `D2` | **PA15** | `TIM2_CH1` | `GPIO_AF1_TIM2` | Internal pullup enabled (open-collector compatible) |
| **Pin 4** | `H2` / `B+` | `D4` | **PB3** | `TIM2_CH2` | `GPIO_AF1_TIM2` | Internal pullup enabled (open-collector compatible) |
| **Pin 5** | `H3` / `Z+` | `D5` | **PB10** | `TIM2_CH3` | `GPIO_AF1_TIM2` | Internal pullup enabled (open-collector compatible) |

```
           +---------------------------------------------+
           |           X-NUCLEO-IHM07M1 Shield           |
           |                                             |
           |   Connector J3                              |
           |   [Pin 1: +5V  ] ------------ Sensor VDD   |
           |   [Pin 2: GND  ] ------------ Sensor GND   |
           |   [Pin 3: H1/A+] --(PA15)--> TIM2_CH1 -+   |
           |   [Pin 4: H2/B+] --(PB3 )--> TIM2_CH2 --+-> [ 3-Input XOR Gate ]
           |   [Pin 5: H3/Z+] --(PB10)--> TIM2_CH3 -+         (TI1S)
           +---------------------------------------------+       |
                                                                 v
                                                            [ TI1F_ED ]
                                                            Filter & Edge
                                                                 |
                                       +-------------------------+-------------------------+
                                       |                                                   |
                                       v (Trigger)                                         v (Reset)
                               +----------------+                                  +----------------+
                               |  TIM2 Capture  |                                  |   TIM2 Counter |
                               |   CCR1 (32-bit)|                                  |   CNT (32-bit) |
                               |   Elapsed dt   |                                  |   Reset to 0   |
                               +----------------+                                  +----------------+
                                       |
                                       v
                                [ TIM2_IRQHandler ]
                                       |
                           Motor_HallTransitionISR()
                           - Instantaneous Hall RPM
                           - Commutation Angle Reference
```

---

## 3. STM32 Timer Hall Sensor Mode Theory & Operation

### 3.1 Hardware XOR Gate & Slave Reset Trigger
Per STM32 reference manual (RM0440 Section 29.3.26 "Hall sensor interface"):
1. **XOR Input Combination (`TIM_CR2_TI1S = 1`)**:
   The inputs `TIM2_CH1`, `TIM2_CH2`, and `TIM2_CH3` are connected to an internal 3-input XOR gate.
2. **Filtered Edge Detector (`TI1F_ED`)**:
   Every transition (rising or falling edge) on any of the three Hall lines changes the parity of the XOR output, generating a pulse on `TI1F_ED`.
   A hardware digital filter of $1.5\,\mu\text{s}$ (`IC1Filter = 0x0F`) suppresses high-frequency motor inverter switching noise.
3. **Hardware Counter Reset (`TIM_SLAVEMODE_RESET`)**:
   On each transition edge, the slave controller latches `CNT` into `CCR1` and resets `CNT` to zero simultaneously in silicon.
4. **Capture/Compare Interrupt (`TIM_IT_CC1`)**:
   An interrupt is fired on each transition edge, providing the software with:
   - Exact interval $\Delta t_{\mu\text{s}}$ between consecutive $60^\circ$ electrical steps in `TIM2->CCR1`.
   - Current 3-bit Hall state $(H_3, H_2, H_1)$ read from the GPIO pins via `IHM07M1_ReadHallSensors()`.

### 3.2 Speed Calculation Formula
For a BLDC motor with $P$ pole pairs ($P = 7$ for the 5010 motor):
- Each electrical revolution contains 6 Hall transitions ($60^\circ$ electrical each).
- Each mechanical revolution contains $6 \times P$ transitions.
- At an interval of $\Delta t_{\mu\text{s}}$ between transitions:
  $$\text{RPM}_{\text{hall}} = \frac{60 \times 10^6}{6 \times P \times \Delta t_{\mu\text{s}}} = \frac{10^7}{P \times \Delta t_{\mu\text{s}}}$$
- At $1.0\,\mu\text{s}$ resolution and 32-bit counter depth ($4.29 \times 10^9\,\mu\text{s}$), the dynamic range extends down to $0.0003\text{ RPM}$ with zero timer rollover risk.

### 3.3 Electrical Angle & Sector Mapping ($120^\circ$ Placement)

| Hall State $(H_3 H_2 H_1)$ | Decimal Value | Electrical Sector | Nominal Electrical Angle |
| :---: | :---: | :---: | :---: |
| `101` | **5** | Sector 1 | $30^\circ$ ($0.524\text{ rad}$) |
| `100` | **4** | Sector 2 | $90^\circ$ ($1.571\text{ rad}$) |
| `110` | **6** | Sector 3 | $150^\circ$ ($2.618\text{ rad}$) |
| `010` | **2** | Sector 4 | $210^\circ$ ($3.665\text{ rad}$) |
| `011` | **3** | Sector 5 | $270^\circ$ ($4.712\text{ rad}$) |
| `001` | **1** | Sector 6 | $330^\circ$ ($5.760\text{ rad}$) |
| `000` / `111` | 0 / 7 | Invalid | Open cable / sensor fault |

---

## 4. Firmware Implementation

### 4.1 Timer & GPIO Initialization (`src/ihm07m1.c`)
```c
/* PA15, PB3, PB10 in Alternate Function AF1 (TIM2 Channels 1, 2, 3) */
GPIO_InitStruct.Pin = IHM07M1_HALL1_PIN | IHM07M1_HALL2_PIN | IHM07M1_HALL3_PIN;
GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
GPIO_InitStruct.Pull = GPIO_PULLUP;
GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
GPIO_InitStruct.Alternate = GPIO_AF1_TIM2;
HAL_GPIO_Init(...);

/* TIM2 Base: 1 MHz tick (1.0 us), 32-bit period */
htim2.Instance = TIM2;
htim2.Init.Prescaler = 169;
htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
htim2.Init.Period = 0xFFFFFFFFU;

TIM_HallSensor_InitTypeDef sHallConfig = {0};
sHallConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
sHallConfig.IC1Prescaler = TIM_ICPSC_DIV1;
sHallConfig.IC1Filter = 0x0F; /* 1.5 us digital debounce filter */
sHallConfig.Commutation_Delay = 0;
HAL_TIMEx_HallSensor_Init(&htim2, &sHallConfig);

HAL_NVIC_SetPriority(TIM2_IRQn, 2, 0);
HAL_NVIC_EnableIRQ(TIM2_IRQn);
HAL_TIMEx_HallSensor_Start_IT(&htim2);
```

### 4.2 Hall Transition ISR Callback (`src/motor_control.c`)
```c
void Motor_HallTransitionISR(uint8_t hall_state, uint32_t dt_us) {
    g_telemetry.hall_state = hall_state;
    if (hall_state >= 1 && hall_state <= 6) {
        g_telemetry.hall_valid = true;
        s_hall_transition_count++;
        g_telemetry.hall_transition_count = s_hall_transition_count;
        s_last_hall_tick_ms = HAL_GetTick();

        if (dt_us >= 50 && dt_us <= 1000000) {
            uint32_t p = g_motor_ctrl.pole_pairs ? g_motor_ctrl.pole_pairs : 7;
            float rpm = 10000000.0f / ((float)p * (float)dt_us);
            if (rpm <= 30000.0f) {
                s_hall_rpm = (s_hall_rpm * 0.7f) + (rpm * 0.3f);
                g_telemetry.hall_rpm = s_hall_rpm;
            }
        }
    } else {
        g_telemetry.hall_valid = false;
    }
}
```

---

## 5. Backward Compatibility & Test Verification

1. **Disconnected / Sensorless Motor State**:
   - Connector J3 is currently unpopulated on the bench.
   - Internal pullups hold `PA15`, `PB3`, `PB10` at logic HIGH (`hall_state = 7`).
   - No XOR edge transitions occur; zero CPU overhead.
   - Live dashboard row 13 displays:
     ```text
     Hall Sensor Signals (H321) : [SENSORLESS MOTOR / J4 DISCONNECTED]
     ```
2. **Optical Tachometer Verification Post-Flash**:
   - Executed spin test to 864 RPM.
   - Optical tachometer on `PC6` (`TIM3`+`TIM4`) counted revolutions and tracked real-time speed (`[ROTATING - OK]`) with zero interference from `TIM2`.
3. **Readiness for Sensored Evaluation**:
   - Once the sensored motor is connected to J3, `TIM2_IRQHandler` will immediately begin capturing Hall transitions and reporting live RPM and electrical angle.
