#include "msp.h"
#include <stdio.h>
#include "lcd.h" // Include your LCD driver header file
#include <stdlib.h>
#include <math.h>

#define DATA_SIZE 5              // Size of data array
#define ZERO_G_OFFSET 2615       // Zero-g value (calibrated for ADXL335 on MSP432 ADC)
#define SENSITIVITY 68078.5      // Sensitivity in ADC units per g
#define RADIUS_INCHES 3.0        // Radius of microwave plate in inches
#define RADIUS_METERS (RADIUS_INCHES * 0.0254) // Convert radius to meters
#define G_TO_M_S2 9.81           // Conversion factor from g to m/s^2

float tangential_accel = 0;

typedef struct {
    uint32_t x_axis[DATA_SIZE]; // ADC readings for X-axis (tangential acceleration)
} ADXL335_t;

ADXL335_t sensor;
uint8_t index = 0;

// Function Prototypes
void calibrateSensor(void);
float convertToAcceleration(uint32_t raw_value, uint16_t zero_g_offset);
float calculateAngularSpeed(float tangential_acceleration, float radius);
void displayAngularSpeed(float angular_speed);
void ADC_Init(void);
float Read_Temperature(void);
void Display_Temperature(float TempF);

// Convert ADC reading to acceleration in m/s^2
float convertToAcceleration(uint32_t raw_value, uint16_t zero_g_offset) {
    return ((raw_value - zero_g_offset) / SENSITIVITY) * G_TO_M_S2;
}

// Calculate angular speed in rad/s
float calculateAngularSpeed(float tangential_acceleration, float radius) {
    if (tangential_acceleration <= 0.0) {
        tangential_acceleration = 0.01; // Avoid division by zero
    }
    return sqrt((tangential_acceleration) / radius);
}

// Display angular speed on the LCD
void displayAngularSpeed(float angular_speed) {
    char speedString[16];
    sprintf(speedString, "Vel: %.2f rad/m", angular_speed);
    lcdSetText(speedString, 0, 0); // Display on row 0
}

// Calibrate sensor by averaging DATA_SIZE readings
void calibrateSensor(void) {
    float sum = 0.0;
    int i;

    for (i = 0; i < DATA_SIZE; i++) {
        ADC14->CTL0 |= ADC14_CTL0_ENC | ADC14_CTL0_SC; // Start ADC conversion
        while (!(ADC14->IFGR0 & ADC14_IFGR0_IFG0));    // Wait for conversion
        sensor.x_axis[i] = ADC14->MEM[0];
        sum += convertToAcceleration(sensor.x_axis[i], ZERO_G_OFFSET);
        __delay_cycles(100000); // Short delay between readings
    }

    tangential_accel = sum / DATA_SIZE; // Compute average resting acceleration
}

// ADC Initialization
void ADC_Init(void) {
    P6->SEL0 |= (BIT0 | BIT1); // P6.0 -> A15 (velocity), P6.1 -> A14 (temperature)
    P6->SEL1 |= (BIT0 | BIT1);

    ADC14->CTL0 = ADC14_CTL0_SHP | ADC14_CTL0_CONSEQ_1 | ADC14_CTL0_MSC | ADC14_CTL0_ON;
    ADC14->CTL1 = ADC14_CTL1_RES__14BIT; // 14-bit resolution
    ADC14->MCTL[0] = ADC14_MCTLN_INCH_15; // Velocity input (A15)
    ADC14->MCTL[1] = ADC14_MCTLN_INCH_14; // Temperature input (A14)
    ADC14->MCTL[1] |= ADC14_MCTLN_EOS;   // End of sequence
    ADC14->CTL0 |= ADC14_CTL0_ENC;
}

// Read temperature from AD22100
float Read_Temperature(void) {
    ADC14->CTL0 |= ADC14_CTL0_SC; // Start ADC conversion
    while (!(ADC14->IFGR0 & ADC14_IFGR0_IFG1)); // Wait for temperature data (A14)
    uint16_t adcValue = ADC14->MEM[1]; // Read ADC value from MEM[1]
    float voltage = (adcValue * 3.3) / 16384.0; // Convert ADC value to voltage
    float temperatureC = (voltage - 1.375) / 0.0225; // Corrected conversion to °C
    float TempF = (temperatureC * 1.8) + 32; // Convert °C to °F
    return TempF;
}

// Display temperature on the LCD
void Display_Temperature(float TempF) {
    char buffer[16];
    sprintf(buffer, "Temp: %.2f F", TempF);
    lcdSetText(buffer, 0, 1); // Display on row 1

}

void main(void) {
    WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD; // Stop watchdog timer

    lcdInit();  // Initialize LCD
    ADC_Init(); // Initialize ADC
    calibrateSensor(); // Calibrate sensor

    while (1) {
        int i;
        // Calculate and display velocity
        ADC14->CTL0 |= ADC14_CTL0_SC; // Start ADC conversion
        while (!(ADC14->IFGR0 & ADC14_IFGR0_IFG0)); // Wait for velocity data (A15)
        sensor.x_axis[index++] = ADC14->MEM[0];

        if (index >= DATA_SIZE) { // When buffer is full
            float avg_tangential_accel = 0.0;
            for (i = 0; i < DATA_SIZE; i++) {
                avg_tangential_accel += convertToAcceleration(sensor.x_axis[i], ZERO_G_OFFSET);
            }
            avg_tangential_accel /= DATA_SIZE;
            float net_acceleration = avg_tangential_accel - tangential_accel;

            if (net_acceleration < 0.0) {
                net_acceleration = 0.1;
            }

            float angular_speed = calculateAngularSpeed(net_acceleration, RADIUS_METERS);
            displayAngularSpeed(angular_speed); // Display velocity

            index = 0; // Reset index
        }

        // Read and display temperature
        float TempF = Read_Temperature();
        Display_Temperature(TempF);

        __delay_cycles(3000000); // Delay for readability
    }
}